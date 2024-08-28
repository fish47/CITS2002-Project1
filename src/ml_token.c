#include "ml_token.h"

#include <stdio.h>
#include <stdlib.h>

#define DEFAULT_READ_BUFFER_SIZE    1024
#define DEFAULT_TOKEN_BUFFER_SIZE   64

static int io_cb_read(void *opaque, char *buffer, int capacity);
static void io_cb_close(void *opaque);

enum token_flag {
    TOKEN_FLAG_CR = 1,
    TOKEN_FLAG_LF = 1 << 1,
    TOKEN_FLAG_SPACE = 1 << 2,
};

struct ml_token_ctx {
    const struct ml_token_io_fns *io_fns;
    void *io_opaque;

    char *read_buffer;
    int read_idx;
    int read_count;
    int read_capacity;

    char *token_buffer;
    int token_idx;
    int token_capacity;
    int token_flags;

    bool stop_reading;
};

static const struct ml_token_io_fns ml_token_io_fns_file = {
    .read = io_cb_read,
    .close = io_cb_close,
};

bool ml_token_ctx_init_file(struct ml_token_ctx **pp, const char *path) {
    FILE *file = fopen(path, "rb");
    return file && ml_token_ctx_init_fns(pp, &ml_token_io_fns_file, file,
                                         DEFAULT_READ_BUFFER_SIZE,
                                         DEFAULT_TOKEN_BUFFER_SIZE);
}

bool ml_token_ctx_init_fns(struct ml_token_ctx **pp, const struct ml_token_io_fns *fns,
                           void *opaque, int read_capacity, int token_capacity) {
    char *read_buffer = NULL;
    char *token_buffer = NULL;
    struct ml_token_ctx *ctx = NULL;

    ctx = malloc(sizeof(struct ml_token_ctx));
    if (!ctx)
        goto fail;

    read_buffer = malloc(read_capacity);
    if (!read_buffer)
        goto fail;

    token_buffer = malloc(token_capacity);
    if (!token_buffer)
        goto fail;

    *ctx = (struct ml_token_ctx) {
        .io_fns = fns,
        .io_opaque = opaque,
        .read_buffer = read_buffer,
        .read_idx = 0,
        .read_count = 0,
        .read_capacity = read_capacity,
        .token_buffer = token_buffer,
        .token_idx = 0,
        .token_capacity = token_capacity,
        .token_flags = 0,
        .stop_reading = false,
    };
    *pp = ctx;
    return true;

fail:
    if (opaque)
        fns->close(opaque);
    if (read_buffer)
        free(read_buffer);
    if (token_buffer)
        free(token_buffer);
    if (ctx)
        free(ctx);
    return false;
}

void ml_token_ctx_uninit(struct ml_token_ctx **pp) {
    struct ml_token_ctx *ctx = pp ? *pp : NULL;
    if (!ctx)
        return;

    if (ctx->io_opaque)
        ctx->io_fns->close(ctx->io_opaque);
    if (ctx->read_buffer)
        free(ctx->read_buffer);
    if (ctx->token_buffer)
        free(ctx->token_buffer);
    free(ctx);
    *pp = NULL;
}

static int io_cb_read(void *opaque, char *buffer, int capacity) {
    FILE *f = opaque;
    return fread(buffer, 1, capacity, f);
}

static void io_cb_close(void *opaque) {
    FILE *f = opaque;
    fclose(f);
}

static enum ml_token_type finish_token(struct ml_token_ctx *ctx,
                                       const char **buf, int *len,
                                       enum ml_token_type *hint) {
    enum ml_token_type found = hint ? *hint : ML_TOKEN_TYPE_ERROR;
    if (ctx->token_flags & (TOKEN_FLAG_CR | TOKEN_FLAG_LF))
        found = ML_TOKEN_TYPE_LINE_TERMINATOR;
    else if (ctx->token_flags & TOKEN_FLAG_SPACE)
        found = ML_TOKEN_TYPE_SPACE;

    if (found != ML_TOKEN_TYPE_ERROR)
        ctx->token_buffer[ctx->token_idx] = 0;

    if (buf)
        *buf = ctx->token_buffer;
    if (len)
        *len = ctx->token_idx;

    ctx->token_idx = 0;
    ctx->token_flags = 0;
    return found;
}

static void expand_token(struct ml_token_ctx *ctx) {
    // it should be large enough with a zero terminator
    if (ctx->token_idx + 2 >= ctx->token_capacity) {
        int new_capacity = ctx->token_capacity << 1;
        char *new_buffer = realloc(ctx->token_buffer, new_capacity);
        ctx->token_buffer = new_buffer;
        ctx->token_capacity = new_capacity;
    }
    ctx->token_buffer[ctx->token_idx] = ctx->read_buffer[ctx->read_idx];
    ctx->token_idx++;
    ctx->read_idx++;
}

static bool check_pending_token(struct ml_token_ctx *ctx, int flags) {
    // empty token always matches any flags, as it can be anything later
    return !ctx->token_idx || (ctx->token_flags & flags);
}

static enum ml_token_type flush_token(struct ml_token_ctx *ctx,
                                      const char **buf, int *len,
                                      enum ml_token_type type) {
    if (ctx->token_idx)
        return finish_token(ctx, buf, len, NULL);

    expand_token(ctx);
    return finish_token(ctx, buf, len, &type);
}

enum ml_token_type ml_token_iterate(struct ml_token_ctx *ctx, const char **buf, int *len) {
    if (ctx->stop_reading)
        return ML_TOKEN_TYPE_EOF;

    while (true) {
        // fill read buffer if it is empty
        if (ctx->read_idx >= ctx->read_count) {
            int n = ctx->io_fns->read(ctx->io_opaque, ctx->read_buffer, ctx->read_capacity);
            if (n <= 0) {
                // block reading from the stream hereafter
                ctx->stop_reading = true;

                // flush the pending token
                if (ctx->token_idx)
                    return finish_token(ctx, buf, len, NULL);
                return ML_TOKEN_TYPE_EOF;
            }
            ctx->read_count = n;
        }

        while (ctx->read_idx < ctx->read_count) {
            char c = ctx->read_buffer[ctx->read_idx];
            if (c == '\r') {
                // cannot be merged with other characters except CRLF
                if (!check_pending_token(ctx, TOKEN_FLAG_CR | TOKEN_FLAG_LF))
                    return finish_token(ctx, buf, len, NULL);

                // if there are successive CR characters, each one should be a line terminator
                if (ctx->token_flags & TOKEN_FLAG_CR)
                    return finish_token(ctx, buf, len, NULL);

                ctx->token_flags |= TOKEN_FLAG_CR;
                expand_token(ctx);
            } else if (c == '\n') {
                if (!check_pending_token(ctx, TOKEN_FLAG_CR | TOKEN_FLAG_LF))
                    return finish_token(ctx, buf, len, NULL);

                // may be the Unix style (LF) or the Windows style (CRLF)
                ctx->token_flags |= TOKEN_FLAG_LF;
                expand_token(ctx);
                return finish_token(ctx, buf, len, NULL);
            } else if (c == ' ') {
                if (!check_pending_token(ctx, TOKEN_FLAG_SPACE))
                    return finish_token(ctx, buf, len, NULL);

                // merge successive spaces into one
                if (ctx->token_idx) {
                    ctx->read_idx++;
                } else {
                    expand_token(ctx);
                    ctx->token_flags |= TOKEN_FLAG_SPACE;
                }
            } else if (c == '\t') {
                return flush_token(ctx, buf, len, ML_TOKEN_TYPE_TAB);
            } else if (c == '+') {
                return flush_token(ctx, buf, len, ML_TOKEN_TYPE_PLUS);
            } else if (c == '-') {
                return flush_token(ctx, buf, len, ML_TOKEN_TYPE_MINUS);
            } else if (c == '*') {
                return flush_token(ctx, buf, len, ML_TOKEN_TYPE_MULTIPLY);
            } else if (c == '/') {
                return flush_token(ctx, buf, len, ML_TOKEN_TYPE_DIVIDE);
            } else if (c == '(') {
                return flush_token(ctx, buf, len, ML_TOKEN_TYPE_PARENTHESIS_L);
            } else if (c == ')') {
                return flush_token(ctx, buf, len, ML_TOKEN_TYPE_PARENTHESIS_R);
            } else {
                if (ctx->token_idx)
                    return finish_token(ctx, buf, len, NULL);

                ctx->read_idx++;
            }
        }
    }
}

#include "ml_token.h"
#include "ml_memory.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>

#define DEFAULT_READ_BUFFER_SIZE    1024
#define DEFAULT_TOKEN_BUFFER_SIZE   64

#define ML_KEYWORD_ARGUMENT     "arg"
#define ML_KEYWORD_PRINT        "print"
#define ML_KEYWORD_RETURN       "return"
#define ML_KEYWORD_FUNCTION     "function"

static int io_cb_read(void *opaque, char *buffer, int capacity);
static void io_cb_close(void *opaque);

enum token_flag {
    // trait flags
    TOKEN_FLAG_CR = 1,
    TOKEN_FLAG_LF = 1 << 1,
    TOKEN_FLAG_SPACE = 1 << 2,
    TOKEN_FLAG_DOT = 1 << 3,
    TOKEN_FLAG_NUMBER = 1 << 4,
    TOKEN_FLAG_ALPHABET = 1 << 5,
    TOKEN_FLAG_LESS_THAN = 1 << 6,
    TOKEN_FLAG_ARGUMENT = 1 << 7,
    TOKEN_FLAG_TRAIT_MASK = (1 << 8) - 1,

    // control flags
    TOKEN_FLAG_SKIP_LINE = 1 << 10,
    TOKEN_FLAG_STOP_READING = 1 << 11,
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
    uint32_t token_flags;
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

    ctx = ml_memory_malloc(sizeof(struct ml_token_ctx));
    if (!ctx)
        goto fail;

    read_buffer = ml_memory_malloc(read_capacity);
    if (!read_buffer)
        goto fail;

    token_buffer = ml_memory_malloc(token_capacity);
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
    };
    *pp = ctx;
    return true;

fail:
    if (opaque)
        fns->close(opaque);
    if (read_buffer)
        ml_memory_free(read_buffer);
    if (token_buffer)
        ml_memory_free(token_buffer);
    if (ctx)
        ml_memory_free(ctx);
    return false;
}

void ml_token_ctx_uninit(struct ml_token_ctx **pp) {
    struct ml_token_ctx *ctx = pp ? *pp : NULL;
    if (!ctx)
        return;

    if (ctx->io_opaque)
        ctx->io_fns->close(ctx->io_opaque);
    if (ctx->read_buffer)
        ml_memory_free(ctx->read_buffer);
    if (ctx->token_buffer)
        ml_memory_free(ctx->token_buffer);
    ml_memory_free(ctx);
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

static void clear_token(struct ml_token_ctx *ctx) {
    ctx->token_idx = 0;
    ctx->token_flags &= ~TOKEN_FLAG_TRAIT_MASK;
}

static enum ml_token_type raise_error(struct ml_token_ctx *ctx, struct ml_token_result *result) {
    if (result) {
        result->buf = NULL;
        result->len = 0;
    }
    clear_token(ctx);
    ctx->read_idx++;
    ctx->token_flags |= TOKEN_FLAG_SKIP_LINE;
    return ML_TOKEN_TYPE_ERROR;
}

static bool is_keyword_matched(struct ml_token_ctx *ctx, size_t n, const char *str) {
    return ctx->token_idx == n && strncmp(ctx->token_buffer, str, n) == 0;
}

static enum ml_token_type resolve_name_token(struct ml_token_ctx *ctx) {
    if (is_keyword_matched(ctx, sizeof(ML_KEYWORD_PRINT) - 1, ML_KEYWORD_PRINT))
        return ML_TOKEN_TYPE_PRINT;
    else if (is_keyword_matched(ctx, sizeof(ML_KEYWORD_RETURN) - 1, ML_KEYWORD_RETURN))
        return ML_TOKEN_TYPE_RETURN;
    else if (is_keyword_matched(ctx, sizeof(ML_KEYWORD_FUNCTION) - 1, ML_KEYWORD_FUNCTION))
        return ML_TOKEN_TYPE_FUNCTION;
    else
        return ML_TOKEN_TYPE_NAME;
}

static enum ml_token_type resolve_number_token(struct ml_token_ctx *ctx,
                                               struct ml_token_result *result) {
    float value = strtod(ctx->token_buffer, NULL);
    if (errno)
        return ML_TOKEN_TYPE_ERROR;

    result->value.real = value;
    return ML_TOKEN_TYPE_NUMBER;
}

static enum ml_token_type resolve_argument_token(struct ml_token_ctx *ctx,
                                                 struct ml_token_result *result) {
    // an argument should consist of alphabets and numbers
    uint32_t traits = ctx->token_flags & TOKEN_FLAG_TRAIT_MASK;
    if (traits & ~(TOKEN_FLAG_ALPHABET | TOKEN_FLAG_NUMBER | TOKEN_FLAG_ARGUMENT))
        return ML_TOKEN_TYPE_ERROR;

    // parse the argument index
    int num_offset = sizeof(ML_KEYWORD_ARGUMENT) - 1;
    const char *num_str = ctx->token_buffer + num_offset;
    int index = strtoimax(num_str, NULL, 10);
    if (errno)
        return ML_TOKEN_TYPE_ERROR;

    // numbers with leading zeros are invalid
    if (index < 10 && num_offset + 1 != ctx->token_idx)
        return ML_TOKEN_TYPE_ERROR;

    result->value.index = index;
    return ML_TOKEN_TYPE_ARGUMENT;
}

static enum ml_token_type finish_token(struct ml_token_ctx *ctx,
                                       struct ml_token_result *result,
                                       enum ml_token_type *hint) {
    // the token is zero-terminated string now
    ctx->token_buffer[ctx->token_idx] = 0;

    enum ml_token_type found = hint ? *hint : ML_TOKEN_TYPE_ERROR;
    if (ctx->token_flags & (TOKEN_FLAG_CR | TOKEN_FLAG_LF)) {
        ctx->token_flags &= ~TOKEN_FLAG_SKIP_LINE;
        found = ML_TOKEN_TYPE_LINE_TERMINATOR;
    } else if (ctx->token_flags & TOKEN_FLAG_SPACE) {
        found = ML_TOKEN_TYPE_SPACE;
    } else if (ctx->token_flags & TOKEN_FLAG_ARGUMENT) {
        found = resolve_argument_token(ctx, result);
    } else if (ctx->token_flags & TOKEN_FLAG_NUMBER) {
        found = resolve_number_token(ctx, result);
    } else if (ctx->token_flags & TOKEN_FLAG_ALPHABET) {
        found = resolve_name_token(ctx);
    }

    if (found == ML_TOKEN_TYPE_ERROR)
        return raise_error(ctx, result);

    if (result) {
        result->buf = ctx->token_buffer;
        result->len = ctx->token_idx;
    }

    clear_token(ctx);
    return found;
}

static void expand_token(struct ml_token_ctx *ctx) {
    // it should be large enough with a zero terminator
    if (ctx->token_idx + 2 >= ctx->token_capacity) {
        int new_capacity = ctx->token_capacity << 1;
        char *new_buffer = ml_memory_realloc(ctx->token_buffer, new_capacity);
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
                                      struct ml_token_result *result,
                                      enum ml_token_type type) {
    if (ctx->token_idx)
        return finish_token(ctx, result, NULL);

    expand_token(ctx);
    return finish_token(ctx, result, &type);
}

enum ml_token_type ml_token_iterate(struct ml_token_ctx *ctx, struct ml_token_result *result) {
    while (true) {
        // fill read buffer if it is empty
        if (ctx->read_idx >= ctx->read_count) {
            // the last chunk of data has been read
            if (ctx->token_flags & TOKEN_FLAG_STOP_READING) {
                // there may be one pending token
                if (ctx->token_idx)
                    return finish_token(ctx, result, NULL);
                return ML_TOKEN_TYPE_EOF;
            }

            // block reading if it is the last chunk of data
            int n = ctx->io_fns->read(ctx->io_opaque, ctx->read_buffer, ctx->read_capacity);
            if (n <= 0)
                ctx->token_flags |= TOKEN_FLAG_STOP_READING;

            ctx->read_idx = 0;
            ctx->read_count = n;
        }

        while (ctx->read_idx < ctx->read_count) {
            char c = ctx->read_buffer[ctx->read_idx];
            if (c == '\r') {
                // cannot be merged with other characters except CRLF
                if (!check_pending_token(ctx, TOKEN_FLAG_CR | TOKEN_FLAG_LF))
                    return finish_token(ctx, result, NULL);

                // if there are successive CR characters, each one should be a line terminator
                if (ctx->token_flags & TOKEN_FLAG_CR)
                    return finish_token(ctx, result, NULL);

                ctx->token_flags |= TOKEN_FLAG_CR;
                expand_token(ctx);
            } else if (c == '\n') {
                if (!check_pending_token(ctx, TOKEN_FLAG_CR | TOKEN_FLAG_LF))
                    return finish_token(ctx, result, NULL);

                // may be the Unix style (LF) or the Windows style (CRLF)
                ctx->token_flags |= TOKEN_FLAG_LF;
                expand_token(ctx);
                return finish_token(ctx, result, NULL);
            } else if (ctx->token_flags & TOKEN_FLAG_SKIP_LINE) {
                // ignore any characters until reaching a line terminator
                ctx->read_idx++;
            } else if (c == '#') {
                // skip this line after returning a comment token
                enum ml_token_type type = flush_token(ctx, result, ML_TOKEN_TYPE_COMMENT);
                if (type == ML_TOKEN_TYPE_COMMENT)
                    ctx->token_flags |= TOKEN_FLAG_SKIP_LINE;
                return type;
            } else if (c == ' ') {
                if (!check_pending_token(ctx, TOKEN_FLAG_SPACE))
                    return finish_token(ctx, result, NULL);

                // merge successive spaces into one
                if (ctx->token_idx) {
                    ctx->read_idx++;
                } else {
                    expand_token(ctx);
                    ctx->token_flags |= TOKEN_FLAG_SPACE;
                }
            } else if ('0' <= c && c <= '9') {
                if (ctx->token_flags & TOKEN_FLAG_ARGUMENT) {
                    // has more index digits
                } else if (ctx->token_flags & TOKEN_FLAG_ALPHABET) {
                    // check if the accumulated token has the argument prefix
                    if (is_keyword_matched(ctx, sizeof(ML_KEYWORD_ARGUMENT) - 1, ML_KEYWORD_ARGUMENT))
                        ctx->token_flags |= TOKEN_FLAG_ARGUMENT;
                    else
                        return raise_error(ctx, result);
                }

                if (!check_pending_token(ctx, (TOKEN_FLAG_NUMBER | TOKEN_FLAG_DOT | TOKEN_FLAG_ARGUMENT)))
                    return finish_token(ctx, result, NULL);

                expand_token(ctx);
                ctx->token_flags |= TOKEN_FLAG_NUMBER;
            } else if (c == '.') {
                if (ctx->token_flags & TOKEN_FLAG_ALPHABET)
                    return raise_error(ctx, result);

                if (!check_pending_token(ctx, (TOKEN_FLAG_NUMBER | TOKEN_FLAG_DOT)))
                    return finish_token(ctx, result, NULL);

                // a redundant dot
                if (ctx->token_flags & TOKEN_FLAG_DOT)
                    return raise_error(ctx, result);

                expand_token(ctx);
                ctx->token_flags |= TOKEN_FLAG_DOT;
            } else if ('a' <= c && c <= 'z') {
                // identifiers only consist of alphabets
                if (ctx->token_flags & (TOKEN_FLAG_NUMBER | TOKEN_FLAG_DOT))
                    return raise_error(ctx, result);

                if (!check_pending_token(ctx, TOKEN_FLAG_ALPHABET))
                    return finish_token(ctx, result, NULL);

                expand_token(ctx);
                ctx->token_flags |= TOKEN_FLAG_ALPHABET;
            } else if (c == '<') {
                if (ctx->token_flags & TOKEN_FLAG_LESS_THAN)
                    return raise_error(ctx, result);
                else if (ctx->token_idx)
                    return finish_token(ctx, result, NULL);

                expand_token(ctx);
                ctx->token_flags |= TOKEN_FLAG_LESS_THAN;
            } else if (c == '\t') {
                return flush_token(ctx, result, ML_TOKEN_TYPE_TAB);
            } else if (c == '+') {
                return flush_token(ctx, result, ML_TOKEN_TYPE_PLUS);
            } else if (c == '-') {
                if (ctx->token_flags & TOKEN_FLAG_LESS_THAN) {
                    expand_token(ctx);
                    return finish_token(ctx, result, &(enum ml_token_type) {ML_TOKEN_TYPE_ASSIGNMENT});
                }
                return flush_token(ctx, result, ML_TOKEN_TYPE_MINUS);
            } else if (c == '*') {
                return flush_token(ctx, result, ML_TOKEN_TYPE_MULTIPLY);
            } else if (c == '/') {
                return flush_token(ctx, result, ML_TOKEN_TYPE_DIVIDE);
            } else if (c == '(') {
                return flush_token(ctx, result, ML_TOKEN_TYPE_PARENTHESIS_L);
            } else if (c == ')') {
                return flush_token(ctx, result, ML_TOKEN_TYPE_PARENTHESIS_R);
            } else {
                if (ctx->token_idx)
                    return raise_error(ctx, result);

                ctx->read_idx++;
            }
        }
    }
}

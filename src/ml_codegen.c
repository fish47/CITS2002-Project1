#include "ml_codegen.h"
#include "ml_token.h"
#include "ml_compile.h"
#include "ml_memory.h"

#include <stdio.h>
#include <string.h>

#define ML_CODEGEN_BUFFER_CAPACITY_WRITE    4096
#define ML_CODEGEN_BUFFER_CAPACITY_NUM      64
#define ML_CODEGEN_SECTION_COMMENT_WIDTH    80

static int cb_codegen_write(void *opaque, char *buffer, int count);
static void cb_codegen_close(void *opaque);

struct codegen_ctx {
    char *buffer;
    int offset;
    int capacity;
    void *opaque;
    const struct ml_codegen_io_fns *fns;
};

static const struct ml_codegen_io_fns ml_codegen_io_fns_file = {
    .write = cb_codegen_write,
    .close = cb_codegen_close,
};

static int cb_codegen_write(void *opaque, char *buffer, int count) {
    FILE *file = opaque;
    return fwrite(buffer, 1, count, file);
}

static void cb_codegen_close(void *opaque) {
    FILE *file = opaque;
    if (file)
        fclose(file);
}

static void do_write_flush(struct codegen_ctx *ctx) {
    if (ctx->offset)
        ctx->fns->write(ctx->opaque, ctx->buffer, ctx->offset);
    ctx->offset = 0;
}

static void do_write_char(struct codegen_ctx *ctx, char c) {
    ctx->buffer[ctx->offset++] = c;
    if (ctx->offset == ctx->capacity)
        do_write_flush(ctx);
}

static void do_write_str(struct codegen_ctx *ctx, const char *s) {
    int idx = 0;
    int count = strlen(s);
    while (idx < count) {
        bool flush = false;
        int capacity = ctx->capacity - ctx->offset;
        int chunk = count - idx;
        if (chunk >= capacity) {
            flush = true;
            chunk = capacity;
        }

        memcpy(ctx->buffer + ctx->offset, s + idx, chunk);
        idx += chunk;
        ctx->offset += chunk;
        if (flush)
            do_write_flush(ctx);
    }
}

static void do_write_newline(struct codegen_ctx *ctx) {
    do_write_char(ctx, '\n');
}

static void do_write_indent(struct codegen_ctx *ctx) {
    do_write_str(ctx, "    ");
}

static void do_write_line(struct codegen_ctx *ctx, const char *s) {
    do_write_str(ctx, s);
    do_write_newline(ctx);
}

static void do_write_line_indent(struct codegen_ctx *ctx, const char *s) {
    do_write_indent(ctx);
    do_write_line(ctx, s);
}

static void do_write_comment_tag(struct codegen_ctx *ctx, const char *name) {
    do_write_str(ctx, "// ");

    int count = name ? strlen(name) : 0;
    int spaced = count ? (count + 2) : 0;
    if (spaced + 2 > ML_CODEGEN_SECTION_COMMENT_WIDTH) {
        do_write_str(ctx, name);
    } else {
        int offset = 0;
        char buf[ML_CODEGEN_SECTION_COMMENT_WIDTH + 1];

        int left = (ML_CODEGEN_SECTION_COMMENT_WIDTH - spaced) / 2;
        int right = ML_CODEGEN_SECTION_COMMENT_WIDTH - spaced - left;

        if (left > 0) {
            memset(buf + offset, '=', left);
            offset += left;
        }

        // center the tag and add 2 spaces for padding
        if (count) {
            buf[offset++] = ' ';
            memcpy(buf + offset, name, count);
            offset += count;
            buf[offset++] = ' ';
        }

        if (right > 0) {
            memset(buf + offset, '=', right);
            offset += right;
        }

        buf[ML_CODEGEN_SECTION_COMMENT_WIDTH] = 0;
        do_write_str(ctx, buf);
    }

    do_write_newline(ctx);
}

static void do_write_framework(struct codegen_ctx *ctx) {
    do_write_line(ctx, "#include <stdio.h>");
    do_write_line(ctx, "#include <stdlib.h>");
    do_write_line(ctx, "#include <math.h>");
    do_write_newline(ctx);
    do_write_newline(ctx);

    do_write_comment_tag(ctx, "framework");
    do_write_line(ctx, "static void ml_print(double ml_val) {");
    do_write_line_indent(ctx, "double ml_int = 0;");
    do_write_line_indent(ctx, "double ml_frac = modf(ml_val, &ml_int);");
    do_write_line_indent(ctx, "const char *ml_fmt = (ml_frac == 0) ? \"%.0f\\n\" : \"%.6f\\n\";");
    do_write_line_indent(ctx, "printf(ml_fmt, ml_val);");
    do_write_line(ctx, "}");
    do_write_newline(ctx);

    do_write_line(ctx, "static double ml_parse_arg(int ml_i, char **ml_argv, int ml_argc) {");
    do_write_line_indent(ctx, "return (ml_i + 1 < ml_argc) ? strtod(ml_argv[ml_i + 1], NULL) : 0;");
    do_write_line(ctx, "}");
    do_write_comment_tag(ctx, NULL);
    do_write_newline(ctx);
    do_write_newline(ctx);
}

static void do_write_token(struct codegen_ctx *ctx, enum ml_token_type token) {
    switch (token) {
        case ML_TOKEN_TYPE_RETURN:
            do_write_str(ctx, "return ");
            break;
        case ML_TOKEN_TYPE_ASSIGNMENT:
            do_write_str(ctx, " = ");
            break;
        case ML_TOKEN_TYPE_PLUS:
            do_write_str(ctx, " + ");
            break;
        case ML_TOKEN_TYPE_MINUS:
            do_write_str(ctx, " - ");
            break;
        case ML_TOKEN_TYPE_MULTIPLY:
            do_write_str(ctx, " * ");
            break;
        case ML_TOKEN_TYPE_DIVIDE:
            do_write_str(ctx, " / ");
            break;
        case ML_TOKEN_TYPE_COMMA:
            do_write_str(ctx, ", ");
            break;
        case ML_TOKEN_TYPE_PARENTHESIS_L:
            do_write_char(ctx, '(');
            break;
        case ML_TOKEN_TYPE_PARENTHESIS_R:
            do_write_char(ctx, ')');
            break;
        default:
            break;
    }
}

static void do_write_compile_data(void *opaque,
                                  enum ml_compile_visit_event event,
                                  const union ml_compile_visit_data *data) {
    char buf[ML_CODEGEN_BUFFER_CAPACITY_NUM];
    struct codegen_ctx *ctx = opaque;
    switch (event) {
        case ML_COMPILE_VISIT_EVENT_ARG_SECTION_START:
            do_write_comment_tag(ctx, "args");
            break;

        case ML_COMPILE_VISIT_EVENT_ARG_VISIT_INDEX:
            // e.g. "double ml_arg4 = 0;"
            snprintf(buf, sizeof(buf), "%d", data->index);
            do_write_str(ctx, "static double ");
            do_write_str(ctx, "ml_arg");
            do_write_str(ctx, buf);
            do_write_str(ctx, " = 0;");
            do_write_newline(ctx);
            break;

        case ML_COMPILE_VISIT_EVENT_GLOBAL_SECTION_START:
            do_write_comment_tag(ctx, "globals");
            break;

        case ML_COMPILE_VISIT_EVENT_GLOBAL_VISIT_VAR:
            // e.g. "double var = 0;"
            do_write_str(ctx, "static double ");
            do_write_str(ctx, data->name);
            do_write_str(ctx, " = 0;");
            do_write_newline(ctx);
            break;

        case ML_COMPILE_VISIT_EVENT_ARG_SECTION_END:
        case ML_COMPILE_VISIT_EVENT_GLOBAL_SECTION_END:
        case ML_COMPILE_VISIT_EVENT_SUB_FUNC_SECTION_END:
            do_write_comment_tag(ctx, NULL);
            do_write_newline(ctx);
            do_write_newline(ctx);
            break;

        case ML_COMPILE_VISIT_EVENT_SUB_FUNC_SECTION_START:
            do_write_comment_tag(ctx, "functions");
            break;

        case ML_COMPILE_VISIT_EVENT_SUB_FUNC_VISIT_START:
            // e.g. "double func(double a, double b) {"
            do_write_str(ctx, "static double ");
            do_write_str(ctx, data->func.name);
            do_write_char(ctx, '(');
            for (int i = 0; i < data->func.count; i++) {
                if (i)
                    do_write_str(ctx, ", ");
                do_write_str(ctx, "double ");
                do_write_str(ctx, data->func.params[i]);
            }
            do_write_str(ctx, ") {");
            do_write_newline(ctx);
            break;

        case ML_COMPILE_VISIT_EVENT_SUB_FUNC_VISIT_END:
            if (!data->func.ret)
                do_write_line_indent(ctx, "return 0;");
            do_write_line(ctx, "}");
            if (!data->func.last)
                do_write_newline(ctx);
            break;

        case ML_COMPILE_VISIT_EVENT_MAIN_FUNC_SECTION_START:
            do_write_line(ctx, "int main(int ml_argc, char **ml_argv) {");
            break;

        case ML_COMPILE_VISIT_EVENT_MAIN_FUNC_VISIT_ARG:
            // e.g. "ml_arg4 = ml_parse_arg(4, ml_argv, ml_argc);"
            snprintf(buf, sizeof(buf), "%d", data->index);
            do_write_indent(ctx);
            do_write_str(ctx, "ml_arg");
            do_write_str(ctx, buf);
            do_write_str(ctx, " = ml_parse_arg(");
            do_write_str(ctx, buf);
            do_write_str(ctx, ", ml_argv, ml_argc);");
            do_write_newline(ctx);
            break;

        case ML_COMPILE_VISIT_EVENT_MAIN_FUNC_SECTION_END:
            do_write_line_indent(ctx, "return EXIT_SUCCESS;");
            do_write_line(ctx, "}");
            break;

        case ML_COMPILE_VISIT_EVENT_STATEMENT_START:
            do_write_indent(ctx);
            break;

        case ML_COMPILE_VISIT_EVENT_STATEMENT_END:
            do_write_char(ctx, ';');
            do_write_newline(ctx);
            break;

        case ML_COMPILE_VISIT_EVENT_STATEMENT_VISIT_PRINT_START:
            do_write_str(ctx, "ml_print(");
            break;

        case ML_COMPILE_VISIT_EVENT_STATEMENT_VISIT_PRINT_END:
            do_write_str(ctx, ")");
            break;

        case ML_COMPILE_VISIT_EVENT_STATEMENT_VISIT_ARG:
            // e.g. "ml_arg4"
            snprintf(buf, sizeof(buf), "%d", data->index);
            do_write_str(ctx, "ml_arg");
            do_write_str(ctx, buf);
            break;

        case ML_COMPILE_VISIT_EVENT_STATEMENT_VISIT_NUMBER:
            snprintf(buf, sizeof(buf), "%a", data->number);
            do_write_str(ctx, buf);
            break;

        case ML_COMPILE_VISIT_EVENT_STATEMENT_VISIT_SYMBOL:
            do_write_str(ctx, data->name);
            break;

        case ML_COMPILE_VISIT_EVENT_STATEMENT_VISIT_TOKEN:
            do_write_token(ctx, data->token);
            break;
    }
}

bool ml_codegen_export_file(struct ml_compile_ctx *compile, const char *path) {
    FILE *file = fopen(path, "wb");
    if (!file)
        return false;

    ml_codegen_export_fns(compile, 0, file, &ml_codegen_io_fns_file);
    return true;
}

void ml_codegen_export_fns(struct ml_compile_ctx *compile, int capacity,
                           void *opaque, const struct ml_codegen_io_fns *fns) {
    int buffer_size = (capacity > 0) ? capacity : ML_CODEGEN_BUFFER_CAPACITY_WRITE;
    void *buffer_data = ml_memory_malloc(buffer_size);
    if (!buffer_data)
        return;

    struct codegen_ctx ctx = {
        .buffer = buffer_data,
        .offset = 0,
        .capacity = buffer_size,
        .opaque = opaque,
        .fns = fns,
    };

    do_write_framework(&ctx);
    ml_compile_accept(compile, &ctx, do_write_compile_data);
    do_write_flush(&ctx);

    ml_memory_free(buffer_data);
    ctx.fns->close(ctx.opaque);
}


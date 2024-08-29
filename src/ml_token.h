#pragma once

#include <stdbool.h>

struct ml_token_ctx;

struct ml_token_io_fns {
    int (*read)(void *opaque, char *buffer, int capacity);
    void (*close)(void *opaque);
};

struct ml_token_result {
    const char *buf;
    int len;
    union {
        double real;
    } value;
};

enum ml_token_type {
    ML_TOKEN_TYPE_EOF,
    ML_TOKEN_TYPE_ERROR,
    ML_TOKEN_TYPE_NUMBER,
    ML_TOKEN_TYPE_NAME,
    ML_TOKEN_TYPE_PRINT,
    ML_TOKEN_TYPE_RETURN,
    ML_TOKEN_TYPE_FUNCTION,
    ML_TOKEN_TYPE_ASSIGNMENT,
    ML_TOKEN_TYPE_COMMENT,
    ML_TOKEN_TYPE_SPACE,
    ML_TOKEN_TYPE_TAB,
    ML_TOKEN_TYPE_PLUS,
    ML_TOKEN_TYPE_MINUS,
    ML_TOKEN_TYPE_MULTIPLY,
    ML_TOKEN_TYPE_DIVIDE,
    ML_TOKEN_TYPE_PARENTHESIS_L,
    ML_TOKEN_TYPE_PARENTHESIS_R,
    ML_TOKEN_TYPE_LINE_TERMINATOR,
};

bool ml_token_ctx_init_file(struct ml_token_ctx **pp, const char *path);

bool ml_token_ctx_init_fns(struct ml_token_ctx **pp, const struct ml_token_io_fns *fns,
                           void *opaque, int read_capacity, int token_capacity);

void ml_token_ctx_uninit(struct ml_token_ctx **pp);

enum ml_token_type ml_token_iterate(struct ml_token_ctx *ctx, struct ml_token_result *result);

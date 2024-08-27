#pragma once

#include <stdbool.h>

struct ml_token_ctx;

struct ml_token_io_fns {
    int (*read)(void *opaque, char *buffer, int capacity);
    void (*close)(void *opaque);
};

enum ml_token_type {
    ML_TOKEN_TYPE_EOF,
    ML_TOKEN_TYPE_ERROR,
    ML_TOKEN_TYPE_LINE_TERMINATOR,
};

bool ml_token_ctx_init_file(struct ml_token_ctx **pp, const char *path);

bool ml_token_ctx_init_fns(struct ml_token_ctx **pp, const struct ml_token_io_fns *fns,
                           void *opaque, int read_capacity, int token_capacity);

void ml_token_ctx_uninit(struct ml_token_ctx **pp);

enum ml_token_type ml_token_iterate(struct ml_token_ctx *ctx, const char **buf, int *len);

#pragma once

#include <stdbool.h>

#include "ml_token.h"

struct ml_compile_ctx_init_args {
    int str_chars_capacity;
    int str_offsets_capacity;
    int global_vars_capacity;
};

struct ml_compile_ctx;

bool ml_compile_ctx_init(struct ml_compile_ctx **pp,
                         const struct ml_compile_ctx_init_args *args);

void ml_compile_ctx_uninit(struct ml_compile_ctx **pp);

int ml_compile_get_global_vars(struct ml_compile_ctx *ctx, const char ***names);

void ml_compile_feed_token(struct ml_compile_ctx *ctx,
                           enum ml_token_type type,
                           const struct ml_token_data *data);

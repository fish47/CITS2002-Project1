#pragma once

#include <stdbool.h>

#include "ml_token.h"

struct ml_compile_ctx_init_args {
    int list_default_capacity;
    int symbol_chars_capacity;
};

enum ml_compile_result {
    ML_COMPILE_RESULT_SUCCEED,
    ML_COMPILE_RESULT_ERROR_SYNTAX_ERROR,
    ML_COMPILE_RESULT_ERROR_INVALID_TOKEN,
    ML_COMPILE_RESULT_ERROR_OUT_OF_MEMORY,
    ML_COMPILE_RESULT_ERROR_NAME_COLLISION,
    ML_COMPILE_RESULT_ERROR_REDUNDANT_TAB,
    ML_COMPILE_RESULT_ERROR_EMPTY_FUNCTION,
    ML_COMPILE_RESULT_ERROR_NESTED_FUNCTION,
    ML_COMPILE_RESULT_ERROR_RETURN_IN_MAIN,
    ML_COMPILE_RESULT_ERROR_REDUNDANT_RETURN,
};

struct ml_compile_ctx;

bool ml_compile_ctx_init(struct ml_compile_ctx **pp,
                         const struct ml_compile_ctx_init_args *args);

void ml_compile_ctx_uninit(struct ml_compile_ctx **pp);

enum ml_compile_result ml_compile_feed_tokens(struct ml_compile_ctx *ctx,
                                              struct ml_token_ctx *token);

int ml_compile_get_global_names(struct ml_compile_ctx *ctx, const char ***names);

int ml_compile_get_func_count(struct ml_compile_ctx *ctx);

const char *ml_compile_get_func_name(struct ml_compile_ctx *ctx, int i);

int ml_compile_get_func_param_count(struct ml_compile_ctx *ctx, int i);

const char *ml_compile_get_func_param_name(struct ml_compile_ctx *ctx, int i, int j);

#pragma once

#include <stdbool.h>

struct ml_token_ctx;
struct ml_compile_ctx;

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

union ml_compile_visit_data {
    int index;
    int token;
    double number;
    const char *name;
    struct {
        bool ret;
        const char *name;
        const char **params;
        int count;
    } func;
};

enum ml_compile_visit_event {
    ML_COMPILE_VISIT_EVENT_ARG_SECTION_START,
    ML_COMPILE_VISIT_EVENT_ARG_VISIT_INDEX,
    ML_COMPILE_VISIT_EVENT_ARG_SECTION_END,
    ML_COMPILE_VISIT_EVENT_GLOBAL_SECTION_START,
    ML_COMPILE_VISIT_EVENT_GLOBAL_VISIT_VAR,
    ML_COMPILE_VISIT_EVENT_GLOBAL_SECTION_END,
    ML_COMPILE_VISIT_EVENT_SUB_FUNC_SECTION_START,
    ML_COMPILE_VISIT_EVENT_SUB_FUNC_VISIT_START,
    ML_COMPILE_VISIT_EVENT_SUB_FUNC_VISIT_END,
    ML_COMPILE_VISIT_EVENT_SUB_FUNC_SECTION_END,
    ML_COMPILE_VISIT_EVENT_MAIN_FUNC_SECTION_START,
    ML_COMPILE_VISIT_EVENT_MAIN_FUNC_VISIT_ARG,
    ML_COMPILE_VISIT_EVENT_MAIN_FUNC_SECTION_END,
    ML_COMPILE_VISIT_EVENT_STATEMENT_START,
    ML_COMPILE_VISIT_EVENT_STATEMENT_VISIT_PRINT_START,
    ML_COMPILE_VISIT_EVENT_STATEMENT_VISIT_PRINT_END,
    ML_COMPILE_VISIT_EVENT_STATEMENT_VISIT_ARG,
    ML_COMPILE_VISIT_EVENT_STATEMENT_VISIT_NUMBER,
    ML_COMPILE_VISIT_EVENT_STATEMENT_VISIT_SYMBOL,
    ML_COMPILE_VISIT_EVENT_STATEMENT_VISIT_TOKEN,
    ML_COMPILE_VISIT_EVENT_STATEMENT_END,
};

typedef void (*ml_compile_visit_fn)(void *opaque,
                                    enum ml_compile_visit_event event,
                                    const union ml_compile_visit_data *data);

struct ml_compile_ctx;

bool ml_compile_ctx_init(struct ml_compile_ctx **pp,
                         const struct ml_compile_ctx_init_args *args);

void ml_compile_ctx_uninit(struct ml_compile_ctx **pp);

enum ml_compile_result ml_compile_feed(struct ml_compile_ctx *ctx, struct ml_token_ctx *token);

void ml_compile_accept(struct ml_compile_ctx *ctx, void *opaque, ml_compile_visit_fn fn);

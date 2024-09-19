#include "ml_compile.h"
#include "ml_memory.h"
#include "ml_token.h"

#include <stdint.h>
#include <string.h>

#define ML_LIST_DECLARE(type, name)                                         \
    struct ml_list_##name {                                                 \
        type *base;                                                         \
        int count;                                                          \
        int capacity;                                                       \
    };                                                                      \
                                                                            \
    static bool list_init_##name(struct ml_list_##name *p, int capacity) {  \
        void *mem = ml_memory_malloc(capacity * sizeof(type));              \
        if (!mem)                                                           \
            return false;                                                   \
        *p = (struct ml_list_##name) {mem, 0, capacity};                    \
        return true;                                                        \
    }                                                                       \
                                                                            \
    static void list_uninit_##name(struct ml_list_##name *p) {              \
        if (!p)                                                             \
            return;                                                         \
        ml_memory_free(p->base);                                            \
        *p = (struct ml_list_##name) {0};                                   \
    }                                                                       \
                                                                            \
    static bool list_grow_##name(struct ml_list_##name *p, int n) {         \
        int req_capacity = p->count + n;                                    \
        if (req_capacity > p->capacity) {                                   \
            int new_capacity = p->capacity;                                 \
            while (new_capacity >= req_capacity)                            \
                new_capacity <<= 1;                                         \
            size_t new_size = new_capacity * sizeof(type);                  \
            void *new_base = ml_memory_realloc(p->base, new_size);          \
            if (!new_base)                                                  \
                return false;                                               \
            p->base = new_base;                                             \
            p->capacity = new_capacity;                                     \
        }                                                                   \
        return true;                                                        \
    }                                                                       \
                                                                            \
    static bool list_append_##name(struct ml_list_##name *p,                \
                                   const type *v) {                         \
        if (!list_grow_##name(p, 1))                                        \
            return false;                                                   \
        p->base[p->count] = *v;                                             \
        p->count++;                                                         \
        return true;                                                        \
    }                                                                       \
                                                                            \
    static bool list_fill_##name(struct ml_list_##name *p,                  \
                                 const type *v, int n) {                    \
        if (!list_grow_##name(p, n))                                        \
            return false;                                                   \
        memcpy(p->base + p->count, v, sizeof(type) * n);                    \
        p->count += n;                                                      \
        return true;                                                        \
    }                                                                       \

enum symbol_usage {
    SYMBOL_USAGE_NONE,
    SYMBOL_USAGE_KEEP,
    SYMBOL_USAGE_GLOBAL_VAR,
    SYMBOL_USAGE_FUNC_NAME,
    SYMBOL_USAGE_FUNC_PARAM,
};

enum symbol_resolve_hint {
    SYMBOL_RESOLVE_HINT_NONE,
    SYMBOL_RESOLVE_HINT_VAR,
};

struct symbol_entry {
    int offset;
    enum symbol_usage usage;
};

enum token_entry_type {
    TOKEN_ENTRY_TYPE_PLAIN,
    TOKEN_ENTRY_TYPE_SYMBOL,
    TOKEN_ENTRY_TYPE_NUMBER,
    TOKEN_ENTRY_TYPE_ARGUMENT,
    TOKEN_ENTRY_TYPE_TERMINATOR,
};

struct token_entry {
    enum token_entry_type type;
    union {
        int index;
        int offset;
        double number;
        enum ml_token_type type;
    } data;
};

struct func_entry {
    bool has_return;
    int name_offset;
    int param_begin;
    int param_end;
    int token_begin;
    int token_end;
};

// we have to use macros because generics are not supported in C language
// more type safety and less lines of code, but losing a little bit of maintainability
ML_LIST_DECLARE(int, int);
ML_LIST_DECLARE(char, str);
ML_LIST_DECLARE(struct func_entry, func);
ML_LIST_DECLARE(struct symbol_entry, sym);
ML_LIST_DECLARE(struct token_entry, token);

struct feed_state {
    struct ml_token_ctx *ctx;
    enum ml_compile_result error;
    enum ml_token_type type;
    struct ml_token_data data;
};

enum compile_flag {
    COMPILE_FLAG_HAS_TAB = 1,
    COMPILE_FLAG_IN_FUNC_BODY = 1 << 1,
};

enum check_line_type {
    CHECK_LINE_TYPE_EOF,
    CHECK_LINE_TYPE_EMPTY,
    CHECK_LINE_TYPE_RETURN,
    CHECK_LINE_TYPE_FUNCTION,
    CHECK_LINE_TYPE_STATEMENT,
};

enum parse_expr_flag {
    PARSE_EXPR_FLAG_SKIP_FIRST_READ = 1,
    PARSE_EXPR_FLAG_CHECK_FUNC_SYMBOL = 1 << 1,
};

struct ml_compile_ctx {
    uint32_t compile_flags;

    struct ml_list_str symbol_chars;
    struct ml_list_sym symbol_entries;

    struct ml_list_func func_list;
    struct ml_list_int param_offsets;

    struct ml_list_token tokens_main;
    struct ml_list_token tokens_sub;

    struct ml_list_int arg_indexes;
};

static const struct ml_compile_ctx_init_args ml_compile_ctx_init_args_default = {
    .list_default_capacity = 64,
    .symbol_chars_capacity = 4096,
};

static bool fail_on_error(struct feed_state *state, enum ml_compile_result error) {
    state->error = error;
    return false;
}

static bool fail_on_no_memory(struct feed_state *state) {
    return fail_on_error(state, ML_COMPILE_RESULT_ERROR_OUT_OF_MEMORY);
}

static bool fail_on_syntax_error(struct feed_state *state) {
    return fail_on_error(state, ML_COMPILE_RESULT_ERROR_SYNTAX_ERROR);
}

static int symbol_find(struct ml_compile_ctx *ctx, const char *name) {
    int low = 0;
    int high = ctx->symbol_entries.count - 1;
    while (low <= high) {
        int mid = (low + high) / 2;
        int offset = ctx->symbol_entries.base[mid].offset;
        int cmp = strcmp(name, ctx->symbol_chars.base + offset);
        if (cmp < 0)
            high = mid - 1;
        else if (cmp > 0)
            low = mid + 1;
        else
            return mid;
    }
    // low is the insert index
    return -(low + 1);
}

static bool symbol_mark(struct symbol_entry *entry,
                        enum ml_compile_result *error,
                        enum symbol_usage usage) {
    // only function parameter names can be reused
    // collisions between global variable names and functions name are not allowed
    if (usage == SYMBOL_USAGE_KEEP) {
        return true;
    } else if (entry->usage == SYMBOL_USAGE_NONE) {
        goto pass;
    } else if (entry->usage != usage) {
        *error = ML_COMPILE_RESULT_ERROR_NAME_COLLISION;
        return false;
    }
pass:
    entry->usage = usage;
    return true;
}

static bool symbol_ensure(struct ml_compile_ctx *ctx, struct feed_state *state,
                          enum symbol_usage usage, struct symbol_entry **entry) {
    int search_idx = symbol_find(ctx, state->data.buf);
    if (search_idx >= 0) {
        *entry = &ctx->symbol_entries.base[search_idx];
        return symbol_mark(*entry, &state->error, usage);
    }

    // strings are zero-terminated and packed into a large chunk of memory
    // so the current end offset is the start offset of the string to be packed
    int offset = ctx->symbol_chars.count;
    if (!list_grow_sym(&ctx->symbol_entries, 1))
        return fail_on_no_memory(state);
    if (!list_fill_str(&ctx->symbol_chars, state->data.buf, state->data.len + 1))
        return fail_on_no_memory(state);

    // keep entries sorted based on the strings they point to
    int insert_idx = -search_idx - 1;
    int move_count = ctx->symbol_entries.count - insert_idx;
    if (move_count) {
        struct symbol_entry *insert_addr = ctx->symbol_entries.base + insert_idx;
        memmove(insert_addr + 1, insert_addr, sizeof(struct symbol_entry) * move_count);
    }
    ctx->symbol_entries.base[insert_idx] = (struct symbol_entry) {
        .offset = offset,
        .usage = SYMBOL_USAGE_NONE,
    };
    ctx->symbol_entries.count++;

    *entry = &ctx->symbol_entries.base[insert_idx];
    return symbol_mark(*entry, &state->error, usage);
}

static enum symbol_usage symbol_resolve(struct ml_compile_ctx *ctx,
                                        struct symbol_entry *entry,
                                        const enum ml_token_type *next,
                                        enum symbol_resolve_hint hint) {
    bool var_hint = false;
    if (next) {
        // it should be a function call
        if (*next == ML_TOKEN_TYPE_PARENTHESIS_L)
            return SYMBOL_USAGE_FUNC_NAME;
        var_hint = true;
    }

    if (var_hint || hint == SYMBOL_RESOLVE_HINT_VAR) {
        // it must be a global variable
        if (!(ctx->compile_flags & COMPILE_FLAG_IN_FUNC_BODY))
            return SYMBOL_USAGE_GLOBAL_VAR;

        switch (entry->usage) {
            // it has been definied
            case SYMBOL_USAGE_GLOBAL_VAR:
            case SYMBOL_USAGE_FUNC_PARAM:
                return entry->usage;

            // it may be function name, but it will fail later
            default:
                return SYMBOL_USAGE_GLOBAL_VAR;
        }
    }

    // follow the previous definition
    if (entry->usage != SYMBOL_USAGE_NONE)
        return entry->usage;

    return SYMBOL_USAGE_GLOBAL_VAR;
}

bool ml_compile_ctx_init(struct ml_compile_ctx **pp,
                         const struct ml_compile_ctx_init_args *args) {
    const struct ml_compile_ctx_init_args *p_args = args;
    if (!p_args)
        p_args = &ml_compile_ctx_init_args_default;

    struct ml_compile_ctx *ctx = ml_memory_malloc(sizeof(struct ml_compile_ctx));
    if (!ctx)
        goto fail;

    *ctx = (struct ml_compile_ctx) {0};
    if (!list_init_str(&ctx->symbol_chars, p_args->symbol_chars_capacity))
        goto fail;
    if (!list_init_sym(&ctx->symbol_entries, p_args->list_default_capacity))
        goto fail;
    if (!list_init_func(&ctx->func_list, p_args->list_default_capacity))
        goto fail;
    if (!list_init_int(&ctx->param_offsets, p_args->list_default_capacity))
        goto fail;
    if (!list_init_token(&ctx->tokens_main, p_args->list_default_capacity))
        goto fail;
    if (!list_init_token(&ctx->tokens_sub, p_args->list_default_capacity))
        goto fail;
    if (!list_init_int(&ctx->arg_indexes, p_args->list_default_capacity))
        goto fail;

    *pp = ctx;
    return true;

fail:
    ml_compile_ctx_uninit(&ctx);
    return false;
}

void ml_compile_ctx_uninit(struct ml_compile_ctx **pp) {
    struct ml_compile_ctx *ctx = pp ? *pp : NULL;
    if (!ctx)
        return;

    list_uninit_str(&ctx->symbol_chars);
    list_uninit_sym(&ctx->symbol_entries);
    list_uninit_func(&ctx->func_list);
    list_uninit_int(&ctx->param_offsets);
    list_uninit_token(&ctx->tokens_main);
    list_uninit_token(&ctx->tokens_sub);
    list_uninit_int(&ctx->arg_indexes);
    ml_memory_free(ctx);
    *pp = NULL;
}

static bool feed_read_next(struct feed_state *state) {
    state->type = ml_token_iterate(state->ctx, &state->data);
    if (state->type != ML_TOKEN_TYPE_ERROR)
        return true;

    state->error = ML_COMPILE_RESULT_ERROR_INVALID_TOKEN;
    return false;
}

static bool feed_skip_space(struct feed_state *state) {
    while (true) {
        if (!feed_read_next(state))
            return false;
        else if (state->type != ML_TOKEN_TYPE_SPACE)
            return true;
    }

    // we will get an EOF even though it is an empty file
    return true;
}

static bool feed_expect_next(struct feed_state *state, enum ml_token_type type) {
    state->type = ml_token_iterate(state->ctx, &state->data);
    return (state->type == type) || fail_on_syntax_error(state);
}

static bool feed_expect_space_and_next(struct feed_state *state, enum ml_token_type type) {
    return feed_expect_next(state, ML_TOKEN_TYPE_SPACE) && feed_expect_next(state, type);
}

static struct ml_list_token *resolve_token_list(struct ml_compile_ctx *ctx) {
    return (ctx->compile_flags & COMPILE_FLAG_HAS_TAB) ? &ctx->tokens_sub : &ctx->tokens_main;
}

static bool do_check_line_start(enum check_line_type type,
                                struct ml_compile_ctx *ctx,
                                struct feed_state *state) {
    bool in_func = (ctx->compile_flags & COMPILE_FLAG_IN_FUNC_BODY);
    bool has_tab = (ctx->compile_flags & COMPILE_FLAG_HAS_TAB);

    if (type == CHECK_LINE_TYPE_RETURN && !in_func)
        return fail_on_error(state, ML_COMPILE_RESULT_ERROR_RETURN_IN_MAIN);

    if (type == CHECK_LINE_TYPE_EMPTY && has_tab)
        return fail_on_error(state, ML_COMPILE_RESULT_ERROR_REDUNDANT_TAB);

    if (type == CHECK_LINE_TYPE_FUNCTION && in_func && has_tab)
        return fail_on_error(state, ML_COMPILE_RESULT_ERROR_NESTED_FUNCTION);

    // a non-empty line without indents can finish the function body
    if (in_func && !has_tab && (type != CHECK_LINE_TYPE_EMPTY)) {
        // the first statement without a indenfirst valid t means finishing the last function
        ctx->compile_flags &= ~COMPILE_FLAG_IN_FUNC_BODY;

        // check empty function
        struct func_entry *func = &ctx->func_list.base[ctx->func_list.count - 1];
        if (func->token_begin == func->token_end)
            return fail_on_error(state, ML_COMPILE_RESULT_ERROR_EMPTY_FUNCTION);
    }

    return true;
}

static bool do_check_line_end(enum check_line_type type,
                              struct ml_compile_ctx *ctx,
                              struct feed_state *state) {
    bool in_func = (ctx->compile_flags & COMPILE_FLAG_IN_FUNC_BODY);
    bool has_tab = (ctx->compile_flags & COMPILE_FLAG_HAS_TAB);
    struct func_entry *func = in_func ? &ctx->func_list.base[ctx->func_list.count - 1] : NULL;

    // append a valid statement to the current function's token list
    if (in_func && has_tab)
        func->token_end = ctx->tokens_sub.count;

    if (type == CHECK_LINE_TYPE_RETURN) {
        if (func->has_return)
            return fail_on_error(state, ML_COMPILE_RESULT_ERROR_REDUNDANT_RETURN);
        func->has_return = true;
    }

    // the following lines may be statements of the last function
    if (type == CHECK_LINE_TYPE_FUNCTION)
        ctx->compile_flags |= COMPILE_FLAG_IN_FUNC_BODY;

    // a tab only works for its line
    ctx->compile_flags &= ~COMPILE_FLAG_HAS_TAB;
    return true;
}

static bool parse_function(struct ml_compile_ctx *ctx, struct feed_state *state) {
    if (!do_check_line_start(CHECK_LINE_TYPE_FUNCTION, ctx, state))
        return false;

    // function name
    if (!feed_expect_space_and_next(state, ML_TOKEN_TYPE_NAME))
        return false;

    struct symbol_entry *symbol_name = NULL;
    if (!symbol_ensure(ctx, state, SYMBOL_USAGE_FUNC_NAME, &symbol_name))
        return false;

    // symbol entry may be invalid after modification, so save the information here
    int name_offset = symbol_name->offset;

    int param_begin = ctx->param_offsets.count;
    while (true) {
        if (!feed_skip_space(state))
            return false;

        if (state->type == ML_TOKEN_TYPE_NAME) {
            struct symbol_entry *symbol_param = NULL;
            if (!symbol_ensure(ctx, state, SYMBOL_USAGE_FUNC_PARAM, &symbol_param))
                return false;
            const int param_offset = symbol_param->offset;
            if (!list_append_int(&ctx->param_offsets, &param_offset))
                return fail_on_no_memory(state);
        } else if (state->type == ML_TOKEN_TYPE_COMMENT) {
            // ignore
        } else if (state->type == ML_TOKEN_TYPE_LINE_TERMINATOR) {
            break;
        } else if (state->type == ML_TOKEN_TYPE_EOF) {
            break;
        } else {
            return fail_on_syntax_error(state);
        }
    }

    // ensure all parameters are unique
    int param_end = ctx->param_offsets.count;
    for (int i = param_begin; i < param_end; i++) {
        for (int j = i + 1; j < param_end; j++) {
            if (ctx->param_offsets.base[i] == ctx->param_offsets.base[j])
                return fail_on_syntax_error(state);
        }
    }

    const struct func_entry entry = {
        .has_return = false,
        .name_offset = name_offset,
        .param_begin = param_begin,
        .param_end = param_end,
        .token_begin = ctx->tokens_sub.count,
        .token_end = ctx->tokens_sub.count,
    };
    if (!list_append_func(&ctx->func_list, &entry))
        return fail_on_no_memory(state);

    if (!do_check_line_end(CHECK_LINE_TYPE_FUNCTION, ctx, state))
        return false;

    return true;
}

static bool parse_do_mark_arg_index(struct ml_compile_ctx *ctx, struct feed_state *state) {
    int idx = 0;
    int val = state->data.value.index;
    while (idx < ctx->arg_indexes.count) {
        const int cmp = ctx->arg_indexes.base[idx];
        if (cmp == val)
            return true;
        else if (cmp > val)
            break;
        idx++;
    }

    if (!list_grow_int(&ctx->arg_indexes, 1))
        return fail_on_no_memory(state);

    // the insert point makes sure values are sorted
    int move = ctx->arg_indexes.count - idx;
    if (move) {
        int *base = ctx->arg_indexes.base + idx;
        memmove(base + 1, base, sizeof(int) * move);
    }

    ctx->arg_indexes.base[idx] = val;
    ctx->arg_indexes.count++;
    return true;
}

static bool parse_do_append_symbol_token(struct ml_compile_ctx *ctx,
                                         struct feed_state *state,
                                         bool *check_func,
                                         struct symbol_entry **symbol,
                                         const enum ml_token_type *next,
                                         enum symbol_resolve_hint hint) {
    if (!*symbol)
        return true;

    enum symbol_usage usage = symbol_resolve(ctx, *symbol, next, hint);
    if (!symbol_mark(*symbol, &state->error, usage))
        return false;

    // the first symbol is supposed to be a function name
    if (*check_func) {
        *check_func = false;
        if (usage != SYMBOL_USAGE_FUNC_NAME)
            return fail_on_syntax_error(state);
    }

    // flush the pending symbol
    const struct token_entry token = {
        .type = TOKEN_ENTRY_TYPE_SYMBOL,
        .data = { .offset = (*symbol)->offset },
    };
    if (!list_append_token(resolve_token_list(ctx), &token))
        return fail_on_no_memory(state);

    *symbol = NULL;
    return true;
}

static bool parse_expression(struct ml_compile_ctx *ctx, struct feed_state *state,
                             struct symbol_entry *sym, uint32_t flags) {
    bool check_func = (flags & PARSE_EXPR_FLAG_CHECK_FUNC_SYMBOL);
    struct symbol_entry *symbol = sym;
    struct ml_list_token *tokens = resolve_token_list(ctx);

    if (!(flags & PARSE_EXPR_FLAG_SKIP_FIRST_READ)) {
        if (!feed_skip_space(state))
            return false;
    }

    while (true) {
        bool skip = false;
        bool stop = false;
        bool valid = false;
        struct token_entry token;
        switch (state->type) {
            case ML_TOKEN_TYPE_EOF:
            case ML_TOKEN_TYPE_LINE_TERMINATOR:
                stop = true;
                break;

            case ML_TOKEN_TYPE_ARGUMENT:
                if (!parse_do_mark_arg_index(ctx, state))
                    return false;
                valid = true;
                token = (struct token_entry) {
                    .type = TOKEN_ENTRY_TYPE_ARGUMENT,
                    .data = { .index = state->data.value.index },
                };
                break;

            case ML_TOKEN_TYPE_NUMBER:
                valid = true;
                token = (struct token_entry) {
                    .type = TOKEN_ENTRY_TYPE_NUMBER,
                    .data = { .number = state->data.value.number },
                };
                break;

            case ML_TOKEN_TYPE_NAME:
                // successive variables
                if (symbol)
                    return fail_on_syntax_error(state);

                // may be a variable or a function call
                if (!symbol_ensure(ctx, state, SYMBOL_USAGE_KEEP, &symbol))
                    return false;

                skip = true;
                valid = true;
                break;

            case ML_TOKEN_TYPE_ERROR:
            case ML_TOKEN_TYPE_PRINT:
            case ML_TOKEN_TYPE_TAB:
            case ML_TOKEN_TYPE_RETURN:
            case ML_TOKEN_TYPE_FUNCTION:
            case ML_TOKEN_TYPE_ASSIGNMENT:
                // these should not appear in statements
                return fail_on_syntax_error(state);

            case ML_TOKEN_TYPE_COMMENT:
            case ML_TOKEN_TYPE_SPACE:
                skip = true;
                break;

            case ML_TOKEN_TYPE_PLUS:
            case ML_TOKEN_TYPE_MINUS:
            case ML_TOKEN_TYPE_MULTIPLY:
            case ML_TOKEN_TYPE_DIVIDE:
            case ML_TOKEN_TYPE_COMMA:
            case ML_TOKEN_TYPE_PARENTHESIS_L:
            case ML_TOKEN_TYPE_PARENTHESIS_R:
                valid = true;
                token = (struct token_entry) {
                    .type = TOKEN_ENTRY_TYPE_PLAIN,
                    .data = { .type = state->type },
                };
                break;
        }

        if (stop)
            break;

        // read the token for the next round
        if (!feed_skip_space(state))
            return false;

        if (skip)
            continue;

        if (!valid)
            return fail_on_syntax_error(state);

        // the symbol usage can be determined after getting one more token
        const enum ml_token_type next = token.data.type;
        if (!parse_do_append_symbol_token(ctx, state, &check_func, &symbol, &next, SYMBOL_RESOLVE_HINT_NONE))
            return false;

        if (!list_append_token(tokens, &token))
            return fail_on_no_memory(state);
    }

    // the last or the only one variable in the expression
    if (!parse_do_append_symbol_token(ctx, state, &check_func, &symbol, NULL, SYMBOL_RESOLVE_HINT_VAR))
        return false;

    const struct token_entry end = { .type = TOKEN_ENTRY_TYPE_TERMINATOR };
    if (!list_append_token(tokens, &end))
        return fail_on_no_memory(state);

    return true;
}

static bool parse_assignment(struct ml_compile_ctx *ctx,
                             struct feed_state *state,
                             int operand_offset) {
    // left operand
    struct ml_list_token *tokens = resolve_token_list(ctx);
    const struct token_entry operand = {
        .type = TOKEN_ENTRY_TYPE_SYMBOL,
        .data = { .offset = operand_offset },
    };
    if (!list_append_token(tokens, &operand))
        return fail_on_no_memory(state);

    // assignment operator
    const struct token_entry assignment = {
        .type = TOKEN_ENTRY_TYPE_PLAIN,
        .data = { .type = ML_TOKEN_TYPE_ASSIGNMENT },
    };
    if (!list_append_token(tokens, &assignment))
        return fail_on_no_memory(state);

    // right statement
    if (!parse_expression(ctx, state, NULL, 0))
        return false;

    return true;
}

static bool parse_instruction(struct ml_compile_ctx *ctx,
                              struct feed_state *state,
                              enum check_line_type type) {
    if (!do_check_line_start(type, ctx, state))
        return false;

    const struct token_entry token = {
        .type = TOKEN_ENTRY_TYPE_PLAIN,
        .data = { .type = state->type },
    };
    struct ml_list_token *tokens = resolve_token_list(ctx);
    if (!list_append_token(tokens, &token))
        return fail_on_no_memory(state);

    if (!parse_expression(ctx, state, NULL, 0))
        return false;

    if (!do_check_line_end(type, ctx, state))
        return false;

    return true;
}

enum ml_compile_result ml_compile_feed(struct ml_compile_ctx *ctx, struct ml_token_ctx *token) {
    struct feed_state state = {
        .ctx = token,
        .error = ML_COMPILE_RESULT_SUCCEED,
        .type = ML_TOKEN_TYPE_EOF,
        .data = {0},
    };

    bool is_comment_line = false;
    while (true) {
        if (!feed_skip_space(&state))
            return state.error;

        if (state.type == ML_TOKEN_TYPE_NAME) {
            if (!do_check_line_start(CHECK_LINE_TYPE_STATEMENT, ctx, &state))
                return state.error;

            // it may be a variable or function call
            struct symbol_entry *symbol = NULL;
            if (!symbol_ensure(ctx, &state, SYMBOL_USAGE_KEEP, &symbol))
                return state.error;
            if (!feed_skip_space(&state))
                return state.error;

            if (state.type == ML_TOKEN_TYPE_ASSIGNMENT) {
                // it should be a variable if it is followed by an assignment operator
                enum symbol_usage usage = symbol_resolve(ctx, symbol, NULL, SYMBOL_RESOLVE_HINT_VAR);
                if (!symbol_mark(symbol, &state.error, usage))
                    return state.error;
                if (!parse_assignment(ctx, &state, symbol->offset))
                    return state.error;
            } else {
                // it should be a function call
                uint32_t flags = PARSE_EXPR_FLAG_SKIP_FIRST_READ | PARSE_EXPR_FLAG_CHECK_FUNC_SYMBOL;
                if (!parse_expression(ctx, &state, symbol, flags))
                    return state.error;
            }

            if (!do_check_line_end(CHECK_LINE_TYPE_STATEMENT, ctx, &state))
                return state.error;
        } else if (state.type == ML_TOKEN_TYPE_FUNCTION) {
            if (!parse_function(ctx, &state))
                return state.error;
        } else if (state.type == ML_TOKEN_TYPE_PRINT) {
            if (!parse_instruction(ctx, &state, CHECK_LINE_TYPE_STATEMENT))
                return state.error;
        } else if (state.type == ML_TOKEN_TYPE_RETURN) {
            if (!parse_instruction(ctx, &state, CHECK_LINE_TYPE_RETURN))
                return state.error;
        } else if (state.type == ML_TOKEN_TYPE_TAB) {
            // no more than one tab
            if (ctx->compile_flags & COMPILE_FLAG_HAS_TAB)
                return ML_COMPILE_RESULT_ERROR_REDUNDANT_TAB;
            ctx->compile_flags |= COMPILE_FLAG_HAS_TAB;
        } else if (state.type == ML_TOKEN_TYPE_COMMENT) {
            // a line that starts with a comment
            is_comment_line = true;
            if (!do_check_line_start(CHECK_LINE_TYPE_EMPTY, ctx, &state))
                return state.error;
        } else if (state.type == ML_TOKEN_TYPE_LINE_TERMINATOR) {
            // the end of comments or empty lines
            is_comment_line = false;
            if (!do_check_line_end(CHECK_LINE_TYPE_EMPTY, ctx, &state))
                return state.error;
        } else if (state.type == ML_TOKEN_TYPE_EOF) {
            if (is_comment_line) {
                // the last line is a comment without a line terminator
                is_comment_line = false;
                if (!do_check_line_end(CHECK_LINE_TYPE_EMPTY, ctx, &state))
                    return state.error;
            } else {
                // if a tab in the last line, finish it first
                bool has_tab = (ctx->compile_flags & COMPILE_FLAG_HAS_TAB);
                enum check_line_type type = has_tab ? CHECK_LINE_TYPE_EMPTY : CHECK_LINE_TYPE_EOF;
                if (!do_check_line_start(type, ctx, &state))
                    return state.error;
                if (!do_check_line_end(type, ctx, &state))
                    return state.error;

                // synthesize a pure EOF line for cleanup and check
                if (!has_tab)
                    break;
            }
        } else if (state.type == ML_TOKEN_TYPE_ERROR) {
            return ML_COMPILE_RESULT_ERROR_INVALID_TOKEN;
        } else {
            return ML_COMPILE_RESULT_ERROR_SYNTAX_ERROR;
        }
    }
    return ML_COMPILE_RESULT_SUCCEED;
}

static void do_accept_statements(struct ml_compile_ctx *ctx,
                                 void *opaque, ml_compile_visit_fn fn,
                                 struct ml_list_token *tokens, int begin, int end) {
    bool is_print = false;
    bool is_started = false;
    for (int i = begin; i < end; i++) {
        if (!is_started) {
            is_started = true;
            fn(opaque, ML_COMPILE_VISIT_EVENT_STATEMENT_START, NULL);
        }

        struct token_entry *token = &tokens->base[i];
        switch (token->type) {
            case TOKEN_ENTRY_TYPE_PLAIN:
                if (token->data.type == ML_TOKEN_TYPE_PRINT) {
                    is_print = true;
                    fn(opaque, ML_COMPILE_VISIT_EVENT_STATEMENT_VISIT_PRINT_START, NULL);
                } else {
                    const union ml_compile_visit_data data = { .token = token->data.type };
                    fn(opaque, ML_COMPILE_VISIT_EVENT_STATEMENT_VISIT_TOKEN, &data);
                }
                break;
            case TOKEN_ENTRY_TYPE_SYMBOL:
                fn(opaque, ML_COMPILE_VISIT_EVENT_STATEMENT_VISIT_SYMBOL,
                   &(union ml_compile_visit_data) { .name = ctx->symbol_chars.base + token->data.offset });
                break;
            case TOKEN_ENTRY_TYPE_NUMBER:
                fn(opaque, ML_COMPILE_VISIT_EVENT_STATEMENT_VISIT_NUMBER,
                   &(union ml_compile_visit_data) { .number = token->data.number });
                break;
            case TOKEN_ENTRY_TYPE_ARGUMENT:
                fn(opaque, ML_COMPILE_VISIT_EVENT_STATEMENT_VISIT_ARG,
                   &(union ml_compile_visit_data) { .index = token->data.index });
                break;
            case TOKEN_ENTRY_TYPE_TERMINATOR:
                if (is_print) {
                    is_print = false;
                    fn(opaque, ML_COMPILE_VISIT_EVENT_STATEMENT_VISIT_PRINT_END, NULL);
                }
                is_started = false;
                fn(opaque, ML_COMPILE_VISIT_EVENT_STATEMENT_END, NULL);
                break;
        }
    }
}

static void do_accept_args(enum ml_compile_visit_event event,
                           struct ml_compile_ctx *ctx,
                           void *opaque, ml_compile_visit_fn fn) {
    for (int i = 0; i < ctx->arg_indexes.count; i++) {
        fn(opaque, event, &(union ml_compile_visit_data) {
            .index = ctx->arg_indexes.base[i],
        });
    }
}

static void do_accept_globals(struct ml_compile_ctx *ctx,
                              void *opaque, ml_compile_visit_fn fn) {
    bool started = false;
    for (int i = 0; i < ctx->symbol_entries.count; i++) {
        struct symbol_entry *symbol = &ctx->symbol_entries.base[i];
        if (symbol->usage != SYMBOL_USAGE_GLOBAL_VAR)
            continue;

        if (!started) {
            started = true;
            fn(opaque, ML_COMPILE_VISIT_EVENT_GLOBAL_SECTION_START, NULL);
        }

        fn(opaque, ML_COMPILE_VISIT_EVENT_GLOBAL_VISIT_VAR, &(union ml_compile_visit_data) {
            .name = ctx->symbol_chars.base + symbol->offset,
        });
    }

    if (started)
        fn(opaque, ML_COMPILE_VISIT_EVENT_GLOBAL_SECTION_END, NULL);
}

static void do_accept_functions(struct ml_compile_ctx *ctx,
                                void *opaque, ml_compile_visit_fn fn) {
    if (ctx->func_list.count <= 0)
        return;

    size_t capacity = 0;
    void *buffer = NULL;

    fn(opaque, ML_COMPILE_VISIT_EVENT_SUB_FUNC_SECTION_START, NULL);
    for (int i = 0; i < ctx->func_list.count; i++) {
        // need an array to hold string pointers of parameters
        struct func_entry *func = &ctx->func_list.base[i];
        int count = func->param_end - func->param_begin;
        size_t request = sizeof(const char*) * count;
        if (capacity < request) {
            void *p = ml_memory_realloc(buffer, request);
            if (!p)
                continue;

            buffer = p;
            capacity = request;
        }

        const char **params = buffer;
        const char *name = ctx->symbol_chars.base + func->name_offset;
        for (int j = 0; j < count; j++) {
            int offset = ctx->param_offsets.base[func->param_begin + j];
            params[j] = ctx->symbol_chars.base + offset;
        }

        const union ml_compile_visit_data data = {
            .func = {
                .ret = func->has_return,
                .last = (i + 1 == ctx->func_list.count),
                .name = name,
                .params = params,
                .count = count,
            }
        };

        fn(opaque, ML_COMPILE_VISIT_EVENT_SUB_FUNC_VISIT_START, &data);
        do_accept_statements(ctx, opaque, fn, &ctx->tokens_sub, func->token_begin, func->token_end);
        fn(opaque, ML_COMPILE_VISIT_EVENT_SUB_FUNC_VISIT_END, &data);
    }
    fn(opaque, ML_COMPILE_VISIT_EVENT_SUB_FUNC_SECTION_END, NULL);

    if (buffer)
        ml_memory_free(buffer);
}

void ml_compile_accept(struct ml_compile_ctx *ctx, void *opaque, ml_compile_visit_fn fn) {
    if (!fn)
        return;

    // args
    if (ctx->arg_indexes.count) {
        fn(opaque, ML_COMPILE_VISIT_EVENT_ARG_SECTION_START, NULL);
        do_accept_args(ML_COMPILE_VISIT_EVENT_ARG_VISIT_INDEX, ctx, opaque, fn);
        fn(opaque, ML_COMPILE_VISIT_EVENT_ARG_SECTION_END, NULL);
    }

    // globals
    do_accept_globals(ctx, opaque, fn);

    // functions
    do_accept_functions(ctx, opaque, fn);

    // main
    fn(opaque, ML_COMPILE_VISIT_EVENT_MAIN_FUNC_SECTION_START, NULL);
    do_accept_args(ML_COMPILE_VISIT_EVENT_MAIN_FUNC_VISIT_ARG, ctx, opaque, fn);
    do_accept_statements(ctx, opaque, fn, &ctx->tokens_main, 0, ctx->tokens_main.count);
    fn(opaque, ML_COMPILE_VISIT_EVENT_MAIN_FUNC_SECTION_END, NULL);
}


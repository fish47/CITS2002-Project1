#include "ml_compile.h"
#include "ml_memory.h"

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

struct symbol_entry {
    int offset;
    enum symbol_usage usage;
};

struct func_entry {
    int name_offset;
    int param_begin;
    int param_count;
};

// we have to use macros because generics are not supported in C language
// more type safety and less lines of code, but losing a little bit of maintainability
ML_LIST_DECLARE(int, int);
ML_LIST_DECLARE(char, str);
ML_LIST_DECLARE(struct func_entry, func);
ML_LIST_DECLARE(struct symbol_entry, sym);

struct feed_state {
    struct ml_token_ctx *ctx;
    enum ml_compile_result error;
    enum ml_token_type type;
    struct ml_token_data data;
};

struct ml_compile_ctx {
    struct ml_list_str symbol_chars;
    struct ml_list_sym symbol_entries;

    struct ml_list_func func_list;
    struct ml_list_int param_offsets;

    void *result_holder;
    size_t result_capacity;
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
    return fail_on_error(state, ML_COMPILE_RESULT_OUT_OF_MEMORY);
}

static bool fail_on_syntax_error(struct feed_state *state) {
    return fail_on_error(state, ML_COMPILE_RESULT_SYNTAX_ERROR);
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
    } else if (entry->usage != usage && entry->usage != SYMBOL_USAGE_FUNC_PARAM) {
        *error = ML_COMPILE_RESULT_NAME_COLLISION;
        return false;
    }
pass:
    entry->usage = usage;
    return true;
}

static bool symbol_ensure(struct ml_compile_ctx *ctx, struct feed_state *state,
                          enum symbol_usage usage, int *idx) {
    int search_idx = symbol_find(ctx, state->data.buf);
    if (search_idx >= 0) {
        *idx = search_idx;
        return symbol_mark(&ctx->symbol_entries.base[search_idx], &state->error, usage);
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

    *idx = insert_idx;
    return symbol_mark(&ctx->symbol_entries.base[insert_idx], &state->error, usage);
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
    if (ctx->result_holder)
        ml_memory_free(ctx->result_holder);
    ml_memory_free(ctx);
    *pp = NULL;
}

static void *ensure_result_capacity(struct ml_compile_ctx *ctx, size_t capacity) {
    if (ctx->result_capacity >= capacity)
        return ctx->result_holder;

    void *base = ml_memory_realloc(ctx->result_holder, capacity);
    if (!base)
        return NULL;

    ctx->result_holder = base;
    ctx->result_capacity = capacity;
    return ctx->result_holder;
}

int ml_compile_get_global_names(struct ml_compile_ctx *ctx, const char ***names) {
    // how many global variable names
    int count = 0;
    for (int i = 0; i < ctx->symbol_entries.count; i++)
        count += (ctx->symbol_entries.base[i].usage == SYMBOL_USAGE_GLOBAL_VAR);

    *names = NULL;
    if (!count)
        return 0;

    const char **base = ensure_result_capacity(ctx, sizeof(const char*) * count);
    if (!base)
        return 0;

    for (int i = 0, j = 0; i < ctx->symbol_entries.count; i++) {
        struct symbol_entry *entry = &ctx->symbol_entries.base[i];
        if (entry->usage == SYMBOL_USAGE_GLOBAL_VAR)
            base[j++] = ctx->symbol_chars.base + entry->offset;
    }
    *names = base;
    return count;
}

static bool feed_read_next(struct feed_state *state) {
    state->type = ml_token_iterate(state->ctx, &state->data);
    if (state->type != ML_TOKEN_TYPE_ERROR)
        return true;

    state->error = ML_COMPILE_RESULT_INVALID_TOKEN;
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

static bool parse_function(struct ml_compile_ctx *ctx, struct feed_state *state) {
    // function name
    if (!feed_expect_space_and_next(state, ML_TOKEN_TYPE_NAME))
        return false;

    int name_sym_idx = 0;
    if (!symbol_ensure(ctx, state, SYMBOL_USAGE_FUNC_NAME, &name_sym_idx))
        return false;

    int param_count = 0;
    int param_begin = ctx->param_offsets.count;
    int name_str_offset = ctx->symbol_entries.base[name_sym_idx].offset;
    while (true) {
        if (!feed_skip_space(state))
            return false;

        if (state->type == ML_TOKEN_TYPE_NAME) {
            int param_sym_idx = 0;
            if (!symbol_ensure(ctx, state, SYMBOL_USAGE_FUNC_PARAM, &param_sym_idx))
                return false;
            int param_str_offset = ctx->symbol_entries.base[param_sym_idx].offset;
            if (!list_append_int(&ctx->param_offsets, &param_str_offset))
                return fail_on_no_memory(state);
            param_count++;
        } else if (state->type == ML_TOKEN_TYPE_COMMENT) {
            // ignore
        } else if (state->type == ML_TOKEN_TYPE_LINE_TERMINATOR) {
            break;
        } else {
            return fail_on_syntax_error(state);
        }
    }

    // ensure all parameters are unique
    for (int i = 0; i < param_count; i++) {
        for (int j = i + 1; j < param_count; j++) {
            int offset_left = ctx->param_offsets.base[param_begin + i];
            int offset_right = ctx->param_offsets.base[param_begin + j];
            if (offset_left == offset_right)
                return fail_on_syntax_error(state);
        }
    }

    const struct func_entry entry = {
        .name_offset = name_str_offset,
        .param_begin = param_begin,
        .param_count = param_count,
    };
    if (!list_append_func(&ctx->func_list, &entry))
        return fail_on_no_memory(state);

    return true;
}

static bool parse_assignment(struct ml_compile_ctx *ctx, struct feed_state *state, int sym_idx) {
    // the operand is considered a global variable unless it was declared as a function parameter
    struct symbol_entry *symbol = &ctx->symbol_entries.base[sym_idx];
    if (symbol->usage == SYMBOL_USAGE_NONE) {
        if (!symbol_mark(symbol, &state->error, SYMBOL_USAGE_GLOBAL_VAR))
            return false;
    }

    //TODO expression parsing
    while (state->type != ML_TOKEN_TYPE_EOF && state->type != ML_TOKEN_TYPE_LINE_TERMINATOR) {
        if (!feed_read_next(state))
            return false;
    }

    return true;
}

enum ml_compile_result ml_compile_feed_tokens(struct ml_compile_ctx *ctx,
                                              struct ml_token_ctx *token) {
    struct feed_state state = {
        .ctx = token,
        .error = ML_COMPILE_RESULT_SUCCEED,
        .type = ML_TOKEN_TYPE_EOF,
        .data = {0},
    };

    while (true) {
        if (!feed_skip_space(&state))
            return state.error;

        if (state.type == ML_TOKEN_TYPE_NAME) {
            // may be an assignment statement or a function call
            int sym_idx = 0;
            if (!symbol_ensure(ctx, &state, SYMBOL_USAGE_KEEP, &sym_idx))
                return state.error;
            if (!feed_skip_space(&state))
                return state.error;

            if (state.type == ML_TOKEN_TYPE_ASSIGNMENT) {
                if (!parse_assignment(ctx, &state, sym_idx))
                    return state.error;
            } else {
                return ML_COMPILE_RESULT_SYNTAX_ERROR;
            }
        } else if (state.type == ML_TOKEN_TYPE_FUNCTION) {
            if (!parse_function(ctx, &state))
                return state.error;
        } else if (state.type == ML_TOKEN_TYPE_TAB) {
            //TODO function body
        } else if (state.type == ML_TOKEN_TYPE_COMMENT) {
            // ignore
        } else if (state.type == ML_TOKEN_TYPE_LINE_TERMINATOR) {
            // the end of comments or empty lines
        } else if (state.type == ML_TOKEN_TYPE_EOF) {
            break;
        } else if (state.type == ML_TOKEN_TYPE_ERROR) {
            return ML_COMPILE_RESULT_INVALID_TOKEN;
        } else {
            return ML_COMPILE_RESULT_SYNTAX_ERROR;
        }
    }
    return ML_COMPILE_RESULT_SUCCEED;
}

int ml_compile_get_func_count(struct ml_compile_ctx *ctx) {
    return ctx->func_list.count;
}

const char *ml_compile_get_func_name(struct ml_compile_ctx *ctx, int i) {
    return ctx->symbol_chars.base + ctx->func_list.base[i].name_offset;
}

int ml_compile_get_func_param_count(struct ml_compile_ctx *ctx, int i) {
    return ctx->func_list.base[i].param_count;
}

const char *ml_compile_get_func_param_name(struct ml_compile_ctx *ctx, int i, int j) {
    struct func_entry *func = &ctx->func_list.base[i];
    int offset = ctx->param_offsets.base[func->param_begin + j];
    return ctx->symbol_chars.base + offset;
}


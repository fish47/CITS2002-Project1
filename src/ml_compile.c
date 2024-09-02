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

// we have to use macros because generics are not supported in C language
// more type safety and less lines of code, but losing a little bit of maintainability
ML_LIST_DECLARE(int, int);
ML_LIST_DECLARE(char, str);

struct ml_compile_ctx {
    struct ml_list_str symbol_str_chars;
    struct ml_list_int symbol_str_offsets;
    struct ml_list_int global_var_names;

    void *result_holder;
    size_t result_capacity;
};

static const struct ml_compile_ctx_init_args ml_compile_ctx_init_args_default = {
    .str_chars_capacity = 2048,
    .str_offsets_capacity = 64,
    .global_vars_capacity = 64,
};

static int symbol_find(struct ml_compile_ctx *ctx, const char *name) {
    int low = 0;
    int high = ctx->symbol_str_offsets.count - 1;
    while (low <= high) {
        int mid = (low + high) / 2;
        int offset = ctx->symbol_str_offsets.base[mid];
        int cmp = strcmp(name, ctx->symbol_str_chars.base + offset);
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

static bool symbol_collect(struct ml_compile_ctx *ctx,
                           const char *name, int n, int *new_id) {
    int idx = symbol_find(ctx, name);
    if (idx >= 0)
        return false;

    // strings are zero-terminated and packed into a large chunk of memory
    // so the current end offset is the start offset of the string to be packed
    int offset = ctx->symbol_str_chars.count;
    if (!list_grow_int(&ctx->symbol_str_offsets, 1))
        return false;
    if (!list_fill_str(&ctx->symbol_str_chars, name, n + 1))
        return false;

    // keep offsets sorted based on the strings they point to
    int insert_idx = -idx - 1;
    int move_count = ctx->symbol_str_offsets.count - insert_idx;
    if (move_count) {
        int *insert_addr = ctx->symbol_str_offsets.base + insert_idx;
        memmove(insert_addr + 1, insert_addr, sizeof(int) * move_count);
    }
    ctx->symbol_str_offsets.base[insert_idx] = offset;
    ctx->symbol_str_offsets.count++;

    if (new_id)
        *new_id = offset;
    return true;
}

static const char *symbol_get(struct ml_compile_ctx *ctx, int id) {
    char *chars_base = ctx->symbol_str_chars.base;
    return chars_base + id;
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
    if (!list_init_str(&ctx->symbol_str_chars, p_args->str_chars_capacity))
        goto fail;
    if (!list_init_int(&ctx->symbol_str_offsets, p_args->str_offsets_capacity))
        goto fail;
    if (!list_init_int(&ctx->global_var_names, p_args->global_vars_capacity))
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

    list_uninit_str(&ctx->symbol_str_chars);
    list_uninit_int(&ctx->symbol_str_offsets);
    list_uninit_int(&ctx->global_var_names);
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

int ml_compile_get_global_vars(struct ml_compile_ctx *ctx, const char ***names) {
    size_t capacity = sizeof(const char*) * ctx->global_var_names.count;
    const char **base = ensure_result_capacity(ctx, capacity);
    if (!base)
        return false;

    const int *ids = ctx->global_var_names.base;
    for (int i = 0; i < ctx->global_var_names.count; i++)
        base[i] = symbol_get(ctx, ids[i]);

    *names = base;
    return ctx->global_var_names.count;
}

void ml_compile_feed_token(struct ml_compile_ctx *ctx,
                           enum ml_token_type type,
                           const struct ml_token_data *data) {
    if (type == ML_TOKEN_TYPE_NAME) {
        int new_id = 0;
        if (symbol_collect(ctx, data->buf, data->len, &new_id))
            list_append_int(&ctx->global_var_names, &new_id);
    }
}

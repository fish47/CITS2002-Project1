#pragma once

#include <stdbool.h>

struct ml_compile_ctx;

struct ml_codegen_io_fns {
    int (*write)(void *opaque, char *buffer, int count);
    void (*close)(void *opaque);
};

bool ml_codegen_export_file(struct ml_compile_ctx *compile, const char *path);

void ml_codegen_export_fns(struct ml_compile_ctx *compile, int capacity,
                           void *opaque, const struct ml_codegen_io_fns *fns);

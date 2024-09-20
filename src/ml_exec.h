#pragma once

#include <stdbool.h>

typedef char ml_exec_path[256];

struct ml_exec_run_fns {
    void (*write_stdout)(void *opaque, const char *buf, int n);
    void (*printf_stderr)(void *opaque, const char *fmt, ...);
    bool (*make_temp_path)(void *opaque, ml_exec_path path, const char *suffix);
};

struct ml_exec_ctx {
    const struct ml_exec_run_fns *fns;
    void *opaque;
};

int ml_exec_run_main(struct ml_exec_ctx *ctx, int argc, char *argv[]);

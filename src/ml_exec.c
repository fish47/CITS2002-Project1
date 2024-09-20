#include "ml_exec.h"
#include "ml_token.h"
#include "ml_compile.h"
#include "ml_codegen.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>

enum exec_run_flag {
    EXEC_RUN_FLAG_GRAB_STDOUT = 1,
    EXEC_RUN_FLAG_SUPPRESS_STDERR = 1 << 1,
    EXEC_RUN_FLAG_SEARCH_BIN_PATH = 1 << 2,
};

static void exec_fn_write_stdout(void *opaque, const char *buf, int n) {
    fwrite(buf, 1, n, stdout);
}

static void exec_fn_printf_stderr(void *opaque, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

static bool exec_fn_make_temp_path(void *opaque, ml_exec_path path, const char *suffix) {
    char buf[50];
    int n = snprintf(buf, sizeof(buf), "ml_tmp_%d_%s", getpid(), suffix);
    if (n + 1 > sizeof(buf) || n + 1 > sizeof(ml_exec_path))
        return false;

    strcpy(path, buf);
    return true;
}

static const struct ml_exec_run_fns ml_exec_run_fns_default = {
    .write_stdout = exec_fn_write_stdout,
    .printf_stderr = exec_fn_printf_stderr,
    .make_temp_path = exec_fn_make_temp_path,
};

static bool is_readable_file(struct ml_exec_ctx *ctx, const char *path) {
    struct stat s = {0};
    return (stat(path, &s) == 0) && S_ISREG(s.st_mode) && (access(path, R_OK) == 0);
}

static const char *resolve_compile_result_msg(enum ml_compile_result result) {
    switch (result) {
        case ML_COMPILE_RESULT_SUCCEED:
            return "succeed";
        case ML_COMPILE_RESULT_ERROR_SYNTAX_ERROR:
            return "syntax error";
        case ML_COMPILE_RESULT_ERROR_INVALID_TOKEN:
            return "invalid token";
        case ML_COMPILE_RESULT_ERROR_OUT_OF_MEMORY:
            return "out of memory";
        case ML_COMPILE_RESULT_ERROR_NAME_COLLISION:
            return "name collision";
        case ML_COMPILE_RESULT_ERROR_REDUNDANT_TAB:
            return "redundant tab";
        case ML_COMPILE_RESULT_ERROR_EMPTY_FUNCTION:
            return "empty function";
        case ML_COMPILE_RESULT_ERROR_NESTED_FUNCTION:
            return "nested function";
        case ML_COMPILE_RESULT_ERROR_RETURN_IN_MAIN:
            return "return in main function";
        case ML_COMPILE_RESULT_ERROR_REDUNDANT_RETURN:
            return "redundant return";
    }
    return "unknown error";
}

static bool do_exec_translate_file(struct ml_exec_ctx *ctx, const char *in, const char *src) {
    bool succeed = false;
    struct ml_token_ctx *token = NULL;
    struct ml_compile_ctx *compile = NULL;

    if (!ml_token_ctx_init_file(&token, in)) {
        ctx->fns->printf_stderr(ctx->opaque, "failed to init ml token context\n");
        goto done;
    }

    if (!ml_compile_ctx_init(&compile, NULL)) {
        ctx->fns->printf_stderr(ctx->opaque, "failed to init ml compile context\n");
        goto done;
    }

    enum ml_compile_result result = ml_compile_feed(compile, token);
    if (result != ML_COMPILE_RESULT_SUCCEED) {
        ctx->fns->printf_stderr(ctx->opaque, "! %s\n", resolve_compile_result_msg(result));
        goto done;
    }

    if (!ml_codegen_export_file(compile, src)) {
        ctx->fns->printf_stderr(ctx->opaque, "failed to write ml translation file\n");
        goto done;
    }

    succeed = true;
done:
    ml_token_ctx_uninit(&token);
    ml_compile_ctx_uninit(&compile);
    return succeed;
}

static bool do_run_subprocess(struct ml_exec_ctx *ctx, uint32_t flags,
                              const char *bin, char **argv,
                              const char *error_msg) {
    int fds[2];
    if ((flags & EXEC_RUN_FLAG_GRAB_STDOUT)) {
        if (pipe(fds) != 0) {
            ctx->fns->printf_stderr(ctx->opaque, "failed to create pipe\n");
            return false;
        }
    }

    pid_t pid = fork();
    if (pid == -1) {
        ctx->fns->printf_stderr(ctx->opaque, "failed to fork subprocess\n");
        return false;
    } else if (pid == 0) {
        if (flags & EXEC_RUN_FLAG_GRAB_STDOUT) {
            // close read pipe and redirect stdout to the write pipe
            bool created = (dup2(fds[1], STDOUT_FILENO) != - 1);
            close(fds[0]);
            close(fds[1]);
            if (!created)
                goto fail;
        }

        if (flags & EXEC_RUN_FLAG_SUPPRESS_STDERR)
            close(STDERR_FILENO);

        if (flags & EXEC_RUN_FLAG_SEARCH_BIN_PATH)
            execvp(bin, argv);
        else
            execv(bin, argv);

fail:
        exit(EXIT_FAILURE);
        return false;
    } else {
        if (flags & EXEC_RUN_FLAG_GRAB_STDOUT) {
            close(fds[1]);

            char buffer[1024];
            while (true) {
                int count = read(fds[0], buffer, sizeof(buffer));
                if (count <= 0)
                    break;
                ctx->fns->write_stdout(ctx->opaque, buffer, count);
            }
            close(fds[0]);
        }

        int status = 0;
        if (waitpid(pid, &status, 0) != pid) {
            ctx->fns->printf_stderr(ctx->opaque, "failed to wait subprocess\n");
            return false;
        }

        if (status != 0) {
            ctx->fns->printf_stderr(ctx->opaque, error_msg);
            return false;
        }

        return true;
    }
}

static bool do_exec_compile_file(struct ml_exec_ctx *ctx, char *src, char *exec) {
    char *args[] = {"cc", "-o", exec, src, NULL};
    uint32_t flags = EXEC_RUN_FLAG_SEARCH_BIN_PATH | EXEC_RUN_FLAG_SUPPRESS_STDERR;
    return do_run_subprocess(ctx, flags, "cc", args,
                             "failed to compile ml translation file");
}

static bool do_exec_run_exec_file(struct ml_exec_ctx *ctx, char *exec, char **argv) {
    return do_run_subprocess(ctx, EXEC_RUN_FLAG_GRAB_STDOUT, exec, argv,
                             "failed to run translated executable file");
}

int ml_exec_run_main(struct ml_exec_ctx *ctx, int argc, char *argv[]) {
    int ret = EXIT_FAILURE;
    bool src_written = false;
    bool exec_written = false;

    if (!ctx->fns)
        ctx->fns = &ml_exec_run_fns_default;

    if (argc < 2) {
        ctx->fns->printf_stderr(ctx->opaque, "no input file\n");
        goto fail;
    }

    const char *input_path = argv[1];
    if (!is_readable_file(ctx, input_path)) {
        ctx->fns->printf_stderr(ctx->opaque, "not a readable file\n");
        goto fail;
    }

    ml_exec_path src_path;
    if (!ctx->fns->make_temp_path(ctx->opaque, src_path, "src.c")) {
        ctx->fns->printf_stderr(ctx->opaque, "failed to generate translation file name\n");
        goto fail;
    }

    ml_exec_path exec_path;
    if (!ctx->fns->make_temp_path(ctx->opaque, exec_path, "exec")) {
        ctx->fns->printf_stderr(ctx->opaque, "failed to generate executable file name\n");
        goto fail;
    }

    if (!do_exec_translate_file(ctx, input_path, src_path))
        goto fail;
    src_written = true;

    if (!do_exec_compile_file(ctx, src_path, exec_path))
        goto fail;
    exec_written = true;

    // the first parameter is ml source file path
    // the following parameters should be passed to run the compiled executable
    if (!do_exec_run_exec_file(ctx, exec_path, argv + 1))
        goto fail;

    ret = EXIT_SUCCESS;
fail:
    if (src_written)
        unlink(src_path);
    if (exec_written)
        unlink(exec_path);
    return ret;
}

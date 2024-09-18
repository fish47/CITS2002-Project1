#include <stdlib.h>

#include "ml_token.h"
#include "ml_compile.h"
#include "ml_codegen.h"

int main(int argc, char *argv[]) {
    struct ml_token_ctx *token = NULL;
    struct ml_compile_ctx *compile = NULL;

    if (argc <= 2)
        goto fail;
    if (!ml_token_ctx_init_file(&token, argv[1]))
        goto fail;
    if (!ml_compile_ctx_init(&compile, NULL))
        goto fail;

    ml_compile_feed(compile, token);
    ml_codegen_export_file(compile, argv[2]);

fail:
    ml_token_ctx_uninit(&token);
    ml_compile_ctx_uninit(&compile);
    return EXIT_SUCCESS;
}

#include "ml_exec.h"

int main(int argc, char *argv[]) {
    struct ml_exec_ctx ctx = {0};
    return ml_exec_run_main(&ctx, argc, argv);
}

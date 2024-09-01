#include "ml_memory.h"

#include <stdlib.h>

void *ml_memory_malloc(size_t size) {
    return malloc(size);
}

void *ml_memory_realloc(void *ptr, size_t size) {
    return realloc(ptr, size);
}

void ml_memory_free(void *ptr) {
    free(ptr);
}

#pragma once

#include <stddef.h>

void *ml_memory_malloc(size_t size);

void *ml_memory_realloc(void *ptr, size_t size);

void ml_memory_free(void *ptr);

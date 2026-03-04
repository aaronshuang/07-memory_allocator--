#ifndef WORST_FIT_H
#define WORST_FIT_H

#include <stddef.h>
#include <stdbool.h>

typedef struct worst_fit_block_header {
    size_t size;
    bool is_free;

    struct worst_fit_block_header *next_free;
    struct worst_fit_block_header *prev_free;
} worst_fit_block_header_t;

#define WORST_FIT_HEADER_SIZE sizeof(worst_fit_block_header_t)

int worst_fit_init(size_t initial_size);
void* worst_fit_malloc(size_t size);
void worst_fit_free(void *ptr);

size_t worst_fit_get_total_mapped_memory();
size_t worst_fit_get_currently_allocated_memory();
size_t worst_fit_get_structural_overhead();

#endif
#ifndef BEST_FIT_H
#define BEST_FIT_H

#include <stddef.h>
#include <stdbool.h>

typedef struct best_fit_block_header {
    size_t size;
    bool is_free;

    struct best_fit_block_header *next_free;
    struct best_fit_block_header *prev_free;
} best_fit_block_header_t;

#define BEST_FIT_HEADER_SIZE sizeof(best_fit_block_header_t)

int best_fit_init(size_t initial_size);
void* best_fit_malloc(size_t size);
void best_fit_free(void *ptr);

size_t best_fit_get_total_mapped_memory();
size_t best_fit_get_currently_allocated_memory();
size_t best_fit_get_structural_overhead();

#endif
#ifndef FIRST_FIT_H
#define FIRST_FIT_H

#include <stddef.h>
#include <stdbool.h>

typedef struct block_header {
    size_t size;
    bool is_free;

    struct block_header *next_free;
    struct block_header *prev_free;
} block_header_t;

#define HEADER_SIZE sizeof(block_header_t)

int first_fit_init(size_t initial_size);
void *first_fit_malloc(size_t size);
void first_fit_free(void *ptr);

size_t first_fit_get_total_mapped_memory();
size_t first_fit_get_currently_allocated_memory();
size_t first_fit_get_structural_overhead();

#endif

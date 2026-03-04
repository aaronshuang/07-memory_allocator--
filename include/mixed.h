#ifndef MIXED_H
#define MIXED_H

#include <stddef.h>
#include <stdbool.h>

typedef struct mixed_block_header {
    size_t size;
    bool is_free;

    struct mixed_block_header *next_free;
    struct mixed_block_header *prev_free;
} mixed_block_header_t;

#define MIXED_HEADER_SIZE sizeof(mixed_block_header_t)

int mixed_init(size_t initial_size);
void* mixed_malloc(size_t size);
void mixed_free(void *ptr);

size_t mixed_get_total_mapped_memory();
size_t mixed_get_currently_allocated_memory();
size_t mixed_get_structural_overhead();

#endif
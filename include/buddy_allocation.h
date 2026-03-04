#ifndef BUDDY_ALLOCATION_H
#define BUDDY_ALLOCATION_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#define MIN_ORDER 0
#define MAX_ORDER 26
#define NUM_BINS (MAX_ORDER - MIN_ORDER + 1)

typedef struct buddy_allocation_block_header {
    uint32_t magic;
    size_t order;
    bool is_free;
    struct buddy_allocation_block_header *next_free;
    struct buddy_allocation_block_header *prev_free;
} buddy_allocation_block_header_t;

#define BUDDY_ALLOCATION_HEADER_SIZE sizeof(buddy_allocation_block_header_t)

int buddy_allocation_init(size_t initial_size);
void* buddy_allocation_malloc(size_t size);
void buddy_allocation_free(void *ptr);

size_t buddy_allocation_get_total_mapped_memory();
size_t buddy_allocation_get_currently_allocated_memory();
size_t buddy_allocation_get_structural_overhead();

#endif
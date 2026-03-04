#include <sys/mman.h>
#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>

#include "worst_fit.h"
#define PAGE_SIZE 4096

static worst_fit_block_header_t *free_list_head = NULL;
static worst_fit_block_header_t *alloc_list_head = NULL;

// Stats
static size_t total_memory_mapped = 0;
static size_t currently_allocated = 0;

/**
 * Returns the 4-aligned byte size
 */
static size_t align4(size_t size) {
    return (size + 3) & ~3;
}

/**
 * Validates if a pointer belongs to our allocated list
 * This prevents erroneous frees
 */
static int is_valid_allocated_pointer(worst_fit_block_header_t *target) {
    worst_fit_block_header_t *curr = alloc_list_head;
    while (curr != NULL) {
        if (curr == target) return 1;
        curr = curr->next_free;
    }
    return 0;
}

/**
 * Requests memory via mmap and adds it to the in order sequence of free list
 */
static worst_fit_block_header_t* request_more_memory(size_t required_size) {
    // We must request memory in multiples of the page size
    size_t num_pages = (required_size + PAGE_SIZE - 1) / PAGE_SIZE;
    size_t mmap_size = num_pages * PAGE_SIZE;

    void *mapped_region = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    
    if (mapped_region == MAP_FAILED) {
        return NULL;
    }

    total_memory_mapped += mmap_size;

    // Format this new region as a single large free block
    worst_fit_block_header_t *new_block = (worst_fit_block_header_t *)mapped_region;
    new_block->size = mmap_size - WORST_FIT_HEADER_SIZE;
    new_block->is_free = true;
    new_block->next_free = NULL;
    new_block->prev_free = NULL;

    // Add it in order of free list
    worst_fit_block_header_t *curr = free_list_head;
    worst_fit_block_header_t *prev = NULL;
    while (curr != NULL && curr < new_block) {
        prev = curr;
        curr = curr->next_free;
    }

    new_block->next_free = curr;
    new_block->prev_free = prev;

    if (prev) prev->next_free = new_block;
    else free_list_head = new_block;

    if (curr) curr->prev_free = new_block;

    return new_block;
}

// Expect intial_size to be 4096
int worst_fit_init(size_t initial_size) {
    void *heap_start = mmap(NULL, initial_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (heap_start == MAP_FAILED) {
        fprintf(stderr, "Error: MMAP failed\n");
        return -1;
    }

    total_memory_mapped = initial_size;

    free_list_head = (worst_fit_block_header_t *) heap_start;
    free_list_head->size = initial_size - WORST_FIT_HEADER_SIZE;
    free_list_head->is_free = true;
    free_list_head->next_free = NULL;
    free_list_head->prev_free = NULL;

    return 0;
}

void *worst_fit_malloc(size_t size) {
    if (size <= 0) return NULL;

    size_t aligned_size = align4(size);
    size_t total_required = aligned_size + WORST_FIT_HEADER_SIZE;

    worst_fit_block_header_t *curr = free_list_head;
    worst_fit_block_header_t *worst_block = NULL;

    while (curr != NULL) {
        if (curr->size >= aligned_size) {
            if (worst_block == NULL || curr->size > worst_block->size) {
                worst_block = curr;
            }
        }
        curr = curr->next_free;
    }
    
    curr = worst_block;

    // If no fit, out of memory and attempt to acquire more memory
    if (curr == NULL) {
        curr = request_more_memory(total_required);
        // Actually out of memory
        if (curr == NULL) return NULL;
    }

    // Only split if remainder can hold a header + 4 bytes
    if (curr->size >= (aligned_size + WORST_FIT_HEADER_SIZE + 4)) {
        worst_fit_block_header_t *new_block = (worst_fit_block_header_t *)((char *)curr + WORST_FIT_HEADER_SIZE + aligned_size);
        new_block->size = curr->size - aligned_size - WORST_FIT_HEADER_SIZE;
        new_block->is_free = true;
        
        // Link new block into the free list where curr used to be
        new_block->next_free = curr->next_free;
        new_block->prev_free = curr->prev_free;
        
        if (new_block->prev_free) new_block->prev_free->next_free = new_block;
        if (new_block->next_free) new_block->next_free->prev_free = new_block;
        if (curr == free_list_head) free_list_head = new_block;
        
        curr->size = aligned_size;
    }
    else {
        // Not splitting, just remove curr from the free list entirely
        if (curr->prev_free) curr->prev_free->next_free = curr->next_free;
        if (curr->next_free) curr->next_free->prev_free = curr->prev_free;
        if (curr == free_list_head) free_list_head = curr->next_free;
    }

    curr->is_free = false;

    // Add to allocated list
    curr->next_free = alloc_list_head;
    curr->prev_free = NULL;
    if (alloc_list_head) alloc_list_head->prev_free = curr;
    alloc_list_head = curr;

    // Update stats
    currently_allocated += curr->size + WORST_FIT_HEADER_SIZE;

    // Return pointer
    return (void *)((char *)curr + WORST_FIT_HEADER_SIZE);
}

void worst_fit_free(void *ptr) {
    if (ptr == NULL) return;

    worst_fit_block_header_t *header = (worst_fit_block_header_t *)((char *)ptr - WORST_FIT_HEADER_SIZE);

    // Check if our ptr can even be freed
    // if (!is_valid_allocated_pointer(header) || header->is_free != 0) {
    //     fprintf(stderr, "Error: Invalid or double free detected.\n");
    //     return;
    // }

    // Remove from Allocated List
    if (header->prev_free) header->prev_free->next_free = header->next_free;
    if (header->next_free) header->next_free->prev_free = header->prev_free;
    if (header == alloc_list_head) alloc_list_head = header->next_free;

    // Update stats
    currently_allocated -= (header->size + WORST_FIT_HEADER_SIZE);

    // Re-insert
    header->is_free = true;
    
    worst_fit_block_header_t *curr = free_list_head;
    worst_fit_block_header_t *prev = NULL;
    
    while (curr != NULL && curr < header) {
        prev = curr;
        curr = curr->next_free;
    }

    // Insert between prev and curr
    header->next_free = curr;
    header->prev_free = prev;

    if (prev) prev->next_free = header;
    else free_list_head = header;

    if (curr) curr->prev_free = header;

    // Coalesce with next physical block
    if (curr && (worst_fit_block_header_t *)((char *)header + WORST_FIT_HEADER_SIZE + header->size) == curr) {
        header->size += WORST_FIT_HEADER_SIZE + curr->size;
        header->next_free = curr->next_free;
        if (curr->next_free) curr->next_free->prev_free = header;
    }

    // Coalesce with previous physical block
    if (prev && (worst_fit_block_header_t *)((char *)prev + WORST_FIT_HEADER_SIZE + prev->size) == header) {
        prev->size += WORST_FIT_HEADER_SIZE + header->size;
        prev->next_free = header->next_free;
        if (header->next_free) header->next_free->prev_free = prev;
    }
}

/**
 * Returns the total bytes requested by OS
 */
size_t worst_fit_get_total_mapped_memory() {
    return total_memory_mapped;
}

/**
 * Returns the total bytes currently requested by the user
 */
size_t worst_fit_get_currently_allocated_memory() {
    return currently_allocated;
}

/**
 * Calculates the total overhead of all headers
 */
size_t worst_fit_get_structural_overhead() {
    size_t overhead = 0;
    worst_fit_block_header_t *curr = alloc_list_head;
    while (curr != NULL) {
        overhead += WORST_FIT_HEADER_SIZE;
        curr = curr->next_free; // Traversing the allocated list
    }

    // Add the free list headers as well
    curr = free_list_head;
    while(curr != NULL) {
        overhead += WORST_FIT_HEADER_SIZE;
        curr = curr->next_free;
    }
    
    return overhead;
}
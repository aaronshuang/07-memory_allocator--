#include <sys/mman.h>
#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>

#include "buddy_allocation.h"
#define PAGE_SIZE 4096

static size_t total_memory_mapped = 0;
static size_t currently_allocated = 0;
static size_t num_regions = 0;
static size_t num_blocks = 0;

static buddy_allocation_block_header_t *free_bins[NUM_BINS];
void *heap_start;

// Used to handle where the mmap blocks are
typedef struct mapped_region {
    char *base_addr;
    size_t size;
    struct mapped_region *next;
} mapped_region_t;

static mapped_region_t *region_list_head = NULL;

/*
Find the smallest order greater than size
*/
static int get_bin_index(size_t size) {
    if (size <= (1 << MIN_ORDER)) return 0;
    int order = 64 - __builtin_clzll((unsigned long long)(size - 1));
    return order - MIN_ORDER;
}

// Returns true on success, false on failure
static bool request_more_memory(size_t required_size) {
    size_t mmap_size = PAGE_SIZE;
    while (mmap_size < required_size) {
        mmap_size *= 2;
    }

    int index = get_bin_index(mmap_size);
    if (index < 0 || index >= NUM_BINS) {
        return false; // Exceeds max allowable order
    }

    void *mapped_region = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (mapped_region == MAP_FAILED) {
        return false;
    }

    total_memory_mapped += mmap_size;
    num_regions++;

    buddy_allocation_block_header_t *root = (buddy_allocation_block_header_t *)mapped_region;
    root->is_free = true;
    root->order = index + MIN_ORDER;
    root->magic = 0;
    
    // Insert into free bins
    root->prev_free = NULL;
    root->next_free = free_bins[index];
    if (free_bins[index]) {
        free_bins[index]->prev_free = root;
    }
    free_bins[index] = root;

    mapped_region_t *tracker = (mapped_region_t *)buddy_allocation_malloc(sizeof(mapped_region_t));
    if (!tracker) {
        return false; 
    }

    tracker->base_addr = (char *)mapped_region;
    tracker->size = mmap_size;
    tracker->next = region_list_head;
    region_list_head = tracker;

    return true;
}

int buddy_allocation_init(size_t initial_size) {
    mapped_region_t *curr = region_list_head;
    while (curr != NULL) {
        mapped_region_t *next = curr->next;
        munmap(curr->base_addr, curr->size);
        curr = next;
    }

    for (int i = 0; i < NUM_BINS; i++) {
        free_bins[i] = NULL;
    }

    total_memory_mapped = 0;
    currently_allocated = 0;
    num_regions = 0;
    
    region_list_head = NULL;

    if (!request_more_memory(initial_size)) {
        fprintf(stderr, "Error: Initial MMAP failed\n");
        return -1;
    }
    return 0;
}

void *buddy_allocation_malloc(size_t size) {
    if (size == 0) return NULL;

    int target_index = get_bin_index(size + BUDDY_ALLOCATION_HEADER_SIZE);
    if (target_index >= NUM_BINS) {
        // Exceeds maximum allocation limit
        return NULL;
    }
    
    int current_index = target_index;
    while (current_index < NUM_BINS && free_bins[current_index] == NULL) {
        current_index++;
    }

    if (current_index == NUM_BINS) {
        size_t needed_size = 1ULL << (target_index + MIN_ORDER);
        
        if (!request_more_memory(needed_size)) {
            return NULL; // OS is out of memory
        }

        current_index = target_index;
        while (current_index < NUM_BINS && free_bins[current_index] == NULL) {
            current_index++;
        }

        if (current_index == NUM_BINS) return NULL;
    }

    while (current_index > target_index) {
        buddy_allocation_block_header_t *block = free_bins[current_index];
        free_bins[current_index] = free_bins[current_index]->next_free;

        if (free_bins[current_index]) {
            free_bins[current_index]->prev_free = NULL;
        }

        current_index--;

        size_t half = 1ULL << (current_index + MIN_ORDER);
        buddy_allocation_block_header_t *buddy = 
            (buddy_allocation_block_header_t *)((char *)block + half);

        num_blocks++;

        block->order = current_index + MIN_ORDER;
        buddy->order = current_index + MIN_ORDER;
        buddy->is_free = true;

        buddy->next_free = block;
        buddy->prev_free = NULL;
        block->prev_free = buddy;
        block->next_free = free_bins[current_index];
        if (free_bins[current_index]) free_bins[current_index]->prev_free = block;
        free_bins[current_index] = buddy;
    }

    buddy_allocation_block_header_t *allocated_block = free_bins[target_index];
    free_bins[target_index] = allocated_block->next_free;
    if (allocated_block->next_free) allocated_block->next_free->prev_free = NULL;

    allocated_block->is_free = false;
    allocated_block->real_size = size;
    allocated_block->magic = 0xDEADBEEF; 
    
    currently_allocated += size;

    return (void *)(allocated_block + 1);
}

void buddy_allocation_free(void *ptr) {
    if (!ptr) return;

    buddy_allocation_block_header_t *block = (buddy_allocation_block_header_t *)ptr - 1;

    // Validate the pointer in O(1)
    if (block->magic != 0xDEADBEEF || block->is_free) {
        fprintf(stderr, "Double free or invalid pointer detected!\n");
        return;
    }

    mapped_region_t *region = region_list_head;
    while (region != NULL) {
        if ((char *)block >= region->base_addr && 
            (char *)block < region->base_addr + region->size) {
            break;
        }
        region = region->next;
    }

    if (region == NULL) {
        fprintf(stderr, "Error: Pointer does not belong to any mapped region.\n");
        return;
    }

    block->is_free = true;
    block->magic = 0; // Clear magic number
    currently_allocated -= block->real_size;

    // Iterative Coalescing
    while (block->order < MAX_ORDER) {
        size_t block_size = 1ULL << block->order;
        
        // Calculate relative offset to find the buddy safely
        uintptr_t relative_offset = (char *)block - (char *)region->base_addr; 
        uintptr_t buddy_offset = relative_offset ^ block_size;

        if (buddy_offset >= region->size) {
            break;
        }
        
        // Use heap_start instead of the undefined heap_base
        buddy_allocation_block_header_t *buddy = 
            (buddy_allocation_block_header_t *)((char *)region->base_addr + buddy_offset);

        // Check if buddy can be merged
        if (!buddy->is_free || buddy->order != block->order) {
            break; // Buddy is allocated or has been split
        }

        // Buddy is free! Remove it from its current free_bin
        int bin_idx = block->order - MIN_ORDER;
        if (buddy->prev_free) buddy->prev_free->next_free = buddy->next_free;
        else free_bins[bin_idx] = buddy->next_free;
        
        if (buddy->next_free) buddy->next_free->prev_free = buddy->prev_free;

        // The merged block starts at the lower of the two addresses
        if (buddy < block) {
            block = buddy;
        }

        num_blocks--;
        // Increment the order and try to merge again
        block->order++;
    }

    // Insert the fully coalesced block into the correct bin
    int final_idx = block->order - MIN_ORDER;
    block->next_free = free_bins[final_idx];
    block->prev_free = NULL;
    if (free_bins[final_idx]) free_bins[final_idx]->prev_free = block;
    free_bins[final_idx] = block;
}

size_t buddy_allocation_get_total_mapped_memory() {
    return total_memory_mapped;
}
size_t buddy_allocation_get_currently_allocated_memory() {
    return currently_allocated;
}
size_t buddy_allocation_get_structural_overhead() {
    size_t block_headers = num_blocks * sizeof(buddy_allocation_block_header_t);
    return block_headers + (num_regions * sizeof(mapped_region_t));
}

 
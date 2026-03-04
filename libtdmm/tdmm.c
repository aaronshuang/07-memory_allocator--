#include "tdmm.h"
#include "first_fit.h"
#include "best_fit.h"
#include "worst_fit.h"
#include "buddy_allocation.h"
#include "mixed.h"

static alloc_strat_e current_strat;

size_t t_get_total_mapped_memory() {
    if (current_strat == FIRST_FIT) return first_fit_get_total_mapped_memory();
    if (current_strat == BEST_FIT) return best_fit_get_total_mapped_memory();
	if (current_strat == BUDDY) return buddy_allocation_get_total_mapped_memory();
    if (current_strat == MIXED) return mixed_get_total_mapped_memory();
    return worst_fit_get_total_mapped_memory();
}

size_t t_get_currently_allocated_memory() {
    if (current_strat == FIRST_FIT) return first_fit_get_currently_allocated_memory();
    if (current_strat == BEST_FIT) return best_fit_get_currently_allocated_memory();
	if (current_strat == BUDDY) return buddy_allocation_get_currently_allocated_memory();
    if (current_strat == MIXED) return mixed_get_currently_allocated_memory();
    return worst_fit_get_currently_allocated_memory();
}

size_t t_get_structural_overhead() {
    if (current_strat == FIRST_FIT) return first_fit_get_structural_overhead();
    if (current_strat == BEST_FIT) return best_fit_get_structural_overhead();
	if (current_strat == BUDDY) return buddy_allocation_get_structural_overhead();
    if (current_strat == MIXED) return mixed_get_structural_overhead();
    return worst_fit_get_structural_overhead();
}

void t_init(alloc_strat_e strat) {
	current_strat = strat;
    
    // Initializing with 1 page
    size_t initial_size = 4096; 
    
    if (strat == FIRST_FIT) first_fit_init(initial_size);
    else if (strat == BEST_FIT) best_fit_init(initial_size);
    else if (strat == WORST_FIT) worst_fit_init(initial_size);
	else if (strat == BUDDY) buddy_allocation_init(initial_size);
    else if (strat == MIXED) mixed_init(initial_size);
}

void *t_malloc(size_t size) {
    if (current_strat == FIRST_FIT) return first_fit_malloc(size);
    if (current_strat == BEST_FIT) return best_fit_malloc(size);
    if (current_strat == WORST_FIT) return worst_fit_malloc(size);
	if (current_strat == BUDDY) return buddy_allocation_malloc(size);
    if (current_strat == MIXED) return mixed_malloc(size);
    return NULL;
}

void t_free(void *ptr) {
    if (current_strat == FIRST_FIT) first_fit_free(ptr);
    else if (current_strat == BEST_FIT) best_fit_free(ptr);
    else if (current_strat == WORST_FIT) worst_fit_free(ptr);
	else if (current_strat == BUDDY) return buddy_allocation_free(ptr);
    else if (current_strat == MIXED) return mixed_free(ptr);
}

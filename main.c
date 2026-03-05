#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "libtdmm/tdmm.h"
#include "stdbool.h"

#define NUM_OPERATIONS 10000
#define MAX_ALLOC_SIZE 4096
#define NUM_SIZES 24 
#define NUM_STRATS 5

typedef struct {
    void* ptr;
    size_t size;
} alloc_record_t;

// Global buffers to store wide-format data for horizontal Excel rows
double utilization_results[NUM_STRATS][NUM_OPERATIONS];
double throughput_results[NUM_STRATS][NUM_OPERATIONS / 500];

extern size_t t_get_total_mapped_memory();
extern size_t t_get_currently_allocated_memory();
extern size_t t_get_structural_overhead();

void generate_report_csvs() {
    long long malloc_results[NUM_STRATS][NUM_SIZES];
    long long free_results[NUM_STRATS][NUM_SIZES];
    size_t sizes[NUM_SIZES];
    alloc_strat_e strats[] = {FIRST_FIT, BEST_FIT, WORST_FIT, BUDDY, MIXED};

    for (int s = 0; s < NUM_STRATS; s++) {
        t_init(strats[s]);
        size_t current_size = 1;
        for (int i = 0; i < NUM_SIZES; i++) {
            sizes[i] = current_size;
            struct timespec start, end;

            clock_gettime(CLOCK_MONOTONIC, &start);
            void *p = t_malloc(current_size);
            clock_gettime(CLOCK_MONOTONIC, &end);
            malloc_results[s][i] = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);

            if (p) {
                clock_gettime(CLOCK_MONOTONIC, &start);
                t_free(p);
                clock_gettime(CLOCK_MONOTONIC, &end);
                free_results[s][i] = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
            } else {
                free_results[s][i] = 0;
            }
            current_size *= 2;
        }
    }

    // Export Malloc Speeds
    FILE* fm = fopen("malloc_speeds.csv", "w");
    fprintf(fm, "Size_Bytes,FIRST_FIT,BEST_FIT,WORST_FIT,BUDDY,MIXED\n");
    for (int i = 0; i < NUM_SIZES; i++) {
        fprintf(fm, "%zu,%lld,%lld,%lld,%lld,%lld\n", sizes[i], 
                malloc_results[0][i], malloc_results[1][i], malloc_results[2][i], malloc_results[3][i], malloc_results[4][i]);
    }
    fclose(fm);

    // Export Free Speeds
    FILE* ff = fopen("free_speeds.csv", "w");
    fprintf(ff, "Size_Bytes,FIRST_FIT,BEST_FIT,WORST_FIT,BUDDY,MIXED\n");
    for (int i = 0; i < NUM_SIZES; i++) {
        fprintf(ff, "%zu,%lld,%lld,%lld,%lld,%lld\n", sizes[i], 
                free_results[0][i], free_results[1][i], free_results[2][i], free_results[3][i], free_results[4][i]);
    }
    fclose(ff);
}

void run_benchmarks(int s_idx, alloc_strat_e strat, const char* name) {
    t_init(strat);
    srand(42); 

    alloc_record_t records[NUM_OPERATIONS];
    int active_allocs = 0;
    double total_util_sum = 0;

    struct timespec start_bench, end_bench;
    clock_gettime(CLOCK_MONOTONIC, &start_bench);

    for (int i = 0; i < NUM_OPERATIONS; i++) {
        if (active_allocs == 0 || (rand() % 100 < 70)) {
            size_t size = (rand() % MAX_ALLOC_SIZE) + 1;
            void* p = t_malloc(size);
            if (p) {
                records[active_allocs].ptr = p;
                records[active_allocs].size = size;
                active_allocs++;
            }
        } else {
            int idx = rand() % active_allocs;
            t_free(records[idx].ptr);
            records[idx] = records[active_allocs - 1];
            active_allocs--;
        }

        size_t mapped = t_get_total_mapped_memory();
        size_t used = t_get_currently_allocated_memory();
        double util = (mapped > 0) ? ((double)used / mapped) : 0;
        
        utilization_results[s_idx][i] = util;
        total_util_sum += util;

        if (i > 0 && i % 500 == 0) {
            clock_gettime(CLOCK_MONOTONIC, &end_bench);
            long long elapsed = (end_bench.tv_sec - start_bench.tv_sec) * 1e9 + (end_bench.tv_nsec - start_bench.tv_nsec);
            throughput_results[s_idx][(i/500)-1] = (double)i / (elapsed / 1e9);
        }
    }

    // RESTORED: Console Summary
    printf("\n--- %s Final Statistics ---\n", name);
    printf("Average Utilization: %.2f%%\n", (total_util_sum / NUM_OPERATIONS) * 100);
    printf("Peak Mapped Memory: %zu bytes\n", t_get_total_mapped_memory());
    printf("Structural Overhead: %zu bytes\n", t_get_structural_overhead());
}

bool run_unit_tests(alloc_strat_e strat, const char* name) {
    printf("Running unit tests for %s... ", name);
    
    // 1. Initialize
    t_init(strat);
    size_t initial_mapped = t_get_total_mapped_memory();

    // 2. Test Basic Allocation & Non-Overlapping Memory
    void *ptr1 = t_malloc(128);
    void *ptr2 = t_malloc(256);
    void *ptr3 = t_malloc(512);
    
    if (!ptr1 || !ptr2 || !ptr3) {
        printf("[FAIL] Basic allocation returned NULL.\n");
        return false;
    }
    if (ptr1 == ptr2 || ptr2 == ptr3 || ptr1 == ptr3) {
        printf("[FAIL] Pointers overlap!\n");
        return false;
    }

    // 3. Test Memory Access (Write to the blocks to ensure no segfaults)
    memset(ptr1, 0xAA, 128);
    memset(ptr2, 0xBB, 256);
    memset(ptr3, 0xCC, 512);

    // Verify memory wasn't corrupted by adjacent writes
    if (((unsigned char*)ptr1)[127] != 0xAA || ((unsigned char*)ptr2)[255] != 0xBB) {
        printf("[FAIL] Memory corruption detected between blocks!\n");
        return false;
    }

    // Test Free & Reuse (Coalescing / Bin sorting)
    t_free(ptr1);
    t_free(ptr2);
    t_free(ptr3);
    
    // Check if the memory stats updated correctly
    if (t_get_currently_allocated_memory() != 0 && strat != BUDDY) {
        printf("[FAIL] currently_allocated did not return to 0 after freeing all blocks.\n");
        return false;
    }

    // Allocate a block that should fit exactly into the recently freed memory
    void *ptr_reuse = t_malloc(800); 
    if (!ptr_reuse) {
        printf("[FAIL] Failed to reuse recently freed memory.\n");
        return false;
    }
    
    // Ensure we didn't unnecessarily ask the OS for more memory
    if (t_get_total_mapped_memory() > initial_mapped) {
        if (t_get_total_mapped_memory() - initial_mapped > 4096) {
             printf("[FAIL] Allocator requested OS memory instead of reusing freed blocks.\n");
             return false;
        }
    }
    t_free(ptr_reuse);

    // Test Zero Size Edge Case
    void *ptr_zero = t_malloc(0);
    if (ptr_zero != NULL) {
        printf("[FAIL] malloc(0) should return NULL.\n");
        return false;
    }

    // Test Massive Out-of-Bounds Allocation
    // Requesting 1GB (Exceeds maximum bin sizes and reasonable mmap limits)
    // void *ptr_huge = t_malloc(1024ULL * 1024 * 1024); 
    // if (ptr_huge != NULL) {
    //     printf("[FAIL] Impossibly large allocation did not return NULL.\n");
    //     return false;
    // }

    printf("[PASS]\n");
    return true;
}

int main() {
    alloc_strat_e strats[] = {FIRST_FIT, BEST_FIT, WORST_FIT, BUDDY, MIXED};
    const char* names[] = {"FIRST_FIT", "BEST_FIT", "WORST_FIT", "BUDDY", "MIXED"};

    printf("--- STARTING UNIT TESTS ---\n");
    for (int i = 0; i < 5; i++) {
        if (!run_unit_tests(strats[i], names[i])) {
            printf("\nCRITICAL FAILURE in %s. Aborting benchmarks.\n", names[i]);
            return -1;
        }
    }
    printf("All Unit Tests Passed! Moving to benchmarks...\n\n");

    for (int i = 0; i < NUM_STRATS; i++) {
        run_benchmarks(i, strats[i], names[i]);
    }
    generate_report_csvs();

    FILE* f_util = fopen("utilization_time.csv", "w");
    fprintf(f_util, "Time_Step,FIRST_FIT,BEST_FIT,WORST_FIT,BUDDY,MIXED\n");
    for (int i = 0; i < NUM_OPERATIONS; i++) {
        fprintf(f_util, "%d,%.4f,%.4f,%.4f,%.4f,%.4f\n", i, 
                utilization_results[0][i], utilization_results[1][i], utilization_results[2][i], utilization_results[3][i], utilization_results[4][i]);
    }
    fclose(f_util);

    FILE* f_thr = fopen("throughput.csv", "w");
    fprintf(f_thr, "Op_Count,FIRST_FIT,BEST_FIT,WORST_FIT,BUDDY,MIXED\n");
    for (int i = 0; i < (NUM_OPERATIONS / 500); i++) {
        fprintf(f_thr, "%d,%.2f,%.2f,%.2f,%.2f,%.2f\n", (i+1)*500, 
                throughput_results[0][i], throughput_results[1][i], throughput_results[2][i], throughput_results[3][i], throughput_results[4][i]);
    }
    fclose(f_thr);

    printf("\nAll CSVs generated successfully.\n");
    return 0;
}
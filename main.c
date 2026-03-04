#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "libtdmm/tdmm.h"

#define NUM_OPERATIONS 10000
#define MAX_ALLOC_SIZE 4096
#define NUM_SIZES 24 // 1B to 8MB in powers of 2

// External stats helpers from your tdmm.c
extern size_t t_get_total_mapped_memory();
extern size_t t_get_currently_allocated_memory();
extern size_t t_get_structural_overhead();

typedef struct {
    void* ptr;
    size_t size;
} alloc_record_t;

/**
 * Requirement: Speed vs. Size (Log Scale 1B to 8MB)
 */
void generate_report_csvs() {
    // Arrays to store results for the wide-format output
    long long malloc_results[5][NUM_SIZES];
    long long free_results[5][NUM_SIZES];
    size_t sizes[NUM_SIZES];

    alloc_strat_e strats[] = {FIRST_FIT, BEST_FIT, WORST_FIT, BUDDY, MIXED};

    for (int s = 0; s < 5; s++) {
        t_init(strats[s]);
        
        size_t current_size = 1;
        for (int i = 0; i < NUM_SIZES; i++) {
            sizes[i] = current_size;
            struct timespec start, end;

            // 1. Benchmark Malloc
            clock_gettime(CLOCK_MONOTONIC, &start);
            void *p = t_malloc(current_size);
            clock_gettime(CLOCK_MONOTONIC, &end);
            
            long long m_nsec = (end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec);
            malloc_results[s][i] = (m_nsec <= 0) ? 1 : m_nsec;

            // 2. Benchmark Free
            if (p) {
                clock_gettime(CLOCK_MONOTONIC, &start);
                t_free(p);
                clock_gettime(CLOCK_MONOTONIC, &end);
                
                long long f_nsec = (end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec);
                free_results[s][i] = (f_nsec <= 0) ? 1 : f_nsec;
            } else {
                free_results[s][i] = 1;
            }
            current_size *= 2;
        }
    }

    // Write Malloc CSV
    FILE* fm = fopen("malloc_speeds.csv", "w");
    fprintf(fm, "Size_Bytes,FIRST_FIT,BEST_FIT,WORST_FIT,BUDDY\n");
    for (int i = 0; i < NUM_SIZES; i++) {
        fprintf(fm, "%zu,%lld,%lld,%lld,%lld,%lld\n", sizes[i], malloc_results[0][i], malloc_results[1][i], malloc_results[2][i], malloc_results[3][i], malloc_results[4][i]);
    }
    fclose(fm);

    // Write Free CSV
    FILE* ff = fopen("free_speeds.csv", "w");
    fprintf(ff, "Size_Bytes,FIRST_FIT,BEST_FIT,WORST_FIT,BUDDY\n");
    for (int i = 0; i < NUM_SIZES; i++) {
        fprintf(ff, "%zu,%lld,%lld,%lld,%lld,%lld\n", sizes[i], free_results[0][i], free_results[1][i], free_results[2][i], free_results[3][i], free_results[4][i]);
    }
    fclose(ff);
}

/**
 * Requirement: %age Utilization over time & Average Utilization
 */
void run_comparative_benchmarks(alloc_strat_e strat, const char* name, FILE* util_f, FILE* throughput_f) {
    t_init(strat);
    srand(42); 

    alloc_record_t records[NUM_OPERATIONS];
    int active_allocs = 0;
    double total_util_sum = 0;

    struct timespec start_bench, end_bench;
    clock_gettime(CLOCK_MONOTONIC, &start_bench);

    for (int i = 0; i < NUM_OPERATIONS; i++) {
        // Decide Operation
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

        // 1. Log Utilization Over Time
        size_t mapped = t_get_total_mapped_memory();
        size_t used = t_get_currently_allocated_memory();
        double util = (mapped > 0) ? ((double)used / mapped) : 0;
        total_util_sum += util;
        fprintf(util_f, "%s,%d,%.4f\n", name, i, util);

        // 2. Log Throughput every 500 ops
        if (i > 0 && i % 500 == 0) {
            clock_gettime(CLOCK_MONOTONIC, &end_bench);
            long long elapsed = (end_bench.tv_sec - start_bench.tv_sec) * 1000000000LL + (end_bench.tv_nsec - start_bench.tv_nsec);
            double throughput = (double)i / (elapsed / 1e9);
            fprintf(throughput_f, "%s,%d,%.2f\n", name, i, throughput);
        }
    }

    // Final console summary for Structural Overhead
    printf("\n--- %s Summary ---\n", name);
    printf("Average Utilization: %.2f%%\n", (total_util_sum / NUM_OPERATIONS) * 100);
    printf("Final Structural Overhead: %zu bytes\n", t_get_structural_overhead());
}

int main() {
    // Open CSV files for Excel export
    FILE* f_speed = fopen("speed_size.csv", "w");
    FILE* f_util = fopen("utilization_time.csv", "w");
    FILE* f_thr = fopen("throughput.csv", "w");

    fprintf(f_speed, "Strategy,Size_Bytes,Time_Nsec\n");
    fprintf(f_util, "Strategy,Time_Step,Utilization_Fraction\n");
    fprintf(f_thr, "Strategy,Op_Count,Ops_Per_Sec\n");

    alloc_strat_e strats[] = {FIRST_FIT, BEST_FIT, WORST_FIT, BUDDY, MIXED};
    const char* names[] = {"FIRST_FIT", "BEST_FIT", "WORST_FIT", "BUDDY", "MIXED"};

    for (int i = 0; i < 5; i++) {
        // generate_speed_data(strats[i], names[i], f_speed);
        run_comparative_benchmarks(strats[i], names[i], f_util, f_thr);
    }
    generate_report_csvs();

    fclose(f_speed); fclose(f_util); fclose(f_thr);
    printf("\nCSVs generated: speed_size.csv, utilization_time.csv, throughput.csv\n");
    return 0;
}
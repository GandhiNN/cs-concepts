/**
 * cpu_clock_demo.c
 * 
 * Demonstrates that every CPU operation has a measurable cost in nanoseconds.
 * This is the absolute foundation of understanding HFT latency:
 * if a single addition takes ~1ns, and you need to make a trading decision
 * in 1 microsecond (1000ns), you can only afford ~1000 operations.
 * 
 * KEY TECHNIQUE: We use "dependency chains" - each operation's result feeds
 * into the next iteration. This prevents:
 *   1. The compiler from eliminating "dead" work
 *   2. The CPU from executing iterations in parallel (out-of-order execution)
 * This measures the TRUE LATENCY of each operation.
 * 
 * Compile: gcc -O0 -o cpu_clock_demo cpu_clock_demo.c
 *      (-O0 disables optimizations so the compiler does not remove our operations)
 * 
 * Run: ./cpu_clock_demo
 */

#include <stdio.h>
#include <stdint.h>
#include <time.h>

#define ITERATIONS 100000000 /* 100 million iterations for measurable results */

/**
 * get_nanos: returns current time in nanoseconds using CLOCK_MONOTONIC
 * (monotonic = never goes backwards, unlike wall clock)
 */
static inline uint64_t get_nanos(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/**
 * "escape" prevents the compiler from optimizing away a computed value.
 * It tells the compiler "this value is used externally" without actually
 * doing anything at runtime.
 */
static void escape(void *p) {
    __asm__ volatile("" : : "g"(p): "memory");
}

/**
 * Measure loop baseline: increment only (the minimum cost per iteration
 * This gives us a baseline to understand relative costs.
 */
double measure_baseline(void) {
    int x = 1;
    uint64_t start = get_nanos();
    for (int i = 0; i < ITERATIONS; i++) {
        x += 1; /* dependency chain: x feeds back into next iteration */
    }
    uint64_t end = get_nanos();
    escape(&x);
    return (double)(end - start) / ITERATIONS;
}

/**
 * Integer addition with dependency chain.
 * Each iteration depends on the previous result.
 */
double measure_int_add(void) {
    int x = 1;
    uint64_t start = get_nanos();
    for (int i = 0; i < ITERATIONS; i++) {
        x += 7; /* result of this add is input to next add */
    }
    uint64_t end = get_nanos();
    escape(&x);
    return (double)(end - start) / ITERATIONS;
}

/**
 * Integer multiplication with dependency chain.
 * We use small multiplier and mask to prevent overflow while keeping dependency.
 */
double measure_int_mul(void) {
    int x = 1;
    uint64_t start = get_nanos();
    for (int i = 0; i < ITERATIONS; i++) {
        x = x * 13 + 1; /* multiply then add to keep values bounded */
        x = x & 0x7FFFFFFF; /* prevent overflow, but maintain chain */
    }
    uint64_t end = get_nanos();
    escape(&x);
    return (double)(end - start) / ITERATIONS;
}

/**
 * Integer division with dependency chain.
 * Division should be noticeably slower than addition.
 */
double measure_int_div(void) {
    int x = 100000000;
    uint64_t start = get_nanos();
    for (int i = 0; i < ITERATIONS; i++) {
        x = x / 3 + 1000000; /* divide, then add to keep value large */
    }
    uint64_t end = get_nanos();
    escape(&x);
    return (double)(end - start) / ITERATIONS;
}

/**
 * Float multiplication with dependency chain.
 */
double measure_float_mul(void) {
    double x = 1.5;
    uint64_t start = get_nanos();
    for (int i = 0; i < ITERATIONS; i++) {
        x = x * 0.9999; /* multiply by <1 to prevent infinity */
        x = x + 0.0001; /* add small value to prevent convergence to 0 */
    }
    uint64_t end = get_nanos();
    escape(&x);
    return (double)(end - start) / ITERATIONS;
}

/**
 * Float division with dependency chain.
 */
double measure_float_div(void) {
    double x = 1000000.0;
    uint64_t start = get_nanos();
    for (int i = 0; i < ITERATIONS; i++) {
        x = x / 1.0001; /* divide by near-1 to keep value stable */
        x = x + 0.01; /* prevent convergence to 0 */
    }
    uint64_t end = get_nanos();
    escape(&x);
    return (double)(end - start) / ITERATIONS;
}

/**
 * Predictable branch: always takes the same path.
 * CPU's branch predictor learns the pattern perfectly.
 */
double measure_branch_predictable(void) {
    int x = 0;
    uint64_t start = get_nanos();
    for (int i = 0; i < ITERATIONS; i++) {
        if (i >= 0) { /* always true = predictable */
            x += 1;
        } else {
            x += 2;
        }
    }
    uint64_t end = get_nanos();
    escape(&x);
    return (double)(end - start) / ITERATIONS;
}

/**
 * Unpredictable branch: alternates randomly using bit manipulation.
 * Defeats the branch predictor, causing pipeline flushes (~10-20 cycle penalty)
 */
double measure_branch_unpredictable(void) {
    int x = 0;
    /* Simple pseudo-random: use middle bits of iteration counter mixed with
        a changing value to create unpredictable pattern */
    unsigned int rng = 12345;
    uint64_t start = get_nanos();
    for (int i = 0; i < ITERATIONS; i++) {
        rng = rng * 1103515245 + 12345; /* LCG random number generator */
        if ((rng >> 16) & 1) {
            x += 1;
        } else {
            x += 2;
        }
    }
    uint64_t end = get_nanos();
    escape(&x);
    return (double)(end - start) / ITERATIONS;
}

/**
 * Function call overhead: forces a real call (no inlining)
 */
volatile int global_sink;

__attribute__((noinline)) /* prevent inlining so we measure actual call */
int add_one(int x) {
    return x + 1;
}

double measure_function_call(void) {
    int x = 0;
    uint64_t start = get_nanos();
    for (int i = 0; i < ITERATIONS; i++) {
        x = add_one(x); /* dependency chain through function call */
    }
    uint64_t end = get_nanos();
    escape(&x);
    return (double)(end - start) / ITERATIONS;
}

int main(void) {
    printf("=== CPU Clock Demo: Cost of Basic Operations ===\n");
    printf("Measuring LATENCY using dependency chains (each op depends on previous)\n");
    printf("Iterations per test: %d (100 million)\n", ITERATIONS);

    /* Run all measurements */
    double t_base = measure_baseline();
    double t_add = measure_int_add();
    double t_mul = measure_int_mul();
    double t_div = measure_int_div();
    double t_fmul = measure_float_mul();
    double t_fdiv = measure_float_div();
    double t_branch_p = measure_branch_predictable();
    double t_branch_u = measure_branch_unpredictable();
    double t_call = measure_function_call();

    printf("--- Baseline ---\n");
    printf("Loop + increment (baseline): %6.2f ns/op\n\n", t_base);

    printf("--- Arithmetic Operations ---\n");
    printf("Integer addition:   %6.2f ns\n", t_add);
    printf("Integer multiplication: %6.2f ns\n", t_mul);
    printf("Integer division: %6.2f ns (division is expensive!)\n", t_div);
    printf("Float multiplication: %6.2f ns\n", t_fmul);
    printf("Float division: %6.2f ns\n\n", t_fdiv);

    printf("--- Control Flow ---\n");
    printf("Predictable branch: %6.2f ns (CPU predicst correctly)\n", t_branch_p);
    printf("Unpredictable branch: %6.2f ns (pipeline stalls!)\n", t_branch_u);
    printf("Function call: %6.2f ns (stack frame overhead)\n\n", t_call);

    printf("--- Analysis ---\n");
    printf("Division vs addition:   %.1fx slower\n", t_div / t_add);
    printf("Unpredictable vs predictable:   %.1fx slower\n", t_branch_u / t_branch_p);
    printf("Function call vs inline add: %.1fx slower\n\n", t_call / t_add);

    printf("--- HFT Perspective ---\n");
    printf("If you have 1 microsecond (1000 ns) to make a trading decision:\n");
    printf("Budget for int additions: ~%.0f ops\n", 1000.0 / t_add);
    printf("Budget for int divisions: ~%.0f ops\n", 1000.0 / t_div);
    printf("Budget for function calls: ~%.0f ops\n", 1000.0 / t_call);
    printf("Budget for unpredictable branches: ~%.0f ops\n", 1000.0 / t_branch_u);

    printf("LESSON: Every operation costs nanoseconds. Division is not free. Branches are\n");
    printf("not free. Function calls are not free. HFT systems minimize all of these\n");
    printf("in the critical 'hot path' where trading decisions are made.\n");

    return 0;
}
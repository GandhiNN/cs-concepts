/*
 * ffi_overhead_demo.c
 *
 * Demonstrates why the C->Python FFI boundary is expensive.
 * Compares three approaches to handling 33M values:
 *
 * 1. Direct buffer write (like what Go does): value -> typed buffer, no per-element overhead
 * 2. Simulated Python object creation (like what pymssql does): malloc + init per cell
 * 3. Object pool (amortized allocation): shows that malloc is the core cost
 *
 * Compile: gcc -O2 -o ffi_demo ffi_overhead_demo.c
 * Run: ./ffi_demo
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#define NUM_CELLS 33000000 /* 1M rows x 33 columns */

/* Simulated Python object header (simplified PyObject) */
typedef struct {
    int64_t ob_refcnt; /* reference count -> 8 bytes */
    void* ob_type; /* type pointer -> 8 bytes */
    int64_t ob_value; /* actual value -> 8 bytes */
} FakePyObject;

/* Fake type pointer (simulates &PyLong_Type) */
static int FAKE_PYLONG_TYPE = 0;

double time_diff(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
}

/* 
 * Test 1: Direct buffer write (Go / Arrow pattern)
 * 
 * This is what Go does: pre-allocate a typed int64 buffer,
 * write each parsed value directly. No per-element allocation.
*/
double test_direct_buffer(void) {
    struct timespec start, end;

    int64_t* buffer = (int64_t*)malloc(NUM_CELLS * sizeof(int64_t));
    if (!buffer) { perror("malloc"); exit(1); }

    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < NUM_CELLS; i++) {
        buffer[i] = (int64_t)i; /* simulate: parsed TDS value -> buffer */
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    /* Prevent compiler from optimizing away */
    volatile int64_t sink = buffer[NUM_CELLS - 1];
    (void)sink;

    free(buffer);
    return time_diff(start, end);
}

/*
 * Test 2: Per-element malloc (simulates Python FFI boundary)
 *
 * This is what happens in pymssql: for EVERY cell value,
 * CPython must malloc a PyObject, set refcnt, set type, copy value.
 * Then store the pointer in a list (another malloc'd array of pointers).
*/
double test_python_objects(void) {
    struct timespec start, end;

    /* The "list" - array of pointers to objects (like PyListObject) */
    FakePyObject** list = (FakePyObject**)malloc(NUM_CELLS * sizeof(FakePyObject*));
    if (!list) { perror("malloc"); exit(1); }

    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < NUM_CELLS; i++) {
        /* This happens for EVERY cell at the FFI boundary: */
        FakePyObject* obj = (FakePyObject*)malloc(sizeof(FakePyObject)); /* 1. heap alloc */
        obj->ob_refcnt = 1; /* 2. init refcount */
        obj->ob_type = &FAKE_PYLONG_TYPE; /* 3. set type pointer */
        obj->ob_value = (int64_t)i; /* 4. copy the actual value */
        list[i] = obj; /* 5. store pointer in list */
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    /* Simulate GC: must free every individual object */
    for (int i = 0; i < NUM_CELLS; i++) {
        free(list[i]);
    }
    free(list);

    return time_diff(start, end);
}

/*
 * Test 3: Pre-allocated object pool (shows malloc is the cost)
 * 
 * Same struct writes as Test 2, but objects come from a pre-allocated
 * contiguous array. No per-element malloc. This isolates the cost of
 * malloc vs the cost of field initialization.
*/
double test_pooled_objects(void) {
    struct timespec start, end;

    FakePyObject* pool = (FakePyObject*)malloc(NUM_CELLS * sizeof(FakePyObject));
    FakePyObject** list = (FakePyObject**)malloc(NUM_CELLS * sizeof(FakePyObject*));
    if (!pool || !list) { perror("malloc"); exit(1); }

    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < NUM_CELLS; i++) {
        FakePyObject* obj = &pool[i]; /* no malloc, just pointer arithmetic */
        obj->ob_refcnt = 1; /* same field writes as Python */
        obj->ob_type = &FAKE_PYLONG_TYPE;
        obj->ob_value = (int64_t)i;
        list[i] = obj;
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    free(pool);
    free(list);

    return time_diff(start, end);
}

int main(void) {
    printf("======================================================================\n");
    printf("FFI BOUNDARY OVERHEAD DEMONSTRATION (C)\n");
    printf("Simulating %d cells (1M rows x 33 columns)\n", NUM_CELLS);
    printf("======================================================================\n\n");

    /* Warm up */
    test_direct_buffer();
    test_python_objects();
    test_pooled_objects();

    /* Actual measurements (average of 3 runs) */
    double t1 = 0, t2 = 0, t3 = 0;
    int runs = 3;

    for (int r = 0; r < runs; r++) {
        t1 += test_direct_buffer();
        t2 += test_python_objects();
        t3 += test_pooled_objects();
    }
    t1 /= runs;
    t2 /= runs;
    t3 /= runs;

    printf("%-50s %8.3fs %10.0f values/s\n",
        "[1] Direct buffer write (Go/Arrow pattern)",
        t1, NUM_CELLS / t1);

    printf("%-50s %8.3fs %10.0f values/s\n",
        "[2] Per-element malloc (Python FFI boundary)",
        t2, NUM_CELLS / t2);

    printf("%-50s %8.3fs %10.0f values/s\n",
        "[3] Pooled objects (no malloc, same field writes)",
        t3, NUM_CELLS / t3);

    printf("\n");
    printf("----------------------------------------------------------------------\n");
    printf("ANALYSIS\n");
    printf("----------------------------------------------------------------------\n");
    printf("Test 2 vs Test 1: %.1fx slower (malloc + object init per cell)\n", t2 / t1);
    printf("Test 3 vs Test 1: %.1fx slower (object init only, no malloc)\n", t3 / t1);
    printf("Test 2 vs Test 3: %.1fx slower (pure malloc overhead)\n", t2 / t3);
    printf("\n");
    printf("Breakdown:\n");
    printf("  - Direct buffer write cost: %.0f ns/cell\n", t1 / NUM_CELLS * 1e9);
    printf("  - Object field init cost: %.0f ns/cell\n", (t3 - t1) / NUM_CELLS * 1e9);
    printf("  - malloc cost: %.0f ns/cell\n", (t2 - t3) / NUM_CELLS * 1e9);
    printf("  - Total Python FFI cost: %.0f ns/cell\n", t2 / NUM_CELLS * 1e9);
    printf("\n");
    printf("Memory:\n");
    printf("  - Direct buffer: %lu MB (contiguous int64 array)\n",
        (unsigned long)(NUM_CELLS * sizeof(int64_t)) / 1024 / 1024);
    printf("  - Python objects: %lu MB (PyObject per cell + pointer list)\n",
        (unsigned long)(NUM_CELLS * (sizeof(FakePyObject) + sizeof(void*))) / 1024 / 1024);
    printf("\n");
    printf("In pymssql: FreeTDS parses TDS -> calls PyLong_FromLong() per int cell\n");
    printf("  = Test 2 pattern (malloc + init + store per cell)\n");
    printf("In go: go-mssqldb parses TDS -> writes int64 to Arrow buffer\n");
    printf("  = Test 1 pattern (one write to contiguous buffer)\n");
    printf("\n");

    return 0;
}
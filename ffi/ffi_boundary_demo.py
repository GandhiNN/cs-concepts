"""
FFI Boundary Cost: Python <-> C object traversal

This demonstrates the cost of crossing the Python/C boundary by comparing
three ways to sum 10M integers:

1. Pure Python loop - slow but no FFI crossing (everything stays as PyObjects)
2. NumPy sum - fast because it stays entirely in C (no per-element FFI crossing)
3. ctypes call per element - crosses the FFI boundary per element (worst case)

The key insight: it's not that C is faster than Python at arithmetic.
It's that CROSSING THE BOUNDARY per element is expensive.

NumPy is fast because we cross ONCE (pass the whole array to C), C sums it,
and we cross back ONCE (return one int). That's 2 boundary crossings total.

ctypes per-element crosses the 10M times - each call has overhead:
    - Python packages the argument into a C-compatible format
    - Calls through a function pointer (indirect call, no inlining)
    - C does trivial work (add one number)
    - Result is wrapped back into a Python object
    - Return to Python

Run: python ffi_boundary_demo.py
Requires: gcc (to compile the tiny C library)
"""

import os
import sys
import time
import ctypes
import tempfile
import subprocess

C_SOURCE = """
#include <stdint.h>

// Adds one number to an accumulator, trivial
int64_t add_one(int64_t acc, int64_t val) {
    return acc + val;
}

// Sums and entire array in C, one FFI call for all elements
int64_t sum_array(int64_t* arr, int n) {
    int64_t total = 0;
    for (int i = 0; i < n; i++) {
        total += arr[i];
    }
    return total;
}
"""


def compile_c_library():
    """Complie the C code into a shared library."""
    tmpdir = tempfile.mkdtemp()
    c_path = os.path.join(tmpdir, "ffi_demo.c")
    so_path = os.path.join(tmpdir, "ffi_demo.so")

    with open(c_path, "w") as f:
        f.write(C_SOURCE)

    result = subprocess.run(
        ["gcc", "-shared", "-fPIC", "-O2", "-o", so_path, c_path],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        print(f"Compilation failed: {result.stderr}")
        sys.exit(1)

    return so_path


def main():
    N = 10_000_000

    print("=" * 70)
    print("FFI BOUNDARY COST: Python <-> C object traversal")
    print(f"Summing {N:,} integers")
    print("=" * 70)
    print()

    # Compile C library
    print("[setup] Compiling C library...")
    so_path = compile_c_library()
    lib = ctypes.CDLL(so_path)  # Load the compiled .so file

    lib.add_one.argtypes = [
        ctypes.c_int64,
        ctypes.c_int64,
    ]  # The C function add_one takes two arguments of 64-bit integers
    lib.add_one.restype = (
        ctypes.c_int64
    )  # The C function add_one returns a 64-bit integer

    lib.sum_array.argtypes = [
        ctypes.POINTER(ctypes.c_int64),
        ctypes.c_int,
    ]  # The C function sum_array takes a pointer to int64 array as
    # the first arg, and a plain int as the second arg
    lib.sum_array.restype = (
        ctypes.c_int64
    )  # The C function sum_array returns a 64-bit integer

    print("[setup] Done.")
    print()

    # --- Test 1: Pure Python loop ---
    print("[1] Pure Python: sum in a loop")
    print(" Boundary crossings: 0 (everything is a PyObject)")
    data = list(range(N))
    start = time.perf_counter()
    total = 0
    for x in data:
        total += x
    elapsed_python = time.perf_counter() - start
    print(f"    Result: {total}")
    print(f"    Time:   {elapsed_python:.3f}s ({N / elapsed_python:,.0f} ops/s)")
    print()

    # --- Test 2: ctypes per-element call ---
    print("[2] ctypes per-element: call C function 10M times")
    print(" Boundary crossings: 20M (Python->C and C->Python per element)")
    start = time.perf_counter()
    total = ctypes.c_int64(0)
    for x in data:
        total = lib.add_one(total, x)
    elapsed_ctypes_per = time.perf_counter() - start
    print(f"    Result: {total}")
    print(f"    Time: {elapsed_ctypes_per:.3f}s ({N / elapsed_ctypes_per:,.0f} ops/s)")
    print()

    # --- Test 3: ctypes single call (bulk) ---
    print("[3] ctypes bulk: pass array to C, one call sums all")
    print(" Boundary crossings: 2 (on call in, one return)")
    arr = (ctypes.c_int64 * N)(*range(N))
    start = time.perf_counter()
    total = lib.sum_array(arr, N)
    elapsed_ctypes_bulk = time.perf_counter() - start
    print(f"    Result: {total}")
    print(
        f"    Time: {elapsed_ctypes_bulk:.3f}s ({N / elapsed_ctypes_bulk:,.0f} ops/s)"
    )
    print()

    # --- Test 4: NumPy (reference) ---
    print("[4] NumPy sum: array stays in C, one result back")
    print(" Boundary crossings: 2")
    import numpy as np

    arr_np = np.arange(N, dtype=np.int64)
    start = time.perf_counter()
    total = arr_np.sum()
    elapsed_numpy = time.perf_counter() - start
    print(f"    Result: {total}")
    print(f"    Time: {elapsed_numpy:.3f}s ({N / elapsed_numpy:,.0f} ops/s)")
    print()

    # --- Summary ---
    print("=" * 70)
    print("SUMMARY")
    print("=" * 70)
    print(f"{'Method':<45} {'Time':>8} {'Crossings':>12} {'Relative':>10}")
    print("-" * 78)
    print(
        f"{'Pure Python loop':<45} {elapsed_python:>7.3f}s {'0':>12} {elapsed_python / elapsed_numpy:>9.1f}x"
    )
    print(
        f"{'ctypes per-element (10M calls)':<45} {elapsed_ctypes_per:>7.3f}s {'20M':>12} {elapsed_ctypes_per / elapsed_numpy:>9.1f}x"
    )
    print(
        f"{'ctypes bulk (1 call)':<45} {elapsed_ctypes_bulk:>7.3f}s {'2':>12} {elapsed_ctypes_bulk / elapsed_numpy:>9.1f}x"
    )
    print(f"{'NumPy (baseline)':<45} {elapsed_numpy:>7.3f}s {'2':>12} {'1.0':>10}")
    print()
    print("-" * 78)
    print("KEY INSIGHT:")
    print()
    print(" ctypes per-element is SLOWER than pure Python!")
    print(" The FFI boundary crossing overhead is so high that calling C 10M times")
    print(
        " is worse than doing the work in Python (which at least avoids the crossing)."
    )
    print()
    print(" This is exactly pymssql's problem:")
    print(" FreeTDS (C) decodes each cell, then crosses back to Python 33M times.")
    print(
        " Each crossing = argument packaging + function pointer call + result unwrapping."
    )
    print()
    print(" Go avoids this entirely: TDS decoding and Arror building are in the same")
    print(" language/runtime. No boundary to cross. Values flow through registers and")
    print(" stack, never wrapped/unwrapped.")
    print()

    # Cleanup
    os.unlink(so_path)
    os.unlink(so_path.replace(".so", ".c"))
    os.rmdir(os.path.dirname(so_path))


if __name__ == "__main__":
    main()

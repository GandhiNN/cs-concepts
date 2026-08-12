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

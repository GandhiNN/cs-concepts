# Latency: The First Principle of High-Frequency Trading

Everything in HFT reduces to one question: **how fast can we react?**

This module demonstrates measureable latency at every layer of a computer system,
from CPU instructions to memory access to network I/O.

## Demos

### 1. `cpu_clock_demo.c`

Measures the cost of basic CPU operations in nanoseconds.
Shows that every instruction has a real, measurable time cost.

```bash
gcc -O0 -o cpu_clock_demo cpu_clock_demo.c
./cpu_clock_demo
```

### 2. `memory_hierarchy_demo.c`

Demonstrates the massive speed difference between L1 cache, L2 cache, RAM,
and random vs sequential memory access patterns.

```bash
gcc -O2 -o memory_hierarchy_demo memory_hierarchy_demo.c
./memory_hierarchy_demo
```

### 3. `data_structure_latency.c`

Compares lookup time for different data structures - array scan, sorted binary
search, and hash table - showing why O(1) matters at nanosecond scale.

```bash
gcc -O2 -o data_structure_latency data_structure_latency.c
./data_structure_latency
```

## Key Takeaways

| Operation | Approximate Latency |
| --------- | ------------------- |
| CPU add/multiply | ~1 ns |
| L1 cache hit | ~1-2 ns |
| L2 cache hit | ~10-30 ns |
| L3 cache hit | ~10-30 ns |
| RAM access | ~50-100 ns |
| Sequential vs random access | 10-100x difference |
| Array scan (1000 items) | ~hundreds of ns |
| Binary search (1000 items) | ~tens of ns |
| Hash lookup | ~tens of ns |

In HFT, decisions must happen in **microseconds** (1000 ns). Every layer of
abstraction we add costs time. This is why HFT systems:

- Keep hot data in cache (memory layout matters)
- Use arrays over linked lists (cache-friendly)
- Avoid heap allocations in the hot path
- Bypass the OS kernel for networking

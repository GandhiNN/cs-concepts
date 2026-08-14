"""
Pre-computed closures concept demo in Python.

In Python the "closure" is just a bound method or lambda that captures
the typed handler, avoiding dict/if-else dispatch in the hot loop.
"""

import time
import random
import string

NUM_ROWS = 500_000
NUM_COLS = 10
COL_TYPES = ["int", "str", "float", "int", "str", "float", "int", "str", "int", "float"]

# Simulate builders


class IntBuilder:
    def __init__(self):
        self.values = []

    def append(self, v):
        self.values.append(v)

    def append_null(self):
        self.values.append(None)


class StrBuilder:
    def __init__(self):
        self.values = []

    def append(self, v):
        self.values.append(v)

    def append_null(self):
        self.values.append(None)


class FloatBuilder:
    def __init__(self):
        self.values = []

    def append(self, v):
        self.values.append(v)

    def append_null(self):
        self.values.append(None)


# Generate fake rows


def generate_rows():
    rows = []
    for _ in range(NUM_ROWS):
        row = []
        for ct in COL_TYPES:
            if random.randint(0, 19) == 0:
                row.append(None)
            elif ct == "int":
                row.append(random.randint(0, 1_000_000))
            elif ct == "str":
                row.append(
                    "val_" + "".join(random.choices(string.ascii_lowercase, k=5))
                )
            else:
                row.append(random.random() * 1000)
        rows.append(row)
    return rows


# APPROACH 1: if/elif dispatch in hot loop


def ingest_with_dispatch(rows, col_types):
    builders = []
    for ct in col_types:
        if ct == "int":
            builders.append(IntBuilder())
        elif ct == "str":
            builders.append(StrBuilder())
        else:
            builders.append(FloatBuilder())

    for row in rows:
        for i, val in enumerate(row):
            if val is None:
                builders[i].append_null()
            elif col_types[i] == "int":
                builders[i].append(val)
            elif col_types[i] == "str":
                builders[i].append(val)
            elif col_types[i] == "float":
                builders[i].append(val)

    return builders


# APPROACH 2: Pre-Computed Closures


def build_appenders(col_types):
    """Resolve type ONCE, return a list of callables."""
    builders = []
    appenders = []

    for ct in col_types:
        if ct == "int":
            b = IntBuilder()
        elif ct == "str":
            b = StrBuilder()
        else:
            b = FloatBuilder()
        builders.append(b)

        # Capture `b` in a closure - no type check needed later
        append = b.append
        append_null = b.append_null
        appenders.append((append, append_null))

    return builders, appenders


def ingest_with_closures(rows, appenders):
    for row in rows:
        for i, val in enumerate(row):
            if val is None:
                appenders[i][1]()  # append_null, already bound
            else:
                appenders[i][0](val)  # append, already bound


# Main

if __name__ == "__main__":
    print(
        f"Generating {NUM_ROWS * NUM_COLS // 1_000_000}M cells ({NUM_ROWS} rows x {NUM_COLS} cols)..."
    )
    rows = generate_rows()

    # Approach 1
    start = time.perf_counter()
    ingest_with_dispatch(rows, COL_TYPES)
    dispatch_dur = time.perf_counter() - start

    # Approach 2
    builders, appenders = build_appenders(COL_TYPES)
    start = time.perf_counter()
    ingest_with_closures(rows, appenders)
    closure_dur = time.perf_counter() - start

    cells = NUM_ROWS * NUM_COLS
    print()
    print(
        f"if/elif dispatch: {dispatch_dur:.3f}s ({cells / dispatch_dur:,.0f} cells/sec)"
    )
    print(
        f"Pre-bound methods: {closure_dur:.3f}s ({cells / closure_dur:,.0f} cells/sec)"
    )
    print(f"Speedup: {dispatch_dur / closure_dur:.2f}x")

# Pre-Computed Append Closures

## The Movie Cinema Analogy

The settings: A movie cinema which has several theater types/classes: "Regular", "IMAX", or "Premiere" and a ticketing booth that is served by a human / "front desk officers".

The flow is: a customer comes to the front desk saying the name of the movie name he/she wants to watch, the front desk officers map the movie name with the theater it's playing at, then the officers print out the ticket and stamp it with the name of the theater and then give it to the customer (the **hot loop**)

### Approach 1: Type Switch (a.k.a the slow way)

Imagine a situation where for every single order, the front desk officer walks to the back office and asks:
> "Hey, this movie, is it playing at Regular, IMAX, or Premiere (theater type/class)?"

The back office checks, points to the right theater, then the front office stamps the ticket with the correct theater placement, walks back his/her desk. Repeats for every single customer.

With 1000 customers x 3 theater types/classes that's 3000 times the front office asks "where does this film play?"

### Approach 2: Pre-computed Closures (a.k.a the fast way)

Now imagine the same situation, but now the front desk officer has a cheat sheet of where each film is exactly playing:
> "The Odyssey always plays in IMAX, Spiderman is playing in Premiere, and Evil Dead is playing at Regular..."

Now, when serving the customers, the front desk officers does not need to ask anyone, he/she just looks at this cheat sheet and directly stamps the ticket with the correct theater type/class. He/she spent less time questioning, waiting, and thinking.

In this case, the cheat sheet is the **closure**. It has the answer ("film A is playing in IMAX, film B is playing in Premiere, etc") at "setup time", so the hot loop never has to figure it out again, saving a lot of time in the process.

## Why does this matter?

Imagine a scenario where you have to do ETL of raw data from an MS-SQL server and save the result sets into Parquet files. Having the cheat sheet will help the computation process ("this column is an int: use the int builder, this column is a string: use the string builder") at ETL setup time so the hot loop (ingest raw data -> casting types into proper Arrow types ->-> save to disk as Parquet files) never has to figure the type mapping again.

To create this cheat sheet, we can run a query to probe the schema once in the beginning of the ETL phase (i.e. executing query `SELECT TOP 0 * FROM (<table_name>) AS _probe` in MS-SQL and parse the result set into type mapping)

This means improved performance: CPU is the main actor on this process. CPUs work like an assembly line, they start processing the next instruction before the current one finishes (pipelining). But when they hit a decision point ("which type is this?"), they have to guess which path to take. If they guess wrong, they throw away the work and start over (pipeline stall).

- When using the **Type switch** approach: The decision changes every column (int, string, float, int, string...). The CPU guesses wrong more often -> more pipeline stalls.
- When using the **Pre-Computed Closures** approach: The only decision is checking whether the cell is null, and the answer most likely is "no". The CPU guesses correctly more often -> the pipeline is flowing smoother.

## Runnable Code

```python
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

```

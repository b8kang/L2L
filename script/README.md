# Logic-to-transistor-network generator

This directory contains the minimal C++ source environment for the PMOS
logic-to-transistor-network generator.

## Source files

- `generator/logic_generator.cpp`: command-line handling, truth-table
  selection, Z3 search, cost minimization, and result logging.
- `generator/logic_smt_utils.cpp` and `generator/logic_smt_utils.h`: gate
  catalog and SMT graph construction.
- `generator/truth_table_filtering.cpp` and
  `generator/truth_table_filtering.h`: canonical truth-table generation for
  enumeration mode.

## Requirements

- A C++17 compiler
- Z3 C++ development headers
- The Z3 library
- POSIX threads

## Tested environment

- Red Hat Enterprise Linux 8.10
- GCC 8.5.0
- Z3 4.15.3
- Z3 runtime library: `/usr/local/lib/libz3.so.4.15.3.0`

Check the local tool and runtime-library versions with:

```bash
g++ --version
z3 --version
ldd ./logic_generator | grep libz3
```

## Build

From the repository root:

```bash
cd script/generator
g++ -std=c++17 -O3 \
    logic_generator.cpp logic_smt_utils.cpp truth_table_filtering.cpp \
    -lz3 -pthread -o logic_generator
```

## Usage

Display the command-line help:

```bash
./logic_generator --help
```

## Enumeration mode

Enumeration mode is the default. It generates the canonical truth-table set
for `INPUT` and solves the requested inclusive index range:

```bash
./logic_generator INPUT START_INDEX END_INDEX MIN_TR MAX_TR
```

Example:

```bash
./logic_generator ABCD 3249 3249 1 13
```

Defaults are `INPUT=AB`, `START_INDEX=0`, `END_INDEX=last`,
`MIN_TR=1`, and `MAX_TR=16`.

## Direct truth-table mode

Truth-table mode bypasses canonical truth-table generation and solves the
supplied output pattern directly:

```bash
./logic_generator --mode tt --truth_table BITS INPUT MIN_TR MAX_TR
```

For a four-input function, `BITS` must contain exactly `2^4 = 16` binary
digits:

```bash
./logic_generator --mode tt \
    --truth_table 0000000000000001 \
    ABCD 1 16
```

`BITS` may contain only `0` and `1`. Its length must equal `2^num_inputs`,
where `num_inputs` is the number of distinct uppercase variables in `INPUT`.

## Current solver configuration

- Series/parallel restriction: off
- Complemented primary inputs (bubbles): enabled
- Non-series-parallel compound inputs: enabled
- Compound-input permutations: enabled
- Stage-one output as a source: disabled
- Maximum PMOS stack: 4 (default, currently fixed in the source)
- Z3 solver threads: 4
- Solver timeout: 36,000 seconds
- Optimization timeout: 25,200 seconds

The executable writes its result log in the current working directory. In
enumeration mode, the range is included in the filename when explicitly
provided. In direct truth-table mode, the filename includes the supplied
truth-table bitstring.

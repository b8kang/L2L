# L2L: Logic to Layout Exploration of Standard Cell Library Design

This repository contains artifacts and experimental results associated with the following paper:

**Project page:** https://b8kang.github.io/L2L-Logic_to_Layout_Exploration/

**Paper (PDF):** https://b8kang.github.io/L2L-Logic_to_Layout_Exploration/L2L_DAC2026.pdf

**L2L: Logic to Layout Exploration of Standard Cell Library Design**  
Byeonggon Kang, Alan Mishchenko, Masahiro Fujita, Bill Lin, and Chung-Kuan Cheng  

To be presented at **DAC 2026 (Design Automation Conference)**  
Long Beach, California, USA

This repository provides artifacts used in the paper, including:

- Generated **standard-cell libraries**
- **Layout and physical design views**
- **Timing and power characterization**
- **Transistor network synthesis summaries**
- Detailed synthesis experiment logs

---

# Repository Contents

This README explains the structure of the repository and the contents of each folder.

---

# Circuit_and_Layout/

Artifacts for finalized standard-cell libraries including layouts and design views.

## CDL/

Transistor-level netlists of the generated standard-cell libraries.

- `L2LAO.cdl` ? Transistor-level netlists for the **Area-Optimized (AO)** library
- `L2LMO.cdl` ? Transistor-level netlists for the **Metal-Usage-Optimized (MO)** library

## GDS/

Physical layout implementations.

- `L2LAO.gds` ? Layouts for **Area-Optimized library**
- `L2LMO.gds` ? Layouts for **Metal-Usage-Optimized library**

## physical_view/

Physical abstraction views used by place-and-route tools.

- `L2LAO.lef` ? LEF view for L2LAO
- `L2LMO.lef` ? LEF view for L2LMO

## timing_power_view/

Timing and power characterization results.

- `L2LAO_tt_0.7_25_nldm.lib` ? Liberty NLDM model (TT, 0.7V, 25¡ÆC) for L2LAO
- `L2LMO_tt_0.7_25_nldm.lib` ? Liberty NLDM model (TT, 0.7V, 25¡ÆC) for L2LMO
- `L2LAO_tt_0.7_25_nldm.db` ? Compiled Liberty database for Synopsys tools
- `L2LMO_tt_0.7_25_nldm.db` ? Compiled Liberty database for Synopsys tools

## cell_info/

Per-cell statistics and summary information.

- `cell_report` ? Per-cell summary including:
  - Cell name
  - Boolean function
  - CPP usage
  - Metal usage (#M1 / #M2)

---

# Logic_to_Transistor_Network_Synthesis/

Experimental artifacts from transistor-network synthesis under different **stack bounds** and **input pools**.

Each directory contains both:

- **Aggregate summaries** (`all_logs_summary.txt`)
- **Detailed synthesis logs** (`logic_*_all_topo_*.log`)

These logs record transistor-network synthesis results including:

- Enumerated transistor network topologies
- Valid implementations for Boolean functions
- Multiple optimal solutions when they exist

These results cover **P-class Boolean functions up to three inputs**.

---

## 2stack_DP/

Synthesis results with stack bound **M = 2** using **DP (Dual Polarity) input pool**.

Files include:

- `all_logs_summary.txt` ? Aggregate synthesis statistics
- `logic_1input_P_CLASS_all_topo_2staged_final_0_to_1.log`
- `logic_2input_P_CLASS_all_topo_2staged_final_0_to_7.log`
- `logic_3input_P_CLASS_all_topo_2staged_final_0_to_67.log`

---

## 2stack_Op/

Synthesis results with stack bound **M = 2** using **Op (permutation-closed input pool)**.

Files include:

- `all_logs_summary.txt`
- `logic_1input_P_CLASS_all_topo_2staged_final_0_to_1.log`
- `logic_2input_P_CLASS_all_topo_2staged_final_0_to_7.log`
- `logic_3input_P_CLASS_all_topo_2staged_final_0_to_67.log`

---

## 3stack_DP/

Synthesis results with stack bound **M = 3** using **DP input pool**.

Contains detailed synthesis logs for 1-, 2-, and 3-input P-class Boolean functions.

---

## 3stack_Op/

Synthesis results with stack bound **M = 3** using **Op input pool**.

Contains detailed synthesis logs for 1-, 2-, and 3-input P-class Boolean functions.

---

## 4stack_DP/

Synthesis results with stack bound **M = 4** using **DP input pool**.

Contains detailed synthesis logs for 1-, 2-, and 3-input P-class Boolean functions.

---

## 4stack_Op/

Synthesis results with stack bound **M = 4** using **Op input pool**.

Contains detailed synthesis logs for 1-, 2-, and 3-input P-class Boolean functions.

---

## Grid_Scaffold/

- `logic_3input_P_CLASS_result_summary`

Synthesis summary for **series-parallel-only transistor network experiments**.

---

# script/

Source environment for generating PMOS logic-to-transistor-network synthesis
results.

The generator supports:

- Canonical truth-table enumeration over an inclusive index range
- Direct truth-table solving with `--mode tt --truth_table <bitstring>`
- Configurable minimum and maximum transistor bounds
- Z3-based topology search and gate-cost minimization

The truth-table bitstring must contain only `0` and `1`, and its length must
equal `2^num_inputs`.

Build and usage instructions are provided in [`script/README.md`](script/README.md).

---

# Notes

## DP vs. Op

**DP (Dual Polarity)**  
Input pool containing both a primary input and its inversion.

**Op (Permutation-Closed Pool)**  
Input pool where permutation-equivalent input instantiations are deduplicated.

---

## Stack Bound (M)

Maximum allowed number of **series transistors** during PMOS network synthesis.

---

## CPP

Contact Poly Pitch.

CPP is used as the **fundamental unit of cell width** in layout generation.

---

## Detailed Results

This repository provides both:

- **Aggregate statistics**
- **Detailed per-function synthesis artifacts**

including multiple optimal transistor-network solutions discovered during synthesis.

## Citation

```bibtex
@inproceedings{kang2026l2l,
  title={L2L: Logic to Layout Exploration of Standard Cell Library Design},
  author={Byeonggon Kang and Alan Mishchenko and Masahiro Fujita and Bill Lin and Chung-Kuan Cheng},
  booktitle={Proceedings of the Design Automation Conference (DAC)},
  year={2026}
}
```

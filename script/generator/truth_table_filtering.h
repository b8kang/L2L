#pragma once

#include <vector>
#include <string>

// A truth table is represented as a vector of integers (0s and 1s).
using TruthTable = std::vector<int>;

/**
 * @brief Generates all unique, non-symmetric Boolean functions for a given number of inputs.
 * * This is the main function that orchestrates the filtering process to find canonical
 * representations of Boolean functions that depend on all their input variables.
 * * @param n_inputs The number of input variables (e.g., 3 for 'A', 'B', 'C').
 * @param use_negation If true, considers input and output negations to find more symmetries (NPN equivalence).
 * @return A vector of unique truth tables.
 */
std::vector<TruthTable> get_unique_functions(int n_inputs, bool use_negation = false);

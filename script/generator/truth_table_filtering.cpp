#include "truth_table_filtering.h"
#include <vector>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <map>
#include <set>
#include <functional> // For std::function

// Helper type for input patterns
using InputPatterns = std::vector<std::vector<int>>;

/**
 * @brief Generates all 2^n input combinations for n variables.
 */
InputPatterns generate_input_patterns(int n_inputs) {
    InputPatterns patterns;
    int n_rows = 1 << n_inputs; // 2^n_inputs
    for (int i = 0; i < n_rows; ++i) {
        std::vector<int> pattern(n_inputs);
        for (int j = 0; j < n_inputs; ++j) {
            pattern[j] = (i >> j) & 1;
        }
        patterns.push_back(pattern);
    }
    return patterns;
}

/**
 * @brief Checks if a function's output depends on every input variable.
 */
bool uses_all_vars(const TruthTable& tt, const InputPatterns& patterns, const std::map<std::vector<int>, int>& pattern_to_index) {
    if (patterns.empty()) return true;
    int n_inputs = patterns[0].size();

    for (int var_idx = 0; var_idx < n_inputs; ++var_idx) {
        bool changed = false;
        for (size_t i = 0; i < patterns.size(); ++i) {
            std::vector<int> flipped_pattern = patterns[i];
            flipped_pattern[var_idx] ^= 1;
            
            auto it = pattern_to_index.find(flipped_pattern);
            if (it != pattern_to_index.end()) {
                int j = it->second;
                if (tt[i] != tt[j]) {
                    changed = true;
                    break;
                }
            }
        }
        if (!changed) {
            return false; // This variable has no influence.
        }
    }
    return true;
}

/**
 * @brief Finds the canonical (lexicographically smallest) form of a truth table.
 */
TruthTable canonical_form(const TruthTable& tt, const InputPatterns& patterns, const std::map<std::vector<int>, int>& pattern_to_index, bool use_negation) {
    int n_inputs = patterns.empty() ? 0 : patterns[0].size();
    if (n_inputs == 0) return tt;

    std::vector<int> p(n_inputs);
    std::iota(p.begin(), p.end(), 0); // Fill with 0, 1, 2, ...

    std::vector<TruthTable> forms;
    int n_rows = 1 << n_inputs;

    int num_flip_cases = use_negation ? (1 << n_inputs) : 1;

    // Loop through all permutations of input variables
    do {
        // Loop through all input negation combinations
        for (int i = 0; i < num_flip_cases; ++i) {
            std::vector<int> flip_bits(n_inputs);
            if (use_negation) {
                for (int j = 0; j < n_inputs; ++j) {
                    flip_bits[j] = (i >> j) & 1;
                }
            }
            
            TruthTable remapped(n_rows);
            for (size_t original_idx = 0; original_idx < patterns.size(); ++original_idx) {
                const auto& bits = patterns[original_idx];
                
                std::vector<int> temp_pattern(n_inputs);
                // Permute and flip bits to find the source pattern
                for (int k = 0; k < n_inputs; ++k) {
                    temp_pattern[k] = bits[p[k]] ^ flip_bits[k];
                }
                
                int source_idx = pattern_to_index.at(temp_pattern);
                remapped[original_idx] = tt[source_idx];
            }
            
            forms.push_back(remapped);
            if (use_negation) {
                TruthTable output_negated = remapped;
                for (int& bit : output_negated) {
                    bit = 1 - bit;
                }
                forms.push_back(output_negated);
            }
        }
    } while (std::next_permutation(p.begin(), p.end()));

    return *std::min_element(forms.begin(), forms.end());
}


/**
 * @brief Main function to generate unique functions.
 */
std::vector<TruthTable> get_unique_functions(int n_inputs, bool use_negation) {
    InputPatterns patterns = generate_input_patterns(n_inputs);
    
    // Create a map for quick lookups of pattern -> index
    std::map<std::vector<int>, int> pattern_to_index;
    for(size_t i = 0; i < patterns.size(); ++i) {
        pattern_to_index[patterns[i]] = i;
    }

    std::set<TruthTable> seen;
    std::vector<TruthTable> unique_functions;
    
    int n_rows = 1 << n_inputs;
    long long n_tables = 1LL << n_rows;

    for (long long i = 0; i < n_tables; ++i) {
        TruthTable tt(n_rows);
        for (int j = 0; j < n_rows; ++j) {
            tt[j] = (i >> j) & 1;
        }

        if (!uses_all_vars(tt, patterns, pattern_to_index)) {
            continue;
        }

        TruthTable cano = canonical_form(tt, patterns, pattern_to_index, use_negation);
        
        if (seen.find(cano) == seen.end()) {
            seen.insert(cano);
            unique_functions.push_back(cano);
        }
    }

    return unique_functions;
}
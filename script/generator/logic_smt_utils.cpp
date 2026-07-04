#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <tuple>
#include <set>
#include <algorithm>
#include <functional>
#include <numeric>
#include <cassert>
#include <iostream>
#include <map>
#include <z3++.h>
#include <functional>

using namespace z3;

// Helper function to generate nets from input variables and number of internal nets
std::vector<std::string> generate_nets_from_vars(const std::vector<std::string>& vars_found, int num_internal_nets) {
    // Sort and deduplicate input nets
    std::set<std::string> input_nets(vars_found.begin(), vars_found.end());
    std::vector<std::string> nets(input_nets.begin(), input_nets.end());
    
    // Add VDD and output Y
    nets.push_back("VDD");

    // Add internal nets (n1, n2, ...)
    for (int i = 0; i < num_internal_nets; ++i) {
        nets.push_back("n" + std::to_string(i + 1));
    }

    nets.push_back("Y");
    
    return nets;
}

// Helper function to compute the logical AND of a vector of bits
int logical_and(const std::vector<int>& bits) {
    return std::accumulate(bits.begin(), bits.end(), 1, std::logical_and<int>());
}

// Helper function to compute the logical OR of a vector of bits
int logical_or(const std::vector<int>& bits) {
    return std::accumulate(bits.begin(), bits.end(), 0, std::logical_or<int>());
}

// Helper function for making one stage function
//using TruthFunc = std::function<int(const std::vector<int>&)>;
using TruthFunc = std::function<int(std::vector<int>)>;
using Spec = std::tuple<std::string, int, TruthFunc, std::vector<int>>;

auto make_signature_with_perm =
    [](const std::function<int(const std::vector<int>&)>& func,
    const std::vector<int>& input_perm,
    int arity = 4) -> std::string
{
    std::string sig;
    for (int pat = 0; pat < (1 << arity); ++pat) {
        std::vector<int> bits(arity);
        for (int k = 0; k < arity; ++k)
            bits[k] = (pat >> (arity - 1 - k)) & 1;

        // Apply permutation to input bits
        std::vector<int> permuted_bits(arity);
        for (int i = 0; i < arity; ++i)
            permuted_bits[i] = bits[input_perm[i]];

        sig += std::to_string(func(permuted_bits));
    }
    return sig;
};

void add_specs_over_permutations(
    const std::string& prefix,                      // "6_SP1", "4_SP2", ...
    TruthFunc f,                                    // SP1, PLANAR1, ...
    std::unordered_set<std::string>& seen_tt,       // seen_tt_6_SP1, ...
    const std::vector<int>& comb,                   // original comb
    const std::vector<std::string>& orig_vars,      // original orig_vars
    std::vector<Spec>& specs                        // output specs
) {
    std::vector<int> base = {0, 1, 2, 3};           // initialization for every function

    do {
        std::vector<int> permuted_comb = {
            comb[base[0]], comb[base[1]], comb[base[2]], comb[base[3]]
        };

        std::string name_suffix =
            orig_vars[permuted_comb[0]] +
            orig_vars[permuted_comb[1]] +
            orig_vars[permuted_comb[2]] +
            orig_vars[permuted_comb[3]];

        std::string sig = make_signature_with_perm(f, base);

        if (seen_tt.insert(sig).second) {
            specs.emplace_back(
                prefix + "_" + name_suffix,
                4,
                f,
                permuted_comb
            );
        }
    } while (std::next_permutation(base.begin(), base.end()));
}

// Function to generate gate specifications based on input variables
//std::vector<std::tuple<std::string, int, std::function<int(std::vector<int>)>, std::vector<int>>> generate_gate_specs(const std::vector<std::string>& orig_vars) {
std::vector<std::tuple<std::string, int, std::function<int(std::vector<int>)>, std::vector<int>>> generate_gate_specs(const std::vector<std::string>& orig_vars, const std::string& non_sp_input = "no", bool canonical_only = false) {
    std::vector<std::tuple<std::string, int, std::function<int(std::vector<int>)>, std::vector<int>>> specs;
    int n = orig_vars.size();

    auto make_signature = [](const std::function<int(const std::vector<int>&)>& func,
                            int arity = 4) -> uint16_t
    {
        uint16_t sig = 0;
        for (int pat = 0; pat < (1 << arity); ++pat) {
            std::vector<int> b(arity);
            for (int k = 0; k < arity; ++k)
                b[k] = (pat >> (arity - 1 - k)) & 1;   // MSB is bits[0]
            sig |= (func(b) & 1) << pat;
        }
        return sig;
    };

    auto make_single_comb = [&](int k) {
        std::vector<int> comb(k);
        std::iota(comb.begin(), comb.end(), 0); // {0,1,...,k-1}
        return comb;
    };

    // NANDk / NORk gates
    for (int k = 2; k <= std::min(4, n); ++k) {
    //for (int k = 2; k <= 2; ++k) { // only for stack 2
        if (canonical_only) {
            auto comb = make_single_comb(k);              // {0,1,...,k-1}
            std::string name_suffix;
            for (int i : comb) name_suffix += orig_vars[i];
            specs.emplace_back(std::to_string(k) + "_NAND" + std::to_string(k) + "_" + name_suffix, k,
                [](const std::vector<int>& bits){ return 1 - logical_and(bits); }, comb);
            specs.emplace_back(std::to_string(k) + "_NOR"  + std::to_string(k) + "_" + name_suffix, k,
                [](const std::vector<int>& bits){ return 1 - logical_or(bits);  }, comb);
        } else {
            std::vector<int> indices(n);
            std::iota(indices.begin(), indices.end(), 0);
            std::vector<std::vector<int>> combs;
            // Generate all combinations of k elements from indices
            std::string bitmask(k, 1);
            bitmask.resize(n, 0);
            do {
                std::vector<int> comb;
                for (int i = 0; i < n; ++i) {
                    if (bitmask[i]) comb.push_back(indices[i]);
                }
                combs.push_back(comb);
            } while (std::prev_permutation(bitmask.begin(), bitmask.end()));

            for (const auto& comb : combs) {
                std::string name_suffix;
                for (int i : comb) {
                    name_suffix += orig_vars[i];
                }
                specs.push_back(std::make_tuple(
                    std::to_string(k) + "_NAND" + std::to_string(k) + "_" + name_suffix, k,
                    [](const std::vector<int>& bits) { return 1 - logical_and(bits); },
                    comb
                ));
                specs.push_back(std::make_tuple(
                    std::to_string(k) + "_NOR" + std::to_string(k) + "_" + name_suffix, k,
                    [](const std::vector<int>& bits) { return 1 - logical_or(bits); },
                    comb
                ));
            }
        }
    }

    // AOI21 / OAI21 (3-input gates)
    if (n >= 3) {
        if (canonical_only) {
            auto comb = make_single_comb(3);
            std::string A = orig_vars[comb[0]], B = orig_vars[comb[1]], C = orig_vars[comb[2]];
            std::vector<int> idxs = {comb[0], comb[1], comb[2]};

            specs.emplace_back("3_AOI21_" + A + B + C, 3,
                [](const std::vector<int>& bits){ return 1 - ((bits[0] & bits[1]) | bits[2]); }, idxs);
            specs.emplace_back("3_OAI21_" + A + B + C, 3,
                [](const std::vector<int>& bits){ return 1 - ((bits[0] | bits[1]) & bits[2]); }, idxs);

            if (non_sp_input == "yes") {
                specs.emplace_back("5_3IPLANAR_" + A + B + C, 3,
                    [](const std::vector<int>& bits){ return 1 - ((bits[0] & bits[1]) | (bits[1] & bits[2]) | (bits[0] & bits[2])); }, idxs);
            }
        } else {
            std::vector<int> indices(n);
            std::iota(indices.begin(), indices.end(), 0);
            std::vector<std::vector<int>> combs;
            // Generate all combinations of 3 elements from indices
            std::string bitmask(3, 1);
            bitmask.resize(n, 0);
            do {
                std::vector<int> comb;
                for (int i = 0; i < n; ++i) {
                    if (bitmask[i]) comb.push_back(indices[i]);
                }
                combs.push_back(comb);
            } while (std::prev_permutation(bitmask.begin(), bitmask.end()));

            for (const auto& comb : combs) {
                std::vector<std::string> name_inputs;
                for (int i : comb) {
                    name_inputs.push_back(orig_vars[i]);
                }
                // Generate all combinations of 2 elements from {0,1,2}
                std::vector<int> and_indices = {0, 1, 2};
                std::vector<std::vector<int>> and_combs;
                std::string and_bitmask(2, 1);
                and_bitmask.resize(3, 0);
                do {
                    std::vector<int> and_comb;
                    for (int i = 0; i < 3; ++i) {
                        if (and_bitmask[i]) and_comb.push_back(and_indices[i]);
                    }
                    and_combs.push_back(and_comb);
                } while (std::prev_permutation(and_bitmask.begin(), and_bitmask.end()));

                for (const auto& and_pos : and_combs) {
                    int or_pos = 3 - and_pos[0] - and_pos[1];
                    int A = and_pos[0], B = and_pos[1], C = or_pos;
                    std::vector<int> idxs = {comb[A], comb[B], comb[C]};
                    specs.push_back(std::make_tuple(
                        "3_AOI21_" + name_inputs[A] + name_inputs[B] + name_inputs[C], 3,
                        [](const std::vector<int>& bits) { return 1 - ((bits[0] & bits[1]) | bits[2]); },
                        idxs
                    ));
                    specs.push_back(std::make_tuple(
                        "3_OAI21_" + name_inputs[A] + name_inputs[B] + name_inputs[C], 3,
                        [](const std::vector<int>& bits) { return 1 - ((bits[0] | bits[1]) & bits[2]); },
                        idxs
                    ));
                }
            }

            if (non_sp_input == "yes") {
                /* --------------------------------------------------------------------------
                * “5_planar” (3-input).  Truth-table: output = 1 iff # of 1-inputs ≤ 1
                * Equivalent Boolean form:  !(A&B | A&C | B&C)
                * We add one spec for each of the 3! = 6 input permutations.
                * --------------------------------------------------------------------------*/
                auto five_planar = [](const std::vector<int>& bits) -> int {
                    return (bits[0] + bits[1] + bits[2] <= 1) ? 1 : 0;
                };

                std::vector<int> indices(n);
                std::iota(indices.begin(), indices.end(), 0);
                std::string bitmask(3, 1);
                bitmask.resize(n, 0);
                do {
                    std::vector<int> comb;
                    for (int i = 0; i < n; ++i) {
                        if (bitmask[i]) comb.push_back(indices[i]);
                    }

                    std::string name_suffix = orig_vars[comb[0]] + orig_vars[comb[1]] + orig_vars[comb[2]];

                    specs.emplace_back(
                        "5_3IPLANAR_" + name_suffix,
                        3,
                        five_planar,
                        comb
                    );
                } while (std::prev_permutation(bitmask.begin(), bitmask.end()));
            }
        }

        // if (non_sp_input == "yes") {
        //     /* --------------------------------------------------------------------------
        //     * “5_planar” (3-input).  Truth-table: output = 1 iff # of 1-inputs ≤ 1
        //     * Equivalent Boolean form:  !(A&B | A&C | B&C)
        //     * --------------------------------------------------------------------------*/
        //     auto five_planar = [](const std::vector<int>& bits) -> int {
        //         return (bits[0] + bits[1] + bits[2] <= 1) ? 1 : 0;
        //     };

        //     std::vector<int> comb = {0, 1, 2};

        //     // std::vector<int> comb = {2, 4, 5};

        //     std::string name_suffix = orig_vars[comb[0]] + orig_vars[comb[1]] + orig_vars[comb[2]];

        //     specs.emplace_back(
        //         "5_3IPLANAR_" + name_suffix,
        //         3,
        //         five_planar,
        //         comb
        //     );
        // }
    }

    // AOI22 / OAI22, AOI211 / OAI211, AOI31 / OAI31 (4-input gates)
    if (n >= 4) {

        if (canonical_only) {
            auto comb = make_single_comb(4); // [0,1,2,3]
            std::string a = orig_vars[comb[0]], b = orig_vars[comb[1]],
                        c = orig_vars[comb[2]], d = orig_vars[comb[3]];

            // AOI22 / OAI22 : (0,1) & (2,3)
            specs.emplace_back("4_AOI22_" + a + b + c + d, 4,
                [](const std::vector<int>& bits){ return 1 - ((bits[0] & bits[1]) | (bits[2] & bits[3])); },
                std::vector<int>{comb[0], comb[1], comb[2], comb[3]});
            specs.emplace_back("4_OAI22_" + a + b + c + d, 4,
                [](const std::vector<int>& bits){ return 1 - ((bits[0] | bits[1]) & (bits[2] | bits[3])); },
                std::vector<int>{comb[0], comb[1], comb[2], comb[3]});

            // AOI211 / OAI211 : AND=(0,1), OR=2,3
            specs.emplace_back("4_AOI211_" + a + b + c + d, 4,
                [](const std::vector<int>& bits){ return 1 - ((bits[0] & bits[1]) | bits[2] | bits[3]); },
                std::vector<int>{comb[0], comb[1], comb[2], comb[3]});
            specs.emplace_back("4_OAI211_" + a + b + c + d, 4,
                [](const std::vector<int>& bits){ return 1 - ((bits[0] | bits[1]) & bits[2] & bits[3]); },
                std::vector<int>{comb[0], comb[1], comb[2], comb[3]});

            // AOI31 / OAI31 : AND=(0,1,2), OR=3
            specs.emplace_back("4_AOI31_" + a + b + c + d, 4,
                [](const std::vector<int>& bits){ return 1 - ((bits[0] & bits[1] & bits[2]) | bits[3]); },
                std::vector<int>{comb[0], comb[1], comb[2], comb[3]});
            specs.emplace_back("4_OAI31_" + a + b + c + d, 4,
                [](const std::vector<int>& bits){ return 1 - ((bits[0] | bits[1] | bits[2]) & bits[3]); },
                std::vector<int>{comb[0], comb[1], comb[2], comb[3]});

            if (non_sp_input == "yes") {
                std::vector<int> permuted_comb(4);
                permuted_comb[0] = comb[0];
                permuted_comb[1] = comb[1];
                permuted_comb[2] = comb[2];
                permuted_comb[3] = comb[3];

                std::string name_suffix =
                    orig_vars[permuted_comb[0]] + orig_vars[permuted_comb[1]] +
                    orig_vars[permuted_comb[2]] + orig_vars[permuted_comb[3]];

                auto emit = [&](const char* tag,
                                const std::function<int(const std::vector<int>&)>& fn) {
                    specs.emplace_back(std::string(tag) + "_" + name_suffix,
                                    4, fn, permuted_comb);
                };

                // 1010 1000 1000 0000  (index 0,2,4,8 = 1)
                emit("6_SP1", [](const std::vector<int>& bits)->int {
                    return (!bits[3] && ((!bits[0] && !bits[1]) ||
                                        (!bits[2] && (bits[0] ^ bits[1])))) ? 1 : 0;
                });

                // 1111 1000 0000 0000  (index 0,1,2,3,4 = 1)
                emit("4_SP2", [](const std::vector<int>& bits)->int {
                    int A = bits[0], B = bits[1], C = bits[2], D = bits[3];
                    int index = (A << 3) | (B << 2) | (C << 1) | D;
                    return (index <= 4) ? 1 : 0;
                });

                // 1110 1000 1000 0000  (index 0,1,2,4,8 = 1)
                emit("8_PLANAR1", [](const std::vector<int>& bits)->int {
                    int A = bits[0], B = bits[1], C = bits[2], D = bits[3];
                    int index = (A << 3) | (B << 2) | (C << 1) | D;
                    return (index == 0 || index == 1 || index == 2 || index == 4 || index == 8) ? 1 : 0;
                });

                // 1111 1000 1000 0000  (index 0,1,2,3,4,8 = 1)
                emit("6_PLANAR2", [](const std::vector<int>& bits)->int {
                    int A = bits[0], B = bits[1], C = bits[2], D = bits[3];
                    int index = (A << 3) | (B << 2) | (C << 1) | D;
                    return (index == 0 || index == 1 || index == 2 ||
                            index == 3 || index == 4 || index == 8) ? 1 : 0;
                });

                // 1111 1100 1000 0000  (index 0,1,2,3,4,5,8 = 1)
                emit("6_SP3", [](const std::vector<int>& bits)->int {
                    int A = bits[0], B = bits[1], C = bits[2], D = bits[3];
                    int index = (A << 3) | (B << 2) | (C << 1) | D;
                    return (index == 0 || index == 1 || index == 2 ||
                            index == 3 || index == 4 || index == 5 || index == 8) ? 1 : 0;
                });

                // 1111 1110 1000 0000  (index 0,1,2,3,4,5,6,8 = 1)
                emit("5_PLANAR3", [](const std::vector<int>& bits)->int {
                    int A = bits[0], B = bits[1], C = bits[2], D = bits[3];
                    int index = (A << 3) | (B << 2) | (C << 1) | D;
                    return (index == 0 || index == 1 || index == 2 || index == 3 ||
                            index == 4 || index == 5 || index == 6 || index == 8) ? 1 : 0;
                });

                // 1111 1010 1100 0000  (index 0,1,2,3,4,6,8,9 = 1)
                emit("5_PLANAR4", [](const std::vector<int>& bits)->int {
                    int A = bits[0], B = bits[1], C = bits[2], D = bits[3];
                    int index = (A << 3) | (B << 2) | (C << 1) | D;
                    return (index == 0 || index == 1 || index == 2 || index == 3 ||
                            index == 4 || index == 6 || index == 8 || index == 9) ? 1 : 0;
                });

                // 1111 1110 1100 0000  (index 0,1,2,3,4,5,6,8,9 = 1)
                emit("6_PLANAR5", [](const std::vector<int>& bits)->int {
                    int A = bits[0], B = bits[1], C = bits[2], D = bits[3];
                    int index = (A << 3) | (B << 2) | (C << 1) | D;
                    return (index == 0 || index == 1 || index == 2 || index == 3 ||
                            index == 4 || index == 5 || index == 6 ||
                            index == 8 || index == 9) ? 1 : 0;
                });

                // 1111 1110 1110 0000  (index 0,1,2,3,4,5,6,8,9,10 = 1)
                emit("6_PLANAR6", [](const std::vector<int>& bits)->int {
                    int A = bits[0], B = bits[1], C = bits[2], D = bits[3];
                    int index = (A << 3) | (B << 2) | (C << 1) | D;
                    return (index == 0 || index == 1 || index == 2 || index == 3 ||
                            index == 4 || index == 5 || index == 6 ||
                            index == 8 || index == 9 || index == 10) ? 1 : 0;
                });

                // 1111 1111 1110 0000  (index 0~10 = 1)
                emit("4_SP4", [](const std::vector<int>& bits)->int {
                    int A = bits[0], B = bits[1], C = bits[2], D = bits[3];
                    int index = (A << 3) | (B << 2) | (C << 1) | D;
                    return (index <= 10) ? 1 : 0;
                });

                // 1111 1110 1110 1000  (index 0,1,2,3,4,5,6,8,9,10,12 = 1)
                emit("8_PLANAR7", [](const std::vector<int>& bits)->int {
                    int A = bits[0], B = bits[1], C = bits[2], D = bits[3];
                    int index = (A << 3) | (B << 2) | (C << 1) | D;
                    return (index == 0 || index == 1 || index == 2 || index == 3 || index == 4 ||
                            index == 5 || index == 6 || index == 8 || index == 9 ||
                            index == 10 || index == 12) ? 1 : 0;
                });

                // 1111 1111 1110 1000  (index 0,1,2,3,4,5,6,7,8,9,10,12 = 1)
                emit("6_PLANAR8", [](const std::vector<int>& bits)->int {
                    int A = bits[0], B = bits[1], C = bits[2], D = bits[3];
                    int index = (A << 3) | (B << 2) | (C << 1) | D;
                    return (index == 0 || index == 1 || index == 2 || index == 3 ||
                            index == 4 || index == 5 || index == 6 || index == 7 ||
                            index == 8 || index == 9 || index == 10 || index == 12) ? 1 : 0;
                });
            }
        } else {
            std::vector<int> indices(n);
            std::iota(indices.begin(), indices.end(), 0);
            std::vector<std::vector<int>> combs;
            // Generate all combinations of 4 elements from indices
            std::string bitmask(4, 1);
            bitmask.resize(n, 0);
            do {
                std::vector<int> comb;
                for (int i = 0; i < n; ++i) {
                    if (bitmask[i]) comb.push_back(indices[i]);
                }
                combs.push_back(comb);
            } while (std::prev_permutation(bitmask.begin(), bitmask.end()));

            for (const auto& comb : combs) {
                std::vector<std::string> name_inputs;
                for (int i : comb) {
                    name_inputs.push_back(orig_vars[i]);
                }

                // AOI22 / OAI22
                std::vector<int> indices4 = {0, 1, 2, 3};
                std::vector<std::vector<int>> pair1_combs;
                std::string pair1_bitmask(2, 1);
                pair1_bitmask.resize(4, 0);
                do {
                    std::vector<int> pair1;
                    for (int i = 0; i < 4; ++i) {
                        if (pair1_bitmask[i]) pair1.push_back(indices4[i]);
                    }
                    pair1_combs.push_back(pair1);
                } while (std::prev_permutation(pair1_bitmask.begin(), pair1_bitmask.end()));

                for (const auto& pair1 : pair1_combs) {
                    std::vector<int> pair2;
                    for (int i = 0; i < 4; ++i) {
                        if (std::find(pair1.begin(), pair1.end(), i) == pair1.end()) {
                            pair2.push_back(i);
                        }
                    }
                    if (pair1[0] < pair2[0]) {  // avoid duplicates
                        int i1 = pair1[0], i2 = pair1[1], j1 = pair2[0], j2 = pair2[1];
                        std::vector<int> idxs = {comb[i1], comb[i2], comb[j1], comb[j2]};
                        specs.push_back(std::make_tuple(
                            "4_AOI22_" + name_inputs[i1] + name_inputs[i2] + name_inputs[j1] + name_inputs[j2], 4,
                            [](const std::vector<int>& bits) { return 1 - ((bits[0] & bits[1]) | (bits[2] & bits[3])); },
                            idxs
                        ));
                        specs.push_back(std::make_tuple(
                            "4_OAI22_" + name_inputs[i1] + name_inputs[i2] + name_inputs[j1] + name_inputs[j2], 4,
                            [](const std::vector<int>& bits) { return 1 - ((bits[0] | bits[1]) & (bits[2] | bits[3])); },
                            idxs
                        ));
                    }
                }

                // AOI211 / OAI211
                for (const auto& and_pair : pair1_combs) {
                    std::vector<int> or_pair;
                    for (int i = 0; i < 4; ++i) {
                        if (std::find(and_pair.begin(), and_pair.end(), i) == and_pair.end()) {
                            or_pair.push_back(i);
                        }
                    }
                    int a1 = and_pair[0], a2 = and_pair[1], o1 = or_pair[0], o2 = or_pair[1];
                    std::vector<int> idxs = {comb[a1], comb[a2], comb[o1], comb[o2]};
                    specs.push_back(std::make_tuple(
                        "4_AOI211_" + name_inputs[a1] + name_inputs[a2] + name_inputs[o1] + name_inputs[o2], 4,
                        [](const std::vector<int>& bits) { return 1 - ((bits[0] & bits[1]) | bits[2] | bits[3]); },
                        idxs
                    ));
                    specs.push_back(std::make_tuple(
                        "4_OAI211_" + name_inputs[a1] + name_inputs[a2] + name_inputs[o1] + name_inputs[o2], 4,
                        [](const std::vector<int>& bits) { return 1 - ((bits[0] | bits[1]) & bits[2] & bits[3]); },
                        idxs
                    ));
                }

                // AOI31 / OAI31
                std::vector<std::vector<int>> and_triple_combs;
                std::string and_triple_bitmask(3, 1);
                and_triple_bitmask.resize(4, 0);
                do {
                    std::vector<int> and_triple;
                    for (int i = 0; i < 4; ++i) {
                        if (and_triple_bitmask[i]) and_triple.push_back(indices4[i]);
                    }
                    and_triple_combs.push_back(and_triple);
                } while (std::prev_permutation(and_triple_bitmask.begin(), and_triple_bitmask.end()));

                for (const auto& and_triple : and_triple_combs) {
                    int or_idx = 6 - and_triple[0] - and_triple[1] - and_triple[2];
                    int x = and_triple[0], y = and_triple[1], z = and_triple[2];
                    std::vector<int> idxs = {comb[x], comb[y], comb[z], comb[or_idx]};
                    specs.push_back(std::make_tuple(
                        "4_AOI31_" + name_inputs[x] + name_inputs[y] + name_inputs[z] + name_inputs[or_idx], 4,
                        [](const std::vector<int>& bits) { return 1 - ((bits[0] & bits[1] & bits[2]) | bits[3]); },
                        idxs
                    ));
                    specs.push_back(std::make_tuple(
                        "4_OAI31_" + name_inputs[x] + name_inputs[y] + name_inputs[z] + name_inputs[or_idx], 4,
                        [](const std::vector<int>& bits) { return 1 - ((bits[0] | bits[1] | bits[2]) & bits[3]); },
                        idxs
                    ));
                }

                if (non_sp_input == "yes") {

                    //std::unordered_set<uint16_t> seen_tt;
                    std::unordered_set<std::string> seen_tt_6_SP1;     // 1010 1000 1000 0000
                    std::unordered_set<std::string> seen_tt_4_SP2;     // 1111 1000 0000 0000
                    std::unordered_set<std::string> seen_tt_8_PLANAR1; // 1110 1000 1000 0000
                    std::unordered_set<std::string> seen_tt_6_PLANAR2; // 1111 1000 1000 0000
                    std::unordered_set<std::string> seen_tt_6_SP3;     // 1111 1100 1000 0000
                    std::unordered_set<std::string> seen_tt_5_PLANAR3; // 1111 1110 1000 0000
                    std::unordered_set<std::string> seen_tt_5_PLANAR4; // 1111 1010 1100 0000
                    std::unordered_set<std::string> seen_tt_6_PLANAR5; // 1111 1110 1100 0000 // dual edge!
                    std::unordered_set<std::string> seen_tt_6_PLANAR6; // 1111 1110 1110 0000 // dual edge!
                    std::unordered_set<std::string> seen_tt_4_SP4;     // 1111 1111 1110 0000
                    std::unordered_set<std::string> seen_tt_8_PLANAR7; // 1111 1110 1110 1000 // dual edge!
                    std::unordered_set<std::string> seen_tt_6_PLANAR8; // 1111 1111 1110 1000

                    
                    // SP1: truth table 1010 1000 1000 0000  (index 0,2,4,8 = 1)
                    {
                        auto SP_1 = [](const std::vector<int>& bits) -> int {
                            return (!bits[3] && ((!bits[0] && !bits[1]) ||
                                                (!bits[2] && (bits[0] ^ bits[1])))) ? 1 : 0;
                        };

                        add_specs_over_permutations("6_SP1",SP_1,seen_tt_6_SP1, comb,orig_vars,specs);
                    }

                    // SP2: truth table 1111 1000 0000 0000 (index 0,1,2,3,4 = 1)
                    {
                        auto SP_2 = [](const std::vector<int>& bits) -> int {
                            int A = bits[0], B = bits[1], C = bits[2], D = bits[3];
                            int index = (A << 3) | (B << 2) | (C << 1) | D;
                            return (index <= 4) ? 1 : 0;
                        };

                        add_specs_over_permutations("4_SP2",SP_2,seen_tt_4_SP2,comb,orig_vars,specs);
                    }

                    // PLANAR1: truth table 1110 1000 1000 0000 (index 0,1,2,4,8 = 1)
                    {
                        auto PLANAR1 = [](const std::vector<int>& bits) -> int {
                            int A = bits[0], B = bits[1], C = bits[2], D = bits[3];
                            int index = (A << 3) | (B << 2) | (C << 1) | D;
                            return (index == 0 || index == 1 || index == 2 ||
                                    index == 4 || index == 8) ? 1 : 0;
                        };

                        add_specs_over_permutations("8_PLANAR1",PLANAR1,seen_tt_8_PLANAR1,comb,orig_vars,specs);
                    }

                    // PLANAR2: truth table 1111 1000 1000 0000 (index 0,1,2,3,4,8 = 1)
                    {
                        auto PLANAR2 = [](const std::vector<int>& bits) -> int {
                            int A = bits[0], B = bits[1], C = bits[2], D = bits[3];
                            int index = (A << 3) | (B << 2) | (C << 1) | D;
                            return (index == 0 || index == 1 || index == 2 ||
                                    index == 3 || index == 4 || index == 8) ? 1 : 0;
                        };

                        add_specs_over_permutations("6_PLANAR2",PLANAR2,seen_tt_6_PLANAR2,comb,orig_vars,specs);
                    }

                    // SP3: truth table 1111 1100 1000 0000 (index 0,1,2,3,4,5,8 = 1)
                    {
                        auto SP3 = [](const std::vector<int>& bits) -> int {
                            int A = bits[0], B = bits[1], C = bits[2], D = bits[3];
                            int index = (A << 3) | (B << 2) | (C << 1) | D;
                            return (index == 0 || index == 1 || index == 2 ||
                                    index == 3 || index == 4 || index == 5 ||
                                    index == 8) ? 1 : 0;
                        };

                        add_specs_over_permutations("6_SP3",SP3,seen_tt_6_SP3,comb,orig_vars,specs);
                    }

                    // PLANAR3: truth table 1111 1110 1000 0000 (index 0,1,2,3,4,5,6,8 = 1)
                    {
                        auto PLANAR3 = [](const std::vector<int>& bits) -> int {
                            int A = bits[0], B = bits[1], C = bits[2], D = bits[3];
                            int index = (A << 3) | (B << 2) | (C << 1) | D;
                            return (index == 0 || index == 1 || index == 2 ||
                                    index == 3 || index == 4 || index == 5 ||
                                    index == 6 || index == 8) ? 1 : 0;
                        };

                        add_specs_over_permutations("5_PLANAR3",PLANAR3,seen_tt_5_PLANAR3,comb,orig_vars,specs);
                    }

                    // PLANAR4: truth table 1111 1010 1100 0000 (index 0,1,2,3,4,6,8,9 = 1)
                    {
                        auto PLANAR4 = [](const std::vector<int>& bits) -> int {
                            int A = bits[0], B = bits[1], C = bits[2], D = bits[3];
                            int index = (A << 3) | (B << 2) | (C << 1) | D;
                            return (index == 0 || index == 1 || index == 2 || index == 3 ||
                                    index == 4 || index == 6 || index == 8 || index == 9) ? 1 : 0;
                        };

                        add_specs_over_permutations("5_PLANAR4",PLANAR4,seen_tt_5_PLANAR4,comb,orig_vars,specs);
                    }

                    // PLANAR5: truth table 1111 1110 1100 0000 (index 0,1,2,3,4,5,6,8,9 = 1)
                    {
                        auto PLANAR5 = [](const std::vector<int>& bits) -> int {
                            int A = bits[0], B = bits[1], C = bits[2], D = bits[3];
                            int index = (A << 3) | (B << 2) | (C << 1) | D;
                            return (index == 0 || index == 1 || index == 2 || index == 3 ||
                                    index == 4 || index == 5 || index == 6 ||
                                    index == 8 || index == 9) ? 1 : 0;
                        };

                        add_specs_over_permutations("6_PLANAR5",PLANAR5,seen_tt_6_PLANAR5,comb,orig_vars,specs);
                    }

                    // PLANAR6: truth table 1111 1110 1110 0000 (index 0,1,2,3,4,5,6,8,9,10 = 1)
                    {
                        auto PLANAR6 = [](const std::vector<int>& bits) -> int {
                            int A = bits[0], B = bits[1], C = bits[2], D = bits[3];
                            int index = (A << 3) | (B << 2) | (C << 1) | D;
                            return (index == 0 || index == 1 || index == 2 || index == 3 ||
                                    index == 4 || index == 5 || index == 6 ||
                                    index == 8 || index == 9 || index == 10) ? 1 : 0;
                        };

                        add_specs_over_permutations("6_PLANAR6",PLANAR6,seen_tt_6_PLANAR6,comb,orig_vars,specs);
                    }

                    // SP4: truth table 1111 1111 1110 0000 (index 0~10 = 1)
                    {
                        auto SP4 = [](const std::vector<int>& bits) -> int {
                            int A = bits[0], B = bits[1], C = bits[2], D = bits[3];
                            int index = (A << 3) | (B << 2) | (C << 1) | D;
                            return (index <= 10) ? 1 : 0;
                        };

                        add_specs_over_permutations("4_SP4",SP4,seen_tt_4_SP4,comb,orig_vars,specs);
                    }

                    // PLANAR7: truth table 1111 1110 1110 1000 (index 0,1,2,3,4,5,6,8,9,10,12 = 1)
                    {
                        auto PLANAR7 = [](const std::vector<int>& bits) -> int {
                            int A = bits[0], B = bits[1], C = bits[2], D = bits[3];
                            int index = (A << 3) | (B << 2) | (C << 1) | D;
                            return (index == 0 || index == 1 || index == 2 || index == 3 || index == 4 ||
                                    index == 5 || index == 6 || index == 8 || index == 9 ||
                                    index == 10 || index == 12) ? 1 : 0;
                        };

                        add_specs_over_permutations("8_PLANAR7",PLANAR7,seen_tt_8_PLANAR7,comb,orig_vars,specs);
                    }

                    // PLANAR8: truth table 1111 1111 1110 1000 (index 0,1,2,3,4,5,6,7,8,9,10,12 = 1)
                    {
                        auto PLANAR8 = [](const std::vector<int>& bits) -> int {
                            int A = bits[0], B = bits[1], C = bits[2], D = bits[3];
                            int index = (A << 3) | (B << 2) | (C << 1) | D;
                            return (index == 0 || index == 1 || index == 2 || index == 3 ||
                                    index == 4 || index == 5 || index == 6 || index == 7 ||
                                    index == 8 || index == 9 || index == 10 || index == 12) ? 1 : 0;
                        };

                        add_specs_over_permutations("6_PLANAR8",PLANAR8,seen_tt_6_PLANAR8,comb,orig_vars,specs);
                    }
                }
            }
        }
    }

    return specs;
}

// Function to compute net value from index expression
// net_value_from_idx_expr -> delete

// Function to build the factor graph for the SMT solver
std::tuple<z3::func_decl, z3::func_decl, z3::func_decl>
build_factor_graph(z3::context& ctx, z3::solver& s, const std::vector<std::string>& nets, int num_tr, const std::vector<std::string>& inputs, const std::string& staged1_output_to_source) {
    // 0. Net name ↔ index mapping
    std::unordered_map<std::string, int> net_idx;
    int gate_allowed_start_idx = -1, gate_allowed_end_idx = -1;
    int n_start_idx = -1, n_end_idx = -1;
    std::vector<int> gate_allowed_indices;
    for (int i = 0; i < nets.size(); ++i) {
        net_idx[nets[i]] = i;
        if (nets[i].rfind("n", 0) == 0) {
            if (n_start_idx == -1) {
                n_start_idx = i;
            }
            n_end_idx = i;
        }
        const std::string& name = nets[i];
        if (name != "VDD" && name != "Y" && name.rfind("n", 0) != 0) {
            gate_allowed_indices.push_back(i);
        }
    }
    if (!gate_allowed_indices.empty()) {
        std::sort(gate_allowed_indices.begin(), gate_allowed_indices.end());
        gate_allowed_start_idx = gate_allowed_indices.front();
        gate_allowed_end_idx = gate_allowed_indices.back();
    }

    // net_vars = {name: Bool(name) for name in nets}
    // std::unordered_map<std::string, expr> net_vars;
    // for (const auto& name : nets)
    //     net_vars[name] = ctx.bool_const(name.c_str());

    // 1. Transistor control
    // std::unordered_map<int, expr> tr_gate;
    // std::unordered_map<int, expr> tr_on;
    // for (int i = 0; i < num_tr; ++i) {
    //     tr_gate[i] = ctx.bool_const(("tr" + std::to_string(i) + "_gate").c_str());
    //     tr_on[i] = !tr_gate[i];
    // }

    // 2. Transistor connection functions
    func_decl sourceOf = function("sourceOf", ctx.int_sort(), ctx.int_sort());
    func_decl drainOf  = function("drainOf", ctx.int_sort(), ctx.int_sort());
    func_decl gateNet  = function("gateNet", ctx.int_sort(), ctx.int_sort());

    // for (int i = 0; i < num_tr; ++i)
    //     s.add(gateOf(i) == tr_gate[i]);

    // 3. VDD is always True
    // s.add(net_vars["VDD"] == ctx.bool_val(true));

    // 4. Transistor connection constraints
    int num_nets = nets.size();
    int vdd_idx = net_idx["VDD"];
    int y_idx = net_idx["Y"];
    std::unordered_set<std::string> input_set(inputs.begin(), inputs.end());

    for (int i = 0; i < num_tr; ++i) {
        expr src = sourceOf(ctx.int_val(i));
        expr drn = drainOf(ctx.int_val(i));
        expr gat = gateNet(ctx.int_val(i));

        //s.add(src >= 0 && src < num_nets);
        //s.add(drn >= 0 && drn < num_nets);
        //s.add(gat >= 0 && gat < num_nets);
        
        // 2. Apply optimized constraints
        // Original: src is VDD or one of n*
        // Original: drn is Y or one of n*
        if (n_start_idx != -1) {
            //s.add((src == vdd_idx) || (src >= n_start_idx && src <= n_end_idx));
            s.add(drn >= n_start_idx && drn <= n_end_idx+1);
            expr o_in_source  = (src >= n_start_idx-1 && src <= n_end_idx);
            if (staged1_output_to_source == "yes") {
                expr o_in_gate  = (src >= gate_allowed_start_idx && src <= gate_allowed_end_idx);
                s.add(o_in_source || o_in_gate);
            } else {
                s.add(o_in_source);
            }
        } else {
            if (staged1_output_to_source == "yes") {
                expr o_in_gate  = (src >= gate_allowed_start_idx && src <= gate_allowed_end_idx);
                s.add(src == vdd_idx || o_in_gate);
            } else {
                s.add(src == vdd_idx); // No "n" nets
            }
            s.add(drn == y_idx); // No "n" nets
        }

        // gate constraint without internal net, Power and output
        if (gate_allowed_start_idx != -1) {
            // gate idx start from end
            s.add(gat >= gate_allowed_start_idx && gat <= gate_allowed_end_idx);
        } else {
            // no net for placing the gate -> error
            s.add(ctx.bool_val(false));
        }

        s.add(src != drn);

        // (1) If tr_on, then source == drain
        // s.add(implies(tr_on[i], src_val == drn_val));

        // (2) If all transistors connected to drain net are off, that net is 0
        // for (const auto& [net_name, idx] : net_idx) {
        //     if (net_name == "VDD" || std::find(inputs.begin(), inputs.end(), net_name) != inputs.end()) continue;
        //     std::vector<expr> drain_connected_conds;
        //     for (int j = 0; j < num_tr; ++j) {
        //         drain_connected_conds.push_back(drainOf(ctx.int_val(j)) == ctx.int_val(idx) && tr_on[j]);
        //     }
        //     s.add(implies(!mk_or(drain_connected_conds), !net_vars[net_name]));
        // }

        // (3) VDD only as source
        //s.add(drn != vdd_idx);

        // (4) Y only as drain
        //s.add(src != y_idx);

        // expr_vector gate_conds(ctx);
        // for (const auto& var_name : inputs) {
        //     gate_conds.push_back(gat == net_idx[var_name]);
        // }
        // s.add(mk_or(gate_conds));

        // (5) gate ordering
        // for (int j = 0; j < num_tr; ++j) {
        //     if (i == j) continue;
        //     expr is_series_flow1 = (drainOf(ctx.int_val(i)) == sourceOf(ctx.int_val(j)));
        //     s.add(implies(is_series_flow1, gateNet(ctx.int_val(i)) >= gateNet(ctx.int_val(j))));
        //     expr is_series_flow2 = (sourceOf(ctx.int_val(i)) == drainOf(ctx.int_val(j)));
        // }
    }

    // // 5. Net usage location restriction (only once globally per net)
    // for (int i = 0; i < num_tr; ++i) {
    //     expr src = sourceOf(ctx.int_val(i));
    //     expr drn = drainOf(ctx.int_val(i));
    //     for (const auto& [name, idx] : net_idx) {
    //         if (std::find(inputs.begin(), inputs.end(), name) != inputs.end()) {
    //             bool has_underbar = name.find('_') != std::string::npos;
    //             if (has_underbar && staged1_output_to_source == "yes") {
    //                 // Allow placement at source if underbar and flag is yes — do nothing for src
    //                 s.add(drn != idx);  // Only restrict drain
    //             } else {
    //                 // Otherwise, restrict both source and drain
    //                 s.add(src != idx);
    //                 s.add(drn != idx);
    //             }
    //             // s.add(src != idx);
    //             // s.add(drn != idx);
    //         }
    //     }
    // }

    // 6. Net appearance constraints (outside transistor loop)
    expr_vector vdd_sources(ctx);
    expr_vector y_drains(ctx);
    for (int i = 0; i < num_tr; ++i) {
        vdd_sources.push_back(sourceOf(ctx.int_val(i)) == vdd_idx);
        y_drains.push_back(drainOf(ctx.int_val(i)) == y_idx);
    }
    s.add(atleast(vdd_sources, 1));
    s.add(atleast(y_drains, 1));

    for (const auto& [name, idx] : net_idx) {
        if (name == "VDD" || name == "Y" || std::find(inputs.begin(), inputs.end(), name) != inputs.end()) {
            continue;
        } else {
            expr_vector sources(ctx), drains(ctx);
            for (int i = 0; i < num_tr; ++i) {
                sources.push_back(sourceOf(ctx.int_val(i)) == idx);
                drains.push_back(drainOf(ctx.int_val(i)) == idx);
            }
            s.add(atleast(sources, 1));
            s.add(atleast(drains, 1));
        }
    }

    // // 7. Input-usage constraints: at least one input must be used
    // for (const auto & X : inputs) {
    //     // expr_vector for this X
    //     expr_vector usage(ctx);
    //     int pos_i = net_idx.at(X);
    //     int neg_i = net_idx.at("1_" + X + "!");

    //     for (int t = 0; t < num_tr; ++t) {
    //         usage.push_back(gateNet(t) == ctx.int_val(pos_i));
    //         usage.push_back(gateNet(t) == ctx.int_val(neg_i));
    //     }

    //     // “at least one of these must be true”
    //     s.add(atleast(usage, 1));
    // }

    return std::make_tuple(sourceOf, drainOf, gateNet);
}

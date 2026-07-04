#include <iostream>
#include <iomanip>
#include <vector>
#include <cstdlib>
#include <string>
#include <set>
#include <map>
#include <regex>
#include <array>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <limits>
#include <numeric>
#include <unordered_set>
#include <utility>
#include <z3++.h>
#include "logic_smt_utils.h"
#include "truth_table_filtering.h"

using namespace z3;

using Clock = std::chrono::high_resolution_clock;

constexpr int kDefaultMaxStack = 4;

struct Config {
    std::string input_formula = "AB";
    int start_idx = 0;
    int end_idx = -1;
    int min_tr = 1;
    int max_tr = 16;
    std::string mode = "enumerate";
    std::string truth_table;
    bool range_provided = false;
};

void print_usage(const char* program, std::ostream& out) {
    out << "Usage:\n"
        << "  " << program
        << " [INPUT [START_INDEX END_INDEX [MIN_TR MAX_TR]]]\n"
        << "  " << program
        << " --mode tt --truth_table BITS [INPUT [MIN_TR MAX_TR]]\n\n"
        << "Modes:\n"
        << "  enumerate  Generate and solve the canonical truth-table set (default).\n"
        << "  tt         Solve exactly one truth table supplied with --truth_table.\n\n"
        << "Options:\n"
        << "  --mode MODE          Select 'enumerate' or 'tt'.\n"
        << "  --truth_table BITS   Binary output string; length must equal 2^num_inputs.\n"
        << "  -h, --help           Show this help message.\n\n"
        << "Default solver settings:\n"
        << "  Maximum PMOS stack: " << kDefaultMaxStack << "\n\n"
        << "Examples:\n"
        << "  " << program << " ABCD 100 109 1 16\n"
        << "  " << program
        << " --mode tt --truth_table 0000000000000001 ABCD 1 16\n";
}

[[noreturn]] void argument_error(const char* program, const std::string& message) {
    std::cerr << "Error: " << message << "\n\n";
    print_usage(program, std::cerr);
    std::exit(2);
}

Config parse_args(int argc, char** argv) {
    Config cfg;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0], std::cout);
            std::exit(0);
        }
        if (arg == "--mode") {
            if (i + 1 >= argc) {
                argument_error(argv[0], "--mode requires a value");
            }
            cfg.mode = argv[++i];
            continue;
        }
        if (arg == "--truth_table") {
            if (i + 1 >= argc) {
                argument_error(argv[0], "--truth_table requires a bitstring");
            }
            cfg.truth_table = argv[++i];
            continue;
        }
        if (arg.rfind("--", 0) == 0) {
            argument_error(argv[0], "unknown option '" + arg + "'");
        }
        positional.push_back(arg);
    }

    if (cfg.mode != "enumerate" && cfg.mode != "tt") {
        argument_error(argv[0], "--mode must be 'enumerate' or 'tt'");
    }

    if (cfg.mode == "tt") {
        if (positional.size() > 3) {
            argument_error(argv[0], "tt mode accepts INPUT, MIN_TR, and MAX_TR");
        }
        if (!positional.empty()) cfg.input_formula = positional[0];
        if (positional.size() >= 2) cfg.min_tr = std::atoi(positional[1].c_str());
        if (positional.size() >= 3) cfg.max_tr = std::atoi(positional[2].c_str());
        if (cfg.truth_table.empty()) {
            argument_error(argv[0], "tt mode requires --truth_table");
        }
    } else {
        if (!cfg.truth_table.empty()) {
            argument_error(argv[0], "--truth_table requires --mode tt");
        }
        if (positional.size() > 5) {
            argument_error(argv[0], "too many positional arguments");
        }
        if (!positional.empty()) cfg.input_formula = positional[0];
        if (positional.size() >= 2) cfg.start_idx = std::atoi(positional[1].c_str());
        if (positional.size() >= 3) {
            cfg.end_idx = std::atoi(positional[2].c_str());
            cfg.range_provided = true;
        }
        if (positional.size() >= 4) cfg.min_tr = std::atoi(positional[3].c_str());
        if (positional.size() >= 5) cfg.max_tr = std::atoi(positional[4].c_str());
    }

    return cfg;
}

struct ResultEntry {
    double min_cost;
    std::vector<std::set<std::tuple<std::string, std::string, std::string>>> solutions;
};

void process_optimal_solution(
    int min_found_cost,
    const std::set<std::tuple<std::string, std::string, std::string>>& best_solution_network,
    int num_tr,
    int pattern_idx,
    std::map<int, ResultEntry>& result,
    int& max_num_tr,
    std::ofstream& result_log
) {
    std::cout << "Optimal solution found!" << std::endl;
    result_log << "Optimal solution found!" << std::endl;

    for(const auto& t : best_solution_network) {
        std::cout << "tr: source=" << std::get<0>(t)
                << ", drain=" << std::get<2>(t)
                << ", gate_net=" << std::get<1>(t) << std::endl;
        result_log << "tr: source=" << std::get<0>(t)
                << ", drain=" << std::get<2>(t)
                << ", gate_net=" << std::get<1>(t) << std::endl;
    }

    int final_cost = min_found_cost + num_tr;
    std::cout << "    Gate Cost: " << min_found_cost << ", Num TR: " << num_tr << ", Final Cost: " << final_cost << std::endl;
    result_log << "    Gate Cost: " << min_found_cost << ", Num TR: " << num_tr << ", Final Cost: " << final_cost << std::endl;

    if (final_cost < result[pattern_idx].min_cost) {
        result[pattern_idx].min_cost = final_cost;
        result[pattern_idx].solutions = {best_solution_network};
        max_num_tr = std::min(max_num_tr, final_cost);
    } else if (final_cost == result[pattern_idx].min_cost) {
        result[pattern_idx].solutions.push_back(best_solution_network);
    }
}

// oneS for 4 input
static constexpr std::array<int, 20> kForcedOff = {
    0, 3, 9, 15, 33, 69, 75, 87, 95, 135,
    267, 273, 285, 463, 469, 549, 923, 963, 1299, 1951
};

int main(int argc, char** argv) {
    Config cfg = parse_args(argc, argv);
    int start_idx = cfg.start_idx;
    int end_idx = cfg.end_idx;

    auto start_time = std::chrono::high_resolution_clock::now();

    // Create Z3 context
    context c;
    
    // Configure solver parameters

    //unsigned int timeout_seconds = 25200; // seconds -> 8hrs:28800, 7hrs:25200
    unsigned int timeout_seconds = 36000; // seconds -> 8hrs:28800, 7hrs:25200
    //unsigned int timeout_seconds = 1800; // 1800 -> only literal
    unsigned int timeout_milliseconds = timeout_seconds * 1000; //
    //z3::set_param("random_seed", "27");
    unsigned int timeout_seconds_opt = 25200; // seconds -> 8hrs:28800, 7hrs:25200
    unsigned int timeout_milliseconds_opt = timeout_seconds_opt * 1000; //

    //set_param("sat.threads", "8"); 

    params p_solver(c);
    p_solver.set("timeout", timeout_milliseconds);
    p_solver.set("threads", 4u); 
    //p_solver.set("random_seed", 27u);
    //p_solver.set("restart_strategy", 2u);
    //p_solver.set("smt.phase_selection", 5u);
    params p_opt(c);
    //p_opt.set("random_seed", 27u);
    p_opt.set("timeout", timeout_milliseconds_opt);
    //p_opt.set("sat.threads", 8u);

    //params p(c);
    //p.set("smt.arith.solver", static_cast<unsigned>(2));  // Use Z3's new arithmetic solver
    //p.set("smt.arith.nl", true);   // Enable non-linear arithmetic
    //p.set("smt.arith.nl.rounds", static_cast<unsigned>(1000));  // Increase rounds for non-linear solving
    //p.set("smt.arith.nl.gb", true);  // Enable Gröbner basis computation
    //p.set("smt.arith.nl.branching", true);  // Enable branching for non-linear solving
    //p.set("opt.maxsat_engine", "core_maxsat");
    //p.set("rlimit", 1000000);
    //p.set("sat.phase", "caching");
    //p.set("opt.maxsat_engine", "core_maxsat");
    // p.set("opt.maxsat_engine", "wmax");
    // p.set("opt.maxsat_engine", "fu_malik");
    // p.set("opt.maxsat_engine", "rc2");

    //p.set("restart_strategy", 2u);
    //p.set("smt.phase_selection", 5);
    //p.set("rlimit", 1000000u);
    //p.set("phase_selection", 5u);
    //p.set("random_seed", 27u);
    //p.set("propagate_values", true);
    //p.set("arith.solver", 2u);
    //p.set("smt.qi.eager_threshold", 50u);
    //p.set("model.compact", true);

    // timeout
    //unsigned int timeout_seconds = 1800; // seconds
    //unsigned int timeout_seconds = 30; // seconds -> 8hrs:28800, 7hrs:25200
    //unsigned int timeout_milliseconds = timeout_seconds * 1000; // 
    //p.set("timeout", timeout_milliseconds);
    //p.set("opt.priority", "box");

    //s.set(p);

    // User-defined input formula
    std::string input_formula = cfg.input_formula;
    for (auto & ch : input_formula) {
        ch = std::toupper(static_cast<unsigned char>(ch));
    }
    std::regex var_regex("[A-Z]");
    std::set<std::string> vars_set;
    for (std::sregex_iterator it(input_formula.begin(), input_formula.end(), var_regex); it != std::sregex_iterator(); ++it) {
        vars_set.insert(it->str());
    }
    std::vector<std::string> vars_found(vars_set.begin(), vars_set.end());
    std::sort(vars_found.begin(), vars_found.end());
    int num_vars = vars_found.size();

    if (cfg.mode == "tt") {
        if (num_vars >= static_cast<int>(std::numeric_limits<std::size_t>::digits)) {
            argument_error(argv[0], "too many inputs to validate the truth-table length");
        }
        const std::size_t expected_length = std::size_t{1} << num_vars;
        if (cfg.truth_table.size() != expected_length) {
            argument_error(
                argv[0],
                "truth-table length is " + std::to_string(cfg.truth_table.size()) +
                ", but " + std::to_string(expected_length) +
                " bits are required for " + std::to_string(num_vars) + " inputs"
            );
        }
        if (cfg.truth_table.find_first_not_of("01") != std::string::npos) {
            argument_error(argv[0], "truth table must contain only '0' and '1'");
        }
        start_idx = 0;
        end_idx = 0;
    }

    //std::string log_name = "logic_" + std::to_string(num_vars) + "input_P_CLASS_all_topo_off_final";
    std::string log_name = "logic_" + std::to_string(num_vars) + "input_P_CLASS_all_topo_2staged_final";
    if (cfg.mode == "tt") {
        log_name += "_tt_" + cfg.truth_table;
    } else if (cfg.range_provided) {
        log_name += "_" + std::to_string(start_idx) + "_to_" + std::to_string(end_idx);
    }
    log_name += ".log";
    std::ofstream result_log(log_name, std::ios::app);

    // Other function settings
    std::string series_parallel = "no";  // yes/no
    std::string bubble = "on";  // on/off
    std::string non_sp_input = "yes";  // yes/no
    std::string staged1_output_to_source = "no";  // yes/no

    std::string other_comb = "on";  // on/off
    //std::string other_comb = (series_parallel == "yes") ? "on" : "off";

    // Design space settings
    int max_stack = kDefaultMaxStack;

    // Generate gate specifications
    //auto gate_specs = generate_gate_specs(vars_found, non_sp_input);
    //auto gate_specs = generate_gate_specs(vars_found, non_sp_input, /*canonical_only=*/true);
    auto gate_specs = generate_gate_specs(vars_found, non_sp_input, /*canonical_only=*/false);

    // Generate input combinations
    std::vector<std::vector<int>> input_combos;

    const bool is4             = (std::to_string(num_vars) == "4");

    for (int i = 0; i < (1 << num_vars); ++i) {
        std::vector<int> combo;
        for (int j = num_vars - 1; j >= 0; --j) {
            combo.push_back((i >> j) & 1);  // MSB-first
        }
        input_combos.push_back(combo);
    }

    std::cout << "start to generate all patterns" << std::endl;
    std::vector<std::vector<bool>> all_patterns;

    if (cfg.mode == "tt") {
        std::vector<bool> supplied_pattern;
        supplied_pattern.reserve(cfg.truth_table.size());
        for (char bit : cfg.truth_table) {
            supplied_pattern.push_back(bit == '1');
        }
        all_patterns.push_back(std::move(supplied_pattern));
        std::cout << "Using explicit truth table: " << cfg.truth_table << std::endl;
    } else {
        std::vector<std::vector<int>> int_based = get_unique_functions(num_vars, false);
        std::cout << "Generated " << int_based.size() << " unique functions." << std::endl;
        for (const auto& pattern : int_based) {
            all_patterns.emplace_back(pattern.begin(), pattern.end());
        }
    }

    result_log << vars_found.size() << "-input functions with " << input_formula << " : " << std::endl;
    result_log << "Series/Parallel: " << series_parallel << std::endl;
    result_log << "Bubble: " << bubble << std::endl;
    if (cfg.mode == "tt") {
        result_log << "Using explicit truth table: " << cfg.truth_table << std::endl;
    } else {
        result_log << "Generated " << all_patterns.size() << " unique functions." << std::endl;
    }

    if (end_idx < 0 || end_idx >= (int)all_patterns.size())
        end_idx = (int)all_patterns.size() - 1;

    result_log << "Processing patterns from " << start_idx << " to " << end_idx << std::endl;

    std::vector<std::string> orig_vars = vars_found;

    // Bubble handling
    if (bubble == "on") {
        std::vector<std::string> negated_vars;
        for (const auto& var : vars_found) {
            negated_vars.push_back("1_" + var + "!");
        }
        vars_found.insert(vars_found.end(), negated_vars.begin(), negated_vars.end());
        std::vector<std::vector<int>> input_combos_transformed;
        for (const auto& combo : input_combos) {
            std::vector<int> transformed_combo = combo;
            for (int val : combo) {
                transformed_combo.push_back(1 - val);
            }
            input_combos_transformed.push_back(transformed_combo);
        }
        input_combos = input_combos_transformed;
        // std::cout << "Transformed input combinations: " << std::endl;
        // for (const auto& combo : input_combos) {
        //     for (int val : combo) {
        //         std::cout << val << " ";
        //     }
        //     std::cout << std::endl;
        // }
    }

    // Other combinations handling
    if (other_comb == "on") {
        int orig_n = num_vars;
        for (const auto& [gate_name, arity, fn, input_idxs] : gate_specs) {
            std::vector<std::vector<int>> idx_tuples;
            std::vector<int> indices(orig_n);
            std::iota(indices.begin(), indices.end(), 0);
            std::string bitmask(arity, 1);
            bitmask.resize(orig_n, 0);
            do {
                std::vector<int> comb;
                for (int i = 0; i < orig_n; ++i) {
                    if (bitmask[i]) comb.push_back(indices[i]);
                }
                idx_tuples.push_back(comb);
            } while (std::prev_permutation(bitmask.begin(), bitmask.end()));

            std::vector<std::string> new_vars = {gate_name};
            vars_found.insert(vars_found.end(), new_vars.begin(), new_vars.end());
            std::vector<int> new_vals;
            for (const auto& combo : input_combos) {
                std::vector<int> bits;
                for (int idx : input_idxs) {
                    bits.push_back(combo[idx]);
                }
                new_vals.push_back(fn(bits));
            }
            for (size_t i = 0; i < input_combos.size(); ++i) {
                input_combos[i].push_back(new_vals[i]);
            }
        }
    }

    std::cout << "Transformed input combinations: " << std::endl;
    for (const auto& var : vars_found) {
        std::cout << var << " ";
    }
    std::cout << std::endl;
    for (const auto& combo : input_combos) {
        for (int val : combo) {
            std::cout << val << " ";
        }
        std::cout << std::endl;
    }
    
    std::map<int, ResultEntry> result;

    // Cost function setup
    std::map<std::string, int> input_cost_map;
    for (const auto& var : vars_found) {
        int cost = 0;
        size_t underscore_pos = var.find('_');
        if (underscore_pos != std::string::npos && underscore_pos > 0) {
            try {
                cost = std::stoi(var.substr(0, underscore_pos));
            } catch (...) {
                cost = 10; // fallback for malformed prefix
            }
        }
        input_cost_map[var] = cost;
    }

    //for (size_t pattern_idx = 0; pattern_idx < all_patterns.size(); ++pattern_idx) {
    for (int pattern_idx = start_idx; pattern_idx <= end_idx; ++pattern_idx) {
        auto pattern_start_time = std::chrono::high_resolution_clock::now();
        std::vector<bool> desired_output = all_patterns[pattern_idx];
        std::cout << std::endl;
        std::cout << "#####################" << std::endl;
        std::cout << pattern_idx << "-th truth table : ";
        for (bool val : desired_output) {
            std::cout << val;
        }
        std::cout << std::endl;
        
        result_log << std::endl;
        result_log << "#####################" << std::endl;
        result_log << pattern_idx << "-th truth table : ";
        for (bool val : desired_output) {
            result_log << val;
        }
        result_log << std::endl;

        // int max_num_tr = 24;
        //int max_num_tr = 16; // only literal
        //int max_num_tr = 13; // 2-stages
        // int max_num_tr = 4;
        result[pattern_idx] = {std::numeric_limits<double>::infinity(), {}};

        int num_tr = cfg.min_tr;
        int max_num_tr = cfg.max_tr;
        // if (series_parallel == "no") {
        //     num_tr = num_vars;
        // }
        //while (num_tr <= max_num_tr) {

        const bool forced_by_idx   = is4 && std::binary_search(kForcedOff.begin(), kForcedOff.end(), pattern_idx);
        const bool both_off = ((other_comb == "off") && (bubble == "off")) || forced_by_idx;

        while ( both_off ? (num_tr <= max_num_tr)
                : (num_tr  < max_num_tr-2) ) {
            //std::cout << "\n";
            //result_log << "\n";
            std::cout << "\n==================================================" << std::endl;
            result_log << "\n==================================================" << std::endl;
            std::cout << "Trying with num_tr = " << num_tr << " (current max_num_tr = " << max_num_tr << ")" << std::endl;
            result_log << "Trying with num_tr = " << num_tr << " (current max_num_tr = " << max_num_tr << ")" << std::endl;
            for (int num_net = 0; num_net < num_tr; ++num_net) {

                int gate_budget = max_num_tr - num_tr;
                if (gate_budget < 0) {
                    std::cout << "  Skipping num_net loop for num_tr=" << num_tr << " because gate_budget is negative." << std::endl;
                    break; 
                }

                //std::cout << "TR : " << num_tr << " / NET : " << num_net << " / MAX : " << max_num_tr << std::endl;
                //result_log << "TR : " << num_tr << " / NET : " << num_net << " / MAX : " << max_num_tr << std::endl;
                std::cout << "  Trying with num_net = " << num_net << ", gate_budget = " << gate_budget << std::endl;
                result_log << "  Trying with num_net = " << num_net << ", gate_budget = " << gate_budget << std::endl;

                // STEP 1: base solver + circuit structure generation
                solver s_base(c);
                //s_base.set(p);
                std::vector<std::string> nets = generate_nets_from_vars(vars_found, num_net);
                
                // Precompute primary-input membership once (O(1) lookups)
                std::unordered_set<std::string> prim_inputs(vars_found.begin(), vars_found.end());
                std::vector<char> is_primary(nets.size(), 0);
                for (size_t i = 0; i < nets.size(); ++i)
                    if (prim_inputs.count(nets[i])) is_primary[i] = 1;
                
                // std::cout << "Nets: ";
                // for (const auto& net : nets) {
                //     std::cout << net << " ";
                // }
                // std::cout << std::endl;

                std::map<std::string, int> net_idx;
                for (size_t i = 0; i < nets.size(); ++i) {
                    net_idx[nets[i]] = i;
                }

                // std::cout << "[net_idx (name → index)]\n";
                // for (const auto& [name, idx] : net_idx) {
                //     std::cout << "  " << std::setw(25) << std::left << name
                //             << " : " << idx << '\n';
                // }
                // std::cout << std::endl;

                std::vector<int> internal_net_indices;
                for (auto & name : nets) {
                    if (!name.empty() && name[0] == 'n') {
                        internal_net_indices.push_back(net_idx[name]);
                    }
                }

                // Cost group setup
                std::map<int, std::vector<int>> cost_groups;
                for (auto& [net_name, net_i] : net_idx) {
                    int cost = input_cost_map[net_name];
                    if (cost > 0) {
                        cost_groups[cost].push_back(net_i);
                    }
                }

                // 1) net_const vector
                std::vector<expr> net_const;
                net_const.reserve(nets.size());
                for (size_t i = 0; i < nets.size(); ++i) {
                    net_const.emplace_back(c.int_val(static_cast<int>(i)));
                }

                // 2) net_used map
                std::vector<std::pair<int,int>> cost_nets;
                cost_nets.reserve(input_cost_map.size());
                for (auto& [name, cost] : input_cost_map) {
                    if (cost <= 0) continue;
                    auto it = net_idx.find(name);
                    if (it == net_idx.end()) continue;
                    cost_nets.emplace_back(it->second, cost);
                }

                // std::cout << "Net indices: ";
                // for (const auto& [name, idx] : net_idx) {
                //     std::cout << name << ":" << idx << " ";
                // }
                // std::cout << std::endl;

                // func_decl sourceOf = function("sourceOf", c.int_sort(), c.int_sort());
                // func_decl drainOf = function("drainOf", c.int_sort(), c.int_sort());
                // func_decl gateNet = function("gateNet", c.int_sort(), c.int_sort());

                // build_factor_graph(s_base, nets, num_tr, vars_found);
                //func_decl sourceOf, drainOf, gateNet;
                auto t0_build = Clock::now();
                func_decl sourceOf(c), drainOf(c), gateNet(c);
                // std::tie(sourceOf, drainOf, gateNet) = build_factor_graph(c, s_base, nets, num_tr, vars_found);
                std::tie(sourceOf, drainOf, gateNet) = build_factor_graph(c, s_base, nets, num_tr, vars_found, staged1_output_to_source);
                
                auto t1_build = Clock::now();
                // std::cout << "[BuildGraph] "
                //         << std::fixed << std::setprecision(3)
                //         << std::chrono::duration<double>(t1_build - t0_build).count()
                //         << "s\n";
                
                auto t0_sym = Clock::now();
                // Speed-up 1: symmetry breaking by net ordering
                
                if (!internal_net_indices.empty()) {
                    int first_internal = internal_net_indices.front();
                    s_base.add( sourceOf(0) == c.int_val( net_idx["VDD"] ) );
                    s_base.add( drainOf(0)  == c.int_val( first_internal ) );
                // }
                
                // if (internal_net_indices.size() >= 2) {
                    int last_internal = internal_net_indices.back();
                    s_base.add( sourceOf(num_tr-1) == c.int_val( last_internal ) );
                    s_base.add( drainOf(num_tr-1)  == c.int_val( net_idx["Y"] ) );
                }
                
                for (int i = 1; i + 1 < num_tr; ++i) {
                    for (int j = i + 1; j + 1 < num_tr; ++j) {
                        expr s_i = sourceOf(i), g_i = gateNet(i), d_i = drainOf(i);
                        expr s_j = sourceOf(j), g_j = gateNet(j), d_j = drainOf(j);
                        expr lex_ij =
                            (s_i < s_j)
                         || ((s_i == s_j) && (d_i < d_j))
                         || ((s_i == s_j) && (d_i == d_j) && (g_i < g_j));
                        s_base.add(lex_ij);
                    }
                }

                auto t1_sym = Clock::now();
                // std::cout << "[Symmetry]  "
                //         << std::fixed << std::setprecision(3)
                //         << std::chrono::duration<double>(t1_sym - t0_sym).count()
                //         << "s\n";
                
                auto t0_prune = Clock::now();
                // // Speed-up 2: remove redundant

                // Speed-up 3: no same transistor
                // for (int i = 0; i < num_tr; ++i) {
                //     for (int j = i + 1; j < num_tr; ++j) {
                //         expr gates_match = (gateNet(i) == gateNet(j));
                //         expr terminals_match_direct = (sourceOf(i) == sourceOf(j)) && (drainOf(i) == drainOf(j));
                //         expr terminals_match_swapped = (sourceOf(i) == drainOf(j)) && (drainOf(i) == sourceOf(j));
                //         expr are_functionally_identical = gates_match && (terminals_match_direct || terminals_match_swapped);
                //         s_base.add(!are_functionally_identical);
                //     }
                // }

                auto t1_prune = Clock::now();
                // std::cout << "[Prune]     "
                //         << std::fixed << std::setprecision(3)
                //         << std::chrono::duration<double>(t1_prune - t0_prune).count()
                //         << "s\n";
                
                // Speed-up 4 : max_num_tr - num_tr
                if (gate_budget < 0) {
                    s_base.add(c.bool_val(false));
                } else {
                    for (auto& [cost, nets_of_cost] : cost_groups) {
                        int max_use = gate_budget / cost;
                        if (max_use <= 0) {
                            for (int t = 0; t < num_tr; ++t) {
                                for (int net_i : nets_of_cost) {
                                    s_base.add(gateNet(t) != c.int_val(net_i));
                                }
                            }
                        }
                        // } else {
                        //     z3::expr_vector lits(c);
                        //     for (int net_i : nets_of_cost) {
                        //         z3::expr_vector ors(c);
                        //         for (int t = 0; t < num_tr; ++t) {
                        //             ors.push_back(gateNet(t) == c.int_val(net_i));
                        //         }
                        //         expr s_used_i = mk_or(ors);
                        //         lits.push_back(s_used_i);
                        //     }
                        //     s_base.add(z3::atmost(lits, max_use));
                        // }
                    }
                }

                auto t0_prop = Clock::now();
                //std::vector<std::vector<StepConn>> all_combos_ALL_STEPS_vdd_vars_for_log;
                // This replaces the huge all_combos_ALL_STEPS_vdd_vars_for_log
                std::vector<std::vector<z3::expr>> final_step_conn_for_all_combos;
                final_step_conn_for_all_combos.reserve(input_combos.size());

                int max_steps = std::min(num_net + 1, max_stack);
                
                for (size_t combo_i = 0; combo_i < input_combos.size(); ++combo_i) {
                    // Initialize net_val map with proper Z3 expressions
                    // ---------- Combo net value table (index -> Bool) ----------
                    std::vector<expr> net_val_vec(nets.size(), c.bool_val(false));
                    for (const auto& [name, idx] : net_idx) {
                        net_val_vec[idx] = c.bool_const((name + "_combo" + std::to_string(combo_i)).c_str());
                    }
                    // Build Array for fast select
                    NetTable net_tbl = make_net_table(c, net_val_vec, /*default=*/false);

                    // for (size_t i = 0; i < vars_found.size(); ++i) {
                    //     solver.add(net_val.at(net_idx.at(vars_found[i])) == (input_combos[combo_i][i] == 1));
                    // }
                    for (size_t i = 0; i < vars_found.size(); ++i) {
                        const std::string& var_name = vars_found[i];
                        bool bit_val = static_cast<bool>(input_combos[combo_i][i]);
                        s_base.add(net_tbl.elems[ net_idx.at(var_name) ] == c.bool_val(bit_val));
                    }

                    std::vector<StepConn> layered_conn; // size = max_steps+1
                    layered_conn.reserve(max_steps+1);
                    for (int s_num = 0; s_num <= max_steps; ++s_num) {
                        layered_conn.push_back(
                            make_step_conn_vars(c, nets, is_primary, s_num, combo_i, staged1_output_to_source)
                        );
                    }

                    //all_combos_ALL_STEPS_vdd_vars_for_log.push_back(layered_conn);

                    for (size_t ni = 0; ni < nets.size(); ++ni) {
                        z3::expr is_vdd_expr = c.bool_val(false);

                        if (is_primary[ni]) {
                            bool has_underbar = (nets[ni].find('_') != std::string::npos);

                            if (staged1_output_to_source == "yes" && has_underbar) {
                                is_vdd_expr = net_val_vec[ni];
                            }

                        } else {
                            if (nets[ni] == "VDD") {
                                is_vdd_expr = c.bool_val(true);
                            }
                        }

                        s_base.add(layered_conn[0].elems[ni] == is_vdd_expr);
                    }

                    // Pre-calculate transistor ON/OFF states for the current input combination
                    std::vector<expr> tr_is_on_for_combo_vec(num_tr, c.bool_val(false)); // Default init
                    for (int t_precalc = 0; t_precalc < num_tr; ++t_precalc) {
                        expr gate_idx_expr = gateNet(t_precalc); // Z3 int expression for gate net of TR t
                        expr gate_signal_expr = net_select(net_tbl, gate_idx_expr);
                        tr_is_on_for_combo_vec[t_precalc] = !gate_signal_expr;  // PMOS
                    }

                    for (int s = 0; s < max_steps; ++s) {
                        StepConn& cur = layered_conn[s];
                        StepConn& nxt = layered_conn[s+1];

                        for (size_t current_net_idx = 0; current_net_idx < nets.size(); ++current_net_idx) {
                            if (is_primary[current_net_idx]) {
                                bool has_underbar = (nets[current_net_idx].find('_') != std::string::npos);
                                if (staged1_output_to_source == "no" || !has_underbar) {
                                    continue; // skip PI propagation
                                }
                            }

                            expr already_conn = cur.elems[current_net_idx];
                            expr_vector newly_vec(c);  // gather OR terms

                            for (int t = 0; t < num_tr; ++t) {
                                expr tr_on = tr_is_on_for_combo_vec[t];
                                expr s_idx_t = sourceOf(t);
                                expr d_idx_t = drainOf(t);

                                expr s_conn_prev = select(cur.arr, s_idx_t);
                                expr d_conn_prev = select(cur.arr, d_idx_t);
                                // int s_idx = m.eval(s_idx_t).get_numeral_int();
                                // int d_idx = m.eval(d_idx_t).get_numeral_int();
                                // expr s_conn_prev = cur.elems[s_idx];
                                // expr d_conn_prev = cur.elems[d_idx];

                                // forward
                                newly_vec.push_back( (d_idx_t == c.int_val((int)current_net_idx)) &&
                                                    tr_on && s_conn_prev );

                                if (series_parallel == "no") {
                                    expr allow_back = (d_idx_t != c.int_val(net_idx["Y"]));
                                    newly_vec.push_back( (s_idx_t == c.int_val((int)current_net_idx)) &&
                                                        tr_on && allow_back && d_conn_prev );
                                }
                            }

                            expr newly_conn = mk_or(newly_vec);
                            s_base.add( nxt.elems[current_net_idx] == (already_conn || newly_conn) );
                        }
                    }

                    // Add output constraints
                    expr output_net_conn = layered_conn[max_steps].elems[ net_idx.at("Y") ];
                    bool target = desired_output[combo_i];
                    s_base.add(output_net_conn == c.bool_val(target));

                    // After building layered_conn, store ONLY the final step's expressions
                    final_step_conn_for_all_combos.push_back(layered_conn[max_steps].elems);
                }

                auto t1_prop = Clock::now();
                // std::cout << "[Propagation] "
                //         << std::fixed << std::setprecision(3)
                //         << std::chrono::duration<double>(t1_prop - t0_prop).count()
                //         << "s\n";
                
                // // only one graph!
                // for (auto& [name, idx] : net_idx) {
                //     if (prim_inputs.count(name) || name == "VDD" || name == "Y") continue;
                //     expr_vector ever_conn(c);
                //     for (size_t combo_i = 0; combo_i < input_combos.size(); ++combo_i) {
                //         ever_conn.push_back(all_combos_ALL_STEPS_vdd_vars_for_log[combo_i][max_steps].elems[idx]);
                //     }
                //     s_base.add(mk_or(ever_conn));
                // }

                // only one graph!
                for (auto& [name, idx] : net_idx) {
                    if (prim_inputs.count(name) || name == "VDD" || name == "Y") continue;
                    expr_vector ever_conn(c);
                    for (size_t combo_i = 0; combo_i < input_combos.size(); ++combo_i) {
                        // Use the new, smaller variable
                        ever_conn.push_back(final_step_conn_for_all_combos[combo_i][idx]);
                    }
                    s_base.add(mk_or(ever_conn));
                }

                //                 // =================================================================================
                // // VDD Propagation Logic (Refactored using Z3 Functions to avoid 'select')
                // // =================================================================================
                // auto t0_prop = Clock::now();

                // int max_steps = std::min(num_net + 1, max_stack);

                // // is_connected(net_idx, step, combo_idx) -> Bool
                // z3::func_decl is_connected = c.function("is_connected", c.int_sort(), c.int_sort(), c.int_sort(), c.bool_sort());
                // // net_value(net_idx, combo_idx) -> Bool
                // z3::func_decl net_value = c.function("net_value", c.int_sort(), c.int_sort(), c.bool_sort());

                // for (size_t combo_i = 0; combo_i < input_combos.size(); ++combo_i) {
                //     auto combo_idx_expr = c.int_val(static_cast<int>(combo_i));

                //     for (size_t i = 0; i < vars_found.size(); ++i) {
                //         const std::string& var_name = vars_found[i];
                //         bool bit_val = static_cast<bool>(input_combos[combo_i][i]);
                //         auto net_idx_expr = c.int_val(net_idx.at(var_name));
                //         s_base.add(net_value(net_idx_expr, combo_idx_expr) == c.bool_val(bit_val));
                //     }

                //     for (size_t ni = 0; ni < nets.size(); ++ni) {
                //         auto net_idx_expr = c.int_val(static_cast<int>(ni));
                //         z3::expr is_vdd_expr = c.bool_val(false);

                //         if (is_primary[ni]) {
                //             bool has_underbar = (nets[ni].find('_') != std::string::npos);
                //             if (staged1_output_to_source == "yes" && has_underbar) {
                //                 is_vdd_expr = net_value(net_idx_expr, combo_idx_expr);
                //             }
                //         } else if (nets[ni] == "VDD") {
                //             is_vdd_expr = c.bool_val(true);
                //         }
                        
                //         s_base.add(is_connected(net_idx_expr, c.int_val(0), combo_idx_expr) == is_vdd_expr);
                //     }
                // }

                // for (int s = 0; s < max_steps; ++s) {
                //     for (size_t combo_i = 0; combo_i < input_combos.size(); ++combo_i) {
                //         auto combo_idx_expr = c.int_val(static_cast<int>(combo_i));
                        
                //         z3::expr_vector tr_is_on_for_combo_vec(c);
                //         for (int t = 0; t < num_tr; ++t) {
                //             expr gate_idx_expr = gateNet(t);
                //             expr gate_signal_expr = net_value(gate_idx_expr, combo_idx_expr);
                //             tr_is_on_for_combo_vec.push_back(!gate_signal_expr); // PMOS
                //         }

                //         for (size_t current_net_idx = 0; current_net_idx < nets.size(); ++current_net_idx) {
                //             if (is_primary[current_net_idx]) {
                //                 bool has_underbar = (nets[current_net_idx].find('_') != std::string::npos);
                //                 if (staged1_output_to_source == "no" || !has_underbar) {
                //                     continue;
                //                 }
                //             }
                //             auto current_net_idx_expr = c.int_val(static_cast<int>(current_net_idx));

                //             expr already_conn = is_connected(current_net_idx_expr, c.int_val(s), combo_idx_expr);

                //             expr_vector newly_vec(c);
                //             for (int t = 0; t < num_tr; ++t) {
                //                 expr tr_on = tr_is_on_for_combo_vec[t];
                //                 expr s_idx_t = sourceOf(t);
                //                 expr d_idx_t = drainOf(t);

                //                 expr s_conn_prev = is_connected(s_idx_t, c.int_val(s), combo_idx_expr);
                //                 expr d_conn_prev = is_connected(d_idx_t, c.int_val(s), combo_idx_expr);

                //                 newly_vec.push_back((d_idx_t == current_net_idx_expr) && tr_on && s_conn_prev);

                //                 if (series_parallel == "no") {
                //                     expr allow_back = (d_idx_t != c.int_val(net_idx.at("Y")));
                //                     newly_vec.push_back((s_idx_t == current_net_idx_expr) && tr_on && allow_back && d_conn_prev);
                //                 }
                //             }
                //             expr newly_conn = mk_or(newly_vec);

                //             s_base.add(is_connected(current_net_idx_expr, c.int_val(s + 1), combo_idx_expr) == (already_conn || newly_conn));
                //         }
                //     }
                // }

                // for (size_t combo_i = 0; combo_i < input_combos.size(); ++combo_i) {
                //     expr output_net_conn = is_connected(c.int_val(net_idx.at("Y")), c.int_val(max_steps), c.int_val(static_cast<int>(combo_i)));
                //     bool target = desired_output[combo_i];
                //     s_base.add(output_net_conn == c.bool_val(target));
                // }

                // auto t1_prop = Clock::now();
                // std::cout << "[Propagation] "
                //         << std::fixed << std::setprecision(3)
                //         << std::chrono::duration<double>(t1_prop - t0_prop).count()
                //         << "s\n";

                // for (auto& [name, idx] : net_idx) {
                //     if (prim_inputs.count(name) || name == "VDD" || name == "Y") continue;

                //     expr_vector ever_conn(c);
                //     for (size_t combo_i = 0; combo_i < input_combos.size(); ++combo_i) {
                //         ever_conn.push_back(is_connected(c.int_val(idx), c.int_val(max_steps), c.int_val(static_cast<int>(combo_i))));
                //     }
                //     s_base.add(mk_or(ever_conn));
                // }
                // // =================================================================================

                // cost calculation
                //z3::expr_vector cost_terms(c);
                expr total_cost = c.int_val(0);
                //std::vector<expr> net_used(nets.size(), c.bool_val(false));
                for (auto& [idx, cost] : cost_nets) {
                    z3::expr_vector used_ors(c);
                    for (int t = 0; t < num_tr; ++t) {
                        used_ors.push_back(gateNet(t) == net_const[idx]);
                        if (staged1_output_to_source == "yes") {
                            // If staged1_output_to_source is "yes", we also check sourceOf(t)
                            // to ensure the transistor is actually used in the circuit.
                            used_ors.push_back(sourceOf(t) == net_const[idx]);
                        }
                    }
                    expr used_i = mk_or(used_ors);
                    // cost calculation implies
                    //expr c_idx  = c.int_const(("c_"+std::to_string(idx)).c_str());
                    //s_base.add( implies( used_i,  c_idx == c.int_val(cost) ) );
                    //s_base.add( implies(!used_i,  c_idx == c.int_val(0)   ) );
                    //total_cost = total_cost + c_idx; 
                    // 

                    // cost caculation ite
                    total_cost = total_cost + ite(used_i, c.int_val(cost), c.int_val(0));
                    //

                    // net_used[idx] = used_i;

                    // cost_terms.push_back( ite(used_i,
                    //                         c.int_val(cost),
                    //                         c.int_val(0)) );
                }
                //expr total_cost = z3::sum(cost_terms);
                
                //bool is_hard_case = (num_tr >= 7 && num_net >= 3 && num_net <= num_tr - 3 && gate_budget >=2 );
                //bool is_hard_case = (num_tr >= 9 && num_net >= 4 && num_net <= num_tr - 4 && gate_budget >=2 );
                bool is_hard_case = (num_tr >= 8 && num_net >= 3 && num_net <= 7 && gate_budget >=4 );
                //bool is_hard_case = (num_tr > 16);
                //bool is_hard_case = (num_tr > 18);
                
                auto process_solution = [&](model& m) {
                    std::cout << "Solution found!" << std::endl;
                    result_log << "Solution found!" << std::endl;
                    std::set<std::tuple<std::string, std::string, std::string>> transistor_network;
                    for (int i = 0; i < num_tr; ++i) {
                        int s_idx = m.eval(sourceOf(i)).get_numeral_int();
                        int g_idx = m.eval(gateNet(i)).get_numeral_int();
                        int d_idx = m.eval(drainOf(i)).get_numeral_int();
                        transistor_network.insert(std::make_tuple(nets[s_idx], nets[g_idx], nets[d_idx]));
                        std::cout << "tr" << i << ": source=" << nets[s_idx]
                                  << ", drain=" << nets[d_idx]
                                  << ", gate_net=" << nets[g_idx] << std::endl;
                    }
                
                    int gate_cost = m.eval(total_cost, true).get_numeral_int();
                    int final_cost = gate_cost + num_tr;
                    std::cout << "    Gate Cost: " << gate_cost << ", Num TR: " << num_tr << ", Final Cost: " << final_cost << std::endl;
                
                    if (final_cost < result[pattern_idx].min_cost) {
                        result[pattern_idx].min_cost = final_cost;
                        result[pattern_idx].solutions = {transistor_network};
                        max_num_tr = std::min(max_num_tr, final_cost);
                    } else if (final_cost == result[pattern_idx].min_cost) {
                        result[pattern_idx].solutions.push_back(transistor_network);
                    }
                };

                if (is_hard_case) {
                    std::cout << "   [Hard Case] Using incremental binary search strategy for cost." << std::endl;
                    z3::solver hsolver(c);
                    hsolver.add(s_base.assertions());
                    hsolver.set(p_solver);

                    bool found_solution = false;

                    int min_found_cost = -1;
                    std::set<std::tuple<std::string, std::string, std::string>> best_solution_network;
                    //std::optional<model> best_model;
                    int low = 0;
                    int high = gate_budget;

                    while (low <= high) {
                        int mid_cost = low + (high - low) / 2;

                        std::string act_name = "act_range_" + std::to_string(low) + "_" + std::to_string(mid_cost);
                        z3::expr act = c.bool_const(act_name.c_str());

                        hsolver.add(implies(act, total_cost >= low && total_cost <= mid_cost));

                        std::cout << "      Checking for total_cost in [" << low << ", " << mid_cost << "]..." << std::endl;
                        auto t0_solve = Clock::now();
                        auto result_sat = hsolver.check(1, &act);
                        auto t1_solve = Clock::now();
                        std::cout << "[Solve]      "
                                    << std::fixed << std::setprecision(3)
                                    << std::chrono::duration<double>(t1_solve - t0_solve).count()
                                    << "s\n";
                        result_log << "[Solve]      "
                                    << std::fixed << std::setprecision(3)
                                    << std::chrono::duration<double>(t1_solve - t0_solve).count()
                                    << "s\n";

                        if (result_sat == z3::sat) {
                            found_solution = true;

                            model m = hsolver.get_model();
                            int actual_cost = m.eval(total_cost, true).get_numeral_int();

                            if (min_found_cost == -1 || actual_cost < min_found_cost) {
                                min_found_cost = actual_cost;
                                best_solution_network.clear();
                                for (int i = 0; i < num_tr; ++i) {
                                    int s_idx = m.eval(sourceOf(i)).get_numeral_int();
                                    int g_idx = m.eval(gateNet(i)).get_numeral_int();
                                    int d_idx = m.eval(drainOf(i)).get_numeral_int();
                                    best_solution_network.insert(std::make_tuple(nets[s_idx], nets[g_idx], nets[d_idx]));
                                }
                            }
                            high = actual_cost - 1; 
                            std::cout << "           ... SAT! Found solution with cost " << actual_cost << ". New range: [" << low << ", " << high << "]" << std::endl;
                        } else {
                            low = mid_cost + 1;
                            std::cout << "           ... UNSAT. New range: [" << low << ", " << high << "]" << std::endl;
                        }
                    }

                    // if (found_solution && best_model) {
                    //     process_solution(*best_model);
                    // } else {
                    //     std::cout << "   No solution found for this configuration." << std::endl;
                    // }
                    if (found_solution) {
                        process_optimal_solution(
                            min_found_cost,
                            best_solution_network,
                            num_tr,
                            pattern_idx,
                            result,
                            max_num_tr,
                            result_log
                        );                        
                    } else {
                        std::cout << "   No solution found for this configuration." << std::endl;
                    }                        
                } else {
                    // -----------------------------------------------------------------
                    // Strategy B: Easy! - Standard `minimize`
                    // -----------------------------------------------------------------
                    std::cout << "    [Easy Case] Using standard minimize strategy." << std::endl;
                    z3::optimize opt(c);
                    opt.add(s_base.assertions());
                    opt.set(p_opt);
                    opt.add(total_cost <= gate_budget);
                    opt.minimize(total_cost);

                    auto t0_solve = Clock::now();
                    auto result_sat = opt.check();
                    auto t1_solve = Clock::now();

                    std::cout << "[Solve]      "
                                << std::fixed << std::setprecision(3)
                                << std::chrono::duration<double>(t1_solve - t0_solve).count()
                                << "s\n";
                    result_log << "[Solve]      "
                                << std::fixed << std::setprecision(3)
                                << std::chrono::duration<double>(t1_solve - t0_solve).count()
                                << "s\n";
                    
                    if (result_sat == sat) {
                        model m = opt.get_model();
                        process_solution(m);
                        //std::ofstream out("your_problem.smt2");
                        //out << opt;
                    } else {
                        std::cout << "    No solution found for this configuration." << std::endl;
                    }
                }
            }
            std::cout << "Finish " << num_tr << " transistors loop" << std::endl;
            result_log << "Finish " << num_tr << " transistors loop" << std::endl;
            ++num_tr;
            // std::cout << "new max " << max_num_tr << " current max tr boundary" << std::endl;
            // result_log << "new max " << max_num_tr << " current max tr boundary" << std::endl;
            std::cout << "Minimum cost: " << int(result[pattern_idx].min_cost) << std::endl;
            result_log << "Minimum cost: " << int(result[pattern_idx].min_cost) << std::endl;
            std::cout << "Current Solution :" << std::endl;
            result_log << "Current Solution :" << std::endl;
            for (const auto& triple_set : result[pattern_idx].solutions) {
                for (const auto& [a, b, c] : triple_set) {
                    std::cout << "(" << a << ", " << b << ", " << c << ") ";
                    result_log << "(" << a << ", " << b << ", " << c << ") ";
                }
                std::cout << std::endl;
                result_log << std::endl;
            }
        }
    }
    
    result_log << std::endl;
    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end_time - start_time;
                    std::cout << std::fixed
                            << std::setprecision(2)
                            << "Total execution time: "
                            << duration.count()
                            << " seconds\n";
                    result_log << std::fixed
                            << std::setprecision(2)
                            << "Total execution time: "
                            << duration.count()
                            << " seconds\n";

    result_log.close();
    return 0;
} 

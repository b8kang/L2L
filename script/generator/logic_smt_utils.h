#ifndef LOGIC_SMT_UTILS_H
#define LOGIC_SMT_UTILS_H

#include <vector>
#include <string>
#include <map>
#include <functional>
#include <tuple>
#include <utility> // For std::move
#include <z3++.h>

/* --------------------------------------------------------------------------
 * FAST INDEX→BOOL LOOKUP SUPPORT
 *
 * Instead of building gigantic OR-of-ITE ladders every time we need
 * "value of net whose index is <expr idx>", we build a single Z3 Array
 *   Array[Int -> Bool]
 * and use (select arr idx).
 *
 * We store both the Array expression and the element expressions in case
 * callers want direct indexed access without select.
 * --------------------------------------------------------------------------*/
struct NetTable {
    z3::expr arr;
    std::vector<z3::expr> elems;

    NetTable(z3::expr a, std::vector<z3::expr> e) : arr(a), elems(std::move(e)) {}
};

/* --------------------------------------------------------------------------
 * STEP CONNECTION TABLE
 * Same idea, but we sometimes don't want vars for primary inputs.
 * To keep indexing uniform (so we can select() blindly), we create
 * constant-false placeholders for primary nets.
 * --------------------------------------------------------------------------*/
struct StepConn {
    z3::expr arr;
    std::vector<z3::expr> elems;

    StepConn(z3::expr a, std::vector<z3::expr> e) : arr(a), elems(std::move(e)) {}
};

// Build an Array from a dense vector<expr> (size = num_nets).
// default_val: what to fill unused slots with before overwriting via store.
inline NetTable make_net_table(z3::context& c,
                               const std::vector<z3::expr>& dense_elems,
                               bool default_val = false) {
    z3::expr arr = z3::const_array(c.int_sort(), c.bool_val(default_val));
    for (int i = 0; i < (int)dense_elems.size(); ++i) {
        arr = z3::store(arr, c.int_val(i), dense_elems[i]);
    }
    return NetTable(arr, dense_elems);
}

// Convenience: select from a NetTable using an Int index expr.
inline z3::expr net_select(const NetTable& tbl, const z3::expr& idx) {
    return z3::select(tbl.arr, idx);
}

// Build StepConn variables for a given step & combo.
//   nets        : global net name list
//   is_primary  : size=nets, nonzero if primary input
//   step_idx    : which propagation step
//   combo_idx   : which input combination
// For primary nets we insert constant false (they are not VDD-propagated).
inline StepConn make_step_conn_vars(z3::context& c,
                                    const std::vector<std::string>& nets,
                                    const std::vector<char>& is_primary,
                                    int step_idx,
                                    size_t combo_idx,
                                    const std::string& staged1_output_to_source = "no") {
    int N = (int)nets.size();
    std::vector<z3::expr> elems;
    elems.reserve(N);
    for (int i = 0; i < N; ++i) {
        bool has_underbar = (nets[i].find('_') != std::string::npos);
        if (is_primary[i] && (staged1_output_to_source == "no" || !has_underbar)) {
            elems.push_back(c.bool_val(false));
        } else {
            std::string nm = nets[i] + "_vdd_conn_s" + std::to_string(step_idx) +
                             "_c" + std::to_string(combo_idx);
            elems.push_back(c.bool_const(nm.c_str()));
        }
    }
    // array generation
    z3::expr arr = z3::const_array(c.int_sort(), c.bool_val(false));
    for (int i = 0; i < N; ++i) {
        arr = z3::store(arr, c.int_val(i), elems[i]);
    }
    return StepConn(arr, std::move(elems));
}

/* --------------------------------------------------------------------------
 * Legacy compatibility shim.
 * Replace calls to net_value_from_idx_expr(...) with net_select(tbl, idx).
 * Provide a thin wrapper that asserts we have a dense 0..N-1 map and
 * dispatches to a store/select array. Use only for transitional builds.
 * --------------------------------------------------------------------------*/
inline z3::expr net_value_from_idx_expr(const z3::expr& index_expr,
                                        const std::map<int, z3::expr>& index_to_expr_map) {
    z3::context& ctx = index_expr.ctx();
    int max_i = -1;
    for (auto const& [key, val] : index_to_expr_map) {
        max_i = std::max(max_i, key);
    }
    if (max_i < 0) return ctx.bool_val(false);

    std::vector<z3::expr> dense(max_i + 1, ctx.bool_val(false));
    for (auto const& [key, val] : index_to_expr_map) {
        dense[key] = val;
    }
    NetTable tbl = make_net_table(ctx, dense, false);
    return z3::select(tbl.arr, index_expr);
}


// Function to generate nets from input variables and number of internal nets
std::vector<std::string> generate_nets_from_vars(const std::vector<std::string>& vars_found, int num_internal_nets);

// Function to generate gate specifications based on input variables
std::vector<std::tuple<std::string, int, std::function<int(std::vector<int>)>, std::vector<int>>> generate_gate_specs(const std::vector<std::string>& orig_vars,
                    const std::string& non_sp_input = "no", bool canonical_only = false);   // optional default

// Function to build the factor graph for the SMT solver
//void build_factor_graph(solver& s, const std::vector<std::string>& nets, int num_tr, const std::vector<std::string>& inputs);
std::tuple<z3::func_decl, z3::func_decl, z3::func_decl>
build_factor_graph(z3::context& ctx, z3::solver& s,
                   const std::vector<std::string>& nets,
                   int num_tr,
                   const std::vector<std::string>& inputs,
                   const std::string& staged1_output_to_source);

#endif // LOGIC_SMT_UTILS_H

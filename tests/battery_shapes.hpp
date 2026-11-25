// Program generators for the battery tests. Included only by programs_test.cpp, one translation unit. Each case is one small program that returns 42 when the
// compiler is right. The shapes are nested arrays, pointer shells, and tuple shells over several leaf
// types, in each context a value can be in: a local, a parameter, a result, a field, a global, and more.
#pragma once

#include "battery.hpp"

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace tlc::test {
namespace {

std::string repeat(const std::string &text, int count) {
    std::string out;
    for (int i = 0; i < count; ++i) {
        out += text;
    }
    return out;
}

// Every shell sequence of length 1 to `max_depth` over `kinds`, in odometer order.
std::vector<std::vector<std::string>> shell_product(const std::vector<std::string> &kinds, int max_depth) {
    std::vector<std::vector<std::string>> out;
    for (int depth = 1; depth <= max_depth; ++depth) {
        std::vector<std::size_t> at(static_cast<std::size_t>(depth), 0);
        while (true) {
            std::vector<std::string> shells;
            for (std::size_t i : at) {
                shells.push_back(kinds[i]);
            }
            out.push_back(std::move(shells));
            int pos = depth - 1;
            while (pos >= 0 && ++at[static_cast<std::size_t>(pos)] == kinds.size()) {
                at[static_cast<std::size_t>(pos)] = 0;
                --pos;
            }
            if (pos < 0) {
                break;
            }
        }
    }
    return out;
}

std::string shell_code(const std::string &kind) {
    if (kind == "[2]") {
        return "A";
    }
    if (kind == "[]") {
        return "S";
    }
    if (kind == "?*") {
        return "O";
    }
    if (kind == "*") {
        return "P";
    }
    return kind;
}

// ---------------------------------------------------------------------------------------------------
// Nested arrays `[2]...[2]E`, depth 1 to 5.

std::string nest_lit_vals(int depth, const std::string &leaf, int &base) {
    if (depth == 0) {
        const int v = ++base;
        if (leaf == "f64") {
            return std::to_string(v) + ".0";
        }
        if (leaf == "bool") {
            return v % 2 != 0 ? "true" : "false";
        }
        return std::to_string(v);
    }
    const std::string first = nest_lit_vals(depth - 1, leaf, base);
    const std::string second = nest_lit_vals(depth - 1, leaf, base);
    return "{" + first + ", " + second + "}";
}

std::string nest_case(const std::string &name, int depth, const std::string &E) {
    const std::string A = repeat("[2]", depth) + E;
    int base = 0;
    const std::string L = "[_]" + repeat("[2]", depth - 1) + E + nest_lit_vals(depth, E, base);
    const int top = 1 << depth;
    const std::string V = E == "f64"   ? std::to_string(top) + ".0"
                          : E == "bool" ? std::string("false")
                          : E == "u8"   ? std::to_string(top) + "u8"
                                        : std::to_string(top);
    const std::string I = repeat("[1]", depth);
    const std::string S = "[]" + repeat("[2]", depth - 1) + E;
    const std::string I1 = repeat("[1]", depth - 1);
    const std::string inner = repeat("[2]", depth - 1) + E;
    const std::string ok = " { return 42; } return 1; }";
    if (name == "local") {
        return "func main() i32 { var a " + A + " = " + L + "; if (a" + I + " == " + V + ")" + ok;
    }
    if (name == "infer") {
        return "func main() i32 { var a = " + L + "; if (a" + I + " == " + V + ")" + ok;
    }
    if (name == "ret") {
        return "func mk() " + A + " { return " + L + "; }\nfunc main() i32 { var a = mk(); if (a" + I + " == " + V + ")" +
               ok;
    }
    if (name == "param") {
        return "func get(a " + A + ") " + E + " { return a" + I + "; }\nfunc main() i32 { var a " + A + " = " + L +
               "; if (get(a) == " + V + ")" + ok;
    }
    if (name == "field") {
        return "type H struct { a " + A + "; n i32; };\nfunc main() i32 { var h H = H{.a = " + L +
               ", .n = 1}; if (h.a" + I + " == " + V + " && h.n == 1)" + ok;
    }
    if (name == "global") {
        return "var g " + A + " = " + L + ";\nfunc main() i32 { if (g" + I + " == " + V + ")" + ok;
    }
    if (name == "gconst") {
        return "const g " + A + " = " + L + ";\nfunc main() i32 { if (g" + I + " == " + V + ")" + ok;
    }
    if (name == "ctconst") {
        return "comptime const g = " + L + ";\nfunc main() i32 { if (g" + I + " == " + V + ")" + ok;
    }
    if (name == "ctfunc") {
        return "comptime func mk() " + A + " { return " + L + "; }\nfunc main() i32 { var a = mk(); if (a" + I +
               " == " + V + ")" + ok;
    }
    if (name == "ctfunc_body") {
        return "comptime func mk(n i32) " + A + " { var x = n; var a " + A + " = " + L +
               "; return a; }\nfunc main() i32 { var a = mk(3); if (a" + I + " == " + V + ")" + ok;
    }
    if (name == "tuple") {
        return "func main() i32 { var t = (" + L + ", 7); var a = t[0]; if (a" + I + " == " + V + " && t[1] == 7)" +
               ok;
    }
    if (name == "multiret") {
        return "func mk() (" + A + ", i32) { return " + L + ", 7; }\nfunc main() i32 { var a, n = mk(); if (a" + I +
               " == " + V + " && n == 7)" + ok;
    }
    if (name == "generic") {
        return "func id<T type>(v T) T { return v; }\nfunc main() i32 { var a " + A + " = " + L +
               "; var b = id(a); if (b" + I + " == " + V + ")" + ok;
    }
    if (name == "slice") {
        return "func main() i32 { var a " + A + " = " + L + "; var s " + S + " = a[:]; if (s" + I + " == " + V +
               " && s.len == 2)" + ok;
    }
    if (name == "slice_infer") {
        return "func main() i32 { var a " + A + " = " + L + "; var s = a[:]; if (s" + I + " == " + V +
               " && s.len == 2)" + ok;
    }
    if (name == "slice_param") {
        return "func get(s " + S + ") " + E + " { return s" + I + "; }\nfunc main() i32 { var a " + A + " = " + L +
               "; if (get(a[:]) == " + V + ")" + ok;
    }
    if (name == "slice_ret") {
        return "func get(s " + S + ") " + S + " { return s[1:]; }\nfunc main() i32 { var a " + A + " = " + L +
               "; var t = get(a[:]); if (t[0]" + I1 + " == " + V + " && t.len == 1)" + ok;
    }
    if (name == "slice_field") {
        return "type H struct { s " + S + "; };\nfunc main() i32 { var a " + A + " = " + L +
               "; var h H = H{.s = a[:]}; if (h.s" + I + " == " + V + ")" + ok;
    }
    if (name == "slice_store") {
        return "func main() i32 { var a " + A + " = " + L + "; var s " + S + " = a[:]; s" + I + " = " + V +
               "; s[0]" + I1 + " = " + V + "; if (a[0]" + I1 + " == " + V + ")" + ok;
    }
    if (name == "ctfunc_slice_arg") {
        return "const g " + A + " = " + L + ";\ncomptime func get(s []const " + inner + ") " + E + " { return s" + I +
               "; }\nfunc main() i32 { if (get(g[:]) == " + V + ")" + ok;
    }
    if (name == "ctfunc_slice_ret") {
        return "const g " + A + " = " + L + ";\ncomptime func get() []const " + inner +
               " { return g[:]; }\nfunc main() i32 { var s = get(); if (s" + I + " == " + V + ")" + ok;
    }
    if (name == "foreach") {
        return "func main() i32 { var a " + A + " = " + L + "; var n = 0; for (var r : a) { n = n + 1; if (n == 2 && r" +
               I1 + " == " + V + ") { return 42; } } return 1; }";
    }
    if (name == "foreach_slice") {
        return "func main() i32 { var a " + A + " = " + L + "; var s " + S +
               " = a[:]; var n = 0; for (var r : s) { n = n + 1; if (n == 2 && r" + I1 + " == " + V +
               ") { return 42; } } return 1; }";
    }
    if (name == "sizeof") {
        return "func main() i32 { if (sizeof(" + A + ") == " + std::to_string(top) + " * sizeof(" + E + "))" + ok;
    }
    if (name == "copy") {
        if (E != "bool") {
            return "func main() i32 { var a " + A + " = " + L + "; var b " + A + " = a; b" + I + " = " + V + " + " + V +
                   "; if (a" + I + " == " + V + ")" + ok;
        }
        return "func main() i32 { var a " + A + " = " + L + "; var b " + A + " = a; b" + I + " = true; if (a" + I +
               " == " + V + ")" + ok;
    }
    if (name == "ptr") {
        return "func main() i32 { var a " + A + " = " + L + "; var p = &a; if ((*p)" + I + " == " + V + ")" + ok;
    }
    if (name == "ptr_typed") {
        return "func main() i32 { var a " + A + " = " + L + "; var p *" + A + " = &a; if (p" + I + " == " + V + ")" +
               ok;
    }
    if (name == "heap") {
        return "func main() i32 { var p = new " + A + "; p" + I + " = " + V + "; var ok = p" + I + " == " + V +
               "; delete p; if (ok)" + ok;
    }
    if (name == "len") {
        return "func main() i32 { var a " + A + " = " + L + "; if (a" + I1 + ".len == 2 && a.len == 2)" + ok;
    }
    if (name == "assert") {
        return "comptime const g = " + L + ";\nassert(g" + I + " == " + V + ", \"x\");\nfunc main() i32 { return 42; }";
    }
    if (name == "eq_struct_default") {
        return "type H struct { a " + A + " = " + L + "; };\nfunc main() i32 { var h H = H{}; if (h.a" + I + " == " + V +
               ")" + ok;
    }
    if (name == "ptr_slice") {
        return "func main() i32 { var a " + A + " = " + L + "; var s " + S + " = a[:]; var p = &s; if ((*p)" + I +
               " == " + V + ")" + ok;
    }
    if (name == "nested_fn") {
        return "func main() i32 { var a " + A + " = " + L + "; func inner() " + E + " { return a" + I +
               "; } if (inner() == " + V + ")" + ok;
    }
    if (name == "struct_arr") {
        return "type H struct { a " + A + "; };\nfunc main() i32 { var hs [2]H; hs[1].a = " + L +
               "; var s = hs[:]; if (s[1].a" + I + " == " + V + ")" + ok;
    }
    if (name == "slice_of_fields") {
        return "type H struct { a " + A + "; };\nfunc get(h *H) " + S +
               " { return h.a[:]; }\nfunc main() i32 { var h H = H{.a = " + L + "}; var s = get(&h); if (s" + I +
               " == " + V + ")" + ok;
    }
    if (name == "ret_slice_index") {
        return "func get(s " + S + ") " + S + " { return s; }\nfunc main() i32 { var a " + A + " = " + L +
               "; if (get(a[:])" + I + " == " + V + ")" + ok;
    }
    if (name == "ternary") {
        return "func main() i32 { var a " + A + " = " + L + "; var b " + A + " = " + L + "; var c = true; var s " + S +
               " = c ? a[:] : b[:]; if (s" + I + " == " + V + ")" + ok;
    }
    if (name == "assign_elem") {
        return "func main() i32 { var a " + A + " = " + L + "; var b " + A + "; b[1] = a[1]; if (b" + I + " == " + V +
               ")" + ok;
    }
    if (name == "unpack") {
        return "func mk() (" + S + ", i32) { return g[:], 7; }\nvar g " + A + " = " + L +
               ";\nfunc main() i32 { var s, n = mk(); if (s" + I + " == " + V + " && n == 7)" + ok;
    }
    return {};
}

void add_nest(std::vector<BatteryCase> &out) {
    const std::vector<std::string> contexts = {
        "local",       "infer",          "ret",          "param",           "field",
        "global",      "gconst",         "ctconst",      "ctfunc",          "ctfunc_body",
        "tuple",       "multiret",       "generic",      "slice",           "slice_infer",
        "slice_param", "slice_ret",      "slice_field",  "slice_store",     "ctfunc_slice_arg",
        "ctfunc_slice_ret", "foreach",   "foreach_slice", "sizeof",         "copy",
        "ptr",         "ptr_typed",      "heap",         "len",             "assert",
        "eq_struct_default", "ptr_slice", "nested_fn",   "struct_arr",      "slice_of_fields",
        "ret_slice_index", "ternary",    "assign_elem",  "unpack"};
    for (const std::string &context : contexts) {
        for (const std::string leaf : {"i32", "u8", "f64", "bool"}) {
            for (int depth = 1; depth <= 5; ++depth) {
                out.push_back({"nest", context + "_" + leaf + "_" + std::to_string(depth),
                               nest_case(context, depth, leaf) + "\n"});
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------
// Pointer shells `*`, `?*`, `[2]`, and `[]`, depth 1 to 3, with at least one pointer.

struct Leaf {
    std::string first;
    std::string second;
};

Leaf ptr_leaf(const std::string &E) {
    if (E == "i32") {
        return {"7", "9"};
    }
    if (E == "u8") {
        return {"7u8", "9u8"};
    }
    if (E == "f64") {
        return {"7.5", "9.5"};
    }
    if (E == "bool") {
        return {"true", "false"};
    }
    return {"P{.a = 1, .b = 7.5}", "P{.a = 2, .b = 9.5}"};
}

std::string join_shells(const std::vector<std::string> &shells, std::size_t from) {
    std::string out;
    for (std::size_t i = from; i < shells.size(); ++i) {
        out += shells[i];
    }
    return out;
}

std::string ptr_access(const std::vector<std::string> &shells, std::size_t from, std::string e) {
    for (std::size_t i = from; i < shells.size(); ++i) {
        const std::string &sh = shells[i];
        if (sh == "*") {
            e = "(*" + e + ")";
        } else if (sh == "?*") {
            e = "(*" + e + "!)";
        } else {
            e = e + "[1]";
        }
    }
    return e;
}

std::string leaf_cmp(const std::string &E, const std::string &e, int which) {
    if (E == "P") {
        return e + ".b == " + (which == 0 ? "7.5" : "9.5");
    }
    const Leaf leaf = ptr_leaf(E);
    return e + " == " + (which == 0 ? leaf.first : leaf.second);
}

std::string ptr_case(const std::string &c, const std::vector<std::string> &shells, const std::string &E) {
    const std::string T = join_shells(shells, 0) + E;
    std::vector<std::string> code{"var x0 " + E + " = " + ptr_leaf(E).first + ";"};
    for (std::size_t k = 1; k <= shells.size(); ++k) {
        const std::string &sh = shells[shells.size() - k];
        const std::string inner = join_shells(shells, shells.size() - k + 1) + E;
        const std::string here = join_shells(shells, shells.size() - k) + E;
        const std::string x = "x" + std::to_string(k);
        const std::string prev = "x" + std::to_string(k - 1);
        if (sh == "*" || sh == "?*") {
            code.push_back("var " + x + " " + here + " = &" + prev + ";");
        } else if (sh == "[2]") {
            code.push_back("var " + x + " " + here + " = [_]" + inner + "{" + prev + ", " + prev + "};");
        } else {
            const std::string a = "a" + std::to_string(k);
            code.push_back("var " + a + " [2]" + inner + " = [_]" + inner + "{" + prev + ", " + prev + "}; var " + x +
                           " " + here + " = " + a + "[:];");
        }
    }
    std::string built;
    for (std::size_t i = 0; i < code.size(); ++i) {
        built += (i == 0 ? "" : " ") + code[i];
    }
    const std::string top = "x" + std::to_string(shells.size());
    const auto acc = [&](const std::string &e) { return ptr_access(shells, 0, e); };
    const auto ok = [&](const std::string &e) { return "if (" + leaf_cmp(E, e, 0) + ") { return 42; } return 1;"; };
    const bool aliasing = std::all_of(shells.begin(), shells.end(), [](const std::string &s) { return s == "*" || s == "?*"; });
    const std::string leaf_type = E == "P" ? "f64" : E;
    const std::string first_value = E == "P" ? "7.5" : ptr_leaf(E).first;
    const bool array_first = shells[0] == "[2]" || shells[0] == "[]";
    const bool pointer_first = shells[0] == "*" || shells[0] == "?*";
    if (c == "local") {
        return "func main() i32 { " + built + " " + ok(acc(top)) + " }";
    }
    if (c == "infer") {
        return "func main() i32 { " + built + " var y = " + top + "; " + ok(acc("y")) + " }";
    }
    if (c == "param") {
        return "func get(p " + T + ") " + leaf_type + " { return " + acc("p") + (E == "P" ? ".b" : "") +
               "; }\nfunc main() i32 { " + built + " if (get(" + top + ") == " + first_value +
               ") { return 42; } return 1; }";
    }
    if (c == "ret") {
        return "func id(p " + T + ") " + T + " { return p; }\nfunc main() i32 { " + built + " var y = id(" + top +
               "); " + ok(acc("y")) + " }";
    }
    if (c == "generic") {
        return "func id<Q type>(p Q) Q { return p; }\nfunc main() i32 { " + built + " var y = id(" + top + "); " +
               ok(acc("y")) + " }";
    }
    if (c == "field") {
        return "type H struct { p " + T + "; n i32; };\nfunc main() i32 { " + built + " var h H = H{.p = " + top +
               ", .n = 3}; if (h.n != 3) { return 2; } " + ok(acc("h.p")) + " }";
    }
    if (c == "field_ptr") {
        return "type H struct { p " + T + "; };\nfunc rd(h *H) bool { return " + leaf_cmp(E, acc("h.p"), 0) +
               "; }\nfunc main() i32 { " + built + " var h H = H{.p = " + top +
               "}; if (rd(&h)) { return 42; } return 1; }";
    }
    if (c == "tuple") {
        return "func main() i32 { " + built + " var t = (" + top + ", 5); var y = t[0]; if (t[1] != 5) { return 2; } " +
               ok(acc("y")) + " }";
    }
    if (c == "tuple_direct") {
        return "func main() i32 { " + built + " var t = (" + top + ", 5); " + ok(acc("t[0]")) + " }";
    }
    if (c == "multiret") {
        return "func mk(p " + T + ") (" + T + ", i32) { return p, 5; }\nfunc main() i32 { " + built +
               " var y, n = mk(" + top + "); if (n != 5) { return 2; } " + ok(acc("y")) + " }";
    }
    if (c == "global") {
        return "var gp " + T + ";\nfunc main() i32 { " + built + " gp = " + top + "; " + ok(acc("gp")) + " }";
    }
    if (c == "store") {
        if (!aliasing) {
            return {};
        }
        return "func main() i32 { " + built + " " + acc(top) + " = " + ptr_leaf(E).second + "; if (" +
               leaf_cmp(E, "x0", 1) + ") { return 42; } return 1; }";
    }
    if (c == "store_param") {
        if (!aliasing) {
            return {};
        }
        return "func put(p " + T + ") void { " + acc("p") + " = " + ptr_leaf(E).second + "; }\nfunc main() i32 { " +
               built + " put(" + top + "); if (" + leaf_cmp(E, "x0", 1) + ") { return 42; } return 1; }";
    }
    if (c == "outparam") {
        return "func set(dst *" + T + ", v " + T + ") void { *dst = v; }\nfunc main() i32 { " + built + " var y " + T +
               " = " + top + "; set(&y, " + top + "); " + ok(acc("y")) + " }";
    }
    if (c == "ternary") {
        return "func main() i32 { " + built + " var c = true; var y = c ? " + top + " : " + top + "; " + ok(acc("y")) +
               " }";
    }
    if (c == "foreach") {
        if (!array_first) {
            return {};
        }
        return "func main() i32 { " + built + " var n = 0; for (var r : " + top + ") { n = n + 1; if (n == 2 && " +
               leaf_cmp(E, ptr_access(shells, 1, "r"), 0) + ") { return 42; } } return 1; }";
    }
    if (c == "null") {
        if (!pointer_first) {
            return {};
        }
        return "func main() i32 { " + built + " if (" + top + " != null) { return 42; } return 1; }";
    }
    if (c == "sizeof") {
        if (!pointer_first) {
            return {};
        }
        return "func main() i32 { if (sizeof(" + T + ") == 8) { return 42; } return 1; }";
    }
    if (c == "heap") {
        return "func main() i32 { " + built + " var hp = new " + T + "; *hp = " + top + "; var good = " +
               leaf_cmp(E, acc("(*hp)"), 0) + "; delete hp; if (good) { return 42; } return 1; }";
    }
    if (c == "addr_of_elem") {
        if (!array_first) {
            return {};
        }
        return "func main() i32 { " + built + " var q = &" + top + "[1]; " + ok(ptr_access(shells, 1, "(*q)")) + " }";
    }
    if (c == "nested_fn_arg") {
        return "func apply(f func (" + T + ") bool, p " + T + ") bool { return f(p); }\nfunc chk(p " + T +
               ") bool { return " + leaf_cmp(E, acc("p"), 0) + "; }\nfunc main() i32 { " + built + " if (apply(chk, " +
               top + ")) { return 42; } return 1; }";
    }
    if (c == "alias") {
        // An alias is a different type. The cast confirms it.
        return "type A " + T + ";\nfunc main() i32 { " + built + " var y A = A(" + top + "); " + ok(acc("y")) + " }";
    }
    if (c == "method") {
        if (E != "P") {
            return {};
        }
        return "func main() i32 { " + built + " if (" + acc(top) + ".get() == 7.5 && " + acc(top) +
               ".val() == 7.5) { return 42; } return 1; }";
    }
    if (c == "method_param") {
        if (E != "P") {
            return {};
        }
        return "func rd(p " + T + ") f64 { return " + acc("p") + ".get(); }\nfunc main() i32 { " + built + " if (rd(" +
               top + ") == 7.5) { return 42; } return 1; }";
    }
    if (c == "copy_out") {
        return "func main() i32 { " + built + " var leaf = " + acc(top) + "; " + ok("leaf") + " }";
    }
    if (c == "cmp_ptr") {
        if (!pointer_first) {
            return {};
        }
        return "func main() i32 { " + built + " var y = " + top + "; if (y == " + top +
               ") { return 42; } return 1; }";
    }
    if (c == "swap") {
        return "func main() i32 { " + built + " var a = " + top + "; var b = " + top + "; a, b = (b, a); " +
               ok(acc("a")) + " }";
    }
    if (c == "defer_ptr") {
        return "func chk(p " + T + ") bool { return " + leaf_cmp(E, acc("p"), 0) + "; }\nfunc main() i32 { " + built +
               " var good = false; { defer good = chk(" + top + "); } if (good) { return 42; } return 1; }";
    }
    return {};
}

void add_ptr(std::vector<BatteryCase> &out) {
    const std::string pre = "type P struct { a i32; b f64; };\nfunc P::get(self *this) f64 { return self.b; }\n"
                            "func P::val(self this) f64 { return self.b; }\n";
    const std::vector<std::string> contexts = {
        "local",   "infer",        "param",         "ret",      "generic",   "field",   "field_ptr",
        "tuple",   "tuple_direct", "multiret",      "global",   "store",     "store_param", "outparam",
        "ternary", "foreach",      "null",          "sizeof",   "heap",      "addr_of_elem", "nested_fn_arg",
        "alias",   "method",       "method_param",  "copy_out", "cmp_ptr",   "swap",    "defer_ptr"};
    std::vector<std::vector<std::string>> shapes;
    for (auto &shape : shell_product({"*", "?*", "[2]", "[]"}, 3)) {
        if (std::any_of(shape.begin(), shape.end(), [](const std::string &s) { return s == "*" || s == "?*"; })) {
            shapes.push_back(std::move(shape));
        }
    }
    for (const std::string &context : contexts) {
        for (const auto &shape : shapes) {
            for (const std::string leaf : {"i32", "u8", "f64", "P"}) {
                const std::string source = ptr_case(context, shape, leaf);
                if (source.empty()) {
                    continue;
                }
                std::string code;
                for (const std::string &sh : shape) {
                    code += shell_code(sh);
                }
                out.push_back({"ptr", context + "__" + code + "__" + leaf, pre + source + "\n"});
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------
// Tuple shells: `t0` and `t1` put the path at element 0 or 1 of a pair, with an f64 beside it.

struct TupleGen {
    std::vector<std::pair<std::string, std::string>> aliases;
    int n = 0;
    std::vector<std::string> code;

    std::string fresh(const std::string &prefix = "v") { return prefix + std::to_string(++n); }

    std::string spell(const std::vector<std::string> &shells, std::size_t from, const std::string &E,
                      bool named = false) {
        if (from == shells.size()) {
            return E;
        }
        const std::string &k = shells[from];
        if (k == "t0" || k == "t1") {
            const std::string a = spell(shells, from + 1, E);
            const std::string t = k == "t0" ? "(" + a + ", f64)" : "(f64, " + a + ")";
            if (named) {
                for (const auto &[name, spelled] : aliases) {
                    if (spelled == t) {
                        return name;
                    }
                }
                const std::string name = "T" + std::to_string(aliases.size() + 1);
                aliases.emplace_back(name, t);
                return name;
            }
            return t;
        }
        return k + spell(shells, from + 1, E, true);
    }

    std::string build(const std::vector<std::string> &shells, std::size_t from, const std::string &E,
                      bool named = false) {
        const std::string T = spell(shells, from, E, named);
        if (from == shells.size()) {
            const std::string v = fresh();
            code.push_back("var " + v + " " + E + " = " + ptr_leaf(E).first + ";");
            return v;
        }
        const std::string &k = shells[from];
        if (k == "t0" || k == "t1") {
            const std::string a = build(shells, from + 1, E);
            const std::string lit = k == "t0" ? "(" + a + ", 0.25)" : "(0.25, " + a + ")";
            const std::string v = fresh();
            if (named) {
                code.push_back("var " + v + " " + T + " = " + T + "(" + lit + ");");
            } else {
                code.push_back("var " + v + " " + T + " = " + lit + ";");
            }
            return v;
        }
        const std::string a = build(shells, from + 1, E, true);
        const std::string sub = spell(shells, from + 1, E, true);
        const std::string v = fresh();
        if (k == "[2]") {
            code.push_back("var " + v + " " + T + " = [_]" + sub + "{" + a + ", " + a + "};");
        } else if (k == "[]") {
            const std::string arr = fresh("a");
            code.push_back("var " + arr + " [2]" + sub + " = [_]" + sub + "{" + a + ", " + a + "}; var " + v + " " + T +
                           " = " + arr + "[:];");
        } else {
            code.push_back("var " + v + " " + T + " = &" + a + ";");
        }
        return v;
    }
};

std::string tup_access(const std::vector<std::string> &shells, std::size_t from, std::string e) {
    for (std::size_t i = from; i < shells.size(); ++i) {
        const std::string &k = shells[i];
        if (k == "t0") {
            e += "[0]";
        } else if (k == "t1" || k == "[2]" || k == "[]") {
            e += "[1]";
        } else if (k == "*") {
            e = "(*" + e + ")";
        } else {
            e = "(*" + e + "!)";
        }
    }
    return e;
}

std::string tup_case(const std::string &c, const std::vector<std::string> &shells, const std::string &E) {
    TupleGen g;
    const std::string top = g.build(shells, 0, E);
    const std::string T = g.spell(shells, 0, E);
    const std::string NT = g.spell(shells, 0, E, true);
    std::string code;
    for (std::size_t i = 0; i < g.code.size(); ++i) {
        code += (i == 0 ? "" : " ") + g.code[i];
    }
    const auto acc = [&](const std::string &e) { return tup_access(shells, 0, e); };
    const auto ok = [&](const std::string &e) { return "if (" + leaf_cmp(E, e, 0) + ") { return 42; } return 1;"; };
    const std::string LT = E == "P" ? "f64" : E;
    const auto lv = [&](const std::string &e) { return E == "P" ? e + ".b" : e; };
    const bool first_is_tuple = shells[0] == "t0" || shells[0] == "t1";
    std::string body;
    std::string extra;
    std::vector<std::string> pointed{"*"};
    pointed.insert(pointed.end(), shells.begin(), shells.end());
    if (c == "local") {
        body = code + " " + ok(acc(top));
    } else if (c == "infer") {
        body = code + " var y = " + top + "; " + ok(acc("y"));
    } else if (c == "param") {
        extra = "func get(p " + T + ") " + LT + " { return " + lv(acc("p")) + "; }\n";
        body = code + " if (get(" + top + ") == " + (E == "P" ? "7.5" : ptr_leaf(E).first) +
               ") { return 42; } return 1;";
    } else if (c == "ret") {
        extra = "func id(p " + T + ") " + T + " { return p; }\n";
        body = code + " var y = id(" + top + "); " + ok(acc("y"));
    } else if (c == "ret_direct") {
        extra = "func id(p " + T + ") " + T + " { return p; }\n";
        body = code + " " + ok(acc("id(" + top + ")"));
    } else if (c == "generic") {
        extra = "func id<Q type>(p Q) Q { return p; }\n";
        body = code + " var y = id(" + top + "); " + ok(acc("y"));
    } else if (c == "field") {
        extra = "type H struct { p " + T + "; n i32; };\n";
        body = code + " var h H = H{.p = " + top + ", .n = 3}; if (h.n != 3) { return 2; } " + ok(acc("h.p"));
    } else if (c == "field_ptr") {
        extra = "type H struct { p " + T + "; };\nfunc rd(h *H) bool { return " + leaf_cmp(E, acc("h.p"), 0) + "; }\n";
        body = code + " var h H = H{.p = " + top + "}; if (rd(&h)) { return 42; } return 1;";
    } else if (c == "wrap") {
        body = code + " var w = (" + top + ", 5); if (w[1] != 5) { return 2; } " + ok(acc("w[0]"));
    } else if (c == "multiret") {
        extra = "func mk(p " + T + ") (" + T + ", i32) { return p, 5; }\n";
        body = code + " var y, n = mk(" + top + "); if (n != 5) { return 2; } " + ok(acc("y"));
    } else if (c == "global") {
        extra = "var gp " + T + ";\n";
        body = code + " gp = " + top + "; " + ok(acc("gp"));
    } else if (c == "store") {
        body = code + " var y " + T + " = " + top + "; " + acc("y") + " = " + ptr_leaf(E).second + "; if (" +
               leaf_cmp(E, acc("y"), 1) + ") { return 42; } return 1;";
    } else if (c == "copy") {
        if (!std::all_of(shells.begin(), shells.end(),
                         [](const std::string &k) { return k == "t0" || k == "t1" || k == "[2]"; })) {
            return {};
        }
        body = code + " var y = " + top + "; " + acc("y") + " = " + ptr_leaf(E).second + "; if (" +
               leaf_cmp(E, acc(top), 0) + " && " + leaf_cmp(E, acc("y"), 1) + ") { return 42; } return 1;";
    } else if (c == "outparam") {
        extra = "func set(dst *" + NT + ", v " + NT + ") void { *dst = v; }\n";
        body = code + " var y " + NT + " = " + (first_is_tuple ? NT + "(" + top + ")" : top) + "; set(&y, y); " +
               ok(acc("y"));
    } else if (c == "ptr_to") {
        body = code + " var q = &" + top + "; " + ok(tup_access(pointed, 0, "q"));
    } else if (c == "ternary") {
        body = code + " var c = true; var y = c ? " + top + " : " + top + "; " + ok(acc("y"));
    } else if (c == "foreach") {
        if (shells[0] != "[2]" && shells[0] != "[]") {
            return {};
        }
        body = code + " var n = 0; for (var r : " + top + ") { n = n + 1; if (n == 2 && " +
               leaf_cmp(E, tup_access(shells, 1, "r"), 0) + ") { return 42; } } return 1;";
    } else if (c == "unpack") {
        if (!first_is_tuple) {
            return {};
        }
        const std::string name = shells[0] == "t0" ? "ua" : "ub";
        body = code + " var ua, ub = " + top + "; " + ok(tup_access(shells, 1, name));
    } else if (c == "heap") {
        if (!first_is_tuple) {
            return {};
        }
        body = code + " var hp = new " + NT + "; *hp = " + NT + "(" + top + "); var good = " +
               leaf_cmp(E, tup_access(pointed, 0, "hp"), 0) + "; delete hp; if (good) { return 42; } return 1;";
    } else if (c == "sizeof") {
        if (!first_is_tuple) {
            return {};
        }
        body = "var x " + T + "; if (sizeof(" + NT + ") == sizeof(x)) { return 42; } return 1;";
    } else if (c == "nested_fn_arg") {
        extra = "func apply(f func (" + T + ") bool, p " + T + ") bool { return f(p); }\nfunc chk(p " + T +
                ") bool { return " + leaf_cmp(E, acc("p"), 0) + "; }\n";
        body = code + " if (apply(chk, " + top + ")) { return 42; } return 1;";
    } else if (c == "swap") {
        body = code + " var a = " + top + "; var b = " + top + "; a, b = (b, a); " + ok(acc("a"));
    } else if (c == "addr_elem") {
        body = code + " var q = &" + acc(top) + "; if (" + leaf_cmp(E, "(*q)", 0) + ") { return 42; } return 1;";
    } else {
        return {};
    }
    std::string al;
    for (const auto &[name, spelled] : g.aliases) {
        al += "type " + name + " " + spelled + ";\n";
    }
    return "type P struct { a i32; b f64; };\n" + al + extra + "func main() i32 { " + body + " }\n";
}

void add_tup(std::vector<BatteryCase> &out) {
    const std::vector<std::string> contexts = {"local",   "infer",    "param",   "ret",       "ret_direct", "generic",
                                               "field",   "field_ptr", "wrap",   "multiret",  "global",     "store",
                                               "copy",    "outparam", "ptr_to",  "ternary",   "foreach",    "unpack",
                                               "heap",    "sizeof",   "nested_fn_arg", "swap", "addr_elem"};
    std::vector<std::vector<std::string>> shapes;
    for (auto &shape : shell_product({"t0", "t1", "[2]", "[]", "*", "?*"}, 3)) {
        if (std::any_of(shape.begin(), shape.end(), [](const std::string &s) { return s == "t0" || s == "t1"; })) {
            shapes.push_back(std::move(shape));
        }
    }
    for (const std::string &context : contexts) {
        for (const auto &shape : shapes) {
            for (const std::string leaf : {"i32", "u8", "f64", "P"}) {
                const std::string source = tup_case(context, shape, leaf);
                if (source.empty()) {
                    continue;
                }
                std::string code;
                for (std::size_t i = 0; i < shape.size(); ++i) {
                    code += (i == 0 ? "" : ".") + shell_code(shape[i]);
                }
                out.push_back({"tup", context + "__" + code + "__" + leaf, source});
            }
        }
    }
}

} // namespace

inline std::vector<BatteryCase> shape_battery_cases() {
    std::vector<BatteryCase> out;
    add_nest(out);
    add_ptr(out);
    add_tup(out);
    return out;
}

} // namespace tlc::test

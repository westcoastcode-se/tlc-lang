// Arithmetic batteries. The checker and codegen each fold constants. A case compares a folded constant
// with the same operation on variables at run time, so the two folders and codegen must agree.
#pragma once

#include "battery.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace tlc::test {
namespace {

const std::vector<std::string> kOps = {"<", ">", "<=", ">=", "==", "!=", "/", "%", ">>", "+", "-", "*", "&", "|", "^"};

// The number written before any suffix: `0u32` is 0, `-7` is -7.
bool literal_is_zero(const std::string &literal) {
    const std::size_t end = literal.find_first_of("ui");
    const std::string digits = literal.substr(0, end);
    return digits == "0" || digits == "-0";
}

const std::string kPrintf = "extern(abi=\"c\") func printf(fmt *const u8, ...) i32;\n";

// `u32`, `u64`, and `i64` against each other and against `i32`, at file scope.
BatteryCase wide_fold() {
    const std::vector<std::pair<std::string, std::vector<std::string>>> vals = {
        {"u32", {"0u32", "1u32", "5u32", "2147483647u32", "4294967295u32"}},
        {"u64", {"0u64", "3u64", "18446744073709551615u64"}},
        {"i64", {"-1i64", "7i64", "-9223372036854775807i64"}},
    };
    const std::vector<std::string> small = {"-1", "2", "-7", "28", "3"};
    const std::vector<std::string> shift_ok = {"2", "3", "28", "1u32", "5u32", "3u64", "7i64"};
    std::vector<std::pair<std::string, std::string>> pairs;
    for (const auto &[type, values] : vals) {
        for (const std::string &a : values) {
            for (const std::string &b : values) {
                pairs.emplace_back(a, b);
            }
            for (const std::string &b : small) {
                pairs.emplace_back(a, b);
                pairs.emplace_back(b, a);
            }
        }
    }
    std::string head = kPrintf;
    std::string body;
    int n = 0;
    for (const auto &[a, b] : pairs) {
        for (const std::string &op : kOps) {
            if ((op == "/" || op == "%") && literal_is_zero(b)) {
                continue;
            }
            if (op == ">>" && std::find(shift_ok.begin(), shift_ok.end(), b) == shift_ok.end()) {
                continue;
            }
            ++n;
            const std::string id = std::to_string(n);
            head += "const c" + id + " = (" + a + ") " + op + " (" + b + ");\n";
            body += "    { var l" + id + " = " + a + "; var r" + id + " = " + b + "; if ((l" + id + " " + op + " r" + id +
                    ") != c" + id + ") { printf(\"FAIL " + a + " " + op + " " + b + "\\n\"b); } }\n";
        }
    }
    return {"arith", "wide_fold", head + "func main() i32 {\n" + body + "    return 0;\n}\n", true};
}

// `u8`, `i8`, `u16`, and `i16` against each other and against wider types, in three kinds of constant.
BatteryCase narrow_fold(const std::string &kind) {
    const std::map<std::string, std::vector<std::string>> vals = {
        {"u8", {"0u8", "250u8", "7u8"}},         {"i8", {"-100i8", "5i8"}},
        {"u16", {"60000u16", "3u16"}},           {"i16", {"-300i16", "9i16"}},
        {"i32", {"-1", "2", "-7", "28"}},        {"u32", {"4294967295u32", "5u32"}},
        {"i64", {"-9i64", "7i64"}},
    };
    const std::vector<std::string> narrow = {"u8", "i8", "u16", "i16"};
    const std::vector<std::string> others = {"i32", "u32", "i64", "u8", "i8", "u16", "i16"};
    std::string head = kPrintf;
    std::string body;
    int n = 0;
    for (const std::string &t : narrow) {
        for (const std::string &a : vals.at(t)) {
            for (const std::string &o : others) {
                for (const std::string &b : vals.at(o)) {
                    for (const std::string &op : kOps) {
                        if ((op == "/" || op == "%") && literal_is_zero(b)) {
                            continue;
                        }
                        if (op == ">>" && (b[0] == '-' || b == "4294967295u32" || b == "60000u16" || b == "250u8")) {
                            continue;
                        }
                        const std::string id = std::to_string(n++);
                        const std::string prefix = kind == "ct" ? "comptime const " : "const ";
                        const std::string decl =
                            prefix + "ca" + id + " = " + a + "; " + prefix + "cb" + id + " = " + b + ";";
                        if (kind == "file") {
                            head += decl + "\n";
                        } else {
                            body += "    " + decl + "\n";
                        }
                        body += "    { var x" + id + " = " + a + "; var y" + id + " = " + b + "; if ((ca" + id + " " + op +
                                " cb" + id + ") != (x" + id + " " + op + " y" + id + ")) { printf(\"FAIL " + a + " " + op +
                                " " + b + "\\n\"b); } }\n";
                    }
                }
            }
        }
    }
    return {"arith", "narrow_fold_" + kind, head + "func main() i32 {\n" + body + "    return 0;\n}\n", true};
}

// `e op= m` stores what `e = T(e op m)` stores, for each place type and each right side.
BatteryCase compound() {
    const std::vector<std::pair<std::string, std::vector<std::string>>> types = {
        {"u8", {"250u8", "7u8"}},           {"i8", {"-100i8", "5i8"}},
        {"u16", {"60000u16", "3u16"}},      {"i16", {"-300i16", "9i16"}},
        {"i32", {"-7", "100000"}},          {"u32", {"4000000000u32", "5u32"}},
        {"i64", {"-9i64", "123456789012i64"}},
    };
    const std::vector<std::string> rhs = {"3", "-2", "7u8", "-5i8", "2u32", "-3i64", "1u16"};
    const std::vector<std::string> ops = {"+", "-", "*", "/", "%", "&", "|", "^", "<<", ">>"};
    std::string body;
    for (const auto &[t, values] : types) {
        for (const std::string &v : values) {
            for (const std::string &r : rhs) {
                for (const std::string &op : ops) {
                    if ((op == "<<" || op == ">>") && r[0] == '-') {
                        continue;
                    }
                    body += "    { var a " + t + " = " + v + "; var b " + t + " = " + v + "; var m = " + r + "; a " + op +
                            "= m; b = " + t + "(b " + op + " m); if (a != b) { printf(\"FAIL " + t + " " + v + " " + op +
                            "= " + r + "\\n\"b); } }\n";
                }
            }
        }
    }
    return {"arith", "compound", kPrintf + "func main() i32 {\n" + body + "    return 0;\n}\n", true};
}

} // namespace

inline std::vector<BatteryCase> arithmetic_battery_cases() {
    return {wide_fold(), narrow_fold("file"), narrow_fold("local"), narrow_fold("ct"), compound()};
}

} // namespace tlc::test

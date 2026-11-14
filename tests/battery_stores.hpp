// The store battery. Each value type stored into each kind of place, by `=` and by `+=`.
//
// One program runs the same statements in a function and in a `comptime func` and returns 42 when the two
// agree. A `comptime func` runs in the compiler's own process, where a store wider than its place
// overwrote the compiler's frame. The tests set `TLC_CHECK_STORES`, so such a store also stops the compiler.
#pragma once

#include "battery.hpp"

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace tlc::test {

inline std::vector<BatteryCase> store_battery_cases() {
    static const std::array<std::pair<const char *, const char *>, 12> types = {{
        {"i8", "-5i8"},
        {"u8", "250u8"},
        {"i16", "-300i16"},
        {"u16", "60000u16"},
        {"i32", "-7"},
        {"u32", "3000000000u32"},
        {"i64", "4294967297i64"},
        {"u64", "18446744073709551615u64"},
        {"f32", "2.5f32"},
        {"f64", "-3.75"},
        {"bool", "true"},
        {"char", "'A'"},
    }};
    struct Place {
        const char *name;
        std::string decl;
        std::string pre;
        std::string target;
        std::string read;
    };
    const auto places = [](const std::string &slot, const std::string &literal) {
        const bool plain = slot == "bool" || slot == "char";
        return std::vector<Place>{
            {"local", "", "var p " + slot + ";", "p", "p"},
            {"inferred", "", plain ? "var p = " + literal + ";" : "var p = " + slot + "(" + literal + ");", "p", "p"},
            {"global", "var g " + slot + ";", "", "g", "g"},
            {"field", "type S struct { a u8; f " + slot + "; b u8; };", "var s S;", "s.f", "s.f"},
            {"element", "", "var arr [3]" + slot + ";", "arr[1]", "arr[1]"},
            {"pointee", "", "var t " + slot + "; var q *" + slot + " = &t;", "*q", "t"},
            {"inferred_ptr", "", "var t " + slot + "; var q = &t;", "*q", "t"},
            {"ptr_field", "type S struct { a u8; f " + slot + "; b u8; };", "var s S; var q = &s;", "q.f", "s.f"},
            {"union_arm", "type U union { f " + slot + "; w i64; };", "var u U;", "u.f", "u.f"},
            {"tuple", "", "var t (" + slot + ", u8) = (" + literal + ", 1u8);", "t[0]", "t[0]"},
            {"nested", "type I struct { x u8; f " + slot + "; }; type O struct { c u8; i I; };", "var o O;", "o.i.f",
             "o.i.f"},
        };
    };
    const auto digest = [](const std::string &slot) -> std::string {
        if (slot == "bool") {
            return "keep ? 1 : 0";
        }
        if (slot == "f32" || slot == "f64") {
            return "i32(keep * 4.0) & 255";
        }
        return "i32(keep) & 255";
    };
    std::vector<BatteryCase> cases;
    for (const auto &[slot, slot_literal] : types) {
        for (const auto &[value_type, value] : types) {
            for (const Place &place : places(slot, slot_literal)) {
                for (const char *op : {"=", "+="}) {
                    const std::string stmts = place.pre + "\n    var v = " + std::string(value) + ";\n    " +
                                              place.target + " " + op + " v;\n    var keep = " + place.read + ";";
                    const std::string ret = "    return " + digest(slot) + ";\n}\n";
                    BatteryCase item;
                    item.battery = "store";
                    item.name = std::string(place.name) + "_" + slot + "_" + value_type + "_" +
                                (std::string(op) == "=" ? "eq" : "pluseq");
                    item.source = place.decl + "\nfunc at_run() i32 {\n    " + stmts + "\n" + ret +
                                  "comptime func at_compile() i32 {\n    " + stmts + "\n" + ret +
                                  "func main() i32 {\n    const k = at_compile();\n    return at_run() == k ? 42 : 1;\n}\n";
                    cases.push_back(std::move(item));
                }
            }
        }
    }
    return cases;
}

} // namespace tlc::test

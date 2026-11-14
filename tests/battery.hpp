// Generated programs for the battery tests.
#pragma once

#include <string>
#include <vector>

namespace tlc::test {

struct BatteryCase {
    // `nest`, `ptr`, `tup`, or an arithmetic battery.
    std::string battery;
    std::string name;
    std::string source;
    // An arithmetic battery prints `FAIL` lines through C `printf` and exits 0.
    // A shape battery returns 42.
    bool prints = false;
};

// Nested arrays, pointer shells, and tuple shells in each context a value can be in.
std::vector<BatteryCase> shape_battery_cases();

// Each folded constant against the same operation at run time, and `e op= m` against `e = T(e op m)`.
std::vector<BatteryCase> arithmetic_battery_cases();

} // namespace tlc::test

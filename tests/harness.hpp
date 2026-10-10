#pragma once

#include <string>
#include <utility>
#include <vector>

namespace tlc::test {

struct Check {
    int failed = 0;
    void fail(const std::string &message);
    void expect(bool cond, const char *expr, const char *file, int line);
    void expect_eq(const std::string &got, const std::string &want, const char *file, int line);
    void expect_contains(const std::string &hay, const std::string &needle, const char *file, int line);
};

struct Proc {
    int code = 0;
    std::string out;
    std::string err;
};

Proc spawn(const std::vector<std::string> &args,
          const std::vector<std::pair<std::string, std::string>> &env = {});

} // namespace tlc::test

#define EXPECT(check, cond) (check).expect(static_cast<bool>(cond), #cond, __FILE__, __LINE__)
#define EXPECT_EQ(check, got, want) (check).expect_eq((got), (want), __FILE__, __LINE__)
#define EXPECT_HAS(check, hay, needle) (check).expect_contains((hay), (needle), __FILE__, __LINE__)

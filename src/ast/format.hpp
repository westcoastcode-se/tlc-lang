#pragma once

#include "ast/ast.hpp"

#include <optional>
#include <string>
#include <utility>

namespace tlc {

// `{0}` is that element of a tuple literal. `{{` and `}}` are the braces themselves.
// A string element is its text. An integer element is decimal. Anything else fails.
template <typename AsInt, typename AsString>
std::optional<std::string> format_placeholders(const std::string &fmt, const Expr &value, AsInt as_int,
                                              AsString as_string) {
    const auto *tuple = std::get_if<Expr::Init>(&value.data);
    if (tuple == nullptr || !tuple->is_tuple || tuple->elements.size() < 2) {
        return std::nullopt;
    }
    std::string out;
    for (std::size_t i = 0; i < fmt.size();) {
        if (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == '{') {
            out.push_back('{');
            i += 2;
            continue;
        }
        if (fmt[i] == '}' && i + 1 < fmt.size() && fmt[i + 1] == '}') {
            out.push_back('}');
            i += 2;
            continue;
        }
        if (fmt[i] != '{') {
            out.push_back(fmt[i]);
            ++i;
            continue;
        }
        std::size_t cursor = i + 1;
        if (cursor >= fmt.size() || fmt[cursor] < '0' || fmt[cursor] > '9') {
            return std::nullopt;
        }
        int index = 0;
        while (cursor < fmt.size() && fmt[cursor] >= '0' && fmt[cursor] <= '9') {
            const int digit = fmt[cursor] - '0';
            if (index > (2147483647 - digit) / 10) {
                return std::nullopt;
            }
            index = index * 10 + digit;
            ++cursor;
        }
        if (cursor >= fmt.size() || fmt[cursor] != '}' || index < 0 ||
            static_cast<std::size_t>(index) >= tuple->elements.size() || tuple->elements[static_cast<std::size_t>(index)] == nullptr) {
            return std::nullopt;
        }
        const Expr &element = *tuple->elements[static_cast<std::size_t>(index)];
        if (const std::optional<std::string> text = as_string(element)) {
            out += *text;
        } else if (const std::optional<int> number = as_int(element)) {
            out += std::to_string(*number);
        } else {
            return std::nullopt;
        }
        i = cursor + 1;
    }
    return out;
}

} // namespace tlc

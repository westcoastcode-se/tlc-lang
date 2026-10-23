// JSON and the bytes on stdin and stdout.
#include "lsp/lsp.hpp"

#include "diag/diag.hpp"
#include "driver/driver.hpp"
#include "lex/lexer.hpp"
#include "parse/parser.hpp"
#include "pkg/pkg.hpp"
#include "check/sema.hpp"

#include <cerrno>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <unistd.h>

#include "lsp/internal.hpp"

namespace tlc::lsp_impl {


Json jnull() { return {}; }

Json jbool(bool value) {
    Json json;
    json.kind = Json::Kind::Bool;
    json.b = value;
    return json;
}

Json jint(int64_t value) {
    Json json;
    json.kind = Json::Kind::Int;
    json.i = value;
    return json;
}

Json jstr(std::string value) {
    Json json;
    json.kind = Json::Kind::String;
    json.s = std::move(value);
    return json;
}

Json jarr(std::vector<Json> value) {
    Json json;
    json.kind = Json::Kind::Array;
    json.a = std::move(value);
    return json;
}

Json jobj(std::vector<std::pair<std::string, Json>> value) {
    Json json;
    json.kind = Json::Kind::Object;
    json.o = std::move(value);
    return json;
}

void dump_string(std::string &out, std::string_view text) {
    out.push_back('"');
    for (unsigned char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20) {
                constexpr char kHex[] = "0123456789abcdef";
                out += "\\u00";
                out.push_back(kHex[c >> 4]);
                out.push_back(kHex[c & 0x0f]);
            } else {
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    out.push_back('"');
}

void dump(std::string &out, const Json &json) {
    switch (json.kind) {
    case Json::Kind::Null:
        out += "null";
        break;
    case Json::Kind::Bool:
        out += json.b ? "true" : "false";
        break;
    case Json::Kind::Int:
        out += std::to_string(json.i);
        break;
    case Json::Kind::String:
        dump_string(out, json.s);
        break;
    case Json::Kind::Array:
        out.push_back('[');
        for (std::size_t n = 0; n < json.a.size(); ++n) {
            if (n != 0) {
                out.push_back(',');
            }
            dump(out, json.a[n]);
        }
        out.push_back(']');
        break;
    case Json::Kind::Object:
        out.push_back('{');
        for (std::size_t n = 0; n < json.o.size(); ++n) {
            if (n != 0) {
                out.push_back(',');
            }
            dump_string(out, json.o[n].first);
            out.push_back(':');
            dump(out, json.o[n].second);
        }
        out.push_back('}');
        break;
    }
}

std::optional<Json> parse_json(std::string_view text) {
    Parse parser{text, 0, true};
    Json value = parser.parse_value();
    parser.skip();
    if (!parser.ok || parser.at != text.size()) {
        return std::nullopt;
    }
    return value;
}

bool write_all(int fd, std::string_view bytes) {
    while (!bytes.empty()) {
        const ssize_t wrote = ::write(fd, bytes.data(), bytes.size());
        if (wrote < 0 && errno == EINTR) {
            continue;
        }
        if (wrote <= 0) {
            return false;
        }
        bytes.remove_prefix(static_cast<std::size_t>(wrote));
    }
    return true;
}

bool read_exact(std::string &out, std::size_t count) {
    out.assign(count, '\0');
    std::size_t got = 0;
    while (got < count) {
        const ssize_t n = ::read(STDIN_FILENO, out.data() + got, count - got);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return false;
        }
        got += static_cast<std::size_t>(n);
    }
    return true;
}

std::optional<std::string> read_message() {
    std::string header;
    char byte = 0;
    while (header.size() < 4 || header.substr(header.size() - 4) != "\r\n\r\n") {
        const ssize_t n = ::read(STDIN_FILENO, &byte, 1);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return std::nullopt;
        }
        header.push_back(byte);
        if (header.size() > 1024 * 1024) {
            return std::nullopt;
        }
        if (header.size() >= 2 && header.substr(header.size() - 2) == "\n\n") {
            break;
        }
    }
    std::size_t length = 0;
    bool saw = false;
    std::size_t line = 0;
    while (line < header.size()) {
        const std::size_t end = header.find('\n', line);
        const std::size_t stop = end == std::string::npos ? header.size() : end;
        std::string_view row(header.data() + line, stop - line);
        if (!row.empty() && row.back() == '\r') {
            row.remove_suffix(1);
        }
        constexpr std::string_view kKey = "content-length:";
        if (row.size() >= kKey.size()) {
            bool match = true;
            for (std::size_t n = 0; n < kKey.size(); ++n) {
                char c = row[n];
                if (c >= 'A' && c <= 'Z') {
                    c = static_cast<char>(c - 'A' + 'a');
                }
                if (c != kKey[n]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                std::string_view digits = row.substr(kKey.size());
                while (!digits.empty() && digits.front() == ' ') {
                    digits.remove_prefix(1);
                }
                std::size_t value = 0;
                const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), value);
                if (parsed.ec == std::errc{}) {
                    length = value;
                    saw = true;
                }
            }
        }
        if (end == std::string::npos) {
            break;
        }
        line = end + 1;
    }
    if (!saw || length > 32 * 1024 * 1024) {
        return std::nullopt;
    }
    std::string body;
    if (!read_exact(body, length)) {
        return std::nullopt;
    }
    return body;
}

void send_json(const Json &json) {
    std::string body;
    dump(body, json);
    const std::string header = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
    if (!write_all(STDOUT_FILENO, header) || !write_all(STDOUT_FILENO, body)) {
        _exit(1);
    }
}

bool ident_char(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

int utf16_units(std::string_view text) {
    int units = 0;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            ++units;
            ++i;
        } else if ((c & 0xE0) == 0xC0) {
            ++units;
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            ++units;
            i += 3;
        } else {
            units += 2;
            i += 4;
        }
    }
    return units;
}

Pos locate(const std::string &text, uint32_t offset) {
    if (offset > text.size()) {
        offset = static_cast<uint32_t>(text.size());
    }
    int line = 0;
    uint32_t start = 0;
    for (uint32_t i = 0; i < offset; ++i) {
        if (text[i] == '\n') {
            ++line;
            start = i + 1;
        }
    }
    return Pos{line, utf16_units(std::string_view(text).substr(start, offset - start))};
}

uint32_t offset_at(const std::string &text, int line, int character) {
    if (line < 0) {
        line = 0;
    }
    if (character < 0) {
        character = 0;
    }
    int current = 0;
    uint32_t i = 0;
    while (i < text.size() && current < line) {
        if (text[i] == '\n') {
            ++current;
        }
        ++i;
    }
    int units = 0;
    while (i < text.size() && text[i] != '\n' && units < character) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::size_t bytes = 1;
        int width = 1;
        if (c >= 0x80 && (c & 0xE0) == 0xC0) {
            bytes = 2;
        } else if (c >= 0x80 && (c & 0xF0) == 0xE0) {
            bytes = 3;
        } else if (c >= 0x80) {
            bytes = 4;
            width = 2;
        }
        if (i + bytes > text.size() || units + width > character) {
            break;
        }
        i += static_cast<uint32_t>(bytes);
        units += width;
    }
    return i;
}

// Index just past a comment or quote that starts at `i`. A raw string is wrapped in backticks and
// does not treat `\` as an escape. Returns `i` when the character starts neither.
uint32_t skip_lex(const std::string &text, uint32_t i) {
    if (i >= text.size()) {
        return i;
    }
    const char c = text[i];
    if (c == '/' && i + 1 < text.size() && (text[i + 1] == '/' || text[i + 1] == '*')) {
        if (text[i + 1] == '/') {
            i += 2;
            while (i < text.size() && text[i] != '\n') {
                ++i;
            }
            return i;
        }
        i += 2;
        while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/')) {
            ++i;
        }
        if (i + 1 < text.size()) {
            i += 2;
        }
        return i;
    }
    if (c != '"' && c != '\'' && c != '`') {
        return i;
    }
    const char quote = c;
    ++i;
    while (i < text.size() && text[i] != quote) {
        if (quote != '`' && text[i] == '\\' && i + 1 < text.size()) {
            i += 2;
            continue;
        }
        // Ordinary strings and characters stop at a newline. A raw string may cross lines.
        if (quote != '`' && text[i] == '\n') {
            break;
        }
        ++i;
    }
    if (i < text.size() && text[i] == quote) {
        ++i;
    }
    return i;
}

bool in_literal(const std::string &text, uint32_t offset) {
    uint32_t i = 0;
    const uint32_t end = offset < text.size() ? offset : static_cast<uint32_t>(text.size());
    while (i < end) {
        const uint32_t next = skip_lex(text, i);
        if (next != i) {
            if (next > end) {
                return true;
            }
            i = next;
            continue;
        }
        ++i;
    }
    return false;
}

uint32_t match_brace(const std::string &text, uint32_t open) {
    if (open >= text.size() || text[open] != '{') {
        return static_cast<uint32_t>(text.size());
    }
    int depth = 0;
    uint32_t i = open;
    while (i < text.size()) {
        const uint32_t next = skip_lex(text, i);
        if (next != i) {
            i = next;
            continue;
        }
        const char c = text[i];
        if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0) {
                return i + 1;
            }
        }
        ++i;
    }
    return static_cast<uint32_t>(text.size());
}

std::string percent_decode(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            int hi = 0;
            int lo = 0;
            auto hex = [](char c) {
                if (c >= '0' && c <= '9') {
                    return c - '0';
                }
                if (c >= 'a' && c <= 'f') {
                    return c - 'a' + 10;
                }
                if (c >= 'A' && c <= 'F') {
                    return c - 'A' + 10;
                }
                return -1;
            };
            hi = hex(text[i + 1]);
            lo = hex(text[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(text[i]);
    }
    return out;
}

std::string uri_to_path(std::string_view uri) {
    constexpr std::string_view kFile = "file://";
    if (!uri.starts_with(kFile)) {
        return percent_decode(uri);
    }
    std::string_view rest = uri.substr(kFile.size());
    if (rest.starts_with("//")) {
        return {};
    }
    return percent_decode(rest);
}

std::string path_to_uri(const std::string &path) {
    std::string uri = "file://";
    for (unsigned char c : path) {
        const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '/' ||
                           c == '-' || c == '_' || c == '.' || c == '~';
        if (plain) {
            uri.push_back(static_cast<char>(c));
        } else {
            constexpr char kHex[] = "0123456789ABCDEF";
            uri.push_back('%');
            uri.push_back(kHex[c >> 4]);
            uri.push_back(kHex[c & 0x0F]);
        }
    }
    return uri;
}

std::string open_path_for(const std::string &path) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(path, ec) && !ec) {
        const std::filesystem::path canonical = std::filesystem::canonical(path, ec);
        if (!ec) {
            return canonical.string();
        }
    }
    return path;
}

Json range_json(Pos start, Pos end) {
    return jobj({{"start", jobj({{"line", jint(start.line)}, {"character", jint(start.character)}})},
                 {"end", jobj({{"line", jint(end.line)}, {"character", jint(end.character)}})}});
}

std::string spell_type(const Place &place) {
    std::string stars;
    for (int n = 0; n < place.indirections; ++n) {
        if (((place.optional >> static_cast<uint32_t>(n)) & 1U) != 0) {
            stars.push_back('?');
        }
        stars.push_back('*');
        if (((place.const_pointee >> static_cast<uint32_t>(n)) & 1U) != 0) {
            stars += "const ";
        }
    }
    std::string core = place.type_name;
    if (place.elem_const) {
        core = "const " + core;
    }
    if (place.elem_slice) {
        core = "[]" + core;
    }
    if (place.is_array) {
        core = place.array_len < 0 ? "[]" + core : "[" + std::to_string(place.array_len) + "]" + core;
    }
    return stars + core;
}

Place place_of_slot(const ResultSlot &slot) {
    return Place{slot.type_name, slot.indirections, slot.optional, slot.const_pointee, slot.is_array, slot.array_len,
                 slot.elem_slice, slot.elem_const};
}

Place place_of_param(const Param &param) {
    return Place{param.type_name, param.indirections, param.optional, param.const_pointee, param.is_array, param.array_len,
                 param.elem_slice, param.elem_const};
}

Place place_of_var(const VarDecl &var) {
    return Place{var.type_name, var.indirections, var.optional, var.const_pointee, var.is_array, var.array_len,
                 var.elem_slice, var.elem_const};
}

Place place_of_field(const FieldDecl &field) {
    return Place{field.type_name, field.indirections, field.optional, field.const_pointee, field.is_array,
                 field.array_len, field.elem_slice, field.elem_const};
}

std::string spell_fn_params(const std::vector<FnParam> &params, bool variadic) {
    std::string out;
    bool any = false;
    for (const FnParam &param : params) {
        if (any) {
            out += ", ";
        }
        any = true;
        out += spell_fn_param(param);
    }
    if (variadic) {
        if (any) {
            out += ", ";
        }
        out += "...";
    }
    return out;
}

std::string spell_fn_results(const std::string &type_name, int indirections, uint32_t optional, uint32_t const_pointee,
                             const std::vector<FnParam> &many) {
    if (many.size() > 1) {
        std::string out = "(";
        for (std::size_t n = 0; n < many.size(); ++n) {
            if (n != 0) {
                out += ", ";
            }
            out += spell_fn_param(many[n]);
        }
        out.push_back(')');
        return out;
    }
    return spell_type(Place{type_name, indirections, optional, const_pointee, false, 0});
}

std::string spell_fn_param(const FnParam &param) {
    if (!param.is_fn_ptr) {
        return spell_type(Place{param.type_name, param.indirections, param.optional, param.const_pointee, param.is_array,
                                param.array_len, param.elem_slice, param.elem_const});
    }
    return "func (" + spell_fn_params(param.params, param.variadic) + ") " +
           spell_fn_results(param.type_name, param.ret_indirections, param.ret_optional, param.ret_const_pointee, param.results);
}

std::string spell_fn_value(const std::string &type_name, int ret_indirections, uint32_t ret_optional, uint32_t ret_const,
                           const std::vector<FnParam> &params, bool variadic, const std::vector<FnParam> &results) {
    return "func (" + spell_fn_params(params, variadic) + ") " +
           spell_fn_results(type_name, ret_indirections, ret_optional, ret_const, results);
}

// `[_]T{...}` is `[N]T`. Pointers and one nested array belong to the element, inside the outer brackets.
// A function element is already a pointer, so its stored indirection is not another star.
std::string spell_array_element(const Expr::Init &init) {
    std::string core;
    if (init.elem_fn) {
        core = spell_fn_value(init.type_name, init.elem_ret_indirections, init.elem_ret_optional, init.elem_ret_const_pointee,
                              init.elem_fn_params, init.elem_fn_variadic, init.elem_fn_results);
    } else {
        Place pointed;
        pointed.type_name = init.type_name;
        pointed.indirections = init.indirections;
        pointed.optional = init.optional;
        pointed.const_pointee = init.const_pointee;
        core = spell_type(pointed);
    }
    if (!init.elem_is_array) {
        return core;
    }
    if (!init.elem_array_len_name.empty() && init.elem_array_len <= 0) {
        return "[" + init.elem_array_len_name + "]" + core;
    }
    if (init.elem_array_len < 0) {
        return std::string(init.elem_slice ? "[]" : "") + "[]" + core;
    }
    return "[" + std::to_string(init.elem_array_len) + "]" + core;
}

std::string spell_slot(const ResultSlot &slot) {
    if (slot.is_fn_ptr) {
        return spell_fn_value(slot.type_name, slot.ret_indirections, slot.ret_optional, slot.ret_const_pointee, slot.fn_params,
                              slot.fn_variadic, slot.fn_results);
    }
    return spell_type(place_of_slot(slot));
}

std::string spell_param(const Param &param) {
    if (param.is_fn_ptr) {
        return spell_fn_value(param.type_name, param.ret_indirections, param.ret_optional, param.ret_const_pointee,
                              param.fn_params, param.fn_variadic, param.fn_results);
    }
    return spell_type(place_of_param(param));
}

std::string spell_var(const VarDecl &var) {
    if (var.is_fn_ptr) {
        return spell_fn_value(var.type_name, var.ret_indirections, var.ret_optional, var.ret_const_pointee, var.fn_params,
                              var.fn_variadic, var.fn_results);
    }
    return spell_type(place_of_var(var));
}

std::string spell_field(const FieldDecl &field) {
    if (field.is_fn_ptr) {
        return spell_fn_value(field.type_name, field.ret_indirections, field.ret_optional, field.ret_const_pointee,
                              field.fn_params, field.fn_variadic, field.fn_results);
    }
    return spell_type(place_of_field(field));
}

std::string spell_results(const std::vector<ResultSlot> &results) {
    if (results.empty()) {
        return {};
    }
    if (results.size() == 1) {
        const std::string one = spell_slot(results.front());
        if (one == "invalid") {
            return {};
        }
        return one;
    }
    std::string out = "(";
    for (std::size_t n = 0; n < results.size(); ++n) {
        if (n != 0) {
            out += ", ";
        }
        out += spell_slot(results[n]);
    }
    out.push_back(')');
    return out;
}

std::string spell_function(const FunctionDecl &fn) {
    std::string out = "func ";
    if (!fn.method_struct.empty()) {
        out += fn.method_struct;
        out.push_back('.');
    }
    out += fn.name;
    if (!fn.type_params.empty()) {
        out.push_back('[');
        for (std::size_t n = 0; n < fn.type_params.size(); ++n) {
            if (n != 0) {
                out += ", ";
            }
            const TypeParam &param = fn.type_params[n];
            if (param.is_pack) {
                out += "...";
            }
            out += param.name;
            if (param.is_type) {
                out += " type";
            } else if (param.is_value) {
                out += " int";
            } else if (!param.constraint.empty()) {
                out.push_back(' ');
                out += param.constraint;
            }
        }
        out.push_back(']');
    }
    out.push_back('(');
    bool any = false;
    for (const Param &param : fn.params) {
        if (any) {
            out += ", ";
        }
        any = true;
        if (!param.name.empty()) {
            out += param.name;
            out.push_back(' ');
        }
        out += spell_param(param);
    }
    if (fn.variadic) {
        if (any) {
            out += ", ";
        }
        out += "...";
    }
    out.push_back(')');
    const std::string result = spell_results(fn.results);
    if (!result.empty()) {
        out.push_back(' ');
        out += result;
    }
    return out;
}

} // namespace tlc::lsp_impl

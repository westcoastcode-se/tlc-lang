#include "lsp.hpp"

#include "diag.hpp"
#include "driver.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "pkg.hpp"
#include "sema.hpp"

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

namespace tlc {
namespace {

struct Json {
    enum class Kind { Null, Bool, Int, String, Array, Object };
    Kind kind = Kind::Null;
    bool b = false;
    int64_t i = 0;
    std::string s;
    std::vector<Json> a;
    std::vector<std::pair<std::string, Json>> o;

    const Json *find(std::string_view key) const {
        if (kind != Kind::Object) {
            return nullptr;
        }
        for (const auto &entry : o) {
            if (entry.first == key) {
                return &entry.second;
            }
        }
        return nullptr;
    }

    std::string str(std::string_view key) const {
        const Json *value = find(key);
        if (value == nullptr || value->kind != Kind::String) {
            return {};
        }
        return value->s;
    }

    int64_t integer(std::string_view key, int64_t fallback) const {
        const Json *value = find(key);
        if (value == nullptr || value->kind != Kind::Int) {
            return fallback;
        }
        return value->i;
    }
};

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

struct Parse {
    std::string_view in;
    std::size_t at = 0;
    bool ok = true;

    void skip() {
        while (at < in.size() && (in[at] == ' ' || in[at] == '\t' || in[at] == '\n' || in[at] == '\r')) {
            ++at;
        }
    }

    bool peek(char c) {
        skip();
        return at < in.size() && in[at] == c;
    }

    bool take(char c) {
        if (!peek(c)) {
            ok = false;
            return false;
        }
        ++at;
        return true;
    }

    int hex_digit() {
        if (at >= in.size()) {
            ok = false;
            return 0;
        }
        const unsigned char c = static_cast<unsigned char>(in[at++]);
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        ok = false;
        return 0;
    }

    void append_utf8(std::string &out, uint32_t code) {
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else if (code < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (code >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }

    Json parse_string() {
        if (!take('"')) {
            return {};
        }
        std::string text;
        while (at < in.size() && in[at] != '"') {
            if (in[at] == '\\') {
                ++at;
                if (at >= in.size()) {
                    ok = false;
                    return {};
                }
                const char esc = in[at++];
                switch (esc) {
                case '"':
                case '\\':
                case '/':
                    text.push_back(esc);
                    break;
                case 'b':
                    text.push_back('\b');
                    break;
                case 'f':
                    text.push_back('\f');
                    break;
                case 'n':
                    text.push_back('\n');
                    break;
                case 'r':
                    text.push_back('\r');
                    break;
                case 't':
                    text.push_back('\t');
                    break;
                case 'u': {
                    int code = 0;
                    for (int n = 0; n < 4; ++n) {
                        code = (code << 4) | hex_digit();
                    }
                    append_utf8(text, static_cast<uint32_t>(code));
                    break;
                }
                default:
                    ok = false;
                    return {};
                }
            } else {
                text.push_back(in[at++]);
            }
        }
        if (!take('"')) {
            return {};
        }
        return jstr(std::move(text));
    }

    Json parse_number() {
        skip();
        const std::size_t begin = at;
        if (at < in.size() && in[at] == '-') {
            ++at;
        }
        if (at >= in.size() || in[at] < '0' || in[at] > '9') {
            ok = false;
            return {};
        }
        while (at < in.size() && in[at] >= '0' && in[at] <= '9') {
            ++at;
        }
        bool fractional = false;
        if (at < in.size() && (in[at] == '.' || in[at] == 'e' || in[at] == 'E')) {
            fractional = true;
            ++at;
            while (at < in.size() && ((in[at] >= '0' && in[at] <= '9') || in[at] == '+' || in[at] == '-' || in[at] == 'e' ||
                                      in[at] == 'E')) {
                ++at;
            }
        }
        if (fractional) {
            return jstr(std::string(in.substr(begin, at - begin)));
        }
        int64_t value = 0;
        const auto parsed = std::from_chars(in.data() + begin, in.data() + at, value);
        if (parsed.ec != std::errc{}) {
            ok = false;
            return {};
        }
        return jint(value);
    }

    Json parse_value() {
        skip();
        if (at >= in.size()) {
            ok = false;
            return {};
        }
        const char c = in[at];
        if (c == '"') {
            return parse_string();
        }
        if (c == '{') {
            ++at;
            std::vector<std::pair<std::string, Json>> fields;
            if (peek('}')) {
                ++at;
                return jobj(std::move(fields));
            }
            while (ok) {
                Json key = parse_string();
                if (!take(':')) {
                    return {};
                }
                Json value = parse_value();
                fields.emplace_back(std::move(key.s), std::move(value));
                skip();
                if (at < in.size() && in[at] == ',') {
                    ++at;
                    continue;
                }
                break;
            }
            if (!take('}')) {
                return {};
            }
            return jobj(std::move(fields));
        }
        if (c == '[') {
            ++at;
            std::vector<Json> items;
            if (peek(']')) {
                ++at;
                return jarr(std::move(items));
            }
            while (ok) {
                items.push_back(parse_value());
                skip();
                if (at < in.size() && in[at] == ',') {
                    ++at;
                    continue;
                }
                break;
            }
            if (!take(']')) {
                return {};
            }
            return jarr(std::move(items));
        }
        if (c == 't' && in.substr(at, 4) == "true") {
            at += 4;
            return jbool(true);
        }
        if (c == 'f' && in.substr(at, 5) == "false") {
            at += 5;
            return jbool(false);
        }
        if (c == 'n' && in.substr(at, 4) == "null") {
            at += 4;
            return jnull();
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            return parse_number();
        }
        ok = false;
        return {};
    }
};

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

struct Pos {
    int line = 0;
    int character = 0;
};

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

bool in_literal(const std::string &text, uint32_t offset) {
    bool line_comment = false;
    bool block = false;
    bool string = false;
    bool character = false;
    for (uint32_t i = 0; i < offset && i < text.size(); ++i) {
        const char c = text[i];
        if (line_comment) {
            if (c == '\n') {
                line_comment = false;
            }
            continue;
        }
        if (block) {
            if (c == '*' && i + 1 < text.size() && text[i + 1] == '/') {
                block = false;
                ++i;
            }
            continue;
        }
        if (string || character) {
            if (c == '\\' && i + 1 < text.size()) {
                ++i;
                continue;
            }
            if (string && c == '"') {
                string = false;
            } else if (character && c == '\'') {
                character = false;
            }
            continue;
        }
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            line_comment = true;
            ++i;
        } else if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            block = true;
            ++i;
        } else if (c == '"') {
            string = true;
        } else if (c == '\'') {
            character = true;
        }
    }
    return line_comment || block || string || character;
}

uint32_t match_brace(const std::string &text, uint32_t open) {
    if (open >= text.size() || text[open] != '{') {
        return static_cast<uint32_t>(text.size());
    }
    int depth = 0;
    uint32_t i = open;
    while (i < text.size()) {
        const char c = text[i];
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            i += 2;
            while (i < text.size() && text[i] != '\n') {
                ++i;
            }
            continue;
        }
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            i += 2;
            while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/')) {
                ++i;
            }
            if (i + 1 < text.size()) {
                i += 2;
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            const char quote = c;
            ++i;
            while (i < text.size() && text[i] != quote) {
                if (text[i] == '\\' && i + 1 < text.size()) {
                    i += 2;
                } else {
                    ++i;
                }
            }
            if (i < text.size()) {
                ++i;
            }
            continue;
        }
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

struct Place {
    std::string type_name;
    int indirections = 0;
    uint32_t optional = 0;
    uint32_t const_pointee = 0;
    bool is_array = false;
    int array_len = 0;
};

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
    if (place.is_array) {
        core = "[" + std::to_string(place.array_len) + "]" + core;
    }
    return stars + core;
}

Place place_of_slot(const ResultSlot &slot) {
    return Place{slot.type_name, slot.indirections, slot.optional, slot.const_pointee, slot.is_array, slot.array_len};
}

Place place_of_param(const Param &param) {
    return Place{param.type_name, param.indirections, param.optional, param.const_pointee, param.is_array, param.array_len};
}

Place place_of_var(const VarDecl &var) {
    return Place{var.type_name, var.indirections, var.optional, var.const_pointee, var.is_array, var.array_len};
}

Place place_of_field(const FieldDecl &field) {
    return Place{field.type_name, field.indirections, field.optional, field.const_pointee, field.is_array, field.array_len};
}

std::string spell_fn_param(const FnParam &param);

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
        return spell_type(Place{param.type_name, param.indirections, param.optional, param.const_pointee, false, 0});
    }
    return "func (" + spell_fn_params(param.params, param.variadic) + ") " +
           spell_fn_results(param.type_name, param.ret_indirections, param.ret_optional, param.ret_const_pointee, param.results);
}

std::string spell_fn_value(const std::string &type_name, int ret_indirections, uint32_t ret_optional, uint32_t ret_const,
                           const std::vector<FnParam> &params, bool variadic, const std::vector<FnParam> &results) {
    return "func (" + spell_fn_params(params, variadic) + ") " +
           spell_fn_results(type_name, ret_indirections, ret_optional, ret_const, results);
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

enum class Role { Function, Method, Field, Variable, Constant, StructType, EnumType, EnumMember, Alias, Trait, Module, TypeParam };

struct Sym {
    std::string name;
    std::string detail;
    std::string owner;
    std::string type_name;
    std::string package_key;
    std::string uri;
    Pos start;
    Pos end;
    bool has_loc = false;
    bool exported = false;
    bool bare = false;
    bool local = false;
    bool outline = false;
    // The type is a struct value, so `{.field = ...}` can name its fields.
    bool direct_type = false;
    int indirections = 0;
    bool is_array = false;
    // A function on a struct or enum with no `this` parameter.
    bool static_method = false;
    uint32_t scope_begin = 0;
    uint32_t scope_end = 0;
    Role role = Role::Variable;
    int completion_kind = 6;
    int symbol_kind = 13;
};

struct Note {
    std::string uri;
    Pos start;
    Pos end;
    std::string message;
};

struct Cursor {
    bool literal = false;
    bool member = false;
    std::string prefix;
    std::string word;
    std::string base;
    // `math.Entity.next` names the package in front of the type.
    std::string qualifier;
    uint32_t prefix_begin = 0;
    uint32_t offset = 0;
};

struct Doc {
    std::string uri;
    std::string path;
    std::string text;
    int version = 0;
    std::vector<Sym> syms;
    std::vector<std::string> published;
};

int completion_kind(Role role) {
    switch (role) {
    case Role::Function:
        return 3;
    case Role::Method:
        return 2;
    case Role::Field:
        return 5;
    case Role::Variable:
        return 6;
    case Role::Constant:
        return 21;
    case Role::StructType:
    case Role::Alias:
        return 22;
    case Role::EnumType:
        return 13;
    case Role::EnumMember:
        return 20;
    case Role::Trait:
        return 8;
    case Role::Module:
        return 9;
    case Role::TypeParam:
        return 25;
    }
    return 6;
}

int symbol_kind(Role role) {
    switch (role) {
    case Role::Function:
        return 12;
    case Role::Method:
        return 6;
    case Role::Field:
        return 8;
    case Role::Variable:
        return 13;
    case Role::Constant:
        return 14;
    case Role::StructType:
    case Role::Alias:
        return 23;
    case Role::EnumType:
        return 10;
    case Role::EnumMember:
        return 22;
    case Role::Trait:
        return 11;
    case Role::Module:
        return 2;
    case Role::TypeParam:
        return 26;
    }
    return 13;
}

void mark_direct(Sym &sym, const std::string &type_name, int indirections, bool is_array) {
    sym.type_name = type_name;
    sym.indirections = indirections;
    sym.is_array = is_array;
    sym.direct_type = indirections == 0 && !is_array && !type_name.empty() && type_name != "invalid";
}

void apply_result(Sym &sym, const FunctionDecl &fn) {
    if (fn.results.size() != 1) {
        return;
    }
    const ResultSlot &slot = fn.results.front();
    if (slot.is_fn_ptr) {
        return;
    }
    mark_direct(sym, slot.type_name, slot.indirections, slot.is_array);
}

void put_loc(Sym &sym, const std::string &text, SourceLoc loc, const std::string &name) {
    if (!loc.valid()) {
        return;
    }
    const uint32_t begin = loc.offset;
    uint32_t end = begin;
    if (begin < text.size()) {
        end = begin + static_cast<uint32_t>(name.size());
        if (end > text.size() || text.compare(begin, name.size(), name) != 0) {
            end = begin + 1;
        }
    }
    sym.has_loc = true;
    sym.start = locate(text, begin);
    sym.end = locate(text, end);
    sym.scope_begin = begin;
}

const SourceFile *file_of(const SourceSet &sources, uint32_t id) { return sources.find(id); }

void add_sym(std::vector<Sym> &out, Sym sym) {
    sym.completion_kind = completion_kind(sym.role);
    sym.symbol_kind = symbol_kind(sym.role);
    if (!sym.name.empty()) {
        out.push_back(std::move(sym));
    }
}

struct UnitIndex {
    std::vector<const FunctionDecl *> fns;
    std::vector<std::string> structs;
    std::vector<std::string> enums;
};

const Sym *lookup_bare(const std::vector<Sym> &syms, const std::string &uri, const std::string &name, uint32_t offset);

struct Shape {
    std::string type_name;
    int indirections = 0;
    bool is_array = false;
    bool known = false;
};

Shape shape_from_slot(const ResultSlot &slot) {
    Shape shape;
    if (slot.type_name.empty() || slot.type_name == "invalid" || slot.is_fn_ptr) {
        return shape;
    }
    shape.type_name = slot.type_name;
    shape.indirections = slot.indirections;
    shape.is_array = slot.is_array;
    shape.known = true;
    return shape;
}

Shape shape_from_sym(const Sym &sym) {
    Shape shape;
    if (sym.type_name.empty() || sym.type_name == "invalid") {
        return shape;
    }
    shape.type_name = sym.type_name;
    shape.indirections = sym.indirections;
    shape.is_array = sym.is_array;
    shape.known = true;
    return shape;
}

void collect_fn_decls(const Stmt &stmt, std::vector<const FunctionDecl *> &fns) {
    if (const auto *fn = std::get_if<Stmt::Func>(&stmt.data)) {
        if (fn->decl != nullptr) {
            fns.push_back(fn->decl.get());
            if (fn->decl->body != nullptr) {
                collect_fn_decls(*fn->decl->body, fns);
            }
        }
        return;
    }
    if (const auto *block = std::get_if<Stmt::Block>(&stmt.data)) {
        for (const auto &child : block->statements) {
            if (child) {
                collect_fn_decls(*child, fns);
            }
        }
        return;
    }
    if (const auto *item = std::get_if<Stmt::If>(&stmt.data)) {
        if (item->then_arm) {
            collect_fn_decls(*item->then_arm, fns);
        }
        if (item->else_arm) {
            collect_fn_decls(*item->else_arm, fns);
        }
        return;
    }
    if (const auto *item = std::get_if<Stmt::While>(&stmt.data)) {
        if (item->body) {
            collect_fn_decls(*item->body, fns);
        }
    } else if (const auto *item = std::get_if<Stmt::For>(&stmt.data)) {
        if (item->init) {
            collect_fn_decls(*item->init, fns);
        }
        if (item->body) {
            collect_fn_decls(*item->body, fns);
        }
    } else if (const auto *item = std::get_if<Stmt::DoWhile>(&stmt.data)) {
        if (item->body) {
            collect_fn_decls(*item->body, fns);
        }
    } else if (const auto *item = std::get_if<Stmt::Switch>(&stmt.data)) {
        if (item->body) {
            collect_fn_decls(*item->body, fns);
        }
    } else if (const auto *item = std::get_if<Stmt::Case>(&stmt.data)) {
        if (item->stmt) {
            collect_fn_decls(*item->stmt, fns);
        }
    }
}

void collect_fn(const FunctionDecl &fn, std::vector<const FunctionDecl *> &fns) {
    fns.push_back(&fn);
    if (fn.body != nullptr) {
        collect_fn_decls(*fn.body, fns);
    }
}

const FunctionDecl *find_free(const UnitIndex &index, const std::string &name) {
    const FunctionDecl *found = nullptr;
    for (const FunctionDecl *fn : index.fns) {
        if (fn->method_struct.empty() && fn->name == name) {
            found = fn;
        }
    }
    return found;
}

const FunctionDecl *find_method(const UnitIndex &index, const std::string &owner, const std::string &name, bool is_static) {
    const FunctionDecl *found = nullptr;
    for (const FunctionDecl *fn : index.fns) {
        if (fn->method_struct == owner && fn->name == name && fn->static_method == is_static) {
            found = fn;
        }
    }
    return found;
}

bool unit_type_name(const UnitIndex &index, const std::string &name) {
    for (const std::string &item : index.structs) {
        if (item == name) {
            return true;
        }
    }
    for (const std::string &item : index.enums) {
        if (item == name) {
            return true;
        }
    }
    return false;
}

const Sym *find_free_sym(const std::vector<Sym> &syms, const std::string &name) {
    const Sym *found = nullptr;
    for (const Sym &sym : syms) {
        if (sym.role == Role::Function && sym.owner.empty() && sym.name == name) {
            found = &sym;
        }
    }
    return found;
}

const Sym *find_method_sym(const std::vector<Sym> &syms, const std::string &owner, const std::string &name, bool is_static) {
    const Sym *found = nullptr;
    for (const Sym &sym : syms) {
        if (sym.role == Role::Method && sym.owner == owner && sym.name == name && sym.static_method == is_static) {
            found = &sym;
        }
    }
    return found;
}

Shape shape_of_expr(const Expr &expr, const UnitIndex &index, const std::vector<Sym> &syms, const std::string &uri,
                    uint32_t offset);

bool type_base(const UnitIndex &index, const std::vector<Sym> &syms, const std::string &uri, const std::string &name,
               uint32_t offset, std::string &owner) {
    const Sym *base = lookup_bare(syms, uri, name, offset);
    if (base != nullptr && base->role != Role::StructType && base->role != Role::EnumType && base->role != Role::Alias) {
        return false;
    }
    if (base != nullptr) {
        owner = base->name;
        return true;
    }
    if (!unit_type_name(index, name)) {
        return false;
    }
    owner = name;
    return true;
}

const FunctionDecl *callee_fn(const Expr &callee, const UnitIndex &index, const std::vector<Sym> &syms, const std::string &uri,
                              uint32_t offset) {
    if (const auto *named = std::get_if<Expr::Name>(&callee.data)) {
        return find_free(index, named->text);
    }
    if (const auto *selector = std::get_if<Expr::Selector>(&callee.data)) {
        if (selector->base == nullptr) {
            return nullptr;
        }
        if (const auto *named = std::get_if<Expr::Name>(&selector->base->data)) {
            std::string owner;
            if (type_base(index, syms, uri, named->text, offset, owner)) {
                return find_method(index, owner, selector->field, true);
            }
            const Sym *base = lookup_bare(syms, uri, named->text, offset);
            if (base != nullptr && !base->type_name.empty() && !base->is_array && base->role != Role::Module) {
                return find_method(index, base->type_name, selector->field, false);
            }
            return nullptr;
        }
        const Shape base = shape_of_expr(*selector->base, index, syms, uri, offset);
        if (!base.known || base.is_array) {
            return nullptr;
        }
        return find_method(index, base.type_name, selector->field, false);
    }
    return nullptr;
}

const Sym *callee_sym(const Expr &callee, const UnitIndex &index, const std::vector<Sym> &syms, const std::string &uri,
                      uint32_t offset) {
    if (const auto *named = std::get_if<Expr::Name>(&callee.data)) {
        return find_free_sym(syms, named->text);
    }
    if (const auto *selector = std::get_if<Expr::Selector>(&callee.data)) {
        if (selector->base == nullptr) {
            return nullptr;
        }
        if (const auto *named = std::get_if<Expr::Name>(&selector->base->data)) {
            std::string owner;
            if (type_base(index, syms, uri, named->text, offset, owner)) {
                return find_method_sym(syms, owner, selector->field, true);
            }
            const Sym *base = lookup_bare(syms, uri, named->text, offset);
            if (base != nullptr && base->role == Role::Module) {
                for (const Sym &sym : syms) {
                    if (sym.package_key == base->package_key && sym.name == selector->field && sym.owner.empty() &&
                        sym.role == Role::Function) {
                        return &sym;
                    }
                }
                return nullptr;
            }
            if (base != nullptr && !base->type_name.empty() && !base->is_array) {
                return find_method_sym(syms, base->type_name, selector->field, false);
            }
            return nullptr;
        }
        const Shape base = shape_of_expr(*selector->base, index, syms, uri, offset);
        if (!base.known || base.is_array) {
            return nullptr;
        }
        return find_method_sym(syms, base.type_name, selector->field, false);
    }
    return nullptr;
}

std::vector<Shape> call_shapes(const Expr::Call &call, const UnitIndex &index, const std::vector<Sym> &syms, const std::string &uri,
                               uint32_t offset) {
    if (call.callee == nullptr) {
        return {};
    }
    if (const FunctionDecl *fn = callee_fn(*call.callee, index, syms, uri, offset)) {
        std::vector<Shape> parts;
        parts.reserve(fn->results.size());
        for (const ResultSlot &slot : fn->results) {
            parts.push_back(shape_from_slot(slot));
        }
        return parts;
    }
    if (const Sym *sym = callee_sym(*call.callee, index, syms, uri, offset)) {
        const Shape shape = shape_from_sym(*sym);
        if (!shape.known) {
            return {};
        }
        return {shape};
    }
    return {};
}

Shape shape_of_expr(const Expr &expr, const UnitIndex &index, const std::vector<Sym> &syms, const std::string &uri,
                    uint32_t offset) {
    if (const auto *call = std::get_if<Expr::Call>(&expr.data)) {
        const std::vector<Shape> parts = call_shapes(*call, index, syms, uri, offset);
        if (parts.size() == 1) {
            return parts.front();
        }
        return {};
    }
    if (const auto *named = std::get_if<Expr::Name>(&expr.data)) {
        const Sym *sym = lookup_bare(syms, uri, named->text, offset);
        if (sym == nullptr || (sym->role != Role::Variable && sym->role != Role::Constant)) {
            return {};
        }
        return shape_from_sym(*sym);
    }
    if (const auto *selector = std::get_if<Expr::Selector>(&expr.data)) {
        if (selector->base == nullptr) {
            return {};
        }
        Shape base;
        if (const auto *named = std::get_if<Expr::Name>(&selector->base->data)) {
            const Sym *sym = lookup_bare(syms, uri, named->text, offset);
            if (sym == nullptr) {
                return {};
            }
            base = shape_from_sym(*sym);
        } else {
            base = shape_of_expr(*selector->base, index, syms, uri, offset);
        }
        if (!base.known || base.is_array) {
            return {};
        }
        for (const Sym &sym : syms) {
            if (sym.role == Role::Field && sym.owner == base.type_name && sym.name == selector->field) {
                return shape_from_sym(sym);
            }
        }
        return {};
    }
    if (const auto *unary = std::get_if<Expr::Unary>(&expr.data)) {
        if (unary->postfix || unary->expr == nullptr) {
            return {};
        }
        Shape inner = shape_of_expr(*unary->expr, index, syms, uri, offset);
        if (!inner.known) {
            return {};
        }
        if (unary->op == TokenKind::Star && inner.indirections > 0) {
            --inner.indirections;
            return inner;
        }
        if (unary->op == TokenKind::Amp && !inner.is_array) {
            ++inner.indirections;
            return inner;
        }
        if ((unary->op == TokenKind::Plus || unary->op == TokenKind::Minus) && inner.indirections == 0 &&
            (inner.type_name == "f32" || inner.type_name == "f64")) {
            return inner;
        }
        return {};
    }
    if (const auto *literal = std::get_if<Expr::Float>(&expr.data)) {
        Shape shape;
        shape.type_name = literal->is_f32 ? "f32" : "f64";
        shape.known = true;
        return shape;
    }
    if (const auto *cast = std::get_if<Expr::Cast>(&expr.data)) {
        Shape shape;
        // A function-pointer cast is not the result type. `op.` has no fields.
        if (cast->is_fn_ptr || cast->type_name.empty() || cast->type_name == "invalid") {
            return shape;
        }
        shape.type_name = cast->type_name;
        shape.indirections = cast->indirections;
        shape.known = true;
        return shape;
    }
    if (const auto *init = std::get_if<Expr::Init>(&expr.data)) {
        Shape shape;
        if (init->type_name.empty() || init->type_name == "invalid") {
            return shape;
        }
        shape.type_name = init->type_name;
        shape.known = true;
        return shape;
    }
    return {};
}

std::vector<Shape> shapes_of(const Expr &expr, const UnitIndex &index, const std::vector<Sym> &syms, const std::string &uri,
                             uint32_t offset) {
    if (const auto *call = std::get_if<Expr::Call>(&expr.data)) {
        return call_shapes(*call, index, syms, uri, offset);
    }
    const Shape shape = shape_of_expr(expr, index, syms, uri, offset);
    if (!shape.known) {
        return {};
    }
    return {shape};
}

void note_shape(Sym &sym, const Shape &shape) {
    if (!shape.known) {
        return;
    }
    mark_direct(sym, shape.type_name, shape.indirections, shape.is_array);
    Place place;
    place.type_name = shape.type_name;
    place.indirections = shape.indirections;
    place.is_array = shape.is_array;
    const std::string spelled = spell_type(place);
    if (sym.detail == sym.name || sym.detail == "const " + sym.name) {
        sym.detail += " ";
        sym.detail += spelled;
    }
}

void walk_stmt(const Stmt &stmt, const std::string &text, const std::string &uri, uint32_t scope_end, std::vector<Sym> &out,
               const UnitIndex &index);

void add_function_locals(const FunctionDecl &fn, const std::string &text, const std::string &uri, uint32_t scope_end,
                         std::vector<Sym> &out, const UnitIndex &index) {
    for (const Param &param : fn.params) {
        Sym sym;
        sym.name = param.name;
        sym.detail = param.name + " " + spell_param(param);
        mark_direct(sym, param.type_name, param.indirections, param.is_array);
        sym.uri = uri;
        sym.bare = true;
        sym.local = true;
        sym.role = Role::Variable;
        sym.scope_end = scope_end;
        put_loc(sym, text, param.loc, param.name);
        sym.scope_begin = param.loc.valid() ? param.loc.offset : 0;
        add_sym(out, std::move(sym));
    }
    for (const TypeParam &param : fn.type_params) {
        Sym sym;
        sym.name = param.name;
        sym.detail = param.name;
        sym.uri = uri;
        sym.bare = true;
        sym.local = true;
        sym.role = Role::TypeParam;
        sym.scope_end = scope_end;
        put_loc(sym, text, param.loc, param.name);
        sym.scope_begin = param.loc.valid() ? param.loc.offset : 0;
        add_sym(out, std::move(sym));
    }
    if (fn.body) {
        walk_stmt(*fn.body, text, uri, scope_end, out, index);
    }
}

void walk_stmt(const Stmt &stmt, const std::string &text, const std::string &uri, uint32_t scope_end, std::vector<Sym> &out,
               const UnitIndex &index) {
    if (const auto *decl = std::get_if<Stmt::Decl>(&stmt.data)) {
        for (const VarDecl &var : decl->vars) {
            Sym sym;
            sym.name = var.name;
            mark_direct(sym, var.type_name, var.indirections, var.is_array);
            sym.detail = var.name;
            if (!var.type_name.empty()) {
                sym.detail += " ";
                sym.detail += spell_var(var);
            }
            sym.uri = uri;
            sym.bare = true;
            sym.local = true;
            sym.role = Role::Variable;
            sym.scope_end = scope_end;
            put_loc(sym, text, var.loc, var.name);
            sym.scope_begin = var.loc.valid() ? var.loc.offset : 0;
            add_sym(out, std::move(sym));
        }
        return;
    }
    if (const auto *bind = std::get_if<Stmt::Bind>(&stmt.data)) {
        const uint32_t at = bind->loc.valid() ? bind->loc.offset : 0;
        std::vector<Shape> shapes;
        if (bind->value != nullptr) {
            shapes = shapes_of(*bind->value, index, out, uri, at);
        }
        if (shapes.size() != bind->names.size()) {
            shapes.clear();
        }
        for (std::size_t n = 0; n < bind->names.size(); ++n) {
            Sym sym;
            sym.name = bind->names[n];
            sym.detail = bind->is_const ? "const " + bind->names[n] : bind->names[n];
            if (n < shapes.size()) {
                note_shape(sym, shapes[n]);
            }
            sym.uri = uri;
            sym.bare = true;
            sym.local = true;
            sym.role = bind->is_const ? Role::Constant : Role::Variable;
            sym.scope_end = scope_end;
            const SourceLoc loc = n < bind->name_locs.size() ? bind->name_locs[n] : bind->loc;
            put_loc(sym, text, loc, bind->names[n]);
            sym.scope_begin = loc.valid() ? loc.offset : 0;
            add_sym(out, std::move(sym));
        }
        return;
    }
    if (const auto *nested = std::get_if<Stmt::Func>(&stmt.data)) {
        if (nested->decl) {
            const FunctionDecl &fn = *nested->decl;
            uint32_t end = scope_end;
            if (fn.body) {
                if (const auto *block = std::get_if<Stmt::Block>(&fn.body->data)) {
                    if (block->loc.valid()) {
                        end = match_brace(text, block->loc.offset);
                    }
                }
            }
            Sym sym;
            sym.name = fn.name;
            sym.detail = spell_function(fn);
            sym.owner = fn.method_struct;
            sym.uri = uri;
            sym.bare = fn.method_struct.empty();
            sym.local = true;
            sym.role = fn.method_struct.empty() ? Role::Function : Role::Method;
            sym.static_method = !fn.method_struct.empty() && (fn.params.empty() || !fn.params.front().is_receiver);
            apply_result(sym, fn);
            put_loc(sym, text, fn.loc, fn.name);
            sym.scope_begin = fn.loc.valid() ? fn.loc.offset : 0;
            sym.scope_end = end;
            add_sym(out, std::move(sym));
            add_function_locals(fn, text, uri, end, out, index);
        }
        return;
    }
    if (const auto *block = std::get_if<Stmt::Block>(&stmt.data)) {
        for (const auto &child : block->statements) {
            if (child) {
                walk_stmt(*child, text, uri, scope_end, out, index);
            }
        }
        return;
    }
    if (const auto *item = std::get_if<Stmt::If>(&stmt.data)) {
        if (item->then_arm) {
            walk_stmt(*item->then_arm, text, uri, scope_end, out, index);
        }
        if (item->else_arm) {
            walk_stmt(*item->else_arm, text, uri, scope_end, out, index);
        }
        return;
    }
    if (const auto *item = std::get_if<Stmt::While>(&stmt.data)) {
        if (item->body) {
            walk_stmt(*item->body, text, uri, scope_end, out, index);
        }
        return;
    }
    if (const auto *item = std::get_if<Stmt::For>(&stmt.data)) {
        if (item->init) {
            walk_stmt(*item->init, text, uri, scope_end, out, index);
        }
        if (item->body) {
            walk_stmt(*item->body, text, uri, scope_end, out, index);
        }
        return;
    }
    if (const auto *item = std::get_if<Stmt::DoWhile>(&stmt.data)) {
        if (item->body) {
            walk_stmt(*item->body, text, uri, scope_end, out, index);
        }
        return;
    }
    if (const auto *item = std::get_if<Stmt::Switch>(&stmt.data)) {
        if (item->body) {
            walk_stmt(*item->body, text, uri, scope_end, out, index);
        }
        return;
    }
    if (const auto *item = std::get_if<Stmt::Case>(&stmt.data)) {
        if (item->stmt) {
            walk_stmt(*item->stmt, text, uri, scope_end, out, index);
        }
    }
}

uint32_t body_end(const FunctionDecl &fn, const std::string &text, uint32_t fallback) {
    if (!fn.body) {
        return fallback;
    }
    if (const auto *block = std::get_if<Stmt::Block>(&fn.body->data)) {
        if (block->loc.valid()) {
            return match_brace(text, block->loc.offset);
        }
    }
    return fallback;
}

void index_unit(const PackageNode &node, const std::string &text, const std::string &uri, bool entry, std::vector<Sym> &out) {
    std::vector<uint32_t> marks;
    marks.push_back(static_cast<uint32_t>(text.size()));
    for (const TopDecl &decl : node.unit.decls) {
        SourceLoc loc;
        if (const auto *fn = std::get_if<FunctionDecl>(&decl.data)) {
            loc = fn->loc;
        } else if (const auto *var = std::get_if<VarDecl>(&decl.data)) {
            loc = var->loc;
        } else if (const auto *item = std::get_if<StructDecl>(&decl.data)) {
            loc = item->loc;
        } else if (const auto *item = std::get_if<EnumDecl>(&decl.data)) {
            loc = item->loc;
        } else if (const auto *item = std::get_if<TypedefDecl>(&decl.data)) {
            loc = item->loc;
        } else if (const auto *item = std::get_if<TraitDecl>(&decl.data)) {
            loc = item->loc;
        }
        if (loc.valid()) {
            marks.push_back(loc.offset);
        }
    }
    auto next_mark = [&](uint32_t offset) {
        uint32_t best = static_cast<uint32_t>(text.size());
        for (uint32_t mark : marks) {
            if (mark > offset && mark < best) {
                best = mark;
            }
        }
        return best;
    };

    UnitIndex index;
    for (const TopDecl &decl : node.unit.decls) {
        if (const auto *fn = std::get_if<FunctionDecl>(&decl.data)) {
            collect_fn(*fn, index.fns);
        } else if (const auto *item = std::get_if<StructDecl>(&decl.data)) {
            if (!item->anonymous && !item->name.empty()) {
                index.structs.push_back(item->name);
            }
        } else if (const auto *item = std::get_if<EnumDecl>(&decl.data)) {
            if (!item->name.empty()) {
                index.enums.push_back(item->name);
            }
        }
    }

    for (const TopDecl &decl : node.unit.decls) {
        if (const auto *fn = std::get_if<FunctionDecl>(&decl.data)) {
            const uint32_t end = body_end(*fn, text, next_mark(fn->loc.offset));
            Sym sym;
            sym.name = fn->name;
            sym.detail = spell_function(*fn);
            sym.owner = fn->method_struct;
            sym.package_key = node.package_key;
            sym.uri = uri;
            sym.exported = fn->exported;
            sym.bare = entry && fn->method_struct.empty();
            sym.outline = entry && fn->method_struct.empty();
            sym.role = fn->method_struct.empty() ? Role::Function : Role::Method;
            sym.static_method = !fn->method_struct.empty() && (fn->params.empty() || !fn->params.front().is_receiver);
            apply_result(sym, *fn);
            put_loc(sym, text, fn->loc, fn->name);
            sym.scope_end = end;
            add_sym(out, std::move(sym));
            if (entry) {
                add_function_locals(*fn, text, uri, end, out, index);
            }
            continue;
        }
        if (const auto *var = std::get_if<VarDecl>(&decl.data)) {
            Sym sym;
            sym.name = var->name;
            mark_direct(sym, var->type_name, var->indirections, var->is_array);
            sym.owner = var->owner;
            sym.package_key = node.package_key;
            sym.uri = uri;
            sym.exported = var->exported;
            sym.bare = entry && var->owner.empty();
            sym.outline = entry && var->owner.empty();
            sym.role = var->is_const_decl ? Role::Constant : Role::Variable;
            if (!var->owner.empty()) {
                std::string detail = (var->is_const_decl ? "const " : "var ") + var->owner;
                if (!var->owner_params.empty()) {
                    detail += '[';
                    for (std::size_t i = 0; i < var->owner_params.size(); ++i) {
                        if (i != 0) {
                            detail += ", ";
                        }
                        detail += var->owner_params[i];
                    }
                    detail += ']';
                }
                detail += "::";
                detail += var->name;
                if (!var->type_name.empty() || var->is_fn_ptr) {
                    detail += " ";
                    detail += spell_var(*var);
                }
                sym.detail = std::move(detail);
            } else if (var->is_const_decl) {
                sym.detail = var->type_name.empty() && !var->is_fn_ptr ? "const " + var->name
                                                                       : "const " + var->name + " " + spell_var(*var);
            } else if (!var->type_name.empty()) {
                sym.detail = var->name + " " + spell_var(*var);
            } else {
                sym.detail = var->name;
            }
            if (var->type_name.empty() && var->init != nullptr) {
                const uint32_t at = var->loc.valid() ? var->loc.offset : 0;
                const std::vector<Shape> shapes = shapes_of(*var->init, index, out, uri, at);
                if (shapes.size() == 1) {
                    note_shape(sym, shapes.front());
                }
            }
            put_loc(sym, text, var->loc, var->name);
            if (!entry && (var->is_static || !var->exported)) {
                sym.bare = false;
                sym.outline = false;
            }
            add_sym(out, std::move(sym));
            continue;
        }
        if (const auto *item = std::get_if<StructDecl>(&decl.data)) {
            if (item->anonymous) {
                continue;
            }
            Sym sym;
            sym.name = item->name;
            sym.type_name = item->name;
            sym.package_key = node.package_key;
            sym.uri = uri;
            sym.exported = item->exported;
            sym.bare = entry;
            sym.outline = entry;
            sym.role = Role::StructType;
            sym.detail = std::string("type ") + item->name + (item->is_union ? " union" : " struct");
            put_loc(sym, text, item->loc, item->name);
            if (!entry && !item->exported) {
                sym.bare = false;
                sym.outline = false;
            }
            add_sym(out, std::move(sym));
            for (const FieldDecl &field : item->fields) {
                Sym member;
                member.name = field.name;
                member.owner = item->name;
                mark_direct(member, field.type_name, field.indirections, field.is_array);
                member.package_key = node.package_key;
                member.uri = uri;
                member.role = Role::Field;
                member.detail = field.name + " " + spell_field(field);
                put_loc(member, text, field.loc, field.name);
                add_sym(out, std::move(member));
            }
            continue;
        }
        if (const auto *item = std::get_if<EnumDecl>(&decl.data)) {
            Sym sym;
            sym.name = item->name;
            sym.type_name = item->name;
            sym.package_key = node.package_key;
            sym.uri = uri;
            sym.exported = item->exported;
            sym.bare = entry;
            sym.outline = entry;
            sym.role = Role::EnumType;
            sym.detail = "type " + item->name + " enum";
            put_loc(sym, text, item->loc, item->name);
            if (!entry && !item->exported) {
                sym.bare = false;
                sym.outline = false;
            }
            add_sym(out, std::move(sym));
            for (const Enumerator &enumerator : item->enumerators) {
                Sym member;
                member.name = enumerator.name;
                member.owner = item->name;
                member.package_key = node.package_key;
                member.uri = uri;
                member.role = Role::EnumMember;
                member.detail = item->name + "." + enumerator.name;
                put_loc(member, text, enumerator.loc, enumerator.name);
                add_sym(out, std::move(member));
            }
            continue;
        }
        if (const auto *item = std::get_if<TypedefDecl>(&decl.data)) {
            Sym sym;
            sym.name = item->name;
            mark_direct(sym, item->type_name, item->indirections, item->is_array);
            sym.package_key = node.package_key;
            sym.uri = uri;
            sym.exported = item->exported;
            sym.bare = entry;
            sym.outline = entry;
            sym.role = Role::Alias;
            sym.detail = "type " + item->name + " " + item->type_name;
            put_loc(sym, text, item->loc, item->name);
            if (!entry && !item->exported) {
                sym.bare = false;
                sym.outline = false;
            }
            add_sym(out, std::move(sym));
            continue;
        }
        if (const auto *item = std::get_if<TraitDecl>(&decl.data)) {
            Sym sym;
            sym.name = item->name;
            sym.type_name = item->name;
            sym.package_key = node.package_key;
            sym.uri = uri;
            sym.bare = entry;
            sym.outline = entry;
            sym.role = Role::Trait;
            sym.detail = "type " + item->name + " trait";
            put_loc(sym, text, item->loc, item->name);
            add_sym(out, std::move(sym));
            for (const FieldDecl &field : item->fields) {
                Sym member;
                member.name = field.name;
                member.owner = item->name;
                mark_direct(member, field.type_name, field.indirections, field.is_array);
                member.package_key = node.package_key;
                member.uri = uri;
                member.role = Role::Field;
                member.detail = field.name + " " + spell_field(field);
                put_loc(member, text, field.loc, field.name);
                add_sym(out, std::move(member));
            }
            for (const FunctionDecl &method : item->methods) {
                Sym member;
                member.name = method.name;
                member.owner = item->name;
                member.detail = spell_function(method);
                member.package_key = node.package_key;
                member.uri = uri;
                member.role = Role::Method;
                apply_result(member, method);
                put_loc(member, text, method.loc, method.name);
                add_sym(out, std::move(member));
            }
        }
    }
}

void index_imports(const PackageGraph &graph, const PackageNode &entry, const std::string &text, const std::string &uri,
                   std::vector<Sym> &out) {
    for (const ImportDecl &import : entry.unit.imports) {
        const PackageNode *target = nullptr;
        for (const ImportEdge &edge : graph.edges) {
            if (edge.from_file != entry.file_id || edge.spec != import.path) {
                continue;
            }
            for (const PackageNode &node : graph.nodes) {
                if (node.file_id == edge.to_file) {
                    target = &node;
                    break;
                }
            }
            break;
        }
        if (target == nullptr) {
            continue;
        }
        if (import.form == ImportDecl::Form::Selective) {
            for (const std::string &name : import.names) {
                const Sym *found = nullptr;
                for (const Sym &sym : out) {
                    if (sym.package_key == target->package_key && sym.name == name && sym.owner.empty() && sym.exported) {
                        found = &sym;
                        break;
                    }
                }
                if (found == nullptr) {
                    continue;
                }
                Sym brought = *found;
                brought.bare = true;
                brought.outline = false;
                brought.local = false;
                out.push_back(std::move(brought));
            }
            continue;
        }
        Sym mod;
        mod.name = import.form == ImportDecl::Form::Alias && !import.alias.empty() ? import.alias : target->qualifier;
        mod.detail = "package " + target->qualifier;
        mod.package_key = target->package_key;
        mod.uri = uri;
        mod.bare = true;
        mod.role = Role::Module;
        put_loc(mod, text, import.loc, mod.name);
        add_sym(out, std::move(mod));
    }
}

struct Checked {
    std::vector<Sym> syms;
    std::vector<Note> notes;
};

Checked check_buffer(const std::string &path, const std::string &text, const std::string &client_uri) {
    Checked checked;
    SourceSet sources;
    DiagnosticEngine diags(sources);
    PackageGraph graph;
    const std::string parent = std::filesystem::path(path).parent_path().string();
    std::error_code ec;
    const bool parent_ok = !parent.empty() && std::filesystem::is_directory(parent, ec) && !ec;
    if (parent_ok) {
        ResolveRequest request;
        request.entry = path;
        request.vendor_root = vendor_library_root();
        request.std_root = std_library_root();
        request.overlays.emplace(path, text);
        resolve_packages(request, sources, diags, graph);
    } else {
        SourceFile &file = sources.add(path.empty() ? "untitled.tlc" : path, text);
        Lexer lexer(file, diags);
        Parser parser(lexer.tokenize(), diags);
        PackageNode node;
        node.file_id = file.id();
        node.path = file.path();
        node.package_key = file.path();
        node.qualifier = std::filesystem::path(file.path()).stem().string();
        node.unit = parser.parse();
        graph.nodes.push_back(std::move(node));
    }

    const PackageNode *entry = nullptr;
    for (const PackageNode &node : graph.nodes) {
        if (node.path == path) {
            entry = &node;
            break;
        }
    }
    if (entry == nullptr && !graph.nodes.empty() && !is_std_builtin_path(graph.nodes.front().path)) {
        entry = &graph.nodes.front();
    }

    auto uri_for = [&](const PackageNode &node) {
        if (entry != nullptr && node.file_id == entry->file_id) {
            return client_uri;
        }
        if (node.path.empty() || is_std_builtin_path(node.path) || node.path.front() != '/') {
            return std::string{};
        }
        return path_to_uri(node.path);
    };

    if (entry != nullptr) {
        for (const PackageNode &node : graph.nodes) {
            const SourceFile *file = file_of(sources, node.file_id);
            if (file == nullptr) {
                continue;
            }
            const std::string uri = uri_for(node);
            index_unit(node, file->text(), uri, node.file_id == entry->file_id, checked.syms);
        }
        if (const SourceFile *file = file_of(sources, entry->file_id)) {
            index_imports(graph, *entry, file->text(), client_uri, checked.syms);
        }
    }

    if (diags.ok() && !graph.nodes.empty()) {
        analyze(graph, diags);
    }

    for (const Diagnostic &item : diags.items()) {
        const SourceFile *file = sources.find(item.loc.file);
        Note note;
        note.message = item.message;
        if (file == nullptr || !item.loc.valid()) {
            note.uri = client_uri;
            note.end.character = 1;
        } else {
            if (entry != nullptr && file->id() == entry->file_id) {
                note.uri = client_uri;
            } else if (!file->path().empty() && file->path().front() == '/') {
                note.uri = path_to_uri(file->path());
            } else {
                continue;
            }
            const uint32_t begin = item.loc.offset > file->text().size() ? static_cast<uint32_t>(file->text().size())
                                                                        : item.loc.offset;
            uint32_t end = begin >= file->text().size() ? begin : begin + 1;
            if (begin < file->text().size() && ident_char(static_cast<unsigned char>(file->text()[begin]))) {
                end = begin;
                while (end < file->text().size() && ident_char(static_cast<unsigned char>(file->text()[end]))) {
                    ++end;
                }
            }
            note.start = locate(file->text(), begin);
            note.end = locate(file->text(), end);
            if (note.start.line == note.end.line && note.start.character == note.end.character) {
                note.end.character = note.start.character + 1;
            }
        }
        checked.notes.push_back(std::move(note));
    }
    return checked;
}

Cursor cursor_at(const std::string &text, int line, int character) {
    Cursor cursor;
    uint32_t offset = offset_at(text, line, character);
    if (offset > text.size()) {
        offset = static_cast<uint32_t>(text.size());
    }
    cursor.offset = offset;
    cursor.literal = in_literal(text, offset);
    uint32_t start = offset;
    while (start > 0 && ident_char(static_cast<unsigned char>(text[start - 1]))) {
        --start;
    }
    cursor.prefix_begin = start;
    cursor.prefix = text.substr(start, offset - start);
    uint32_t word_end = offset;
    while (word_end < text.size() && ident_char(static_cast<unsigned char>(text[word_end]))) {
        ++word_end;
    }
    cursor.word = text.substr(start, word_end - start);
    uint32_t before = start;
    while (before > 0 && (text[before - 1] == ' ' || text[before - 1] == '\t')) {
        --before;
    }
    if (before > 0 && text[before - 1] == '.') {
        uint32_t base_end = before - 1;
        while (base_end > 0 && (text[base_end - 1] == ' ' || text[base_end - 1] == '\t')) {
            --base_end;
        }
        uint32_t base_begin = base_end;
        while (base_begin > 0 && ident_char(static_cast<unsigned char>(text[base_begin - 1]))) {
            --base_begin;
        }
        if (base_begin < base_end) {
            cursor.member = true;
            cursor.base = text.substr(base_begin, base_end - base_begin);
            uint32_t qual_end = base_begin;
            while (qual_end > 0 && (text[qual_end - 1] == ' ' || text[qual_end - 1] == '\t')) {
                --qual_end;
            }
            if (qual_end > 0 && text[qual_end - 1] == '.') {
                uint32_t qual_base = qual_end - 1;
                while (qual_base > 0 && (text[qual_base - 1] == ' ' || text[qual_base - 1] == '\t')) {
                    --qual_base;
                }
                uint32_t qual_begin = qual_base;
                while (qual_begin > 0 && ident_char(static_cast<unsigned char>(text[qual_begin - 1]))) {
                    --qual_begin;
                }
                if (qual_begin < qual_base) {
                    cursor.qualifier = text.substr(qual_begin, qual_base - qual_begin);
                }
            }
        }
    }
    return cursor;
}

bool prefix_ok(const std::string &name, const std::string &prefix) {
    return prefix.empty() || name.starts_with(prefix);
}

const Sym *lookup_bare(const std::vector<Sym> &syms, const std::string &uri, const std::string &name, uint32_t offset) {
    const Sym *best = nullptr;
    int best_rank = 100;
    uint32_t best_begin = 0;
    for (const Sym &sym : syms) {
        if (sym.name != name || !sym.bare) {
            continue;
        }
        int rank = 2;
        if (sym.local) {
            if (sym.uri != uri || offset < sym.scope_begin || offset > sym.scope_end) {
                continue;
            }
            rank = 0;
        } else if (sym.uri == uri) {
            rank = 1;
        }
        const uint32_t begin = sym.scope_begin;
        const bool closer = best == nullptr || rank < best_rank || (rank == best_rank && begin >= best_begin);
        if (closer) {
            best = &sym;
            best_rank = rank;
            best_begin = begin;
        }
    }
    return best;
}

bool document_owns(const Doc &doc, const std::string &package_key) {
    if (package_key.empty()) {
        return false;
    }
    for (const Sym &sym : doc.syms) {
        if (sym.uri == doc.uri && !sym.local && sym.role != Role::Module && sym.package_key == package_key) {
            return true;
        }
    }
    return false;
}

const Sym *exported_type(const std::vector<Sym> &syms, const std::string &package_key, const std::string &name) {
    for (const Sym &sym : syms) {
        if (sym.package_key == package_key && sym.owner.empty() && sym.name == name && sym.exported &&
            (sym.role == Role::StructType || sym.role == Role::EnumType || sym.role == Role::Alias)) {
            return &sym;
        }
    }
    return nullptr;
}

// `math.Entity.next`: an exported variable on the type, or an enumerator.
const Sym *qualified_member(const std::vector<Sym> &syms, const std::string &uri, const Cursor &cursor) {
    const Sym *package = lookup_bare(syms, uri, cursor.qualifier, cursor.offset);
    if (package == nullptr || package->role != Role::Module) {
        return nullptr;
    }
    const Sym *type = exported_type(syms, package->package_key, cursor.base);
    if (type == nullptr) {
        return nullptr;
    }
    for (const Sym &sym : syms) {
        if (sym.package_key != type->package_key || sym.owner != type->name || sym.name != cursor.word) {
            continue;
        }
        if (sym.role == Role::EnumMember ||
            ((sym.role == Role::Variable || sym.role == Role::Constant) && sym.exported)) {
            return &sym;
        }
    }
    return nullptr;
}

const Sym *lookup_member(const std::vector<Sym> &syms, const Sym &base, const std::string &name) {
    if (base.role == Role::Module) {
        for (const Sym &sym : syms) {
            if (sym.package_key == base.package_key && sym.name == name && sym.owner.empty() &&
                (sym.exported || sym.uri == base.uri)) {
                return &sym;
            }
        }
        return nullptr;
    }
    const std::string type = (base.role == Role::StructType || base.role == Role::EnumType || base.role == Role::Alias ||
                              base.role == Role::Trait)
                                 ? base.name
                                 : base.type_name;
    if (type.empty()) {
        return nullptr;
    }
    for (const Sym &sym : syms) {
        if (sym.owner == type && sym.name == name) {
            return &sym;
        }
    }
    return nullptr;
}

Json location_of(const Sym &sym) {
    if (!sym.has_loc || sym.uri.empty()) {
        return jnull();
    }
    return jobj({{"uri", jstr(sym.uri)}, {"range", range_json(sym.start, sym.end)}});
}

const char *kKeywords[] = {"func",    "type",    "var",     "const",   "struct", "enum",    "union",  "trait",
                           "if",      "else",    "while",   "for",     "do",     "switch",  "case",   "default",
                           "return",  "break",   "continue","defer",   "import", "package", "export", "extern",
                           "test",    "assert",  "sizeof",  "typeof",  "true",    "false",  "null"};

const char *kBuiltinTypes[] = {"bool", "void", "i8", "u8", "i16", "u16", "i32", "u32", "i64", "u64", "f32", "f64"};

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

uint32_t skip_back(const std::string &text, uint32_t pos) {
    while (pos > 0 && is_space(text[pos - 1])) {
        --pos;
    }
    return pos;
}

bool keyword_back(const std::string &text, uint32_t end, std::string_view word) {
    if (end < word.size()) {
        return false;
    }
    const uint32_t begin = end - static_cast<uint32_t>(word.size());
    if (text.compare(begin, word.size(), word) != 0) {
        return false;
    }
    return begin == 0 || !ident_char(static_cast<unsigned char>(text[begin - 1]));
}

uint32_t innermost_brace(const std::string &text, uint32_t pos) {
    std::vector<uint32_t> stack;
    uint32_t i = 0;
    while (i < pos && i < text.size()) {
        const char c = text[i];
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            i += 2;
            while (i < text.size() && text[i] != '\n') {
                ++i;
            }
            continue;
        }
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            i += 2;
            while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/')) {
                ++i;
            }
            if (i + 1 < text.size()) {
                i += 2;
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            const char quote = c;
            ++i;
            while (i < text.size() && text[i] != quote) {
                if (text[i] == '\\' && i + 1 < text.size()) {
                    i += 2;
                } else {
                    ++i;
                }
            }
            if (i < text.size()) {
                ++i;
            }
            continue;
        }
        if (c == '{') {
            stack.push_back(i);
        } else if (c == '}' && !stack.empty()) {
            stack.pop_back();
        }
        ++i;
    }
    if (stack.empty()) {
        return static_cast<uint32_t>(-1);
    }
    return stack.back();
}

// Walk back over `*`, `?`, and `const`. `stars` counts pointer levels. Returns the index after the token before that prefix.
uint32_t skip_type_prefix(const std::string &text, uint32_t begin, int &stars) {
    stars = 0;
    uint32_t s = skip_back(text, begin);
    while (s > 0) {
        if (text[s - 1] == '*' || text[s - 1] == '?') {
            if (text[s - 1] == '*') {
                ++stars;
            }
            s = skip_back(text, s - 1);
            continue;
        }
        if (keyword_back(text, s, "const")) {
            s = skip_back(text, s - 5);
            continue;
        }
        break;
    }
    return s;
}

bool has_fields(const Doc &doc, const std::string &owner) {
    for (const Sym &sym : doc.syms) {
        if (sym.role == Role::Field && sym.owner == owner) {
            return true;
        }
    }
    return false;
}

std::string struct_owner(const Doc &doc, const std::string &name) {
    if (name.empty()) {
        return {};
    }
    if (has_fields(doc, name)) {
        return name;
    }
    for (const Sym &sym : doc.syms) {
        if (sym.role == Role::Alias && sym.name == name && sym.direct_type && has_fields(doc, sym.type_name)) {
            return sym.type_name;
        }
    }
    return {};
}

const Sym *enclosing_function(const Doc &doc, uint32_t offset) {
    const Sym *best = nullptr;
    uint32_t best_span = UINT32_MAX;
    for (const Sym &sym : doc.syms) {
        if (sym.role != Role::Function && sym.role != Role::Method) {
            continue;
        }
        if (sym.uri != doc.uri || sym.scope_end == 0 || offset < sym.scope_begin || offset > sym.scope_end) {
            continue;
        }
        const uint32_t span = sym.scope_end - sym.scope_begin;
        if (span < best_span) {
            best = &sym;
            best_span = span;
        }
    }
    return best;
}

std::string field_direct_type(const Doc &doc, const std::string &owner, const std::string &field) {
    for (const Sym &sym : doc.syms) {
        if (sym.role == Role::Field && sym.owner == owner && sym.name == field && sym.direct_type) {
            return sym.type_name;
        }
    }
    return {};
}

std::string type_of_brace(const Doc &doc, uint32_t brace);

std::string type_of_brace(const Doc &doc, uint32_t brace) {
    const std::string &text = doc.text;
    if (brace >= text.size() || text[brace] != '{') {
        return {};
    }
    const uint32_t at = skip_back(text, brace);
    if (keyword_back(text, at, "return")) {
        if (const Sym *fn = enclosing_function(doc, brace); fn != nullptr && fn->direct_type) {
            return struct_owner(doc, fn->type_name);
        }
        return {};
    }
    if (at == 0 || text[at - 1] != '=') {
        return {};
    }
    const bool inferred = at >= 2 && text[at - 2] == ':';
    const uint32_t eq = inferred ? at - 2 : at - 1;
    const uint32_t end = skip_back(text, eq);
    if (end == 0 || !ident_char(static_cast<unsigned char>(text[end - 1]))) {
        return {};
    }
    uint32_t begin = end;
    while (begin > 0 && ident_char(static_cast<unsigned char>(text[begin - 1]))) {
        --begin;
    }
    const std::string name = text.substr(begin, end - begin);
    const uint32_t before_name = skip_back(text, begin);
    if (before_name > 0 && text[before_name - 1] == '.') {
        const uint32_t dot = before_name - 1;
        const uint32_t before_dot = skip_back(text, dot);
        if (before_dot > 0 && ident_char(static_cast<unsigned char>(text[before_dot - 1]))) {
            uint32_t qualifier = before_dot;
            while (qualifier > 0 && ident_char(static_cast<unsigned char>(text[qualifier - 1]))) {
                --qualifier;
            }
            int stars = 0;
            skip_type_prefix(text, qualifier, stars);
            if (stars > 0) {
                return {};
            }
            return struct_owner(doc, name);
        }
        const uint32_t outer = innermost_brace(text, dot);
        if (outer == static_cast<uint32_t>(-1) || outer == brace) {
            return {};
        }
        const std::string outer_type = type_of_brace(doc, outer);
        if (outer_type.empty()) {
            return {};
        }
        return struct_owner(doc, field_direct_type(doc, outer_type, name));
    }
    int stars = 0;
    const uint32_t before = skip_type_prefix(text, begin, stars);
    if (stars > 0) {
        return {};
    }
    if (before > 0 && ident_char(static_cast<unsigned char>(text[before - 1]))) {
        uint32_t name_end = before;
        uint32_t name_begin = name_end;
        while (name_begin > 0 && ident_char(static_cast<unsigned char>(text[name_begin - 1]))) {
            --name_begin;
        }
        if (keyword_back(text, skip_back(text, name_begin), "var")) {
            return struct_owner(doc, name);
        }
    }
    if (const Sym *sym = lookup_bare(doc.syms, doc.uri, name, brace); sym != nullptr && sym->direct_type) {
        return struct_owner(doc, sym->type_name);
    }
    return {};
}

std::string designated_struct(const Doc &doc, const Cursor &cursor) {
    const uint32_t at = skip_back(doc.text, cursor.prefix_begin);
    if (at == 0 || doc.text[at - 1] != '.') {
        return {};
    }
    const uint32_t dot = at - 1;
    const uint32_t before = skip_back(doc.text, dot);
    if (before > 0 && ident_char(static_cast<unsigned char>(doc.text[before - 1]))) {
        return {};
    }
    const uint32_t brace = innermost_brace(doc.text, dot);
    if (brace == static_cast<uint32_t>(-1)) {
        return {};
    }
    return type_of_brace(doc, brace);
}

bool field_already_named(const std::string &text, uint32_t brace, uint32_t dot, const std::string &name) {
    int depth = 0;
    uint32_t i = brace;
    while (i < dot && i < text.size()) {
        const char c = text[i];
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            i += 2;
            while (i < text.size() && text[i] != '\n') {
                ++i;
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            const char quote = c;
            ++i;
            while (i < text.size() && text[i] != quote) {
                if (text[i] == '\\' && i + 1 < text.size()) {
                    i += 2;
                } else {
                    ++i;
                }
            }
            if (i < text.size()) {
                ++i;
            }
            continue;
        }
        if (c == '{') {
            ++depth;
            ++i;
            continue;
        }
        if (c == '}') {
            --depth;
            ++i;
            continue;
        }
        if (depth == 1 && c == '.') {
            uint32_t start = i + 1;
            while (start < text.size() && is_space(text[start])) {
                ++start;
            }
            uint32_t end = start;
            while (end < text.size() && ident_char(static_cast<unsigned char>(text[end]))) {
                ++end;
            }
            if (start < dot && text.compare(start, end - start, name) == 0 && end - start == name.size()) {
                return true;
            }
        }
        ++i;
    }
    return false;
}

const Sym *find_field(const Doc &doc, const std::string &owner, const std::string &name) {
    for (const Sym &sym : doc.syms) {
        if (sym.role == Role::Field && sym.owner == owner && sym.name == name) {
            return &sym;
        }
    }
    return nullptr;
}

Json completion_item(const std::string &label, int kind, const std::string &detail, const Doc &doc, const Cursor &cursor) {
    std::vector<std::pair<std::string, Json>> fields;
    fields.emplace_back("label", jstr(label));
    fields.emplace_back("kind", jint(kind));
    if (!detail.empty()) {
        fields.emplace_back("detail", jstr(detail));
    }
    const Pos start = locate(doc.text, cursor.prefix_begin);
    const Pos end = locate(doc.text, cursor.offset);
    fields.emplace_back("textEdit", jobj({{"range", range_json(start, end)}, {"newText", jstr(label)}}));
    return jobj(std::move(fields));
}

Json complete(const Doc &doc, const Cursor &cursor) {
    std::vector<Json> items;
    auto push = [&](const std::string &label, int kind, const std::string &detail) {
        if (items.size() >= 100 || !prefix_ok(label, cursor.prefix)) {
            return;
        }
        for (const Json &item : items) {
            if (const Json *have = item.find("label"); have != nullptr && have->s == label) {
                return;
            }
        }
        items.push_back(completion_item(label, kind, detail, doc, cursor));
    };
    const std::string init_type = designated_struct(doc, cursor);
    if (!cursor.literal && !init_type.empty()) {
        const uint32_t dot = skip_back(doc.text, cursor.prefix_begin);
        const uint32_t brace = dot == 0 ? static_cast<uint32_t>(-1) : innermost_brace(doc.text, dot - 1);
        for (const Sym &sym : doc.syms) {
            if (sym.role != Role::Field || sym.owner != init_type) {
                continue;
            }
            if (brace != static_cast<uint32_t>(-1) && dot > 0 &&
                field_already_named(doc.text, brace, dot - 1, sym.name)) {
                continue;
            }
            push(sym.name, sym.completion_kind, sym.detail);
        }
        return jobj({{"isIncomplete", jbool(false)}, {"items", jarr(std::move(items))}});
    }
    if (!cursor.literal && cursor.member) {
        if (!cursor.qualifier.empty()) {
            const Sym *package = lookup_bare(doc.syms, doc.uri, cursor.qualifier, cursor.offset);
            const Sym *type = package != nullptr && package->role == Role::Module
                                  ? exported_type(doc.syms, package->package_key, cursor.base)
                                  : nullptr;
            if (type != nullptr) {
                for (const Sym &sym : doc.syms) {
                    if (sym.package_key != type->package_key || sym.owner != type->name) {
                        continue;
                    }
                    if (sym.role == Role::EnumMember ||
                        ((sym.role == Role::Variable || sym.role == Role::Constant) && sym.exported)) {
                        push(sym.name, sym.completion_kind, sym.detail);
                    }
                }
            }
            return jobj({{"isIncomplete", jbool(false)}, {"items", jarr(std::move(items))}});
        }
        if (const Sym *base = lookup_bare(doc.syms, doc.uri, cursor.base, cursor.offset)) {
            if (base->role == Role::Module) {
                for (const Sym &sym : doc.syms) {
                    if (sym.role != Role::Module && sym.package_key == base->package_key && sym.owner.empty() &&
                        (sym.exported || sym.bare)) {
                        push(sym.name, sym.completion_kind, sym.detail);
                    }
                }
            } else {
                const std::string type = (base->role == Role::StructType || base->role == Role::EnumType ||
                                          base->role == Role::Alias || base->role == Role::Trait)
                                             ? base->name
                                             : base->type_name;
                const bool on_type = base->role == Role::StructType || base->role == Role::EnumType ||
                                     base->role == Role::Trait || base->role == Role::Alias;
                for (const Sym &sym : doc.syms) {
                    if (type.empty() || sym.owner != type) {
                        continue;
                    }
                    if (sym.role == Role::Method && on_type != sym.static_method) {
                        continue;
                    }
                    // A trait field is offered on a value. `Shape.` is the type name.
                    if (sym.role == Role::Field && on_type && base->role == Role::Trait) {
                        continue;
                    }
                    // `Entity.next` is offered on the type. `e.next` is a field.
                    // Another package sees the variable only when it is exported.
                    if ((sym.role == Role::Variable || sym.role == Role::Constant) && !sym.owner.empty() &&
                        (!on_type || sym.package_key != base->package_key ||
                         (!sym.exported && !document_owns(doc, base->package_key)))) {
                        continue;
                    }
                    push(sym.name, sym.completion_kind, sym.detail);
                }
            }
        }
    } else if (!cursor.literal) {
        for (const Sym &sym : doc.syms) {
            if (!sym.bare) {
                continue;
            }
            if (sym.local && (sym.uri != doc.uri || cursor.offset < sym.scope_begin || cursor.offset > sym.scope_end)) {
                continue;
            }
            if (!sym.local && sym.uri != doc.uri) {
                continue;
            }
            push(sym.name, sym.completion_kind, sym.detail);
        }
        for (const char *word : kKeywords) {
            push(word, 14, {});
        }
        for (const char *word : kBuiltinTypes) {
            push(word, 22, {});
        }
    }
    return jobj({{"isIncomplete", jbool(false)}, {"items", jarr(std::move(items))}});
}

Json hover_of(const Sym &sym) {
    return jobj({{"contents", jobj({{"kind", jstr("plaintext")}, {"value", jstr(sym.detail.empty() ? sym.name : sym.detail)}})},
                 {"range", range_json(sym.start, sym.end)}});
}

std::vector<Json> outline_children(const Doc &doc, const Sym &parent) {
    std::vector<Json> children;
    for (const Sym &sym : doc.syms) {
        if (sym.owner != parent.name || sym.uri != parent.uri) {
            continue;
        }
        children.push_back(jobj({{"name", jstr(sym.name)},
                                 {"detail", jstr(sym.detail)},
                                 {"kind", jint(sym.symbol_kind)},
                                 {"range", range_json(sym.start, sym.end)},
                                 {"selectionRange", range_json(sym.start, sym.end)}}));
    }
    return children;
}

Json document_symbols(const Doc &doc) {
    std::vector<Json> items;
    for (const Sym &sym : doc.syms) {
        if (!sym.outline || sym.uri != doc.uri || sym.local) {
            continue;
        }
        std::vector<std::pair<std::string, Json>> fields;
        fields.emplace_back("name", jstr(sym.name));
        fields.emplace_back("detail", jstr(sym.detail));
        fields.emplace_back("kind", jint(sym.symbol_kind));
        fields.emplace_back("range", range_json(sym.has_loc ? sym.start : Pos{}, sym.has_loc ? sym.end : Pos{}));
        fields.emplace_back("selectionRange", range_json(sym.has_loc ? sym.start : Pos{}, sym.has_loc ? sym.end : Pos{}));
        std::vector<Json> children = outline_children(doc, sym);
        if (!children.empty()) {
            fields.emplace_back("children", jarr(std::move(children)));
        }
        items.push_back(jobj(std::move(fields)));
    }
    return jarr(std::move(items));
}

Json publish_params(const std::string &uri, int version, bool with_version, const std::vector<Note> &notes) {
    std::vector<Json> items;
    for (const Note &note : notes) {
        if (note.uri != uri) {
            continue;
        }
        items.push_back(jobj({{"range", range_json(note.start, note.end)},
                              {"severity", jint(1)},
                              {"source", jstr("tlc")},
                              {"message", jstr(note.message)}}));
    }
    std::vector<std::pair<std::string, Json>> fields;
    fields.emplace_back("uri", jstr(uri));
    if (with_version) {
        fields.emplace_back("version", jint(version));
    }
    fields.emplace_back("diagnostics", jarr(std::move(items)));
    return jobj(std::move(fields));
}

void publish(Doc &doc, const std::vector<Note> &notes) {
    std::vector<std::string> uris = doc.published;
    auto remember = [&](const std::string &uri) {
        for (const std::string &have : uris) {
            if (have == uri) {
                return;
            }
        }
        uris.push_back(uri);
    };
    remember(doc.uri);
    for (const Note &note : notes) {
        if (!note.uri.empty()) {
            remember(note.uri);
        }
    }
    for (const std::string &uri : uris) {
        send_json(jobj({{"jsonrpc", jstr("2.0")},
                        {"method", jstr("textDocument/publishDiagnostics")},
                        {"params", publish_params(uri, doc.version, uri == doc.uri, notes)}}));
    }
    doc.published = std::move(uris);
}

void recheck(Doc &doc) {
    Checked checked = check_buffer(doc.path, doc.text, doc.uri);
    doc.syms = std::move(checked.syms);
    publish(doc, checked.notes);
}

const Json *text_document(const Json &params) {
    const Json *doc = params.find("textDocument");
    if (doc == nullptr || doc->kind != Json::Kind::Object) {
        return nullptr;
    }
    return doc;
}

Json error_response(const Json &id, int code, const std::string &message) {
    return jobj({{"jsonrpc", jstr("2.0")},
                 {"id", id},
                 {"error", jobj({{"code", jint(code)}, {"message", jstr(message)}})}});
}

Json result_response(const Json &id, Json result) {
    return jobj({{"jsonrpc", jstr("2.0")}, {"id", id}, {"result", std::move(result)}});
}

} // namespace

int run_lsp() {
    std::vector<Doc> docs;
    bool shutdown = false;
    auto find_doc = [&](const std::string &uri) -> Doc * {
        for (Doc &doc : docs) {
            if (doc.uri == uri) {
                return &doc;
            }
        }
        return nullptr;
    };
    while (true) {
        const std::optional<std::string> body = read_message();
        if (!body) {
            return shutdown ? 0 : 1;
        }
        const std::optional<Json> message = parse_json(*body);
        if (!message || message->kind != Json::Kind::Object) {
            continue;
        }
        const Json *id = message->find("id");
        const bool request = id != nullptr && id->kind != Json::Kind::Null;
        const std::string method = message->str("method");
        const Json *params = message->find("params");
        const Json empty = jobj({});
        if (params == nullptr) {
            params = &empty;
        }
        if (method == "initialize") {
            if (!request) {
                continue;
            }
            send_json(result_response(
                *id, jobj({{"capabilities",
                            jobj({{"positionEncoding", jstr("utf-16")},
                                 {"textDocumentSync", jint(1)},
                                 {"completionProvider", jobj({{"triggerCharacters", jarr({jstr(".")})}, {"resolveProvider", jbool(false)}})},
                                 {"hoverProvider", jbool(true)},
                                 {"definitionProvider", jbool(true)},
                                 {"documentSymbolProvider", jbool(true)}})},
                           {"serverInfo", jobj({{"name", jstr("tlc")}, {"version", jstr(kVersion)}})}})));
            continue;
        }
        if (method == "shutdown") {
            shutdown = true;
            if (request) {
                send_json(result_response(*id, jnull()));
            }
            continue;
        }
        if (method == "exit") {
            return shutdown ? 0 : 1;
        }
        if (method.empty()) {
            continue;
        }
        if (method == "initialized" || method == "workspace/didChangeConfiguration" || method.starts_with("$/")) {
            continue;
        }
        if (method == "textDocument/didOpen" || method == "textDocument/didChange") {
            const Json *doc_json = text_document(*params);
            if (doc_json == nullptr) {
                continue;
            }
            const std::string uri = doc_json->str("uri");
            std::string text;
            bool got_text = false;
            if (method == "textDocument/didOpen") {
                text = doc_json->str("text");
                got_text = true;
            } else if (const Json *changes = params->find("contentChanges");
                       changes != nullptr && changes->kind == Json::Kind::Array && !changes->a.empty()) {
                const Json &last = changes->a.back();
                if (last.kind == Json::Kind::Object && last.find("range") == nullptr && last.find("text") != nullptr) {
                    text = last.str("text");
                    got_text = true;
                }
            }
            if (!got_text) {
                continue;
            }
            const int version = static_cast<int>(doc_json->integer("version", 0));
            Doc *doc = find_doc(uri);
            if (doc == nullptr) {
                docs.push_back(Doc{});
                doc = &docs.back();
                doc->uri = uri;
            }
            doc->path = open_path_for(uri_to_path(uri));
            doc->text = std::move(text);
            doc->version = version;
            recheck(*doc);
            continue;
        }
        if (method == "textDocument/didClose") {
            const Json *doc_json = text_document(*params);
            if (doc_json == nullptr) {
                continue;
            }
            const std::string uri = doc_json->str("uri");
            for (std::size_t n = 0; n < docs.size(); ++n) {
                if (docs[n].uri != uri) {
                    continue;
                }
                publish(docs[n], {});
                docs.erase(docs.begin() + static_cast<std::ptrdiff_t>(n));
                break;
            }
            continue;
        }
        if (!request) {
            continue;
        }
        if (method == "textDocument/completion" || method == "textDocument/hover" || method == "textDocument/definition" ||
            method == "textDocument/documentSymbol") {
            const Json *doc_json = text_document(*params);
            const std::string uri = doc_json == nullptr ? std::string{} : doc_json->str("uri");
            Doc *doc = find_doc(uri);
            if (doc == nullptr) {
                send_json(result_response(*id, method == "textDocument/documentSymbol" ? jarr({}) : jnull()));
                continue;
            }
            if (method == "textDocument/documentSymbol") {
                send_json(result_response(*id, document_symbols(*doc)));
                continue;
            }
            const Json *position = params->find("position");
            const int line = position == nullptr ? 0 : static_cast<int>(position->integer("line", 0));
            const int character = position == nullptr ? 0 : static_cast<int>(position->integer("character", 0));
            const Cursor cursor = cursor_at(doc->text, line, character);
            if (method == "textDocument/completion") {
                send_json(result_response(*id, complete(*doc, cursor)));
                continue;
            }
            if (cursor.literal || cursor.word.empty()) {
                send_json(result_response(*id, jnull()));
                continue;
            }
            const Sym *sym = nullptr;
            if (const std::string init_type = designated_struct(*doc, cursor); !init_type.empty()) {
                sym = find_field(*doc, init_type, cursor.word);
            } else if (cursor.member) {
                if (!cursor.qualifier.empty()) {
                    sym = qualified_member(doc->syms, doc->uri, cursor);
                } else if (const Sym *base = lookup_bare(doc->syms, doc->uri, cursor.base, cursor.offset)) {
                    sym = lookup_member(doc->syms, *base, cursor.word);
                }
            } else {
                sym = lookup_bare(doc->syms, doc->uri, cursor.word, cursor.offset);
            }
            if (sym == nullptr) {
                send_json(result_response(*id, jnull()));
            } else if (method == "textDocument/hover") {
                send_json(result_response(*id, hover_of(*sym)));
            } else if (method == "textDocument/definition") {
                const Json loc = location_of(*sym);
                if (loc.kind == Json::Kind::Null) {
                    send_json(result_response(*id, jnull()));
                } else {
                    send_json(result_response(*id, jarr({loc})));
                }
            } else {
                send_json(result_response(*id, location_of(*sym)));
            }
            continue;
        }
        send_json(error_response(*id, -32601, "method not found"));
    }
}

} // namespace tlc

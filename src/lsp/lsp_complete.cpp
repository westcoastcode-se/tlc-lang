// Completion, hover, and publishing diagnostics.
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
            ((sym.role == Role::Variable || sym.role == Role::Constant) && sym.exported) ||
            (sym.role == Role::Method && sym.static_method && sym.exported)) {
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

const char *kKeywords[] = {"func",    "type",    "var",     "const",   "struct", "enum",    "union",  "unsafe", "trait",
                           "if",      "else",    "while",   "for",     "do",     "switch",  "case",   "default",
                           "return",  "break",   "continue","defer",   "import", "package", "export", "extern",
                           "test",    "assert",  "sizeof",  "alignof", "typeof",  "new",     "delete",
                           "true",    "false",   "null"};

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
        const uint32_t next = skip_lex(text, i);
        if (next != i) {
            i = next;
            continue;
        }
        const char c = text[i];
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

std::vector<const Sym *> bare_candidates(const std::vector<Sym> &syms, const std::string &uri, const std::string &name,
                                         uint32_t offset) {
    std::vector<const Sym *> found;
    int best_rank = 100;
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
        if (rank < best_rank) {
            best_rank = rank;
            found.clear();
        }
        if (rank == best_rank) {
            found.push_back(&sym);
        }
    }
    return found;
}

std::vector<const Sym *> member_candidates(const std::vector<Sym> &syms, const Sym &base, const std::string &name) {
    std::vector<const Sym *> found;
    if (base.role == Role::Module) {
        for (const Sym &sym : syms) {
            if (sym.package_key == base.package_key && sym.name == name && sym.owner.empty() &&
                (sym.exported || sym.uri == base.uri)) {
                found.push_back(&sym);
            }
        }
        return found;
    }
    const std::string type = (base.role == Role::StructType || base.role == Role::EnumType || base.role == Role::Alias ||
                              base.role == Role::Trait)
                                 ? base.name
                                 : base.type_name;
    if (type.empty()) {
        return found;
    }
    for (const Sym &sym : syms) {
        if (sym.owner == type && sym.name == name) {
            found.push_back(&sym);
        }
    }
    return found;
}

const Sym *latest_sym(const std::vector<const Sym *> &cands) {
    const Sym *best = nullptr;
    uint32_t begin = 0;
    for (const Sym *sym : cands) {
        if (best == nullptr || sym->scope_begin >= begin) {
            best = sym;
            begin = sym->scope_begin;
        }
    }
    return best;
}

// Arguments of the call that follows the name. -1 when the next token is not `(`.
int call_nargs(const std::string &text, uint32_t word_end) {
    uint32_t i = word_end;
    while (i < text.size() && is_space(text[i])) {
        ++i;
    }
    if (i >= text.size() || text[i] != '(') {
        return -1;
    }
    ++i;
    int depth = 1;
    int commas = 0;
    bool any = false;
    while (i < text.size() && depth > 0) {
        const uint32_t next = skip_lex(text, i);
        if (next != i) {
            if (depth == 1 && text[i] != '/') {
                any = true;
            }
            i = next;
            continue;
        }
        const char c = text[i];
        if (c == '(' || c == '[' || c == '{') {
            ++depth;
        } else if (c == ')' || c == ']' || c == '}') {
            --depth;
            if (depth == 0) {
                break;
            }
        } else if (c == ',' && depth == 1) {
            ++commas;
        } else if (depth == 1 && !is_space(c)) {
            any = true;
        }
        ++i;
    }
    if (depth != 0) {
        return -1;
    }
    if (!any && commas == 0) {
        return 0;
    }
    return commas + 1;
}

// One parameter list returns its body. Several lists use the call's argument count, or the declaration under the cursor.
const Sym *refine_overloads(const std::vector<const Sym *> &cands, const std::string &text, const Cursor &cursor,
                            bool prefer_first) {
    if (cands.empty()) {
        return nullptr;
    }
    bool all_fn = true;
    for (const Sym *sym : cands) {
        if (sym->role != Role::Function && sym->role != Role::Method) {
            all_fn = false;
            break;
        }
    }
    if (!all_fn) {
        return prefer_first ? cands.front() : latest_sym(cands);
    }
    struct Group {
        std::string detail;
        bool static_method = false;
        Role role = Role::Function;
        std::string owner;
        const Sym *last = nullptr;
        uint32_t begin = 0;
    };
    std::vector<Group> groups;
    for (const Sym *sym : cands) {
        Group *group = nullptr;
        for (Group &have : groups) {
            if (have.detail == sym->detail && have.static_method == sym->static_method && have.role == sym->role &&
                have.owner == sym->owner) {
                group = &have;
                break;
            }
        }
        if (group == nullptr) {
            groups.push_back(Group{sym->detail, sym->static_method, sym->role, sym->owner, sym, sym->scope_begin});
            continue;
        }
        if (sym->scope_begin >= group->begin) {
            group->last = sym;
            group->begin = sym->scope_begin;
        }
    }
    if (groups.size() == 1) {
        return groups.front().last;
    }
    for (const Sym *sym : cands) {
        const uint32_t end = sym->scope_begin + static_cast<uint32_t>(sym->name.size());
        if (cursor.offset >= sym->scope_begin && cursor.offset < end) {
            return sym;
        }
    }
    const uint32_t word_end = cursor.prefix_begin + static_cast<uint32_t>(cursor.word.size());
    const int nargs = call_nargs(text, word_end);
    if (nargs < 0) {
        return nullptr;
    }
    const Group *matched = nullptr;
    int matches = 0;
    for (const Group &group : groups) {
        const Sym *sym = group.last;
        if (sym->arity < 0) {
            continue;
        }
        const bool ok = sym->variadic ? nargs >= sym->arity : nargs == sym->arity;
        if (!ok) {
            continue;
        }
        ++matches;
        matched = &group;
    }
    if (matches == 1) {
        return matched->last;
    }
    return nullptr;
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

// Index of the `(` that matches `close`, or -1 when the scan falls off the start.
uint32_t matching_open(const std::string &text, uint32_t close) {
    if (close >= text.size() || text[close] != ')') {
        return static_cast<uint32_t>(-1);
    }
    int depth = 0;
    uint32_t i = close;
    while (i > 0) {
        --i;
        const char c = text[i];
        if (c == ')') {
            ++depth;
        } else if (c == '(') {
            if (depth == 0) {
                return i;
            }
            --depth;
        }
    }
    return static_cast<uint32_t>(-1);
}

// Index of the `[` that matches `close`, or -1 when the scan falls off the start.
uint32_t matching_bracket(const std::string &text, uint32_t close) {
    if (close >= text.size() || text[close] != ']') {
        return static_cast<uint32_t>(-1);
    }
    int depth = 0;
    uint32_t i = close;
    while (i > 0) {
        --i;
        const char c = text[i];
        if (c == ']') {
            ++depth;
        } else if (c == '[') {
            if (depth == 0) {
                return i;
            }
            --depth;
        }
    }
    return static_cast<uint32_t>(-1);
}

// `Point{` and `new(allocator) Point{`. A function body `func f() Point {` is not a value.
std::string type_named_before(const Doc &doc, uint32_t brace) {
    const std::string &text = doc.text;
    uint32_t at = skip_back(text, brace);
    // `Box[i32]{` and `new Box[i32]{`. The name is before the argument list.
    if (at > 0 && text[at - 1] == ']') {
        const uint32_t open = matching_bracket(text, at - 1);
        if (open == static_cast<uint32_t>(-1)) {
            return {};
        }
        at = skip_back(text, open);
    }
    if (at == 0 || !ident_char(static_cast<unsigned char>(text[at - 1]))) {
        return {};
    }
    uint32_t end = at;
    uint32_t begin = end;
    while (begin > 0 && ident_char(static_cast<unsigned char>(text[begin - 1]))) {
        --begin;
    }
    const std::string name = text.substr(begin, end - begin);
    uint32_t type_start = begin;
    const uint32_t before_name = skip_back(text, begin);
    if (before_name > 0 && text[before_name - 1] == '.') {
        const uint32_t dot = before_name - 1;
        const uint32_t before_dot = skip_back(text, dot);
        if (before_dot == 0 || !ident_char(static_cast<unsigned char>(text[before_dot - 1]))) {
            return {};
        }
        uint32_t qualifier = before_dot;
        while (qualifier > 0 && ident_char(static_cast<unsigned char>(text[qualifier - 1]))) {
            --qualifier;
        }
        type_start = qualifier;
    }
    int stars = 0;
    const uint32_t before_type = skip_type_prefix(text, type_start, stars);
    if (stars > 0) {
        return {};
    }
    // `new(allocator) Point{` is an initializer. `func f() Point {` is a body.
    if (before_type > 0 && text[before_type - 1] == ')') {
        const uint32_t open = matching_open(text, before_type - 1);
        if (open == static_cast<uint32_t>(-1) || !keyword_back(text, skip_back(text, open), "new")) {
            return {};
        }
    }
    return struct_owner(doc, name);
}

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
    if (const std::string named = type_named_before(doc, brace); !named.empty()) {
        return named;
    }
    // `new [_]Point{{.x` — the inner brace is one element of the list.
    if (at > 0 && (text[at - 1] == '{' || text[at - 1] == ',')) {
        uint32_t list = static_cast<uint32_t>(-1);
        if (text[at - 1] == '{') {
            list = at - 1;
        } else {
            int depth = 0;
            uint32_t i = at - 1;
            while (i > 0) {
                --i;
                if (text[i] == '}') {
                    ++depth;
                } else if (text[i] == '{') {
                    if (depth == 0) {
                        list = i;
                        break;
                    }
                    --depth;
                }
            }
        }
        if (list != static_cast<uint32_t>(-1)) {
            if (const std::string named = type_named_before(doc, list); !named.empty()) {
                return named;
            }
        }
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
        const uint32_t next = skip_lex(text, i);
        if (next != i) {
            i = next;
            continue;
        }
        const char c = text[i];
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

bool alias_slice(const Doc &doc, const Sym &sym) {
    if (sym.is_array || sym.indirections != 0 || sym.type_name.empty()) {
        return false;
    }
    const Sym *same = nullptr;
    const Sym *exported = nullptr;
    for (const Sym &other : doc.syms) {
        if (other.role != Role::Alias || other.name != sym.type_name) {
            continue;
        }
        if (sym.package_key.empty() || other.package_key == sym.package_key) {
            same = &other;
            break;
        }
        if (exported == nullptr && other.exported) {
            exported = &other;
        }
    }
    const Sym *match = same != nullptr ? same : exported;
    return match != nullptr && match->is_array && match->array_len < 0 && match->indirections == 0;
}

bool slice_receiver(const Doc &doc, const Sym &sym) {
    if (sym.role != Role::Variable && sym.role != Role::Constant && sym.role != Role::Field) {
        return false;
    }
    if (sym.indirections != 0) {
        return false;
    }
    if (sym.is_array && sym.array_len < 0) {
        return true;
    }
    return alias_slice(doc, sym);
}

bool alias_sized_array(const Doc &doc, const Sym &sym) {
    if (sym.is_array || sym.indirections != 0 || sym.type_name.empty()) {
        return false;
    }
    const Sym *same = nullptr;
    const Sym *exported = nullptr;
    for (const Sym &other : doc.syms) {
        if (other.role != Role::Alias || other.name != sym.type_name) {
            continue;
        }
        if (sym.package_key.empty() || other.package_key == sym.package_key) {
            same = &other;
            break;
        }
        if (exported == nullptr && other.exported) {
            exported = &other;
        }
    }
    const Sym *match = same != nullptr ? same : exported;
    return match != nullptr && match->is_array && match->array_len >= 0 && match->indirections == 0;
}

bool sized_array_receiver(const Doc &doc, const Sym &sym) {
    if (sym.role != Role::Variable && sym.role != Role::Constant && sym.role != Role::Field) {
        return false;
    }
    if (sym.indirections != 0) {
        return false;
    }
    if (sym.is_array && sym.array_len >= 0) {
        return true;
    }
    return alias_sized_array(doc, sym);
}

namespace {

struct View {
    enum class Kind { None, Value, Type, Func };
    Kind kind = Kind::None;
    Sym sym;
};

bool ident_ending(const std::string &text, uint32_t end, std::string &name, uint32_t &begin) {
    end = skip_back(text, end);
    if (end == 0 || !ident_char(static_cast<unsigned char>(text[end - 1]))) {
        return false;
    }
    begin = end;
    while (begin > 0 && ident_char(static_cast<unsigned char>(text[begin - 1]))) {
        --begin;
    }
    const unsigned char first = static_cast<unsigned char>(text[begin]);
    if (first >= '0' && first <= '9') {
        return false;
    }
    name = text.substr(begin, end - begin);
    return true;
}

bool call_shape(const std::string &text, uint32_t open) {
    const uint32_t at = skip_back(text, open);
    if (at == 0) {
        return false;
    }
    const unsigned char c = static_cast<unsigned char>(text[at - 1]);
    return text[at - 1] == ']' || ident_char(c);
}

bool bracket_range(const std::string &text, uint32_t open, uint32_t close) {
    int depth = 0;
    uint32_t i = open + 1;
    while (i < close && i < text.size()) {
        const uint32_t next = skip_lex(text, i);
        if (next != i) {
            i = next;
            continue;
        }
        const char c = text[i];
        if (c == '(' || c == '[' || c == '{') {
            ++depth;
        } else if ((c == ')' || c == ']' || c == '}') && depth > 0) {
            --depth;
        } else if (c == ':' && depth == 0) {
            return true;
        }
        ++i;
    }
    return false;
}

const Sym *pick_arity(const std::vector<const Sym *> &cands, int nargs) {
    if (cands.size() == 1) {
        return cands.front();
    }
    if (nargs < 0) {
        return nullptr;
    }
    const Sym *matched = nullptr;
    int matches = 0;
    for (const Sym *sym : cands) {
        if (sym->arity < 0) {
            continue;
        }
        const bool ok = sym->variadic ? nargs >= sym->arity : nargs == sym->arity;
        if (!ok) {
            continue;
        }
        ++matches;
        matched = sym;
    }
    return matches == 1 ? matched : nullptr;
}

View view_of(const Sym &sym) {
    View view;
    view.sym = sym;
    switch (sym.role) {
    case Role::Module:
        view.kind = View::Kind::Type;
        view.sym.role = Role::Module;
        break;
    case Role::StructType:
    case Role::EnumType:
    case Role::Alias:
    case Role::Trait:
        view.kind = View::Kind::Type;
        break;
    case Role::Function:
    case Role::Method:
        view.kind = View::Kind::Func;
        break;
    case Role::Variable:
    case Role::Constant:
    case Role::Field:
        view.kind = View::Kind::Value;
        view.sym.role = Role::Variable;
        break;
    default:
        break;
    }
    return view;
}

View value_result(const Sym &fn) {
    if (fn.type_name.empty()) {
        return {};
    }
    View view;
    view.kind = View::Kind::Value;
    view.sym.role = Role::Variable;
    view.sym.type_name = fn.type_name;
    view.sym.indirections = fn.indirections;
    view.sym.optional = fn.optional;
    view.sym.is_array = fn.is_array;
    view.sym.array_len = fn.array_len;
    view.sym.package_key = fn.package_key;
    view.sym.uri = fn.uri;
    return view;
}

const Sym *alias_target(const Doc &doc, const Sym &sym) {
    if (sym.indirections != 0 || sym.is_array || sym.type_name.empty()) {
        return nullptr;
    }
    const Sym *same = nullptr;
    const Sym *exported = nullptr;
    for (const Sym &other : doc.syms) {
        if (other.role != Role::Alias || other.name != sym.type_name) {
            continue;
        }
        if (sym.package_key.empty() || other.package_key == sym.package_key) {
            same = &other;
            break;
        }
        if (exported == nullptr && other.exported) {
            exported = &other;
        }
    }
    const Sym *match = same != nullptr ? same : exported;
    if (match == nullptr || (!match->is_array && match->indirections == 0)) {
        return nullptr;
    }
    return match;
}

// One shell written into `type_name`, such as `[2]i32` left by a nested array.
void adopt_spelled(Sym &sym) {
    const std::string &name = sym.type_name;
    if (name.size() >= 2 && name[0] == '[') {
        const std::size_t end = name.find(']');
        if (end == std::string::npos || end == 0) {
            return;
        }
        const std::string inside = name.substr(1, end - 1);
        sym.is_array = true;
        sym.array_len = -1;
        if (!inside.empty()) {
            bool digits = true;
            int len = 0;
            for (unsigned char c : inside) {
                if (c < '0' || c > '9') {
                    digits = false;
                    break;
                }
                len = len * 10 + (c - '0');
            }
            sym.array_len = digits ? len : 0;
        }
        sym.type_name = name.substr(end + 1);
        return;
    }
    if (name.starts_with("?*")) {
        sym.optional |= 1u;
        sym.indirections += 1;
        sym.type_name = name.substr(2);
        return;
    }
    if (name.starts_with("*")) {
        sym.indirections += 1;
        sym.type_name = name.substr(1);
    }
}

View element_view(Sym sym) {
    if (sym.indirections == 0 && sym.is_array) {
        sym.is_array = false;
        sym.array_len = 0;
        adopt_spelled(sym);
    } else if (sym.indirections > 0 && sym.is_array && sym.array_len >= 0) {
        // `*[N]T` is `(*p)[i]`, one element.
        --sym.indirections;
        sym.optional >>= 1u;
        sym.is_array = false;
        sym.array_len = 0;
        adopt_spelled(sym);
    } else if (sym.indirections > 0 && sym.is_array) {
        // A pointer to a slice steps by one slice header.
        --sym.indirections;
        sym.optional >>= 1u;
    } else if (sym.indirections > 0) {
        --sym.indirections;
        sym.optional >>= 1u;
        adopt_spelled(sym);
    } else {
        return {};
    }
    if (sym.type_name.empty()) {
        return {};
    }
    sym.role = Role::Variable;
    sym.owner.clear();
    sym.name.clear();
    View view;
    view.kind = View::Kind::Value;
    view.sym = std::move(sym);
    return view;
}

bool can_range(const Sym &sym) {
    if ((sym.optional & 1u) != 0) {
        return false;
    }
    if (sym.indirections == 0 && sym.is_array) {
        return true;
    }
    return sym.indirections == 1 && sym.is_array && sym.array_len >= 0;
}

View index_view(const Doc &doc, Sym sym, bool range) {
    bool slice_alias = false;
    std::string alias_name;
    if (sym.indirections == 0 && !sym.is_array) {
        if (const Sym *alias = alias_target(doc, sym)) {
            slice_alias = alias->is_array && alias->array_len < 0;
            alias_name = sym.type_name;
            sym.type_name = alias->type_name;
            sym.indirections = alias->indirections;
            sym.optional = alias->optional;
            sym.is_array = alias->is_array;
            sym.array_len = alias->array_len;
            sym.package_key = alias->package_key;
        }
    }
    if ((sym.optional & 1u) != 0) {
        return {};
    }
    if (range) {
        if (!can_range(sym)) {
            return {};
        }
        View elem = element_view(sym);
        if (elem.kind != View::Kind::Value) {
            return {};
        }
        if (slice_alias) {
            elem.sym.type_name = std::move(alias_name);
            elem.sym.indirections = 0;
            elem.sym.optional = 0;
            elem.sym.is_array = false;
            elem.sym.array_len = 0;
        } else {
            elem.sym.is_array = true;
            elem.sym.array_len = -1;
            elem.sym.indirections = 0;
            elem.sym.optional = 0;
        }
        elem.sym.role = Role::Variable;
        return elem;
    }
    return element_view(std::move(sym));
}

std::vector<const Sym *> methods_named(const Doc &doc, const std::string &owner, const std::string &package, const std::string &name,
                                       bool is_static) {
    std::vector<const Sym *> found;
    for (const Sym &sym : doc.syms) {
        if (sym.role != Role::Method || sym.owner != owner || sym.name != name || sym.static_method != is_static) {
            continue;
        }
        if (!package.empty() && sym.package_key != package) {
            continue;
        }
        found.push_back(&sym);
    }
    return found;
}

View select_member(const Doc &doc, const View &base, const std::string &name, bool want_func, int nargs) {
    if (base.kind == View::Kind::None) {
        return {};
    }
    if (base.sym.role == Role::Module) {
        if (want_func) {
            std::vector<const Sym *> fns;
            for (const Sym &sym : doc.syms) {
                if (sym.role == Role::Function && sym.package_key == base.sym.package_key && sym.owner.empty() &&
                    sym.name == name && (sym.exported || sym.bare)) {
                    fns.push_back(&sym);
                }
            }
            if (const Sym *fn = pick_arity(fns, nargs)) {
                return view_of(*fn);
            }
            return {};
        }
        if (const Sym *type = exported_type(doc.syms, base.sym.package_key, name)) {
            return view_of(*type);
        }
        for (const Sym &sym : doc.syms) {
            if (sym.package_key == base.sym.package_key && sym.owner.empty() && sym.name == name && sym.exported &&
                (sym.role == Role::Variable || sym.role == Role::Constant || sym.role == Role::Function)) {
                return view_of(sym);
            }
        }
        return {};
    }
    if (base.kind == View::Kind::Type) {
        if (want_func) {
            if (const Sym *fn = pick_arity(methods_named(doc, base.sym.name, base.sym.package_key, name, true), nargs)) {
                return view_of(*fn);
            }
            return {};
        }
        for (const Sym &sym : doc.syms) {
            if (sym.owner != base.sym.name || sym.name != name) {
                continue;
            }
            if (!base.sym.package_key.empty() && sym.package_key != base.sym.package_key) {
                continue;
            }
            if (sym.role == Role::Variable || sym.role == Role::Constant) {
                return view_of(sym);
            }
        }
        return {};
    }
    if (base.kind != View::Kind::Value) {
        return {};
    }
    if (want_func) {
        if (const Sym *fn = pick_arity(methods_named(doc, base.sym.type_name, base.sym.package_key, name, false), nargs)) {
            return view_of(*fn);
        }
        return {};
    }
    for (const Sym &sym : doc.syms) {
        if (sym.role == Role::Field && sym.owner == base.sym.type_name && sym.name == name) {
            if (!base.sym.package_key.empty() && sym.package_key != base.sym.package_key && !sym.package_key.empty()) {
                continue;
            }
            return view_of(sym);
        }
    }
    return {};
}

View parse_ending(const Doc &doc, uint32_t end, uint32_t scope, bool want_func, int nargs, int depth);

View name_view(const Doc &doc, const std::string &name, uint32_t scope, bool want_func, int nargs) {
    if (want_func) {
        std::vector<const Sym *> fns;
        for (const Sym *sym : bare_candidates(doc.syms, doc.uri, name, scope)) {
            if (sym->role == Role::Function) {
                fns.push_back(sym);
            }
        }
        if (const Sym *fn = pick_arity(fns, nargs)) {
            return view_of(*fn);
        }
        return {};
    }
    if (const Sym *sym = lookup_bare(doc.syms, doc.uri, name, scope)) {
        if (sym->role == Role::Function || sym->role == Role::Method) {
            return {};
        }
        return view_of(*sym);
    }
    return {};
}

View parse_ending(const Doc &doc, uint32_t end, uint32_t scope, bool want_func, int nargs, int depth) {
    if (depth > 40) {
        return {};
    }
    end = skip_back(doc.text, end);
    if (end == 0 || end > doc.text.size()) {
        return {};
    }
    const char last = doc.text[end - 1];
    if (last == ')') {
        const uint32_t open = matching_open(doc.text, end - 1);
        if (open == static_cast<uint32_t>(-1)) {
            return {};
        }
        if (call_shape(doc.text, open)) {
            const View callee = parse_ending(doc, open, scope, true, call_nargs(doc.text, open), depth + 1);
            if (callee.kind != View::Kind::Func) {
                return {};
            }
            return value_result(callee.sym);
        }
        const View inner = parse_ending(doc, end - 1, scope, false, -1, depth + 1);
        return inner.kind == View::Kind::Value ? inner : View{};
    }
    if (last == ']') {
        const uint32_t open = matching_bracket(doc.text, end - 1);
        if (open == static_cast<uint32_t>(-1)) {
            return {};
        }
        const bool range = bracket_range(doc.text, open, end - 1);
        const View base = parse_ending(doc, open, scope, false, -1, depth + 1);
        if (base.kind == View::Kind::Type && !range && base.sym.role != Role::Module) {
            return base;
        }
        if (base.kind == View::Kind::Func && !range) {
            return base;
        }
        if (base.kind != View::Kind::Value) {
            return {};
        }
        return index_view(doc, base.sym, range);
    }
    std::string name;
    uint32_t begin = 0;
    if (!ident_ending(doc.text, end, name, begin)) {
        return {};
    }
    const uint32_t before = skip_back(doc.text, begin);
    if (before > 0 && doc.text[before - 1] == '.') {
        const View base = parse_ending(doc, before - 1, scope, false, -1, depth + 1);
        return select_member(doc, base, name, want_func, nargs);
    }
    return name_view(doc, name, scope, want_func, nargs);
}

template <typename Push>
void offer_members(const Doc &doc, const Sym &base, Push push) {
    if (base.role == Role::Module) {
        for (const Sym &sym : doc.syms) {
            if (sym.role != Role::Module && sym.package_key == base.package_key && sym.owner.empty() &&
                (sym.exported || sym.bare)) {
                push(sym.name, sym.completion_kind, sym.detail);
            }
        }
        return;
    }
    if (slice_receiver(doc, base) || sized_array_receiver(doc, base)) {
        // `v.len` is the number of elements. The element type's fields are `v[i].field`.
        push("len", 5, "i32");
        if (alias_slice(doc, base)) {
            for (const Sym &sym : doc.syms) {
                if (sym.role != Role::Method || sym.static_method || sym.owner != base.type_name) {
                    continue;
                }
                if (!sym.exported && !document_owns(doc, sym.package_key)) {
                    continue;
                }
                push(sym.name, sym.completion_kind, sym.detail);
            }
        }
        return;
    }
    const std::string type = (base.role == Role::StructType || base.role == Role::EnumType || base.role == Role::Alias ||
                              base.role == Role::Trait)
                                 ? base.name
                                 : base.type_name;
    const bool on_type = base.role == Role::StructType || base.role == Role::EnumType || base.role == Role::Trait ||
                         base.role == Role::Alias;
    for (const Sym &sym : doc.syms) {
        if (type.empty() || sym.owner != type) {
            continue;
        }
        if (sym.role == Role::Field && !sym.exported && !document_owns(doc, sym.package_key)) {
            continue;
        }
        if (sym.role == Role::Method && on_type != sym.static_method) {
            continue;
        }
        if (sym.role == Role::Method && !sym.exported && !document_owns(doc, sym.package_key)) {
            continue;
        }
        // A trait field is offered on a value. `Shape.` is the type name.
        if (sym.role == Role::Field && on_type && base.role == Role::Trait) {
            continue;
        }
        // `Entity.next` is offered on the type. `e.next` is a field.
        // Another package sees the variable only when it is exported.
        if ((sym.role == Role::Variable || sym.role == Role::Constant) && !sym.owner.empty() &&
            (!on_type || sym.package_key != base.package_key || (!sym.exported && !document_owns(doc, base.package_key)))) {
            continue;
        }
        push(sym.name, sym.completion_kind, sym.detail);
    }
}

} // namespace

// `Point.make(1).`, `slice[0].`, and `(p).` name a value. A bare `p.` does not.
ExprBase expression_base(const Doc &doc, const Cursor &cursor) {
    ExprBase out;
    if (cursor.literal || cursor.member) {
        return out;
    }
    const uint32_t after_dot = skip_back(doc.text, cursor.prefix_begin);
    if (after_dot == 0 || doc.text[after_dot - 1] != '.') {
        return out;
    }
    const uint32_t receiver = skip_back(doc.text, after_dot - 1);
    if (receiver == 0) {
        return out;
    }
    const char last = doc.text[receiver - 1];
    if (last != ')' && last != ']') {
        return out;
    }
    // The receiver is not a name. An unresolved call or index offers nothing, not keywords.
    out.handled = true;
    const View view = parse_ending(doc, after_dot - 1, cursor.offset, false, -1, 0);
    if (view.kind == View::Kind::Value || view.kind == View::Kind::Type) {
        out.sym = view.sym;
    }
    return out;
}

Json completion_item(const std::string &label, int kind, const std::string &detail, const Doc &doc, const Cursor &cursor) {
    std::vector<std::pair<std::string, Json>> fields;
    fields.emplace_back("label", jstr(label));
    fields.emplace_back("kind", jint(kind));
    if (!detail.empty()) {
        fields.emplace_back("detail", jstr(detail));
    }
    const Pos start = locate(doc.text, cursor.prefix_begin);
    const uint32_t word_end = cursor.prefix_begin + static_cast<uint32_t>(cursor.word.size());
    const Pos end = locate(doc.text, word_end);
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
                        ((sym.role == Role::Variable || sym.role == Role::Constant) && sym.exported) ||
                        (sym.role == Role::Method && sym.static_method && sym.exported)) {
                        push(sym.name, sym.completion_kind, sym.detail);
                    }
                }
            }
            return jobj({{"isIncomplete", jbool(false)}, {"items", jarr(std::move(items))}});
        }
        if (const Sym *base = lookup_bare(doc.syms, doc.uri, cursor.base, cursor.offset)) {
            offer_members(doc, *base, push);
        }
    } else if (!cursor.literal) {
        const ExprBase expr = expression_base(doc, cursor);
        if (expr.handled) {
            offer_members(doc, expr.sym, push);
            return jobj({{"isIncomplete", jbool(false)}, {"items", jarr(std::move(items))}});
        }
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

Json hover_text(const std::string &value, Pos start, Pos end) {
    return jobj({{"contents", jobj({{"kind", jstr("plaintext")}, {"value", jstr(value)}})}, {"range", range_json(start, end)}});
}

Json hover_of(const Sym &sym, Pos start, Pos end) {
    return hover_text(sym.detail.empty() ? sym.name : sym.detail, start, end);
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
    doc.notes = checked.notes;
    publish(doc, doc.notes);
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

} // namespace tlc::lsp_impl

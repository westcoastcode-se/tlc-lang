#pragma once

#include "diag/diag.hpp"
#include "driver/driver.hpp"
#include "lex/lexer.hpp"
#include "parse/parser.hpp"
#include "pkg/pkg.hpp"
#include "check/sema.hpp"

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tlc::lsp_impl {


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

Json jnull();
Json jbool(bool value);
Json jint(int64_t value);
Json jstr(std::string value);
Json jarr(std::vector<Json> value);
Json jobj(std::vector<std::pair<std::string, Json>> value);
void dump_string(std::string &out, std::string_view text);
void dump(std::string &out, const Json &json);

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

std::optional<Json> parse_json(std::string_view text);
bool write_all(int fd, std::string_view bytes);
bool read_exact(std::string &out, std::size_t count);
std::optional<std::string> read_message();
void send_json(const Json &json);
bool ident_char(unsigned char c);
int utf16_units(std::string_view text);

struct Pos {
    int line = 0;
    int character = 0;
};

Pos locate(const std::string &text, uint32_t offset);
uint32_t offset_at(const std::string &text, int line, int character);
uint32_t skip_lex(const std::string &text, uint32_t i);
bool in_literal(const std::string &text, uint32_t offset);
uint32_t match_brace(const std::string &text, uint32_t open);
std::string percent_decode(std::string_view text);
std::string uri_to_path(std::string_view uri);
std::string path_to_uri(const std::string &path);
std::string open_path_for(const std::string &path);
Json range_json(Pos start, Pos end);

struct Place {
    std::string type_name;
    int indirections = 0;
    uint32_t optional = 0;
    uint32_t const_pointee = 0;
    bool is_array = false;
    int array_len = 0;
    // One extra empty `[]` before the named type: `[][]i32` and `[2][]i32`.
    bool elem_slice = false;
    // `[]const u8`. Const sits on the leaf, inside the brackets.
    bool elem_const = false;
};

std::string spell_type(const Place &place);
Place place_of_slot(const ResultSlot &slot);
Place place_of_param(const Param &param);
Place place_of_var(const VarDecl &var);
Place place_of_field(const FieldDecl &field);

std::string spell_fn_param(const FnParam &param);
std::string spell_fn_params(const std::vector<FnParam> &params, bool variadic);
std::string spell_fn_results(const std::string &type_name, int indirections, uint32_t optional, uint32_t const_pointee,
                             const std::vector<FnParam> &many);
std::string spell_fn_param(const FnParam &param);
std::string spell_fn_value(const std::string &type_name, int ret_indirections, uint32_t ret_optional, uint32_t ret_const,
                           const std::vector<FnParam> &params, bool variadic, const std::vector<FnParam> &results);
std::string spell_array_element(const Expr::Init &init);
std::string spell_slot(const ResultSlot &slot);
std::string spell_param(const Param &param);
std::string spell_var(const VarDecl &var);
std::string spell_field(const FieldDecl &field);
std::string spell_results(const std::vector<ResultSlot> &results);
std::string spell_function(const FunctionDecl &fn);

enum class Role { Function, Method, Field, Variable, Constant, StructType, EnumType, EnumMember, Alias, Trait, Module, TypeParam };


struct Sym {
    std::string name;
    std::string detail;
    // `//` lines immediately above the declaration. A blank line ends them.
    std::string docs;
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
    // Bit 0 is the outermost pointer. A set bit is `?*`.
    uint32_t optional = 0;
    bool is_array = false;
    int array_len = 0;
    // `var t = (40, 2)`. `array_len` is the element count. `.len` is that count.
    bool is_tuple = false;
    // A function on a struct or enum with no `this` parameter.
    bool static_method = false;
    // Remaining parameters. The receiver is not one of them. -1 when this symbol is not a function.
    int arity = -1;
    bool variadic = false;
    uint32_t scope_begin = 0;
    uint32_t scope_end = 0;
    // The initializer of this binding. A use inside it still names the previous binding.
    uint32_t init_begin = 0;
    uint32_t init_end = 0;
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
    std::vector<Note> notes;
    std::vector<std::string> published;
};

int completion_kind(Role role);
int symbol_kind(Role role);
void mark_direct(Sym &sym, const std::string &type_name, int indirections, bool is_array, int array_len = 0,
                 uint32_t optional = 0);
void apply_result(Sym &sym, const FunctionDecl &fn);
void note_params(Sym &sym, const FunctionDecl &fn);
void put_loc(Sym &sym, const std::string &text, SourceLoc loc, const std::string &name);
const SourceFile *file_of(const SourceSet &sources, uint32_t id);
void add_sym(std::vector<Sym> &out, Sym sym);

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
    int array_len = 0;
    // `array_len` is the element count.
    bool is_tuple = false;
    bool known = false;
};

Shape shape_from_slot(const ResultSlot &slot);
Shape shape_from_sym(const Sym &sym);
void collect_fn_decls(const Stmt &stmt, std::vector<const FunctionDecl *> &fns);
void collect_fn(const FunctionDecl &fn, std::vector<const FunctionDecl *> &fns);
const FunctionDecl *find_free(const UnitIndex &index, const std::string &name);
const FunctionDecl *find_method(const UnitIndex &index, const std::string &owner, const std::string &name, bool is_static);
bool unit_type_name(const UnitIndex &index, const std::string &name);
const Sym *find_free_sym(const std::vector<Sym> &syms, const std::string &name);
const Sym *find_method_sym(const std::vector<Sym> &syms, const std::string &owner, const std::string &name, bool is_static);

Shape shape_of_expr(const Expr &expr, const UnitIndex &index, const std::vector<Sym> &syms, const std::string &uri,
                    uint32_t offset);
bool type_base(const UnitIndex &index, const std::vector<Sym> &syms, const std::string &uri, const std::string &name,
               uint32_t offset, std::string &owner);
const FunctionDecl *callee_fn(const Expr &callee, const UnitIndex &index, const std::vector<Sym> &syms, const std::string &uri,
                              uint32_t offset);
const Sym *callee_sym(const Expr &callee, const UnitIndex &index, const std::vector<Sym> &syms, const std::string &uri,
                      uint32_t offset);
std::vector<Shape> call_shapes(const Expr::Call &call, const UnitIndex &index, const std::vector<Sym> &syms, const std::string &uri,
                               uint32_t offset);
Shape shape_of_expr(const Expr &expr, const UnitIndex &index, const std::vector<Sym> &syms, const std::string &uri,
                    uint32_t offset);
std::vector<Shape> shapes_of(const Expr &expr, const UnitIndex &index, const std::vector<Sym> &syms, const std::string &uri,
                             uint32_t offset);
void note_shape(Sym &sym, const Shape &shape);

void walk_stmt(const Stmt &stmt, const std::string &text, const std::string &uri, uint32_t scope_end, std::vector<Sym> &out,
               const UnitIndex &index);
void add_function_locals(const FunctionDecl &fn, const std::string &text, const std::string &uri, uint32_t scope_end,
                         std::vector<Sym> &out, const UnitIndex &index);
void walk_stmt(const Stmt &stmt, const std::string &text, const std::string &uri, uint32_t scope_end, std::vector<Sym> &out,
               const UnitIndex &index);
uint32_t body_end(const FunctionDecl &fn, const std::string &text, uint32_t fallback);
void index_unit(const PackageNode &node, const std::string &text, const std::string &uri, bool entry, std::vector<Sym> &out);
void index_imports(const PackageGraph &graph, const PackageNode &entry, const std::string &text, const std::string &uri,
                   std::vector<Sym> &out);

struct Checked {
    std::vector<Sym> syms;
    std::vector<Note> notes;
};

Checked check_buffer(const std::string &path, const std::string &text, const std::string &client_uri);
Cursor cursor_at(const std::string &text, int line, int character);
bool prefix_ok(const std::string &name, const std::string &prefix);
const Sym *lookup_bare(const std::vector<Sym> &syms, const std::string &uri, const std::string &name, uint32_t offset);
bool document_owns(const Doc &doc, const std::string &package_key);
const Sym *exported_type(const std::vector<Sym> &syms, const std::string &package_key, const std::string &name);
const Sym *qualified_member(const std::vector<Sym> &syms, const std::string &uri, const Cursor &cursor);
Json location_of(const Sym &sym);
bool is_space(char c);
uint32_t skip_back(const std::string &text, uint32_t pos);
bool keyword_back(const std::string &text, uint32_t end, std::string_view word);
uint32_t innermost_brace(const std::string &text, uint32_t pos);
std::vector<const Sym *> bare_candidates(const std::vector<Sym> &syms, const std::string &uri, const std::string &name,
                                         uint32_t offset);
std::vector<const Sym *> member_candidates(const std::vector<Sym> &syms, const Sym &base, const std::string &name);
const Sym *latest_sym(const std::vector<const Sym *> &cands);
int call_nargs(const std::string &text, uint32_t word_end);
const Sym *refine_overloads(const std::vector<const Sym *> &cands, const std::string &text, const Cursor &cursor,
                            bool prefer_first);
uint32_t skip_type_prefix(const std::string &text, uint32_t begin, int &stars);
bool has_fields(const Doc &doc, const std::string &owner);
std::string struct_owner(const Doc &doc, const std::string &name);
const Sym *enclosing_function(const Doc &doc, uint32_t offset);
std::string field_direct_type(const Doc &doc, const std::string &owner, const std::string &field);
uint32_t matching_open(const std::string &text, uint32_t close);
uint32_t matching_bracket(const std::string &text, uint32_t close);
std::string type_named_before(const Doc &doc, uint32_t brace);

std::string type_of_brace(const Doc &doc, uint32_t brace);
std::string type_of_brace(const Doc &doc, uint32_t brace);
std::string designated_struct(const Doc &doc, const Cursor &cursor);
bool field_already_named(const std::string &text, uint32_t brace, uint32_t dot, const std::string &name);
const Sym *find_field(const Doc &doc, const std::string &owner, const std::string &name);
bool alias_slice(const Doc &doc, const Sym &sym);
bool slice_receiver(const Doc &doc, const Sym &sym);
bool alias_sized_array(const Doc &doc, const Sym &sym);
bool sized_array_receiver(const Doc &doc, const Sym &sym);
Json completion_item(const std::string &label, int kind, const std::string &detail, const Doc &doc, const Cursor &cursor);

// The value before `.` when that receiver is a call, an index, or a parenthesized expression.
// `handled` is false when the receiver is a name. `sym` is then the value, or the type `Name[args]`.
struct ExprBase {
    bool handled = false;
    Sym sym;
};

ExprBase expression_base(const Doc &doc, const Cursor &cursor);
Json complete(const Doc &doc, const Cursor &cursor);
Json hover_text(const std::string &value, Pos start, Pos end);
Json hover_of(const Sym &sym, Pos start, Pos end);
std::vector<Json> outline_children(const Doc &doc, const Sym &parent);
Json document_symbols(const Doc &doc);
Json publish_params(const std::string &uri, int version, bool with_version, const std::vector<Note> &notes);
void publish(Doc &doc, const std::vector<Note> &notes);
void recheck(Doc &doc);
const Json *text_document(const Json &params);
Json error_response(const Json &id, int code, const std::string &message);
Json result_response(const Json &id, Json result);
std::string apply_content_changes(const std::string &text, const Json &changes);
const Sym *symbol_at(const Doc &doc, const Cursor &cursor);
Json rename_edits(const Doc &doc, const Cursor &cursor, const std::string &new_name);
Json format_edits(const Doc &doc);
Json code_actions(const Doc &doc, Pos start, Pos end);
Json signature_help(const Doc &doc, uint32_t offset);
// Names in `doc`, encoded as semantic-token deltas. `ranged` keeps tokens that overlap `[start, end)`.
// The result is `{"data":[...]}` even when nothing matches. The caller sends null for an unknown document.
Json semantic_tokens(const Doc &doc, bool ranged, Pos start, Pos end);

} // namespace tlc::lsp_impl

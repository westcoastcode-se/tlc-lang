// Rename, formatting, code actions, and signature help.
#include "lsp/lsp.hpp"

#include "lsp/internal.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tlc::lsp_impl {

namespace {

bool ident_start(unsigned char c) { return ident_char(c) && (c < '0' || c > '9'); }

bool good_name(const std::string &name) {
    if (name.empty() || !ident_start(static_cast<unsigned char>(name.front()))) {
        return false;
    }
    for (unsigned char c : name) {
        if (!ident_char(c)) {
            return false;
        }
    }
    return true;
}

std::string trim_copy(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t')) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t')) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

// The binding a bare name sees. A use in a later binding's initializer still sees the earlier one.
const Sym *binding_at(const std::vector<Sym> &syms, const std::string &uri, const std::string &name, uint32_t offset) {
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
            bool earlier = false;
            if (sym.init_end > sym.init_begin && offset >= sym.init_begin && offset < sym.init_end) {
                for (const Sym &other : syms) {
                    if (&other == &sym || other.name != name || !other.bare || !other.local || other.uri != uri) {
                        continue;
                    }
                    if (offset >= other.scope_begin && offset <= other.scope_end && other.scope_begin < sym.scope_begin) {
                        earlier = true;
                        break;
                    }
                }
            }
            if (earlier) {
                continue;
            }
            rank = 0;
        } else if (sym.uri == uri) {
            rank = 1;
        }
        const bool closer = best == nullptr || rank < best_rank || (rank == best_rank && sym.scope_begin >= best_begin);
        if (closer) {
            best = &sym;
            best_rank = rank;
            best_begin = sym.scope_begin;
        }
    }
    return best;
}

uint32_t ident_end(const std::string &text, uint32_t begin) {
    uint32_t end = begin;
    while (end < text.size() && ident_char(static_cast<unsigned char>(text[end]))) {
        ++end;
    }
    return end;
}

// Identifier that ends at `end`, skipping one `[...]` and one dot.
bool name_before(const std::string &text, uint32_t end, std::string &name, uint32_t &begin, bool &member, std::string &base) {
    member = false;
    base.clear();
    uint32_t at = skip_back(text, end);
    if (at > 0 && text[at - 1] == ']') {
        const uint32_t open = matching_bracket(text, at - 1);
        if (open == static_cast<uint32_t>(-1)) {
            return false;
        }
        at = skip_back(text, open);
    }
    if (at == 0 || !ident_char(static_cast<unsigned char>(text[at - 1]))) {
        return false;
    }
    begin = at;
    while (begin > 0 && ident_char(static_cast<unsigned char>(text[begin - 1]))) {
        --begin;
    }
    if (!ident_start(static_cast<unsigned char>(text[begin]))) {
        return false;
    }
    name = text.substr(begin, at - begin);
    const uint32_t dot = skip_back(text, begin);
    if (dot > 0 && text[dot - 1] == '.') {
        uint32_t base_end = skip_back(text, dot - 1);
        if (base_end > 0 && ident_char(static_cast<unsigned char>(text[base_end - 1]))) {
            uint32_t base_begin = base_end;
            while (base_begin > 0 && ident_char(static_cast<unsigned char>(text[base_begin - 1]))) {
                --base_begin;
            }
            if (ident_start(static_cast<unsigned char>(text[base_begin]))) {
                member = true;
                base = text.substr(base_begin, base_end - base_begin);
            }
        }
    }
    return true;
}

Json text_edit(const std::string &text, uint32_t begin, uint32_t end, const std::string &neu) {
    return jobj({{"range", range_json(locate(text, begin), locate(text, end))}, {"newText", jstr(neu)}});
}

} // namespace

const Sym *symbol_at(const Doc &doc, const Cursor &cursor) {
    if (cursor.literal || cursor.word.empty()) {
        return nullptr;
    }
    if (const std::string init_type = designated_struct(doc, cursor); !init_type.empty()) {
        return find_field(doc, init_type, cursor.word);
    }
    if (cursor.member) {
        if (!cursor.qualifier.empty()) {
            return qualified_member(doc.syms, doc.uri, cursor);
        }
        if (const Sym *base = binding_at(doc.syms, doc.uri, cursor.base, cursor.offset)) {
            return refine_overloads(member_candidates(doc.syms, *base, cursor.word), doc.text, cursor, true);
        }
        return nullptr;
    }
    return refine_overloads(bare_candidates(doc.syms, doc.uri, cursor.word, cursor.offset), doc.text, cursor, false);
}

Json rename_edits(const Doc &doc, const Cursor &cursor, const std::string &new_name) {
    if (!good_name(new_name)) {
        return jnull();
    }
    // A bare use in an initializer still names the earlier binding.
    const Sym *target = !cursor.member && !cursor.word.empty()
                            ? binding_at(doc.syms, doc.uri, cursor.word, cursor.offset)
                            : symbol_at(doc, cursor);
    if (target == nullptr) {
        target = symbol_at(doc, cursor);
    }
    if (target == nullptr || target->name.empty()) {
        return jnull();
    }
    std::vector<std::pair<uint32_t, uint32_t>> spans;
    auto add_span = [&](uint32_t begin, uint32_t end) {
        if (begin >= end) {
            return;
        }
        for (const auto &have : spans) {
            if (have.first == begin) {
                return;
            }
        }
        spans.emplace_back(begin, end);
    };
    if (target->has_loc) {
        const uint32_t begin = offset_at(doc.text, target->start.line, target->start.character);
        add_span(begin, ident_end(doc.text, begin));
    }
    uint32_t i = 0;
    while (i < doc.text.size()) {
        const uint32_t next = skip_lex(doc.text, i);
        if (next != i) {
            i = next;
            continue;
        }
        if (!ident_start(static_cast<unsigned char>(doc.text[i]))) {
            ++i;
            continue;
        }
        const uint32_t end = ident_end(doc.text, i);
        const std::string word = doc.text.substr(i, end - i);
        if (word == target->name) {
            const uint32_t before = skip_back(doc.text, i);
            const bool dotted = before > 0 && doc.text[before - 1] == '.';
            if (dotted) {
                std::string dotted_name;
                uint32_t name_at = 0;
                bool member = false;
                std::string base;
                if (name_before(doc.text, end, dotted_name, name_at, member, base) && member) {
                    Cursor at;
                    at.member = true;
                    at.word = dotted_name;
                    at.base = base;
                    at.prefix_begin = name_at;
                    at.offset = name_at;
                    if (symbol_at(doc, at) == target) {
                        add_span(i, end);
                    }
                }
            } else if (binding_at(doc.syms, doc.uri, word, i) == target) {
                add_span(i, end);
            }
        }
        i = end;
    }
    if (spans.empty()) {
        return jnull();
    }
    std::vector<Json> edits;
    for (const auto &span : spans) {
        edits.push_back(text_edit(doc.text, span.first, span.second, new_name));
    }
    return jobj({{"changes", jobj({{doc.uri, jarr(std::move(edits))}})}});
}

std::string format_source(const std::string &text) {
    std::string out;
    int depth = 0;
    bool line_start = true;
    uint32_t i = 0;
    while (i < text.size()) {
        if (line_start) {
            while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) {
                ++i;
            }
            int use = depth;
            if (i < text.size() && text[i] == '}' && use > 0) {
                --use;
            }
            out.append(static_cast<std::size_t>(use) * 4, ' ');
            line_start = false;
            if (i >= text.size()) {
                break;
            }
        }
        const uint32_t next = skip_lex(text, i);
        if (next != i) {
            out.append(text, i, next - i);
            i = next;
            continue;
        }
        const char c = text[i];
        if (c == '{') {
            ++depth;
        } else if (c == '}' && depth > 0) {
            --depth;
        }
        if (c == '\n') {
            while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) {
                out.pop_back();
            }
            line_start = true;
        }
        out.push_back(c);
        ++i;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) {
        out.pop_back();
    }
    if (!out.empty() && out.back() != '\n') {
        out.push_back('\n');
    }
    return out;
}

Json format_edits(const Doc &doc) {
    const std::string formatted = format_source(doc.text);
    if (formatted == doc.text) {
        return jarr({});
    }
    uint32_t end = static_cast<uint32_t>(doc.text.size());
    if (!doc.text.empty() && doc.text.back() == '\n') {
        // The range covers the text the edit replaces, including the last newline.
    }
    return jarr({text_edit(doc.text, 0, end, formatted)});
}

Json code_actions(const Doc &doc, Pos start, Pos end) {
    std::vector<Json> actions;
    for (const Note &note : doc.notes) {
        if (note.uri != doc.uri || note.message != "expected ';'") {
            continue;
        }
        const bool caret = start.line == end.line && start.character == end.character;
        const bool on_line = note.start.line == start.line;
        const bool inside = (note.start.line > start.line ||
                             (note.start.line == start.line && note.start.character >= start.character)) &&
                            (note.start.line < end.line || (note.start.line == end.line && note.start.character <= end.character));
        if (!inside && !(caret && on_line)) {
            continue;
        }
        const Json edit = jobj({{"changes", jobj({{doc.uri, jarr({jobj({{"range", range_json(note.start, note.start)},
                                                                        {"newText", jstr(";")}})})}})}});
        actions.push_back(jobj({{"title", jstr("Insert ';'")}, {"kind", jstr("quickfix")}, {"edit", edit}}));
    }
    return jarr(std::move(actions));
}

namespace {

struct OpenCall {
    uint32_t paren = 0;
    std::string name;
    std::string base;
    bool member = false;
    uint32_t name_at = 0;
    int commas = 0;
};

} // namespace

Json signature_help(const Doc &doc, uint32_t offset) {
    std::vector<OpenCall> stack;
    int brackets = 0;
    uint32_t i = 0;
    while (i < doc.text.size()) {
        const uint32_t next = skip_lex(doc.text, i);
        if (next != i) {
            i = next;
            continue;
        }
        if (i > offset && (stack.empty() || doc.text[i] != ')')) {
            break;
        }
        const char c = doc.text[i];
        if (c == '(') {
            OpenCall call;
            call.paren = i;
            name_before(doc.text, i, call.name, call.name_at, call.member, call.base);
            stack.push_back(std::move(call));
        } else if (c == ')') {
            if (!stack.empty()) {
                stack.pop_back();
            }
        } else if (c == '[' || c == '{') {
            ++brackets;
        } else if ((c == ']' || c == '}') && brackets > 0) {
            --brackets;
        } else if (c == ',' && brackets == 0 && !stack.empty() && i < offset) {
            ++stack.back().commas;
        }
        if (i >= offset && c != '(') {
            break;
        }
        ++i;
    }
    if (stack.empty() || stack.back().name.empty()) {
        return jnull();
    }
    const OpenCall &call = stack.back();
    Cursor cursor;
    cursor.member = call.member;
    cursor.word = call.name;
    cursor.base = call.base;
    cursor.prefix_begin = call.name_at;
    cursor.offset = call.name_at;
    std::vector<const Sym *> found;
    if (call.member) {
        if (const Sym *base = binding_at(doc.syms, doc.uri, call.base, call.name_at)) {
            found = member_candidates(doc.syms, *base, call.name);
        }
    } else {
        found = bare_candidates(doc.syms, doc.uri, call.name, call.name_at);
    }
    std::vector<const Sym *> functions;
    for (const Sym *sym : found) {
        if (sym->role == Role::Function || sym->role == Role::Method) {
            functions.push_back(sym);
        }
    }
    if (const Sym *one = refine_overloads(functions, doc.text, cursor, false); one != nullptr) {
        functions.clear();
        functions.push_back(one);
    }
    if (functions.empty()) {
        return jnull();
    }
    std::vector<Json> signatures;
    for (const Sym *sym : functions) {
        std::vector<std::string> params;
        std::string result;
        const auto open = sym->detail.find('(');
        if (open != std::string::npos) {
            std::string current;
            int depth = 0;
            for (std::size_t n = open + 1; n < sym->detail.size(); ++n) {
                const char c = sym->detail[n];
                if (c == '(' || c == '[') {
                    ++depth;
                    current.push_back(c);
                } else if (c == ')' && depth == 0) {
                    const std::string piece = trim_copy(current);
                    if (!piece.empty()) {
                        params.push_back(piece);
                    }
                    result = sym->detail.substr(n + 1);
                    break;
                } else if (c == ')' || c == ']') {
                    if (depth > 0) {
                        --depth;
                    }
                    current.push_back(c);
                } else if (c == ',' && depth == 0) {
                    params.push_back(trim_copy(current));
                    current.clear();
                } else {
                    current.push_back(c);
                }
            }
        }
        if (sym->role == Role::Method && !sym->static_method && !params.empty()) {
            params.erase(params.begin());
        }
        std::string label = sym->name + "(";
        std::vector<Json> labeled;
        for (std::size_t n = 0; n < params.size(); ++n) {
            if (n != 0) {
                label += ", ";
            }
            label += params[n];
            labeled.push_back(jobj({{"label", jstr(params[n])}}));
        }
        label.push_back(')');
        label += result;
        int active = call.commas;
        if (!params.empty() && active >= static_cast<int>(params.size())) {
            active = static_cast<int>(params.size()) - 1;
        }
        if (params.empty()) {
            active = 0;
        }
        signatures.push_back(jobj({{"label", jstr(label)},
                                   {"parameters", jarr(std::move(labeled))},
                                   {"activeParameter", jint(active)}}));
    }
    return jobj({{"signatures", jarr(std::move(signatures))}, {"activeSignature", jint(0)}});
}

} // namespace tlc::lsp_impl

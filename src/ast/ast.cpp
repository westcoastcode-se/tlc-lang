#include "ast/ast.hpp"

#include <type_traits>

namespace tlc {

Expr::Expr() = default;
Expr::~Expr() = default;
Expr::Expr(Expr &&) noexcept = default;
Expr &Expr::operator=(Expr &&) noexcept = default;

Stmt::Stmt() = default;
Stmt::~Stmt() = default;
Stmt::Stmt(Stmt &&) noexcept = default;
Stmt &Stmt::operator=(Stmt &&) noexcept = default;

FieldDecl::FieldDecl(const FieldDecl &other)
    : loc(other.loc), name(other.name), type_name(other.type_name), type_package(other.type_package),
      is_struct(other.is_struct), is_enum(other.is_enum), indirections(other.indirections), optional(other.optional),
      const_pointee(other.const_pointee), is_array(other.is_array), elem_const(other.elem_const),
      is_const(other.is_const), elem_slice(other.elem_slice), shells(other.shells), array_len(other.array_len),
      array_len_name(other.array_len_name), type_args(other.type_args), is_fn_ptr(other.is_fn_ptr),
      ret_indirections(other.ret_indirections), ret_optional(other.ret_optional),
      ret_const_pointee(other.ret_const_pointee), fn_params(other.fn_params), fn_variadic(other.fn_variadic),
      fn_results(other.fn_results), is_tuple(other.is_tuple), tuple_parts(other.tuple_parts),
      exported(other.exported), init(other.init ? clone_expr(*other.init) : nullptr),
      inline_fields(other.inline_fields) {}

FieldDecl &FieldDecl::operator=(const FieldDecl &other) {
    if (this == &other) {
        return *this;
    }
    FieldDecl copy(other);
    *this = std::move(copy);
    return *this;
}

SourceLoc expr_loc(const Expr &expr) {
    return std::visit([](const auto &node) { return node.loc; }, expr.data);
}

namespace {

void append_indent(std::string &out, int indent) {
    out.append(static_cast<std::size_t>(indent) * 2, ' ');
}

std::string quote_string(const std::string &text);

void append_extern(std::string &out, bool is_extern, bool extern_c, const std::string &link_name = {}) {
    if (!is_extern) {
        return;
    }
    if (!extern_c && link_name.empty()) {
        out += " extern";
        return;
    }
    out += " extern(";
    if (extern_c) {
        out += "abi=\"c\"";
        if (!link_name.empty()) {
            out += ", ";
        }
    }
    if (!link_name.empty()) {
        out += "name=";
        out += quote_string(link_name);
    }
    out += ')';
}

void dump_stmt(const Stmt &stmt, std::string &out, int indent);
void dump_function(const FunctionDecl &fn, std::string &out, int indent);

std::string type_text(const std::string &name, const std::string &package, bool is_struct, bool is_enum,
                       int indirections, bool is_array, int array_len, uint32_t optional = 0,
                       const std::string &len_name = {}, uint32_t const_pointee = 0) {
    std::string text;
    if (!package.empty()) {
        text = package + "." + name;
    } else if (is_enum) {
        text = name.empty() ? "enum" : "enum " + name;
    } else if (is_struct) {
        text = "struct " + name;
    } else {
        text = name;
    }
    if (indirections > 0 && (const_pointee & (1u << (indirections - 1))) != 0) {
        text = "const " + text;
    }
    for (int i = 0; i < indirections; ++i) {
        if ((optional & (1u << i)) != 0) {
            text += '?';
        }
        text += '*';
        if (i + 1 < indirections && (const_pointee & (1u << i)) != 0) {
            text += "const ";
        }
    }
    if (is_array) {
        text += '[';
        if (!len_name.empty()) {
            text += len_name;
        } else if (array_len >= 0) {
            text += std::to_string(array_len);
        }
        text += ']';
    }
    return text;
}

void append_many_results(std::string &out, const std::vector<FnParam> &results);

void dump_field(const FieldDecl &field, std::string &out, int indent) {
    append_indent(out, indent);
    out += "(field ";
    if (field.exported) {
        out += "export ";
    }
    out += field.name;
    if (!field.inline_fields.empty()) {
        out += " struct\n";
        for (const FieldDecl &inner : field.inline_fields) {
            dump_field(inner, out, indent + 1);
        }
        append_indent(out, indent);
        out += ")\n";
        return;
    }
    out += ' ';
    out += type_text(field.type_name, field.type_package, field.is_struct, field.is_enum, field.indirections,
                     field.is_array, field.array_len, field.optional, {}, field.const_pointee);
    if (field.is_fn_ptr) {
        out += " fnptr";
        append_many_results(out, field.fn_results);
    }
    out += ")\n";
}

void append_many_results(std::string &out, const std::vector<FnParam> &results) {
    if (results.size() == 1 && results.front().is_tuple) {
        out += " ((";
        const std::vector<FnParam> &parts = results.front().results;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (i != 0) {
                out += ", ";
            }
            const FnParam &part = parts[i];
            out += type_text(part.type_name, part.type_package, part.is_struct, part.is_enum, part.indirections,
                             part.is_array, part.array_len, part.optional, {}, part.const_pointee);
        }
        out += "))";
        return;
    }
    if (results.size() == 1 && results.front().is_fn_ptr) {
        const FnParam &part = results.front();
        out += " (";
        out += type_text(part.type_name, part.type_package, part.is_struct, part.is_enum, part.indirections,
                         part.is_array, part.array_len, part.optional, {}, part.const_pointee);
        out += " fnptr";
        append_many_results(out, part.results);
        out += ')';
        return;
    }
    if (results.size() < 2) {
        return;
    }
    out += " (";
    for (std::size_t i = 0; i < results.size(); ++i) {
        if (i != 0) {
            out += ", ";
        }
        const FnParam &part = results[i];
        if (part.is_tuple) {
            out += '(';
            for (std::size_t n = 0; n < part.results.size(); ++n) {
                if (n != 0) {
                    out += ", ";
                }
                const FnParam &inner = part.results[n];
                out += type_text(inner.type_name, inner.type_package, inner.is_struct, inner.is_enum, inner.indirections,
                                 inner.is_array, inner.array_len, inner.optional, {}, inner.const_pointee);
            }
            out += ')';
            continue;
        }
        out += type_text(part.type_name, part.type_package, part.is_struct, part.is_enum, part.indirections,
                         part.is_array, part.array_len, part.optional, {}, part.const_pointee);
    }
    out += ')';
}

std::string quote_string(const std::string &text) {
    std::string out = "\"";
    for (unsigned char c : text) {
        if (c == '\\' || c == '"') {
            out.push_back('\\');
            out.push_back(static_cast<char>(c));
        } else if (c == '\n') {
            out += "\\n";
        } else if (c >= 32 && c < 127) {
            out.push_back(static_cast<char>(c));
        } else {
            constexpr char hex[] = "0123456789abcdef";
            out += "\\x";
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
    }
    out.push_back('"');
    return out;
}

void dump_var(const VarDecl &var, std::string &out, int indent) {
    append_indent(out, indent);
    out += "(var ";
    if (!var.owner.empty()) {
        out += var.owner;
        if (!var.owner_params.empty()) {
            out += '[';
            for (std::size_t i = 0; i < var.owner_params.size(); ++i) {
                if (i != 0) {
                    out += ", ";
                }
                out += var.owner_params[i];
            }
            out += ']';
        }
        out += "::";
    }
    out += var.name;
    out += ' ';
    if (var.inferred) {
        out += ":=";
        if (var.init) {
            out += ' ';
            out += dump_expr(*var.init);
        }
    } else {
        out += type_text(var.type_name, var.type_package, var.is_struct, var.is_enum, var.indirections, var.is_array,
                         var.array_len, var.optional, var.array_len_name, var.const_pointee);
        if (var.ptr_to_array) {
            out += " ptrarr";
        }
        if (var.is_fn_ptr) {
            out += " fnptr";
            append_many_results(out, var.fn_results);
        }
        if (var.init) {
            out += ' ';
            out += dump_expr(*var.init);
        }
    }
    if (var.exported) {
        out += " export";
    }
    if (var.is_static) {
        out += " static";
    }
    append_extern(out, var.is_extern, var.extern_c, var.link_name);
    out += ")\n";
}

void dump_stmt(const Stmt &stmt, std::string &out, int indent) {
    std::visit(
        [&](const auto &node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Stmt::Block>) {
                append_indent(out, indent);
                out += "(block\n";
                for (const auto &child : node.statements) {
                    if (child) {
                        dump_stmt(*child, out, indent + 1);
                    }
                }
                append_indent(out, indent);
                out += ")\n";
            } else if constexpr (std::is_same_v<T, Stmt::Return>) {
                append_indent(out, indent);
                out += "(return";
                if (node.value) {
                    out += ' ';
                    out += dump_expr(*node.value);
                }
                for (const auto &extra : node.extra) {
                    if (extra) {
                        out += ' ';
                        out += dump_expr(*extra);
                    }
                }
                out += ")\n";
            } else if constexpr (std::is_same_v<T, Stmt::Expression>) {
                append_indent(out, indent);
                if (!node.expr) {
                    out += "(empty)\n";
                } else {
                    out += "(expr ";
                    out += dump_expr(*node.expr);
                    out += ")\n";
                }
            } else if constexpr (std::is_same_v<T, Stmt::If>) {
                append_indent(out, indent);
                out += "(if ";
                out += node.cond ? dump_expr(*node.cond) : "(unsupported)";
                out += "\n";
                if (node.then_arm) {
                    dump_stmt(*node.then_arm, out, indent + 1);
                }
                if (node.else_arm) {
                    dump_stmt(*node.else_arm, out, indent + 1);
                }
                append_indent(out, indent);
                out += ")\n";
            } else if constexpr (std::is_same_v<T, Stmt::While>) {
                append_indent(out, indent);
                out += "(while ";
                out += node.cond ? dump_expr(*node.cond) : "(unsupported)";
                out += "\n";
                if (node.body) {
                    dump_stmt(*node.body, out, indent + 1);
                }
                append_indent(out, indent);
                out += ")\n";
            } else if constexpr (std::is_same_v<T, Stmt::DoWhile>) {
                append_indent(out, indent);
                out += "(do\n";
                if (node.body) {
                    dump_stmt(*node.body, out, indent + 1);
                }
                append_indent(out, indent + 1);
                out += node.cond ? dump_expr(*node.cond) : "(unsupported)";
                out += "\n";
                append_indent(out, indent);
                out += ")\n";
            } else if constexpr (std::is_same_v<T, Stmt::Switch>) {
                append_indent(out, indent);
                out += "(switch ";
                out += node.cond ? dump_expr(*node.cond) : "(unsupported)";
                out += "\n";
                if (node.body) {
                    dump_stmt(*node.body, out, indent + 1);
                }
                append_indent(out, indent);
                out += ")\n";
            } else if constexpr (std::is_same_v<T, Stmt::Case>) {
                append_indent(out, indent);
                out += node.is_default ? "(default\n" : "(case ";
                if (!node.is_default) {
                    out += node.value ? dump_expr(*node.value) : "(unsupported)";
                    out += "\n";
                }
                if (node.stmt) {
                    dump_stmt(*node.stmt, out, indent + 1);
                }
                append_indent(out, indent);
                out += ")\n";
            } else if constexpr (std::is_same_v<T, Stmt::For>) {
                append_indent(out, indent);
                out += "(for\n";
                if (node.init) {
                    dump_stmt(*node.init, out, indent + 1);
                } else {
                    append_indent(out, indent + 1);
                    out += "(empty)\n";
                }
                append_indent(out, indent + 1);
                out += node.cond ? dump_expr(*node.cond) : "(empty)";
                out += "\n";
                append_indent(out, indent + 1);
                out += node.step ? dump_expr(*node.step) : "(empty)";
                out += "\n";
                if (node.body) {
                    dump_stmt(*node.body, out, indent + 1);
                }
                append_indent(out, indent);
                out += ")\n";
            } else if constexpr (std::is_same_v<T, Stmt::Break>) {
                append_indent(out, indent);
                out += "(break)\n";
            } else if constexpr (std::is_same_v<T, Stmt::Continue>) {
                append_indent(out, indent);
                out += "(continue)\n";
            } else if constexpr (std::is_same_v<T, Stmt::Decl>) {
                append_indent(out, indent);
                out += "(decl\n";
                for (const VarDecl &var : node.vars) {
                    dump_var(var, out, indent + 1);
                }
                append_indent(out, indent);
                out += ")\n";
            } else if constexpr (std::is_same_v<T, Stmt::Func>) {
                if (node.decl) {
                    dump_function(*node.decl, out, indent);
                }
            } else if constexpr (std::is_same_v<T, Stmt::Defer>) {
                append_indent(out, indent);
                out += "(defer ";
                out += node.call ? dump_expr(*node.call) : "(unsupported)";
                out += ")\n";
            } else if constexpr (std::is_same_v<T, Stmt::Assert>) {
                append_indent(out, indent);
                out += "(assert ";
                out += node.cond ? dump_expr(*node.cond) : "(unsupported)";
                out += ' ';
                out += quote_string(node.message);
                out += ")\n";
            } else if constexpr (std::is_same_v<T, Stmt::Bind>) {
                append_indent(out, indent);
                out += "(bind ";
                out += node.is_const ? "const" : (node.is_var ? "var" : (node.is_define ? "def" : "set"));
                for (const std::string &name : node.names) {
                    out += ' ';
                    out += name;
                }
                out += ' ';
                out += node.value ? dump_expr(*node.value) : "(unsupported)";
                out += ")\n";
            }
        },
        stmt.data);
}

void dump_function(const FunctionDecl &fn, std::string &out, int indent) {
    append_indent(out, indent);
    const bool anon = fn.name.empty() && fn.method_struct.empty();
    if (anon && fn.inferred) {
        out += "(anon inferred (params";
        for (const Param &param : fn.params) {
            out += " (";
            out += param.name.empty() ? "_" : param.name;
            out += ')';
        }
        out += ")\n";
        if (fn.body) {
            dump_stmt(*fn.body, out, indent + 1);
        }
        append_indent(out, indent);
        out += ")\n";
        return;
    }
    if (anon) {
        out += "(anon (params";
    } else {
        out += "(fn ";
        if (!fn.method_struct.empty()) {
            out += fn.method_struct;
            if (!fn.method_params.empty()) {
                out += '[';
                for (std::size_t i = 0; i < fn.method_params.size(); ++i) {
                    if (i != 0) {
                        out += ", ";
                    }
                    out += fn.method_params[i];
                }
                out += ']';
            }
            out += "::";
        }
        out += fn.name;
        out += " (params";
    }
    for (const Param &param : fn.params) {
        out += " (";
        out += param.name.empty() ? "_" : param.name;
        out += ' ';
        out += type_text(param.type_name, param.type_package, param.is_struct, param.is_enum, param.indirections,
                         param.is_array, param.array_len, param.optional, {}, param.const_pointee);
        if (param.is_fn_ptr) {
            out += " fnptr";
            append_many_results(out, param.fn_results);
        }
        if (param.is_receiver) {
            out += " receiver";
        }
        out += ')';
    }
    if (fn.variadic) {
        out += " ...";
    }
    out += ") ";
    if (fn.results.size() > 1) {
        out += '(';
        for (std::size_t i = 0; i < fn.results.size(); ++i) {
            if (i != 0) {
                out += ", ";
            }
            const ResultSlot &slot = fn.results[i];
            out += type_text(slot.type_name, slot.type_package, slot.is_struct, slot.is_enum, slot.indirections,
                             slot.is_array, slot.array_len, slot.optional, slot.array_len_name, slot.const_pointee);
            if (slot.is_fn_ptr) {
                out += " fnptr";
                append_many_results(out, slot.fn_results);
            }
        }
        out += ')';
    } else {
        out += type_text(fn.return_type, fn.type_package, fn.is_struct, fn.is_enum, fn.indirections, false, 0,
                         fn.optional, {}, fn.const_pointee);
        if (fn.results.size() == 1 && fn.results.front().is_fn_ptr) {
            out += " fnptr";
            append_many_results(out, fn.results.front().fn_results);
        }
    }
    if (fn.exported) {
        out += " export";
    }
    if (fn.unsafe) {
        out += " unsafe";
    }
    if (fn.is_static) {
        out += " static";
    }
    append_extern(out, fn.is_extern, fn.extern_c, fn.link_name);
    out += "\n";
    if (fn.body) {
        dump_stmt(*fn.body, out, indent + 1);
    }
    append_indent(out, indent);
    out += ")\n";
}

void append_type_args(std::string &text, const std::vector<TypeArg> &args) {
    if (args.empty()) {
        return;
    }
    text += '[';
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i != 0) {
            text += ", ";
        }
        const TypeArg &arg = args[i];
        if (arg.has_value) {
            text += std::to_string(arg.value);
            continue;
        }
        if (!arg.type_package.empty()) {
            text += arg.type_package;
            text += '.';
        }
        text += arg.type_name;
        append_type_args(text, arg.args);
    }
    text += ']';
}

} // namespace

std::string dump_expr(const Expr &expr) {
    return std::visit(
        [](const auto &node) -> std::string {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Expr::Name>) {
                return "(name " + node.text + ")";
            } else if constexpr (std::is_same_v<T, Expr::Int>) {
                std::string text = "(int " + std::to_string(node.value);
                if (node.suffix.bits != 0) {
                    text += node.suffix.is_unsigned ? " u" : " i";
                    text += std::to_string(node.suffix.bits);
                }
                text += ')';
                return text;
            } else if constexpr (std::is_same_v<T, Expr::Unary>) {
                const std::string op = node.postfix ? "!post" : token_kind_name(node.op);
                return "(" + op + " " + (node.expr ? dump_expr(*node.expr) : "(unsupported)") + ")";
            } else if constexpr (std::is_same_v<T, Expr::Binary>) {
                return std::string("(") + token_kind_name(node.op) + " " +
                       (node.lhs ? dump_expr(*node.lhs) : "(unsupported)") + " " +
                       (node.rhs ? dump_expr(*node.rhs) : "(unsupported)") + ")";
            } else if constexpr (std::is_same_v<T, Expr::Assign>) {
                return "(= " + (node.lhs ? dump_expr(*node.lhs) : "(unsupported)") + " " +
                       (node.rhs ? dump_expr(*node.rhs) : "(unsupported)") + ")";
            } else if constexpr (std::is_same_v<T, Expr::Call>) {
                std::string text = "(call ";
                text += node.callee ? dump_expr(*node.callee) : "(unsupported)";
                for (const auto &arg : node.args) {
                    text += ' ';
                    text += arg ? dump_expr(*arg) : "(unsupported)";
                }
                text += ')';
                return text;
            } else if constexpr (std::is_same_v<T, Expr::Selector>) {
                return "(. " + (node.base ? dump_expr(*node.base) : "(unsupported)") + " " + node.field + ")";
            } else if constexpr (std::is_same_v<T, Expr::String>) {
                return "(string " + quote_string(node.text) + ")";
            } else if constexpr (std::is_same_v<T, Expr::Index>) {
                if (node.is_range) {
                    return "([:] " + (node.base ? dump_expr(*node.base) : "(unsupported)") + " " +
                           (node.index ? dump_expr(*node.index) : "_") + " " +
                           (node.end ? dump_expr(*node.end) : "_") + ")";
                }
                return "([] " + (node.base ? dump_expr(*node.base) : "(unsupported)") + " " +
                       (node.index ? dump_expr(*node.index) : "(unsupported)") + ")";
            } else if constexpr (std::is_same_v<T, Expr::Sizeof> || std::is_same_v<T, Expr::Typeof> ||
                                 std::is_same_v<T, Expr::Cast>) {
                std::string spelled;
                if (!node.type_package.empty()) {
                    spelled = node.type_package + "." + node.type_name;
                } else if (node.is_enum) {
                    spelled = node.type_name.empty() ? "enum" : "enum " + node.type_name;
                } else if (node.is_struct) {
                    spelled = "struct " + node.type_name;
                } else {
                    spelled = node.type_name;
                }
                for (int i = 0; i < node.indirections; ++i) {
                    if ((node.optional & (1u << i)) != 0) {
                        spelled += '?';
                    }
                    spelled += '*';
                }
                if constexpr (std::is_same_v<T, Expr::Cast>) {
                    if (node.is_fn_ptr) {
                        spelled += " fnptr";
                        append_many_results(spelled, node.fn_results);
                    }
                }
                if constexpr (std::is_same_v<T, Expr::Sizeof> || std::is_same_v<T, Expr::Typeof>) {
                    const char *op = "typeof";
                    if constexpr (std::is_same_v<T, Expr::Sizeof>) {
                        op = node.align ? "alignof" : "sizeof";
                    }
                    if (!node.on_type) {
                        return std::string("(") + op + " " + (node.expr ? dump_expr(*node.expr) : "(unsupported)") + ")";
                    }
                    return std::string("(") + op + " " + spelled + ")";
                } else {
                    return "(cast " + spelled + " " + (node.expr ? dump_expr(*node.expr) : "(unsupported)") + ")";
                }
            } else if constexpr (std::is_same_v<T, Expr::New>) {
                std::string text = "(new";
                if (node.allocator) {
                    text += " ";
                    text += dump_expr(*node.allocator);
                }
                text += " ";
                if (node.count_inferred) {
                    text += "[_] ";
                } else if (node.count) {
                    text += "[";
                    text += dump_expr(*node.count);
                    text += "] ";
                }
                text += node.type_package.empty() ? node.type_name : node.type_package + "." + node.type_name;
                if (node.init) {
                    text += " ";
                    text += dump_expr(*node.init);
                }
                text += ")";
                return text;
            } else if constexpr (std::is_same_v<T, Expr::Delete>) {
                std::string text = "(delete";
                if (node.allocator) {
                    text += " ";
                    text += dump_expr(*node.allocator);
                }
                text += " ";
                text += node.expr ? dump_expr(*node.expr) : "(unsupported)";
                text += ")";
                return text;
            } else if constexpr (std::is_same_v<T, Expr::Bool>) {
                return node.value ? "(true)" : "(false)";
            } else if constexpr (std::is_same_v<T, Expr::Nullptr>) {
                return "(null)";
            } else if constexpr (std::is_same_v<T, Expr::Char>) {
                return "(char " + std::to_string(node.value) + ")";
            } else if constexpr (std::is_same_v<T, Expr::Float>) {
                return std::string(node.is_f32 ? "(float " : "(double ") + node.text + ")";
            } else if constexpr (std::is_same_v<T, Expr::Func>) {
                if (!node.decl) {
                    return "(anon)";
                }
                std::string text;
                dump_function(*node.decl, text, 0);
                return text;
            } else if constexpr (std::is_same_v<T, Expr::Init>) {
                std::string text = node.is_tuple ? "(tuple" : node.array_literal ? "(array" : "(init";
                if (!node.type_name.empty() || node.array_literal) {
                    text += ' ';
                    for (int star = 0; star < node.indirections; ++star) {
                        if ((node.optional & (1u << star)) != 0) {
                            text += '?';
                        }
                        text += '*';
                        if ((node.const_pointee & (1u << star)) != 0) {
                            text += "const ";
                        }
                    }
                    if (!node.type_package.empty()) {
                        text += node.type_package;
                        text += '.';
                    }
                    text += node.type_name;
                    append_type_args(text, node.type_args);
                }
                for (const Expr::FieldInit &field : node.fields) {
                    text += " (.";
                    text += field.field;
                    text += ' ';
                    text += field.value ? dump_expr(*field.value) : "(unsupported)";
                    text += ')';
                }
                for (const auto &element : node.elements) {
                    text += ' ';
                    text += element ? dump_expr(*element) : "(unsupported)";
                }
                text += ')';
                return text;
            } else {
                return "(unsupported)";
            }
        },
        expr.data);
}

std::string dump_translation_unit(const TranslationUnit &unit) {
    std::string out = "(tu\n";
    if (unit.package) {
        out += "  (package ";
        out += unit.package->name;
        out += ")\n";
    }
    for (const ImportDecl &import : unit.imports) {
        out += "  ";
        if (import.form == ImportDecl::Form::Alias) {
            out += "(import-as \"";
            out += import.path;
            out += "\" ";
            out += import.alias;
            out += ")\n";
        } else if (import.form == ImportDecl::Form::Selective) {
            out += "(import-from \"";
            out += import.path;
            out += "\"";
            for (const std::string &name : import.names) {
                out += ' ';
                out += name;
            }
            out += ")\n";
        } else {
            out += "(import \"";
            out += import.path;
            out += "\")\n";
        }
    }
    for (const TopDecl &decl : unit.decls) {
        if (const auto *fn = std::get_if<FunctionDecl>(&decl.data)) {
            dump_function(*fn, out, 1);
        } else if (const auto *var = std::get_if<VarDecl>(&decl.data)) {
            dump_var(*var, out, 1);
        } else if (const auto *item = std::get_if<StructDecl>(&decl.data)) {
            if (item->anonymous) {
                continue;
            }
            append_indent(out, 1);
            out += item->is_union ? "(union " : "(struct ";
            out += item->name;
            if (!item->tag_name.empty()) {
                out += ' ';
                if (!item->tag_package.empty()) {
                    out += item->tag_package;
                    out += '.';
                }
                out += item->tag_name;
            }
            if (item->align > 0) {
                out += " align ";
                out += std::to_string(item->align);
            }
            if (item->exported) {
                out += " export";
            }
            append_extern(out, item->is_extern, item->extern_c);
            out += "\n";
            for (const FieldDecl &field : item->fields) {
                dump_field(field, out, 2);
            }
            append_indent(out, 1);
            out += ")\n";
        } else if (const auto *item = std::get_if<EnumDecl>(&decl.data)) {
            append_indent(out, 1);
            out += "(enum";
            if (!item->name.empty()) {
                out += ' ';
                out += item->name;
            }
            if (!item->underlying.empty()) {
                out += ' ';
                out += item->underlying;
            }
            if (item->exported) {
                out += " export";
            }
            append_extern(out, item->is_extern, item->extern_c);
            out += "\n";
            for (const Enumerator &enumerator : item->enumerators) {
                append_indent(out, 2);
                out += "(enumerator ";
                out += enumerator.name;
                if (enumerator.has_value) {
                    out += ' ';
                    out += std::to_string(enumerator.value);
                }
                out += ")\n";
            }
            append_indent(out, 1);
            out += ")\n";
        } else if (const auto *item = std::get_if<TypedefDecl>(&decl.data)) {
            append_indent(out, 1);
            out += "(type ";
            out += item->name;
            out += ' ';
            out += type_text(item->type_name, item->type_package, item->is_struct, item->is_enum, item->indirections,
                             item->is_array, item->array_len, item->optional, {}, item->const_pointee);
            if (item->is_fn_ptr) {
                out += " fnptr";
                append_many_results(out, item->fn_results);
            }
            if (item->exported) {
                out += " export";
            }
            out += ")\n";
        } else if (const auto *item = std::get_if<TestDecl>(&decl.data)) {
            append_indent(out, 1);
            out += "(test ";
            out += quote_string(item->name);
            out += "\n";
            if (item->body) {
                dump_stmt(*item->body, out, 2);
            }
            append_indent(out, 1);
            out += ")\n";
        } else if (const auto *item = std::get_if<AssertDecl>(&decl.data)) {
            append_indent(out, 1);
            out += "(assert ";
            out += item->cond ? dump_expr(*item->cond) : "(unsupported)";
            out += ' ';
            out += quote_string(item->message);
            out += ")\n";
        } else if (const auto *item = std::get_if<TraitDecl>(&decl.data)) {
            append_indent(out, 1);
            out += "(trait ";
            out += item->name;
            out += "\n";
            if (!item->parent_name.empty()) {
                append_indent(out, 2);
                out += "(parent ";
                if (!item->parent_package.empty()) {
                    out += item->parent_package;
                    out += '.';
                }
                out += item->parent_name;
                out += ")\n";
            }
            for (const FieldDecl &field : item->fields) {
                dump_field(field, out, 2);
            }
            for (const FunctionDecl &method : item->methods) {
                dump_function(method, out, 2);
            }
            append_indent(out, 1);
            out += ")\n";
        }
    }
    out += ")\n";
    return out;
}

VarDecl clone_var(const VarDecl &var) {
    VarDecl copy;
    copy.loc = var.loc;
    copy.name = var.name;
    copy.type_name = var.type_name;
    copy.type_package = var.type_package;
    copy.is_struct = var.is_struct;
    copy.is_enum = var.is_enum;
    copy.indirections = var.indirections;
    copy.optional = var.optional;
    copy.const_pointee = var.const_pointee;
    copy.is_array = var.is_array;
    copy.elem_const = var.elem_const;
    copy.elem_slice = var.elem_slice;
    copy.shells = var.shells;
    copy.array_len = var.array_len;
    copy.array_len_name = var.array_len_name;
    copy.type_args = var.type_args;
    copy.ptr_to_array = var.ptr_to_array;
    copy.is_fn_ptr = var.is_fn_ptr;
    copy.ret_indirections = var.ret_indirections;
    copy.ret_optional = var.ret_optional;
    copy.ret_const_pointee = var.ret_const_pointee;
    copy.fn_params = var.fn_params;
    copy.fn_variadic = var.fn_variadic;
    copy.fn_results = var.fn_results;
    copy.init = var.init ? clone_expr(*var.init) : nullptr;
    copy.exported = var.exported;
    copy.is_static = var.is_static;
    copy.is_extern = var.is_extern;
    copy.extern_c = var.extern_c;
    copy.link_name = var.link_name;
    copy.inferred = var.inferred;
    copy.is_const_decl = var.is_const_decl;
    copy.has_const = var.has_const;
    copy.const_value = var.const_value;
    copy.runtime_init = var.runtime_init;
    copy.owner = var.owner;
    copy.owner_params = var.owner_params;
    return copy;
}

std::unique_ptr<Expr> clone_expr(const Expr &expr) {
    auto out = std::make_unique<Expr>();
    out->grouped = expr.grouped;
    std::visit(
        [&](const auto &node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Expr::Unary>) {
                Expr::Unary copy;
                copy.loc = node.loc;
                copy.op = node.op;
                copy.postfix = node.postfix;
                copy.expr = node.expr ? clone_expr(*node.expr) : nullptr;
                out->data = std::move(copy);
            } else if constexpr (std::is_same_v<T, Expr::Binary>) {
                Expr::Binary copy;
                copy.loc = node.loc;
                copy.op = node.op;
                copy.lhs = node.lhs ? clone_expr(*node.lhs) : nullptr;
                copy.rhs = node.rhs ? clone_expr(*node.rhs) : nullptr;
                out->data = std::move(copy);
            } else if constexpr (std::is_same_v<T, Expr::Assign>) {
                Expr::Assign copy;
                copy.loc = node.loc;
                copy.lhs = node.lhs ? clone_expr(*node.lhs) : nullptr;
                copy.rhs = node.rhs ? clone_expr(*node.rhs) : nullptr;
                out->data = std::move(copy);
            } else if constexpr (std::is_same_v<T, Expr::Call>) {
                Expr::Call copy;
                copy.loc = node.loc;
                copy.callee = node.callee ? clone_expr(*node.callee) : nullptr;
                copy.type_args = node.type_args;
                copy.type_name = node.type_name;
                copy.type_fields = node.type_fields;
                copy.trait_method = node.trait_method;
                for (const auto &arg : node.args) {
                    copy.args.push_back(arg ? clone_expr(*arg) : nullptr);
                }
                out->data = std::move(copy);
            } else if constexpr (std::is_same_v<T, Expr::Selector>) {
                Expr::Selector copy;
                copy.loc = node.loc;
                copy.base = node.base ? clone_expr(*node.base) : nullptr;
                copy.field = node.field;
                copy.type_args = node.type_args;
                copy.resolved_owner = node.resolved_owner;
                copy.resolved_file = node.resolved_file;
                copy.slice_len = node.slice_len;
                copy.len_value = node.len_value;
                out->data = std::move(copy);
            } else if constexpr (std::is_same_v<T, Expr::Index>) {
                Expr::Index copy;
                copy.loc = node.loc;
                copy.tuple_index = node.tuple_index;
                copy.slice_element = node.slice_element;
                copy.is_range = node.is_range;
                copy.base = node.base ? clone_expr(*node.base) : nullptr;
                copy.index = node.index ? clone_expr(*node.index) : nullptr;
                copy.end = node.end ? clone_expr(*node.end) : nullptr;
                out->data = std::move(copy);
            } else if constexpr (std::is_same_v<T, Expr::Sizeof>) {
                Expr::Sizeof copy;
                copy.loc = node.loc;
                copy.on_type = node.on_type;
                copy.type_name = node.type_name;
                copy.type_package = node.type_package;
                copy.is_struct = node.is_struct;
                copy.is_enum = node.is_enum;
                copy.indirections = node.indirections;
                copy.optional = node.optional;
                copy.const_pointee = node.const_pointee;
                copy.is_array = node.is_array;
                copy.elem_const = node.elem_const;
                copy.elem_slice = node.elem_slice;
                copy.shells = node.shells;
                copy.array_len = node.array_len;
                copy.expr = node.expr ? clone_expr(*node.expr) : nullptr;
                copy.align = node.align;
                out->data = std::move(copy);
            } else if constexpr (std::is_same_v<T, Expr::Typeof>) {
                Expr::Typeof copy;
                copy.loc = node.loc;
                copy.on_type = node.on_type;
                copy.type_name = node.type_name;
                copy.type_package = node.type_package;
                copy.is_struct = node.is_struct;
                copy.is_enum = node.is_enum;
                copy.indirections = node.indirections;
                copy.optional = node.optional;
                copy.const_pointee = node.const_pointee;
                copy.is_array = node.is_array;
                copy.elem_const = node.elem_const;
                copy.elem_slice = node.elem_slice;
                copy.shells = node.shells;
                copy.array_len = node.array_len;
                copy.canonical = node.canonical;
                copy.expr = node.expr ? clone_expr(*node.expr) : nullptr;
                out->data = std::move(copy);
            } else if constexpr (std::is_same_v<T, Expr::Cast>) {
                Expr::Cast copy;
                copy.loc = node.loc;
                copy.type_name = node.type_name;
                copy.type_package = node.type_package;
                copy.is_struct = node.is_struct;
                copy.is_enum = node.is_enum;
                copy.indirections = node.indirections;
                copy.optional = node.optional;
                copy.const_pointee = node.const_pointee;
                copy.is_fn_ptr = node.is_fn_ptr;
                copy.ret_indirections = node.ret_indirections;
                copy.ret_optional = node.ret_optional;
                copy.ret_const_pointee = node.ret_const_pointee;
                copy.fn_params = node.fn_params;
                copy.fn_variadic = node.fn_variadic;
                copy.fn_results = node.fn_results;
                copy.expr = node.expr ? clone_expr(*node.expr) : nullptr;
                out->data = std::move(copy);
            } else if constexpr (std::is_same_v<T, Expr::Func>) {
                Expr::Func copy;
                copy.loc = node.loc;
                if (node.decl) {
                    copy.decl = std::make_unique<FunctionDecl>(clone_function(*node.decl));
                }
                out->data = std::move(copy);
            } else if constexpr (std::is_same_v<T, Expr::Init>) {
                Expr::Init copy;
                copy.loc = node.loc;
                copy.is_tuple = node.is_tuple;
                copy.explicit_one = node.explicit_one;
                copy.zero_rest = node.zero_rest;
                copy.struct_id = node.struct_id;
                copy.type_name = node.type_name;
                copy.type_package = node.type_package;
                copy.array_literal = node.array_literal;
                copy.elem_is_array = node.elem_is_array;
                copy.elem_ptr_to_array = node.elem_ptr_to_array;
                copy.elem_fn = node.elem_fn;
                copy.elem_array_len = node.elem_array_len;
                copy.elem_array_len_name = node.elem_array_len_name;
                copy.elem_slice = node.elem_slice;
                copy.elem_shells = node.elem_shells;
                copy.elem_const = node.elem_const;
                copy.elem_ret_indirections = node.elem_ret_indirections;
                copy.elem_ret_optional = node.elem_ret_optional;
                copy.elem_ret_const_pointee = node.elem_ret_const_pointee;
                copy.elem_fn_params = node.elem_fn_params;
                copy.elem_fn_variadic = node.elem_fn_variadic;
                copy.elem_fn_results = node.elem_fn_results;
                copy.is_struct = node.is_struct;
                copy.is_enum = node.is_enum;
                copy.indirections = node.indirections;
                copy.optional = node.optional;
                copy.const_pointee = node.const_pointee;
                copy.type_args = node.type_args;
                for (const Expr::FieldInit &field : node.fields) {
                    Expr::FieldInit cloned;
                    cloned.loc = field.loc;
                    cloned.field = field.field;
                    cloned.value = field.value ? clone_expr(*field.value) : nullptr;
                    copy.fields.push_back(std::move(cloned));
                }
                for (const auto &element : node.elements) {
                    copy.elements.push_back(element ? clone_expr(*element) : nullptr);
                }
                out->data = std::move(copy);
            } else if constexpr (std::is_same_v<T, Expr::New>) {
                Expr::New copy;
                copy.loc = node.loc;
                copy.allocator = node.allocator ? clone_expr(*node.allocator) : nullptr;
                copy.type_name = node.type_name;
                copy.type_package = node.type_package;
                copy.is_struct = node.is_struct;
                copy.indirections = node.indirections;
                copy.optional = node.optional;
                copy.const_pointee = node.const_pointee;
                copy.type_args = node.type_args;
                copy.count = node.count ? clone_expr(*node.count) : nullptr;
                copy.count_inferred = node.count_inferred;
                copy.repeat_init = node.repeat_init;
                copy.init = node.init ? clone_expr(*node.init) : nullptr;
                copy.builtin_alloc = node.builtin_alloc;
                copy.alloc_file = node.alloc_file;
                copy.alloc_name = node.alloc_name;
                copy.alloc_is_pointer = node.alloc_is_pointer;
                out->data = std::move(copy);
            } else if constexpr (std::is_same_v<T, Expr::Delete>) {
                Expr::Delete copy;
                copy.loc = node.loc;
                copy.allocator = node.allocator ? clone_expr(*node.allocator) : nullptr;
                copy.expr = node.expr ? clone_expr(*node.expr) : nullptr;
                copy.builtin_alloc = node.builtin_alloc;
                copy.alloc_file = node.alloc_file;
                copy.alloc_name = node.alloc_name;
                copy.alloc_is_pointer = node.alloc_is_pointer;
                out->data = std::move(copy);
            } else {
                out->data = node;
            }
        },
        expr.data);
    return out;
}

std::unique_ptr<Stmt> clone_stmt(const Stmt &stmt) {
    auto out = std::make_unique<Stmt>();
    std::visit(
        [&](const auto &node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Stmt::Block>) {
                Stmt::Block copy;
                copy.loc = node.loc;
                for (const auto &child : node.statements) {
                    copy.statements.push_back(child ? clone_stmt(*child) : nullptr);
                }
                out->data = std::move(copy);
            } else if constexpr (std::is_same_v<T, Stmt::Return>) {
                Stmt::Return copy;
                copy.loc = node.loc;
                copy.value = node.value ? clone_expr(*node.value) : nullptr;
                for (const auto &extra : node.extra) {
                    copy.extra.push_back(extra ? clone_expr(*extra) : nullptr);
                }
                out->data = std::move(copy);
            } else if constexpr (std::is_same_v<T, Stmt::Expression>) {
                Stmt::Expression copy;
                copy.loc = node.loc;
                copy.expr = node.expr ? clone_expr(*node.expr) : nullptr;
                copy.emit = node.emit;
                out->data = std::move(copy);
            } else if constexpr (std::is_same_v<T, Stmt::If>) {
                Stmt::If copy;
                copy.loc = node.loc;
                copy.cond = node.cond ? clone_expr(*node.cond) : nullptr;
                copy.then_arm = node.then_arm ? clone_stmt(*node.then_arm) : nullptr;
                copy.else_arm = node.else_arm ? clone_stmt(*node.else_arm) : nullptr;
                copy.emit = node.emit;
                copy.comptime_if = node.comptime_if;
                copy.folded = node.folded;
                copy.taken = node.taken;
                out->data = std::move(copy);
            } else if constexpr (std::is_same_v<T, Stmt::While>) {
                Stmt::While copy;
                copy.loc = node.loc;
                copy.cond = node.cond ? clone_expr(*node.cond) : nullptr;
                copy.body = node.body ? clone_stmt(*node.body) : nullptr;
                copy.emit = node.emit;
                out->data = std::move(copy);
            } else if constexpr (std::is_same_v<T, Stmt::For>) {
                Stmt::For copy;
                copy.loc = node.loc;
                copy.init = node.init ? clone_stmt(*node.init) : nullptr;
                copy.cond = node.cond ? clone_expr(*node.cond) : nullptr;
                copy.step = node.step ? clone_expr(*node.step) : nullptr;
                copy.body = node.body ? clone_stmt(*node.body) : nullptr;
                copy.each = node.each;
                copy.each_const = node.each_const;
                copy.each_name = node.each_name;
                copy.each_name_loc = node.each_name_loc;
                copy.each_expr = node.each_expr ? clone_expr(*node.each_expr) : nullptr;
                copy.emit = node.emit;
                out->data = std::move(copy);
            } else if constexpr (std::is_same_v<T, Stmt::DoWhile>) {
                Stmt::DoWhile copy;
                copy.loc = node.loc;
                copy.body = node.body ? clone_stmt(*node.body) : nullptr;
                copy.cond = node.cond ? clone_expr(*node.cond) : nullptr;
                copy.emit = node.emit;
                out->data = std::move(copy);
            } else if constexpr (std::is_same_v<T, Stmt::Switch>) {
                Stmt::Switch copy;
                copy.loc = node.loc;
                copy.cond = node.cond ? clone_expr(*node.cond) : nullptr;
                copy.body = node.body ? clone_stmt(*node.body) : nullptr;
                copy.comptime_switch = node.comptime_switch;
                copy.folded = node.folded;
                copy.folded_value = node.folded_value;
                out->data = std::move(copy);
            } else if constexpr (std::is_same_v<T, Stmt::Case>) {
                Stmt::Case copy;
                copy.loc = node.loc;
                copy.value = node.value ? clone_expr(*node.value) : nullptr;
                copy.stmt = node.stmt ? clone_stmt(*node.stmt) : nullptr;
                copy.is_default = node.is_default;
                out->data = std::move(copy);
            } else if constexpr (std::is_same_v<T, Stmt::Decl>) {
                Stmt::Decl copy;
                copy.loc = node.loc;
                for (const VarDecl &var : node.vars) {
                    copy.vars.push_back(clone_var(var));
                }
                out->data = std::move(copy);
            } else if constexpr (std::is_same_v<T, Stmt::Func>) {
                Stmt::Func copy;
                if (node.decl) {
                    copy.decl = std::make_unique<FunctionDecl>(clone_function(*node.decl));
                }
                out->data = std::move(copy);
            } else if constexpr (std::is_same_v<T, Stmt::Defer>) {
                Stmt::Defer copy;
                copy.loc = node.loc;
                copy.call = node.call ? clone_expr(*node.call) : nullptr;
                out->data = std::move(copy);
            } else if constexpr (std::is_same_v<T, Stmt::Bind>) {
                Stmt::Bind copy;
                copy.loc = node.loc;
                copy.names = node.names;
                copy.name_locs = node.name_locs;
                copy.value = node.value ? clone_expr(*node.value) : nullptr;
                copy.is_var = node.is_var;
                copy.is_define = node.is_define;
                copy.is_const = node.is_const;
                out->data = std::move(copy);
            } else if constexpr (std::is_same_v<T, Stmt::Assert>) {
                Stmt::Assert copy;
                copy.loc = node.loc;
                copy.cond = node.cond ? clone_expr(*node.cond) : nullptr;
                copy.message = node.message;
                out->data = std::move(copy);
            } else {
                out->data = node;
            }
        },
        stmt.data);
    return out;
}

FunctionDecl clone_function(const FunctionDecl &fn) {
    FunctionDecl copy;
    copy.loc = fn.loc;
    copy.name = fn.name;
    copy.return_type = fn.return_type;
    copy.type_package = fn.type_package;
    copy.is_struct = fn.is_struct;
    copy.is_enum = fn.is_enum;
    copy.indirections = fn.indirections;
    copy.optional = fn.optional;
    copy.const_pointee = fn.const_pointee;
    copy.results = fn.results;
    copy.params = fn.params;
    copy.body = fn.body ? clone_stmt(*fn.body) : nullptr;
    copy.exported = fn.exported;
    copy.is_comptime = fn.is_comptime;
    copy.is_macro = fn.is_macro;
    copy.unsafe = fn.unsafe;
    copy.is_static = fn.is_static;
    copy.is_extern = fn.is_extern;
    copy.extern_c = fn.extern_c;
    copy.link_name = fn.link_name;
    copy.variadic = fn.variadic;
    copy.method_struct = fn.method_struct;
    copy.static_method = fn.static_method;
    copy.method_params = fn.method_params;
    copy.type_params = fn.type_params;
    copy.infer_result = fn.infer_result;
    for (const auto &expr : fn.result_typeof) {
        copy.result_typeof.push_back(expr ? clone_expr(*expr) : nullptr);
    }
    copy.inferred = fn.inferred;
    copy.elaborated = fn.elaborated;
    copy.function_id = fn.function_id;
    copy.captures = fn.captures;
    copy.captured_here = fn.captured_here;
    copy.iteration_kind = fn.iteration_kind;
    copy.generic_instance = fn.generic_instance;
    return copy;
}

StructDecl clone_struct(const StructDecl &item) { return item; }

void replace_names(std::unique_ptr<Expr> &slot, const std::vector<std::string> &names,
                   const std::vector<const Expr *> &exprs) {
    if (slot == nullptr || names.empty()) {
        return;
    }
    if (const auto *named = std::get_if<Expr::Name>(&slot->data)) {
        for (std::size_t i = 0; i < names.size() && i < exprs.size(); ++i) {
            if (!names[i].empty() && names[i] == named->text && exprs[i] != nullptr) {
                slot = clone_expr(*exprs[i]);
                return;
            }
        }
        return;
    }
    // A nested function has its own parameters.
    if (std::holds_alternative<Expr::Func>(slot->data)) {
        return;
    }
    std::visit(
        [&](auto &node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Expr::Unary> || std::is_same_v<T, Expr::Sizeof> ||
                          std::is_same_v<T, Expr::Typeof> || std::is_same_v<T, Expr::Cast>) {
                replace_names(node.expr, names, exprs);
            } else if constexpr (std::is_same_v<T, Expr::Binary> || std::is_same_v<T, Expr::Assign>) {
                replace_names(node.lhs, names, exprs);
                replace_names(node.rhs, names, exprs);
            } else if constexpr (std::is_same_v<T, Expr::Call>) {
                replace_names(node.callee, names, exprs);
                for (auto &arg : node.args) {
                    replace_names(arg, names, exprs);
                }
            } else if constexpr (std::is_same_v<T, Expr::Selector>) {
                replace_names(node.base, names, exprs);
            } else if constexpr (std::is_same_v<T, Expr::Index>) {
                replace_names(node.base, names, exprs);
                replace_names(node.index, names, exprs);
                replace_names(node.end, names, exprs);
            } else if constexpr (std::is_same_v<T, Expr::Init>) {
                for (auto &field : node.fields) {
                    replace_names(field.value, names, exprs);
                }
                for (auto &element : node.elements) {
                    replace_names(element, names, exprs);
                }
            } else if constexpr (std::is_same_v<T, Expr::New>) {
                replace_names(node.allocator, names, exprs);
                replace_names(node.count, names, exprs);
                replace_names(node.init, names, exprs);
            } else if constexpr (std::is_same_v<T, Expr::Delete>) {
                replace_names(node.allocator, names, exprs);
                replace_names(node.expr, names, exprs);
            }
        },
        slot->data);
}

TypeArg::TypeArg(const TypeArg &other)
    : loc(other.loc), is_value(other.is_value), has_value(other.has_value), value(other.value),
      type_name(other.type_name), type_package(other.type_package), is_struct(other.is_struct),
      is_enum(other.is_enum), indirections(other.indirections), optional(other.optional),
      const_pointee(other.const_pointee), is_array(other.is_array), elem_const(other.elem_const),
      array_len(other.array_len), array_len_name(other.array_len_name), shells(other.shells), args(other.args) {
    if (other.value_expr != nullptr) {
        value_expr = std::shared_ptr<Expr>(clone_expr(*other.value_expr));
    }
}

TypeArg &TypeArg::operator=(const TypeArg &other) {
    if (this == &other) {
        return *this;
    }
    TypeArg copy(other);
    *this = std::move(copy);
    return *this;
}

void append_value_expr_key(std::string &out, const Expr &expr) {
    const auto *sized = std::get_if<Expr::Sizeof>(&expr.data);
    if (sized == nullptr || !sized->on_type) {
        out += dump_expr(expr);
        return;
    }
    out += "sizeof(";
    if (sized->is_array) {
        out.push_back('[');
        if (!sized->elem_slice && sized->array_len >= 0) {
            out += std::to_string(sized->array_len);
        }
        out.push_back(']');
        if (sized->elem_const) {
            out += "const.";
        }
    }
    for (const TypeShell &shell : sized->shells) {
        if (shell.kind == TypeShell::Kind::Slice) {
            out += "[]";
            continue;
        }
        if (shell.kind == TypeShell::Kind::Array) {
            out.push_back('[');
            if (!shell.length_name.empty()) {
                out += shell.length_name;
            } else {
                out += std::to_string(shell.length);
            }
            out.push_back(']');
            continue;
        }
        if (shell.optional) {
            out.push_back('?');
        }
        out.push_back('*');
        if (shell.const_pointee) {
            out += "const";
        }
    }
    for (int bit = sized->indirections - 1; bit >= 0; --bit) {
        const uint32_t mask = 1u << static_cast<unsigned>(bit);
        if ((sized->optional & mask) != 0) {
            out.push_back('?');
        }
        out.push_back('*');
        if ((sized->const_pointee & mask) != 0) {
            out += "const";
        }
    }
    if (!sized->type_package.empty()) {
        out += sized->type_package;
        out.push_back('.');
    }
    out += sized->type_name;
    out.push_back(')');
}

} // namespace tlc

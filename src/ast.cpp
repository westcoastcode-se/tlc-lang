#include "ast.hpp"

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

SourceLoc expr_loc(const Expr &expr) {
    return std::visit([](const auto &node) { return node.loc; }, expr.data);
}

namespace {

void append_indent(std::string &out, int indent) {
    out.append(static_cast<std::size_t>(indent) * 2, ' ');
}

void dump_stmt(const Stmt &stmt, std::string &out, int indent);
void dump_function(const FunctionDecl &fn, std::string &out, int indent);

std::string type_text(const std::string &name, const std::string &package, bool is_struct, bool is_enum,
                       int indirections, bool is_array, int array_len) {
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
    if (indirections > 0) {
        text.append(static_cast<std::size_t>(indirections), '*');
    }
    if (is_array) {
        text += '[';
        if (array_len >= 0) {
            text += std::to_string(array_len);
        }
        text += ']';
    }
    return text;
}

void append_many_results(std::string &out, const std::vector<FnParam> &results) {
    if (results.size() < 2) {
        return;
    }
    out += " (";
    for (std::size_t i = 0; i < results.size(); ++i) {
        if (i != 0) {
            out += ", ";
        }
        const FnParam &part = results[i];
        out += type_text(part.type_name, part.type_package, part.is_struct, part.is_enum, part.indirections, false, 0);
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
                         var.array_len);
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
    if (var.is_extern) {
        out += " extern";
    }
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
            } else if constexpr (std::is_same_v<T, Stmt::Bind>) {
                append_indent(out, indent);
                out += "(bind ";
                out += node.is_var ? "var" : (node.is_define ? "def" : "set");
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
    if (anon) {
        out += "(anon (params";
    } else {
        out += "(fn ";
        if (!fn.method_struct.empty()) {
            out += fn.method_struct;
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
                         param.is_array, param.array_len);
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
            out += type_text(slot.type_name, slot.type_package, slot.is_struct, slot.is_enum, slot.indirections, false, 0);
            if (slot.is_fn_ptr) {
                out += " fnptr";
                append_many_results(out, slot.fn_results);
            }
        }
        out += ')';
    } else {
        out += type_text(fn.return_type, fn.type_package, fn.is_struct, fn.is_enum, fn.indirections, false, 0);
        if (fn.results.size() == 1 && fn.results.front().is_fn_ptr) {
            out += " fnptr";
            append_many_results(out, fn.results.front().fn_results);
        }
    }
    if (fn.exported) {
        out += " export";
    }
    if (fn.is_static) {
        out += " static";
    }
    if (fn.is_extern) {
        out += " extern";
    }
    out += "\n";
    if (fn.body) {
        dump_stmt(*fn.body, out, indent + 1);
    }
    append_indent(out, indent);
    out += ")\n";
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
                return std::string("(") + token_kind_name(node.op) + " " +
                       (node.expr ? dump_expr(*node.expr) : "(unsupported)") + ")";
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
                return "([] " + (node.base ? dump_expr(*node.base) : "(unsupported)") + " " +
                       (node.index ? dump_expr(*node.index) : "(unsupported)") + ")";
            } else if constexpr (std::is_same_v<T, Expr::Sizeof> || std::is_same_v<T, Expr::Cast>) {
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
                if (node.indirections > 0) {
                    spelled.append(static_cast<std::size_t>(node.indirections), '*');
                }
                if constexpr (std::is_same_v<T, Expr::Sizeof>) {
                    if (!node.on_type) {
                        return "(sizeof " + (node.expr ? dump_expr(*node.expr) : "(unsupported)") + ")";
                    }
                    return "(sizeof " + spelled + ")";
                } else {
                    return "(cast " + spelled + " " + (node.expr ? dump_expr(*node.expr) : "(unsupported)") + ")";
                }
            } else if constexpr (std::is_same_v<T, Expr::Bool>) {
                return node.value ? "(true)" : "(false)";
            } else if constexpr (std::is_same_v<T, Expr::Nullptr>) {
                return "(nullptr)";
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
            append_indent(out, 1);
            out += "(struct ";
            out += item->name;
            if (item->exported) {
                out += " export";
            }
            if (item->is_extern) {
                out += " extern";
            }
            out += "\n";
            for (const FieldDecl &field : item->fields) {
                append_indent(out, 2);
                out += "(field ";
                out += field.name;
                out += ' ';
                out += type_text(field.type_name, field.type_package, field.is_struct, field.is_enum, field.indirections,
                                 field.is_array, field.array_len);
                if (field.is_fn_ptr) {
                    out += " fnptr";
                    append_many_results(out, field.fn_results);
                }
                out += ")\n";
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
            if (item->is_extern) {
                out += " extern";
            }
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
            out += "(typedef ";
            out += item->name;
            out += ' ';
            out += type_text(item->type_name, item->type_package, item->is_struct, item->is_enum, item->indirections,
                             item->is_array, item->array_len);
            if (item->is_fn_ptr) {
                out += " fnptr";
                append_many_results(out, item->fn_results);
            }
            if (item->exported) {
                out += " export";
            }
            out += ")\n";
        } else if (const auto *item = std::get_if<TraitDecl>(&decl.data)) {
            append_indent(out, 1);
            out += "(trait ";
            out += item->name;
            out += "\n";
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

} // namespace tlc

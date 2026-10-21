// Indexing a parsed file for completion, hover, and go to definition.
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

void mark_direct(Sym &sym, const std::string &type_name, int indirections, bool is_array, int array_len ) {
    sym.type_name = type_name;
    sym.indirections = indirections;
    sym.is_array = is_array;
    sym.array_len = array_len;
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
    mark_direct(sym, slot.type_name, slot.indirections, slot.is_array, slot.array_len);
}

// The receiver is not a parameter the call uses to choose a list.
void note_params(Sym &sym, const FunctionDecl &fn) {
    std::size_t count = fn.params.size();
    if (!fn.params.empty() && fn.params.front().is_receiver) {
        --count;
    }
    sym.arity = static_cast<int>(count);
    sym.variadic = fn.variadic;
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

Shape shape_from_slot(const ResultSlot &slot) {
    Shape shape;
    if (slot.type_name.empty() || slot.type_name == "invalid" || slot.is_fn_ptr) {
        return shape;
    }
    shape.type_name = slot.type_name;
    shape.indirections = slot.indirections;
    shape.is_array = slot.is_array;
    shape.array_len = slot.array_len;
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
    shape.array_len = sym.array_len;
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
    if (const auto *range = std::get_if<Expr::Index>(&expr.data)) {
        // `a[i]` is left to the element. `a[i:j]`, `a[i:]`, `a[:j]`, and `a[:]` are slices.
        if (!range->is_range || range->base == nullptr) {
            return {};
        }
        const Shape base = shape_of_expr(*range->base, index, syms, uri, offset);
        if (!base.known) {
            return {};
        }
        if (base.is_array) {
            Shape slice = base;
            slice.is_array = true;
            slice.array_len = -1;
            return slice;
        }
        // `type Row []i32` stays `Row`. A sized-array alias becomes `[]element`.
        for (const Sym &sym : syms) {
            if (sym.role != Role::Alias || sym.name != base.type_name || !sym.is_array) {
                continue;
            }
            if (sym.array_len < 0) {
                return base;
            }
            Shape slice;
            slice.type_name = sym.type_name;
            slice.indirections = sym.indirections;
            slice.is_array = true;
            slice.array_len = -1;
            slice.known = true;
            return slice;
        }
        return {};
    }
    if (const auto *selector = std::get_if<Expr::Selector>(&expr.data)) {
        if (selector->base == nullptr) {
            return {};
        }
        Shape base;
        const Sym *named_sym = nullptr;
        if (const auto *named = std::get_if<Expr::Name>(&selector->base->data)) {
            named_sym = lookup_bare(syms, uri, named->text, offset);
            if (named_sym == nullptr) {
                return {};
            }
            base = shape_from_sym(*named_sym);
        } else {
            base = shape_of_expr(*selector->base, index, syms, uri, offset);
        }
        if (selector->field == "len") {
            bool array = base.known && base.is_array;
            if (!array && named_sym != nullptr && !named_sym->type_name.empty()) {
                for (const Sym &other : syms) {
                    if (other.role == Role::Alias && other.name == named_sym->type_name && other.is_array &&
                        other.indirections == 0) {
                        array = true;
                        break;
                    }
                }
            }
            if (array) {
                Shape length;
                length.type_name = "i32";
                length.known = true;
                return length;
            }
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
        if (init->array_literal) {
            if ((init->type_name.empty() || init->type_name == "invalid") && !init->elem_fn) {
                return shape;
            }
            shape.type_name = spell_array_element(*init);
            if (shape.type_name.empty() || shape.type_name == "invalid") {
                return shape;
            }
            shape.is_array = true;
            shape.array_len = static_cast<int>(init->elements.size());
            shape.known = true;
            return shape;
        }
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
    mark_direct(sym, shape.type_name, shape.indirections, shape.is_array, shape.array_len);
    Place place;
    place.type_name = shape.type_name;
    place.indirections = shape.indirections;
    place.is_array = shape.is_array;
    place.array_len = shape.array_len;
    const std::string spelled = spell_type(place);
    if (sym.detail == sym.name || sym.detail == "const " + sym.name) {
        sym.detail += " ";
        sym.detail += spelled;
    }
}

void add_function_locals(const FunctionDecl &fn, const std::string &text, const std::string &uri, uint32_t scope_end,
                         std::vector<Sym> &out, const UnitIndex &index) {
    for (const Param &param : fn.params) {
        Sym sym;
        sym.name = param.name;
        sym.detail = param.name + " " + spell_param(param);
        mark_direct(sym, param.type_name, param.indirections, param.is_array, param.array_len);
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
            mark_direct(sym, var.type_name, var.indirections, var.is_array, var.array_len);
            sym.detail = var.is_const_decl ? "const " + var.name : var.name;
            if (!var.type_name.empty()) {
                sym.detail += " ";
                sym.detail += spell_var(var);
            }
            sym.uri = uri;
            sym.bare = true;
            sym.local = true;
            sym.role = var.is_const_decl ? Role::Constant : Role::Variable;
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
            note_params(sym, fn);
            put_loc(sym, text, fn.loc, fn.name);
            sym.scope_begin = fn.loc.valid() ? fn.loc.offset : 0;
            // The name is visible in the enclosing function. Only the parameters use the body.
            sym.scope_end = scope_end;
            add_sym(out, std::move(sym));
            add_function_locals(fn, text, uri, end, out, index);
        }
        return;
    }
    if (const auto *block = std::get_if<Stmt::Block>(&stmt.data)) {
        const uint32_t end = block->loc.valid() ? match_brace(text, block->loc.offset) : scope_end;
        for (const auto &child : block->statements) {
            if (child) {
                walk_stmt(*child, text, uri, end, out, index);
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
        uint32_t end = scope_end;
        if (item->body != nullptr) {
            if (const auto *body = std::get_if<Stmt::Block>(&item->body->data)) {
                if (body->loc.valid()) {
                    end = match_brace(text, body->loc.offset);
                }
            }
        }
        if (item->init) {
            walk_stmt(*item->init, text, uri, end, out, index);
        }
        if (item->body) {
            walk_stmt(*item->body, text, uri, end, out, index);
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
            note_params(sym, *fn);
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
            mark_direct(sym, var->type_name, var->indirections, var->is_array, var->array_len);
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
                mark_direct(member, field.type_name, field.indirections, field.is_array, field.array_len);
                member.package_key = node.package_key;
                member.uri = uri;
                member.exported = field.exported || item->is_extern || item->extern_c || item->is_union;
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
            mark_direct(sym, item->type_name, item->indirections, item->is_array, item->array_len);
            sym.package_key = node.package_key;
            sym.uri = uri;
            sym.exported = item->exported;
            sym.bare = entry;
            sym.outline = entry;
            sym.role = Role::Alias;
            if (item->is_array) {
                const Place place{item->type_name, item->indirections, item->optional, item->const_pointee, true,
                                  item->array_len, item->elem_slice};
                sym.detail = "type " + item->name + " " + spell_type(place);
            } else {
                sym.detail = "type " + item->name + " " + item->type_name;
            }
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
            sym.exported = item->exported;
            sym.bare = entry;
            sym.outline = entry;
            sym.role = Role::Trait;
            sym.detail = "type " + item->name + " trait";
            if (item->type_params.size() == 1 && item->type_params[0].is_type) {
                sym.detail = "type " + item->name + "[" + item->type_params[0].name + " type] trait";
            }
            put_loc(sym, text, item->loc, item->name);
            add_sym(out, std::move(sym));
            const uint32_t trait_end = next_mark(item->loc.offset);
            for (const TypeParam &param : item->type_params) {
                Sym type_param;
                type_param.name = param.name;
                type_param.detail = param.is_type ? param.name + " type" : param.name;
                type_param.uri = uri;
                type_param.bare = true;
                type_param.local = true;
                type_param.role = Role::TypeParam;
                type_param.scope_end = trait_end;
                put_loc(type_param, text, param.loc, param.name);
                type_param.scope_begin = param.loc.valid() ? param.loc.offset : 0;
                add_sym(out, std::move(type_param));
            }
            for (const FieldDecl &field : item->fields) {
                Sym member;
                member.name = field.name;
                member.owner = item->name;
                mark_direct(member, field.type_name, field.indirections, field.is_array, field.array_len);
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
                note_params(member, method);
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

} // namespace tlc::lsp_impl

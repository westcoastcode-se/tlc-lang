// Emitting function bodies, calls, expressions, and statements.
#include "gen.hpp"

#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

// Stable name for one captured function pointer. The id is the function that owns the name.
std::string capture_key(int function_id, const std::string &name) {
    return ".cap." + std::to_string(function_id) + "." + name;
}

// `Entity.next` is one global. A package `next` keeps the bare name.
std::string global_key(const tlc::VarDecl &var) {
    if (var.owner.empty()) {
        return var.name;
    }
    return var.owner + "." + var.name;
}

} // namespace

namespace tlc {

void CodeGen::declare_file(const PackageNode &node) {
    current_file_ = node.file_id;
    struct Acc {
        const FunctionDecl *fn = nullptr;
        bool exported = false;
        bool is_static = false;
        bool is_extern = false;
        bool extern_c = false;
    };
    std::unordered_map<std::string, Acc> functions;
    for (const TopDecl &decl : node.unit.decls) {
        const auto *fn = std::get_if<FunctionDecl>(&decl.data);
        if (fn == nullptr || fn->name.empty()) {
            continue;
        }
        Acc &acc = functions[function_key(*fn)];
        if (acc.fn == nullptr || fn->body != nullptr) {
            acc.fn = fn;
        }
        acc.exported = acc.exported || fn->exported;
        acc.is_static = acc.is_static || fn->is_static;
        acc.is_extern = acc.is_extern || fn->is_extern;
        acc.extern_c = acc.extern_c || fn->extern_c;
    }
    FileSymbols &file = files_[node.file_id];
    const bool entry = node.file_id == entry_file_;
    for (const auto &[name, acc] : functions) {
        if (!acc.fn->type_params.empty()) {
            continue;
        }
        if (!acc.fn->method_struct.empty()) {
            bool method_template = false;
            for (const TopDecl &decl : node.unit.decls) {
                const auto *item = std::get_if<StructDecl>(&decl.data);
                if (item != nullptr && item->name == acc.fn->method_struct && !item->type_params.empty()) {
                    method_template = true;
                    break;
                }
            }
            if (method_template) {
                continue;
            }
        }
        const bool many = acc.fn->results.size() > 1;
        const bool returns_void = !many && acc.fn->return_type == "void" && acc.fn->indirections == 0 && !acc.fn->is_struct &&
                                  acc.fn->type_package.empty();
        const bool ret_fnptr = !many && acc.fn->results.size() == 1 && acc.fn->results.front().is_fn_ptr;
        const ResultSlot *ret_slot = ret_fnptr ? &acc.fn->results.front() : nullptr;
        const Lowered ret = returns_void || many
                                ? Lowered{}
                                : (ret_fnptr ? lower_fnptr(node.file_id, ret_slot->type_name, ret_slot->type_package,
                                                          ret_slot->is_struct, ret_slot->is_enum, ret_slot->ret_indirections,
                                                          ret_slot->indirections, ret_slot->fn_params, ret_slot->fn_variadic,
                                                          ret_slot->fn_results)
                                             : lower(node.file_id, acc.fn->return_type, acc.fn->type_package, acc.fn->is_struct,
                                                     acc.fn->indirections, false, 0, false));
        if (function_is_generic(node.file_id, *acc.fn)) {
            Binding binding;
            binding.is_function = true;
            binding.is_generic = true;
            binding.is_static = acc.is_static;
            binding.decl = acc.fn;
            binding.depth = returns_void ? 0 : ret.depth;
            binding.elem_char = !returns_void && ret.elem_char;
            binding.struct_id = returns_void ? -1 : ret.struct_id;
            binding.elem_void = !returns_void && ret.elem_void;
            binding.elem_bool = !returns_void && ret.elem_bool;
            binding.elem_bits = returns_void ? 0 : ret.elem_bits;
            binding.elem_unsigned = !returns_void && ret.elem_unsigned;
            binding.elem_float = !returns_void && ret.elem_float;
            binding.trait_id = returns_void ? -1 : ret.trait_id;
            binding.enum_id = returns_void ? -1 : ret.enum_id;
            file.functions.emplace(name, binding);
            continue;
        }
        const SymbolForm form = symbol_form(node, name, acc.is_extern, acc.extern_c, acc.is_static, acc.exported,
                                            entry && !testing_ && name == "main");
        llvm::Function *function = acc.is_extern ? module_.getFunction(form.name) : nullptr;
        const ExternLayout layout = acc.extern_c ? extern_layout(*acc.fn, node.file_id) : ExternLayout{};
        if (function == nullptr) {
            llvm::FunctionType *ftype = layout.active ? layout.type : function_type(*acc.fn, node.file_id);
            function = llvm::Function::Create(ftype, form.linkage, form.name, module_);
            function->setCallingConv(llvm::CallingConv::C);
            if (layout.active) {
                apply_extern_attrs(function, layout);
                extern_abis_.emplace(function, layout);
            }
            if (!layout.active) {
                unsigned index = 0;
                for (auto &arg : function->args()) {
                    if (index < acc.fn->params.size() && !acc.fn->params[index].name.empty()) {
                        arg.setName(acc.fn->params[index].name);
                    }
                    ++index;
                }
            }
        } else if (layout.active && !extern_abis_.contains(function)) {
            apply_extern_attrs(function, layout);
            extern_abis_.emplace(function, layout);
        }
        Binding binding;
        binding.value = function;
        binding.depth = returns_void ? 0 : ret.depth;
        binding.is_function = true;
        binding.elem_char = !returns_void && ret.elem_char;
        binding.struct_id = returns_void ? -1 : ret.struct_id;
        binding.elem_void = !returns_void && ret.elem_void;
        binding.elem_bool = !returns_void && ret.elem_bool;
        binding.elem_bits = returns_void ? 0 : ret.elem_bits;
        binding.elem_unsigned = !returns_void && ret.elem_unsigned;
        binding.elem_float = !returns_void && ret.elem_float;
        binding.trait_id = returns_void ? -1 : ret.trait_id;
        binding.enum_id = returns_void ? -1 : ret.enum_id;
        binding.is_static = acc.is_static;
        binding.decl = acc.fn;
        file.functions.emplace(name, binding);
    }
    for (const TopDecl &decl : node.unit.decls) {
        const auto *var = std::get_if<VarDecl>(&decl.data);
        // A generic template mentions its parameters. Each specialization is appended later.
        if (var == nullptr || var->name.empty() || !var->owner_params.empty()) {
            continue;
        }
        const std::string key = global_key(*var);
        if (file.globals.contains(key)) {
            continue;
        }
        if (var->is_const_decl && var->has_const) {
            Binding binding;
            binding.immediate = true;
            binding.const_value = var->const_value;
            binding.elem_bits = 32;
            binding.is_static = var->is_static;
            file.globals.emplace(key, binding);
            continue;
        }
        const SymbolForm form =
            symbol_form(node, key, var->is_extern, var->extern_c, var->is_static, var->exported, false);
        Lowered low;
        if (var->inferred) {
            if (var->init == nullptr || !infer_global(node.file_id, *var->init, low)) {
                error(var->loc, "type mismatch");
                low = lower(node.file_id, "i32", "", false, 0, false, 0, false);
            }
        } else {
            low = lower_declared(node.file_id, *var);
        }
        llvm::Type *ty = stored_type(low);
        llvm::GlobalVariable *existing = var->is_extern ? module_.getGlobalVariable(form.name) : nullptr;
        llvm::GlobalVariable *global = existing;
        if (global == nullptr) {
            llvm::Constant *init = nullptr;
            if (!var->is_extern) {
                if (low.is_slice || low.is_array || (low.depth == 0 && (low.struct_id >= 0 || low.trait_id >= 0))) {
                    init = llvm::ConstantAggregateZero::get(ty);
                } else if (low.depth > 0) {
                    init = null_ptr();
                } else if (low.elem_float) {
                    init = llvm::ConstantFP::get(low.elem_bits >= 64 ? f64_ty() : f32_ty(), 0.0);
                } else if (low.elem_bool) {
                    init = llvm::ConstantInt::get(i1(), 0, false);
                } else if (low.elem_char || low.elem_bits == 8) {
                    init = llvm::ConstantInt::get(i8(), 0, false);
                } else if (low.elem_bits == 16) {
                    init = llvm::ConstantInt::get(i16(), 0, false);
                } else if (low.elem_bits == 64) {
                    init = llvm::ConstantInt::get(i64(), 0, false);
                } else {
                    init = const_i32(0);
                }
            }
            global = new llvm::GlobalVariable(module_, ty, var->is_const_decl, form.linkage, init, form.name);
            debug_global(key, global, var->loc, low, var->is_static);
        }
        Binding binding;
        binding.value = global;
        binding.depth = low.depth;
        binding.elem_char = low.elem_char;
        binding.is_array = low.is_array;
        binding.array_len = low.array_len;
        binding.ptr_to_array = low.ptr_to_array;
        binding.ptr_array_len = low.ptr_array_len;
        binding.struct_id = low.struct_id;
        binding.elem_void = low.elem_void;
        binding.elem_bool = low.elem_bool;
        binding.elem_bits = low.elem_bits;
        binding.elem_unsigned = low.elem_unsigned;
        binding.elem_float = low.elem_float;
        binding.trait_id = low.trait_id;
        binding.fn_ty = low.fn_ty;
        binding.fn_ret_depth = low.fn_ret_depth;
        binding.fn_ret_float = low.fn_ret_float;
        binding.fn_ret_bits = low.fn_ret_bits;
        binding.fn_ret_unsigned = low.fn_ret_unsigned;
        binding.fn_ret_char = low.fn_ret_char;
        binding.fn_ret_bool = low.fn_ret_bool;
        binding.fn_ret_struct = low.fn_ret_struct;
        binding.fn_results = low.fn_results;
        binding.enum_id = low.enum_id;
        binding.inner_array_len = low.inner_array_len;
        binding.is_slice = low.is_slice;
        binding.is_static = var->is_static;
        file.globals.emplace(key, binding);
    }
}

void CodeGen::bind_imports(const PackageNode &node) {
    if (graph_ == nullptr) {
        return;
    }
    for (const ImportDecl &import : node.unit.imports) {
        const PackageNode *target = nullptr;
        for (const ImportEdge &edge : graph_->edges) {
            if (edge.from_file == node.file_id && edge.spec == import.path && edge.loc.file == import.loc.file &&
                edge.loc.offset == import.loc.offset) {
                target = find_node(edge.to_file);
                break;
            }
        }
        if (target == nullptr) {
            continue;
        }
        if (import.form == ImportDecl::Form::Selective) {
            for (const std::string &name : import.names) {
                if (const Binding *function = visible(target->file_id, name, true)) {
                    scopes_.back()[name] = *function;
                } else if (const Binding *global = visible(target->file_id, name, false)) {
                    scopes_.back()[name] = *global;
                }
            }
            continue;
        }
        const std::string binding = import.form == ImportDecl::Form::Alias ? import.alias : target->qualifier;
        if (!binding.empty()) {
            packages_[binding] = target->file_id;
        }
    }
}

void CodeGen::emit_bodies(const PackageNode &node) {
    current_file_ = node.file_id;
    packages_.clear();
    push();
    bind_package_globals();
    bind_imports(node);
    std::unordered_set<std::string> emitted;
    for (const TopDecl &decl : node.unit.decls) {
        const auto *fn = std::get_if<FunctionDecl>(&decl.data);
        if (fn == nullptr || fn->body == nullptr || function_is_generic(node.file_id, *fn) ||
            !emitted.insert(function_key(*fn)).second) {
            continue;
        }
        emit_function(*fn);
    }
    pop();
}

std::optional<int32_t> CodeGen::fold_global(uint32_t file, const std::string &name, bool allow_local_static) const {
    for (const uint32_t id : package_files(file)) {
        const auto found = files_.find(id);
        if (found == files_.end() || !found->second.folded.contains(name)) {
            continue;
        }
        const auto global = found->second.globals.find(name);
        if (global == found->second.globals.end()) {
            continue;
        }
        if (global->second.is_static && !(allow_local_static && id == file)) {
            continue;
        }
        if (global->second.immediate) {
            return global->second.const_value;
        }
        if (global->second.value == nullptr) {
            continue;
        }
        const auto *variable = llvm::dyn_cast<llvm::GlobalVariable>(global->second.value);
        if (variable == nullptr || !variable->hasInitializer()) {
            continue;
        }
        if (const auto *constant = llvm::dyn_cast<llvm::ConstantInt>(variable->getInitializer())) {
            return static_cast<int32_t>(constant->getSExtValue());
        }
    }
    return std::nullopt;
}

namespace {

double round_float(double value, bool wide) {
    if (wide) {
        return value;
    }
    llvm::APFloat parsed(value);
    bool loses = false;
    parsed.convert(llvm::APFloat::IEEEsingle(), llvm::APFloat::rmNearestTiesToEven, &loses);
    return parsed.convertToDouble();
}

double float_op(TokenKind op, double lhs, double rhs, bool wide) {
    llvm::APFloat left(lhs);
    llvm::APFloat right(rhs);
    if (!wide) {
        bool loses = false;
        left.convert(llvm::APFloat::IEEEsingle(), llvm::APFloat::rmNearestTiesToEven, &loses);
        right.convert(llvm::APFloat::IEEEsingle(), llvm::APFloat::rmNearestTiesToEven, &loses);
    }
    switch (op) {
    case TokenKind::Plus: left.add(right, llvm::APFloat::rmNearestTiesToEven); break;
    case TokenKind::Minus: left.subtract(right, llvm::APFloat::rmNearestTiesToEven); break;
    case TokenKind::Star: left.multiply(right, llvm::APFloat::rmNearestTiesToEven); break;
    case TokenKind::Slash: left.divide(right, llvm::APFloat::rmNearestTiesToEven); break;
    default: break;
    }
    return left.convertToDouble();
}

bool is_float_logic(TokenKind op) {
    switch (op) {
    case TokenKind::EqEq:
    case TokenKind::BangEq:
    case TokenKind::Lt:
    case TokenKind::Gt:
    case TokenKind::Le:
    case TokenKind::Ge:
    case TokenKind::AmpAmp:
    case TokenKind::PipePipe:
        return true;
    default:
        return false;
    }
}

} // namespace

std::optional<CodeGen::FoldedNumber> CodeGen::fold_number(const Expr &expr) const {
    std::unordered_set<std::string> seen;
    return fold_number(expr, seen);
}

std::optional<CodeGen::FoldedNumber> CodeGen::fold_number(const Expr &expr, std::unordered_set<std::string> &seen) const {
    const auto from_int = [](int32_t value) {
        return FoldedNumber{static_cast<double>(value), false, false};
    };
    if (const auto *cast = std::get_if<Expr::Cast>(&expr.data)) {
        if (cast->indirections != 0 || cast->is_fn_ptr || cast->expr == nullptr) {
            return std::nullopt;
        }
        std::optional<FoldedNumber> inner = fold_number(*cast->expr, seen);
        if (!inner) {
            return std::nullopt;
        }
        if (cast->type_name == "f32" || cast->type_name == "float") {
            inner->value = round_float(inner->value, false);
            inner->is_float = true;
            inner->wide = false;
        } else if (cast->type_name == "f64" || cast->type_name == "double") {
            inner->is_float = true;
            inner->wide = true;
        }
        return inner;
    }
    if (const auto *literal = std::get_if<Expr::Float>(&expr.data)) {
        const double parsed = literal->text.empty() ? 0.0 : std::strtod(literal->text.c_str(), nullptr);
        if (literal->is_f32) {
            return FoldedNumber{round_float(parsed, false), true, false};
        }
        return FoldedNumber{parsed, true, true};
    }
    if (const auto *unary = std::get_if<Expr::Unary>(&expr.data)) {
        if (unary->postfix || unary->expr == nullptr ||
            (unary->op != TokenKind::Plus && unary->op != TokenKind::Minus)) {
            return std::nullopt;
        }
        std::optional<FoldedNumber> inner = fold_number(*unary->expr, seen);
        if (!inner) {
            return std::nullopt;
        }
        if (unary->op == TokenKind::Minus) {
            inner->value = -inner->value;
        }
        return inner;
    }
    if (const auto *binary = std::get_if<Expr::Binary>(&expr.data)) {
        if (binary->lhs == nullptr || binary->rhs == nullptr) {
            return std::nullopt;
        }
        const std::optional<int32_t> left_int = fold(*binary->lhs);
        const std::optional<int32_t> right_int = fold(*binary->rhs);
        if (left_int && right_int) {
            const std::optional<int32_t> value = fold(expr);
            if (!value) {
                return std::nullopt;
            }
            return from_int(*value);
        }
        const std::optional<FoldedNumber> left = fold_number(*binary->lhs, seen);
        const std::optional<FoldedNumber> right = fold_number(*binary->rhs, seen);
        if (!left || !right || (!left->is_float && !right->is_float)) {
            return std::nullopt;
        }
        const bool wide = left->wide || right->wide;
        const double a = round_float(left->value, wide);
        const double b = round_float(right->value, wide);
        if (binary->op == TokenKind::Slash && b == 0.0) {
            return std::nullopt;
        }
        if (is_float_logic(binary->op)) {
            bool yes = false;
            switch (binary->op) {
            case TokenKind::EqEq: yes = a == b; break;
            case TokenKind::BangEq: yes = a != b; break;
            case TokenKind::Lt: yes = a < b; break;
            case TokenKind::Gt: yes = a > b; break;
            case TokenKind::Le: yes = a <= b; break;
            case TokenKind::Ge: yes = a >= b; break;
            case TokenKind::AmpAmp: yes = a != 0.0 && b != 0.0; break;
            case TokenKind::PipePipe: yes = a != 0.0 || b != 0.0; break;
            default: return std::nullopt;
            }
            return from_int(yes ? 1 : 0);
        }
        switch (binary->op) {
        case TokenKind::Plus:
        case TokenKind::Minus:
        case TokenKind::Star:
        case TokenKind::Slash:
            return FoldedNumber{float_op(binary->op, a, b, wide), true, wide};
        default:
            return std::nullopt;
        }
    }
    if (const std::optional<int32_t> number = fold(expr)) {
        return from_int(*number);
    }
    if (const auto *named = std::get_if<Expr::Name>(&expr.data)) {
        if (!seen.insert(named->text).second) {
            return std::nullopt;
        }
        const VarDecl *written = global_decl(named->text);
        std::optional<FoldedNumber> inner;
        if (written != nullptr && written->is_const_decl && written->init != nullptr) {
            inner = fold_number(*written->init, seen);
        }
        seen.erase(named->text);
        return inner;
    }
    return std::nullopt;
}

std::optional<int32_t> CodeGen::fold(const Expr &expr) const {
    auto wrap = [](int64_t value) { return static_cast<int32_t>(static_cast<uint32_t>(value)); };
    return std::visit(
        [&](const auto &node) -> std::optional<int32_t> {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Expr::Int>) {
                return static_cast<int32_t>(node.value);
            } else if constexpr (std::is_same_v<T, Expr::Char>) {
                return static_cast<int32_t>(node.value & 0xFFULL);
            } else if constexpr (std::is_same_v<T, Expr::Bool>) {
                return node.value ? 1 : 0;
            } else if constexpr (std::is_same_v<T, Expr::Nullptr>) {
                return 0;
            } else if constexpr (std::is_same_v<T, Expr::Name>) {
                if (const auto local = fold_global(current_file_, node.text)) {
                    return local;
                }
                const PackageNode *owner = find_node(current_file_);
                if (owner == nullptr || graph_ == nullptr) {
                    return std::nullopt;
                }
                for (const ImportDecl &import : owner->unit.imports) {
                    if (import.form != ImportDecl::Form::Selective) {
                        continue;
                    }
                    bool wanted = false;
                    for (const std::string &item : import.names) {
                        if (item == node.text) {
                            wanted = true;
                        }
                    }
                    if (!wanted) {
                        continue;
                    }
                    for (const ImportEdge &edge : graph_->edges) {
                        if (edge.from_file == current_file_ && edge.spec == import.path && edge.loc.file == import.loc.file &&
                            edge.loc.offset == import.loc.offset) {
                            if (const auto imported = fold_global(edge.to_file, node.text, false)) {
                                return imported;
                            }
                        }
                    }
                }
                return std::nullopt;
            } else if constexpr (std::is_same_v<T, Expr::Selector>) {
                const auto *base = node.base == nullptr ? nullptr : std::get_if<Expr::Name>(&node.base->data);
                if (base == nullptr) {
                    return std::nullopt;
                }
                const uint32_t target = package_file(current_file_, base->text);
                if (target == 0) {
                    return std::nullopt;
                }
                return fold_global(target, node.field, false);
            } else if constexpr (std::is_same_v<T, Expr::Unary>) {
                if (node.postfix || node.expr == nullptr) {
                    return std::nullopt;
                }
                if (node.op == TokenKind::Bang) {
                    if (const auto inner = fold(*node.expr)) {
                        return *inner == 0 ? 1 : 0;
                    }
                    const auto inner = fold_number(*node.expr);
                    if (!inner) {
                        return std::nullopt;
                    }
                    return inner->value == 0.0 ? 1 : 0;
                }
                const auto inner = fold(*node.expr);
                if (!inner) {
                    return std::nullopt;
                }
                switch (node.op) {
                case TokenKind::Plus: return *inner;
                case TokenKind::Minus: return wrap(-static_cast<int64_t>(*inner));
                case TokenKind::Tilde: return wrap(~static_cast<uint32_t>(*inner));
                default: return std::nullopt;
                }
            } else if constexpr (std::is_same_v<T, Expr::Binary>) {
                if (node.lhs != nullptr && node.rhs != nullptr &&
                    (node.op == TokenKind::AmpAmp || node.op == TokenKind::PipePipe)) {
                    const auto lhs = fold(*node.lhs);
                    if (lhs && node.op == TokenKind::AmpAmp && *lhs == 0) {
                        return 0;
                    }
                    if (lhs && node.op == TokenKind::PipePipe && *lhs != 0) {
                        return 1;
                    }
                    const auto rhs = lhs ? fold(*node.rhs) : std::nullopt;
                    if (lhs && rhs) {
                        return *rhs != 0 ? 1 : 0;
                    }
                }
                const auto lhs = node.lhs ? fold(*node.lhs) : std::nullopt;
                const auto rhs = node.rhs ? fold(*node.rhs) : std::nullopt;
                if (!lhs || !rhs) {
                    if (!is_float_logic(node.op) || node.lhs == nullptr || node.rhs == nullptr) {
                        return std::nullopt;
                    }
                    const auto left = fold_number(*node.lhs);
                    const auto right = fold_number(*node.rhs);
                    if (!left || !right || (!left->is_float && !right->is_float)) {
                        return std::nullopt;
                    }
                    const bool wide = left->wide || right->wide;
                    const double a = round_float(left->value, wide);
                    const double b = round_float(right->value, wide);
                    switch (node.op) {
                    case TokenKind::EqEq: return a == b ? 1 : 0;
                    case TokenKind::BangEq: return a != b ? 1 : 0;
                    case TokenKind::Lt: return a < b ? 1 : 0;
                    case TokenKind::Gt: return a > b ? 1 : 0;
                    case TokenKind::Le: return a <= b ? 1 : 0;
                    case TokenKind::Ge: return a >= b ? 1 : 0;
                    case TokenKind::AmpAmp: return (a != 0.0 && b != 0.0) ? 1 : 0;
                    case TokenKind::PipePipe: return (a != 0.0 || b != 0.0) ? 1 : 0;
                    default: return std::nullopt;
                    }
                }
                const int64_t a = *lhs;
                const int64_t b = *rhs;
                switch (node.op) {
                case TokenKind::Plus: return wrap(a + b);
                case TokenKind::Minus: return wrap(a - b);
                case TokenKind::Star: return wrap(a * b);
                case TokenKind::Slash:
                    if (b == 0) return std::nullopt;
                    return wrap(a / b);
                case TokenKind::Percent:
                    if (b == 0) return std::nullopt;
                    return wrap(a % b);
                case TokenKind::Amp: return wrap(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
                case TokenKind::Pipe: return wrap(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
                case TokenKind::Caret: return wrap(static_cast<uint32_t>(a) ^ static_cast<uint32_t>(b));
                case TokenKind::LtLt:
                    if (b < 0 || b >= 32) return std::nullopt;
                    return wrap(static_cast<uint32_t>(a) << static_cast<uint32_t>(b));
                case TokenKind::GtGt:
                    if (b < 0 || b >= 32) return std::nullopt;
                    return static_cast<int32_t>(static_cast<uint32_t>(static_cast<int32_t>(a) >> static_cast<uint32_t>(b)));
                case TokenKind::EqEq: return a == b ? 1 : 0;
                case TokenKind::BangEq: return a != b ? 1 : 0;
                case TokenKind::Lt: return a < b ? 1 : 0;
                case TokenKind::Gt: return a > b ? 1 : 0;
                case TokenKind::Le: return a <= b ? 1 : 0;
                case TokenKind::Ge: return a >= b ? 1 : 0;
                default: return std::nullopt;
                }
            } else if constexpr (std::is_same_v<T, Expr::Cast>) {
                if (node.indirections != 0 || node.is_struct || node.is_enum || node.is_fn_ptr || node.expr == nullptr) {
                    return std::nullopt;
                }
                return fold(*node.expr);
            } else {
                return std::nullopt;
            }
        },
        expr.data);
}

llvm::Constant *CodeGen::const_field(const Expr &expr, const FieldLayout &slot) {
    if (slot.is_slice) {
        std::unordered_set<std::string> seen;
        return constant_slice(expr, seen);
    }
    if (slot.is_array || slot.type == nullptr) {
        return nullptr;
    }
    if (const auto *init = std::get_if<Expr::Init>(&expr.data)) {
        if (slot.depth != 0 || slot.struct_id < 0) {
            return nullptr;
        }
        return const_struct(*init, slot.struct_id);
    }
    if (slot.depth > 0) {
        if (std::holds_alternative<Expr::Nullptr>(expr.data)) {
            return null_ptr();
        }
        return nullptr;
    }
    if (slot.elem_float) {
        if (const auto *literal = std::get_if<Expr::Float>(&expr.data)) {
            const std::string number = literal->text.empty() ? "0" : literal->text;
            return llvm::ConstantFP::get(slot.type, std::strtod(number.c_str(), nullptr));
        }
        const std::optional<int32_t> number = fold(expr);
        if (!number) {
            return nullptr;
        }
        return llvm::ConstantFP::get(slot.type, static_cast<double>(*number));
    }
    if (const auto *selector = std::get_if<Expr::Selector>(&expr.data);
        selector != nullptr && selector->base != nullptr) {
        const int enum_id = enum_type_designator(*selector->base);
        if (enum_id >= 0 && enum_id < static_cast<int>(enums_.size())) {
            const auto member = enums_[static_cast<std::size_t>(enum_id)].values.find(selector->field);
            if (member == enums_[static_cast<std::size_t>(enum_id)].values.end() || !slot.type->isIntegerTy()) {
                return nullptr;
            }
            return llvm::ConstantInt::get(slot.type, static_cast<uint64_t>(member->second), true);
        }
    }
    if (!slot.type->isIntegerTy()) {
        return nullptr;
    }
    const std::optional<int32_t> number = fold(expr);
    if (!number) {
        return nullptr;
    }
    return llvm::ConstantInt::get(slot.type, static_cast<uint64_t>(static_cast<int64_t>(*number)), true);
}

void CodeGen::store_union_tag(int struct_id, llvm::Value *pointer, const std::string &field) {
    if (pointer == nullptr || struct_id < 0 || struct_id >= static_cast<int>(structs_.size())) {
        return;
    }
    const StructLayout &layout = structs_[static_cast<std::size_t>(struct_id)];
    if (layout.tag_enum < 0 || layout.tag_enum >= static_cast<int>(enums_.size()) || layout.type == nullptr ||
        layout.type->getNumElements() == 0) {
        return;
    }
    const auto member = enums_[static_cast<std::size_t>(layout.tag_enum)].values.find(field);
    if (member == enums_[static_cast<std::size_t>(layout.tag_enum)].values.end()) {
        return;
    }
    llvm::Constant *tag = enum_constant(layout.tag_enum, member->second);
    if (tag == nullptr || layout.type->getNumElements() < 3 || tag->getType() != layout.type->getElementType(2)) {
        return;
    }
    llvm::Value *addr = builder_.CreateInBoundsGEP(layout.type, pointer,
                                                    llvm::ArrayRef<llvm::Value *>{const_i32(0), const_i32(2)});
    builder_.CreateStore(tag, addr);
}

bool CodeGen::write_constant_bytes(llvm::Constant *value, uint64_t offset, std::vector<uint8_t> &bytes) const {
    if (value == nullptr) {
        return false;
    }
    const llvm::DataLayout &data_layout = module_.getDataLayout();
    llvm::Type *type = value->getType();
    if (type == nullptr || !type->isSized()) {
        return false;
    }
    const uint64_t size = data_layout.getTypeAllocSize(type).getFixedValue();
    if (offset + size > bytes.size()) {
        return false;
    }
    if (llvm::isa<llvm::ConstantAggregateZero>(value) || llvm::isa<llvm::UndefValue>(value) ||
        llvm::isa<llvm::PoisonValue>(value) || llvm::isa<llvm::ConstantPointerNull>(value)) {
        return true;
    }
    auto put_int = [&](const llvm::APInt &bits) {
        if (bits.getBitWidth() > 64) {
            return false;
        }
        const uint64_t raw = bits.getZExtValue();
        const unsigned nbytes = static_cast<unsigned>(size);
        const bool little = data_layout.isLittleEndian();
        for (unsigned i = 0; i < nbytes; ++i) {
            const unsigned shift = little ? i * 8 : (nbytes - 1U - i) * 8U;
            bytes[offset + i] = shift >= 64 ? 0 : static_cast<uint8_t>((raw >> shift) & 0xFFU);
        }
        return true;
    };
    if (const auto *integer = llvm::dyn_cast<llvm::ConstantInt>(value)) {
        return put_int(integer->getValue());
    }
    if (const auto *fp = llvm::dyn_cast<llvm::ConstantFP>(value)) {
        return put_int(fp->getValueAPF().bitcastToAPInt());
    }
    if (const auto *structure = llvm::dyn_cast<llvm::ConstantStruct>(value)) {
        const llvm::StructLayout *slots = data_layout.getStructLayout(llvm::cast<llvm::StructType>(type));
        for (unsigned index = 0; index < structure->getNumOperands(); ++index) {
            if (!write_constant_bytes(structure->getOperand(index), offset + slots->getElementOffset(index), bytes)) {
                return false;
            }
        }
        return true;
    }
    if (const auto *array = llvm::dyn_cast<llvm::ConstantArray>(value)) {
        const uint64_t stride = data_layout.getTypeAllocSize(array->getType()->getElementType()).getFixedValue();
        for (unsigned index = 0; index < array->getNumOperands(); ++index) {
            if (!write_constant_bytes(array->getOperand(index), offset + stride * index, bytes)) {
                return false;
            }
        }
        return true;
    }
    if (const auto *data = llvm::dyn_cast<llvm::ConstantDataSequential>(value)) {
        const uint64_t stride = data_layout.getTypeAllocSize(data->getElementType()).getFixedValue();
        for (unsigned index = 0; index < data->getNumElements(); ++index) {
            if (!write_constant_bytes(data->getElementAsConstant(index), offset + stride * index, bytes)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

llvm::Constant *CodeGen::const_struct(const Expr::Init &init, int struct_id) {
    if (struct_id < 0 || struct_id >= static_cast<int>(structs_.size())) {
        return nullptr;
    }
    const StructLayout &layout = structs_[static_cast<std::size_t>(struct_id)];
    if (layout.type == nullptr || !layout.type->isSized() || (layout.is_union && layout.tag_enum < 0)) {
        return nullptr;
    }
    if (layout.tag_enum >= 0) {
        if (layout.type->getNumElements() < 3 || layout.tag_enum >= static_cast<int>(enums_.size())) {
            return nullptr;
        }
        const auto *bytes_type = llvm::dyn_cast<llvm::ArrayType>(layout.type->getElementType(1));
        if (bytes_type == nullptr) {
            return nullptr;
        }
        std::vector<uint8_t> raw(bytes_type->getNumElements(), 0);
        int64_t tag_value = 0;
        for (const Expr::FieldInit &field : init.fields) {
            if (field.value == nullptr || field.field.empty()) {
                return nullptr;
            }
            int index = -1;
            const FieldLayout *slot = find_field(struct_id, field.field, index);
            if (slot == nullptr) {
                return nullptr;
            }
            llvm::Constant *value = const_field(*field.value, *slot);
            if (value == nullptr) {
                return nullptr;
            }
            std::fill(raw.begin(), raw.end(), 0);
            if (!write_constant_bytes(value, 0, raw)) {
                return nullptr;
            }
            const auto member = enums_[static_cast<std::size_t>(layout.tag_enum)].values.find(field.field);
            if (member == enums_[static_cast<std::size_t>(layout.tag_enum)].values.end()) {
                return nullptr;
            }
            tag_value = member->second;
        }
        llvm::Constant *tag = enum_constant(layout.tag_enum, tag_value);
        if (tag == nullptr || tag->getType() != layout.type->getElementType(2)) {
            return nullptr;
        }
        llvm::Constant *bytes = llvm::ConstantDataArray::get(context_, raw);
        if (bytes->getType() != bytes_type) {
            return nullptr;
        }
        return llvm::ConstantStruct::get(layout.type,
                                         {llvm::ConstantAggregateZero::get(layout.type->getElementType(0)), bytes, tag});
    }
    const unsigned count = layout.type->getNumElements();
    std::vector<llvm::Constant *> elements;
    elements.reserve(count);
    for (unsigned index = 0; index < count; ++index) {
        elements.push_back(llvm::Constant::getNullValue(layout.type->getElementType(index)));
    }
    for (const Expr::FieldInit &field : init.fields) {
        if (field.value == nullptr || field.field.empty()) {
            return nullptr;
        }
        int index = -1;
        const FieldLayout *slot = find_field(struct_id, field.field, index);
        if (slot == nullptr || index < 0 || static_cast<unsigned>(index) >= count) {
            return nullptr;
        }
        llvm::Constant *value = const_field(*field.value, *slot);
        if (value == nullptr || value->getType() != elements[static_cast<std::size_t>(index)]->getType()) {
            return nullptr;
        }
        elements[static_cast<std::size_t>(index)] = value;
    }
    return llvm::ConstantStruct::get(layout.type, elements);
}

const VarDecl *CodeGen::global_decl(const std::string &name) const {
    const PackageNode *node = find_node(current_file_);
    if (node == nullptr) {
        return nullptr;
    }
    for (const TopDecl &decl : node->unit.decls) {
        const auto *var = std::get_if<VarDecl>(&decl.data);
        if (var != nullptr && var->owner.empty() && var->name == name) {
            return var;
        }
    }
    return nullptr;
}

llvm::Constant *CodeGen::fill_array(const Expr::Init &init, llvm::ArrayType *array_ty, const Binding &flags) {
    if (array_ty == nullptr || init.elements.size() != array_ty->getNumElements() || !init.fields.empty()) {
        return nullptr;
    }
    llvm::Type *element = array_ty->getElementType();
    std::vector<llvm::Constant *> values;
    values.reserve(init.elements.size());
    for (const auto &item : init.elements) {
        if (!item) {
            return nullptr;
        }
        llvm::Constant *value = nullptr;
        if (auto *inner = llvm::dyn_cast<llvm::ArrayType>(element)) {
            const auto *nested = std::get_if<Expr::Init>(&item->data);
            if (nested == nullptr) {
                return nullptr;
            }
            Binding row = flags;
            row.is_array = true;
            row.array_len = static_cast<int>(inner->getNumElements());
            row.inner_array_len = 0;
            row.value = nullptr;
            value = fill_array(*nested, inner, row);
        } else {
            Binding one = flags;
            one.is_array = false;
            one.array_len = 0;
            one.inner_array_len = 0;
            one.value = nullptr;
            value = const_initializer(*item, one);
        }
        if (value == nullptr || value->getType() != element) {
            return nullptr;
        }
        values.push_back(value);
    }
    return llvm::ConstantArray::get(array_ty, values);
}

llvm::Constant *CodeGen::const_array(const Expr::Init &init, const Binding &dest) {
    auto *global = llvm::dyn_cast_or_null<llvm::GlobalVariable>(dest.value);
    if (global == nullptr || !dest.is_array) {
        return nullptr;
    }
    auto *array_ty = llvm::dyn_cast<llvm::ArrayType>(global->getValueType());
    if (array_ty == nullptr) {
        return nullptr;
    }
    return fill_array(init, array_ty, dest);
}

llvm::AllocaInst *CodeGen::emit_array_literal(const Expr::Init &init, Lowered &low) {
    const std::optional<Lowered> got = lower_array_literal(current_file_, init);
    if (!got || got->type == nullptr || !got->type->isArrayTy()) {
        error(init.loc, "not supported yet");
        return nullptr;
    }
    low = *got;
    llvm::Type *ty = stored_type(low);
    llvm::AllocaInst *slot = alloca_typed(ty, "array");
    auto *array_ty = llvm::cast<llvm::ArrayType>(ty);
    bool complete = init.elements.size() == array_ty->getNumElements();
    if (complete && low.inner_array_len > 0) {
        for (const auto &element : init.elements) {
            const auto *brace = element == nullptr ? nullptr : std::get_if<Expr::Init>(&element->data);
            if (brace == nullptr || brace->elements.size() != static_cast<std::size_t>(low.inner_array_len)) {
                complete = false;
                break;
            }
        }
    }
    // Every listed element is stored below. The array starts as that list, not as zeros.
    if (!complete) {
        builder_.CreateStore(llvm::ConstantAggregateZero::get(ty), slot);
    }
    if (low.inner_array_len > 0) {
        auto *inner = llvm::dyn_cast<llvm::ArrayType>(array_ty->getElementType());
        if (inner == nullptr) {
            error(init.loc, "not supported yet");
            return nullptr;
        }
        unsigned row = 0;
        for (const auto &element : init.elements) {
            if (row >= array_ty->getNumElements() || terminated()) {
                break;
            }
            const auto *brace = element == nullptr ? nullptr : std::get_if<Expr::Init>(&element->data);
            if (brace == nullptr) {
                error(element == nullptr ? init.loc : expr_loc(*element), "not supported yet");
                ++row;
                continue;
            }
            llvm::Value *row_ptr = builder_.CreateInBoundsGEP(array_ty, slot, {const_i32(0), const_i32(static_cast<int>(row))});
            unsigned col = 0;
            for (const auto &cell : brace->elements) {
                if (col >= inner->getNumElements() || terminated()) {
                    break;
                }
                const TypedValue value = cell ? emit_rvalue(*cell) : TypedValue{};
                if (terminated()) {
                    break;
                }
                llvm::Value *slot_i =
                    builder_.CreateInBoundsGEP(inner, row_ptr, {const_i32(0), const_i32(static_cast<int>(col))});
                llvm::Type *cell_ty = inner->getElementType();
                llvm::Value *stored = value.value == nullptr ? llvm::Constant::getNullValue(cell_ty)
                                                             : as_stored(value.value, value.depth, cell_ty, value.elem_unsigned);
                builder_.CreateStore(stored, slot_i);
                ++col;
            }
            ++row;
        }
        return slot;
    }
    llvm::Type *elem = array_ty->getElementType();
    unsigned index = 0;
    for (const auto &element : init.elements) {
        if (index >= array_ty->getNumElements() || terminated()) {
            break;
        }
        const TypedValue value = element ? emit_rvalue(*element) : TypedValue{};
        if (terminated()) {
            break;
        }
        llvm::Value *slot_i =
            builder_.CreateInBoundsGEP(array_ty, slot, {const_i32(0), const_i32(static_cast<int>(index))});
        llvm::Value *stored = value.value == nullptr ? llvm::Constant::getNullValue(elem)
                                                     : as_stored(value.value, value.depth, elem, value.elem_unsigned);
        builder_.CreateStore(stored, slot_i);
        ++index;
    }
    return slot;
}

TypedValue CodeGen::emit_init(const Expr::Init &init) {
    if (init.struct_id < 0 || init.struct_id >= static_cast<int>(structs_.size())) {
        error(init.loc, "type mismatch");
        return {zero(), 0};
    }
    const StructLayout &layout = structs_[static_cast<std::size_t>(init.struct_id)];
    if (layout.type == nullptr) {
        error(init.loc, "type mismatch");
        return {zero(), 0};
    }
    // Omitted fields stay zero. A later assignment replaces the whole struct.
    llvm::AllocaInst *slot = alloca_typed(layout.type, "init");
    builder_.CreateStore(llvm::ConstantAggregateZero::get(layout.type), slot);
    store_init_fields(init.struct_id, slot, init);
    if (terminated()) {
        return {zero(), 0};
    }
    return load_object(slot, 0, false, init.struct_id);
}

void CodeGen::store_init_fields(int struct_id, llvm::Value *slot, const Expr::Init &init) {
    if (slot == nullptr) {
        return;
    }
    for (const Expr::FieldInit &field : init.fields) {
        if (terminated()) {
            return;
        }
        if (field.value == nullptr || field.field.empty()) {
            continue;
        }
        const TypedValue addr = field_addr(struct_id, slot, field.field, field.loc);
        if (addr.value == nullptr) {
            continue;
        }
        if (addr.is_array) {
            error(field.loc, "not supported yet");
            continue;
        }
        const TypedValue rhs = emit_rvalue(*field.value);
        if (terminated()) {
            return;
        }
        if (rhs.value == nullptr) {
            continue;
        }
        if (addr.is_slice) {
            builder_.CreateStore(as_slice_value(rhs, field.loc), addr.value);
        } else {
            builder_.CreateStore(stored_value(rhs, addr), addr.value);
        }
        // The enum tag is stored after the payload.
        store_union_tag(struct_id, slot, field.field);
    }
}

llvm::Constant *CodeGen::intern_type_name(const std::string &text) {
    const auto found = type_names_.find(text);
    if (found != type_names_.end()) {
        return found->second;
    }
    llvm::Constant *data = llvm::ConstantDataArray::getString(context_, text, true);
    auto *global = new llvm::GlobalVariable(module_, data->getType(), true, llvm::GlobalValue::PrivateLinkage, data, ".type");
    type_names_.emplace(text, global);
    return global;
}

llvm::Constant *CodeGen::sized_int(const Binding &dest, int32_t value) const {
    // f64 is 64 bits wide. That width is an integer only when the slot is not a float.
    if (dest.depth != 0 || dest.struct_id >= 0 || dest.fn_ty != nullptr || dest.elem_float) {
        return nullptr;
    }
    if (dest.elem_bool) {
        return llvm::ConstantInt::get(i1(), value == 0 ? 0 : 1, false);
    }
    if (dest.elem_char || dest.elem_bits == 8) {
        return llvm::ConstantInt::get(i8(), static_cast<uint64_t>(value) & 0xFFU, false);
    }
    if (dest.elem_bits == 16) {
        return llvm::ConstantInt::get(i16(), static_cast<uint64_t>(value) & 0xFFFFU, false);
    }
    if (dest.elem_bits == 64) {
        return llvm::ConstantInt::get(i64(), static_cast<uint64_t>(static_cast<int64_t>(value)), true);
    }
    return const_i32(value);
}

llvm::Constant *CodeGen::const_initializer(const Expr &expr, const Binding &dest) {
    // Initializers run before function scopes exist. Names are the file's globals and functions.
    const auto find_here = [&](const std::string &name) -> const Binding * {
        const auto file = files_.find(current_file_);
        if (file == files_.end()) {
            return nullptr;
        }
        const auto global = file->second.globals.find(name);
        if (global != file->second.globals.end()) {
            return &global->second;
        }
        const auto function = file->second.functions.find(name);
        if (function != file->second.functions.end()) {
            return &function->second;
        }
        return nullptr;
    };
    const Expr *cursor = &expr;
    while (const auto *cast = std::get_if<Expr::Cast>(&cursor->data)) {
        if (cast->expr == nullptr || cast->indirections != 0) {
            break;
        }
        cursor = cast->expr.get();
    }
    if (const auto *init = std::get_if<Expr::Init>(&cursor->data)) {
        if (dest.is_array) {
            return const_array(*init, dest);
        }
        if (dest.struct_id < 0) {
            return nullptr;
        }
        return const_struct(*init, dest.struct_id);
    }
    if (const Binding *designator = function_designator(current_file_, *cursor)) {
        auto *function = llvm::dyn_cast_or_null<llvm::Function>(designator->value);
        if (function == nullptr || dest.depth <= 0 || dest.fn_ty == nullptr) {
            return nullptr;
        }
        return function;
    }
    llvm::Type *float_ty = dest.elem_bits >= 64 ? f64_ty() : f32_ty();
    const auto as_float = [&](llvm::Constant *value) -> llvm::Constant * {
        if (!dest.elem_float || value == nullptr) {
            return value;
        }
        if (value->getType() == float_ty) {
            return value;
        }
        if (const auto *fp = llvm::dyn_cast<llvm::ConstantFP>(value)) {
            return llvm::ConstantFP::get(float_ty, fp->getValueAPF().convertToDouble());
        }
        if (const auto *integer = llvm::dyn_cast<llvm::ConstantInt>(value)) {
            return llvm::ConstantFP::get(float_ty, static_cast<double>(integer->getSExtValue()));
        }
        return nullptr;
    };
    if (const auto *literal = std::get_if<Expr::Float>(&cursor->data)) {
        if (!dest.elem_float) {
            return nullptr;
        }
        const std::string number = literal->text.empty() ? "0" : literal->text;
        return llvm::ConstantFP::get(float_ty, std::strtod(number.c_str(), nullptr));
    }
    if (const auto *unary = std::get_if<Expr::Unary>(&cursor->data);
        unary != nullptr && unary->expr != nullptr && dest.elem_float &&
        (unary->op == TokenKind::Plus || unary->op == TokenKind::Minus)) {
        llvm::Constant *inner = const_initializer(*unary->expr, dest);
        if (inner == nullptr || unary->op == TokenKind::Plus) {
            return inner;
        }
        const auto *fp = llvm::dyn_cast<llvm::ConstantFP>(inner);
        if (fp == nullptr) {
            return nullptr;
        }
        llvm::APFloat negated = fp->getValueAPF();
        negated.changeSign();
        return llvm::ConstantFP::get(context_, negated);
    }
    if (std::holds_alternative<Expr::Nullptr>(cursor->data)) {
        return dest.depth > 0 ? null_ptr() : nullptr;
    }
    // `const label *const u8 = "unix"` stores the address of this string.
    if (const auto *text = std::get_if<Expr::String>(&cursor->data)) {
        if (dest.depth != 1 || (dest.elem_bits != 8 && !dest.elem_char)) {
            return nullptr;
        }
        llvm::Constant *data = llvm::ConstantDataArray::getString(context_, text->text, true);
        return new llvm::GlobalVariable(module_, data->getType(), true, llvm::GlobalValue::PrivateLinkage, data, ".str");
    }
    if (const auto *unary = std::get_if<Expr::Unary>(&cursor->data);
        unary != nullptr && unary->op == TokenKind::Amp && unary->expr != nullptr) {
        if (const auto *named = std::get_if<Expr::Name>(&unary->expr->data)) {
            if (const Binding *binding = find_here(named->text)) {
                if (auto *global = llvm::dyn_cast_or_null<llvm::GlobalVariable>(binding->value);
                    global != nullptr && dest.depth > 0) {
                    return global;
                }
            }
        }
        return nullptr;
    }
    if (const auto *selector = std::get_if<Expr::Selector>(&cursor->data); selector != nullptr && selector->base != nullptr) {
        const int enum_id = enum_type_designator(*selector->base);
        if (enum_id >= 0 && enum_id < static_cast<int>(enums_.size())) {
            const auto member = enums_[static_cast<std::size_t>(enum_id)].values.find(selector->field);
            if (member != enums_[static_cast<std::size_t>(enum_id)].values.end()) {
                return sized_int(dest, static_cast<int32_t>(member->second));
            }
        }
    }
    if (const auto *named = std::get_if<Expr::Name>(&cursor->data)) {
        if (const Binding *binding = find_here(named->text)) {
            if (binding->immediate) {
                if (dest.elem_float) {
                    return llvm::ConstantFP::get(float_ty, static_cast<double>(binding->const_value));
                }
                return sized_int(dest, binding->const_value);
            }
            if (auto *global = llvm::dyn_cast_or_null<llvm::GlobalVariable>(binding->value);
                global != nullptr && global->isConstant() && global->hasInitializer()) {
                return as_float(global->getInitializer());
            }
            // `var n = 1; var row = [_]i32{n}` copies n's constant initializer.
            if (const VarDecl *written = global_decl(named->text)) {
                if (!const_names_.insert(named->text).second) {
                    return nullptr;
                }
                llvm::Constant *copied = nullptr;
                if (written->has_const) {
                    copied = dest.elem_float
                                 ? llvm::ConstantFP::get(float_ty, static_cast<double>(written->const_value))
                                 : sized_int(dest, written->const_value);
                } else if (written->init != nullptr) {
                    copied = const_initializer(*written->init, dest);
                } else {
                    llvm::Type *ty = leaf_type(dest.depth, dest.elem_char, dest.struct_id, dest.elem_bool, dest.elem_bits,
                                               dest.elem_float);
                    copied = ty == nullptr ? nullptr : llvm::Constant::getNullValue(ty);
                }
                const_names_.erase(named->text);
                return copied;
            }
        }
    }
    if (dest.elem_float) {
        std::unordered_set<std::string> seen;
        if (const std::optional<FoldedNumber> number = fold_number(*cursor, seen); number && number->is_float) {
            llvm::Type *ty = dest.elem_bits >= 64 ? f64_ty() : f32_ty();
            return llvm::ConstantFP::get(ty, number->value);
        }
        const std::optional<int32_t> number = fold(*cursor);
        if (!number) {
            return nullptr;
        }
        llvm::Type *ty = dest.elem_bits >= 64 ? f64_ty() : f32_ty();
        return llvm::ConstantFP::get(ty, static_cast<double>(*number));
    }
    const std::optional<int32_t> number = fold(*cursor);
    if (!number) {
        return nullptr;
    }
    if (dest.depth > 0) {
        return *number == 0 ? null_ptr() : nullptr;
    }
    return sized_int(dest, *number);
}

llvm::Constant *CodeGen::constant_slice(const Expr &expr, std::unordered_set<std::string> &seen) {
    const auto *name = std::get_if<Expr::Name>(&expr.data);
    if (name == nullptr || !seen.insert(name->text).second) {
        return nullptr;
    }
    const Binding *binding = nullptr;
    if (const auto file = files_.find(current_file_); file != files_.end()) {
        const auto found = file->second.globals.find(name->text);
        if (found != file->second.globals.end()) {
            binding = &found->second;
        }
    }
    if (binding == nullptr) {
        binding = visible(current_file_, name->text, false);
    }
    if (binding == nullptr || binding->value == nullptr) {
        return nullptr;
    }
    auto *global = llvm::dyn_cast<llvm::GlobalVariable>(binding->value);
    if (global == nullptr) {
        return nullptr;
    }
    if (binding->is_array && global->getValueType()->isArrayTy()) {
        auto *array = llvm::cast<llvm::ArrayType>(global->getValueType());
        llvm::Constant *data = llvm::ConstantExpr::getInBoundsGetElementPtr(
            array, global, llvm::ArrayRef<llvm::Constant *>{const_i32(0), const_i32(0)});
        const int len = binding->array_len > 0 ? binding->array_len : static_cast<int>(array->getNumElements());
        return llvm::ConstantStruct::get(slice_type(), {data, const_i32(len)});
    }
    if (!binding->is_slice) {
        return nullptr;
    }
    const VarDecl *source = global_decl(name->text);
    if (source == nullptr || source->init == nullptr) {
        return llvm::ConstantAggregateZero::get(slice_type());
    }
    return constant_slice(*source->init, seen);
}

void CodeGen::apply_initializer(const VarDecl &var) {
    if (!var.owner_params.empty()) {
        return;
    }
    const std::string key = global_key(var);
    FileSymbols &file = files_[current_file_];
    if (var.is_const_decl && var.has_const) {
        file.folded.insert(key);
        return;
    }
    if (var.is_const_decl) {
        const auto found = file.globals.find(key);
        auto *global = found == file.globals.end() ? nullptr : llvm::dyn_cast_or_null<llvm::GlobalVariable>(found->second.value);
        llvm::Constant *value = var.init == nullptr || global == nullptr ? nullptr : const_initializer(*var.init, found->second);
        if (value == nullptr || value->getType() != global->getValueType()) {
            error(var.loc, "global initializer is not a constant");
            return;
        }
        global->setInitializer(value);
        file.folded.insert(key);
        return;
    }
    if (var.is_extern && !var.init) {
        return;
    }
    if (!var.init) {
        file.folded.insert(key);
        return;
    }
    const auto found_early = file.globals.find(key);
    if (const auto *kind = std::get_if<Expr::Typeof>(&var.init->data)) {
        const bool type_slot = found_early != file.globals.end() && found_early->second.depth == 1 &&
                               !found_early->second.elem_char && found_early->second.struct_id < 0 &&
                               !found_early->second.elem_void && !found_early->second.elem_bool &&
                               !found_early->second.elem_float && found_early->second.elem_bits == 0 &&
                               found_early->second.enum_id < 0;
        if (!type_slot || kind->canonical.empty() || found_early->second.value == nullptr) {
            error(var.loc, "type mismatch");
            return;
        }
        auto *global = llvm::cast<llvm::GlobalVariable>(found_early->second.value);
        global->setInitializer(intern_type_name(kind->canonical));
        file.folded.insert(key);
        return;
    }
    if (found_early != file.globals.end() && found_early->second.is_slice) {
        std::unordered_set<std::string> seen;
        llvm::Constant *packed = constant_slice(*var.init, seen);
        auto *global = llvm::dyn_cast_or_null<llvm::GlobalVariable>(found_early->second.value);
        if (packed == nullptr || global == nullptr || packed->getType() != global->getValueType()) {
            error(var.loc, "global initializer is not a constant");
            return;
        }
        global->setInitializer(packed);
        file.folded.insert(key);
        return;
    }
    if (const auto *init = std::get_if<Expr::Init>(&var.init->data)) {
        if (found_early != file.globals.end() && found_early->second.is_array) {
            llvm::Constant *value = const_array(*init, found_early->second);
            if (value == nullptr || found_early->second.value == nullptr) {
                error(var.loc, "global initializer is not a constant");
                return;
            }
            llvm::cast<llvm::GlobalVariable>(found_early->second.value)->setInitializer(value);
            file.folded.insert(key);
            return;
        }
        if (found_early == file.globals.end() || found_early->second.struct_id < 0) {
            error(var.loc, "type mismatch");
            return;
        }
        llvm::Constant *value = const_struct(*init, found_early->second.struct_id);
        if (value == nullptr) {
            error(var.loc, "global initializer is not a constant");
            return;
        }
        auto *global = llvm::cast<llvm::GlobalVariable>(found_early->second.value);
        global->setInitializer(value);
        file.folded.insert(key);
        return;
    }
    if (const Binding *designator = function_designator(current_file_, *var.init)) {
        auto *function = llvm::dyn_cast<llvm::Function>(designator->value);
        if (found_early == file.globals.end() || found_early->second.fn_ty == nullptr ||
            found_early->second.value == nullptr || function == nullptr) {
            error(var.loc, "global initializer is not a constant");
            return;
        }
        llvm::cast<llvm::GlobalVariable>(found_early->second.value)->setInitializer(function);
        file.folded.insert(key);
        return;
    }
    if (found_early != file.globals.end() && found_early->second.elem_float && !found_early->second.is_array &&
        found_early->second.depth == 0) {
        auto *global = llvm::dyn_cast_or_null<llvm::GlobalVariable>(found_early->second.value);
        llvm::Constant *value = global == nullptr ? nullptr : const_initializer(*var.init, found_early->second);
        if (value == nullptr || global == nullptr || value->getType() != global->getValueType()) {
            error(var.loc, "global initializer is not a constant");
            return;
        }
        global->setInitializer(value);
        file.folded.insert(key);
        return;
    }
    const auto value = fold(*var.init);
    const auto found = file.globals.find(key);
    if (!value || found == file.globals.end()) {
        error(var.loc, "global initializer is not a constant");
        return;
    }
    auto *global = llvm::cast<llvm::GlobalVariable>(found->second.value);
    if (var.is_array) {
        error(var.loc, "not supported yet");
        return;
    }
    if (var.is_struct || !var.type_package.empty() || found->second.struct_id >= 0 || found->second.trait_id >= 0 ||
        found->second.elem_float) {
        error(var.loc, "global initializer is not a constant");
        return;
    }
    if (var.indirections > 0 || found->second.depth > 0) {
        if (*value != 0) {
            error(var.loc, "global initializer is not a constant");
            return;
        }
        global->setInitializer(null_ptr());
    } else if (found->second.elem_bool) {
        global->setInitializer(llvm::ConstantInt::get(i1(), *value == 0 ? 0 : 1, false));
    } else if (found->second.elem_bits == 8) {
        global->setInitializer(llvm::ConstantInt::get(i8(), static_cast<uint64_t>(*value) & 0xFFU, false));
    } else if (found->second.elem_bits == 16) {
        global->setInitializer(llvm::ConstantInt::get(i16(), static_cast<uint64_t>(*value) & 0xFFFFU, false));
    } else if (found->second.elem_bits == 64) {
        global->setInitializer(llvm::ConstantInt::get(i64(), static_cast<uint64_t>(static_cast<int64_t>(*value)), true));
    } else {
        global->setInitializer(const_i32(*value));
    }
    file.folded.insert(key);
}

void CodeGen::finish_function() {
    const bool returns_void = return_type_ != nullptr && return_type_->isVoidTy();
    for (llvm::BasicBlock &block : *function_) {
        if (block.hasTerminator()) {
            continue;
        }
        llvm::IRBuilder<> fix(&block);
        stamp_builder(fix);
        if (extern_sret_ && extern_sret_ptr_ != nullptr && extern_ret_struct_ != nullptr) {
            fix.CreateStore(llvm::ConstantAggregateZero::get(extern_ret_struct_), extern_sret_ptr_);
            fix.CreateRetVoid();
        } else if (returns_void) {
            fix.CreateRetVoid();
        } else if (return_type_ == slice_type()) {
            fix.CreateRet(llvm::ConstantAggregateZero::get(slice_type()));
        } else if (return_depth_ > 0) {
            fix.CreateRet(null_ptr());
        } else if (return_type_ != nullptr && return_type_->isIntegerTy(32)) {
            fix.CreateRet(const_i32(0));
        } else if (return_type_ != nullptr) {
            fix.CreateRet(llvm::Constant::getNullValue(return_type_));
        } else {
            fix.CreateRet(const_i32(0));
        }
    }
}

bool CodeGen::param_is_trait(uint32_t file, const Param &param) const {
    if (param.is_struct || param.is_enum || param.is_array || !param.type_package.empty() || param.indirections > 1) {
        return false;
    }
    if (has_typedef(file, param.type_name) || tag_id(file, param.type_name) >= 0 ||
        selective_tag(file, param.type_name) >= 0) {
        return false;
    }
    return trait_named(file, param.type_name) >= 0;
}

bool CodeGen::function_is_generic(uint32_t file, const FunctionDecl &fn) const {
    if (!fn.type_params.empty()) {
        return true;
    }
    for (const Param &param : fn.params) {
        if (param_is_trait(file, param)) {
            return true;
        }
    }
    return false;
}

// A trait argument that is a function pointer. -1 still means "not a trait parameter".
constexpr int kFnSpec = -2;

bool trait_fn_argument(const TypedValue &arg) {
    if (arg.value != nullptr && llvm::isa<llvm::Function>(arg.value)) {
        return true;
    }
    return arg.fn_ty != nullptr && arg.struct_id < 0;
}

CodeGen::Lowered CodeGen::trait_fn_lowered(const std::string &trait_name) const {
    const int id = trait_named(current_file_, trait_name);
    if (id < 0 || id >= static_cast<int>(trait_decls_.size()) || trait_decls_[static_cast<std::size_t>(id)] == nullptr ||
        trait_decls_[static_cast<std::size_t>(id)]->methods.size() != 1) {
        return {};
    }
    const FunctionDecl &method = trait_decls_[static_cast<std::size_t>(id)]->methods.front();
    Lowered out;
    out.depth = 1;
    out.type = ptr_type();
    out.fn_ty = function_type(method, current_file_);
    const bool many = method.results.size() > 1;
    const bool returns_void = !many && method.return_type == "void" && method.indirections == 0 && !method.is_struct &&
                              method.type_package.empty();
    if (!returns_void && !many) {
        const bool result_array = method.results.size() == 1 && method.results.front().is_array &&
                                  method.results.front().array_len > 0 && !method.results.front().is_fn_ptr;
        const Lowered ret =
            method.results.size() == 1 && method.results.front().is_fn_ptr
                ? lower_fnptr(current_file_, method.results.front().type_name, method.results.front().type_package,
                              method.results.front().is_struct, method.results.front().is_enum,
                              method.results.front().ret_indirections, method.results.front().indirections,
                              method.results.front().fn_params, method.results.front().fn_variadic,
                              method.results.front().fn_results)
                : lower(current_file_, method.return_type, method.type_package, method.is_struct, method.indirections,
                        result_array, result_array ? method.results.front().array_len : 0, false);
        out.fn_ret_depth = ret.depth;
        out.fn_ret_float = ret.elem_float;
        out.fn_ret_bits = ret.elem_bits;
        out.fn_ret_unsigned = ret.elem_unsigned;
        out.fn_ret_char = ret.elem_char;
        out.fn_ret_bool = ret.elem_bool;
        out.fn_ret_struct = ret.struct_id;
    }
    return out;
}

CodeGen::Lowered CodeGen::spec_param(const FunctionDecl &fn, std::size_t index, const std::vector<int> &spec) const {
    const Param &param = fn.params[index];
    if (index < spec.size() && spec[index] == kFnSpec) {
        // `Shape` and `*Shape` are both this one function pointer. The call is `s.method(...)`.
        return trait_fn_lowered(param.type_name);
    }
    if (index < spec.size() && spec[index] >= 0 && spec[index] < static_cast<int>(structs_.size())) {
        return lower(current_file_, structs_[static_cast<std::size_t>(spec[index])].name, "", true, param.indirections,
                     false, 0, true);
    }
    return lower_param_type(current_file_, param);
}

CodeGen::Lowered CodeGen::lower_param(const FunctionDecl &fn, std::size_t index) const {
    return spec_param(fn, index, active_spec_);
}

llvm::FunctionType *CodeGen::spec_type(const FunctionDecl &fn, const std::vector<int> &spec) const {
    const bool many = fn.results.size() > 1;
    const bool returns_void =
        !many && fn.return_type == "void" && fn.indirections == 0 && !fn.is_struct && fn.type_package.empty();
    const bool result_slice = !many && fn.results.size() == 1 && fn.results.front().is_array &&
                             fn.results.front().array_len < 0 && !fn.results.front().is_fn_ptr;
    llvm::Type *ret = many ? aggregate_return(fn, current_file_)
                           : (returns_void ? llvm::Type::getVoidTy(context_)
                                           : stored_type(lower(current_file_, fn.return_type, fn.type_package, fn.is_struct,
                                                               fn.indirections, result_slice,
                                                               result_slice ? fn.results.front().array_len : 0, false)));
    std::vector<llvm::Type *> params;
    params.reserve(fn.params.size());
    for (std::size_t index = 0; index < fn.params.size(); ++index) {
        params.push_back(stored_type(spec_param(fn, index, spec)));
    }
    return llvm::FunctionType::get(ret, params, false);
}

void CodeGen::emit_function_body(const FunctionDecl &fn, llvm::Function *function, int return_depth,
                                 const std::vector<Binding> *captures) {
    if (function == nullptr || !fn.body || !function->empty()) {
        return;
    }
    const FunctionDecl *saved_emitting = emitting_fn_;
    emitting_fn_ = &fn;
    std::vector<llvm::DIScope *> saved_scopes = std::move(di_scopes_);
    di_scopes_.clear();
    llvm::DISubprogram *saved_subprogram = di_subprogram_;
    const llvm::DebugLoc saved_loc = builder_.getCurrentDebugLocation();
    builder_.SetCurrentDebugLocation(llvm::DebugLoc());
    const std::string saved_trace = trace_name_;
    trace_name_ = fn.name.empty() ? std::string("anon") : fn.name;
    const bool saved_many = return_many_;
    const bool saved_share = share_function_scope_;
    const bool saved_extern = extern_struct_ret_;
    const bool saved_sret = extern_sret_;
    llvm::Value *saved_sret_ptr = extern_sret_ptr_;
    const CAbi saved_abi = extern_ret_abi_;
    llvm::Type *saved_struct = extern_ret_struct_;
    function_ = function;
    return_type_ = function->getReturnType();
    return_depth_ = return_depth;
    return_many_ = fn.results.size() > 1;
    share_function_scope_ = true;
    extern_struct_ret_ = false;
    extern_sret_ = false;
    extern_sret_ptr_ = nullptr;
    extern_ret_abi_ = {};
    extern_ret_struct_ = nullptr;
    const auto abi_found = extern_abis_.find(function);
    const bool abi_body = abi_found != extern_abis_.end() && abi_found->second.active;
    const ExternLayout abi_layout = abi_body ? abi_found->second : ExternLayout{};
    if (abi_body && abi_layout.ret_struct != nullptr) {
        extern_struct_ret_ = true;
        extern_sret_ = abi_layout.sret;
        extern_ret_abi_ = abi_layout.ret;
        extern_ret_struct_ = abi_layout.ret_struct;
    }
    llvm::BasicBlock *entry = llvm::BasicBlock::Create(context_, "entry", function);
    builder_.SetInsertPoint(entry);
    debug_begin_function(fn, function);
    push();
    if (abi_body) {
        const ExternLayout &layout = abi_layout;
        unsigned abi = 0;
        if (layout.sret && abi < function->arg_size()) {
            extern_sret_ptr_ = function->getArg(abi++);
        }
        for (std::size_t index = 0; index < fn.params.size(); ++index) {
            const std::string name = fn.params[index].name;
            const ExternLayout::Arg &info = layout.args[index];
            const Lowered low = lower_param(fn, index);
            if (info.is_struct && info.abi.memory) {
                llvm::Argument *arg = function->getArg(abi++);
                if (!name.empty()) {
                    Binding binding;
                    binding.value = arg;
                    apply_low(binding, low);
                    scopes_.back()[name] = binding;
                    alias_capture(name, binding);
                }
            } else if (info.is_struct) {
                const unsigned align = std::max(info.abi.natural_align, 8u);
                llvm::AllocaInst *slot = aligned_slot(info.struct_type, align, name.empty() ? "arg" : name);
                for (std::size_t part = 0; part < info.abi.parts.size() && abi < function->arg_size(); ++part) {
                    const uint64_t offset = part < info.abi.offsets.size() ? info.abi.offsets[part] : 0;
                    store_at(slot, function->getArg(abi++), offset);
                }
                if (!name.empty()) {
                    Binding binding;
                    binding.value = slot;
                    apply_low(binding, low);
                    scopes_.back()[name] = binding;
                    alias_capture(name, binding);
                    debug_declare(name, fn.params[index].loc, binding, static_cast<unsigned>(index + 1));
                }
            } else if (abi < function->arg_size()) {
                llvm::Argument *arg = function->getArg(abi++);
                if (!name.empty()) {
                    llvm::AllocaInst *slot = alloca_typed(stored_type(low), name);
                    builder_.CreateStore(arg, slot);
                    Binding binding;
                    binding.value = slot;
                    apply_low(binding, low);
                    scopes_.back()[name] = binding;
                    alias_capture(name, binding);
                    debug_declare(name, fn.params[index].loc, binding, static_cast<unsigned>(index + 1));
                }
            }
        }
    } else {
        unsigned index = 0;
        for (auto &arg : function->args()) {
            const std::string name = index < fn.params.size() ? fn.params[index].name : "";
            if (!name.empty()) {
                const Lowered low = lower_param(fn, index);
                llvm::AllocaInst *slot = alloca_typed(stored_type(low), name);
                builder_.CreateStore(&arg, slot);
                Binding binding;
                binding.value = slot;
                apply_low(binding, low);
                scopes_.back()[name] = binding;
                alias_capture(name, binding);
                debug_declare(name, fn.params[index].loc, binding, static_cast<unsigned>(index + 1));
            }
            ++index;
        }
    }
    if (captures != nullptr) {
        const unsigned base = static_cast<unsigned>(fn.params.size());
        for (std::size_t index = 0; index < fn.captures.size() && index < captures->size(); ++index) {
            const unsigned arg_index = base + static_cast<unsigned>(index);
            if (arg_index >= function->arg_size()) {
                break;
            }
            const FnCapture &item = fn.captures[index];
            llvm::Argument *arg = function->getArg(arg_index);
            if (!item.name.empty()) {
                arg->setName(item.name);
            }
            llvm::AllocaInst *slot = alloca_typed(ptr_type(), item.name.empty() ? "cap" : item.name);
            builder_.CreateStore(arg, slot);
            Binding binding = (*captures)[index];
            binding.value = slot;
            binding.is_function = false;
            binding.immediate = false;
            binding.is_array = false;
            binding.is_tuple = false;
            binding.decl = nullptr;
            if (binding.depth < 1) {
                binding.depth = 1;
            }
            if (!item.name.empty()) {
                scopes_.back()[item.name] = binding;
                scopes_.back()[capture_key(item.function_id, item.name)] = binding;
            }
        }
    }
    emit_stmt(*fn.body);
    finish_function();
    pop();
    debug_end_function();
    di_scopes_ = std::move(saved_scopes);
    di_subprogram_ = saved_subprogram;
    builder_.SetCurrentDebugLocation(saved_loc);
    function_ = nullptr;
    return_type_ = nullptr;
    return_depth_ = 0;
    return_many_ = saved_many;
    share_function_scope_ = saved_share;
    extern_struct_ret_ = saved_extern;
    extern_sret_ = saved_sret;
    extern_sret_ptr_ = saved_sret_ptr;
    extern_ret_abi_ = saved_abi;
    extern_ret_struct_ = saved_struct;
    trace_name_ = saved_trace;
    emitting_fn_ = saved_emitting;
}

void CodeGen::emit_function(const FunctionDecl &fn) {
    const std::string key = function_key(fn);
    const auto found = files_.at(current_file_).functions.find(key);
    if (found == files_.at(current_file_).functions.end() || found->second.is_generic || found->second.value == nullptr) {
        return;
    }
    emit_function_body(fn, llvm::cast<llvm::Function>(found->second.value), found->second.depth);
}

llvm::FunctionType *CodeGen::function_type_with_captures(const FunctionDecl &fn) const {
    llvm::FunctionType *base = function_type(fn, current_file_);
    if (fn.captures.empty() || base == nullptr) {
        return base;
    }
    std::vector<llvm::Type *> params;
    params.reserve(base->getNumParams() + fn.captures.size());
    for (unsigned index = 0; index < base->getNumParams(); ++index) {
        params.push_back(base->getParamType(index));
    }
    for (std::size_t index = 0; index < fn.captures.size(); ++index) {
        params.push_back(ptr_type());
    }
    return llvm::FunctionType::get(base->getReturnType(), params, base->isVarArg());
}

std::vector<Binding> CodeGen::capture_bindings(const FunctionDecl &fn) const {
    std::vector<Binding> out;
    out.reserve(fn.captures.size());
    for (const FnCapture &item : fn.captures) {
        const Binding *found = lookup(capture_key(item.function_id, item.name));
        if (found == nullptr) {
            found = lookup(item.name);
        }
        out.push_back(found == nullptr ? Binding{} : *found);
    }
    return out;
}

void CodeGen::alias_capture(const std::string &name, const Binding &binding) {
    if (emitting_fn_ == nullptr || scopes_.empty() || name.empty() || emitting_fn_->function_id == 0) {
        return;
    }
    bool owned = false;
    for (const std::string &kept : emitting_fn_->captured_here) {
        if (kept == name) {
            owned = true;
            break;
        }
    }
    if (!owned) {
        return;
    }
    const std::string slot = capture_key(emitting_fn_->function_id, name);
    if (lookup(slot) != nullptr) {
        return;
    }
    scopes_.back()[slot] = binding;
}

void CodeGen::append_captures(const FunctionDecl *decl, std::vector<llvm::Value *> &args, SourceLoc loc) {
    if (decl == nullptr) {
        return;
    }
    for (const FnCapture &item : decl->captures) {
        const Binding *binding = lookup(capture_key(item.function_id, item.name));
        if (binding == nullptr) {
            binding = lookup(item.name);
        }
        if (binding == nullptr || binding->value == nullptr) {
            error(loc, "not supported yet");
            args.push_back(null_ptr());
            continue;
        }
        const TypedValue value = value_of_binding(*binding);
        if (value.value == nullptr || !value.value->getType()->isPointerTy()) {
            error(loc, "not supported yet");
            args.push_back(null_ptr());
            continue;
        }
        args.push_back(value.value);
    }
}

void CodeGen::emit_local_function(const FunctionDecl &fn) {
    if (fn.name.empty() || !fn.method_struct.empty() || scopes_.empty()) {
        return;
    }
    if (function_is_generic(current_file_, fn)) {
        error(fn.loc, "not supported yet");
        return;
    }
    llvm::Function *function = nullptr;
    const auto found = scopes_.back().find(fn.name);
    if (found != scopes_.back().end()) {
        if (!found->second.is_function) {
            return;
        }
        function = llvm::dyn_cast_or_null<llvm::Function>(found->second.value);
        found->second.decl = &fn;
    }
    const bool many = fn.results.size() > 1;
    const bool returns_void =
        !many && fn.return_type == "void" && fn.indirections == 0 && !fn.is_struct && fn.type_package.empty();
    const Lowered ret = returns_void || many
                            ? Lowered{}
                            : lower(current_file_, fn.return_type, fn.type_package, fn.is_struct, fn.indirections, false, 0,
                                    false);
    if (function == nullptr) {
        const PackageNode *owner = find_node(current_file_);
        const std::string qualifier = owner != nullptr && !owner->qualifier.empty() ? owner->qualifier : "local";
        const std::string symbol = qualifier + ".local." + fn.name + "." + std::to_string(++local_seq_);
        function = llvm::Function::Create(function_type_with_captures(fn), llvm::GlobalValue::InternalLinkage, symbol,
                                           module_);
        function->setCallingConv(llvm::CallingConv::C);
        unsigned index = 0;
        for (auto &arg : function->args()) {
            if (index < fn.params.size() && !fn.params[index].name.empty()) {
                arg.setName(fn.params[index].name);
            }
            ++index;
        }
        Binding binding;
        binding.value = function;
        binding.depth = returns_void ? 0 : ret.depth;
        binding.is_function = true;
        binding.elem_char = !returns_void && ret.elem_char;
        binding.struct_id = returns_void ? -1 : ret.struct_id;
        binding.elem_void = !returns_void && ret.elem_void;
        binding.elem_bool = !returns_void && ret.elem_bool;
        binding.elem_bits = returns_void ? 0 : ret.elem_bits;
        binding.elem_unsigned = !returns_void && ret.elem_unsigned;
        binding.elem_float = !returns_void && ret.elem_float;
        binding.trait_id = returns_void ? -1 : ret.trait_id;
        binding.enum_id = returns_void ? -1 : ret.enum_id;
        binding.decl = &fn;
        scopes_.back()[fn.name] = binding;
    }
    if (function != nullptr && !fn.captures.empty() &&
        function->arg_size() != fn.params.size() + fn.captures.size()) {
        error(fn.loc, "not supported yet");
        return;
    }
    emit_nested_body(fn, function, returns_void ? 0 : ret.depth, capture_bindings(fn));
}

void CodeGen::emit_nested_body(const FunctionDecl &fn, llvm::Function *function, int depth,
                               std::vector<Binding> captures) {
    if (fn.body == nullptr || function == nullptr || !function->empty()) {
        return;
    }
    std::vector<std::unordered_map<std::string, Binding>> kept;
    for (const auto &scope : scopes_) {
        std::unordered_map<std::string, Binding> filtered;
        for (const auto &[name, binding] : scope) {
            if (binding.is_function || llvm::isa_and_nonnull<llvm::GlobalVariable>(binding.value)) {
                filtered.emplace(name, binding);
            }
        }
        if (!filtered.empty()) {
            kept.push_back(std::move(filtered));
        }
    }
    auto saved_scopes = std::move(scopes_);
    llvm::Function *saved_function = function_;
    llvm::Type *saved_return = return_type_;
    const int saved_depth = return_depth_;
    auto saved_loops = std::move(loops_);
    auto saved_defers = std::move(defers_);
    auto saved_cases = std::move(case_blocks_);
    const llvm::IRBuilderBase::InsertPoint saved_ip = builder_.saveIP();
    scopes_ = std::move(kept);
    loops_.clear();
    defers_.clear();
    case_blocks_.clear();
    auto saved_spec = active_spec_;
    active_spec_.clear();
    emit_function_body(fn, function, depth, captures.empty() ? nullptr : &captures);
    active_spec_ = std::move(saved_spec);
    scopes_ = std::move(saved_scopes);
    function_ = saved_function;
    return_type_ = saved_return;
    return_depth_ = saved_depth;
    loops_ = std::move(saved_loops);
    defers_ = std::move(saved_defers);
    case_blocks_ = std::move(saved_cases);
    builder_.restoreIP(saved_ip);
}

TypedValue CodeGen::emit_anon_function(const FunctionDecl &fn) {
    if (!fn.method_struct.empty() || fn.variadic || function_is_generic(current_file_, fn) || fn.body == nullptr) {
        error(fn.loc, "not supported yet");
        return {zero(), 0};
    }
    const bool many = fn.results.size() > 1;
    const bool returns_void =
        !many && fn.return_type == "void" && fn.indirections == 0 && !fn.is_struct && fn.type_package.empty();
    const Lowered ret = returns_void || many
                            ? Lowered{}
                            : lower(current_file_, fn.return_type, fn.type_package, fn.is_struct, fn.indirections, false, 0,
                                    false);
    const PackageNode *owner = find_node(current_file_);
    const std::string qualifier = owner != nullptr && !owner->qualifier.empty() ? owner->qualifier : "anon";
    const std::string symbol = qualifier + ".anon." + std::to_string(++local_seq_);
    llvm::Function *function = llvm::Function::Create(function_type_with_captures(fn), llvm::GlobalValue::InternalLinkage,
                                                       symbol, module_);
    function->setCallingConv(llvm::CallingConv::C);
    unsigned index = 0;
    for (auto &arg : function->args()) {
        if (index < fn.params.size() && !fn.params[index].name.empty()) {
            arg.setName(fn.params[index].name);
        }
        ++index;
    }
    emit_nested_body(fn, function, returns_void ? 0 : ret.depth, capture_bindings(fn));
    return {function,
            returns_void ? 0 : ret.depth,
            !returns_void && ret.elem_char,
            returns_void ? -1 : ret.struct_id,
            false,
            !returns_void && ret.elem_void,
            !returns_void && ret.elem_bool,
            returns_void ? 0 : ret.elem_bits,
            !returns_void && ret.elem_unsigned};
}

CodeGen::PreparedCall CodeGen::prepare_call(const Expr::Call &node) {
    PreparedCall prepared;
    prepared.loc = node.loc;
    if (node.callee == nullptr) {
        error(node.loc, "type mismatch");
        return prepared;
    }
    prepared.trace = !is_std_builtin_call(*node.callee, "trace") && !is_std_builtin_call(*node.callee, "va");
    auto remember = [&](llvm::Function *function, std::vector<llvm::Value *> args, int depth, bool elem_char, int struct_id,
                        bool elem_void, bool elem_bool, int elem_bits, bool elem_unsigned) {
        prepared.function = function;
        prepared.args = std::move(args);
        prepared.depth = depth;
        prepared.elem_char = elem_char;
        prepared.struct_id = struct_id;
        prepared.elem_void = elem_void;
        prepared.elem_bool = elem_bool;
        prepared.elem_bits = elem_bits;
        prepared.elem_unsigned = elem_unsigned;
    };
    if (const auto *selector = std::get_if<Expr::Selector>(&node.callee->data);
        selector != nullptr && selector->base != nullptr) {
        const auto *base_name = std::get_if<Expr::Name>(&selector->base->data);
        const bool package_call = base_name != nullptr && packages_.contains(base_name->text);
        if (!package_call) {
            // `Box[i32].from(...)`. The owner is the specialization, and the call passes no receiver.
            if (!selector->resolved_owner.empty()) {
                const Binding *method = static_method_binding(current_file_, selector->resolved_owner, selector->field);
                if (method != nullptr && method->decl != nullptr && method->decl->static_method) {
                    auto *function = llvm::dyn_cast<llvm::Function>(method->value);
                    if (function == nullptr) {
                        error(node.loc, "type mismatch");
                        return prepared;
                    }
                    std::vector<llvm::Value *> args;
                    args.reserve(node.args.size());
                    unsigned index = 0;
                    for (const auto &arg : node.args) {
                        const TypedValue value = emit_rvalue(*arg);
                        llvm::Type *param = index < function->getFunctionType()->getNumParams()
                                                ? function->getFunctionType()->getParamType(index)
                                                : nullptr;
                        args.push_back(adapt(value, param, node.loc));
                        ++index;
                    }
                    remember(function, std::move(args), method->depth, method->elem_char, method->struct_id,
                             method->elem_void, method->elem_bool, method->elem_bits, method->elem_unsigned);
                    prepared.decl = method->decl;
                    prepared.elem_float = method->elem_float;
                    prepared.enum_id = method->enum_id;
                    return prepared;
                }
            }
            bool value_named = false;
            if (base_name != nullptr) {
                for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
                    const auto found = scope->find(base_name->text);
                    if (found == scope->end()) {
                        continue;
                    }
                    value_named = !found->second.is_function;
                    break;
                }
            }
            std::string static_type;
            if (base_name != nullptr && !value_named && !packages_.contains(base_name->text)) {
                for (const StructLayout &item : structs_) {
                    if (item.name == base_name->text) {
                        static_type = item.name;
                        break;
                    }
                }
                if (static_type.empty()) {
                    for (const EnumLayout &item : enums_) {
                        if (item.name == base_name->text) {
                            static_type = item.name;
                            break;
                        }
                    }
                }
                if (static_type.empty() && find_alias(current_file_, base_name->text) != nullptr) {
                    static_type = base_name->text;
                }
            }
            if (!static_type.empty()) {
                const Binding *method = static_method_binding(current_file_, static_type, selector->field);
                if (method != nullptr && method->is_generic && method->decl != nullptr) {
                    generic_method_call(*method, nullptr, node, prepared);
                    return prepared;
                }
                auto *function = method == nullptr ? nullptr : llvm::dyn_cast<llvm::Function>(method->value);
                if (function == nullptr) {
                    error(node.loc, "type mismatch");
                    return prepared;
                }
                std::vector<llvm::Value *> args;
                args.reserve(node.args.size());
                unsigned index = 0;
                for (const auto &arg : node.args) {
                    const TypedValue value = emit_rvalue(*arg);
                    llvm::Type *param = index < function->getFunctionType()->getNumParams()
                                            ? function->getFunctionType()->getParamType(index)
                                            : nullptr;
                    args.push_back(adapt(value, param, node.loc));
                    ++index;
                }
                remember(function, std::move(args), method->depth, method->elem_char, method->struct_id, method->elem_void,
                         method->elem_bool, method->elem_bits, method->elem_unsigned);
                prepared.decl = method->decl;
                prepared.elem_float = method->elem_float;
                prepared.enum_id = method->enum_id;
                return prepared;
            }
            const int receiver_enum = enum_value_id(*selector->base);
            if (receiver_enum >= 0 && receiver_enum < static_cast<int>(enums_.size())) {
                const std::string key = enums_[static_cast<std::size_t>(receiver_enum)].name + "." + selector->field;
                const Binding *method = nullptr;
                for (const uint32_t id : package_files(current_file_)) {
                    const auto file = files_.find(id);
                    if (file == files_.end()) {
                        continue;
                    }
                    const auto found = file->second.functions.find(key);
                    if (found == file->second.functions.end() || !found->second.is_function) {
                        continue;
                    }
                    if (found->second.is_static && id != current_file_) {
                        continue;
                    }
                    method = &found->second;
                    break;
                }
                if (method != nullptr && method->is_generic && method->decl != nullptr && !method->decl->static_method) {
                    const bool receiver_pointer =
                        !method->decl->params.empty() && method->decl->params.front().indirections > 0;
                    TypedValue receiver;
                    if (receiver_pointer) {
                        if (!addressable(*selector->base)) {
                            error(node.loc, "not an lvalue");
                            return prepared;
                        }
                        const TypedValue addr = emit_addr(*selector->base);
                        receiver.value = addr.value;
                        receiver.depth = 0;
                    } else {
                        receiver = emit_rvalue(*selector->base);
                    }
                    if (receiver.value == nullptr) {
                        error(node.loc, "type mismatch");
                        return prepared;
                    }
                    generic_method_call(*method, &receiver, node, prepared);
                    return prepared;
                }
                auto *function = method == nullptr ? nullptr : llvm::dyn_cast<llvm::Function>(method->value);
                if (function == nullptr || (method->decl != nullptr && method->decl->static_method)) {
                    error(node.loc, "type mismatch");
                    return prepared;
                }
                const bool receiver_pointer = function->getFunctionType()->getNumParams() > 0 &&
                                              function->getFunctionType()->getParamType(0)->isPointerTy();
                llvm::Value *receiver_arg = nullptr;
                if (receiver_pointer) {
                    if (!addressable(*selector->base)) {
                        error(node.loc, "not an lvalue");
                        return prepared;
                    }
                    const TypedValue addr = emit_addr(*selector->base);
                    receiver_arg = addr.value;
                } else {
                    const TypedValue value = emit_rvalue(*selector->base);
                    llvm::Type *param = function->getFunctionType()->getNumParams() > 0
                                            ? function->getFunctionType()->getParamType(0)
                                            : nullptr;
                    receiver_arg = as_stored(value.value, value.depth, param, value.elem_unsigned || value.elem_char);
                }
                if (receiver_arg == nullptr) {
                    error(node.loc, "type mismatch");
                    return prepared;
                }
                std::vector<llvm::Value *> args;
                args.reserve(node.args.size() + 1);
                args.push_back(receiver_arg);
                unsigned index = 1;
                for (const auto &arg : node.args) {
                    const TypedValue value = emit_rvalue(*arg);
                    llvm::Type *param = index < function->getFunctionType()->getNumParams()
                                            ? function->getFunctionType()->getParamType(index)
                                            : nullptr;
                    args.push_back(adapt(value, param, node.loc));
                    ++index;
                }
                remember(function, std::move(args), method->depth, method->elem_char, method->struct_id, method->elem_void,
                         method->elem_bool, method->elem_bits, method->elem_unsigned);
                prepared.decl = method->decl;
                prepared.elem_float = method->elem_float;
                prepared.enum_id = method->enum_id;
                return prepared;
            }
            if (!selector->resolved_owner.empty()) {
                const std::string key = selector->resolved_owner + "." + selector->field;
                const Binding *method = nullptr;
                for (const uint32_t id : package_files(current_file_)) {
                    const auto file = files_.find(id);
                    if (file == files_.end()) {
                        continue;
                    }
                    const auto found = file->second.functions.find(key);
                    if (found == file->second.functions.end() || !found->second.is_function) {
                        continue;
                    }
                    if (found->second.is_static && id != current_file_) {
                        continue;
                    }
                    method = &found->second;
                    break;
                }
                auto *function = method == nullptr ? nullptr : llvm::dyn_cast<llvm::Function>(method->value);
                if (function == nullptr || (method->decl != nullptr && method->decl->static_method)) {
                    error(node.loc, "type mismatch");
                    return prepared;
                }
                const bool receiver_pointer = function->getFunctionType()->getNumParams() > 0 &&
                                              function->getFunctionType()->getParamType(0)->isPointerTy();
                llvm::Value *receiver_arg = nullptr;
                if (receiver_pointer) {
                    if (!addressable(*selector->base)) {
                        error(node.loc, "not an lvalue");
                        return prepared;
                    }
                    // `var n Integer` is depth 0: the alloca is the integer.
                    // `var p *Integer` is depth 1: the alloca holds the pointer, so load it.
                    const TypedValue addr = emit_addr(*selector->base);
                    if (addr.value == nullptr) {
                        return prepared;
                    }
                    receiver_arg = addr.depth > 0 ? builder_.CreateLoad(ptr_type(), addr.value) : addr.value;
                } else {
                    llvm::Type *param = function->getFunctionType()->getNumParams() > 0
                                            ? function->getFunctionType()->getParamType(0)
                                            : nullptr;
                    if (addressable(*selector->base)) {
                        const TypedValue addr = emit_addr(*selector->base);
                        if (addr.value == nullptr) {
                            return prepared;
                        }
                        llvm::Value *slot = addr.value;
                        if (addr.depth > 0) {
                            slot = builder_.CreateLoad(ptr_type(), addr.value);
                        }
                        receiver_arg = builder_.CreateLoad(param, slot);
                    } else {
                        const TypedValue value = emit_rvalue(*selector->base);
                        receiver_arg = as_stored(value.value, value.depth, param, value.elem_unsigned || value.elem_char);
                    }
                }
                if (receiver_arg == nullptr) {
                    error(node.loc, "type mismatch");
                    return prepared;
                }
                std::vector<llvm::Value *> args;
                args.reserve(node.args.size() + 1);
                args.push_back(receiver_arg);
                unsigned index = 1;
                for (const auto &arg : node.args) {
                    const TypedValue value = emit_rvalue(*arg);
                    llvm::Type *param = index < function->getFunctionType()->getNumParams()
                                            ? function->getFunctionType()->getParamType(index)
                                            : nullptr;
                    args.push_back(adapt(value, param, node.loc));
                    ++index;
                }
                remember(function, std::move(args), method->depth, method->elem_char, method->struct_id, method->elem_void,
                         method->elem_bool, method->elem_bits, method->elem_unsigned);
                prepared.decl = method->decl;
                prepared.elem_float = method->elem_float;
                prepared.enum_id = method->enum_id;
                return prepared;
            }
            int struct_id = -1;
            llvm::Value *struct_ptr = nullptr;
            llvm::Value *struct_value = nullptr;
            llvm::Value *pointer_value = nullptr;
            if (addressable(*selector->base)) {
                const TypedValue addr = emit_addr(*selector->base);
                if (addr.value == nullptr) {
                    return prepared;
                }
                if (addr.fn_ty != nullptr && addr.struct_id < 0) {
                    call_trait_fn(builder_.CreateLoad(ptr_type(), addr.value), addr, node, prepared);
                    return prepared;
                }
                struct_id = addr.struct_id;
                if (addr.depth == 0 && addr.struct_id >= 0) {
                    struct_ptr = addr.value;
                } else if (addr.depth == 1 && addr.struct_id >= 0) {
                    pointer_value = builder_.CreateLoad(ptr_type(), addr.value);
                }
            } else {
                const TypedValue base = emit_rvalue(*selector->base);
                if (base.value == nullptr) {
                    return prepared;
                }
                if (base.fn_ty != nullptr && base.struct_id < 0) {
                    call_trait_fn(base.value, base, node, prepared);
                    return prepared;
                }
                struct_id = base.struct_id;
                if (base.depth == 0 && base.struct_id >= 0) {
                    struct_value = base.value;
                } else if (base.depth == 1 && base.struct_id >= 0) {
                    pointer_value = base.value;
                }
            }
            const Binding *method = nullptr;
            if (struct_id >= 0 && struct_id < static_cast<int>(structs_.size())) {
                const std::string key = structs_[static_cast<std::size_t>(struct_id)].name + "." + selector->field;
                for (const uint32_t id : package_files(current_file_)) {
                    const auto file = files_.find(id);
                    if (file == files_.end()) {
                        continue;
                    }
                    const auto found = file->second.functions.find(key);
                    if (found == file->second.functions.end() || !found->second.is_function) {
                        continue;
                    }
                    if (found->second.is_static && id != current_file_) {
                        continue;
                    }
                    method = &found->second;
                    break;
                }
            }
            if (method != nullptr && method->is_generic && method->decl != nullptr && !method->decl->static_method) {
                const bool receiver_pointer =
                    !method->decl->params.empty() && method->decl->params.front().indirections > 0;
                TypedValue receiver;
                if (receiver_pointer) {
                    receiver.value = pointer_value != nullptr ? pointer_value : struct_ptr;
                    receiver.depth = 0;
                } else if (struct_value != nullptr) {
                    receiver.value = struct_value;
                    receiver.depth = 0;
                    receiver.struct_id = struct_id;
                } else if (struct_id >= 0 && struct_id < static_cast<int>(structs_.size())) {
                    llvm::Value *ptr = pointer_value != nullptr ? pointer_value : struct_ptr;
                    if (ptr != nullptr) {
                        receiver.value = builder_.CreateLoad(structs_[static_cast<std::size_t>(struct_id)].type, ptr);
                        receiver.depth = 0;
                        receiver.struct_id = struct_id;
                    }
                }
                if (receiver.value == nullptr) {
                    error(node.loc, "type mismatch");
                    return prepared;
                }
                generic_method_call(*method, &receiver, node, prepared);
                return prepared;
            }
            auto *function = method == nullptr ? nullptr : llvm::dyn_cast<llvm::Function>(method->value);
            if (function == nullptr || (method->decl != nullptr && method->decl->static_method)) {
                error(node.loc, "type mismatch");
                return prepared;
            }
            const bool receiver_pointer = function->getFunctionType()->getNumParams() > 0 &&
                                          function->getFunctionType()->getParamType(0)->isPointerTy();
            llvm::Value *receiver_arg = nullptr;
            if (receiver_pointer) {
                if (pointer_value != nullptr) {
                    receiver_arg = pointer_value;
                } else if (struct_ptr != nullptr) {
                    receiver_arg = struct_ptr;
                }
            } else if (struct_value != nullptr) {
                receiver_arg = struct_value;
            } else if (pointer_value != nullptr) {
                receiver_arg = builder_.CreateLoad(structs_[static_cast<std::size_t>(struct_id)].type, pointer_value);
            } else if (struct_ptr != nullptr) {
                receiver_arg = builder_.CreateLoad(structs_[static_cast<std::size_t>(struct_id)].type, struct_ptr);
            }
            if (receiver_arg == nullptr) {
                error(node.loc, "type mismatch");
                return prepared;
            }
            std::vector<llvm::Value *> args;
            args.reserve(node.args.size() + 1);
            args.push_back(receiver_arg);
            unsigned index = 1;
            for (const auto &arg : node.args) {
                const TypedValue value = emit_rvalue(*arg);
                llvm::Type *param = index < function->getFunctionType()->getNumParams()
                                        ? function->getFunctionType()->getParamType(index)
                                        : nullptr;
                args.push_back(adapt(value, param, node.loc));
                ++index;
            }
            remember(function, std::move(args), method->depth, method->elem_char, method->struct_id, method->elem_void,
                     method->elem_bool, method->elem_bits, method->elem_unsigned);
            prepared.decl = method->decl;
            prepared.elem_float = method->elem_float;
            return prepared;
        }
    }
    prepared.decl = node.callee == nullptr ? nullptr : callee_decl(*node.callee);
    const TypedValue callee = emit_rvalue(*node.callee);
    prepared.enum_id = callee.enum_id;
    if (callee.fn_ty != nullptr && callee.value != nullptr && !llvm::isa<llvm::Function>(callee.value)) {
        std::vector<llvm::Value *> args;
        args.reserve(node.args.size());
        unsigned index = 0;
        for (const auto &arg : node.args) {
            const TypedValue value = emit_rvalue(*arg);
            llvm::Type *param = index < callee.fn_ty->getNumParams() ? callee.fn_ty->getParamType(index) : nullptr;
            args.push_back(adapt(value, param, node.loc));
            ++index;
        }
        prepared.indirect = callee.value;
        prepared.fn_ty = callee.fn_ty;
        prepared.args = std::move(args);
        prepared.depth = callee.fn_ret_depth;
        prepared.elem_float = callee.fn_ret_float;
        prepared.elem_bits = callee.fn_ret_bits;
        prepared.elem_unsigned = callee.fn_ret_unsigned;
        prepared.elem_char = callee.fn_ret_char;
        prepared.elem_bool = callee.fn_ret_bool;
        prepared.struct_id = callee.fn_ret_struct;
        prepared.fn_results = callee.fn_results;
        return prepared;
    }
    auto *function = llvm::dyn_cast_or_null<llvm::Function>(callee.value);
    if (function == nullptr) {
        error(node.loc, "type mismatch");
        return prepared;
    }
    const auto abi = extern_abis_.find(function);
    if (abi != extern_abis_.end() && abi->second.active) {
        const ExternLayout layout = abi->second;
        std::vector<llvm::Value *> args;
        unsigned attr_index = 0;
        if (layout.sret && layout.ret_struct != nullptr) {
            llvm::AllocaInst *slot = aligned_slot(layout.ret_struct, std::max(layout.ret.natural_align, 1u), "sret");
            args.push_back(slot);
            prepared.abi_return = true;
            prepared.abi_sret = true;
            prepared.abi_slot = slot;
            prepared.abi_struct = layout.ret_struct;
            prepared.abi_struct_id = layout.ret_struct_id;
            prepared.abi_ret = layout.ret;
            AbiMark mark;
            mark.index = attr_index++;
            mark.sret = true;
            mark.type = layout.ret_struct;
            mark.align = layout.ret.natural_align == 0 ? 1 : layout.ret.natural_align;
            prepared.abi_attrs.push_back(mark);
        } else if (layout.ret_struct != nullptr) {
            llvm::AllocaInst *slot = aligned_slot(layout.ret_struct, std::max(layout.ret.natural_align, 8u), "cret");
            prepared.abi_return = true;
            prepared.abi_slot = slot;
            prepared.abi_struct = layout.ret_struct;
            prepared.abi_struct_id = layout.ret_struct_id;
            prepared.abi_ret = layout.ret;
        }
        std::size_t source = 0;
        for (const auto &arg : node.args) {
            const TypedValue value = emit_rvalue(*arg);
            if (source < layout.args.size() && layout.args[source].is_struct) {
                const ExternLayout::Arg &info = layout.args[source];
                llvm::AllocaInst *slot =
                    aligned_slot(info.struct_type, std::max(info.abi.natural_align, 8u), "carg");
                if (value.value != nullptr && value.value->getType() == info.struct_type) {
                    builder_.CreateStore(value.value, slot);
                } else if (info.struct_type != nullptr) {
                    builder_.CreateStore(llvm::ConstantAggregateZero::get(info.struct_type), slot);
                }
                if (info.abi.memory) {
                    args.push_back(slot);
                    AbiMark mark;
                    mark.index = attr_index++;
                    mark.type = info.struct_type;
                    mark.align = info.abi.indirect_align == 0 ? 8 : info.abi.indirect_align;
                    prepared.abi_attrs.push_back(mark);
                } else {
                    for (std::size_t part = 0; part < info.abi.parts.size(); ++part) {
                        const uint64_t offset = part < info.abi.offsets.size() ? info.abi.offsets[part] : 0;
                        args.push_back(load_at(slot, info.abi.parts[part], offset));
                        ++attr_index;
                    }
                }
            } else {
                llvm::Type *param = attr_index < function->getFunctionType()->getNumParams()
                                        ? function->getFunctionType()->getParamType(attr_index)
                                        : nullptr;
                args.push_back(adapt(value, param, node.loc));
                ++attr_index;
            }
            ++source;
        }
        remember(function, std::move(args), callee.depth, callee.elem_char, callee.struct_id, callee.elem_void,
                 callee.elem_bool, callee.elem_bits, callee.elem_unsigned);
        prepared.elem_float = callee.elem_float;
        return prepared;
    }
    std::vector<llvm::Value *> args;
    args.reserve(node.args.size());
    unsigned index = 0;
    for (const auto &arg : node.args) {
        const TypedValue value = emit_rvalue(*arg);
        llvm::Type *param = index < function->getFunctionType()->getNumParams()
                                ? function->getFunctionType()->getParamType(index)
                                : nullptr;
        llvm::Value *stored = adapt(value, param, node.loc);
        if (param == nullptr && function->isVarArg()) {
            TypedValue widened = value;
            widened.value = stored;
            stored = variadic_arg(widened);
        }
        args.push_back(stored);
        ++index;
    }
    append_captures(prepared.decl, args, node.loc);
    if (prepared.decl != nullptr && !prepared.decl->captures.empty() && !function->isVarArg() &&
        args.size() != function->arg_size()) {
        error(node.loc, "not supported yet");
        return prepared;
    }
    remember(function, std::move(args), callee.depth, callee.elem_char, callee.struct_id, callee.elem_void, callee.elem_bool,
             callee.elem_bits, callee.elem_unsigned);
    prepared.elem_float = callee.elem_float;
    return prepared;
}

TypedValue CodeGen::emit_prepared(const PreparedCall &call) {
    if (terminated() || (call.function == nullptr && (call.indirect == nullptr || call.fn_ty == nullptr))) {
        return {zero(), 0};
    }
    llvm::Value *pushed = call.trace ? push_trace(call.loc) : nullptr;
    llvm::CallInst *inst = call.indirect != nullptr ? builder_.CreateCall(call.fn_ty, call.indirect, call.args)
                                                    : builder_.CreateCall(call.function, call.args);
    if (pushed != nullptr) {
        pop_trace(pushed);
    }
    inst->setCallingConv(llvm::CallingConv::C);
    if (!call.abi_attrs.empty()) {
        annotate_call(inst, call.abi_attrs);
    }
    if (call.abi_return && call.abi_slot != nullptr && call.abi_struct != nullptr) {
        if (!call.abi_sret) {
            unpack_return(inst, call.abi_slot, call.abi_ret);
        }
        return load_object(call.abi_slot, 0, false, call.abi_struct_id, false, false, 0, false, false);
    }
    if (inst->getType() == slice_type()) {
        TypedValue result{inst, call.depth, call.elem_char, call.struct_id, false, call.elem_void, call.elem_bool,
                          call.elem_bits, call.elem_unsigned};
        result.is_slice = true;
        result.array_len = -1;
        result.elem_float = call.elem_float;
        result.enum_id = call.enum_id;
        return result;
    }
    if (call.elem_float) {
        return {inst, 0, false, -1, false, false, false, call.elem_bits >= 64 ? 64 : 32, false, -1, true};
    }
    if (inst->getType()->isVoidTy()) {
        return {zero(), 0};
    }
    auto tagged = [&](TypedValue value) {
        value.enum_id = call.enum_id;
        return value;
    };
    if (inst->getType()->isArrayTy() && function_ != nullptr) {
        auto *array_ty = llvm::cast<llvm::ArrayType>(inst->getType());
        llvm::IRBuilder<> allocas(&function_->getEntryBlock(), function_->getEntryBlock().begin());
        stamp_builder(allocas);
        llvm::AllocaInst *slot = allocas.CreateAlloca(array_ty, nullptr, "arr.ret");
        builder_.CreateStore(inst, slot);
        llvm::Value *first = builder_.CreateInBoundsGEP(array_ty, slot, {const_i32(0), const_i32(0)});
        return tagged({first, call.depth + 1, call.elem_char, call.struct_id, false, call.elem_void, call.elem_bool,
                       call.elem_bits, call.elem_unsigned});
    }
    if (call.depth == 0 && call.elem_bool) {
        return tagged({builder_.CreateZExt(inst, i32()), 0});
    }
    if (call.depth == 0 && (call.elem_char || call.elem_bits == 8 || call.elem_bits == 16)) {
        const bool zero_extend = call.elem_unsigned || call.elem_char;
        llvm::Value *wide = zero_extend ? builder_.CreateZExt(inst, i32()) : builder_.CreateSExt(inst, i32());
        return tagged({wide, 0});
    }
    TypedValue result = tagged({inst, call.depth, call.elem_char, call.struct_id, false, call.elem_void, call.elem_bool,
                                call.elem_bits, call.elem_unsigned});
    if (inst->getType()->isStructTy() &&
        ((call.decl != nullptr && call.decl->results.size() > 1) || call.fn_results.size() > 1)) {
        result.is_tuple = true;
        if (call.decl != nullptr && call.decl->results.size() > 1) {
            result.tuple_parts = call.decl->results;
        } else {
            result.tuple_parts.reserve(call.fn_results.size());
            for (const FnParam &part : call.fn_results) {
                ResultSlot slot;
                slot.type_name = part.type_name;
                slot.type_package = part.type_package;
                slot.is_struct = part.is_struct;
                slot.is_enum = part.is_enum;
                slot.indirections = part.indirections;
                slot.optional = part.optional;
                slot.const_pointee = part.const_pointee;
                slot.is_array = false;
                result.tuple_parts.push_back(std::move(slot));
            }
        }
    }
    attach_returned_fn(result, call.decl);
    return result;
}

void CodeGen::attach_returned_fn(TypedValue &value, const FunctionDecl *decl) const {
    if (decl == nullptr || decl->results.size() != 1 || !decl->results.front().is_fn_ptr) {
        return;
    }
    const ResultSlot &slot = decl->results.front();
    const Lowered ret = lower_fnptr(current_file_, slot.type_name, slot.type_package, slot.is_struct, slot.is_enum,
                                     slot.ret_indirections, slot.indirections, slot.fn_params, slot.fn_variadic,
                                     slot.fn_results);
    value.depth = ret.depth;
    value.elem_char = false;
    value.elem_void = false;
    value.elem_bool = false;
    value.elem_bits = 0;
    value.elem_unsigned = false;
    value.elem_float = false;
    value.struct_id = -1;
    value.fn_ty = ret.fn_ty;
    value.fn_ret_depth = ret.fn_ret_depth;
    value.fn_ret_float = ret.fn_ret_float;
    value.fn_ret_bits = ret.fn_ret_bits;
    value.fn_ret_unsigned = ret.fn_ret_unsigned;
    value.fn_ret_char = ret.fn_ret_char;
    value.fn_ret_bool = ret.fn_ret_bool;
    value.fn_ret_struct = ret.fn_ret_struct;
    value.fn_results = ret.fn_results;
}

llvm::AllocaInst *CodeGen::defer_flag() {
    llvm::IRBuilder<> allocas(&function_->getEntryBlock(), function_->getEntryBlock().begin());
    stamp_builder(allocas);
    llvm::AllocaInst *flag = allocas.CreateAlloca(i1(), nullptr, "defer.on");
    allocas.CreateStore(llvm::ConstantInt::getFalse(context_), flag);
    return flag;
}

void CodeGen::emit_one_defer(const DeferredCall &call) {
    if (terminated() || call.function == nullptr || call.flag == nullptr) {
        return;
    }
    llvm::BasicBlock *call_block = llvm::BasicBlock::Create(context_, "defer.call", function_);
    llvm::BasicBlock *skip_block = llvm::BasicBlock::Create(context_, "defer.skip", function_);
    llvm::Value *flag = builder_.CreateLoad(i1(), call.flag);
    builder_.CreateCondBr(flag, call_block, skip_block);
    builder_.SetInsertPoint(call_block);
    std::vector<llvm::Value *> args;
    args.reserve(call.args.size());
    for (llvm::AllocaInst *slot : call.args) {
        llvm::Type *type = slot->getAllocatedType();
        args.push_back(builder_.CreateLoad(type, slot));
    }
    llvm::CallInst *inst = builder_.CreateCall(call.function, args);
    inst->setCallingConv(call.function->getCallingConv());
    builder_.CreateStore(llvm::ConstantInt::getFalse(context_), call.flag);
    builder_.CreateBr(skip_block);
    builder_.SetInsertPoint(skip_block);
}

void CodeGen::emit_defer_frame(DeferFrame &frame) {
    for (int index = static_cast<int>(frame.calls.size()) - 1; index >= 0; --index) {
        emit_one_defer(frame.calls[static_cast<std::size_t>(index)]);
        if (terminated()) {
            return;
        }
    }
}

void CodeGen::emit_defers_from(int min_loop_index) {
    for (int frame = static_cast<int>(defers_.size()) - 1; frame >= 0; --frame) {
        if (defers_[static_cast<std::size_t>(frame)].loop_index < min_loop_index) {
            break;
        }
        emit_defer_frame(defers_[static_cast<std::size_t>(frame)]);
        if (terminated()) {
            return;
        }
    }
}

llvm::Function *CodeGen::specialize(const FunctionDecl &fn, const Binding &meta,
                                      const std::vector<TypedValue> &arg_values, SourceLoc loc,
                                      std::vector<llvm::Value *> &out_args) {
    if (arg_values.size() != fn.params.size()) {
        error(loc, "argument count");
        return nullptr;
    }
    std::vector<int> spec(fn.params.size(), -1);
    for (std::size_t i = 0; i < fn.params.size(); ++i) {
        if (!param_is_trait(current_file_, fn.params[i])) {
            continue;
        }
        const TypedValue &arg = arg_values[i];
        if (trait_fn_argument(arg)) {
            if (trait_fn_lowered(fn.params[i].type_name).fn_ty == nullptr) {
                error(loc, "type mismatch");
                return nullptr;
            }
            spec[i] = kFnSpec;
            continue;
        }
        const int indirections = fn.params[i].indirections;
        const bool value_ok = indirections == 0 && arg.depth == 0 && arg.struct_id >= 0;
        const bool pointer_ok = indirections == 1 && arg.depth == 1 && arg.struct_id >= 0;
        if (!value_ok && !pointer_ok) {
            error(loc, "type mismatch");
            return nullptr;
        }
        spec[i] = arg.struct_id;
    }
    const PackageNode *owner = find_node(current_file_);
    if (owner == nullptr) {
        error(loc, "type mismatch");
        return nullptr;
    }
    std::string symbol = owner->qualifier + ".";
    if (!fn.method_struct.empty()) {
        symbol += fn.method_struct;
        symbol += '.';
    }
    symbol += fn.name;
    symbol += ".spec";
    for (int sid : spec) {
        if (sid == -1) {
            continue;
        }
        symbol += '.';
        if (sid == kFnSpec) {
            symbol += "fn";
            continue;
        }
        if (sid >= 0 && sid < static_cast<int>(structs_.size())) {
            symbol += structs_[static_cast<std::size_t>(sid)].name;
        }
    }
    llvm::Function *function = nullptr;
    if (const auto found = specs_.find(symbol); found != specs_.end()) {
        function = found->second;
    } else {
        function = llvm::Function::Create(spec_type(fn, spec), llvm::GlobalValue::InternalLinkage, symbol, module_);
        function->setCallingConv(llvm::CallingConv::C);
        unsigned index = 0;
        for (auto &arg : function->args()) {
            if (index < fn.params.size() && !fn.params[index].name.empty()) {
                arg.setName(fn.params[index].name);
            }
            ++index;
        }
        specs_.emplace(symbol, function);
    }
    if (fn.body != nullptr && function->empty() && !emitting_specs_.contains(symbol)) {
        emitting_specs_.insert(symbol);
        auto saved_scopes = std::move(scopes_);
        auto saved_packages = std::move(packages_);
        llvm::Function *saved_function = function_;
        llvm::Type *saved_return = return_type_;
        const int saved_depth = return_depth_;
        auto saved_spec = active_spec_;
        auto saved_loops = std::move(loops_);
        auto saved_defers = std::move(defers_);
        auto saved_cases = std::move(case_blocks_);
        const auto saved_ip = builder_.saveIP();
        scopes_.clear();
        packages_.clear();
        loops_.clear();
        defers_.clear();
        case_blocks_.clear();
        active_spec_ = spec;
        push();
        bind_package_globals();
        if (const PackageNode *node_owner = find_node(current_file_)) {
            bind_imports(*node_owner);
        }
        emit_function_body(fn, function, meta.depth);
        pop();
        scopes_ = std::move(saved_scopes);
        packages_ = std::move(saved_packages);
        function_ = saved_function;
        return_type_ = saved_return;
        return_depth_ = saved_depth;
        active_spec_ = std::move(saved_spec);
        loops_ = std::move(saved_loops);
        defers_ = std::move(saved_defers);
        case_blocks_ = std::move(saved_cases);
        builder_.restoreIP(saved_ip);
        emitting_specs_.erase(symbol);
    }
    out_args.clear();
    out_args.reserve(arg_values.size());
    unsigned index = 0;
    for (const TypedValue &value : arg_values) {
        llvm::Type *param = index < function->getFunctionType()->getNumParams()
                                ? function->getFunctionType()->getParamType(index)
                                : nullptr;
        out_args.push_back(adapt(value, param, loc));
        ++index;
    }
    return function;
}

void CodeGen::generic_method_call(const Binding &method, const TypedValue *receiver, const Expr::Call &node,
                                  PreparedCall &prepared) {
    if (method.decl == nullptr) {
        error(node.loc, "type mismatch");
        return;
    }
    std::vector<TypedValue> values;
    values.reserve((receiver != nullptr ? 1 : 0) + node.args.size());
    if (receiver != nullptr) {
        values.push_back(*receiver);
    }
    for (const auto &arg : node.args) {
        values.push_back(arg == nullptr ? TypedValue{} : emit_rvalue(*arg));
    }
    std::vector<llvm::Value *> stored;
    llvm::Function *function = specialize(*method.decl, method, values, node.loc, stored);
    if (function == nullptr) {
        return;
    }
    prepared.function = function;
    prepared.args = std::move(stored);
    prepared.depth = method.depth;
    prepared.elem_char = method.elem_char;
    prepared.struct_id = method.struct_id;
    prepared.elem_void = method.elem_void;
    prepared.elem_bool = method.elem_bool;
    prepared.elem_bits = method.elem_bits;
    prepared.elem_unsigned = method.elem_unsigned;
    prepared.decl = method.decl;
    prepared.elem_float = method.elem_float;
    prepared.enum_id = method.enum_id;
}

void CodeGen::call_trait_fn(llvm::Value *fnptr, const TypedValue &meta, const Expr::Call &node, PreparedCall &prepared) {
    if (fnptr == nullptr || meta.fn_ty == nullptr) {
        error(node.loc, "type mismatch");
        return;
    }
    std::vector<llvm::Value *> args;
    args.reserve(node.args.size());
    unsigned index = 0;
    for (const auto &arg : node.args) {
        if (arg == nullptr) {
            ++index;
            continue;
        }
        const TypedValue value = emit_rvalue(*arg);
        llvm::Type *param = index < meta.fn_ty->getNumParams() ? meta.fn_ty->getParamType(index) : nullptr;
        args.push_back(adapt(value, param, node.loc));
        ++index;
    }
    prepared.indirect = fnptr;
    prepared.fn_ty = meta.fn_ty;
    prepared.args = std::move(args);
    prepared.depth = meta.fn_ret_depth;
    prepared.elem_char = meta.fn_ret_char;
    prepared.elem_bool = meta.fn_ret_bool;
    prepared.elem_bits = meta.fn_ret_bits;
    prepared.elem_unsigned = meta.fn_ret_unsigned;
    prepared.elem_float = meta.fn_ret_float;
    prepared.struct_id = meta.fn_ret_struct;
}

TypedValue CodeGen::emit_generic_call(const Binding &binding, const Expr::Call &node) {
    const Binding meta = binding;
    const FunctionDecl *fn = meta.decl;
    std::vector<TypedValue> arg_values;
    arg_values.reserve(node.args.size());
    for (const auto &arg : node.args) {
        arg_values.push_back(arg == nullptr ? TypedValue{} : emit_rvalue(*arg));
    }
    if (fn == nullptr) {
        error(node.loc, "type mismatch");
        return {zero(), 0};
    }
    std::vector<llvm::Value *> args;
    llvm::Function *function = specialize(*fn, meta, arg_values, node.loc, args);
    if (function == nullptr) {
        return {zero(), 0};
    }
    llvm::CallInst *call = builder_.CreateCall(function, args);
    call->setCallingConv(function->getCallingConv());
    if (call->getType()->isVoidTy()) {
        return {zero(), 0};
    }
    if (meta.depth == 0 && meta.elem_bool) {
        return {builder_.CreateZExt(call, i32()), 0};
    }
    if (meta.depth == 0 && (meta.elem_char || meta.elem_bits == 8 || meta.elem_bits == 16)) {
        const bool zero_extend = meta.elem_unsigned || meta.elem_char;
        llvm::Value *wide = zero_extend ? builder_.CreateZExt(call, i32()) : builder_.CreateSExt(call, i32());
        return {wide, 0};
    }
    TypedValue result{call, meta.depth, meta.elem_char, meta.struct_id, false, meta.elem_void, meta.elem_bool,
                      meta.elem_bits, meta.elem_unsigned, -1, meta.elem_float};
    result.enum_id = meta.enum_id;
    attach_returned_fn(result, fn);
    return result;
}

TypedValue CodeGen::emit_addr(const Expr &expr) {
    if (const auto *name = std::get_if<Expr::Name>(&expr.data)) {
        const Binding *binding = lookup(name->text);
        if (binding == nullptr || binding->is_function || binding->is_array) {
            error(name->loc, binding != nullptr && binding->is_array ? "not supported yet" : "not an lvalue");
            return {};
        }
        TypedValue slot{binding->value, binding->depth, binding->elem_char, binding->struct_id, false, binding->elem_void,
                        binding->elem_bool, binding->elem_bits, binding->elem_unsigned, binding->trait_id, binding->elem_float,
                        binding->fn_ty, binding->fn_ret_depth, binding->fn_ret_float, binding->fn_ret_bits,
                        binding->fn_ret_unsigned, binding->fn_ret_char, binding->fn_ret_bool, binding->fn_ret_struct,
                        binding->fn_results, binding->enum_id};
        slot.is_slice = binding->is_slice;
        return slot;
    }
    if (const auto *star = std::get_if<Expr::Unary>(&expr.data); star != nullptr && star->op == TokenKind::Star) {
        const TypedValue inner = emit_rvalue(*star->expr);
        if (inner.depth <= 0 || inner.value == nullptr) {
            error(star->loc, "not an lvalue");
            return {};
        }
        return {inner.value, inner.depth - 1, inner.elem_char, inner.struct_id, false, inner.elem_void, inner.elem_bool,
                inner.elem_bits, inner.elem_unsigned, inner.trait_id, inner.elem_float};
    }
    if (const auto *index = std::get_if<Expr::Index>(&expr.data)) {
        if (index->end != nullptr) {
            error(index->loc, "not an lvalue");
            return {};
        }
        if (index->tuple_index >= 0) {
            error(index->loc, "not supported yet");
            return {};
        }
        const TypedValue base = emit_rvalue(*index->base);
        const TypedValue subscript = emit_rvalue(*index->index);
        if (base.is_slice) {
            if (base.value == nullptr || subscript.value == nullptr) {
                error(index->loc, "not an lvalue");
                return {};
            }
            llvm::Value *data = builder_.CreateExtractValue(base.value, 0);
            llvm::Type *element = leaf_type(base.depth, base.elem_char, base.struct_id, base.elem_bool, base.elem_bits,
                                            base.elem_float);
            llvm::Value *slot =
                builder_.CreateInBoundsGEP(element, data, llvm::ArrayRef<llvm::Value *>{subscript.value});
            TypedValue out{slot,       base.depth,     base.elem_char, base.struct_id, false,          base.elem_void,
                           base.elem_bool, base.elem_bits, base.elem_unsigned, -1,     base.elem_float};
            out.enum_id = base.enum_id;
            return out;
        }
        if (base.depth <= 0 || base.value == nullptr || subscript.value == nullptr) {
            error(index->loc, "not an lvalue");
            return {};
        }
        llvm::Value *offset = subscript.value;
        if (base.inner_array_len > 0) {
            llvm::Type *leaf =
                leaf_type(base.depth - 1, base.elem_char, base.struct_id, base.elem_bool, base.elem_bits, base.elem_float);
            llvm::Type *row = llvm::ArrayType::get(leaf, static_cast<uint64_t>(base.inner_array_len));
            llvm::Value *slot = builder_.CreateInBoundsGEP(row, base.value, llvm::ArrayRef<llvm::Value *>{offset});
            TypedValue out{slot, base.depth, base.elem_char, base.struct_id, false, base.elem_void, base.elem_bool,
                           base.elem_bits, base.elem_unsigned, -1, base.elem_float};
            out.row_decay = true;
            return out;
        }
        if (base.ptr_to_array) {
            const int depth = base.depth - 1;
            llvm::Type *element =
                leaf_type(depth, base.elem_char, base.struct_id, base.elem_bool, base.elem_bits, base.elem_float);
            llvm::Type *array = llvm::ArrayType::get(element, static_cast<uint64_t>(base.ptr_array_len));
            llvm::Value *slot = builder_.CreateInBoundsGEP(
                array, base.value, llvm::ArrayRef<llvm::Value *>{const_i32(0), offset});
            return {slot, depth, base.elem_char, base.struct_id, false, base.elem_void, base.elem_bool, base.elem_bits,
                    base.elem_unsigned, -1, base.elem_float};
        }
        const int depth = base.depth - 1;
        llvm::Value *slot = builder_.CreateInBoundsGEP(
            leaf_type(depth, base.elem_char, base.struct_id, base.elem_bool, base.elem_bits, base.elem_float), base.value,
            llvm::ArrayRef<llvm::Value *>{offset});
        TypedValue out{slot,
                       depth,
                       base.elem_char,
                       base.struct_id,
                       false,
                       base.elem_void,
                       base.elem_bool,
                       base.elem_bits,
                       base.elem_unsigned,
                       -1,
                       base.elem_float};
        out.fn_ty = base.fn_ty;
        out.fn_ret_depth = base.fn_ret_depth;
        out.fn_ret_float = base.fn_ret_float;
        out.fn_ret_bits = base.fn_ret_bits;
        out.fn_ret_unsigned = base.fn_ret_unsigned;
        out.fn_ret_char = base.fn_ret_char;
        out.fn_ret_bool = base.fn_ret_bool;
        out.fn_ret_struct = base.fn_ret_struct;
        out.fn_results = base.fn_results;
        out.enum_id = base.enum_id;
        return out;
    }
    if (const auto *selector = std::get_if<Expr::Selector>(&expr.data)) {
        if (!selector->resolved_owner.empty()) {
            const Binding *found =
                visible(assoc_home(*selector), selector->resolved_owner + "." + selector->field, false);
            if (found == nullptr || found->value == nullptr || found->is_function || found->is_array) {
                error(selector->loc, "not an lvalue");
                return {};
            }
            TypedValue slot;
            slot.value = found->value;
            slot.depth = found->depth;
            slot.elem_char = found->elem_char;
            slot.struct_id = found->struct_id;
            slot.elem_void = found->elem_void;
            slot.elem_bool = found->elem_bool;
            slot.elem_bits = found->elem_bits;
            slot.elem_unsigned = found->elem_unsigned;
            slot.elem_float = found->elem_float;
            slot.trait_id = found->trait_id;
            slot.enum_id = found->enum_id;
            slot.fn_ty = found->fn_ty;
            return slot;
        }
        const auto *base_name = selector->base == nullptr ? nullptr : std::get_if<Expr::Name>(&selector->base->data);
        const auto package = base_name == nullptr ? packages_.end() : packages_.find(base_name->text);
        if (base_name != nullptr && package != packages_.end()) {
            const Binding *found = visible(package->second, selector->field, false);
            if (found == nullptr || found->is_function || found->is_array) {
                error(selector->loc, found != nullptr && found->is_array ? "not supported yet" : "not an lvalue");
                return {};
            }
            return {found->value, found->depth, found->elem_char, found->struct_id, false, found->elem_void,
                    found->elem_bool, found->elem_bits, found->elem_unsigned, found->trait_id, found->elem_float};
        }
        if (selector->base == nullptr) {
            error(selector->loc, "not an lvalue");
            return {};
        }
        if (addressable(*selector->base)) {
            const TypedValue base = emit_addr(*selector->base);
            if (base.value == nullptr) {
                return {};
            }
            if (base.depth == 0 && base.struct_id >= 0) {
                return field_addr(base.struct_id, base.value, selector->field, selector->loc);
            }
            if (base.depth == 1 && base.struct_id >= 0) {
                llvm::Value *pointer = builder_.CreateLoad(ptr_type(), base.value);
                return field_addr(base.struct_id, pointer, selector->field, selector->loc);
            }
            error(selector->loc, "not an lvalue");
            return {};
        }
        const TypedValue base = emit_rvalue(*selector->base);
        if (base.value != nullptr && base.depth == 1 && base.struct_id >= 0) {
            return field_addr(base.struct_id, base.value, selector->field, selector->loc);
        }
        error(selector->loc, "not an lvalue");
        return {};
    }
    error(expr_loc(expr), "not an lvalue");
    return {};
}

llvm::Value *CodeGen::emit_short_circuit(const Expr::Binary &expr, bool is_and) {
    llvm::Value *lhs = emit_rvalue(*expr.lhs).value;
    llvm::BasicBlock *lhs_block = builder_.GetInsertBlock();
    llvm::BasicBlock *rhs_block = llvm::BasicBlock::Create(context_, is_and ? "land.rhs" : "lor.rhs", function_);
    llvm::BasicBlock *end_block = llvm::BasicBlock::Create(context_, is_and ? "land.end" : "lor.end", function_);
    builder_.CreateCondBr(is_nonzero(lhs), is_and ? rhs_block : end_block, is_and ? end_block : rhs_block);
    builder_.SetInsertPoint(rhs_block);
    llvm::Value *rhs = emit_rvalue(*expr.rhs).value;
    llvm::Value *rhs_flag = is_nonzero(rhs);
    llvm::BasicBlock *rhs_end = builder_.GetInsertBlock();
    if (!terminated()) {
        builder_.CreateBr(end_block);
    }
    builder_.SetInsertPoint(end_block);
    auto *phi = builder_.CreatePHI(llvm::Type::getInt1Ty(context_), 2);
    phi->addIncoming(is_and ? llvm::ConstantInt::getFalse(context_) : llvm::ConstantInt::getTrue(context_), lhs_block);
    phi->addIncoming(rhs_flag, rhs_end);
    return builder_.CreateZExt(phi, i32());
}

llvm::Type *CodeGen::shape_type(const Shape &shape) const {
    if (shape.is_slice) {
        return slice_type();
    }
    if (shape.is_function || (shape.depth == 0 && shape.elem_void && !shape.is_array)) {
        return nullptr;
    }
    if (shape.is_array) {
        if (shape.array_len < 0) {
            return nullptr;
        }
        return llvm::ArrayType::get(
            leaf_type(shape.depth, shape.elem_char, shape.struct_id, shape.elem_bool, shape.elem_bits, shape.elem_float),
            static_cast<uint64_t>(shape.array_len));
    }
    return leaf_type(shape.depth, shape.elem_char, shape.struct_id, shape.elem_bool, shape.elem_bits);
}

CodeGen::Shape CodeGen::decay_shape(Shape shape) const {
    if (shape.is_array) {
        shape.depth += 1;
        shape.is_array = false;
        shape.array_len = 0;
        shape.type = ptr_type();
        return shape;
    }
    if (shape.depth == 0 && shape.elem_char) {
        shape.elem_char = false;
        shape.type = i32();
    }
    if (shape.depth == 0 && shape.elem_bool) {
        shape.elem_bool = false;
        shape.type = i32();
    }
    return shape;
}

CodeGen::Shape CodeGen::shape_from_binding(const Binding &binding) const {
    Shape shape;
    shape.depth = binding.depth;
    shape.elem_char = binding.elem_char;
    shape.elem_void = binding.elem_void;
    shape.elem_bool = binding.elem_bool;
    shape.elem_bits = binding.elem_bits;
    shape.elem_unsigned = binding.elem_unsigned;
    shape.struct_id = binding.struct_id;
    shape.is_array = binding.is_array;
    shape.array_len = binding.array_len;
    shape.is_function = binding.is_function;
    shape.is_slice = binding.is_slice;
    shape.elem_float = binding.elem_float;
    shape.type = shape_type(shape);
    return shape;
}

CodeGen::Shape CodeGen::raw_shape(const Expr &expr) const {
    return std::visit(
        [&](const auto &node) -> Shape {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Expr::Name>) {
                if (const Binding *binding = lookup(node.text)) {
                    return shape_from_binding(*binding);
                }
                const Alias *alias = find_alias(current_file_, node.text);
                if (alias == nullptr) {
                    alias = selective_alias(current_file_, node.text);
                }
                if (alias != nullptr) {
                    Shape shape;
                    shape.depth = alias->depth;
                    shape.elem_char = alias->elem_char;
                    shape.elem_void = alias->elem_void;
                    shape.elem_bool = alias->elem_bool;
                    shape.elem_bits = alias->elem_bits;
                    shape.elem_unsigned = alias->elem_unsigned;
                    shape.struct_id = alias->struct_id;
                    shape.is_array = alias->is_array;
                    shape.array_len = alias->array_len;
                    shape.is_slice = alias->is_slice;
                    shape.elem_float = alias->elem_float;
                    shape.type = shape_type(shape);
                    return shape;
                }
                int sid = tag_id(current_file_, node.text);
                if (sid < 0) {
                    sid = selective_tag(current_file_, node.text);
                }
                if (sid < 0) {
                    int eid = enum_named(current_file_, node.text);
                    if (eid < 0) {
                        eid = selective_enum(current_file_, node.text);
                    }
                    if (eid < 0 || eid >= static_cast<int>(enums_.size())) {
                        return {};
                    }
                    const EnumLayout &layout = enums_[static_cast<std::size_t>(eid)];
                    Shape shape;
                    shape.elem_bool = layout.is_bool;
                    shape.elem_char = layout.is_char;
                    shape.elem_unsigned = layout.is_unsigned;
                    shape.elem_bits = layout.is_bool ? 0 : layout.bits;
                    shape.type = shape_type(shape);
                    return shape;
                }
                Shape shape;
                shape.struct_id = sid;
                shape.type = shape_type(shape);
                return shape;
            } else if constexpr (std::is_same_v<T, Expr::Typeof>) {
                Shape shape;
                shape.depth = 1;
                shape.type = ptr_type();
                return shape;
            } else if constexpr (std::is_same_v<T, Expr::Int> || std::is_same_v<T, Expr::Sizeof>) {
                Shape shape;
                shape.type = i32();
                return shape;
            } else if constexpr (std::is_same_v<T, Expr::Bool>) {
                Shape shape;
                shape.elem_bool = true;
                shape.type = i1();
                return shape;
            } else if constexpr (std::is_same_v<T, Expr::Nullptr>) {
                Shape shape;
                shape.depth = 1;
                shape.elem_void = true;
                shape.type = ptr_type();
                return shape;
            } else if constexpr (std::is_same_v<T, Expr::String>) {
                Shape shape;
                shape.depth = 1;
                shape.elem_bits = 8;
                shape.elem_unsigned = true;
                shape.type = ptr_type();
                return shape;
            } else if constexpr (std::is_same_v<T, Expr::Unary>) {
                if (node.postfix && node.op == TokenKind::Bang && node.expr) {
                    return raw_shape(*node.expr);
                }
                if (node.op == TokenKind::Star && node.expr) {
                    const Shape inner = decay_shape(raw_shape(*node.expr));
                    if (inner.depth <= 0) {
                        return {};
                    }
                    Shape pointee = inner;
                    pointee.depth -= 1;
                    pointee.is_array = false;
                    pointee.array_len = 0;
                    pointee.is_function = false;
                    pointee.type = shape_type(pointee);
                    return pointee;
                }
                if (node.op == TokenKind::Amp && node.expr) {
                    Shape inner = raw_shape(*node.expr);
                    if (inner.is_array || inner.is_function || inner.type == nullptr) {
                        return {};
                    }
                    inner.depth += 1;
                    inner.type = ptr_type();
                    return inner;
                }
                Shape shape;
                shape.type = i32();
                return shape;
            } else if constexpr (std::is_same_v<T, Expr::Binary>) {
                Shape shape;
                shape.type = i32();
                return shape;
            } else if constexpr (std::is_same_v<T, Expr::Assign>) {
                return node.lhs ? raw_shape(*node.lhs) : Shape{};
            } else if constexpr (std::is_same_v<T, Expr::Index>) {
                if (node.end != nullptr && node.base != nullptr) {
                    Shape base = raw_shape(*node.base);
                    Shape slice = base;
                    slice.is_slice = true;
                    slice.is_array = false;
                    slice.array_len = -1;
                    slice.type = slice_type();
                    if (!base.is_slice && base.depth > 0) {
                        slice.depth = base.depth - 1;
                    }
                    return slice;
                }
                if (node.tuple_index >= 0 && node.base != nullptr) {
                    const auto *named = std::get_if<Expr::Name>(&node.base->data);
                    const Binding *binding = named == nullptr ? nullptr : lookup(named->text);
                    if (binding != nullptr && binding->is_tuple &&
                        node.tuple_index < static_cast<int>(binding->tuple_parts.size())) {
                        const ResultSlot &slot = binding->tuple_parts[static_cast<std::size_t>(node.tuple_index)];
                        const Lowered low = lower(current_file_, slot.type_name, slot.type_package, slot.is_struct,
                                                  slot.indirections, slot.is_array, slot.array_len, false);
                        Shape shape;
                        shape.depth = low.depth;
                        shape.elem_char = low.elem_char;
                        shape.elem_void = low.elem_void;
                        shape.elem_bool = low.elem_bool;
                        shape.elem_bits = low.elem_bits;
                        shape.elem_unsigned = low.elem_unsigned;
                        shape.elem_float = low.elem_float;
                        shape.struct_id = low.struct_id;
                        shape.is_array = low.is_array;
                        shape.array_len = low.array_len;
                        shape.type = low.type;
                        return shape;
                    }
                    return {};
                }
                if (node.base == nullptr) {
                    return {};
                }
                const Shape base = raw_shape(*node.base);
                if (base.is_slice) {
                    Shape element = base;
                    element.is_slice = false;
                    element.array_len = 0;
                    element.type = shape_type(element);
                    return element;
                }
                const Shape inner = decay_shape(base);
                if (inner.depth <= 0) {
                    return {};
                }
                Shape pointee = inner;
                pointee.depth -= 1;
                pointee.is_array = false;
                pointee.array_len = 0;
                pointee.type = shape_type(pointee);
                return pointee;
            } else if constexpr (std::is_same_v<T, Expr::Cast>) {
                const Lowered low =
                    node.is_fn_ptr ? lower_fnptr(current_file_, node.type_name, node.type_package, node.is_struct,
                                                 node.is_enum, node.ret_indirections, node.indirections, node.fn_params,
                                                 node.fn_variadic, node.fn_results)
                                   : lower(current_file_, node.type_name, node.type_package, node.is_struct,
                                           node.indirections, false, 0, false);
                Shape shape;
                shape.depth = low.depth;
                shape.elem_char = low.elem_char;
                shape.elem_void = low.elem_void;
                shape.elem_bool = low.elem_bool;
                shape.elem_bits = low.elem_bits;
                shape.elem_unsigned = low.elem_unsigned;
                shape.struct_id = low.struct_id;
                shape.type = low.depth == 0 && low.elem_void ? nullptr : low.type;
                return shape;
            } else if constexpr (std::is_same_v<T, Expr::Call>) {
                if (node.type_name) {
                    Shape shape;
                    shape.depth = 1;
                    shape.elem_bits = 8;
                    shape.elem_unsigned = true;
                    shape.type = ptr_type();
                    return shape;
                }
                if (node.callee == nullptr) {
                    return {};
                }
                const Binding *binding = nullptr;
                if (const auto *name = std::get_if<Expr::Name>(&node.callee->data)) {
                    binding = lookup(name->text);
                } else if (const auto *selector = std::get_if<Expr::Selector>(&node.callee->data)) {
                    const auto *base = selector->base == nullptr ? nullptr : std::get_if<Expr::Name>(&selector->base->data);
                    if (base != nullptr) {
                        const auto package = packages_.find(base->text);
                        if (package != packages_.end()) {
                            binding = visible(package->second, selector->field, true);
                        }
                    }
                }
                if (binding == nullptr || !binding->is_function) {
                    return {};
                }
                Shape shape;
                shape.depth = binding->depth;
                shape.elem_char = binding->elem_char;
                shape.elem_void = binding->elem_void;
                shape.elem_bool = binding->elem_bool;
                shape.elem_bits = binding->elem_bits;
                shape.elem_unsigned = binding->elem_unsigned;
                shape.struct_id = binding->struct_id;
                shape.type = shape_type(shape);
                return shape;
            } else if constexpr (std::is_same_v<T, Expr::Selector>) {
                if (!node.resolved_owner.empty()) {
                    const std::string key = node.resolved_owner + "." + node.field;
                    const Binding *global = visible(assoc_home(node), key, false);
                    if (global != nullptr) {
                        return shape_from_binding(*global);
                    }
                    const Binding *method = static_method_binding(current_file_, node.resolved_owner, node.field);
                    if (method != nullptr) {
                        return shape_from_binding(*method);
                    }
                    return {};
                }
                const auto *base_name = node.base == nullptr ? nullptr : std::get_if<Expr::Name>(&node.base->data);
                if (base_name != nullptr) {
                    const auto package = packages_.find(base_name->text);
                    if (package != packages_.end()) {
                        if (const Binding *function = visible(package->second, node.field, true)) {
                            return shape_from_binding(*function);
                        }
                        if (const Binding *global = visible(package->second, node.field, false)) {
                            return shape_from_binding(*global);
                        }
                        return {};
                    }
                }
                if (node.base == nullptr) {
                    return {};
                }
                const Shape base = raw_shape(*node.base);
                if (base.depth > 1 || base.struct_id < 0 || base.is_array) {
                    return {};
                }
                int field_index = -1;
                const FieldLayout *field = find_field(base.struct_id, node.field, field_index);
                if (field == nullptr) {
                    return {};
                }
                Shape shape;
                shape.depth = field->depth;
                shape.elem_char = field->elem_char;
                shape.elem_void = field->elem_void;
                shape.elem_bool = field->elem_bool;
                shape.elem_bits = field->elem_bits;
                shape.elem_unsigned = field->elem_unsigned;
                shape.struct_id = field->struct_id;
                shape.is_array = field->is_array;
                shape.array_len = field->array_len;
                shape.is_slice = field->is_slice;
                shape.type = field->type != nullptr ? field->type : shape_type(shape);
                return shape;
            } else {
                return {};
            }
        },
        expr.data);
}

TypedValue CodeGen::load_field(const TypedValue &addr) {
    if (addr.value == nullptr) {
        return {zero(), 0};
    }
    // A slice field is the `{ ptr, i32 }` header, not the first element.
    if (addr.is_slice) {
        TypedValue loaded;
        loaded.value = builder_.CreateLoad(slice_type(), addr.value);
        loaded.is_slice = true;
        loaded.array_len = -1;
        loaded.depth = addr.depth;
        loaded.elem_char = addr.elem_char;
        loaded.struct_id = addr.struct_id;
        loaded.elem_void = addr.elem_void;
        loaded.elem_bool = addr.elem_bool;
        loaded.elem_bits = addr.elem_bits;
        loaded.elem_unsigned = addr.elem_unsigned;
        loaded.elem_float = addr.elem_float;
        return loaded;
    }
    // An array field decays to a pointer to its first element. The length stays so a range can use it.
    if (addr.is_array) {
        TypedValue value{addr.value, addr.depth + 1, addr.elem_char, addr.struct_id, false, addr.elem_void,
                         addr.elem_bool, addr.elem_bits, addr.elem_unsigned, -1, addr.elem_float};
        value.array_len = addr.array_len;
        return value;
    }
    return load_object(addr.value, addr.depth, addr.elem_char, addr.struct_id, addr.elem_void, addr.elem_bool,
                       addr.elem_bits, addr.elem_unsigned, addr.elem_float);
}

TypedValue CodeGen::emit_range(const Expr::Index &node) {
    if (node.base == nullptr || node.index == nullptr || node.end == nullptr) {
        error(node.loc, "type mismatch");
        return {zero(), 0};
    }
    const TypedValue base = emit_rvalue(*node.base);
    const TypedValue low = emit_rvalue(*node.index);
    const TypedValue high = emit_rvalue(*node.end);
    if (base.value == nullptr || low.value == nullptr || high.value == nullptr || terminated()) {
        return {zero(), 0};
    }
    llvm::Value *packed = as_slice_value(base, node.loc);
    if (terminated() || packed == nullptr) {
        return {zero(), 0};
    }
    llvm::Value *data = builder_.CreateExtractValue(packed, 0);
    // A slice value's depth is the element. A decayed array's depth is one more.
    const int elem_depth = base.is_slice ? base.depth : base.depth > 0 ? base.depth - 1 : base.depth;
    llvm::Type *element =
        leaf_type(elem_depth, base.elem_char, base.struct_id, base.elem_bool, base.elem_bits, base.elem_float);
    auto as_bound = [&](const TypedValue &bound) -> llvm::Value * {
        if (bound.value == nullptr || !bound.value->getType()->isIntegerTy()) {
            return nullptr;
        }
        if (bound.value->getType() == i32()) {
            return bound.value;
        }
        return resize_int(bound.value, i32(), bound.elem_unsigned);
    };
    llvm::Value *start_index = as_bound(low);
    llvm::Value *end_index = as_bound(high);
    if (start_index == nullptr || end_index == nullptr) {
        error(node.loc, "type mismatch");
        return {zero(), 0};
    }
    llvm::Value *start = builder_.CreateInBoundsGEP(element, data, llvm::ArrayRef<llvm::Value *>{start_index});
    llvm::Value *len = builder_.CreateSub(end_index, start_index);
    llvm::Value *out = llvm::ConstantAggregateZero::get(slice_type());
    out = builder_.CreateInsertValue(out, start, 0);
    out = builder_.CreateInsertValue(out, len, 1);
    TypedValue result;
    result.value = out;
    result.is_slice = true;
    result.array_len = -1;
    result.depth = elem_depth;
    result.elem_char = base.elem_char;
    result.struct_id = base.struct_id;
    result.elem_void = base.elem_void;
    result.elem_bool = base.elem_bool;
    result.elem_bits = base.elem_bits;
    result.elem_unsigned = base.elem_unsigned;
    result.elem_float = base.elem_float;
    result.enum_id = base.enum_id;
    result.fn_ty = base.fn_ty;
    result.fn_ret_depth = base.fn_ret_depth;
    result.fn_ret_float = base.fn_ret_float;
    result.fn_ret_bits = base.fn_ret_bits;
    result.fn_ret_unsigned = base.fn_ret_unsigned;
    result.fn_ret_char = base.fn_ret_char;
    result.fn_ret_bool = base.fn_ret_bool;
    result.fn_ret_struct = base.fn_ret_struct;
    result.fn_results = base.fn_results;
    return result;
}

TypedValue CodeGen::emit_rvalue(const Expr &expr) {
    if (terminated()) {
        return {zero(), 0};
    }
    set_loc(std::visit([](const auto &node) -> SourceLoc { return node.loc; }, expr.data));
    return std::visit(
        [&](const auto &node) -> TypedValue {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Expr::Name>) {
                const Binding *binding = lookup(node.text);
                if (binding == nullptr) {
                    error(node.loc, "undeclared identifier");
                    return {zero(), 0};
                }
                if (binding->is_generic) {
                    error(node.loc, "not supported yet");
                    return {zero(), 0};
                }
                if (binding->immediate) {
                    return value_of_binding(*binding);
                }
                if (binding->value == nullptr) {
                    error(node.loc, "undeclared identifier");
                    return {zero(), 0};
                }
                return value_of_binding(*binding);
            } else if constexpr (std::is_same_v<T, Expr::Float>) {
                const std::string number = node.text.empty() ? "0" : node.text;
                const double parsed = std::strtod(number.c_str(), nullptr);
                llvm::Type *ty = node.is_f32 ? f32_ty() : f64_ty();
                return {llvm::ConstantFP::get(ty, parsed), 0, false, -1, false, false, false, node.is_f32 ? 32 : 64, false,
                        -1, true};
            } else if constexpr (std::is_same_v<T, Expr::Int>) {
                const int bits = node.suffix.bits;
                const bool uns = node.suffix.is_unsigned;
                if (bits >= 64) {
                    return {llvm::ConstantInt::get(i64(), node.value, false), 0, false, -1, false, false, false, 64, uns};
                }
                if (uns && bits == 32) {
                    return {const_i32(static_cast<int32_t>(node.value)), 0, false, -1, false, false, false, 32, true};
                }
                // A narrower suffix promotes to int when it is used as a value.
                return {const_i32(static_cast<int32_t>(node.value)), 0};
            } else if constexpr (std::is_same_v<T, Expr::Char>) {
                return {const_i32(static_cast<int32_t>(node.value & 0xFFULL)), 0};
            } else if constexpr (std::is_same_v<T, Expr::Unary>) {
                if (node.op == TokenKind::Star) {
                    const TypedValue inner = emit_rvalue(*node.expr);
                    if (inner.depth <= 0 || inner.value == nullptr) {
                        error(node.loc, "type mismatch");
                        return {zero(), 0};
                    }
                    // `*arr` on a pointer to an array is the array, which decays to a pointer to its element.
                    if (inner.ptr_to_array) {
                        TypedValue decayed = inner;
                        decayed.ptr_to_array = false;
                        decayed.ptr_array_len = 0;
                        return decayed;
                    }
                    return load_object(inner.value, inner.depth - 1, inner.elem_char, inner.struct_id, inner.elem_void,
                                       inner.elem_bool, inner.elem_bits, inner.elem_unsigned, inner.elem_float);
                }
                if (node.op == TokenKind::Amp) {
                    if (const auto *named = std::get_if<Expr::Name>(&node.expr->data)) {
                        if (const Binding *binding = lookup(named->text); binding != nullptr && binding->is_array) {
                            TypedValue pointer;
                            pointer.value = binding->value;
                            pointer.depth = binding->depth + 1;
                            pointer.elem_char = binding->elem_char;
                            pointer.struct_id = binding->struct_id;
                            pointer.elem_void = binding->elem_void;
                            pointer.elem_bool = binding->elem_bool;
                            pointer.elem_bits = binding->elem_bits;
                            pointer.elem_unsigned = binding->elem_unsigned;
                            pointer.elem_float = binding->elem_float;
                            pointer.ptr_to_array = true;
                            pointer.ptr_array_len = binding->array_len;
                            return pointer;
                        }
                    }
                    const TypedValue addr = emit_addr(*node.expr);
                    if (addr.value == nullptr) {
                        return {zero(), 0};
                    }
                    return {addr.value, addr.depth + 1, addr.elem_char, addr.struct_id, false, addr.elem_void,
                            addr.elem_bool, addr.elem_bits, addr.elem_unsigned, addr.trait_id, addr.elem_float};
                }
                const TypedValue inner_value = emit_rvalue(*node.expr);
                if (node.op == TokenKind::Bang && node.postfix) {
                    return inner_value;
                }
                llvm::Value *inner = inner_value.value;
                switch (node.op) {
                case TokenKind::Plus: return inner_value;
                case TokenKind::Minus:
                    if (inner_value.elem_float || (inner != nullptr && inner->getType()->isFloatingPointTy())) {
                        return {builder_.CreateFNeg(inner), 0, false, -1, false, false, false, inner_value.elem_bits, false,
                                -1, true};
                    }
                    return {builder_.CreateNeg(inner), 0, false, -1, false, false, false, inner_value.elem_bits,
                            inner_value.elem_unsigned};
                case TokenKind::Tilde:
                    return {builder_.CreateNot(inner), 0, false, -1, false, false, false, inner_value.elem_bits,
                            inner_value.elem_unsigned};
                case TokenKind::Bang:
                    if (inner_value.elem_float || (inner != nullptr && inner->getType()->isFloatingPointTy())) {
                        llvm::Value *zero_fp = llvm::ConstantFP::get(inner->getType(), 0.0);
                        return {builder_.CreateZExt(builder_.CreateFCmpOEQ(inner, zero_fp), i32()), 0};
                    }
                    return {builder_.CreateZExt(builder_.CreateICmpEQ(inner, typed_zero(inner)), i32()), 0};
                default: return {zero(), 0};
                }
            } else if constexpr (std::is_same_v<T, Expr::Binary>) {
                if (node.op == TokenKind::AmpAmp) {
                    return {emit_short_circuit(node, true), 0};
                }
                if (node.op == TokenKind::PipePipe) {
                    return {emit_short_circuit(node, false), 0};
                }
                const TypedValue left = emit_rvalue(*node.lhs);
                const TypedValue right = emit_rvalue(*node.rhs);
                llvm::Value *lhs = left.value;
                llvm::Value *rhs = right.value;
                if ((node.op == TokenKind::EqEq || node.op == TokenKind::BangEq) && (left.depth > 0 || right.depth > 0)) {
                    if (left.depth <= 0) {
                        lhs = null_ptr();
                    }
                    if (right.depth <= 0) {
                        rhs = null_ptr();
                    }
                    llvm::Value *cmp = node.op == TokenKind::EqEq ? builder_.CreateICmpEQ(lhs, rhs)
                                                                  : builder_.CreateICmpNE(lhs, rhs);
                    return {builder_.CreateZExt(cmp, i32()), 0};
                }
                if (left.elem_float || right.elem_float ||
                    (lhs != nullptr && lhs->getType()->isFloatingPointTy()) ||
                    (rhs != nullptr && rhs->getType()->isFloatingPointTy())) {
                    const bool wide = (left.elem_float && left.elem_bits >= 64) || (right.elem_float && right.elem_bits >= 64) ||
                                      (lhs != nullptr && lhs->getType()->isDoubleTy()) ||
                                      (rhs != nullptr && rhs->getType()->isDoubleTy());
                    llvm::Type *dest = wide ? f64_ty() : f32_ty();
                    auto to_fp = [&](llvm::Value *value, const TypedValue &meta) -> llvm::Value * {
                        if (value == nullptr) {
                            return llvm::ConstantFP::get(dest, 0.0);
                        }
                        return as_stored(value, meta.depth, dest, meta.elem_unsigned);
                    };
                    lhs = to_fp(lhs, left);
                    rhs = to_fp(rhs, right);
                    auto widened_fp = [&](llvm::Value *value) -> TypedValue {
                        return {value, 0, false, -1, false, false, false, wide ? 64 : 32, false, -1, true};
                    };
                    auto compared_fp = [&](llvm::CmpInst::Predicate pred) -> TypedValue {
                        return {builder_.CreateZExt(builder_.CreateFCmp(pred, lhs, rhs), i32()), 0};
                    };
                    switch (node.op) {
                    case TokenKind::Plus: return widened_fp(builder_.CreateFAdd(lhs, rhs));
                    case TokenKind::Minus: return widened_fp(builder_.CreateFSub(lhs, rhs));
                    case TokenKind::Star: return widened_fp(builder_.CreateFMul(lhs, rhs));
                    case TokenKind::Slash: return widened_fp(builder_.CreateFDiv(lhs, rhs));
                    case TokenKind::EqEq: return compared_fp(llvm::CmpInst::FCMP_OEQ);
                    case TokenKind::BangEq: return compared_fp(llvm::CmpInst::FCMP_ONE);
                    case TokenKind::Lt: return compared_fp(llvm::CmpInst::FCMP_OLT);
                    case TokenKind::Gt: return compared_fp(llvm::CmpInst::FCMP_OGT);
                    case TokenKind::Le: return compared_fp(llvm::CmpInst::FCMP_OLE);
                    case TokenKind::Ge: return compared_fp(llvm::CmpInst::FCMP_OGE);
                    default:
                        error(node.loc, "type mismatch");
                        return {zero(), 0};
                    }
                }
                const int left_bits = value_bits(left);
                const int right_bits = value_bits(right);
                const int common_bits = left_bits > right_bits ? left_bits : right_bits;
                bool common_unsigned = false;
                if (left.elem_unsigned == right.elem_unsigned) {
                    common_unsigned = left.elem_unsigned;
                } else if (left_bits == right_bits) {
                    common_unsigned = true;
                } else if (left.elem_unsigned && left_bits > right_bits) {
                    common_unsigned = true;
                } else if (right.elem_unsigned && right_bits > left_bits) {
                    common_unsigned = true;
                }
                lhs = resize_int(lhs, bits_type(common_bits), left.elem_unsigned);
                rhs = resize_int(rhs, bits_type(common_bits), right.elem_unsigned);
                auto widened = [&](llvm::Value *value) -> TypedValue {
                    return {value, 0, false, -1, false, false, false, common_bits, common_unsigned};
                };
                auto compared = [&](llvm::CmpInst::Predicate pred) -> TypedValue {
                    return {builder_.CreateZExt(builder_.CreateICmp(pred, lhs, rhs), i32()), 0};
                };
                const bool uns = common_unsigned;
                switch (node.op) {
                case TokenKind::Plus: return widened(builder_.CreateAdd(lhs, rhs));
                case TokenKind::Minus: return widened(builder_.CreateSub(lhs, rhs));
                case TokenKind::Star: return widened(builder_.CreateMul(lhs, rhs));
                case TokenKind::Slash: return widened(uns ? builder_.CreateUDiv(lhs, rhs) : builder_.CreateSDiv(lhs, rhs));
                case TokenKind::Percent: return widened(uns ? builder_.CreateURem(lhs, rhs) : builder_.CreateSRem(lhs, rhs));
                case TokenKind::Amp: return widened(builder_.CreateAnd(lhs, rhs));
                case TokenKind::Pipe: return widened(builder_.CreateOr(lhs, rhs));
                case TokenKind::Caret: return widened(builder_.CreateXor(lhs, rhs));
                case TokenKind::LtLt: return widened(builder_.CreateShl(lhs, rhs));
                case TokenKind::GtGt:
                    return widened(uns ? builder_.CreateLShr(lhs, rhs) : builder_.CreateAShr(lhs, rhs));
                case TokenKind::EqEq: return compared(llvm::CmpInst::ICMP_EQ);
                case TokenKind::BangEq: return compared(llvm::CmpInst::ICMP_NE);
                case TokenKind::Lt: return compared(uns ? llvm::CmpInst::ICMP_ULT : llvm::CmpInst::ICMP_SLT);
                case TokenKind::Gt: return compared(uns ? llvm::CmpInst::ICMP_UGT : llvm::CmpInst::ICMP_SGT);
                case TokenKind::Le: return compared(uns ? llvm::CmpInst::ICMP_ULE : llvm::CmpInst::ICMP_SLE);
                case TokenKind::Ge: return compared(uns ? llvm::CmpInst::ICMP_UGE : llvm::CmpInst::ICMP_SGE);
                default: return {zero(), 0};
                }
            } else if constexpr (std::is_same_v<T, Expr::Assign>) {
                arm_union_tag_ = true;
                const TypedValue slot = emit_addr(*node.lhs);
                arm_union_tag_ = false;
                const TypedValue rhs = emit_rvalue(*node.rhs);
                if (slot.is_slice) {
                    if (slot.value != nullptr && rhs.value != nullptr && !terminated()) {
                        builder_.CreateStore(as_slice_value(rhs, node.loc), slot.value);
                    }
                    return rhs;
                }
                llvm::Value *stored = nullptr;
                if (slot.value != nullptr && rhs.value != nullptr) {
                    stored = stored_value(rhs, slot);
                    if (!terminated()) {
                        builder_.CreateStore(stored, slot.value);
                    }
                }
                if (slot.depth == 0 && slot.elem_bool && stored != nullptr) {
                    return {builder_.CreateZExt(stored, i32()), 0};
                }
                if (rhs.depth == 0 && slot.depth > 0) {
                    return {null_ptr(), slot.depth, slot.elem_char, slot.struct_id, false, slot.elem_void};
                }
                return rhs;
            } else if constexpr (std::is_same_v<T, Expr::Call>) {
                if (node.type_name) {
                    const auto *selector = node.callee == nullptr ? nullptr : std::get_if<Expr::Selector>(&node.callee->data);
                    if (selector == nullptr || selector->base == nullptr) {
                        error(node.loc, "type mismatch");
                        return {zero(), 0};
                    }
                    auto name_pointer = [&](llvm::Value *value, int depth) -> TypedValue {
                        if (value == nullptr || depth < 1 || depth > 2) {
                            error(node.loc, "type mismatch");
                            return {zero(), 0};
                        }
                        if (depth == 2) {
                            value = builder_.CreateLoad(ptr_type(), value);
                        }
                        return {value, 1, true};
                    };
                    if (addressable(*selector->base)) {
                        const TypedValue addr = emit_addr(*selector->base);
                        if (addr.value == nullptr) {
                            return {zero(), 0};
                        }
                        llvm::Value *loaded = builder_.CreateLoad(ptr_type(), addr.value);
                        return name_pointer(loaded, addr.depth);
                    }
                    const TypedValue base = emit_rvalue(*selector->base);
                    return name_pointer(base.value, base.depth);
                }
                if (node.callee != nullptr && is_std_builtin_call(*node.callee, "va")) {
                    return emit_builtin_va(node.loc);
                }
                if (node.callee != nullptr) {
                    if (const auto *named = std::get_if<Expr::Name>(&node.callee->data)) {
                        if (const Binding *binding = lookup(named->text);
                            binding != nullptr && binding->is_generic && binding->decl != nullptr) {
                            return emit_generic_call(*binding, node);
                        }
                        if (lookup(named->text) == nullptr && node.args.size() == 1 && node.args.front() != nullptr) {
                            int enum_id = enum_named(current_file_, named->text);
                            if (enum_id < 0) {
                                enum_id = selective_enum(current_file_, named->text);
                            }
                            if (enum_id >= 0 && enum_id < static_cast<int>(enums_.size())) {
                                const TypedValue source = emit_rvalue(*node.args.front());
                                if (source.value != nullptr && source.depth == 0 && source.struct_id >= 0 &&
                                    source.struct_id < static_cast<int>(structs_.size()) &&
                                    source.value->getType()->isStructTy() &&
                                    structs_[static_cast<std::size_t>(source.struct_id)].tag_enum == enum_id) {
                                    llvm::Value *tag = builder_.CreateExtractValue(source.value, 2);
                                    const EnumLayout &enumeration = enums_[static_cast<std::size_t>(enum_id)];
                                    TypedValue value;
                                    value.value = tag;
                                    value.enum_id = enum_id;
                                    value.elem_bool = enumeration.is_bool;
                                    value.elem_char = enumeration.is_char;
                                    value.elem_unsigned = enumeration.is_unsigned;
                                    value.elem_bits = enumeration.is_bool ? 0 : enumeration.bits;
                                    return value;
                                }
                                error(node.loc, "type mismatch");
                                return {zero(), 0};
                            }
                        }
                    }
                }
                return emit_prepared(prepare_call(node));
            } else if constexpr (std::is_same_v<T, Expr::Selector>) {
                if (node.slice_len) {
                    const TypedValue base = node.base != nullptr ? emit_rvalue(*node.base) : TypedValue{};
                    if (base.value == nullptr || !base.is_slice) {
                        error(node.loc, "type mismatch");
                        return {zero(), 0};
                    }
                    return {builder_.CreateExtractValue(base.value, 1), 0};
                }
                if (!node.resolved_owner.empty()) {
                    const std::string key = node.resolved_owner + "." + node.field;
                    const Binding *global = visible(assoc_home(node), key, false);
                    if (global != nullptr && (global->value != nullptr || global->immediate)) {
                        return value_of_binding(*global);
                    }
                    const Binding *method = static_method_binding(current_file_, node.resolved_owner, node.field);
                    if (method != nullptr && method->value != nullptr) {
                        return value_of_binding(*method);
                    }
                    error(node.loc, "undeclared identifier");
                    return {zero(), 0};
                }
                if (node.base != nullptr) {
                    const int enum_id = enum_type_designator(*node.base);
                    if (enum_id >= 0 && enum_id < static_cast<int>(enums_.size())) {
                        const EnumLayout &layout = enums_[static_cast<std::size_t>(enum_id)];
                        const auto member = layout.values.find(node.field);
                        if (member != layout.values.end()) {
                            TypedValue value;
                            value.value = enum_constant(enum_id, member->second);
                            value.enum_id = enum_id;
                            value.elem_bool = layout.is_bool;
                            value.elem_char = layout.is_char;
                            value.elem_unsigned = layout.is_unsigned;
                            value.elem_bits = layout.is_bool ? 0 : layout.bits;
                            return value;
                        }
                    }
                    if (const auto *named = std::get_if<Expr::Name>(&node.base->data)) {
                        if (unshadowed_type_name(named->text)) {
                            const Binding *method = static_method_binding(current_file_, named->text, node.field);
                            auto *function = method == nullptr ? nullptr : llvm::dyn_cast<llvm::Function>(method->value);
                            if (function == nullptr) {
                                error(node.loc, "type mismatch");
                                return {zero(), 0};
                            }
                            return value_of_binding(*method);
                        }
                    }
                }
                const auto *base_name = node.base == nullptr ? nullptr : std::get_if<Expr::Name>(&node.base->data);
                const auto package = base_name == nullptr ? packages_.end() : packages_.find(base_name->text);
                if (base_name != nullptr && package != packages_.end()) {
                    if (const Binding *function = visible(package->second, node.field, true)) {
                        return value_of_binding(*function);
                    }
                    if (const Binding *global = visible(package->second, node.field, false)) {
                        return value_of_binding(*global);
                    }
                    error(node.loc, "undeclared identifier");
                    return {zero(), 0};
                }
                if (node.base != nullptr && addressable(*node.base)) {
                    const TypedValue addr = emit_addr(expr);
                    if (addr.value == nullptr) {
                        return {zero(), 0};
                    }
                    return load_field(addr);
                }
                const TypedValue base = node.base != nullptr ? emit_rvalue(*node.base) : TypedValue{};
                if (base.value != nullptr && base.depth == 1 && base.struct_id >= 0) {
                    const TypedValue addr = field_addr(base.struct_id, base.value, node.field, node.loc);
                    if (addr.value == nullptr) {
                        return {zero(), 0};
                    }
                    return load_field(addr);
                }
                if (base.value == nullptr || base.depth != 0 || base.struct_id < 0) {
                    error(node.loc, "type mismatch");
                    return {zero(), 0};
                }
                int field_index = -1;
                const FieldLayout *field = find_field(base.struct_id, node.field, field_index);
                if (field == nullptr || field_index < 0) {
                    error(node.loc, "undeclared identifier");
                    return {zero(), 0};
                }
                if (field->is_array) {
                    error(node.loc, "not supported yet");
                    return {zero(), 0};
                }
                if (structs_[static_cast<std::size_t>(base.struct_id)].is_union) {
                    llvm::AllocaInst *slot =
                        alloca_typed(structs_[static_cast<std::size_t>(base.struct_id)].type, "uval");
                    if (base.value != nullptr) {
                        builder_.CreateStore(base.value, slot);
                    }
                    if (structs_[static_cast<std::size_t>(base.struct_id)].tag_enum >= 0) {
                        const TypedValue addr = field_addr(base.struct_id, slot, node.field, node.loc);
                        if (addr.value == nullptr) {
                            return {zero(), 0};
                        }
                        return load_object(addr.value, addr.depth, addr.elem_char, addr.struct_id, addr.elem_void,
                                           addr.elem_bool, addr.elem_bits, addr.elem_unsigned, addr.elem_float);
                    }
                    return load_object(slot, field->depth, field->elem_char, field->struct_id, field->elem_void,
                                       field->elem_bool, field->elem_bits, field->elem_unsigned, field->elem_float);
                }
                llvm::Value *extracted = builder_.CreateExtractValue(base.value, static_cast<unsigned>(field_index));
                if (field->is_slice) {
                    TypedValue loaded;
                    loaded.value = extracted;
                    loaded.is_slice = true;
                    loaded.array_len = -1;
                    loaded.depth = field->depth;
                    loaded.elem_char = field->elem_char;
                    loaded.struct_id = field->struct_id;
                    loaded.elem_void = field->elem_void;
                    loaded.elem_bool = field->elem_bool;
                    loaded.elem_bits = field->elem_bits;
                    loaded.elem_unsigned = field->elem_unsigned;
                    loaded.elem_float = field->elem_float;
                    return loaded;
                }
                if (field->depth == 0 && field->elem_bool) {
                    return {builder_.CreateZExt(extracted, i32()), 0, false, -1, false, field->elem_void, true};
                }
                if (field->depth == 0 && (field->elem_char || field->elem_bits == 8 || field->elem_bits == 16)) {
                    const bool zero_extend = field->elem_unsigned || field->elem_char;
                    llvm::Value *wide =
                        zero_extend ? builder_.CreateZExt(extracted, i32()) : builder_.CreateSExt(extracted, i32());
                    const int bits = field->elem_bits == 0 ? 8 : field->elem_bits;
                    return {wide, 0, field->elem_char, -1, false, field->elem_void, false, bits, field->elem_unsigned};
                }
                if (field->depth == 0 && field->elem_float) {
                    return {extracted, 0, false, -1, false, false, false, field->elem_bits, false, -1, true};
                }
                if (field->depth == 0 && field->elem_bits == 64) {
                    return {extracted, 0, false, -1, false, false, false, 64, field->elem_unsigned};
                }
                if (field->depth == 0 && field->elem_unsigned) {
                    return {extracted, 0, false, -1, false, false, false, 32, true};
                }
                return {extracted, field->depth, field->elem_char, field->struct_id, false, field->elem_void,
                        field->elem_bool, field->elem_bits, field->elem_unsigned, -1, field->elem_float};
            } else if constexpr (std::is_same_v<T, Expr::String>) {
                llvm::Constant *data = llvm::ConstantDataArray::getString(context_, node.text, true);
                auto *global = new llvm::GlobalVariable(module_, data->getType(), true,
                                                        llvm::GlobalValue::PrivateLinkage, data, ".str");
                TypedValue out;
                out.value = global;
                out.depth = 1;
                out.elem_bits = 8;
                out.elem_unsigned = true;
                return out;
            } else if constexpr (std::is_same_v<T, Expr::Index>) {
                if (node.end != nullptr) {
                    return emit_range(node);
                }
                if (node.tuple_index >= 0) {
                    if (node.base == nullptr) {
                        error(node.loc, "type mismatch");
                        return {zero(), 0};
                    }
                    const TypedValue base = emit_rvalue(*node.base);
                    if (base.value == nullptr || !base.value->getType()->isStructTy()) {
                        error(node.loc, "type mismatch");
                        return {zero(), 0};
                    }
                    auto *struct_ty = llvm::cast<llvm::StructType>(base.value->getType());
                    const auto index = static_cast<unsigned>(node.tuple_index);
                    if (index >= struct_ty->getNumElements()) {
                        error(node.loc, "type mismatch");
                        return {zero(), 0};
                    }
                    llvm::Value *extracted = builder_.CreateExtractValue(base.value, index);
                    const auto *named = std::get_if<Expr::Name>(&node.base->data);
                    const Binding *binding = named == nullptr ? nullptr : lookup(named->text);
                    if (binding != nullptr && index < binding->tuple_parts.size()) {
                        const ResultSlot &slot = binding->tuple_parts[index];
                        const Lowered part = lower(current_file_, slot.type_name, slot.type_package, slot.is_struct,
                                                   slot.indirections, slot.is_array, slot.array_len, false);
                        if (part.depth == 0 && part.elem_bool) {
                            return {builder_.CreateZExt(extracted, i32()), 0, false, -1, false, part.elem_void, true};
                        }
                        if (part.depth == 0 && (part.elem_char || part.elem_bits == 8 || part.elem_bits == 16)) {
                            const bool zero_extend = part.elem_unsigned || part.elem_char;
                            llvm::Value *wide = zero_extend ? builder_.CreateZExt(extracted, i32())
                                                            : builder_.CreateSExt(extracted, i32());
                            const int bits = part.elem_bits == 0 ? 8 : part.elem_bits;
                            return {wide, 0, part.elem_char, -1, false, part.elem_void, false, bits, part.elem_unsigned};
                        }
                        if (part.depth == 0 && part.elem_float) {
                            return {extracted, 0, false, -1, false, false, false, part.elem_bits, false, -1, true};
                        }
                        if (part.depth == 0 && part.elem_bits == 64) {
                            return {extracted, 0, false, -1, false, false, false, 64, part.elem_unsigned};
                        }
                        if (part.depth == 0 && part.elem_unsigned) {
                            return {extracted, 0, false, -1, false, false, false, 32, true};
                        }
                        return {extracted, part.depth, part.elem_char, part.struct_id, false, part.elem_void,
                                part.elem_bool, part.elem_bits, part.elem_unsigned, -1, part.elem_float};
                    }
                    llvm::Type *field_ty = extracted->getType();
                    if (field_ty->isIntegerTy(1) || field_ty->isIntegerTy(8) || field_ty->isIntegerTy(16)) {
                        return {builder_.CreateZExt(extracted, i32()), 0};
                    }
                    if (field_ty->isFloatingPointTy()) {
                        const int bits = field_ty->getPrimitiveSizeInBits() >= 64 ? 64 : 32;
                        return {extracted, 0, false, -1, false, false, false, bits, false, -1, true};
                    }
                    if (field_ty->isIntegerTy(64)) {
                        return {extracted, 0, false, -1, false, false, false, 64, false};
                    }
                    if (field_ty->isPointerTy()) {
                        return {extracted, 1};
                    }
                    return {extracted, 0};
                }
                const TypedValue addr = emit_addr(expr);
                if (addr.value == nullptr) {
                    return {zero(), 0};
                }
                if (addr.row_decay) {
                    TypedValue row = addr;
                    row.row_decay = false;
                    return row;
                }
                TypedValue loaded = load_object(addr.value, addr.depth, addr.elem_char, addr.struct_id, addr.elem_void,
                                                addr.elem_bool, addr.elem_bits, addr.elem_unsigned, addr.elem_float);
                loaded.fn_ty = addr.fn_ty;
                loaded.fn_ret_depth = addr.fn_ret_depth;
                loaded.fn_ret_float = addr.fn_ret_float;
                loaded.fn_ret_bits = addr.fn_ret_bits;
                loaded.fn_ret_unsigned = addr.fn_ret_unsigned;
                loaded.fn_ret_char = addr.fn_ret_char;
                loaded.fn_ret_bool = addr.fn_ret_bool;
                loaded.fn_ret_struct = addr.fn_ret_struct;
                loaded.fn_results = addr.fn_results;
                if (loaded.enum_id < 0) {
                    loaded.enum_id = addr.enum_id;
                }
                return loaded;
            } else if constexpr (std::is_same_v<T, Expr::Sizeof>) {
                llvm::Type *measured = nullptr;
                if (node.on_type) {
                    const Lowered low = lower(current_file_, node.type_name, node.type_package, node.is_struct, node.indirections,
                                              node.is_array, node.is_array ? node.array_len : 0, false);
                    if (low.depth == 0 && low.elem_void) {
                        error(node.loc, "not supported yet");
                        return {zero(), 0};
                    }
                    measured = low.type;
                } else if (node.expr) {
                    const Shape shape = raw_shape(*node.expr);
                    if (shape.is_function || (shape.depth == 0 && shape.elem_void)) {
                        error(node.loc, "not supported yet");
                        return {zero(), 0};
                    }
                    measured = shape.type;
                }
                if (measured == nullptr || measured->isVoidTy() || measured->isFunctionTy()) {
                    error(node.loc, "not supported yet");
                    return {zero(), 0};
                }
                const uint64_t bytes = module_.getDataLayout().getTypeAllocSize(measured).getFixedValue();
                if (bytes > 2147483647ULL) {
                    error(node.loc, "not supported yet");
                    return {zero(), 0};
                }
                return {const_i32(static_cast<int32_t>(bytes)), 0};
            } else if constexpr (std::is_same_v<T, Expr::New>) {
                return emit_new(node);
            } else if constexpr (std::is_same_v<T, Expr::Delete>) {
                emit_delete(node);
                return {zero(), 0, false, -1, false, true};
            } else if constexpr (std::is_same_v<T, Expr::Typeof>) {
                if (node.canonical.empty()) {
                    error(node.loc, "type mismatch");
                    return {zero(), 0};
                }
                return {intern_type_name(node.canonical), 1};
            } else if constexpr (std::is_same_v<T, Expr::Bool>) {
                return {const_i32(node.value ? 1 : 0), 0};
            } else if constexpr (std::is_same_v<T, Expr::Nullptr>) {
                return {null_ptr(), 1, false, -1, false, true};
            } else if constexpr (std::is_same_v<T, Expr::Cast>) {
                const TypedValue source = node.expr ? emit_rvalue(*node.expr) : TypedValue{};
                const Lowered target =
                    node.is_fn_ptr ? lower_fnptr(current_file_, node.type_name, node.type_package, node.is_struct,
                                                 node.is_enum, node.ret_indirections, node.indirections, node.fn_params,
                                                 node.fn_variadic, node.fn_results)
                                   : lower(current_file_, node.type_name, node.type_package, node.is_struct,
                                           node.indirections, false, 0, false);
                if (source.value == nullptr) {
                    return {zero(), 0};
                }
                // The call uses this signature. A data pointer and another function pointer are both an address.
                auto stamp_fn = [&](llvm::Value *bits) {
                    TypedValue value;
                    value.value = bits;
                    value.depth = target.depth > 0 ? target.depth : 1;
                    value.fn_ty = target.fn_ty;
                    value.fn_ret_depth = target.fn_ret_depth;
                    value.fn_ret_float = target.fn_ret_float;
                    value.fn_ret_bits = target.fn_ret_bits;
                    value.fn_ret_unsigned = target.fn_ret_unsigned;
                    value.fn_ret_char = target.fn_ret_char;
                    value.fn_ret_bool = target.fn_ret_bool;
                    value.fn_ret_struct = target.fn_ret_struct;
                    value.fn_results = target.fn_results;
                    return value;
                };
                if (target.fn_ty != nullptr &&
                    (source.depth > 0 || source.value->getType()->isPointerTy())) {
                    return stamp_fn(source.value);
                }
                if (source.depth == 0 && source.struct_id >= 0 && source.struct_id < static_cast<int>(structs_.size()) &&
                    source.value->getType()->isStructTy()) {
                    const StructLayout &layout = structs_[static_cast<std::size_t>(source.struct_id)];
                    if (layout.tag_enum >= 0 && target.enum_id == layout.tag_enum &&
                        layout.tag_enum < static_cast<int>(enums_.size()) && layout.type != nullptr &&
                        layout.type->getNumElements() > 2) {
                        llvm::Value *tag = builder_.CreateExtractValue(source.value, 2);
                        const EnumLayout &enumeration = enums_[static_cast<std::size_t>(layout.tag_enum)];
                        TypedValue value;
                        value.value = tag;
                        value.enum_id = layout.tag_enum;
                        value.elem_bool = enumeration.is_bool;
                        value.elem_char = enumeration.is_char;
                        value.elem_unsigned = enumeration.is_unsigned;
                        value.elem_bits = enumeration.is_bool ? 0 : enumeration.bits;
                        return value;
                    }
                }
                if (target.depth > 0 && source.depth > 0) {
                    return {source.value, target.depth, target.elem_char, target.struct_id, false, target.elem_void,
                            target.elem_bool, target.elem_bits, target.elem_unsigned, target.trait_id, target.elem_float};
                }
                if (target.depth > 0 && source.depth == 0) {
                    llvm::IntegerType *intptr = module_.getDataLayout().getIntPtrType(context_, 0);
                    llvm::Value *wide = source.value;
                    if (wide->getType()->isIntegerTy() && wide->getType() != intptr) {
                        if (wide->getType()->getIntegerBitWidth() < intptr->getIntegerBitWidth()) {
                            wide = source.elem_unsigned ? builder_.CreateZExt(wide, intptr)
                                                        : builder_.CreateSExt(wide, intptr);
                        } else {
                            wide = builder_.CreateTrunc(wide, intptr);
                        }
                    }
                    llvm::Value *pointer = builder_.CreateIntToPtr(wide, ptr_type());
                    if (target.fn_ty != nullptr) {
                        return stamp_fn(pointer);
                    }
                    return {pointer, target.depth, target.elem_char, target.struct_id, false, target.elem_void,
                            target.elem_bool, target.elem_bits, target.elem_unsigned, target.trait_id, target.elem_float};
                }
                if (source.depth > 0 && target.depth == 0 && !target.elem_char && !target.elem_void && target.struct_id < 0 &&
                    !target.elem_bool && target.elem_bits != 8 && target.elem_bits != 16 && target.elem_bits != 64) {
                    llvm::IntegerType *intptr = module_.getDataLayout().getIntPtrType(context_, 0);
                    llvm::Value *wide = builder_.CreatePtrToInt(source.value, intptr);
                    llvm::Value *narrow = wide->getType() == i32() ? wide : builder_.CreateTrunc(wide, i32());
                    return {narrow, 0};
                }
                // An alias and its representation are the same bits. The cast only changes the type.
                auto scalar_bits = [](int bits, bool is_bool, bool is_char) {
                    if (is_bool) {
                        return 1;
                    }
                    if (bits == 0 && is_char) {
                        return 8;
                    }
                    if (bits == 0) {
                        return 32;
                    }
                    return bits;
                };
                const bool scalar_source = source.depth == 0 && source.struct_id < 0 && !source.elem_float &&
                                           source.value->getType()->isIntegerTy();
                const bool scalar_target = target.depth == 0 && target.struct_id < 0 && !target.elem_float &&
                                           !target.elem_void && target.fn_ty == nullptr;
                if (scalar_source && scalar_target && source.elem_unsigned == target.elem_unsigned &&
                    source.elem_bool == target.elem_bool &&
                    scalar_bits(source.elem_bits, source.elem_bool, source.elem_char) ==
                        scalar_bits(target.elem_bits, target.elem_bool, target.elem_char)) {
                    TypedValue value = source;
                    value.elem_bits = target.elem_bits;
                    value.elem_bool = target.elem_bool;
                    value.elem_char = target.elem_char;
                    value.elem_unsigned = target.elem_unsigned;
                    value.enum_id = target.enum_id;
                    return value;
                }
                if (target.depth == 0 && source.depth == 0 && target.struct_id >= 0 &&
                    source.struct_id == target.struct_id && source.value->getType()->isStructTy()) {
                    return source;
                }
                if (target.depth == 0 && source.depth == 0 && target.elem_float && source.elem_float &&
                    target.elem_bits == source.elem_bits) {
                    TypedValue value = source;
                    value.elem_float = true;
                    value.elem_bits = target.elem_bits;
                    return value;
                }
                if (target.depth == 0 && source.depth == 0 && (target.elem_float || source.elem_float ||
                                                              source.value->getType()->isFloatingPointTy())) {
                    llvm::Type *dest = target.elem_float ? (target.elem_bits >= 64 ? f64_ty() : f32_ty())
                                                         : bits_type(target.elem_bits == 0 ? 32 : target.elem_bits);
                    llvm::Value *converted = as_stored(source.value, 0, dest, source.elem_unsigned);
                    if (target.elem_float) {
                        return {converted, 0, false, -1, false, false, false, target.elem_bits, false, -1, true};
                    }
                    return {converted, 0, target.elem_char, -1, false, false, target.elem_bool, target.elem_bits,
                            target.elem_unsigned};
                }
                if (target.depth == 0 && source.depth == 0 && target.struct_id < 0 && !target.elem_void) {
                    int bits = target.elem_bits;
                    if (target.elem_bool) {
                        bits = 1;
                    } else if (bits == 0 && target.elem_char) {
                        bits = 8;
                    } else if (bits == 0) {
                        bits = 32;
                    }
                    llvm::Value *converted = resize_int(source.value, bits_type(bits), source.elem_unsigned);
                    if (bits < 32) {
                        const bool zero_extend = target.elem_unsigned || target.elem_char;
                        converted = zero_extend ? builder_.CreateZExt(converted, i32()) : builder_.CreateSExt(converted, i32());
                        return {converted, 0};
                    }
                    return {converted, 0, false, -1, false, false, false, bits, target.elem_unsigned};
                }
                return {source.value, 0};
            } else if constexpr (std::is_same_v<T, Expr::Func>) {
                if (node.decl == nullptr) {
                    return {zero(), 0};
                }
                return emit_anon_function(*node.decl);
            } else if constexpr (std::is_same_v<T, Expr::Init>) {
                if (node.array_literal) {
                    Lowered low;
                    llvm::AllocaInst *slot = emit_array_literal(node, low);
                    if (slot == nullptr) {
                        return {zero(), 0};
                    }
                    // Same shape as a loaded array name: the address, one level above the element.
                    TypedValue value{slot,          low.depth + 1, low.elem_char,     low.struct_id, false,
                                     low.elem_void, low.elem_bool, low.elem_bits,     low.elem_unsigned, -1,
                                     low.elem_float};
                    value.array_len = low.array_len;
                    value.inner_array_len = low.inner_array_len;
                    value.fn_ty = low.fn_ty;
                    value.fn_ret_depth = low.fn_ret_depth;
                    value.fn_ret_float = low.fn_ret_float;
                    value.fn_ret_bits = low.fn_ret_bits;
                    value.fn_ret_unsigned = low.fn_ret_unsigned;
                    value.fn_ret_char = low.fn_ret_char;
                    value.fn_ret_bool = low.fn_ret_bool;
                    value.fn_ret_struct = low.fn_ret_struct;
                    value.fn_results = low.fn_results;
                    value.enum_id = low.enum_id;
                    return value;
                }
                if (node.is_tuple) {
                    std::vector<llvm::Value *> fields;
                    std::vector<llvm::Type *> types;
                    std::vector<TypedValue> element_values;
                    fields.reserve(node.elements.size());
                    types.reserve(node.elements.size());
                    element_values.reserve(node.elements.size());
                    for (const auto &element : node.elements) {
                        TypedValue value = element ? emit_rvalue(*element) : TypedValue{};
                        if (terminated()) {
                            return {zero(), 0};
                        }
                        if (value.value == nullptr) {
                            value.value = zero();
                        }
                        // An rvalue promotes char, bool, and a narrow integer to int. The tuple keeps
                        // the type sema gave the element, the same width a function result uses.
                        auto restore = [&](llvm::Type *ty, bool elem_char, bool elem_bool, int elem_bits,
                                           bool elem_unsigned) {
                            if (ty == nullptr || value.value == nullptr) {
                                return;
                            }
                            if (value.value->getType() == ty) {
                                value.elem_char = elem_char;
                                value.elem_bool = elem_bool;
                                value.elem_bits = elem_bits;
                                value.elem_unsigned = elem_unsigned;
                                return;
                            }
                            if (!value.value->getType()->isIntegerTy() || !ty->isIntegerTy() ||
                                ty->getIntegerBitWidth() >= value.value->getType()->getIntegerBitWidth()) {
                                return;
                            }
                            value.value = as_stored(value.value, 0, ty, elem_unsigned || elem_char);
                            value.elem_char = elem_char;
                            value.elem_bool = elem_bool;
                            value.elem_bits = elem_bits;
                            value.elem_unsigned = elem_unsigned;
                            value.elem_float = false;
                            value.depth = 0;
                            value.struct_id = -1;
                        };
                        if (element != nullptr && std::holds_alternative<Expr::Char>(element->data)) {
                            restore(i8(), false, false, 8, true);
                        } else if (element != nullptr && std::holds_alternative<Expr::Bool>(element->data)) {
                            restore(i1(), false, true, 0, false);
                        } else if (element != nullptr) {
                            if (const auto *number = std::get_if<Expr::Int>(&element->data);
                                number != nullptr && (number->suffix.bits == 8 || number->suffix.bits == 16)) {
                                llvm::Type *ty = number->suffix.bits == 8 ? i8() : i16();
                                restore(ty, false, false, number->suffix.bits, number->suffix.is_unsigned);
                            } else if (const auto *named = std::get_if<Expr::Name>(&element->data)) {
                                const Binding *binding = lookup(named->text);
                                if (binding != nullptr && !binding->is_tuple && !binding->is_function &&
                                    !binding->is_array && binding->depth == 0 && binding->struct_id < 0 &&
                                    binding->value != nullptr) {
                                    restore(object_type(binding->value), binding->elem_char, binding->elem_bool,
                                            binding->elem_bits, binding->elem_unsigned);
                                    if (binding->enum_id >= 0) {
                                        value.enum_id = binding->enum_id;
                                    }
                                }
                            }
                        }
                        fields.push_back(value.value);
                        types.push_back(value.value->getType());
                        element_values.push_back(std::move(value));
                    }
                    llvm::StructType *aggregate_type = llvm::StructType::get(context_, types);
                    llvm::Value *aggregate = llvm::ConstantAggregateZero::get(aggregate_type);
                    for (unsigned index = 0; index < fields.size(); ++index) {
                        aggregate = builder_.CreateInsertValue(aggregate, fields[index], index);
                    }
                    auto slot_of = [&](const TypedValue &element) {
                        ResultSlot slot;
                        auto scalar = [&]() -> std::string {
                            if (element.elem_bool) {
                                return "bool";
                            }
                            if (element.elem_char) {
                                return "u8";
                            }
                            if (element.elem_float) {
                                return element.elem_bits >= 64 ? "f64" : "f32";
                            }
                            if (element.elem_bits == 64) {
                                return element.elem_unsigned ? "u64" : "i64";
                            }
                            if (element.elem_bits == 16) {
                                return element.elem_unsigned ? "u16" : "i16";
                            }
                            if (element.elem_bits == 8) {
                                return element.elem_unsigned ? "u8" : "i8";
                            }
                            if (element.elem_unsigned) {
                                return "u32";
                            }
                            return "i32";
                        };
                        if (element.depth > 0) {
                            slot.indirections = element.depth;
                            if (element.struct_id >= 0 && element.struct_id < static_cast<int>(structs_.size())) {
                                slot.is_struct = true;
                                slot.type_name = structs_[static_cast<std::size_t>(element.struct_id)].name;
                            } else if (element.elem_void) {
                                slot.type_name = "void";
                            } else {
                                slot.type_name = scalar();
                            }
                            return slot;
                        }
                        if (element.struct_id >= 0 && element.struct_id < static_cast<int>(structs_.size())) {
                            slot.is_struct = true;
                            slot.type_name = structs_[static_cast<std::size_t>(element.struct_id)].name;
                            return slot;
                        }
                        if (element.enum_id >= 0 && element.enum_id < static_cast<int>(enums_.size())) {
                            slot.is_enum = true;
                            slot.type_name = enums_[static_cast<std::size_t>(element.enum_id)].name;
                            return slot;
                        }
                        slot.type_name = scalar();
                        return slot;
                    };
                    TypedValue out;
                    out.value = aggregate;
                    out.is_tuple = true;
                    out.tuple_parts.reserve(element_values.size());
                    for (const TypedValue &element : element_values) {
                        out.tuple_parts.push_back(slot_of(element));
                    }
                    return out;
                }
                if (node.elements.size() == 1 && node.elements.front()) {
                    return emit_rvalue(*node.elements.front());
                }
                return emit_init(node);
            } else {
                return {zero(), 0};
            }
        },
        expr.data);
}

void CodeGen::collect_cases(const Stmt &stmt, std::vector<CaseArm> &arms) const {
    std::visit(
        [&](const auto &node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Stmt::Case>) {
                CaseArm arm;
                arm.node = &node;
                arm.is_default = node.is_default;
                if (!node.is_default && node.value) {
                    if (const std::optional<int32_t> folded = fold(*node.value)) {
                        arm.value = *folded;
                        arm.has_value = true;
                    }
                }
                arms.push_back(arm);
                if (node.stmt) {
                    collect_cases(*node.stmt, arms);
                }
            } else if constexpr (std::is_same_v<T, Stmt::Block>) {
                for (const auto &child : node.statements) {
                    if (child) {
                        collect_cases(*child, arms);
                    }
                }
            } else if constexpr (std::is_same_v<T, Stmt::If>) {
                if (node.then_arm) {
                    collect_cases(*node.then_arm, arms);
                }
                if (node.else_arm) {
                    collect_cases(*node.else_arm, arms);
                }
            } else if constexpr (std::is_same_v<T, Stmt::While> || std::is_same_v<T, Stmt::DoWhile>) {
                if (node.body) {
                    collect_cases(*node.body, arms);
                }
            } else if constexpr (std::is_same_v<T, Stmt::For>) {
                if (node.init) {
                    collect_cases(*node.init, arms);
                }
                if (node.body) {
                    collect_cases(*node.body, arms);
                }
            } else if constexpr (std::is_same_v<T, Stmt::Switch>) {
                return;
            } else {
                return;
            }
        },
        stmt.data);
}

void CodeGen::emit_case(const Stmt::Case &node) {
    llvm::BasicBlock *block = nullptr;
    for (auto it = case_blocks_.rbegin(); it != case_blocks_.rend(); ++it) {
        const auto found = it->find(&node);
        if (found != it->end()) {
            block = found->second;
            break;
        }
    }
    if (block == nullptr) {
        error(node.loc, node.is_default ? "default outside switch" : "case outside switch");
        if (node.stmt && !terminated()) {
            emit_stmt(*node.stmt);
        }
        return;
    }
    if (!terminated()) {
        builder_.CreateBr(block);
    }
    builder_.SetInsertPoint(block);
    if (node.stmt) {
        emit_stmt(*node.stmt);
    }
}

void CodeGen::emit_switch(const Stmt::Switch &node) {
    const TypedValue cond = node.cond ? emit_rvalue(*node.cond) : TypedValue{zero(), 0};
    if (terminated()) {
        return;
    }
    std::vector<CaseArm> arms;
    if (node.body) {
        collect_cases(*node.body, arms);
    }
    llvm::BasicBlock *end_block = llvm::BasicBlock::Create(context_, "switch.end", function_);
    llvm::BasicBlock *default_block = end_block;
    unsigned cases = 0;
    for (CaseArm &arm : arms) {
        if (arm.is_default) {
            if (default_block == end_block) {
                arm.block = llvm::BasicBlock::Create(context_, "switch.default", function_);
                default_block = arm.block;
            }
            continue;
        }
        arm.block = llvm::BasicBlock::Create(context_, "switch.case", function_);
        if (arm.has_value) {
            ++cases;
        }
    }
    llvm::SwitchInst *inst = builder_.CreateSwitch(cond.value == nullptr ? zero() : cond.value, default_block, cases);
    for (const CaseArm &arm : arms) {
        if (!arm.is_default && arm.has_value && arm.block != nullptr) {
            inst->addCase(llvm::cast<llvm::ConstantInt>(const_i32(arm.value)), arm.block);
        }
    }
    std::unordered_map<const Stmt::Case *, llvm::BasicBlock *> blocks;
    for (const CaseArm &arm : arms) {
        if (arm.node != nullptr && arm.block != nullptr) {
            blocks.emplace(arm.node, arm.block);
        }
    }
    case_blocks_.push_back(std::move(blocks));
    llvm::BasicBlock *step = loops_.empty() ? nullptr : loops_.back().step;
    loops_.push_back(LoopTargets{step, end_block, true});
    llvm::BasicBlock *skip_block = llvm::BasicBlock::Create(context_, "switch.skip", function_);
    builder_.SetInsertPoint(skip_block);
    if (node.body) {
        emit_stmt(*node.body);
    }
    if (!terminated()) {
        builder_.CreateBr(end_block);
    }
    loops_.pop_back();
    case_blocks_.pop_back();
    builder_.SetInsertPoint(end_block);
}

const FunctionDecl *CodeGen::callee_decl(const Expr &callee) const {
    if (const auto *fn = std::get_if<Expr::Func>(&callee.data)) {
        return fn->decl.get();
    }
    if (const auto *name = std::get_if<Expr::Name>(&callee.data)) {
        const Binding *binding = lookup(name->text);
        return binding == nullptr ? nullptr : binding->decl;
    }
    if (const auto *selector = std::get_if<Expr::Selector>(&callee.data); selector != nullptr && selector->base != nullptr) {
        if (const auto *base = std::get_if<Expr::Name>(&selector->base->data)) {
            const auto package = packages_.find(base->text);
            if (package != packages_.end()) {
                const Binding *binding = visible(package->second, selector->field, true);
                return binding == nullptr ? nullptr : binding->decl;
            }
        }
    }
    return nullptr;
}

void CodeGen::emit_bind(const Stmt::Bind &node) {
    if (node.value == nullptr || node.names.empty() || scopes_.empty()) {
        error(node.loc, "type mismatch");
        return;
    }
    auto note = [&](const std::string &name, const Binding &binding) {
        SourceLoc loc = node.loc;
        for (std::size_t i = 0; i < node.names.size() && i < node.name_locs.size(); ++i) {
            if (node.names[i] == name) {
                loc = node.name_locs[i];
                break;
            }
        }
        debug_declare(name, loc, binding, 0);
    };
    auto create_scalar = [&](const std::string &name, const TypedValue &got) {
        if (got.is_tuple && got.value != nullptr && got.value->getType()->isStructTy()) {
            llvm::AllocaInst *slot = alloca_typed(got.value->getType(), name);
            builder_.CreateStore(got.value, slot);
            Binding binding;
            binding.value = slot;
            binding.is_tuple = true;
            binding.tuple_parts = got.tuple_parts;
            scopes_.back()[name] = binding;
            alias_capture(name, binding);
            note(name, binding);
            return;
        }
        // A designator's depth is the return depth. The value itself is already a function pointer.
        if (got.is_slice) {
            llvm::AllocaInst *slot = alloca_typed(slice_type(), name);
            llvm::Value *stored = got.value != nullptr && got.value->getType() == slice_type() ? got.value
                                                                                              : as_slice_value(got, node.loc);
            builder_.CreateStore(stored, slot);
            Binding binding;
            binding.value = slot;
            binding.is_slice = true;
            binding.array_len = -1;
            binding.depth = got.depth;
            binding.elem_char = got.elem_char;
            binding.struct_id = got.struct_id;
            binding.elem_void = got.elem_void;
            binding.elem_bool = got.elem_bool;
            binding.elem_bits = got.elem_bits;
            binding.elem_unsigned = got.elem_unsigned;
            binding.elem_float = got.elem_float;
            binding.enum_id = got.enum_id;
            scopes_.back()[name] = binding;
            alias_capture(name, binding);
            note(name, binding);
            return;
        }
        if (auto *function = llvm::dyn_cast_or_null<llvm::Function>(got.value)) {
            llvm::AllocaInst *slot = alloca_typed(ptr_type(), name);
            builder_.CreateStore(function, slot);
            Binding binding;
            binding.value = slot;
            binding.depth = 1;
            binding.fn_ty = got.fn_ty != nullptr ? got.fn_ty : function->getFunctionType();
            binding.fn_ret_depth = got.depth;
            binding.fn_ret_float = got.elem_float;
            binding.fn_ret_bits = got.elem_bits;
            binding.fn_ret_unsigned = got.elem_unsigned;
            binding.fn_ret_char = got.elem_char;
            binding.fn_ret_bool = got.elem_bool;
            binding.fn_ret_struct = got.struct_id;
            binding.fn_results = got.fn_results;
            scopes_.back()[name] = binding;
            alias_capture(name, binding);
            note(name, binding);
            return;
        }
        llvm::Type *ty = i32();
        Binding binding;
        if (got.enum_id >= 0 && got.enum_id < static_cast<int>(enums_.size())) {
            const EnumLayout &layout = enums_[static_cast<std::size_t>(got.enum_id)];
            binding.enum_id = got.enum_id;
            binding.elem_bool = layout.is_bool;
            binding.elem_char = layout.is_char;
            binding.elem_unsigned = layout.is_unsigned;
            binding.elem_bits = layout.is_bool ? 0 : layout.bits;
            ty = leaf_type(0, binding.elem_char, -1, binding.elem_bool, binding.elem_bits, false);
        } else if (got.depth > 0) {
            ty = ptr_type();
            binding.depth = got.depth;
            binding.elem_char = got.elem_char;
            binding.struct_id = got.struct_id;
            binding.elem_void = got.elem_void;
            binding.elem_bool = got.elem_bool;
            binding.elem_bits = got.elem_bits;
            binding.elem_unsigned = got.elem_unsigned;
            binding.elem_float = got.elem_float;
            binding.ptr_to_array = got.ptr_to_array;
            binding.ptr_array_len = got.ptr_array_len;
            binding.fn_ty = got.fn_ty;
            binding.fn_ret_depth = got.fn_ret_depth;
            binding.fn_ret_float = got.fn_ret_float;
            binding.fn_ret_bits = got.fn_ret_bits;
            binding.fn_ret_unsigned = got.fn_ret_unsigned;
            binding.fn_ret_char = got.fn_ret_char;
            binding.fn_ret_bool = got.fn_ret_bool;
            binding.fn_ret_struct = got.fn_ret_struct;
            binding.fn_results = got.fn_results;
        } else if (got.struct_id >= 0 && got.struct_id < static_cast<int>(structs_.size()) &&
                   structs_[static_cast<std::size_t>(got.struct_id)].type != nullptr) {
            ty = structs_[static_cast<std::size_t>(got.struct_id)].type;
            binding.struct_id = got.struct_id;
        } else if (got.elem_float || (got.value != nullptr && got.value->getType()->isFloatingPointTy())) {
            // An f64 value is 64 bits. Checking the width first stored it as i64.
            const bool value_is_float = got.value != nullptr && got.value->getType()->isFloatingPointTy();
            const bool wide = value_is_float ? got.value->getType()->isDoubleTy() : got.elem_bits >= 64;
            ty = wide ? f64_ty() : f32_ty();
            binding.elem_float = true;
            binding.elem_bits = wide ? 64 : 32;
        } else if (got.elem_bits == 64) {
            ty = i64();
            binding.elem_bits = 64;
            binding.elem_unsigned = got.elem_unsigned;
        } else if (got.elem_unsigned) {
            binding.elem_bits = 32;
            binding.elem_unsigned = true;
        }
        llvm::AllocaInst *slot = alloca_typed(ty, name);
        llvm::Value *stored = got.value == nullptr ? llvm::Constant::getNullValue(ty) : got.value;
        if (stored->getType() != ty) {
            stored = as_stored(stored, got.depth, ty, got.elem_unsigned);
        }
        builder_.CreateStore(stored, slot);
        binding.value = slot;
        scopes_.back()[name] = binding;
        alias_capture(name, binding);
        note(name, binding);
    };
    auto destination = [&](const std::string &name, bool &create) -> llvm::Value * {
        create = node.is_var || node.is_const;
        if (create) {
            return nullptr;
        }
        if (node.is_define) {
            const auto found = scopes_.back().find(name);
            if (found == scopes_.back().end()) {
                create = true;
                return nullptr;
            }
            return found->second.value;
        }
        const Binding *found = lookup(name);
        return found == nullptr ? nullptr : found->value;
    };
    if (node.names.size() == 1) {
        if (const auto *init = std::get_if<Expr::Init>(&node.value->data); init != nullptr && init->array_literal) {
            bool create = false;
            destination(node.names[0], create);
            if (!create) {
                error(node.loc, "not an lvalue");
                return;
            }
            Lowered low;
            llvm::AllocaInst *slot = emit_array_literal(*init, low);
            if (slot == nullptr || terminated()) {
                return;
            }
            Binding binding;
            apply_low(binding, low);
            binding.value = slot;
            binding.is_array = true;
            binding.array_len = low.array_len;
            scopes_.back()[node.names[0]] = binding;
            alias_capture(node.names[0], binding);
            note(node.names[0], binding);
            return;
        }
        const TypedValue value = emit_rvalue(*node.value);
        if (terminated()) {
            return;
        }
        bool create = false;
        llvm::Value *dest = destination(node.names[0], create);
        if (create) {
            create_scalar(node.names[0], value);
        } else if (dest != nullptr) {
            const Binding *found = lookup(node.names[0]);
            if (found != nullptr && found->is_slice) {
                builder_.CreateStore(as_slice_value(value, node.loc), dest);
            } else {
                llvm::Type *ty = object_type(dest);
                builder_.CreateStore(as_stored(value.value, value.depth, ty, value.elem_unsigned), dest);
            }
        }
        return;
    }
    // A call's component types live on the function, or on the function-pointer type.
    // A tuple value carries the same slots on `tuple_parts`.
    const FunctionDecl *decl = nullptr;
    std::vector<FnParam> fn_results;
    TypedValue value;
    if (const auto *call = std::get_if<Expr::Call>(&node.value->data)) {
        const PreparedCall prepared = prepare_call(*call);
        decl = prepared.decl;
        fn_results = prepared.fn_results;
        value = emit_prepared(prepared);
    } else {
        value = emit_rvalue(*node.value);
    }
    if (terminated()) {
        return;
    }
    std::vector<Lowered> parts;
    if (decl != nullptr && value.value != nullptr && decl->results.size() == node.names.size() &&
        value.value->getType()->isStructTy()) {
        parts.reserve(decl->results.size());
        for (const ResultSlot &slot : decl->results) {
            parts.push_back(lower(current_file_, slot.type_name, slot.type_package, slot.is_struct, slot.indirections,
                                  false, 0, false));
        }
    } else if (decl == nullptr && value.value != nullptr && fn_results.size() == node.names.size() &&
               value.value->getType()->isStructTy()) {
        parts.reserve(fn_results.size());
        for (const FnParam &part : fn_results) {
            parts.push_back(lower(current_file_, part.type_name, part.type_package, part.is_struct, part.indirections,
                                  false, 0, false));
        }
    } else if (value.is_tuple && value.value != nullptr && value.value->getType()->isStructTy() &&
               value.tuple_parts.size() == node.names.size()) {
        parts.reserve(value.tuple_parts.size());
        for (const ResultSlot &slot : value.tuple_parts) {
            parts.push_back(lower(current_file_, slot.type_name, slot.type_package, slot.is_struct, slot.indirections,
                                  slot.is_array, slot.array_len, false));
        }
    } else if (value.value != nullptr && value.value->getType()->isStructTy() && value.tuple_parts.empty()) {
        auto *aggregate = llvm::cast<llvm::StructType>(value.value->getType());
        if (aggregate->getNumElements() != node.names.size()) {
            error(node.loc, "type mismatch");
            return;
        }
        parts.reserve(node.names.size());
        for (unsigned index = 0; index < aggregate->getNumElements(); ++index) {
            llvm::Type *ty = aggregate->getElementType(index);
            Lowered low;
            if (ty != nullptr && ty->isIntegerTy(1)) {
                low.elem_bool = true;
                low.type = leaf_type(0, false, -1, true, 0, false);
            } else if (ty != nullptr && ty->isIntegerTy(8)) {
                low.elem_bits = 8;
                low.elem_unsigned = true;
                low.type = leaf_type(0, false, -1, false, 8, false);
            } else if (ty != nullptr && ty->isIntegerTy(16)) {
                low.elem_bits = 16;
                low.type = leaf_type(0, false, -1, false, 16, false);
            } else if (ty != nullptr && ty->isIntegerTy(64)) {
                low.elem_bits = 64;
                low.type = leaf_type(0, false, -1, false, 64, false);
            } else if (ty != nullptr && ty->isFloatingPointTy()) {
                low.elem_float = true;
                low.elem_bits = ty->isDoubleTy() ? 64 : 32;
                low.type = leaf_type(0, false, -1, false, low.elem_bits, true);
            } else if (ty != nullptr && ty->isPointerTy()) {
                low.depth = 1;
                low.type = ptr_type();
            } else if (ty != nullptr && ty->isStructTy()) {
                for (int id = 0; id < static_cast<int>(structs_.size()); ++id) {
                    if (structs_[static_cast<std::size_t>(id)].type == ty) {
                        low.struct_id = id;
                        break;
                    }
                }
                low.type = ty;
            } else {
                low.type = ty == nullptr ? i32() : ty;
            }
            parts.push_back(std::move(low));
        }
    } else {
        error(node.loc, "type mismatch");
        return;
    }
    for (std::size_t index = 0; index < node.names.size(); ++index) {
        const Lowered &low = parts[index];
        llvm::Value *field = builder_.CreateExtractValue(value.value, static_cast<unsigned>(index));
        bool create = false;
        llvm::Value *dest = destination(node.names[index], create);
        if (create) {
            llvm::Type *ty = stored_type(low);
            llvm::AllocaInst *slot_alloca = alloca_typed(ty, node.names[index]);
            llvm::Value *stored = field;
            if (stored == nullptr) {
                stored = llvm::Constant::getNullValue(ty);
            } else if (stored->getType() != ty) {
                stored = as_stored(stored, low.depth, ty, low.elem_unsigned);
            }
            builder_.CreateStore(stored, slot_alloca);
            Binding binding;
            binding.value = slot_alloca;
            apply_low(binding, low);
            scopes_.back()[node.names[index]] = binding;
            alias_capture(node.names[index], binding);
            note(node.names[index], binding);
        } else if (dest != nullptr) {
            llvm::Type *ty = object_type(dest);
            builder_.CreateStore(as_stored(field, low.depth, ty, low.elem_unsigned), dest);
        }
        if (terminated()) {
            return;
        }
    }
}

void CodeGen::emit_stmt(const Stmt &stmt) {
    set_loc(std::visit(
        [](const auto &node) -> SourceLoc {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Stmt::Func>) {
                return node.decl ? node.decl->loc : SourceLoc{};
            } else {
                return node.loc;
            }
        },
        stmt.data));
    std::visit(
        [&](const auto &node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Stmt::Case>) {
                emit_case(node);
            } else if constexpr (std::is_same_v<T, Stmt::Block>) {
                const bool share = share_function_scope_;
                share_function_scope_ = false;
                if (!share) {
                    push();
                    debug_enter(node.loc);
                }
                defers_.push_back(DeferFrame{static_cast<int>(loops_.size()), {}});
                for (const auto &child : node.statements) {
                    if (child) {
                        emit_stmt(*child);
                    }
                }
                if (!terminated() && !defers_.empty()) {
                    emit_defer_frame(defers_.back());
                }
                if (!defers_.empty()) {
                    defers_.pop_back();
                }
                if (!share) {
                    debug_leave();
                    pop();
                }
            } else if constexpr (std::is_same_v<T, Stmt::Func>) {
                if (node.decl) {
                    emit_local_function(*node.decl);
                }
            } else if (terminated()) {
                return;
            } else if constexpr (std::is_same_v<T, Stmt::Return>) {
                if (extern_struct_ret_ && extern_ret_struct_ != nullptr) {
                    llvm::Value *value = nullptr;
                    if (node.value) {
                        const TypedValue got = emit_rvalue(*node.value);
                        value = got.value;
                    }
                    if (value == nullptr || value->getType() != extern_ret_struct_) {
                        value = llvm::ConstantAggregateZero::get(extern_ret_struct_);
                    }
                    if (!terminated()) {
                        emit_defers_from(0);
                    }
                    if (terminated()) {
                        return;
                    }
                    if (extern_sret_ && extern_sret_ptr_ != nullptr) {
                        builder_.CreateStore(value, extern_sret_ptr_);
                        builder_.CreateRetVoid();
                        return;
                    }
                    const unsigned align = std::max(extern_ret_abi_.natural_align, 8u);
                    llvm::AllocaInst *slot = aligned_slot(extern_ret_struct_, align, "ret");
                    builder_.CreateStore(value, slot);
                    builder_.CreateRet(pack_return(slot, extern_ret_abi_));
                    return;
                }
                if (return_many_ && return_type_ != nullptr && return_type_->isStructTy()) {
                    std::vector<const Expr *> exprs;
                    if (node.value) {
                        exprs.push_back(node.value.get());
                    }
                    for (const auto &extra : node.extra) {
                        if (extra) {
                            exprs.push_back(extra.get());
                        }
                    }
                    auto *aggregate_type = llvm::cast<llvm::StructType>(return_type_);
                    llvm::Value *aggregate = llvm::ConstantAggregateZero::get(aggregate_type);
                    const unsigned count = aggregate_type->getNumElements();
                    // `return args` and `return (40, 'a')` are one tuple. Spread it across the results.
                    if (exprs.size() == 1 && exprs[0] != nullptr) {
                        const TypedValue value = emit_rvalue(*exprs[0]);
                        if (terminated()) {
                            return;
                        }
                        const bool matching = value.is_tuple && value.value != nullptr &&
                                              value.value->getType()->isStructTy() &&
                                              llvm::cast<llvm::StructType>(value.value->getType())->getNumElements() == count;
                        if (matching) {
                            llvm::Value *converted = as_stored(value.value, 0, aggregate_type, value.elem_unsigned);
                            if (converted != nullptr && converted->getType() == aggregate_type) {
                                aggregate = converted;
                            }
                        } else if (count > 0) {
                            llvm::Type *field = aggregate_type->getElementType(0);
                            llvm::Value *piece = llvm::Constant::getNullValue(field);
                            if (value.value != nullptr) {
                                if (field->isPointerTy()) {
                                    piece = value.depth > 0 && value.value->getType()->isPointerTy() ? value.value : null_ptr();
                                } else if (field->isIntegerTy() && value.value->getType()->isIntegerTy()) {
                                    piece = resize_int(value.value, field, value.elem_unsigned);
                                } else if (value.value->getType() == field) {
                                    piece = value.value;
                                }
                            }
                            aggregate = builder_.CreateInsertValue(aggregate, piece, 0);
                        }
                        if (!terminated()) {
                            emit_defers_from(0);
                        }
                        if (terminated()) {
                            return;
                        }
                        builder_.CreateRet(aggregate);
                        return;
                    }
                    for (unsigned index = 0; index < count; ++index) {
                        llvm::Type *field = aggregate_type->getElementType(index);
                        llvm::Value *piece = llvm::Constant::getNullValue(field);
                        if (index < exprs.size() && exprs[index] != nullptr) {
                            const TypedValue value = emit_rvalue(*exprs[index]);
                            if (value.value != nullptr) {
                                if (field->isPointerTy()) {
                                    piece = value.depth > 0 && value.value->getType()->isPointerTy() ? value.value : null_ptr();
                                } else if (field->isIntegerTy() && value.value->getType()->isIntegerTy()) {
                                    piece = resize_int(value.value, field, value.elem_unsigned);
                                } else if (value.value->getType() == field) {
                                    piece = value.value;
                                }
                            }
                        }
                        if (terminated()) {
                            return;
                        }
                        aggregate = builder_.CreateInsertValue(aggregate, piece, index);
                    }
                    if (!terminated()) {
                        emit_defers_from(0);
                    }
                    if (terminated()) {
                        return;
                    }
                    builder_.CreateRet(aggregate);
                    return;
                }
                if (return_type_ == slice_type() && node.value != nullptr) {
                    const TypedValue value = emit_rvalue(*node.value);
                    llvm::Value *packed = value.value == nullptr ? llvm::ConstantAggregateZero::get(slice_type())
                                                                : as_slice_value(value, node.loc);
                    if (!terminated()) {
                        emit_defers_from(0);
                    }
                    if (!terminated()) {
                        builder_.CreateRet(packed);
                    }
                    return;
                }
                if (return_type_ != nullptr && return_type_->isArrayTy() && node.value != nullptr) {
                    const auto *name = std::get_if<Expr::Name>(&node.value->data);
                    const Binding *binding = name == nullptr ? nullptr : lookup(name->text);
                    if (binding == nullptr || !binding->is_array || binding->value == nullptr) {
                        error(node.loc, "not supported yet");
                        return;
                    }
                    llvm::Value *loaded = builder_.CreateLoad(return_type_, binding->value);
                    if (!terminated()) {
                        emit_defers_from(0);
                    }
                    if (!terminated()) {
                        builder_.CreateRet(loaded);
                    }
                    return;
                }
                llvm::Value *ret = nullptr;
                const bool returns_void = return_type_ != nullptr && return_type_->isVoidTy();
                if (!returns_void && node.value) {
                    const TypedValue value = emit_rvalue(*node.value);
                    ret = value.value;
                    if (return_depth_ > 0 && value.depth == 0 && ret != nullptr && !ret->getType()->isPointerTy()) {
                        ret = null_ptr();
                    } else if (ret != nullptr && return_type_ != nullptr &&
                               (return_type_->isFloatingPointTy() || ret->getType()->isFloatingPointTy())) {
                        ret = as_stored(ret, 0, return_type_, value.elem_unsigned);
                    } else if (ret != nullptr && return_type_ != nullptr && ret->getType()->isIntegerTy() &&
                               return_type_->isIntegerTy() && ret->getType() != return_type_) {
                        ret = resize_int(ret, return_type_, value.elem_unsigned);
                    }
                }
                if (!terminated()) {
                    emit_defers_from(0);
                }
                if (terminated()) {
                    return;
                }
                if (returns_void) {
                    builder_.CreateRetVoid();
                } else if (node.value) {
                    builder_.CreateRet(ret);
                } else if (return_type_ == slice_type()) {
                    builder_.CreateRet(llvm::ConstantAggregateZero::get(slice_type()));
                } else if (return_depth_ > 0) {
                    builder_.CreateRet(null_ptr());
                } else if (return_type_ != nullptr && !return_type_->isIntegerTy(32)) {
                    builder_.CreateRet(llvm::Constant::getNullValue(return_type_));
                } else {
                    builder_.CreateRet(zero());
                }
            } else if constexpr (std::is_same_v<T, Stmt::Expression>) {
                if (node.expr) {
                    emit_rvalue(*node.expr);
                }
            } else if constexpr (std::is_same_v<T, Stmt::If>) {
                const SourceLoc if_loc = node.loc;
                llvm::Value *cond = emit_rvalue(*node.cond).value;
                if (terminated()) {
                    return;
                }
                llvm::BasicBlock *then_block = llvm::BasicBlock::Create(context_, "if.then", function_);
                llvm::BasicBlock *else_block = llvm::BasicBlock::Create(context_, "if.else", function_);
                llvm::BasicBlock *merge_block = llvm::BasicBlock::Create(context_, "if.end", function_);
                // The branch itself is the if line. The arms must not leak their
                // lines onto the path that skips them.
                set_loc(if_loc);
                builder_.CreateCondBr(is_nonzero(cond), then_block, else_block);
                builder_.SetInsertPoint(then_block);
                if (node.then_arm) {
                    emit_stmt(*node.then_arm);
                }
                if (!terminated()) {
                    builder_.CreateBr(merge_block);
                }
                builder_.SetInsertPoint(else_block);
                set_loc(if_loc);
                if (node.else_arm) {
                    emit_stmt(*node.else_arm);
                }
                if (!terminated()) {
                    if (node.else_arm == nullptr) {
                        set_loc(if_loc);
                    }
                    builder_.CreateBr(merge_block);
                }
                builder_.SetInsertPoint(merge_block);
                set_loc(if_loc);
            } else if constexpr (std::is_same_v<T, Stmt::While>) {
                llvm::BasicBlock *cond_block = llvm::BasicBlock::Create(context_, "while.cond", function_);
                llvm::BasicBlock *body_block = llvm::BasicBlock::Create(context_, "while.body", function_);
                llvm::BasicBlock *end_block = llvm::BasicBlock::Create(context_, "while.end", function_);
                builder_.CreateBr(cond_block);
                builder_.SetInsertPoint(cond_block);
                llvm::Value *cond = emit_rvalue(*node.cond).value;
                if (!terminated()) {
                    builder_.CreateCondBr(is_nonzero(cond), body_block, end_block);
                }
                builder_.SetInsertPoint(body_block);
                loops_.push_back(LoopTargets{cond_block, end_block});
                if (node.body) {
                    emit_stmt(*node.body);
                }
                loops_.pop_back();
                if (!terminated()) {
                    builder_.CreateBr(cond_block);
                }
                builder_.SetInsertPoint(end_block);
            } else if constexpr (std::is_same_v<T, Stmt::DoWhile>) {
                llvm::BasicBlock *body_block = llvm::BasicBlock::Create(context_, "do.body", function_);
                llvm::BasicBlock *cond_block = llvm::BasicBlock::Create(context_, "do.cond", function_);
                llvm::BasicBlock *end_block = llvm::BasicBlock::Create(context_, "do.end", function_);
                builder_.CreateBr(body_block);
                builder_.SetInsertPoint(body_block);
                loops_.push_back(LoopTargets{cond_block, end_block});
                if (node.body) {
                    emit_stmt(*node.body);
                }
                loops_.pop_back();
                if (!terminated()) {
                    builder_.CreateBr(cond_block);
                }
                builder_.SetInsertPoint(cond_block);
                llvm::Value *cond = node.cond ? emit_rvalue(*node.cond).value : zero();
                if (!terminated()) {
                    builder_.CreateCondBr(is_nonzero(cond), body_block, end_block);
                }
                builder_.SetInsertPoint(end_block);
            } else if constexpr (std::is_same_v<T, Stmt::Switch>) {
                emit_switch(node);
            } else if constexpr (std::is_same_v<T, Stmt::For>) {
                push();
                if (node.init) {
                    emit_stmt(*node.init);
                }
                llvm::BasicBlock *cond_block = llvm::BasicBlock::Create(context_, "for.cond", function_);
                llvm::BasicBlock *body_block = llvm::BasicBlock::Create(context_, "for.body", function_);
                llvm::BasicBlock *step_block = llvm::BasicBlock::Create(context_, "for.step", function_);
                llvm::BasicBlock *end_block = llvm::BasicBlock::Create(context_, "for.end", function_);
                if (!terminated()) {
                    builder_.CreateBr(cond_block);
                }
                builder_.SetInsertPoint(cond_block);
                if (node.cond) {
                    llvm::Value *cond = emit_rvalue(*node.cond).value;
                    if (!terminated()) {
                        builder_.CreateCondBr(is_nonzero(cond), body_block, end_block);
                    }
                } else if (!terminated()) {
                    builder_.CreateBr(body_block);
                }
                builder_.SetInsertPoint(body_block);
                loops_.push_back(LoopTargets{step_block, end_block});
                if (node.body) {
                    emit_stmt(*node.body);
                }
                loops_.pop_back();
                if (!terminated()) {
                    builder_.CreateBr(step_block);
                }
                builder_.SetInsertPoint(step_block);
                if (node.step && !terminated()) {
                    emit_rvalue(*node.step);
                }
                if (!terminated()) {
                    builder_.CreateBr(cond_block);
                }
                builder_.SetInsertPoint(end_block);
                pop();
            } else if constexpr (std::is_same_v<T, Stmt::Break>) {
                if (!loops_.empty()) {
                    emit_defers_from(static_cast<int>(loops_.size()));
                    if (!terminated()) {
                        builder_.CreateBr(loops_.back().after);
                    }
                }
            } else if constexpr (std::is_same_v<T, Stmt::Continue>) {
                int target = -1;
                for (int index = static_cast<int>(loops_.size()) - 1; index >= 0; --index) {
                    if (!loops_[static_cast<std::size_t>(index)].is_switch) {
                        target = index;
                        break;
                    }
                }
                if (target >= 0 && loops_[static_cast<std::size_t>(target)].step != nullptr) {
                    emit_defers_from(target + 1);
                    if (!terminated()) {
                        builder_.CreateBr(loops_[static_cast<std::size_t>(target)].step);
                    }
                }
            } else if constexpr (std::is_same_v<T, Stmt::Defer>) {
                if (node.call == nullptr) {
                    return;
                }
                const auto *call = std::get_if<Expr::Call>(&node.call->data);
                if (call == nullptr) {
                    error(node.loc, "type mismatch");
                    return;
                }
                if (call->callee != nullptr) {
                    if (const auto *named = std::get_if<Expr::Name>(&call->callee->data)) {
                        if (const Binding *binding = lookup(named->text); binding != nullptr && binding->is_generic) {
                            error(node.loc, "not supported yet");
                            return;
                        }
                    }
                }
                // Arguments and the receiver are evaluated here. The call runs when the block is left.
                const PreparedCall prepared = prepare_call(*call);
                if (prepared.function == nullptr || terminated() || defers_.empty()) {
                    return;
                }
                DeferredCall item;
                item.flag = defer_flag();
                item.function = prepared.function;
                for (llvm::Value *arg : prepared.args) {
                    if (arg == nullptr) {
                        continue;
                    }
                    llvm::AllocaInst *slot = alloca_typed(arg->getType(), "defer.arg");
                    builder_.CreateStore(arg, slot);
                    item.args.push_back(slot);
                }
                builder_.CreateStore(llvm::ConstantInt::getTrue(context_), item.flag);
                defers_.back().calls.push_back(std::move(item));
            } else if constexpr (std::is_same_v<T, Stmt::Bind>) {
                emit_bind(node);
            } else if constexpr (std::is_same_v<T, Stmt::Assert>) {
            } else if constexpr (std::is_same_v<T, Stmt::Decl>) {
                for (const VarDecl &var : node.vars) {
                    if (var.name.empty()) {
                        continue;
                    }
                    const Lowered low = lower_declared(current_file_, var);
                    if (low.is_slice) {
                        llvm::AllocaInst *slot = alloca_typed(slice_type(), var.name);
                        Binding binding;
                        binding.value = slot;
                        apply_low(binding, low);
                        binding.is_slice = true;
                        scopes_.back()[var.name] = binding;
                        alias_capture(var.name, binding);
                        debug_declare(var.name, var.loc, binding, 0);
                        if (var.init == nullptr) {
                            builder_.CreateStore(llvm::ConstantAggregateZero::get(slice_type()), slot);
                        } else {
                            const TypedValue value = emit_rvalue(*var.init);
                            if (!terminated()) {
                                builder_.CreateStore(as_slice_value(value, var.loc), slot);
                            }
                        }
                        continue;
                    }
                    if (!var.ptr_to_array && (var.is_array || low.is_array)) {
                        if (!low.is_array || low.array_len <= 0) {
                            error(var.loc, "not supported yet");
                            continue;
                        }
                        llvm::Type *ty = stored_type(low);
                        llvm::AllocaInst *slot = alloca_typed(ty, var.name);
                        // A full initializer is the variable's first value. A shorter list still
                        // starts from zero so the elements it does not name stay zero.
                        const auto *list = var.init == nullptr ? nullptr : std::get_if<Expr::Init>(&var.init->data);
                        const bool listed = list != nullptr && ty->isArrayTy() &&
                                            list->elements.size() == llvm::cast<llvm::ArrayType>(ty)->getNumElements();
                        if (!listed) {
                            builder_.CreateStore(llvm::ConstantAggregateZero::get(ty), slot);
                        }
                        Binding binding;
                        binding.value = slot;
                        binding.depth = low.depth;
                        binding.elem_char = low.elem_char;
                        binding.is_array = true;
                        binding.array_len = low.array_len;
                        binding.struct_id = low.struct_id;
                        binding.elem_void = low.elem_void;
                        binding.elem_bool = low.elem_bool;
                        binding.elem_bits = low.elem_bits;
                        binding.elem_unsigned = low.elem_unsigned;
                        binding.elem_float = low.elem_float;
                        binding.enum_id = low.enum_id;
                        scopes_.back()[var.name] = binding;
                        alias_capture(var.name, binding);
                        debug_declare(var.name, var.loc, binding, 0);
                        if (var.init != nullptr && ty->isArrayTy()) {
                            const auto *init = std::get_if<Expr::Init>(&var.init->data);
                            if (init != nullptr && !init->elements.empty()) {
                                auto *array_ty = llvm::cast<llvm::ArrayType>(ty);
                                llvm::Type *elem = array_ty->getElementType();
                                const unsigned limit = array_ty->getNumElements();
                                unsigned index = 0;
                                for (const auto &element : init->elements) {
                                    if (index >= limit || terminated()) {
                                        break;
                                    }
                                    const TypedValue value = element ? emit_rvalue(*element) : TypedValue{};
                                    if (terminated()) {
                                        break;
                                    }
                                    llvm::Value *slot_i = builder_.CreateInBoundsGEP(
                                        array_ty, slot, {const_i32(0), const_i32(static_cast<int>(index))});
                                    llvm::Value *stored = value.value == nullptr
                                                              ? llvm::Constant::getNullValue(elem)
                                                              : as_stored(value.value, value.depth, elem, value.elem_unsigned);
                                    builder_.CreateStore(stored, slot_i);
                                    ++index;
                                }
                            }
                        }
                        continue;
                    }
                    llvm::Type *ty = stored_type(low);
                    llvm::AllocaInst *slot = alloca_typed(ty, var.name);
                    Binding binding;
                    binding.value = slot;
                    binding.depth = low.depth;
                    binding.elem_char = low.elem_char;
                    binding.struct_id = low.struct_id;
                    binding.elem_void = low.elem_void;
                    binding.elem_bool = low.elem_bool;
                    apply_low(binding, low);
                    scopes_.back()[var.name] = binding;
                    alias_capture(var.name, binding);
                    debug_declare(var.name, var.loc, binding, 0);
                    if (var.init) {
                        // The initializer is the first value. Clearing the slot first would store a
                        // zero the programmer did not write.
                        const TypedValue value = emit_rvalue(*var.init);
                        if (!terminated()) {
                            builder_.CreateStore(
                                as_stored(value.value, value.depth, object_type(slot), value.elem_unsigned), slot);
                        }
                    } else {
                        llvm::Value *initial = zero();
                        if (low.depth > 0) {
                            initial = null_ptr();
                        } else if (low.elem_float) {
                            initial = llvm::ConstantFP::get(low.elem_bits >= 64 ? f64_ty() : f32_ty(), 0.0);
                        } else if (low.elem_bool) {
                            initial = llvm::ConstantInt::get(i1(), 0, false);
                        } else if (low.elem_char || low.elem_bits == 8) {
                            initial = llvm::ConstantInt::get(i8(), 0, false);
                        } else if (low.elem_bits == 16) {
                            initial = llvm::ConstantInt::get(i16(), 0, false);
                        } else if (low.elem_bits == 64) {
                            initial = llvm::ConstantInt::get(i64(), 0, false);
                        } else if (low.trait_id >= 0 || low.struct_id >= 0) {
                            initial = llvm::ConstantAggregateZero::get(ty);
                        }
                        builder_.CreateStore(initial, slot);
                    }
                }
            }
        },
        stmt.data);
}

void CodeGen::emit(const PackageGraph &graph) {
    if (graph.nodes.empty()) {
        return;
    }
    graph_ = &graph;
    entry_file_ = graph.nodes.front().file_id;
    if (debug_) {
        open_debug();
    }
    prepare_enums();
    prepare_structs();
    prepare_traits();
    for (const PackageNode &node : graph.nodes) {
        declare_file(node);
    }
    for (const PackageNode &node : graph.nodes) {
        current_file_ = node.file_id;
        for (const TopDecl &decl : node.unit.decls) {
            if (const auto *var = std::get_if<VarDecl>(&decl.data)) {
                apply_initializer(*var);
            }
        }
    }
    for (const PackageNode &node : graph.nodes) {
        emit_bodies(node);
    }
    emit_builtin_trace();
    emit_builtin_panic();
    if (testing_) {
        emit_tests();
    }
    if (di_builder_ != nullptr) {
        finish_debug();
    }
    graph_ = nullptr;
}

bool CodeGen::is_std_builtin_call(const Expr &callee, const std::string &field) const {
    if (graph_ == nullptr) {
        return false;
    }
    if (const auto *selector = std::get_if<Expr::Selector>(&callee.data)) {
        if (selector->field != field || selector->base == nullptr) {
            return false;
        }
        const auto *base = std::get_if<Expr::Name>(&selector->base->data);
        if (base == nullptr) {
            return false;
        }
        const auto package = packages_.find(base->text);
        if (package == packages_.end()) {
            return false;
        }
        const PackageNode *node = find_node(package->second);
        return node != nullptr && is_std_builtin_path(node->path);
    }
    if (const auto *named = std::get_if<Expr::Name>(&callee.data)) {
        const Binding *binding = lookup(named->text);
        if (binding == nullptr || binding->decl == nullptr || binding->decl->name != field) {
            return false;
        }
        for (const PackageNode &file : graph_->nodes) {
            if (!is_std_builtin_path(file.path)) {
                continue;
            }
            for (const TopDecl &decl : file.unit.decls) {
                if (const auto *fn = std::get_if<FunctionDecl>(&decl.data); fn == binding->decl) {
                    return true;
                }
            }
        }
    }
    return false;
}

void CodeGen::ensure_trace() {
    if (trace_frame_ != nullptr) {
        return;
    }
    trace_frame_ = llvm::StructType::create(context_, {ptr_type(), i32(), i32(), ptr_type()}, "tlc.frame");
    trace_stack_type_ = llvm::ArrayType::get(trace_frame_, 32);
    trace_depth_ = new llvm::GlobalVariable(module_, i32(), false, llvm::GlobalValue::InternalLinkage, const_i32(0),
                                            "tlc.trace.depth");
    trace_stack_ = new llvm::GlobalVariable(module_, trace_stack_type_, false, llvm::GlobalValue::InternalLinkage,
                                            llvm::ConstantAggregateZero::get(trace_stack_type_), "tlc.trace.stack");
    auto *buffer_type = llvm::ArrayType::get(i8(), 4096);
    trace_buffer_ = new llvm::GlobalVariable(module_, buffer_type, false, llvm::GlobalValue::InternalLinkage,
                                             llvm::ConstantAggregateZero::get(buffer_type), "tlc.trace.buf");
}

std::string CodeGen::display_path(SourceLoc loc) const {
    const SourceFile *file = diags_.source(loc.file);
    if (file == nullptr) {
        return "unknown";
    }
    const std::string &path = file->path();
    std::string dir;
    if (graph_ != nullptr && !graph_->nodes.empty()) {
        const std::string &entry = graph_->nodes.front().path;
        const auto slash = entry.find_last_of("/\\");
        if (slash != std::string::npos) {
            dir = entry.substr(0, slash + 1);
        }
    }
    if (!dir.empty() && path.starts_with(dir)) {
        return path.substr(dir.size());
    }
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

llvm::Value *CodeGen::push_trace(SourceLoc loc) {
    if (trace_name_.empty() || function_ == nullptr || terminated()) {
        return nullptr;
    }
    ensure_trace();
    int line = 1;
    int column = 1;
    if (const SourceFile *file = diags_.source(loc.file)) {
        const LineCol at = file->line_col(loc.offset);
        line = at.line;
        column = at.column;
    }
    llvm::BasicBlock *from = builder_.GetInsertBlock();
    llvm::Value *depth = builder_.CreateLoad(i32(), trace_depth_);
    llvm::Value *can = builder_.CreateICmpULT(depth, const_i32(32));
    auto *push_block = llvm::BasicBlock::Create(context_, "trace.push", function_);
    auto *call_block = llvm::BasicBlock::Create(context_, "trace.call", function_);
    builder_.CreateCondBr(can, push_block, call_block);
    builder_.SetInsertPoint(push_block);
    llvm::Value *frame = builder_.CreateInBoundsGEP(trace_stack_type_, trace_stack_, {const_i32(0), depth});
    auto store_field = [&](unsigned index, llvm::Value *value) {
        llvm::Value *slot = builder_.CreateInBoundsGEP(trace_frame_, frame, {const_i32(0), const_i32(static_cast<int>(index))});
        builder_.CreateStore(value, slot);
    };
    store_field(0, builder_.CreateGlobalString(display_path(loc)));
    store_field(1, const_i32(line));
    store_field(2, const_i32(column));
    store_field(3, builder_.CreateGlobalString(trace_name_));
    builder_.CreateStore(builder_.CreateAdd(depth, const_i32(1)), trace_depth_);
    builder_.CreateBr(call_block);
    builder_.SetInsertPoint(call_block);
    auto *pushed = builder_.CreatePHI(llvm::Type::getInt1Ty(context_), 2);
    pushed->addIncoming(llvm::ConstantInt::getTrue(context_), push_block);
    pushed->addIncoming(llvm::ConstantInt::getFalse(context_), from);
    return pushed;
}

void CodeGen::pop_trace(llvm::Value *pushed) {
    if (pushed == nullptr || function_ == nullptr || terminated()) {
        return;
    }
    auto *pop_block = llvm::BasicBlock::Create(context_, "trace.pop", function_);
    auto *done_block = llvm::BasicBlock::Create(context_, "trace.done", function_);
    builder_.CreateCondBr(pushed, pop_block, done_block);
    builder_.SetInsertPoint(pop_block);
    llvm::Value *depth = builder_.CreateLoad(i32(), trace_depth_);
    builder_.CreateStore(builder_.CreateSub(depth, const_i32(1)), trace_depth_);
    builder_.CreateBr(done_block);
    builder_.SetInsertPoint(done_block);
}

llvm::Value *CodeGen::variadic_arg(const TypedValue &value) {
    llvm::Value *arg = value.value == nullptr ? zero() : value.value;
    if (arg->getType()->isFloatTy()) {
        return builder_.CreateFPExt(arg, f64_ty());
    }
    if (arg->getType()->isIntegerTy() && arg->getType()->getIntegerBitWidth() < 32) {
        const bool zero_extend = value.elem_unsigned || value.elem_char;
        return zero_extend ? builder_.CreateZExt(arg, i32()) : builder_.CreateSExt(arg, i32());
    }
    return arg;
}

TypedValue CodeGen::emit_builtin_va(SourceLoc loc) {
    if (function_ == nullptr || !function_->isVarArg() || terminated()) {
        error(loc, "not supported yet");
        return {null_ptr(), 1, true};
    }
    auto *tag = llvm::StructType::get(context_, {i32(), i32(), ptr_type(), ptr_type()});
    auto *list = llvm::ArrayType::get(tag, 1);
    llvm::IRBuilder<> allocas(&function_->getEntryBlock(), function_->getEntryBlock().begin());
    stamp_builder(allocas);
    llvm::AllocaInst *slot = allocas.CreateAlloca(list, nullptr, "va");
    slot->setAlignment(llvm::Align(16));
    llvm::Value *ap = builder_.CreateInBoundsGEP(list, slot, {const_i32(0), const_i32(0)});
    llvm::Function *start = llvm::Intrinsic::getOrInsertDeclaration(&module_, llvm::Intrinsic::vastart, {ptr_type()});
    builder_.CreateCall(start, {ap});
    return {ap, 1, true};
}

llvm::Function *CodeGen::builtin_function(const std::string &name) const {
    if (graph_ == nullptr) {
        return nullptr;
    }
    for (const PackageNode &node : graph_->nodes) {
        if (!is_std_builtin_path(node.path)) {
            continue;
        }
        const auto file = files_.find(node.file_id);
        if (file == files_.end()) {
            continue;
        }
        const auto found = file->second.functions.find(name);
        if (found == file->second.functions.end()) {
            continue;
        }
        return llvm::dyn_cast<llvm::Function>(found->second.value);
    }
    return nullptr;
}

void CodeGen::emit_builtin_trace() {
    llvm::Function *function = builtin_function("trace");
    if (function == nullptr || !function->empty()) {
        return;
    }
    ensure_trace();
    const llvm::IRBuilderBase::InsertPoint saved = builder_.saveIP();
    auto *entry = llvm::BasicBlock::Create(context_, "entry", function);
    auto *head = llvm::BasicBlock::Create(context_, "head", function);
    auto *body = llvm::BasicBlock::Create(context_, "body", function);
    auto *write = llvm::BasicBlock::Create(context_, "write", function);
    auto *next = llvm::BasicBlock::Create(context_, "next", function);
    auto *done = llvm::BasicBlock::Create(context_, "done", function);
    auto *buffer_type = llvm::cast<llvm::ArrayType>(trace_buffer_->getValueType());
    builder_.SetInsertPoint(entry);
    llvm::Value *first = builder_.CreateInBoundsGEP(buffer_type, trace_buffer_, {const_i32(0), const_i32(0)});
    builder_.CreateStore(llvm::ConstantInt::get(i8(), 0), first);
    llvm::Value *depth = builder_.CreateLoad(i32(), trace_depth_);
    builder_.CreateBr(head);
    builder_.SetInsertPoint(head);
    auto *index = builder_.CreatePHI(i32(), 2);
    auto *length = builder_.CreatePHI(i32(), 2);
    // `index` is how many frames are not written yet. The newest frame is first.
    index->addIncoming(depth, entry);
    length->addIncoming(const_i32(0), entry);
    builder_.CreateCondBr(builder_.CreateICmpNE(index, const_i32(0)), body, done);
    builder_.SetInsertPoint(body);
    llvm::Value *remain = builder_.CreateSub(const_i32(4096), length);
    builder_.CreateCondBr(builder_.CreateICmpSGT(remain, const_i32(1)), write, done);
    builder_.SetInsertPoint(write);
    llvm::Value *frame_index = builder_.CreateSub(index, const_i32(1));
    llvm::Value *frame = builder_.CreateInBoundsGEP(trace_stack_type_, trace_stack_, {const_i32(0), frame_index});
    auto load_field = [&](unsigned field_index, llvm::Type *type) {
        llvm::Value *slot =
            builder_.CreateInBoundsGEP(trace_frame_, frame, {const_i32(0), const_i32(static_cast<int>(field_index))});
        return builder_.CreateLoad(type, slot);
    };
    llvm::Value *file = load_field(0, ptr_type());
    llvm::Value *line = load_field(1, i32());
    llvm::Value *column = load_field(2, i32());
    llvm::Value *name = load_field(3, ptr_type());
    llvm::Value *last = builder_.CreateICmpEQ(frame_index, const_i32(0));
    llvm::Value *format = builder_.CreateSelect(last, builder_.CreateGlobalString("%s:%d:%d: %s"),
                                                builder_.CreateGlobalString("%s:%d:%d: %s\n"));
    llvm::Value *dest = builder_.CreateInBoundsGEP(buffer_type, trace_buffer_, {const_i32(0), length});
    auto *snprintf_type = llvm::FunctionType::get(i32(), {ptr_type(), i64(), ptr_type()}, true);
    llvm::FunctionCallee snprintf_fn = module_.getOrInsertFunction("snprintf", snprintf_type);
    llvm::Value *wrote = builder_.CreateCall(snprintf_type, snprintf_fn.getCallee(),
                                             {dest, builder_.CreateZExt(remain, i64()), format, file, line, column, name});
    llvm::Value *count = builder_.CreateSelect(builder_.CreateICmpSGT(wrote, const_i32(0)), wrote, const_i32(0));
    llvm::Value *limit = builder_.CreateSub(remain, const_i32(1));
    llvm::Value *fitted = builder_.CreateSelect(builder_.CreateICmpSLT(count, limit), count, limit);
    llvm::Value *sum = builder_.CreateAdd(length, fitted);
    llvm::Value *next_index = frame_index;
    builder_.CreateBr(next);
    builder_.SetInsertPoint(next);
    index->addIncoming(next_index, next);
    length->addIncoming(sum, next);
    builder_.CreateBr(head);
    builder_.SetInsertPoint(done);
    builder_.CreateRet(trace_buffer_);
    builder_.restoreIP(saved);
}

llvm::Value *CodeGen::allocator_receiver(const Expr &allocator, bool is_pointer) {
    if (is_pointer) {
        const TypedValue value = emit_rvalue(allocator);
        return value.value;
    }
    if (!addressable(allocator)) {
        error(expr_loc(allocator), "not an lvalue");
        return nullptr;
    }
    const TypedValue addr = emit_addr(allocator);
    if (addr.value == nullptr) {
        return nullptr;
    }
    if (addr.depth > 0) {
        return builder_.CreateLoad(ptr_type(), addr.value);
    }
    return addr.value;
}

llvm::Function *CodeGen::allocator_method(uint32_t file_id, const std::string &type_name, const std::string &name) {
    const auto file = files_.find(file_id);
    if (file == files_.end() || type_name.empty()) {
        return nullptr;
    }
    const auto found = file->second.functions.find(type_name + "." + name);
    if (found == file->second.functions.end() || found->second.value == nullptr) {
        return nullptr;
    }
    return llvm::dyn_cast<llvm::Function>(found->second.value);
}

void CodeGen::abort_now() {
    auto *abort_type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_), false);
    llvm::FunctionCallee abort_fn = module_.getOrInsertFunction("abort", abort_type);
    builder_.CreateCall(abort_type, abort_fn.getCallee());
    builder_.CreateUnreachable();
}

void CodeGen::abort_out_of_memory() {
    auto *print_type = llvm::FunctionType::get(i32(), {i32(), ptr_type()}, true);
    llvm::FunctionCallee print = module_.getOrInsertFunction("dprintf", print_type);
    builder_.CreateCall(print_type, print.getCallee(),
                        {const_i32(2), builder_.CreateGlobalString("panic: out of memory\n")});
    abort_now();
}

llvm::Value *CodeGen::heap_alloc(const Expr::New &node, llvm::Value *receiver, llvm::Value *bytes, uint64_t align) {
    if (bytes == nullptr) {
        return nullptr;
    }
    if (node.builtin_alloc) {
        if (align > 16) {
            llvm::Value *step = llvm::ConstantInt::get(i64(), align);
            llvm::Value *padded = builder_.CreateAdd(bytes, llvm::ConstantInt::get(i64(), align - 1));
            bytes = builder_.CreateMul(builder_.CreateUDiv(padded, step), step);
            auto *fn_type = llvm::FunctionType::get(ptr_type(), {i64(), i64()}, false);
            llvm::FunctionCallee fn = module_.getOrInsertFunction("aligned_alloc", fn_type);
            return builder_.CreateCall(fn_type, fn.getCallee(), {step, bytes});
        }
        auto *fn_type = llvm::FunctionType::get(ptr_type(), {i64()}, false);
        llvm::FunctionCallee fn = module_.getOrInsertFunction("malloc", fn_type);
        return builder_.CreateCall(fn_type, fn.getCallee(), {bytes});
    }
    if (receiver == nullptr) {
        error(node.loc, "undeclared identifier");
        return nullptr;
    }
    // The trait method takes an i32 byte count.
    llvm::BasicBlock *big = llvm::BasicBlock::Create(context_, "new.big", function_);
    llvm::BasicBlock *fit = llvm::BasicBlock::Create(context_, "new.fit", function_);
    builder_.CreateCondBr(builder_.CreateICmpUGT(bytes, llvm::ConstantInt::get(i64(), 2147483647)), big, fit);
    builder_.SetInsertPoint(big);
    abort_now();
    builder_.SetInsertPoint(fit);
    llvm::Function *method = allocator_method(node.alloc_file, node.alloc_name, "malloc");
    if (method == nullptr) {
        error(node.loc, "undeclared identifier");
        return nullptr;
    }
    if (align > 2147483647ULL) {
        error(node.loc, "not supported yet");
        return nullptr;
    }
    return builder_.CreateCall(method->getFunctionType(), method,
                              {receiver, builder_.CreateTrunc(bytes, i32()), const_i32(static_cast<int>(align))});
}

llvm::Value *CodeGen::call_malloc(const Expr::New &node, uint64_t size, uint64_t align) {
    if (node.builtin_alloc) {
        uint64_t bytes = size == 0 ? 1 : size;
        llvm::Value *raw = nullptr;
        if (align > 16) {
            bytes = (bytes + align - 1) / align * align;
            auto *fn_type = llvm::FunctionType::get(ptr_type(), {i64(), i64()}, false);
            llvm::FunctionCallee fn = module_.getOrInsertFunction("aligned_alloc", fn_type);
            raw = builder_.CreateCall(fn_type, fn.getCallee(),
                                     {llvm::ConstantInt::get(i64(), align), llvm::ConstantInt::get(i64(), bytes)});
        } else {
            auto *fn_type = llvm::FunctionType::get(ptr_type(), {i64()}, false);
            llvm::FunctionCallee fn = module_.getOrInsertFunction("malloc", fn_type);
            raw = builder_.CreateCall(fn_type, fn.getCallee(), {llvm::ConstantInt::get(i64(), bytes)});
        }
        return raw;
    }
    llvm::Function *method = allocator_method(node.alloc_file, node.alloc_name, "malloc");
    if (method == nullptr || node.allocator == nullptr) {
        error(node.loc, "undeclared identifier");
        return nullptr;
    }
    llvm::Value *receiver = allocator_receiver(*node.allocator, node.alloc_is_pointer);
    if (receiver == nullptr) {
        return nullptr;
    }
    if (size > 2147483647ULL || align > 2147483647ULL) {
        error(node.loc, "not supported yet");
        return nullptr;
    }
    auto *fn_type = method->getFunctionType();
    return builder_.CreateCall(fn_type, method,
                              {receiver, const_i32(static_cast<int32_t>(size)), const_i32(static_cast<int32_t>(align))});
}

void CodeGen::call_free(const Expr::Delete &node, llvm::Value *pointer, llvm::Value *receiver) {
    if (pointer == nullptr) {
        return;
    }
    if (node.builtin_alloc) {
        auto *fn_type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_), {ptr_type()}, false);
        llvm::FunctionCallee fn = module_.getOrInsertFunction("free", fn_type);
        builder_.CreateCall(fn_type, fn.getCallee(), {pointer});
        return;
    }
    llvm::Function *method = allocator_method(node.alloc_file, node.alloc_name, "free");
    if (method == nullptr || receiver == nullptr) {
        error(node.loc, "undeclared identifier");
        return;
    }
    builder_.CreateCall(method->getFunctionType(), method, {receiver, pointer});
}

void CodeGen::store_new_element(llvm::Value *raw, llvm::Value *index, const Lowered &low, const TypedValue &value,
                                SourceLoc loc) {
    if (raw == nullptr || index == nullptr || value.value == nullptr || low.type == nullptr || terminated()) {
        return;
    }
    llvm::Value *slot = builder_.CreateInBoundsGEP(low.type, raw, llvm::ArrayRef<llvm::Value *>{index});
    if (low.is_slice) {
        builder_.CreateStore(as_slice_value(value, loc), slot);
        return;
    }
    builder_.CreateStore(as_stored(value.value, value.depth, low.type, value.elem_unsigned), slot);
}

void CodeGen::fill_new_buffer(llvm::Value *raw, llvm::Value *count, const Lowered &low, const Expr::Init &init,
                              bool repeat) {
    if (repeat) {
        if (init.elements.size() != 1 || init.elements.front() == nullptr || count == nullptr) {
            error(init.loc, "type mismatch");
            return;
        }
        const TypedValue value = emit_rvalue(*init.elements.front());
        if (terminated() || value.value == nullptr) {
            return;
        }
        llvm::AllocaInst *step = alloca_typed(i32(), "new.i");
        builder_.CreateStore(const_i32(0), step);
        llvm::BasicBlock *cond = llvm::BasicBlock::Create(context_, "new.rep", function_);
        llvm::BasicBlock *body = llvm::BasicBlock::Create(context_, "new.rep.body", function_);
        llvm::BasicBlock *done = llvm::BasicBlock::Create(context_, "new.rep.done", function_);
        builder_.CreateBr(cond);
        builder_.SetInsertPoint(cond);
        llvm::Value *index = builder_.CreateLoad(i32(), step);
        builder_.CreateCondBr(builder_.CreateICmpSLT(index, count), body, done);
        builder_.SetInsertPoint(body);
        store_new_element(raw, index, low, value, expr_loc(*init.elements.front()));
        builder_.CreateStore(builder_.CreateAdd(index, const_i32(1)), step);
        builder_.CreateBr(cond);
        builder_.SetInsertPoint(done);
        return;
    }
    int index = 0;
    for (const auto &element : init.elements) {
        if (element == nullptr || terminated()) {
            ++index;
            continue;
        }
        const TypedValue value = emit_rvalue(*element);
        if (terminated()) {
            return;
        }
        store_new_element(raw, const_i32(index), low, value, expr_loc(*element));
        ++index;
    }
}

TypedValue CodeGen::emit_new(const Expr::New &node) {
    set_loc(node.loc);
    if (function_ == nullptr) {
        error(node.loc, "global initializer is not a constant");
        return {zero(), 0};
    }
    const Lowered low = lower(current_file_, node.type_name, node.type_package, node.is_struct, node.indirections, false,
                              0, false);
    if (low.type == nullptr || low.elem_void || low.type->isFunctionTy()) {
        error(node.loc, "type mismatch");
        return {zero(), 0};
    }
    const uint64_t size = module_.getDataLayout().getTypeAllocSize(low.type).getFixedValue();
    uint64_t align = module_.getDataLayout().getABITypeAlign(low.type).value();
    if (align == 0) {
        align = 1;
    }
    llvm::Value *raw = nullptr;
    llvm::Value *buffer_bytes = nullptr;
    llvm::Value *element_count = nullptr;
    const bool buffer = node.count != nullptr || node.count_inferred;
    int list_len = 0;
    if (node.count_inferred && node.init != nullptr) {
        if (const auto *list = std::get_if<Expr::Init>(&node.init->data)) {
            list_len = static_cast<int>(list->elements.size());
        }
    }
    if (node.count_inferred) {
        if (list_len <= 0) {
            error(node.loc, "type mismatch");
            return {zero(), 0};
        }
        uint64_t total = 1;
        if (size != 0) {
            if (static_cast<uint64_t>(list_len) > UINT64_MAX / size) {
                error(node.loc, "not supported yet");
                return {zero(), 0};
            }
            total = size * static_cast<uint64_t>(list_len);
        }
        raw = call_malloc(node, total, align);
        buffer_bytes = llvm::ConstantInt::get(i64(), total);
        element_count = const_i32(list_len);
    } else if (node.count != nullptr) {
        // The allocator is written first, so its side effects run before the count.
        llvm::Value *receiver = nullptr;
        if (!node.builtin_alloc) {
            if (node.allocator == nullptr) {
                error(node.loc, "undeclared identifier");
                return {zero(), 0};
            }
            receiver = allocator_receiver(*node.allocator, node.alloc_is_pointer);
            if (receiver == nullptr || terminated()) {
                return {zero(), 0};
            }
        }
        const TypedValue count_value = emit_rvalue(*node.count);
        if (count_value.value == nullptr || terminated()) {
            return {zero(), 0};
        }
        llvm::BasicBlock *bad = llvm::BasicBlock::Create(context_, "new.count", function_);
        llvm::BasicBlock *ready = llvm::BasicBlock::Create(context_, "new.ready", function_);
        element_count = count_value.value;
        builder_.CreateCondBr(builder_.CreateICmpSLE(count_value.value, const_i32(0)), bad, ready);
        builder_.SetInsertPoint(bad);
        abort_now();
        builder_.SetInsertPoint(ready);
        llvm::Value *wide = builder_.CreateZExt(count_value.value, i64());
        if (size == 0) {
            buffer_bytes = llvm::ConstantInt::get(i64(), 1);
        } else {
            const uint64_t limit = UINT64_MAX / size;
            llvm::BasicBlock *overflow = llvm::BasicBlock::Create(context_, "new.overflow", function_);
            llvm::BasicBlock *sized = llvm::BasicBlock::Create(context_, "new.sized", function_);
            llvm::Value *cap = llvm::ConstantInt::get(llvm::cast<llvm::IntegerType>(i64()), limit, false);
            builder_.CreateCondBr(builder_.CreateICmpUGT(wide, cap), overflow, sized);
            builder_.SetInsertPoint(overflow);
            abort_now();
            builder_.SetInsertPoint(sized);
            buffer_bytes = builder_.CreateMul(wide, llvm::ConstantInt::get(i64(), size));
        }
        raw = heap_alloc(node, receiver, buffer_bytes, align);
    } else {
        raw = call_malloc(node, size, align);
    }
    if (raw == nullptr || terminated()) {
        return {zero(), 0};
    }
    llvm::BasicBlock *oom = llvm::BasicBlock::Create(context_, "new.oom", function_);
    llvm::BasicBlock *ok = llvm::BasicBlock::Create(context_, "new.ok", function_);
    builder_.CreateCondBr(builder_.CreateICmpEQ(raw, null_ptr()), oom, ok);
    builder_.SetInsertPoint(oom);
    abort_out_of_memory();
    builder_.SetInsertPoint(ok);
    if (buffer) {
        builder_.CreateMemSet(raw, llvm::ConstantInt::get(llvm::Type::getInt8Ty(context_), 0), buffer_bytes,
                             llvm::MaybeAlign(align));
    } else if (size > 0) {
        builder_.CreateMemSet(raw, llvm::ConstantInt::get(llvm::Type::getInt8Ty(context_), 0),
                             llvm::ConstantInt::get(i64(), size), llvm::MaybeAlign(align));
    }
    // Named fields are written into the reserved block. Omitted fields stay zero.
    if (buffer && node.init != nullptr) {
        if (const auto *list = std::get_if<Expr::Init>(&node.init->data)) {
            fill_new_buffer(raw, element_count, low, *list, node.repeat_init);
        }
    } else if (node.init != nullptr) {
        const auto *init = std::get_if<Expr::Init>(&node.init->data);
        if (init == nullptr || init->struct_id < 0) {
            error(node.loc, "type mismatch");
        } else {
            store_init_fields(init->struct_id, raw, *init);
        }
    }
    if (terminated()) {
        return {zero(), 0};
    }
    TypedValue result;
    result.value = raw;
    result.depth = low.depth + 1;
    result.elem_char = low.elem_char;
    result.struct_id = low.struct_id;
    result.elem_void = low.elem_void;
    result.elem_bool = low.elem_bool;
    result.elem_bits = low.elem_bits;
    result.elem_unsigned = low.elem_unsigned;
    result.elem_float = low.elem_float;
    result.enum_id = low.enum_id;
    if (node.count_inferred) {
        result.ptr_to_array = true;
        result.ptr_array_len = list_len;
    }
    return result;
}

void CodeGen::emit_delete(const Expr::Delete &node) {
    set_loc(node.loc);
    if (node.expr == nullptr || function_ == nullptr) {
        error(node.loc, "type mismatch");
        return;
    }
    // The allocator is written first, so its side effects run before the pointer.
    llvm::Value *receiver = nullptr;
    if (node.allocator != nullptr) {
        receiver = allocator_receiver(*node.allocator, node.alloc_is_pointer);
        if (receiver == nullptr || terminated()) {
            return;
        }
    } else if (!node.builtin_alloc) {
        error(node.loc, "undeclared identifier");
        return;
    }
    const TypedValue value = emit_rvalue(*node.expr);
    if (value.value == nullptr || terminated()) {
        return;
    }
    call_free(node, value.value, receiver);
}

void CodeGen::emit_builtin_panic() {
    llvm::Function *function = builtin_function("panic");
    if (function == nullptr || !function->empty() || function->arg_empty()) {
        return;
    }
    ensure_trace();
    const llvm::IRBuilderBase::InsertPoint saved = builder_.saveIP();
    auto *entry = llvm::BasicBlock::Create(context_, "entry", function);
    auto *head = llvm::BasicBlock::Create(context_, "head", function);
    auto *body = llvm::BasicBlock::Create(context_, "frame", function);
    auto *die = llvm::BasicBlock::Create(context_, "abort", function);
    builder_.SetInsertPoint(entry);
    llvm::Value *message = function->getArg(0);
    llvm::Value *missing = builder_.CreateICmpEQ(message, null_ptr());
    llvm::Value *text = builder_.CreateSelect(missing, builder_.CreateGlobalString("(null)"), message);
    auto *print_type = llvm::FunctionType::get(i32(), {i32(), ptr_type()}, true);
    llvm::FunctionCallee print = module_.getOrInsertFunction("dprintf", print_type);
    builder_.CreateCall(print_type, print.getCallee(),
                        {const_i32(2), builder_.CreateGlobalString("panic: %s\n"), text});
    llvm::Value *depth = builder_.CreateLoad(i32(), trace_depth_);
    builder_.CreateBr(head);
    builder_.SetInsertPoint(head);
    auto *index = builder_.CreatePHI(i32(), 2);
    index->addIncoming(depth, entry);
    builder_.CreateCondBr(builder_.CreateICmpNE(index, const_i32(0)), body, die);
    builder_.SetInsertPoint(body);
    // The newest frame is the last one pushed.
    llvm::Value *frame_index = builder_.CreateSub(index, const_i32(1));
    llvm::Value *frame = builder_.CreateInBoundsGEP(trace_stack_type_, trace_stack_, {const_i32(0), frame_index});
    auto load_field = [&](unsigned field_index, llvm::Type *type) {
        llvm::Value *slot =
            builder_.CreateInBoundsGEP(trace_frame_, frame, {const_i32(0), const_i32(static_cast<int>(field_index))});
        return builder_.CreateLoad(type, slot);
    };
    builder_.CreateCall(print_type, print.getCallee(),
                        {const_i32(2), builder_.CreateGlobalString("%s:%d:%d: %s\n"), load_field(0, ptr_type()),
                         load_field(1, i32()), load_field(2, i32()), load_field(3, ptr_type())});
    builder_.CreateBr(head);
    index->addIncoming(frame_index, body);
    builder_.SetInsertPoint(die);
    auto *abort_type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_), false);
    llvm::FunctionCallee abort_fn = module_.getOrInsertFunction("abort", abort_type);
    builder_.CreateCall(abort_type, abort_fn.getCallee());
    builder_.CreateUnreachable();
    builder_.restoreIP(saved);
}

void CodeGen::emit_tests() {
    struct TestFn {
        std::string name;
        llvm::Function *function = nullptr;
    };
    std::vector<TestFn> tests;
    if (graph_ == nullptr) {
        return;
    }
    for (const PackageNode &node : graph_->nodes) {
        int index = 0;
        current_file_ = node.file_id;
        packages_.clear();
        push();
        bind_package_globals();
        bind_imports(node);
        for (const TopDecl &decl : node.unit.decls) {
            const auto *test = std::get_if<TestDecl>(&decl.data);
            if (test == nullptr || test->body == nullptr) {
                continue;
            }
            const std::string symbol = "tlc.test." + std::to_string(node.file_id) + "." + std::to_string(index);
            ++index;
            auto *type = llvm::FunctionType::get(i32(), false);
            auto *function =
                llvm::Function::Create(type, llvm::GlobalValue::InternalLinkage, symbol, module_);
            function->setCallingConv(llvm::CallingConv::C);
            const bool saved_many = return_many_;
            const bool saved_share = share_function_scope_;
            const bool saved_extern = extern_struct_ret_;
            const bool saved_sret = extern_sret_;
            llvm::Value *saved_sret_ptr = extern_sret_ptr_;
            const CAbi saved_abi = extern_ret_abi_;
            llvm::Type *saved_struct = extern_ret_struct_;
            function_ = function;
            return_type_ = function->getReturnType();
            return_depth_ = 0;
            return_many_ = false;
            share_function_scope_ = true;
            const std::string saved_trace = trace_name_;
            trace_name_ = test->name;
            extern_struct_ret_ = false;
            extern_sret_ = false;
            extern_sret_ptr_ = nullptr;
            extern_ret_abi_ = {};
            extern_ret_struct_ = nullptr;
            llvm::BasicBlock *entry = llvm::BasicBlock::Create(context_, "entry", function);
            builder_.SetInsertPoint(entry);
            push();
            emit_stmt(*test->body);
            finish_function();
            pop();
            trace_name_ = saved_trace;
            function_ = nullptr;
            return_type_ = nullptr;
            return_depth_ = 0;
            return_many_ = saved_many;
            share_function_scope_ = saved_share;
            extern_struct_ret_ = saved_extern;
            extern_sret_ = saved_sret;
            extern_sret_ptr_ = saved_sret_ptr;
            extern_ret_abi_ = saved_abi;
            extern_ret_struct_ = saved_struct;
            tests.push_back(TestFn{test->name, function});
        }
        pop();
    }
    if (tests.empty()) {
        error(SourceLoc{}, "no tests");
        return;
    }
    auto *main_type = llvm::FunctionType::get(i32(), false);
    auto *main = llvm::Function::Create(main_type, llvm::GlobalValue::ExternalLinkage, "main", module_);
    main->setCallingConv(llvm::CallingConv::C);
    auto *printf_type = llvm::FunctionType::get(i32(), {ptr_type()}, true);
    llvm::FunctionCallee printf_fn = module_.getOrInsertFunction("printf", printf_type);
    llvm::BasicBlock *block = llvm::BasicBlock::Create(context_, "entry", main);
    builder_.SetInsertPoint(block);
    llvm::Value *failed = const_i32(0);
    for (const TestFn &test : tests) {
        llvm::BasicBlock *from = builder_.GetInsertBlock();
        llvm::Value *result = builder_.CreateCall(test.function);
        llvm::Value *bad = builder_.CreateICmpNE(result, const_i32(0));
        auto *fail_block = llvm::BasicBlock::Create(context_, "fail", main);
        auto *next_block = llvm::BasicBlock::Create(context_, "next", main);
        builder_.CreateCondBr(bad, fail_block, next_block);
        builder_.SetInsertPoint(fail_block);
        llvm::Value *format = builder_.CreateGlobalString("fail: %s\n");
        llvm::Value *label = builder_.CreateGlobalString(test.name);
        builder_.CreateCall(printf_type, printf_fn.getCallee(), {format, label});
        llvm::Value *added = builder_.CreateAdd(failed, const_i32(1));
        builder_.CreateBr(next_block);
        builder_.SetInsertPoint(next_block);
        auto *phi = builder_.CreatePHI(i32(), 2);
        phi->addIncoming(failed, from);
        phi->addIncoming(added, fail_block);
        failed = phi;
    }
    builder_.CreateRet(failed);
}

} // namespace tlc

// Compile-time folding of constants.
#include "gen/gen.hpp"

namespace tlc {

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
        if (unary->op == TokenKind::KwUnsafe && unary->expr != nullptr) {
            return fold_number(*unary->expr, seen);
        }
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

std::optional<std::string> CodeGen::constant_string(const Expr &expr, std::unordered_set<std::string> &seen) const {
    if (const auto *text = std::get_if<Expr::String>(&expr.data)) {
        return text->text;
    }
    const auto *named = std::get_if<Expr::Name>(&expr.data);
    if (named == nullptr || !seen.insert(named->text).second) {
        return std::nullopt;
    }
    const VarDecl *var = global_decl(named->text);
    std::optional<std::string> bytes;
    // `const label = "hi"` is that slice. `const p *const u8 = "hi"` is a pointer, and a `var` can be reseated.
    if (var != nullptr && var->is_const_decl && var->indirections == 0 && var->init != nullptr) {
        bytes = constant_string(*var->init, seen);
    }
    seen.erase(named->text);
    return bytes;
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
            } else if constexpr (std::is_same_v<T, Expr::Sizeof>) {
                return folded_sizeof(node);
            } else if constexpr (std::is_same_v<T, Expr::Name>) {
                if (const auto local = fold_global(current_file_, node.text)) {
                    return local;
                }
                const PackageNode *owner = find_node(current_file_);
                if (owner == nullptr || graph_ == nullptr) {
                    return std::nullopt;
                }
                for (const ImportDecl &import : owner->unit.imports) {
                    if (!import_selects(import, node.text)) {
                        continue;
                    }
                    const PackageNode *target = import_target(graph_, current_file_, import);
                    if (target == nullptr) {
                        continue;
                    }
                    if (const auto imported = fold_global(target->file_id, node.text, false)) {
                        return imported;
                    }
                }
                return std::nullopt;
            } else if constexpr (std::is_same_v<T, Expr::Selector>) {
                if (node.len_value >= 0) {
                    return node.len_value;
                }
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
                if (node.op == TokenKind::KwUnsafe) {
                    return fold(*node.expr);
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
            } else if constexpr (std::is_same_v<T, Expr::Index>) {
                // `"hi"[1]` is the byte. The length does not include the terminating null.
                if (node.is_range || node.base == nullptr || node.index == nullptr) {
                    return std::nullopt;
                }
                const std::optional<int32_t> at = fold(*node.index);
                if (!at || *at < 0) {
                    return std::nullopt;
                }
                std::unordered_set<std::string> seen;
                const std::optional<std::string> bytes = constant_string(*node.base, seen);
                if (!bytes || static_cast<std::size_t>(*at) >= bytes->size()) {
                    return std::nullopt;
                }
                return static_cast<int32_t>(static_cast<unsigned char>((*bytes)[static_cast<std::size_t>(*at)]));
            } else if constexpr (std::is_same_v<T, Expr::Call>) {
                if (!node.type_args.empty() || !node.args.empty() || node.callee == nullptr) {
                    return std::nullopt;
                }
                const auto *named = std::get_if<Expr::Name>(&node.callee->data);
                if (named == nullptr) {
                    return std::nullopt;
                }
                const FunctionDecl *fn = comptime_named(named->text);
                if (fn == nullptr || !fn->params.empty() || !fn->type_params.empty() || !fn->method_struct.empty()) {
                    return std::nullopt;
                }
                if (!comptime_folding_.insert(fn).second) {
                    return std::nullopt;
                }
                struct Leave {
                    std::unordered_set<const FunctionDecl *> &set;
                    const FunctionDecl *fn;
                    ~Leave() { set.erase(fn); }
                } leave{comptime_folding_, fn};
                return fold_comptime_body(*fn);
            } else {
                return std::nullopt;
            }
        },
        expr.data);
}

const FunctionDecl *CodeGen::comptime_named(const std::string &name) const {
    if (graph_ == nullptr || name.empty()) {
        return nullptr;
    }
    const FunctionDecl *found = nullptr;
    for (const uint32_t file : package_files(current_file_)) {
        const PackageNode *node = find_node(file);
        if (node == nullptr) {
            continue;
        }
        for (const TopDecl &decl : node->unit.decls) {
            const auto *fn = std::get_if<FunctionDecl>(&decl.data);
            if (fn == nullptr || !fn->is_comptime || fn->name != name || !fn->method_struct.empty()) {
                continue;
            }
            if (found != nullptr) {
                return nullptr;
            }
            found = fn;
        }
    }
    return found;
}

std::optional<int32_t> CodeGen::fold_comptime_body(const FunctionDecl &fn) const {
    if (fn.results.size() != 1 || fn.return_type != "i32" || fn.body == nullptr) {
        return std::nullopt;
    }
    const ResultSlot &slot = fn.results.front();
    if (slot.type_name != "i32" || !slot.type_package.empty() || slot.is_struct || slot.is_enum || slot.indirections != 0 ||
        slot.optional != 0 || slot.is_array || slot.is_fn_ptr || slot.const_pointee != 0) {
        return std::nullopt;
    }
    const auto *body = std::get_if<Stmt::Block>(&fn.body->data);
    if (body == nullptr) {
        return std::nullopt;
    }
    const Expr *value = nullptr;
    for (const auto &stmt : body->statements) {
        if (stmt == nullptr) {
            continue;
        }
        if (const auto *ret = std::get_if<Stmt::Return>(&stmt->data)) {
            if (value != nullptr || ret->value == nullptr || !ret->extra.empty()) {
                return std::nullopt;
            }
            value = ret->value.get();
            continue;
        }
        return std::nullopt;
    }
    if (value == nullptr) {
        return std::nullopt;
    }
    return fold(*value);
}

} // namespace tlc

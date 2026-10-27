// Compile-time folding of constants.
#include "gen/gen.hpp"

#include "ast/format.hpp"

namespace tlc {

namespace {

std::optional<int32_t> eval_local_op(TokenKind op, int32_t lhs, int32_t rhs) {
    auto wrap = [](int64_t value) { return static_cast<int32_t>(static_cast<uint32_t>(value)); };
    const int64_t a = lhs;
    const int64_t b = rhs;
    switch (op) {
    case TokenKind::Plus: return wrap(a + b);
    case TokenKind::Minus: return wrap(a - b);
    case TokenKind::Star: return wrap(a * b);
    case TokenKind::Slash: return b == 0 ? std::nullopt : std::optional<int32_t>(wrap(a / b));
    case TokenKind::Percent: return b == 0 ? std::nullopt : std::optional<int32_t>(wrap(a % b));
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
    case TokenKind::AmpAmp: return (lhs != 0 && rhs != 0) ? 1 : 0;
    case TokenKind::PipePipe: return (lhs != 0 || rhs != 0) ? 1 : 0;
    default: return std::nullopt;
    }
}

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

std::unique_ptr<Expr> folded_int(int32_t value, SourceLoc loc) {
    if (value < 0) {
        if (value == -2147483647 - 1) {
            return nullptr;
        }
        auto inner = folded_int(-value, loc);
        if (inner == nullptr) {
            return nullptr;
        }
        auto expr = std::make_unique<Expr>();
        Expr::Unary node;
        node.loc = loc;
        node.op = TokenKind::Minus;
        node.expr = std::move(inner);
        expr->data = std::move(node);
        return expr;
    }
    auto expr = std::make_unique<Expr>();
    Expr::Int node;
    node.loc = loc;
    node.value = static_cast<uint64_t>(value);
    expr->data = std::move(node);
    return expr;
}

// A field of a struct literal, including `literal.inner.x`. A name is not a literal.
const Expr *const_struct_field(const Expr &object, const std::string &field) {
    const Expr *cursor = &object;
    if (const auto *selector = std::get_if<Expr::Selector>(&cursor->data)) {
        if (selector->base == nullptr) {
            return nullptr;
        }
        cursor = const_struct_field(*selector->base, selector->field);
        if (cursor == nullptr) {
            return nullptr;
        }
    }
    if (const auto *index = std::get_if<Expr::Index>(&cursor->data)) {
        // `(Point{.x = 1}, 2)[0].x` is that field. The index is the literal.
        if (index->is_range || index->base == nullptr || index->index == nullptr) {
            return nullptr;
        }
        const auto *number = std::get_if<Expr::Int>(&index->index->data);
        if (number == nullptr || number->value > 2147483647ULL) {
            return nullptr;
        }
        const auto at = static_cast<std::size_t>(number->value);
        const auto *tuple = std::get_if<Expr::Init>(&index->base->data);
        if (tuple == nullptr || !tuple->is_tuple || at >= tuple->elements.size() || tuple->elements[at] == nullptr) {
            return nullptr;
        }
        cursor = tuple->elements[at].get();
    }
    const auto *init = std::get_if<Expr::Init>(&cursor->data);
    if (init == nullptr || init->is_tuple || !init->elements.empty()) {
        return nullptr;
    }
    for (const Expr::FieldInit &item : init->fields) {
        if (item.field == field && item.value != nullptr) {
            return item.value.get();
        }
    }
    return nullptr;
}

bool plain_float_name(const std::string &name) {
    return name == "f32" || name == "f64" || name == "float" || name == "double";
}

std::unique_ptr<Expr> folded_string(std::string text, SourceLoc loc) {
    auto expr = std::make_unique<Expr>();
    Expr::String node;
    node.loc = loc;
    node.text = std::move(text);
    expr->data = std::move(node);
    return expr;
}

const Expr *single_i32_return(const FunctionDecl &fn) {
    if (fn.results.size() != 1 || fn.return_type != "i32" || fn.body == nullptr) {
        return nullptr;
    }
    const ResultSlot &slot = fn.results.front();
    if (slot.type_name != "i32" || !slot.type_package.empty() || slot.is_struct || slot.is_enum || slot.indirections != 0 ||
        slot.optional != 0 || slot.is_array || slot.is_fn_ptr || slot.const_pointee != 0) {
        return nullptr;
    }
    const auto *body = std::get_if<Stmt::Block>(&fn.body->data);
    if (body == nullptr) {
        return nullptr;
    }
    const Expr *value = nullptr;
    for (const auto &stmt : body->statements) {
        if (stmt == nullptr) {
            continue;
        }
        const auto *ret = std::get_if<Stmt::Return>(&stmt->data);
        if (ret == nullptr || value != nullptr || ret->value == nullptr || !ret->extra.empty()) {
            return nullptr;
        }
        value = ret->value.get();
    }
    return value;
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
        if (unary->op == TokenKind::Star && !unary->postfix && unary->expr != nullptr) {
            std::unordered_set<std::string> pointed;
            if (const Expr *target = comptime_pointee(*unary->expr, pointed)) {
                return fold_number(*target, seen);
            }
            return std::nullopt;
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
        if (written != nullptr && written->is_comptime && written->init != nullptr) {
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
    if (const auto *call = std::get_if<Expr::Call>(&expr.data)) {
        if (!is_builtin_fmt(*call) || call->args.size() != 2 || call->args[0] == nullptr || call->args[1] == nullptr) {
            return std::nullopt;
        }
        const std::optional<std::string> fmt = constant_string(*call->args[0], seen);
        if (!fmt) {
            return std::nullopt;
        }
        return format_placeholders(
            *fmt, *call->args[1], [&](const Expr &element) { return fold(element); },
            [&](const Expr &element) {
                std::unordered_set<std::string> inner;
                return constant_string(element, inner);
            });
    }
    if (const auto *index = std::get_if<Expr::Index>(&expr.data)) {
        // `"hello"[1:4]` is those bytes. The length does not include the terminating null.
        // A high bound below the low bound, or a bound past that length, is not this slice.
        if (!index->is_range || index->base == nullptr) {
            return std::nullopt;
        }
        const std::optional<std::string> bytes = constant_string(*index->base, seen);
        if (!bytes) {
            return std::nullopt;
        }
        std::size_t low = 0;
        if (index->index != nullptr) {
            const std::optional<int32_t> at = fold(*index->index);
            if (!at || *at < 0 || static_cast<std::size_t>(*at) > bytes->size()) {
                return std::nullopt;
            }
            low = static_cast<std::size_t>(*at);
        }
        std::size_t high = bytes->size();
        if (index->end != nullptr) {
            const std::optional<int32_t> at = fold(*index->end);
            if (!at || *at < 0 || static_cast<std::size_t>(*at) > bytes->size()) {
                return std::nullopt;
            }
            high = static_cast<std::size_t>(*at);
        }
        if (low > high) {
            return std::nullopt;
        }
        return bytes->substr(low, high - low);
    }
    const auto *named = std::get_if<Expr::Name>(&expr.data);
    if (named == nullptr || !seen.insert(named->text).second) {
        return std::nullopt;
    }
    const VarDecl *var = global_decl(named->text);
    std::optional<std::string> bytes;
    // `const label = "hi"` is that slice. `const p *const u8 = "hi"` is a pointer, and a `var` can be reseated.
    if (var != nullptr && var->is_comptime && var->indirections == 0 && var->init != nullptr) {
        bytes = constant_string(*var->init, seen);
    }
    seen.erase(named->text);
    return bytes;
}

std::optional<int32_t> CodeGen::known_len(const Expr &base) const {
    std::unordered_set<std::string> seen;
    if (const std::optional<std::string> text = constant_string(base, seen)) {
        if (text->size() > 2147483647) {
            return std::nullopt;
        }
        return static_cast<int32_t>(text->size());
    }
    const auto *call = std::get_if<Expr::Call>(&base.data);
    if (call == nullptr || call->callee == nullptr || !call->args.empty() || graph_ == nullptr) {
        return std::nullopt;
    }
    const auto *method = std::get_if<Expr::Selector>(&call->callee->data);
    if (method == nullptr || method->field != "fields" || method->base == nullptr) {
        return std::nullopt;
    }
    const auto *info = std::get_if<Expr::Typeof>(&method->base->data);
    if (info == nullptr || info->canonical.empty()) {
        return std::nullopt;
    }
    const auto found = graph_->reflected_fields.find(info->canonical);
    if (found == graph_->reflected_fields.end()) {
        return 0;
    }
    return static_cast<int32_t>(found->second.size());
}

std::optional<int32_t> CodeGen::folded_local_name(const Expr &expr) const {
    const auto *named = std::get_if<Expr::Name>(&expr.data);
    if (named == nullptr) {
        return std::nullopt;
    }
    const Binding *binding = lookup(named->text);
    if (binding == nullptr || !binding->folded_local) {
        return std::nullopt;
    }
    return binding->const_value;
}

const Expr *CodeGen::comptime_pointee(const Expr &pointer, std::unordered_set<std::string> &seen) const {
    // `*pair[0]` is the pointee of that element. `pair[0]` is `&addr`.
    if (const Expr *element = comptime_tuple_element(pointer)) {
        return comptime_pointee(*element, seen);
    }
    if (const auto *unary = std::get_if<Expr::Unary>(&pointer.data)) {
        if (unary->postfix || unary->expr == nullptr) {
            return nullptr;
        }
        if (unary->op == TokenKind::Amp) {
            const auto *named = std::get_if<Expr::Name>(&unary->expr->data);
            if (named == nullptr) {
                return nullptr;
            }
            if (const Binding *binding = lookup(named->text);
                binding != nullptr && (binding->folded_local || binding->immediate || binding->const_init != nullptr)) {
                return unary->expr.get();
            }
            if (const VarDecl *var = global_decl(named->text); var != nullptr && var->is_comptime) {
                return unary->expr.get();
            }
            return nullptr;
        }
        if (unary->op == TokenKind::Star) {
            const Expr *mid = comptime_pointee(*unary->expr, seen);
            if (mid == nullptr) {
                return nullptr;
            }
            return comptime_pointee(*mid, seen);
        }
        return nullptr;
    }
    const auto *named = std::get_if<Expr::Name>(&pointer.data);
    if (named == nullptr || !seen.insert(named->text).second) {
        return nullptr;
    }
    const Expr *init = nullptr;
    if (const Binding *binding = lookup(named->text)) {
        init = binding->const_init;
    }
    if (init == nullptr) {
        if (const VarDecl *var = global_decl(named->text); var != nullptr && var->is_comptime) {
            init = var->init.get();
        }
    }
    const Expr *target = init == nullptr ? nullptr : comptime_pointee(*init, seen);
    seen.erase(named->text);
    return target;
}

std::optional<int32_t> CodeGen::local_folded_i32(const Expr &expr) const {
    if (const std::optional<int32_t> local = folded_local_name(expr)) {
        return local;
    }
    // A unary or binary is walked here. `fold` would read a file-scope const through
    // a local of the same name. Other expressions, including a field default, use `fold`.
    if (std::holds_alternative<Expr::Unary>(expr.data) || std::holds_alternative<Expr::Binary>(expr.data)) {
        return std::visit(
            [&](const auto &node) -> std::optional<int32_t> {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, Expr::Unary>) {
                    if (node.postfix || node.expr == nullptr) {
                        return std::nullopt;
                    }
                    if (node.op == TokenKind::Star) {
                        std::unordered_set<std::string> pointed;
                        if (const Expr *target = comptime_pointee(*node.expr, pointed)) {
                            return local_folded_i32(*target);
                        }
                        return std::nullopt;
                    }
                    const std::optional<int32_t> inner = local_folded_i32(*node.expr);
                    if (!inner) {
                        return std::nullopt;
                    }
                    switch (node.op) {
                    case TokenKind::Plus: return *inner;
                    case TokenKind::Minus:
                        return static_cast<int32_t>(static_cast<uint32_t>(-static_cast<int64_t>(*inner)));
                    case TokenKind::Tilde: return static_cast<int32_t>(~static_cast<uint32_t>(*inner));
                    default: return std::nullopt;
                    }
                } else if constexpr (std::is_same_v<T, Expr::Binary>) {
                    if (node.lhs == nullptr || node.rhs == nullptr) {
                        return std::nullopt;
                    }
                    const std::optional<int32_t> lhs = local_folded_i32(*node.lhs);
                    const std::optional<int32_t> rhs = local_folded_i32(*node.rhs);
                    if (!lhs || !rhs) {
                        return std::nullopt;
                    }
                    return eval_local_op(node.op, *lhs, *rhs);
                } else {
                    return std::nullopt;
                }
            },
            expr.data);
    }
    return fold(expr);
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
                if (node.field == "len" && node.base != nullptr) {
                    if (const std::optional<int32_t> known = known_len(*node.base)) {
                        return known;
                    }
                    if (const auto *init = std::get_if<Expr::Init>(&node.base->data)) {
                        if (init->is_tuple && init->elements.size() >= 2) {
                            return static_cast<int32_t>(init->elements.size());
                        }
                    }
                }
                // `Point{.x = 1}.x` is that field. A file-scope name is still loaded. An enumerator is not a field.
                if (node.base != nullptr) {
                    if (const Expr *field = const_struct_field(*node.base, node.field)) {
                        return fold(*field);
                    }
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
                if (node.op == TokenKind::Star) {
                    std::unordered_set<std::string> pointed;
                    if (const Expr *target = comptime_pointee(*node.expr, pointed)) {
                        return fold(*target);
                    }
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
                // Two `typeof` values compare by spelling and flags. The union is not required.
                if (node.lhs != nullptr && node.rhs != nullptr &&
                    (node.op == TokenKind::EqEq || node.op == TokenKind::BangEq)) {
                    const auto *left_of = std::get_if<Expr::Typeof>(&node.lhs->data);
                    const auto *right_of = std::get_if<Expr::Typeof>(&node.rhs->data);
                    if (left_of != nullptr && right_of != nullptr && !left_of->canonical.empty() &&
                        !right_of->canonical.empty()) {
                        const bool same = left_of->canonical == right_of->canonical &&
                                          left_of->type_arm == right_of->type_arm &&
                                          left_of->type_has_this == right_of->type_has_this &&
                                          left_of->type_generic == right_of->type_generic &&
                                          left_of->type_variadic == right_of->type_variadic;
                        if (node.op == TokenKind::EqEq) {
                            return same ? 1 : 0;
                        }
                        return same ? 0 : 1;
                    }
                }
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
                // `pair[0][1]` walks each index. The base may itself be an index.
                if (node.is_range || node.base == nullptr || node.index == nullptr) {
                    return std::nullopt;
                }
                const std::optional<int32_t> at = fold(*node.index);
                if (!at || *at < 0) {
                    return std::nullopt;
                }
                const auto tuple_element = [&](const auto &self, const Expr &base, int index, int depth) -> const Expr * {
                    if (depth > 8 || index < 0) {
                        return nullptr;
                    }
                    if (const auto *init = std::get_if<Expr::Init>(&base.data)) {
                        if (!init->is_tuple || static_cast<std::size_t>(index) >= init->elements.size()) {
                            return nullptr;
                        }
                        return init->elements[static_cast<std::size_t>(index)].get();
                    }
                    if (const auto *named = std::get_if<Expr::Name>(&base.data)) {
                        const Expr *init_expr = nullptr;
                        if (const Binding *binding = lookup(named->text)) {
                            init_expr = binding->const_init;
                        }
                        if (init_expr == nullptr) {
                            if (const VarDecl *var = global_decl(named->text); var != nullptr && var->is_comptime) {
                                init_expr = var->init.get();
                            }
                        }
                        if (init_expr == nullptr) {
                            return nullptr;
                        }
                        return self(self, *init_expr, index, depth + 1);
                    }
                    if (const auto *inner = std::get_if<Expr::Index>(&base.data)) {
                        if (inner->is_range || inner->base == nullptr || inner->index == nullptr) {
                            return nullptr;
                        }
                        const std::optional<int32_t> at_inner = fold(*inner->index);
                        if (!at_inner || *at_inner < 0) {
                            return nullptr;
                        }
                        const Expr *element = self(self, *inner->base, *at_inner, depth + 1);
                        if (element == nullptr) {
                            return nullptr;
                        }
                        return self(self, *element, index, depth + 1);
                    }
                    return nullptr;
                };
                if (const Expr *element = tuple_element(tuple_element, *node.base, *at, 0)) {
                    return fold(*element);
                }
                // `pair[0][0]` reads that array element. A lone `row[0]` still loads.
                if (const auto *inner = std::get_if<Expr::Index>(&node.base->data);
                    inner != nullptr && !inner->is_range && inner->base != nullptr && inner->index != nullptr) {
                    const std::optional<int32_t> slot = fold(*inner->index);
                    if (slot && *slot >= 0) {
                        if (const Expr *array = tuple_element(tuple_element, *inner->base, *slot, 0)) {
                            const auto array_at = [&](const auto &self, const Expr &value, int depth) -> const Expr * {
                                if (depth > 8) {
                                    return nullptr;
                                }
                                if (const auto *made = std::get_if<Expr::Init>(&value.data)) {
                                    if (!made->array_literal ||
                                        static_cast<std::size_t>(*at) >= made->elements.size()) {
                                        return nullptr;
                                    }
                                    return made->elements[static_cast<std::size_t>(*at)].get();
                                }
                                const auto *named = std::get_if<Expr::Name>(&value.data);
                                if (named == nullptr) {
                                    return nullptr;
                                }
                                const Expr *next = nullptr;
                                if (const Binding *binding = lookup(named->text)) {
                                    next = binding->const_init;
                                }
                                if (next == nullptr) {
                                    if (const VarDecl *var = global_decl(named->text);
                                        var != nullptr && var->is_comptime) {
                                        next = var->init.get();
                                    }
                                }
                                if (next == nullptr || next == &value) {
                                    return nullptr;
                                }
                                return self(self, *next, depth + 1);
                            };
                            if (const Expr *item = array_at(array_at, *array, 0)) {
                                return fold(*item);
                            }
                        }
                    }
                }
                std::unordered_set<std::string> seen;
                const std::optional<std::string> bytes = constant_string(*node.base, seen);
                if (!bytes || static_cast<std::size_t>(*at) >= bytes->size()) {
                    return std::nullopt;
                }
                return static_cast<int32_t>(static_cast<unsigned char>((*bytes)[static_cast<std::size_t>(*at)]));
            } else if constexpr (std::is_same_v<T, Expr::Call>) {
                if (const FunctionDecl *fn = node.callee != nullptr ? mem_compare_decl(node) : nullptr) {
                    const MemCompare kind = mem_compare_kind(*fn);
                    if (mem_compare_slice(kind) && node.args.size() == 2 && node.type_args.size() <= 1 &&
                        node.args[0] != nullptr && node.args[1] != nullptr &&
                        (node.type_args.empty() || (plain_named_type_arg(node.type_args.front()) &&
                                                    node.type_args.front().type_name == "u8"))) {
                        std::unordered_set<std::string> left_seen;
                        std::unordered_set<std::string> right_seen;
                        const std::optional<std::string> left = constant_string(*node.args[0], left_seen);
                        const std::optional<std::string> right = constant_string(*node.args[1], right_seen);
                        if (left && right) {
                            return mem_compare_bytes(*left, *right, kind);
                        }
                    }
                }
                if (!node.type_args.empty() || node.callee == nullptr) {
                    return std::nullopt;
                }
                const auto *named = std::get_if<Expr::Name>(&node.callee->data);
                if (named == nullptr) {
                    return std::nullopt;
                }
                const FunctionDecl *fn = fold_call_target(named->text, node);
                if (fn == nullptr) {
                    return std::nullopt;
                }
                return fold_substituted_call(*fn, node);
            } else {
                return std::nullopt;
            }
        },
        expr.data);
}

const FunctionDecl *CodeGen::mem_compare_decl(const Expr::Call &call) const {
    if (call.callee == nullptr || graph_ == nullptr || call.args.size() != 2 || call.type_args.size() > 1 ||
        call.args[0] == nullptr || call.args[1] == nullptr) {
        return nullptr;
    }
    if (call.type_args.size() == 1 &&
        (!plain_named_type_arg(call.type_args.front()) || call.type_args.front().type_name != "u8")) {
        return nullptr;
    }
    std::unordered_set<std::string> left_seen;
    std::unordered_set<std::string> right_seen;
    if (!constant_string(*call.args[0], left_seen) || !constant_string(*call.args[1], right_seen)) {
        return nullptr;
    }
    auto exported = [&](uint32_t file, const std::string &name) -> const FunctionDecl * {
        const PackageNode *start = find_node(file);
        if (start == nullptr || !is_compiler_builtin(start)) {
            return nullptr;
        }
        const FunctionDecl *comptime_fn = nullptr;
        const FunctionDecl *runtime_fn = nullptr;
        bool clash = false;
        for (const uint32_t id : package_files(file)) {
            const PackageNode *node = find_node(id);
            if (node == nullptr || !is_compiler_builtin(node)) {
                continue;
            }
            for (const TopDecl &decl : node->unit.decls) {
                const auto *fn = std::get_if<FunctionDecl>(&decl.data);
                if (fn == nullptr || !fn->exported || fn->name != name || fn->params.size() != 2) {
                    continue;
                }
                const MemCompare kind = mem_compare_kind(*fn);
                if (!mem_compare_slice(kind)) {
                    continue;
                }
                const FunctionDecl **slot = mem_compare_comptime(kind) ? &comptime_fn : &runtime_fn;
                if (*slot != nullptr) {
                    clash = true;
                }
                *slot = fn;
            }
        }
        if (clash) {
            return nullptr;
        }
        if (comptime_fn != nullptr) {
            return comptime_fn;
        }
        return runtime_fn;
    };
    if (const auto *named = std::get_if<Expr::Name>(&call.callee->data)) {
        const PackageNode *node = find_node(current_file_);
        if (node == nullptr) {
            return nullptr;
        }
        const FunctionDecl *found = nullptr;
        for (const ImportDecl &import : node->unit.imports) {
            if (import.form != ImportDecl::Form::Selective) {
                continue;
            }
            bool listed = false;
            for (const std::string &item : import.names) {
                if (item == named->text) {
                    listed = true;
                }
            }
            if (!listed) {
                continue;
            }
            const PackageNode *target = import_target(graph_, current_file_, import);
            if (target == nullptr) {
                continue;
            }
            const FunctionDecl *fn = exported(target->file_id, named->text);
            if (fn == nullptr) {
                continue;
            }
            if (found != nullptr) {
                return nullptr;
            }
            found = fn;
        }
        return found;
    }
    const auto *selector = std::get_if<Expr::Selector>(&call.callee->data);
    if (selector == nullptr || selector->base == nullptr) {
        return nullptr;
    }
    const auto *base = std::get_if<Expr::Name>(&selector->base->data);
    if (base == nullptr) {
        return nullptr;
    }
    const uint32_t target = package_file(current_file_, base->text);
    if (target == 0) {
        return nullptr;
    }
    return exported(target, selector->field);
}

bool CodeGen::is_builtin_fmt(const Expr::Call &call) const {
    if (graph_ == nullptr || call.callee == nullptr || call.args.size() != 2) {
        return false;
    }
    uint32_t file = 0;
    std::string field;
    if (const auto *selector = std::get_if<Expr::Selector>(&call.callee->data)) {
        const auto *base = selector->base == nullptr ? nullptr : std::get_if<Expr::Name>(&selector->base->data);
        if (base == nullptr) {
            return false;
        }
        file = package_file(current_file_, base->text);
        field = selector->field;
    } else if (const auto *named = std::get_if<Expr::Name>(&call.callee->data)) {
        if (named->text != "fmt") {
            return false;
        }
        const PackageNode *node = find_node(current_file_);
        if (node == nullptr) {
            return false;
        }
        for (const ImportDecl &import : node->unit.imports) {
            if (import.form != ImportDecl::Form::Selective) {
                continue;
            }
            bool listed = false;
            for (const std::string &item : import.names) {
                if (item == "fmt") {
                    listed = true;
                }
            }
            if (!listed) {
                continue;
            }
            const PackageNode *target = import_target(graph_, current_file_, import);
            if (target == nullptr) {
                continue;
            }
            file = target->file_id;
            field = "fmt";
            break;
        }
    }
    if (file == 0 || field != "fmt") {
        return false;
    }
    const PackageNode *start = find_node(file);
    if (start == nullptr || !is_compiler_builtin(start)) {
        return false;
    }
    for (const uint32_t id : package_files(file)) {
        const PackageNode *node = find_node(id);
        if (node == nullptr) {
            continue;
        }
        for (const TopDecl &decl : node->unit.decls) {
            const auto *fn = std::get_if<FunctionDecl>(&decl.data);
            if (fn != nullptr && fn->exported && fn->name == "fmt" && fn->type_params.size() == 1 && fn->params.size() == 2 &&
                fn->params[0].is_comptime && fn->params[1].is_comptime) {
                return true;
            }
        }
    }
    return false;
}

const FunctionDecl *CodeGen::fold_call_target(const std::string &name, const Expr::Call &call) const {
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
            if (fn == nullptr || fn->name != name || fn->is_macro || !fn->method_struct.empty() ||
                !fn->type_params.empty() || fn->params.size() != call.args.size()) {
                continue;
            }
            bool baked = !fn->is_comptime && !fn->params.empty();
            if (baked) {
                for (const Param &param : fn->params) {
                    if (!param.is_comptime) {
                        baked = false;
                    }
                }
            }
            if (!fn->is_comptime && !baked) {
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

std::optional<int32_t> CodeGen::fold_substituted_call(const FunctionDecl &fn, const Expr::Call &call) const {
    if (fn.is_macro || fn.variadic || !fn.type_params.empty() || !fn.method_struct.empty() ||
        call.args.size() != fn.params.size() || comptime_folding_.contains(&fn)) {
        return std::nullopt;
    }
    std::vector<std::unique_ptr<Expr>> owned;
    std::vector<std::string> names;
    owned.reserve(fn.params.size());
    names.reserve(fn.params.size());
    for (std::size_t i = 0; i < fn.params.size(); ++i) {
        if (call.args[i] == nullptr || (!fn.is_comptime && !fn.params[i].is_comptime)) {
            return std::nullopt;
        }
        const SourceLoc loc = expr_loc(*call.args[i]);
        std::unique_ptr<Expr> literal;
        if (const std::optional<int32_t> local = folded_local_name(*call.args[i])) {
            literal = folded_int(*local, loc);
        } else if (const std::optional<int32_t> number = fold(*call.args[i])) {
            literal = folded_int(*number, loc);
        } else {
            std::unordered_set<std::string> seen;
            if (const std::optional<std::string> text = constant_string(*call.args[i], seen)) {
                literal = folded_string(*text, loc);
            } else if (comptime_constant(*call.args[i]) || folded_case_value(*call.args[i])) {
                // The name of a file-scope const becomes its initializer. An enumerator stays the selector.
                literal = comptime_substitute(*call.args[i]);
            }
        }
        if (literal == nullptr) {
            return std::nullopt;
        }
        names.push_back(fn.params[i].name);
        owned.push_back(std::move(literal));
    }
    const Expr *value = single_i32_return(fn);
    if (value == nullptr || !comptime_folding_.insert(&fn).second) {
        return std::nullopt;
    }
    struct Leave {
        std::unordered_set<const FunctionDecl *> &set;
        const FunctionDecl *kept;
        ~Leave() { set.erase(kept); }
    } leave{comptime_folding_, &fn};
    std::vector<const Expr *> exprs;
    exprs.reserve(owned.size());
    for (const auto &item : owned) {
        exprs.push_back(item.get());
    }
    std::unique_ptr<Expr> rewritten = clone_expr(*value);
    replace_names(rewritten, names, exprs);
    return fold(*rewritten);
}

bool CodeGen::local_shadow(const std::string &name) const {
    // scopes_[0] is the package. A file-scope const lives there. A local is a later scope.
    for (std::size_t index = 1; index < scopes_.size(); ++index) {
        if (scopes_[index].contains(name)) {
            return true;
        }
    }
    return false;
}

bool CodeGen::file_function_name(const std::string &name) const {
    if (name.empty() || graph_ == nullptr || local_shadow(name)) {
        return false;
    }
    for (const uint32_t file : package_files(current_file_)) {
        const PackageNode *node = find_node(file);
        if (node == nullptr) {
            continue;
        }
        for (const TopDecl &decl : node->unit.decls) {
            const auto *fn = std::get_if<FunctionDecl>(&decl.data);
            if (fn == nullptr || fn->name != name || !fn->method_struct.empty()) {
                continue;
            }
            if (file != current_file_ && fn->is_static) {
                continue;
            }
            return true;
        }
    }
    return false;
}

const VarDecl *CodeGen::package_var(const std::string &name) const {
    if (const VarDecl *found = global_decl(name)) {
        return found;
    }
    if (graph_ == nullptr || name.empty()) {
        return nullptr;
    }
    for (const uint32_t file : package_files(current_file_)) {
        if (file == current_file_) {
            continue;
        }
        const PackageNode *node = find_node(file);
        if (node == nullptr) {
            continue;
        }
        for (const TopDecl &decl : node->unit.decls) {
            const auto *var = std::get_if<VarDecl>(&decl.data);
            if (var != nullptr && var->owner.empty() && var->name == name && !var->is_static) {
                return var;
            }
        }
    }
    return nullptr;
}

const TypedefDecl *CodeGen::package_typedef_decl(const std::string &name) const {
    if (graph_ == nullptr || name.empty()) {
        return nullptr;
    }
    const TypedefDecl *fallback = nullptr;
    for (const uint32_t file : package_files(current_file_)) {
        const PackageNode *node = find_node(file);
        if (node == nullptr) {
            continue;
        }
        for (const TopDecl &decl : node->unit.decls) {
            const auto *alias = std::get_if<TypedefDecl>(&decl.data);
            if (alias == nullptr || alias->name != name || !alias->type_params.empty()) {
                continue;
            }
            if (file != current_file_ && !alias->exported) {
                continue;
            }
            if (file == current_file_) {
                return alias;
            }
            if (fallback == nullptr) {
                fallback = alias;
            }
        }
    }
    return fallback;
}

bool CodeGen::enumerator_value(const Expr &expr) const {
    const auto *selector = std::get_if<Expr::Selector>(&expr.data);
    if (selector == nullptr || selector->base == nullptr) {
        return false;
    }
    const int id = enum_type_designator(*selector->base);
    if (id < 0 || id >= static_cast<int>(enums_.size())) {
        return false;
    }
    const auto &values = enums_[static_cast<std::size_t>(id)].values;
    return values.find(selector->field) != values.end();
}

bool CodeGen::file_constant_operand(const Expr &expr) const {
    return std::visit(
        [&](const auto &node) -> bool {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Expr::Int> || std::is_same_v<T, Expr::Bool> ||
                          std::is_same_v<T, Expr::Char> || std::is_same_v<T, Expr::Float> ||
                          std::is_same_v<T, Expr::Nullptr>) {
                return true;
            } else if constexpr (std::is_same_v<T, Expr::Sizeof>) {
                return fold(expr).has_value();
            } else if constexpr (std::is_same_v<T, Expr::Binary>) {
                if (fold(expr)) {
                    return true;
                }
                const std::optional<FoldedNumber> number = fold_number(expr);
                return number.has_value() && number->is_float;
            } else if constexpr (std::is_same_v<T, Expr::Name>) {
                if (local_shadow(node.text)) {
                    return false;
                }
                if (file_function_name(node.text)) {
                    return true;
                }
                const VarDecl *var = package_var(node.text);
                return var != nullptr && var->is_comptime;
            } else if constexpr (std::is_same_v<T, Expr::Selector>) {
                if (node.len_value >= 0 || enumerator_value(expr)) {
                    return true;
                }
                if (node.base == nullptr) {
                    return false;
                }
                const auto *named = std::get_if<Expr::Name>(&node.base->data);
                if (named == nullptr || local_shadow(named->text)) {
                    return false;
                }
                const uint32_t target = package_file(current_file_, named->text);
                if (target == 0) {
                    return false;
                }
                const PackageNode *package = find_node(target);
                if (package == nullptr) {
                    return false;
                }
                for (const TopDecl &decl : package->unit.decls) {
                    if (const auto *fn = std::get_if<FunctionDecl>(&decl.data)) {
                        if (fn->name == node.field && fn->exported && fn->method_struct.empty()) {
                            return true;
                        }
                    }
                    if (const auto *var = std::get_if<VarDecl>(&decl.data)) {
                        if (var->name == node.field && var->is_comptime && var->exported && var->owner.empty()) {
                            return true;
                        }
                    }
                }
                return false;
            } else if constexpr (std::is_same_v<T, Expr::Index>) {
                if (node.is_range) {
                    std::unordered_set<std::string> seen;
                    return constant_string(expr, seen).has_value();
                }
                return fold(expr).has_value();
            } else if constexpr (std::is_same_v<T, Expr::Cast>) {
                if (node.indirections != 0 || node.is_fn_ptr || node.expr == nullptr) {
                    return false;
                }
                return file_constant_operand(*node.expr);
            } else if constexpr (std::is_same_v<T, Expr::Init>) {
                if (!node.elements.empty()) {
                    return false;
                }
                for (const Expr::FieldInit &field : node.fields) {
                    if (field.value == nullptr || !file_constant_operand(*field.value)) {
                        return false;
                    }
                }
                return true;
            } else if constexpr (std::is_same_v<T, Expr::Unary>) {
                if (node.expr == nullptr) {
                    return false;
                }
                if (node.op == TokenKind::Plus || node.op == TokenKind::Minus || node.op == TokenKind::Tilde ||
                    node.op == TokenKind::Bang || node.op == TokenKind::KwUnsafe) {
                    return file_constant_operand(*node.expr);
                }
                if (node.op == TokenKind::Star && !node.postfix) {
                    std::unordered_set<std::string> pointed;
                    if (const Expr *target = comptime_pointee(*node.expr, pointed)) {
                        return file_constant_operand(*target);
                    }
                    return false;
                }
                if (node.op != TokenKind::Amp) {
                    return false;
                }
                const auto *named = std::get_if<Expr::Name>(&node.expr->data);
                if (named == nullptr || local_shadow(named->text)) {
                    return false;
                }
                const VarDecl *var = package_var(named->text);
                return var != nullptr && !var->is_extern && var->owner.empty();
            } else {
                return false;
            }
        },
        expr.data);
}

const Expr *CodeGen::comptime_tuple_element(const Expr &expr) const {
    const auto *index = std::get_if<Expr::Index>(&expr.data);
    if (index == nullptr || index->is_range || index->base == nullptr || index->index == nullptr) {
        return nullptr;
    }
    const std::optional<int32_t> at = fold(*index->index);
    if (!at || *at < 0) {
        return nullptr;
    }
    const auto walk = [&](const auto &self, const Expr &base, int slot, int depth) -> const Expr * {
        if (depth > 8 || slot < 0) {
            return nullptr;
        }
        if (const auto *init = std::get_if<Expr::Init>(&base.data)) {
            if (!init->is_tuple || static_cast<std::size_t>(slot) >= init->elements.size()) {
                return nullptr;
            }
            return init->elements[static_cast<std::size_t>(slot)].get();
        }
        if (const auto *named = std::get_if<Expr::Name>(&base.data)) {
            const Expr *init_expr = nullptr;
            if (const Binding *binding = lookup(named->text)) {
                init_expr = binding->const_init;
            }
            if (init_expr == nullptr) {
                if (const VarDecl *var = global_decl(named->text); var != nullptr && var->is_comptime) {
                    init_expr = var->init.get();
                }
            }
            if (init_expr == nullptr) {
                return nullptr;
            }
            return self(self, *init_expr, slot, depth + 1);
        }
        if (const auto *inner = std::get_if<Expr::Index>(&base.data)) {
            if (inner->is_range || inner->base == nullptr || inner->index == nullptr) {
                return nullptr;
            }
            const std::optional<int32_t> at_inner = fold(*inner->index);
            if (!at_inner || *at_inner < 0) {
                return nullptr;
            }
            const Expr *element = self(self, *inner->base, *at_inner, depth + 1);
            if (element == nullptr) {
                return nullptr;
            }
            return self(self, *element, slot, depth + 1);
        }
        return nullptr;
    };
    return walk(walk, *index->base, *at, 0);
}

bool CodeGen::comptime_constant(const Expr &expr) const {
    std::unordered_set<std::string> seen;
    const auto visit = [&](const auto &self, const Expr &value) -> bool {
        // `pair[0]` is that element. A function does not fold to an integer.
        if (const Expr *element = comptime_tuple_element(value)) {
            if (self(self, *element)) {
                return true;
            }
            std::unordered_set<std::string> text_seen;
            if (constant_string(*element, text_seen) || enumerator_value(*element)) {
                return true;
            }
            if (std::holds_alternative<Expr::Nullptr>(element->data)) {
                return false;
            }
            return fold(*element).has_value();
        }
        if (const auto *init = std::get_if<Expr::Init>(&value.data)) {
            if (init->array_literal || (!init->is_tuple && !init->elements.empty())) {
                return false;
            }
            if (init->is_tuple) {
                if (init->elements.size() < 2) {
                    return false;
                }
                // `(&addr, 2)` and `(p, 2)` when `p` is `&addr`. A lone `&addr` is not this value.
                const auto address_element = [&](const Expr &start) {
                    const Expr *cursor = &start;
                    for (int depth = 0; cursor != nullptr && depth <= 8; ++depth) {
                        if (const auto *unary = std::get_if<Expr::Unary>(&cursor->data);
                            unary != nullptr && !unary->postfix && unary->op == TokenKind::Amp &&
                            unary->expr != nullptr) {
                            const auto *named = std::get_if<Expr::Name>(&unary->expr->data);
                            if (named == nullptr) {
                                return false;
                            }
                            if (const Binding *binding = lookup(named->text);
                                binding != nullptr && binding->const_init != nullptr) {
                                return true;
                            }
                            if (const VarDecl *var = global_decl(named->text); var != nullptr && var->is_comptime) {
                                return true;
                            }
                            return false;
                        }
                        const auto *named = std::get_if<Expr::Name>(&cursor->data);
                        if (named == nullptr) {
                            return false;
                        }
                        const Expr *next = nullptr;
                        if (const Binding *binding = lookup(named->text)) {
                            next = binding->const_init;
                        }
                        if (next == nullptr) {
                            if (const VarDecl *var = global_decl(named->text);
                                var != nullptr && var->is_comptime) {
                                next = var->init.get();
                            }
                        }
                        if (next == nullptr || next == cursor) {
                            return false;
                        }
                        cursor = next;
                    }
                    return false;
                };
                // `([_]i32{40, 2}, 0)` and `(row, 0)` when `row` is that array. A lone array is not
                // this value. The element stays `[N]T`.
                const auto array_element = [&](const auto &self_array, const Expr &start, int depth) -> bool {
                    if (depth > 8) {
                        return false;
                    }
                    if (const auto *made = std::get_if<Expr::Init>(&start.data)) {
                        if (!made->array_literal) {
                            return false;
                        }
                        for (const auto &item : made->elements) {
                            if (item == nullptr || std::holds_alternative<Expr::Nullptr>(item->data)) {
                                return false;
                            }
                            if (const auto *inner = std::get_if<Expr::Init>(&item->data);
                                inner != nullptr && inner->array_literal) {
                                if (!self_array(self_array, *item, depth + 1)) {
                                    return false;
                                }
                                continue;
                            }
                            if (self(self, *item)) {
                                continue;
                            }
                            std::unordered_set<std::string> text_seen;
                            if (constant_string(*item, text_seen) || enumerator_value(*item)) {
                                continue;
                            }
                            if (!fold(*item)) {
                                return false;
                            }
                        }
                        return true;
                    }
                    const auto *named = std::get_if<Expr::Name>(&start.data);
                    if (named == nullptr || local_shadow(named->text)) {
                        return false;
                    }
                    if (const VarDecl *var = global_decl(named->text); var != nullptr && var->is_comptime) {
                        if (var->is_array && var->ptr_to_array) {
                            return false;
                        }
                        if (var->is_array && var->array_len < 0) {
                            return false;
                        }
                        if (var->is_array && var->array_len >= 0) {
                            return true;
                        }
                        if (var->init == nullptr) {
                            return false;
                        }
                        return self_array(self_array, *var->init, depth + 1);
                    }
                    const Expr *local = nullptr;
                    if (const Binding *binding = lookup(named->text)) {
                        local = binding->const_init;
                    }
                    if (local == nullptr || local == &start) {
                        return false;
                    }
                    return self_array(self_array, *local, depth + 1);
                };
                for (const auto &element : init->elements) {
                    if (element == nullptr) {
                        return false;
                    }
                    if (self(self, *element)) {
                        continue;
                    }
                    std::unordered_set<std::string> text_seen;
                    if (constant_string(*element, text_seen) || enumerator_value(*element)) {
                        continue;
                    }
                    if (address_element(*element)) {
                        continue;
                    }
                    if (array_element(array_element, *element, 0)) {
                        continue;
                    }
                    // `null` folds to 0 here. It is not this value. A bool literal folds to 0 or 1 and is.
                    if (std::holds_alternative<Expr::Nullptr>(element->data) || !fold(*element)) {
                        return false;
                    }
                }
                return true;
            }
            return file_constant_operand(value);
        }
        if (const auto *named = std::get_if<Expr::Name>(&value.data)) {
            if (local_shadow(named->text)) {
                return false;
            }
            if (file_function_name(named->text)) {
                return true;
            }
            const VarDecl *var = package_var(named->text);
            if (var == nullptr || !var->is_comptime) {
                return false;
            }
            if (var->has_const || var->is_enum || var->indirections != 0 || var->is_tuple) {
                return false;
            }
            if (var->is_struct || var->is_fn_ptr) {
                return true;
            }
            if (plain_float_name(var->type_name) && !var->is_array) {
                return true;
            }
            if (const TypedefDecl *alias = package_typedef_decl(var->type_name)) {
                if (!alias->is_enum) {
                    return true;
                }
            }
            if (var->is_array) {
                return false;
            }
            if (var->init != nullptr && (var->inferred || var->type_name.empty())) {
                if (!seen.insert(named->text).second) {
                    return false;
                }
                const bool inner = self(self, *var->init);
                seen.erase(named->text);
                return inner;
            }
            return false;
        }
        if (std::holds_alternative<Expr::Float>(value.data)) {
            return true;
        }
        if (const auto *cast = std::get_if<Expr::Cast>(&value.data)) {
            return cast->expr != nullptr && cast->indirections == 0 && file_constant_operand(*cast->expr);
        }
        if (const auto *unary = std::get_if<Expr::Unary>(&value.data)) {
            if (!unary->postfix && unary->op == TokenKind::Star && unary->expr != nullptr) {
                std::unordered_set<std::string> pointed;
                if (const Expr *target = comptime_pointee(*unary->expr, pointed)) {
                    return self(self, *target);
                }
                return false;
            }
            if (unary->postfix || unary->expr == nullptr ||
                (unary->op != TokenKind::Plus && unary->op != TokenKind::Minus)) {
                return false;
            }
            return self(self, *unary->expr);
        }
        if (std::holds_alternative<Expr::Binary>(value.data)) {
            const std::optional<FoldedNumber> number = fold_number(value);
            return number.has_value() && number->is_float;
        }
        if (const auto *selector = std::get_if<Expr::Selector>(&value.data)) {
            (void)selector;
            if (enumerator_value(value) || fold(value)) {
                return false;
            }
            return file_constant_operand(value);
        }
        return false;
    };
    return visit(visit, expr);
}

std::unique_ptr<Expr> CodeGen::comptime_substitute(const Expr &expr) const {
    std::unordered_set<std::string> seen;
    const Expr *cursor = &expr;
    while (cursor != nullptr) {
        if (const Expr *element = comptime_tuple_element(*cursor)) {
            cursor = element;
            continue;
        }
        if (const auto *star = std::get_if<Expr::Unary>(&cursor->data);
            star != nullptr && !star->postfix && star->op == TokenKind::Star && star->expr != nullptr) {
            std::unordered_set<std::string> pointed;
            const Expr *target = comptime_pointee(*star->expr, pointed);
            if (target == nullptr) {
                break;
            }
            cursor = target;
            continue;
        }
        const auto *named = std::get_if<Expr::Name>(&cursor->data);
        if (named == nullptr || !seen.insert(named->text).second || local_shadow(named->text)) {
            break;
        }
        const VarDecl *var = package_var(named->text);
        if (var == nullptr || !var->is_comptime || var->init == nullptr) {
            break;
        }
        cursor = var->init.get();
    }
    return clone_expr(*cursor);
}

} // namespace tlc

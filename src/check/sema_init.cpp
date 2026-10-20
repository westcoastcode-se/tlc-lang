// Definite assignment for local variables.
#include "check/check.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace tlc {

namespace {

// One byte per leaf. Past this, the array is a single leaf so checking a huge
// local does not allocate a flag for every element. That leaf means the whole
// array: an element store does not mark it, and an element read needs all of it.
constexpr int kInitLeafCap = 1 << 20;

bool sized_array(const Type &type) {
    return type.kind == TypeKind::Array && type.length >= 0 && type.element != nullptr;
}

} // namespace

int Checker::init_leaf_count(const Type &type) {
    std::unordered_set<int> seen;
    return init_leaf_count(type, seen);
}

int Checker::init_leaf_count(const Type &type, std::unordered_set<int> &seen) {
    switch (type.kind) {
    case TypeKind::Invalid:
    case TypeKind::Void:
    case TypeKind::Meta:
        return 0;
    case TypeKind::Array: {
        if (type.element == nullptr) {
            return 0;
        }
        // A slice is one header. The elements are not part of this local.
        if (type.length < 0) {
            return 1;
        }
        if (type.length == 0) {
            return 0;
        }
        const int elem = init_leaf_count(*type.element, seen);
        if (elem <= 0) {
            return 0;
        }
        const int64_t product = static_cast<int64_t>(type.length) * elem;
        if (product > kInitLeafCap) {
            return 1;
        }
        return static_cast<int>(product);
    }
    case TypeKind::Struct: {
        if (type.struct_id < 0 || type.struct_id >= static_cast<int>(structs_.size())) {
            return 0;
        }
        const auto cached = init_struct_leaves_.find(type.struct_id);
        if (cached != init_struct_leaves_.end()) {
            return cached->second;
        }
        if (!seen.insert(type.struct_id).second) {
            return 0;
        }
        const StructInfo &info = structs_[static_cast<std::size_t>(type.struct_id)];
        int total = 0;
        if (info.is_union) {
            total = info.fields.empty() ? 0 : 1;
        } else {
            for (const StructInfo::Field &field : info.fields) {
                total += init_leaf_count(field.type, seen);
            }
        }
        seen.erase(type.struct_id);
        if (info.complete) {
            init_struct_leaves_[type.struct_id] = total;
        }
        return total;
    }
    default:
        return 1;
    }
}

void Checker::init_open(Symbol &symbol) {
    if (symbol.init_id != 0) {
        return;
    }
    const int leaves = init_leaf_count(symbol.type);
    if (leaves <= 0) {
        return;
    }
    symbol.init_id = ++init_seq_;
    init_leaves_[symbol.init_id] = leaves;
    assign_.open[symbol.init_id] = std::vector<uint8_t>(static_cast<std::size_t>(leaves), 0);
    init_note_local(symbol.init_id, leaves);
}

void Checker::init_note_local(int id, int leaves) {
    for (FlowFrame &frame : flow_frames_) {
        if (frame.kind == FlowFrame::Kind::Switch) {
            frame.locals.push_back(FlowFrame::Local{id, leaves});
        }
    }
}

void Checker::init_mark_span(int id, int offset, int count) {
    if (id == 0 || count <= 0 || assign_.unreachable || quiet_ || init_quiet_) {
        return;
    }
    const auto found = assign_.open.find(id);
    if (found == assign_.open.end()) {
        return;
    }
    std::vector<uint8_t> &bits = found->second;
    if (offset < 0 || offset > static_cast<int>(bits.size())) {
        return;
    }
    const int end = std::min(offset + count, static_cast<int>(bits.size()));
    for (int i = offset; i < end; ++i) {
        bits[static_cast<std::size_t>(i)] = 1;
    }
    for (const uint8_t bit : bits) {
        if (bit == 0) {
            return;
        }
    }
    assign_.open.erase(found);
}

void Checker::init_mark_all(int id) {
    if (id == 0 || assign_.unreachable || quiet_ || init_quiet_) {
        return;
    }
    const auto leaves = init_leaves_.find(id);
    if (leaves == init_leaves_.end()) {
        assign_.open.erase(id);
        return;
    }
    init_mark_span(id, 0, leaves->second);
}

void Checker::init_require_span(int id, int offset, int count, SourceLoc loc) {
    if (id == 0 || count <= 0 || assign_.unreachable || quiet_ || init_quiet_) {
        return;
    }
    const auto found = assign_.open.find(id);
    if (found == assign_.open.end()) {
        return;
    }
    const std::vector<uint8_t> &bits = found->second;
    if (offset < 0 || offset >= static_cast<int>(bits.size())) {
        error(loc, "uninitialized");
        return;
    }
    const int end = std::min(offset + count, static_cast<int>(bits.size()));
    for (int i = offset; i < end; ++i) {
        if (bits[static_cast<std::size_t>(i)] == 0) {
            error(loc, "uninitialized");
            return;
        }
    }
}

Checker::InitTouch Checker::init_resolve(Type type, int offset, int count, const std::vector<InitStep> &steps,
                                         bool storing) {
    const bool keep = init_keep_array_ > 0;
    const bool address = init_use_ == InitUse::Address;
    // A method names the receiver. The copy is required later, only for `self this`.
    const bool method = init_use_ == InitUse::Method;
    const bool designate = address || method;
    bool opaque = false;
    for (const InitStep &step : steps) {
        // A pointer or a slice is one value. The pointee is not storage in this local.
        if (type.kind == TypeKind::Pointer || is_slice(type)) {
            if (storing) {
                return {};
            }
            InitTouch touch;
            touch.read = true;
            touch.offset = offset;
            touch.count = count;
            return touch;
        }
        if (step.kind == InitStep::Kind::Range && sized_array(type)) {
            return {};
        }
        if (step.kind == InitStep::Kind::Field && step.field == "len" && sized_array(type)) {
            return {};
        }
        if (step.kind == InitStep::Kind::Field) {
            if (type.kind != TypeKind::Struct || type.struct_id < 0 ||
                type.struct_id >= static_cast<int>(structs_.size())) {
                break;
            }
            const StructInfo &info = structs_[static_cast<std::size_t>(type.struct_id)];
            const StructInfo::Field *found = nullptr;
            int at = offset;
            for (const StructInfo::Field &field : info.fields) {
                const int leaves = init_leaf_count(field.type);
                if (field.name == step.field) {
                    found = &field;
                    if (!info.is_union && !opaque) {
                        offset = at;
                        count = leaves;
                    }
                    break;
                }
                if (!info.is_union && !opaque) {
                    at += leaves;
                }
            }
            if (found == nullptr) {
                break;
            }
            type = found->type;
            if (info.is_union) {
                opaque = true;
            }
            continue;
        }
        if (!sized_array(type)) {
            if (storing) {
                return {};
            }
            InitTouch touch;
            touch.read = true;
            touch.offset = offset;
            touch.count = count;
            return touch;
        }
        const int length = type.length;
        const int elem = init_leaf_count(*type.element);
        const bool collapsed = elem > 0 && static_cast<int64_t>(length) * elem > kInitLeafCap;
        const bool oob = step.known && (step.index < 0 || step.index >= length);
        const bool dynamic = !step.known || oob;
        // A capped array is one leaf for every element. Writing one element does not
        // initialize the others, and reading one needs the whole array.
        if (collapsed && !opaque) {
            if (storing || (designate && !keep)) {
                return {};
            }
            InitTouch touch;
            touch.read = true;
            touch.offset = offset;
            touch.count = count;
            return touch;
        }
        if (opaque) {
            if (dynamic) {
                // A runtime write does not pick an element. An opaque value is one leaf, so a
                // runtime write sets it. An index past the constant length sets nothing.
                if (storing) {
                    if (oob) {
                        return {};
                    }
                    InitTouch touch;
                    touch.mark = true;
                    touch.offset = offset;
                    touch.count = count;
                    return touch;
                }
                if (designate && !keep) {
                    return {};
                }
                InitTouch touch;
                touch.read = true;
                touch.offset = offset;
                touch.count = count;
                return touch;
            }
            type = *type.element;
            continue;
        }
        if (dynamic) {
            // A runtime store assigns no element. A runtime read needs every element.
            if (storing || (designate && !keep)) {
                return {};
            }
            InitTouch touch;
            touch.read = true;
            touch.offset = offset;
            touch.count = count;
            return touch;
        }
        type = *type.element;
        offset += step.index * std::max(elem, 0);
        count = elem;
    }
    if (storing) {
        InitTouch touch;
        touch.mark = true;
        touch.offset = offset;
        touch.count = count;
        return touch;
    }
    // `self *this` does not load a struct. Loading a pointer still reads that pointer.
    if (address || (method && type.kind != TypeKind::Pointer)) {
        return {};
    }
    // A sized array used as a pointer is not a read of its elements. A copy is.
    // The span stays so `*[N]T` can initialize that array and not the object around it.
    if (sized_array(type) && !keep) {
        InitTouch touch;
        touch.array_addr = true;
        touch.offset = offset;
        touch.count = count;
        return touch;
    }
    InitTouch touch;
    touch.read = true;
    touch.offset = offset;
    touch.count = count;
    return touch;
}

bool Checker::init_pointee_steps(const Symbol &symbol, std::vector<InitStep> steps) const {
    if (symbol.pointee_id == 0 || symbol.type.kind != TypeKind::Pointer || symbol.type.pointee == nullptr) {
        return false;
    }
    if (steps.empty()) {
        return false;
    }
    if (steps.front().kind == InitStep::Kind::Deref) {
        return true;
    }
    return steps.front().kind == InitStep::Kind::Field || steps.front().kind == InitStep::Kind::Index;
}

void Checker::init_touch(const Symbol &symbol, SourceLoc loc) {
    if (assign_.unreachable || quiet_ || init_quiet_) {
        return;
    }
    std::vector<InitStep> steps(init_stack_.rbegin(), init_stack_.rend());
    if (init_pointee_steps(symbol, steps)) {
        if (steps.front().kind == InitStep::Kind::Deref) {
            steps.erase(steps.begin());
        }
        const auto leaves = init_leaves_.find(symbol.pointee_id);
        if (leaves == init_leaves_.end()) {
            return;
        }
        const InitTouch touch = init_resolve(*symbol.type.pointee, 0, leaves->second, steps, false);
        if (touch.read) {
            init_require_span(symbol.pointee_id, touch.offset, touch.count, loc);
        }
        return;
    }
    if (symbol.init_id == 0) {
        return;
    }
    const auto leaves = init_leaves_.find(symbol.init_id);
    if (leaves == init_leaves_.end()) {
        return;
    }
    const InitTouch touch = init_resolve(symbol.type, 0, leaves->second, steps, false);
    if (touch.read) {
        init_require_span(symbol.init_id, touch.offset, touch.count, loc);
    }
}

bool Checker::init_collect(const Expr &expr, std::vector<InitStep> &steps) {
    if (const auto *unary = std::get_if<Expr::Unary>(&expr.data)) {
        if (unary->op == TokenKind::KwUnsafe && unary->expr != nullptr) {
            return init_collect(*unary->expr, steps);
        }
        if (unary->op == TokenKind::Star && unary->expr != nullptr && init_collect(*unary->expr, steps)) {
            InitStep step;
            step.kind = InitStep::Kind::Deref;
            steps.push_back(std::move(step));
            return true;
        }
        return false;
    }
    if (const auto *index = std::get_if<Expr::Index>(&expr.data)) {
        if (index->is_range || index->base == nullptr) {
            return false;
        }
        if (!init_collect(*index->base, steps)) {
            return false;
        }
        InitStep step;
        step.kind = InitStep::Kind::Index;
        if (index->index != nullptr) {
            if (const std::optional<int> folded = fold_int(*index->index)) {
                step.known = true;
                step.index = *folded;
            }
        }
        steps.push_back(std::move(step));
        return true;
    }
    if (const auto *selector = std::get_if<Expr::Selector>(&expr.data)) {
        if (selector->base == nullptr || !init_collect(*selector->base, steps)) {
            return false;
        }
        InitStep step;
        step.kind = InitStep::Kind::Field;
        step.field = selector->field;
        steps.push_back(std::move(step));
        return true;
    }
    return std::holds_alternative<Expr::Name>(expr.data);
}

void Checker::init_replace(const Expr &lhs, const Expr &rhs) {
    std::vector<InitStep> steps;
    if (!init_collect(lhs, steps)) {
        init_store(lhs);
        return;
    }
    const Expr *walk = &lhs;
    const Expr::Name *named = nullptr;
    while (walk != nullptr && named == nullptr) {
        if (const auto *unary = std::get_if<Expr::Unary>(&walk->data)) {
            if ((unary->op == TokenKind::KwUnsafe || unary->op == TokenKind::Star) && unary->expr != nullptr) {
                walk = unary->expr.get();
                continue;
            }
            break;
        }
        if (const auto *index = std::get_if<Expr::Index>(&walk->data)) {
            walk = index->base.get();
            continue;
        }
        if (const auto *selector = std::get_if<Expr::Selector>(&walk->data)) {
            walk = selector->base.get();
            continue;
        }
        named = std::get_if<Expr::Name>(&walk->data);
        break;
    }
    if (named == nullptr) {
        return;
    }
    Symbol *symbol = symbol_slot(named->text);
    if (symbol == nullptr) {
        return;
    }
    int id = symbol->init_id;
    Type focused = symbol->type;
    if (init_pointee_steps(*symbol, steps)) {
        id = symbol->pointee_id;
        if (symbol->type.pointee != nullptr) {
            focused = *symbol->type.pointee;
        }
        if (!steps.empty() && steps.front().kind == InitStep::Kind::Deref) {
            steps.erase(steps.begin());
        }
    }
    const auto total = init_leaves_.find(id);
    if (id == 0 || total == init_leaves_.end()) {
        return;
    }
    int offset = 0;
    for (const InitStep &step : steps) {
        if (step.kind == InitStep::Kind::Field && focused.kind == TypeKind::Struct) {
            const StructInfo &info = structs_[static_cast<std::size_t>(focused.struct_id)];
            int at = 0;
            bool found = false;
            for (const StructInfo::Field &field : info.fields) {
                const int count = init_leaf_count(field.type);
                if (field.name == step.field) {
                    focused = field.type;
                    offset += at;
                    found = true;
                    break;
                }
                if (!info.is_union) {
                    at += count;
                }
            }
            if (!found) {
                return;
            }
            continue;
        }
        if (step.kind == InitStep::Kind::Index && focused.kind == TypeKind::Array && focused.element != nullptr &&
            step.known) {
            const int elem = init_leaf_count(*focused.element);
            offset += step.index * std::max(elem, 0);
            focused = *focused.element;
            continue;
        }
        return;
    }
    const int count = init_leaf_count(focused);
    std::vector<uint8_t> bits(static_cast<std::size_t>(total->second), 1);
    const auto open = assign_.open.find(id);
    if (open != assign_.open.end()) {
        bits = open->second;
    }
    std::vector<uint8_t> cover(static_cast<std::size_t>(std::max(count, 0)), 0);
    init_fill(focused, &rhs, cover, 0);
    for (int i = 0; i < count; ++i) {
        const int at = offset + i;
        if (at >= 0 && at < static_cast<int>(bits.size())) {
            bits[static_cast<std::size_t>(at)] = i < static_cast<int>(cover.size()) ? cover[static_cast<std::size_t>(i)] : 0;
        }
    }
    bool all = true;
    for (const uint8_t bit : bits) {
        if (bit == 0) {
            all = false;
            break;
        }
    }
    if (all) {
        assign_.open.erase(id);
    } else {
        assign_.open[id] = std::move(bits);
    }
}

void Checker::init_store(const Expr &lhs) {
    if (assign_.unreachable || quiet_ || init_quiet_) {
        return;
    }
    if (init_cover_rhs_ != nullptr) {
        const Expr *rhs = init_cover_rhs_;
        init_cover_rhs_ = nullptr;
        init_replace(lhs, *rhs);
        return;
    }
    std::vector<InitStep> steps;
    if (!init_collect(lhs, steps)) {
        return;
    }
    const Expr *walk = &lhs;
    const Expr::Name *named = nullptr;
    while (walk != nullptr && named == nullptr) {
        if (const auto *unary = std::get_if<Expr::Unary>(&walk->data)) {
            if ((unary->op == TokenKind::KwUnsafe || unary->op == TokenKind::Star) && unary->expr != nullptr) {
                walk = unary->expr.get();
                continue;
            }
            break;
        }
        if (const auto *index = std::get_if<Expr::Index>(&walk->data)) {
            walk = index->base.get();
            continue;
        }
        if (const auto *selector = std::get_if<Expr::Selector>(&walk->data)) {
            walk = selector->base.get();
            continue;
        }
        named = std::get_if<Expr::Name>(&walk->data);
        break;
    }
    if (named == nullptr) {
        return;
    }
    const Symbol *symbol = lookup(named->text);
    if (symbol == nullptr) {
        return;
    }
    if (init_pointee_steps(*symbol, steps)) {
        if (steps.front().kind == InitStep::Kind::Deref) {
            steps.erase(steps.begin());
        }
        const auto leaves = init_leaves_.find(symbol->pointee_id);
        if (leaves == init_leaves_.end()) {
            return;
        }
        const InitTouch touch = init_resolve(*symbol->type.pointee, 0, leaves->second, steps, true);
        if (touch.mark) {
            init_mark_span(symbol->pointee_id, touch.offset, touch.count);
        }
        return;
    }
    if (symbol->init_id == 0) {
        return;
    }
    const auto leaves = init_leaves_.find(symbol->init_id);
    if (leaves == init_leaves_.end()) {
        return;
    }
    const InitTouch touch = init_resolve(symbol->type, 0, leaves->second, steps, true);
    if (touch.mark) {
        init_mark_span(symbol->init_id, touch.offset, touch.count);
    }
}

void Checker::init_store_if(const Expr &lhs, std::size_t before) {
    if (diags_.size() == before) {
        init_store(lhs);
    }
}

void Checker::init_require_receiver(const Expr &expr) {
    if (assign_.unreachable || quiet_ || init_quiet_) {
        return;
    }
    std::vector<InitStep> steps;
    if (!init_collect(expr, steps)) {
        return;
    }
    const Expr *walk = &expr;
    const Expr::Name *named = nullptr;
    while (walk != nullptr && named == nullptr) {
        if (const auto *unary = std::get_if<Expr::Unary>(&walk->data)) {
            if (unary->op == TokenKind::KwUnsafe && unary->expr != nullptr) {
                walk = unary->expr.get();
                continue;
            }
            break;
        }
        if (const auto *index = std::get_if<Expr::Index>(&walk->data)) {
            walk = index->base.get();
            continue;
        }
        if (const auto *selector = std::get_if<Expr::Selector>(&walk->data)) {
            walk = selector->base.get();
            continue;
        }
        named = std::get_if<Expr::Name>(&walk->data);
        break;
    }
    if (named == nullptr) {
        return;
    }
    const Symbol *symbol = lookup(named->text);
    if (symbol == nullptr || symbol->init_id == 0) {
        return;
    }
    const auto leaves = init_leaves_.find(symbol->init_id);
    if (leaves == init_leaves_.end()) {
        return;
    }
    // The call already loaded a pointer. Do not report that load again.
    {
        InitUseGuard method(*this, InitUse::Method);
        if (init_resolve(symbol->type, 0, leaves->second, steps, false).read) {
            return;
        }
    }
    InitUseGuard read(*this, InitUse::Read);
    const InitTouch touch = init_resolve(symbol->type, 0, leaves->second, steps, false);
    if (touch.read) {
        init_require_span(symbol->init_id, touch.offset, touch.count, expr_loc(expr));
    }
}

Checker::AssignState Checker::init_merge(const AssignState &lhs, const AssignState &rhs) const {
    if (lhs.unreachable) {
        return rhs;
    }
    if (rhs.unreachable) {
        return lhs;
    }
    AssignState out;
    auto consider = [&](int id, const std::vector<uint8_t> *left, const std::vector<uint8_t> *right) {
        if (left == nullptr) {
            out.open[id] = *right;
            return;
        }
        if (right == nullptr) {
            out.open[id] = *left;
            return;
        }
        const std::size_t width = std::max(left->size(), right->size());
        std::vector<uint8_t> bits(width, 0);
        bool all = width > 0;
        for (std::size_t i = 0; i < width; ++i) {
            const uint8_t bit = (i < left->size() ? (*left)[i] : 0) & (i < right->size() ? (*right)[i] : 0);
            bits[i] = bit;
            if (bit == 0) {
                all = false;
            }
        }
        if (!all) {
            out.open[id] = std::move(bits);
        }
    };
    for (const auto &[id, bits] : lhs.open) {
        const auto other = rhs.open.find(id);
        consider(id, &bits, other == rhs.open.end() ? nullptr : &other->second);
    }
    for (const auto &[id, bits] : rhs.open) {
        if (!lhs.open.contains(id)) {
            consider(id, nullptr, &bits);
        }
    }
    return out;
}

Checker::AssignState Checker::init_loop_exit(const std::vector<AssignState> &breaks, const AssignState &fail,
                                             bool can_fail) const {
    AssignState out;
    out.unreachable = true;
    for (const AssignState &stopped : breaks) {
        out = init_merge(out, stopped);
    }
    if (can_fail) {
        out = init_merge(out, fail);
    }
    return out;
}

bool Checker::init_has_default(const Stmt *stmt) const {
    if (stmt == nullptr) {
        return false;
    }
    return std::visit(
        [&](const auto &node) -> bool {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Stmt::Case>) {
                return node.is_default || init_has_default(node.stmt.get());
            } else if constexpr (std::is_same_v<T, Stmt::Switch> || std::is_same_v<T, Stmt::Func>) {
                return false;
            } else if constexpr (std::is_same_v<T, Stmt::Block>) {
                for (const auto &child : node.statements) {
                    if (init_has_default(child.get())) {
                        return true;
                    }
                }
                return false;
            } else if constexpr (std::is_same_v<T, Stmt::If>) {
                return init_has_default(node.then_arm.get()) || init_has_default(node.else_arm.get());
            } else if constexpr (std::is_same_v<T, Stmt::While> || std::is_same_v<T, Stmt::DoWhile>) {
                return init_has_default(node.body.get());
            } else if constexpr (std::is_same_v<T, Stmt::For>) {
                return init_has_default(node.init.get()) || init_has_default(node.body.get());
            } else {
                return false;
            }
        },
        stmt->data);
}

void Checker::init_break() {
    if (flow_frames_.empty() || assign_.unreachable) {
        return;
    }
    flow_frames_.back().breaks.push_back(assign_);
    assign_.unreachable = true;
}

void Checker::init_continue() {
    if (assign_.unreachable) {
        return;
    }
    for (auto it = flow_frames_.rbegin(); it != flow_frames_.rend(); ++it) {
        if (it->kind != FlowFrame::Kind::Loop) {
            continue;
        }
        it->continues.push_back(assign_);
        assign_.unreachable = true;
        return;
    }
}

Checker::FlowFrame *Checker::init_switch_frame() {
    for (auto it = flow_frames_.rbegin(); it != flow_frames_.rend(); ++it) {
        if (it->kind == FlowFrame::Kind::Switch) {
            return &(*it);
        }
    }
    return nullptr;
}

void Checker::init_suspend(InitSuspend &saved) {
    saved.assign = assign_;
    saved.frames = std::move(flow_frames_);
    flow_frames_.clear();
    saved.stack = std::move(init_stack_);
    init_stack_.clear();
    saved.use = init_use_;
    init_use_ = InitUse::Read;
    saved.quiet = init_quiet_;
    init_quiet_ = false;
    saved.keep = init_keep_array_;
    init_keep_array_ = 0;
    saved.split = cond_split_;
    cond_split_ = false;
    saved.cond_true = cond_true_;
    saved.cond_false = cond_false_;
    cond_true_ = {};
    cond_false_ = {};
    // The inner body sees the outer locals as they are at the definition. Its own stores do not
    // escape: init_resume puts this snapshot back.
    assign_.unreachable = false;
}

void Checker::init_resume(InitSuspend &saved) {
    assign_ = std::move(saved.assign);
    flow_frames_ = std::move(saved.frames);
    init_stack_ = std::move(saved.stack);
    init_use_ = saved.use;
    init_quiet_ = saved.quiet;
    init_keep_array_ = saved.keep;
    cond_split_ = saved.split;
    cond_true_ = std::move(saved.cond_true);
    cond_false_ = std::move(saved.cond_false);
}

Checker::InitUseGuard::InitUseGuard(Checker &self, InitUse use) : self_(&self), saved_(self.init_use_) {
    self.init_use_ = use;
}

Checker::InitUseGuard::~InitUseGuard() { self_->init_use_ = saved_; }

Checker::InitPush::InitPush(Checker &self, InitStep step) : self_(&self) { self.init_stack_.push_back(std::move(step)); }

Checker::InitPush::~InitPush() { self_->init_stack_.pop_back(); }

Checker::InitKeep::InitKeep(Checker &self) : self_(&self) { ++self.init_keep_array_; }

Checker::InitKeep::~InitKeep() { --self_->init_keep_array_; }

Checker::InitQuiet::InitQuiet(Checker &self) : self_(&self), saved_(self.init_quiet_) { self.init_quiet_ = true; }

Checker::InitQuiet::~InitQuiet() { self_->init_quiet_ = saved_; }

Checker::UnreachableAfter::UnreachableAfter(Checker &self) : self_(&self) {}

Checker::UnreachableAfter::~UnreachableAfter() { self_->assign_.unreachable = true; }

Checker::InitSuspend::InitSuspend(Checker &self) : self_(&self) { self.init_suspend(*this); }

Checker::InitSuspend::~InitSuspend() { self_->init_resume(*this); }

Symbol *Checker::symbol_slot(const std::string &name) {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        const auto found = it->symbols.find(name);
        if (found != it->symbols.end()) {
            return &found->second;
        }
    }
    return nullptr;
}

void Checker::init_set_span(std::vector<uint8_t> &bits, int offset, int count) {
    if (offset < 0 || count <= 0) {
        return;
    }
    const int end = std::min(offset + count, static_cast<int>(bits.size()));
    for (int i = offset; i < end; ++i) {
        bits[static_cast<std::size_t>(i)] = 1;
    }
}

void Checker::init_fill(const Type &type, const Expr *init, std::vector<uint8_t> &bits, int offset) {
    const int leaves = init_leaf_count(type);
    if (leaves <= 0) {
        return;
    }
    const Expr::Init *brace = nullptr;
    if (init != nullptr) {
        brace = std::get_if<Expr::Init>(&init->data);
        if (brace != nullptr && brace->is_tuple) {
            brace = nullptr;
        }
    }
    if (init != nullptr && brace == nullptr) {
        init_set_span(bits, offset, leaves);
        return;
    }
    // A slice header is one value. The elements are not leaves of this local.
    if (type.kind == TypeKind::Array && type.length < 0) {
        if (init != nullptr) {
            init_set_span(bits, offset, leaves);
        }
        return;
    }
    init_fill_brace(type, brace, bits, offset);
}

void Checker::init_fill_brace(const Type &type, const Expr::Init *brace, std::vector<uint8_t> &bits, int offset) {
    const int leaves = init_leaf_count(type);
    if (leaves <= 0) {
        return;
    }
    if (type.kind == TypeKind::Array && type.length >= 0 && type.element != nullptr) {
        const int elem = init_leaf_count(*type.element);
        const int listed = brace == nullptr ? 0 : static_cast<int>(brace->elements.size());
        for (int index = 0; index < type.length; ++index) {
            const int at = offset + index * std::max(elem, 0);
            if (brace != nullptr && index < listed && brace->elements[static_cast<std::size_t>(index)] != nullptr) {
                init_fill(*type.element, brace->elements[static_cast<std::size_t>(index)].get(), bits, at);
            } else if (brace != nullptr && brace->zero_rest && index >= listed) {
                init_set_span(bits, at, elem);
            } else if (brace == nullptr) {
                init_fill(*type.element, nullptr, bits, at);
            }
        }
        return;
    }
    if (type.kind == TypeKind::Struct && type.struct_id >= 0 &&
        type.struct_id < static_cast<int>(structs_.size())) {
        const StructInfo &info = structs_[static_cast<std::size_t>(type.struct_id)];
        if (info.is_union) {
            if (brace != nullptr) {
                init_set_span(bits, offset, leaves);
            }
            return;
        }
        int at = offset;
        for (const StructInfo::Field &field : info.fields) {
            const int count = init_leaf_count(field.type);
            const Expr *named = nullptr;
            if (brace != nullptr) {
                for (const Expr::FieldInit &item : brace->fields) {
                    if (item.field == field.name) {
                        named = item.value.get();
                        break;
                    }
                }
            }
            if (named != nullptr) {
                init_fill(field.type, named, bits, at);
            } else if (brace != nullptr && brace->zero_rest) {
                init_set_span(bits, at, count);
            } else if (field.has_init) {
                init_set_span(bits, at, count);
            } else {
                init_fill(field.type, nullptr, bits, at);
            }
            at += count;
        }
        return;
    }
}

void Checker::init_cover(Symbol &symbol, const Expr *init) {
    if (symbol.init_id == 0) {
        return;
    }
    const auto leaves = init_leaves_.find(symbol.init_id);
    if (leaves == init_leaves_.end() || leaves->second <= 0) {
        return;
    }
    std::vector<uint8_t> bits(static_cast<std::size_t>(leaves->second), 0);
    init_fill(symbol.type, init, bits, 0);
    bool all = true;
    for (const uint8_t bit : bits) {
        if (bit == 0) {
            all = false;
            break;
        }
    }
    if (all) {
        assign_.open.erase(symbol.init_id);
        return;
    }
    assign_.open[symbol.init_id] = std::move(bits);
}

void Checker::init_track_pointee(Symbol &symbol, const Type &object, const Expr *init, bool fresh) {
    const int leaves = init_leaf_count(object);
    if (leaves <= 0) {
        return;
    }
    const int id = ++init_seq_;
    symbol.pointee_id = id;
    init_leaves_[id] = leaves;
    init_note_local(id, leaves);
    if (fresh) {
        assign_.open[id] = std::vector<uint8_t>(static_cast<std::size_t>(leaves), 0);
        return;
    }
    std::vector<uint8_t> bits(static_cast<std::size_t>(leaves), 0);
    init_fill(object, init, bits, 0);
    bool all = true;
    for (const uint8_t bit : bits) {
        if (bit == 0) {
            all = false;
            break;
        }
    }
    if (!all) {
        assign_.open[id] = std::move(bits);
    }
}

void Checker::init_address_span(const Expr &expr, bool mark, SourceLoc loc, bool array_pointer) {
    if (assign_.unreachable || quiet_ || init_quiet_) {
        return;
    }
    const Expr *cursor = &expr;
    if (const auto *unary = std::get_if<Expr::Unary>(&expr.data);
        unary != nullptr && unary->op == TokenKind::Amp && unary->expr != nullptr) {
        cursor = unary->expr.get();
    }
    std::vector<InitStep> steps;
    if (!init_collect(*cursor, steps)) {
        return;
    }
    const Expr *walk = cursor;
    const Expr::Name *named = nullptr;
    while (walk != nullptr && named == nullptr) {
        if (const auto *unary = std::get_if<Expr::Unary>(&walk->data)) {
            if ((unary->op == TokenKind::KwUnsafe || unary->op == TokenKind::Star) && unary->expr != nullptr) {
                walk = unary->expr.get();
                continue;
            }
            break;
        }
        if (const auto *index = std::get_if<Expr::Index>(&walk->data)) {
            walk = index->base.get();
            continue;
        }
        if (const auto *selector = std::get_if<Expr::Selector>(&walk->data)) {
            walk = selector->base.get();
            continue;
        }
        named = std::get_if<Expr::Name>(&walk->data);
        break;
    }
    if (named == nullptr) {
        return;
    }
    const Symbol *symbol = lookup(named->text);
    if (symbol == nullptr) {
        return;
    }
    int id = symbol->init_id;
    Type type = symbol->type;
    int total = 0;
    if (symbol->pointee_id != 0 && symbol->type.pointee != nullptr &&
        (steps.empty() || init_pointee_steps(*symbol, steps))) {
        id = symbol->pointee_id;
        type = *symbol->type.pointee;
        if (!steps.empty() && steps.front().kind == InitStep::Kind::Deref) {
            steps.erase(steps.begin());
        }
    }
    const auto leaves = init_leaves_.find(id);
    if (id == 0 || leaves == init_leaves_.end()) {
        return;
    }
    total = leaves->second;
    const InitUse saved_use = init_use_;
    init_use_ = InitUse::Read;
    const InitTouch touch = init_resolve(type, 0, total, steps, false);
    init_use_ = saved_use;
    // `*[N]T` stores into that array. `*T` and a parameter `[N]T` point at one element.
    if (touch.array_addr && (!array_pointer || touch.count <= 0)) {
        return;
    }
    const int offset = touch.array_addr || touch.read || touch.mark ? touch.offset : 0;
    const int count = touch.array_addr || touch.read || touch.mark ? touch.count : total;
    if (mark) {
        std::vector<uint8_t> bits(static_cast<std::size_t>(total), 1);
        const auto found = assign_.open.find(id);
        if (found != assign_.open.end()) {
            bits = found->second;
        }
        init_set_span(bits, offset, count);
        bool all = true;
        for (const uint8_t bit : bits) {
            if (bit == 0) {
                all = false;
                break;
            }
        }
        if (all) {
            assign_.open.erase(id);
        } else {
            assign_.open[id] = std::move(bits);
        }
        return;
    }
    init_require_span(id, offset, count, loc);
}

} // namespace tlc

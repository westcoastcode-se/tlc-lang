// Definite assignment for local variables.
#include "check.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace tlc {

namespace {

constexpr int kInitLeafCap = 4096;

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
        // One bit stands for the whole array. An element access touches that bit.
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
        if (collapsed || opaque) {
            opaque = true;
            if (dynamic) {
                // A runtime write does not pick an element. An opaque array is one bit, so a
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
    if (sized_array(type) && !keep) {
        return {};
    }
    InitTouch touch;
    touch.read = true;
    touch.offset = offset;
    touch.count = count;
    return touch;
}

void Checker::init_touch(const Symbol &symbol, SourceLoc loc) {
    if (symbol.init_id == 0 || assign_.unreachable || quiet_ || init_quiet_) {
        return;
    }
    const auto leaves = init_leaves_.find(symbol.init_id);
    if (leaves == init_leaves_.end()) {
        return;
    }
    std::vector<InitStep> steps(init_stack_.rbegin(), init_stack_.rend());
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

void Checker::init_store(const Expr &lhs) {
    if (assign_.unreachable || quiet_ || init_quiet_) {
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

} // namespace tlc

#pragma once

#include "gen/gen.hpp"

#include "llvm/IR/Function.h"

namespace tlc {

inline std::string global_key(const VarDecl &var) {
    if (var.owner.empty()) {
        return var.name;
    }
    return var.owner + "." + var.name;
}

// `{...}` and `[_]T{...}` fill an array. A struct value and a tuple do not.
inline bool array_brace(const Expr::Init &init) { return !init.is_tuple && init.fields.empty(); }

// A trait argument that is a function pointer. -1 still means "not a trait parameter".
inline constexpr int kFnSpec = -2;

inline bool trait_fn_argument(const TypedValue &arg) {
    if (arg.value != nullptr && llvm::isa<llvm::Function>(arg.value)) {
        return true;
    }
    return arg.fn_ty != nullptr && arg.struct_id < 0;
}

// A local `[N]...T` decays by dropping the outer array. The pointer already addresses element 0.
// `*p` and `p[i]` on `*[N]...T` use that value, so the next index peels one more shell.
inline TypedValue decay_outer_array(TypedValue array) {
    if (array.shells.empty() || array.shells.front().kind != TypeShell::Kind::Array) {
        return array;
    }
    array.array_len = array.shells.front().length;
    array.shells.erase(array.shells.begin());
    array.ptr_to_array = false;
    array.ptr_array_len = 0;
    array.depth += 1;
    array.row_decay = false;
    array.is_slice = !array.shells.empty() && array.shells.front().kind == TypeShell::Kind::Slice;
    array.inner_array_len = 0;
    array.elem_slice = false;
    if (!array.shells.empty() && array.shells.front().kind == TypeShell::Kind::Array) {
        array.inner_array_len = array.shells.front().length;
    }
    if (array.shells.size() >= 2 && array.shells[1].kind == TypeShell::Kind::Slice) {
        array.elem_slice = true;
    }
    return array;
}

inline bool nested_array_element(const std::vector<TypeShell> &shells, std::size_t array_at) {
    const std::size_t next = array_at + 1;
    if (next >= shells.size()) {
        return false;
    }
    const TypeShell::Kind kind = shells[next].kind;
    return kind == TypeShell::Kind::Array || kind == TypeShell::Kind::Slice;
}

} // namespace tlc

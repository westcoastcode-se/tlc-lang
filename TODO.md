# Left from generics and compile-time

What the current generics pass does not do. Diagnostics stay the short English phrases already used (`not supported yet`, `type mismatch`).

## `const if`

Compile-time branching was left out on purpose. `const if` is not a form yet.

## Still `not supported yet`

- A generic used as a value: `var f = id`.
- `extern` on a generic, a generic with no body, and a variadic generic.
- A type-parameter list on a method: `func Vec::get[T type]`. The method uses the struct's parameters.
- Type parameters on an alias, an enum, a trait, or a union. Only `type Name[T type] struct` takes the list.
- A qualified type application: `pkg.Vec[int]`.
- A qualified trait constraint: `T pkg.Shape`.
- A type argument that is `sizeof(...)`.
- An array type parameter under a pointer, or a pointer to an array, as in `*T` when `T` is `[4]int`.
- `sizeof(T)` when `T` is an array.
- `const` inside a function. File scope is `const name = expr`. A local `const` is still the C qualifier.
- A non-positive compile-time length, including `const N = 0` used as `[N]int`.
- Returning an array from anything other than a local array name. `return out` works for `[N]T`. Another return expression does not.

## Inference

- A compile-time integer is written at the call: `repeat[4](7)`. It is not inferred from a value argument.
- A type is inferred from `T` and from `*T`. `Vec[T]` is inferred when the argument is already that specialization.
- There is no partial specialization and no SFINAE.

## Parsing

- `name[args](` is a generic call. A call of an indexed value is `(ops[i])()`.
- An array length may be a name, `[N]int`. The name has to be a file-scope `const` already folded. A length that needs `sizeof` of a struct that is not complete yet does not fold.

## Constant evaluation

`assert`, `const`, and a compile-time integer parameter share `fold_int`. That folds integers, names of those constants, and `sizeof`. A general constant evaluator is not in place.

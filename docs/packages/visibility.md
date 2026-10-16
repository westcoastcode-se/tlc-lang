# Visibility

[Index](../README.md) · [Previous: Libraries](library.md) · [Next: extern](extern.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

## Names inside a package

Every file in a package can use a name declared in another file of that directory. The same name in two files is `redefinition`. A method is written in the file that defines its struct or enum. The other files of the package can call it.

## Visibility

Only a file-scope `export` is published to importers. A name that was not exported is reported as `name is not exported`.

```c
package math;

func secret() i32 {
    return 1;
}

export func add(a i32, b i32) i32 {
    return a + b + secret();
}
```

`secret` can be called from any file in the package. An importer sees `add`.

`export` applies to functions, variables, constants, variables on a type, and `type` declarations. `export extern(abi="c")` is allowed on a function, and the symbol keeps its C name. `export` together with `extern` on a variable is written `export extern`.

```c
package math;

export type Entity struct {
    id i32;
};

export var Entity::next i32 = 40;
export const bias = 2;
```

```c
import "math";

func main() i32 {
    math.Entity.next = math.Entity.next + math.bias;
    return math.Entity.next;
}
```

The program returns 42. `math.bias` is the exported constant. `math.Entity.next` is the exported variable on `Entity`. A constant or variable without `export` is reported as `name is not exported`. Assigning `math.bias` is reported as `not an lvalue`. `math.Entity.make()` is reported as `type mismatch` when `make` is a function.

`export id i32` publishes a member. Another package reads and assigns `value.id`. A member without `export` is reported as `name is not exported`. `var` inside the struct is reported as `not supported yet`. `export var bonus = 4` and `export spare := 2` publish inferred globals. Another package assigns them. `sizeof` still counts only the fields.

```c
package math;

export type Entity struct {
    export id i32;
};

export var Entity::next i32 = 32;

export func width() i32 {
    return sizeof(Entity);
}

export var bonus = 4;
export spare := 2;
```

```c
import "math";
import { spare } from "math";

func main() i32 {
    var e math.Entity;
    e.id = 4;
    math.Entity.next = math.Entity.next + math.bonus + spare;
    return math.Entity.next + math.width();
}
```

The program returns 42. `math.width()` is `sizeof(Entity)`, which is 4. `e.id` is the exported member. `math.Entity.next` started at 32.

`const Entity::next` is a constant on the type, written beside the struct. `export const` publishes it, including `export const Box[T]::limit` on a generic struct. Another package reads `pkg.Entity.next`, `pkg.Box[i32].limit`, and `pkg.Buf[4, i32].count`. Assigning that constant is reported as `not an lvalue`. A constant without `export` is reported as `name is not exported`. An unexported generic struct is reported as `Box is not exported`. A `const` inside the struct is reported as `not supported yet`.

```c
package math;

export type Entity struct {
    id i32;
};

export const Entity::next = 15;
export const Entity::bias = 1.0f;

export func width() i32 {
    return sizeof(Entity);
}

export type Box[T type] struct {
    value T;
};

export const Box[T]::limit = 6;

export const spare = 2;
```

```c
import "math";
import { spare } from "math";

type Buf[N i32, T type] struct {
    value T;
};

const Buf[N, T]::count = N;

func main() i32 {
    var e math.Entity;
    e.id = 10;
    return math.Entity.next + e.id + math.Box[i32].limit + spare + math.width() + Buf[4, i32].count +
        (math.Entity.bias == 1.0f);
}
```

The program returns 42. `math.width()` is `sizeof(Entity)`, which is 4. `math.Box[i32].limit` is the exported constant on that specialization. `Buf[4, i32].count` is 4.

## Cost

`export` and a name that is visible across files in the package are resolved while compiling. They add no instructions. Reading an exported file-scope `var` is 1 `movl` from its address. A call of an exported function is a traced call, 36 instructions in the caller when fewer than 32 frames are stored.

[Index](../README.md) · [Previous: Libraries](library.md) · [Next: extern](extern.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

# Visibility

[Index](../README.md) · [Previous: Libraries](library.md) · [Next: extern](extern.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

Visibility decides which names the rest of the package can use, and which names another package can use.

## Names inside a package

Every file in a package can use a name declared in another file of that directory.

A variable, a type, or the same parameter list in two files is `redefinition`.

Another parameter list of a function may be written in a second file of the package.

Two `export` declarations of one name are `redefinition`.

A method is written in the file that defines its struct or enum.

The other files of the package can call it.

## What `export` publishes

Only a file-scope `export` is published to importers.

A name that was not exported is reported as `name is not exported`.

```c
package math;

func secret() i32 {
    return 1;
}

export func add(a i32, b i32) i32 {
    return a + b + secret();
}
```

```asm
main:
# trace frame for the call, then the matching pop
movl $40, %edi                   # 40
movl $2, %esi                    # 2
callq add
retq

add:
movl %edi, 32(%rsp)              # a
movl %esi, 36(%rsp)              # b
movl 32(%rsp), %eax
addl 36(%rsp), %eax              # a + b
movl %eax, 20(%rsp)
# trace frame for secret, then the matching pop
callq secret
movl 16(%rsp), %ecx              # secret()
movl 20(%rsp), %eax              # a + b
addl %ecx, %eax                  # a + b + secret()
addq $40, %rsp
retq

secret:
movl $1, %eax                    # return 1
retq
```

`math.add(40, 2)` adds the arguments, then calls `secret`.

`secret` can be called from any file in the package.

An importer sees `add`.

## Methods and trait parameters

`export func Point::sum` publishes that value method.

Another package calls `value.sum(...)`.

A method without `export` is reported as `sum is not exported`.

`export func use(s Shape)` publishes a function whose parameter is a trait.

Another package calls `pkg.use(r)`.

When the struct is in another package, its method is exported, or the call is `area is not exported`.

`extern func Point::read` names the same method in a tlc library.

The call stays `value.read(...)`.

`extern func Vec[T]::get` is that rule for a generic struct.

The call stays `value.get(...)`.

`pkg.Point.sum` is reported as `not supported yet`.

## Declarations `export` can mark

`export` applies to functions, variables, constants, variables on a type, and `type` declarations.

`export extern(abi="c")` is allowed on a function.

The symbol keeps its C name.

`extern(abi="c", name="symbol")` overrides that C object name.

`name` without `abi="c"` is `name requires abi="c"`.

`export` together with `extern` on a variable is written `export extern`.

```c
package math;

export type Entity struct {
    id i32;
};

export var Entity::next i32 = 40;
export const bias = 2;
```

```asm
main:
xorl %eax, %eax                  # zero the result
retq                             # return
```

```c
import "math";

func main() i32 {
    math.Entity.next = math.Entity.next + math.bias;
    return math.Entity.next;
}
```

```asm
main:
movq (%rip), %rax                # address of Entity.next
movl (%rax), %ecx                # load it
addl $2, %ecx                    # add bias
movq (%rip), %rax
movl %ecx, (%rax)                # store it
movq (%rip), %rax
movl (%rax), %eax                # return it
retq
```

The program returns 42.

`math.bias` is the exported constant.

`math.Entity.next` is the exported variable on `Entity`.

`export var Box[T]::extra` publishes a variable on a generic struct.

Another package reads and assigns `pkg.Box[i32].extra` when `Box` and the variable are exported.

A constant or variable without `export` is reported as `name is not exported`.

Assigning `math.bias` is reported as `not an lvalue`.

`export func Entity::make` publishes `math.Entity.make(...)`.

Without `export` that call is reported as `make is not exported`.

## Members

`export id i32` publishes a member.

Another package reads and assigns `value.id`.

A member without `export` is reported as `name is not exported`.

A member is already a variable, written `name type` and not `var name type`.

`var` inside the braces is reported as `not supported yet`.

`export var bonus = 4` and `export spare := 2` publish inferred globals.

Another package assigns them.

`sizeof` still counts only the fields.

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

```asm
main:
xorl %eax, %eax                  # zero the result
retq                             # return
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

```asm
main:
movq (%rip), %rax
movl (%rax), %ecx                # Entity.next
movq (%rip), %rax
addl (%rax), %ecx                # + bonus
movq (%rip), %rax
addl (%rax), %ecx                # + spare
movq (%rip), %rax
movl %ecx, (%rax)                # store Entity.next
movq (%rip), %rax
movl (%rax), %eax                # reload it
# trace frame for width, then the matching pop
callq width
addl %ecx, %eax                  # next + width()
addq $24, %rsp
retq

width:
movl $4, %eax                    # sizeof(Entity)
retq
```

`e.id = 4` is not read, so that store is removed.

`next` is loaded, `bonus` and `spare` are added, and `width` returns `sizeof(Entity)`.

The program returns 42.

`math.width()` is `sizeof(Entity)`, which is 4.

`e.id` is the exported member.

`math.Entity.next` started at 32.

## Constants on a type

`const Entity::next` is a constant on the type, written beside the struct.

`export const` publishes it, including `export const Box[T]::limit` on a generic struct.

Another package reads `pkg.Entity.next`, `pkg.Box[i32].limit`, and `pkg.Buf[4, i32].count`.

Assigning that constant is reported as `not an lvalue`.

A constant without `export` is reported as `name is not exported`.

An unexported generic struct is reported as `Box is not exported`.

Another package names an exported generic struct as `pkg.Vec[i32]`.

A `const` inside the braces is not a member.

It is reported as `not supported yet`.

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

```asm
main:
xorl %eax, %eax                  # zero the result
retq                             # return
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

`id` is not exported, so this example is rejected (`id is not exported`) and emits nothing.

The program returns 42.

`math.width()` is `sizeof(Entity)`, which is 4.

`math.Box[i32].limit` is the exported constant on that specialization.

`Buf[4, i32].count` is 4.

[Index](../README.md) · [Previous: Libraries](library.md) · [Next: extern](extern.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

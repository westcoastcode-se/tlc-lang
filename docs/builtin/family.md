# builtin/env/consts

[Index](../README.md) · [Previous: memeql and memcmp](mem.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

`import "builtin/env/consts"` loads the compiler package `consts`. It exports the constant `family`. The type is `*const u8`. On Windows the text is `windows`. On Linux, macOS, and every other system the text is `unix`. The value is chosen from the system `tlc` is running on. A directory named `builtin/env/consts` on the search path is that package instead. The compiler package is used when the search finds none.

```c
import "builtin/env/consts";

extern(abi="c") func printf(fmt *const u8, ...) i32;

func main() i32 {
    printf("%s\n", consts.family);
    return 0;
}
```

On Linux or macOS the program prints `unix`. On Windows it prints `windows`. `import { family } from "builtin/env/consts"` brings the name into the file. Assigning `consts.family`, or writing `consts.family[0] = 1`, is reported as `type mismatch`.

## Cost

`consts.family` is a string constant, the same as `"unix"` or `"windows"`. Passing it to `printf` is one `leaq` of that address, plus the traced call of `printf` counted on [extern](../packages/extern.md). The name adds no load of a global variable. Assigning it is rejected while compiling.

[Index](../README.md) · [Previous: memeql and memcmp](mem.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

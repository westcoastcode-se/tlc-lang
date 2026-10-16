# Symbol names

[Index](../README.md) · [Previous: Linking](link.md) · [Next: A program](program.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

A symbol is named `qualifier.name`. `extern` and `extern(abi="tlc")` use that name and are linked from a tlc library. `extern(abi="c")` keeps the C name.

## Cost

The symbol name is chosen while compiling. It adds no instructions. `extern(abi="c")` keeps the C name and is still a traced call.

[Index](../README.md) · [Previous: Linking](link.md) · [Next: A program](program.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

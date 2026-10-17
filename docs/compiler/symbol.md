# Symbol names

[Index](../README.md) · [Previous: Linking](link.md) · [Next: A program](program.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

A symbol is named `qualifier.name`. Each parameter list of one name is a separate object when there are several lists. A name with one list stays `qualifier.name`. `extern` and `extern(abi="tlc")` use that name and are linked from a tlc library. `extern(abi="c")` keeps the C name. `name="symbol"` on `extern` replaces that object name. The identifier in the program stays the name used in calls and in other files.

## Cost

The symbol name is chosen while compiling. It adds no instructions. `extern(abi="c")` keeps the C name and is still a traced call. `name="symbol"` adds no instructions either. The call is still traced.

[Index](../README.md) · [Previous: Linking](link.md) · [Next: A program](program.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

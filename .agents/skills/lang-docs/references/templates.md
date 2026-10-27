# Documentation Templates

## Full documentation outline

```
docs/
├── index.md                    # Overview
├── design.md                   # Design philosophy (Explanation)
├── getting-started.md          # Install + first program (Tutorial)
├── tutorials/
│   ├── tour.md                 # Language tour
│   └── ...
├── reference/
│   ├── syntax.md
│   ├── types.md
│   ├── control-flow.md
│   ├── functions.md
│   ├── modules.md
│   └── error-handling.md
├── stdlib/
├── cookbook/
├── tooling.md
└── glossary.md
```

## Tutorial template

```markdown
# [Clear outcome title]

In this tutorial you will build [X]. You will learn [key concepts].

**Time:** ~15 minutes  
**Prerequisites:** [list]

## What you will build

[Show final result or expected output]

## Step 1: [Action]

[Short explanation]

```lang
// complete code
```

Expected output:
```
...
```

## Step 2: ...

## Next steps

- [Link to related tutorial]
- [Link to reference]
```

## Reference entry template

```markdown
# [Feature name]

[One-line summary of what it is and when to use it.]

## Syntax

```lang
formal or example syntax
```

## Description

[Precise, factual description of behavior, types, and semantics.]

## Examples

### Basic usage

```lang
// complete, runnable example
```

Output:
```
...
```

### Edge case / error

```lang
// example that fails
```

## Errors and edge cases

- Condition → what happens

## See also

- [Related feature]
```

## Getting started template

```markdown
# Getting started with [Language]

[One sentence: what the language is for.]

## Installation

[Exact commands]

## Hello world

```lang
// shortest complete program
```

Run it:
```
[command]
```

Output:
```
Hello, world!
```

## Next steps

- [Language tour]
- [Basic concepts]
```

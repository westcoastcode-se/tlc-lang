---
name: lang-docs
description: Write professional, clear, and easy-to-understand documentation for a programming language under development. Use when creating tutorials, language reference, getting-started guides, standard library docs, design explanations, or any docs for a custom programming language. Follows Diátaxis, prioritizes runnable examples and accessible style.
---

# Lang Docs — Professional Programming Language Documentation

Write high-quality documentation for a programming language the user is developing. Produce professional, skimmable, example-driven Markdown that is clear to both beginners and experienced developers.

## When to use

Trigger on requests to document a programming language, write tutorials, language specs, reference docs, getting-started guides, stdlib documentation, or design philosophy for a custom language.

## Core principles

Follow the **Diátaxis** framework. Separate content by reader need:

| Type | Purpose | Style | Example |
|------|---------|-------|---------|
| **Tutorial** | Teach by doing | Guided, one path, no branches | "Build a calculator in 10 minutes" |
| **How-to** | Solve a specific goal | Practical, assumes some knowledge | "How to handle errors" |
| **Reference** | Precise specification | Factual, complete, consistent | Syntax, types, semantics |
| **Explanation** | Understand why | Conceptual, discursive | Design goals, memory model |

### Writing rules

- **Lead with the answer.** Put the goal or summary first.
- **Active voice and short sentences.** Aim for 15–20 words per sentence.
- **Examples first.** Every concept gets at least one complete, runnable code example with expected output.
- **Progressive disclosure.** Start simple, then add details and edge cases.
- **Skimmable.** Descriptive headings, short paragraphs, lists, and callouts.
- **Explain jargon.** Define terms on first use and link to a glossary.
- **Consistent terminology.** Maintain a glossary and reuse the same words.
- **No condescension.** Avoid "obviously", "simply", "just", and "basically".
- **Complete examples.** Show imports, setup, and cleanup. Mark incomplete snippets clearly.
- **Show both happy path and common errors.**
- **Do not invent features.** If syntax or semantics are unclear, ask for examples or a specification rather than guessing.
- **Language of output.** Generate documentation in the language requested by the user (English or Swedish). Default to English unless specified.
- **Assembly.** Show what the assembly looks like for each working example to give the developer in idea on the cost of a specific feature

## Workflow

1. **Clarify the language** if details are missing. Ask for (or extract from context):
   - Language name and version
   - Design goals and philosophy
   - Key features (types, concurrency, memory model, error handling, etc.)
   - Syntax examples or grammar
   - Existing code samples or stdlib
   - Target audience

2. **Identify the documentation type** (tutorial, how-to, reference, explanation, or a full docs site structure).

3. **Generate structured Markdown** following the templates in `references/templates.md`.

4. **Include a glossary** for new terms.

5. **Review** for clarity, completeness of examples, and consistency.

## Document structure templates

See `references/templates.md` for ready-to-use outlines.

Typical full documentation set:

- Overview / What is [Language]?
- Design philosophy and goals
- Getting started (install + first program)
- Language tour / Tutorial path
- Language reference (syntax, types, control flow, functions, modules, error handling)
- Standard library
- Cookbook / Common patterns
- Tooling and interop
- Glossary

## Style notes

- Use Markdown with fenced code blocks. Specify the language if known (or use a placeholder like `lang`).
- Prefer present tense for reference material.
- Use callouts for warnings, notes, and tips:
  ```markdown
  > **Note:** ...
  > **Warning:** ...
  > **Tip:** ...
  ```
- Link related pages frequently.
- For reference entries, use this order: one-line summary → detailed description → examples → errors/edge cases → see also.

## References

- `references/templates.md` — Document outlines and section templates
- `references/style-guide.md` — Detailed writing style rules
- `references/examples.md` — Annotated good/bad examples

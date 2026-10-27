# Style Guide for Language Documentation

## Tone

- Professional and helpful.
- Confident but not arrogant.
- Address the reader as "you".
- Friendly without being casual or humorous unless the language's culture warrants it.

## Language

- Active voice: "The compiler rejects the program" not "The program is rejected by the compiler."
- Short sentences (target 15–20 words).
- One idea per paragraph.
- Present tense for reference material.
- Define every technical term on first use.
- Prefer concrete examples over abstract descriptions.

## Words to avoid

| Avoid | Prefer |
|-------|--------|
| simply, just, obviously, basically, easy | (omit or rephrase) |
| utilize | use |
| in order to | to |
| leverage | use |
| note that | (start the sentence directly) |

## Formatting

- Descriptive headings that say what the section contains.
- Fenced code blocks with language tag.
- Tables for comparisons, parameters, or syntax summaries.
- Callouts:

  ```markdown
  > **Note:** Additional context.
  > **Warning:** This can cause data loss.
  > **Tip:** A better approach is...
  ```

- Inline code for keywords, types, and short snippets.
- Bold for first definition of important terms.

## Code examples

- Complete and runnable whenever possible.
- Show expected output.
- Comment only the non-obvious parts.
- Prefer realistic variable names over `foo`/`bar` when it helps clarity.
- Mark experimental or incomplete features clearly.

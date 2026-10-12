(comment) @comment

(string) @string
(character) @string

(number) @number

(keyword) @keyword
(builtin_type) @type.builtin

(identifier) @variable

(function_definition
  "func" @keyword
  name: (identifier) @function)

(type_definition
  "type" @keyword
  name: (identifier) @type)

(operator) @operator
(punctuation) @punctuation

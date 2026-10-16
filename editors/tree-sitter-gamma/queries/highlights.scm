; Highlights for Gamma. The capture names follow the nvim-treesitter conventions.
;
; Where several patterns match the same node, the later one is the more specific, as in
; nvim-treesitter.

; Comments
; --------

(comment) @comment @spell
(block_comment) @comment @spell
(doc_comment) @comment.documentation @spell

; Literals
; --------

[
  (string_literal)
  (c_string_literal)
  (raw_string_literal)
] @string

(escape_sequence) @string.escape
(character_literal) @character
(integer_literal) @number
(float_literal) @number.float
(boolean_literal) @boolean
(null) @constant.builtin
(uninitialized) @constant.builtin
(discard) @variable.builtin

; Identifiers by convention
; -------------------------

; Predeclared types.
((identifier) @type.builtin
  (#match? @type.builtin "^([ui][1-9][0-9]*|usize|isize|f16|f32|f64|bool|never|type|rawptr|string|cstring|rune|cont)$"))

; Types and effects are capitalized.
((identifier) @type
  (#match? @type "^([A-Z]|[A-Z][A-Za-z0-9_]*[a-z][A-Za-z0-9_]*)$"))

; Constants are written in capitals.
((identifier) @constant
  (#match? @constant "^[A-Z][A-Z0-9_]+$"))

; Declarations
; ------------

(const_declaration
  name: (identifier) @function
  value: (function_expression))

(const_declaration
  name: (identifier) @type
  value: [
    (struct_type)
    (union_type)
    (effect_type)
    (attributed_type)
  ])

(const_declaration
  name: (identifier) @module
  value: [
    (import_expression)
    (foreign_block)
  ])

(foreign_member
  name: (identifier) @function)

(operation
  name: (identifier) @function.method)

(handler_clause
  name: (identifier) @function.method)

(handler_declaration
  effect: (identifier) @type)

(handler_expression
  effect: (identifier) @type)

(parameter
  name: (identifier) @variable.parameter)

(clause_parameters
  (identifier) @variable.parameter)

(constraint
  name: (identifier) @type)

(field_declaration
  name: (identifier) @variable.member)

(variant
  name: (identifier) @constant)

; Expressions
; -----------

(field_expression
  field: (identifier) @variable.member)

(call_expression
  function: (identifier) @function.call)

(call_expression
  function: (field_expression
    field: (identifier) @function.method.call))

(call_expression
  function: (result_unwrap_expression
    (identifier) @function.call))

(named_argument
  name: (identifier) @variable.parameter)

(named_element
  name: (identifier) @variable.member)

(named_type
  name: (identifier) @variable.member)

(variant_expression
  name: (identifier) @constant)

(scoped_type
  name: (identifier) @type)

(generic_type
  type: (identifier) @type)

(label
  (identifier) @label)

(intrinsic_name) @function.builtin
(attribute_name) @attribute

; Keywords
; --------

[
  "if"
  "else"
  "match"
  "when"
] @keyword.conditional

[
  "while"
  "for"
] @keyword.repeat

(for_expression "in" @keyword.repeat)

"return" @keyword.return

[
  "break"
  "continue"
  "resume"
  "cancel"
  "with"
  "on"
  "needs"
] @keyword

[
  "defer"
  "errdefer"
  "try"
] @keyword.exception

"fn" @keyword.function

[
  "struct"
  "union"
  "effect"
  "handler"
  "foreign"
  "where"
  "test"
  "bench"
] @keyword.type

"import" @keyword.import

[
  "pub"
  "using"
  "const"
  "ctl"
  "esc"
] @keyword.modifier

[
  "and"
  "or"
  "not"
  "is"
  "is not"
  "not in"
] @keyword.operator

(binary_expression "in" @keyword.operator)

; Operators
; ---------

[
  "+" "-" "*" "/" "%"
  "+%" "-%" "*%"
  "+|" "-|" "*|"
  "+?" "-?" "*?"
  "<<" ">>" "<<%" "<<|" "<<?"
  "&" "|" "~"
  "<" "<=" ">" ">="
  ".." "..=" "..."
  "=" ":="
  "+=" "-=" "*=" "/=" "%="
  "<<=" ">>=" "&=" "|=" "~="
  "+%=" "-%=" "*%=" "<<%="
  "+|=" "-|=" "*|=" "<<|="
  "=>" "->" "::"
  "!" "?" "^"
] @operator

; Punctuation
; -----------

[
  "("
  ")"
  "["
  "]"
  "{"
  "}"
] @punctuation.bracket

[
  ","
  ";"
  ":"
  "."
] @punctuation.delimiter

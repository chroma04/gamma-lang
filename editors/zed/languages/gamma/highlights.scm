; Highlights for Gamma, using the capture names Zed themes understand.
;
; Where several patterns match the same node, the later one is the more specific.

; Comments
; --------

(comment) @comment
(block_comment) @comment
(doc_comment) @comment.doc

; Literals
; --------

[
  (string_literal)
  (c_string_literal)
  (raw_string_literal)
] @string

(escape_sequence) @string.escape
(character_literal) @string
(integer_literal) @number
(float_literal) @number
(boolean_literal) @boolean
(null) @constant.builtin
(uninitialized) @constant.builtin
(discard) @variable.special

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
  name: (identifier) @type @namespace
  value: [
    (import_expression)
    (foreign_block)
  ])

(foreign_member
  name: (identifier) @function)

(operation
  name: (identifier) @function)

(handler_clause
  name: (identifier) @function)

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
  name: (identifier) @property)

(variant
  name: (identifier) @constant)

; Expressions
; -----------

(field_expression
  field: (identifier) @property)

(call_expression
  function: (identifier) @function)

(call_expression
  function: (field_expression
    field: (identifier) @function))

(call_expression
  function: (result_unwrap_expression
    (identifier) @function))

(named_argument
  name: (identifier) @variable.parameter)

(named_element
  name: (identifier) @property)

(named_type
  name: (identifier) @property)

(variant_expression
  name: (identifier) @constant)

(scoped_type
  name: (identifier) @type)

(generic_type
  type: (identifier) @type)

(label
  (identifier) @label)

(intrinsic_name) @function @function.builtin
(attribute_name) @attribute

; Keywords
; --------

[
  "if"
  "else"
  "match"
  "when"
] @keyword

[
  "while"
  "for"
] @keyword

(for_expression "in" @keyword)

"return" @keyword

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
] @keyword

"fn" @keyword

[
  "struct"
  "union"
  "effect"
  "handler"
  "foreign"
  "where"
  "test"
  "bench"
] @keyword

"import" @keyword

[
  "pub"
  "using"
  "const"
  "ctl"
  "esc"
] @keyword

[
  "and"
  "or"
  "not"
  "is"
  "is not"
  "not in"
] @keyword

(binary_expression "in" @keyword)

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

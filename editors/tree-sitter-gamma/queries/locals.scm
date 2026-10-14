; Scopes
; ------

[
  (source_file)
  (block)
  (function_expression)
  (handler_clause)
  (match_arm)
  (for_expression)
  (while_statement)
  (if_expression)
  (else_if_clause)
  (else_clause)
  (else_expression)
] @local.scope

; Definitions
; -----------

(parameter
  name: (identifier) @local.definition.parameter)

(clause_parameters
  (identifier) @local.definition.parameter)

(const_declaration
  name: (identifier) @local.definition.var)

(short_declaration
  name: (identifier) @local.definition.var)

(typed_declaration
  name: (identifier) @local.definition.var)

(for_expression
  pattern: (identifier) @local.definition.var)

(let_condition
  pattern: (identifier) @local.definition.var)

(else_expression
  capture: (identifier) @local.definition.var)

(else_clause
  capture: (identifier) @local.definition.var)

(match_arm
  pattern: (identifier) @local.definition.var)

(match_arm
  pattern: (call_expression
    arguments: (arguments (identifier) @local.definition.var)))

; References
; ----------

(identifier) @local.reference

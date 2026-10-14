[
  (block)
  (match_body)
  (handler_body)
  (field_declaration_list)
  (variant_list)
  (operation_list)
  (declaration_block)
  (foreign_block)
  (arguments)
  (parameters)
  (tuple_expression)
  (tuple_type)
  (effect_set)
] @indent.begin

[
  "}"
  ")"
  "]"
] @indent.end @indent.branch

(block_comment) @indent.auto
(string_literal) @indent.ignore
(raw_string_literal) @indent.ignore

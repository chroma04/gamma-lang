(const_declaration
  value: (function_expression
    body: (block
      "{"
      (_)* @function.inside
      "}"))) @function.around

(test_declaration
  body: (block
    "{"
    (_)* @function.inside
    "}")) @function.around

(bench_declaration
  body: (block
    "{"
    (_)* @function.inside
    "}")) @function.around

(const_declaration
  value: [
    (struct_type
      body: (field_declaration_list
        "{"
        (_)* @class.inside
        "}"))
    (union_type
      body: (variant_list
        "{"
        (_)* @class.inside
        "}"))
    (effect_type
      body: (operation_list
        "{"
        (_)* @class.inside
        "}"))
  ]) @class.around

[
  (comment)
  (doc_comment)
  (block_comment)
] @comment.around

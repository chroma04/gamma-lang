(const_declaration
  name: (identifier) @name
  value: [
    (function_expression)
    (struct_type)
    (union_type)
    (effect_type)
    (attributed_type)
    (import_expression)
    (foreign_block)
  ]) @item

(handler_declaration
  "handler" @context
  effect: (_) @name) @item

(test_declaration
  "test" @context
  name: (_) @name) @item

(bench_declaration
  "bench" @context
  name: (_) @name) @item

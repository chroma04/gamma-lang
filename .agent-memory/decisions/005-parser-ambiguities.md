# 005 How the parser resolves grammar ambiguities (all written into spec §A.7)
- `!` after operand (rule 3): speculative type parse → `Bang{lhs,rhs}` else `ResUnwrap`; sema
  decides: lhs type → E!T, else unwrap + reinterpret rhs's leading parens as call args.
- Type prefixes before calls (rule 12): parser builds `?(Call(T,args))`; sema re-associates to
  `Call(?T, args)` when the call is a construction/conversion (not generic type application).
- Heads (rule 4): first `{` at head depth begins body; brace constructs need parens. The lexer
  relies on this to find match bodies (first `{` at same depth after `match`).
- break/continue labels (rule 11): parser tracks lexically enclosing labels.
- `is` rhs parsed as expression (with `_` allowed as an expression), turned into a pattern by sema.
- Struct pattern shorthand `S(x)`: parsed as positional Bind; sema treats as `x: x`.

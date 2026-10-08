# Spec amendments made (current truth is the spec itself; this is a quick index)
- §2.1 lone CR: ill-formed everywhere; raw strings normalize CRLF→LF.
- §2.5.1 defined `width`, digit classes, lowercase prefixes, invalid-suffix rule.
- §2.6 added `...` token (foreign variadics); numeric field after `.` lexes as plain int
  (`t.0.1`); `is not` / `not in` formed by the lexer when on one line.
- §2.8 rule 3 reworded (innermost bracket decides); rule 2 exception = directly inside a match
  body; defined how a match body is recognized.
- §6.1 literal inference: shift amounts not unified with lhs (unsigned, fallback u32); index and
  slice bounds unsigned (fallback usize); coercion target ?T/E!T fixes literal to T.
- §14.7 example: `resume self.next()` was wrong per A.7 rule 10 → bind `r := self.next(); resume r()`.
- A.4 stmt grammar: when_stmt alternatives were misattached; fixed.
- A.7 rules 3 (Bang algorithm), 4 (heads, generalized), new 11 (break labels), 12 (type prefix
  before call), 13 (newlines after attributes).

# The Gamma Language Specification

Version 1.0. Gamma is a systems programming language intended to replace C: simple, fast and sensible, with algebraic effects for allocation, I/O, iteration, early exit and coroutines.

This specification is self-contained. Chapters 1–21 and Appendices A–B are normative; [Appendix C](#appendix-c-design-rationale) explains the reasoning behind the design, and [Appendix D](#appendix-d-implementation-notes) sketches the intended implementation.

| § | Chapter |
| --- | --- |
| 1 | [Introduction](#1-introduction): goals, notation, terms, build modes |
| 2 | [Lexical structure](#2-lexical-structure) |
| 3 | [Programs, modules and top-level declarations](#3-programs-modules-and-top-level-declarations) |
| 4 | [Bindings, scopes and mutability](#4-bindings-scopes-and-mutability) |
| 5 | [Types](#5-types) |
| 6 | [Literals, constants and conversions](#6-literals-constants-and-conversions) |
| 7 | [Expressions](#7-expressions) |
| 8 | [Statements and control flow](#8-statements-and-control-flow) |
| 9 | [Patterns](#9-patterns) |
| 10 | [Optionals and results](#10-optionals-and-results) |
| 11 | [Functions](#11-functions) |
| 12 | [Effects and handlers](#12-effects-and-handlers) |
| 13 | [Iteration](#13-iteration) |
| 14 | [Fibers and continuations](#14-fibers-and-continuations) |
| 15 | [Memory and allocation](#15-memory-and-allocation) |
| 16 | [Generics](#16-generics) |
| 17 | [Compile-time evaluation](#17-compile-time-evaluation) |
| 18 | [Safety, checks and undefined behavior](#18-safety-checks-and-undefined-behavior) |
| 19 | [Interoperability and low-level facilities](#19-interoperability-and-low-level-facilities) |
| 20 | [Attributes and intrinsics](#20-attributes-and-intrinsics) |
| 21 | [The core library](#21-the-core-library) |
| A | [Grammar](#appendix-a-grammar) |
| B | [Reference tables](#appendix-b-reference-tables) |
| C | [Design rationale](#appendix-c-design-rationale) |
| D | [Implementation notes](#appendix-d-implementation-notes) |

---

## 1. Introduction

Gamma is a systems programming language intended as a replacement for C. Its goals, in priority order:

1. **Sensible.** Every construct has one meaning that can be stated in a sentence. Rules that read the same in different places mean the same thing. Footguns inherited from C (implicit conversions, octal literals, `x & 1 == 0`, fallthrough, null by default, undefined signed overflow) are removed.
2. **Fast.** Nothing costs more than the equivalent C unless the program asks for a check, and every check can be removed by a build mode. Abstractions (generics, effects, iteration) compile away when their implementation is known statically.
3. **Simple.** A small number of orthogonal features. Where two features could do one job, one is removed.

Gamma's distinguishing feature is **algebraic effects with handlers**, used for the things C does with globals, callbacks, `setjmp`, and hand-written state machines: allocation, I/O, logging, iteration, early exit, and coroutines.

### 1.1 Status of this document

This document is the complete, normative definition of Gamma version 1. Sections and paragraphs marked *Note*, *Example*, *Rationale* or *Implementation note* are non-normative.

### 1.2 Notation

Grammar productions use EBNF:

| Notation | Meaning |
| --- | --- |
| `'x'` or `` `x` `` | literal token |
| `a b` | sequence |
| `a \| b` | alternative |
| `[ a ]` | optional |
| `{ a }` | zero or more |
| `a % ','` | one or more `a` separated by `,`, with an optional trailing `,` |

The complete grammar is collected in Appendix A.

In code examples, `...` stands for elided code and is not Gamma syntax.

### 1.3 Terms

- **Compile error.** The program is ill-formed. A conforming implementation must reject it with a diagnostic.
- **Trap.** Execution stops abnormally: the implementation reports the source location and reason (in Debug and Checked builds) and terminates the process. A trap is not catchable by Gamma code and does not run `defer` statements. *Note:* `trap` is not an exception mechanism; recoverable failure uses results (§10).
- **Checked operation.** An operation with a precondition that is verified at run time in Debug and Checked builds (where a violation traps) and assumed in Fast builds. §18 lists every checked operation and what a violation means in Fast builds.
- **Unspecified value.** A valid value of the correct type, about which nothing else is promised. Different evaluations may produce different values. Producing an unspecified value is never undefined behavior by itself.
- **Undefined behavior (UB).** The program has no meaning. The complete list of operations that can cause UB is in §18.4; anything not on that list is defined, unspecified, implementation-defined, or a trap.
- **Implementation-defined.** The implementation chooses and documents the behavior.
- **Place.** An expression that denotes a storage location (§7.2).

### 1.4 Conformance and build modes

A conforming implementation provides three build modes. They differ only in the handling of checked operations and in diagnostic extras; a program that performs no failing checked operation and no UB has the same observable behavior in all three.

| Mode | Checked operations | Required extras |
| --- | --- | --- |
| **Debug** | trap on failure | see §18.3 |
| **Checked** | trap on failure | none |
| **Fast** | not performed | none |

### 1.5 Organization

| § | Topic |
| --- | --- |
| 2 | Lexical structure |
| 3 | Programs, modules, and top-level declarations |
| 4 | Bindings, scopes and mutability |
| 5 | Types |
| 6 | Literals, constants and conversions |
| 7 | Expressions |
| 8 | Statements and control flow |
| 9 | Patterns |
| 10 | Optionals and results |
| 11 | Functions |
| 12 | Effects and handlers |
| 13 | Iteration |
| 14 | Fibers and continuations |
| 15 | Memory and allocation |
| 16 | Generics |
| 17 | Compile-time evaluation |
| 18 | Safety, checks and undefined behavior |
| 19 | Interoperability and low-level facilities |
| 20 | Attributes and intrinsics |
| 21 | The core library |
| A | Grammar |
| B | Reference tables |
| C | Design rationale |
| D | Implementation notes |

---

## 2. Lexical structure

### 2.1 Source text

A source file is a sequence of Unicode scalar values encoded as UTF-8. A byte-order mark at the start of a file is ignored. A file that is not valid UTF-8 is ill-formed. Source files have the extension `.gm`.

Line terminators are LF (U+000A) or CR LF. A CR (U+000D) that is not immediately followed by LF is ill-formed everywhere, including inside string literals. Inside raw string literals, a CR LF line terminator is normalized to LF.

The horizontal tab (U+0009) and space (U+0020) are whitespace. No other Unicode whitespace is permitted outside comments and string literals.

### 2.2 Comments

- A line comment starts with `//` and extends to the end of the line.
- A block comment starts with `/*` and ends with the matching `*/`. Block comments nest: `/* a /* b */ c */` is one comment.
- A doc comment is a line comment starting with `///`. It attaches to the declaration that follows it. Doc comments have no semantic effect.

A comment is equivalent to whitespace, except that a block comment containing a line terminator is equivalent to a line terminator for the purposes of §2.8.

### 2.3 Tokens

The token classes are: identifiers, keywords, literals, operators and punctuation, intrinsic names (`@name`), attribute names (`#name`), and the significant newline (§2.8). Tokenization is maximal munch: at each point the longest token that matches is taken, with the exceptions stated in §2.6.

### 2.4 Identifiers and keywords

```
ident      = ident_start { ident_cont }
ident_start= 'A'..'Z' | 'a'..'z' | '_'
ident_cont = ident_start | '0'..'9'
```

Identifiers are ASCII. They are case-sensitive. The identifier consisting of a single `_` is the *discard* and cannot be referenced (§4.6).

**Keywords** (reserved everywhere):

```
and      break    continue ctl      defer    discard  effect   else
errdefer esc      false    fn       for      foreign  handler  if
import   in       is       match    not      null     or       pub
resume   return   struct   true     try      union    using    when
while    with
```

**Contextual keywords** are ordinary identifiers except in the syntactic position given:

| Word | Special only |
| --- | --- |
| `const` | immediately after `^`, `[^]` or `[]` in a type (§5.5) |
| `on` | after the handler list of a `with` (§12.6) |
| `where` | after a function signature or a type's parameter list (§16.3) |
| `test` | at the start of a top-level declaration (§3.8) |

`type` and `cont` are not keywords; they are predeclared, reserved type names (§21.1).

### 2.5 Literals

#### 2.5.1 Integer literals

```
int_lit    = ( dec_lit | hex_lit | oct_lit | bin_lit ) [ int_suffix ]
dec_lit    = digit { [ '_' ] digit }
hex_lit    = '0x' hexdigit { [ '_' ] hexdigit }
oct_lit    = '0o' octdigit { [ '_' ] octdigit }
bin_lit    = '0b' bindigit { [ '_' ] bindigit }
int_suffix = ( 'u' | 'i' ) width | 'usize' | 'isize'
width      = nzdigit { digit }                   // 1 … 65535, no leading 0
```

`digit` is `0`–`9`, `nzdigit` is `1`–`9`, `hexdigit` is `0`–`9`, `a`–`f`, `A`–`F`, `octdigit` is `0`–`7` and `bindigit` is `0` or `1`. The prefixes `0x`, `0o`, `0b` are lowercase. A literal immediately followed by an identifier character that does not form a valid suffix (`12abc`, `0x1g`) is ill-formed.

A decimal literal may not have a leading `0` unless it is exactly `0` (so `017` is ill-formed, avoiding C's octal trap). Underscores separate digits and have no meaning; a literal may not begin or end with `_` and may not contain `__`. The value of an integer literal is a mathematical integer of unbounded size. A suffix fixes the type (§6.1); without one the literal is *untyped*.

#### 2.5.2 Float literals

```
float_lit  = dec_lit '.' dec_lit [ exponent ] [ float_suffix ]
           | dec_lit exponent [ float_suffix ]
           | hex_lit '.' hexdigits 'p' [ '+' | '-' ] dec_lit [ float_suffix ]
exponent   = ( 'e' | 'E' ) [ '+' | '-' ] dec_lit
float_suffix = 'f16' | 'f32' | 'f64'
```

Both sides of the `.` are required (`1.` and `.5` are not float literals). This keeps `1..10` a range and `x.0` a field access. The value of a float literal is an exact rational number; it is rounded only when it receives a type (§6.1).

#### 2.5.3 Character literals

`'c'` is an **integer literal** whose value is the Unicode code point of `c`. `c` is a single Unicode scalar value or one escape sequence (§2.5.5). `''` is ill-formed. A character literal is untyped and receives its type like any integer literal; `'é'` does not fit `u8` and is a compile error at type `u8`.

#### 2.5.4 String literals

| Form | Example | Type |
| --- | --- | --- |
| Regular | `"tab\there"` | `string` (`[]const u8`) |
| Raw | `` `C:\dir` `` | `string` |
| C string | `c"hello"` | `cstring` |

- Regular strings may not contain an unescaped line terminator.
- Raw strings are delimited by one or more backticks; a raw string opened with *n* backticks ends at the next run of exactly *n* backticks. Raw strings contain no escapes and may span lines. If the opening delimiter is immediately followed by a line terminator, that line terminator is not part of the string.
- A C string is a regular string with a `c` prefix. It may not contain `\0`; a NUL terminator is appended.
- All string literals denote UTF-8 bytes stored in read-only static memory for the life of the program. A regular string literal is not NUL-terminated.
- Adjacent string literals are **not** concatenated.

#### 2.5.5 Escape sequences

| Escape | Meaning |
| --- | --- |
| `\n` `\r` `\t` `\0` | LF, CR, TAB, NUL |
| `\\` `\"` `\'` | backslash, double quote, single quote |
| `\xHH` | the byte HH (exactly two hex digits) |
| `\u{H…}` | the UTF-8 encoding of the scalar value H… (1–6 hex digits; surrogates ill-formed) |

In a string literal `\xHH` inserts one raw byte, so a string literal may hold invalid UTF-8 if written that way. In a character literal `\xHH` denotes the value HH.

### 2.6 Operators and punctuation

```
+    -    *    /    %    <<   >>   &    |    ~
+%   -%   *%   <<%
+|   -|   *|   <<|
+?   -?   *?   <<?
=    +=   -=   *=   /=   %=   <<=  >>=  &=   |=   ~=
+%=  -%=  *%=  <<%= +|=  -|=  *|=  <<|=
<    <=   >    >=
?    !    ^    @    #    \
..   ..=  ...  ---  ::   :=   :    ,    .    ;
=>   ->
(    )    [    ]    {    }
```

`...` appears only as the variadic marker of a foreign function type (§19.2).

Exceptions to maximal munch:

- `[^]` is three tokens, `[`, `^`, `]`; the parser recognizes the sequence.
- `---` is always one token, so `a---b` is ill-formed; write `a - -b`.
- A numeric field name is lexed as a plain decimal integer: after a `.` token, digits form an integer token and never a float literal, so `t.0.1` is `t` `.` `0` `.` `1` (the field `1` of the field `0` of `t`).
- The words `is not` and `not in`, when they appear together on one line (separated only by whitespace or comments), form the single operator tokens `is not` and `not in` (§7.3).
- `==` and `!=` are not tokens; the character pairs are ill-formed (equality is written `is` and `is not`, §7.6). Implementations should say so and may read them as `is` and `is not` to continue.

`;` may separate statements on one line (§2.8). It is never required.

### 2.7 Intrinsics and attributes

`@` immediately followed by an identifier forms an **intrinsic name** (`@size_of`). `#` immediately followed by an identifier forms an **attribute name** (`#packed`). Whitespace between `@`/`#` and the identifier is not permitted. The set of intrinsics and attributes is closed and listed in §20.

### 2.8 Newlines and statement termination

Newlines are significant inside braces `{ }` and at the top level, where they terminate statements and declarations. They are insignificant inside parentheses `( )` and brackets `[ ]`.

A newline token is **suppressed** (treated as whitespace) when any of the following holds:

1. The last token before it is a binary operator (any operator in §B.1 levels 3–11 that is binary, including `and`, `or`, `else`, `is`, `in`, `not in`, `is not`), an assignment operator, `,`, `::`, `:=`, `:`, `=>`, `->`, `\`, `.`, or an opening bracket of any kind.
2. The first token after it (skipping blank lines and comments) is `else`, `and`, `or`, or `.` followed by an identifier — **except** directly inside a `match` body (§8.6), where every line begins an arm and a newline always ends the preceding arm.
3. Its innermost enclosing bracket is `( )` or `[ ]`. Only the innermost bracket matters: newlines inside a `{ }` block are significant even when that block is itself inside parentheses (a function literal passed as an argument, for example).

Rule 2's exception means a method chain cannot continue an arm's expression onto a new line in a `match` body; wrap the arm body in `{ }` or `( )` for that. Inside those, normal rules resume.

A **`match` body** is the `{ }` that follows the scrutinee of a `match`: the first `{` after the `match` keyword at the same bracket depth. This is unambiguous because the head of a `match` (like the heads of `if`, `while`, `for`, `with` and `when`) cannot contain an unparenthesized `{` (§A.7, rule 4).

Multiple consecutive newlines are one newline. `;` is equivalent to a newline.

---

## 3. Programs, modules and top-level declarations

### 3.1 Packages and modules

- A **module** is a directory. Every `.gm` file directly inside the directory belongs to the module; subdirectories are separate modules. All files of a module share one top-level namespace, and the order of files and of declarations within them has no meaning.
- A **package** is a directory tree containing a `gamma.toml` manifest at its root. The manifest names the package and lists its dependencies, each as a name mapped to a source location and version. The manifest format is implementation-defined, except as stated here.
- The **root module** of a program is the module named on the compiler's command line. It must declare `main` (§3.7). A library has no `main`.

### 3.2 Imports

```
import_expr = 'import' string_lit
```

An import is an expression that may appear only as the initializer of a top-level `::` binding (possibly inside a top-level `when`, §3.6):

```
io   :: import "std/io"
util :: import "./util"
```

The binding's name is chosen by the importer and has no relation to the path. The import path is resolved as follows:

1. A path starting with `./` or `../` is relative to the importing module's directory. It may not leave the package root.
2. Otherwise, the first path segment names a package: `std` is the standard library; any other name must be a dependency listed in the importing package's `gamma.toml`. The remaining segments name a directory inside that package.

A module is identified by its resolved directory. Importing the same module twice (under any names) yields the same module.

The import graph must be acyclic; a cycle is a compile error naming the modules in it.

An imported module's `pub` declarations are accessed as `name.member`. A module binding is not a value: it cannot be passed, stored, or compared.

### 3.3 Visibility

Top-level declarations and struct fields are private to their module unless marked `pub`. A private name is accessible from any file in the same module, and from nowhere else.

- A `pub` declaration's signature must not mention a private type. (A private type may appear inside a `pub` struct's private fields.)
- A struct with any private field cannot be constructed, spread, or matched with a struct pattern outside its module. It can still be copied, passed and compared with `is`.
- Union variants are public whenever the union is.
- Effect operations are public whenever the effect is.

### 3.4 Top-level declarations

```
top_decl = [ doc ] { attribute } [ 'pub' ] binding
         | 'when' expr top_block { 'else' 'when' expr top_block } [ 'else' top_block ]
         | handler_decl
         | 'test' string_lit block
binding  = ident '::' expr
         | ident ':' type ':' expr
```

Every top-level binding is immutable and its initializer is evaluated at compile time (§17). The kinds of value a top-level binding can denote are: a constant of any type whose value is known at compile time, a function, a type (`struct`, `union`, an alias), an effect, a module (`import`), and a foreign block. The `#global` attribute (§19.3) introduces the only kind of mutable top-level storage.

Top-level declarations are mutually visible regardless of order. Their dependency graph — *A* depends on *B* if evaluating *A*'s initializer or computing *A*'s type requires *B*'s value — must be acyclic, except that functions may refer to each other (including recursively) and types may refer to each other through pointers. A cycle is a compile error naming every declaration in it.

### 3.5 Type aliases

A top-level binding whose initializer is a type expression that is not a `struct`, `union` or `effect` literal is an **alias**: `Bytes :: []u8` makes `Bytes` and `[]u8` the same type. `struct`, `union` and `effect` literals create a new nominal type each time they are written; binding one to a name gives that type its name. An alias may also be declared inside a function body (`Elem :: @type_of(xs[0])`, `Row :: [N]T`); it is visible to the rest of its block, like a local. Nominal types (`struct`, `union` and `effect` literals) are declared only at top level.

### 3.6 Conditional compilation: `when`

```
when target.os is .Linux {
    sys :: import "./sys_linux"
} else {
    sys :: import "./sys_other"
}
```

- The conditions of a `when` must be compile-time constant `bool` expressions (§17). Exactly one branch (or none, if there is no `else`) is **taken**.
- A top-level or struct-body `when` does not introduce a scope: the declarations in the taken branch are declared in the enclosing scope as if written there.
- Declarations in untaken branches are parsed but not otherwise checked; names in them need not resolve.
- `when` may appear at top level, among the fields of a `struct` body or the variants of a `union` body, and as a statement (§8.9).
- A `when` condition may not depend on a type parameter of an enclosing generic (§16.7).

The predeclared module `target` (no import needed) provides compile-time constants describing the compilation target: `target.os` (a union: `.Linux`, `.MacOS`, `.Windows`, `.FreeBSD`, `.Freestanding`, …), `target.arch` (`.X86_64`, `.Aarch64`, `.Riscv64`, `.Wasm32`, …), `target.endian` (`.Little`, `.Big`), `target.ptr_bits` (an untyped integer constant), and `target.mode` (`.Debug`, `.Checked`, `.Fast`).

### 3.7 The entry point

The root module of an executable declares `main` with one of these signatures, with any effect set drawn from the **ambient effects** `Alloc`, `Out`, `Err`, `In`, `Clock`, `Rand`, `Fs`, `Net`, `Env`, `Sys` (§12.9):

```
main :: fn() \ {...}
main :: fn() -> u8 \ {...}               // exit status
main :: fn() -> E!() \ {...}             // E is any type
```

The runtime installs the default handler for each ambient effect, calls `main`, and exits with status 0 (or the returned `u8`). If `main` returns an error, the runtime writes `error: ` and the error value to standard error, formatted as by `{}` (§21.6) and, for a union, preceded by its type name (`error: ConfigError.Missing`), and exits with status 1.

Command-line arguments and the environment are obtained through the `Env` effect.

### 3.8 Tests

```
test "parse rejects letters" {
    expect(parse_u16("12a") is !.BadDigit(at: 2))
}
```

A `test` declaration is compiled only when building tests. Its body is the body of an anonymous function of type `fn() -> E!()` for an inferred error type `E` (or `()` if it never fails), so `try` and `return` work in it; returning an error fails the test, reporting the error value. The body may perform any ambient effect, each handled by the **test handler** listed in §12.9. A test passes if its body completes; it fails if `expect` fails, if a trap occurs, or if the test `Alloc` handler detects a leak. Test names in a module must be distinct.

---

## 4. Bindings, scopes and mutability

### 4.1 Binding forms

| Form | Meaning |
| --- | --- |
| `x :: e` | immutable binding; type from `e` |
| `x: T : e` | immutable binding of type `T` |
| `x := e` | mutable variable; type from `e` |
| `x: T = e` | mutable variable of type `T` |
| `x: T = ---` | mutable variable of type `T`, uninitialized (§4.4) |
| `a, b := e` / `a, b :: e` | destructure a tuple (§4.5) |

Mutable variables (`:=`, `: T =`) may appear only inside function bodies, `test` bodies and handler clause bodies. At top level, only immutable bindings exist (plus `#global`, §19.3).

The type of `x` in `x := e` and `x :: e` is the type of `e`. If `e` is an untyped literal, that type is found by literal inference (§6.1), so `count := 0` takes its type from how `count` is used. Exception: an immutable binding whose initializer is an untyped constant expression is an untyped constant (§6.2), not a variable of an inferred type.

### 4.2 Immutability

An immutable binding's value never changes after initialization.

- It is not a place that can be assigned (§7.2).
- `&x` gives `^const T`, and slicing `x[i..j]` gives `[]const T`.
- Its fields and elements are likewise not assignable.

Immutability is shallow with respect to pointers: if `p :: &v` and `v` is mutable, `p` is `^T`, `p` cannot be rebound, but `p^` can be assigned.

An immutable local is evaluated at compile time if its initializer is a constant expression (§17.2); otherwise at run time when control reaches it.

### 4.3 Function parameters

Parameters are immutable bindings (§11.2). A parameter that the function wishes to modify is copied into a local: `n := n` is not allowed (shadowing, §4.6); write `m := n`.

### 4.4 Uninitialized storage: `---`

`x: T = ---` declares `x` without initializing it. It is permitted only when `T` is a **plain-data type**: an integer or float type, or an array, tuple or struct (including `#packed` structs) all of whose element and field types are plain-data, or a `#bits` struct whose fields are all `bool`, integers or such `#bits` structs. Reading an uninitialized object (or any part of one) yields an unspecified value; it is never undefined behavior.

Types that are not plain-data — `bool`, unions, optionals, results, pointers of every kind, slices, function pointers, `cont` — have invariants an arbitrary bit pattern could violate, so `---` is a compile error for them.

*Note:* Debug builds fill `---` storage with a recognizable pattern (`0xAA` bytes).

`---` may also be used as the value of a struct field in a constructor, or as an argument, with the same restriction on the field's or parameter's type: `Buf(len: 0, data: ---)`, `new(Page, ---)`. Inside a generic function, `---` at type `T` requires the constraint `Plain(T)` (§16.3).

### 4.5 Destructuring

`a, b := e` requires `e` to be a tuple with exactly as many elements; `a, b := 1, 2` is `a, b := (1, 2)`, as in multiple assignment (§4.7). Each name binds the corresponding element; `_` discards one. The names may not be annotated individually; annotate the whole binding with a tuple type instead: `q, r: (u32, u32) = divmod(17, 5)`.

Destructuring is the only multi-name binding form outside patterns (§9).

### 4.6 Scopes and shadowing

Scopes nest: the universe scope (predeclared names, §21.1) contains each module's top-level scope, which contains function bodies, which contain blocks.

- A local binding is in scope from the end of its declaration to the end of the enclosing block. A binding's own initializer cannot refer to it.
- A local binding may not have the same name as another local binding or parameter of the same function that is in scope (**no shadowing** within a function). Locals *may* reuse names declared at top level, imported module names, and predeclared names; the local hides them for its scope.
- Function literals and handler literals are part of their enclosing function for this rule.
- Top-level names in one module must be distinct, across all files of the module.
- `_` never introduces a binding and may be used any number of times.

### 4.7 Assignment

```
place = expr          place op= expr
```

Assignment is a statement, never an expression. The left side must be a mutable place (§7.2), or `_`, which evaluates and discards the right side (§7.14). The right side is converted to the place's type by the implicit coercions of §6.4.

Compound assignment `p op= e` is `p = p op e` with `p` evaluated once. It exists for `+ - * / % << >> & | ~` and the wrapping and saturating forms `+% -% *% <<% +| -| *| <<|`. There is no `+?=` (its result would be an optional).

The order of evaluation is: the place's sub-expressions (left to right), then the right side, then the store.

Multiple assignment `a, b = (b, a)` assigns a tuple to a list of places; all right-side values are computed before any store, so it swaps. The parentheses may be left out: with two or more places, a comma-separated list on the right is that tuple, so `a, b = b, a + b` is `a, b = (b, a + b)`.

---

## 5. Types

Types are compile-time values of type `type`. A type expression may appear wherever a type is expected (after `:` in a binding or parameter, after `->`, in a constructor head) and as an argument to a parameter of type `type`.

### 5.1 Predeclared types

| Name | Kind |
| --- | --- |
| `u1` … `u65535`, `i1` … `i65535` | fixed-width integers (§5.2) |
| `usize`, `isize` | pointer-sized integers |
| `f16`, `f32`, `f64` | IEEE 754 binary floating point |
| `bool` | `true` or `false` |
| `()` | the unit type: the empty tuple, with the single value `()` |
| `never` | the type with no values (§5.17) |
| `type` | the type of types; compile-time only |
| `rawptr` | an untyped, non-null address (§5.5) |
| `string` | alias for `[]const u8` |
| `cstring` | alias for `[^]const u8`, by convention NUL-terminated |
| `rune` | alias for `u32` |

`usize` and `isize` are distinct from every `uN`/`iN`, even the one with the same width on the target: code that is correct on a 64-bit target must not silently break on a 32-bit one.

### 5.2 Integer types

`uN` holds the integers 0 … 2ᴺ−1, `iN` holds −2ᴺ⁻¹ … 2ᴺ⁻¹−1, for 1 ≤ N ≤ 65535. Signed integers are two's complement. `usize` and `isize` have the target's pointer width.

**Storage.** An integer of width N occupies the smallest of 1, 2, 4, 8, 16 bytes that holds N bits; above 128 bits it occupies ⌈N/64⌉ × 8 bytes. Alignment equals the size up to 8 bytes; 16-byte integers have the alignment the target C ABI gives `__int128` (or 16 if none); wider integers have alignment 8. Byte order is the target's. The bits above N in the storage are **padding**: they have unspecified content, and loads ignore them. There is no implicit bit-packing of integers in arrays or ordinary structs; use `#bits` structs (§5.10.5) for that.

### 5.3 Floating-point types

`f16`, `f32` and `f64` are IEEE 754 binary16, binary32 and binary64. Arithmetic is correctly rounded with round-to-nearest-even; no contraction (fused multiply-add), reassociation or flush-to-zero is performed unless the program opts in per function with `#fast_math` (§20.2). NaN payloads are unspecified except as preserved by `bits` (§6.6).

### 5.4 `bool`

`bool` occupies one byte holding 0 (`false`) or 1 (`true`). Any other byte value in a `bool` object is undefined behavior to read (it can arise only through `bits`-style reinterpretation, `#c_union`, or foreign code).

### 5.5 Pointers

| Type | Points to | Arithmetic | Null |
| --- | --- | --- | --- |
| `^T` | one `T` | no | never |
| `^const T` | one `T`, read-only through this pointer | no | never |
| `[^]T` | the first of an unknown number of `T` | yes | never |
| `[^]const T` | as `[^]T`, read-only | yes | never |
| `rawptr` | unknown | no | never |

All pointer kinds are one machine address in size and alignment. No pointer type admits null; nullable pointers are spelled `?^T`, `?[^]T`, `?rawptr` and so on, and are guaranteed to have the same size as the pointer, with null represented as address 0 (§5.12).

**Dereference.** `p^` is the place `p` points to. Field access auto-dereferences exactly one level of pointer: if `p: ^S` then `p.f` means `p^.f`. Indexing a `[^]T` (`p[i]`) is the place at address `p + i·stride(T)` and is unchecked. `^T` and `^const T` cannot be indexed.

**Pointer arithmetic.** `[^]T` supports `p + n` and `p - n` for `n: usize` or `isize`, giving `[^]T`, and `p - q` giving `isize` (the element distance; both must point into the same allocation, §18.4). `[^]T` can be sliced: `p[a..b]` is `[]T`.

**Implicit coercions** (also listed in §6.4):

- `^T` → `^const T`; `[^]T` → `[^]const T`; `[]T` → `[]const T`.
- `^[N]T` → `[]T` and `^const [N]T` → `[]const T` (the slice covers the whole array).
- `^[N]T` → `[^]T`, and `^T` → `[^]T` (a pointer to one element is a pointer to the first of one).
- Any pointer kind → `rawptr`.
- Each of the above also applies inside `?`: `?^T` → `?^const T`, and so on.
- A sequence of the coercions in this list counts as one coercion: `^u8` → `[^]const u8` (adding `const` and changing kind) is allowed directly.

Converting back from `rawptr`, from `[^]T` to `^T`, from `const` to non-`const`, and between unrelated pointee types uses the explicit `p.cast(P)` (§6.6).

### 5.6 Arrays

`[N]T` is an array of exactly `N` elements of type `T`, where `N` is a compile-time `usize` constant (0 is allowed). Arrays are values: assignment, parameter passing and return copy the whole array. An array never converts implicitly to a pointer or slice; only a *pointer to* an array does (§5.5).

Layout: `N` elements at consecutive multiples of `stride(T)` (= `@size_of(T)`, which is always a multiple of `@align_of(T)`). Alignment is `@align_of(T)`.

`a.len` is the constant `N`.

### 5.7 Slices

`[]T` and `[]const T` are a pair `(ptr: [^]T, len: usize)` (`[^]const T` for the `const` form), laid out as a struct with those two fields in that order. The fields can be read but not assigned; build a slice by slicing (§7.9).

An empty slice still holds a non-null, suitably aligned pointer, which must not be dereferenced. `empty_slice(T)` (§21.1) is an empty `[]T`, for when there is nothing to slice.

### 5.8 Strings

`string` is exactly `[]const u8`. String literals are UTF-8 (unless escapes say otherwise), but `string` values carry no encoding guarantee. There is no character type; iterate with `bytes(s)` (yields `u8`), `runes(s)` (yields `rune`, replacing invalid sequences with U+FFFD) or the library's grapheme iterator.

`cstring` is exactly `[^]const u8`. A `c"..."` literal is a `cstring`. `s.cstr_len()` counts bytes up to the NUL. A string literal can also initialize a `[N]u8` whose length equals the literal's byte length.

### 5.9 Tuples

A tuple type `(T1, T2, …)` or `(a: T1, b: T2, …)` is structural: two tuple types are the same type iff they have the same element types in order and the same element names (an unnamed element matches only an unnamed one). A tuple type must be either fully named or fully unnamed. `(T)` with one element and no trailing comma is just a parenthesized `T`; a one-element tuple is `(T,)` or `(a: T)`. `()` is the empty tuple.

Elements are accessed by position `t.0`, `t.1` or, if named, by name `t.a`.

**Name-erasing coercion.** A tuple value converts implicitly to a tuple type with the same element types in the same order if either type is unnamed (`(q: u32, r: u32)` ↔ `(u32, u32)`). Between two differently named tuple types there is no implicit conversion.

Layout: like a struct with the same fields in the same order (§5.10.4).

### 5.10 Structs

```
Vec2 :: struct { x, y: f32 }

Window :: struct {
    pub title: string
    size: Vec2 = Vec2(x: 800, y: 600)
    open: bool = true
}
```

A `struct` literal creates a new nominal type. Fields are declared `name: Type`, optionally with `= default` where `default` is a compile-time constant expression of the field's type. `a, b: T` declares several fields of one type. Fields may be `pub` (§3.3). Field names must be distinct.

#### 5.10.1 Construction

`S(args)` constructs a struct. The arguments follow the call rules (§11.3), except that positional arguments are not allowed: named arguments fill fields by name, `..e` spreads a tuple or struct (§11.4), and fields not supplied take their defaults. A field without a default that is not supplied is a compile error. A field may be supplied as `---` if its type is plain-data (§4.4).

#### 5.10.2 Parameters

`struct(P1: T1, …) { … }` declares a generic struct (§16.4). Its parameters are compile-time values.

#### 5.10.3 Embedding

The first field of a struct may be declared `using name: S` where `S` is a struct type. Then:

- The fields of `S` (recursively through `S`'s own `using` field) are accessible directly: `p.pos` means `p.base.pos`. A name declared in the outer struct takes precedence over a promoted one.
- `^Outer` coerces to `^S` and `^const Outer` to `^const S`. The embedded field is at offset 0, so the coercion is free.
- `p.downcast_unchecked(Outer)` on a `^S` gives `^Outer` (and on `^const S`, `^const Outer`). It is never checked; if `p` does not point at the first field of an `Outer`, using the result is undefined behavior.

Only the first field can be `using`, so there is no ambiguity and no multiple embedding.

#### 5.10.4 Layout

Fields are laid out in declaration order. Each field is placed at the next offset that is a multiple of its alignment; the struct's alignment is the maximum field alignment (1 if empty), and its size is rounded up to a multiple of its alignment. This is the C layout algorithm, so a struct whose fields are all C-compatible types (§19.1) is layout-compatible with the corresponding C struct.

- `#packed` places every field at the next byte offset, with alignment 1. Fields are read and written directly, but a pointer or slice into a field of a `#packed` struct may not be formed when what it points to has alignment above 1 (`&h.count` with `count: u32` is a compile error; `&h.flags` with `flags: u8` is allowed): such a pointer could be misaligned.
- `#align(n)` on the struct raises its alignment to `n` (a power of two). On a field, it raises that field's alignment.
- An empty struct has size 0.

#### 5.10.5 Bit structs

```
Mode :: #bits(u16) struct {
    read, write, exec: bool    // 1 bit each
    kind: u4
    _: u9                      // explicit padding, unnamed
}
```

`#bits(U)` packs the fields into the unsigned integer type `U` starting at the least significant bit, in declaration order. Field types must be `bool` (1 bit), `uN`, `iN`, payload-less unions with an explicit tag type, or other `#bits` structs. The widths must sum to exactly the width of `U`; fields named `_` are padding that is zero when constructed. The struct has the size and alignment of `U`. Fields cannot have their address taken. A `#bits` struct is **plain** when every field is a `bool`, an integer or a plain `#bits` struct: then every bit pattern of `U` is a valid value, so the struct is plain-data (§5.19), converts to and from `U` by `U(m)` and `Mode(u)` (§6.5), and takes the bitwise operators (§7.4). A `#bits` struct with an enum field converts to `U` by `bits` (§6.6), and from `U` only by constructing it. A field of a nested `#bits` field is read and assigned like any field: `r.inner.flag = true`.

### 5.11 Unions

```
Shape :: union {
    Circle: f32
    Rect: (w: f32, h: f32)
    Empty
}
Dir :: union { N, E, S, W }                  // no payloads: an enum
Errno :: union(i32) { Ok = 0, Perm = 1, NoEnt = 2 }
```

A `union` literal creates a new nominal **tagged union**. Each variant has a distinct name and either a payload type or no payload. A union all of whose variants lack payloads is an **enum**.

- **Tag type.** By default the tag is the smallest of `u8`, `u16`, `u32` able to number the variants. `union(T)` sets it explicitly; `T` must be an integer type.
- **Tag values.** By default variants are numbered 0, 1, 2, … in declaration order. Explicit values `Name = c` are allowed only when a tag type is given; a variant without an explicit value takes the previous value plus one. Tag values must be distinct and fit `T`.
- **Construction.** `.Name(payload)` or `.Name` when the union type is known from context; `Shape.Circle(1.0)` otherwise. A tuple payload's elements are written directly as arguments: `.Rect(w: 2, h: 3)`.
- **Inspection.** `match` (§8.6), `if pattern := e` (§8.2) and `is` (§7.6). `u.tag()` returns the tag value as the tag type.
- **From a raw tag.** `Dir.from_tag(x)` gives `?Dir`, which is `null` unless `x` is the tag of a payload-less variant. It is the only way to turn an integer into an enum; there is no unchecked form.
- **Layout.** A tagged union is laid out as a struct `{ tag: TagType, payload: P }` where `P` is an untagged union of all payload types (size = maximum payload size, alignment = maximum payload alignment). An enum is just its tag.

#### 5.11.1 Untagged unions

`#c_union union { a: A, b: B }` is an untagged union with C layout: all fields at offset 0; size and alignment are the maximum. Every field must have a payload. It has no tag, cannot be matched or compared with `is`, and is constructed with exactly one named field: `U(a: x)`. Reading a field other than the one most recently written reinterprets its bytes (as `bits` does); doing so with a field type that is not plain-data is undefined behavior if the bytes are not a valid value of that type.

### 5.12 Optionals

`?T` holds either a value of `T` or `null`. `T` may be any type, including another optional: `??T` has two distinct "absent" values (§10.1).

**Layout.** If `T` is a non-null pointer kind (`^T`, `^const T`, `[^]T`, `[^]const T`, `rawptr`, `cstring`) or a function pointer `^fn(...)`, then `?T` has the layout of `T` with null as address 0. For every other `T`, the layout of `?T` is unspecified (implementations may use spare bits of `T`), and `?T` is not C-compatible.

### 5.13 Results

`E!T` holds either a success value of type `T` or an error value of type `E`. `E` may be any type, though it is conventionally a union. `!` in a type is right-associative and binds looser than prefix type operators: `E!?^T` is `E!(?(^T))`. Layout is unspecified.

### 5.14 Function types

There are two kinds of function type.

| Type | Kind | Captures | Effects |
| --- | --- | --- | --- |
| `fn(params) -> R \ {E}` | **function**: second-class (§11.6) | may capture locals by reference | `{E}` are handled by whoever calls it; any others it performs are handled where it was written |
| `^fn(params) -> R \ {E}` | **function pointer**: first-class | none | exactly `{E}`, handled by whoever calls it |

`-> R` may be omitted when `R` is `()`. `\ {E}` may be omitted when the set is empty. Parameter names in function types are optional and are not part of the type; parameter types, the return type, and the effect set are. A function pointer type also has a calling convention, `.C` unless written otherwise (§19.2); it too is part of the type.

A function pointer is one code address in size, C-compatible when its signature is (§19.1), and comparable with `is`. A top-level or nested named function (both capture nothing) converts to either kind. A function literal converts to a function pointer only if it captures nothing.

### 5.15 Effect, handler and continuation types

- An `effect` literal creates a new nominal **effect type** (§12.1). Effects are not types of values; they appear in effect sets and `with`.
- A `handler` literal has an anonymous second-class type (§12.5).
- `cont(T)` is the type of a one-shot continuation that expects a value of type `T` (§14.2).

### 5.16 Ranges

`a..b` has type `Range(T)` and `a..=b` has type `RangeIncl(T)`, where `T` is an integer type (or, for `Range`, any type with `Ord`). Both are predeclared structs with fields `lo` and `hi`. They are ordinary values: they can be stored, passed, iterated (§13.3), used with `in`, and used to slice. `a[r]` with `r: Range(usize)` (or `RangeIncl(usize)`) is `a[r.lo..r.hi]` (or `a[r.lo..=r.hi]`). When a range expression is used as a value rather than directly in `for`, `in` or slicing, its `T` comes from the expected range type if there is one, and otherwise from the bounds; untyped bounds that nothing else fixes take `usize` at that point (§6.1).

### 5.17 `never`

`never` has no values. An expression of type `never` does not complete normally: `return`, `break`, `continue`, `resume`, a call to a function returning `never`, a `while true` loop with no `break`, and a `match` all of whose arms are `never`. `never` coerces implicitly to every type.

### 5.18 Type identity

- Predeclared types, arrays, slices, pointers, optionals, results, function types and tuples are **structural**: two such types are the same iff they are built the same way from the same types (and, for arrays, the same length; for tuples, the same names).
- `struct`, `union` and `effect` literals are **nominal**: each literal is a distinct type, even if two are written identically.
- An instantiation of a generic type `G(a1, …)` is identified by `G` and its arguments: type arguments compare by identity, value arguments compare with `is` — except function arguments, which compare by the identity of the named function they denote (§16.4).
- An alias is the same type as what it names.

### 5.19 Plain-data types

A type is **plain-data** if every bit pattern of its size is a valid value: integer and float types; plain `#bits` structs (§5.10.5); arrays, tuples and structs whose elements and fields are all plain-data. (Padding bytes are ignored.) Plain-data types are the ones that may be initialized with `---` (§4.4), reinterpreted with `bits` (§6.6), and read from a `#c_union` without restriction.

---

## 6. Literals, constants and conversions

### 6.1 Typing literals

An integer, float or character literal without a suffix is **untyped**: it denotes an exact number, and its type is determined by **literal inference** within the enclosing function body (or the enclosing top-level declaration).

**Literal inference.** Each untyped literal *L* is given a fresh type variable α\_L, restricted to integer types (for integer and character literals) or to numeric types (for float literals: float types only). Type variables are unified with each other and with concrete types by the ordinary typing rules, wherever a type equality is required:

- a variable binding `x := L` gives `x` the type α\_L, so later uses of `x` constrain it (an immutable `x :: L` is an untyped constant instead, §6.2);
- the two operands of a binary operator (other than a shift), the two bounds of a range, both sides of an assignment and of a comparison, the arms of an `if`/`match` expression, and the operand of `in` and its element type must have equal types;
- a coercion site (§6.4) whose target type is known fixes the variable to that type (no widening is inferred: the variable becomes exactly the target type). If the target is `?T` or `E!T` (possibly nested), the variable becomes `T`, the type that the wrapping coercion starts from;
- the amount operand of a shift (§7.4.3), an index (§7.9) and a slicing bound need not equal any other type; a variable used there is restricted to unsigned integer types.

Inference considers the whole function body at once; the order of statements does not matter. After unification, every variable that is still unresolved takes its **fallback**: `usize` if it was created for, or unified with, a range bound, an array length, an index or a slicing bound; `u32` if it was used as a shift amount; otherwise `i32` for an integer variable and `f64` for a float variable. Then each literal is checked against its final type:

- An integer literal must be exactly representable in its type. If the type is a float type, the integer must convert exactly.
- A float literal at a float type is rounded to nearest-even; it is a compile error if the result is infinite. A float literal can never have an integer type (`3.0` cannot be an `i32`).

A suffix (`200u8`, `1.5f32`) fixes the type directly.

```
count := 0                     // usize: fixed by the comparison below
while count < items.len { count += 1 }
total := 0                     // i32: nothing else constrains it
scale := 1.5                   // f64
for i in 0..10 { ... }         // usize, unless i is used at another type
mask := 0xff; b: u8 = ...; if b & mask is 0 { ... }   // u8
```

*Rationale:* this is unification restricted to literals. It keeps `count := 0` working in every common case while never converting between types implicitly: the variable gets one type, and a literal that doesn't fit it is an error, not a wrap.

### 6.2 Untyped constants

An immutable binding without a type annotation whose initializer is an **untyped constant expression** — built only from untyped literals, other untyped constants, the arithmetic operators `+ - * / % << >> & | ~` and unary `-`, comparisons, and parentheses — is an **untyped constant**:

```
PAGE :: 4096
MASK :: PAGE - 1
PI   :: 3.14159265358979323846
TAU  :: 2 * PI
```

Its value is computed exactly (integers are unbounded; float-valued expressions are exact rationals; `/` on two integer values is truncating integer division; mixing an integer and a float value makes a float value). A float value also has a sign when it is zero, as in IEEE arithmetic: unary `-` flips it, so `-0.0` is negative zero; a product or quotient whose exact value is zero is negative when exactly one operand is negative (counting negative zero); a sum or difference whose exact value is zero is negative zero only when it is `-0.0 + -0.0` or `-0.0 - 0.0`. Comparisons treat both zeros as equal. Each *use* of the constant is treated as if its exact value were written there as a literal, typed by literal inference (§6.1). `x: u8 = PAGE` is a compile error (4096 doesn't fit).

Untyped constants are permitted at top level and as locals. A typed constant is written `MAX: usize : 4096`.

### 6.3 Typed constants and the meaning of `::`

`x :: e` where `e` has a type is a typed immutable binding (§4.1). At top level its initializer is evaluated at compile time; locally it is evaluated at compile time if it can be (§17.2).

### 6.4 Implicit coercions

Gamma has no implicit conversions between different numeric types in expressions. Binary operators require operands of the identical type (§7.4). The following coercions happen implicitly at **coercion sites** only: initialization of a binding, assignment, argument passing (including named and spread arguments), return, field values in constructors, `else` fallbacks, the operand of a `break` that supplies the value of a `with` or `for` (§12.7, §13.2), and arms of `if`/`match` expressions joined to a known result type.

| From | To | Rule |
| --- | --- | --- |
| `uN` | `uM`, M ≥ N | widening |
| `uN` | `iM`, M ≥ N + 1 | widening |
| `iN` | `iM`, M ≥ N | widening |
| `uN`, N ≤ 32 | `usize` | see note |
| `uN`, N ≤ 31; `iN`, N ≤ 32 | `isize` | see note |
| `fN` | `fM`, M > N | exact |
| `T` | `?T` | wrap present |
| `T` | `E!T` | wrap success |
| `never` | any | |
| pointer forms | see §5.5 | const-adding, array-to-slice, to `rawptr` |
| `^Outer` | `^S` | embedding (§5.10.3) |
| tuple | tuple | name erasure (§5.9) |
| named or nested function | function, function pointer | §5.14 |
| `^fn(...) \ {A}` | `^fn(...) \ {A, B}` | effect subsumption (§12.3) |

Note on `usize` and `isize`: Gamma targets have a pointer width of at least 32 bits, so these widenings are lossless on every target. `usize` and `isize` never widen implicitly to a fixed-width type, and wider fixed-width types never widen to them, because whether that is lossless depends on the target.

Coercions do not chain, with one exception: any number of wrap-present / wrap-success steps may follow at most one other coercion (`u8` → `?u16` and `T` → `??T` are allowed).

### 6.5 Lossless conversion: `T(x)`

`T(x)` with a single positional argument whose type is not a tuple whose fields fill `T` is a **conversion**. When `x` coerces to `T` implicitly (§6.4), the conversion is that coercion: `string(buf)` with `buf: []u8`. Otherwise it is permitted exactly when the conversion is lossless for every value of the source type, and is a compile error otherwise:

| `x: S` | `T(x)` permitted when |
| --- | --- |
| integer | `T` is an integer type that contains every value of `S` |
| integer | `T` is a float type whose significand holds every value of `S` (`u24`/`i25` → `f32`, `u53`/`i54` → `f64`, `u11`/`i12` → `f16`) |
| float | `T` is a wider float type |
| `bool` | `T` is an integer type (`true` → 1, `false` → 0) |
| enum (§5.11) | `T` is its tag type, or an integer type its tag type widens to |
| plain `#bits(U)` struct (§5.10.5) | `T` is `U` |
| `U` integer | `T` is a plain `#bits(U)` struct (§5.10.5: every bit pattern is valid) |

For `usize` and `isize` the rule is applied against every supported target (pointer width ≥ 32): `usize(x)` is permitted for `x: uN` with N ≤ 32, and so on. Converting between `usize`/`isize` and wider fixed-width types uses the methods of §6.6.

When the type of `x` is still a literal type variable (§6.1), `T(x)` with an integer type `T` fixes the variable to `T`, like a coercion site, and the conversion does nothing. With a float type `T`, only an expression made of literals takes `T` (`f64(1 + 2)` is the `f64` 3.0); a variable keeps its own integer type and is converted from it, so `for i in 0..n { f64(i) }` converts from `usize` and needs `i.round(f64)`.

### 6.6 Lossy and reinterpreting conversions

Every conversion that can lose information is a named builtin method. `T` is a type argument.

| Method | On | Meaning |
| --- | --- | --- |
| `x.trunc(T)` | integer → integer | keep the low bits (two's-complement truncation); always defined |
| `x.trunc(T)` | float → integer | round toward zero; **checked**: the result must fit `T` and `x` must not be NaN |
| `x.round(T)` | float → integer | round to nearest, ties to even; checked as above |
| `x.floor(T)`, `x.ceil(T)` | float → integer | checked as above |
| `x.round(T)` | integer → float, float → narrower float | round to nearest-even; overflow gives ±∞ |
| `x.checked(T)` | integer → integer | `?T`: `null` if the value doesn't fit |
| `x.checked(T)` | float → integer | `?T`: `null` if not integral or doesn't fit |
| `x.sat(T)` | integer → integer, float → integer | clamp to `T`'s range; NaN → 0 |
| `x.bits(T)` | plain-data, `bool` or `#bits` struct → plain-data | reinterpret the bytes; `@size_of` must match |
| `p.cast(P)` | pointer → pointer | change pointee type, constness, or pointer kind; checked: the address must be suitably aligned for `P` |
| `p.addr()` | pointer → `usize` | the address |
| `n.to_ptr(P)` | `usize` → `?P` | the pointer with address `n`; `null` if `n` is 0 |

`bits` accepts `bool` and any `#bits` struct as a source but not as a target, and accepts no non-plain-data target, so it can never create an invalid value. `cast` between `?P` types maps null to null.

### 6.7 Conversions that do not exist

There is no implicit or `T(x)` conversion from integer to `bool` (write `x is not 0`), from `bool` to anything but integers, from integers to unions (use `from_tag`), from slices to pointers (use `.ptr`), from arrays to slices (take a pointer or slice explicitly), or between distinct struct types (construct one).

---

## 7. Expressions

### 7.1 Evaluation order

Operands, arguments, constructor fields, array elements and tuple elements are evaluated **left to right**, each completely (including its side effects) before the next begins. The exceptions are the short-circuit operators `and`, `or` and `else`, and the conditional constructs. The callee of a call is evaluated before its arguments. For an assignment, see §4.7.

### 7.2 Places

A **place** is an expression that denotes a storage location: a variable or parameter name, `p^`, `e.f` where `e` is a place or a pointer (auto-deref), `a[i]` where `a` is a place of array type or any slice or many-pointer, and a parenthesized place.

A place is **mutable** if it is rooted in a mutable variable (through fields and array elements), or is reached through a non-`const` pointer, slice or many-pointer (`p^`, `p.f`, `s[i]`, `m[i]`). Assignment, compound assignment and `&` producing a non-`const` pointer require a mutable place. `&` of an immutable place gives a `const` pointer; `&` of a non-place expression is a compile error (no implicit temporaries are materialized).

### 7.3 Operator summary

Precedence, tightest first. Operators on one level associate left-to-right unless stated.

| Level | Operators | Notes |
| --- | --- | --- |
| 1 | `e?` `e!` `e^` `e(args)` `e[i]` `e[i..j]` `e.name` `e.0` | postfix |
| 2 | `-e` `~e` `&e` `!e` `try e` | prefix; `!e` makes an error value (§10.2) |
| 3 | `*` `/` `%` and `*% *| *?` | |
| 4 | `+` `-` and `+% +| +? -% -| -?` | |
| 5 | `<<` `>>` and `<<% <<| <<?` | non-associative; see §7.4.3 |
| 6 | `&` `|` `~` | bitwise; see §7.4.4 |
| 7 | `..` `..=` | non-associative |
| 8 | `is` `is not` `in` `not in` `<` `<=` `>` `>=` | non-associative |
| 9 | `not` | prefix |
| 10 | `and`, `or` | one kind per chain (mixing requires parentheses) |
| 11 | `e else d`, `e else x => d` | right-associative |

**Mixing `and` and `or` requires parentheses**, as does mixing different bitwise operators and combining shifts with arithmetic. These are the C precedences most often misread; Gamma makes them compile errors instead of choosing.

**Binary `~` is exclusive-or.** It is the one operator that does not keep its C meaning, because `^` is the pointer sigil. Unary `~` is complement, as in C.

**Two-word operators.** `is not` and `not in` are single operators. After `is`, the word `not` always begins `is not`; before `in`, it always ends `not in`. They may not be split across lines.

**Type operators.** In type expressions, the prefix forms `?T`, `^T`, `^const T`, `[]T`, `[]const T`, `[N]T`, `[^]T`, `[^]const T` apply to the entire type that follows (so `?^T` is `?(^T)` and `[]?u8` is a slice of `?u8`). The infix `E!T` is right-associative and looser than every prefix form: `E!?^T` is `E!(?(^T))`. A type expression used as a value argument (`list(?u8)`) parses the same way.

**`else` forms.** After `else`: the keyword `if` continues an `if` chain; `{` begins an `else` block of an `if`, or the block fallback of `x else { … }` (§10.4); an identifier immediately followed by `=>` begins an error capture (§10.4); anything else is the fallback operand of `e else d`.

### 7.4 Arithmetic

#### 7.4.1 Operand types

Binary arithmetic and bitwise operators require both operands to have the same type (an untyped literal or constant takes the other operand's type, §6.1). The result has that type. Integer operators apply to integer types; `+ - * /` and unary `-` also apply to float types; `%` does not apply to floats (use `math.rem`). Unary `-` applies only to signed integers and floats. Bitwise operators apply to unsigned and signed integers (as bit patterns) and to plain `#bits` structs (§5.10.5; on the underlying integer, so fieldwise).

A unary `-` applied directly to an untyped literal is part of the literal for typing purposes: `-128i8` and `x: i8 = -128` are valid.

#### 7.4.2 Overflow flavors

For `+ - * <<` on integers:

| Form | Result type | On overflow |
| --- | --- | --- |
| `a + b` | `T` | **checked** (§18): traps; in Fast builds, unspecified value |
| `a +% b` | `T` | wraps modulo 2ᴺ |
| `a +| b` | `T` | saturates to `T`'s minimum or maximum |
| `a +? b` | `?T` | `null` |

Overflow means the mathematical result is not representable in `T`. For `<<`, overflow means any bit shifted out differs from the result's sign bit (for signed) or is 1 (for unsigned); `<<%` discards shifted-out bits; `<<|` saturates; `<<?` gives `null`.

Unary `-x` overflows only for the minimum signed value; it is checked like `+`.

#### 7.4.3 Shifts

`a << n` and `a >> n` require `n` to be an unsigned integer type of any width (it need not match `a`'s type). The shift amount must be less than the width of `a`'s type; this is **checked** for all flavors (in Fast builds a too-large shift gives an unspecified value). `>>` on signed integers is arithmetic (sign-filling); on unsigned, logical. `>>` never overflows and has no flavors.

A shift may not be an operand of `*`, `/`, `%`, `+`, `-` or another shift without parentheses, and none of those may be an operand of a shift without parentheses: `1 << n - 1` is a compile error; write `(1 << n) - 1` or `1 << (n - 1)`.

#### 7.4.4 Bitwise

`a & b` is and, `a | b` is or, `a ~ b` is exclusive-or, `~a` is complement. A chain may use only one of `&`, `|`, `~`; `a & b | c` is a compile error.

#### 7.4.5 Division

`a / b` and `a % b` on integers truncate toward zero; `a % b` has the sign of `a`, and `(a / b) * b + a % b is a`. Division by zero, and `MIN / -1` / `MIN % -1` for signed types, **always trap, in every build mode**. There are no flavored division operators.

Float division follows IEEE 754 (division by zero gives ±∞ or NaN; it does not trap).

#### 7.4.6 Elementwise array arithmetic

If `a` and `b` are arrays `[N]T` of a numeric `T`, `a op b` for `op` in `+ - * /` (and the overflow flavors) applies `op` to each pair of elements and gives `[N]T`; `-a` negates each element. The `?` flavors are not available on arrays. There is no implicit broadcasting of scalars; use `[N]T(fill: x)`.

### 7.5 Comparison

`< <= > >=` apply to two operands of the same integer type, the same float type (IEEE ordered comparison: false if either is NaN), the same enum type (payload-less union; compared by tag value), the same many-pointer type (which must point into the same allocation, §18.4), or the same tuple, array or slice type whose element types are all integers, floats, enums, or (recursively) such tuples, arrays and slices.

Tuples, arrays and slices (including `string`) compare **lexicographically**: find the first index `i` at which element `i` of `a` is not `is`-equal to element `i` of `b`; then `a < b` is element `i` of `a` `<` element `i` of `b`. If there is no such position, the shorter is less (tuples and arrays of one type have equal length, so they are equal). `a <= b` is `a < b or a is b`; `>` and `>=` are the mirror images. Strings therefore compare bytewise. Structs are deliberately **not** ordered: their field order records layout, not priority. To order structs, order a tuple of keys (`sort_by_key`, §21.3).

Comparisons do not chain: `a < b < c` is a compile error.

### 7.6 Equality: `is`

`a is b` and `a is not b` give `bool`. `is` is **value equality**, defined by type:

| Type | `a is b` when |
| --- | --- |
| integers, `bool` | same value |
| floats | IEEE equality: `nan is nan` is false, `0.0 is -0.0` is true |
| pointers of every kind, function pointers | same address |
| arrays, tuples, structs | every element/field pair is `is`-equal (padding is ignored) |
| slices (including `string`) | same length and every element pair is `is`-equal: **content**, not address |
| tagged unions | same tag, and payloads are `is`-equal |
| `?T` | both `null`, or both present and the values are `is`-equal |
| `E!T` | both errors with `is`-equal error values, or both successes with `is`-equal values |
| `type` (compile time) | same type |

`is` is not defined for second-class function values, handlers, continuations, `#c_union`s, or any type containing one. Comparing a struct with private fields is allowed everywhere.

**Pattern tests.** If the right operand of `is` is a *pattern form* that is not itself a value — a variant with `_` in its payload (`.Circle(_)`), a variant name whose payload is omitted (`.Circle`), `null` for optionals (which is also a value), or an error pattern `!p` where `p` is `_` or one of these — then `is` tests whether the left operand matches the pattern. A pattern on the right of `is` may not bind names; `s is .Circle(r)` is a compile error (use `if .Circle(r) := s`).

**`is not`** is one operator. Because `not` binds looser than comparisons, an operand of `is` cannot start with `not`; `a is (not b)` needs parentheses.

### 7.7 Membership: `in`

`x in c` and `x not in c` give `bool`:

| `c` | `x in c` when |
| --- | --- |
| `Range(T)` `lo..hi` | `lo <= x and x < hi` |
| `RangeIncl(T)` `lo..=hi` | `lo <= x and x <= hi` |
| array, slice | some element `e` has `e is x` |
| tuple whose elements all have `x`'s type | some element `e` has `e is x` |

The tuple form is written inline: `c in ('\n', '\r')`, `d in (.N, .S)`.

### 7.8 Logical operators

`and`, `or` and `not` take and give `bool`. `and` and `or` short-circuit. Mixing `and` and `or` in one expression requires parentheses. There are no truthy values: conditions must be `bool` (or binding conditions, §8.2).

### 7.9 Indexing and slicing

- `a[i]` where `a` is an array, a pointer to an array, or a slice: `i` must have an unsigned integer type. The access is **checked**: `i < len`.
- `p[i]` where `p` is a many-pointer: unchecked.
- `a[lo..hi]`, `a[lo..=hi]`, `a[lo..]`, `a[..hi]`, `a[..]` give a slice. Open-ended ranges are allowed only here. `a` must be a place of array type, a pointer to an array, a slice, or a many-pointer (for which both bounds are required). Checked: `lo <= hi <= len`. The result is `[]const T` if `a` is an immutable place or `const` view, `[]T` otherwise.
- `a.get(i)` on arrays and slices gives `?T` (the element value) without a check failure; `a.get_ptr(i)` gives `?^T` (or `?^const T`).

Indexing a `string` gives a `u8`.

### 7.10 Field access and method calls

`e.name` is resolved in this order:

1. If `e` has (after at most one auto-dereference of `^S`/`^const S`) a struct or tuple type with a field `name` — including a field promoted by `using` — it is that field.
2. If `e` is a slice, array, integer, float, pointer, union, optional, result, or range value and `name` is a builtin method of that type (§21.3), it is that method.
3. Otherwise, `e.name(args)` is a **method call**: let `S` be `e`'s type with at most one pointer level removed, and `M` the module that declares `S` (for a generic instantiation, the module declaring the generic). If `M` has a top-level function `name` visible at the call site, the call means `name(e', args)`, where `e'` is `e` adjusted by the receiver rules below. Otherwise it is a compile error.

A struct type may not have both a field and a method candidate with the same name: declaring a function `f` in `S`'s module whose first run-time parameter has type `S`, `^S` or `^const S` is a compile error if `S` has a field `f`.

**Receiver rules.** The receiver fills the function's first run-time parameter: compile-time parameters (types and `::` parameters) are skipped and inferred (§16.2, §16.5). If that parameter has type `^T` or `^const T` and `e` is a place of type `T`, `&e` is passed (a mutable place is required for `^T`). If the parameter has type `T` and `e` has type `^T` or `^const T`, `e^` is passed. If `e` is not a place and the parameter is `^const T`, a temporary holding `e`'s value is created for the duration of the call and its address passed. If the parameter is `^T` (mutable), `e` must be a mutable place: calling a mutating method on a temporary is a compile error, since the mutation would be lost. No other adjustment is made.

Methods cannot be added to types declared in other modules; write a free function instead.

### 7.11 Calls and construction

`f(args)` calls `f` if `f` is a function, function pointer, or second-class function value. If `f` denotes a **type**, `f(args)` is construction or conversion:

- A struct type: construction (§5.10.1). Struct construction takes **named** arguments and spreads only; positional arguments are not allowed, so reordering fields can never silently change meaning, and `S(x)` with one positional argument is always a conversion.
- A union variant `.V(args)` / `U.V(args)`: construction with the payload.
- An array type `[N]T(e1, …, eN)`: exactly `N` elements; `[_]T(…)` infers `N`; `[N]T(fill: x)` repeats `x`; `[N]T(..t)` spreads a tuple of `N` elements.
- A tuple type: construction from positional/named arguments.
- An optional type `?T(x)`: an optional holding `x` (coerced to `T`), or with `null`, the absent value of `?T`. This is how inner levels of nested optionals are written: `??i32` holds `?i32(null)`.
- Any other type with exactly one argument: conversion (§6.5).

Argument binding rules are in §11.3.

### 7.12 Conditional expressions

`if` and `match` are expressions (§8.2, §8.6). Used as expressions, each branch's block yields its value: a block's value is the value of its final statement if that statement is an expression; otherwise `()`. All branches must have the same type after coercion to the expected type, if any; otherwise the type of the first branch that is not `never`. An `if` without `else` has type `()`.

### 7.13 Function and handler literals

`fn(params) -> R \ {E} { body }`, `fn(params) -> R => expr`, and `handler Eff { clauses }` are expressions. Their restrictions are in §11.6 and §12.5.

### 7.14 Expression statements

An expression used as a statement must have type `()` or `never`. To discard a value deliberately, assign it to `_`: `_ = f()`. Discarding a result (`E!T`) or optional this way is permitted and visible.

### 7.15 Constant expressions

A **constant expression** is an expression whose value is computed at compile time: literals, untyped and typed constants, compile-time parameters, `type` values, calls to functions with empty effect sets whose arguments are constant expressions (§17), operators on constant expressions, constructors whose arguments are constant expressions, and intrinsics marked constant in §20.

---

## 8. Statements and control flow

### 8.1 Blocks and statements

```
block     = '{' { statement terminator } [ statement ] '}'
statement = binding | assignment | expr | defer_stmt | when_stmt
          | 'return' [ expr ] | 'break' [ label ] [ expr ] | 'continue' [ label ]
```

`return`, `break`, `continue` and `resume` are expressions of type `never` (§5.17), so they can appear as operands, most usefully after `else`: `n = n *? 10 else return !.TooBig`. An operand after `return`/`break` extends to the end of the enclosing expression.

A block is a sequence of statements, each ended by a newline or `;` (§2.8). A block introduces a scope. The value of a block used as an expression is defined in §7.12.

### 8.2 `if`

```
if_expr   = 'if' cond block { 'else' 'if' cond block } [ 'else' ( block | capture ) ]
cond      = cond_part { 'and' cond_part }
cond_part = pattern ':=' expr | expr
capture   = ident '=>' ( block | expr )
```

The condition is a `bool` expression, or a **binding condition** `pattern := e`, or several of these joined by `and`.

- A binding condition succeeds if `e`'s value matches `pattern` (§9), binding the pattern's names. Refutable patterns are the point: `if user := find(id)` (an irrefutable binding pattern on an optional matches only a present value, §9.3), `if .Circle(r) := shape`, `if f := open(path)` on a result (matches only success).
- In a chain `c1 and c2 and …`, parts are evaluated left to right and the chain stops at the first failure. Names bound by a part are in scope in the later parts and in the `then` block — not in `else` branches.
- Binding conditions may not be combined with `or` or negated with `not`.
- **Error capture.** If the *last* part of an `if` condition is a binding condition on a result, the final `else` may be `else e => …`, which binds the error value to `e`. This is permitted only when the `if` has exactly one condition part.

`if` is an expression (§7.12).

### 8.3 `while`

```
while_stmt = [ label ':' ] 'while' cond block
```

Evaluates `cond` before each iteration and runs the block while it succeeds. The condition may be a binding condition, as for `if`: `while line := reader.next() { … }`. `while true { … }` is an infinite loop; if it contains no `break` targeting it, its type is `never`.

### 8.4 `for`

```
for_stmt = [ label ':' ] 'for' pattern { ',' pattern } 'in' expr block   // also an expression, §13.2
```

`for` runs its block once for each value **yielded** by the expression. It is defined in terms of effects in §13. The pattern must be irrefutable (§9.4) and introduces fresh names; naming an existing local is a compile error (no shadowing, §4.6). Use `_` to ignore the value.

```
for i in 0..n { … }                  // half-open; 0..=n is inclusive
for x in xs { … }                    // xs: an array place or a slice
for i, x in enumerate(xs) { … }      // tuple pattern without parentheses
```

In a `for` head, a comma-separated list of patterns `p1, p2, …` is a tuple pattern `(p1, p2, …)`. (In `match` arms, by contrast, commas separate alternatives, §8.6.)

### 8.5 `break`, `continue` and labels

```
outer: for row in 0..h {
    for col in 0..w {
        if grid[row][col] is target { break outer }
    }
}
```

A **label** `name:` may precede `while`, `for` and `with` (and nothing else). Labels share the namespace of local bindings (§4.6).

- `break` and `continue` without a label target the innermost enclosing `while` or `for`, or — directly inside a handler clause and not inside a loop in that clause — the `with` that installed the handler (§12.7).
- `break L` / `continue L` target the labeled construct, which must lexically enclose the statement.
- `continue` is permitted only for loops (including `for`).
- `break v` / `break L v` supply the value of a `with` expression (§12.7) or a `for` loop (§13.2). `while` loops have no value, so `break` targeting one takes no operand.

When `break` or `continue` leaves scopes, their `defer` statements run (§8.8).

### 8.6 `match`

```
match_expr = 'match' expr '{' { arm terminator } '}'
arm        = pattern { ',' pattern } [ 'if' expr ] '=>' ( expr | block )
```

```
area :: fn(s: Shape) -> f32 => match s {
    .Circle(r)            => PI * r * r
    .Rect(w, h) if w is h => w * w
    .Rect(w, h)           => w * h
    .Empty                => 0
}
```

- The scrutinee is evaluated once. Arms are tried in order; the first whose pattern matches and whose guard (if any) is `true` is chosen, and its body is evaluated. There is no fallthrough.
- An arm with several patterns `p1, p2 => …` matches if any does; all alternatives must bind the same names with the same types.
- **Exhaustiveness.** It is a compile error if some value of the scrutinee's type matches no arm, ignoring guards (an arm with a guard counts as matching nothing for exhaustiveness). It is also a compile error if an arm can never be chosen because earlier unguarded arms cover it.
- Each arm begins on a new line (or after `;`). A newline at the end of an arm always ends it, even if the next line starts with `.` (§2.8).
- `match` is an expression (§7.12).

### 8.7 `return`

`return e` ends the current function with value `e`, which is coerced to the return type. `return` alone is `return ()`. Inside a function literal, `return` returns from the literal. Inside a handler literal's clause, `return` returns from the function that lexically contains the handler literal (§12.7). `return` has type `never`.

### 8.8 `defer` and `errdefer`

```
defer_stmt = ( 'defer' | 'errdefer' ) ( block | expr | assignment )
```

`defer S` registers `S` to run when control leaves the enclosing block, by any means except a trap. Deferred statements of one block run in the reverse order of registration. A `defer` that was never reached is not registered.

`errdefer S` registers `S` to run only when the enclosing block is left **abnormally**:

- by a `return` of an error value (`return !e`, or `try` propagating an error) from the enclosing function, through this block; or
- by the block's frame being **unwound**: an `esc` exit (§12.7) or a cancellation (§14.5) that passes through the function containing the block, on its way to a `with` in some *caller*.

A block left by falling off its end, by `break`/`continue`, by `return` of a non-error value (including `null`), or by a `break` from a handler clause whose installing `with` is in the *same* function, does not run `errdefer` statements.

A deferred statement:

- runs with the bindings in scope where it was written, using their values at the time it runs;
- may not contain `return`, `try`, `resume`, or a `break`/`continue` that leaves the deferred statement;
- may not perform `esc` or `ctl` operations that are not handled inside the deferred statement itself (that is, it may only *perform* `fn` operations; a `for` loop inside a `defer` is fine because the loop handles its own `Yield`).

*Rationale:* cleanup must run to completion and cannot be interrupted, suspended, or turned into a second exit.

### 8.9 Statement-level `when`

`when c { … } else { … }` inside a function is like `if` with a compile-time condition: only the taken branch is type-checked and compiled; untaken branches are parsed only. Each branch is a block with its own scope. It is not an expression.

### 8.10 Unreachable code

A statement that follows a statement of type `never` in the same block is a compile error ("unreachable code"). This catches code after `return`, `break`, `trap(...)`.

---

## 9. Patterns

### 9.1 Syntax

```
pattern = '_'
        | ident                                   // binding
        | literal | '-' literal                   // integer, char, string, bool literal
        | qualified_const                         // a constant reached through a module: m.MAX
        | range_pattern                           // lo..hi, lo..=hi
        | 'null'
        | '!' pattern                             // error of a result
        | '.' ident [ '(' pattern_args ')' ]      // union variant, type from scrutinee
        | type_name '.' ident [ '(' pattern_args ')' ]
        | type_name '(' field_pattern % ',' ')'   // struct
        | '(' pattern_args ')'                    // tuple
pattern_args  = pattern % ','  |  field_pattern % ','
field_pattern = ident ':' pattern | ident        // `x` alone means `x: x`
```

### 9.2 Meaning

A pattern is matched against a value of a known type, the **scrutinee type**.

| Pattern | Matches | Binds |
| --- | --- | --- |
| `_` | anything | nothing |
| `x` | anything (but see §9.3) | `x` to the value |
| literal, constant | values `is`-equal to it | nothing |
| `lo..hi`, `lo..=hi` | integers (or chars) in the range | nothing |
| `null` | an absent optional | nothing |
| `!p` | a result holding an error that matches `p` | what `p` binds |
| `.V` | union value with variant `V` and no payload | nothing |
| `.V(p…)` | union value with variant `V` whose payload matches | what the sub-patterns bind |
| `S(f: p, …)` | struct value whose listed fields match; unlisted fields are ignored | what the sub-patterns bind |
| `(p1, p2, …)` | tuple whose elements match | what the sub-patterns bind |

- Literal patterns are permitted for integer, `bool`, and `string` scrutinees (strings compare by content). Float literal patterns are not permitted.
- Range patterns are permitted for integer scrutinees. Bounds must be constant, `lo <= hi` (for `..`, `lo < hi`).
- In a variant pattern with a tuple payload, the payload's elements are written directly: `.Rect(w, h)` or `.Rect(w: a, h: b)`. A single-element payload `.Circle(r)`.
- In a struct pattern, the struct's fields must be visible (§3.3).
- A binding pattern binds an immutable name (a copy of the matched part).
- **Bare names always bind.** A pattern consisting of a bare identifier is always a binding, never a comparison. To prevent the classic mistake of a binding silently capturing everything, a bare identifier pattern that names a visible constant value (a top-level or local `::` binding of a number, string, `bool` or enum — not a function, type, module or effect) is a compile error: compare with a guard (`x if x is PAGE => …`) or, for a constant of another module, with a qualified pattern (`limits.MAX => …`).
- A qualified pattern `a.b` or `a.b(…)` is a union variant if `a` names a union type, and a constant if `a` names a module and `b` a constant there.

### 9.3 Optionals and results

Values of `?T` and `E!T` implicitly wrap, and patterns implicitly unwrap:

- Against a scrutinee of type `?T`, any pattern other than `null`, `_` and a bare binding is matched against the present value: `5` matches a present 5. A **bare binding** `x` against `?T` matches **only a present value** and binds `x: T`; to bind the whole optional, write `_` and use the scrutinee.
- Against `E!T`, any pattern other than `!p` and `_` is matched against the success value, and a bare binding matches only success.

Each rule unwraps one level: against `??T` a bare binding binds the present `?T`, and against `E!?T` it binds the success value `?T`, so `null` and a binding are exhaustive for `??T`. A pattern that is neither a bare binding nor excluded above is matched against the present value by the same rules, so `5` against `??i32` matches only a doubly present 5.

This is what makes `if user := find(id)` and `while line := next()` work. The implicit unwrapping applies only in refutable contexts (binding conditions and `match`). In a `for` pattern or a destructuring binding, a bare binding binds the whole value, so `for r in results() { … }` binds `r: E!T`. Within `match`, an exhaustive set for `?T` is `null` plus patterns covering `T`; for `E!T`, `!p` patterns covering `E` plus patterns covering `T`.

*Note:* because of this rule, a bare binding is refutable when the scrutinee is an optional or result. In `x := e` statements (not conditions) no pattern is involved, so `x` simply gets the type of `e`.

### 9.4 Irrefutability

A pattern is **irrefutable** for a type if it matches every value of the type. Irrefutable: `_`; a binding against a non-optional, non-result type; tuple and struct patterns of irrefutable sub-patterns; a variant pattern of a union with exactly one variant. `for` patterns and destructuring must be irrefutable. Binding conditions (§8.2) are normally refutable; an irrefutable binding condition is a compile error ("condition always succeeds").

### 9.5 Patterns in `is`

The right side of `is` may be a pattern that binds nothing (§7.6).

---

## 10. Optionals and results

The two types mirror each other; `try` and `else` work on both.

|  | Optional `?T` | Result `E!T` |
| --- | --- | --- |
| Absent / failed value | `null` | `!e` |
| Present / success value | `x` (wraps implicitly, §6.4) | `x` (wraps implicitly) |
| Trust-me unwrap | `x?` | `x!` |
| Propagate | `try x` | `try x` |
| Fallback | `x else d` | `x else d`, `x else e => d` |
| Checked unwrap | `if v := x` | `if v := x { } else e => …` |

### 10.1 Nesting

Wrapping is one level. For `x: ??i32`, `null` is the outer absent value, and the inner absent value is written `?i32(null)`. A value of type `T` coerces to `??T` by wrapping twice. Similarly, `E!?T` holds an error, a present `T`, or a success that is `null`.

### 10.2 Error values: `!e`

`!e` is a prefix expression that makes a result in the error state from `e`. Its type is `E!T` where `E` is `e`'s type and `T` comes from context. With a union error type known from context, variant shorthand works: `return !.TooBig`.

### 10.3 Unwrap: `x?` and `x!`

`x?` on `?T` gives the `T`. `x!` on `E!T` gives the `T`. Both are **checked operations**: if `x` is `null` (or an error), the program traps in Debug and Checked builds, reporting the source location (and, for `!`, the error value's type and variant). In Fast builds the result is an unspecified value of type `T` (§18).

`x?` and `x!` say "this cannot fail here." To handle failure, use `try`, `else` or a binding condition.

### 10.4 Fallback: `else`

`x else d` evaluates `x`; if it is present (or success), the result is the unwrapped value; otherwise `d` is evaluated and is the result. `d` is not evaluated when `x` is present.

- If `x: ?T`, then `d` must have type `T`, `?T`, `E!T`, or `never`. The result type is `T` if `d` is `T` or `never`, `?T` if `d` is `?T`, and `E!T` if `d` is `E!T`: this is how an absent value is given an error, as in `try (lookup(k) else !.Missing)`. When `d` is an error value `!e` whose error type comes from context, the context is the enclosing `try`'s function result type `F!U` (so `E` is `F`), or the expected type of the whole `else` expression. Since `else` is right-associative, `a else b else c` means `a else (b else c)`.
- If `x: E!T`, `d` must have type `T`, `E2!T` (giving a result `E2!T`), or `never`.
- `x else e => d` (results only) binds the error value to the immutable name `e` in `d`. The `=>` body extends as far as an expression can (to the end of the enclosing statement, argument or arm) — parenthesize to limit it.
- `d` may be `return …`, `break`, `continue`, `trap(…)`, all of type `never`: `n = n *? 10 else return !.TooBig`.

### 10.5 Propagation: `try`

`try x` evaluates `x`. If it is present (success), the result is the unwrapped value. Otherwise the enclosing function returns immediately:

- In a function returning `?U`, `x` must be an optional; the function returns `null`.
- In a function returning `F!U`, `x` must be a result `E!T` whose error type `E` is `F` or **lifts** into `F` (§10.6); the function returns `!lift(e)`.
- In any other function, `try` is a compile error. In particular `try` never converts between optionals and results; write `try (lookup(k) else !.Missing)` to give an absent value an error.

`try` is a prefix operator at level 2: `try a.b()` applies to `a.b()`, and `try x + 1` is `(try x) + 1`. Unparenthesized `try x else d` is a compile error, because it would mean `(try x) else d`; write `try (x else d)` or `(try x) else d`.

Inside a handler clause or a function literal, `try` returns from the function that `return` would (§8.7). `errdefer` statements run when `try` propagates an error (§8.8).

### 10.6 Error lifting

If `F` is a union type that has **exactly one** variant whose payload type is `E`, then a value `e: E` lifts to `F` as that variant: `F.V(e)`. Lifting applies:

- in `try` (§10.5);
- in `return !e` and at any coercion site where an `E!T` is converted to `F!T` (this is a coercion, §6.4).

Lifting is one level: it does not search payloads of payloads. If two variants carry payload type `E`, lifting is ambiguous and a compile error; handle the error explicitly: `x else e => return !.Which(e)`.

```
LoadError :: union { Parse: ParseError, Io: IoError, Missing }

load_config :: fn(path: string) -> LoadError!Config \ {Alloc, Fs} {
    text := try read_file(path)        // IoError lifts to .Io
    port := try parse_u16(text)        // ParseError lifts to .Parse
    …
}
```

### 10.7 Optional-returning arithmetic

`a +? b`, `a -? b`, `a *? b`, `a <<? b` give `?T` (§7.4.2). `x.checked(T)` and `a.get(i)` give optionals. These compose with `else` and `try`:

```
n = n *? 10 else return !.TooBig
```

---

## 11. Functions

### 11.1 Declarations and literals

A function is a value written as a **function literal** and usually bound to a name with `::`:

```
add    :: fn(a, b: i32) -> i32 { return a + b }
square :: fn(x: f32) -> f32 => x * x
divmod :: fn(n, d: u32) -> (q: u32, r: u32) => (n / d, n % d)
greet  :: fn(name: string) \ {Out} => print("hi {}\n", name)
```

```
fn_lit     = 'fn' '(' [ param % ',' ] ')' [ '->' type ] [ '\' effect_set ] [ where_clause ] fn_body
fn_body    = block | '=>' expr
param      = ident { ',' ident } [ ( ':' | '::' ) type ] [ '=' expr ]
effect_set = '{' [ effect % ',' ] '}'
```

- `a, b: i32` declares two parameters of one type.
- `-> R` may be omitted when the return type is `()`. With a block body, the end of the block must be unreachable or the return type must be `()` or `E!()` (falling off the end returns `()`, which for `E!()` is a success). With an `=>` body, the expression's value is returned.
- The effect set lists every effect the function may perform that it does not handle itself (§12.3). Omitted means empty.
- Parameter types may be omitted only in a literal whose expected type is a known function type (e.g. an argument to a parameter of function type); they are then taken from it. When the expected return type is a type parameter still to be inferred, an `=>` literal without `-> R` has the type of its body expression, which determines that parameter: `people.sort_by_key(fn(p) => (p.last, p.first))` infers the key type.

### 11.2 Parameters

Parameters are immutable bindings. Each parameter is a **distinct object** initialized from its argument: modifying the original through another path during the call is never visible through the parameter, and `&p` (which gives `^const T`) is never equal to the address of the argument's original storage.

*Implementation note:* an implementation may pass a large argument by reference instead of copying when this is unobservable. See Appendix D.

A parameter may have a default `= e`, where `e` is a constant expression (§7.15) of the parameter's type (if that type mentions a type parameter, the default is used only when that type parameter is inferred from it, §16.5; supplying the type parameter and omitting the argument is a compile error unless the default has the supplied type), or the intrinsic `@caller_location()`, which is evaluated at each call site and gives that call's location. Parameters with defaults may be followed only by parameters with defaults. A parameter whose default is `@caller_location()` is bound only by name: positional arguments (and unnamed tuple spreads) never bind to it, and it is not counted among the non-inferable parameters of §16.2.

A parameter of type `type` is a **type parameter**, and a parameter declared `name :: T` is a **compile-time value parameter**; either makes the function generic (§16.1, §16.5).

### 11.3 Calls

A call `f(args)` binds arguments to parameters:

1. **Positional** arguments bind to parameters in order. Inferable parameters (§16.2) are skipped by positional binding when the call omits them; see §16.2 for the rule.
2. **Named** arguments `name: e` bind to the parameter of that name. They follow all positional arguments.
3. **Spread** arguments `..e` (§11.4) expand into positional or named arguments.
4. Each parameter must be bound exactly once, or have a default, which is used.

Each argument is coerced to its parameter's type (§6.4). Arguments are evaluated left to right in the order written, then the call is made.

The argument list is a tuple: `f(..t)` where `t: (i32, i32)` passes `t`'s elements.

### 11.4 Spread

`..e` in an argument list or constructor:

- If `e` is an unnamed tuple, its elements become positional arguments.
- If `e` is a named tuple or a struct (whose fields are all visible), its elements/fields become named arguments.
- Several spreads may appear. A later explicit **named** argument overrides a name supplied by a spread: `Window(..w, title: "new")` is `w` with a different title. A name supplied twice by spreads, or twice explicitly, is a compile error.

The prefix `..` in an argument list is always spread; there are no open-ended ranges outside index brackets (§7.9).

### 11.5 Results of calls

A function returns one value. Several values are returned as a tuple, and destructured by the caller: `q, r := divmod(17, 5)`. A function whose return type is `never` must not return; every path through its body must end in an expression of type `never`.

### 11.6 Second-class functions

A function literal that refers to a local binding of an enclosing function **captures** it by reference. Such a literal, and every value of a function type `fn(…)` (as opposed to a function pointer `^fn(…)`), is **second-class**:

- It may be bound with `::` to a local, passed as an argument, and called.
- It may not be stored in a mutable variable, a struct, tuple, array, union, optional, or result; returned; assigned; or captured by a function pointer or a nested named function. It may be captured by another function literal or handler literal (which is then itself second-class).

These restrictions ensure a second-class function never outlives the frame that created it, so captured locals are always valid while it can be called. Accordingly, a second-class function type `fn(…)` may be written only as the type of a parameter or of an immutable local; it may not be a field, element or type argument (§16.1).

A handler literal's type has no name, so handler literals cannot be passed as arguments; pass a second-class function that installs the handler instead.

**Effects of second-class functions.** A parameter `op: fn(A) -> R \ {E}` declares that the callee will call `op` in a context that handles `E`. An argument passed for it may perform:

- the effects in `E`, which are handled by the handlers in scope **where the callee calls `op`**; and
- any other effects, which are handled by the handlers in scope **where the argument was written** (the caller's context). These effects are part of the caller's effect requirement, not the callee's.

```
retry :: fn(times: u32, op: fn() -> IoError!()) -> IoError!() {
    for _ in 1..times { if op() is not !_ { return () } }
    return op()                         // last attempt: its error is ours
}

retry(3, fn() => send(packet))      // send's Net effect is handled by retry's caller
```

`retry` performs no effects of its own and needs no effect annotation to accept callbacks that perform any. A callee that wants to *provide* an effect lists it:

```
capture_output :: fn(body: fn() \ {Out}) -> string \ {Alloc} {
    buf := string_builder()
    with buf.out_handler() { body() }         // body's Out is handled here
    return buf.finish()
}
```

A named function passed for a second-class parameter behaves the same way as a literal: effects in the parameter's set are handled at the call inside the callee; its other effects are handled where the argument expression was written.

### 11.7 Function pointers

`^fn(A) -> R \ {E}` is a first-class function pointer (§5.14). A named function, or a function literal that captures nothing, converts to a function pointer type when its parameter types and return type are identical, its calling convention is the type's (§19.2), and its effect set is a subset of `E` (§12.3). Calling a function pointer performs exactly the effects in its type, handled at the call site, like a call to a named function.

Function pointers may be stored anywhere, compared with `is`, and passed to C when their signature is C-compatible and their effect set is empty or `{Sys}` (§19.2).

### 11.8 Nested named functions

A `name :: fn(…) { … }` binding inside a function body whose literal captures nothing declares a **nested function**: its body refers only to top-level names, to compile-time constant locals (untyped constants and `::` locals evaluated at compile time), to its own parameters and locals, and to itself by name. It is equivalent to a private top-level function and is first-class. If the literal refers to any other local of the enclosing function, the binding is instead an immutable local holding a second-class function value (§11.6), which cannot refer to itself.

A nested function may be generic (§16.1); it is then always a nested function, and its body refers only to top-level names, to its own parameters and locals, and to itself. A function literal used as a value cannot be generic, since a function value has a single type.

### 11.9 Recursion

Functions may call themselves and each other recursively. Second-class function literals cannot refer to themselves.

### 11.10 Function attributes

`#inline` requests inlining at every call site (a compile error if impossible, e.g. for a recursive function). `#noinline` forbids it. `#cold` marks a function unlikely to be called. `#fast_math` permits float contraction and reassociation inside the function. These never change meaning, except `#fast_math`, whose float results may differ.

---

## 12. Effects and handlers

An **effect** is an interface whose implementation is supplied by the caller's context instead of by an argument. A function lists the effects it performs after `\`; a `with` block somewhere up the call chain must **handle** each of them by installing a **handler**.

### 12.1 Effect declarations

```
Log :: effect {
    log: fn(level: Level, msg: string)
}
Yield :: effect(T: type) {
    yield: esc(x: T)
}
Abort :: effect {
    abort: esc() -> never
}
Async :: effect {
    suspend: ctl(what: Waitable)
    spawn:   ctl(task: Task)
}
```

```
effect_lit = 'effect' [ '(' param % ',' ')' ] '{' { op_decl terminator } '}'
op_decl    = ident ':' ( 'fn' | 'esc' | 'ctl' ) '(' [ param % ',' ] ')' [ '->' type ]
```

An `effect` literal creates a nominal effect type. Its parameters, if any, are types (`effect(T: type)`); an effect is instantiated by applying it to type arguments. Each **operation** has a name, a **kind** (`fn`, `esc` or `ctl`), parameters, and a result type (default `()`).

Declaring an effect `E` in module `M` also declares each operation `op` as a top-level function of `M` named `op` (public if `E` is), with the operation's parameters (preceded by `E`'s parameters as inferable type parameters, §16.2), its result type, and effect set `{E(…)}`. Calling it **performs** the operation. When exactly one instantiation of `E` is available at the call — in the enclosing function's effect set or installed by an enclosing `with` — and `E`'s parameters cannot be inferred from the arguments (for example, `yield(1)` with an untyped literal) or are inferred as an instantiation that is not available, that one instantiation is used and the arguments are coerced to its parameter types: in a function performing `Yield(string)`, `yield(buf)` with `buf: []u8` yields a `string`. It is also reachable as `E.op`. A name clash between an operation and another top-level name of `M` is a compile error.

### 12.2 Operation kinds

The kind is a promise about what a handler's clause may do, so the signature tells the caller the worst case.

| Kind | The clause may | The performer |
| --- | --- | --- |
| `fn` | only return a value | always continues after the perform, with that value |
| `esc` | return a value, or **exit** the `with` (§12.7) | continues, or is unwound and never continues |
| `ctl` | capture the continuation and switch stacks (§14) | is suspended; may continue later, or be cancelled |

An `esc` or `ctl` operation with result type `never` can never return to the performer.

### 12.3 Effect sets

An effect set `{E1, E2, …}` lists effect types, each possibly instantiated (`Yield(i32)`). Two instantiations of a generic effect with different arguments are different effects; `{Yield(i32), Yield(u8)}` is a valid set. Order and duplicates are irrelevant.

**Performed effects.** The effects an expression *performs* are:

- for a call to a function or function pointer with effect set `S`: `S`;
- for a call to a second-class function value `p` of type `fn(…) \ {S}`: `S` (the argument's other effects belong to the caller, §11.6);
- for a function literal or handler literal written as an expression: the effects its body performs that are not among the effects it is given by its expected type (the literal's "own" effects, §11.6 and §12.5); these are performed *where the literal is written*;
- for `with h { B }`: the effects `B` performs, minus those handled by `h`, plus the effects the installed handlers' clauses require (§12.6);
- for any other expression: the union over its sub-expressions.

**Checking.** For every function, the effects its body performs must be a subset of its declared effect set. A `test` body and `main` are checked against the ambient set (§3.7). A top-level constant's initializer must perform no effects.

**Subsumption.** Where a function pointer type `^fn(…) \ {S}` is expected, a function pointer or named function with the same calling convention (§19.2) whose effect set is a subset of `S` is accepted. There are no effect variables, and no other subtyping on effect sets.

### 12.4 Handlers

A **handler** for effect `E` provides one **clause** for each operation of `E`; a handler missing a clause, or with a clause for a name that is not an operation of `E`, is a compile error. Handlers come from two sources:

- a **handler literal** (§12.5), a second-class value; or
- a value of a type `T` for which a **handler declaration** `handler E for T` exists (§12.6).

### 12.5 Handler literals

```
handler_lit = 'handler' effect_ref '{' { clause terminator } '}'
clause      = ident '(' [ ident % ',' ] ')' '=>' ( expr | block )
```

```
save_quietly :: fn(doc: ^const Doc) -> (IoError!(), u32) \ {Fs} {
    warnings := 0u32
    quiet :: handler Log {
        log(level, msg) => if level is .Warn { warnings += 1 }
    }
    r := with quiet { save(doc) }
    return (r, warnings)
}
```

- Clause parameters are names only; their types come from the operation. A `ctl` clause has one extra, final parameter: the continuation (§14.2).
- A handler literal may capture locals of the enclosing function by reference, and is **second-class** under exactly the rules of §11.6: it may be bound with `::`, used in `with`, and captured by other second-class literals; it cannot be stored, returned, or outlive its frame.
- Effects performed by a clause body are handled by the handlers in scope **where the literal is written**. They count as effects performed by the enclosing function at that point.
- `break`, `continue` and `return` in clause bodies are described in §12.7.

### 12.6 Handler declarations

```
handler_decl = 'handler' effect_ref 'for' type [ '\' effect_set ] '{' { decl_clause terminator } '}'
decl_clause  = ident '(' ident { ',' ident } ')' '=>' ( expr | block )
```

```
FileLogger :: struct { fd: Fd, min: Level }

handler Log for ^FileLogger \ {Fs} {
    log(self, level, msg) => if level >= self.min { _ = fs.write(self.fd, msg) }
}
```

- The first clause parameter (`self`) has type `T`; the rest are the operation's parameters (and for `ctl`, the continuation).
- `\ {A}` declares the effects the clauses may perform; the effects performed by every clause body must be a subset of `A`, checked as for functions (§12.3). They are handled by the handlers in scope at the `with` that installs the handler, **outside** that `with` (a clause never handles its own performs). They count as effects performed by the `with` statement.
- A handler declaration must appear in the module that declares `E` or the module that declares `T` (or, if `T` is `^S`/`^const S`, the module that declares `S`). For each `E` and `T` there is at most one declaration.
- Clauses of a handler declaration have no enclosing function: `return`, labeled `break`/`continue`, and captures are not available. Unlabeled `break` (for `esc` and `ctl` operations) exits the installing `with` (§12.7).
- A value of type `T` used in `with` installs a handler for every effect `E` that has a declaration `handler E for T`.

### 12.7 `with`

```
with_expr = [ label ':' ] 'with' expr % ',' [ 'on' expr ] block
```

`with h1, h2, … { B }` evaluates `h1`, `h2`, … left to right, then runs `B` with their handlers installed. `with a, b { B }` is equivalent to `with a { with b { B } }`: a later handler is inside the earlier ones, so effects required by `b`'s clauses (§12.6) may be handled by `a`, but not the reverse. List providers first: `with heap_allocator(), logger_that_allocates() { … }`. Two handlers for the same effect in one `with` is a compile error. `on s` is required if and only if some installed handler handles a `ctl` operation (§14.1).

A handler literal written directly in the `with` head must be parenthesized, since `{` begins the body: `with (handler Log { … }) { … }`.

The handler values are evaluated once and live until the `with` completes.

**Which handler handles a perform.** Every point in a program has a **handler context**: a mapping from each effect it may perform to one installed handler. At the start of a function body, the context is the caller's context at the call (restricted to the function's effect set). Inside `with h { B }`, the context of `B` is the outer context with `h`'s effects mapped to `h`. A second-class function value carries the context of the point where it was written, except for the effects listed in its parameter type, which are taken from the point where it is called (§11.6). A clause runs in the context described in §12.5 and §12.6 — never in a context where its own handler handles its own performs.

Thus a `yield` written inside a `for` loop body goes to the handler outside the loop, not to the loop itself, and nested loops over the same `Yield(T)` work as expected (§13).

**Value.** `with` is an expression. Its value is the value of `B`, or the operand of a `break` that exits it. If some clause can `break` out with a value, the types must agree as for `if` arms (§7.12).

#### 12.7.1 Clause outcomes

When an operation is performed, its handler's clause runs with the operation's arguments. What happens next depends on the kind of operation and how the clause ends:

| Clause ends by | `fn` | `esc` | `ctl` |
| --- | --- | --- | --- |
| producing a value `v` (falling off the end, or `=> v`) | the perform returns `v` | the perform returns `v` | not allowed: a `ctl` clause has type `never` |
| `continue` (unlabeled, at clause level) | not allowed | same as producing `()`; only for `()`-returning operations | not allowed |
| `break` / `break v` (unlabeled, at clause level) | not allowed | **exit** the installing `with`, with value `v` | exit the `with` (§14.4) |
| `break L` / `continue L` / `return` (handler literals only) | not allowed | **exit** to that target, which must lexically enclose the installing `with` | as `esc` (§14.4) |
| `resume k(v)` | — | — | §14.3 |

"At clause level" means not inside a loop or labeled `with` that is itself inside the clause body; inside such a construct, `break` and `continue` refer to it as usual.

**Exit.** An exit **unwinds** every frame between the perform and the target: innermost first, each frame's pending `defer` and `errdefer` statements run, as if each function had returned (but with `errdefer` running, since its result will never be delivered; §8.8). Then control transfers to the target: after the `with` (for `break`), to the next iteration or after the loop (for `continue L` / `break L`), or out of the enclosing function (for `return`, which runs that function's own defers normally). Frames between the `with` and the target in the same function are left like an ordinary `break`/`return` would leave them.

*Note:* unwinding runs ordinary Gamma code (defers); it is not a stack-unwinding table mechanism. See Appendix D.

### 12.8 Effect polymorphism

Gamma has no effect variables. Higher-order functions are effect-polymorphic through second-class function parameters (§11.6): their callbacks' unlisted effects are handled at the callback's definition site, so the higher-order function never mentions them. Function *pointers* have a fixed effect set.

Generic effects (`Yield(T)`) and effect sets mentioning type parameters (`\ {Yield(T)}`) are allowed; the set is fixed per instantiation.

### 12.9 Ambient effects

The following effects are predeclared. `main` and tests may perform any of them; the runtime installs a default handler for each before calling `main`.

| Effect | Operations (summary; signatures in §21.7) | Default handler | Test handler |
| --- | --- | --- | --- |
| `Alloc` | `current: fn() -> Allocator` | the system heap | leak-checking heap; a leak fails the test |
| `Out`, `Err` | `write_out` / `write_err: fn(bytes: []const u8)` | stdout, stderr | captured to buffers |
| `In` | `read: fn(buf: []u8) -> IoError!usize` | stdin | empty input |
| `Clock` | `now: fn() -> Instant`, `sleep: fn(d: Duration)` | system clock | fake clock; `sleep` advances it |
| `Rand` | `fill: fn(buf: []u8)` | OS-seeded generator | fixed seed |
| `Fs` | file-system operations (§21.7) | real file system | in-memory file system |
| `Net` | socket operations (§21.7) | real sockets | loopback fake |
| `Env` | `args: fn() -> []const string`, `var: fn(name: string) -> ?string`, `exit: esc(code: u8) -> never` | process arguments and environment | no arguments and no variables; `exit` ends the test, which passes only with code 0 |
| `Sys` | none (§12.10) | — | not replaceable |

Any of these may also be handled by user handlers; only `Sys` cannot.

### 12.10 `Sys`

`Sys` has no operations and cannot be handled. It is the effect of operations that escape Gamma's model: calling foreign functions (unless annotated otherwise, §19.2), accessing `#global` storage, volatile memory access, inline assembly, creating threads, and the low-level primitives from which the ambient effects' default handlers are built. A function whose effect set does not contain `Sys` (and calls no foreign function annotated with a smaller set) cannot observe or modify state outside its arguments, the memory they point to, and its handlers. An exported function may have `Sys` and installs handlers internally (§19.2).

### 12.11 Static guarantees

- A perform is always handled: every performed effect is in the enclosing function's declared set or handled by an enclosing `with`, and every caller in turn handles or declares it. There is no runtime "unhandled effect" condition.
- A function whose effect set contains no `esc` or `ctl` operation, and which calls no second-class function parameter, always returns to its caller (or traps, or loops forever). Its frames are never unwound.
- A function with an empty effect set (and no `Sys`) can be evaluated at compile time (§17).

---

## 13. Iteration

### 13.1 The `Yield` effect

```
Yield :: effect(T: type) { yield: esc(x: T) }
```

`Yield` is predeclared. An **iterator** is any function whose effect set contains `Yield(T)`; it produces values by performing `yield(x)`. Because `yield` is an `esc` operation, the consumer may stop the iterator early, which unwinds it (running its `defer`s).

### 13.2 `for` is a handler

```
[L:] for p in e { B }
```

is defined as follows.

1. **The source.** `e` must be one of:
   - a call `f(a1, …, an)` (including a method call, §7.10) whose callee's effect set contains exactly one instantiation `Yield(T)`;
   - a range `lo..hi` or `lo..=hi` of an integer type, or a value of type `Range(T)` or `RangeIncl(T)` with an integer `T` (§5.16), meaning `range(lo, hi)` or `range_incl(lo, hi)` (§21.4), which yield the integers in ascending order;
   - an array place, a pointer to an array, or a slice, meaning `each(e[..])`, which yields the elements' values in index order;
   - any other expression of array type, such as an array constructor or a call that returns an array (and has no `Yield` effect): the array is evaluated into a hidden variable that lives as long as the loop, which is then iterated as an array place.
   Any other form is a compile error. `T` is the type of the yielded values.

2. **Evaluation.** The callee and arguments `a1 … an` are evaluated first, left to right, **in the context outside the loop**. Then the call is made in a context where `Yield(T)` is handled by the loop's handler and every other effect is handled as outside the loop. Second-class arguments therefore keep their own (outer) context (§11.6).

3. **The handler.** The loop's handler is equivalent to the handler literal

   ```
   handler Yield(T) { yield(x) => { p :: x; B } }
   ```

   installed by a `with` labeled `L`. The pattern `p` must be irrefutable for `T` and binds immutable names.

4. **Control.** Inside `B` at clause level (§12.7.1):
   - reaching the end of `B`, or `continue`, returns from the clause: `yield` returns and the iterator proceeds to its next value;
   - `break` exits the loop: the iterator's frames are unwound, then execution continues after the loop;
   - `return`, labeled `break`/`continue`, and `try` propagate to their targets as described in §12.7.1.
   When the call to `f` returns, the loop ends.

5. **Effects.** Effects performed by `B` (including `yield` of any type) are handled in the context outside the loop, by the clause rule (§12.5). This is why an iterator can be written as a loop over another iterator:

   ```
   evens :: fn(xs: []const i32) \ {Yield(i32)} {
       for x in xs { if x % 2 is 0 { yield(x) } }   // yield goes to evens's caller
   }

   for e in evens(data) {
       if e > 100 { break }
       print("{}\n", e)
   }
   ```

**Value.** A `for` is an expression whose type is the return type `R` of the source call (`()` for ranges and arrays). If the source returns normally, that is the loop's value. `break v` exits with the value `v: R`; plain `break` is allowed only when `R` is `()`. Since a non-`()` value may not be silently discarded (§7.14), an iterator's error cannot be ignored:

```
tokenize :: fn(src: string) -> LexError!() \ {Yield(Token)}

try for t in tokenize(src) {
    if t is .Eof { break () }        // break with a success value
    push_token(t)
}
```

### 13.3 Standard iterators

The core library (§21.4) provides at least:

| Iterator | Yields |
| --- | --- |
| `range(lo, hi)`, `range_incl(lo, hi)` | integers |
| `each(xs)` | `xs: []const T` → `T` values |
| `each_ptr(xs)` | `xs: []T` → `^T` (to modify elements) |
| `enumerate(xs)` | `(i: usize, x: T)` |
| `reversed(xs)` | `T` values, last to first |
| `bytes(s)`, `runes(s)` | `u8`, `rune` from a string |

```
for p in each_ptr(scores) { p^ = p^ +| 10 }
for i, x in enumerate(names) { … }
```

Any user function with `Yield(T)` in its effect set is an iterator; there is no other iterator protocol.

### 13.4 Lockstep iteration

Internal iteration cannot advance two iterators in lockstep without fibers. Standard containers and slices therefore also provide **cursors**: values with a `next` method returning `?T` or `?^T`, used with `while`:

```
ca := a.cursor()
cb := b.cursor()
while x := ca.next() {
    y := cb.next() else break
    combine(x, y)
}
```

A general iterator can be turned into a pull-style generator by running it on a fiber (§14); a library type such as `Generator(T)` can package this (it is not part of the core library).

### 13.5 Performance

*Implementation note (non-normative):* when the iterator's body is visible to the compiler, `for` compiles to the same loop as hand-written index code, because the handler is known statically and the `yield` clause is inlined. The standard iterators are `#inline`. See Appendix D.

---

## 14. Fibers and continuations

`ctl` operations suspend the code that performs them. This is how Gamma expresses coroutines, async I/O and schedulers. Suspended code needs a stack of its own, so `ctl` handlers are installed together with a stack, and suspension is a stack switch. Nothing is allocated implicitly.

### 14.1 `with … on`

```
with h on stack { B }
```

If any handler installed by a `with` handles a `ctl` operation, the `with` must have an `on` clause, and vice versa. `stack` is an expression of type `[]u8`.

- `B` runs on a new **fiber** whose stack is the memory of `stack`. The code that executes the `with` statement — on whatever stack it was already running — is the `with`'s **driver**.
- `fn` and `esc` clauses of the installed handlers run where performed, like any other handler.
- `ctl` clauses run **on the driver's stack**, as if called from the `with` statement. Their effects are handled as stated in §12.5 and §12.6.
- The stack memory must stay valid, and must not be used for anything else, until the `with` completes. Stacks never grow or move; pointers into a suspended fiber's stack remain valid while the fiber exists.
- Running out of fiber stack is a **checked** condition (§18): it traps in Debug and Checked builds. `stack` must be at least `@min_stack` bytes (checked).

### 14.2 Continuations

When code running on a fiber performs a `ctl` operation `op(args)` with result type `R`, the fiber is **suspended** at the perform, and the clause runs on the driver with the arguments and a **continuation** `k: cont(R)` as its final parameter. A continuation represents the suspended fiber — including any fibers of nested `with … on` blocks that are part of it — and can be **consumed** exactly once, by `resume` or `discard`.

`cont(R)` is a first-class type: a continuation can be stored in variables, structs and queues, copied, and passed around. The rules that keep this safe:

1. **One-shot.** Each suspension produces a continuation that must be consumed exactly once. Consuming one twice (through any copy) is a checked error: Debug and Checked builds trap; it is undefined behavior in Fast builds.
2. **Bounded by its `with`.** Every continuation produced under a `with` must be consumed before that `with` completes (§14.4).
3. **Same `with`.** A continuation may be consumed only by code running in a `ctl` clause of a handler installed by the same `with` that produced it.
4. **Same thread.** A continuation may be consumed only on the thread that created it (checked).

If a clause uses `k` only as the operand of `resume` or `discard` — never storing, copying or passing it — the compiler verifies statically that no path consumes it twice, and that every path ending in `resume` or in a `break` to the installing `with` has consumed it (paths that exit past the `with` discard it, §14.4); rules 1–3 then need no run-time check for it.

### 14.3 `resume`

```
resume k(v)
```

`resume` may appear only in the body of a `ctl` clause. `v` must have type `R` for `k: cont(R)` (written `resume k()` when `R` is `()`).

`resume k(v)` has type `never`. It ends the clause: the clause's pending `defer` statements run, the clause's frame is discarded, and the driver switches to `k`'s fiber, where the suspended perform returns `v`. When that fiber (or any fiber of the same `with`) next performs a `ctl` operation of the `with`'s handlers, a fresh clause invocation runs on the driver.

Because `resume` is a transfer, not a call, a scheduler can switch between any number of fibers without the driver's stack growing.

A `ctl` clause has type `never`: every path through it must end in `resume`, an exit (`break`, labeled `break`/`continue`, `return`), a call to a `never`-returning function such as `trap`, or an infinite loop.

### 14.4 Completing a `with … on`

A `with … on` completes in one of two ways:

- **`B` finishes** on its fiber with value `w`. Control returns to the driver and the `with`'s value is `w`.
- **A `ctl` clause exits** with `break`/`break v` to the installing `with`. The `with` completes with that value.

A third way is an **exit that crosses the `with … on`**: an exit whose target lies outside the `with`, taken by code on one of its fibers or by one of its clauses — a labeled `break`/`continue` or `return` in a clause of a handler literal, an `esc` operation (performed on a fiber or in a clause) whose handler is installed outside the `with`, or the discarding of a continuation that a fiber of an enclosing `with … on` is suspended in. When it starts on a fiber, the unwinding (§12.7.1) proceeds through that fiber's frames to its base. Then every unconsumed continuation produced under the `with` is discarded (§14.5), most recently created first, and the exit continues on the driver. Exits therefore never leave suspended fibers behind.

In the first two cases, every continuation produced under this `with` must already have been consumed; otherwise the program has violated rule 2 of §14.2. This is a checked error: it traps in Debug and Checked builds ("with completed while N fibers are suspended") and is undefined behavior in Fast builds. (To shut down, discard the suspended fibers first.)

### 14.5 `discard`

```
discard k
```

`discard k` is a statement, permitted only in a `ctl` clause. It **cancels** the suspended fiber: control switches to it, and its frames are unwound from the perform outward to the base of the fiber, running their `defer` and `errdefer` statements (§8.8) — exactly like an `esc` exit. Then control returns to the clause, which continues after the `discard`.

Deferred statements cannot perform `ctl` operations (§8.8), so cancellation cannot suspend. If `k` represents the fiber running `B`, discarding it unwinds `B`'s frames, including the parts of `B` that are lexically in the function containing the `with`.

### 14.6 Creating fibers

```
@fiber(f, arg, stack) -> cont(())
```

`@fiber` may be used only inside the body of a `ctl` clause. `f` is a function pointer `^fn(a: A) -> never \ {S}`, `arg: A`, and `stack: []u8`. `A` must be a first-class type (§16.1): a second-class value cannot be smuggled into a fiber that may outlive its frame. It creates a new fiber belonging to the clause's `with`, whose initial state is "about to call `f(arg)` on `stack`", and returns its continuation; resuming it (with `()`) starts the call.

- The new fiber runs in the handler context of the `with`'s body: the handlers installed by the `with`, plus the context outside it. The effects in `S` other than the clause's own effect are part of the effects the handler requires, since the clause cannot know which other handlers its `with` installs: for a handler literal, they must be available where it is written; for a handler declaration, `S` is determined by the operation's parameter type and the effects other than the declaration's own must be listed in its `\ {A}`. (A fiber that needs several `ctl` operations of its scheduler gets them from one effect, as `Async` in §14.7 does.)
- `f` returns `never`: a fiber ends by performing a `ctl` operation whose clause discards it (a library `spawn` wraps the user's function to do this).
- The stack rules of §14.1 apply to `stack`.

### 14.7 Example: a scheduler

*Example (non-normative).* `Queue`, `Poller` and `spawn_task` are library code not shown.

```
Async :: effect {
    wait:  ctl(fd: Fd, events: Events)
    spawn: ctl(entry: ^fn(arg: rawptr) -> never \ {Async}, arg: rawptr, stack: []u8)
    exit:  ctl() -> never
    join:  ctl()                      // wait until every spawned task has exited
}

Scheduler :: struct {
    ready:   Queue(cont(()))
    waiting: Poller(cont(()))         // fds → continuations
    joiner:  ?cont(())
    live:    usize
}

handler Async for ^Scheduler \ {Sys} {
    wait(self, fd, events, k) => {
        self.waiting.add(fd, events, k)
        r := self.next()
        resume r()
    }
    spawn(self, entry, arg, stack, k) => {
        self.ready.push(@fiber(entry, arg, stack))
        self.live += 1
        resume k()
    }
    exit(self, k) => {
        discard k
        self.live -= 1
        if self.live is 0 { if j := self.joiner { self.joiner = null; resume j() } }
        r := self.next()
        resume r()
    }
    join(self, k) => {
        if self.live is 0 { resume k() }
        self.joiner = k
        r := self.next()
        resume r()
    }
}

// next(): pop a ready continuation, or block in the poller (a Sys call) until one is ready.
// `resume k(v)` takes the continuation from a postfix expression without its final call (§A.7),
// so a continuation returned by a call is bound to a name first.
```

```
sched := Scheduler(…)
with &sched on main_stack[..] {
    spawn_task(serve, &conn_a, stack_a)       // library wrapper around `spawn`
    spawn_task(serve, &conn_b, stack_b)
    join()
}   // completes only after both tasks exit; every continuation has been consumed
```

### 14.8 Stack size analysis

`@stack_size(f)` is a compile-time `usize` constant: an upper bound on the stack bytes needed by a call to the named function `f`, including everything it calls and the space the implementation reserves on a fiber stack for its run-time support. It is at least `@min_stack`, so a stack of `@stack_size(f)` bytes is big enough for a fiber that calls `f` (a `with … on` whose body just calls `f`, or a fiber made by `@fiber(f, …)`). It is intended for sizing fiber stacks in embedded code.

The bound follows the calls that `f` can make, using what is statically known at each call:

- A call of a named function, or through a function value whose target is statically known, counts that function. A function value is statically known when it is a function name or literal, or a parameter or immutable local bound to one along the calls being followed — so `s.sort_by(fn(a, b) => …)` counts the literal where the sort calls `less`.
- A perform of a `fn` or `esc` operation counts as a call to the clause when the handler is statically known: installed by a `with` that the calls being followed pass through, with a handler literal or a value whose type has `handler … for` declarations (§12.6). The clause's own performs then use the handlers outside that `with` (§12.5).
- A perform of a `ctl` operation suspends the fiber; its clause runs on another stack (§14.1).
- A `with … on` counts its `ctl` clauses, which run on the current stack (§14.1), but not its body, which runs on the fiber's stack.

Recursion, calls through function values whose targets are not statically known, performs of operations whose handlers are not statically known (including the ambient effects of §12.9, which `f` cannot see), and foreign calls are compile errors in `@stack_size`. The bound is conservative: each frame is costed generously from the sizes of its locals and temporaries, and the implementation checks fiber stacks at run time in Debug and Checked builds (§14.1).

### 14.9 Threads

Fibers do not migrate between threads. Each thread (§19.6) has its own handlers; a `with` and all its fibers belong to the thread that executed it.

---

## 15. Memory and allocation

### 15.1 Objects and lifetimes

Storage comes from four places:

| Storage | Created by | Lives until |
| --- | --- | --- |
| static | string literals, top-level constants, `#global` | program exit |
| automatic | locals, parameters, temporaries | the end of the enclosing block (temporaries: the end of the full expression or call that created them) |
| fiber stack | locals of code running on a fiber | as automatic, on the fiber's stack (§14.1) |
| allocated | an allocator (§15.2) | freed through the same allocator |

Using a pointer to an object after its lifetime ends is undefined behavior (§18.4). Gamma does not track lifetimes in general; it rejects only the direct cases:

- returning `&x`, `x[..]`, or a slice of `x`, where `x` is a local or parameter of the returning function;
- returning a second-class value (always rejected, §11.6).

*Note:* Debug builds poison freed allocations and dead stack frames, which detects most other dangling uses (§18.3).

### 15.2 Allocators

```
Allocator :: struct {
    pub data: rawptr
    pub vt:   ^const AllocVtable
}
AllocVtable :: struct {
    pub alloc:  ^fn(data: rawptr, size, align: usize) -> ?[^]u8
    pub resize: ^fn(data: rawptr, p: [^]u8, old, new, align: usize) -> bool
    pub free:   ^fn(data: rawptr, p: [^]u8, size, align: usize)
}
Alloc :: effect { current: fn() -> Allocator }
AllocError :: union { OutOfMemory }
```

An **`Allocator`** is a first-class handle. The **`Alloc`** effect supplies the *current* allocator.

- **`Allocator` is itself a handler for `Alloc`**: the core library declares `handler Alloc for Allocator { current(self) => self }` (legal because both are declared in the same core module, §12.6), so `with arena.allocator() { … }` makes an arena current.
- The core functions `new`, `make` and `free` (§21.2) perform `Alloc` and use `current()`. They are for memory that dies inside the `with` that provided the allocator.
- **Long-lived structures capture their allocator at construction** and use that stored handle for every later operation. A `List` created under the heap allocator keeps growing on the heap even when used inside an arena's `with`.

```
names := list(string)                 // captures the current allocator (the heap)
defer names.deinit()
with arena.allocator() {
    tmp := try make(u8, 256)          // from the arena
    try names.push("ada")             // grows on the heap: the list kept its allocator
}
```

An allocator's `resize` and `free` receive the size and alignment that the block was allocated (or last resized) with; the core functions and library containers guarantee this. Freeing memory through an allocator other than the one that allocated it is undefined behavior. Debug builds tag every allocation with its allocator and trap on a mismatch, reporting both call sites.

A function without `Alloc` in its effect set does not allocate through the current allocator, though it may allocate through an `Allocator` value it was given.

### 15.3 No hidden allocation

No language construct allocates heap memory. Closures capture by reference (no environment allocation); continuations live on caller-provided stacks; `for` allocates nothing; string literals are static. Only calls to functions that perform `Alloc` or receive an `Allocator` allocate.

### 15.4 Copying

Assignment, argument passing and return copy values bitwise, like C. Copying a struct that contains a pointer copies the pointer; Gamma has no move semantics, destructors or copy constructors. Freeing memory reachable from two copies twice is undefined behavior (detected in Debug builds).

*Rationale:* ownership tracking would make Gamma a different language. The explicit `deinit`/`free` + `defer` style, combined with allocators that capture themselves and Debug-build tagging, covers C's common failure modes without one.

### 15.5 Alignment

Every object is aligned to at least its type's alignment. Allocators must return memory aligned to the requested alignment (any power of two up to the page size). `make_aligned(T, n, align)` requests a larger alignment; its storage is released with `free_aligned(s, align)`.

---

## 16. Generics

Gamma has one generics mechanism: parameters whose values are known at compile time, most often types.

### 16.1 Generic functions

A function with one or more parameters of type `type`, or one or more compile-time value parameters (§16.5), is generic. It is checked as follows:

```
max :: fn(T: type, xs: []const T) -> ?T where Ord(T) {
    best := try xs.get(0)
    for x in xs { if x > best { best = x } }
    return best
}
```

- **Checked once.** The body is type-checked once, where it is written, treating each type parameter as an opaque type that supports only what its constraints grant (§16.3). An error in a generic body is reported there, whether or not the function is ever called.
- **Monomorphized.** Each distinct set of type arguments produces a separate compiled function. Instantiation never fails type-checking, with two exceptions, both reported at the instantiating call: an untyped literal used at type `T` is checked against each instantiation (`T(300)` where `T` is `u8`), and an `@write` template is checked against its arguments (§21.6.1). `T(c)` for an untyped constant `c` types `c` at `T`, exactly as `x: T = c` does (it needs `Num(T)`, or `Float(T)` for a float constant); `T(x)` for a typed `x` is not available, since no conversion to every type `T` is lossless: use `x.trunc(T)`, `x.sat(T)` or `x.checked(T)`.
- Type parameters and `::` parameters are compile-time; parameters declared with `:` whose type is not `type` are ordinary run-time parameters.
- **Type arguments must be first-class run-time types.** A second-class function type `fn(…)`, `type`, an effect type, or any type containing one is not a valid argument for a type parameter (of a function or of a generic type). This is checked at each instantiation. Without it, `Box(fn() -> i32)` could store a second-class value and let it outlive its frame.

### 16.2 Inference of type arguments

At a call, type parameters may be omitted when they can be **inferred** from the value arguments:

- A parameter is **inferable** if it has type `type`, or is a compile-time value parameter (§16.5) that appears in the type of another parameter. Let *r* be the number of non-inferable parameters without defaults, and *n* the number of all non-inferable parameters, excluding in both counts any parameter bound by name. If the call supplies between *r* and *n* positional arguments, all inferable parameters are omitted and must be inferred; positional arguments then bind to the non-inferable parameters in order. Otherwise every inferable parameter must be supplied, positionally or by name.
- Inference matches each value argument's type against its parameter's type structurally (`[]const T` against `[]u8` gives `T = u8`, using the coercion `[]T → []const T` where necessary). Each type parameter must be determined by at least one argument, and all determinations must agree **exactly** — no widening is applied during inference. `f(1u8, 300u16)` against `fn(T: type, a, b: T)` is a compile error; write `f(u16, 1, 300)` or convert.
- In a method call (§7.10), the receiver is matched first, and the type parameters it determines are fixed: an argument whose parameter type mentions no other type parameter is coerced to that type (§6.4) instead of taking part in inference. With `names: List(string)`, `names.push(buf[..])` pushes a `string` even though `buf[..]` is a `[]u8`.
- Untyped literal arguments do not determine a type parameter; they take the inferred type. A type parameter that nothing but untyped literals could determine takes their fallback types (§6.1): with `log :: fn(A: type, template :: string, args: A)`, `log("{}", 5)` has `A = i32` and `log("{} {}", (5, 1.5))` has `A = (i32, f64)`.
- A type parameter that appears only in the result type, or not at all, cannot be inferred and must be written: `list(string)`.

```
biggest := max(scores) else 0        // T = i32, from scores: []i32 (coerced to []const T)
names   := list(string)              // T written explicitly
try names.push("ada")                // push(string, &names, "ada"): T inferred, & inserted (§7.10)
```

### 16.3 Constraints

```
where_clause = 'where' constraint % ','
constraint   = ident '(' type ')'
```

The constraints are a closed set of predicates on types. A generic body may use exactly the operations its constraints grant:

| Constraint | Satisfied by | Grants |
| --- | --- | --- |
| `Eq(T)` | every type for which `is` is defined (§7.6) | `is`, `is not`, `in` over `T`, literal patterns of `T`, `@hash` (§16.8) |
| `Ord(T)` | integer, float and enum types; tuples, arrays and slices of `Ord` types (§7.5) | `Eq`, plus `< <= > >=` |
| `Num(T)` | integer and float types | `Ord`, plus `+ - * /`, untyped literals at `T` |
| `Int(T)` | integer types | `Num`, plus `%`, the overflow flavors, shifts, bitwise ops, `.trunc` / `.checked` / `.sat` |
| `Signed(T)` | signed integer and float types | `Num`, plus unary `-` |
| `Float(T)` | float types | `Num`, plus float methods (§21.3) |
| `Plain(T)` | plain-data types (§5.19) | `---` at `T`, `.bits` to and from `T` |

Without any constraint, a type parameter `T` still supports: declaring variables and parameters of type `T`, copying, taking addresses, forming `^T`, `[]T`, `?T`, `E!T`, `[N]T`, passing `T` to other generics whose constraints it satisfies, `@size_of(T)` and `@align_of(T)`.

Operations beyond these come in as function parameters (`less: fn(a, b: ^const T) -> bool`) or as compile-time parameters (§16.4, §16.5). Gamma has **no user-defined traits, interfaces, typeclasses or overloading**; §16.9 explains how the jobs they usually do are done instead.

### 16.4 Generic types

The parameters of a `struct`, `union` or `effect` are always compile-time, because they are part of the type. They may be types or values of the types allowed for compile-time value parameters (§16.5), including function values:

```
HashMap :: struct(K, V: type,
                  hash: ?^fn(key: ^const K, seed: u64) -> u64 = null,
                  eq:   ?^fn(a, b: ^const K) -> bool = null) {
    …
}

ids := map(string, u32)                                // @hash and `is`
try ids.put("ada", 1)

names := map_with(string, u32, hash: ascii_fold_hash, eq: ascii_fold_eq)   // case-insensitive keys
```

- Parameters may have defaults, which must be constant expressions; arguments may be named.
- A function-pointer parameter of a generic type must be given a **named function** (or a constant bound to one, or `null`) with an empty effect set. It is baked in at monomorphization, so calls to it are direct.
- *Note:* `Eq` is `is`, so a map keyed by floats cannot find NaN keys; such maps should key by `x.bits(u64)`.
- Two instantiations `G(a1, …)` and `G(b1, …)` are the same type iff each pair of arguments is the same: types are identical, values are `is`-equal, functions are the same declaration (§5.18).
- `where` clauses on a generic type constrain its type parameters; they are checked at each instantiation (a compile error at the instantiation if violated) and assumed inside the type's declaration and its methods.
- A generic type is written applied: `List(T)`. Inside its own module, methods take it as `^List(T)`, with the type's parameters as their leading compile-time parameters.

### 16.5 Compile-time value parameters

A function parameter declared with `::` instead of `:` is a **compile-time value parameter**:

```
rotate :: fn(N :: usize, T: type, a: [N]T, by: usize) -> [N]T { … }
print  :: fn(A: type, template :: string, args: A = ()) \ {Out}
```

- Its argument must be a constant expression (§7.15). Each distinct argument value produces a separate instantiation, as for type parameters, so inside the body the parameter is a constant: it may size arrays and be passed to other compile-time parameters.
- The body is still checked once, treating the parameter as an unknown constant of its type. `when` conditions may not depend on it (§16.7).
- Its type must be one whose values can be compared with `is` at compile time: a plain-data type without pointers (§5.19), `string` (compared by contents), or a function pointer type `^fn(…)` or `?^fn(…)` (compared by declaration, §16.4). Two arguments give the same instantiation iff they are `is`-equal.
- Inside the body, an array length that depends on a compile-time parameter must be the parameter itself: `[N]T` is allowed, `[N + 1]T` is a compile error, because the body is checked once and array types must be comparable without knowing `N`. An array `[N]T` is constructed with `[N]T(fill: x)` or `---` (positional elements cannot be counted against an unknown `N`), and `a.len` is `N`.
- A compile-time value parameter that appears in the type of another parameter is inferable (`N` above, from `a: [N]T`) and is omitted or supplied together with the type parameters (§16.2). Any other compile-time value parameter (such as `template`) is bound like an ordinary parameter.
- A type parameter used only by an omitted defaulted parameter takes the default's type (`A` is `()` in `print("hi\n")`).

### 16.6 Generic effects

Effects may have parameters (`Yield(T)`). An effect set may mention type parameters of the enclosing generic function: `\ {Yield(T)}`. There is no abstraction over effect sets themselves (§12.8).

**Distinctness.** Because generic bodies are checked once, handler resolution must not depend on the instantiation. Two instantiations of the same generic effect are **provably distinct** if they differ in some argument position where both are concrete and unequal (`Yield(i32)` and `Yield(u8)`), or where one is concrete and the other is a different type constructor applied (`Yield(i32)` and `Yield(^T)`). Inside a generic function or type, two instantiations that are neither identical nor provably distinct may not meet in either of these ways:

- both in the declared effect set: `\ {Yield(T), Yield(U)}` and `\ {Yield(T), Yield(i32)}` are compile errors, since `T` might be `U` or `i32`. (A second-class parameter's effects are performed where it is called, which the next rule covers: `map :: fn(A, B: type, src: fn() \ {Yield(A)}, f: fn(a: A) -> B) \ {Yield(B)}` may loop `for a in src() { yield(f(a)) }`, since the loop's handler takes exactly the calls of `src`.)
- one performed inside a `with` that handles the other: `with (handler Yield(u32) { … }) { yield(x) }` with `x: T` is a compile error, since whether the handler catches the `yield` would depend on `T`. The implicit `with` of a `for` loop handles only the loop's source call (§13.2); performs in the loop body are handled outside the loop and are not affected by it.

### 16.7 Restrictions

- `when` conditions may not depend on type parameters or compile-time value parameters of an enclosing generic function or type (that would make "checked once" impossible). Use separate functions.
- A generic function does not convert to a function pointer, and there is no partial application of type arguments; write a non-generic wrapper (`max_i32 :: fn(xs: []const i32) -> ?i32 => max(xs)`).
- Recursion between generic instantiations must terminate: an instantiation that (transitively) requires an instantiation with a strictly larger type argument is a compile error after an implementation-defined depth (at least 64).

### 16.8 Structural hashing

`@hash(x, seed)` (§20.3) is defined for every type that satisfies `Eq`, and only those. It is derived from the structure of the type exactly as `is` is (§7.6): integers, `bool` and enums by value; floats by value with `-0.0` hashed as `0.0`; pointers by address; arrays, tuples and structs by their elements and fields in order (padding ignored); slices by length and content; unions by tag and payload; optionals and results by state and value. The guarantee is:

> if `a is b`, then `@hash(a, s) is @hash(b, s)` for every seed `s`.

The algorithm is implementation-defined, need not be stable across builds or targets, and must give good distribution (it is meant for hash tables, not for persistence or security). It is constant-evaluable for values that contain no pointers or function pointers. Because it is defined by the compiler from the type's structure, it never needs to be written by hand and cannot be overridden; a container that needs a different notion of equality takes its own `hash` and `eq` functions (§16.4).

### 16.9 No user-defined traits

Gamma has no user-defined traits, interfaces or typeclasses: no declaration that lets a type promise to provide named operations, and no mechanism that finds an implementation implicitly. The closed constraints of §16.3 are fixed by the language.

*Rationale.* Traits combine five jobs. Four are already done by other features, each with its cost visible at the use site; the fifth — implicit, compositional resolution of implementations — is what Gamma deliberately omits, because its rules (coherence, orphan rules, overlap, associated types, effect sets on trait methods) would be the largest and least predictable part of the language, and because a reader could not tell from a call which code runs. See Appendix C.4.

The idiomatic replacements:

| Need | Gamma idiom |
| --- | --- |
| Bound a generic body to operations on `T` | a closed constraint (`where Ord(T)`), or a function parameter |
| An algorithm that needs per-call behavior | a second-class function parameter: `xs.sort_by(fn(a, b) => a.score > b.score)` |
| Order structs | order a tuple of keys: `people.sort_by_key(fn(p) => (p.last, p.first))` |
| Hash or compare keys | nothing to write: `@hash` and `is` are structural; custom ones are `HashMap` parameters |
| A data structure with fixed, consistent behavior | a compile-time parameter of the type: `BTree(K, V, less: by_name)` |
| Print or log any value | `print("{} at {}", (name, pos))`, which formats every type structurally (§21.6); custom display is an ordinary function returning a string |
| Run-time dispatch over a closed set of cases | a tagged union and `match` |
| Run-time dispatch over an open set (plugins, drivers) | a struct holding a data pointer and a `^const` vtable of `^fn`s, as `Allocator` does (§15.2) |
| An implementation supplied by the caller's context | an effect and a handler (§12) |
| Generic arithmetic over user-defined number types | pass the operations as a constant struct: `dot(FIXED_RING, xs, ys)` with `Ring :: struct(T: type) { zero: T; add, mul: ^fn(a, b: T) -> T }` |
| Methods on a type | functions in the type's module, called with method syntax (§7.10) |
| Extend another module's type | a free function |

---

## 17. Compile-time evaluation

Gamma has no macro language and no separate compile-time sublanguage. Ordinary functions run at compile time when their effect set proves they cannot touch the outside world.

### 17.1 What can run at compile time

A call can be evaluated at compile time if:

- the callee is a Gamma function (not `foreign`) whose effect set is empty. Its body may still perform effects that it handles itself (an iterator consumed by a `for`, an `Alloc` served by a fixed-buffer allocator over a local array), because those never leave the evaluation. An empty set excludes `Sys`, and therefore foreign calls and `#global` access; and
- every argument is a constant expression (§7.15).

The compile-time evaluator implements the language exactly as the target would, including the target's integer sizes, layout and float semantics, with every checked operation checked (regardless of build mode) and every form of undefined behavior detected. A failing check, detected UB, a `trap`, or exceeding the step budget is a **compile error** that reports the compile-time call stack.

```
CRC32 :: crc_table(0xEDB88320)        // crc_table :: fn(poly: u32) -> [256]u32
```

### 17.2 Where compile-time evaluation happens

**Required.** Compile-time evaluation is required — and failure to evaluate is a compile error — for: top-level `::` initializers; typed constants `x: T : e`; array lengths; `when` conditions; arguments for compile-time parameters (type parameters and compile-time value parameters of functions, all parameters of generic types); default values of parameters and fields; `@static_assert`; and `#global` initializers.

**Opportunistic.** An immutable local `x :: e` whose initializer is a constant expression is evaluated at compile time if evaluation succeeds within the step budget; if it runs out of steps, it is evaluated at run time instead (no error). Any other failure (a failing check, UB, `trap`) is still a compile error, since the same call would fail at run time.

### 17.3 Step budget

Each required evaluation has a budget of evaluation steps (implementation-defined, at least 10⁷ per top-level declaration). `#fuel(n)` on a declaration raises its budget to `n`. Exceeding the budget in a required evaluation is a compile error naming the call.

### 17.4 What a constant may contain

A compile-time value that becomes part of the program (a top-level constant, a `#global` initializer, an opportunistically-evaluated local) is placed in static memory. Its type and value are restricted:

- It may contain pointers only of `const` kinds (`^const T`, `[^]const T`, `[]const T`, `cstring`, `?` of these) and function pointers. A mutable pointer, `rawptr`, `cont`, `Allocator`, or second-class value in a constant is a compile error — except in a `#global` initializer, where `null` is allowed for optional pointers.
- Every pointer in it must point into static data: a string literal or another top-level constant (or into the constant itself). A pointer into memory that existed only during the evaluation — its stack frames or buffers it allocated from — is a compile error.

So a table built at compile time is returned by value (`[N]Entry`), and other constants may refer to it with `const` slices.

### 17.5 Compile-time-only values

Values of type `type`, effect types, modules, and untyped constants exist only at compile time. They may be bound with `::`, passed to compile-time parameters, compared with `is` (types), and used in `when`, but cannot be stored in run-time variables.

### 17.6 Foreign code

`foreign` functions are never evaluated at compile time, whatever their effect annotation (§19.2). A function that calls one is not compile-time evaluable.

### 17.7 Static assertions

`@static_assert(cond, "message")` may appear as a top-level declaration or statement. `cond` must be a compile-time `bool`; if it is `false`, compilation fails with the message.

```
@static_assert(@size_of(Header) is 16, "Header must match the on-disk format")
```

---

## 18. Safety, checks and undefined behavior

### 18.1 The one rule

Every run-time check in Gamma follows one rule: **it traps in Debug and Checked builds and is removed in Fast builds.** There is no syntax to disable a check locally. Implementations remove checks they can prove cannot fail, in every mode. Two checks are exceptions and are never removed: integer division by zero and signed division overflow (§7.4.5).

### 18.2 Checked operations

| Operation | Condition checked | In Fast builds, on failure |
| --- | --- | --- |
| `+ - * <<` and unary `-` on integers (plain flavor) | no overflow | unspecified value |
| shift by ≥ the width (any flavor) | amount < width | unspecified value |
| `x.trunc(T)`, `.round`, `.floor`, `.ceil` float → integer | in range, not NaN | unspecified value |
| `x?` | not `null` | unspecified value |
| `x!` | not an error | unspecified value |
| `a[i]` on arrays and slices | `i < len` | undefined behavior |
| slicing `a[lo..hi]` | `lo <= hi <= len` | undefined behavior |
| `p.cast(P)` | address aligned for `P` | undefined behavior on use |
| `@min_stack` and fiber stack overflow | enough stack | undefined behavior |
| consuming a continuation twice; `with` completing with unconsumed continuations; consuming on the wrong `with` or thread | §14.2 rules | undefined behavior |
| `expect`, `assert` (§21.6) | condition is true | `assert`: removed; `expect`: tests only |
| integer `/`, `%` | divisor ≠ 0; not `MIN / -1` | **always traps** |

An *unspecified value* is a valid value of the result type (§1.3): using it cannot by itself cause UB. However, an unspecified *pointer* (from `x?` on a null `?^T` in Fast builds) may be dereferenced only at the risk of UB, as with any invalid pointer.

### 18.3 Debug build extras

In addition to performing all checks, a Debug build must:

- fill `---` storage, freed allocations and dead stack frames with recognizable patterns (`0xAA`, `0xDD`);
- tag each allocation with its allocator and trap when it is freed through a different one, reporting both sites;
- place guard pages (or equivalent probes) on fiber stacks;
- use the leak-checking allocator for tests;
- report, with a trap's location, the calls that led to it: each call's location and the function it lies in, innermost first. Implementations may leave out frames of the core library and shorten long reports.

Debug builds may skip optimizations, including handler specialization; this never changes meaning.

### 18.4 Undefined behavior

The following, and only the following, have undefined behavior in Checked and Fast builds:

1. Accessing an object outside its lifetime: use after `free`, use of a dangling pointer to a dead local or fiber stack.
2. Accessing memory through a pointer that does not point to an object of a compatible type (including misaligned access, and a `downcast_unchecked` to the wrong type).
3. Indexing or slicing out of bounds in Fast builds (§18.2), and any access through a many-pointer outside its allocation; pointer arithmetic that leaves an allocation (other than one-past-the-end); subtracting or comparing many-pointers into different allocations.
4. Writing to an object through a pointer derived (by `cast`) from a `const` pointer when the object is a constant, string literal, or otherwise in read-only static memory.
5. Reading a `bool`, union, optional, result, pointer, or function pointer whose bytes are not a valid value of its type (reachable only through `#c_union`, foreign code, or `cast`).
6. Freeing memory through the wrong allocator, or twice.
7. A data race (§19.6).
8. The Fast-build cases marked "undefined behavior" in §18.2.
9. Violating the contract of a foreign function, or a foreign function's own undefined behavior.
10. Volatile, atomic, or assembly operations used against their stated requirements (§19).

Everything else is defined. In particular, integer overflow, reading `---` storage, NaN handling, and shift amounts never cause UB.

### 18.5 Traps

A trap terminates the process immediately (Debug and Checked builds report the location and reason to standard error first). It does not unwind, run `defer` statements, or return to any handler. `trap(msg)` (§21.6) traps unconditionally in every build mode.

### 18.6 What Checked mode does not check

Checked mode checks local facts: arithmetic, bounds, unwraps and the continuation rules. It does not check use-after-free, dangling pointers, wrong-allocator frees or data races; those need tracking that only Debug builds (or external tools) pay for. The mode is called Checked, not Safe, for this reason.

---

## 19. Interoperability and low-level facilities

### 19.1 C-compatible types

A type is **C-compatible** if it is one of:

| Gamma | C |
| --- | --- |
| `u8 u16 u32 u64`, `i8 i16 i32 i64` | `uint8_t` … `int64_t` |
| `u128`, `i128` | `unsigned __int128`, `__int128` (where the C ABI has them) |
| `usize`, `isize` | `size_t`, `ptrdiff_t` |
| `f32`, `f64` | `float`, `double` |
| `bool` | `_Bool` |
| `^T`, `^const T`, `[^]T`, `[^]const T`, `rawptr`, `cstring`, and `?` of each | `T *`, `const T *`, `void *`, `const char *` (nullable only when `?`) |
| `^fn(…) -> R` with C-compatible parameters and result, empty effect set or `{Sys}` | function pointer |
| `[N]T` of a C-compatible `T` (as a struct field) | `T[N]` |
| `struct` whose fields are all C-compatible (including `#packed`, `#align`) | `struct` with the same layout |
| `#c_union` whose fields are all C-compatible | `union` |
| `#bits(U)` struct | `U` (C bitfields have implementation-defined layout, so Gamma exposes the underlying integer instead) |
| `()` as a return type | `void` |

The module `std/c` provides aliases for C's platform-dependent types: `c_char`, `c_schar`, `c_uchar`, `c_short`, `c_ushort`, `c_int`, `c_uint`, `c_long`, `c_ulong`, `c_longlong`, `c_size_t`, and so on.

Unions (tagged), optionals of non-pointers, results, slices, tuples, arrays as parameters, second-class functions and `cont` are not C-compatible.

### 19.2 Foreign functions

```
c    :: import "std/c"               // C's platform-dependent types (§19.1)
libc :: foreign "c" {
    puts   :: fn(s: cstring) -> c.c_int
    qsort  :: fn(base: rawptr, n, size: usize, cmp: ^fn(a, b: rawptr) -> c.c_int)
    printf :: fn(fmt: cstring, ...) -> c.c_int
    strlen :: fn(s: cstring) -> usize \ {}          // trusted: performs no effects
    #global errno: c.c_int                            // a foreign variable
}
```

- `foreign "lib" { … }` declares functions (and `#global` variables) provided by the named library ("c" is the C standard library; other names are passed to the linker). The block is bound to a name like a module. `#link_name("sym")` on a member overrides the symbol name.
- Parameter and result types must be C-compatible.
- A foreign function's effect set is `{Sys}` unless annotated. An annotation with a smaller set is a **trusted assertion** by the programmer that the C function touches nothing but its arguments; it is not verified. Foreign functions are never evaluated at compile time regardless (§17.6).
- **Variadic** functions are declared with a final `...`. Arguments passed in the variadic position must have C-compatible scalar types of at least C `int` width for integers and `f64` for floats (C's default argument promotions are not applied implicitly; convert explicitly, e.g. `c.c_int(x)`, `f64(y)`). Gamma functions cannot be variadic.
- Narrowing at the boundary (`usize` lengths to `c.c_uint`, and so on) is never implicit: write `n.trunc(c.c_uint)` or `n.checked(c.c_uint) else …`. `T(x)` checks the *type's* range, never a proven value range, so that legality never depends on how clever the compiler's analysis is.
- **Calling convention**: the target's C ABI by default. `#callconv(.X)` selects another: `.C` (the target's C ABI, the default, so it only states the default), `.SysV` (the System V AMD64 ABI) and `.Win64` (the Microsoft x64 ABI). `.SysV` and `.Win64` exist only where `target.arch` is `.X86_64`; naming them on another architecture, or naming any other convention, is an error. On the supported x86-64 targets the C ABI is System V, so `.SysV` means exactly `.C`; only `.Win64` differs.
  - The attribute applies to a top-level function declaration, to a foreign member (it describes the C function being called), and to a `^fn` type: `#callconv(.Win64) ^fn(a: i32) -> i32`. Anywhere else it is an error.
  - A function or function pointer type with a convention other than `.C` has the plain calling convention (§19.1): its effect set must be empty or `{Sys}`, and a function must be top-level, non-generic and capture nothing. A function literal that captures nothing and is written where such a pointer type is expected has that convention.
  - The convention is part of a function pointer type: `#callconv(.Win64) ^fn(i32) -> i32` and `^fn(i32) -> i32` are different types and no coercion converts between them, including the effect-widening coercion of §11.6. A function with a convention other than `.C` used as a value is a `^fn` carrying its convention; it cannot become a second-class `fn` value, which always uses the language's own convention. `@fiber` (§14.6) and thread entry points (§19.6) take `.C` pointers only.
  - Calls, direct or through a pointer of the right type, use the function's convention. An `#export`ed function (§19.4) is exported with its convention. The convention changes no Gamma semantics: the interpreter and compile-time evaluation ignore it.

### 19.3 Global variables

```
#global last_error: i32 = 0
```

`#global name: T = e` declares a mutable variable with static storage. `e` must be a compile-time constant (§17.4). Every read or write of a `#global`, and taking its address, performs `Sys`; accesses through a pointer obtained that way do not (they are ordinary memory accesses, §19.6). `#global` inside a `foreign` block declares a variable defined by foreign code (no initializer).

`#thread_local` may be added to make one instance per thread.

*Rationale:* requiring `Sys` keeps functions without `Sys` free of hidden state, which is what makes compile-time evaluation and test handlers sound.

### 19.4 Exported functions

```
#export("gm_checksum")
checksum :: fn(data: [^]const u8, len: usize) -> u32 { … }

#export("gm_load")
load :: fn(path: cstring) -> c.c_int \ {Sys} {
    with heap_allocator(), stderr_log(), real_fs() {
        cfg := load_config(path_from(path)) else e => { last_error = code(e); return -1 }
        …
    }
}
```

`#export("sym")` gives a top-level function external linkage under `sym` with the C ABI, or with the function's `#callconv` (§19.2). Its signature must be C-compatible and its effect set must be empty or `{Sys}`, because C callers cannot supply handlers; it installs whatever handlers it needs, using `Sys`-level constructors from the library (`heap_allocator()`, `real_fs()`, …). The same restriction applies to any function pointer passed to C, which is enforced by the C-compatibility of `^fn` types (§19.1).

A trap inside an exported function terminates the process; it does not return to C.

### 19.5 Low-level memory access

All of these perform `Sys`.

| Intrinsic | Meaning |
| --- | --- |
| `@volatile_load(p)` | load `p^` (`p: ^const T`, `T` integer, float, pointer or `#bits`), never elided, merged or reordered with other volatile accesses |
| `@volatile_store(p, v)` | store `v` to `p^` likewise |
| `@asm(template, outputs, inputs, clobbers)` | inline assembly (below) |

`@asm(template, outputs, inputs, clobbers)` is a statement-like expression of type `()` that executes inline assembly:

- `template` is a compile-time string in the syntax of the target's GCC-compatible assembler, with operands referred to as `%0`, `%1`, … (outputs first, then inputs) as in GNU C.
- `outputs` is a list of pairs `(constraint, p)`, where `constraint` is a compile-time string such as `"=r"` or `"+r"` and `p: ^T` points to the place the operand is written to (and read from, for `+`).
- `inputs` is a list of pairs `(constraint, v)` with values `v`.
- `clobbers` is a list of compile-time strings naming registers, `"cc"` or `"memory"`.
- A list is written `()` when empty, as its single entry, or as a tuple of entries: `("=r", &x)` is one output, `(("=r", &x), ("=r", &y))` two.
- Operand types are integers of 8 to 64 bits, `usize`, `isize`, `f32`, `f64`, `bool` and pointers (`T` for outputs).
- The assembly is never removed, duplicated or reordered with other `@asm` or volatile accesses. Its meaning, and any misuse of constraints, registers or memory, is up to the target: violating the compiler's assumptions is undefined behavior. It is never evaluated at compile time.

### 19.6 Threads and atomics

Gamma adopts the C11 memory model.

- **Threads** are created by the library (`std/thread`), which requires `Sys`. A thread starts by calling a function pointer `^fn(arg: rawptr) \ {Sys}` (or with an empty set); it begins with no handlers installed and installs its own. Second-class values, handlers and continuations cannot cross threads, because only function pointers and data can be passed.
- A **data race** — two accesses to the same memory location from different threads, at least one a write, at least one non-atomic, not ordered by happens-before — is undefined behavior.
- **Atomics** are intrinsics on integer, `bool` and pointer places, with an explicit memory order — a value of the core type `MemoryOrder`: `.Relaxed`, `.Acquire`, `.Release`, `.AcqRel` or `.SeqCst` — as in C11:

| Intrinsic | Result |
| --- | --- |
| `@atomic_load(p, order)` | `T` |
| `@atomic_store(p, v, order)` | `()` |
| `@atomic_swap(p, v, order)` | old value |
| `@atomic_cas(p, expected, new, success, failure)` | `?T`: `null` on success, the observed value on failure |
| `@atomic_add(p, v, order)`, `_sub`, `_and`, `_or`, `_xor`, `_max`, `_min` | old value (wrapping arithmetic) |
| `@fence(order)` | `()` |

`p` is a `^T`. Atomic intrinsics do not require `Sys`: they are memory operations on memory the function can already reach.

*Rationale:* a C replacement must be able to write a thread pool and a lock-free queue. Adopting C11's model means existing knowledge and tools (ThreadSanitizer) apply unchanged. Effects provide the structure on top: a thread's handlers are its own, so handler literals capturing locals are never shared.

### 19.7 Layout guarantees summary

| Type | Guaranteed layout |
| --- | --- |
| integers, floats, `bool` | §5.2–5.4 |
| pointers, `?` pointers | one address; null is 0 |
| arrays | §5.6 |
| slices | `(ptr, len)` |
| structs | C layout in declaration order (§5.10.4) |
| tagged unions | `{ tag, payload }` (§5.11) |
| `#c_union`, `#bits` | C union; underlying integer |
| tuples | as a struct of the same fields |
| `?T` (non-pointer), `E!T`, `cont`, second-class values | unspecified |

---

## 20. Attributes and intrinsics

### 20.1 Syntax

```
attribute = '#' ident [ '(' [ expr % ',' ] ')' ]
intrinsic = '@' ident [ '(' [ arg % ',' ] ')' ]
```

Attributes precede the declaration, field, or type literal they modify. Intrinsics are expressions. Both sets are closed; an unknown name is a compile error.

### 20.2 Attributes

| Attribute | Applies to | Meaning | § |
| --- | --- | --- | --- |
| `#packed` | struct literal | no padding, alignment 1 | 5.10.4 |
| `#align(n)` | struct literal, field, `#global` | raise alignment to `n` | 5.10.4 |
| `#bits(U)` | struct literal | bit-packed into `U` | 5.10.5 |
| `#c_union` | union literal | untagged C union | 5.11.1 |
| `#export("sym")` | top-level function | external C-ABI symbol | 19.4 |
| `#link_name("sym")` | foreign member | symbol name | 19.2 |
| `#callconv(.X)` | top-level function, foreign member, `^fn` type | calling convention: `.C` (default), `.SysV`, `.Win64` (x86-64 only); part of a `^fn` type; plain functions only, never second-class | 19.2 |
| `#global` | binding | mutable static storage | 19.3 |
| `#thread_local` | `#global` | per-thread storage | 19.3 |
| `#inline`, `#noinline`, `#cold` | function | inlining hints/requirements | 11.10 |
| `#fast_math` | function | relaxed float semantics | 5.3 |
| `#fuel(n)` | top-level declaration | compile-time step budget | 17.3 |
| `#deprecated`, `#deprecated("msg")` | declaration | a warning at each use, quoting the message | — |

### 20.3 Intrinsics

| Intrinsic | Result | Compile-time | § |
| --- | --- | --- | --- |
| `@size_of(T)`, `@align_of(T)` | `usize` | yes | 5 |
| `@offset_of(S, field)` | `usize` | yes | 5.10.4 |
| `@type_of(e)` | `type` (`e` is not evaluated) | yes | — |
| `@type_name(T)` | `string` | yes | — |
| `@location()` | `SourceLocation` (file, line, column, function) of the call | yes | — |
| `@caller_location()` | as above, of the caller (only as a parameter default) | yes | — |
| `@static_assert(c, msg)` | `()` | yes | 17.7 |
| `@compile_error(msg)` | `never`; a compile error if the call is in a taken `when` branch or compiled code | yes | — |
| `@hash(x, seed: u64)` | `u64`; structural hash, for `Eq` types | yes | 16.8 |
| `@write(sink, template, args)` | `()`; structural formatting into a sink | yes, if `sink` is | 21.6 |
| `@stack_size(f)` | `usize` | yes | 14.8 |
| `@min_stack` | `usize` | yes | 14.1 |
| `@fiber(f, arg, stack)` | `cont(())` | no | 14.6 |
| `@volatile_load`, `@volatile_store`, `@asm` | | no | 19.5 |
| `@atomic_*`, `@fence` | | no | 19.6 |
| `@unreachable()` | `never`; checked: traps in Debug/Checked, UB in Fast | no | 18 |
| `@expect_true(b)`, `@expect_false(b)` | `bool`; branch-probability hint | yes | — |
| `@prefetch(p)` | `()`; hint | no | — |
| `@clz(x)`, `@ctz(x)`, `@popcount(x)` | `u32` (`@clz(0)` and `@ctz(0)` are the bit width) | yes | — |
| `@bswap(x)` (whole bytes only), `@bitreverse(x)` | `x`'s type | yes | — |
| `@rotl(x, n)`, `@rotr(x, n)` (`n: u32`, taken modulo the width; rotates the bit pattern, also of signed `x`) | `x`'s type | yes | — |
| `@sqrt(x)`, `@fma(a, b, c)` and the IEEE operations of `std/math` | float | yes | — |

---

## 21. The core library

The core library is the part of the standard library that the language definition depends on. Everything else (file and network APIs, further collections, text processing, …) is outside the scope of this specification.

### 21.1 The universe scope

These names are predeclared in every module, without import. The type names in the first row are reserved and may not be redeclared anywhere; the others may be hidden by a local or top-level declaration of the same name (§4.6).

| Category | Names |
| --- | --- |
| reserved type names | `u1`…`u65535`, `i1`…`i65535`, `usize`, `isize`, `f16`, `f32`, `f64`, `bool`, `never`, `type`, `rawptr`, `cont` |
| types | `string`, `cstring`, `rune`, `Range`, `RangeIncl`, `Allocator`, `AllocVtable`, `AllocError`, `IoError`, `Instant`, `Duration`, `SourceLocation`, `MemoryOrder`, `List`, `HashMap` |
| effects | `Yield`, `Alloc`, `Out`, `Err`, `In`, `Clock`, `Rand`, `Fs`, `Net`, `Env`, `Sys` |
| operations (from the effects) | `yield`, `current`, `write_out`, `write_err`, … (each effect's operations, §12.1) |
| functions | `range`, `range_incl`, `each`, `each_ptr`, `enumerate`, `reversed`, `bytes`, `runes`, `empty_slice`, `new`, `make`, `make_fill`, `make_aligned`, `free`, `free_aligned`, `destroy`, `list`, `map`, `map_with`, `print`, `eprint`, `trap`, `assert`, `expect`, `ns`, `us`, `ms`, `secs`, and the methods of `Instant` and `Duration` (§21.7) |
| modules | `target` (§3.6) |

### 21.2 Allocation

```
new          :: fn(T: type, init: T) -> AllocError!^T \ {Alloc}
make         :: fn(T: type, n: usize) -> AllocError![]T \ {Alloc} where Plain(T)
make_fill    :: fn(T: type, n: usize, x: T) -> AllocError![]T \ {Alloc}
make_aligned :: fn(T: type, n: usize, align: usize) -> AllocError![]T \ {Alloc} where Plain(T)
free         :: fn(T: type, s: []const T) \ {Alloc}
free_aligned :: fn(T: type, s: []const T, align: usize) \ {Alloc}
destroy      :: fn(T: type, p: ^const T) \ {Alloc}
```

`make` returns uninitialized storage (filled with `0xAA` in Debug builds), so it requires a plain-data element type; `make_fill` works for any type. `new(T, ---)` is allowed when `T` is plain-data (§4.4).

`free(s)` and `destroy(p)` release storage obtained from the current allocator; the slice or pointer must be exactly what was returned (§15.2). Freeing an empty slice is a no-op. Storage from `make_aligned(T, n, align)` is released with `free_aligned(s, align)`, passing the same `align`, so that the allocator gets back the alignment it allocated with.

Each of these functions also has a final parameter `site: SourceLocation = @caller_location()` (bound only by name, §11.3). Debug builds use the sites to report where a block freed through the wrong allocator was allocated and freed (§18.3).

### 21.3 Builtin methods

Builtin methods are resolved by §7.10 rule 2. `T` is a type argument.

**All integers:** `trunc(T)`, `checked(T)`, `sat(T)`, `bits(T)`, `round(F)` (to float), `min(y)`, `max(y)`, `clamp(lo, hi)`, `abs()` (signed; checked like `-`), `abs_diff(y)` (unsigned result), `pow(n: u32)` (checked), and the overflow-flavored `pow_wrap`, `pow_sat`, `pow_checked`.

**All floats:** `trunc(T)`, `round(T)`, `floor(T)`, `ceil(T)`, `checked(T)`, `sat(T)` (to integer); `round(F)` (to narrower float); `bits(T)`; `is_nan()`, `is_inf()`, `is_finite()`, `sign_bit()`; `min(y)`, `max(y)` (IEEE `minimumNumber`/`maximumNumber`); `abs()`, `sqrt()`.

**Pointers:** `cast(P)`, `addr()`; on `^S` and `^const S` with `using`, `downcast_unchecked(Outer)`.

**`usize`:** `to_ptr(P)`.

**Arrays and slices** (`a` of element type `T`): `len` (field-like, no parentheses), `ptr` (slices), `get(i) -> ?T`, `get_ptr(i) -> ?^T`, `first() -> ?T`, `last() -> ?T`, `cursor()`, `copy_from(src: []const T)` (lengths must be equal: checked; overlap allowed), `fill(x: T)`, `starts_with(p)`, `ends_with(p)`, `index_of(x) -> ?usize` (`Eq`), `swap(i, j)`, `sort()` (`Ord` elements), `sort_by(less: fn(a, b: ^const T) -> bool)`, `sort_by_key(key: fn(x: ^const T) -> K)` (`Ord(K)`), `min()`, `max()` (`Ord`; `?T`), `binary_search(x) -> (found: bool, at: usize)` (`Ord`). `stable_sort()`, `stable_sort_by(less)` and `stable_sort_by_key(key)` are the stable variants. All sorts are in place and allocate nothing. If `<` (or `less`) is not a strict weak ordering on the elements — for example, floats including NaN — the result is an unspecified permutation of the input: never a trap or undefined behavior. `min`, `max` and `binary_search` likewise give an unspecified element or index in that case.

**Strings** additionally: `cstr_len()` on `cstring`, `utf8_valid()`.

**Unions:** `tag()`; on the type, `from_tag(x) -> ?U` (enums only).

**Optionals and results:** none beyond the operators of §10.

**Ranges:** `lo`, `hi` (fields), `len()`.

### 21.4 Iterators

```
range      :: fn(T: type, lo, hi: T) \ {Yield(T)} where Int(T)
range_incl :: fn(T: type, lo, hi: T) \ {Yield(T)} where Int(T)
each       :: fn(T: type, xs: []const T) \ {Yield(T)}
each_ptr   :: fn(T: type, xs: []T) \ {Yield(^T)}
enumerate  :: fn(T: type, xs: []const T) \ {Yield((i: usize, x: T))}
reversed   :: fn(T: type, xs: []const T) \ {Yield(T)}
bytes      :: fn(s: string) \ {Yield(u8)}
runes      :: fn(s: string) \ {Yield(rune)}
```

`range_incl(lo, hi)` yields `hi` without overflowing, and yields nothing if `lo > hi`. All are `#inline`.

### 21.5 Containers (summary)

`List(T)` and `HashMap(K, V, …)` are in the universe scope because they are used throughout the language's examples and by the test runner. Each is declared, with its functions, in its own core module (`core/list`, `core/map`), whose public names are re-exported to the universe scope; method calls on them resolve in those modules (§7.10). Their required interface:

```
// core/list
List   :: struct(T: type) { pub items: []T; cap: usize; alloc: Allocator }    // items.len is the length
list   :: fn(T: type) -> List(T) \ {Alloc}                        // captures current()
push   :: fn(T: type, l: ^List(T), x: T) -> AllocError!()
extend :: fn(T: type, l: ^List(T), xs: []const T) -> AllocError!()
insert :: fn(T: type, l: ^List(T), i: usize, x: T) -> AllocError!()  // traps if i > len
reserve :: fn(T: type, l: ^List(T), extra: usize) -> AllocError!()    // room for `extra` more
pop    :: fn(T: type, l: ^List(T)) -> ?T
remove :: fn(T: type, l: ^List(T), i: usize) -> T                  // keeps the order
swap_remove :: fn(T: type, l: ^List(T), i: usize) -> T             // moves the last element into i
truncate :: fn(T: type, l: ^List(T), n: usize)                      // keeps the first n
clear  :: fn(T: type, l: ^List(T))
deinit :: fn(T: type, l: ^List(T))                                 // frees through l.alloc

// core/map
HashMap  :: struct(K, V: type,
                   hash: ?^fn(key: ^const K, seed: u64) -> u64 = null,   // null: @hash
                   eq:   ?^fn(a, b: ^const K) -> bool = null)            // null: `is`
map      :: fn(K, V: type) -> HashMap(K, V) \ {Alloc} where Eq(K)
map_with :: fn(K, V: type, hash :: ^fn(key: ^const K, seed: u64) -> u64,
               eq :: ^fn(a, b: ^const K) -> bool) -> HashMap(K, V, hash, eq) \ {Alloc}
put      :: fn(m: ^HashMap(…), k: K, v: V) -> AllocError!()
get      :: fn(m: ^const HashMap(…), k: K) -> ?V
get_ptr  :: fn(m: ^HashMap(…), k: K) -> ?^V                         // valid until the next put/remove
get_or_put :: fn(m: ^HashMap(…), k: K, default: V) -> AllocError!^V // stores `default` if absent
contains :: fn(m: ^const HashMap(…), k: K) -> bool
remove   :: fn(m: ^HashMap(…), k: K) -> ?V
len      :: fn(m: ^const HashMap(…)) -> usize
entries  :: fn(m: ^const HashMap(…)) \ {Yield((key: K, value: V))}   // unspecified order
clear    :: fn(m: ^HashMap(…))                                      // keeps the storage
deinit   :: fn(m: ^HashMap(…))
```

Signatures are abbreviated: the `HashMap` functions take the map type's parameters as leading compile-time parameters, inferred from the receiver. `Eq(K)` is required when `hash` or `eq` is `null`. A custom `hash` must agree with the map's equality: keys equal under `eq` (or `is`) must hash equally. Each map stores an implementation-defined seed chosen at construction without performing effects; copies of the map keep it. Both containers capture `current()` at construction and never perform `Alloc` afterwards (§15.2). `for x in l.items` iterates a list. The pointers from `get_ptr` and `get_or_put` are invalidated by the next insertion or removal.

### 21.6 Diagnostics and output

```
trap   :: fn(msg: string = "trap", loc: SourceLocation = @caller_location()) -> never
assert :: fn(cond: bool, msg: string = "assertion failed", loc: SourceLocation = @caller_location())
expect :: fn(cond: bool, msg: string = "expectation failed", loc: SourceLocation = @caller_location())
print  :: fn(A: type, template :: string, args: A = ()) \ {Out}
eprint :: fn(A: type, template :: string, args: A = ()) \ {Err}
```

- `trap` traps in every build mode (§18.5).
- `assert` is a checked operation: it traps on `false` in Debug and Checked builds and is removed (its argument not evaluated) in Fast builds.
- `expect` may be called only in tests; on `false` it fails the current test (reporting the location) and stops it.
- `print(t, args)` is `@write(fn(b) => write_out(b), t, args)`, and `eprint` likewise with `write_err`.

#### 21.6.1 Formatting: `@write`

```
@write(sink, template, args)
```

`sink` is a second-class function `fn(bytes: []const u8)`; `template` is a compile-time `string`: a literal, a constant, or a `string` compile-time parameter of the enclosing generic function, so that functions like `log(template :: string, args: A)` can forward to `print` or `@write`; `args` holds the arguments. `@write` calls `sink` zero or more times with consecutive pieces of output, allocating nothing. Effects performed by `sink` are the effects of the `@write` expression.

**Template.** `{}` is a placeholder; `{{` and `}}` write `{` and `}`; any other use of braces is a compile error. The number of placeholders *n* decides how `args` is read, independent of whether its type is a tuple:

- *n* = 0: `args` must be `()`.
- *n* = 1: `args` is the one argument, whatever its type (a tuple prints as a tuple).
- *n* ≥ 2: `args` must be a tuple of exactly *n* elements, the arguments in order.

Since the template is a compile-time parameter, a mismatch is reported at the instantiating call (§16.1).

**Display rules.** Each argument is written according to its type:

| Type | Written as |
| --- | --- |
| `string` or `[]u8` as an argument itself | its bytes, unchanged |
| integers | decimal |
| floats | the shortest decimal that reads back as the same value at the argument's type, without a trailing `.0`: positional notation when 10⁻⁶ ≤ \|x\| < 10²¹ (`1.5`, `-2`, `0.001`), otherwise `<digits>e<exponent>` (`1e21`, `2.5e-7`); `-0.0` is `-0`; `nan`, `inf`, `-inf` |
| `bool` | `true`, `false` |
| enums | `.Name` |
| tagged unions | `.Name(payload)`; a tuple payload's elements are written directly: `.Rect(w: 2, h: 3)` |
| structs | `Name(field: value, …)`, all fields (including private ones) in declaration order |
| tuples | `(a, b)`, or `(name: a, …)` |
| arrays, slices | `[a, b, …]`; a `string` or `[]u8` anywhere other than as an argument itself (inside an aggregate, optional or result) is written quoted with escapes |
| `?T` | the value, or `null` |
| `E!T` | the value, or `!` followed by the error |
| pointers of every kind (including `cstring`), function pointers | `0x` and the address in hexadecimal; to print a C string, slice it: `s[0..s.cstr_len()]` |
| `#c_union`, `cont`, `Allocator` internals | `<TypeName>` |

The rules are fixed and not extensible: there is no way for a type to customize its formatting. A type that wants a particular presentation provides a function that produces it (`date.iso(d, &buf)`), and the caller passes the result as a `string`. Other bases, widths and precisions are library functions over buffers (`fmt.hex(n, &buf)`).

```
print("hello, world\n")
print("{} scored {} ({})\n", (player.name, score, rank))     // Ada scored 1200 (.Gold)
print("{}\n", pos)                                           // Vec2(x: 1.5, y: -2)
```

### 21.7 Core types and ambient effect signatures

```
IoError :: union {
    NotFound, PermissionDenied, AlreadyExists, WouldBlock, Interrupted,
    BrokenPipe, ConnectionReset, InvalidInput, UnexpectedEof, NoSpace, ConnectionRefused,
    Other: i32                                  // OS error code
}
Instant  :: struct { pub ns: u64 }              // monotonic, since an unspecified epoch
Duration :: struct { pub ns: u64 }
// methods: i.since(earlier) -> Duration (checked: not earlier than `earlier`), i.after(d) -> Instant,
// d.plus(d2), d.minus(d2) (checked), d.to_ms() -> u64 (whole milliseconds);
// constructors ns(n), us(n), ms(n), secs(n) -> Duration (n: u64; checked)
SourceLocation :: struct { pub file: string; pub line, column: u32; pub function: string }

Out   :: effect { write_out: fn(bytes: []const u8) }
Err   :: effect { write_err: fn(bytes: []const u8) }
In    :: effect { read: fn(buf: []u8) -> IoError!usize }      // 0 at end of input
Clock :: effect { now: fn() -> Instant; wall: fn() -> i64; sleep: fn(d: Duration) }
Rand  :: effect { fill: fn(buf: []u8) }
Env   :: effect {
    args: fn() -> []const string
    var:  fn(name: string) -> ?string
    exit: esc(code: u8) -> never
}
```

`Clock.wall` gives Unix time in nanoseconds. `Fs` and `Net` are declared by the core library with operations for files, directories and sockets whose exact set is outside the scope of this specification; the language requires only that they exist, report failures as `IoError`, and have the default and test handlers of §12.9. The reference core library's `Fs` is:

```
File, Dir :: struct { … }                       // handles
OpenOptions :: struct { pub read: bool = true; pub write, append, create, truncate, exclusive: bool = false }
FileInfo :: struct { pub size: u64; pub is_dir: bool; pub modified: i64 }   // Unix nanoseconds
SeekFrom :: union { Start, Current, End }
Fs :: effect {
    open: fn(path: string, opts: OpenOptions) -> IoError!File
    close: fn(f: File)
    read_from: fn(f: File, buf: []u8) -> IoError!usize          // 0 at the end of the file
    write_to: fn(f: File, bytes: []const u8) -> IoError!usize   // may write fewer bytes
    seek: fn(f: File, offset: i64, from: SeekFrom) -> IoError!u64
    stat: fn(path: string) -> IoError!FileInfo
    remove, make_dir, remove_dir: fn(path: string) -> IoError!()
    rename: fn(from, to: string) -> IoError!()
    open_dir: fn(path: string) -> IoError!Dir
    next_entry: fn(d: Dir, buf: []u8) -> IoError!usize         // a name (never . or ..); 0 after the last
    close_dir: fn(d: Dir)
}
```

and its `Net` (TCP) is:

```
Socket, Listener :: struct { … }               // handles
Net :: effect {
    connect: fn(host: string, port: u16) -> IoError!Socket
    listen: fn(host: string, port: u16) -> IoError!Listener     // port 0: any free port
    local_port: fn(l: Listener) -> IoError!u16
    accept: fn(l: Listener) -> IoError!Socket
    send: fn(s: Socket, bytes: []const u8) -> IoError!usize       // may send fewer bytes
    recv: fn(s: Socket, buf: []u8) -> IoError!usize               // 0 once the peer has closed
    close_socket: fn(s: Socket)
    close_listener: fn(l: Listener)
}
```

Operations whose first parameter is a `File`, `Dir`, `Socket` or `Listener` are called as methods (`f.read_from(buf)`), by §7.10 rule 3: an effect operation is a function of the module that declares the effect (§12.1). `Env.exit`'s default handler unwinds to `main` (running `defer`s) and exits with the code.

---

## Appendix A. Grammar

This grammar is normative. Notation is that of §1.2. Terminals in `'quotes'`. `NL` is a significant newline or `;` (§2.8). Lexical productions are in §2. Where the grammar is ambiguous, the disambiguation rules listed in §A.7 apply. In every brace-delimited list (blocks, struct fields, union variants, effect operations, handler clauses, match arms, foreign members, top-level `when` bodies), the `NL` after the last element may be omitted, so short bodies fit on one line: `handler Log { log(l, m) => {} }`.

### A.1 Files and declarations

```
file          = { NL } { top_decl NL { NL } }

top_decl      = { attribute } [ 'pub' ] binding
              | 'when' expr top_block { 'else' 'when' expr top_block } [ 'else' top_block ]
              | handler_decl
              | 'test' STRING block
              | intrinsic_call                                  // @static_assert(...)
top_block     = '{' { NL } { top_decl NL { NL } } '}'

binding       = IDENT '::' expr
              | IDENT ':' type ':' expr
              | IDENT ':' type '=' expr                         // only inside functions or with #global
              | IDENT ':=' expr                                 // only inside functions
              | ident_list ( '::' | ':=' ) expr                 // destructuring
              | ident_list ':' type ( ':' | '=' ) expr
ident_list    = IDENT ',' IDENT { ',' IDENT }

attribute     = '#' IDENT [ '(' [ expr % ',' ] ')' ]
```

### A.2 Types

Types are expressions (§5); this production lists the type-forming syntax.

```
type          = prefix_type [ '!' type ]                        // E!T, right-assoc
prefix_type   = '?' prefix_type
              | '^' [ 'const' ] prefix_type
              | '[' ']' [ 'const' ] prefix_type
              | '[' '^' ']' [ 'const' ] prefix_type
              | '[' expr ']' prefix_type
              | '[' '_' ']' prefix_type                         // only in array constructors
              | '^' fn_type
              | fn_type
              | tuple_type
              | struct_lit | union_lit | effect_lit
              | 'cont' '(' type ')'
              | postfix_expr                                    // names, m.T, G(args)
fn_type       = 'fn' '(' [ fn_type_param % ',' ] ')' [ '->' type ] [ '\' effect_set ]
fn_type_param = [ IDENT { ',' IDENT } ':' ] type | '...'                       // '...' only in foreign
tuple_type    = '(' ')' | '(' type ',' ')' | '(' type ',' type { ',' type } [ ',' ] ')'
              | '(' IDENT ':' type { ',' IDENT ':' type } [ ',' ] ')'
effect_set    = '{' [ effect_ref % ',' ] '}'
effect_ref    = postfix_expr                                    // Log, Yield(T), m.Net

struct_lit    = { attribute } 'struct' [ '(' param_list ')' ] [ where_clause ] '{' { NL } [ field_decl { ( ',' | NL ) { NL } field_decl } ] { NL } '}'
field_decl    = [ 'pub' ] [ 'using' ] IDENT { ',' IDENT } ':' type [ '=' expr ]
              | { attribute } field_decl
              | 'when' expr '{' { field_decl NL } '}' [ 'else' '{' { field_decl NL } '}' ]
union_lit     = { attribute } 'union' [ '(' type ')' | '(' param_list ')' ] [ where_clause ]
                '{' { NL } [ variant { ( ',' | NL ) { NL } variant } ] { NL } [ ',' ] { NL } '}'
variant       = IDENT [ ':' type ] [ '=' expr ]
effect_lit    = 'effect' [ '(' param_list ')' ] '{' { NL } { op_decl NL { NL } } '}'
op_decl       = IDENT ':' ( 'fn' | 'esc' | 'ctl' ) '(' [ param_list ] ')' [ '->' type ]
where_clause  = 'where' ( IDENT '(' type ')' ) % ','
```

In `struct`, `union` and `effect` parameter lists every parameter is compile-time, so parameters are declared with `:`; `::` is not allowed there. `union(T)` with a single type argument sets the tag type; `union(P: type, …)` with named parameters declares a generic union.

### A.3 Functions and handlers

```
fn_lit        = 'fn' '(' [ param_list ] ')' [ '->' type ] [ '\' effect_set ] [ where_clause ] fn_body
fn_body       = block | '=>' expr
param_list    = param % ','
param         = IDENT { ',' IDENT } [ ( ':' | '::' ) type ] [ '=' expr ]     // '::' = compile-time (§16.5)

handler_lit   = 'handler' effect_ref '{' { NL } { clause NL { NL } } '}'
handler_decl  = 'handler' effect_ref 'for' type [ '\' effect_set ] '{' { NL } { clause NL { NL } } '}'
clause        = IDENT '(' [ IDENT % ',' ] ')' '=>' ( expr | block )

foreign_block = 'foreign' STRING '{' { NL } { foreign_member NL { NL } } '}'
foreign_member= { attribute } IDENT '::' fn_type
              | '#' 'global' IDENT ':' type
import_expr   = 'import' STRING
```

### A.4 Statements

```
block         = '{' { NL } [ stmt { NL { NL } stmt } ] { NL } '}'
stmt          = binding
              | place_list assign_op expr
              | '_' '=' expr
              | defer_stmt
              | when_stmt
              | [ label ] while_stmt
              | [ label ] for_stmt
              | 'return' [ expr ]
              | 'break' [ IDENT ] [ expr ]
              | 'continue' [ IDENT ]
              | 'discard' expr
              | intrinsic_call
              | block                                             // a nested scope
              | expr
when_stmt     = 'when' expr block [ 'else' ( block | when_stmt ) ]
place_list    = expr { ',' expr }
assign_op     = '=' | '+=' | '-=' | '*=' | '/=' | '%=' | '<<=' | '>>=' | '&=' | '|=' | '~='
              | '+%=' | '-%=' | '*%=' | '<<%=' | '+|=' | '-|=' | '*|=' | '<<|='
defer_stmt    = ( 'defer' | 'errdefer' ) ( block | expr | place_list assign_op expr )
label         = IDENT ':'
while_stmt    = 'while' cond block
for_stmt      = 'for' pattern { ',' pattern } 'in' expr block
cond          = cond_part { 'and' cond_part }
cond_part     = pattern ':=' expr | expr
```

### A.5 Expressions

Precedence levels are those of §7.3 / Appendix B; each `exprN` uses operators of level N.

```
expr          = else_expr
else_expr     = logic_expr [ 'else' ( IDENT '=>' ( expr | block ) | else_expr ) ]
logic_expr    = not_expr { 'and' not_expr } | not_expr 'or' not_expr { 'or' not_expr }
not_expr      = 'not' not_expr | cmp_expr
cmp_expr      = range_expr [ cmp_op ( range_expr | is_pattern ) ]
cmp_op        = 'is' [ 'not' ] | [ 'not' ] 'in' | '<' | '<=' | '>' | '>='
range_expr    = bit_expr [ ( '..' | '..=' ) bit_expr ]
bit_expr      = shift_expr { '&' shift_expr } | shift_expr '|' shift_expr { '|' shift_expr }
              | shift_expr '~' shift_expr { '~' shift_expr }
shift_expr    = add_expr [ shift_op add_expr ]                    // operands may not be unparenthesized level 3/4
add_expr      = mul_expr { add_op mul_expr }
mul_expr      = prefix_expr { mul_op prefix_expr }
shift_op      = '<<' | '>>' | '<<%' | '<<|' | '<<?'
add_op        = '+' | '-' | '+%' | '-%' | '+|' | '-|' | '+?' | '-?'
mul_op        = '*' | '/' | '%' | '*%' | '*|' | '*?'
prefix_expr   = ( '-' | '~' | '&' | '!' | 'try' ) prefix_expr
              | postfix_expr
postfix_expr  = primary { postfix }
postfix       = '?' | '!' | '^'
              | '(' [ arg % ',' ] ')'
              | '[' expr ']'
              | '[' [ expr ] ( '..' | '..=' ) [ expr ] ']'
              | '.' IDENT | '.' INT
arg           = expr | IDENT ':' expr | '..' expr | '---'
primary       = literal | IDENT | 'null' | 'true' | 'false'
              | '(' expr ')' | tuple_expr
              | '.' IDENT                                         // variant, type from context
              | type_only                                          // ?T, ^T, []T, [N]T, fn types, struct/union/effect literals
              | fn_lit | handler_lit | import_expr | foreign_block
              | if_expr | match_expr | with_expr | [ label ] for_stmt   // for is an expression, §13.2
              | intrinsic_call
              | jump_expr
jump_expr     = 'return' [ expr ] | 'break' [ IDENT ] [ expr ] | 'continue' [ IDENT ]
              | 'resume' postfix_expr '(' [ expr ] ')'                 // all have type never
tuple_expr    = '(' ')' | '(' expr ',' ')' | '(' expr ',' expr { ',' expr } [ ',' ] ')'
              | '(' IDENT ':' expr { ',' IDENT ':' expr } [ ',' ] ')'
if_expr       = 'if' cond block { 'else' 'if' cond block } [ 'else' ( block | IDENT '=>' ( block | expr ) ) ]
match_expr    = 'match' expr '{' { NL } { arm NL { NL } } '}'
arm           = pattern { ',' pattern } [ 'if' expr ] '=>' ( expr | block )
with_expr     = [ label ] 'with' expr { ',' expr } [ 'on' expr ] block
intrinsic_call= '@' IDENT [ '(' [ arg % ',' ] ')' ]
is_pattern    = pattern                                            // binding-free; §7.6
```

### A.6 Patterns

```
pattern       = '_' | IDENT | 'null'
              | [ '-' ] literal
              | literal_bound ( '..' | '..=' ) literal_bound
              | '!' pattern
              | '.' IDENT [ '(' pattern_args ')' ]
              | postfix_expr '.' IDENT [ '(' pattern_args ')' ]    // Shape.Circle(r), m.CONST (§9.2)
              | postfix_expr '(' field_pat % ',' ')'               // struct pattern
              | '(' pattern_args ')'
pattern_args  = pattern % ',' | field_pat % ','
field_pat     = IDENT ':' pattern | IDENT
literal_bound = [ '-' ] literal | postfix_expr '.' IDENT           // qualified constant
```

### A.7 Disambiguation rules

1. **Statements beginning with an identifier.** `IDENT ':'` followed by `for`, `while` or `with` is a label. `IDENT ':'` followed by anything else begins a typed binding. `IDENT '::'`, `IDENT ':='` and `IDENT ','` begin bindings (or destructuring / multiple assignment, decided by the operator after the list). Otherwise the statement is an expression or assignment.
2. **Pattern vs expression in conditions.** In `cond_part`, the parser first tries `pattern ':='`; if no `:=` follows at the same nesting depth, the part is an expression.
3. **`!` after an operand.** In a type position (after `:` in a binding, parameter or field, after `->`, inside a function type, in an effect set), `!` is always the error-union operator. In an expression position, a `!` directly after an operand is ambiguous between the postfix unwrap `x!` and the error-union type `E!T`. If the token after the `!` can begin a type, the parser attempts to parse a type there; if that succeeds, it produces a single node `Bang(lhs, rhs)`, and otherwise the postfix unwrap `lhs!`. Name resolution decides a `Bang`: if `lhs` denotes a type it is `E!T`; otherwise it is `lhs!` followed by the postfix operators of `rhs`, whose leading parenthesized part is reinterpreted as an argument list (so `x!(a)` calls the unwrapped value). An attempted type parse fails on `x![i]`, `x!?.f` and `x!.f`, so those simply unwrap and continue. When the rhs is a type but `lhs` is a value (`x!?u8`), resolution reports an error.
4. **`{` after a head.** In the heads of `if`, `while`, `for … in`, `match`, `with` and `when`, the first `{` at the head's own bracket depth always begins the body. Any construct containing a `{` — a struct, union, effect or handler literal, a function literal with a block body, or an `if`, `match`, `with` or `for` expression — must be parenthesized when it appears in such a head. (Construction uses `( )`, so this rarely matters.)
5. **`fn` types vs literals.** In an expression position, `fn(...)` followed by `{`, `=>`, or `-> T {` / `-> T =>` (after an optional effect set and `where` clause) is a literal; otherwise it is a type.
6. **`else` dispatch.** See §7.3.
7. **`..` in arguments** is spread (§11.4). Ranges with missing bounds exist only inside index brackets.
8. **Match arms.** A newline at the end of a match arm always ends it (§2.8).
9. **`is` right operand.** The right operand of `is`/`is not` is parsed as an expression; if it contains `_` or a variant name without its required payload, it is reinterpreted as a binding-free pattern (§7.6).
10. **`resume k(v)`** parses `k` as a postfix expression without the final call, which supplies the value.
11. **Labels in `break` and `continue`.** In `break IDENT …` and `continue IDENT`, the identifier is a label if a `while`, `for` or `with` carrying that label lexically encloses the statement (labels are visible inside handler literal clauses written within the labeled construct, but not inside function literals or nested functions). Otherwise, for `break`, the identifier begins the value operand. `break` and `return` take an operand exactly when the next token can begin an expression.
12. **Type prefixes before a call.** A prefix type operator (`?`, `^`, `^const`, `[]`, `[N]`, `[_]`, `[^]`, with or without `const`) applies to the postfix expression that follows it, including calls: `?List(u8)` is `?(List(u8))`. If name resolution finds that the final call of that operand is not the application of a generic type (it is a construction or conversion, so it produces a value, not a type), the call is instead applied to the whole prefixed type: `?i32(5)` is `(?i32)(5)` and `[3]u8(1, 2, 3)` is `([3]u8)(1, 2, 3)`. Postfix operations after such a construction apply to the constructed value: `[3]u8(1, 2, 3)[0]` is `(([3]u8)(1, 2, 3))[0]`.
13. **Attributes.** Newlines directly after an attribute are not significant, so an attribute may be written on its own line before the declaration, field or literal it modifies.

---

## Appendix B. Reference tables

### B.1 Operator precedence, tightest first

| Level | Operators | Associativity | Notes |
| --- | --- | --- | --- |
| 1 | `x?` `x!` `x^` `f(…)` `a[i]` `a[i..j]` `.name` `.0` | left | postfix |
| 2 | `-x` `~x` `&x` `!e` `try x` | prefix | `!e` makes an error value |
| 3 | `*` `/` `%` `*%` `*\|` `*?` | left | `/` and `%` have no flavors |
| 4 | `+` `-` `+%` `+\|` `+?` `-%` `-\|` `-?` | left | |
| 5 | `<<` `>>` `<<%` `<<\|` `<<?` | none | not mixable with levels 3–4 without parentheses |
| 6 | `&` `\|` `~` | left | one operator kind per chain; binary `~` is XOR |
| 7 | `..` `..=` | none | |
| 8 | `is` `is not` `in` `not in` `<` `<=` `>` `>=` | none | never chain |
| 9 | `not` | prefix | |
| 10 | `and` `or` | left | one operator kind per chain: mixing requires parentheses |
| 11 | `x else y`, `x else e => y` | right | `try x else y` unparenthesized is an error |

Type operators: prefix `?`, `^`, `^const`, `[]`, `[]const`, `[N]`, `[^]`, `[^]const` apply to the whole type that follows; infix `!` is right-associative and looser than all of them.

Assignment (`=` and compound forms) is a statement.

### B.2 Sigils

| Sigil | Story | Forms |
| --- | --- | --- |
| `?` | maybe absent | `?T`, `x?`, `+?` |
| `!` | may have failed | `E!T`, `x!`, `!e` |
| `^` | pointer | `^T`, `^const T`, `[^]T`, `^fn`, `x^` |
| `:` | naming | `x: T`, `x := e`, `x :: e`, `x: T : e`, `label:` |
| `=>` | maps to | expression bodies, match arms, handler clauses, `else e =>` |
| `\` | effects | `-> T \ {E1, E2}` |
| `..` | span | ranges `a..b`, slicing `a[i..]`, spread `f(..t)` |
| `@` / `#` | intrinsic / attribute | `@size_of(T)`, `#packed` |
| `---` | don't care | uninitialized plain data |

### B.3 Keywords

**Reserved (34):** `and` `break` `continue` `ctl` `defer` `discard` `effect` `else` `errdefer` `esc` `false` `fn` `for` `foreign` `handler` `if` `import` `in` `is` `match` `not` `null` `or` `pub` `resume` `return` `struct` `true` `try` `union` `using` `when` `while` `with`.

**Contextual:** `const` (after `^`, `[]`, `[^]`), `on` (in `with`), `where` (after a signature or type parameters), `test` (top-level). `type` and `cont` are predeclared, reserved type names.

### B.4 Build modes

| Build | Optimized | Checks | Extras |
| --- | --- | --- | --- |
| Debug | no | all | poisoning, allocation tags, fiber guard pages, leak-checking tests |
| Checked (release default) | yes | all | — |
| Fast | yes | none (except integer division) | — |

---

## Appendix C. Design rationale

This appendix is non-normative. It explains why Gamma is the way it is and lists the main alternatives it rejects, with the reasons.

The goals of §1 are applied in priority order: **sensible**, then **fast**, then **simple**. When they conflict, the earlier one wins; when two features could do one job, one is removed.

### C.1 Syntax

- **`::` names everything; `T(...)` makes everything; sigils describe shapes and words describe logic.** A reader learns three patterns and then reads declarations, construction and control flow uniformly. `?`, `!` and `^` each tell one story across types, operators and unwrapping.
- **Equality is the word `is`.** `=` is only assignment, so `if x = 0` cannot be written. `is` is always value equality; for slices (and so strings) that means content, because `name is "admin"` comparing addresses would be a trap.
- **Precedence errors instead of precedence rules.** Mixing `and` with `or`, different bitwise operators, or shifts with arithmetic requires parentheses. These are the precedences programmers most often misremember; Gamma rejects the ambiguous text rather than choosing a meaning for it. Comparisons do not chain.
- **Binary `~` is exclusive-or.** `^` is the pointer sigil, and pointers are far more common than XOR.
- **Newlines end statements** except where a line obviously continues. Inside `( )` and `[ ]` newlines are ignored. A line starting with `.` continues a method chain, except at the start of a match arm, where `.Variant` begins a new arm.
- **Struct construction is by name only.** Reordering fields can never silently change a constructor's meaning, and `S(x)` with one positional argument is always a conversion.
- **No octal-by-leading-zero, no implicit string concatenation, no fallthrough, no truthiness.** Each is a classic C bug with no compensating benefit.

### C.2 Types and values

- **Literals are typed by inference, not defaults.** `count := 0` takes its type from how `count` is used anywhere in the function; only a literal nothing constrains falls back (`i32`, `f64`, or `usize` for ranges and array lengths). This keeps the common case unannotated while never converting between types: a literal that does not fit its inferred type is an error, not a wrap.
- **No implicit numeric conversion in expressions.** Lossless widening happens only at coercion sites (assignment, arguments, return). `T(x)` converts only when lossless for every value of the source *type*; legality never depends on how clever the compiler's range analysis is. Lossy conversions are named methods: `trunc`, `round`, `checked`, `sat`, `bits`.
- **`usize` is not `u64`.** Code correct on a 64-bit target must not silently break on a 32-bit one.
- **Arbitrary-width integers** make protocol fields and bit manipulation exact, with storage rules that keep them addressable; packing is opt-in through `#bits` structs, whose layout is fully specified (unlike C bitfields).
- **No null by default.** Every pointer kind is non-null; `?^T` is nullable and costs nothing extra. Optionals and results mirror each other, and wrap implicitly so success paths read like plain code.
- **Unions are tagged** and matches are exhaustive. Integers become enums only through `from_tag`, which checks, because values arriving from C or the network may be out of range.
- **`---` only for plain data.** Reading uninitialized integers gives an unspecified value, never UB; types with invariants (`bool`, unions, pointers) must be initialized, so no invalid value can arise from forgetting to.
- **Ignored results must be discarded explicitly** (`_ = f()`), so an unhandled error is always visible in the source.

### C.3 Functions

- **Two kinds of function value.** `fn(…)` is second-class: it may capture locals by reference, costs no allocation, and can be passed down but never stored or returned. `^fn(…)` is a plain code pointer: first-class, C-compatible, capturing nothing. Between them they cover callbacks and stored function pointers without closures that allocate or dangle.
- **Effect polymorphism without effect variables.** A second-class callback's effects are handled where it was written, except those its parameter type lists, which the callee handles. So `retry(3, fn() => send(p))` needs no annotation for `send`'s effects, while `capture_output(body: fn() \ {Out})` can provide an effect to its callback.
- **Parameters are distinct objects.** Passing a large value by reference is an optimization the implementation may make when unobservable, never a semantic the programmer must reason about.

### C.4 Generics, and why there are no traits

Generic code is ordinary code with compile-time parameters, checked once where it is written and monomorphized. Constraints are a closed set (`Eq`, `Ord`, `Num`, `Int`, `Signed`, `Float`, `Plain`) that describe what built-in operators a body may use.

**Gamma has no user-defined traits, interfaces or typeclasses.** Traits combine five jobs:

| Job | In Gamma |
| --- | --- |
| Bounding a generic body | closed constraints, plus function parameters |
| Implementation fixed per type, called directly | compile-time parameters of functions and types (`HashMap(K, V, hash: f)`) |
| Implementation chosen at run time | a struct with a vtable of `^fn` (as `Allocator`), or a tagged union |
| Implementation supplied by context | effects and handlers |
| Implementation found **implicitly** and **composed** (`Hash` for `(A, B)` from `Hash` for `A` and `B`) | deliberately absent |

The first four already exist, each with its cost and its code visible where it is used. The fifth is the part that makes a trait system: it requires coherence and orphan rules, overlap resolution, associated types, conditional implementations, and — in a language with effects — either fixed effect sets on trait methods (rigid) or effect variables (the row polymorphism Gamma rejects). It is the largest and least predictable component of the languages that have it, and it means a reader cannot tell from `m.put(k, v)` which code runs.

What implicit composition is usually *for* is covered structurally instead, by rules the compiler derives from a type's shape, which nothing can override and nothing needs to resolve:

- **Hashing**: `@hash` is defined for every `Eq` type and agrees with `is` (§16.8), so `HashMap((string, u32), V)` needs no code.
- **Ordering**: tuples, arrays and slices compare lexicographically, so structs are ordered by an explicit key tuple: `xs.sort_by_key(fn(p) => (p.last, p.first))`. Structs themselves are not ordered: their field order records layout, not priority.
- **Printing**: `print("{}", x)` formats every type structurally (§21.6.1). Custom presentation is an ordinary function returning a string.

What remains costs one explicit argument: generic arithmetic over user-defined number types passes its operations as a constant struct (`dot(FIXED_RING, xs, ys)`), and combining two unrelated libraries takes a small adapter function. That is the price of knowing what every call does.

Operator overloading is excluded for the same reason: an operator never calls user code, so `a + b` always costs what it looks like it costs. Arrays have elementwise arithmetic for the common numeric case.

### C.5 Effects

- **Why effects at all.** C programs express allocation policy, logging, I/O, iteration, early exit and coroutines with globals, callback-plus-`void*` pairs, `setjmp`, and hand-written state machines. Effects express all six with one mechanism that the type system tracks, and that tests can replace.
- **Three operation kinds** (`fn`, `esc`, `ctl`) make the worst case visible in the signature: `fn` operations always return (a call), `esc` operations may exit (a return-flag check), `ctl` operations may suspend (a stack switch). Programs pay only for the kind they use.
- **A clause never handles its own performs.** Clauses run in the context outside their handler. This makes an iterator written as a loop over another iterator correct, and makes nested handlers for the same effect compose predictably.
- **Exits are explicit control flow.** An `esc` clause leaves by `break` (the `with` is an expression, so this can carry a value), and the frames in between run their `defer`s. There are no exceptions, unwinding tables or `setjmp`.
- **`Sys` marks the boundary of the model.** Foreign calls, globals, volatile memory and threads all need `Sys`; a function without it has no hidden state. This is what makes compile-time evaluation and test handlers sound. It is not purity: such functions may still write through pointers they are given.
- **Generic effect instances must be distinguishable** when checked once, so `\ {Yield(T), Yield(U)}` is rejected: which handler a `yield` reaches must not depend on the instantiation.

### C.6 Fibers and continuations

- **One-shot, stack-switching continuations.** Multi-shot continuations would require copying stacks, which invalidates pointers into them. One-shot continuations on caller-provided stacks allocate nothing and keep pointers stable.
- **`resume` is a transfer, not a call.** `ctl` clauses run on the `with`'s own stack and `resume` (type `never`) switches away. A scheduler can switch between any number of fibers without its stack growing.
- **Structured lifetime.** A `with … on` cannot complete while any fiber it created is suspended; an exit that crosses it cancels the remaining fibers first. Continuations are first-class so schedulers can queue them, with one-shot use checked at run time where it cannot be checked statically.
- **Fibers start from a function pointer and an argument**, never from a second-class closure, since a fiber can outlive the frame that would have created the closure.

### C.7 Memory and safety

- **Allocation is explicit.** No construct allocates behind the program's back. The current allocator comes from the `Alloc` effect; long-lived structures capture their allocator when constructed, so memory is always freed through the allocator that produced it.
- **No ownership system.** Values copy bitwise like C; there are no destructors or move semantics. `defer`/`errdefer` structure cleanup, and Debug builds tag allocations, poison freed memory and detect leaks. Ownership tracking would make Gamma a different language.
- **One rule for checks.** Every run-time check traps in Debug and Checked builds and is removed in Fast builds; nothing in the source opts out. Integer division by zero always traps, since hardware does anyway.
- **Arithmetic is never undefined.** Overflow without a check gives an unspecified value, so an optimizer cannot use overflow to delete a program's own safety checks. Undefined behavior is confined to a closed list (§18.4) of memory and concurrency errors that cannot be checked cheaply.
- **Compile-time evaluation is ordinary code** whose empty effect set proves it touches nothing outside. The evaluator checks everything, so a compile-time bug is a compile error, never compiler misbehavior. Constants contain only read-only pointers into static data.

### C.8 Interoperability and threads

- **C layout by default** for structs, unions and pointers; `?^T` is a nullable C pointer. Tagged unions, optionals of non-pointers, slices and tuples stay out of foreign signatures, where their layouts would be a promise to C.
- **Exported functions install their own handlers**, because C cannot pass any.
- **Threads follow the C11 memory model**, with atomics as intrinsics. Existing knowledge and tools apply unchanged. Handlers, second-class values and continuations are per-thread, so nothing captured by reference is shared implicitly.

### C.9 Alternatives considered and rejected

| Alternative | Reason for rejection |
| --- | --- |
| User-defined traits / interfaces / typeclasses | C.4 |
| Operator overloading | operators would hide calls and costs (C.4) |
| Move semantics or a `#no_copy` marker | an ownership system; Debug tagging catches the double frees it would prevent |
| Lexicographic ordering of structs | field order records layout, not priority; order key tuples instead |
| Bitwise equality for floats (to find NaN keys) | two notions of equality is worse than one documented caveat |
| A default literal type without inference | `i := 0; while i < xs.len` would fail |
| `T(x)` succeeding when the *value* is provably in range | legality would depend on the compiler's analysis |
| Case-sensitive line continuation (`.lower` continues, `.Upper` does not) | would make naming conventions part of the grammar |
| Effect variables / row polymorphism | second-class callbacks provide effect polymorphism with no syntax |
| Multi-shot continuations | require copying stacks; break pointers into them |
| Exceptions | `esc` exits and results cover early exit and failure, visibly |
| Implicit allocation for closures | second-class closures need none |
| Macros | compile-time evaluation of ordinary functions covers tables and constants; `when` covers configuration |

### C.10 Future directions

- A `Send`-like rule that would let fibers migrate between threads.
- Stored function values polymorphic in their effects (would need effect variables or existential handlers).
- SIMD vector types with guaranteed code generation.
- If experience shows heavy dictionary-passing boilerplate for user-defined numeric types, a compile-time parameter defaulting to a constant found by name in the type's own module — which would still involve no global resolution.

---

## Appendix D. Implementation notes

This appendix is non-normative. It describes the implementation strategy the language was designed around, to show that the semantics can be met at C-like cost.

### D.1 Evidence passing

A function with a non-empty effect set receives one hidden parameter: a pointer to an **evidence record** with one entry per effect in its set, in a canonical order (sorted by effect identity). Each entry points to the installed handler's clause table and its data (the handler value, or the frame of a handler literal). A perform loads the entry and calls the clause. Calling a function with a smaller set passes a pointer to a re-projected record (built on the stack when the sets differ).

A second-class function value is a pair: code pointer and environment pointer (the creating frame), plus the creating context's evidence for effects not in its parameter type. Function pointers carry no environment and receive evidence from their caller, like named functions.

There are no unwinding tables and no `setjmp`.

### D.2 Exits as return flags

`esc` exits must run the `defer`s of every frame they cross. A function that can be crossed — one whose effect set contains an `esc` or `ctl` operation, or which calls a second-class parameter — returns an extra **exit flag** alongside its result. After each such call, the caller tests the flag; if set, it runs its pending defers (and errdefers) and returns with the flag set. The `with` that is the exit's target clears it and continues at the target. Fiber cancellation uses the same protocol on the cancelled fiber's stack.

The cost is visible in types: calls to functions with only `fn` effects never test the flag, and exported functions (empty or `{Sys}` sets) never see it.

### D.3 Static specialization

When the handler installed by a `with` is statically known — a handler literal, or a value of a type with a `handler … for` declaration — the compiler may specialize the callees in the `with` body for that handler, exactly as it monomorphizes generics. Performs become direct calls and can be inlined; an inlined `for` loop over `range` or `each` becomes a counted loop with no flag tests. This is the "statically known handler" referred to by `@stack_size` (§14.8). Debug builds may skip it.

### D.4 Fibers

`with … on` switches the stack pointer to the provided stack and calls the body. A `ctl` perform saves callee-saved registers on the fiber stack and switches back to the driver, which runs the clause. `resume` restores the fiber. A continuation is a pointer to the saved context plus a generation counter used for the one-shot checks in Debug and Checked builds. Nothing is heap-allocated.

### D.5 Parameter passing

Since parameters are distinct objects (§11.2), the straightforward implementation copies. An implementation may pass a pointer to the caller's object instead when it can show the difference is unobservable: the callee does not take the parameter's address in a way that is compared or escapes, and nothing can write to the original during the call (no non-`const` pointer to an overlapping type is passed, no `fn`-kind clause that captures the original can run, and the callee cannot suspend).

### D.6 Compile-time evaluation

The evaluator is an interpreter over the checked intermediate representation, with the target's layout. It tracks the dynamic type and provenance of every allocation so it can detect the UB listed in §18.4, including wrong downcasts and use of dead stack memory.

### D.7 Checks

Bounds and overflow checks are ordinary branches to a cold trap routine that receives the source location. Implementations should hoist checks out of loops where the loop's range proves them, which is what makes `for i in 0..a.len { a[i] }` and `for x in xs` check-free in Checked builds.
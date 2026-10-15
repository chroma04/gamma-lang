/**
 * Tree-sitter grammar for Gamma (gamma-spec.md, Appendix A).
 *
 * Types are expressions in Gamma. Type positions use the `_type` rules, which add the
 * infix `E!T` operator; expression positions reach the same prefix type forms through
 * the primary expressions.
 *
 * Statement termination (spec 2.8) is implemented by the external scanner in
 * `src/scanner.c`. The scanner emits `_newline` only where the grammar can use it, so
 * newlines inside parentheses and brackets are ordinary whitespace, and it applies the
 * continuation rules for the next line (`else`, `and`, `or`, `.name`).
 */

/* eslint-disable arrow-parens */
/* eslint-disable camelcase */
/* eslint-disable-next-line spaced-comment */
/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

const PREC = {
  ELSE: 1,
  AND: 2,
  LET: 3,
  OR: 4,
  NOT: 5,
  CMP: 6,
  RANGE: 7,
  BIT: 8,
  SHIFT: 9,
  ADD: 10,
  MUL: 11,
  PREFIX: 12,
  POSTFIX: 13,
};

const ASSIGN_OPS = [
  '=', '+=', '-=', '*=', '/=', '%=', '<<=', '>>=', '&=', '|=', '~=',
  '+%=', '-%=', '*%=', '<<%=', '+|=', '-|=', '*|=', '<<|=',
];

const INT_SUFFIX = /(u|i)[1-9][0-9]*|usize|isize/;
const FLOAT_SUFFIX = /f16|f32|f64/;

/** @param {RuleOrLiteral} rule */
function commaSep1(rule) {
  return seq(rule, repeat(seq(',', rule)));
}

/**
 * The inside of a brace-delimited list: items separated by one or more separators, with
 * optional separators at both ends.
 * @param {RuleOrLiteral} item
 * @param {RuleOrLiteral} sep
 */
function listBody(item, sep) {
  return seq(
    optional(sep),
    optional(seq(item, repeat(seq(sep, item)), optional(sep))),
  );
}

/** @param {RuleOrLiteral} rule */
function commaSep(rule) {
  return optional(commaSep1(rule));
}

module.exports = grammar({
  name: 'gamma',

  word: $ => $.identifier,

  externals: $ => [
    $._newline,
    $._arm_newline,
    $.block_comment,
    $.raw_string_literal,
    $._is_not,
    $._not_in,
    $._test_keyword,
    $._bench_keyword,
    $._error_sentinel,
  ],

  extras: $ => [
    /[ \t\r\n\f\v\uFEFF]/,
    $.comment,
    $.doc_comment,
    $.block_comment,
  ],

  supertypes: $ => [
    $._expression,
    $._type_atom,
  ],

  conflicts: $ => [
    [$._primary_expression, $.label],
    [$.label, $.named_element],
    [$.label, $.named_argument],
    [$.result_unwrap_expression, $.error_union_expression],
    [$.if_expression],
  ],

  rules: {
    // ------------------------------------------------------------------
    // Files and declarations
    // ------------------------------------------------------------------

    source_file: $ => listBody($._item, $._terminators),

    _terminators: $ => repeat1(choice($._newline, ';')),
    _arm_terminators: $ => repeat1(choice($._arm_newline, ';')),
    _separators: $ => repeat1(choice($._newline, ';', ',')),

    _item: $ => choice(
      $.when_declaration,
      $.handler_declaration,
      $.test_declaration,
      $.bench_declaration,
      $.intrinsic_expression,
      seq(
        optional($._attributes),
        optional('pub'),
        choice($.const_declaration, $.typed_declaration, $.short_declaration),
      ),
    ),

    _attribute_line: $ => seq($.attribute, optional($._newline)),
    _attributes: $ => repeat1($._attribute_line),

    attribute: $ => prec.right(seq(
      field('name', $.attribute_name),
      optional(field('arguments', $.arguments)),
    )),
    attribute_name: _ => token(seq('#', /[A-Za-z_][A-Za-z0-9_]*/)),

    when_declaration: $ => seq(
      'when',
      field('condition', $._expression),
      field('consequence', $.declaration_block),
      optional(field('alternative', $.when_else_declaration)),
    ),
    when_else_declaration: $ => seq(
      'else',
      choice($.when_declaration, $.declaration_block),
    ),
    declaration_block: $ => seq('{', listBody($._item, $._terminators), '}'),

    handler_declaration: $ => seq(
      'handler',
      optional(field('parameters', $.parameters)),
      field('effect', $._type_path),
      'for',
      field('type', $._type),
      optional(field('effects', $.effect_set)),
      field('body', $.handler_body),
    ),

    test_declaration: $ => seq(
      alias($._test_keyword, 'test'),
      field('name', $._string),
      field('body', $.block),
    ),

    bench_declaration: $ => seq(
      alias($._bench_keyword, 'bench'),
      field('name', $._string),
      field('body', $.block),
    ),

    // name :: value      names : T : value      names : T = value      names := value
    const_declaration: $ => prec.dynamic(1, seq(
      field('name', $._lhs_list),
      '::',
      field('value', $._rhs_list),
    )),
    typed_declaration: $ => seq(
      field('name', $._lhs_list),
      ':',
      field('type', $._type),
      choice(':', '='),
      field('value', $._rhs_list),
    ),
    short_declaration: $ => seq(
      field('name', $._lhs_list),
      ':=',
      field('value', $._rhs_list),
    ),

    _lhs_list: $ => commaSep1($._expression),
    _rhs_list: $ => commaSep1($._value),

    // A value that may carry attributes in front: `#packed struct { ... }`.
    _value: $ => choice($._expression, $.attributed_type),

    // ------------------------------------------------------------------
    // Statements
    // ------------------------------------------------------------------

    block: $ => seq('{', listBody($._statement, $._terminators), '}'),

    _statement: $ => choice(
      seq($._attributes, choice($.const_declaration, $.typed_declaration, $.short_declaration)),
      $.const_declaration,
      $.typed_declaration,
      $.short_declaration,
      $.assignment_statement,
      $.defer_statement,
      $.when_statement,
      $.while_statement,
      $.cancel_statement,
      $.block,
      $._expression,
    ),

    assignment_statement: $ => seq(
      field('left', $._lhs_list),
      field('operator', choice(...ASSIGN_OPS)),
      field('right', $._rhs_list),
    ),

    defer_statement: $ => seq(
      choice('defer', 'errdefer'),
      choice($.block, $.assignment_statement, $._expression),
    ),

    cancel_statement: $ => seq('cancel', $._expression),

    when_statement: $ => seq(
      'when',
      field('condition', $._expression),
      field('consequence', $.block),
      optional(field('alternative', $.when_else_statement)),
    ),
    when_else_statement: $ => seq(
      'else',
      choice($.when_statement, $.block),
    ),

    while_statement: $ => seq(
      optional(seq(field('label', $.label), ':')),
      'while',
      field('condition', $._condition),
      field('body', $.block),
    ),

    label: $ => $.identifier,

    // ------------------------------------------------------------------
    // Conditions
    // ------------------------------------------------------------------

    _condition: $ => choice($._expression, $.let_condition, $.condition_chain),

    // `a and p := e and b`: conditions joined by `and`, at least one of which binds.
    condition_chain: $ => choice(
      prec.left(PREC.AND, seq(
        field('left', choice($._expression, $.let_condition, $.condition_chain)),
        'and',
        field('right', $.let_condition),
      )),
      prec.left(PREC.AND, seq(
        field('left', choice($.let_condition, $.condition_chain)),
        'and',
        field('right', $._expression),
      )),
    ),

    let_condition: $ => prec.left(PREC.LET, seq(
      field('pattern', $._expression),
      ':=',
      field('value', $._expression),
    )),

    // ------------------------------------------------------------------
    // Expressions
    // ------------------------------------------------------------------

    _expression: $ => choice(
      $.else_expression,
      $.and_expression,
      $.binary_expression,
      $.not_expression,
      $.unary_expression,
      $.optional_unwrap_expression,
      $.result_unwrap_expression,
      $.dereference_expression,
      $.error_union_expression,
      $.call_expression,
      $.index_expression,
      $.slice_expression,
      $.field_expression,
      $._primary_expression,
    ),

    _primary_expression: $ => choice(
      $.identifier,
      $.discard,
      $.integer_literal,
      $.float_literal,
      $._string,
      $.character_literal,
      $.boolean_literal,
      $.null,
      $.uninitialized,
      $.parenthesized_expression,
      $.tuple_expression,
      $.variant_expression,
      $.effect_set,
      $._type_forms,
      $.function_expression,
      $.handler_expression,
      $.import_expression,
      $.foreign_block,
      $.if_expression,
      $.match_expression,
      $.with_expression,
      $.for_expression,
      $.return_expression,
      $.break_expression,
      $.continue_expression,
      $.resume_expression,
      $.intrinsic_expression,
    ),

    // Forms that are types and that also appear in expression positions.
    _type_forms: $ => choice(
      $.optional_type,
      $.pointer_type,
      $.slice_type,
      $.array_type,
      $.many_pointer_type,
      $.function_type,
      $.struct_type,
      $.union_type,
      $.effect_type,
    ),

    discard: _ => '_',
    null: _ => 'null',
    uninitialized: _ => '---',
    boolean_literal: _ => choice('true', 'false'),

    parenthesized_expression: $ => seq('(', $._expression, ')'),

    tuple_expression: $ => choice(
      seq('(', ')'),
      seq('(', $._expression, ',', ')'),
      seq('(', $._expression, ',', commaSep1($._expression), optional(','), ')'),
      seq('(', commaSep1($.named_element), optional(','), ')'),
    ),
    named_element: $ => seq(
      field('name', $.identifier),
      ':',
      field('value', $._expression),
    ),

    variant_expression: $ => seq('.', field('name', $.identifier)),

    // Logical, comparison and arithmetic operators, loosest first.

    else_expression: $ => prec.right(PREC.ELSE, seq(
      field('left', $._expression),
      'else',
      choice(
        seq(field('capture', choice($.identifier, $.discard)), '=>', field('right', choice($.block, $._expression))),
        field('right', $.block),
        field('right', $._expression),
      ),
    )),

    and_expression: $ => prec.left(PREC.AND, seq(
      field('left', $._expression),
      'and',
      field('right', $._expression),
    )),

    not_expression: $ => prec(PREC.NOT, seq('not', $._expression)),

    binary_expression: $ => {
      /** @type {[number, any][]} */
      const table = [
        [PREC.OR, 'or'],
        [PREC.CMP, 'is'],
        [PREC.CMP, alias($._is_not, 'is not')],
        [PREC.CMP, 'in'],
        [PREC.CMP, alias($._not_in, 'not in')],
        [PREC.CMP, choice('<', '<=', '>', '>=')],
        [PREC.RANGE, choice('..', '..=')],
        [PREC.BIT, choice('&', '|', '~')],
        [PREC.SHIFT, choice('<<', '>>', '<<%', '<<|', '<<?')],
        [PREC.ADD, choice('+', '-', '+%', '-%', '+|', '-|', '+?', '-?')],
        [PREC.MUL, choice('*', '/', '%', '*%', '*|', '*?')],
      ];
      return choice(...table.map(([precedence, operator]) => prec.left(precedence, seq(
        field('left', $._expression),
        field('operator', operator),
        field('right', $._expression),
      ))));
    },

    unary_expression: $ => prec(PREC.PREFIX, seq(
      field('operator', choice('-', '~', '&', '!', 'try')),
      field('operand', $._expression),
    )),

    // Postfix operators.

    optional_unwrap_expression: $ => prec(PREC.POSTFIX, seq($._expression, '?')),
    result_unwrap_expression: $ => prec(PREC.POSTFIX, seq($._expression, '!')),
    dereference_expression: $ => prec(PREC.POSTFIX, seq($._expression, '^')),

    // `E!T` in an expression position (A.7 rule 3).
    error_union_expression: $ => prec.dynamic(-1, prec.right(PREC.POSTFIX, seq(
      field('error', $._expression),
      '!',
      field('ok', $._type),
    ))),

    call_expression: $ => prec(PREC.POSTFIX, seq(
      field('function', $._expression),
      field('arguments', $.arguments),
    )),

    arguments: $ => seq(
      '(',
      commaSep($._argument),
      optional(','),
      ')',
    ),
    _argument: $ => choice(
      $.named_argument,
      $.spread_argument,
      $._expression,
    ),
    named_argument: $ => seq(
      field('name', choice($.identifier, $.discard)),
      ':',
      field('value', $._expression),
    ),
    spread_argument: $ => seq('..', $._expression),

    index_expression: $ => prec(PREC.POSTFIX, seq(
      field('value', $._expression),
      '[',
      field('index', $._expression),
      ']',
    )),

    slice_expression: $ => prec(PREC.POSTFIX, seq(
      field('value', $._expression),
      '[',
      choice(
        seq(field('start', $._expression), choice('..', '..=')),
        seq(optional(field('start', $._expression)), choice('..', '..='), field('end', $._expression)),
        '..',
      ),
      ']',
    )),

    field_expression: $ => prec(PREC.POSTFIX, seq(
      field('value', $._expression),
      '.',
      field('field', choice($.identifier, $.integer_literal)),
    )),

    // Control flow.

    if_expression: $ => seq(
      'if',
      field('condition', $._condition),
      field('consequence', $.block),
      repeat($.else_if_clause),
      optional($.else_clause),
    ),
    else_if_clause: $ => prec.dynamic(1, seq(
      'else',
      'if',
      field('condition', $._condition),
      field('consequence', $.block),
    )),
    else_clause: $ => prec.dynamic(1, seq(
      'else',
      choice(
        field('body', $.block),
        seq(field('capture', choice($.identifier, $.discard)), '=>', field('body', choice($.block, $._expression))),
      ),
    )),

    match_expression: $ => seq(
      'match',
      field('value', $._expression),
      field('body', $.match_body),
    ),
    match_body: $ => seq('{', listBody($.match_arm, $._arm_terminators), '}'),
    match_arm: $ => seq(
      field('pattern', commaSep1($._expression)),
      optional(seq('if', field('guard', $._expression))),
      '=>',
      field('value', choice($.block, $._expression)),
    ),

    with_expression: $ => seq(
      optional(seq(field('label', $.label), ':')),
      'with',
      commaSep1(field('handler', $._expression)),
      optional(seq('on', field('on', $._expression))),
      field('body', $.block),
    ),

    for_expression: $ => seq(
      optional(seq(field('label', $.label), ':')),
      'for',
      commaSep1(field('pattern', $._for_pattern)),
      'in',
      field('iterable', $._expression),
      field('body', $.block),
    ),

    // The patterns of a `for` cannot be general expressions: `x in xs` would read as a
    // membership test.
    _for_pattern: $ => choice(
      $.identifier,
      $.discard,
      $.tuple_expression,
      $.parenthesized_expression,
      $.variant_expression,
    ),

    return_expression: $ => prec.right(seq('return', optional($._expression))),
    break_expression: $ => prec.right(seq(
      'break',
      optional(field('label', $.label)),
      optional($._expression),
    )),
    continue_expression: $ => prec.right(seq('continue', optional(field('label', $.label)))),
    resume_expression: $ => prec.right(seq('resume', $._expression)),

    import_expression: $ => seq('import', $._string),

    intrinsic_expression: $ => prec.right(PREC.POSTFIX + 1, seq(
      field('name', $.intrinsic_name),
      optional(field('arguments', $.arguments)),
    )),
    intrinsic_name: _ => token(seq('@', /[A-Za-z_][A-Za-z0-9_]*/)),

    // ------------------------------------------------------------------
    // Functions, handlers and effect sets
    // ------------------------------------------------------------------

    function_expression: $ => seq(
      'fn',
      field('parameters', $.parameters),
      optional(seq('->', field('return_type', $._type))),
      optional(field('effects', $.effect_set)),
      optional(field('constraints', $.where_clause)),
      choice(
        field('body', $.block),
        seq('=>', field('body', $._expression)),
      ),
    ),

    function_type: $ => prec.dynamic(-1, prec.right(seq(
      'fn',
      field('parameters', $.parameters),
      optional(seq('->', field('return_type', $._type))),
      optional(field('effects', $.effect_set)),
    ))),

    parameters: $ => seq(
      '(',
      commaSep($._parameter),
      optional(','),
      ')',
    ),
    _parameter: $ => choice($.parameter, '...', $._type_only_parameter),
    parameter: $ => seq(
      field('name', $.identifier),
      optional(seq(choice(':', '::'), field('type', $._type))),
      optional(seq('=', field('default', $._expression))),
    ),
    _type_only_parameter: $ => choice(
      $.optional_type, $.pointer_type, $.slice_type, $.array_type, $.many_pointer_type,
      $.function_type, $.tuple_type, $.struct_type, $.union_type, $.effect_type,
      $.scoped_type, $.generic_type, $.attributed_type,
    ),

    where_clause: $ => seq('where', commaSep1($.constraint)),
    constraint: $ => seq(
      field('name', $.identifier),
      '(',
      field('type', $._type),
      ')',
    ),

    effect_set: $ => seq('\\', '{', listBody($._effect_entry, $._separators), '}'),
    _effect_entry: $ => choice(
      $.effect_spread,
      $._type,
    ),
    effect_spread: $ => seq('..', $._type),

    handler_expression: $ => seq(
      'handler',
      field('effect', $._type_path),
      field('body', $.handler_body),
    ),
    handler_body: $ => seq('{', listBody($.handler_clause, $._terminators), '}'),
    handler_clause: $ => seq(
      field('name', $.identifier),
      field('parameters', $.clause_parameters),
      '=>',
      field('body', choice($.block, $._expression)),
    ),
    clause_parameters: $ => seq('(', commaSep($.identifier), optional(','), ')'),

    foreign_block: $ => seq(
      'foreign',
      field('library', $._string),
      '{',
      listBody($.foreign_member, $._terminators),
      '}',
    ),
    foreign_member: $ => seq(
      optional($._attributes),
      field('name', $.identifier),
      choice(
        seq('::', field('type', $.function_type)),
        seq(':', field('type', $._type)),
      ),
    ),

    // ------------------------------------------------------------------
    // Types
    // ------------------------------------------------------------------

    _type: $ => choice($.error_union_type, $._type_atom),

    _type_atom: $ => choice(
      $._type_forms,
      $.tuple_type,
      $.attributed_type,
      $._type_path,
      $.intrinsic_expression,
    ),

    _type_path: $ => choice($.identifier, $.scoped_type, $.generic_type),

    scoped_type: $ => prec.left(1, seq(
      field('scope', $._type_path),
      '.',
      field('name', $.identifier),
    )),
    generic_type: $ => prec.left(1, seq(
      field('type', $._type_path),
      field('arguments', $.arguments),
    )),

    error_union_type: $ => prec.right(1, seq(
      field('error', $._type_atom),
      '!',
      field('ok', $._type),
    )),

    optional_type: $ => seq('?', field('type', $._type_atom)),
    pointer_type: $ => seq('^', optional('const'), field('type', $._type_atom)),
    slice_type: $ => seq('[', ']', optional('const'), field('type', $._type_atom)),
    many_pointer_type: $ => seq('[', '^', ']', optional('const'), field('type', $._type_atom)),
    array_type: $ => seq('[', field('length', $._expression), ']', field('type', $._type_atom)),

    tuple_type: $ => choice(
      seq('(', ')'),
      seq('(', $._type, ')'),
      seq('(', $._type, ',', commaSep($._type), optional(','), ')'),
      seq('(', commaSep1($.named_type), optional(','), ')'),
    ),
    named_type: $ => seq(
      field('name', $.identifier),
      ':',
      field('type', $._type),
    ),

    attributed_type: $ => seq(
      $._attributes,
      $._type_forms,
    ),

    struct_type: $ => seq(
      'struct',
      optional(field('parameters', $.parameters)),
      optional(field('constraints', $.where_clause)),
      field('body', $.field_declaration_list),
    ),
    field_declaration_list: $ => seq('{', listBody($._field_member, $._separators), '}'),
    _field_member: $ => choice($.field_declaration, $.when_fields),
    field_declaration: $ => seq(
      optional($._attributes),
      optional('pub'),
      optional('using'),
      commaSep1(field('name', $.identifier)),
      ':',
      field('type', $._type),
      optional(seq('=', field('default', $._expression))),
    ),
    when_fields: $ => seq(
      'when',
      field('condition', $._expression),
      field('consequence', $.field_declaration_list),
      optional(seq('else', field('alternative', $.field_declaration_list))),
    ),

    union_type: $ => seq(
      'union',
      optional(field('parameters', $.parameters)),
      optional(field('constraints', $.where_clause)),
      field('body', $.variant_list),
    ),
    variant_list: $ => seq('{', listBody($._variant_member, $._separators), '}'),
    _variant_member: $ => choice($.variant, $.when_variants),
    variant: $ => seq(
      field('name', $.identifier),
      optional(seq(':', field('type', $._type))),
      optional(seq('=', field('value', $._expression))),
    ),
    when_variants: $ => seq(
      'when',
      field('condition', $._expression),
      field('consequence', $.variant_list),
      optional(seq('else', field('alternative', $.variant_list))),
    ),

    effect_type: $ => seq(
      'effect',
      optional(field('parameters', $.parameters)),
      field('body', $.operation_list),
    ),
    operation_list: $ => seq('{', listBody($.operation, $._terminators), '}'),
    operation: $ => seq(
      field('name', $.identifier),
      ':',
      field('kind', choice('fn', 'esc', 'ctl')),
      field('parameters', $.parameters),
      optional(seq('->', field('return_type', $._type))),
    ),

    // ------------------------------------------------------------------
    // Lexical
    // ------------------------------------------------------------------

    identifier: _ => /[A-Za-z_][A-Za-z0-9_]*/,

    // `///` starts a doc comment unless a fourth `/` follows. When both match the same
    // text, the earlier rule wins.
    doc_comment: _ => token(/\/\/\/([^\/\n][^\n]*)?/),
    comment: _ => token(/\/\/[^\n]*/),

    _string: $ => choice($.string_literal, $.c_string_literal, $.raw_string_literal),

    string_literal: $ => seq(
      '"',
      repeat(choice($.escape_sequence, $.string_content)),
      token.immediate('"'),
    ),
    c_string_literal: $ => seq(
      token(seq('c', '"')),
      repeat(choice($.escape_sequence, $.string_content)),
      token.immediate('"'),
    ),
    string_content: _ => token.immediate(prec(1, /[^"\\\n]+/)),
    escape_sequence: _ => token.immediate(seq(
      '\\',
      choice(/[nrt0\\"']/, /x[0-9a-fA-F]{2}/, /u\{[0-9a-fA-F]{1,6}\}/),
    )),

    character_literal: _ => token(seq(
      "'",
      choice(
        /[^'\\\n]/,
        seq('\\', choice(/[nrt0\\"']/, /x[0-9a-fA-F]{2}/, /u\{[0-9a-fA-F]{1,6}\}/)),
      ),
      "'",
    )),

    integer_literal: _ => token(seq(
      choice(
        /0x[0-9a-fA-F][0-9a-fA-F_]*/,
        /0o[0-7][0-7_]*/,
        /0b[01][01_]*/,
        /[0-9][0-9_]*/,
      ),
      optional(INT_SUFFIX),
    )),

    float_literal: _ => token(choice(
      seq(/[0-9][0-9_]*\.[0-9][0-9_]*/, optional(/[eE][+-]?[0-9][0-9_]*/), optional(FLOAT_SUFFIX)),
      seq(/[0-9][0-9_]*[eE][+-]?[0-9][0-9_]*/, optional(FLOAT_SUFFIX)),
      seq(/[0-9][0-9_]*/, FLOAT_SUFFIX),
      seq(/0x[0-9a-fA-F][0-9a-fA-F_]*/, optional(seq('.', /[0-9a-fA-F][0-9a-fA-F_]*/)), 'p', optional(/[+-]/), /[0-9][0-9_]*/, optional(FLOAT_SUFFIX)),
    )),
  },
});

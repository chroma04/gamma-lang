// External scanner for the Gamma grammar.
//
// It produces the tokens that need more than a regular expression:
//
//   _newline      a significant newline (spec 2.8), only where the grammar accepts one
//   _arm_newline  the same, inside the body of a `match`, where the continuation rule
//                 for the next line does not apply
//   block_comment a nested block comment
//   raw_string_literal  a string between runs of n backticks
//   _is_not       the two-word operator `is not` (both words on one line)
//   _not_in       the two-word operator `not in`
//   _test_keyword the contextual keyword `test`, only before a string
//
// A block comment that contains a line break counts as a line break (spec 2.2). The
// comment is still produced as a `block_comment` token; its line break follows it as a
// zero-width `_newline`, which the scanner remembers in its one byte of state between
// the two calls. Newlines inside parentheses and brackets never reach the scanner as
// significant, because the grammar does not accept `_newline` there.

#include "tree_sitter/parser.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum TokenType {
  NEWLINE,
  ARM_NEWLINE,
  BLOCK_COMMENT,
  RAW_STRING,
  IS_NOT,
  NOT_IN,
  TEST_KEYWORD,
  BENCH_KEYWORD,
  ERROR_SENTINEL,
};

// The scanner's state: whether the last token was a block comment whose line break is
// a significant newline still to be produced.
typedef struct {
  bool pending_newline;
} Scanner;

void *tree_sitter_gamma_external_scanner_create(void) { return calloc(1, sizeof(Scanner)); }
void tree_sitter_gamma_external_scanner_destroy(void *payload) { free(payload); }
unsigned tree_sitter_gamma_external_scanner_serialize(void *payload, char *buffer) {
  buffer[0] = (char)((Scanner *)payload)->pending_newline;
  return 1;
}
void tree_sitter_gamma_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
  ((Scanner *)payload)->pending_newline = length > 0 && buffer[0];
}

static inline void advance(TSLexer *lexer) { lexer->advance(lexer, false); }
static inline void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

static bool is_ident_start(int32_t c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_ident_cont(int32_t c) { return is_ident_start(c) || (c >= '0' && c <= '9'); }

// Skips the rest of a block comment whose `/*` has been consumed, setting `*newline` if
// it contains a line break. Returns false at the end of the file.
static bool skip_block_comment_body(TSLexer *lexer, bool *newline) {
  int depth = 1;
  while (depth > 0) {
    if (lexer->eof(lexer)) return false;
    int32_t c = lexer->lookahead;
    if (c == '\n') *newline = true;
    advance(lexer);
    if (c == '/' && lexer->lookahead == '*') {
      advance(lexer);
      depth++;
    } else if (c == '*' && lexer->lookahead == '/') {
      advance(lexer);
      depth--;
    }
  }
  return true;
}

// True if the rest of the line has a `:=` outside brackets (spec 2.8, rule 2): then a
// line starting with `.name` is a pattern binding and not a continuation.
static bool line_binds(TSLexer *lexer) {
  int depth = 0;
  while (!lexer->eof(lexer)) {
    int32_t c = lexer->lookahead;
    if (c == '\n') return false;
    switch (c) {
      case '(':
      case '[':
      case '{':
        depth++;
        break;
      case ')':
      case ']':
      case '}':
        depth--;
        if (depth < 0) return false;
        break;
      case '"':
      case '\'': {
        int32_t quote = c;
        advance(lexer);
        while (!lexer->eof(lexer) && lexer->lookahead != quote && lexer->lookahead != '\n') {
          if (lexer->lookahead == '\\') advance(lexer);
          if (lexer->eof(lexer) || lexer->lookahead == '\n') break;
          advance(lexer);
        }
        break;
      }
      case '/':
        advance(lexer);
        if (lexer->lookahead == '/') return false;
        continue;
      case '`':
        return false;
      case ':':
        advance(lexer);
        if (depth == 0 && lexer->lookahead == '=') return true;
        continue;
      default:
        break;
    }
    if (lexer->eof(lexer)) break;
    advance(lexer);
  }
  return false;
}

// Called after the first line terminator has been consumed and marked as the end of the
// token. Looks at the next token to apply rule 2 of spec 2.8. Returns true if the newline
// is significant.
static bool newline_is_significant(TSLexer *lexer, bool in_match_body) {
  for (;;) {
    int32_t c = lexer->lookahead;
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      advance(lexer);
    } else if (c == '/') {
      advance(lexer);
      if (lexer->lookahead == '/') {
        while (!lexer->eof(lexer) && lexer->lookahead != '\n') advance(lexer);
      } else if (lexer->lookahead == '*') {
        advance(lexer);
        bool newline = false;
        if (!skip_block_comment_body(lexer, &newline)) return true;
      } else {
        return true;
      }
    } else {
      break;
    }
  }
  if (in_match_body || lexer->eof(lexer)) return true;

  if (lexer->lookahead == '.') {
    advance(lexer);
    if (is_ident_start(lexer->lookahead)) return line_binds(lexer);
    return true;
  }

  if (is_ident_start(lexer->lookahead)) {
    char word[8];
    size_t len = 0;
    while (is_ident_cont(lexer->lookahead)) {
      if (len < sizeof(word) - 1) word[len] = (char)lexer->lookahead;
      len++;
      advance(lexer);
    }
    if (len < sizeof(word)) {
      word[len] = '\0';
      if (strcmp(word, "else") == 0 || strcmp(word, "and") == 0 || strcmp(word, "or") == 0) return false;
    }
  }
  return true;
}

// Matches `word` at the lookahead, not followed by an identifier character.
static bool match_word(TSLexer *lexer, const char *word) {
  for (const char *p = word; *p; p++) {
    if (lexer->lookahead != (int32_t)*p) return false;
    advance(lexer);
  }
  return !is_ident_cont(lexer->lookahead);
}

static void skip_blanks_advance(TSLexer *lexer) {
  while (lexer->lookahead == ' ' || lexer->lookahead == '\t') advance(lexer);
}

bool tree_sitter_gamma_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid) {
  Scanner *scanner = payload;
  bool pending = scanner->pending_newline;
  scanner->pending_newline = false;
  if (valid[ERROR_SENTINEL]) return false;

  // The line break of the block comment just produced.
  if (pending && (valid[NEWLINE] || valid[ARM_NEWLINE])) {
    lexer->mark_end(lexer);
    lexer->result_symbol = valid[ARM_NEWLINE] ? ARM_NEWLINE : NEWLINE;
    return true;
  }

  while (lexer->lookahead == ' ' || lexer->lookahead == '\t' || lexer->lookahead == '\r') skip(lexer);

  int32_t c = lexer->lookahead;

  if (c == '\n') {
    if (!valid[NEWLINE] && !valid[ARM_NEWLINE]) return false;
    bool in_match_body = valid[ARM_NEWLINE];
    advance(lexer);
    lexer->mark_end(lexer);
    if (!newline_is_significant(lexer, in_match_body)) return false;
    lexer->result_symbol = in_match_body ? ARM_NEWLINE : NEWLINE;
    return true;
  }

  if (c == '/') {
    advance(lexer);
    if (lexer->lookahead != '*') return false;
    advance(lexer);
    bool newline = false;
    if (!skip_block_comment_body(lexer, &newline)) return false;
    lexer->mark_end(lexer);
    lexer->result_symbol = BLOCK_COMMENT;
    // A line break inside the comment is a newline where one may end the statement,
    // subject to the continuation rule for what follows (spec 2.8).
    if (newline && (valid[NEWLINE] || valid[ARM_NEWLINE])) {
      scanner->pending_newline = newline_is_significant(lexer, valid[ARM_NEWLINE]);
    }
    return true;
  }

  if (c == '`' && valid[RAW_STRING]) {
    int open = 0;
    while (lexer->lookahead == '`') {
      advance(lexer);
      open++;
    }
    for (;;) {
      if (lexer->eof(lexer)) return false;
      if (lexer->lookahead == '`') {
        int run = 0;
        while (lexer->lookahead == '`') {
          advance(lexer);
          run++;
        }
        if (run == open) break;
      } else {
        advance(lexer);
      }
    }
    lexer->mark_end(lexer);
    lexer->result_symbol = RAW_STRING;
    return true;
  }

  if (c == 'i' && valid[IS_NOT]) {
    if (!match_word(lexer, "is")) return false;
    if (lexer->lookahead != ' ' && lexer->lookahead != '\t') return false;
    skip_blanks_advance(lexer);
    if (!match_word(lexer, "not")) return false;
    lexer->mark_end(lexer);
    lexer->result_symbol = IS_NOT;
    return true;
  }

  if (c == 'n' && valid[NOT_IN]) {
    if (!match_word(lexer, "not")) return false;
    if (lexer->lookahead != ' ' && lexer->lookahead != '\t') return false;
    skip_blanks_advance(lexer);
    if (!match_word(lexer, "in")) return false;
    lexer->mark_end(lexer);
    lexer->result_symbol = NOT_IN;
    return true;
  }

  if ((c == 't' && valid[TEST_KEYWORD]) || (c == 'b' && valid[BENCH_KEYWORD])) {
    if (!match_word(lexer, c == 't' ? "test" : "bench")) return false;
    lexer->mark_end(lexer);
    skip_blanks_advance(lexer);
    if (lexer->lookahead != '"' && lexer->lookahead != '`') return false;
    lexer->result_symbol = c == 't' ? TEST_KEYWORD : BENCH_KEYWORD;
    return true;
  }

  return false;
}

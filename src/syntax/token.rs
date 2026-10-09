//! Tokens produced by the lexer (spec §2).

use crate::source::Span;
use crate::symbol::Symbol;
use num_bigint::BigUint;
use num_rational::BigRational;
use std::fmt;

#[derive(Clone, Debug, PartialEq)]
pub struct Token {
    pub kind: TokenKind,
    pub span: Span,
}

#[derive(Clone, Debug, PartialEq)]
pub enum TokenKind {
    /// An identifier (never a reserved keyword). Contextual keywords
    /// (`const`, `on`, `where`, `test`) are identifiers; `_` is [`TokenKind::Underscore`].
    Ident(Symbol),
    /// The discard identifier `_`.
    Underscore,
    Keyword(Keyword),
    /// Integer literal, including character literals (`'a'`), which are integers (§2.5.3).
    Int(Box<IntLit>),
    Float(Box<FloatLit>),
    Str(Box<StrLit>),
    /// `@name`.
    Intrinsic(Symbol),
    /// `#name`.
    Attribute(Symbol),
    Punct(Punct),
    /// A significant newline or `;` (§2.8). Consecutive ones are merged.
    Newline,
    Eof,
}

/// Width suffix of an integer literal (§2.5.1).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum IntSuffix {
    /// `uN`
    U(u32),
    /// `iN`
    I(u32),
    Usize,
    Isize,
}

impl fmt::Display for IntSuffix {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            IntSuffix::U(n) => write!(f, "u{}", n),
            IntSuffix::I(n) => write!(f, "i{}", n),
            IntSuffix::Usize => f.write_str("usize"),
            IntSuffix::Isize => f.write_str("isize"),
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum FloatSuffix {
    F16,
    F32,
    F64,
}

#[derive(Clone, Debug, PartialEq)]
pub struct IntLit {
    /// The exact value (literals are never negative; `-` is an operator).
    pub value: BigUint,
    pub suffix: Option<IntSuffix>,
    /// True for character literals such as `'a'`.
    pub is_char: bool,
}

#[derive(Clone, Debug, PartialEq)]
pub struct FloatLit {
    /// The exact value as a rational number (§2.5.2).
    pub value: BigRational,
    pub suffix: Option<FloatSuffix>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum StrKind {
    /// `"..."`
    Regular,
    /// `` `...` ``
    Raw,
    /// `c"..."`
    C,
}

#[derive(Clone, Debug, PartialEq)]
pub struct StrLit {
    /// The bytes after escape processing (no NUL terminator is included for C strings).
    pub bytes: Vec<u8>,
    pub kind: StrKind,
}

macro_rules! keywords {
    ($($variant:ident = $text:literal,)*) => {
        /// Reserved keywords (§2.4).
        #[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
        pub enum Keyword { $($variant,)* }

        impl Keyword {
            pub fn as_str(self) -> &'static str {
                match self { $(Keyword::$variant => $text,)* }
            }

            #[allow(clippy::should_implement_trait)]
            pub fn from_str(s: &str) -> Option<Keyword> {
                match s { $($text => Some(Keyword::$variant),)* _ => None }
            }

            pub const ALL: &'static [Keyword] = &[$(Keyword::$variant,)*];
        }
    };
}

keywords! {
    And = "and", Break = "break", Continue = "continue", Ctl = "ctl", Defer = "defer",
    Discard = "discard", Effect = "effect", Else = "else", Errdefer = "errdefer", Esc = "esc",
    False = "false", Fn = "fn", For = "for", Foreign = "foreign", Handler = "handler", If = "if",
    Import = "import", In = "in", Is = "is", Match = "match", Not = "not", Null = "null",
    Or = "or", Pub = "pub", Resume = "resume", Return = "return", Struct = "struct",
    True = "true", Try = "try", Union = "union", Using = "using", When = "when",
    While = "while", With = "with",
}

macro_rules! puncts {
    ($($variant:ident = $text:literal,)*) => {
        /// Operators and punctuation (§2.6), plus the two-word operators
        /// `is not` and `not in`, which the lexer forms when both words are on one line.
        #[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
        pub enum Punct { $($variant,)* }

        impl Punct {
            pub fn as_str(self) -> &'static str {
                match self { $(Punct::$variant => $text,)* }
            }

            pub const ALL: &'static [Punct] = &[$(Punct::$variant,)*];
        }
    };
}

puncts! {
    Plus = "+", Minus = "-", Star = "*", Slash = "/", Percent = "%",
    Shl = "<<", Shr = ">>", Amp = "&", Pipe = "|", Tilde = "~",
    PlusWrap = "+%", MinusWrap = "-%", StarWrap = "*%", ShlWrap = "<<%",
    PlusSat = "+|", MinusSat = "-|", StarSat = "*|", ShlSat = "<<|",
    PlusOpt = "+?", MinusOpt = "-?", StarOpt = "*?", ShlOpt = "<<?",
    Assign = "=", PlusAssign = "+=", MinusAssign = "-=", StarAssign = "*=", SlashAssign = "/=",
    PercentAssign = "%=", ShlAssign = "<<=", ShrAssign = ">>=", AmpAssign = "&=",
    PipeAssign = "|=", TildeAssign = "~=",
    PlusWrapAssign = "+%=", MinusWrapAssign = "-%=", StarWrapAssign = "*%=", ShlWrapAssign = "<<%=",
    PlusSatAssign = "+|=", MinusSatAssign = "-|=", StarSatAssign = "*|=", ShlSatAssign = "<<|=",
    Lt = "<", Le = "<=", Gt = ">", Ge = ">=",
    Question = "?", Bang = "!", Caret = "^", At = "@", Hash = "#", Backslash = "\\",
    DotDot = "..", DotDotEq = "..=", Ellipsis = "...", Uninit = "---",
    ColonColon = "::", ColonEq = ":=", Colon = ":", Comma = ",", Dot = ".",
    FatArrow = "=>", Arrow = "->",
    LParen = "(", RParen = ")", LBracket = "[", RBracket = "]", LBrace = "{", RBrace = "}",
    IsNot = "is not", NotIn = "not in",
}

impl TokenKind {
    pub fn is_punct(&self, p: Punct) -> bool {
        matches!(self, TokenKind::Punct(q) if *q == p)
    }

    pub fn is_keyword(&self, k: Keyword) -> bool {
        matches!(self, TokenKind::Keyword(q) if *q == k)
    }

    /// True for an identifier with the given text (used for contextual keywords).
    pub fn is_ident(&self, text: &str) -> bool {
        matches!(self, TokenKind::Ident(s) if s.as_str() == text)
    }

    /// A short human-readable description for diagnostics.
    pub fn describe(&self) -> String {
        match self {
            TokenKind::Ident(s) => format!("identifier `{}`", s),
            TokenKind::Underscore => "`_`".to_string(),
            TokenKind::Keyword(k) => format!("keyword `{}`", k.as_str()),
            TokenKind::Int(l) if l.is_char => "character literal".to_string(),
            TokenKind::Int(_) => "integer literal".to_string(),
            TokenKind::Float(_) => "float literal".to_string(),
            TokenKind::Str(_) => "string literal".to_string(),
            TokenKind::Intrinsic(s) => format!("intrinsic `@{}`", s),
            TokenKind::Attribute(s) => format!("attribute `#{}`", s),
            TokenKind::Punct(p) => format!("`{}`", p.as_str()),
            TokenKind::Newline => "newline".to_string(),
            TokenKind::Eof => "end of file".to_string(),
        }
    }
}

/// A doc comment (`/// text`) recorded by the lexer. The parser attaches it to
/// the declaration that begins on the line after the comment block.
#[derive(Clone, Debug, PartialEq)]
pub struct DocComment {
    pub span: Span,
    /// The comment text after `///`, with one leading space removed if present.
    pub text: String,
}

/// The result of lexing one file.
#[derive(Clone, Debug, Default)]
pub struct LexOutput {
    /// Tokens, ending with exactly one [`TokenKind::Eof`].
    pub tokens: Vec<Token>,
    pub docs: Vec<DocComment>,
}

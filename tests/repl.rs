//! Tests of `gamma repl`, driven through pipes: stdin is not a terminal, so the
//! REPL prints no banner and no prompts, only what the inputs print.

use std::io::Write;
use std::process::{Command, Stdio};

const GAMMA: &str = env!("CARGO_BIN_EXE_gamma");

/// Feeds `input` to a REPL and returns its standard output and error.
fn repl(input: &str) -> (String, String) {
    let mut child = Command::new(GAMMA)
        .arg("repl")
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .expect("cannot start gamma");
    child.stdin.take().unwrap().write_all(input.as_bytes()).unwrap();
    let out = child.wait_with_output().unwrap();
    assert!(out.status.success(), "the REPL failed: {:?}", out.status);
    (String::from_utf8_lossy(&out.stdout).into_owned(), String::from_utf8_lossy(&out.stderr).into_owned())
}

#[test]
fn expressions_print_their_values() {
    let (out, err) = repl("1 + 2\n\"hello\"\n(1, true)\n2.5 * 2.0\n");
    assert_eq!(out, "3\nhello\n(1, true)\n5\n");
    assert_eq!(err, "");
}

#[test]
fn unit_expressions_print_nothing() {
    let (out, err) = repl("print(\"hi\\n\")\n");
    assert_eq!((out.as_str(), err.as_str()), ("hi\n", ""));
}

#[test]
fn bindings_persist() {
    let (out, err) = repl("x := 10\nx * 2\ny :: x + 1\ny\nx = 7\nx + y\n");
    assert_eq!(out, "20\n11\n18\n");
    assert_eq!(err, "");
}

#[test]
fn a_binding_can_be_defined_again_with_another_type() {
    let (out, err) = repl("x := 5\ny := x + 1\nx := \"text\"\nx\ny\nx := x\nx\n");
    assert_eq!(out, "text\n6\ntext\n");
    assert_eq!(err, "");
}

#[test]
fn statements_run_once() {
    // Printing is not repeated by the replay that keeps variables alive.
    let (out, _) = repl("greet :: fn() needs Out { print(\"hello\\n\") }\nn := 1\ngreet()\nn := n + 1\nn\ngreet()\n");
    assert_eq!(out, "hello\n2\nhello\n");
}

#[test]
fn mutation_through_statements_and_methods_is_kept() {
    let (out, err) = repl(
        "total := 0\nfor i in 0..5 {\n    total += i\n}\ntotal\nl := list(i32)\ntry l.push(3)\ntry l.push(4)\nl.items\n",
    );
    assert_eq!(out, "10\n[3, 4]\n");
    assert_eq!(err, "");
}

#[test]
fn functions_can_be_defined_and_redefined() {
    let (out, err) = repl(
        "sq :: fn(n: i32) -> i32 => n * n\nsq(7)\nsq :: fn(n: i32) -> i32 => n * n + 1\nsq(7)\nuse_sq :: fn() -> i32 => sq(2)\nuse_sq()\n",
    );
    assert_eq!(out, "49\n50\n5\n");
    assert_eq!(err, "");
}

#[test]
fn types_and_constants() {
    let (out, err) = repl(
        "Point :: struct {\n    x: i32,\n    y: i32,\n}\np := Point(x: 1, y: 2)\np\np.x + p.y\nN :: 3\narr := [N]i32(1, 2, 3)\narr[N - 1]\n",
    );
    assert_eq!(out, "Point(x: 1, y: 2)\n3\n3\n");
    assert_eq!(err, "");
}

#[test]
fn multi_line_input() {
    let (out, err) = repl(
        "total :: fn(n: i32) -> i32 {\n    s := 0\n    for i in 0..n {\n        s += i\n    }\n    return s\n}\ntotal(5)\n(1 +\n 2)\n",
    );
    assert_eq!(out, "10\n3\n");
    assert_eq!(err, "");
}

#[test]
fn type_command() {
    let (out, err) = repl("x := 5\n:type x\n:type 1 + 2\n:type \"s\"\n:type (x, true)\n:type [2]u8(1, 2)\n");
    assert_eq!(out, "i32\ni32\nstring\n(i32, bool)\n[2]u8\n");
    assert_eq!(err, "");
}

#[test]
fn type_command_does_not_run_its_expression() {
    let (out, _) = repl("noisy :: fn() -> i32 needs Out { print(\"ran\\n\"); return 1 }\n:type noisy()\n");
    assert_eq!(out, "i32\n");
}

#[test]
fn errors_leave_the_session_unchanged() {
    let (out, err) = repl("x := 1\ny := undefined_name\nx + \"a\"\nx\nbad :: fn() -> i32 => \"s\"\nx + 1\n");
    assert_eq!(out, "1\n2\n");
    assert!(err.contains("unknown name `undefined_name`"), "{}", err);
    // Positions are relative to the input, not to the synthesized program.
    assert!(err.contains("--> <repl>:1:6"), "{}", err);
    assert!(!err.contains("<session>"), "{}", err);
}

#[test]
fn a_failed_definition_is_not_remembered() {
    let (out, err) = repl("f :: fn() -> i32 => 1\nf :: fn() -> i32 => \"s\"\nf()\n");
    assert_eq!(out, "1\n");
    assert!(err.contains("error"), "{}", err);
}

#[test]
fn a_trap_is_reported_and_the_session_continues() {
    let (out, err) = repl("x := 3\ny := 10 / (x - 3)\nx\ntrap(\"boom\")\nx + 1\n");
    assert_eq!(out, "3\n4\n");
    assert!(err.contains("trap: division by zero"), "{}", err);
    assert!(err.contains("trap: boom"), "{}", err);
    assert!(err.contains("at <repl>:1:"), "{}", err);
}

#[test]
fn std_modules_can_be_imported() {
    let (out, err) = repl("math :: import \"std/math\"\nmath.sqrt(16.0)\nmath.floor(2.7)\n");
    assert_eq!(out, "4\n2\n");
    assert_eq!(err, "");
}

#[test]
fn effects_of_definitions_are_noted() {
    let (out, err) =
        repl("get :: fn() -> i32 needs Out {\n    print(\"once\\n\")\n    return 7\n}\nv := get()\nv + 1\nv + 2\n");
    // `v` is evaluated again before each later input, but only the first run is visible.
    assert_eq!(out, "once\n8\n9\n");
    assert!(err.contains("note: v will be evaluated again"), "{}", err);
}

#[test]
fn list_and_reset() {
    let (out, err) = repl("x := 4\nsq :: fn(n: i32) -> i32 => n * n\n:list\n:reset\n:list\nx\n");
    assert!(out.contains("sq :: fn(n: i32) -> i32 => n * n\nx := 4\n"), "{}", out);
    assert!(err.contains("unknown name `x`"), "{}", err);
}

#[test]
fn load_adds_declarations() {
    let dir = std::env::temp_dir().join(format!("gamma_repl_load_{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let file = dir.join("lib.gm");
    std::fs::write(&file, "double :: fn(n: i32) -> i32 => n * 2\n\nmain :: fn() {\n}\n").unwrap();
    let (out, err) = repl(&format!(":load {}\ndouble(21)\n", file.display()));
    std::fs::remove_dir_all(&dir).ok();
    assert_eq!(out, "42\n");
    assert!(err.contains("`main`"), "{}", err);
}

#[test]
fn commands_help_and_quit() {
    let (out, _) = repl(":help\n:quit\n1 + 1\n");
    assert!(out.contains(":type <expr>"), "{}", out);
    assert!(!out.contains("\n2\n"), "{}", out);
}

#[test]
fn unfinished_input_at_end_of_input_is_reported() {
    let (out, err) = repl("1 + 1\nf :: fn() {\n");
    assert_eq!(out, "2\n");
    assert!(err.contains("error"), "{}", err);
}

#[test]
fn main_is_reserved() {
    let (_, err) = repl("main :: fn() {}\n");
    assert!(err.contains("`main` is reserved"), "{}", err);
}

#[test]
fn a_stray_closing_bracket_is_reported_at_once() {
    let (out, err) = repl("}\n3\n4\n");
    assert_eq!(out, "3\n4\n");
    assert!(err.contains("unmatched"), "{err}");
}

#[test]
fn a_line_opening_a_block_comment_is_not_blank() {
    let (out, err) = repl("/* a\n*/\n5\n");
    assert_eq!(out, "5\n");
    assert_eq!(err, "");
}

#[test]
fn a_missing_preload_file_is_an_error_status() {
    let out = Command::new(GAMMA)
        .args(["repl", "definitely_missing_file.gm"])
        .stdin(Stdio::null())
        .output()
        .expect("cannot start gamma");
    assert_eq!(out.status.code(), Some(1), "{:?}", out);
    assert!(String::from_utf8_lossy(&out.stderr).contains("cannot read"));
}

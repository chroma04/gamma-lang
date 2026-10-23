/* Support for programs compiled by the experimental Gamma LLVM back end (src/llvm).
 *
 * The LLVM back end writes the program as textual LLVM IR, which clang compiles to an object,
 * and links it with the runtime compiled on its own (GM_RT_SEPARATE): gm_rt.h, gm_rt.c and
 * then this file, as one translation unit. This file adds the process's `main` and the
 * formatting of the scalar values that `print` writes. The generated code calls gm_trap for
 * failed checks and gm_write_out for literal text directly. */

/* Nonzero in Debug, Checked and Safe builds; the C back end defines it in each program. */
GM_API int gm_checks = 1;

/* Defined by the generated code: the program's `main` and the build mode. */
extern void gm_ll_main(void);
extern const uint8_t gm_ll_debug, gm_ll_checks;

static int gm_ll_run(gm_node *cx) {
    (void)cx;
    gm_ll_main();
    gm_flush();
    return 0;
}

int main(int argc, char **argv) {
    gm_set_args(argc, argv);
    gm_thread_stack();
    gm_debug = gm_ll_debug;
    gm_checks = gm_ll_checks;
    return gm_run_main(gm_ll_run, NULL);
}

/* Writes the value at `v` of a scalar type of `kind`, `bits` and signedness to stdout as `{}`
 * formats it. */
static void gm_ll_write(uint8_t kind, uint16_t bits, bool sgn, const void *v) {
    gm_type t;
    memset(&t, 0, sizeof t);
    t.kind = kind;
    t.bits = bits;
    t.is_signed = sgn;
    t.size = kind == GM_K_BOOL ? 1 : bits / 8;
    t.align = t.size;
    gm_buf b = {0};
    gm_format(&b, &t, v, true);
    gm_write_out(b.data, b.len);
    gm_buf_free(&b);
}

GM_API void gm_ll_write_int(uint64_t v, uint32_t bits, uint32_t sgn) {
    /* The value's low `bits` bits in target byte order, as the formatter reads them. */
    uint8_t s[8];
    switch (bits) {
    case 8: s[0] = (uint8_t)v; break;
    case 16: { uint16_t x = (uint16_t)v; memcpy(s, &x, 2); break; }
    case 32: { uint32_t x = (uint32_t)v; memcpy(s, &x, 4); break; }
    default: memcpy(s, &v, 8); break;
    }
    gm_ll_write(GM_K_INT, (uint16_t)bits, sgn != 0, s);
}

GM_API void gm_ll_write_f64(double v) { gm_ll_write(GM_K_FLOAT, 64, false, &v); }
GM_API void gm_ll_write_f32(float v) { gm_ll_write(GM_K_FLOAT, 32, false, &v); }

GM_API void gm_ll_write_bool(uint8_t v) { gm_ll_write(GM_K_BOOL, 1, false, &v); }

/* ---- checks whose trap messages are formatted (as the other back ends word them) ---------- */

GM_API _Noreturn void gm_ll_trap_index(uint64_t i, uint64_t len, const char *file, uint32_t line, uint32_t col) {
    char m[96];
    snprintf(m, sizeof m, "index %llu out of bounds for length %llu", (unsigned long long)i, (unsigned long long)len);
    gm_trap(m, file, line, col);
}

GM_API _Noreturn void gm_ll_trap_shift(uint64_t n, const char *ty, const char *file, uint32_t line, uint32_t col) {
    char m[128];
    snprintf(m, sizeof m, "shift amount %llu too large for %s", (unsigned long long)n, ty);
    gm_trap(m, file, line, col);
}

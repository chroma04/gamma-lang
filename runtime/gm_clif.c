/* Support for programs compiled by the Gamma Cranelift back end (src/clif).
 *
 * The Cranelift back end emits machine code for the program itself and links it with the
 * runtime compiled on its own (GM_RT_SEPARATE): gm_rt.h, gm_rt.c and then this file, as one
 * translation unit. This file adds what the C back end writes into each program as C: the
 * process's `main`, structural equality, ordering and hashing of values (driven by the type
 * descriptors the formatter reads, rather than by a function per type), the checks whose
 * trap messages are formatted, arithmetic on 128-bit integers and on f16, and the writes of
 * `@write` to an output effect.
 *
 * The generated code reaches the runtime's records (gm_node, gm_fiber, gm_unw, …) at fixed
 * offsets, which the assertions below pin down. The back end supports 64-bit targets only.
 */

#include <stddef.h>

_Static_assert(sizeof(void *) == 8, "the Cranelift back end supports 64-bit targets only");
_Static_assert(sizeof(gm_frame) == 16 && offsetof(gm_frame, clause_lit) == 8, "gm_frame");
_Static_assert(sizeof(gm_node) == 72 && offsetof(gm_node, kind) == 4 && offsetof(gm_node, clauses) == 8 &&
                   offsetof(gm_node, env) == 16 && offsetof(gm_node, hctx) == 24 &&
                   offsetof(gm_node, with_frame) == 32 && offsetof(gm_node, with_label) == 40 &&
                   offsetof(gm_node, result) == 48 && offsetof(gm_node, fw) == 56 && offsetof(gm_node, next) == 64,
               "gm_node");
_Static_assert(sizeof(gm_unwind) == 24 && offsetof(gm_unwind, frame) == 8 && offsetof(gm_unwind, label) == 16 &&
                   offsetof(gm_unwind, is_err) == 20 && offsetof(gm_unwind, code) == 21,
               "gm_unwind");
_Static_assert(sizeof(gm_bt) == 32 && offsetof(gm_bt, fn) == 8 && offsetof(gm_bt, file) == 16 &&
                   offsetof(gm_bt, line) == 24 && offsetof(gm_bt, col) == 28,
               "gm_bt");
_Static_assert(sizeof(gm_with) == 64 && offsetof(gm_with, body_cx) == 32 && offsetof(gm_with, event) == 40 &&
                   offsetof(gm_with, event_fiber) == 48 && offsetof(gm_with, next) == 56,
               "gm_with");
_Static_assert(offsetof(gm_fiber, res) == 112 && offsetof(gm_fiber, frame) == 120 && offsetof(gm_fiber, env) == 128 &&
                   offsetof(gm_fiber, fn) == 136 && offsetof(gm_fiber, arg) == 144 && offsetof(gm_fiber, cx) == 152 &&
                   offsetof(gm_fiber, inst) == 160,
               "gm_fiber");
_Static_assert(sizeof(gm_buf) == 24 && offsetof(gm_buf, len) == 8, "gm_buf");
_Static_assert(sizeof(gm_spec) == 16 && offsetof(gm_spec, width) == 8 && offsetof(gm_spec, precision) == 12, "gm_spec");
_Static_assert(sizeof(gm_field) == 24 && offsetof(gm_field, offset) == 8 && offsetof(gm_field, type) == 16, "gm_field");
_Static_assert(sizeof(gm_variant) == 24 && offsetof(gm_variant, tag) == 8 && offsetof(gm_variant, payload) == 16,
               "gm_variant");
_Static_assert(sizeof(gm_type) == 104 && offsetof(gm_type, size) == 4 && offsetof(gm_type, align) == 8 &&
                   offsetof(gm_type, name) == 16 && offsetof(gm_type, bits) == 24 && offsetof(gm_type, is_signed) == 26 &&
                   offsetof(gm_type, elem) == 32 && offsetof(gm_type, err) == 40 && offsetof(gm_type, len) == 48 &&
                   offsetof(gm_type, nfields) == 56 && offsetof(gm_type, fields) == 64 && offsetof(gm_type, named) == 72 &&
                   offsetof(gm_type, flag_offset) == 76 && offsetof(gm_type, tag_offset) == 80 &&
                   offsetof(gm_type, payload_offset) == 84 && offsetof(gm_type, tag_size) == 88 &&
                   offsetof(gm_type, tag_signed) == 89 && offsetof(gm_type, nvariants) == 92 &&
                   offsetof(gm_type, variants) == 96,
               "gm_type");

typedef unsigned __int128 gm_u128;
typedef __int128 gm_i128;

/* Nonzero in Debug, Checked and Safe builds; the C back end defines it in each program. */
GM_API int gm_checks = 1;

/* ---- the program's entry points ----------------------------------------------------------
 *
 * The generated code defines gm_clif_prog: its entry functions (`main`, or each test of a
 * `gamma test` binary, chosen by the first argument), the build mode and the effect ids of
 * the ambient handlers. Each entry function runs the Gamma function and stores its result;
 * what follows turns the result into the exit status, as the interpreter does. */

typedef struct {
    /* Runs the entry under handler context `cx` and stores its result at `out`. */
    void (*fn)(gm_node *cx, void *out);
    uint32_t ret_size, ret_align;
    /* 0: the status is 0; 1: the result is an integer of int_size bytes, whose low byte is the
     * status; 2: the result is a result type (tag at tag_offset, payload at payload_offset) whose
     * error is reported with err (named err_name first, if not NULL), and whose success value
     * is an integer of int_size bytes (0: not an integer). */
    uint8_t kind;
    uint32_t int_size;
    uint32_t tag_offset, payload_offset;
    const gm_type *err;
    const char *err_name;
} gm_clif_entry;

typedef struct {
    uint32_t nentries;
    uint8_t select, debug, checks;
    /* The effect ids of Out, Err and Env, or UINT32_MAX for those the program does not use. */
    uint32_t ambient[3];
    const gm_clif_entry *entries;
} gm_clif_program;

extern const gm_clif_program gm_clif_prog;

static const gm_clif_entry *gm_clif_running;

static int gm_clif_run(gm_node *cx) {
    const gm_clif_entry *e = gm_clif_running;
    size_t align = e->ret_align < 16 ? 16 : e->ret_align;
    size_t size = (e->ret_size + align - 1) / align * align;
    uint8_t *r = (uint8_t *)aligned_alloc(align, size == 0 ? align : size);
    if (r == NULL) gm_trap("out of memory", NULL, 0, 0);
    memset(r, 0, size == 0 ? align : size);
    e->fn(cx, r);
    if (gm_unw.kind == GM_EXIT) {
        gm_flush();
        return gm_unw.code;
    }
    int status = 0;
    if (e->kind == 1) {
        status = *gm_ibp(r, e->int_size, 0);
    } else if (e->kind == 2) {
        if (r[e->tag_offset]) {
            gm_buf b = {0};
            gm_buf_push(&b, "error: ", 7);
            if (e->err_name != NULL) gm_buf_push(&b, e->err_name, strlen(e->err_name));
            gm_format(&b, e->err, r + e->payload_offset, true);
            gm_buf_push(&b, "\n", 1);
            gm_flush();
            gm_write_err(b.data, b.len);
            return 1;
        }
        if (e->int_size > 0) status = *gm_ibp(r + e->payload_offset, e->int_size, 0);
    }
    gm_flush();
    return status;
}

int main(int argc, char **argv) {
    long which = 0;
    if (gm_clif_prog.select) {
        /* The program sees only its own name; the index selects the entry. */
        if (argc < 2) {
            fputs("usage: test-binary INDEX\n", stderr);
            return 2;
        }
        which = strtol(argv[1], NULL, 10);
        gm_set_args(1, argv);
    } else {
        gm_set_args(argc, argv);
    }
    gm_thread_stack();
    gm_debug = gm_clif_prog.debug;
    gm_checks = gm_clif_prog.checks;
    gm_node *cx = NULL;
    static gm_node amb[3];
    for (int k = 0; k < 3; k++) {
        if (gm_clif_prog.ambient[k] == UINT32_MAX) continue;
        memset(&amb[k], 0, sizeof amb[k]);
        amb[k].eff = gm_clif_prog.ambient[k];
        amb[k].kind = GM_NATIVE;
        amb[k].next = cx;
        cx = &amb[k];
    }
    if (which < 0 || (unsigned long)which >= gm_clif_prog.nentries) return 2;
    gm_clif_running = &gm_clif_prog.entries[which];
    return gm_run_main(gm_clif_run, cx);
}

/* ---- structural equality, ordering and hashing (§7.5, §7.6, §16.8) ----------------------
 *
 * These follow the C back end's per-type functions exactly (the hash too, so that hash maps
 * iterate in the same order everywhere). An opaque descriptor with bits == 1 is a #c_union,
 * whose values are never equal. */

/* The integer of type t at p, sign- or zero-extended from its bits. */
static gm_u128 gm_clif_int(const gm_type *t, const uint8_t *p) {
    gm_u128 v = gm_load_u128(p, t->size);
    unsigned bits = t->bits;
    if (bits > 0 && bits < 128) {
        v &= (((gm_u128)1) << bits) - 1;
        if (t->is_signed && ((v >> (bits - 1)) & 1)) v |= ~(gm_u128)0 << bits;
    }
    return v;
}

static double gm_clif_float(const gm_type *t, const uint8_t *p) {
    if (t->bits == 16) {
        uint16_t h;
        memcpy(&h, p, 2);
        return gm_f16_to_f64(h);
    }
    if (t->bits == 32) {
        float f;
        memcpy(&f, p, 4);
        return f;
    }
    double d;
    memcpy(&d, p, 8);
    return d;
}

static uint64_t gm_clif_ptr(const uint8_t *p) {
    uint64_t v;
    memcpy(&v, p, 8);
    return v;
}

static int64_t gm_clif_tag(const gm_type *t, const uint8_t *v) {
    uint64_t raw = gm_load_uint(v + t->tag_offset, t->tag_size);
    int64_t tag = (int64_t)raw;
    if (t->tag_signed && t->tag_size > 0 && t->tag_size < 8) {
        unsigned shift = 64 - 8u * t->tag_size;
        tag = (int64_t)(raw << shift) >> shift;
    }
    return tag;
}

GM_API bool gm_clif_eq(const gm_type *t, const uint8_t *a, const uint8_t *b) {
    switch (t->kind) {
    case GM_K_INT: return gm_clif_int(t, a) == gm_clif_int(t, b);
    case GM_K_FLOAT: return gm_clif_float(t, a) == gm_clif_float(t, b);
    case GM_K_BOOL: return (a[0] != 0) == (b[0] != 0);
    case GM_K_PTR: return gm_clif_ptr(a) == gm_clif_ptr(b);
    case GM_K_BITS: return gm_load_u128(a, t->size) == gm_load_u128(b, t->size);
    case GM_K_TUPLE:
    case GM_K_STRUCT:
        for (uint32_t i = 0; i < t->nfields; i++) {
            const gm_field *f = &t->fields[i];
            if (!gm_clif_eq(f->type, a + f->offset, b + f->offset)) return false;
        }
        return true;
    case GM_K_ARRAY: {
        size_t esz = t->elem->size;
        for (uint64_t i = 0; i < t->len; i++)
            if (!gm_clif_eq(t->elem, a + i * esz, b + i * esz)) return false;
        return true;
    }
    case GM_K_SLICE: {
        uint64_t n = gm_clif_ptr(a + 8);
        if (n != gm_clif_ptr(b + 8)) return false;
        const uint8_t *pa = (const uint8_t *)(uintptr_t)gm_clif_ptr(a), *pb = (const uint8_t *)(uintptr_t)gm_clif_ptr(b);
        size_t esz = t->elem->size;
        for (uint64_t i = 0; i < n; i++)
            if (!gm_clif_eq(t->elem, pa + i * esz, pb + i * esz)) return false;
        return true;
    }
    case GM_K_OPTIONAL:
        if (t->flag_offset == GM_NICHE) return gm_clif_ptr(a) == gm_clif_ptr(b);
        if ((a[t->flag_offset] != 0) != (b[t->flag_offset] != 0)) return false;
        return !a[t->flag_offset] || gm_clif_eq(t->elem, a, b);
    case GM_K_RESULT: {
        bool ea = a[t->tag_offset] != 0, eb = b[t->tag_offset] != 0;
        if (ea != eb) return false;
        return gm_clif_eq(ea ? t->err : t->elem, a + t->payload_offset, b + t->payload_offset);
    }
    case GM_K_UNION: {
        int64_t ta = gm_clif_tag(t, a), tb = gm_clif_tag(t, b);
        if (ta != tb) return false;
        for (uint32_t i = 0; i < t->nvariants; i++) {
            const gm_variant *v = &t->variants[i];
            if (v->tag == ta && v->payload != NULL)
                return gm_clif_eq(v->payload, a + t->payload_offset, b + t->payload_offset);
        }
        return true;
    }
    case GM_K_OPAQUE: return t->bits != 1;
    }
    return true;
}

GM_API bool gm_clif_lt(const gm_type *t, const uint8_t *a, const uint8_t *b) {
    switch (t->kind) {
    case GM_K_INT:
        if (t->is_signed) return (gm_i128)gm_clif_int(t, a) < (gm_i128)gm_clif_int(t, b);
        return gm_clif_int(t, a) < gm_clif_int(t, b);
    case GM_K_FLOAT: return gm_clif_float(t, a) < gm_clif_float(t, b);
    case GM_K_BOOL: return a[0] < b[0];
    case GM_K_PTR: return gm_clif_ptr(a) < gm_clif_ptr(b);
    case GM_K_BITS: return gm_load_u128(a, t->size) < gm_load_u128(b, t->size);
    case GM_K_TUPLE:
        for (uint32_t i = 0; i < t->nfields; i++) {
            const gm_field *f = &t->fields[i];
            if (!gm_clif_eq(f->type, a + f->offset, b + f->offset)) return gm_clif_lt(f->type, a + f->offset, b + f->offset);
        }
        return false;
    case GM_K_ARRAY:
    case GM_K_SLICE: {
        uint64_t na, nb;
        const uint8_t *pa, *pb;
        if (t->kind == GM_K_ARRAY) {
            na = nb = t->len;
            pa = a;
            pb = b;
        } else {
            na = gm_clif_ptr(a + 8);
            nb = gm_clif_ptr(b + 8);
            pa = (const uint8_t *)(uintptr_t)gm_clif_ptr(a);
            pb = (const uint8_t *)(uintptr_t)gm_clif_ptr(b);
        }
        size_t esz = t->elem->size;
        for (uint64_t i = 0; i < na && i < nb; i++) {
            if (!gm_clif_eq(t->elem, pa + i * esz, pb + i * esz)) return gm_clif_lt(t->elem, pa + i * esz, pb + i * esz);
        }
        return na < nb;
    }
    case GM_K_UNION: return gm_clif_tag(t, a) < gm_clif_tag(t, b);
    default: return false;
    }
}

static uint64_t gm_clif_mix(uint64_t h, const void *p, size_t n) {
    const uint8_t *b = (const uint8_t *)p;
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

/* Mixes the eight little-endian bytes of x, whatever the byte order of the machine. */
static uint64_t gm_clif_mix64(uint64_t h, uint64_t x) {
    for (int i = 0; i < 8; i++) {
        h ^= (uint8_t)(x >> (8 * i));
        h *= 0x100000001b3ULL;
    }
    return h;
}

/* Mixes the minimal two's-complement little-endian bytes of v (as BigInt::to_signed_bytes_le). */
static uint64_t gm_clif_hash_int(uint64_t h, gm_i128 v) {
    for (;;) {
        uint8_t byte = (uint8_t)v;
        v >>= 8;
        h ^= byte;
        h *= 0x100000001b3ULL;
        if ((v == 0 && !(byte & 0x80)) || (v == -1 && (byte & 0x80))) break;
    }
    return h;
}

static uint64_t gm_clif_hash_uint(uint64_t h, gm_u128 v) {
    for (;;) {
        uint8_t byte = (uint8_t)v;
        v >>= 8;
        h ^= byte;
        h *= 0x100000001b3ULL;
        if (v == 0 && !(byte & 0x80)) break;
        if (v == 0) {
            h *= 0x100000001b3ULL;
            break;
        }
    }
    return h;
}

GM_API uint64_t gm_clif_hash(const gm_type *t, const uint8_t *v, uint64_t h) {
    switch (t->kind) {
    case GM_K_INT:
        if (t->is_signed) return gm_clif_hash_int(h, (gm_i128)gm_clif_int(t, v));
        return gm_clif_hash_uint(h, gm_clif_int(t, v));
    case GM_K_FLOAT: {
        double x = gm_clif_float(t, v);
        if (x == 0) x = 0;
        uint64_t bits;
        memcpy(&bits, &x, 8);
        return gm_clif_mix64(h, bits);
    }
    case GM_K_BOOL: {
        uint8_t x = v[0] != 0;
        return gm_clif_mix(h, &x, 1);
    }
    case GM_K_PTR: return gm_clif_mix64(h, gm_clif_ptr(v));
    case GM_K_BITS: return gm_clif_hash_uint(h, gm_load_u128(v, t->size));
    case GM_K_TUPLE:
    case GM_K_STRUCT:
        for (uint32_t i = 0; i < t->nfields; i++) h = gm_clif_hash(t->fields[i].type, v + t->fields[i].offset, h);
        return h;
    case GM_K_ARRAY: {
        size_t esz = t->elem->size;
        for (uint64_t i = 0; i < t->len; i++) h = gm_clif_hash(t->elem, v + i * esz, h);
        return h;
    }
    case GM_K_SLICE: {
        uint64_t n = gm_clif_ptr(v + 8);
        const uint8_t *d = (const uint8_t *)(uintptr_t)gm_clif_ptr(v);
        size_t esz = t->elem->size;
        h = gm_clif_mix64(h, n);
        for (uint64_t i = 0; i < n; i++) h = gm_clif_hash(t->elem, d + i * esz, h);
        return h;
    }
    case GM_K_OPTIONAL: {
        if (t->flag_offset == GM_NICHE) return gm_clif_mix64(h, gm_clif_ptr(v));
        uint8_t s = v[t->flag_offset] != 0;
        h = gm_clif_mix(h, &s, 1);
        return s ? gm_clif_hash(t->elem, v, h) : h;
    }
    case GM_K_RESULT: {
        uint8_t e = v[t->tag_offset] != 0;
        h = gm_clif_mix(h, &e, 1);
        return gm_clif_hash(e ? t->err : t->elem, v + t->payload_offset, h);
    }
    case GM_K_UNION: {
        int64_t tag = gm_clif_tag(t, v);
        for (uint32_t i = 0; i < t->nvariants; i++) {
            const gm_variant *var = &t->variants[i];
            if (var->tag != tag) continue;
            h = gm_clif_mix64(h, i);
            if (var->payload != NULL) h = gm_clif_hash(var->payload, v + t->payload_offset, h);
            return h;
        }
        return h;
    }
    default: return h;
    }
}

/* ---- checks whose trap messages are formatted -------------------------------------------- */

GM_API _Noreturn void gm_clif_trap_index(uint64_t i, uint64_t len, const char *file, uint32_t line, uint32_t col) {
    char m[96];
    snprintf(m, sizeof m, "index %llu out of bounds for length %llu", (unsigned long long)i, (unsigned long long)len);
    gm_trap(m, file, line, col);
}

GM_API _Noreturn void gm_clif_trap_slice(uint64_t lo, uint64_t hi, uint64_t len, const char *file, uint32_t line,
                                        uint32_t col) {
    char m[128];
    snprintf(m, sizeof m, "slice bounds %llu..%llu invalid for length %llu", (unsigned long long)lo,
             (unsigned long long)hi, (unsigned long long)len);
    gm_trap(m, file, line, col);
}

GM_API _Noreturn void gm_clif_trap_shift(uint64_t n, const char *ty, const char *file, uint32_t line, uint32_t col) {
    char m[128];
    snprintf(m, sizeof m, "shift amount %llu too large for %s", (unsigned long long)n, ty);
    gm_trap(m, file, line, col);
}

/* `x!` of an error value. */
GM_API _Noreturn void gm_clif_trap_unwrap(const gm_type *err, const void *v, const char *file, uint32_t line,
                                         uint32_t col) {
    gm_buf b = {0};
    gm_buf_push(&b, "unwrap of error value ", 22);
    gm_format(&b, err, v, false);
    gm_trap_bytes(b.data, b.len, file, line, col);
}

/* A failed `assert` with message `msg` (§21.6). */
GM_API _Noreturn void gm_clif_trap_assert(const uint8_t *msg, size_t len, const char *file, uint32_t line,
                                         uint32_t col) {
    static const char prefix[] = "assertion failed";
    size_t n = sizeof prefix - 1;
    gm_buf b = {0};
    if (len == n && memcmp(msg, prefix, n) == 0) {
        gm_buf_push(&b, msg, len);
    } else {
        gm_buf_push(&b, prefix, n);
        gm_buf_push(&b, ": ", 2);
        gm_buf_push(&b, msg, len);
    }
    gm_trap_bytes(b.data, b.len, file, line, col);
}

/* No handler for effect `name` is installed. */
GM_API _Noreturn void gm_clif_trap_unhandled(const char *name, const char *file, uint32_t line, uint32_t col) {
    char m[256];
    snprintf(m, sizeof m, "no handler installed for effect `%s`", name);
    gm_trap(m, file, line, col);
}

/* ---- integers of 65 to 128 bits ----------------------------------------------------------- */

/* v reduced to `bits` bits, sign- or zero-extended. */
static gm_u128 gm_clif_norm(gm_u128 v, uint32_t bits, bool sgn) {
    if (bits >= 128) return v;
    v &= (((gm_u128)1) << bits) - 1;
    if (sgn && ((v >> (bits - 1)) & 1)) v |= ~(gm_u128)0 << bits;
    return v;
}

static bool gm_clif_fits(gm_u128 v, uint32_t bits, bool sgn) { return gm_clif_norm(v, bits, sgn) == v; }

/* The 128-bit operations take and return their integers through pointers, which every
 * target's C ABI passes the same way. */

/* *r = a + b, a - b or a * b (op 0, 1, 2) for an integer type of `bits` bits stored in 16 bytes,
 * wrapped; returns whether the exact result does not fit. */
GM_API bool gm_clif_arith128(uint32_t op, uint32_t sgn, uint32_t bits, const gm_u128 *pa, const gm_u128 *pb,
                             gm_u128 *out) {
    gm_u128 a = *pa, b = *pb;
    bool o;
    if (sgn) {
        gm_i128 r;
        gm_i128 x = (gm_i128)a, y = (gm_i128)b;
        o = op == 0 ? __builtin_add_overflow(x, y, &r) : op == 1 ? __builtin_sub_overflow(x, y, &r)
                                                                 : __builtin_mul_overflow(x, y, &r);
        o = o || !gm_clif_fits((gm_u128)r, bits, true);
        *out = gm_clif_norm((gm_u128)r, bits, true);
        return o;
    }
    gm_u128 r;
    o = op == 0 ? __builtin_add_overflow(a, b, &r) : op == 1 ? __builtin_sub_overflow(a, b, &r)
                                                             : __builtin_mul_overflow(a, b, &r);
    o = o || !gm_clif_fits(r, bits, false);
    *out = gm_clif_norm(r, bits, false);
    return o;
}

/* *r = a / b or a % b (op 0, 1); the divisor is not zero and the quotient fits. */
GM_API void gm_clif_divrem128(uint32_t op, uint32_t sgn, const gm_u128 *pa, const gm_u128 *pb, gm_u128 *out) {
    gm_u128 a = *pa, b = *pb;
    if (sgn)
        *out = (gm_u128)(op == 0 ? (gm_i128)a / (gm_i128)b : (gm_i128)a % (gm_i128)b);
    else
        *out = op == 0 ? a / b : a % b;
}

GM_API double gm_clif_int_to_f64(const gm_u128 *v, uint32_t sgn) { return sgn ? (double)(gm_i128)*v : (double)*v; }
GM_API float gm_clif_int_to_f32(const gm_u128 *v, uint32_t sgn) { return sgn ? (float)(gm_i128)*v : (float)*v; }
/* x is finite and in range of the type. */
GM_API void gm_clif_f64_to_int(double x, uint32_t sgn, gm_u128 *out) {
    *out = sgn ? (gm_u128)(gm_i128)x : (gm_u128)x;
}

/* *r = base ^ n for an integer type of `bits` bits stored in `storage` bytes, as the C back end
 * computes it: repeated squaring, wrapping at the storage width. Returns whether a product that
 * contributes to the result did not fit the storage or the result does not fit the type. */
GM_API bool gm_clif_pow(uint32_t sgn, uint32_t bits, uint32_t storage, const gm_u128 *pbase, uint64_t n,
                        gm_u128 *out) {
    gm_u128 base = *pbase;
    bool o = false;
#define GM_CLIF_POW(T)                                                                                                 \
    {                                                                                                                  \
        T r = 1, b = (T)base;                                                                                          \
        for (;;) {                                                                                                     \
            if ((n & 1) && __builtin_mul_overflow(r, b, &r)) o = true;                                                 \
            n >>= 1;                                                                                                   \
            if (!n) break;                                                                                             \
            if (__builtin_mul_overflow(b, b, &b)) o = true;                                                            \
        }                                                                                                              \
        gm_u128 w = (gm_u128)r;                                                                                        \
        if (!gm_clif_fits(w, bits, sgn)) o = true;                                                                     \
        *out = gm_clif_norm(w, bits, sgn);                                                                             \
        return o;                                                                                                      \
    }
    switch (storage) {
    case 1:
        if (sgn) GM_CLIF_POW(int8_t) else GM_CLIF_POW(uint8_t)
    case 2:
        if (sgn) GM_CLIF_POW(int16_t) else GM_CLIF_POW(uint16_t)
    case 4:
        if (sgn) GM_CLIF_POW(int32_t) else GM_CLIF_POW(uint32_t)
    case 8:
        if (sgn) GM_CLIF_POW(int64_t) else GM_CLIF_POW(uint64_t)
    default:
        if (sgn) GM_CLIF_POW(gm_i128) else GM_CLIF_POW(gm_u128)
    }
#undef GM_CLIF_POW
}

/* *r = a bit operation on the low `bits` bits of x: 0 clz, 1 ctz, 2 popcount, 3 bswap,
 * 4 bitreverse, 5 rotate left by k (modulo bits). */
GM_API void gm_clif_bitop(uint32_t op, const gm_u128 *px, uint32_t bits, uint64_t k, gm_u128 *out) {
    gm_u128 mask = bits >= 128 ? ~(gm_u128)0 : ((((gm_u128)1) << bits) - 1);
    gm_u128 x = *px & mask;
    int n = 0;
    gm_u128 r = 0;
    switch (op) {
    case 0:
        for (int i = (int)bits - 1; i >= 0 && !((x >> i) & 1); i--) n++;
        *out = (gm_u128)n;
        return;
    case 1:
        while (n < (int)bits && !((x >> n) & 1)) n++;
        *out = (gm_u128)n;
        return;
    case 2:
        while (x) {
            n += (int)(x & 1);
            x >>= 1;
        }
        *out = (gm_u128)n;
        return;
    case 3:
        for (uint32_t i = 0; i < bits / 8; i++) {
            r = (r << 8) | (x & 0xff);
            x >>= 8;
        }
        *out = r;
        return;
    case 4:
        for (uint32_t i = 0; i < bits; i++) {
            r = (r << 1) | (x & 1);
            x >>= 1;
        }
        *out = r;
        return;
    default:
        k %= (uint64_t)bits;
        *out = k == 0 ? x : ((x << k) | (x >> ((uint64_t)bits - k))) & mask;
        return;
    }
}

/* A vector load or store (`store` 1) of n lanes with a slice of len elements. */
GM_API _Noreturn void gm_clif_trap_vec(uint32_t store, uint64_t n, uint64_t len, const char *file, uint32_t line,
                                      uint32_t col) {
    char m[128];
    snprintf(m, sizeof m, "vector %s of %llu lanes with a slice of %llu elements", store ? "store" : "load",
             (unsigned long long)n, (unsigned long long)len);
    gm_trap(m, file, line, col);
}

/* `x ** n` overflowed: the message names the operands. */
GM_API _Noreturn void gm_clif_trap_pow(const gm_u128 *base, uint32_t bits, uint32_t sgn, uint64_t n, const char *file,
                                      uint32_t line, uint32_t col) {
    gm_trap_pow((const uint8_t *)base, bits, sgn != 0, (uint32_t)n, file, line, col);
}

/* ---- floats ------------------------------------------------------------------------------- */

GM_API double gm_clif_fmin(double x, double y) {
    if (x != x) return y;
    if (y != y) return x;
    if (x == 0 && y == 0) return __builtin_signbit(x) ? x : y;
    return x < y ? x : y;
}

GM_API double gm_clif_fmax(double x, double y) {
    if (x != x) return y;
    if (y != y) return x;
    if (x == 0 && y == 0) return __builtin_signbit(x) ? y : x;
    return x > y ? x : y;
}

/* f16 values are their bits in the generated code; arithmetic goes through double, which
 * rounds each operation correctly. */
GM_API double gm_clif_f16_to_f64(uint16_t h) { return gm_f16_to_f64(h); }
GM_API uint16_t gm_clif_f64_to_f16(double d) { return gm_f64_to_f16(d); }

/* ---- writes of `@write` to an output effect ------------------------------------------------
 *
 * Performs operation `op` of effect `eff` (named `name`) with the bytes [p, p + n): natively when
 * the handler found is the runtime's (`native` 1: stdout, 2: stderr), else by calling the
 * clause, which takes the `[]const u8` by address. Returns whether an exit is in progress. */

static bool gm_clif_emit(gm_node *cx, uint32_t eff, uint32_t op, uint32_t native, const char *name, const void *p,
                         size_t n, const char *file, uint32_t line, uint32_t col) {
    gm_node *pn = gm_find(cx, eff);
    if (pn == NULL) gm_clif_trap_unhandled(name, file, line, col);
    if (pn->kind == GM_NATIVE && native != 0) {
        if (native == 1) gm_write_out((const uint8_t *)p, n);
        else gm_write_err((const uint8_t *)p, n);
        return false;
    }
    struct {
        const void *p;
        size_t n;
    } s = {p, n};
    ((void (*)(void *, gm_node *, gm_node *, const void *))pn->clauses[op])(pn->env, pn->hctx, pn, &s);
    return gm_unw.kind != 0;
}

GM_API bool gm_clif_write(gm_node *cx, uint32_t eff, uint32_t op, uint32_t native, const char *name, const void *p,
                          size_t n, const char *file, uint32_t line, uint32_t col) {
    return gm_clif_emit(cx, eff, op, native, name, p, n, file, line, col);
}

/* Formats the value of type d at v (with spec s, or as `{}` if s is NULL) and writes it as
 * gm_clif_write does, unless it comes out empty. */
GM_API bool gm_clif_write_fmt(gm_node *cx, uint32_t eff, uint32_t op, uint32_t native, const char *name,
                              const gm_type *d, const void *v, const gm_spec *s, const char *file, uint32_t line,
                              uint32_t col) {
    gm_buf b = {0};
    if (s != NULL) gm_format_spec(&b, d, v, s);
    else gm_format(&b, d, v, true);
    bool r = false;
    if (b.len != 0) r = gm_clif_emit(cx, eff, op, native, name, b.data, b.len, file, line, col);
    gm_buf_free(&b);
    return r;
}

/* Formats the value of type d at v into the buffer b (with spec s, or as `{}` if s is NULL). */
GM_API void gm_clif_format(gm_buf *b, const gm_type *d, const void *v, const gm_spec *s) {
    if (s != NULL) gm_format_spec(b, d, v, s);
    else gm_format(b, d, v, true);
}

/* ---- memory ------------------------------------------------------------------------------ */

GM_API uint64_t gm_clif_vload(const void *p, uint32_t size) {
    switch (size) {
    case 1: return *(const volatile uint8_t *)p;
    case 2: return *(const volatile uint16_t *)p;
    case 4: return *(const volatile uint32_t *)p;
    default: return *(const volatile uint64_t *)p;
    }
}

GM_API void gm_clif_vstore(void *p, uint32_t size, uint64_t v) {
    switch (size) {
    case 1: *(volatile uint8_t *)p = (uint8_t)v; break;
    case 2: *(volatile uint16_t *)p = (uint16_t)v; break;
    case 4: *(volatile uint32_t *)p = (uint32_t)v; break;
    default: *(volatile uint64_t *)p = v; break;
    }
}

/* `@black_box`: the value at p may have been read and changed. */
GM_API void gm_clif_black_box(void *p) { __asm__ volatile("" : : "r"(p) : "memory"); }

/* ---- thread-local state ------------------------------------------------------------------
 *
 * The generated code reaches the runtime's thread-local variables through their addresses,
 * which each function asks for once (a fiber never moves to another thread). */

GM_API gm_unwind *gm_clif_unw(void) { return &gm_unw; }
GM_API gm_bt **gm_clif_bt_top(void) { return &gm_bt_top; }
GM_API uintptr_t *gm_clif_stack_limit(void) { return &gm_stack_limit; }

/* Fills a dead frame in Debug builds (§18.3). */
GM_API void gm_clif_poison(void *p, int c, size_t n) { gm_poison(p, c, n); }

/* The continuation of a fiber made by `@fiber`, stored at out. */
GM_API void gm_clif_fiber_cont(gm_with *w, gm_fiber *fb, gm_cont *out) { *out = gm_fiber_cont(w, fb); }

/* The C library's memory functions under names of their own, so that a program declaring
 * them as foreign functions with other parameter types does not clash with these uses. */
GM_API void *gm_clif_memmove(void *d, const void *s, size_t n) { return memmove(d, s, n); }
GM_API void *gm_clif_memset(void *d, int c, size_t n) { return memset(d, c, n); }
GM_API size_t gm_clif_strlen(const char *s) { return strlen(s); }

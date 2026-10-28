/* Tests of the C back end's runtime. Built and run by tests/runtime.rs:
 *
 *     cc -std=gnu11 -O1 -pthread -Wall -Werror -Wno-unused-function -I runtime runtime/rt_test.c
 *
 * With no arguments it runs every check and exits non-zero if any failed. With a case name
 * (`trap`, `trap-nofile`, `double-free`, `invalid-free`, `wrong-size`, `args`) it runs that one
 * scenario, which (except `args`, which only starts up with its arguments) is expected to end
 * the process, so the test runner can check the exit status and the output.
 */
#include "gm_rt.h"
#include "gm_rt.c"

static int failures;

#define FAIL(...) do { fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); \
                       fputc('\n', stderr); failures++; } while (0)

/* Checks that the value formats as `expect`. */
static void check_fmt_n(const gm_type *t, const void *v, bool top, const char *expect, size_t n) {
    gm_buf b = {0};
    gm_format(&b, t, v, top);
    if (b.len != n || (n > 0 && memcmp(b.data, expect, n) != 0)) {
        FAIL("expected [%s], got [%.*s]", expect, (int)b.len, b.data ? (const char *)b.data : "");
    }
    gm_buf_free(&b);
}

static void check_fmt(const gm_type *t, const void *v, bool top, const char *expect) {
    check_fmt_n(t, v, top, expect, strlen(expect));
}

#define CHECK(cond) do { if (!(cond)) FAIL("check failed: %s", #cond); } while (0)

/* ---- descriptors --------------------------------------------------------------- */

#define INT_TY(name, sz, nbits, sgn) \
    static const gm_type name = {.kind = GM_K_INT, .size = (sz), .align = (sz), .bits = (nbits), .is_signed = (sgn)}
INT_TY(ty_u8, 1, 8, false);
INT_TY(ty_i8, 1, 8, true);
INT_TY(ty_i7, 1, 7, true);
INT_TY(ty_u7, 1, 7, false);
INT_TY(ty_u16, 2, 16, false);
INT_TY(ty_i16, 2, 16, true);
INT_TY(ty_i24, 4, 24, true);
INT_TY(ty_u32, 4, 32, false);
INT_TY(ty_i32, 4, 32, true);
INT_TY(ty_u64, 8, 64, false);
INT_TY(ty_i64, 8, 64, true);
INT_TY(ty_u128, 16, 128, false);
INT_TY(ty_i128, 16, 128, true);

#define FLOAT_TY(name, sz, nbits) \
    static const gm_type name = {.kind = GM_K_FLOAT, .size = (sz), .align = (sz), .bits = (nbits)}
FLOAT_TY(ty_f16, 2, 16);
FLOAT_TY(ty_f32, 4, 32);
FLOAT_TY(ty_f64, 8, 64);

static const gm_type ty_bool = {.kind = GM_K_BOOL, .size = 1, .align = 1};
static const gm_type ty_ptr = {.kind = GM_K_PTR, .size = sizeof(void *), .align = sizeof(void *)};

static gm_type slice_of(const gm_type *elem) {
    gm_type t = {.kind = GM_K_SLICE, .size = 2 * sizeof(void *), .align = sizeof(void *), .elem = elem};
    return t;
}

typedef struct { const void *ptr; uintptr_t len; } slice_blob;

static void check_int(const gm_type *t, const void *v, const char *expect) { check_fmt(t, v, true, expect); }

static void test_ints(void) {
    uint8_t u8 = 255, i8 = 0x80, z = 0;
    check_int(&ty_u8, &u8, "255");
    check_int(&ty_i8, &i8, "-128");
    check_int(&ty_i8, &z, "0");

    /* i7 / u7 live in one byte; only the low 7 bits count. */
    uint8_t i7a = 0x7f, i7b = 0x40, i7c = 0x3f;
    check_int(&ty_i7, &i7a, "-1");
    check_int(&ty_i7, &i7b, "-64");
    check_int(&ty_i7, &i7c, "63");
    check_int(&ty_u7, &i7a, "127");
    uint8_t u7hi = 0xff; /* stray high bit is ignored */
    check_int(&ty_u7, &u7hi, "127");

    uint16_t u16 = 65535;
    int16_t i16 = -32768;
    check_int(&ty_u16, &u16, "65535");
    check_int(&ty_i16, &i16, "-32768");

    /* A 24-bit signed integer stored in four bytes. */
    int32_t i24 = -8388608;
    check_int(&ty_i24, &i24, "-8388608");
    i24 = 8388607;
    check_int(&ty_i24, &i24, "8388607");

    uint32_t u32 = 4294967295u;
    int32_t i32 = -2147483647 - 1;
    check_int(&ty_u32, &u32, "4294967295");
    check_int(&ty_i32, &i32, "-2147483648");

    uint64_t u64 = UINT64_MAX;
    int64_t i64 = INT64_MIN;
    check_int(&ty_u64, &u64, "18446744073709551615");
    check_int(&ty_i64, &i64, "-9223372036854775808");

    unsigned __int128 u128 = ~(unsigned __int128)0;
    __int128 i128 = (__int128)(((unsigned __int128)1) << 127);
    check_int(&ty_u128, &u128, "340282366920938463463374607431768211455");
    check_int(&ty_i128, &i128, "-170141183460469231731687303715884105728");
    i128 = (__int128)(~(unsigned __int128)0 >> 1);
    check_int(&ty_i128, &i128, "170141183460469231731687303715884105727");
    i128 = -1;
    check_int(&ty_i128, &i128, "-1");
    i128 = 0;
    check_int(&ty_i128, &i128, "0");
    i128 = -1000000000000000000;
    i128 *= 1000000000000000000;
    check_int(&ty_i128, &i128, "-1000000000000000000000000000000000000");
}

static void test_floats(void) {
    double d;
    float f;
    uint16_t h;
#define D(x, s) do { d = (x); check_fmt(&ty_f64, &d, true, s); } while (0)
    D(0.1, "0.1");
    D(1e21, "1e21");
    D(1e20, "100000000000000000000");
    D(2.5e-7, "2.5e-7");
    D(1e-6, "0.000001");
    D(1e-7, "1e-7");
    D(-0.0, "-0");
    D(0.0, "0");
    D(1.5, "1.5");
    D(-2.0, "-2");
    D(100.0, "100");
    D(0.001, "0.001");
    D(123456.789, "123456.789");
    D(1.2345e25, "1.2345e25");
    D(-1.5e-10, "-1.5e-10");
    D(1.0 / 3.0, "0.3333333333333333");
    D(2.0 / 3.0, "0.6666666666666666");
    D(1.7976931348623157e308, "1.7976931348623157e308");
    D(5e-324, "5e-324");
    D((double)0.1f, "0.10000000149011612");
    D(123456789012345680000.0, "123456789012345680000");
    D(1e23, "1e23");
    D(9007199254740993.0, "9007199254740992");
    D(4.35, "4.35");
    D(9.5367431640625e-7, "9.5367431640625e-7"); /* 2^-20: the interval below is narrower */
    D(1.0 / 1024.0 / 1024.0 / 1024.0 / 1024.0 / 1024.0 / 1024.0, "8.673617379884035e-19");
    D(2.2250738585072014e-308, "2.2250738585072014e-308");
    D(2.225073858507201e-308, "2.225073858507201e-308"); /* the largest subnormal */
    D(1e-323, "1e-323");
    D(1.5e-323, "1.5e-323");
    /* Exact ties between two shortest candidates go to the even digit. */
    D(1618551636065423.25, "1618551636065423.2");
    D(1840508283716554.75, "1840508283716554.8");
    D(2156270916921129.25, "2156270916921129.2");
    D(625246290564228.75, "625246290564228.8");
    D(10433110499478.1875, "10433110499478.188");
    D(__builtin_nan(""), "nan");
    D(__builtin_inf(), "inf");
    D(-__builtin_inf(), "-inf");
#undef D
#define F(x, s) do { f = (x); check_fmt(&ty_f32, &f, true, s); } while (0)
    F(0.1f, "0.1");
    F(1.0f / 3.0f, "0.33333334");
    F(1.5f, "1.5");
    F(100.0f, "100");
    F(-0.0f, "-0");
    F(3.4028235e38f, "3.4028235e38");
    F(16777216.0f, "16777216");
    F(1e-10f, "1e-10");
    /* Exact ties between two shortest candidates go to the even digit. */
    F(-3056815.25f, "-3056815.2");
    F(30453.5625f, "30453.562");
    F(__builtin_nanf(""), "nan");
    F(-__builtin_inff(), "-inf");
#undef F
#define H(bits, s) do { h = (bits); check_fmt(&ty_f16, &h, true, s); } while (0)
    H(gm_f64_to_f16(0.1), "0.1");
    H(0x7bff, "65500");  /* 65504, printed with the shortest digits that identify it */
    H(0x3c00, "1");
    H(0xbc00, "-1");
    H(0x3e00, "1.5");
    H(0x0000, "0");
    H(0x8000, "-0");
    H(0x0001, "6e-8");
    H(0x7c00, "inf");
    H(0xfc00, "-inf");
    H(0x7e00, "nan");
    H(gm_f64_to_f16(0.3333), "0.3333");
#undef H
    CHECK(gm_f64_to_f16(65520.0) == 0x7c00);
    CHECK(gm_f64_to_f16(65519.0) == 0x7bff);
    CHECK(gm_f64_to_f16(0.1) == 0x2e66);
    CHECK(gm_f16_to_f64(0x2e66) > 0.0999 && gm_f16_to_f64(0x2e66) < 0.1001);
}

/* The digits gm_shortest returns read back as the value, and no shorter correctly rounded
 * decimal does, over a pseudo-random sample of doubles and floats. */
static void test_float_shortest(void) {
    uint64_t s = 0x9E3779B97F4A7C15ULL;
    for (int i = 0; i < 200000; i++) {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        double x;
        unsigned bits = (i & 1) ? 32 : 64;
        if (bits == 64) {
            uint64_t u = s & 0x7fffffffffffffffULL;
            memcpy(&x, &u, 8);
            if (!(x > 0) || __builtin_isinf(x)) continue;
        } else {
            uint32_t u = (uint32_t)s & 0x7fffffffu;
            float fl;
            memcpy(&fl, &u, 4);
            if (!(fl > 0) || __builtin_isinf(fl)) continue;
            x = (double)fl;
        }
        char digits[24], buf[64];
        int e;
        gm_shortest(x, bits, digits, &e);
        size_t n = strlen(digits);
        snprintf(buf, sizeof buf, "%c.%se%d", digits[0], digits + 1, e);
        if (bits == 64 ? strtod(buf, NULL) != x : strtof(buf, NULL) != (float)x) FAIL("[%s] does not read back as %.17g", buf, x);
        if (n > 1) {
            snprintf(buf, sizeof buf, "%.*e", (int)n - 2, x);
            if (bits == 64 ? strtod(buf, NULL) == x : strtof(buf, NULL) == (float)x) FAIL("[%s] is shorter than [%s] for %.17g", buf, digits, x);
        }
        if (digits[n - 1] == '0') FAIL("trailing zero in [%s]", digits);
    }
}

static void test_bool_ptr(void) {
    uint8_t t = 1, fa = 0, odd = 7;
    check_fmt(&ty_bool, &t, true, "true");
    check_fmt(&ty_bool, &fa, true, "false");
    check_fmt(&ty_bool, &odd, false, "true");
    void *p = NULL;
    check_fmt(&ty_ptr, &p, true, "0x0");
    p = (void *)(uintptr_t)0xdeadbeef;
    check_fmt(&ty_ptr, &p, true, "0xdeadbeef");
    p = (void *)(uintptr_t)0x10;
    check_fmt(&ty_ptr, &p, false, "0x10");
}

static void check_str(const char *bytes, size_t n, const char *top, const char *nested) {
    gm_type st = slice_of(&ty_u8);
    slice_blob s = {bytes, n};
    check_fmt(&st, &s, true, top);
    check_fmt(&st, &s, false, nested);
}

static void test_strings(void) {
    check_str("hello", 5, "hello", "\"hello\"");
    check_str("", 0, "", "\"\"");
    check_str("a\"b\\c\n\r\t", 8, "a\"b\\c\n\r\t", "\"a\\\"b\\\\c\\n\\r\\t\"");
    {
        gm_type st = slice_of(&ty_u8);
        slice_blob s = {"a\0b", 3};
        check_fmt_n(&st, &s, true, "a\0b", 3);
        check_fmt(&st, &s, false, "\"a\\0b\"");
    }
    /* Valid UTF-8 is kept: e-acute, euro sign, an emoji. */
    check_str("\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80", 9, "\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80",
              "\"\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80\"");
    /* Everything else non-printable is a lowercase \xNN escape. */
    check_str("\x01\x7f\xff", 3, "\x01\x7f\xff", "\"\\x01\\x7f\\xff\"");
    check_str("\xe2\x82", 2, "\xe2\x82", "\"\\xe2\\x82\"");               /* truncated */
    check_str("\xc0\x80", 2, "\xc0\x80", "\"\\xc0\\x80\"");               /* overlong */
    check_str("\xed\xa0\x80", 3, "\xed\xa0\x80", "\"\\xed\\xa0\\x80\"");  /* surrogate */
    check_str("\xf4\x90\x80\x80", 4, "\xf4\x90\x80\x80", "\"\\xf4\\x90\\x80\\x80\""); /* > U+10FFFF */
    check_str("\xc3(", 2, "\xc3(", "\"\\xc3(\"");
    check_str("\x80", 1, "\x80", "\"\\x80\"");
    check_str("\xc3\xa9x", 3, "\xc3\xa9x", "\"\xc3\xa9x\"");

    /* A slice of signed bytes is a list of numbers. */
    gm_type is = slice_of(&ty_i8);
    int8_t bytes[] = {1, -2, 3};
    slice_blob s = {bytes, 3};
    check_fmt(&is, &s, true, "[1, -2, 3]");
}

static void test_aggregates(void) {
    int32_t arr[3] = {1, -2, 3};
    gm_type a3 = {.kind = GM_K_ARRAY, .size = 12, .align = 4, .elem = &ty_i32, .len = 3};
    gm_type a0 = {.kind = GM_K_ARRAY, .size = 0, .align = 4, .elem = &ty_i32, .len = 0};
    check_fmt(&a3, arr, true, "[1, -2, 3]");
    check_fmt(&a0, arr, true, "[]");

    gm_type s32 = slice_of(&ty_i32);
    slice_blob sb = {arr, 3};
    check_fmt(&s32, &sb, true, "[1, -2, 3]");
    slice_blob empty = {NULL, 0};
    check_fmt(&s32, &empty, true, "[]");

    /* Strings nested in an array are quoted. */
    gm_type str = slice_of(&ty_u8);
    slice_blob strs[2] = {{"ab", 2}, {"c\n", 2}};
    gm_type as = {.kind = GM_K_ARRAY, .size = sizeof strs, .align = sizeof(void *), .elem = &str, .len = 2};
    check_fmt(&as, strs, true, "[\"ab\", \"c\\n\"]");

    /* Arrays of arrays. */
    int32_t grid[2][2] = {{1, 2}, {3, 4}};
    gm_type row = {.kind = GM_K_ARRAY, .size = 8, .align = 4, .elem = &ty_i32, .len = 2};
    gm_type g = {.kind = GM_K_ARRAY, .size = 16, .align = 4, .elem = &row, .len = 2};
    check_fmt(&g, grid, true, "[[1, 2], [3, 4]]");

    /* Tuples. */
    struct { int32_t a; uint8_t b; } tv = {7, 1};
    gm_field tf[] = {{NULL, 0, &ty_i32}, {NULL, 4, &ty_bool}};
    gm_type tup = {.kind = GM_K_TUPLE, .size = 8, .align = 4, .nfields = 2, .fields = tf};
    check_fmt(&tup, &tv, true, "(7, true)");
    gm_field nf[] = {{"x", 0, &ty_i32}, {"y", 4, &ty_bool}};
    gm_type named = {.kind = GM_K_TUPLE, .size = 8, .align = 4, .nfields = 2, .fields = nf, .named = true};
    check_fmt(&named, &tv, true, "(x: 7, y: true)");
    gm_type one = {.kind = GM_K_TUPLE, .size = 4, .align = 4, .nfields = 1, .fields = tf};
    check_fmt(&one, &tv, true, "(7,)");
    gm_field onef[] = {{"x", 0, &ty_i32}};
    gm_type one_named = {.kind = GM_K_TUPLE, .size = 4, .align = 4, .nfields = 1, .fields = onef, .named = true};
    check_fmt(&one_named, &tv, true, "(x: 7)");
    gm_type unit = {.kind = GM_K_TUPLE, .size = 0, .align = 1};
    check_fmt(&unit, &tv, true, "()");
    /* Tuples nest. */
    struct { struct { int32_t a; uint8_t b; } t; slice_blob s; } ov = {{7, 0}, {"q", 1}};
    gm_field outer_f[] = {{NULL, 0, &tup}, {NULL, offsetof(__typeof__(ov), s), &str}};
    gm_type outer = {.kind = GM_K_TUPLE, .size = sizeof ov, .align = sizeof(void *), .nfields = 2, .fields = outer_f};
    check_fmt(&outer, &ov, true, "((7, false), \"q\")");

    /* Opaque values print their name. */
    gm_type op = {.kind = GM_K_OPAQUE, .size = 8, .align = 8, .name = "<handler>"};
    check_fmt(&op, arr, true, "<handler>");
}

static void test_optional_result(void) {
    /* Niche: a pointer payload, null means absent. */
    gm_type optp = {.kind = GM_K_OPTIONAL, .size = sizeof(void *), .align = sizeof(void *), .elem = &ty_ptr,
                    .flag_offset = GM_NICHE};
    void *p = NULL;
    check_fmt(&optp, &p, true, "null");
    p = (void *)(uintptr_t)0x2a;
    check_fmt(&optp, &p, true, "0x2a");

    /* Flag byte after the payload. */
    gm_type opti = {.kind = GM_K_OPTIONAL, .size = 8, .align = 4, .elem = &ty_i32, .flag_offset = 4};
    struct { int32_t v; uint8_t flag; } ov = {5, 1};
    check_fmt(&opti, &ov, true, "5");
    ov.flag = 0;
    check_fmt(&opti, &ov, true, "null");
    ov.v = 0;
    ov.flag = 1;
    check_fmt(&opti, &ov, false, "0");

    /* An optional string is quoted, being nested. */
    gm_type str = slice_of(&ty_u8);
    gm_type opts = {.kind = GM_K_OPTIONAL, .size = 24, .align = 8, .elem = &str, .flag_offset = 16};
    struct { slice_blob s; uint8_t flag; } sv = {{"hi", 2}, 1};
    check_fmt(&opts, &sv, true, "\"hi\"");

    /* Optional of optional. */
    gm_type oo = {.kind = GM_K_OPTIONAL, .size = 12, .align = 4, .elem = &opti, .flag_offset = 8};
    struct { int32_t v; uint8_t f; uint8_t pad[3]; uint8_t outer; } oov = {3, 1, {0}, 1};
    check_fmt(&oo, &oov, true, "3");
    oov.f = 0;
    check_fmt(&oo, &oov, true, "null");
    oov.outer = 0;
    check_fmt(&oo, &oov, true, "null");

    /* Results: tag byte, payload at offset 8. */
    gm_type res = {.kind = GM_K_RESULT, .size = 24, .align = 8, .elem = &ty_i32, .err = &str, .tag_offset = 0,
                   .payload_offset = 8};
    struct { uint8_t tag; uint8_t pad[7]; union { int32_t i; slice_blob s; } u; } rv;
    memset(&rv, 0, sizeof rv);
    rv.u.i = 42;
    check_fmt(&res, &rv, true, "42");
    rv.tag = 1;
    rv.u.s = (slice_blob){"bad", 3};
    check_fmt(&res, &rv, true, "!\"bad\"");
    gm_type resi = {.kind = GM_K_RESULT, .size = 8, .align = 4, .elem = &ty_i32, .err = &ty_i32, .tag_offset = 0,
                    .payload_offset = 4};
    struct { uint8_t tag; uint8_t pad[3]; int32_t v; } iv = {1, {0}, -9};
    check_fmt(&resi, &iv, true, "!-9");
    iv.tag = 0;
    check_fmt(&resi, &iv, true, "-9");
}

static void test_structs_unions(void) {
    gm_field sf[] = {{"x", 0, &ty_i32}, {"_", 4, &ty_i32}, {"ok", 8, &ty_bool}};
    gm_type st = {.kind = GM_K_STRUCT, .size = 12, .align = 4, .name = "Point", .nfields = 3, .fields = sf};
    struct { int32_t x, pad; uint8_t ok; } sv = {1, 99, 1};
    check_fmt(&st, &sv, true, "Point(x: 1, ok: true)");

    gm_field only_pad[] = {{"_", 0, &ty_i32}};
    gm_type pt = {.kind = GM_K_STRUCT, .size = 4, .align = 4, .name = "Pad", .nfields = 1, .fields = only_pad};
    check_fmt(&pt, &sv, true, "Pad()");
    gm_type nofields = {.kind = GM_K_STRUCT, .size = 0, .align = 1, .name = "Empty"};
    check_fmt(&nofields, &sv, true, "Empty()");

    /* A struct holding a string and a nested struct. */
    gm_type str = slice_of(&ty_u8);
    gm_field wf[] = {{"name", 0, &str}, {"at", 16, &st}};
    gm_type wt = {.kind = GM_K_STRUCT, .size = 32, .align = 8, .name = "Who", .nfields = 2, .fields = wf};
    struct { slice_blob name; struct { int32_t x, pad; uint8_t ok; } at; } wv = {{"bo", 2}, {2, 0, 0}};
    check_fmt(&wt, &wv, true, "Who(name: \"bo\", at: Point(x: 2, ok: false))");

    /* Unions: a one-byte signed tag at offset 0, payload at offset 4. */
    gm_field rect_f[] = {{"w", 0, &ty_i32}, {"h", 4, &ty_i32}};
    gm_type rect = {.kind = GM_K_TUPLE, .size = 8, .align = 4, .nfields = 2, .fields = rect_f, .named = true};
    gm_field pair_f[] = {{NULL, 0, &ty_i32}, {NULL, 4, &ty_i32}};
    gm_type pair = {.kind = GM_K_TUPLE, .size = 8, .align = 4, .nfields = 2, .fields = pair_f};
    gm_field one_f[] = {{NULL, 0, &ty_i32}};
    gm_type one = {.kind = GM_K_TUPLE, .size = 4, .align = 4, .nfields = 1, .fields = one_f};
    gm_variant vs[] = {
        {"None", -1, NULL},
        {"Num", 5, &ty_i32},
        {"Rect", 7, &rect},
        {"Pair", 8, &pair},
        {"Name", 9, &str},
        {"One", 10, &one},
    };
    gm_type un = {.kind = GM_K_UNION, .size = 24, .align = 8, .name = "Shape", .tag_offset = 0, .payload_offset = 8,
                  .tag_size = 1, .tag_signed = true, .nvariants = 6, .variants = vs};
    struct { int8_t tag; uint8_t pad[7]; union { int32_t i; int32_t two[2]; slice_blob s; } u; } uv;
    memset(&uv, 0, sizeof uv);
    uv.tag = -1;
    check_fmt(&un, &uv, true, ".None");
    uv.tag = 5;
    uv.u.i = -3;
    check_fmt(&un, &uv, true, ".Num(-3)");
    uv.tag = 7;
    uv.u.two[0] = 2;
    uv.u.two[1] = 3;
    check_fmt(&un, &uv, true, ".Rect(w: 2, h: 3)");
    uv.tag = 8;
    check_fmt(&un, &uv, true, ".Pair(2, 3)");
    uv.tag = 10;
    check_fmt(&un, &uv, true, ".One(2)");
    uv.tag = 9;
    uv.u.s = (slice_blob){"zed", 3};
    check_fmt(&un, &uv, true, ".Name(\"zed\")");
    uv.tag = 6;
    check_fmt(&un, &uv, true, ".?");

    /* An unsigned two-byte tag, and a union inside a list. */
    gm_variant ws[] = {{"A", 0, NULL}, {"B", 300, NULL}, {"C", 65535, NULL}};
    gm_type wn = {.kind = GM_K_UNION, .size = 2, .align = 2, .name = "W", .tag_offset = 0, .tag_size = 2,
                  .tag_signed = false, .nvariants = 3, .variants = ws};
    uint16_t tags[3] = {0, 300, 65535};
    gm_type wa = {.kind = GM_K_ARRAY, .size = 6, .align = 2, .elem = &wn, .len = 3};
    check_fmt(&wa, tags, true, "[.A, .B, .C]");
    /* The same bits read with a signed tag are -1. */
    gm_variant ns[] = {{"Minus", -1, NULL}};
    gm_type nn = {.kind = GM_K_UNION, .size = 2, .align = 2, .name = "N", .tag_size = 2, .tag_signed = true,
                  .nvariants = 1, .variants = ns};
    check_fmt(&nn, &tags[2], true, ".Minus");
}

static void test_buf(void) {
    gm_buf b = {0};
    for (int i = 0; i < 10000; i++) gm_buf_push(&b, "ab", 2);
    CHECK(b.len == 20000);
    CHECK(memcmp(b.data, "abab", 4) == 0 && memcmp(b.data + 19996, "abab", 4) == 0);
    gm_buf_push(&b, NULL, 0);
    CHECK(b.len == 20000);
    gm_buf_free(&b);
    CHECK(b.data == NULL && b.len == 0 && b.cap == 0);
    gm_buf_push(&b, "x", 1);
    CHECK(b.len == 1 && b.data[0] == 'x');
    gm_buf_free(&b);
}

static void test_heap(void) {
    CHECK(gm_debug != 0);
    uint8_t *p = (uint8_t *)gm_heap_alloc(10, 1);
    CHECK(p != NULL);
    for (int i = 0; i < 10; i++) {
        if (p[i] != 0xAA) FAIL("fresh heap byte %d is %02x, not aa", i, p[i]);
    }
    memset(p, 1, 10);
    gm_heap_free(p, 10, 1, "t.gm", 1, 1);

    /* Alignments up to a page, and many live blocks (which grows the block table). */
    enum { N = 2000 };
    static void *blocks[N];
    for (int i = 0; i < N; i++) {
        size_t align = (size_t)1 << (i % 13);
        size_t size = (size_t)(i % 40);
        blocks[i] = gm_heap_alloc(size, align);
        CHECK(blocks[i] != NULL);
        CHECK(((uintptr_t)blocks[i] & (align - 1)) == 0);
        memset(blocks[i], 0x5a, size);
    }
    for (int i = 0; i < N; i++) {
        for (size_t j = 0; j < (size_t)(i % 40); j++) CHECK(((uint8_t *)blocks[i])[j] == 0x5a);
    }
    for (int i = 0; i < N; i++) gm_heap_free(blocks[i], (size_t)(i % 40), (size_t)1 << (i % 13), NULL, 0, 0);

    /* Zero-sized blocks are distinct non-null pointers. */
    void *z1 = gm_heap_alloc(0, 8), *z2 = gm_heap_alloc(0, 8);
    CHECK(z1 != NULL && z2 != NULL && z1 != z2);
    gm_heap_free(z1, 0, 8, NULL, 0, 0);
    gm_heap_free(z2, 0, 8, NULL, 0, 0);

    /* Freed addresses get reused and are live again afterwards. */
    for (int i = 0; i < 100; i++) {
        void *q = gm_heap_alloc(32, 16);
        CHECK(q != NULL);
        gm_heap_free(q, 32, 16, NULL, 0, 0);
    }

    /* Outside debug mode nothing is tracked or filled. */
    gm_debug = 0;
    uint8_t *r = (uint8_t *)gm_heap_alloc(8, 8);
    CHECK(r != NULL);
    gm_heap_free(r, 8, 8, NULL, 0, 0);
    gm_heap_free(NULL, 0, 1, NULL, 0, 0);
    gm_debug = 1;
}

static int saved_argc;
static char **saved_argv;

static void test_env_args(void) {
    const uint8_t *v = NULL;
    size_t n = 0;
    CHECK(!gm_env_var((const uint8_t *)"GM_RT_SURELY_UNSET_VAR", 22, &v, &n));
    /* The name is given by length: the trailing "X" is not part of it. */
    const char *real = getenv("PATH");
    if (real != NULL) {
        CHECK(gm_env_var((const uint8_t *)"PATHX", 4, &v, &n));
        CHECK(n == strlen(real) && memcmp(v, real, n) == 0);
    }

    size_t count = 0;
    const gm_rt_str *a = (const gm_rt_str *)gm_args(&count);
    CHECK(count == (size_t)saved_argc);
    for (size_t i = 0; i < count && i < (size_t)saved_argc; i++) {
        CHECK(a[i].len == strlen(saved_argv[i]) && memcmp(a[i].ptr, saved_argv[i], a[i].len) == 0);
    }
}

/* ---- scenarios that end the process ------------------------------------------------ */

static int write_entry(gm_node *cx) {
    (void)cx;
    gm_write_out((const uint8_t *)"hello\n", 6);
    return 0;
}

static int scenario(const char *name) {
    if (strcmp(name, "trap") == 0) {
        gm_write_out((const uint8_t *)"before\n", 7);
        gm_write_err((const uint8_t *)"err\n", 4);
        gm_trap("boom", "f.gm", 3, 4);
    } else if (strcmp(name, "trap-nofile") == 0) {
        gm_trap_bytes((const uint8_t *)"bytes!", 6, NULL, 0, 0);
    } else if (strcmp(name, "double-free") == 0) {
        void *p = gm_heap_alloc(16, 8);
        gm_heap_free(p, 16, 8, "d.gm", 5, 6);
        gm_heap_free(p, 16, 8, "d.gm", 7, 8);
    } else if (strcmp(name, "invalid-free") == 0) {
        int x;
        gm_heap_free(&x, 4, 4, "i.gm", 1, 2);
    } else if (strcmp(name, "wrong-size") == 0) {
        void *p = gm_heap_alloc(16, 8);
        gm_heap_free(p, 8, 8, "w.gm", 1, 2);
    } else if (strcmp(name, "write-failure") == 0) {
        return gm_run_main(write_entry, NULL);
    } else if (strcmp(name, "args") == 0) {
        return 0;
    } else {
        fprintf(stderr, "unknown scenario %s\n", name);
        return 2;
    }
    return 0;
}

/* Integers are addressed by significance, whatever the byte order. */
static void test_byte_order(void) {
    uint32_t w32 = 0x01020304u;
    CHECK(*gm_ibp(&w32, 4, 0) == 4 && *gm_ibp(&w32, 4, 3) == 1);
    CHECK(gm_load_u128(&w32, 4) == 0x01020304u);
    uint16_t w16 = 0;
    gm_store_u128(&w16, 2, 0xbeef);
    CHECK(w16 == 0xbeef);
    unsigned __int128 w128 = ((unsigned __int128)0x1122334455667788ULL << 64) | 0x99aabbccddeeff00ULL;
    CHECK(gm_load_u128(&w128, 16) == w128 && *gm_ibp(&w128, 16, 15) == 0x11 && *gm_ibp(&w128, 16, 0) == 0x00);
    /* A #bits struct is an integer of its storage size. */
    static const gm_field bf[] = {{"lo", 0, &ty_u8}, {"hi", 8, &ty_u8}};
    static const gm_type bits16 = {.kind = GM_K_BITS, .size = 2, .align = 2, .name = "B", .nfields = 2, .fields = bf};
    uint16_t b = 0x0a0b;
    check_fmt(&bits16, &b, true, "B(lo: 11, hi: 10)");
    /* Tag formatting sees the same layout. */
    uint8_t bytes32[4] = {0};
    gm_store_u128(bytes32, 4, 305419896);
    check_int(&ty_u32, bytes32, "305419896");
}

/* The Windows command line made from an argument vector, with the C runtime's quoting rules. */
static void check_cmdline(const char *const *args, size_t n, const char *expect) {
    gm_rt_str v[8];
    for (size_t i = 0; i < n; i++) {
        v[i].ptr = (const uint8_t *)args[i];
        v[i].len = strlen(args[i]);
    }
    size_t len;
    char *s = gm_win_cmdline(v, n, &len);
    if (len != strlen(expect) || strcmp(s, expect) != 0) FAIL("command line: expected [%s], got [%s]", expect, s);
    free(s);
}

static void test_cmdline(void) {
    const char *plain[] = {"prog", "-x", "a.txt"};
    check_cmdline(plain, 3, "prog -x a.txt");
    const char *spaces[] = {"my prog", "two words", "tab\there"};
    check_cmdline(spaces, 3, "\"my prog\" \"two words\" \"tab\there\"");
    const char *empty[] = {"p", "", "q"};
    check_cmdline(empty, 3, "p \"\" q");
    const char *quotes[] = {"say \"hi\"", "a\"b"};
    check_cmdline(quotes, 2, "\"say \\\"hi\\\"\" \"a\\\"b\"");
    /* Backslashes are literal unless they precede a quote (or the closing quote). */
    const char *slashes[] = {"C:\\dir\\file", "C:\\my dir\\", "a\\\\\"b", "x\\"};
    check_cmdline(slashes, 4, "C:\\dir\\file \"C:\\my dir\\\\\" \"a\\\\\\\\\\\"b\" x\\");
    const char *one[] = {"a"};
    check_cmdline(one, 0, "");
    check_cmdline(one, 1, "a");
}

/* Handles of directories, child processes and threads are not reused: a handle that has been
 * closed fails, and does not reach a newer object that took over its table slot. */
static void noop_thread(void *arg) { (void)arg; }

static void test_stale_handles(void) {
    int64_t ebadf = gm_io_error(EBADF);

    int64_t d1 = gm_fs_open_dir(".", 1);
    CHECK(d1 > 0);
    CHECK(gm_fs_close_dir(d1) == 0);
    CHECK(gm_fs_close_dir(d1) == ebadf);
    char name[8];
    CHECK(gm_fs_next_entry(d1, name, sizeof name) == ebadf);
    int64_t d2 = gm_fs_open_dir(".", 1);
    CHECK(d2 > 0 && d2 != d1);
    CHECK(gm_fs_close_dir(d1) == ebadf);
    CHECK(gm_fs_close_dir(d2) == 0);
    CHECK(gm_fs_close_dir(0) == ebadf && gm_fs_close_dir(-1) == ebadf && gm_fs_close_dir((int64_t)1 << 40) == ebadf);
    CHECK(gm_fs_open_dir("", 0) < 0);

    int64_t t1 = gm_thread_spawn((void *)noop_thread, NULL);
    CHECK(t1 > 0);
    CHECK(gm_thread_join(t1) == 0);
    CHECK(gm_thread_join(t1) == ebadf);
    int64_t t2 = gm_thread_spawn((void *)noop_thread, NULL);
    CHECK(t2 > 0 && t2 != t1);
    CHECK(gm_thread_join(t1) == ebadf);
    CHECK(gm_thread_detach(t1) == ebadf);
    CHECK(gm_thread_join(t2) == 0);
    CHECK(gm_thread_join(12345) == ebadf);

#ifndef _WIN32
    gm_rt_str argv[1] = {{(const uint8_t *)"true", 4}};
    uint32_t null_streams = 1 | (1 << 2) | (1 << 4);
    int64_t p1 = gm_proc_launch(argv, 1, NULL, 0, "", 0, NULL, 0, null_streams);
    CHECK(p1 > 0);
    CHECK(gm_proc_wait(p1) == 0);
    CHECK(gm_proc_wait(p1) == ebadf);
    int64_t p2 = gm_proc_launch(argv, 1, NULL, 0, "", 0, NULL, 0, null_streams);
    CHECK(p2 > 0 && p2 != p1);
    CHECK(gm_proc_wait(p1) == ebadf);
    CHECK(gm_proc_kill(p1) == ebadf);
    CHECK(gm_proc_close_in(p1) == ebadf);
    CHECK(gm_proc_wait(p2) == 0);
    CHECK(gm_proc_wait(p2) == ebadf);
    CHECK(gm_proc_try_wait(p2) == ebadf);

    /* try_wait: 2^33 while the child runs, then its status, once. */
    gm_rt_str sleeper[2] = {{(const uint8_t *)"sleep", 5}, {(const uint8_t *)"5", 1}};
    int64_t p3 = gm_proc_launch(sleeper, 2, NULL, 0, "", 0, NULL, 0, null_streams);
    CHECK(p3 > 0);
    CHECK(gm_proc_try_wait(p3) == ((int64_t)1 << 33));
    CHECK(gm_proc_kill(p3) == 0);
    int64_t st = gm_proc_try_wait(p3);
    while (st == ((int64_t)1 << 33)) st = gm_proc_try_wait(p3);
    CHECK(st == 9 + ((int64_t)1 << 32));
    CHECK(gm_proc_try_wait(p3) == ebadf);

    /* program: the file to run, apart from the name the child sees. */
    gm_rt_str named[1] = {{(const uint8_t *)"no-such-name", 12}};
    int64_t p4 = gm_proc_launch(named, 1, NULL, 0, "", 0, "true", 4, null_streams | GM_PROC_PROGRAM);
    CHECK(p4 > 0 && gm_proc_wait(p4) == 0);

    char link_buf[16];
    CHECK(gm_fs_canonical("/", 1, link_buf, sizeof link_buf) == 1 && link_buf[0] == '/');
    CHECK(gm_fs_canonical("/no/such/path", 13, link_buf, sizeof link_buf) < 0);
    CHECK(gm_fs_read_link("/", 1, link_buf, sizeof link_buf) < 0);
#endif
}

int main(int argc, char **argv) {
#ifdef _WIN32
    _setmode(1, _O_BINARY);   /* as gm_run_main does: no newline translation */
    _setmode(2, _O_BINARY);
#endif
    gm_set_args(argc, argv);
    saved_argc = argc;
    saved_argv = argv;
    if (argc > 1) return scenario(argv[1]);
    test_ints();
    test_byte_order();
    test_floats();
    test_float_shortest();
    test_bool_ptr();
    test_strings();
    test_aggregates();
    test_optional_result();
    test_structs_unions();
    test_buf();
    test_heap();
    test_env_args();
    test_cmdline();
    test_stale_handles();
    gm_flush();
    if (failures != 0) {
        fprintf(stderr, "%d runtime check(s) failed\n", failures);
        return 1;
    }
    printf("runtime ok\n");
    return 0;
}

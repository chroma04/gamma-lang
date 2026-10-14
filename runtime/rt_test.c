/* Tests of the C back end's runtime. Built and run by tests/runtime.rs:
 *
 *     cc -std=gnu11 -O1 -pthread -Wall -Werror -Wno-unused-function -I runtime runtime/rt_test.c
 *
 * With no arguments it runs every check and exits non-zero if any failed. With a case name
 * (`trap`, `double-free`, `invalid-free`, `wrong-size`, `args`) it runs that one scenario,
 * which is expected to end the process, so the test runner can check the exit status and the
 * output.
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

/* ---- integers wider than 128 bits ------------------------------------------------ */

static uint64_t rng_state = 0x9e3779b97f4a7c15ULL;

static uint64_t rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

typedef __int128 i128;
typedef unsigned __int128 u128;

/* Byte i, counting from the least significant, of an integer in the layout of the wide
 * routines (64-bit words, least significant first) or, if `nat`, of an integer of `size`
 * bytes as the target stores it (the layout of gm_wconv's operands). */
static uint8_t *w_byte(uint8_t *buf, size_t size, bool nat, size_t i) { return nat ? gm_ibp(buf, size, i) : gm_wbp(buf, i); }

/* Writes the value v (sign- or zero-extended from `bits`) to the `size` bytes at buf, with
 * random padding above `bits` if junk. */
static void w_pack(uint8_t *buf, size_t size, bool nat, uint32_t bits, i128 v, bool junk) {
    for (size_t i = 0; i < size; i++) *w_byte(buf, size, nat, i) = i < 16 ? (uint8_t)((u128)v >> (8 * i)) : (v < 0 ? 0xff : 0);
    if (junk && bits < size * 8) {
        for (uint32_t i = bits; i < size * 8; i++) {
            if (rng() & 1) *w_byte(buf, size, nat, i / 8) ^= (uint8_t)(1u << (i % 8));
        }
    }
}

/* The expected bytes of v in a type of `bits` bits: sign- or zero-extended to the storage. */
static bool w_canonical(const uint8_t *buf, uint32_t bits, bool sgn, i128 v) {
    size_t size = GM_WBYTES(bits);
    for (size_t i = 0; i < size; i++) {
        uint8_t want = i < 16 ? (uint8_t)((u128)v >> (8 * i)) : (v < 0 ? 0xff : 0);
        if (*gm_wbp(buf, i) != want) return false;
    }
    (void)sgn;
    return true;
}

static i128 w_wrap(u128 v, uint32_t bits, bool sgn) {
    if (bits < 128) {
        v &= ((u128)1 << bits) - 1;
        if (sgn && ((v >> (bits - 1)) & 1)) v |= ~(u128)0 << bits;
    }
    return (i128)v;
}

/* The range of a type of at most 128 bits, clamped to what an __int128 holds. */
static i128 w_lo(uint32_t bits, bool sgn) { return sgn ? (i128)(~(u128)0 << (bits - 1)) : 0; }
static i128 w_hi(uint32_t bits, bool sgn) {
    if (sgn) return (i128)(((u128)1 << (bits - 1)) - 1);
    return bits >= 127 ? (i128)(~(u128)0 >> 1) : (i128)(((u128)1 << bits) - 1);
}

static i128 w_rand(uint32_t bits, bool sgn) {
    i128 lo = w_lo(bits, sgn), hi = w_hi(bits, sgn);
    switch (rng() % 8) {
    case 0: {
        i128 pick[] = {0, 1, 2, 10, hi, lo, hi - 1, lo + 1, -1, -2};
        i128 v = pick[rng() % 10];
        return v < lo || v > hi ? 0 : v;
    }
    case 1: return w_wrap(rng() % 100, bits, sgn);
    case 2: return w_wrap(-(i128)(rng() % 100), bits, sgn);
    case 3: return w_wrap(rng(), bits, sgn);
    default: return w_wrap(((u128)rng() << 64) | rng(), bits, sgn);
    }
}

#define WCHECK(cond, ...) do { if (!(cond)) { FAIL(__VA_ARGS__); return; } } while (0)

static void w_cross_check(uint32_t bits, bool sgn) {
    uint8_t A[16], B[16], R[16], Q[16];
    size_t size = GM_WBYTES(bits);
    i128 lo = w_lo(bits, sgn), hi = w_hi(bits, sgn);
    for (int iter = 0; iter < 3000; iter++) {
        i128 a = w_rand(bits, sgn), b = w_rand(bits, sgn);
        w_pack(A, size, false, bits, a, true);
        w_pack(B, size, false, bits, b, true);
        i128 s = a + b, d = a - b;
        bool ovf = gm_wadd(R, A, B, bits, sgn);
        WCHECK(ovf == (s < lo || s > hi) && w_canonical(R, bits, sgn, w_wrap((u128)s, bits, sgn)),
               "add bits=%u sgn=%d", bits, sgn);
        ovf = gm_wsub(R, A, B, bits, sgn);
        WCHECK(ovf == (d < lo || d > hi) && w_canonical(R, bits, sgn, w_wrap((u128)d, bits, sgn)),
               "sub bits=%u sgn=%d", bits, sgn);
        i128 p;
        bool pov = __builtin_mul_overflow(a, b, &p);
        u128 pw = (u128)a * (u128)b;
        ovf = gm_wmul(R, A, B, bits, sgn);
        WCHECK(ovf == (pov || p < lo || p > hi) && w_canonical(R, bits, sgn, w_wrap(pw, bits, sgn)),
               "mul bits=%u sgn=%d", bits, sgn);
        ovf = gm_wneg(R, A, bits, sgn);
        WCHECK(ovf == (-a < lo || -a > hi) && w_canonical(R, bits, sgn, w_wrap((u128)0 - (u128)a, bits, sgn)),
               "neg bits=%u sgn=%d", bits, sgn);
        int st = gm_wdiv(Q, R, A, B, bits, sgn);
        if (b == 0) {
            WCHECK(st == 1, "div by zero bits=%u", bits);
        } else if (sgn && a == lo && b == -1) {
            WCHECK(st == 2, "div overflow bits=%u", bits);
        } else {
            WCHECK(st == 0 && w_canonical(Q, bits, sgn, a / b) && w_canonical(R, bits, sgn, a % b),
                   "div bits=%u sgn=%d", bits, sgn);
        }
        gm_wand(R, A, B, bits, sgn);
        WCHECK(w_canonical(R, bits, sgn, w_wrap((u128)a & (u128)b, bits, sgn)), "and");
        gm_wor(R, A, B, bits, sgn);
        WCHECK(w_canonical(R, bits, sgn, w_wrap((u128)a | (u128)b, bits, sgn)), "or");
        gm_wxor(R, A, B, bits, sgn);
        WCHECK(w_canonical(R, bits, sgn, w_wrap((u128)a ^ (u128)b, bits, sgn)), "xor");
        gm_wnot(R, A, bits, sgn);
        WCHECK(w_canonical(R, bits, sgn, w_wrap(~(u128)a, bits, sgn)), "not bits=%u sgn=%d", bits, sgn);
        WCHECK(gm_wcmp(A, B, bits, sgn) == (a < b ? -1 : a > b ? 1 : 0), "cmp bits=%u sgn=%d", bits, sgn);
        WCHECK(gm_wsign(A, bits, sgn) == (a < 0 ? -1 : a > 0 ? 1 : 0), "sign");
        uint32_t sh = (uint32_t)(rng() % bits);
        u128 shifted = (u128)a << sh;
        i128 back = sgn ? (i128)w_wrap(shifted, bits, true) >> sh : (i128)((u128)w_wrap(shifted, bits, false) >> sh);
        ovf = gm_wshl(R, A, sh, bits, sgn);
        WCHECK(ovf == (back != a) && w_canonical(R, bits, sgn, w_wrap(shifted, bits, sgn)),
               "shl bits=%u sgn=%d sh=%u", bits, sgn, sh);
        gm_wshr(R, A, sh, bits, sgn);
        i128 right = sgn ? a >> sh : (i128)((u128)a >> sh);
        WCHECK(w_canonical(R, bits, sgn, right), "shr bits=%u sgn=%d sh=%u", bits, sgn, sh);
        /* In place. */
        memcpy(R, A, size);
        gm_wadd(R, R, B, bits, sgn);
        WCHECK(w_canonical(R, bits, sgn, w_wrap((u128)s, bits, sgn)), "add in place");
        memcpy(R, A, size);
        gm_wshl(R, R, sh, bits, sgn);
        WCHECK(w_canonical(R, bits, sgn, w_wrap(shifted, bits, sgn)), "shl in place");
        memcpy(R, A, size);
        gm_wshr(R, R, sh, bits, sgn);
        WCHECK(w_canonical(R, bits, sgn, right), "shr in place");
        /* Bit counts of the unsigned pattern. */
        u128 pat = (u128)a;
        if (bits < 128) pat &= ((u128)1 << bits) - 1;
        uint32_t lz = 0, tz = 0, pc = 0;
        while (lz < bits && !((pat >> (bits - 1 - lz)) & 1)) lz++;
        while (tz < bits && !((pat >> tz) & 1)) tz++;
        for (uint32_t i = 0; i < bits; i++) pc += (uint32_t)((pat >> i) & 1);
        WCHECK(gm_wclz(A, bits) == lz && gm_wctz(A, bits) == tz && gm_wpopcount(A, bits) == pc, "bit counts");
        u128 rev = 0;
        for (uint32_t i = 0; i < bits; i++) rev |= ((pat >> i) & 1) << (bits - 1 - i);
        gm_wbitreverse(R, A, bits, sgn);
        WCHECK(w_canonical(R, bits, sgn, w_wrap(rev, bits, sgn)), "bitreverse bits=%u sgn=%d", bits, sgn);
    }
}

/* Conversions between storage sizes and widths, against __int128. */
static void w_conv_check(uint32_t sbits, bool ssgn, uint32_t dbits, bool dsgn) {
    size_t ssize = sbits <= 8 ? 1 : sbits <= 16 ? 2 : sbits <= 32 ? 4 : sbits <= 64 ? 8 : sbits <= 128 ? 16 : GM_WBYTES(sbits);
    size_t dsize = dbits <= 8 ? 1 : dbits <= 16 ? 2 : dbits <= 32 ? 4 : dbits <= 64 ? 8 : dbits <= 128 ? 16 : GM_WBYTES(dbits);
    uint8_t A[40], R[40];
    i128 dlo = w_lo(dbits, dsgn), dhi = w_hi(dbits, dsgn);
    for (int iter = 0; iter < 500; iter++) {
        i128 v = w_rand(sbits, ssgn);
        w_pack(A, ssize, true, sbits, v, true);
        memset(R, 0x5a, sizeof R);
        int f = gm_wconv(R, dsize, dbits, dsgn, A, ssize, sbits, ssgn);
        bool fits = v >= dlo && v <= dhi;
        i128 want = w_wrap((u128)v, dbits, dsgn);
        WCHECK((f & 1) == fits && ((f >> 1) & 1) == (v < 0), "conv flags %u/%d -> %u/%d", sbits, ssgn, dbits, dsgn);
        uint8_t expect[40];
        w_pack(expect, dsize, true, dbits, want, false);
        WCHECK(memcmp(R, expect, dsize) == 0, "conv value %u/%d -> %u/%d", sbits, ssgn, dbits, dsgn);
    }
}

/* Parses a decimal into a `bits`-bit integer. */
static void w_dec(uint8_t *r, uint32_t bits, bool sgn, const char *s) {
    size_t size = GM_WBYTES(bits);
    uint8_t *ten = (uint8_t *)calloc(2, size), *dig = ten + size;
    gm_wput(ten, 0, 10);
    memset(r, 0, size);
    bool neg = *s == '-';
    if (neg) s++;
    for (; *s; s++) {
        gm_wmul(r, r, ten, bits, sgn);
        memset(dig, 0, size);
        gm_wput(dig, 0, (uint64_t)(*s - '0'));
        if (neg) gm_wsub(r, r, dig, bits, sgn);
        else gm_wadd(r, r, dig, bits, sgn);
    }
    free(ten);
}

static void w_expect(const uint8_t *v, uint32_t bits, bool sgn, const char *want, const char *what, int row) {
    gm_buf b = {0};
    gm_wfmt(&b, v, bits, sgn);
    if (b.len != strlen(want) || memcmp(b.data, want, b.len) != 0) {
        FAIL("row %d (%u bits, %s) %s: expected %s, got %.*s", row, bits, sgn ? "signed" : "unsigned", what, want,
             (int)b.len, (const char *)b.data);
    }
    gm_buf_free(&b);
}

typedef struct {
    int bits, sgn;
    const char *a, *b, *sum;
    int sum_ovf;
    const char *diff;
    int diff_ovf;
    const char *prod;
    int prod_ovf;
    const char *quot, *rem;
    const char *shl1;
    int shl1_ovf;
    const char *shr1;
    const char *shl2;
    int shl2_ovf;
    const char *shr2;
    const char *and_, *or_, *xor_, *not_;
    int cmp;
    uint64_t hash;
} wide_vec;

/* Generated with exact big-integer arithmetic. */
static const wide_vec wide_vecs[] = {
    {129, 0, "14809938836708178893", "532884965584162691049149968702778208528", "532884965584162691063959907539486387421", 0, "147679768257714235892409184997466393277", 1, "94392867361846490327871698672418175440", 1, "0", "14809938836708178893", "473918042774661724576", 0, "462810588647130590", "143867010218749458095903552552952659968", 1, "0", "109010276136792320", "532884965584162691063850897263349595101", "532884965584162691063741886987212802781", "680564733841876926911939276026828244018", -1, 1759098294063505789ULL},
    {129, 0, "382", "2", "384", 0, "380", 0, "764", 0, "191", "0", "12224", 0, "11", "56373249889256389738496", 0, "0", "2", "382", "380", "680564733841876926926749214863536422529", 1, 642338024591939808ULL},
    {129, 0, "590522786847514635539500255525912644818", "2931332081818", "590522786847514635539500258457244726636", 0, "590522786847514635539500252594580563000", 0, "101384903519392322451894947895423403604", 1, "201452026029502209186024818", "2546158085694", "521481365389791310241779375513721215552", 1, "18453837088984832360609382985184770150", "84506404873708548715013531781965021184", 1, "4001538052514186450", "2929184540818", "590522786847514635539500255528060185818", "590522786847514635539500252598875645000", "90041946994362291387248959337623778093", 1, 1411818590669224210ULL},
    {129, 1, "-340282366920938463463374607431768211246", "116226989933199130392345007519263716491", "-224055376987739333071029599912504494755", 0, "224055376987739333071029599912504495175", 1, "247619834585186476492854451389837449734", 1, "-2", "-107828387054540202678684592393240778264", "6720", 1, "-10633823966279326983230456482242756602", "30990530043832046714880", 1, "-2305843009213693952", "130", "-224055376987739333071029599912504494885", "-224055376987739333071029599912504495015", "340282366920938463463374607431768211245", -1, 18054825117247309144ULL},
    {129, 1, "10", "222047960748724778112612728384598485973", "222047960748724778112612728384598485983", 0, "-222047960748724778112612728384598485963", 0, "178785405961617000345879639255375590994", 1, "0", "10", "320", 0, "0", "1475739525896764129280", 0, "0", "0", "222047960748724778112612728384598485983", "222047960748724778112612728384598485983", "-11", -1, 12638164110811449565ULL},
    {129, 1, "-2", "-340282366920938463463374607431768210702", "-340282366920938463463374607431768210704", 0, "340282366920938463463374607431768210700", 0, "-1508", 1, "0", "-2", "-64", 0, "-1", "-295147905179352825856", 0, "-1", "-340282366920938463463374607431768210702", "-2", "340282366920938463463374607431768210700", "1", 1, 12638353226811501857ULL},
    {200, 0, "857631703270447505850966366569", "0", "857631703270447505850966366569", 0, "857631703270447505850966366569", 0, "0", 0, NULL, NULL, "27444214504654320187230923730208", 0, "26800990727201484557842698955", "662728526109380729510136870052847938455455463013959883292672", 1, "0", "0", "857631703270447505850966366569", "857631703270447505850966366569", "1606938044258990275541962092340304970818932546276941868934806", 1, 17706811705084819424ULL},
    {200, 0, "10", "754", "764", 0, "1606938044258990275541962092341162602522202993782792835300632", 1, "7540", 0, "0", "10", "320", 0, "0", "101412048018258352119736256430080", 0, "0", "2", "762", "760", "1606938044258990275541962092341162602522202993782792835301365", -1, 12638164110811449565ULL},
    {200, 0, "40321416383773678256", "2395661151836497898", "42717077535610176154", 0, "37925755231937180358", 0, "96596450817630287823682075824872305888", 0, "16", "1990837954389711888", "1290285324280757704192", 0, "1260044261992927445", "408907741447544529873547064568434825605543308034048", 0, "0", "2382984820740457120", "40334092714869719034", "37951107894129261914", "1606938044258990275541962092341162602522162672366409061623119", 1, 18108570666283802310ULL},
    {200, 1, "-803469022129495137770981046170581301261101496891396417650380", "803469022129495137770981046170581301261101496891396417650687", "307", 0, "309", 1, "803469022129495137770981046170581301261101496891396417650380", 1, "0", "-803469022129495137770981046170581301261101496891396417650380", "9856", 1, "-25108406941546723055343157692830665664409421777856138051575", "3123491078962357245287876698046464", 1, "-79228162514264337593543950336", "308", "-1", "-309", "803469022129495137770981046170581301261101496891396417650379", -1, 13660112426095052066ULL},
    {200, 1, "-803469022129495137770981046170581301261101496891396417650330", "-58236017469844825126", "803469022129495137770981046170581301261043260873926572825920", 1, "-803469022129495137770981046170581301261043260873926572825204", 0, "-20848494254204447395108", 1, "13796771431795438158067382399947861671807", "-27365464598498227648", "11456", 1, "-25108406941546723055343157692830665664409421777856138051573", "3630551319053649005886557980196864", 1, "-79228162514264337593543950336", "-803469022129495137770981046170581301261101496891396417650366", "-58236017469844825090", "803469022129495137770981046170581301261043260873926572825276", "803469022129495137770981046170581301261101496891396417650329", -1, 10807974379271131736ULL},
    {200, 1, "439890267858022660799697771730148236273790283385330920431492", "-803469022129495137770981046170581301261101496891396417649881", "-363578754271472476971283274440433064987311213506065497218389", 0, "-363578754271472476971283274440433064987311213506065497220003", 1, "-141861619812563629417520621167308484458102934035163813390052", 1, "0", "439890267858022660799697771730148236273790283385330920431492", "-385953826874187334287330135705719861938537875714546063904640", 1, "13746570870563208149990555366567132383555946355791591263484", "-754545835763363751659800441020898544256200758657678781186048", 1, "43376529362549138748913508599", "772", "-363578754271472476971283274440433064987311213506065497219161", "-363578754271472476971283274440433064987311213506065497219933", "-439890267858022660799697771730148236273790283385330920431493", 1, 11446250984232575734ULL},
    {256, 0, "1", "694", "695", 0, "115792089237316195423570985008687907853269984665640564039457584007913129639243", 1, "694", 0, "0", "1", "32", 0, "0", "2722258935367507707706996859454145691648", 0, "0", "0", "695", "695", "115792089237316195423570985008687907853269984665640564039457584007913129639934", -1, 12638152016183539244ULL},
    {256, 0, "37198249047630682618190532", "32560033743813263569636374", "69758282791443946187826906", 0, "4638215303817419048554158", 0, "1211176244201624621055106286002534100396797089610968", 0, "1", "4638215303817419048554158", "1190343969524181843782097024", 0, "1162445282738458831818454", "101263265849938509575727100475127437449347520040843498661385076736", 0, "0", "32357804374945607832356868", "37400478416498338355470038", "5042674041552730523113170", "115792089237316195423570985008687907853269984665640526841208536377230511449403", 1, 17970934762581138798ULL},
    {256, 0, "9236703472267792479178389", "38290479999458396899120504", "47527183471726189378298893", 0, "115792089237316195423570985008687907853269984665640534985681056817308709697821", 1, "353677809565797835325645876242794664650326623588056", 0, "0", "9236703472267792479178389", "295574511112569359333708448", 0, "288646983508368514974324", "25144698560721082509031565764732941961947506726210753111179395072", 0, "0", "9219573184388522384032784", "38307610287337666994266109", "29088037102949144610233325", "115792089237316195423570985008687907853269984665640554802754111740120650461546", -1, 11380523745454067376ULL},
    {256, 1, "-57896044618658097711785492504343953926634992332820282019728792003956564819055", "28950725381121070744219916965161499344980839057478416444761874438504485039292", "-28945319237537026967565575539182454581654153275341865574966917565452079779763", 0, "28945319237537026967565575539182454581654153275341865574966917565452079781589", 1, "31415926855445032898599607211605911421949555711745613071262208550401282968188", 1, "-1", "-28945319237537026967565575539182454581654153275341865574966917565452079779763", "29216", 1, "-1809251394333065553493296640760748560207343510400633813116524750123642650596", "2485422407990534537136488132681635016474624", 1, "-21267647932558653966460912964485513216", "144", "-28945319237537026967565575539182454581654153275341865574966917565452079779907", "-28945319237537026967565575539182454581654153275341865574966917565452079780051", "57896044618658097711785492504343953926634992332820282019728792003956564819054", -1, 2713581493372850213ULL},
    {256, 1, "10", "298928997886980567049935591858221839899", "298928997886980567049935591858221839909", 0, "-298928997886980567049935591858221839889", 0, "2989289978869805670499355918582218398990", 0, "0", "10", "320", 0, "0", "27222589353675077077069968594541456916480", 0, "0", "10", "298928997886980567049935591858221839899", "298928997886980567049935591858221839889", "-11", -1, 12638164110811449565ULL},
    {256, 1, "-57896044618658097711785492504343953926634992332820282019728792003956564819967", "-43984144353170544391195684759917186497092411592675064518055356960793679982126", "13911900265487553320589807744426767429542580740145217501673435043162884837843", 1, "-13911900265487553320589807744426767429542580740145217501673435043162884837841", 0, "-43984144353170544391195684759917186497092411592675064518055356960793679982126", 1, "1", "-13911900265487553320589807744426767429542580740145217501673435043162884837841", "32", 1, "-1809251394333065553493296640760748560207343510400633813116524750123642650624", "2722258935367507707706996859454145691648", 1, "-21267647932558653966460912964485513216", "-57896044618658097711785492504343953926634992332820282019728792003956564819968", "-43984144353170544391195684759917186497092411592675064518055356960793679982125", "13911900265487553320589807744426767429542580740145217501673435043162884837843", "57896044618658097711785492504343953926634992332820282019728792003956564819966", -1, 516187250012556964ULL},
};

static void test_wide_vectors(void) {
    for (size_t i = 0; i < sizeof wide_vecs / sizeof wide_vecs[0]; i++) {
        const wide_vec *v = &wide_vecs[i];
        uint32_t bits = (uint32_t)v->bits;
        bool sgn = v->sgn != 0;
        uint8_t a[160], b[160], r[160], q[160];
        w_dec(a, bits, sgn, v->a);
        w_dec(b, bits, sgn, v->b);
        w_expect(a, bits, sgn, v->a, "parse", (int)i);
        WCHECK(gm_wadd(r, a, b, bits, sgn) == (v->sum_ovf != 0), "sum overflow row %zu", i);
        w_expect(r, bits, sgn, v->sum, "sum", (int)i);
        WCHECK(gm_wsub(r, a, b, bits, sgn) == (v->diff_ovf != 0), "diff overflow row %zu", i);
        w_expect(r, bits, sgn, v->diff, "diff", (int)i);
        WCHECK(gm_wmul(r, a, b, bits, sgn) == (v->prod_ovf != 0), "prod overflow row %zu", i);
        w_expect(r, bits, sgn, v->prod, "prod", (int)i);
        int st = gm_wdiv(q, r, a, b, bits, sgn);
        if (v->quot == NULL) {
            WCHECK(st != 0, "div status row %zu", i);
        } else {
            WCHECK(st == 0, "div status row %zu", i);
            w_expect(q, bits, sgn, v->quot, "quot", (int)i);
            w_expect(r, bits, sgn, v->rem, "rem", (int)i);
        }
        WCHECK(gm_wshl(r, a, 5, bits, sgn) == (v->shl1_ovf != 0), "shl overflow row %zu", i);
        w_expect(r, bits, sgn, v->shl1, "shl", (int)i);
        gm_wshr(r, a, 5, bits, sgn);
        w_expect(r, bits, sgn, v->shr1, "shr", (int)i);
        uint32_t big = (uint32_t)bits / 2 + 3;
        WCHECK(gm_wshl(r, a, big, bits, sgn) == (v->shl2_ovf != 0), "shl2 overflow row %zu", i);
        w_expect(r, bits, sgn, v->shl2, "shl2", (int)i);
        gm_wshr(r, a, big, bits, sgn);
        w_expect(r, bits, sgn, v->shr2, "shr2", (int)i);
        gm_wand(r, a, b, bits, sgn);
        w_expect(r, bits, sgn, v->and_, "and", (int)i);
        gm_wor(r, a, b, bits, sgn);
        w_expect(r, bits, sgn, v->or_, "or", (int)i);
        gm_wxor(r, a, b, bits, sgn);
        w_expect(r, bits, sgn, v->xor_, "xor", (int)i);
        gm_wnot(r, a, bits, sgn);
        w_expect(r, bits, sgn, v->not_, "not", (int)i);
        WCHECK(gm_wcmp(a, b, bits, sgn) == v->cmp, "cmp row %zu", i);
        WCHECK(gm_whash(0xcbf29ce484222325ULL, a, bits, sgn) == v->hash, "hash row %zu", i);
    }
}

/* Algebraic identities at widths too large for the vectors. */
static void test_wide_identities(void) {
    static const uint32_t widths[] = {129, 192, 200, 256, 1000, 4100};
    for (size_t wi = 0; wi < sizeof widths / sizeof widths[0]; wi++) {
        uint32_t bits = widths[wi];
        size_t size = GM_WBYTES(bits);
        uint8_t *buf = (uint8_t *)malloc(size * 8);
        uint8_t *a = buf, *b = buf + size, *c = buf + 2 * size, *d = buf + 3 * size, *q = buf + 4 * size,
                *r = buf + 5 * size, *t = buf + 6 * size;
        for (int sgn = 0; sgn <= 1; sgn++) {
            for (int iter = 0; iter < (bits > 1000 ? 20 : 200); iter++) {
                for (size_t i = 0; i < size; i++) {
                    a[i] = (uint8_t)rng();
                    b[i] = (uint8_t)rng();
                    c[i] = (uint8_t)rng();
                }
                /* Make some operands small or sparse. */
                size_t keep = 1 + rng() % size;
                if (iter % 3 == 0) {
                    for (size_t i = keep; i < size; i++) *gm_wbp(b, i) = 0;
                }
                if (iter % 5 == 0) {
                    uint8_t fill = sgn && (*gm_wbp(a, keep - 1) & 0x80) ? 0xff : 0;
                    for (size_t i = keep; i < size; i++) *gm_wbp(a, i) = fill;
                }
                /* (a + b) - b == a and (a - b) + b == a, even with overflow. */
                gm_wadd(t, a, b, bits, sgn);
                gm_wsub(d, t, b, bits, sgn);
                gm_wfix(a, bits, sgn);
                CHECK(memcmp(d, a, size) == 0);
                /* a * (b + c) == a*b + a*c (mod 2^bits). */
                gm_wadd(t, b, c, bits, sgn);
                gm_wmul(d, a, t, bits, sgn);
                gm_wmul(q, a, b, bits, sgn);
                gm_wmul(r, a, c, bits, sgn);
                gm_wadd(t, q, r, bits, sgn);
                CHECK(memcmp(d, t, size) == 0);
                /* a == (a / b) * b + a % b when b != 0 and the division is defined. */
                gm_wfix(b, bits, sgn);
                int st = gm_wdiv(q, r, a, b, bits, sgn);
                if (st == 0) {
                    gm_wmul(t, q, b, bits, sgn);
                    bool o = gm_wadd(d, t, r, bits, sgn);
                    CHECK(!o && memcmp(d, a, size) == 0);
                    /* |r| < |b| */
                    uint8_t *ab = t, *ar = d;
                    if (sgn && gm_wisneg(b, bits, true)) gm_wneg(ab, b, bits, true); else memcpy(ab, b, size);
                    if (sgn && gm_wisneg(r, bits, true)) gm_wneg(ar, r, bits, true); else memcpy(ar, r, size);
                    CHECK(gm_wcmp(ar, ab, bits, false) < 0);
                }
                /* Shifts: (a << k) >> k == a when no overflow; a >> k == a / 2^k for unsigned. */
                uint32_t k = (uint32_t)(rng() % bits);
                bool ovf = gm_wshl(t, a, k, bits, sgn);
                gm_wshr(d, t, k, bits, sgn);
                CHECK(ovf == (memcmp(d, a, size) != 0));
                /* Conversion round trip through a wider type. */
                uint32_t wider = bits + 70;
                size_t wsize = GM_WBYTES(wider);
                uint8_t *w = (uint8_t *)malloc(wsize);
                int f = gm_wconv(w, wsize, wider, sgn, a, size, bits, sgn);
                CHECK((f & 1) != 0);
                f = gm_wconv(t, size, bits, sgn, w, wsize, wider, sgn);
                CHECK((f & 1) != 0 && memcmp(t, a, size) == 0);
                free(w);
                /* Decimal text parses back. */
                gm_buf text = {0};
                gm_wfmt(&text, a, bits, sgn);
                gm_buf_push(&text, "", 1);
                w_dec(t, bits, sgn, (const char *)text.data);
                CHECK(memcmp(t, a, size) == 0);
                gm_buf_free(&text);
            }
        }
        free(buf);
    }
}

static void test_wide_misc(void) {
    uint8_t a[160], b[160], r[160];
    /* Extremes, printing. */
    gm_wmax(a, 200, false);
    w_expect(a, 200, false, "1606938044258990275541962092341162602522202993782792835301375", "u200 max", 0);
    gm_wmin(a, 256, true);
    w_expect(a, 256, true, "-57896044618658097711785492504343953926634992332820282019728792003956564819968",
             "i256 min", 0);
    gm_wmax(a, 256, true);
    w_expect(a, 256, true, "57896044618658097711785492504343953926634992332820282019728792003956564819967",
             "i256 max", 0);
    memset(a, 0, sizeof a);
    w_expect(a, 129, true, "0", "zero", 0);
    memset(a, 0xff, sizeof a);
    w_expect(a, 129, true, "-1", "minus one", 0);
    w_expect(a, 129, false, "680564733841876926926749214863536422911", "u129 max with junk", 0);

    /* Overflow edges. */
    gm_wmax(a, 200, false);
    memset(b, 0, sizeof b);
    gm_wput(b, 0, 1);
    CHECK(gm_wadd(r, a, b, 200, false));
    w_expect(r, 200, false, "0", "wrapped sum", 0);
    CHECK(gm_wsub(r, b, a, 200, false) && gm_wsub(r, b, b, 200, false) == false);
    gm_wmin(a, 200, true);
    memset(b, 0xff, GM_WBYTES(200));
    CHECK(gm_wdiv(NULL, NULL, a, b, 200, true) == 2);
    CHECK(gm_wmul(r, a, b, 200, true));
    CHECK(gm_wneg(r, a, 200, true));
    gm_wmax(a, 200, true);
    CHECK(gm_wmul(r, a, a, 200, true));
    w_expect(r, 200, true, "1", "max squared wraps to 1", 0);
    gm_wmin(a, 200, true);
    memset(b, 0, sizeof b);
    gm_wput(b, 0, 1);
    CHECK(gm_wdiv(r, NULL, a, b, 200, true) == 0);
    CHECK(gm_wcmp(r, a, 200, true) == 0);

    /* -2^100 * 2^99 is -2^199, the smallest i200, so it does not overflow. */
    gm_wmin(a, 200, true);
    gm_wshr(a, a, 99, 200, true);
    w_expect(a, 200, true, "-1267650600228229401496703205376", "min >> 99", 0);
    memset(b, 0, sizeof b);
    gm_wput(b, 0, 1);
    CHECK(!gm_wshl(b, b, 99, 200, true));
    CHECK(!gm_wmul(r, a, b, 200, true));
    gm_wmin(b, 200, true);
    CHECK(gm_wcmp(r, b, 200, true) == 0);

    /* Powers. */
    memset(a, 0, sizeof a);
    gm_wput(a, 0, 3);
    CHECK(!gm_wpow(r, a, 100, 200, false));
    w_expect(r, 200, false, "515377520732011331036461129765621272702107522001", "3^100", 0);
    CHECK(gm_wpow(r, a, 200, 200, false));
    CHECK(!gm_wpow(r, a, 0, 200, false));
    w_expect(r, 200, false, "1", "3^0", 0);

    /* Shift amounts. */
    uint8_t amt[32] = {0};
    *gm_wbp(amt, 0) = 200;
    CHECK(gm_wshamt(amt, 200) == 200);
    *gm_wbp(amt, 20) = 1;
    CHECK(gm_wshamt(amt, 200) == UINT64_MAX);
    /* An amount of at most 128 bits is an ordinary integer. */
    unsigned __int128 small = 77;
    CHECK(gm_wshamt((const uint8_t *)&small, 128) == 77);
    small = (unsigned __int128)1 << 70;
    CHECK(gm_wshamt((const uint8_t *)&small, 128) == UINT64_MAX);
    uint16_t small16 = 9;
    CHECK(gm_wshamt((const uint8_t *)&small16, 16) == 9);

    /* Hash of small values equals the bytes of the minimal two's complement form. */
    memset(a, 0, sizeof a);
    uint64_t h0 = 0xcbf29ce484222325ULL;
    uint64_t want = (h0 ^ 0) * 0x100000001b3ULL;
    CHECK(gm_whash(h0, a, 200, true) == want);
    memset(a, 0xff, sizeof a);
    want = (h0 ^ 0xff) * 0x100000001b3ULL;
    CHECK(gm_whash(h0, a, 200, true) == want);

    /* Floats. */
    memset(a, 0, sizeof a);
    *gm_wbp(a, 24) = 1; /* 2^192 */
    CHECK(gm_wtof(a, 256, false, 53) == 6277101735386680763835789423207666416102355444464034512896.0);
    CHECK(gm_wtof(a, 256, false, 24) == 6277101735386680763835789423207666416102355444464034512896.0);
    memset(a, 0xff, 32); /* -1 as i256, 2^256 - 1 as u256 */
    CHECK(gm_wtof(a, 256, true, 53) == -1.0);
    CHECK(gm_wtof(a, 256, false, 53) == 115792089237316195423570985008687907853269984665640564039457584007913129639936.0);
    /* 2^53 + 1 rounds to even, 2^53 + 3 rounds up. */
    memset(a, 0, sizeof a);
    *gm_wbp(a, 0) = 1;
    *gm_wbp(a, 6) = 0x20;
    CHECK(gm_wtof(a, 200, false, 53) == 9007199254740992.0);
    *gm_wbp(a, 0) = 3;
    CHECK(gm_wtof(a, 200, false, 53) == 9007199254740996.0);
    *gm_wbp(a, 0) = 0;
    *gm_wbp(a, 8) = 1;
    *gm_wbp(a, 24) = 0;
    CHECK(gm_wfromd(r, 200, false, 18446744073709551616.0) == 0);
    CHECK(*gm_wbp(r, 8) == 1 && *gm_wbp(r, 0) == 0);
    CHECK(gm_wfromd(r, 200, false, -1.0) == 1);
    CHECK(gm_wfromd(r, 200, true, -1.0) == 0 && *gm_wbp(r, 24) == 0xff);
    CHECK(gm_wfromd(r, 130, true, 1.0 / 0.0) == 2 && gm_wfromd(r, 130, true, -1.0 / 0.0) == 1);
    CHECK(gm_wfromd(r, 130, false, 0.0 / 0.0) == 3);
    CHECK(gm_wfromd(r, 131, false, 1361129467683753853853498429727072845824.0) == 0);  /* 2^130 */
    CHECK(gm_wfromd(r, 130, false, 1361129467683753853853498429727072845824.0) == 2);
    CHECK(gm_wfromd(r, 131, true, -1361129467683753853853498429727072845824.0) == 0);  /* -2^130 = i131 min */
    CHECK(gm_wfromd(r, 130, true, -1361129467683753853853498429727072845824.0) == 1);

    /* bswap */
    for (int i = 0; i < 32; i++) *gm_wbp(a, (size_t)i) = (uint8_t)i;
    gm_wbswap(r, a, 256);
    CHECK(*gm_wbp(r, 0) == 31 && *gm_wbp(r, 31) == 0 && *gm_wbp(r, 15) == 16);
}

static void test_wide(void) {
    static const uint32_t widths[] = {1, 2, 7, 63, 64, 65, 100, 126};
    for (size_t i = 0; i < sizeof widths / sizeof widths[0]; i++) {
        w_cross_check(widths[i], false);
        w_cross_check(widths[i], true);
    }
    static const struct { uint32_t bits; bool sgn; } types[] = {
        {8, false}, {8, true}, {24, true}, {32, false}, {64, true}, {100, false}, {126, true}, {128, false},
        {129, false}, {129, true}, {200, true}, {1, false},
    };
    for (size_t i = 0; i < sizeof types / sizeof types[0]; i++) {
        for (size_t j = 0; j < sizeof types / sizeof types[0]; j++) {
            /* Sources above 127 bits are covered by the identity tests; the reference needs 127. */
            if (types[i].bits > 126 || types[j].bits > 128) continue;
            w_conv_check(types[i].bits, types[i].sgn, types[j].bits, types[j].sgn);
        }
    }
    test_wide_vectors();
    test_wide_identities();
    test_wide_misc();
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
    /* A wider-than-128-bit integer is 64-bit words, least significant first, each native. */
    uint64_t wide[3] = {0x0102030405060708ULL, 0x1112131415161718ULL, 0x2122232425262728ULL};
    CHECK(*gm_wbp((uint8_t *)wide, 0) == 0x08 && *gm_wbp((uint8_t *)wide, 7) == 0x01);
    CHECK(*gm_wbp((uint8_t *)wide, 8) == 0x18 && *gm_wbp((uint8_t *)wide, 23) == 0x21);
    /* A #bits struct is an integer of its storage size. */
    static const gm_field bf[] = {{"lo", 0, &ty_u8}, {"hi", 8, &ty_u8}};
    static const gm_type bits16 = {.kind = GM_K_BITS, .size = 2, .align = 2, .name = "B", .nfields = 2, .fields = bf};
    uint16_t b = 0x0a0b;
    check_fmt(&bits16, &b, true, "B(lo: 11, hi: 10)");
    /* Tag and wide formatting see the same layout. */
    uint8_t bytes32[4] = {0};
    gm_store_u128(bytes32, 4, 305419896);
    check_int(&ty_u32, bytes32, "305419896");
}

int main(int argc, char **argv) {
    gm_set_args(argc, argv);
    saved_argc = argc;
    saved_argv = argv;
    if (argc > 1) return scenario(argv[1]);
    test_ints();
    test_wide();
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
    gm_flush();
    if (failures != 0) {
        fprintf(stderr, "%d runtime check(s) failed\n", failures);
        return 1;
    }
    printf("runtime ok\n");
    return 0;
}

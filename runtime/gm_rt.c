/* The runtime of programs compiled by the Gamma C back end: implementations of the
 * declarations in gm_rt.h. The compiler emits gm_rt.h, then this file, then the generated
 * code, as one translation unit, so this file includes nothing itself.
 *
 * Observable behavior (trap output, value display, heap diagnostics) mirrors the reference
 * interpreter byte for byte.
 */

/* ---- traps ---------------------------------------------------------------- */

static _Noreturn void gm_trap_bytes(const uint8_t *msg, size_t len, const char *file, uint32_t line, uint32_t col) {
    gm_flush();
    fputs("trap: ", stderr);
    if (len > 0) fwrite(msg, 1, len, stderr);
    fputc('\n', stderr);
    if (file != NULL) fprintf(stderr, "  at %s:%u:%u\n", file, (unsigned)line, (unsigned)col);
    fflush(stderr);
    /* The status of a process terminated by `abort` (SIGABRT). */
    exit(134);
}

static _Noreturn void gm_trap(const char *msg, const char *file, uint32_t line, uint32_t col) {
    gm_trap_bytes((const uint8_t *)msg, strlen(msg), file, line, col);
}

/* ---- growable byte buffer --------------------------------------------------- */

static void gm_buf_push(gm_buf *b, const void *data, size_t len) {
    if (len == 0) return;
    if (len > SIZE_MAX - b->len) gm_trap("out of memory", NULL, 0, 0);
    size_t need = b->len + len;
    if (need > b->cap) {
        size_t cap = b->cap < 64 ? 64 : b->cap;
        while (cap < need) {
            if (cap > SIZE_MAX / 2) {
                cap = need;
                break;
            }
            cap *= 2;
        }
        uint8_t *p = (uint8_t *)realloc(b->data, cap);
        if (p == NULL) gm_trap("out of memory", NULL, 0, 0);
        b->data = p;
        b->cap = cap;
    }
    memcpy(b->data + b->len, data, len);
    b->len = need;
}

static void gm_buf_free(gm_buf *b) {
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

/* ---- value display (spec §21.6.1) ---------------------------------------------- */

static void gm_fmt_str(gm_buf *out, const char *s) { gm_buf_push(out, s, strlen(s)); }
static void gm_fmt_ch(gm_buf *out, char c) { gm_buf_push(out, &c, 1); }

/* Decimal digits of v into buf (at least 40 bytes); returns the length. */
static size_t gm_u128_dec(char *buf, unsigned __int128 v) {
    char tmp[40];
    size_t n = 0;
    do {
        tmp[n++] = (char)('0' + (int)(v % 10));
        v /= 10;
    } while (v != 0);
    for (size_t i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
    return n;
}

/* The value of a `bits`-bit integer stored in `size` little-endian bytes at p. */
static void gm_fmt_int(gm_buf *out, const gm_type *t, const uint8_t *p) {
    unsigned __int128 raw = 0;
    uint32_t size = t->size > 16 ? 16 : t->size;
    for (uint32_t i = size; i > 0; i--) raw = (raw << 8) | p[i - 1];
    unsigned bits = t->bits > 128 ? 128 : t->bits;
    unsigned __int128 mask = bits >= 128 ? ~(unsigned __int128)0 : (((unsigned __int128)1 << bits) - 1);
    raw &= mask;
    char buf[48];
    size_t n = 0;
    if (t->is_signed && bits > 0 && ((raw >> (bits - 1)) & 1) != 0) {
        unsigned __int128 mag = (~raw + 1) & mask; /* |value| as an unsigned number */
        buf[n++] = '-';
        n += gm_u128_dec(buf + n, mag);
    } else {
        n = gm_u128_dec(buf, raw);
    }
    gm_buf_push(out, buf, n);
}

/* binary16 -> double, exactly. */
static double gm_f16_to_f64(uint16_t h) {
    uint64_t sign = (uint64_t)(h >> 15) << 63;
    unsigned exp = (h >> 10) & 0x1f;
    uint64_t man = h & 0x3ff;
    uint64_t bits;
    double d;
    if (exp == 0) {
        d = (double)man / 16777216.0; /* man * 2^-24 */
        memcpy(&bits, &d, 8);
        bits |= sign;
    } else if (exp == 0x1f) {
        bits = sign | (0x7ffull << 52) | (man << 42);
    } else {
        bits = sign | ((uint64_t)(exp - 15 + 1023) << 52) | (man << 42);
    }
    memcpy(&d, &bits, 8);
    return d;
}

/* Rounds a double to the nearest binary16 (ties to even); returns its bits. */
static uint16_t gm_f64_to_f16(double x) {
    uint64_t bits;
    memcpy(&bits, &x, 8);
    uint16_t sign = (uint16_t)((bits >> 63) << 15);
    int exp = (int)((bits >> 52) & 0x7ff);
    uint64_t man = bits & ((1ull << 52) - 1);
    if (exp == 0x7ff) return man == 0 ? (uint16_t)(sign | 0x7c00) : (uint16_t)(sign | 0x7e00);
    if (exp == 0) return sign;
    int e = exp - 1023;
    int he = e + 15;
    if (he >= 1) {
        if (he >= 0x1f) return (uint16_t)(sign | 0x7c00);
        uint32_t keep = (uint32_t)(man >> 42);
        uint64_t rem = man & ((1ull << 42) - 1);
        uint64_t half = 1ull << 41;
        uint32_t r = ((uint32_t)he << 10) | keep;
        if (rem > half || (rem == half && (keep & 1) == 1)) r += 1;
        return (uint16_t)(sign | r);
    }
    unsigned __int128 m = ((unsigned __int128)1 << 52) | man;
    int shift = 28 - e;
    if (shift >= 120) return sign;
    unsigned __int128 q = m >> shift;
    unsigned __int128 rem = m & (((unsigned __int128)1 << shift) - 1);
    unsigned __int128 half = (unsigned __int128)1 << (shift - 1);
    uint32_t r = (uint32_t)q;
    if (rem > half || (rem == half && (r & 1) == 1)) r += 1;
    return (uint16_t)(sign | r);
}

/* Splits "d.ddde+XX" into its digits and the exponent. */
static size_t gm_split_digits(const char *s, char *digits, int *exp10) {
    size_t n = 0;
    for (; *s != 'e' && *s != '\0'; s++) {
        if (*s >= '0' && *s <= '9') digits[n++] = *s;
    }
    *exp10 = *s == 'e' ? atoi(s + 1) : 0;
    digits[n] = '\0';
    return n;
}

/* printf rounds an exact tie (x lies halfway between two n-digit decimals) to even, but the
 * display rule picks the larger one when it reads back as x as well. Detects the tie by
 * checking that its exact expansion is the (n+1)-digit one ending in 5, then bumps the last digit. */
static void gm_round_tie_up(double x, unsigned bits, size_t n, char *digits, int *exp10) {
    char buf[64], longer[24];
    int e2;
    snprintf(buf, sizeof buf, "%.*e", (int)n, x);
    size_t m = gm_split_digits(buf, longer, &e2);
    if (m != n + 1 || longer[n] != '5' || strtod(buf, NULL) != x) return;
    /* The (n+1)-digit rendering reads back as x, but only an exact expansion is a tie. A double
     * has at most 767 significant digits. */
    char exact[800];
    snprintf(exact, sizeof exact, "%.770e", x);
    for (const char *q = exact + 2 + n + 1; *q != 'e' && *q != '\0'; q++) {
        if (*q != '0') return;
    }
    char up[24];
    memcpy(up, longer, n);
    size_t i = n;
    while (i > 0 && up[i - 1] == '9') up[--i] = '0';
    if (i == 0) {
        /* 99...9 rounds up to 100...0 one decade higher. */
        memset(up, '0', n);
        up[0] = '1';
        e2 += 1;
    } else {
        up[i - 1]++;
    }
    up[n] = '\0';
    snprintf(buf, sizeof buf, "%c.%se%d", up[0], up + 1, e2);
    if (bits == 32 ? strtof(buf, NULL) == (float)x : strtod(buf, NULL) == x) {
        memcpy(digits, up, n + 1);
        *exp10 = e2;
    }
}

/* The shortest decimal digits (no trailing zeros, at least one) that read back as the
 * same value at the given width, and the decimal exponent of the first digit. x is positive
 * and finite, and exactly representable at that width. */
static void gm_shortest(double x, unsigned bits, char *digits, int *exp10) {
    char buf[64];
    int found = 0;
    if (bits == 16) {
        uint16_t target = gm_f64_to_f16(x);
        for (int d = 1; d <= 6 && !found; d++) {
            snprintf(buf, sizeof buf, "%.*e", d - 1, x);
            found = gm_f64_to_f16(strtod(buf, NULL)) == target;
        }
    } else if (bits == 32) {
        float f = (float)x;
        for (int d = 1; d <= 9 && !found; d++) {
            snprintf(buf, sizeof buf, "%.*e", d - 1, (double)f);
            found = strtof(buf, NULL) == f;
        }
    }
    for (int d = 1; d <= 17 && !found; d++) {
        snprintf(buf, sizeof buf, "%.*e", d - 1, x);
        found = strtod(buf, NULL) == x;
    }
    size_t n = gm_split_digits(buf, digits, exp10);
    if (bits != 16 && n >= 2) gm_round_tie_up(x, bits, n, digits, exp10);
    n = strlen(digits);
    while (n > 1 && digits[n - 1] == '0') digits[--n] = '\0';
}

/* The display form of a float (§21.6.1): "nan", "inf", "-inf", "0", "-0", else the shortest
 * round-trip digits in positional notation for decimal exponents -6..=20 and scientific
 * (`d.ddde<exp>`) otherwise. */
static void gm_fmt_float(gm_buf *out, const gm_type *t, const uint8_t *p) {
    double x;
    if (t->bits == 16) {
        uint16_t h;
        memcpy(&h, p, 2);
        x = gm_f16_to_f64(h);
    } else if (t->bits == 32) {
        float f;
        memcpy(&f, p, 4);
        x = (double)f;
    } else {
        memcpy(&x, p, 8);
    }
    if (__builtin_isnan(x)) return gm_fmt_str(out, "nan");
    if (__builtin_isinf(x)) return gm_fmt_str(out, x > 0 ? "inf" : "-inf");
    if (x == 0) return gm_fmt_str(out, __builtin_signbit(x) ? "-0" : "0");
    if (x < 0) {
        gm_fmt_ch(out, '-');
        x = -x;
    }
    char digits[24];
    int exp;
    gm_shortest(x, t->bits, digits, &exp);
    int nd = (int)strlen(digits);
    if (exp >= -6 && exp <= 20) {
        if (exp >= 0) {
            int int_len = exp + 1;
            if (nd <= int_len) {
                gm_buf_push(out, digits, (size_t)nd);
                for (int i = nd; i < int_len; i++) gm_fmt_ch(out, '0');
            } else {
                gm_buf_push(out, digits, (size_t)int_len);
                gm_fmt_ch(out, '.');
                gm_buf_push(out, digits + int_len, (size_t)(nd - int_len));
            }
        } else {
            gm_fmt_str(out, "0.");
            for (int i = 0; i < -exp - 1; i++) gm_fmt_ch(out, '0');
            gm_buf_push(out, digits, (size_t)nd);
        }
    } else {
        gm_fmt_ch(out, digits[0]);
        if (nd > 1) {
            gm_fmt_ch(out, '.');
            gm_buf_push(out, digits + 1, (size_t)(nd - 1));
        }
        char e[16];
        int n = snprintf(e, sizeof e, "e%d", exp);
        gm_buf_push(out, e, (size_t)n);
    }
}

/* Whether the n bytes at p are one complete, well-formed UTF-8 sequence. */
static bool gm_utf8_seq_ok(const uint8_t *p, size_t n) {
    uint8_t lo = 0x80, hi = 0xbf;
    if (n == 2 && p[0] >= 0xc2 && p[0] <= 0xdf) {
        /* any continuation byte */
    } else if (n == 3 && p[0] >= 0xe0 && p[0] <= 0xef) {
        if (p[0] == 0xe0) lo = 0xa0;
        if (p[0] == 0xed) hi = 0x9f;
    } else if (n == 4 && p[0] >= 0xf0 && p[0] <= 0xf4) {
        if (p[0] == 0xf0) lo = 0x90;
        if (p[0] == 0xf4) hi = 0x8f;
    } else {
        return false;
    }
    if (p[1] < lo || p[1] > hi) return false;
    for (size_t i = 2; i < n; i++) {
        if (p[i] < 0x80 || p[i] > 0xbf) return false;
    }
    return true;
}

static void gm_fmt_hex_escape(gm_buf *out, uint8_t b) {
    static const char hex[] = "0123456789abcdef";
    char e[4] = {'\\', 'x', hex[b >> 4], hex[b & 15]};
    gm_buf_push(out, e, 4);
}

/* Appends the bytes quoted with escapes; valid UTF-8 sequences are kept as they are. */
static void gm_quote_bytes(gm_buf *out, const uint8_t *s, size_t n) {
    gm_fmt_ch(out, '"');
    size_t i = 0;
    while (i < n) {
        uint8_t b = s[i];
        switch (b) {
        case '"': gm_fmt_str(out, "\\\""); break;
        case '\\': gm_fmt_str(out, "\\\\"); break;
        case '\n': gm_fmt_str(out, "\\n"); break;
        case '\r': gm_fmt_str(out, "\\r"); break;
        case '\t': gm_fmt_str(out, "\\t"); break;
        case 0: gm_fmt_str(out, "\\0"); break;
        default:
            if (b >= 0x20 && b <= 0x7e) {
                gm_fmt_ch(out, (char)b);
            } else if (b >= 0x80) {
                size_t len = b >= 0xf0 ? 4 : b >= 0xe0 ? 3 : b >= 0xc2 ? 2 : 0;
                if (b > 0xf4) len = 0;
                if (len > 0 && i + len <= n && gm_utf8_seq_ok(s + i, len)) {
                    gm_buf_push(out, s + i, len);
                    i += len;
                    continue;
                }
                gm_fmt_hex_escape(out, b);
            } else {
                gm_fmt_hex_escape(out, b);
            }
        }
        i++;
    }
    gm_fmt_ch(out, '"');
}

/* Reads an unsigned little-endian integer of n (at most 8) bytes. */
static uint64_t gm_load_uint(const uint8_t *p, uint32_t n) {
    uint64_t v = 0;
    if (n > 8) n = 8;
    for (uint32_t i = n; i > 0; i--) v = (v << 8) | p[i - 1];
    return v;
}

static uint64_t gm_load_ptr(const uint8_t *p) {
    uintptr_t v;
    memcpy(&v, p, sizeof v);
    return (uint64_t)v;
}

static void gm_fmt_seq(gm_buf *out, const gm_type *elem, const uint8_t *data, uint64_t n) {
    gm_fmt_ch(out, '[');
    for (uint64_t i = 0; i < n; i++) {
        if (i > 0) gm_fmt_str(out, ", ");
        gm_format(out, elem, data + i * elem->size, false);
    }
    gm_fmt_ch(out, ']');
}

/* The elements of a tuple-like list of fields, separated by ", " (named ones as `name: v`). */
static void gm_fmt_fields(gm_buf *out, const gm_type *t, const uint8_t *v) {
    for (uint32_t i = 0; i < t->nfields; i++) {
        const gm_field *f = &t->fields[i];
        if (i > 0) gm_fmt_str(out, ", ");
        if (t->named && f->name != NULL) {
            gm_fmt_str(out, f->name);
            gm_fmt_str(out, ": ");
        }
        gm_format(out, f->type, v + f->offset, false);
    }
}

static void gm_format(gm_buf *out, const gm_type *t, const void *vp, bool top) {
    const uint8_t *v = (const uint8_t *)vp;
    switch (t->kind) {
    case GM_K_INT: gm_fmt_int(out, t, v); break;
    case GM_K_FLOAT: gm_fmt_float(out, t, v); break;
    case GM_K_BOOL: gm_fmt_str(out, v[0] != 0 ? "true" : "false"); break;
    case GM_K_PTR: {
        char buf[32];
        int n = snprintf(buf, sizeof buf, "0x%llx", (unsigned long long)gm_load_ptr(v));
        gm_buf_push(out, buf, (size_t)n);
        break;
    }
    case GM_K_SLICE: {
        const uint8_t *data = (const uint8_t *)(uintptr_t)gm_load_ptr(v);
        uint64_t n = gm_load_ptr(v + sizeof(void *));
        const gm_type *e = t->elem;
        if (e->kind == GM_K_INT && e->bits == 8 && !e->is_signed) {
            if (top) {
                gm_buf_push(out, data, (size_t)n);
            } else {
                gm_quote_bytes(out, data, (size_t)n);
            }
        } else {
            gm_fmt_seq(out, e, data, n);
        }
        break;
    }
    case GM_K_ARRAY: gm_fmt_seq(out, t->elem, v, t->len); break;
    case GM_K_TUPLE:
        gm_fmt_ch(out, '(');
        gm_fmt_fields(out, t, v);
        if (t->nfields == 1 && !t->named) gm_fmt_ch(out, ',');
        gm_fmt_ch(out, ')');
        break;
    case GM_K_OPTIONAL: {
        bool present = t->flag_offset == GM_NICHE ? gm_load_ptr(v) != 0 : v[t->flag_offset] != 0;
        if (present) {
            gm_format(out, t->elem, v, false);
        } else {
            gm_fmt_str(out, "null");
        }
        break;
    }
    case GM_K_RESULT:
        if (v[t->tag_offset] != 0) {
            gm_fmt_ch(out, '!');
            gm_format(out, t->err, v + t->payload_offset, false);
        } else {
            gm_format(out, t->elem, v + t->payload_offset, false);
        }
        break;
    case GM_K_STRUCT: {
        gm_fmt_str(out, t->name);
        gm_fmt_ch(out, '(');
        bool first = true;
        for (uint32_t i = 0; i < t->nfields; i++) {
            const gm_field *f = &t->fields[i];
            if (f->name != NULL && strcmp(f->name, "_") == 0) continue;
            if (!first) gm_fmt_str(out, ", ");
            first = false;
            gm_fmt_str(out, f->name != NULL ? f->name : "");
            gm_fmt_str(out, ": ");
            gm_format(out, f->type, v + f->offset, false);
        }
        gm_fmt_ch(out, ')');
        break;
    }
    case GM_K_UNION: {
        uint64_t raw = gm_load_uint(v + t->tag_offset, t->tag_size);
        int64_t tag = (int64_t)raw;
        if (t->tag_signed && t->tag_size > 0 && t->tag_size < 8) {
            unsigned shift = 64 - 8u * t->tag_size;
            tag = (int64_t)(raw << shift) >> shift;
        }
        const gm_variant *var = NULL;
        for (uint32_t i = 0; i < t->nvariants; i++) {
            if (t->variants[i].tag == tag) {
                var = &t->variants[i];
                break;
            }
        }
        if (var == NULL) {
            gm_fmt_str(out, ".?");
            break;
        }
        gm_fmt_ch(out, '.');
        gm_fmt_str(out, var->name);
        if (var->payload != NULL) {
            const gm_type *pt = var->payload;
            const uint8_t *pv = v + t->payload_offset;
            gm_fmt_ch(out, '(');
            if (pt->kind == GM_K_TUPLE) {
                gm_fmt_fields(out, pt, pv);
            } else {
                gm_format(out, pt, pv, false);
            }
            gm_fmt_ch(out, ')');
        }
        break;
    }
    case GM_K_OPAQUE: gm_fmt_str(out, t->name); break;
    case GM_K_BITS: {
        unsigned __int128 whole = 0;
        for (uint32_t i = t->size; i > 0; i--) whole = (whole << 8) | v[i - 1];
        gm_fmt_str(out, t->name);
        gm_fmt_ch(out, '(');
        bool first = true;
        for (uint32_t i = 0; i < t->nfields; i++) {
            const gm_field *f = &t->fields[i];
            if (f->name != NULL && strcmp(f->name, "_") == 0) continue;
            if (!first) gm_fmt_str(out, ", ");
            first = false;
            gm_fmt_str(out, f->name != NULL ? f->name : "");
            gm_fmt_str(out, ": ");
            const gm_type *ft = f->type;
            unsigned w = ft->kind == GM_K_BOOL ? 1u : ft->bits;
            unsigned __int128 raw = whole >> f->offset;
            if (w < 128) raw &= (((unsigned __int128)1) << w) - 1;
            /* Sign-extend signed integers and signed tags from the field's width. */
            bool sign = (ft->kind == GM_K_INT && ft->is_signed) || (ft->kind == GM_K_UNION && ft->tag_signed);
            if (sign && w > 0 && w < 128 && ((raw >> (w - 1)) & 1)) raw |= ~(unsigned __int128)0 << w;
            uint8_t tmp[16];
            for (unsigned k = 0; k < sizeof tmp; k++) tmp[k] = (uint8_t)(raw >> (8 * k));
            if (ft->kind == GM_K_UNION) {
                /* An enum's value is its tag at the tag offset. */
                uint8_t e[16] = {0};
                memcpy(e + ft->tag_offset, tmp, ft->tag_size);
                gm_format(out, ft, e, false);
            } else {
                gm_format(out, ft, tmp, false);
            }
        }
        gm_fmt_ch(out, ')');
        break;
    }
    }
}

/* ---- ambient effects ---------------------------------------------------------- */

static void gm_write_out(const uint8_t *p, size_t len) {
    if (len > 0) fwrite(p, 1, len, stdout);
}

static void gm_write_err(const uint8_t *p, size_t len) {
    if (len > 0) fwrite(p, 1, len, stderr);
    fflush(stderr);
}

static void gm_flush(void) { fflush(stdout); }

typedef struct { const uint8_t *ptr; uintptr_t len; } gm_rt_str;
static gm_rt_str *gm_rt_args;
static size_t gm_rt_argc;

static void gm_set_args(int argc, char **argv) {
    if (gm_rt_args != NULL || argc <= 0) return;
    gm_rt_args = (gm_rt_str *)malloc((size_t)argc * sizeof(gm_rt_str));
    if (gm_rt_args == NULL) gm_trap("out of memory", NULL, 0, 0);
    for (int i = 0; i < argc; i++) {
        gm_rt_args[i].ptr = (const uint8_t *)argv[i];
        gm_rt_args[i].len = strlen(argv[i]);
    }
    gm_rt_argc = (size_t)argc;
}

static const void *gm_args(size_t *count) {
    *count = gm_rt_argc;
    return gm_rt_args;
}

static bool gm_env_var(const uint8_t *name, size_t len, const uint8_t **value, size_t *value_len) {
    char *key = (char *)malloc(len + 1);
    if (key == NULL) gm_trap("out of memory", NULL, 0, 0);
    if (len > 0) memcpy(key, name, len);
    key[len] = '\0';
    const char *v = getenv(key);
    free(key);
    if (v == NULL) return false;
    *value = (const uint8_t *)v;
    *value_len = strlen(v);
    return true;
}

/* ---- system services ---------------------------------------------------------------- */

static int64_t gm_io_error(int err) {
    switch (err) {
    case ENOENT: return -1;
    case EACCES: case EPERM: return -2;
    case EEXIST: return -3;
    case EAGAIN: return -4;
    case EINTR: return -5;
    case EPIPE: return -6;
    case ECONNRESET: return -7;
    case EINVAL: return -8;
    case ENOSPC: return -10;
    default: return -1000 - (int64_t)err;
    }
}

static int64_t gm_sys_read(void *p, size_t n) {
    gm_flush();
    for (;;) {
        ssize_t r = read(0, p, n);
        if (r >= 0) return (int64_t)r;
        if (errno != EINTR) return gm_io_error(errno);
    }
}

static uint64_t gm_clock_now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

static int64_t gm_clock_wall(void) {
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    return (int64_t)t.tv_sec * 1000000000 + (int64_t)t.tv_nsec;
}

static void gm_clock_sleep(uint64_t ns) {
    gm_flush();
    struct timespec t = {(time_t)(ns / 1000000000u), (long)(ns % 1000000000u)}, rest;
    while (nanosleep(&t, &rest) != 0 && errno == EINTR) t = rest;
}

static void gm_rand_fill(void *vp, size_t n) {
    static int fd = -1;
    uint8_t *p = (uint8_t *)vp;
    if (fd < 0) fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    while (n > 0) {
        ssize_t r = fd < 0 ? -1 : read(fd, p, n);
        if (r <= 0) {
            if (r < 0 && errno == EINTR) continue;
            gm_trap("cannot read random bytes", NULL, 0, 0);
        }
        p += r;
        n -= (size_t)r;
    }
}

/* A NUL-terminated copy of a path (freed by the caller), or NULL if it contains a NUL. */
static char *gm_path(const void *p, size_t n) {
    if (memchr(p, 0, n) != NULL) return NULL;
    char *s = (char *)malloc(n + 1);
    if (s == NULL) gm_trap("out of memory", NULL, 0, 0);
    memcpy(s, p, n);
    s[n] = 0;
    return s;
}

#define GM_RET_ERR(cond) ((cond) ? gm_io_error(errno) : 0)

static int64_t gm_fs_open(const void *path, size_t len, uint32_t flags) {
    char *s = gm_path(path, len);
    if (s == NULL) return gm_io_error(EINVAL);
    int rw = (flags & GM_FS_READ) && (flags & (GM_FS_WRITE | GM_FS_APPEND)) ? O_RDWR
             : (flags & (GM_FS_WRITE | GM_FS_APPEND)) ? O_WRONLY : O_RDONLY;
    int f = rw | O_CLOEXEC;
    if (flags & GM_FS_APPEND) f |= O_APPEND;
    if (flags & GM_FS_CREATE) f |= O_CREAT;
    if (flags & GM_FS_EXCLUSIVE) f |= O_CREAT | O_EXCL;
    if (flags & GM_FS_TRUNCATE) f |= O_TRUNC;
    int fd = open(s, f, 0666);
    int err = errno;
    free(s);
    return fd < 0 ? gm_io_error(err) : (int64_t)fd;
}

static int64_t gm_fs_close(int64_t f) { return GM_RET_ERR(close((int)f) != 0); }

static int64_t gm_fs_read(int64_t f, void *p, size_t n) {
    for (;;) {
        ssize_t r = read((int)f, p, n);
        if (r >= 0) return (int64_t)r;
        if (errno != EINTR) return gm_io_error(errno);
    }
}

static int64_t gm_fs_write(int64_t f, const void *p, size_t n) {
    for (;;) {
        ssize_t r = write((int)f, p, n);
        if (r >= 0) return (int64_t)r;
        if (errno != EINTR) return gm_io_error(errno);
    }
}

static int64_t gm_fs_seek(int64_t f, int64_t offset, uint32_t whence) {
    if (whence > 2 || (whence == 0 && offset < 0)) return gm_io_error(EINVAL);
    off_t r = lseek((int)f, (off_t)offset, whence == 0 ? SEEK_SET : whence == 1 ? SEEK_CUR : SEEK_END);
    return r < 0 ? gm_io_error(errno) : (int64_t)r;
}

static int64_t gm_fs_stat(const void *path, size_t len, void *out, size_t n) {
    char *s = gm_path(path, len);
    if (s == NULL) return gm_io_error(EINVAL);
    struct stat st;
    int r = stat(s, &st);
    int err = errno;
    free(s);
    if (r != 0) return gm_io_error(err);
#if defined(__APPLE__)
    int64_t mtime = (int64_t)st.st_mtimespec.tv_sec * 1000000000 + st.st_mtimespec.tv_nsec;
#else
    int64_t mtime = (int64_t)st.st_mtim.tv_sec * 1000000000 + st.st_mtim.tv_nsec;
#endif
    int64_t v[3] = {(int64_t)st.st_size, S_ISDIR(st.st_mode) ? 1 : 0, mtime};
    memcpy(out, v, sizeof(int64_t) * (n < 3 ? n : 3));
    return 0;
}

/* Calls f on the NUL-terminated copy of a path. */
static int64_t gm_path_call(const void *path, size_t len, int (*f)(const char *)) {
    char *s = gm_path(path, len);
    if (s == NULL) return gm_io_error(EINVAL);
    int r = f(s);
    int err = errno;
    free(s);
    return r != 0 ? gm_io_error(err) : 0;
}

static int gm_mkdir(const char *s) { return mkdir(s, 0777); }

static int64_t gm_fs_remove(const void *path, size_t len) { return gm_path_call(path, len, unlink); }
static int64_t gm_fs_make_dir(const void *path, size_t len) { return gm_path_call(path, len, gm_mkdir); }
static int64_t gm_fs_remove_dir(const void *path, size_t len) { return gm_path_call(path, len, rmdir); }

static int64_t gm_fs_rename(const void *from, size_t from_len, const void *to, size_t to_len) {
    char *a = gm_path(from, from_len), *b = gm_path(to, to_len);
    int r = a && b ? rename(a, b) : -1;
    int err = a && b ? errno : EINVAL;
    free(a);
    free(b);
    return r != 0 ? gm_io_error(err) : 0;
}

static DIR **gm_dirs;
static size_t gm_ndirs;

static int64_t gm_fs_open_dir(const void *path, size_t len) {
    char *s = gm_path(path, len);
    if (s == NULL) return gm_io_error(EINVAL);
    DIR *d = opendir(s);
    int err = errno;
    free(s);
    if (d == NULL) return gm_io_error(err);
    size_t i = 0;
    while (i < gm_ndirs && gm_dirs[i] != NULL) i++;
    if (i == gm_ndirs) {
        DIR **t = (DIR **)realloc(gm_dirs, sizeof(DIR *) * (gm_ndirs * 2 + 4));
        if (t == NULL) gm_trap("out of memory", NULL, 0, 0);
        memset(t + gm_ndirs, 0, sizeof(DIR *) * (gm_ndirs + 4));
        gm_dirs = t;
        gm_ndirs = gm_ndirs * 2 + 4;
    }
    gm_dirs[i] = d;
    return (int64_t)i + 1;
}

static DIR *gm_dir(int64_t d) { return d >= 1 && (size_t)d <= gm_ndirs ? gm_dirs[d - 1] : NULL; }

static int64_t gm_fs_next_entry(int64_t d, void *p, size_t n) {
    DIR *dir = gm_dir(d);
    if (dir == NULL) return gm_io_error(EBADF);
    for (;;) {
        errno = 0;
        struct dirent *e = readdir(dir);
        if (e == NULL) return errno ? gm_io_error(errno) : 0;
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        size_t len = strlen(e->d_name);
        if (len > n) return gm_io_error(EINVAL);
        memcpy(p, e->d_name, len);
        return (int64_t)len;
    }
}

static int64_t gm_fs_close_dir(int64_t d) {
    DIR *dir = gm_dir(d);
    if (dir == NULL) return gm_io_error(EBADF);
    gm_dirs[d - 1] = NULL;
    return GM_RET_ERR(closedir(dir) != 0);
}

/* ---- heap ------------------------------------------------------------------------ */

/* Blocks are over-allocated and aligned by hand so that any power-of-two alignment works on
 * every libc; the address of the underlying allocation is stored just before the block. */

/* In debug builds every block ever handed out is remembered: a hash table keyed by address
 * (open addressing; entries are never removed, a freed block is only marked) tells a double
 * free from a free of an address that was never a block. */
enum { GM_HB_EMPTY = 0, GM_HB_LIVE = 1, GM_HB_FREED = 2 };
typedef struct { uintptr_t addr; size_t size; uint8_t state; } gm_hblock;
static gm_hblock *gm_hb_tab;
static size_t gm_hb_cap, gm_hb_used;

static size_t gm_hb_index(uintptr_t addr, size_t cap) {
    uint64_t h = ((uint64_t)addr >> 3) * 0x9e3779b97f4a7c15ull;
    return (size_t)(h >> 29) & (cap - 1);
}

/* The slot for addr: its entry if it has one, else the empty slot where it belongs. */
static gm_hblock *gm_hb_slot(uintptr_t addr) {
    size_t i = gm_hb_index(addr, gm_hb_cap);
    while (gm_hb_tab[i].state != GM_HB_EMPTY && gm_hb_tab[i].addr != addr) i = (i + 1) & (gm_hb_cap - 1);
    return &gm_hb_tab[i];
}

static gm_hblock *gm_hb_find(uintptr_t addr) {
    if (gm_hb_cap == 0) return NULL;
    gm_hblock *s = gm_hb_slot(addr);
    return s->state == GM_HB_EMPTY ? NULL : s;
}

static void gm_hb_record(uintptr_t addr, size_t size) {
    if ((gm_hb_used + 1) * 2 > gm_hb_cap) {
        gm_hblock *old = gm_hb_tab;
        size_t old_cap = gm_hb_cap;
        size_t cap = old_cap == 0 ? 256 : old_cap * 2;
        gm_hblock *tab = (gm_hblock *)calloc(cap, sizeof(gm_hblock));
        if (tab == NULL) gm_trap("out of memory", NULL, 0, 0);
        gm_hb_tab = tab;
        gm_hb_cap = cap;
        for (size_t i = 0; i < old_cap; i++) {
            if (old[i].state != GM_HB_EMPTY) *gm_hb_slot(old[i].addr) = old[i];
        }
        free(old);
    }
    gm_hblock *s = gm_hb_slot(addr);
    if (s->state == GM_HB_EMPTY) gm_hb_used++;
    s->addr = addr;
    s->size = size;
    s->state = GM_HB_LIVE;
}

static void *gm_heap_alloc(size_t size, size_t align) {
    if (align == 0) align = 1;
    if (size > SIZE_MAX - align - sizeof(void *)) return NULL;
    uint8_t *raw = (uint8_t *)malloc(size + align + sizeof(void *));
    if (raw == NULL) return NULL;
    uintptr_t a = ((uintptr_t)raw + sizeof(void *) + (align - 1)) & ~(uintptr_t)(align - 1);
    uint8_t *p = (uint8_t *)a;
    memcpy(p - sizeof(void *), &raw, sizeof raw);
    if (gm_debug != 0) {
        memset(p, 0xAA, size);
        gm_hb_record(a, size);
    }
    return p;
}

static void gm_heap_release(void *p) {
    uint8_t *raw;
    memcpy(&raw, (uint8_t *)p - sizeof(void *), sizeof raw);
    free(raw);
}

static void gm_heap_free(void *p, size_t size, size_t align, const char *file, uint32_t line, uint32_t col) {
    (void)align;
    if (gm_debug == 0) {
        if (p != NULL) gm_heap_release(p);
        return;
    }
    char msg[160];
    gm_hblock *b = gm_hb_find((uintptr_t)p);
    if (b != NULL && b->state == GM_HB_FREED) {
        snprintf(msg, sizeof msg, "double free of heap block 0x%llx", (unsigned long long)(uintptr_t)p);
        gm_trap(msg, file, line, col);
    }
    if (b == NULL || b->size != size) {
        snprintf(msg, sizeof msg, "invalid free of 0x%llx: not the start of a live heap block of %llu bytes",
                 (unsigned long long)(uintptr_t)p, (unsigned long long)size);
        gm_trap(msg, file, line, col);
    }
    memset(p, 0xDD, size);
    b->state = GM_HB_FREED;
    gm_heap_release(p);
}

/* ---- handler contexts and non-local exits ---------------------------------------------- */

static gm_node *gm_find(gm_node *cx, uint32_t eff) {
    for (; cx; cx = cx->next) {
        if (cx->eff == eff) return cx;
    }
    return NULL;
}

/* Whether `target` is the function lexically containing the code of frame `fr`. */
static bool gm_lexically_same(gm_frame *fr, gm_frame *target) {
    for (;;) {
        if (fr == target) return true;
        if (!fr->clause_lit || !fr->up) return false;
        fr = fr->up;
    }
}

static bool gm_errdefer_runs(gm_frame *fr) {
    switch (gm_unw.kind) {
    case GM_RETURN: return gm_lexically_same(fr, gm_unw.frame) ? gm_unw.is_err : true;
    case GM_BREAK: case GM_CONTINUE: return !gm_lexically_same(fr, gm_unw.frame);
    case GM_EXIT: case GM_CANCEL: return true;
    default: return false;
    }
}

/* ---- context switching ------------------------------------------------------------------
 *
 * gm_rt_switch(&save, to, arg) saves the callee-saved registers on the current stack, stores
 * the stack pointer in `save`, loads `to` as the stack pointer, restores the registers saved
 * there and returns into that context. A new fiber's stack is prepared so that the switch
 * "returns" into gm_rt_fiber_tramp, which calls gm_fiber_entry(fiber).
 */

static void gm_fiber_entry(gm_fiber *fb);

#if defined(__x86_64__)
extern void *gm_rt_switch(void **save, void *to, void *arg) __asm__(GM_SYM("gm_rt_switch"));
extern void gm_rt_fiber_tramp(void) __asm__(GM_SYM("gm_rt_fiber_tramp"));
__asm__(
    ".text\n"
    ".p2align 4\n"
    ".globl " GM_SYM("gm_rt_switch") "\n"
    GM_SYM("gm_rt_switch") ":\n"
    "    pushq %rbp\n"
    "    pushq %rbx\n"
    "    pushq %r12\n"
    "    pushq %r13\n"
    "    pushq %r14\n"
    "    pushq %r15\n"
    "    movq %rsp, (%rdi)\n"
    "    movq %rsi, %rsp\n"
    "    popq %r15\n"
    "    popq %r14\n"
    "    popq %r13\n"
    "    popq %r12\n"
    "    popq %rbx\n"
    "    popq %rbp\n"
    "    movq %rdx, %rax\n"
    "    ret\n"
    ".p2align 4\n"
    ".globl " GM_SYM("gm_rt_fiber_tramp") "\n"
    GM_SYM("gm_rt_fiber_tramp") ":\n"
    "    movq %r12, %rdi\n"
    "    callq *%r13\n"
    "    ud2\n");

/* The initial context of a fiber whose stack ends at `top` (16-byte aligned). */
static void *gm_ctx_init(uintptr_t top, gm_fiber *fb) {
    void **sp = (void **)(top - 72);
    sp[0] = NULL;                       /* r15 */
    sp[1] = NULL;                       /* r14 */
    sp[2] = (void *)gm_fiber_entry;     /* r13 */
    sp[3] = fb;                         /* r12 */
    sp[4] = NULL;                       /* rbx */
    sp[5] = NULL;                       /* rbp */
    sp[6] = (void *)gm_rt_fiber_tramp;  /* return address */
    return sp;
}
#elif defined(__aarch64__)
extern void *gm_rt_switch(void **save, void *to, void *arg) __asm__(GM_SYM("gm_rt_switch"));
extern void gm_rt_fiber_tramp(void) __asm__(GM_SYM("gm_rt_fiber_tramp"));
__asm__(
    ".text\n"
    ".p2align 4\n"
    ".globl " GM_SYM("gm_rt_switch") "\n"
    GM_SYM("gm_rt_switch") ":\n"
    "    sub sp, sp, #160\n"
    "    stp x19, x20, [sp, #0]\n"
    "    stp x21, x22, [sp, #16]\n"
    "    stp x23, x24, [sp, #32]\n"
    "    stp x25, x26, [sp, #48]\n"
    "    stp x27, x28, [sp, #64]\n"
    "    stp x29, x30, [sp, #80]\n"
    "    stp d8, d9, [sp, #96]\n"
    "    stp d10, d11, [sp, #112]\n"
    "    stp d12, d13, [sp, #128]\n"
    "    stp d14, d15, [sp, #144]\n"
    "    mov x9, sp\n"
    "    str x9, [x0]\n"
    "    mov sp, x1\n"
    "    ldp x19, x20, [sp, #0]\n"
    "    ldp x21, x22, [sp, #16]\n"
    "    ldp x23, x24, [sp, #32]\n"
    "    ldp x25, x26, [sp, #48]\n"
    "    ldp x27, x28, [sp, #64]\n"
    "    ldp x29, x30, [sp, #80]\n"
    "    ldp d8, d9, [sp, #96]\n"
    "    ldp d10, d11, [sp, #112]\n"
    "    ldp d12, d13, [sp, #128]\n"
    "    ldp d14, d15, [sp, #144]\n"
    "    add sp, sp, #160\n"
    "    mov x0, x2\n"
    "    ret\n"
    ".p2align 4\n"
    ".globl " GM_SYM("gm_rt_fiber_tramp") "\n"
    GM_SYM("gm_rt_fiber_tramp") ":\n"
    "    mov x0, x19\n"
    "    blr x20\n"
    "    brk #0\n");

static void *gm_ctx_init(uintptr_t top, gm_fiber *fb) {
    void **sp = (void **)(top - 160);
    memset(sp, 0, 160);
    sp[0] = fb;                         /* x19 */
    sp[1] = (void *)gm_fiber_entry;     /* x20 */
    sp[11] = (void *)gm_rt_fiber_tramp; /* x30 */
    return sp;
}
#else
static void *gm_rt_switch(void **save, void *to, void *arg) {
    (void)save; (void)to; (void)arg;
    gm_trap("fibers are not supported on this architecture", NULL, 0, 0);
}
static void *gm_ctx_init(uintptr_t top, gm_fiber *fb) {
    (void)top; (void)fb;
    return NULL;
}
#endif

/* ---- fibers ---------------------------------------------------------------------------- */

static gm_fiber *gm_fiber_pool;

static _Noreturn void gm_stack_overflow(void) {
    gm_trap("stack overflow on a fiber stack", NULL, 0, 0);
}

static void gm_fiber_entry(gm_fiber *fb) {
    fb->start(fb);
    gm_fiber_finish(fb);
}

static gm_fiber *gm_fiber_new(gm_with *w, uint8_t *stack, size_t len, void (*start)(gm_fiber *), size_t arg_size,
                              const char *file, uint32_t line, uint32_t col) {
    if (gm_checks && len < GM_MIN_STACK) {
        char msg[128];
        snprintf(msg, sizeof msg, "a fiber stack needs at least %d bytes (`@min_stack`), but this one has %llu",
                 GM_MIN_STACK, (unsigned long long)len);
        gm_trap(msg, file, line, col);
    }
    gm_fiber *fb = gm_fiber_pool;
    if (fb != NULL) {
        gm_fiber_pool = fb->pool_next;
    } else {
        fb = (gm_fiber *)calloc(1, sizeof *fb);
        if (fb == NULL) gm_trap("out of memory creating a fiber", file, line, col);
    }
    uintptr_t epoch = fb->epoch + 1;
    memset(fb, 0, sizeof *fb);
    fb->epoch = epoch;
    fb->owner = w;
    fb->start = start;
    fb->state = GM_FB_NEW;
    fb->own_next = w->owned;
    w->owned = fb;
    uintptr_t top = ((uintptr_t)stack + len) & ~(uintptr_t)15;
    if (arg_size > 0) {
        top = (top - arg_size) & ~(uintptr_t)15;
        fb->arg = (void *)top;
    }
    fb->limit = (uintptr_t)stack + GM_STACK_RESERVE;
    fb->sp = gm_ctx_init(top, fb);
    return fb;
}

static void gm_susp_link(gm_with *w, gm_fiber *fb) {
    fb->state = GM_FB_SUSPENDED;
    fb->susp_with = w;
    fb->susp_prev = NULL;
    fb->susp_next = w->suspended;
    if (w->suspended) w->suspended->susp_prev = fb;
    w->suspended = fb;
    w->nsuspended++;
}

static void gm_susp_unlink(gm_with *w, gm_fiber *fb) {
    if (fb->susp_prev) fb->susp_prev->susp_next = fb->susp_next;
    else w->suspended = fb->susp_next;
    if (fb->susp_next) fb->susp_next->susp_prev = fb->susp_prev;
    fb->susp_next = fb->susp_prev = NULL;
    fb->susp_with = NULL;
    fb->state = GM_FB_RUNNING;
    w->nsuspended--;
}

static gm_cont gm_fiber_cont(gm_with *w, gm_fiber *fb) {
    fb->epoch++;
    gm_susp_link(w, fb);
    return (gm_cont){fb, fb->epoch};
}

static void gm_with_go(gm_with *w, gm_fiber *fb) {
    gm_fiber *me = gm_cur_fiber;
    gm_cur_fiber = fb;
    fb->state = GM_FB_RUNNING;
    fb->started = 1;
    w->event = 0;
    gm_rt_switch(&w->driver_sp, fb->sp, NULL);
    gm_cur_fiber = me;
}

static void gm_with_clause(gm_with *w) {
    gm_fiber *fb = w->event_fiber;
    fb->thunk(fb->req, (gm_cont){fb, fb->epoch});
}

static void gm_ctl_perform(gm_with *w, gm_clause_thunk thunk, void *req, void *res) {
    gm_fiber *fb = gm_cur_fiber;
    if (fb == NULL || w == NULL) gm_trap("internal error: a `ctl` operation performed outside a fiber", NULL, 0, 0);
    fb->thunk = thunk;
    fb->req = req;
    fb->res = res;
    fb->epoch++;
    gm_susp_link(w, fb);
    w->event = GM_EV_PERFORM;
    w->event_fiber = fb;
    gm_rt_switch(&fb->sp, w->driver_sp, NULL);
    if (fb->cancel) {
        fb->cancel = 0;
        gm_unw.kind = GM_CANCEL;
        gm_unw.frame = NULL;
        gm_unw.label = 0;
        gm_unw.is_err = 0;
    }
}

static _Noreturn void gm_fiber_finish(gm_fiber *fb) {
    gm_with *w = fb->owner;
    fb->state = GM_FB_DONE;
    w->event = GM_EV_DONE;
    w->event_fiber = fb;
    gm_rt_switch(&fb->sp, w->driver_sp, NULL);
    abort();
}

/* Checks continuation *k for consumption by a clause of `w` and takes it off w's list. */
static gm_fiber *gm_cont_take(gm_with *w, const void *k, const char *file, uint32_t line, uint32_t col) {
    gm_cont c;
    memcpy(&c, k, sizeof c);
    gm_fiber *fb = c.fiber;
    if (fb == NULL || fb->epoch != c.epoch || fb->state != GM_FB_SUSPENDED) {
        gm_trap("this continuation was already consumed (§14.2)", file, line, col);
    }
    if (fb->susp_with != w) {
        gm_trap("a continuation can be consumed only by a clause of the `with` that produced it (§14.2)", file, line,
                col);
    }
    gm_susp_unlink(w, fb);
    return fb;
}

static void *gm_resume(gm_with *w, const void *k, const char *file, uint32_t line, uint32_t col) {
    gm_fiber *fb = gm_cont_take(w, k, file, line, col);
    w->next = fb;
    return fb->res;
}

/* Cancels fiber `fb` (no longer on any list): it unwinds to its base, running its defers. */
static void gm_cancel(gm_with *w, gm_fiber *fb, const char *file, uint32_t line, uint32_t col) {
    if (!fb->started) {
        fb->state = GM_FB_DONE;
        return;
    }
    gm_unwind saved = gm_unw;
    gm_unw.kind = GM_NONE;
    fb->cancel = 1;
    gm_with_go(w, fb);
    if (w->event != GM_EV_DONE) {
        gm_trap("a `ctl` operation was performed while a fiber was being discarded", file, line, col);
    }
    /* An exit started by a deferred statement of the fiber goes on; otherwise the code
     * that discarded the fiber continues. */
    if (gm_unw.kind == GM_CANCEL || gm_unw.kind == GM_NONE) gm_unw = saved;
}

static void gm_discard(gm_with *w, const void *k, const char *file, uint32_t line, uint32_t col) {
    gm_fiber *fb = gm_cont_take(w, k, file, line, col);
    gm_cancel(w, fb, file, line, col);
}

static void gm_with_end(gm_with *w, bool crossing, const char *file, uint32_t line, uint32_t col) {
    if (gm_unw.kind != GM_EXIT) {
        if (crossing) {
            gm_unwind saved = gm_unw;
            while (w->suspended) {
                gm_fiber *fb = w->suspended;
                gm_susp_unlink(w, fb);
                gm_cancel(w, fb, file, line, col);
            }
            gm_unw = saved;
        } else if (w->nsuspended > 0) {
            char msg[96];
            snprintf(msg, sizeof msg, "with completed while %u fiber%s suspended", (unsigned)w->nsuspended,
                     w->nsuspended == 1 ? " is" : "s are");
            gm_trap(msg, file, line, col);
        }
    }
    while (w->owned) {
        gm_fiber *fb = w->owned;
        w->owned = fb->own_next;
        fb->state = GM_FB_DONE;
        fb->pool_next = gm_fiber_pool;
        gm_fiber_pool = fb;
    }
}

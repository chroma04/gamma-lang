/* The runtime of programs compiled by the Gamma C back end: implementations of the
 * declarations in gm_rt.h. The compiler puts gm_rt.h before this file (and the generated
 * code after it, unless GM_RT_SEPARATE is defined), so this file includes nothing itself.
 *
 * Observable behavior (trap output, value display, heap diagnostics) mirrors the reference
 * interpreter byte for byte.
 */

/* Definitions of the variables declared in gm_rt.h (gm_checks is the generated code's). */
GM_API _Thread_local gm_bt *gm_bt_top;
#ifdef _WIN32
static SRWLOCK gm_rt_lock = SRWLOCK_INIT;
GM_API void gm_rt_acquire(void) { AcquireSRWLockExclusive(&gm_rt_lock); }
GM_API void gm_rt_release(void) { ReleaseSRWLockExclusive(&gm_rt_lock); }
#else
static pthread_mutex_t gm_rt_lock = PTHREAD_MUTEX_INITIALIZER;
GM_API void gm_rt_acquire(void) { pthread_mutex_lock(&gm_rt_lock); }
GM_API void gm_rt_release(void) { pthread_mutex_unlock(&gm_rt_lock); }
#endif
GM_API int gm_debug = 1;
GM_API void *(*volatile gm_poison)(void *, int, size_t) = memset;
GM_API _Thread_local gm_unwind gm_unw;
GM_API _Thread_local gm_fiber *gm_cur_fiber;
GM_API _Thread_local uintptr_t gm_stack_limit;
GM_API _Thread_local uintptr_t gm_thread_stack_limit;

/* ---- traps ---------------------------------------------------------------- */

/* Prints the callers in the backtrace chain; past GM_BT_MAX of them, only the first and
 * last GM_BT_MAX / 2, with a line that does not count the calls in between (the depth that
 * a stack overflow reaches differs between back ends). */
#define GM_BT_MAX 20
static void gm_backtrace(void) {
    size_t n = 0;
    if (gm_bt_top == NULL) return;
    for (gm_bt *b = gm_bt_top->prev; b != NULL; b = b->prev)
        if (b->fn != NULL && b->file != NULL) n++;
    size_t i = 0;
    for (gm_bt *b = gm_bt_top->prev; b != NULL; b = b->prev) {
        if (b->fn == NULL || b->file == NULL) continue;
        if (n <= GM_BT_MAX || i < GM_BT_MAX / 2 || i >= n - GM_BT_MAX / 2)
            fprintf(stderr, "  called from %s:%u:%u in %s\n", b->file, (unsigned)b->line, (unsigned)b->col, b->fn);
        else if (i == GM_BT_MAX / 2)
            fputs("  ... more calls\n", stderr);
        i++;
    }
}

/* Set while a stack overflow is reported: the report then ends the process without the
 * C library's exit processing, which needs stack the overflowed thread may not have. */
static _Thread_local int gm_overflowing;

GM_API _Noreturn void gm_trap_bytes(const uint8_t *msg, size_t len, const char *file, uint32_t line, uint32_t col) {
    gm_flush();
    fputs("trap: ", stderr);
    if (len > 0) fwrite(msg, 1, len, stderr);
    fputc('\n', stderr);
    if (file != NULL) {
        fprintf(stderr, "  at %s:%u:%u", file, (unsigned)line, (unsigned)col);
        if (gm_bt_top != NULL && gm_bt_top->fn != NULL) fprintf(stderr, " in %s", gm_bt_top->fn);
        fputc('\n', stderr);
    }
    gm_backtrace();
    fflush(stderr);
    /* The status of a process terminated by `abort` (SIGABRT). */
#ifdef _WIN32
    if (gm_overflowing) TerminateProcess(GetCurrentProcess(), 134);
#endif
    exit(134);
}

GM_API _Noreturn void gm_trap(const char *msg, const char *file, uint32_t line, uint32_t col) {
    gm_trap_bytes((const uint8_t *)msg, strlen(msg), file, line, col);
}

GM_API _Noreturn void gm_trap_misaligned(uintptr_t addr, unsigned align, const char *file, uint32_t line, uint32_t col) {
    char m[128];
    snprintf(m, sizeof m, "misaligned pointer cast: address 0x%llx is not aligned to %u bytes",
             (unsigned long long)addr, align);
    gm_trap(m, file, line, col);
}

/* ---- growable byte buffer --------------------------------------------------- */

GM_API void gm_buf_push(gm_buf *b, const void *data, size_t len) {
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

GM_API void gm_buf_free(gm_buf *b) {
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

/* ---- integers wider than 128 bits ------------------------------------------------- */

#define GM_WORDS(bits) (((size_t)(bits) + 63) / 64)
/* Scratch space of up to GM_WSTACK words per array lives on the stack; more uses the heap. */
#define GM_WSTACK 64

static uint64_t *gm_wscratch(uint64_t *stack, size_t cap, size_t words) {
    if (words <= cap) return stack;
    uint64_t *p = (uint64_t *)malloc(words * 8);
    if (p == NULL) gm_trap("out of memory", NULL, 0, 0);
    return p;
}

static void gm_wscratch_free(uint64_t *p, uint64_t *stack) {
    if (p != stack) free(p);
}

/* Word i of the n-word integer a, with the padding of the top word replaced by the extension. */
static uint64_t gm_wget(const uint8_t *a, size_t i, size_t n, uint32_t bits, bool sgn) {
    uint64_t w;
    memcpy(&w, a + i * 8, 8);
    unsigned k = bits & 63;
    if (i == n - 1 && k != 0) {
        if (sgn && ((w >> (k - 1)) & 1) != 0) w |= ~(uint64_t)0 << k;
        else w &= ((uint64_t)1 << k) - 1;
    }
    return w;
}

static void gm_wput(uint8_t *r, size_t i, uint64_t w) { memcpy(r + i * 8, &w, 8); }

/* Normalizes the padding of r. */
static void gm_wfix(uint8_t *r, uint32_t bits, bool sgn) {
    size_t n = GM_WORDS(bits);
    gm_wput(r, n - 1, gm_wget(r, n - 1, n, bits, sgn));
}

/* Byte k (counting from the least significant) of a wider-than-128-bit integer. */
#define gm_wbp(p, k) gm_ibp((p), 24, (k))

static bool gm_wbit(const uint8_t *a, uint32_t i) { return ((*gm_wbp(a, i / 8) >> (i % 8)) & 1) != 0; }

static bool gm_wisneg(const uint8_t *a, uint32_t bits, bool sgn) { return sgn && gm_wbit(a, bits - 1); }

/* The signed top word w of a sum, which was computed in 64 bits, does not fit k bits. */
static bool gm_wtop_bad(uint64_t w, unsigned k) {
    if (k == 0) return false;
    int64_t t = (int64_t)w >> (k - 1);
    return t != 0 && t != -1;
}

/* Number of significant bits of the value (of its complement if inv). */
static uint32_t gm_wbitlen(const uint8_t *a, uint32_t bits, bool sgn, bool inv) {
    size_t n = GM_WORDS(bits);
    for (size_t i = n; i > 0; i--) {
        uint64_t w = gm_wget(a, i - 1, n, bits, sgn);
        if (inv) w = ~w;
        if (w != 0) return (uint32_t)((i - 1) * 64 + 64 - (unsigned)__builtin_clzll(w));
    }
    return 0;
}

GM_API bool gm_wadd(uint8_t *r, const uint8_t *a, const uint8_t *b, uint32_t bits, bool sgn) {
    size_t n = GM_WORDS(bits);
    unsigned k = bits & 63;
    uint64_t carry = 0, x = 0, y = 0, s = 0;
    for (size_t i = 0; i < n; i++) {
        x = gm_wget(a, i, n, bits, sgn);
        y = gm_wget(b, i, n, bits, sgn);
        s = x + y;
        uint64_t c1 = s < x;
        s += carry;
        carry = c1 | (s < carry);
        gm_wput(r, i, s);
    }
    bool ovf = sgn ? ((((x ^ s) & (y ^ s)) >> 63) != 0 || gm_wtop_bad(s, k)) : (carry != 0 || (k != 0 && (s >> k) != 0));
    gm_wfix(r, bits, sgn);
    return ovf;
}

GM_API bool gm_wsub(uint8_t *r, const uint8_t *a, const uint8_t *b, uint32_t bits, bool sgn) {
    size_t n = GM_WORDS(bits);
    unsigned k = bits & 63;
    uint64_t borrow = 0, x = 0, y = 0, s = 0;
    for (size_t i = 0; i < n; i++) {
        x = gm_wget(a, i, n, bits, sgn);
        y = gm_wget(b, i, n, bits, sgn);
        s = x - y;
        uint64_t b1 = x < y;
        uint64_t t = s;
        s -= borrow;
        borrow = b1 | (t < borrow);
        gm_wput(r, i, s);
    }
    bool ovf = sgn ? ((((x ^ y) & (x ^ s)) >> 63) != 0 || gm_wtop_bad(s, k)) : borrow != 0;
    gm_wfix(r, bits, sgn);
    return ovf;
}

GM_API bool gm_wneg(uint8_t *r, const uint8_t *a, uint32_t bits, bool sgn) {
    size_t n = GM_WORDS(bits);
    unsigned k = bits & 63;
    uint64_t borrow = 0, x = 0, s = 0;
    bool nonzero = false;
    for (size_t i = 0; i < n; i++) {
        x = gm_wget(a, i, n, bits, sgn);
        nonzero = nonzero || x != 0;
        s = (uint64_t)0 - x - borrow;
        borrow = (x != 0) | borrow;
        gm_wput(r, i, s);
    }
    bool ovf = sgn ? (((x & s) >> 63) != 0 || gm_wtop_bad(s, k)) : nonzero;
    gm_wfix(r, bits, sgn);
    return ovf;
}

/* Loads the canonical value of a into the n words w; returns whether it is negative. */
static bool gm_wload(uint64_t *w, const uint8_t *a, uint32_t bits, bool sgn) {
    size_t n = GM_WORDS(bits);
    for (size_t i = 0; i < n; i++) w[i] = gm_wget(a, i, n, bits, sgn);
    return gm_wisneg(a, bits, sgn);
}

static void gm_wneg_words(uint64_t *w, size_t n) {
    uint64_t borrow = 0;
    for (size_t i = 0; i < n; i++) {
        uint64_t x = w[i];
        w[i] = (uint64_t)0 - x - borrow;
        borrow = (x != 0) | borrow;
    }
}

static void gm_wstore(uint8_t *r, const uint64_t *w, uint32_t bits, bool sgn) {
    size_t n = GM_WORDS(bits);
    for (size_t i = 0; i < n; i++) gm_wput(r, i, w[i]);
    gm_wfix(r, bits, sgn);
}

static uint32_t gm_wwords_bitlen(const uint64_t *w, size_t n) {
    for (size_t i = n; i > 0; i--) {
        if (w[i - 1] != 0) return (uint32_t)((i - 1) * 64 + 64 - (unsigned)__builtin_clzll(w[i - 1]));
    }
    return 0;
}

static bool gm_wwords_zero(const uint64_t *w, size_t n) { return gm_wwords_bitlen(w, n) == 0; }

GM_API bool gm_wmul(uint8_t *r, const uint8_t *a, const uint8_t *b, uint32_t bits, bool sgn) {
    size_t n = GM_WORDS(bits);
    uint64_t stack[4 * GM_WSTACK];
    uint64_t *buf = gm_wscratch(stack, 4 * GM_WSTACK, 4 * n);
    uint64_t *A = buf, *B = buf + n, *P = buf + 2 * n;
    bool na = gm_wload(A, a, bits, sgn), nb = gm_wload(B, b, bits, sgn);
    if (na) gm_wneg_words(A, n);
    if (nb) gm_wneg_words(B, n);
    memset(P, 0, 2 * n * 8);
    for (size_t i = 0; i < n; i++) {
        unsigned __int128 carry = 0;
        for (size_t j = 0; j < n; j++) {
            unsigned __int128 cur = (unsigned __int128)A[i] * B[j] + P[i + j] + carry;
            P[i + j] = (uint64_t)cur;
            carry = cur >> 64;
        }
        P[i + n] = (uint64_t)carry;
    }
    bool neg = na != nb;
    /* The magnitude P must fit: below 2^bits (unsigned), below 2^(bits-1), or equal to it if
     * the result is negative (signed). */
    bool ovf = !gm_wwords_zero(P + n, n);
    uint32_t len = gm_wwords_bitlen(P, n);
    if (!sgn) {
        ovf = ovf || len > bits;
    } else if (len > bits) {
        ovf = true;
    } else if (len == bits) {
        bool pow2 = true;
        for (size_t i = 0; i < n && pow2; i++) {
            uint64_t w = P[i];
            pow2 = (i == (len - 1) / 64) ? w == (uint64_t)1 << ((len - 1) % 64) : w == 0;
        }
        ovf = ovf || !(neg && pow2);
    }
    if (neg) gm_wneg_words(P, n);
    gm_wstore(r, P, bits, sgn);
    gm_wscratch_free(buf, stack);
    return ovf;
}

/* q = a / b and rm = a % b for n-word unsigned numbers (b != 0). */
static void gm_wudivmod(uint64_t *q, uint64_t *rm, const uint64_t *a, const uint64_t *b, size_t n) {
    memset(q, 0, n * 8);
    memset(rm, 0, n * 8);
    size_t bn = n;
    while (bn > 1 && b[bn - 1] == 0) bn--;
    if (bn == 1) {
        unsigned __int128 rem = 0;
        for (size_t i = n; i > 0; i--) {
            unsigned __int128 cur = (rem << 64) | a[i - 1];
            q[i - 1] = (uint64_t)(cur / b[0]);
            rem = cur % b[0];
        }
        rm[0] = (uint64_t)rem;
        return;
    }
    for (uint32_t i = gm_wwords_bitlen(a, n); i > 0; i--) {
        uint32_t bit = i - 1;
        uint64_t top = rm[n - 1] >> 63;
        for (size_t j = n - 1; j > 0; j--) rm[j] = (rm[j] << 1) | (rm[j - 1] >> 63);
        rm[0] = (rm[0] << 1) | ((a[bit / 64] >> (bit % 64)) & 1);
        bool ge = top != 0;
        for (size_t j = n; !ge && j > 0; j--) {
            if (rm[j - 1] != b[j - 1]) {
                ge = rm[j - 1] > b[j - 1];
                break;
            }
            if (j == 1) ge = true;
        }
        if (ge) {
            uint64_t borrow = 0;
            for (size_t j = 0; j < n; j++) {
                uint64_t x = rm[j], y = b[j];
                uint64_t d = x - y;
                uint64_t b1 = x < y;
                uint64_t t = d;
                d -= borrow;
                borrow = b1 | (t < borrow);
                rm[j] = d;
            }
            q[bit / 64] |= (uint64_t)1 << (bit % 64);
        }
    }
}

GM_API int gm_wdiv(uint8_t *q, uint8_t *rm, const uint8_t *a, const uint8_t *b, uint32_t bits, bool sgn) {
    size_t n = GM_WORDS(bits);
    uint64_t stack[4 * GM_WSTACK];
    uint64_t *buf = gm_wscratch(stack, 4 * GM_WSTACK, 4 * n);
    uint64_t *A = buf, *B = buf + n, *Q = buf + 2 * n, *R = buf + 3 * n;
    bool na = gm_wload(A, a, bits, sgn), nb = gm_wload(B, b, bits, sgn);
    int status = 0;
    if (gm_wwords_zero(B, n)) {
        status = 1;
    } else {
        if (na) gm_wneg_words(A, n);
        if (nb) gm_wneg_words(B, n);
        /* MIN / -1: the magnitude of the quotient is 2^(bits-1) and the signs agree. */
        if (sgn && na && nb && gm_wwords_bitlen(B, n) == 1 && gm_wwords_bitlen(A, n) == bits) {
            bool pow2 = true;
            for (size_t i = 0; i < n && pow2; i++) pow2 = (i == (bits - 1) / 64) ? A[i] == (uint64_t)1 << ((bits - 1) % 64) : A[i] == 0;
            if (pow2) status = 2;
        }
    }
    if (status == 0) {
        gm_wudivmod(Q, R, A, B, n);
        if (na != nb) gm_wneg_words(Q, n);
        if (na) gm_wneg_words(R, n);
        if (q != NULL) gm_wstore(q, Q, bits, sgn);
        if (rm != NULL) gm_wstore(rm, R, bits, sgn);
    }
    gm_wscratch_free(buf, stack);
    return status;
}

GM_API bool gm_wpow(uint8_t *r, const uint8_t *a, uint32_t n, uint32_t bits, bool sgn) {
    size_t sz = GM_WBYTES(bits);
    uint64_t stack[2 * GM_WSTACK];
    uint64_t *buf = gm_wscratch(stack, 2 * GM_WSTACK, 2 * (sz / 8));
    uint8_t *res = (uint8_t *)buf, *base = (uint8_t *)buf + sz;
    memset(res, 0, sz);
    gm_wput(res, 0, 1);
    memcpy(base, a, sz);
    gm_wfix(base, bits, sgn);
    bool ovf = false;
    while (n > 0) {
        if (n & 1) ovf = gm_wmul(res, res, base, bits, sgn) || ovf;
        n >>= 1;
        if (n > 0) ovf = gm_wmul(base, base, base, bits, sgn) || ovf;
    }
    memcpy(r, res, sz);
    gm_wscratch_free(buf, stack);
    return ovf;
}

GM_API void gm_wnot(uint8_t *r, const uint8_t *a, uint32_t bits, bool sgn) {
    size_t n = GM_WORDS(bits);
    for (size_t i = 0; i < n; i++) gm_wput(r, i, ~gm_wget(a, i, n, bits, sgn));
    gm_wfix(r, bits, sgn);
}

GM_API void gm_wand(uint8_t *r, const uint8_t *a, const uint8_t *b, uint32_t bits, bool sgn) {
    size_t n = GM_WORDS(bits);
    for (size_t i = 0; i < n; i++) gm_wput(r, i, gm_wget(a, i, n, bits, sgn) & gm_wget(b, i, n, bits, sgn));
    gm_wfix(r, bits, sgn);
}

GM_API void gm_wor(uint8_t *r, const uint8_t *a, const uint8_t *b, uint32_t bits, bool sgn) {
    size_t n = GM_WORDS(bits);
    for (size_t i = 0; i < n; i++) gm_wput(r, i, gm_wget(a, i, n, bits, sgn) | gm_wget(b, i, n, bits, sgn));
    gm_wfix(r, bits, sgn);
}

GM_API void gm_wxor(uint8_t *r, const uint8_t *a, const uint8_t *b, uint32_t bits, bool sgn) {
    size_t n = GM_WORDS(bits);
    for (size_t i = 0; i < n; i++) gm_wput(r, i, gm_wget(a, i, n, bits, sgn) ^ gm_wget(b, i, n, bits, sgn));
    gm_wfix(r, bits, sgn);
}

GM_API bool gm_wshl(uint8_t *r, const uint8_t *a, uint32_t sh, uint32_t bits, bool sgn) {
    size_t n = GM_WORDS(bits);
    /* The value must still fit in the bits that remain. */
    uint32_t need = sgn ? gm_wbitlen(a, bits, true, gm_wisneg(a, bits, sgn)) + 1 : gm_wbitlen(a, bits, false, false);
    bool ovf = need > bits - sh;
    size_t ws = sh / 64;
    unsigned bs = sh % 64;
    for (size_t i = n; i > 0; i--) {
        size_t d = i - 1;
        uint64_t v = 0;
        if (d >= ws) {
            v = gm_wget(a, d - ws, n, bits, sgn) << bs;
            if (bs != 0 && d > ws) v |= gm_wget(a, d - ws - 1, n, bits, sgn) >> (64 - bs);
        }
        gm_wput(r, d, v);
    }
    gm_wfix(r, bits, sgn);
    return ovf;
}

GM_API void gm_wshr(uint8_t *r, const uint8_t *a, uint32_t sh, uint32_t bits, bool sgn) {
    size_t n = GM_WORDS(bits);
    uint64_t fill = gm_wisneg(a, bits, sgn) ? ~(uint64_t)0 : 0;
    size_t ws = sh / 64;
    unsigned bs = sh % 64;
    for (size_t i = 0; i < n; i++) {
        uint64_t lo = i + ws < n ? gm_wget(a, i + ws, n, bits, sgn) : fill;
        uint64_t hi = i + ws + 1 < n ? gm_wget(a, i + ws + 1, n, bits, sgn) : fill;
        gm_wput(r, i, bs != 0 ? (lo >> bs) | (hi << (64 - bs)) : lo);
    }
    gm_wfix(r, bits, sgn);
}

static void gm_fmt_int(gm_buf *out, const gm_type *t, const uint8_t *p);

/* The storage size of an integer of at most 128 bits. */
static size_t gm_isize(uint32_t bits) { return bits <= 8 ? 1 : bits <= 16 ? 2 : bits <= 32 ? 4 : bits <= 64 ? 8 : 16; }

/* The amounts the shift routines are given are unsigned integers of any width: those of at
 * most 128 bits are ordinary integers, the others wider-than-128-bit ones. */
GM_API uint64_t gm_wshamt(const uint8_t *amt, uint32_t abits) {
    if (abits <= 128) {
        unsigned __int128 v = gm_load_u128(amt, gm_isize(abits));
        if (abits < 128) v &= ((unsigned __int128)1 << abits) - 1;
        return v > UINT64_MAX ? UINT64_MAX : (uint64_t)v;
    }
    size_t n = GM_WORDS(abits);
    for (size_t i = 1; i < n; i++) {
        if (gm_wget(amt, i, n, abits, false) != 0) return UINT64_MAX;
    }
    return gm_wget(amt, 0, n, abits, false);
}

GM_API _Noreturn void gm_trap_shift(const uint8_t *amt, uint32_t abits, const char *ty, const char *file,
                                    uint32_t line, uint32_t col) {
    gm_buf m = {0};
    gm_buf_push(&m, "shift amount ", 13);
    if (abits <= 128) {
        gm_type t = {0};
        t.kind = GM_K_INT, t.bits = abits, t.size = (uint32_t)gm_isize(abits);
        gm_fmt_int(&m, &t, amt);
    } else {
        gm_wfmt(&m, amt, abits, false);
    }
    gm_buf_push(&m, " too large for ", 15);
    gm_buf_push(&m, ty, strlen(ty));
    gm_trap_bytes(m.data, m.len, file, line, col);
}

GM_API _Noreturn void gm_trap_pow(const uint8_t *base, uint32_t bits, bool sgn, uint32_t n, const char *file,
                                  uint32_t line, uint32_t col) {
    gm_buf m = {0};
    gm_buf_push(&m, "integer overflow in `pow` (", 27);
    if (bits <= 128) {
        gm_type t = {0};
        t.kind = GM_K_INT, t.bits = (uint16_t)bits, t.is_signed = sgn, t.size = (uint32_t)gm_isize(bits);
        gm_fmt_int(&m, &t, base);
    } else {
        gm_wfmt(&m, base, bits, sgn);
    }
    char s[16];
    int k = snprintf(s, sizeof s, "^%u)", (unsigned)n);
    gm_buf_push(&m, s, (size_t)k);
    gm_trap_bytes(m.data, m.len, file, line, col);
}

GM_API int gm_wcmp(const uint8_t *a, const uint8_t *b, uint32_t bits, bool sgn) {
    size_t n = GM_WORDS(bits);
    for (size_t i = n; i > 0; i--) {
        uint64_t x = gm_wget(a, i - 1, n, bits, sgn), y = gm_wget(b, i - 1, n, bits, sgn);
        if (x == y) continue;
        if (sgn && i == n) return (int64_t)x < (int64_t)y ? -1 : 1;
        return x < y ? -1 : 1;
    }
    return 0;
}

GM_API int gm_wsign(const uint8_t *a, uint32_t bits, bool sgn) {
    if (gm_wisneg(a, bits, sgn)) return -1;
    return gm_wbitlen(a, bits, sgn, false) == 0 ? 0 : 1;
}

GM_API void gm_wmin(uint8_t *r, uint32_t bits, bool sgn) {
    memset(r, 0, GM_WBYTES(bits));
    if (sgn) *gm_wbp(r, (bits - 1) / 8) |= (uint8_t)(1u << ((bits - 1) % 8));
    gm_wfix(r, bits, sgn);
}

GM_API void gm_wmax(uint8_t *r, uint32_t bits, bool sgn) {
    memset(r, 0xff, GM_WBYTES(bits));
    if (sgn) *gm_wbp(r, (bits - 1) / 8) &= (uint8_t)~(1u << ((bits - 1) % 8));
    gm_wfix(r, bits, sgn);
}

/* Byte i (from the least significant) of the infinite sign or zero extension of the
 * `bits`-bit integer a of `size` bytes. */
static uint8_t gm_wbyte(const uint8_t *a, size_t size, uint32_t bits, bool neg, size_t i) {
    uint8_t fill = neg ? 0xff : 0;
    size_t lo = i * 8;
    if (lo >= bits) return fill;
    uint8_t v = *gm_ibp(a, size, i);
    if (lo + 8 > bits) {
        unsigned k = (unsigned)(bits - lo);
        uint8_t mask = (uint8_t)((1u << k) - 1);
        v = (uint8_t)((v & mask) | (fill & (uint8_t)~mask));
    }
    return v;
}

GM_API int gm_wconv(uint8_t *r, size_t rsize, uint32_t rbits, bool rsgn, const uint8_t *a, size_t asize,
                    uint32_t abits, bool asgn) {
    bool neg = asgn && ((*gm_ibp(a, asize, (abits - 1) / 8) >> ((abits - 1) % 8)) & 1);
    for (size_t i = 0; i < rsize; i++) *gm_ibp(r, rsize, i) = gm_wbyte(a, asize, abits, neg, i);
    /* Wrap: the bits at and above rbits repeat the sign (or are zero). */
    bool rneg = rsgn && ((*gm_ibp(r, rsize, (rbits - 1) / 8) >> ((rbits - 1) % 8)) & 1);
    for (size_t i = rbits / 8; i < rsize; i++) {
        size_t lo = i * 8;
        uint8_t *rb = gm_ibp(r, rsize, i);
        if (lo >= rbits) {
            *rb = rneg ? 0xff : 0;
        } else if (lo + 8 > rbits) {
            uint8_t mask = (uint8_t)((1u << (rbits - lo)) - 1);
            *rb = (uint8_t)((*rb & mask) | (rneg ? (uint8_t)~mask : 0));
        }
    }
    /* Fits: every bit of the value from rbits (unsigned) or rbits - 1 (signed) up is the sign. */
    bool fits = !(neg && !rsgn);
    size_t lo = rbits - (rsgn ? 1 : 0);
    size_t total = (rsize > asize ? rsize : asize) + 1;
    uint8_t want = rsgn && neg ? 0xff : 0;
    for (size_t i = lo / 8; i < total && fits; i++) {
        uint8_t mask = i == lo / 8 ? (uint8_t)(0xffu << (lo % 8)) : 0xff;
        if ((gm_wbyte(a, asize, abits, neg, i) & mask) != (want & mask)) fits = false;
    }
    return (fits ? 1 : 0) | (neg ? 2 : 0);
}

GM_API double gm_wtof(const uint8_t *a, uint32_t bits, bool sgn, unsigned mant) {
    size_t n = GM_WORDS(bits);
    uint64_t stack[GM_WSTACK];
    uint64_t *m = gm_wscratch(stack, GM_WSTACK, n);
    bool neg = gm_wload(m, a, bits, sgn);
    if (neg) gm_wneg_words(m, n);
    uint32_t len = gm_wwords_bitlen(m, n);
    double result = 0;
    if (len != 0) {
        uint32_t shift = len > mant ? len - mant : 0;
        uint64_t top = 0;
        for (uint32_t t = 0; t < len - shift; t++) {
            uint32_t bit = shift + t;
            top |= ((m[bit / 64] >> (bit % 64)) & 1) << t;
        }
        if (shift > 0) {
            uint32_t rb = shift - 1;
            bool round = ((m[rb / 64] >> (rb % 64)) & 1) != 0, sticky = false;
            for (uint32_t i = 0; i < rb && !sticky; i += 64) {
                uint64_t w = m[i / 64];
                if (rb - i < 64) w &= ((uint64_t)1 << (rb - i)) - 1;
                sticky = w != 0;
            }
            if (round && (sticky || (top & 1) != 0)) top++;
        }
        result = __builtin_ldexp((double)top, (int)(shift > 70000 ? 70000 : shift));
    }
    gm_wscratch_free(m, stack);
    return neg ? -result : result;
}

GM_API int gm_wfromd(uint8_t *r, uint32_t bits, bool sgn, double x) {
    size_t sz = GM_WBYTES(bits);
    memset(r, 0, sz);
    if (x != x) return 3;
    if (x == 0) return 0;
    bool neg = x < 0;
    double ax = neg ? -x : x;
    if (ax > 1.7976931348623157e308) return neg ? 1 : 2;
    int e;
    double f = __builtin_frexp(ax, &e); /* ax = f * 2^e with f in [0.5, 1): ax has e significant bits */
    uint64_t m = (uint64_t)__builtin_ldexp(f, 53);
    if (neg && !sgn) return 1;
    if (neg) {
        /* Down to -2^(bits-1). */
        if ((uint32_t)e > bits || ((uint32_t)e == bits && m != (uint64_t)1 << 52)) return 1;
    } else if ((uint32_t)e > (sgn ? bits - 1 : bits)) {
        return 2;
    }
    int shift = e - 53;
    for (int t = 0; t < 53; t++) {
        if (((m >> t) & 1) == 0) continue;
        int pos = shift + t;
        if (pos >= 0 && (size_t)pos < sz * 8) *gm_wbp(r, (size_t)pos / 8) |= (uint8_t)(1u << (pos % 8));
    }
    if (neg) {
        uint64_t stack[GM_WSTACK];
        size_t n = GM_WORDS(bits);
        uint64_t *w = gm_wscratch(stack, GM_WSTACK, n);
        gm_wload(w, r, bits, false);
        gm_wneg_words(w, n);
        gm_wstore(r, w, bits, sgn);
        gm_wscratch_free(w, stack);
    }
    return 0;
}

GM_API uint32_t gm_wclz(const uint8_t *a, uint32_t bits) { return bits - gm_wbitlen(a, bits, false, false); }

GM_API uint32_t gm_wctz(const uint8_t *a, uint32_t bits) {
    size_t n = GM_WORDS(bits);
    for (size_t i = 0; i < n; i++) {
        uint64_t w = gm_wget(a, i, n, bits, false);
        if (w != 0) return (uint32_t)(i * 64 + (unsigned)__builtin_ctzll(w));
    }
    return bits;
}

GM_API uint32_t gm_wpopcount(const uint8_t *a, uint32_t bits) {
    size_t n = GM_WORDS(bits);
    uint32_t c = 0;
    for (size_t i = 0; i < n; i++) c += (uint32_t)__builtin_popcountll(gm_wget(a, i, n, bits, false));
    return c;
}

GM_API void gm_wbswap(uint8_t *r, const uint8_t *a, uint32_t bits, bool sgn) {
    size_t n = bits / 8, sz = GM_WBYTES(bits);
    uint64_t stack[GM_WSTACK];
    uint64_t *t = gm_wscratch(stack, GM_WSTACK, sz / 8);
    uint8_t *tb = (uint8_t *)t;
    memset(tb, 0, sz);
    for (size_t i = 0; i < n; i++) *gm_wbp(tb, i) = *gm_wbp(a, n - 1 - i);
    memcpy(r, tb, sz);
    gm_wfix(r, bits, sgn);
    gm_wscratch_free(t, stack);
}

GM_API void gm_wbitreverse(uint8_t *r, const uint8_t *a, uint32_t bits, bool sgn) {
    size_t sz = GM_WBYTES(bits);
    uint64_t stack[GM_WSTACK];
    uint64_t *t = gm_wscratch(stack, GM_WSTACK, sz / 8);
    uint8_t *tb = (uint8_t *)t;
    memset(tb, 0, sz);
    for (uint32_t i = 0; i < bits; i++) {
        if (gm_wbit(a, i)) *gm_wbp(tb, (bits - 1 - i) / 8) |= (uint8_t)(1u << ((bits - 1 - i) % 8));
    }
    memcpy(r, tb, sz);
    gm_wfix(r, bits, sgn);
    gm_wscratch_free(t, stack);
}

GM_API void gm_wrotl(uint8_t *r, const uint8_t *a, uint32_t bits, bool sgn, uint64_t k) {
    size_t sz = GM_WBYTES(bits);
    uint64_t stack[GM_WSTACK];
    uint64_t *t = gm_wscratch(stack, GM_WSTACK, sz / 8);
    uint8_t *tb = (uint8_t *)t;
    memset(tb, 0, sz);
    k %= bits;
    for (uint32_t i = 0; i < bits; i++) {
        uint32_t j = (uint32_t)((i + k) % bits);
        if (gm_wbit(a, i)) *gm_wbp(tb, j / 8) |= (uint8_t)(1u << (j % 8));
    }
    memcpy(r, tb, sz);
    gm_wfix(r, bits, sgn);
    gm_wscratch_free(t, stack);
}

GM_API uint64_t gm_whash(uint64_t h, const uint8_t *a, uint32_t bits, bool sgn) {
    size_t n = GM_WORDS(bits);
    bool neg = gm_wisneg(a, bits, sgn);
    /* As BigInt::to_signed_bytes_le: the fewest bytes holding the value in two's complement. */
    size_t len = ((size_t)gm_wbitlen(a, bits, sgn, neg) + 1 + 7) / 8;
    for (size_t j = 0; j < len; j++) {
        uint8_t byte = j < n * 8 ? (uint8_t)(gm_wget(a, j / 8, n, bits, sgn) >> (8 * (j % 8))) : (neg ? 0xff : 0);
        h ^= byte;
        h *= 0x100000001b3ULL;
    }
    return h;
}

GM_API void gm_wfmt(gm_buf *out, const uint8_t *a, uint32_t bits, bool sgn) {
    size_t n = GM_WORDS(bits);
    uint64_t stack[GM_WSTACK];
    uint64_t *w = gm_wscratch(stack, GM_WSTACK, 2 * n + 2);
    uint64_t *chunks = w + n;
    bool neg = gm_wload(w, a, bits, sgn);
    if (neg) gm_wneg_words(w, n);
    size_t top = n, count = 0;
    while (top > 0 && w[top - 1] == 0) top--;
    while (top > 0) {
        unsigned __int128 rem = 0;
        for (size_t i = top; i > 0; i--) {
            unsigned __int128 cur = (rem << 64) | w[i - 1];
            w[i - 1] = (uint64_t)(cur / 10000000000000000000ULL);
            rem = cur % 10000000000000000000ULL;
        }
        chunks[count++] = (uint64_t)rem;
        while (top > 0 && w[top - 1] == 0) top--;
    }
    if (neg) gm_buf_push(out, "-", 1);
    char digits[24];
    if (count == 0) {
        gm_buf_push(out, "0", 1);
    }
    for (size_t c = count; c > 0; c--) {
        uint64_t v = chunks[c - 1];
        int len = 0;
        char tmp[24];
        do {
            tmp[len++] = (char)('0' + v % 10);
            v /= 10;
        } while (v != 0);
        size_t pad = c == count ? 0 : (size_t)(19 - len);
        size_t k = 0;
        while (pad-- > 0) digits[k++] = '0';
        while (len > 0) digits[k++] = tmp[--len];
        gm_buf_push(out, digits, k);
    }
    gm_wscratch_free(w, stack);
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

/* The value of a `bits`-bit integer stored in `size` bytes at p. */
static void gm_fmt_int(gm_buf *out, const gm_type *t, const uint8_t *p) {
    if (t->bits > 128) {
        gm_wfmt(out, p, t->bits, t->is_signed);
        return;
    }
    unsigned __int128 raw = gm_load_u128(p, t->size);
    unsigned bits = t->bits;
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

/* Shortest-digits float printing: the Schubfach algorithm (R. Giulietti, "The Schubfach way
 * to render doubles"). For a value c * 2^q it finds, among the decimals in the interval of
 * reals that round to the value, those with the fewest digits, and then the one nearest the
 * value (an exact tie goes to the even last digit). The table holds g(k) =
 * floor(10^-k * 2^r) + 1 with 2^125 <= g < 2^126, as { g >> 63, g & (2^63 - 1) }, for
 * k = -324..292; tools/gen_float_table.py generates it. */
#define GM_G10_KMIN (-324)
static const uint64_t gm_g10[][2] = {
    {0x4f0cedc95a718dd4ULL, 0x5b01e8b09aa0d1b5ULL},
    {0x7e7b160ef71c1621ULL, 0x119ca780f767b5eeULL},
    {0x652f44d8c5b011b4ULL, 0x0e16ec672c52f7f2ULL},
    {0x50f29d7a37c00e29ULL, 0x581256b8f0425ff5ULL},
    {0x40c21794f96671baULL, 0x79a84560c0351991ULL},
    {0x679cf287f570b5f7ULL, 0x75da089acd21c281ULL},
    {0x52e3f5399126f7f9ULL, 0x44ae6d48a41b0201ULL},
    {0x424ff76140ebf994ULL, 0x36f1f106e9af34cdULL},
    {0x6a198bcece465c20ULL, 0x57e981a4a918547bULL},
    {0x54e13ca571d1e34dULL, 0x2cbace1d541376c9ULL},
    {0x43e763b78e4182a4ULL, 0x23c8a4e44342c56eULL},
    {0x6ca56c58e39c043aULL, 0x060dd4a06b9e08b0ULL},
    {0x56eabd13e9499cfbULL, 0x1e7176e6bc7e6d59ULL},
    {0x458897432107b0c8ULL, 0x7ec12bebc9febde1ULL},
    {0x6f40f20501a5e7a7ULL, 0x7e01dfdfa9979635ULL},
    {0x5900c19d9aeb1fb9ULL, 0x4b34b319547944f7ULL},
    {0x4733ce17af227fc7ULL, 0x55c3c27aa9fa9d93ULL},
    {0x71ec7cf2b1d0cc72ULL, 0x560603f7765dc8eaULL},
    {0x5b2397288e40a38eULL, 0x7804cff92b7e3a55ULL},
    {0x48e945ba0b66e93fULL, 0x13370cc755fe9511ULL},
    {0x74a86f90123e41feULL, 0x51f1ae0bbcca881bULL},
    {0x5d538c7341cb67feULL, 0x74c1580963d539afULL},
    {0x4aa93d29016f8665ULL, 0x43cde0078310faf3ULL},
    {0x77752ea8024c0a3cULL, 0x0616333f381b2b1eULL},
    {0x5f90f22001d66e96ULL, 0x3811c298f9af55b1ULL},
    {0x4c73f4e667debedeULL, 0x600e35472e25de28ULL},
    {0x7a532170a6313164ULL, 0x3349eed849d6303fULL},
    {0x61dc1ac084f42783ULL, 0x42a18be03b11c033ULL},
    {0x4e49af006a5cec69ULL, 0x1bb46fe695a7ccf5ULL},
    {0x7d42b19a43c7e0a8ULL, 0x2c53e63dbc3fae55ULL},
    {0x64355ae1cfd31a20ULL, 0x237651cafcffbeaaULL},
    {0x502aaf1b0ca8e1b3ULL, 0x35f8416f30cc9888ULL},
    {0x402225af3d53e7c2ULL, 0x5e603458f3d6e06dULL},
    {0x669d0918621fd937ULL, 0x4a3386f4b957cd7bULL},
    {0x52173a79e8197a92ULL, 0x6e8f9f2a2ddfd796ULL},
    {0x41ac2ec7ece12edbULL, 0x720c7f54f17fdfabULL},
    {0x69137e0cae3517c6ULL, 0x1ce0cbbb1bffcc45ULL},
    {0x540f980a24f74638ULL, 0x171a3c95afffd69eULL},
    {0x433facd4ea5f6b60ULL, 0x127b63aaf3331218ULL},
    {0x6b991487dd657899ULL, 0x6a5f05de51eb5026ULL},
    {0x5614106cb11dfa14ULL, 0x5518d17ea7ef7352ULL},
    {0x44dcd9f08db194ddULL, 0x2a7a41321ff2c2a8ULL},
    {0x6e2e2980e2b5bafbULL, 0x5d906850331e043fULL},
    {0x5824ee00b55e2f2fULL, 0x647386a68f4b3699ULL},
    {0x4683f19a2ab1bf59ULL, 0x36c2d21ed908f87bULL},
    {0x70d31c29dde93228ULL, 0x579e1cfe280e5a5dULL},
    {0x5a427cee4b20f4edULL, 0x2c7e7d98200b7b7eULL},
    {0x483530bea280c3f1ULL, 0x09fecae019a2c932ULL},
    {0x73884dfdd0ce064eULL, 0x43314499c29e0eb6ULL},
    {0x5c6d0b3173d8050bULL, 0x4f5a9d47cee4d891ULL},
    {0x49f0d5c129799da2ULL, 0x72aee4397250ad41ULL},
    {0x764e22cea8c295d1ULL, 0x377e39f583b44868ULL},
    {0x5ea4e8a553cede41ULL, 0x12cb61913629d387ULL},
    {0x4bb72084430be500ULL, 0x756f8140f8217605ULL},
    {0x792500d39e796e67ULL, 0x6f18cece59cf233cULL},
    {0x60ea670fb1fabeb9ULL, 0x3f470bd847d8e8fdULL},
    {0x4d885272f4c89894ULL, 0x329f3cad064720caULL},
    {0x7c0d50b7ee0dc0edULL, 0x37652de1a3a50143ULL},
    {0x633dda2cbe716724ULL, 0x2c50f1814fb73436ULL},
    {0x4f64ae8a31f45283ULL, 0x3d0d8e010c92902bULL},
    {0x7f077da9e986ea6bULL, 0x7b48e334e0ea8045ULL},
    {0x659f97bb2138bb89ULL, 0x49071c2a4d88669dULL},
    {0x514c796280fa2fa1ULL, 0x20d27ceea46d1ee4ULL},
    {0x4109fab533fb594dULL, 0x670eca58838a7f1dULL},
    {0x680ff788532bc216ULL, 0x0b4add5a6c10cb62ULL},
    {0x533ff939dc2301abULL, 0x22a24aaebcda3c4eULL},
    {0x4299942e49b59aefULL, 0x354ea22563e1c9d8ULL},
    {0x6a8f537d42bc2b18ULL, 0x554a9d089fcfa95aULL},
    {0x553f75fdcefcef46ULL, 0x776ee406e63fbaaeULL},
    {0x4432c4cb0bfd8c38ULL, 0x5f8be99f1e996225ULL},
    {0x6d1e07ab466279f4ULL, 0x327975cb64289d08ULL},
    {0x574b3955d1e86190ULL, 0x28612b091ced4a6dULL},
    {0x45d5c777db204e0dULL, 0x06b4226db0bdd524ULL},
    {0x6fbc72595e9a167bULL, 0x24536a491ac95506ULL},
    {0x59638eade54811fcULL, 0x1d0f883a7bd44405ULL},
    {0x4782d88b1dd34196ULL, 0x4a72d361fca9d004ULL},
    {0x726af411c952028aULL, 0x43eaebcffaa94cd3ULL},
    {0x5b88c3416ddb353bULL, 0x4fef230cc88770a9ULL},
    {0x493a35cdf17c2a96ULL, 0x0cbf4f3d6d3926eeULL},
    {0x7529efafe8c6aa89ULL, 0x61321862485b717cULL},
    {0x5dbb262653d22207ULL, 0x675b46b506af8dfdULL},
    {0x4afc1e850fdb4e6cULL, 0x52af6bc405593e64ULL},
    {0x77f9ca6e7fc54a47ULL, 0x377f12d33bc1fd6dULL},
    {0x5ffb085866376e9fULL, 0x45ff42429634cabdULL},
    {0x4cc8d379eb5f8bb2ULL, 0x6b329b68782a3bcbULL},
    {0x7adaebf64565ac51ULL, 0x2b842bda59dd2c77ULL},
    {0x6248bcc5045156a7ULL, 0x3c69bcaeae4a89f9ULL},
    {0x4ea0970403744552ULL, 0x6387ca25583ba194ULL},
    {0x7dcdbe6cd253a21eULL, 0x05a6103bc05f68edULL},
    {0x64a498570ea94e7eULL, 0x37b80cfc99e5ed8aULL},
    {0x5083ad1272210b98ULL, 0x2c933d96e184be08ULL},
    {0x40695741f4e73c79ULL, 0x7075cadf1ad09807ULL},
    {0x670ef2032171fa5cULL, 0x4d8944982ae759a4ULL},
    {0x52725b35b45b2eb0ULL, 0x3e076a135585e150ULL},
    {0x41f515c49048f226ULL, 0x64d2bb42aad1810dULL},
    {0x698822d41a0e503eULL, 0x07b7920444826815ULL},
    {0x546ce8a9ae71d9cbULL, 0x1fc60e69d0685344ULL},
    {0x438a53baf1f4ae3cULL, 0x196b3ebb0d20429dULL},
    {0x6c1085f7e9877d2dULL, 0x0f11fdf815006a94ULL},
    {0x56739e5fee05fdbdULL, 0x58db319344005543ULL},
    {0x45294b7ff19e6497ULL, 0x60af5adc3666aa9cULL},
    {0x6ea878ccb5ca3a8cULL, 0x344bc4938a3dddc7ULL},
    {0x5886c70a2b082ed6ULL, 0x5d096a0fa1cb17d2ULL},
    {0x46d238d4ef39bf12ULL, 0x173abb3fb4a27975ULL},
    {0x71505aee4b8f981dULL, 0x0b912b992103f588ULL},
    {0x5aa6af25093face4ULL, 0x0940efadb4032ad3ULL},
    {0x488558ea6dcc8a50ULL, 0x07672624900288a9ULL},
    {0x74088e43e2e0dd4cULL, 0x723ea36db337410eULL},
    {0x5cd3a5031be71770ULL, 0x5b654f8af5c5cda5ULL},
    {0x4a42ea68e31f45f3ULL, 0x62b772d5916b0aebULL},
    {0x76d1770e38320986ULL, 0x0458b7bc1bde77ddULL},
    {0x5f0df8d82cf4d46bULL, 0x1d13c630164b9318ULL},
    {0x4c0b2d79bd90a9efULL, 0x30dc9e8cdea2dc13ULL},
    {0x79ab7bf5fc1aa97fULL, 0x0160fdae31049351ULL},
    {0x6155fcc4c9aeedffULL, 0x1ab3fe24f403a90eULL},
    {0x4dde63d0a158be65ULL, 0x6229981d9002eda5ULL},
    {0x7c97061a9bc130a2ULL, 0x69dc2695b337e2a1ULL},
    {0x63ac04e2163426e8ULL, 0x54b01ede28f9821bULL},
    {0x4fbcd0b4de901f20ULL, 0x43c018b1ba6134e2ULL},
    {0x7f9481216419cb67ULL, 0x1f99c11c5d68549dULL},
    {0x6610674de9ae3c52ULL, 0x4c7b00e37ded107eULL},
    {0x51a6b90b21583042ULL, 0x09fc00b5fe574065ULL},
    {0x41522da2811359ceULL, 0x3b3000919845cd1dULL},
    {0x68837c3734ebc2e3ULL, 0x784ccdb5c06fae95ULL},
    {0x539c635f5d8968b6ULL, 0x2d0a3e2b00595877ULL},
    {0x42e382b2b13aba2bULL, 0x3da1cb5599e11393ULL},
    {0x6b059deab52ac378ULL, 0x629c7888f634ec1eULL},
    {0x559e17eef755692dULL, 0x3549fa072b5d89b1ULL},
    {0x447e798bf91120f1ULL, 0x1107fb38ef7e07c1ULL},
    {0x6d9728dff4e834b5ULL, 0x01a65ec17f300c68ULL},
    {0x57ac20b32a535d5dULL, 0x4e1eb23465c009edULL},
    {0x46234d5c21dc4ab1ULL, 0x24e55b5d1e333b24ULL},
    {0x70387bc69c93aab5ULL, 0x216ef894fd1ec506ULL},
    {0x59c6c96bb076222aULL, 0x4df2607730e56a6cULL},
    {0x47d23abc8d2b4e88ULL, 0x3e5b805f5a5121f0ULL},
    {0x72e9f79415121740ULL, 0x63c59a322a1b697fULL},
    {0x5bee5fa9aa74df67ULL, 0x03047b5b54e2baccULL},
    {0x498b7fbaeec3e5ecULL, 0x0269fc4910b5623dULL},
    {0x75abff917e063cacULL, 0x6a432d41b45569fbULL},
    {0x5e2332dacb38308aULL, 0x21cf5767c37787fcULL},
    {0x4b4f5be23c2cf3a1ULL, 0x67d912b9692c6ccaULL},
    {0x787ef969f9e185cfULL, 0x595b5128a8471476ULL},
    {0x60659454c7e79e3fULL, 0x6115da86ed05a9f8ULL},
    {0x4d1e1043d31fb1ccULL, 0x4dab1538bd9e2193ULL},
    {0x7b634d3951cc4fadULL, 0x62ab552795c9cf52ULL},
    {0x62b5d7610e3d0c8bULL, 0x0222aa86116e3f75ULL},
    {0x4ef7df80d830d6d5ULL, 0x4e822204dabe992aULL},
    {0x7e59659af38157bcULL, 0x17369cd49130f510ULL},
    {0x65145148c2cddfc9ULL, 0x5f5ee3dd40f3f740ULL},
    {0x50dd0dd3cf0b196eULL, 0x1918b64a9a5cc5cdULL},
    {0x40b0d7dca5a27abeULL, 0x4746f83baeb09e3eULL},
    {0x678159610903f797ULL, 0x253e59f91780fd2fULL},
    {0x52cde11a6d9cc612ULL, 0x50feae60df9a6426ULL},
    {0x423e4daebe1704dbULL, 0x5a65584d7faeb685ULL},
    {0x69fd4917968b3af9ULL, 0x10a226e265e4573bULL},
    {0x54caa0dfaba29594ULL, 0x0d4e8581eb1d1295ULL},
    {0x43d54d7fbc821143ULL, 0x243ed134bc174211ULL},
    {0x6c887bff94034ed2ULL, 0x06cae85460253682ULL},
    {0x56d396661002a574ULL, 0x6bd586a9e6842b9bULL},
    {0x457611eb40021df7ULL, 0x09779eee52035616ULL},
    {0x6f234fdeccd02ff1ULL, 0x5bf297e3b66bbcefULL},
    {0x58e90cb23d73598eULL, 0x165bacb62b8963f3ULL},
    {0x4720d6f4fdf5e13eULL, 0x451623c4efa11cc2ULL},
    {0x71ce24bb2fefcecaULL, 0x3b569fa17f682e03ULL},
    {0x5b0b5095bff30bd5ULL, 0x15dee61acc535803ULL},
    {0x48d5da11665c0977ULL, 0x2b18b8157042accfULL},
    {0x74895ce8a3c6758bULL, 0x5e8df355806aae18ULL},
    {0x5d3ab0ba1c9ec46fULL, 0x653e5c4466bbbe7aULL},
    {0x4a955a2e7d4bd059ULL, 0x3765169d1efc9861ULL},
    {0x77555d172edfb3c2ULL, 0x256e8a94fe60f3cfULL},
    {0x5f777dac257fc301ULL, 0x6abed543feb3f63fULL},
    {0x4c5f97bceacc9c01ULL, 0x3bcbddcffef65e99ULL},
    {0x7a328c6177adc668ULL, 0x5fac961997f0975bULL},
    {0x61c209e792f16b86ULL, 0x7fbd44e1465a12afULL},
    {0x4e34d4b9425abc6bULL, 0x7fca9d810514dbbfULL},
    {0x7d21545b9d5dfa46ULL, 0x32ddc8ce6e87c5ffULL},
    {0x641aa9e2e44b2e9eULL, 0x5be4a0a525396b32ULL},
    {0x501554b5836f587eULL, 0x7cb6e6ea842def5cULL},
    {0x4011109135f2ad32ULL, 0x30925255368b25e3ULL},
    {0x6681b41b89844850ULL, 0x4db6ea21f0dea304ULL},
    {0x52015ce2d469d373ULL, 0x57c5881b2718826aULL},
    {0x419ab0b576bb0f8fULL, 0x5fd139af527a01efULL},
    {0x68f781225791b27fULL, 0x4c81f5e550c3364aULL},
    {0x53f9341b79415b99ULL, 0x239b2b1dda35c508ULL},
    {0x432dc3492dcde2e1ULL, 0x02e288e4ae916a6dULL},
    {0x6b7c6ba849496b01ULL, 0x516a74a1174f10aeULL},
    {0x55fd22ed076def34ULL, 0x4121f6e745d8da25ULL},
    {0x44ca82573924bf5dULL, 0x1a8192529e4714ebULL},
    {0x6e10d08b8ea1322eULL, 0x5d9c1d50fd3e87ddULL},
    {0x580d73a2d880f4f2ULL, 0x17b01773fdcb9fe4ULL},
    {0x4671294f139a5d8eULL, 0x4626792997d61984ULL},
    {0x70b50ee4ec2a2f4aULL, 0x3d0a5b75bfbcf59fULL},
    {0x5a2a7250bcee8c3bULL, 0x4a6eaf916630c47fULL},
    {0x4821f50d63f209c9ULL, 0x21f2260deb5a36ccULL},
    {0x736988156cb6760eULL, 0x69837016455d247aULL},
    {0x5c546cddf091f80bULL, 0x6e02c011d1175062ULL},
    {0x49dd23e4c074c66fULL, 0x719bccdb0dac404eULL},
    {0x762e9fd467213d7fULL, 0x68f947c4e2ad33b0ULL},
    {0x5e8bb3105280fdffULL, 0x6d94396a4ef0f627ULL},
    {0x4ba2f5a6a8673199ULL, 0x3e102deea58d91b9ULL},
    {0x7904bc3dda3eb5c2ULL, 0x3019e3176f48e927ULL},
    {0x60d09697e1cbc49bULL, 0x4014b5ac590720ecULL},
    {0x4d73abacb4a303afULL, 0x4cdd5e237a6c1a57ULL},
    {0x7bec45e12104d2b2ULL, 0x47c8969f2a46908aULL},
    {0x63236b1a80d0a88eULL, 0x6ca0787f5505406fULL},
    {0x4f4f88e200a6ed3fULL, 0x0a19f9ff773766bfULL},
    {0x7ee5a7d0010b1531ULL, 0x5cf65ccbf1f23dfeULL},
    {0x6584864000d5aa8eULL, 0x172b7d6ff4c1cb32ULL},
    {0x5136d1cccd77bba4ULL, 0x78ef978cc3ce3c28ULL},
    {0x40f8a7d70ac62fb7ULL, 0x13f2dfa3cfd83020ULL},
    {0x67f43fbe77a37f8bULL, 0x398499061959e699ULL},
    {0x5329cc985fb5ffa2ULL, 0x6136e0d1ade18548ULL},
    {0x4287d6e04c91994fULL, 0x00f8b3daf181376dULL},
    {0x6a72f166e0e8f54bULL, 0x1b27862b1c01f247ULL},
    {0x5528c11f1a53f76fULL, 0x2f52d1bc1667f506ULL},
    {0x44209a7f48432c59ULL, 0x0c424163451ff738ULL},
    {0x6d00f7320d3846f4ULL, 0x7a039bd208332526ULL},
    {0x5733f8f4d76038c3ULL, 0x7b361641a028ea85ULL},
    {0x45c32d90ac4cfa36ULL, 0x2f5e78348020bb9eULL},
    {0x6f9eaf4de07b29f0ULL, 0x4bca59ed99cdf8fcULL},
    {0x594bbf71806287f3ULL, 0x563b7b247b0b2d96ULL},
    {0x476fcc5acd1b9ff6ULL, 0x11c92f50626f57acULL},
    {0x724c7a2ae1c5ccbdULL, 0x02db7ee703e55912ULL},
    {0x5b7061bbe7d17097ULL, 0x1be2cbec031de0dcULL},
    {0x4926b496530df3acULL, 0x164f09899c17e716ULL},
    {0x750aba8a1e7cb913ULL, 0x3d4b4275c68ca4f0ULL},
    {0x5da22ed4e530940fULL, 0x4aa29b916ba3b726ULL},
    {0x4ae825771dc07672ULL, 0x6ee87c74561c9285ULL},
    {0x77d9d58b62cd8a51ULL, 0x3173fa53bcfa8408ULL},
    {0x5fe177a2b5713b74ULL, 0x278ffb7630c869a0ULL},
    {0x4cb45fb55df42f90ULL, 0x1fa662c4f3d387b3ULL},
    {0x7aba32bbc986b280ULL, 0x32a3d13b1fb8d91fULL},
    {0x622e8efca1388ecdULL, 0x0ee9742f4c93e0e6ULL},
    {0x4e8ba596e760723dULL, 0x58bac3590a0fe71eULL},
    {0x7dac3c24a5671d2fULL, 0x412ad228101971c9ULL},
    {0x6489c9b6eab8e426ULL, 0x00ef0e8673478e3bULL},
    {0x506e3af8bbc71cebULL, 0x1a58d86b8f6c71c9ULL},
    {0x40582f2d6305b0bcULL, 0x1513e0560c56c16eULL},
    {0x66f37eaf04d5e793ULL, 0x3b530089ad579be2ULL},
    {0x525c6558d0ab1fa9ULL, 0x15dc006e2446164fULL},
    {0x41e384470d55b2edULL, 0x5e4999f1b69e783fULL},
    {0x696c06d81555eb15ULL, 0x7d428fe92430c065ULL},
    {0x54566be0111188deULL, 0x31020cba835a3384ULL},
    {0x4378564cda746d7eULL, 0x5a680a2ecf7b5c69ULL},
    {0x6bf3bd47c3ed7bfdULL, 0x770cdd17b25efa42ULL},
    {0x565c976c9cbdfccbULL, 0x1270b0dfc1e59502ULL},
    {0x4516df8a16fe63d5ULL, 0x5b8d5a4c9b1e10ceULL},
    {0x6e8aff4357fd6c89ULL, 0x127bc3adc4fce7b0ULL},
    {0x586f329c466456d4ULL, 0x0ec96957d0ca52f3ULL},
    {0x46bf5bb038504576ULL, 0x3f07877973d50f29ULL},
    {0x71322c4d26e6d58aULL, 0x31a5a58f1fbb4b75ULL},
    {0x5a8e89d75252446eULL, 0x5aeaead8e62f6f91ULL},
    {0x487207df750e9d25ULL, 0x2f22557a51bf8c74ULL},
    {0x73e9a63254e42ea2ULL, 0x1836ef2a1c65ad86ULL},
    {0x5cbaeb5b771cf21bULL, 0x2cf8bf54e3848ad2ULL},
    {0x4a2f22af927d8e7cULL, 0x23fa32aa4f9d3bdbULL},
    {0x76b1d118ea627d93ULL, 0x5329eaaa18fb92f8ULL},
    {0x5ef4a74721e86476ULL, 0x0f54bbbb472fa8c6ULL},
    {0x4bf6ec38e7ed1d2bULL, 0x25dd62fc38f2ed6cULL},
    {0x798b138e3fe1c845ULL, 0x22fbd1938e517bdfULL},
    {0x613c0fa4ffe7d36aULL, 0x4f2fdadc71dac97fULL},
    {0x4dc9a61d998642bbULL, 0x58f3157d27e23accULL},
    {0x7c75d695c2706ac5ULL, 0x74b82261d969f7adULL},
    {0x63917877cec0556bULL, 0x10934eb4adee5fbeULL},
    {0x4fa793930bcd1122ULL, 0x4075d8908b251965ULL},
    {0x7f7285b812e1b504ULL, 0x00bc8db411d4f56eULL},
    {0x65f537c675815d9cULL, 0x66fd3e29a7dd9125ULL},
    {0x5190f96b91344ae3ULL, 0x6bfdcb54864ada84ULL},
    {0x4140c78940f6a24fULL, 0x6ffe3c439ea2486aULL},
    {0x6867a5a867f103b2ULL, 0x7ffd2d38fdd073dcULL},
    {0x53861e2053273628ULL, 0x6664242d97d9f64aULL},
    {0x42d1b1b375b8f820ULL, 0x51e9b68adfe191d5ULL},
    {0x6ae91c5255f4c034ULL, 0x1ca924116635b621ULL},
    {0x558749db77f70029ULL, 0x63ba83411e915e81ULL},
    {0x446c3b15f9926687ULL, 0x6962029a7edab201ULL},
    {0x6d79f82328ea3da6ULL, 0x0f03375d97c45001ULL},
    {0x5794c6828721caebULL, 0x259c2c4adfd04001ULL},
    {0x46109eced2816f22ULL, 0x5149bd08b30d0001ULL},
    {0x701a97b150cf1837ULL, 0x3542c80deb480001ULL},
    {0x59aedfc10d7279c5ULL, 0x7768a00b22a00001ULL},
    {0x47bf19673df52e37ULL, 0x79208008e8800001ULL},
    {0x72cb5bd86321e38cULL, 0x5b67334174000001ULL},
    {0x5bd5e313828182d6ULL, 0x7c528f6790000001ULL},
    {0x4977e8dc68679bdfULL, 0x16a872b940000001ULL},
    {0x758ca7c70d7292feULL, 0x5773eac200000001ULL},
    {0x5e0a1fd271287598ULL, 0x45f6556800000001ULL},
    {0x4b3b4ca85a86c47aULL, 0x04c5112000000001ULL},
    {0x785ee10d5da46d90ULL, 0x07a1b50000000001ULL},
    {0x604be73de4838ad9ULL, 0x52e7c40000000001ULL},
    {0x4d0985cb1d3608aeULL, 0x0f1fd00000000001ULL},
    {0x7b426fab61f00de3ULL, 0x31cc800000000001ULL},
    {0x629b8c891b267182ULL, 0x5b0a000000000001ULL},
    {0x4ee2d6d415b85aceULL, 0x7c08000000000001ULL},
    {0x7e37be2022c0914bULL, 0x1340000000000001ULL},
    {0x64f964e68233a76fULL, 0x2900000000000001ULL},
    {0x50c783eb9b5c85f2ULL, 0x5400000000000001ULL},
    {0x409f9cbc7c4a04c2ULL, 0x1000000000000001ULL},
    {0x6765c793fa10079dULL, 0x0000000000000001ULL},
    {0x52b7d2dcc80cd2e4ULL, 0x0000000000000001ULL},
    {0x422ca8b0a00a4250ULL, 0x0000000000000001ULL},
    {0x69e10de76676d080ULL, 0x0000000000000001ULL},
    {0x54b40b1f852bda00ULL, 0x0000000000000001ULL},
    {0x43c33c1937564800ULL, 0x0000000000000001ULL},
    {0x6c6b935b8bbd4000ULL, 0x0000000000000001ULL},
    {0x56bc75e2d6310000ULL, 0x0000000000000001ULL},
    {0x4563918244f40000ULL, 0x0000000000000001ULL},
    {0x6f05b59d3b200000ULL, 0x0000000000000001ULL},
    {0x58d15e1762800000ULL, 0x0000000000000001ULL},
    {0x470de4df82000000ULL, 0x0000000000000001ULL},
    {0x71afd498d0000000ULL, 0x0000000000000001ULL},
    {0x5af3107a40000000ULL, 0x0000000000000001ULL},
    {0x48c2739500000000ULL, 0x0000000000000001ULL},
    {0x746a528800000000ULL, 0x0000000000000001ULL},
    {0x5d21dba000000000ULL, 0x0000000000000001ULL},
    {0x4a817c8000000000ULL, 0x0000000000000001ULL},
    {0x7735940000000000ULL, 0x0000000000000001ULL},
    {0x5f5e100000000000ULL, 0x0000000000000001ULL},
    {0x4c4b400000000000ULL, 0x0000000000000001ULL},
    {0x7a12000000000000ULL, 0x0000000000000001ULL},
    {0x61a8000000000000ULL, 0x0000000000000001ULL},
    {0x4e20000000000000ULL, 0x0000000000000001ULL},
    {0x7d00000000000000ULL, 0x0000000000000001ULL},
    {0x6400000000000000ULL, 0x0000000000000001ULL},
    {0x5000000000000000ULL, 0x0000000000000001ULL},
    {0x4000000000000000ULL, 0x0000000000000001ULL},
    {0x6666666666666666ULL, 0x3333333333333334ULL},
    {0x51eb851eb851eb85ULL, 0x0f5c28f5c28f5c29ULL},
    {0x4189374bc6a7ef9dULL, 0x5916872b020c49bbULL},
    {0x68db8bac710cb295ULL, 0x74f0d844d013a92bULL},
    {0x53e2d6238da3c211ULL, 0x43f3e0370cdc8755ULL},
    {0x431bde82d7b634daULL, 0x698fe69270b06c44ULL},
    {0x6b5fca6af2bd215eULL, 0x0f4ca41d811a46d4ULL},
    {0x55e63b88c230e77eULL, 0x3f70834acdae9f10ULL},
    {0x44b82fa09b5a52cbULL, 0x4c5a02a23e254c0dULL},
    {0x6df37f675ef6eadfULL, 0x2d5cd10396a21347ULL},
    {0x57f5ff85e592557fULL, 0x3de3da69454e75d3ULL},
    {0x465e6604b7a84465ULL, 0x7e4fe1edd10b9175ULL},
    {0x709709a125da0709ULL, 0x4a19697c81ac1befULL},
    {0x5a126e1a84ae6c07ULL, 0x54e1213067bce326ULL},
    {0x480ebe7b9d58566cULL, 0x43e74dc052fd8285ULL},
    {0x734aca5f6226f0adULL, 0x530baf9a1e626a6dULL},
    {0x5c3bd5191b525a24ULL, 0x426fbfae7eb521f1ULL},
    {0x49c97747490eae83ULL, 0x4ebfcc8b9890e7f4ULL},
    {0x760f253edb4ab0d2ULL, 0x4acc7a78f41b0cbaULL},
    {0x5e72843249088d75ULL, 0x223d2ec729af3d62ULL},
    {0x4b8ed0283a6d3df7ULL, 0x34fdbf05baf29781ULL},
    {0x78e480405d7b9658ULL, 0x54c931a2c4b758cfULL},
    {0x60b6cd004ac94513ULL, 0x5d6dc14f03c5e0a5ULL},
    {0x4d5f0a66a23a9da9ULL, 0x31249aa59c9e4d51ULL},
    {0x7bcb43d769f762a8ULL, 0x4ea0f76f60fd4882ULL},
    {0x63090312bb2c4eedULL, 0x254d92bf80caa068ULL},
    {0x4f3a68dbc8f03f24ULL, 0x1dd7a89933d54d20ULL},
    {0x7ec3daf941806506ULL, 0x62f2a75b86221500ULL},
    {0x65697bfa9acd1d9fULL, 0x025bb91604e810cdULL},
    {0x51212ffbaf0a7e18ULL, 0x684960de6a5340a4ULL},
    {0x40e7599625a1fe7aULL, 0x203ab3e521dc33b6ULL},
    {0x67d88f56a29cca5dULL, 0x19f7863b696052bdULL},
    {0x5313a5dee87d6eb0ULL, 0x7b2c6b62bab37564ULL},
    {0x42761e4bed31255aULL, 0x2f56bc4efbc2c450ULL},
    {0x6a5696dfe1e83bc3ULL, 0x655793b192d13a1aULL},
    {0x5512124cb4b9c969ULL, 0x377942f475742e7bULL},
    {0x440e750a2a2e3abaULL, 0x5f9435905df68b96ULL},
    {0x6ce3ee76a9e3912aULL, 0x65b9ef4d63241289ULL},
    {0x571cbec554b60dbbULL, 0x6afb25d782834207ULL},
    {0x45b0989ddd5e7163ULL, 0x08c8eb12cecf6806ULL},
    {0x6f80f42fc8971bd1ULL, 0x5adb11b7b14bd9a3ULL},
    {0x5933f68ca078e30eULL, 0x157c0e2c8dd647b5ULL},
    {0x475cc53d4d2d8271ULL, 0x5dfcd823a4ab6c91ULL},
    {0x722e086215159d82ULL, 0x632e269f6ddf141bULL},
    {0x5b5806b4ddaae468ULL, 0x4f581ee5f17f4349ULL},
    {0x49133890b1558386ULL, 0x72ace584c1329c3bULL},
    {0x74eb8db44eef38d7ULL, 0x6aae3c079b842d2aULL},
    {0x5d893e29d8bf60acULL, 0x5558300616035755ULL},
    {0x4ad431bb13cc4d56ULL, 0x7779c004de6912abULL},
    {0x77b9e92b52e07bbeULL, 0x258f99a163db5111ULL},
    {0x5fc7edbc424d2fcbULL, 0x37a614811caf740dULL},
    {0x4c9ff163683dbfd5ULL, 0x7951aa00e3bf900bULL},
    {0x7a998238a6c932efULL, 0x754f7667d2cc19abULL},
    {0x6214682d523a8f26ULL, 0x2aa5f8530f09ae22ULL},
    {0x4e76b9bddb620c1eULL, 0x55519375a5a1581bULL},
    {0x7d8ac2c95f034697ULL, 0x3bb5b8bc3c3559c5ULL},
    {0x646f023ab2690545ULL, 0x7c9160969691149eULL},
    {0x5058ce955b87376bULL, 0x16dab3ababa743b2ULL},
    {0x40470baaaf9f5f88ULL, 0x78aef622efb902f5ULL},
    {0x66d812aab29898dbULL, 0x0de4bd04b2c19e54ULL},
    {0x524675555bad4715ULL, 0x57ea30d08f014b76ULL},
    {0x41d1f7777c8a9f44ULL, 0x4654f3da0c01092cULL},
    {0x694ff258c7443207ULL, 0x23bb1fc346680eacULL},
    {0x543ff513d29cf4d2ULL, 0x4fc8e635d1ecd88aULL},
    {0x43665da9754a5d75ULL, 0x263a51c4a7f0ad3bULL},
    {0x6bd6fc425543c8bbULL, 0x56c3b607731aaec4ULL},
    {0x5645969b77696d62ULL, 0x789c919f8f488bd0ULL},
    {0x4504787c5f878ab5ULL, 0x46e3a7b2d906d640ULL},
    {0x6e6d8d93cc0c1122ULL, 0x3e390c515b3e239aULL},
    {0x5857a4763cd6741bULL, 0x4b60d6a77c31b615ULL},
    {0x46ac8391ca4529afULL, 0x55e7121f968e2b44ULL},
    {0x711405b6106ea919ULL, 0x0971b698f0e3786dULL},
    {0x5a766af80d255414ULL, 0x078e2bad8d82c6bdULL},
    {0x485ebbf9a41ddcdcULL, 0x6c71bc8ad79bd231ULL},
    {0x73cac65c39c96161ULL, 0x2d82c7448c2c8382ULL},
    {0x5ca23849c7d44de7ULL, 0x3e023903a356cf9bULL},
    {0x4a1b603b06437185ULL, 0x7e682d9c82abd949ULL},
    {0x76923391a39f1c09ULL, 0x4a4048fa6aac8edbULL},
    {0x5edb5c7482e5b007ULL, 0x55003a61eef07249ULL},
    {0x4be2b05d35848cd2ULL, 0x773361e7f259f507ULL},
    {0x796ab3c855a0e151ULL, 0x3eb89ca6508fee71ULL},
    {0x6122296d114d810dULL, 0x7efa16eb73a6585bULL},
    {0x4db4edf0daa4673eULL, 0x3261abef8fb846afULL},
    {0x7c54afe7c43a3ecaULL, 0x1d691318e5f3a44bULL},
    {0x6376f31fd02e98a1ULL, 0x64540f471e5c836fULL},
    {0x4f925c1973587a1bULL, 0x0376729f4b7d35f3ULL},
    {0x7f50935bebc0c35eULL, 0x38bd84321261efebULL},
    {0x65da0f7cbc9a35e5ULL, 0x13cad0280eb4bfefULL},
    {0x517b3f96fd482b1dULL, 0x5ca240200bc3ccbfULL},
    {0x412f66126439bc17ULL, 0x63b50019a3030a33ULL},
    {0x684bd683d38f9359ULL, 0x1f88002904d1a9eaULL},
    {0x536fdecfdc72dc47ULL, 0x32d3335403daee55ULL},
    {0x42bfe57316c249d2ULL, 0x5bdc291003158b77ULL},
    {0x6acca251be03a951ULL, 0x12f9db4cd1bc1258ULL},
    {0x557081dafe695440ULL, 0x7594af70a7c9a847ULL},
    {0x445a017bfebaa9cdULL, 0x4476f2c0863aed06ULL},
    {0x6d5ccf2ccac442e2ULL, 0x3a57eacda3917b3cULL},
    {0x577d728a3bd03581ULL, 0x7b7988a482dac8fdULL},
    {0x45fdf53b630cf79bULL, 0x15fad3b6cf156d97ULL},
    {0x6ffcbb923814bf5eULL, 0x565e1f8ae4ef15beULL},
    {0x5996fc74f9aa32b2ULL, 0x11e4e608b725aaffULL},
    {0x47abfd2a6154f55bULL, 0x27ea51a0928488ccULL},
    {0x72acc843ceee555eULL, 0x7310829a84074146ULL},
    {0x5bbd6d030bf1dde5ULL, 0x42739baed005cdd2ULL},
    {0x49645735a327e4b7ULL, 0x4ec2e2f24004a4a8ULL},
    {0x756d5855d1d96df2ULL, 0x4ad16b1d333aa10cULL},
    {0x5df11377db1457f5ULL, 0x2241227dc2954da3ULL},
    {0x4b2742c648dd132aULL, 0x4e9a81fe35443e1cULL},
    {0x783ed13d4161b844ULL, 0x175d9cc9eed39694ULL},
    {0x603240fdcde7c69cULL, 0x7917b0a18bdc7876ULL},
    {0x4cf500cb0b1fd217ULL, 0x1412f3b46fe39392ULL},
    {0x7b219ade7832e9beULL, 0x535185ed7fd285b6ULL},
    {0x628148b1f9c25498ULL, 0x42a79e57997537c5ULL},
    {0x4ecdd3c1949b76e0ULL, 0x3552e512e12a9304ULL},
    {0x7e161f9c20f8be33ULL, 0x6eeb081e3510eb39ULL},
    {0x64de7fb01a609829ULL, 0x3f226ce4f740bc2eULL},
    {0x50b1ffc0151a1354ULL, 0x3281f0b72c33c9beULL},
    {0x408e66334414dc43ULL, 0x42018d5f568fd498ULL},
    {0x674a3d1ed354939fULL, 0x1ccf48988a7fba8dULL},
    {0x52a1ca7f0f76dc7fULL, 0x30a5d3ad3b99620bULL},
    {0x421b0865a5f8b065ULL, 0x73b7dc8a96144e6fULL},
    {0x69c4da3c3cc11a3cULL, 0x52bfc7442353b0b1ULL},
    {0x549d7b6363cdae96ULL, 0x756639034f7626f4ULL},
    {0x43b12f82b63e2545ULL, 0x4451c735d92b525dULL},
    {0x6c4eb26abd303ba2ULL, 0x3a1c71efc1deea2eULL},
    {0x56a55b889759c94eULL, 0x61b05b2634b254f2ULL},
    {0x45511606df7b0772ULL, 0x1af37c1e908eaa5bULL},
    {0x6ee8233e325e7250ULL, 0x2b1f2cfdb41776f8ULL},
    {0x58b9b5cb5b7ec1d9ULL, 0x6f4c23fe29ac5f2dULL},
    {0x46faf7d5e2cbce47ULL, 0x72a34ffe87bd18f1ULL},
    {0x71918c896adfb073ULL, 0x04387ffda5fb5b1bULL},
    {0x5adad6d4557fc05cULL, 0x0360666484c915afULL},
    {0x48af1243779966b0ULL, 0x02b3851d3707448cULL},
    {0x744b506bf28f0ab3ULL, 0x1dec082ebe720746ULL},
    {0x5d090d2328726ef5ULL, 0x64bcd358985b3905ULL},
    {0x4a6da41c205b8bf7ULL, 0x6a30a913ad15c738ULL},
    {0x7715d36033c5acbfULL, 0x5d1aa81f7b560b8cULL},
    {0x5f44a919c3048a32ULL, 0x7daeece5fc44d609ULL},
    {0x4c36edae359d3b5bULL, 0x7e258a51969d7808ULL},
    {0x79f17c49ef61f893ULL, 0x16a276e8f0fbf33fULL},
    {0x618dfd07f2b4c6dcULL, 0x121b9253f3fcc299ULL},
    {0x4e0b30d328909f16ULL, 0x41afa84329970214ULL},
    {0x7cdeb4850db431bdULL, 0x4f7f739ea8f19cedULL},
    {0x63e55d373e29c164ULL, 0x3f99294bba5ae3f1ULL},
    {0x4feab0f8fe87cde9ULL, 0x7fadbaa2fb7be98dULL},
    {0x7fdde7f4ca72e30fULL, 0x7f7c5dd1925fdc15ULL},
    {0x664b1ff7085be8d9ULL, 0x4c637e4141e649abULL},
    {0x51d5b32c06afed7aULL, 0x704f983434b83aefULL},
    {0x4177c2899ef32462ULL, 0x26a6135cf6f9c8bfULL},
    {0x68bf9da8fe51d3d0ULL, 0x3dd685618b294132ULL},
    {0x53cc7e20cb74a973ULL, 0x4b12044e08edcdc2ULL},
    {0x4309fe80a2c3bac2ULL, 0x6f419d0b3a57d7ceULL},
    {0x6b4330cdd1392ad1ULL, 0x320294dec3bfbfb0ULL},
    {0x55cf5a3e40fa88a7ULL, 0x419baa4bcfcc995aULL},
    {0x44a5e1cb672ed3b9ULL, 0x1ae2eea30ca3ade1ULL},
    {0x6dd636123eb152c1ULL, 0x77d17dd1add2afcfULL},
    {0x57de91a832277567ULL, 0x797464a7be42263fULL},
    {0x464ba7b9c1b92ab9ULL, 0x4790508631ce84ffULL},
    {0x70790c5c6928445cULL, 0x0c1a1a704fb0d4ccULL},
    {0x59fa7049edb9d049ULL, 0x567b4859d95a43d6ULL},
    {0x47fb8d07f161736eULL, 0x11fc39e17aae9cabULL},
    {0x732c14d98235857dULL, 0x032d2968c44a9445ULL},
    {0x5c2343e134f79dfdULL, 0x4f575453d03ba9d1ULL},
    {0x49b5cfe75d92e4caULL, 0x72ac4376402fbb0eULL},
    {0x75efb30bc8eb07abULL, 0x0446d256cd192b49ULL},
    {0x5e595c096d88d2efULL, 0x1d0575123dadbc3aULL},
    {0x4b7ab0078ad3dbf2ULL, 0x4a6ac40e97be302fULL},
    {0x78c44cd8de1fc650ULL, 0x771139b0f2c9e6b1ULL},
    {0x609d0a4718196b73ULL, 0x78da948d8f07ebc1ULL},
    {0x4d4a6e9f467abc5cULL, 0x60aedd3e0c065634ULL},
    {0x7baa4a9870c46094ULL, 0x344afb9679a3bd20ULL},
    {0x62eea2138d69e6ddULL, 0x103bfc78614fca80ULL},
    {0x4f254e760abb1f17ULL, 0x26966393810ca200ULL},
    {0x7ea21723445e9825ULL, 0x2423d2859b476999ULL},
    {0x654e78e9037ee01dULL, 0x69b642047c392148ULL},
    {0x510b93ed9c658017ULL, 0x6e2b680396941aa0ULL},
    {0x40d60ff149eaccdfULL, 0x71bc53361210154dULL},
    {0x67bce64edcaae166ULL, 0x1c6085235019bbaeULL},
    {0x52fd850be3bbe784ULL, 0x7d1a041c40149625ULL},
    {0x42646a6fe9631f9dULL, 0x4a7b367d0010781dULL},
    {0x6a3a43e642383295ULL, 0x5d91f0c8001a59c8ULL},
    {0x54fb698501c68edeULL, 0x17a7f3d3334847d4ULL},
    {0x43fc546a67d20be4ULL, 0x79532975c2a03976ULL},
    {0x6cc6ed770c83463bULL, 0x0eeb75893766c256ULL},
    {0x57058ac5a39c382fULL, 0x25892ad42c523512ULL},
    {0x459e089e1c7cf9bfULL, 0x37a0ef102374f742ULL},
    {0x6f6340fcfa618f98ULL, 0x59017e8038bb2536ULL},
    {0x591c33fd951ad946ULL, 0x7a67986693c8ea91ULL},
    {0x4749c33144157a9fULL, 0x151fad1edca0bba8ULL},
    {0x720f9eb539bbf765ULL, 0x0832ae97c76792a5ULL},
    {0x5b3fb22a94965f84ULL, 0x068ef21305ec7551ULL},
    {0x48ffc1bbaa11e603ULL, 0x1ed8c1a8d189f774ULL},
    {0x74cc692c434fd66bULL, 0x4af4690e1c0ff253ULL},
    {0x5d705423690cab89ULL, 0x225d20d816732843ULL},
    {0x4ac0434f873d5607ULL, 0x35174d79ab8f5369ULL},
    {0x779a054c0b955672ULL, 0x21bee25c45b21f0eULL},
    {0x5fae6aa33c77785bULL, 0x3498b5169e2818d8ULL},
    {0x4c8b888296c5f9e2ULL, 0x5d46f7454b534713ULL},
    {0x7a78da6a8ad65c9dULL, 0x7ba4bed545520b52ULL},
    {0x61fa48553bdeb07eULL, 0x2fb6ff110441a2a8ULL},
    {0x4e61d37763188d31ULL, 0x72f8cc0d9d014eedULL},
    {0x7d6952589e8daeb6ULL, 0x1e5ae015c80217e1ULL},
    {0x645441e07ed7bef8ULL, 0x1848b344a001acb4ULL},
    {0x504367e6cbdfcbf9ULL, 0x603a2903b3348a2aULL},
    {0x4035ecb8a3196ffbULL, 0x002e873628f6d4eeULL},
    {0x66bcadf43828b32bULL, 0x19e40b89db2487e3ULL},
    {0x52308b29c686f5bcULL, 0x14b66fa17c1d3983ULL},
    {0x41c06f549ed25e30ULL, 0x1091f2e7967dc79cULL},
    {0x6933e554315096b3ULL, 0x341cb7d8f0c93f5fULL},
    {0x542984435aa6def5ULL, 0x767d5fe0c0a0ff80ULL},
    {0x435469cf7bb8b25eULL, 0x2b977fe70080cc66ULL},
    {0x6bba42e592c11d63ULL, 0x5f58cca4cd9ae0a3ULL},
    {0x562e9beadbcdb11cULL, 0x4c470a1d7148b3b6ULL},
    {0x44f216557ca48db0ULL, 0x3d05a1b1276d5c92ULL},
    {0x6e5023bbfaa0e2b3ULL, 0x7b3c35e83f1560e9ULL},
    {0x58401c96621a4ef6ULL, 0x2f635e5365aab3edULL},
    {0x4699b0784e7b725eULL, 0x591c4b75eaeef658ULL},
    {0x70f5e726e3f8b6fdULL, 0x74fa125644b18a26ULL},
    {0x5a5e5285832d5f31ULL, 0x43fb41de9d5ad4ebULL},
    {0x484b75379c244c27ULL, 0x4ffc34b2177bdd89ULL},
    {0x73abeebf603a1372ULL, 0x4cc6bab68bf96274ULL},
    {0x5c898bcc4cfb42c2ULL, 0x0a38955ed6611b90ULL},
    {0x4a07a309d72f689bULL, 0x21c6dde5784dafa7ULL},
    {0x76729e762518a75eULL, 0x693e2fd58d49190bULL},
    {0x5ec2185e8413b918ULL, 0x5431bfde0aa0e0d5ULL},
    {0x4bce79e536762dadULL, 0x29c1664b3bb3e711ULL},
    {0x794a5ca1f0bd15e2ULL, 0x0f9bd6dec5eca4e8ULL},
    {0x61084a1b26fdab1bULL, 0x2616457f04bd50baULL},
    {0x4da03b48ebfe227cULL, 0x1e783798d09773c8ULL},
    {0x7c33920e46636a60ULL, 0x30c058f480f252d9ULL},
    {0x635c74d8384f884dULL, 0x0d66ad9067284247ULL},
    {0x4f7d2a469372d370ULL, 0x711ef14052869b6cULL},
    {0x7f2eaa0a85848581ULL, 0x34fe4ecd50d75f14ULL},
    {0x65beee6ed136d134ULL, 0x2a650bd773df7f43ULL},
    {0x51658b8bda9240f6ULL, 0x551da312c319329cULL},
    {0x411e093caedb672bULL, 0x5db14f4235adc217ULL},
    {0x68300ec77e2bd845ULL, 0x7c4ee536bc49368aULL},
    {0x5359a56c64efe037ULL, 0x7d0bea92303a9208ULL},
    {0x42ae1df050bfe693ULL, 0x173cbba8269541a0ULL},
    {0x6ab02fe6e79970ebULL, 0x3ec792a6a422029aULL},
    {0x5559bfebec7ac0bcULL, 0x3239421ee9b4cee1ULL},
    {0x4447ccbcbd2f0096ULL, 0x5b6101b25490a581ULL},
    {0x6d3fadfac84b3424ULL, 0x2bce691d541aa268ULL},
    {0x576624c8a03c29b6ULL, 0x563eba7ddce21b87ULL},
    {0x45eb50a08030215eULL, 0x78322ecb171b4939ULL},
    {0x6fdee76733803564ULL, 0x59e9e47824f87527ULL},
    {0x597f1f85c2ccf783ULL, 0x6187e9f9b72d2a86ULL},
    {0x4798e6049bd72c69ULL, 0x346cbb2e2c242205ULL},
    {0x728e3cd42c8b7a42ULL, 0x20adf849e039d007ULL},
    {0x5ba4fd768a092e9bULL, 0x33be603b19c7d99fULL},
    {0x4950cac53b3a8bafULL, 0x42feb3627b0647b3ULL},
    {0x754e113b91f745e5ULL, 0x5197856a5e7072b8ULL},
    {0x5dd80dc941929e51ULL, 0x27ac6abb7ec05bc6ULL},
    {0x4b133e3a9adbb1daULL, 0x52f05562cbcd1638ULL},
    {0x781ec9f75e2c4fc4ULL, 0x1e4d556adfae89f3ULL},
    {0x6018a192b1bd0c9cULL, 0x7ea444557fbed4c3ULL},
    {0x4ce0814227ca707dULL, 0x4bb69d1132ff109cULL},
    {0x7b00ced03faa4d95ULL, 0x5f8a94e851981a93ULL},
    {0x62670bd9cc883e11ULL, 0x32d543ed0e134875ULL},
    {0x4eb8d647d6d364daULL, 0x5bddcff0d80f6d2bULL},
    {0x7df48a0c8aebd491ULL, 0x12fc7fe7c018aeabULL},
    {0x64c3a1a3a25643a7ULL, 0x28c9ffec99ad5889ULL},
    {0x509c814fb511cfb9ULL, 0x0707fff07af113a1ULL},
    {0x407d343fc40e3fc7ULL, 0x1f39998d2f2742e7ULL},
    {0x672eb9ffa016cc71ULL, 0x7ec28f484b7204a4ULL},
    {0x528bc7ffb345705bULL, 0x189ba5d36f8e6a1dULL},
    {0x42096ccc8f6ac048ULL, 0x7a161e42bfa521b1ULL},
    {0x69a8ae1418aacd41ULL, 0x435696d132a1cf81ULL},
    {0x5486f1a9ad557101ULL, 0x1c454574288172ceULL},
    {0x439f27baf1112734ULL, 0x169dd129ba0128a5ULL},
    {0x6c31d92b1b4ea520ULL, 0x242fb50f9001daa1ULL},
    {0x568e4755af721db3ULL, 0x368c90d940017bb4ULL},
    {0x453e9f77bf8e7e29ULL, 0x120a0d7a999ac95dULL},
    {0x6eca98bf98e3fd0eULL, 0x50101590f5c47561ULL},
    {0x58a213cc7a4ffda5ULL, 0x26734473f7d05de8ULL},
    {0x46e80fd6c83ffe1dULL, 0x6b8f69f65fd9e4b9ULL},
    {0x71734c8ad9fffcfcULL, 0x45b24323cc8fd45cULL},
    {0x5ac2a3a247fffd96ULL, 0x6af502830a0ca9e3ULL},
    {0x489bb61b6ccccadfULL, 0x08c402026e7087e9ULL},
    {0x742c569247ae1164ULL, 0x746cd003e3e73fdbULL},
    {0x5cf04541d2f1a783ULL, 0x76bd73364fec3315ULL},
    {0x4a59d101758e1f9cULL, 0x5efdf5c50cbcf5abULL},
    {0x76f61b3588e365c7ULL, 0x4b2fefa1adfb22abULL},
    {0x5f2b48f7a0b5eb06ULL, 0x08f3261af195b555ULL},
    {0x4c22a0c61a2b226bULL, 0x20c284e25ade2aabULL},
    {0x79d1013cf6ab6a45ULL, 0x1ad0d49d5e304444ULL},
    {0x617400fd9222bb6aULL, 0x48a7107de4f369d0ULL},
    {0x4df6673141b562bbULL, 0x53b8d9fe50c2bb0dULL},
    {0x7cbd71e869223792ULL, 0x52c15cca1ad12b48ULL},
    {0x63cac186ba81c60eULL, 0x75677d6e7bda8906ULL},
    {0x4fd5679efb9b04d8ULL, 0x5dec645863153a6cULL},
    {0x7fbbd8fe5f5e6e27ULL, 0x497a3a2704eec3dfULL},
};

#define GM_MASK63 0x7fffffffffffffffULL

static uint64_t gm_mulhi(uint64_t a, uint64_t b) {
    return (uint64_t)(((unsigned __int128)a * b) >> 64);
}

/* g * cp, scaled down by 2^125 and rounded to odd, so that comparisons with the scaled
 * interval ends stay exact. */
static uint64_t gm_rop(const uint64_t *g, uint64_t cp) {
    uint64_t x1 = gm_mulhi(g[1], cp);
    uint64_t y0 = g[0] * cp;
    uint64_t y1 = gm_mulhi(g[0], cp);
    uint64_t z = (y0 >> 1) + x1;
    uint64_t vbp = y1 + (z >> 63);
    return vbp | (((z & GM_MASK63) + GM_MASK63) >> 63);
}

/* The shortest decimal digits (no trailing zeros, at least one) that read back as the
 * same value at the given width, and the decimal exponent of the first digit. Among the
 * shortest digit strings that read back, the one nearest the exact value of x wins, and an
 * exact tie goes to the even last digit. x is positive and finite, and exactly representable
 * at that width. */
static void gm_shortest(double x, unsigned bits, char *digits, int *exp10) {
    /* x = c * 2^q; cmin is the smallest normal significand and qmin the exponent of the
     * subnormals. */
    uint64_t c, cmin;
    int q, qmin;
    if (bits == 16) {
        uint16_t h = gm_f64_to_f16(x);
        unsigned be = (h >> 10) & 0x1fu;
        c = (h & 0x3ffu) | (be != 0 ? 0x400u : 0u);
        q = be != 0 ? (int)be - 25 : -24;
        qmin = -24;
        cmin = 1u << 10;
    } else if (bits == 32) {
        float f = (float)x;
        uint32_t u;
        memcpy(&u, &f, 4);
        unsigned be = (u >> 23) & 0xffu;
        c = (u & 0x7fffffu) | (be != 0 ? 0x800000u : 0u);
        q = be != 0 ? (int)be - 150 : -149;
        qmin = -149;
        cmin = 1u << 23;
    } else {
        uint64_t u;
        memcpy(&u, &x, 8);
        unsigned be = (unsigned)((u >> 52) & 0x7ffu);
        c = (u & 0xfffffffffffffULL) | (be != 0 ? 1ULL << 52 : 0);
        q = be != 0 ? (int)be - 1075 : -1074;
        qmin = -1074;
        cmin = 1ULL << 52;
    }
    unsigned out = (unsigned)(c & 1); /* the interval ends belong to it iff c is even */
    uint64_t cb = c << 2, cbr = cb + 2, cbl;
    int k;
    if (c != cmin || q == qmin) {
        cbl = cb - 2;
        k = (int)(((int64_t)q * 661971961083LL) >> 41); /* floor(log10(2^q)) */
    } else {
        /* The interval below is half as wide: floor(log10(3/4 * 2^q)). */
        cbl = cb - 1;
        k = (int)(((int64_t)q * 661971961083LL - 274743187321LL) >> 41);
    }
    int h = q + (int)(((int64_t)(-k) * 913124641741LL) >> 38) + 2; /* floor(-k * log2(10)) */
    const uint64_t *g = gm_g10[k - GM_G10_KMIN];
    uint64_t vb = gm_rop(g, cb << h), vbl = gm_rop(g, cbl << h), vbr = gm_rop(g, cbr << h);
    uint64_t s = vb >> 2, t = s + 1, r;
    /* A candidate with one digit fewer is a multiple of ten around s; the interval is
     * narrower than ten units, so at most one of them lies in it. */
    uint64_t sp10 = s / 10 * 10, tp10 = sp10 + 10;
    int upin = vbl + out <= sp10 << 2, wpin = (tp10 << 2) + out <= vbr;
    if (upin != wpin) {
        r = upin ? sp10 : tp10;
    } else {
        int uin = vbl + out <= s << 2, win = (t << 2) + out <= vbr;
        if (uin != win) {
            r = uin ? s : t;
        } else {
            int64_t cmp = (int64_t)(vb - ((s + t) << 1));
            r = cmp < 0 || (cmp == 0 && (s & 1) == 0) ? s : t;
        }
    }
    /* The digits of r * 10^k, without trailing zeros. */
    while (r % 10 == 0) {
        r /= 10;
        k++;
    }
    char rev[24];
    int n = 0;
    for (; r > 0; r /= 10) rev[n++] = (char)('0' + r % 10);
    for (int i = 0; i < n; i++) digits[i] = rev[n - 1 - i];
    digits[n] = '\0';
    *exp10 = k + n - 1;
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

/* Reads an unsigned integer of n (at most 8) bytes. */
static uint64_t gm_load_uint(const uint8_t *p, uint32_t n) { return (uint64_t)gm_load_u128(p, n > 8 ? 8 : n); }

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

GM_API void gm_format(gm_buf *out, const gm_type *t, const void *vp, bool top) {
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
        const uint8_t *data;
        memcpy(&data, v, sizeof data);
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
        unsigned __int128 whole = gm_load_u128(v, t->size);
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
            uint8_t tmp[16] = {0};
            if (ft->kind == GM_K_UNION) {
                /* An enum's value is its tag at the tag offset. */
                gm_store_u128(tmp + ft->tag_offset, ft->tag_size, raw);
            } else {
                gm_store_u128(tmp, ft->size, raw);
            }
            gm_format(out, ft, tmp, false);
        }
        gm_fmt_ch(out, ')');
        break;
    }
    }
}

/* ---- format specs (spec §21.6.1) ----------------------------------------------- */

/* Writes head and body padded to the spec's width, counted in Unicode scalar values. */
static void gm_spec_pad(gm_buf *out, const char *head, size_t hn, const uint8_t *body, size_t bn,
                        const gm_spec *s, bool number) {
    size_t len = 0;
    for (size_t i = 0; i < hn; i++) len += ((uint8_t)head[i] & 0xc0) != 0x80;
    for (size_t i = 0; i < bn; i++) len += (body[i] & 0xc0) != 0x80;
    size_t gap = s->width > len ? s->width - len : 0, before, after;
    if (s->zero) {
        gm_buf_push(out, head, hn);
        for (size_t i = 0; i < gap; i++) gm_fmt_ch(out, '0');
        gm_buf_push(out, body, bn);
        return;
    }
    switch (s->align) {
    case 1: before = 0, after = gap; break;
    case 2: before = gap, after = 0; break;
    case 3: before = gap / 2, after = gap - gap / 2; break;
    default: before = number ? gap : 0, after = number ? 0 : gap; break;
    }
    for (size_t i = 0; i < before; i++) gm_fmt_ch(out, (char)s->fill);
    gm_buf_push(out, head, hn);
    gm_buf_push(out, body, bn);
    for (size_t i = 0; i < after; i++) gm_fmt_ch(out, (char)s->fill);
}

/* The magnitude of an integer in base 2^shift (2, 8 or 16) into body; returns whether it is
 * negative. */
static bool gm_spec_radix(gm_buf *body, const gm_type *t, const uint8_t *p, unsigned shift, bool upper) {
    size_t n = t->size;
    uint8_t *m = (uint8_t *)malloc(n ? n : 1);
    if (m == NULL) gm_trap("out of memory", NULL, 0, 0);
    /* Work on the little-endian bytes of the value. */
    for (size_t i = 0; i < n; i++) m[i] = *gm_ibp(p, n, i);
    unsigned bits = t->bits;
    /* Clear the bits above the width. */
    for (size_t i = 0; i < n; i++) {
        size_t lo = i * 8;
        if (lo >= bits) m[i] = 0;
        else if (lo + 8 > bits) m[i] &= (uint8_t)((1u << (bits - lo)) - 1);
    }
    bool neg = t->is_signed && bits > 0 && ((m[(bits - 1) / 8] >> ((bits - 1) % 8)) & 1);
    if (neg) {
        unsigned carry = 1;
        for (size_t i = 0; i < n; i++) {
            unsigned v = (uint8_t)~m[i] + carry;
            m[i] = (uint8_t)v;
            carry = v >> 8;
        }
        for (size_t i = 0; i < n; i++) {
            size_t lo = i * 8;
            if (lo >= bits) m[i] = 0;
            else if (lo + 8 > bits) m[i] &= (uint8_t)((1u << (bits - lo)) - 1);
        }
    }
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    size_t ndig = (bits + shift - 1) / shift;
    bool started = false;
    for (size_t d = ndig; d > 0; d--) {
        size_t bit = (d - 1) * shift;
        unsigned v = 0;
        for (unsigned k = 0; k < shift; k++) {
            size_t b = bit + k;
            if (b < (size_t)n * 8) v |= (unsigned)((m[b / 8] >> (b % 8)) & 1) << k;
        }
        if (v != 0) started = true;
        if (started) gm_fmt_ch(body, dig[v]);
    }
    if (!started) gm_fmt_ch(body, '0');
    free(m);
    return neg;
}

/* Rewrites printf's exponent ("e+05", "e-07") in the display style ("e5", "e-7"). */
static void gm_spec_exp(gm_buf *body, const char *s) {
    const char *e = strchr(s, 'e');
    gm_buf_push(body, s, (size_t)(e - s));
    int x = atoi(e + 1);
    char tmp[16];
    int k = snprintf(tmp, sizeof tmp, "e%d", x);
    gm_buf_push(body, tmp, (size_t)k);
}

GM_API void gm_format_spec(gm_buf *out, const gm_type *t, const void *vp, const gm_spec *s) {
    const uint8_t *v = (const uint8_t *)vp;
    gm_buf body = {0};
    bool neg = false, finite = true, nan = false, number = true;
    if (t->kind == GM_K_INT) {
        if (s->kind >= 1 && s->kind <= 4) {
            unsigned shift = s->kind <= 2 ? 4 : s->kind == 3 ? 3 : 1;
            neg = gm_spec_radix(&body, t, v, shift, s->kind == 2);
        } else {
            gm_fmt_int(&body, t, v);
            if (body.len > 0 && body.data[0] == '-') {
                neg = true;
                memmove(body.data, body.data + 1, --body.len);
            }
        }
    } else if (t->kind == GM_K_FLOAT) {
        double x;
        if (t->bits == 16) {
            uint16_t h;
            memcpy(&h, v, 2);
            x = gm_f16_to_f64(h);
        } else if (t->bits == 32) {
            float f;
            memcpy(&f, v, 4);
            x = (double)f;
        } else {
            memcpy(&x, v, 8);
        }
        nan = __builtin_isnan(x);
        finite = !nan && !__builtin_isinf(x);
        neg = !nan && __builtin_signbit(x);
        double a = __builtin_fabs(x);
        if (nan) {
            gm_fmt_str(&body, "nan");
        } else if (!finite) {
            gm_fmt_str(&body, "inf");
        } else if (s->kind == 5 && s->precision >= 0) {
            int k = snprintf(NULL, 0, "%.*e", s->precision, a);
            char *tmp = (char *)malloc((size_t)k + 1);
            if (tmp == NULL) gm_trap("out of memory", NULL, 0, 0);
            snprintf(tmp, (size_t)k + 1, "%.*e", s->precision, a);
            gm_spec_exp(&body, tmp);
            free(tmp);
        } else if (s->kind == 5) {
            if (a == 0) {
                gm_fmt_str(&body, "0e0");
            } else {
                char digits[24], tmp[16];
                int e;
                gm_shortest(a, t->bits, digits, &e);
                gm_fmt_ch(&body, digits[0]);
                if (digits[1] != '\0') {
                    gm_fmt_ch(&body, '.');
                    gm_fmt_str(&body, digits + 1);
                }
                int k = snprintf(tmp, sizeof tmp, "e%d", e);
                gm_buf_push(&body, tmp, (size_t)k);
            }
        } else if (s->precision >= 0) {
            int k = snprintf(NULL, 0, "%.*f", s->precision, a);
            char *tmp = (char *)malloc((size_t)k + 1);
            if (tmp == NULL) gm_trap("out of memory", NULL, 0, 0);
            snprintf(tmp, (size_t)k + 1, "%.*f", s->precision, a);
            gm_buf_push(&body, tmp, (size_t)k);
            free(tmp);
        } else {
            uint8_t pos[8];
            if (t->bits == 16) {
                uint16_t h = gm_f64_to_f16(a);
                memcpy(pos, &h, 2);
            } else if (t->bits == 32) {
                float f = (float)a;
                memcpy(pos, &f, 4);
            } else {
                memcpy(pos, &a, 8);
            }
            gm_fmt_float(&body, t, pos);
        }
    } else {
        number = false;
        gm_format(&body, t, v, true);
        gm_spec_pad(out, "", 0, body.data, body.len, s, false);
        gm_buf_free(&body);
        return;
    }
    char head[4];
    size_t hn = 0;
    if (neg) head[hn++] = '-';
    else if (s->plus && !nan) head[hn++] = '+';
    if (s->alt) {
        head[hn++] = '0';
        head[hn++] = s->kind <= 2 ? 'x' : s->kind == 3 ? 'o' : 'b';
    }
    if (s->zero && !finite) {
        gm_spec s2 = *s;
        s2.zero = 0;
        s2.align = 2;
        gm_spec_pad(out, head, hn, body.data, body.len, &s2, number);
    } else {
        gm_spec_pad(out, head, hn, body.data, body.len, s, number);
    }
    gm_buf_free(&body);
}

/* ---- ambient effects ---------------------------------------------------------- */

GM_API void gm_write_out(const uint8_t *p, size_t len) {
    if (len > 0) fwrite(p, 1, len, stdout);
#if defined(__FILC__)
    /* Fil-C ends the process on a memory error without running any code of ours, so
     * output must not wait in the buffer for a trap or exit to flush it. */
    fflush(stdout);
#endif
}

GM_API void gm_write_err(const uint8_t *p, size_t len) {
    if (len > 0) fwrite(p, 1, len, stderr);
    fflush(stderr);
}

GM_API void gm_flush(void) { fflush(stdout); }

typedef struct { const uint8_t *ptr; uintptr_t len; } gm_rt_str;
#ifdef _WIN32
static char *gm_utf8_arg(const wchar_t *w, size_t *len) {
    int wn = (int)wcslen(w);
    int n = wn ? WideCharToMultiByte(CP_UTF8, 0, w, wn, NULL, 0, NULL, NULL) : 0;
    char *s = (char *)malloc((size_t)n + 1);
    if (s == NULL) gm_trap("out of memory", NULL, 0, 0);
    if (n) WideCharToMultiByte(CP_UTF8, 0, w, wn, s, n, NULL, NULL);
    s[n] = 0;
    *len = (size_t)n;
    return s;
}
#endif
static gm_rt_str *gm_rt_args;
static size_t gm_rt_argc;

GM_API void gm_set_args(int argc, char **argv) {
    if (gm_rt_args != NULL || argc <= 0) return;
#ifdef _WIN32
    /* The arguments as UTF-8, from the command line (argv is in the ANSI code page). */
    int wc = 0;
    wchar_t **wv = CommandLineToArgvW(GetCommandLineW(), &wc);
    if (wv != NULL && wc > 0) {
        if (wc > argc) wc = argc;
        gm_rt_args = (gm_rt_str *)malloc((size_t)wc * sizeof(gm_rt_str));
        if (gm_rt_args == NULL) gm_trap("out of memory", NULL, 0, 0);
        for (int i = 0; i < wc; i++) {
            size_t len;
            gm_rt_args[i].ptr = (const uint8_t *)gm_utf8_arg(wv[i], &len);
            gm_rt_args[i].len = len;
        }
        gm_rt_argc = (size_t)wc;
        LocalFree(wv);
        return;
    }
#endif
    gm_rt_args = (gm_rt_str *)malloc((size_t)argc * sizeof(gm_rt_str));
    if (gm_rt_args == NULL) gm_trap("out of memory", NULL, 0, 0);
    for (int i = 0; i < argc; i++) {
        gm_rt_args[i].ptr = (const uint8_t *)argv[i];
        gm_rt_args[i].len = strlen(argv[i]);
    }
    gm_rt_argc = (size_t)argc;
}

GM_API const void *gm_args(size_t *count) {
    *count = gm_rt_argc;
    return gm_rt_args;
}

GM_API bool gm_env_var(const uint8_t *name, size_t len, const uint8_t **value, size_t *value_len) {
#ifdef _WIN32
    if (len > 0x7fffffff || memchr(name, 0, len) != NULL) return false;
    int wn = len ? MultiByteToWideChar(CP_UTF8, 0, (const char *)name, (int)len, NULL, 0) : 0;
    wchar_t *wk = (wchar_t *)malloc(((size_t)wn + 1) * sizeof(wchar_t));
    if (wk == NULL) gm_trap("out of memory", NULL, 0, 0);
    if (wn) MultiByteToWideChar(CP_UTF8, 0, (const char *)name, (int)len, wk, wn);
    wk[wn] = 0;
    DWORD need = GetEnvironmentVariableW(wk, NULL, 0);
    if (need == 0) {
        free(wk);
        return false;
    }
    wchar_t *wv = (wchar_t *)malloc((size_t)need * sizeof(wchar_t));
    if (wv == NULL) gm_trap("out of memory", NULL, 0, 0);
    DWORD got = GetEnvironmentVariableW(wk, wv, need);
    free(wk);
    if (got >= need) {
        free(wv);
        return false;
    }
    size_t vl;
    *value = (const uint8_t *)gm_utf8_arg(wv, &vl);   /* lives for the rest of the process */
    *value_len = vl;
    free(wv);
    return true;
#else
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
#endif
}

/* ---- system services ---------------------------------------------------------------- */

GM_API int64_t gm_io_error(int err) {
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
    case ECONNREFUSED: return -11;
    case ETIMEDOUT: return -12;
    default: return -1000 - (int64_t)err;
    }
}

#ifndef _WIN32
GM_API int64_t gm_sys_read(void *p, size_t n) {
    gm_flush();
    for (;;) {
        ssize_t r = read(0, p, n);
        if (r >= 0) return (int64_t)r;
        if (errno != EINTR) return gm_io_error(errno);
    }
}

GM_API uint64_t gm_clock_now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

GM_API int64_t gm_clock_wall(void) {
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    return (int64_t)t.tv_sec * 1000000000 + (int64_t)t.tv_nsec;
}

GM_API void gm_clock_sleep(uint64_t ns) {
    gm_flush();
    struct timespec t = {(time_t)(ns / 1000000000u), (long)(ns % 1000000000u)}, rest;
    while (nanosleep(&t, &rest) != 0 && errno == EINTR) t = rest;
}

GM_API void gm_rand_fill(void *vp, size_t n) {
    static int fd = -1;
    uint8_t *p = (uint8_t *)vp;
    gm_rt_acquire();
    if (fd < 0) fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    gm_rt_release();
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

GM_API int64_t gm_fs_open(const void *path, size_t len, uint32_t flags) {
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

GM_API int64_t gm_fs_close(int64_t f) { return GM_RET_ERR(close((int)f) != 0); }

GM_API int64_t gm_fs_read(int64_t f, void *p, size_t n) {
    for (;;) {
        ssize_t r = read((int)f, p, n);
        if (r >= 0) return (int64_t)r;
        if (errno != EINTR) return gm_io_error(errno);
    }
}

GM_API int64_t gm_fs_write(int64_t f, const void *p, size_t n) {
    for (;;) {
        ssize_t r = write((int)f, p, n);
        if (r >= 0) return (int64_t)r;
        if (errno != EINTR) return gm_io_error(errno);
    }
}

GM_API int64_t gm_fs_seek(int64_t f, int64_t offset, uint32_t whence) {
    if (whence > 2 || (whence == 0 && offset < 0)) return gm_io_error(EINVAL);
    off_t r = lseek((int)f, (off_t)offset, whence == 0 ? SEEK_SET : whence == 1 ? SEEK_CUR : SEEK_END);
    return r < 0 ? gm_io_error(errno) : (int64_t)r;
}

static int64_t gm_mode_kind(mode_t mode) {
    return S_ISREG(mode) ? 0 : S_ISDIR(mode) ? 1 : S_ISLNK(mode) ? 2 : 3;
}

static int64_t gm_stat_path(const void *path, size_t len, void *out, size_t n, bool follow) {
    char *s = gm_path(path, len);
    if (s == NULL) return gm_io_error(EINVAL);
    struct stat st;
    int r = follow ? stat(s, &st) : lstat(s, &st);
    int err = errno;
    free(s);
    if (r != 0) return gm_io_error(err);
#if defined(__APPLE__)
    int64_t mtime = (int64_t)st.st_mtimespec.tv_sec * 1000000000 + st.st_mtimespec.tv_nsec;
#else
    int64_t mtime = (int64_t)st.st_mtim.tv_sec * 1000000000 + st.st_mtim.tv_nsec;
#endif
    int64_t v[3] = {(int64_t)st.st_size, gm_mode_kind(st.st_mode), mtime};
    memcpy(out, v, sizeof(int64_t) * (n < 3 ? n : 3));
    return 0;
}

GM_API int64_t gm_fs_stat(const void *path, size_t len, void *out, size_t n) {
    return gm_stat_path(path, len, out, n, true);
}

GM_API int64_t gm_fs_lstat(const void *path, size_t len, void *out, size_t n) {
    return gm_stat_path(path, len, out, n, false);
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

GM_API int64_t gm_fs_remove(const void *path, size_t len) { return gm_path_call(path, len, unlink); }
GM_API int64_t gm_fs_make_dir(const void *path, size_t len) { return gm_path_call(path, len, gm_mkdir); }
GM_API int64_t gm_fs_remove_dir(const void *path, size_t len) { return gm_path_call(path, len, rmdir); }

GM_API int64_t gm_fs_rename(const void *from, size_t from_len, const void *to, size_t to_len) {
    char *a = gm_path(from, from_len), *b = gm_path(to, to_len);
    int r = a && b ? rename(a, b) : -1;
    int err = a && b ? errno : EINVAL;
    free(a);
    free(b);
    return r != 0 ? gm_io_error(err) : 0;
}

static DIR **gm_dirs;
static size_t gm_ndirs;

GM_API int64_t gm_fs_open_dir(const void *path, size_t len) {
    char *s = gm_path(path, len);
    if (s == NULL) return gm_io_error(EINVAL);
    DIR *d = opendir(s);
    int err = errno;
    free(s);
    if (d == NULL) return gm_io_error(err);
    gm_rt_acquire();
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
    gm_rt_release();
    return (int64_t)i + 1;
}

static DIR *gm_dir(int64_t d) {
    gm_rt_acquire();
    DIR *dir = d >= 1 && (size_t)d <= gm_ndirs ? gm_dirs[d - 1] : NULL;
    gm_rt_release();
    return dir;
}

GM_API int64_t gm_fs_next_entry(int64_t d, void *p, size_t n) {
    DIR *dir = gm_dir(d);
    if (dir == NULL) return gm_io_error(EBADF);
    for (;;) {
        errno = 0;
        struct dirent *e = readdir(dir);
        if (e == NULL) return errno ? gm_io_error(errno) : 0;
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        size_t len = strlen(e->d_name);
        if (len > n) return gm_io_error(EINVAL);
        int64_t kind;
        switch (e->d_type) {
        case DT_REG: kind = 0; break;
        case DT_DIR: kind = 1; break;
        case DT_LNK: kind = 2; break;
        case DT_UNKNOWN: {
            struct stat st;
            if (fstatat(dirfd(dir), e->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) return gm_io_error(errno);
            kind = gm_mode_kind(st.st_mode);
            break;
        }
        default: kind = 3;
        }
        memcpy(p, e->d_name, len);
        return (int64_t)len + (kind << 32);
    }
}

GM_API int64_t gm_fs_close_dir(int64_t d) {
    DIR *dir = gm_dir(d);
    if (dir == NULL) return gm_io_error(EBADF);
    gm_rt_acquire();
    gm_dirs[d - 1] = NULL;
    gm_rt_release();
    return GM_RET_ERR(closedir(dir) != 0);
}

GM_API int64_t gm_fs_truncate(int64_t f, uint64_t size) {
    if (size > (uint64_t)INT64_MAX) return gm_io_error(EINVAL);
    return GM_RET_ERR(ftruncate((int)f, (off_t)size) != 0);
}

GM_API int64_t gm_fs_sync(int64_t f) { return GM_RET_ERR(fsync((int)f) != 0); }

GM_API int64_t gm_fs_cwd(void *p, size_t n) {
    char tmp[4096];
    if (getcwd(tmp, sizeof tmp) == NULL) return gm_io_error(errno);
    size_t len = strlen(tmp);
    if (len > n) return gm_io_error(EINVAL);
    memcpy(p, tmp, len);
    return (int64_t)len;
}

/* Resolves host:port for a stream socket (passive: for listening). */
static struct addrinfo *gm_resolve(const void *host, size_t len, uint16_t port, bool passive, int64_t *err) {
    char *h = gm_path(host, len);
    if (h == NULL) {
        *err = gm_io_error(EINVAL);
        return NULL;
    }
    char service[8];
    snprintf(service, sizeof service, "%u", (unsigned)port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = passive ? AI_PASSIVE : 0;
    struct addrinfo *res = NULL;
    int r = getaddrinfo(len ? h : NULL, service, &hints, &res);
    free(h);
    if (r != 0) {
        *err = -1000;
        return NULL;
    }
    return res;
}

static int gm_socket(const struct addrinfo *a) {
    int fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
    if (fd < 0) return fd;
#if defined(SO_NOSIGPIPE)
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    return fd;
}

GM_API int64_t gm_net_connect(const void *host, size_t len, uint16_t port) {
    int64_t err = 0;
    struct addrinfo *res = gm_resolve(host, len, port, false, &err);
    if (res == NULL) return err;
    int fd = -1, e = 0;
    for (struct addrinfo *a = res; a != NULL; a = a->ai_next) {
        fd = gm_socket(a);
        if (fd < 0) { e = errno; continue; }
        int r;
        do r = connect(fd, a->ai_addr, a->ai_addrlen); while (r != 0 && errno == EINTR);
        if (r == 0) break;
        e = errno;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd < 0 ? gm_io_error(e) : (int64_t)fd;
}

GM_API int64_t gm_net_listen(const void *host, size_t len, uint16_t port) {
    int64_t err = 0;
    struct addrinfo *res = gm_resolve(host, len, port, true, &err);
    if (res == NULL) return err;
    int fd = -1, e = 0;
    for (struct addrinfo *a = res; a != NULL; a = a->ai_next) {
        fd = gm_socket(a);
        if (fd < 0) { e = errno; continue; }
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        if (bind(fd, a->ai_addr, a->ai_addrlen) == 0 && listen(fd, 128) == 0) break;
        e = errno;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd < 0 ? gm_io_error(e) : (int64_t)fd;
}

GM_API int64_t gm_net_local_port(int64_t h) {
    struct sockaddr_storage a;
    socklen_t n = sizeof a;
    if (getsockname((int)h, (struct sockaddr *)&a, &n) != 0) return gm_io_error(errno);
    if (a.ss_family == AF_INET) return ntohs(((struct sockaddr_in *)&a)->sin_port);
    if (a.ss_family == AF_INET6) return ntohs(((struct sockaddr_in6 *)&a)->sin6_port);
    return gm_io_error(EINVAL);
}

GM_API int64_t gm_net_accept(int64_t h) {
    for (;;) {
        int fd = accept((int)h, NULL, NULL);
        if (fd >= 0) {
#if defined(SO_NOSIGPIPE)
            int one = 1;
            setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
            return fd;
        }
        if (errno != EINTR) return gm_io_error(errno);
    }
}

/* A timed-out blocking call fails with EAGAIN; sockets are never non-blocking. */
static int64_t gm_net_error(int err) {
    return err == EAGAIN || err == EWOULDBLOCK ? gm_io_error(ETIMEDOUT) : gm_io_error(err);
}

GM_API int64_t gm_net_send(int64_t h, const void *p, size_t n) {
#if defined(MSG_NOSIGNAL)
    int flags = MSG_NOSIGNAL;
#else
    int flags = 0;
#endif
    for (;;) {
        ssize_t r = send((int)h, p, n, flags);
        if (r >= 0) return (int64_t)r;
        if (errno != EINTR) return gm_net_error(errno);
    }
}

GM_API int64_t gm_net_recv(int64_t h, void *p, size_t n) {
    for (;;) {
        ssize_t r = recv((int)h, p, n, 0);
        if (r >= 0) return (int64_t)r;
        if (errno != EINTR) return gm_net_error(errno);
    }
}

GM_API int64_t gm_net_close(int64_t h) { return GM_RET_ERR(close((int)h) != 0); }

GM_API int64_t gm_net_shutdown(int64_t h, uint32_t how) {
    return GM_RET_ERR(shutdown((int)h, how == 0 ? SHUT_RD : how == 1 ? SHUT_WR : SHUT_RDWR) != 0);
}

static int gm_set_timeout(int fd, int opt, uint64_t ns) {
    struct timeval tv;
    tv.tv_sec = (time_t)(ns / 1000000000u);
    tv.tv_usec = (suseconds_t)((ns % 1000000000u) / 1000u);
    if (ns > 0 && tv.tv_sec == 0 && tv.tv_usec == 0) tv.tv_usec = 1;   /* zero would mean no timeout */
    return setsockopt(fd, SOL_SOCKET, opt, &tv, sizeof tv);
}

GM_API int64_t gm_net_set_timeouts(int64_t h, uint64_t recv_ns, uint64_t send_ns) {
    if (gm_set_timeout((int)h, SO_RCVTIMEO, recv_ns) != 0) return gm_io_error(errno);
    return GM_RET_ERR(gm_set_timeout((int)h, SO_SNDTIMEO, send_ns) != 0);
}

GM_API int64_t gm_net_peer(int64_t h, void *p, size_t n) {
    struct sockaddr_storage a;
    socklen_t al = sizeof a;
    if (getpeername((int)h, (struct sockaddr *)&a, &al) != 0) return gm_io_error(errno);
    char text[INET6_ADDRSTRLEN + 16];
    int len;
    if (a.ss_family == AF_INET) {
        struct sockaddr_in *s = (struct sockaddr_in *)&a;
        char host[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &s->sin_addr, host, sizeof host);
        len = snprintf(text, sizeof text, "%s:%u", host, (unsigned)ntohs(s->sin_port));
    } else if (a.ss_family == AF_INET6) {
        struct sockaddr_in6 *s = (struct sockaddr_in6 *)&a;
        char host[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &s->sin6_addr, host, sizeof host);
        len = snprintf(text, sizeof text, "[%s]:%u", host, (unsigned)ntohs(s->sin6_port));
    } else {
        return gm_io_error(EINVAL);
    }
    if (len < 0 || (size_t)len > n) return gm_io_error(EINVAL);
    memcpy(p, text, (size_t)len);
    return len;
}
#else
/* Windows: the system services on the Win32 API and Winsock 2. Paths and names cross the
 * boundary as UTF-8 and are converted to UTF-16 for the wide-character calls. Files are
 * C runtime descriptors (so a handle is an int, as elsewhere) opened in binary mode;
 * sockets are SOCKET values. */

/* The error code (see gm_io_error) of the Win32 error `e`. The kinds the interpreter
 * distinguishes map to the same codes as there; any other error is Other(e). */
static int64_t gm_win_err(DWORD e) {
    switch (e) {
    case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: case ERROR_INVALID_DRIVE:
    case ERROR_BAD_NETPATH: case ERROR_BAD_NET_NAME:
        return gm_io_error(ENOENT);
    case ERROR_ACCESS_DENIED: return gm_io_error(EACCES);
    case ERROR_ALREADY_EXISTS: case ERROR_FILE_EXISTS: return gm_io_error(EEXIST);
    case ERROR_INVALID_PARAMETER: return gm_io_error(EINVAL);
    case ERROR_DISK_FULL: case ERROR_HANDLE_DISK_FULL: return gm_io_error(ENOSPC);
    case ERROR_BROKEN_PIPE: return gm_io_error(EPIPE);
    case ERROR_SEM_TIMEOUT: case WAIT_TIMEOUT: return gm_io_error(ETIMEDOUT);
    default: return -1000 - (int64_t)e;
    }
}

/* The error code of the Winsock error `e`. */
static int64_t gm_wsa_err(int e) {
    switch (e) {
    case WSAEINTR: return gm_io_error(EINTR);
    case WSAEWOULDBLOCK: return gm_io_error(EAGAIN);
    case WSAEACCES: return gm_io_error(EACCES);
    case WSAEINVAL: return gm_io_error(EINVAL);
    case WSAECONNRESET: case WSAECONNABORTED: return gm_io_error(ECONNRESET);
    case WSAECONNREFUSED: return gm_io_error(ECONNREFUSED);
    case WSAETIMEDOUT: return gm_io_error(ETIMEDOUT);
    case WSAESHUTDOWN: return gm_io_error(EPIPE);
    default: return -1000 - (int64_t)e;
    }
}

/* A NUL-terminated UTF-16 copy of the UTF-8 text at p (freed by the caller), or NULL if it
 * contains a NUL or is not valid UTF-8. */
static wchar_t *gm_wide(const void *p, size_t n) {
    if (memchr(p, 0, n) != NULL || n > 0x7fffffff) return NULL;
    int wn = n ? MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (const char *)p, (int)n, NULL, 0) : 0;
    if (n && wn == 0) return NULL;
    wchar_t *w = (wchar_t *)malloc(((size_t)wn + 1) * sizeof(wchar_t));
    if (w == NULL) gm_trap("out of memory", NULL, 0, 0);
    if (wn) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (const char *)p, (int)n, w, wn);
    w[wn] = 0;
    return w;
}

/* The UTF-8 form of the UTF-16 text w (wn units) in a new NUL-terminated buffer; *len is its length. */
static char *gm_utf8(const wchar_t *w, int wn, size_t *len) {
    int n = wn ? WideCharToMultiByte(CP_UTF8, 0, w, wn, NULL, 0, NULL, NULL) : 0;
    char *s = (char *)malloc((size_t)n + 1);
    if (s == NULL) gm_trap("out of memory", NULL, 0, 0);
    if (n) WideCharToMultiByte(CP_UTF8, 0, w, wn, s, n, NULL, NULL);
    s[n] = 0;
    if (len) *len = (size_t)n;
    return s;
}

static char *gm_path(const void *p, size_t n) {
    if (memchr(p, 0, n) != NULL) return NULL;
    char *s = (char *)malloc(n + 1);
    if (s == NULL) gm_trap("out of memory", NULL, 0, 0);
    memcpy(s, p, n);
    s[n] = 0;
    return s;
}

#define GM_RET_ERR(cond) ((cond) ? gm_io_error(errno) : 0)
#define GM_CHUNK ((size_t)1 << 30)

GM_API int64_t gm_sys_read(void *p, size_t n) {
    gm_flush();
    for (;;) {
        int r = _read(0, p, (unsigned)(n < GM_CHUNK ? n : GM_CHUNK));
        if (r >= 0) return (int64_t)r;
        if (errno != EINTR) return gm_io_error(errno);
    }
}

GM_API uint64_t gm_clock_now(void) {
    static LARGE_INTEGER freq;
    LARGE_INTEGER c;
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&c);
    uint64_t f = (uint64_t)freq.QuadPart, v = (uint64_t)c.QuadPart;
    return (v / f) * 1000000000u + (v % f) * 1000000000u / f;
}

GM_API int64_t gm_clock_wall(void) {
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;   /* 100 ns since 1601 */
    return ((int64_t)t - 116444736000000000LL) * 100;
}

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x2
#endif

GM_API void gm_clock_sleep(uint64_t ns) {
    gm_flush();
    HANDLE t = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (t != NULL) {
        LARGE_INTEGER due;
        due.QuadPart = -(int64_t)((ns + 99) / 100);
        if (due.QuadPart < 0 && SetWaitableTimer(t, &due, 0, NULL, NULL, FALSE)) {
            WaitForSingleObject(t, INFINITE);
            CloseHandle(t);
            return;
        }
        CloseHandle(t);
    }
    uint64_t ms = (ns + 999999) / 1000000;
    while (ms > 0) {
        DWORD step = ms > 0x7fffffffu ? 0x7fffffffu : (DWORD)ms;
        Sleep(step);
        ms -= step;
    }
}

GM_API void gm_rand_fill(void *vp, size_t n) {
    uint8_t *p = (uint8_t *)vp;
    while (n > 0) {
        ULONG k = n > 0x10000000u ? 0x10000000u : (ULONG)n;
        if (BCryptGenRandom(NULL, p, k, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
            gm_trap("cannot read random bytes", NULL, 0, 0);
        p += k;
        n -= k;
    }
}

/* Opens a file as a C runtime descriptor. All sharing is allowed, including deletion of an
 * open file, as for the interpreter. */
GM_API int64_t gm_fs_open(const void *path, size_t len, uint32_t flags) {
    wchar_t *w = gm_wide(path, len);
    if (w == NULL) return gm_io_error(EINVAL);
    bool rd = (flags & GM_FS_READ) || !(flags & (GM_FS_WRITE | GM_FS_APPEND));
    bool wr = (flags & GM_FS_WRITE) != 0, ap = (flags & GM_FS_APPEND) != 0;
    DWORD access = (rd ? GENERIC_READ : 0) | (wr ? GENERIC_WRITE : 0) | (ap && !wr ? FILE_APPEND_DATA : 0);
    DWORD disp = OPEN_EXISTING;
    if (flags & GM_FS_EXCLUSIVE) disp = CREATE_NEW;
    else if ((flags & GM_FS_CREATE) && (flags & GM_FS_TRUNCATE)) disp = CREATE_ALWAYS;
    else if (flags & GM_FS_CREATE) disp = OPEN_ALWAYS;
    else if (flags & GM_FS_TRUNCATE) disp = TRUNCATE_EXISTING;
    HANDLE h = CreateFileW(w, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, disp,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD err = GetLastError();
    free(w);
    if (h == INVALID_HANDLE_VALUE) return gm_win_err(err);
    int fd = _open_osfhandle((intptr_t)h, _O_BINARY | (ap ? _O_APPEND : 0) | (rd && !wr && !ap ? _O_RDONLY : 0));
    if (fd < 0) {
        CloseHandle(h);
        return gm_io_error(EMFILE);
    }
    return (int64_t)fd;
}

GM_API int64_t gm_fs_close(int64_t f) { return GM_RET_ERR(_close((int)f) != 0); }

GM_API int64_t gm_fs_read(int64_t f, void *p, size_t n) {
    for (;;) {
        int r = _read((int)f, p, (unsigned)(n < GM_CHUNK ? n : GM_CHUNK));
        if (r >= 0) return (int64_t)r;
        if (errno != EINTR) return gm_io_error(errno);
    }
}

GM_API int64_t gm_fs_write(int64_t f, const void *p, size_t n) {
    for (;;) {
        int r = _write((int)f, p, (unsigned)(n < GM_CHUNK ? n : GM_CHUNK));
        if (r >= 0) return (int64_t)r;
        if (errno != EINTR) return gm_io_error(errno);
    }
}

GM_API int64_t gm_fs_seek(int64_t f, int64_t offset, uint32_t whence) {
    if (whence > 2 || (whence == 0 && offset < 0)) return gm_io_error(EINVAL);
    __int64 r = _lseeki64((int)f, (__int64)offset, whence == 0 ? SEEK_SET : whence == 1 ? SEEK_CUR : SEEK_END);
    return r < 0 ? gm_io_error(errno) : (int64_t)r;
}

static int64_t gm_attr_kind(DWORD attrs, DWORD tag) {
    if ((attrs & FILE_ATTRIBUTE_REPARSE_POINT) && (tag == IO_REPARSE_TAG_SYMLINK || tag == IO_REPARSE_TAG_MOUNT_POINT))
        return 2;
    return (attrs & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
}

static int64_t gm_stat_path(const void *path, size_t len, void *out, size_t n, bool follow) {
    wchar_t *w = gm_wide(path, len);
    if (w == NULL) return gm_io_error(EINVAL);
    HANDLE h = CreateFileW(w, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | (follow ? 0 : FILE_FLAG_OPEN_REPARSE_POINT), NULL);
    DWORD err = GetLastError();
    if (h == INVALID_HANDLE_VALUE) {
        free(w);
        return gm_win_err(err);
    }
    BY_HANDLE_FILE_INFORMATION bi;
    BOOL ok = GetFileInformationByHandle(h, &bi);
    err = GetLastError();
    CloseHandle(h);
    if (!ok) {
        free(w);
        return gm_win_err(err);
    }
    DWORD tag = 0;
    if (!follow && (bi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        WIN32_FIND_DATAW fd;
        HANDLE fh = FindFirstFileW(w, &fd);
        if (fh != INVALID_HANDLE_VALUE) {
            tag = fd.dwReserved0;
            FindClose(fh);
        }
    }
    free(w);
    uint64_t t = ((uint64_t)bi.ftLastWriteTime.dwHighDateTime << 32) | bi.ftLastWriteTime.dwLowDateTime;
    int64_t mtime = ((int64_t)t - 116444736000000000LL) * 100;
    int64_t size = (int64_t)(((uint64_t)bi.nFileSizeHigh << 32) | bi.nFileSizeLow);
    int64_t v[3] = {size, gm_attr_kind(bi.dwFileAttributes, tag), mtime};
    memcpy(out, v, sizeof(int64_t) * (n < 3 ? n : 3));
    return 0;
}

GM_API int64_t gm_fs_stat(const void *path, size_t len, void *out, size_t n) {
    return gm_stat_path(path, len, out, n, true);
}

GM_API int64_t gm_fs_lstat(const void *path, size_t len, void *out, size_t n) {
    return gm_stat_path(path, len, out, n, false);
}

GM_API int64_t gm_fs_remove(const void *path, size_t len) {
    wchar_t *w = gm_wide(path, len);
    if (w == NULL) return gm_io_error(EINVAL);
    BOOL ok = DeleteFileW(w);
    DWORD err = GetLastError();
    if (!ok && err == ERROR_ACCESS_DENIED) {
        /* A read-only file can be removed on POSIX systems. */
        DWORD a = GetFileAttributesW(w);
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_READONLY) && !(a & FILE_ATTRIBUTE_DIRECTORY)) {
            SetFileAttributesW(w, a & ~(DWORD)FILE_ATTRIBUTE_READONLY);
            ok = DeleteFileW(w);
            if (!ok) SetFileAttributesW(w, a);
            err = GetLastError();
        }
    }
    free(w);
    return ok ? 0 : gm_win_err(err);
}

GM_API int64_t gm_fs_make_dir(const void *path, size_t len) {
    wchar_t *w = gm_wide(path, len);
    if (w == NULL) return gm_io_error(EINVAL);
    BOOL ok = CreateDirectoryW(w, NULL);
    DWORD err = GetLastError();
    free(w);
    return ok ? 0 : gm_win_err(err);
}

GM_API int64_t gm_fs_remove_dir(const void *path, size_t len) {
    wchar_t *w = gm_wide(path, len);
    if (w == NULL) return gm_io_error(EINVAL);
    BOOL ok = RemoveDirectoryW(w);
    DWORD err = GetLastError();
    free(w);
    return ok ? 0 : gm_win_err(err);
}

GM_API int64_t gm_fs_rename(const void *from, size_t from_len, const void *to, size_t to_len) {
    wchar_t *a = gm_wide(from, from_len), *b = gm_wide(to, to_len);
    BOOL ok = FALSE;
    DWORD err = ERROR_INVALID_PARAMETER;
    if (a && b) {
        ok = MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING);
        err = GetLastError();
    }
    free(a);
    free(b);
    return ok ? 0 : gm_win_err(err);
}

typedef struct { HANDLE h; WIN32_FIND_DATAW fd; bool pending; } gm_wdir;
static gm_wdir **gm_dirs;
static size_t gm_ndirs;

GM_API int64_t gm_fs_open_dir(const void *path, size_t len) {
    wchar_t *w = gm_wide(path, len);
    if (w == NULL) return gm_io_error(EINVAL);
    size_t wl = wcslen(w);
    wchar_t *pat = (wchar_t *)malloc((wl + 3) * sizeof(wchar_t));
    if (pat == NULL) gm_trap("out of memory", NULL, 0, 0);
    memcpy(pat, w, wl * sizeof(wchar_t));
    if (wl > 0 && w[wl - 1] != L'\\' && w[wl - 1] != L'/') pat[wl++] = L'\\';
    pat[wl++] = L'*';
    pat[wl] = 0;
    free(w);
    gm_wdir *d = (gm_wdir *)malloc(sizeof *d);
    if (d == NULL) gm_trap("out of memory", NULL, 0, 0);
    d->h = FindFirstFileW(pat, &d->fd);
    DWORD err = GetLastError();
    free(pat);
    if (d->h == INVALID_HANDLE_VALUE) {
        free(d);
        return gm_win_err(err);
    }
    d->pending = true;
    gm_rt_acquire();
    size_t i = 0;
    while (i < gm_ndirs && gm_dirs[i] != NULL) i++;
    if (i == gm_ndirs) {
        gm_wdir **t = (gm_wdir **)realloc(gm_dirs, sizeof(gm_wdir *) * (gm_ndirs * 2 + 4));
        if (t == NULL) gm_trap("out of memory", NULL, 0, 0);
        memset(t + gm_ndirs, 0, sizeof(gm_wdir *) * (gm_ndirs + 4));
        gm_dirs = t;
        gm_ndirs = gm_ndirs * 2 + 4;
    }
    gm_dirs[i] = d;
    gm_rt_release();
    return (int64_t)i + 1;
}

static gm_wdir *gm_dir(int64_t d) {
    gm_rt_acquire();
    gm_wdir *dir = d >= 1 && (size_t)d <= gm_ndirs ? gm_dirs[d - 1] : NULL;
    gm_rt_release();
    return dir;
}

GM_API int64_t gm_fs_next_entry(int64_t di, void *p, size_t n) {
    gm_wdir *d = gm_dir(di);
    if (d == NULL) return gm_io_error(EBADF);
    for (;;) {
        if (!d->pending) {
            if (!FindNextFileW(d->h, &d->fd)) {
                DWORD err = GetLastError();
                return err == ERROR_NO_MORE_FILES ? 0 : gm_win_err(err);
            }
        }
        d->pending = false;
        const wchar_t *name = d->fd.cFileName;
        if (wcscmp(name, L".") == 0 || wcscmp(name, L"..") == 0) continue;
        size_t len;
        char *u = gm_utf8(name, (int)wcslen(name), &len);
        if (len > n) {
            free(u);
            return gm_io_error(EINVAL);
        }
        memcpy(p, u, len);
        free(u);
        return (int64_t)len + (gm_attr_kind(d->fd.dwFileAttributes, d->fd.dwReserved0) << 32);
    }
}

GM_API int64_t gm_fs_close_dir(int64_t di) {
    gm_wdir *d = gm_dir(di);
    if (d == NULL) return gm_io_error(EBADF);
    gm_rt_acquire();
    gm_dirs[di - 1] = NULL;
    gm_rt_release();
    FindClose(d->h);
    free(d);
    return 0;
}

GM_API int64_t gm_fs_truncate(int64_t f, uint64_t size) {
    if (size > (uint64_t)INT64_MAX) return gm_io_error(EINVAL);
    errno_t e = _chsize_s((int)f, (__int64)size);
    return e != 0 ? gm_io_error(e) : 0;
}

GM_API int64_t gm_fs_sync(int64_t f) { return GM_RET_ERR(_commit((int)f) != 0); }

GM_API int64_t gm_fs_cwd(void *p, size_t n) {
    wchar_t tmp[32768];
    DWORD wl = GetCurrentDirectoryW(32768, tmp);
    if (wl == 0 || wl >= 32768) return gm_win_err(GetLastError());
    size_t len;
    char *u = gm_utf8(tmp, (int)wl, &len);
    if (len > n) {
        free(u);
        return gm_io_error(EINVAL);
    }
    memcpy(p, u, len);
    free(u);
    return (int64_t)len;
}

/* Winsock must be started once per process before any socket call. */
static void gm_net_init(void) {
    static volatile LONG state;   /* 0 not started, 1 started */
    if (state) return;
    gm_rt_acquire();
    if (!state) {
        WSADATA d;
        WSAStartup(MAKEWORD(2, 2), &d);
        state = 1;
    }
    gm_rt_release();
}

/* Resolves host:port for a stream socket (passive: for listening). */
static struct addrinfo *gm_resolve(const void *host, size_t len, uint16_t port, bool passive, int64_t *err) {
    gm_net_init();
    char *h = gm_path(host, len);
    if (h == NULL) {
        *err = gm_io_error(EINVAL);
        return NULL;
    }
    char service[8];
    snprintf(service, sizeof service, "%u", (unsigned)port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = passive ? AI_PASSIVE : 0;
    struct addrinfo *res = NULL;
    int r = getaddrinfo(len ? h : NULL, service, &hints, &res);
    free(h);
    if (r != 0) {
        *err = -1000;
        return NULL;
    }
    return res;
}

GM_API int64_t gm_net_connect(const void *host, size_t len, uint16_t port) {
    int64_t err = 0;
    struct addrinfo *res = gm_resolve(host, len, port, false, &err);
    if (res == NULL) return err;
    SOCKET fd = INVALID_SOCKET;
    int e = 0;
    for (struct addrinfo *a = res; a != NULL; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd == INVALID_SOCKET) { e = WSAGetLastError(); continue; }
        if (connect(fd, a->ai_addr, (int)a->ai_addrlen) == 0) break;
        e = WSAGetLastError();
        closesocket(fd);
        fd = INVALID_SOCKET;
    }
    freeaddrinfo(res);
    return fd == INVALID_SOCKET ? gm_wsa_err(e) : (int64_t)fd;
}

GM_API int64_t gm_net_listen(const void *host, size_t len, uint16_t port) {
    int64_t err = 0;
    struct addrinfo *res = gm_resolve(host, len, port, true, &err);
    if (res == NULL) return err;
    SOCKET fd = INVALID_SOCKET;
    int e = 0;
    for (struct addrinfo *a = res; a != NULL; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd == INVALID_SOCKET) { e = WSAGetLastError(); continue; }
        /* No SO_REUSEADDR: on Windows it would let another socket take over a used port. */
        if (bind(fd, a->ai_addr, (int)a->ai_addrlen) == 0 && listen(fd, 128) == 0) break;
        e = WSAGetLastError();
        closesocket(fd);
        fd = INVALID_SOCKET;
    }
    freeaddrinfo(res);
    return fd == INVALID_SOCKET ? gm_wsa_err(e) : (int64_t)fd;
}

GM_API int64_t gm_net_local_port(int64_t h) {
    struct sockaddr_storage a;
    int n = sizeof a;
    if (getsockname((SOCKET)h, (struct sockaddr *)&a, &n) != 0) return gm_wsa_err(WSAGetLastError());
    if (a.ss_family == AF_INET) return ntohs(((struct sockaddr_in *)&a)->sin_port);
    if (a.ss_family == AF_INET6) return ntohs(((struct sockaddr_in6 *)&a)->sin6_port);
    return gm_io_error(EINVAL);
}

GM_API int64_t gm_net_accept(int64_t h) {
    for (;;) {
        SOCKET fd = accept((SOCKET)h, NULL, NULL);
        if (fd != INVALID_SOCKET) return (int64_t)fd;
        int e = WSAGetLastError();
        if (e != WSAEINTR) return gm_wsa_err(e);
    }
}

/* A timed-out blocking call fails with WSAETIMEDOUT; sockets are never non-blocking. */
static int64_t gm_net_error(int e) {
    return e == WSAEWOULDBLOCK ? gm_io_error(ETIMEDOUT) : gm_wsa_err(e);
}

GM_API int64_t gm_net_send(int64_t h, const void *p, size_t n) {
    for (;;) {
        int r = send((SOCKET)h, (const char *)p, (int)(n < GM_CHUNK ? n : GM_CHUNK), 0);
        if (r >= 0) return (int64_t)r;
        int e = WSAGetLastError();
        if (e != WSAEINTR) return gm_net_error(e);
    }
}

GM_API int64_t gm_net_recv(int64_t h, void *p, size_t n) {
    for (;;) {
        int r = recv((SOCKET)h, (char *)p, (int)(n < GM_CHUNK ? n : GM_CHUNK), 0);
        if (r >= 0) return (int64_t)r;
        int e = WSAGetLastError();
        if (e == WSAECONNRESET) return gm_io_error(ECONNRESET);
        if (e != WSAEINTR) return gm_net_error(e);
    }
}

GM_API int64_t gm_net_close(int64_t h) { return closesocket((SOCKET)h) != 0 ? gm_wsa_err(WSAGetLastError()) : 0; }

GM_API int64_t gm_net_shutdown(int64_t h, uint32_t how) {
    /* SD_RECEIVE, SD_SEND and SD_BOTH are 0, 1 and 2. */
    return shutdown((SOCKET)h, how == 0 ? SD_RECEIVE : how == 1 ? SD_SEND : SD_BOTH) != 0
               ? gm_wsa_err(WSAGetLastError()) : 0;
}

static int gm_set_timeout(SOCKET fd, int opt, uint64_t ns) {
    DWORD ms = (DWORD)(ns / 1000000u);
    if (ns > 0 && ms == 0) ms = 1;   /* zero would mean no timeout */
    return setsockopt(fd, SOL_SOCKET, opt, (const char *)&ms, sizeof ms);
}

GM_API int64_t gm_net_set_timeouts(int64_t h, uint64_t recv_ns, uint64_t send_ns) {
    if (gm_set_timeout((SOCKET)h, SO_RCVTIMEO, recv_ns) != 0) return gm_wsa_err(WSAGetLastError());
    return gm_set_timeout((SOCKET)h, SO_SNDTIMEO, send_ns) != 0 ? gm_wsa_err(WSAGetLastError()) : 0;
}

GM_API int64_t gm_net_peer(int64_t h, void *p, size_t n) {
    struct sockaddr_storage a;
    int al = sizeof a;
    if (getpeername((SOCKET)h, (struct sockaddr *)&a, &al) != 0) return gm_wsa_err(WSAGetLastError());
    char text[INET6_ADDRSTRLEN + 16];
    int len;
    if (a.ss_family == AF_INET) {
        struct sockaddr_in *s = (struct sockaddr_in *)&a;
        char host[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &s->sin_addr, host, sizeof host);
        len = snprintf(text, sizeof text, "%s:%u", host, (unsigned)ntohs(s->sin_port));
    } else if (a.ss_family == AF_INET6) {
        struct sockaddr_in6 *s = (struct sockaddr_in6 *)&a;
        char host[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &s->sin6_addr, host, sizeof host);
        len = snprintf(text, sizeof text, "[%s]:%u", host, (unsigned)ntohs(s->sin6_port));
    } else {
        return gm_io_error(EINVAL);
    }
    if (len < 0 || (size_t)len > n) return gm_io_error(EINVAL);
    memcpy(p, text, (size_t)len);
    return len;
}
#endif

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

GM_API void *gm_heap_alloc(size_t size, size_t align) {
    if (align == 0) align = 1;
#ifdef __FILC__
    /* Under Fil-C a block is an allocation of its own, so the capability of the pointer
       covers exactly the block, and freeing it revokes every pointer into it. */
    /* Fil-C stops the program on a request larger than it can ever serve; no address
       space holds 2^47 bytes, so such a request fails like any other. */
    if ((uint64_t)size >= ((uint64_t)1 << 47)) return NULL;
    uint8_t *p = (uint8_t *)zgc_aligned_alloc(align, size);
    if (p == NULL) return NULL;
    uintptr_t a = (uintptr_t)p;
#else
    if (size > SIZE_MAX - align - sizeof(void *)) return NULL;
    uint8_t *raw = (uint8_t *)malloc(size + align + sizeof(void *));
    if (raw == NULL) return NULL;
    uintptr_t a = ((uintptr_t)raw + sizeof(void *) + (align - 1)) & ~(uintptr_t)(align - 1);
    uint8_t *p = raw + (a - (uintptr_t)raw);
    memcpy(p - sizeof(void *), &raw, sizeof raw);
#endif
    if (gm_debug != 0) {
        memset(p, 0xAA, size);
        gm_rt_acquire();
        gm_hb_record(a, size);
        gm_rt_release();
    }
    return p;
}

static void gm_heap_release(void *p) {
#ifdef __FILC__
    free(p);
#else
    uint8_t *raw;
    memcpy(&raw, (uint8_t *)p - sizeof(void *), sizeof raw);
    free(raw);
#endif
}

/* ---- allocation tags (Debug builds, §18.3) ---------------------------------------------- */

typedef struct { void *p, *owner, *vt; char *site; } gm_atag;
static gm_atag *gm_atags;      /* open addressing; p == NULL is empty, p == 1 a deleted entry */
static size_t gm_atag_cap, gm_atag_used;

static size_t gm_atag_slot(void *p) {
    uintptr_t h = (uintptr_t)p * (uintptr_t)0x9E3779B97F4A7C15ull;
    return (size_t)(h >> 7) & (gm_atag_cap - 1);
}

static char *gm_site_str(const uint8_t *file, size_t flen, uint32_t line, uint32_t col) {
    char *s = (char *)malloc(flen + 32);
    if (s == NULL) gm_trap("out of memory", NULL, 0, 0);
    memcpy(s, file, flen);
    snprintf(s + flen, 32, ":%u:%u", (unsigned)line, (unsigned)col);
    return s;
}

static void gm_atag_grow(void) {
    gm_atag *old = gm_atags;
    size_t old_cap = gm_atag_cap;
    gm_atag_cap = old_cap ? old_cap * 2 : 256;
    gm_atags = (gm_atag *)calloc(gm_atag_cap, sizeof(gm_atag));
    if (gm_atags == NULL) gm_trap("out of memory", NULL, 0, 0);
    gm_atag_used = 0;
    for (size_t i = 0; i < old_cap; i++) {
        if ((uintptr_t)old[i].p > 1) {
            size_t j = gm_atag_slot(old[i].p);
            while (gm_atags[j].p != NULL) j = (j + 1) & (gm_atag_cap - 1);
            gm_atags[j] = old[i];
            gm_atag_used++;
        }
    }
    free(old);
}

/* The slot holding p, or NULL. */
static gm_atag *gm_atag_find(void *p) {
    if (gm_atag_cap == 0) return NULL;
    for (size_t j = gm_atag_slot(p);; j = (j + 1) & (gm_atag_cap - 1)) {
        if (gm_atags[j].p == NULL) return NULL;
        if (gm_atags[j].p == p) return &gm_atags[j];
    }
}

GM_API void gm_alloc_tag(void *p, void *owner, void *vt, const uint8_t *file, size_t flen, uint32_t line, uint32_t col) {
    char *site = gm_site_str(file, flen, line, col);
    gm_rt_acquire();
    gm_atag *t = gm_atag_find(p);
    if (t == NULL) {
        if (2 * (gm_atag_used + 1) > gm_atag_cap) gm_atag_grow();
        size_t j = gm_atag_slot(p);
        while ((uintptr_t)gm_atags[j].p > 1) j = (j + 1) & (gm_atag_cap - 1);
        t = &gm_atags[j];
        if (t->p == NULL) gm_atag_used++;
    } else {
        free(t->site);
    }
    *t = (gm_atag){p, owner, vt, site};
    gm_rt_release();
}

GM_API void gm_alloc_check(void *p, void *owner, void *vt, const uint8_t *file, size_t flen, uint32_t line, uint32_t col,
                           const char *tfile, uint32_t tline, uint32_t tcol) {
    gm_rt_acquire();
    gm_atag *t = gm_atag_find(p);
    if (t == NULL) {
        gm_rt_release();
        return;
    }
    if (t->owner != owner || t->vt != vt) {
        char *at = gm_site_str(file, flen, line, col);
        char msg[1024];
        snprintf(msg, sizeof msg, "memory allocated at %s is freed at %s through a different allocator", t->site, at);
        gm_rt_release();
        gm_trap(msg, tfile, tline, tcol);
    }
    free(t->site);
    *t = (gm_atag){(void *)1, NULL, NULL, NULL};
    gm_rt_release();
}

GM_API void gm_heap_free(void *p, size_t size, size_t align, const char *file, uint32_t line, uint32_t col) {
    (void)align;
    if (gm_debug == 0) {
        if (p != NULL) gm_heap_release(p);
        return;
    }
    char msg[160];
    gm_rt_acquire();
    gm_hblock *b = gm_hb_find((uintptr_t)p);
    if (b != NULL && b->state == GM_HB_FREED) {
        snprintf(msg, sizeof msg, "double free of heap block 0x%llx", (unsigned long long)(uintptr_t)p);
        gm_trap(msg, file, line, col);
    }
    if (b != NULL && b->size != size) {
        snprintf(msg, sizeof msg, "invalid free of 0x%llx: the heap block there has %llu bytes, not %llu",
                 (unsigned long long)(uintptr_t)p, (unsigned long long)b->size, (unsigned long long)size);
        gm_trap(msg, file, line, col);
    }
    if (b == NULL) {
        snprintf(msg, sizeof msg, "invalid free of 0x%llx: not the start of a live heap block of %llu bytes",
                 (unsigned long long)(uintptr_t)p, (unsigned long long)size);
        gm_trap(msg, file, line, col);
    }
    b->state = GM_HB_FREED;
    gm_rt_release();
    memset(p, 0xDD, size);
    gm_heap_release(p);
}

#ifndef _WIN32
/* ---- threads ---------------------------------------------------------------------------- */

/* `detached` and `finished` are guarded by gm_rt_lock: whichever of the thread's end and
 * the call to detach it comes second frees the record. */
typedef struct { pthread_t t; void (*f)(void *); void *arg; int detached, finished; } gm_thread;

/* The stack size of the threads the runtime starts. */
#define GM_THREAD_STACK_SIZE ((size_t)8 << 20)

static void *gm_thread_main(void *p) {
    gm_thread_stack();
    gm_thread *t = (gm_thread *)p;
    t->f(t->arg);
    pthread_mutex_lock(&gm_rt_lock);
    if (t->detached) {
        pthread_mutex_unlock(&gm_rt_lock);
        free(t);
    } else {
        t->finished = 1;
        pthread_mutex_unlock(&gm_rt_lock);
    }
    return NULL;
}

/* The stack of the thread that runs the program's entry function: larger than a
 * typical process default, so deep recursion behaves alike on every host. */
#define GM_MAIN_STACK_SIZE ((size_t)64 << 20)

typedef struct { int (*f)(gm_node *); gm_node *cx; int status; } gm_main_call;

static void *gm_main_thread(void *p) {
    gm_thread_stack();
    gm_main_call *c = (gm_main_call *)p;
    c->status = c->f(c->cx);
    return NULL;
}

/* Runs entry `f` under context `cx` on a thread of its own and returns its status;
 * on the calling thread if no such thread can be started. */
GM_API int gm_run_main(int (*f)(gm_node *), gm_node *cx) {
    gm_main_call c = {f, cx, 0};
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, GM_MAIN_STACK_SIZE);
    pthread_t t;
    int r = pthread_create(&t, &attr, gm_main_thread, &c);
    pthread_attr_destroy(&attr);
    if (r != 0) return f(cx);
    pthread_join(t, NULL);
    return c.status;
}

/* A thread's handle is 1 + its index in this table, rather than its address: a pointer
 * turned into an integer and back loses its capability under Fil-C. A joined thread's
 * slot is reused. */
static gm_thread **gm_threads;
static size_t gm_nthreads;

GM_API int64_t gm_thread_spawn(void *f, void *arg) {
    gm_thread *t = (gm_thread *)malloc(sizeof *t);
    if (t == NULL) return gm_io_error(ENOMEM);
    t->f = (void (*)(void *))f;
    t->arg = arg;
    t->detached = t->finished = 0;
    gm_rt_acquire();
    size_t slot = 0;
    while (slot < gm_nthreads && gm_threads[slot] != NULL) slot++;
    if (slot == gm_nthreads) {
        gm_thread **tab = (gm_thread **)realloc(gm_threads, sizeof(gm_thread *) * (gm_nthreads * 2 + 4));
        if (tab == NULL) {
            gm_rt_release();
            free(t);
            return gm_io_error(ENOMEM);
        }
        for (size_t i = gm_nthreads; i < gm_nthreads * 2 + 4; i++) tab[i] = NULL;
        gm_threads = tab;
        gm_nthreads = gm_nthreads * 2 + 4;
    }
    gm_threads[slot] = t;
    gm_rt_release();
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, GM_THREAD_STACK_SIZE);
    int r = pthread_create(&t->t, &attr, gm_thread_main, t);
    pthread_attr_destroy(&attr);
    if (r != 0) {
        gm_rt_acquire();
        gm_threads[slot] = NULL;
        gm_rt_release();
        free(t);
        return gm_io_error(r);
    }
    return (int64_t)slot + 1;
}

GM_API int64_t gm_thread_join(int64_t h) {
    gm_rt_acquire();
    gm_thread *t = gm_threads[h - 1];
    gm_rt_release();
    int r = pthread_join(t->t, NULL);
    gm_rt_acquire();
    gm_threads[h - 1] = NULL;
    gm_rt_release();
    free(t);
    return r == 0 ? 0 : gm_io_error(r);
}

GM_API int64_t gm_thread_detach(int64_t h) {
    pthread_mutex_lock(&gm_rt_lock);
    gm_thread *t = gm_threads[h - 1];
    gm_threads[h - 1] = NULL;
    int done = t->finished;
    pthread_t tid = t->t;
    t->detached = 1;
    pthread_mutex_unlock(&gm_rt_lock);
    int r = pthread_detach(tid);
    if (done) free(t);
    return r == 0 ? 0 : gm_io_error(r);
}

GM_API void gm_thread_yield(void) { sched_yield(); }

#if defined(__linux__) && defined(SYS_futex) && !defined(__FILC__)
GM_API int64_t gm_futex_wait(uint32_t *addr, uint32_t expected, int64_t timeout_ns) {
    struct timespec ts, *tp = NULL;
    if (timeout_ns >= 0) {
        ts.tv_sec = (time_t)(timeout_ns / 1000000000);
        ts.tv_nsec = (long)(timeout_ns % 1000000000);
        tp = &ts;
    }
    /* FUTEX_WAIT_PRIVATE */
    long r = syscall(SYS_futex, addr, 128, expected, tp, NULL, 0);
    if (r != 0 && errno != EAGAIN && errno != ETIMEDOUT && errno != EINTR) return gm_io_error(errno);
    return 0;
}

GM_API int64_t gm_futex_wake(uint32_t *addr, uint32_t n) {
    /* FUTEX_WAKE_PRIVATE */
    syscall(SYS_futex, addr, 129, n > 0x7fffffffu ? 0x7fffffff : (int)n, NULL, NULL, 0);
    return 0;
}
#elif defined(__APPLE__)
extern int __ulock_wait(uint32_t op, void *addr, uint64_t value, uint32_t timeout_us);
extern int __ulock_wake(uint32_t op, void *addr, uint64_t wake_value);

GM_API int64_t gm_futex_wait(uint32_t *addr, uint32_t expected, int64_t timeout_ns) {
    uint64_t us = timeout_ns < 0 ? 0 : (uint64_t)timeout_ns / 1000 + 1;
    if (us > 0xffffffffu) us = 0xffffffffu;
    /* UL_COMPARE_AND_WAIT; a timeout of 0 means forever */
    __ulock_wait(1, addr, expected, (uint32_t)us);
    return 0;
}

GM_API int64_t gm_futex_wake(uint32_t *addr, uint32_t n) {
    /* UL_COMPARE_AND_WAIT | ULF_WAKE_ALL: one wake-up per call is not offered */
    (void)n;
    __ulock_wake(1 | 0x100, addr, 0);
    return 0;
}
#else
/* No futex: poll the word, sleeping a little between looks. */
GM_API int64_t gm_futex_wait(uint32_t *addr, uint32_t expected, int64_t timeout_ns) {
    int64_t waited = 0;
    while (__atomic_load_n(addr, __ATOMIC_ACQUIRE) == expected) {
        if (timeout_ns >= 0 && waited >= timeout_ns) break;
        struct timespec t = {0, 50000};
        nanosleep(&t, NULL);
        waited += 50000;
        if (waited >= 1000000) break; /* wake up spuriously now and then */
    }
    return 0;
}

GM_API int64_t gm_futex_wake(uint32_t *addr, uint32_t n) {
    (void)addr;
    (void)n;
    return 0;
}
#endif

GM_API uint32_t gm_cpu_count(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n < 1 ? 1 : (uint32_t)n;
}
#else
/* Windows: threads from _beginthreadex, the futex from WaitOnAddress. */

/* `detached` and `finished` are guarded by gm_rt_lock, as on other systems. */
typedef struct { HANDLE t; void (*f)(void *); void *arg; int detached, finished; } gm_thread;

/* The stack size of the threads the runtime starts (address space only: the pages are
 * committed as the stack grows). */
#define GM_THREAD_STACK_SIZE ((size_t)8 << 20)

static unsigned __stdcall gm_thread_main(void *p) {
    gm_thread_stack();
    gm_thread *t = (gm_thread *)p;
    t->f(t->arg);
    gm_rt_acquire();
    if (t->detached) {
        gm_rt_release();
        free(t);
    } else {
        t->finished = 1;
        gm_rt_release();
    }
    return 0;
}

/* The stack of the thread that runs the program's entry function: larger than a
 * typical process default, so deep recursion behaves alike on every host. */
#define GM_MAIN_STACK_SIZE ((size_t)64 << 20)

typedef struct { int (*f)(gm_node *); gm_node *cx; int status; } gm_main_call;

static unsigned __stdcall gm_main_thread(void *p) {
    gm_thread_stack();
    gm_main_call *c = (gm_main_call *)p;
    c->status = c->f(c->cx);
    return 0;
}

/* Runs entry `f` under context `cx` on a thread of its own and returns its status;
 * on the calling thread if no such thread can be started. */
GM_API int gm_run_main(int (*f)(gm_node *), gm_node *cx) {
    /* Standard streams carry bytes: no newline translation, no end-of-file character. */
    _setmode(0, _O_BINARY);
    _setmode(1, _O_BINARY);
    _setmode(2, _O_BINARY);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    DWORD mode;
    if (GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mode)) SetConsoleOutputCP(CP_UTF8);
    gm_main_call c = {f, cx, 0};
    uintptr_t h = _beginthreadex(NULL, (unsigned)GM_MAIN_STACK_SIZE, gm_main_thread, &c,
                                 STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (h == 0) return f(cx);
    WaitForSingleObject((HANDLE)h, INFINITE);
    CloseHandle((HANDLE)h);
    return c.status;
}

/* A thread's handle is 1 + its index in this table. A joined thread's slot is reused. */
static gm_thread **gm_threads;
static size_t gm_nthreads;

GM_API int64_t gm_thread_spawn(void *f, void *arg) {
    gm_thread *t = (gm_thread *)malloc(sizeof *t);
    if (t == NULL) return gm_io_error(ENOMEM);
    t->f = (void (*)(void *))f;
    t->arg = arg;
    t->detached = t->finished = 0;
    gm_rt_acquire();
    size_t slot = 0;
    while (slot < gm_nthreads && gm_threads[slot] != NULL) slot++;
    if (slot == gm_nthreads) {
        gm_thread **tab = (gm_thread **)realloc(gm_threads, sizeof(gm_thread *) * (gm_nthreads * 2 + 4));
        if (tab == NULL) {
            gm_rt_release();
            free(t);
            return gm_io_error(ENOMEM);
        }
        for (size_t i = gm_nthreads; i < gm_nthreads * 2 + 4; i++) tab[i] = NULL;
        gm_threads = tab;
        gm_nthreads = gm_nthreads * 2 + 4;
    }
    gm_threads[slot] = t;
    gm_rt_release();
    uintptr_t h = _beginthreadex(NULL, (unsigned)GM_THREAD_STACK_SIZE, gm_thread_main, t,
                                 STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (h == 0) {
        int e = errno;
        gm_rt_acquire();
        gm_threads[slot] = NULL;
        gm_rt_release();
        free(t);
        return gm_io_error(e);
    }
    t->t = (HANDLE)h;
    return (int64_t)slot + 1;
}

GM_API int64_t gm_thread_join(int64_t h) {
    gm_rt_acquire();
    gm_thread *t = gm_threads[h - 1];
    gm_rt_release();
    WaitForSingleObject(t->t, INFINITE);
    CloseHandle(t->t);
    gm_rt_acquire();
    gm_threads[h - 1] = NULL;
    gm_rt_release();
    free(t);
    return 0;
}

GM_API int64_t gm_thread_detach(int64_t h) {
    gm_rt_acquire();
    gm_thread *t = gm_threads[h - 1];
    gm_threads[h - 1] = NULL;
    int done = t->finished;
    t->detached = 1;
    HANDLE th = t->t;
    gm_rt_release();
    /* Closing the handle does not stop the thread; it only gives up the right to join it. */
    CloseHandle(th);
    if (done) free(t);
    return 0;
}

GM_API void gm_thread_yield(void) { SwitchToThread(); }

GM_API int64_t gm_futex_wait(uint32_t *addr, uint32_t expected, int64_t timeout_ns) {
    DWORD ms = INFINITE;
    if (timeout_ns >= 0) {
        int64_t m = timeout_ns / 1000000 + (timeout_ns % 1000000 != 0);
        ms = m >= (int64_t)INFINITE ? INFINITE - 1 : (DWORD)m;
    }
    WaitOnAddress((volatile VOID *)addr, &expected, sizeof expected, ms);
    return 0;
}

GM_API int64_t gm_futex_wake(uint32_t *addr, uint32_t n) {
    if (n == 1) WakeByAddressSingle((PVOID)addr);
    else if (n > 64) WakeByAddressAll((PVOID)addr);
    else for (uint32_t i = 0; i < n; i++) WakeByAddressSingle((PVOID)addr);
    return 0;
}

GM_API uint32_t gm_cpu_count(void) {
    DWORD n = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    return n < 1 ? 1 : (uint32_t)n;
}
#endif

GM_API void gm_sys_write(int32_t fd, const void *p, size_t n) {
    if (fd == 2) gm_write_err((const uint8_t *)p, n);
    else gm_write_out((const uint8_t *)p, n);
}

/* ---- handler contexts and non-local exits ---------------------------------------------- */

GM_API gm_node *gm_find(gm_node *cx, uint32_t eff) {
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

GM_API bool gm_errdefer_runs(gm_frame *fr) {
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
 * gm_ctx_init(top, len, fiber) prepares that context on the `len` bytes of stack that end
 * at `top`.
 */

static void gm_fiber_entry(gm_fiber *fb);

#if defined(__FILC__)
/* Fil-C compiles no assembly that switches stacks; it provides ucontext instead, and
 * runs each context on a stack it allocates itself (the stack given to makecontext only
 * tells its size). A saved context is then a gm_filc_ctx: a ucontext and the fiber it
 * starts. Fil-C saves only into the context that is running (or a new one), so each
 * execution keeps one context object, `gm_filc_running` while it runs. */
#include <ucontext.h>
typedef struct { ucontext_t uc; gm_fiber *fb; } gm_filc_ctx;
static _Thread_local gm_fiber *gm_filc_starting;
/* The context running now; NULL on the thread's own stack before its first switch. */
static _Thread_local gm_filc_ctx *gm_filc_running;

static void gm_filc_entry(void) { gm_fiber_entry(gm_filc_starting); }

static void *gm_rt_switch(void **save, void *to, void *arg) {
    gm_filc_ctx *me = gm_filc_running;
    if (me == NULL) {
        me = (gm_filc_ctx *)calloc(1, sizeof *me);
        if (me == NULL) gm_trap("out of memory switching fibers", NULL, 0, 0);
    }
    *save = me;
    gm_filc_ctx *t = (gm_filc_ctx *)to;
    gm_filc_starting = t->fb;
    gm_filc_running = t;
    swapcontext(&me->uc, &t->uc);
    gm_filc_running = me;
    return arg;
}

static void *gm_ctx_init(uintptr_t top, size_t len, gm_fiber *fb) {
    (void)top;
    gm_filc_ctx *c = (gm_filc_ctx *)calloc(1, sizeof *c);
    if (c == NULL || getcontext(&c->uc) != 0) gm_trap("out of memory creating a fiber", NULL, 0, 0);
    c->fb = fb;
    c->uc.uc_stack.ss_sp = c;
    c->uc.uc_stack.ss_size = len;
    c->uc.uc_link = NULL;
    makecontext(&c->uc, gm_filc_entry, 0);
    return c;
}
#elif defined(__x86_64__) && defined(_WIN32)
/* The Win64 convention: arguments in rcx, rdx, r8; callee-saved are rbx, rbp, rdi, rsi,
 * r12-r15 and xmm6-xmm15. A context also holds the stack bounds in the thread information
 * block (StackBase, StackLimit, DeallocationStack at gs:0x08, 0x10, 0x1478), which the
 * system consults when it dispatches an exception: they describe the running stack. The
 * saved context is 192 bytes (xmm6-15 at 0, the three bounds at 160) below the eight
 * saved registers and the return address. */
extern void *gm_rt_switch(void **save, void *to, void *arg) __asm__(GM_SYM("gm_rt_switch"));
extern void gm_rt_fiber_tramp(void) __asm__(GM_SYM("gm_rt_fiber_tramp"));
__asm__(
    ".text\n"
    ".p2align 4\n"
    ".globl " GM_SYM("gm_rt_switch") "\n"
    GM_SYM("gm_rt_switch") ":\n"
    "    pushq %rbp\n"
    "    pushq %rbx\n"
    "    pushq %rdi\n"
    "    pushq %rsi\n"
    "    pushq %r12\n"
    "    pushq %r13\n"
    "    pushq %r14\n"
    "    pushq %r15\n"
    "    subq $192, %rsp\n"
    "    movups %xmm6, 0(%rsp)\n"
    "    movups %xmm7, 16(%rsp)\n"
    "    movups %xmm8, 32(%rsp)\n"
    "    movups %xmm9, 48(%rsp)\n"
    "    movups %xmm10, 64(%rsp)\n"
    "    movups %xmm11, 80(%rsp)\n"
    "    movups %xmm12, 96(%rsp)\n"
    "    movups %xmm13, 112(%rsp)\n"
    "    movups %xmm14, 128(%rsp)\n"
    "    movups %xmm15, 144(%rsp)\n"
    "    movq %gs:0x08, %r10\n"
    "    movq %r10, 160(%rsp)\n"
    "    movq %gs:0x10, %r10\n"
    "    movq %r10, 168(%rsp)\n"
    "    movq %gs:0x1478, %r10\n"
    "    movq %r10, 176(%rsp)\n"
    "    movq %rsp, (%rcx)\n"
    "    movq %rdx, %rsp\n"
    "    movq 160(%rsp), %r10\n"
    "    movq %r10, %gs:0x08\n"
    "    movq 168(%rsp), %r10\n"
    "    movq %r10, %gs:0x10\n"
    "    movq 176(%rsp), %r10\n"
    "    movq %r10, %gs:0x1478\n"
    "    movups 0(%rsp), %xmm6\n"
    "    movups 16(%rsp), %xmm7\n"
    "    movups 32(%rsp), %xmm8\n"
    "    movups 48(%rsp), %xmm9\n"
    "    movups 64(%rsp), %xmm10\n"
    "    movups 80(%rsp), %xmm11\n"
    "    movups 96(%rsp), %xmm12\n"
    "    movups 112(%rsp), %xmm13\n"
    "    movups 128(%rsp), %xmm14\n"
    "    movups 144(%rsp), %xmm15\n"
    "    addq $192, %rsp\n"
    "    popq %r15\n"
    "    popq %r14\n"
    "    popq %r13\n"
    "    popq %r12\n"
    "    popq %rsi\n"
    "    popq %rdi\n"
    "    popq %rbx\n"
    "    popq %rbp\n"
    "    movq %r8, %rax\n"
    "    ret\n"
    ".p2align 4\n"
    ".globl " GM_SYM("gm_rt_fiber_tramp") "\n"
    GM_SYM("gm_rt_fiber_tramp") ":\n"
    "    movq %r12, %rcx\n"
    "    subq $32, %rsp\n"
    "    callq *%r13\n"
    "    ud2\n");

/* The initial context of a fiber whose stack ends at `top` (16-byte aligned) and spans
 * `len` bytes. After the switch "returns" into the trampoline, rsp is 16 bytes below top. */
static void *gm_ctx_init(uintptr_t top, size_t len, gm_fiber *fb) {
    void **sp = (void **)(top - 280);
    memset(sp, 0, 264);
    sp[20] = (void *)top;                 /* StackBase */
    sp[21] = (void *)(top - len);         /* StackLimit */
    sp[22] = (void *)(top - len);         /* DeallocationStack */
    sp[24 + 2] = (void *)gm_fiber_entry;  /* r13 */
    sp[24 + 3] = fb;                      /* r12 */
    sp[24 + 8] = (void *)gm_rt_fiber_tramp;
    return sp;
}
#elif defined(__x86_64__)
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
static void *gm_ctx_init(uintptr_t top, size_t len, gm_fiber *fb) {
    (void)len;
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

static void *gm_ctx_init(uintptr_t top, size_t len, gm_fiber *fb) {
    (void)len;
    void **sp = (void **)(top - 160);
    memset(sp, 0, 160);
    sp[0] = fb;                         /* x19 */
    sp[1] = (void *)gm_fiber_entry;     /* x20 */
    sp[11] = (void *)gm_rt_fiber_tramp; /* x30 */
    return sp;
}
#elif defined(__s390x__)
extern void *gm_rt_switch(void **save, void *to, void *arg) __asm__(GM_SYM("gm_rt_switch"));
extern void gm_rt_fiber_tramp(void) __asm__(GM_SYM("gm_rt_fiber_tramp"));
/* The saved context is 144 bytes: r6-r14 at 0, f8-f15 at 72. */
__asm__(
    ".text\n"
    ".align 8\n"
    ".globl " GM_SYM("gm_rt_switch") "\n"
    GM_SYM("gm_rt_switch") ":\n"
    "    aghi %r15, -144\n"
    "    stmg %r6, %r14, 0(%r15)\n"
    "    std %f8, 72(%r15)\n"
    "    std %f9, 80(%r15)\n"
    "    std %f10, 88(%r15)\n"
    "    std %f11, 96(%r15)\n"
    "    std %f12, 104(%r15)\n"
    "    std %f13, 112(%r15)\n"
    "    std %f14, 120(%r15)\n"
    "    std %f15, 128(%r15)\n"
    "    stg %r15, 0(%r2)\n"
    "    lgr %r15, %r3\n"
    "    lmg %r6, %r14, 0(%r15)\n"
    "    ld %f8, 72(%r15)\n"
    "    ld %f9, 80(%r15)\n"
    "    ld %f10, 88(%r15)\n"
    "    ld %f11, 96(%r15)\n"
    "    ld %f12, 104(%r15)\n"
    "    ld %f13, 112(%r15)\n"
    "    ld %f14, 120(%r15)\n"
    "    ld %f15, 128(%r15)\n"
    "    aghi %r15, 144\n"
    "    lgr %r2, %r4\n"
    "    br %r14\n"
    ".align 8\n"
    ".globl " GM_SYM("gm_rt_fiber_tramp") "\n"
    GM_SYM("gm_rt_fiber_tramp") ":\n"
    "    lgr %r2, %r6\n"
    "    basr %r14, %r7\n"
    "    .short 0\n");

/* The initial context of a fiber whose stack ends at `top`: the 144 saved bytes, and above
 * them the 160-byte register save area the entry function's frame sits on. */
static void *gm_ctx_init(uintptr_t top, size_t len, gm_fiber *fb) {
    (void)len;
    void **sp = (void **)(top - 144 - 160);
    memset(sp, 0, 144);
    sp[0] = fb;                         /* r6 */
    sp[1] = (void *)gm_fiber_entry;     /* r7 */
    sp[8] = (void *)gm_rt_fiber_tramp;  /* r14 */
    return sp;
}
#else
static void *gm_rt_switch(void **save, void *to, void *arg) {
    (void)save; (void)to; (void)arg;
    gm_trap("fibers are not supported on this architecture", NULL, 0, 0);
}
static void *gm_ctx_init(uintptr_t top, size_t len, gm_fiber *fb) {
    (void)top; (void)len; (void)fb;
    return NULL;
}
#endif

/* ---- fibers ---------------------------------------------------------------------------- */

static _Thread_local gm_fiber *gm_fiber_pool;

GM_API _Noreturn void gm_stack_overflow(void) {
    gm_overflowing = 1;
    const char *msg = gm_cur_fiber ? "stack overflow on a fiber stack" : "stack overflow";
    /* In Debug builds, the call that overflowed is the last one the caller recorded. */
    gm_bt *b = gm_bt_top;
    if (b != NULL && b->file != NULL) gm_trap(msg, b->file, b->line, b->col);
    gm_trap(msg, NULL, 0, 0);
}

/* The space kept below a thread's limit for reporting the overflow. */
#define GM_THREAD_STACK_RESERVE ((size_t)256 << 10)

#if defined(_WIN32) && !defined(GM_NO_FRAME_PROBES)
/* A fault just below a thread's stack limit, an access violation with the stack pointer
 * below the running stack's limit, or the system's own stack overflow exception is the
 * trap this is for; any other exception gets the default action. */
static LONG CALLBACK gm_veh(PEXCEPTION_POINTERS ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_STACK_OVERFLOW) gm_stack_overflow();
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
        uintptr_t a = (uintptr_t)ep->ExceptionRecord->ExceptionInformation[1];
        uintptr_t limit = gm_thread_stack_limit;
        uintptr_t sp = (uintptr_t)ep->ContextRecord->Rsp;
        if (limit != 0 && a < limit && a + GM_THREAD_STACK_RESERVE + ((uintptr_t)1 << 20) >= limit)
            gm_stack_overflow();
        if (gm_stack_limit != 0 && sp < gm_stack_limit) gm_stack_overflow();
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static INIT_ONCE gm_veh_once = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK gm_veh_install(PINIT_ONCE o, PVOID p, PVOID *c) {
    (void)o; (void)p; (void)c;
    AddVectoredExceptionHandler(1, gm_veh);
    return TRUE;
}

/* Keeps room on the calling thread's stack for gm_veh to report an overflow, and installs
 * the handler once per process. */
static void gm_overflow_handler(void) {
    ULONG guarantee = 64 << 10;
    SetThreadStackGuarantee(&guarantee);
    InitOnceExecuteOnce(&gm_veh_once, gm_veh_install, NULL, NULL);
}
#elif !defined(GM_NO_FRAME_PROBES)
#if defined(__APPLE__)
#include <sys/ucontext.h>
#else
#include <ucontext.h>
#endif
/* The stack pointer of the interrupted code, or 0 where it is not known. */
static uintptr_t gm_fault_sp(void *uc) {
    ucontext_t *u = uc;
#if defined(__linux__) && defined(__x86_64__)
    return (uintptr_t)u->uc_mcontext.gregs[REG_RSP];
#elif defined(__linux__) && defined(__aarch64__)
    return (uintptr_t)u->uc_mcontext.sp;
#elif defined(__linux__) && defined(__s390x__)
    return (uintptr_t)u->uc_mcontext.gregs[15];
#elif defined(__APPLE__) && defined(__x86_64__)
    return (uintptr_t)u->uc_mcontext->__ss.__rsp;
#elif defined(__APPLE__) && defined(__aarch64__)
    return (uintptr_t)u->uc_mcontext->__ss.__sp;
#else
    (void)u;
    return 0;
#endif
}

/* A fault just below a thread's stack limit is an overflow that a function's prologue
 * hit before its stack check ran (compilers probe each page of a large frame), and so is
 * any fault while the stack pointer is below the limit (a frame so large that it skipped
 * the guard pages, with the check inlined after the frame was allocated): report it as
 * the trap it is. Any other fault gets the default action. */
static void gm_segv(int sig, siginfo_t *si, void *uc) {
    uintptr_t a = (uintptr_t)si->si_addr;
    uintptr_t limit = gm_thread_stack_limit;
    uintptr_t sp = gm_fault_sp(uc);
    if (limit != 0 && a < limit && a + GM_THREAD_STACK_RESERVE + ((uintptr_t)1 << 20) >= limit)
        gm_stack_overflow();
    /* The running stack may be a fiber's, whose limit is its own. */
    if (gm_stack_limit != 0 && sp != 0 && sp < gm_stack_limit) gm_stack_overflow();
    signal(sig, SIG_DFL);
}

static void gm_overflow_install(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = gm_segv;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
}

/* Gives the calling thread a stack for gm_segv, and installs it once per process. */
static void gm_overflow_handler(void) {
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    size_t size = (size_t)64 << 10;
    stack_t ss;
    ss.ss_sp = malloc(size);
    ss.ss_size = size;
    ss.ss_flags = 0;
    if (ss.ss_sp == NULL || sigaltstack(&ss, NULL) != 0) return;
    pthread_once(&once, gm_overflow_install);
}
#endif

GM_API void gm_thread_stack(void) {
    uintptr_t low = 0;
    size_t size = 0;
#if defined(__APPLE__)
    pthread_t me = pthread_self();
    size = pthread_get_stacksize_np(me);
    low = (uintptr_t)pthread_get_stackaddr_np(me) - size;
#elif defined(_WIN32)
    ULONG_PTR lo, hi;
    GetCurrentThreadStackLimits(&lo, &hi);
    low = (uintptr_t)lo;
    size = (size_t)(hi - lo);
#elif defined(__linux__)
    pthread_attr_t a;
    void *addr;
    if (pthread_getattr_np(pthread_self(), &a) == 0) {
        if (pthread_attr_getstack(&a, &addr, &size) == 0) low = (uintptr_t)addr;
        pthread_attr_destroy(&a);
    }
#endif
    /* Without a known stack (other systems), nothing is checked. */
    gm_thread_stack_limit = low != 0 && size > 2 * GM_THREAD_STACK_RESERVE ? low + GM_THREAD_STACK_RESERVE : 0;
    gm_stack_limit = gm_thread_stack_limit;
#ifndef GM_NO_FRAME_PROBES
    if (gm_thread_stack_limit != 0) gm_overflow_handler();
#endif
}

/* Makes `fb` (NULL: the thread's own stack) the running stack. */
static void gm_set_cur_fiber(gm_fiber *fb) {
    gm_cur_fiber = fb;
    gm_stack_limit = fb ? fb->limit : gm_thread_stack_limit;
}

static void gm_fiber_entry(gm_fiber *fb) {
    fb->start(fb);
    gm_fiber_finish(fb);
}

GM_API gm_fiber *gm_fiber_new(gm_with *w, uint8_t *stack, size_t len, void (*start)(gm_fiber *), size_t arg_size,
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
        fb->arg = stack + (top - (uintptr_t)stack);
    }
    fb->limit = (uintptr_t)stack + GM_STACK_RESERVE;
    fb->sp = gm_ctx_init(top, top - (uintptr_t)stack, fb);
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

GM_API gm_cont gm_fiber_cont(gm_with *w, gm_fiber *fb) {
    fb->epoch++;
    gm_susp_link(w, fb);
    return (gm_cont){fb, fb->epoch};
}

GM_API void gm_with_go(gm_with *w, gm_fiber *fb) {
    gm_fiber *me = gm_cur_fiber;
    gm_bt *bt = gm_bt_top;
    gm_set_cur_fiber(fb);
    if (!fb->started) fb->bt = bt;
    gm_bt_top = fb->bt;
    fb->state = GM_FB_RUNNING;
    fb->started = 1;
    w->event = 0;
    gm_rt_switch(&w->driver_sp, fb->sp, NULL);
    fb->bt = gm_bt_top;
    gm_bt_top = bt;
    gm_set_cur_fiber(me);
}

GM_API void gm_with_clause(gm_with *w) {
    gm_fiber *fb = w->event_fiber;
    fb->thunk(fb->req, (gm_cont){fb, fb->epoch});
}

GM_API void gm_ctl_perform(gm_with *w, gm_clause_thunk thunk, void *req, void *res) {
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

GM_API _Noreturn void gm_fiber_finish(gm_fiber *fb) {
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

GM_API void *gm_resume(gm_with *w, const void *k, const char *file, uint32_t line, uint32_t col) {
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

GM_API void gm_cancel_cont(gm_with *w, const void *k, const char *file, uint32_t line, uint32_t col) {
    gm_fiber *fb = gm_cont_take(w, k, file, line, col);
    gm_cancel(w, fb, file, line, col);
}

GM_API void gm_with_end(gm_with *w, bool crossing, const char *file, uint32_t line, uint32_t col) {
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

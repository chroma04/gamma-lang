/* The runtime of programs compiled by the Gamma C back end: implementations of the
 * declarations in gm_rt.h. The compiler puts gm_rt.h before this file (and the generated
 * code after it, unless GM_RT_SEPARATE is defined), so this file includes nothing itself.
 *
 * Observable behavior (trap output, value display, heap diagnostics) mirrors the reference
 * interpreter byte for byte.
 */

/* Definitions of the variables declared in gm_rt.h (gm_checks is the generated code's). */
GM_API _Thread_local gm_bt *gm_bt_top;
GM_API pthread_mutex_t gm_rt_lock = PTHREAD_MUTEX_INITIALIZER;
GM_API int gm_debug = 1;
GM_API void *(*volatile gm_poison)(void *, int, size_t) = memset;
GM_API _Thread_local gm_unwind gm_unw;
GM_API _Thread_local gm_fiber *gm_cur_fiber;
GM_API _Thread_local uintptr_t gm_stack_limit;
GM_API _Thread_local uintptr_t gm_thread_stack_limit;

/* ---- traps ---------------------------------------------------------------- */

/* Prints the callers in the backtrace chain; past GM_BT_MAX of them, only the first and
 * last GM_BT_MAX / 2. */
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
            fprintf(stderr, "  ... %llu more calls\n", (unsigned long long)(n - GM_BT_MAX));
        i++;
    }
}

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
    exit(134);
}

GM_API _Noreturn void gm_trap(const char *msg, const char *file, uint32_t line, uint32_t col) {
    gm_trap_bytes((const uint8_t *)msg, strlen(msg), file, line, col);
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

static bool gm_wbit(const uint8_t *a, uint32_t i) { return ((a[i / 8] >> (i % 8)) & 1) != 0; }

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
    res[0] = 1;
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

GM_API uint64_t gm_wshamt(const uint8_t *amt, uint32_t abits) {
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
    gm_wfmt(&m, amt, abits, false);
    gm_buf_push(&m, " too large for ", 15);
    gm_buf_push(&m, ty, strlen(ty));
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
    if (sgn) r[(bits - 1) / 8] |= (uint8_t)(1u << ((bits - 1) % 8));
    gm_wfix(r, bits, sgn);
}

GM_API void gm_wmax(uint8_t *r, uint32_t bits, bool sgn) {
    memset(r, 0xff, GM_WBYTES(bits));
    if (sgn) r[(bits - 1) / 8] &= (uint8_t)~(1u << ((bits - 1) % 8));
    gm_wfix(r, bits, sgn);
}

/* Byte i of the infinite sign or zero extension of the `bits`-bit integer a. */
static uint8_t gm_wbyte(const uint8_t *a, uint32_t bits, bool neg, size_t i) {
    uint8_t fill = neg ? 0xff : 0;
    size_t lo = i * 8;
    if (lo >= bits) return fill;
    uint8_t v = a[i];
    if (lo + 8 > bits) {
        unsigned k = (unsigned)(bits - lo);
        uint8_t mask = (uint8_t)((1u << k) - 1);
        v = (uint8_t)((v & mask) | (fill & (uint8_t)~mask));
    }
    return v;
}

GM_API int gm_wconv(uint8_t *r, size_t rsize, uint32_t rbits, bool rsgn, const uint8_t *a, size_t asize,
                    uint32_t abits, bool asgn) {
    bool neg = asgn && gm_wbit(a, abits - 1);
    for (size_t i = 0; i < rsize; i++) r[i] = gm_wbyte(a, abits, neg, i);
    /* Wrap: the bits at and above rbits repeat the sign (or are zero). */
    bool rneg = rsgn && gm_wbit(r, rbits - 1);
    for (size_t i = rbits / 8; i < rsize; i++) {
        size_t lo = i * 8;
        if (lo >= rbits) {
            r[i] = rneg ? 0xff : 0;
        } else if (lo + 8 > rbits) {
            uint8_t mask = (uint8_t)((1u << (rbits - lo)) - 1);
            r[i] = (uint8_t)((r[i] & mask) | (rneg ? (uint8_t)~mask : 0));
        }
    }
    /* Fits: every bit of the value from rbits (unsigned) or rbits - 1 (signed) up is the sign. */
    bool fits = !(neg && !rsgn);
    size_t lo = rbits - (rsgn ? 1 : 0);
    size_t total = (rsize > asize ? rsize : asize) + 1;
    uint8_t want = rsgn && neg ? 0xff : 0;
    for (size_t i = lo / 8; i < total && fits; i++) {
        uint8_t mask = i == lo / 8 ? (uint8_t)(0xffu << (lo % 8)) : 0xff;
        if ((gm_wbyte(a, abits, neg, i) & mask) != (want & mask)) fits = false;
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
        if (pos >= 0 && (size_t)pos < sz * 8) r[pos / 8] |= (uint8_t)(1u << (pos % 8));
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

GM_API void gm_wbswap(uint8_t *r, const uint8_t *a, uint32_t bits) {
    size_t sz = GM_WBYTES(bits);
    for (size_t i = 0; i < sz / 2; i++) {
        uint8_t x = a[i], y = a[sz - 1 - i];
        r[i] = y;
        r[sz - 1 - i] = x;
    }
}

GM_API void gm_wbitreverse(uint8_t *r, const uint8_t *a, uint32_t bits, bool sgn) {
    size_t sz = GM_WBYTES(bits);
    uint64_t stack[GM_WSTACK];
    uint64_t *t = gm_wscratch(stack, GM_WSTACK, sz / 8);
    uint8_t *tb = (uint8_t *)t;
    memset(tb, 0, sz);
    for (uint32_t i = 0; i < bits; i++) {
        if (gm_wbit(a, i)) tb[(bits - 1 - i) / 8] |= (uint8_t)(1u << ((bits - 1 - i) % 8));
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
        if (gm_wbit(a, i)) tb[j / 8] |= (uint8_t)(1u << (j % 8));
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

/* The value of a `bits`-bit integer stored in `size` little-endian bytes at p. */
static void gm_fmt_int(gm_buf *out, const gm_type *t, const uint8_t *p) {
    if (t->bits > 128) {
        gm_wfmt(out, p, t->bits, t->is_signed);
        return;
    }
    unsigned __int128 raw = 0;
    uint32_t size = t->size;
    for (uint32_t i = size; i > 0; i--) raw = (raw << 8) | p[i - 1];
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
    memcpy(m, p, n);
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
static gm_rt_str *gm_rt_args;
static size_t gm_rt_argc;

GM_API void gm_set_args(int argc, char **argv) {
    if (gm_rt_args != NULL || argc <= 0) return;
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
    pthread_mutex_lock(&gm_rt_lock);
    if (fd < 0) fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    pthread_mutex_unlock(&gm_rt_lock);
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
    pthread_mutex_lock(&gm_rt_lock);
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
    pthread_mutex_unlock(&gm_rt_lock);
    return (int64_t)i + 1;
}

static DIR *gm_dir(int64_t d) {
    pthread_mutex_lock(&gm_rt_lock);
    DIR *dir = d >= 1 && (size_t)d <= gm_ndirs ? gm_dirs[d - 1] : NULL;
    pthread_mutex_unlock(&gm_rt_lock);
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
    pthread_mutex_lock(&gm_rt_lock);
    gm_dirs[d - 1] = NULL;
    pthread_mutex_unlock(&gm_rt_lock);
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
        pthread_mutex_lock(&gm_rt_lock);
        gm_hb_record(a, size);
        pthread_mutex_unlock(&gm_rt_lock);
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
    pthread_mutex_lock(&gm_rt_lock);
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
    pthread_mutex_unlock(&gm_rt_lock);
}

GM_API void gm_alloc_check(void *p, void *owner, void *vt, const uint8_t *file, size_t flen, uint32_t line, uint32_t col,
                           const char *tfile, uint32_t tline, uint32_t tcol) {
    pthread_mutex_lock(&gm_rt_lock);
    gm_atag *t = gm_atag_find(p);
    if (t == NULL) {
        pthread_mutex_unlock(&gm_rt_lock);
        return;
    }
    if (t->owner != owner || t->vt != vt) {
        char *at = gm_site_str(file, flen, line, col);
        char msg[1024];
        snprintf(msg, sizeof msg, "memory allocated at %s is freed at %s through a different allocator", t->site, at);
        pthread_mutex_unlock(&gm_rt_lock);
        gm_trap(msg, tfile, tline, tcol);
    }
    free(t->site);
    *t = (gm_atag){(void *)1, NULL, NULL, NULL};
    pthread_mutex_unlock(&gm_rt_lock);
}

GM_API void gm_heap_free(void *p, size_t size, size_t align, const char *file, uint32_t line, uint32_t col) {
    (void)align;
    if (gm_debug == 0) {
        if (p != NULL) gm_heap_release(p);
        return;
    }
    char msg[160];
    pthread_mutex_lock(&gm_rt_lock);
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
    pthread_mutex_unlock(&gm_rt_lock);
    memset(p, 0xDD, size);
    gm_heap_release(p);
}

/* ---- threads ---------------------------------------------------------------------------- */

typedef struct { pthread_t t; void (*f)(void *); void *arg; } gm_thread;

/* The stack size of the threads the runtime starts. */
#define GM_THREAD_STACK_SIZE ((size_t)8 << 20)

static void *gm_thread_main(void *p) {
    gm_thread_stack();
    gm_thread *t = (gm_thread *)p;
    t->f(t->arg);
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
    pthread_mutex_lock(&gm_rt_lock);
    size_t slot = 0;
    while (slot < gm_nthreads && gm_threads[slot] != NULL) slot++;
    if (slot == gm_nthreads) {
        gm_thread **tab = (gm_thread **)realloc(gm_threads, sizeof(gm_thread *) * (gm_nthreads * 2 + 4));
        if (tab == NULL) {
            pthread_mutex_unlock(&gm_rt_lock);
            free(t);
            return gm_io_error(ENOMEM);
        }
        for (size_t i = gm_nthreads; i < gm_nthreads * 2 + 4; i++) tab[i] = NULL;
        gm_threads = tab;
        gm_nthreads = gm_nthreads * 2 + 4;
    }
    gm_threads[slot] = t;
    pthread_mutex_unlock(&gm_rt_lock);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, GM_THREAD_STACK_SIZE);
    int r = pthread_create(&t->t, &attr, gm_thread_main, t);
    pthread_attr_destroy(&attr);
    if (r != 0) {
        pthread_mutex_lock(&gm_rt_lock);
        gm_threads[slot] = NULL;
        pthread_mutex_unlock(&gm_rt_lock);
        free(t);
        return gm_io_error(r);
    }
    return (int64_t)slot + 1;
}

GM_API int64_t gm_thread_join(int64_t h) {
    pthread_mutex_lock(&gm_rt_lock);
    gm_thread *t = gm_threads[h - 1];
    pthread_mutex_unlock(&gm_rt_lock);
    int r = pthread_join(t->t, NULL);
    pthread_mutex_lock(&gm_rt_lock);
    gm_threads[h - 1] = NULL;
    pthread_mutex_unlock(&gm_rt_lock);
    free(t);
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
    const char *msg = gm_cur_fiber ? "stack overflow on a fiber stack" : "stack overflow";
    /* In Debug builds, the call that overflowed is the last one the caller recorded. */
    gm_bt *b = gm_bt_top;
    if (b != NULL && b->file != NULL) gm_trap(msg, b->file, b->line, b->col);
    gm_trap(msg, NULL, 0, 0);
}

/* The space kept below a thread's limit for reporting the overflow. */
#define GM_THREAD_STACK_RESERVE ((size_t)256 << 10)

#ifndef GM_NO_FRAME_PROBES
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

GM_API void gm_discard(gm_with *w, const void *k, const char *file, uint32_t line, uint32_t col) {
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

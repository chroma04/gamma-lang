#define GM_RT_SEPARATE 1
#define GM_RT_PROGRAM 1
/* The runtime of programs compiled by the Gamma C back end.
 *
 * The runtime is linked into a program in one of two ways. Normally the compiler
 * emits one C translation unit: this header, gm_rt.c, then the generated code, and
 * everything here is `static` so the unit is self-contained (and the optimizer sees
 * the whole program). With GM_RT_SEPARATE defined, this header and gm_rt.c are
 * compiled on their own into an object that is reused across programs, and the
 * generated code includes only this header: the declarations marked GM_API and
 * GM_API_VAR then have external linkage. The generated code also defines GM_RT_PROGRAM,
 * which leaves out the system headers that only gm_rt.c needs.
 * GNU C11 is required (statement expressions, zero-size arrays, __int128,
 * __builtin_*_overflow).
 */
#ifndef GM_RT_H
#define GM_RT_H

#ifdef GM_FREESTANDING
/* A freestanding target (spec 3.7.1): no operating system and no C library beyond the
 * functions every freestanding toolchain provides (memcpy, memmove, memset, memcmp). The
 * parts of the runtime that need an operating system are left out, and there is one thread
 * of execution. */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
void *memcpy(void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
void *memset(void *, int, size_t);
int memcmp(const void *, const void *, size_t);
#define strlen gm_strlen
static inline size_t gm_strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}
#define snprintf gm_snprintf
static int gm_snprintf(char *dst, size_t cap, const char *fmt, ...);
#define GM_TLS
/* The context switch is private to the object: only the program's exports are symbols. */
#define GM_ASM_GLOBL ".local "
#else
#define GM_ASM_GLOBL ".globl "
#if defined(__wasi__)
/* WASI (wasm32-wasi): a hosted target with files, clocks, arguments and the environment,
 * but no threads, child processes, sockets or signals; the runtime's functions for those
 * report IoError.Unsupported. There is one thread of execution, and fibers switch stacks
 * through Binaryen's Asyncify transformation, which the compiler applies to the module
 * (see "context switching" in gm_rt.c). */
#define GM_WASI 1
#define GM_TLS
#else
#define GM_TLS _Thread_local
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* pthread_getattr_np */
#endif

#if defined(_WIN32)
/* The Windows API level of the runtime (WaitOnAddress, GetCurrentThreadStackLimits), set
 * before any system header fixes a lower one. */
#if defined(_WIN32_WINNT) && _WIN32_WINNT < 0x0A00
#undef _WIN32_WINNT
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#endif

#if defined(GM_RT_PROGRAM) && defined(GM_RT_SEPARATE)
/* The generated code of a program that is linked with the separately compiled runtime
 * needs only the declarations below, which use nothing from the operating system's headers;
 * parsing those would take the C compiler about as long as a small program (on Windows,
 * where every unit of a split build would parse <windows.h> and Winsock, much longer). */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#else
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#ifndef GM_WASI
#include <signal.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#if defined(_WIN32)
/* Windows (MinGW-w64): the Win32 API and Winsock 2 stand in for the POSIX headers. */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <io.h>
#include <direct.h>
#include <process.h>
#include <bcrypt.h>
#elif defined(GM_WASI)
#include <unistd.h>
#include <dirent.h>
#else
#include <unistd.h>
#include <dirent.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sched.h>
#include <poll.h>
#include <sys/wait.h>
#if defined(__linux__) && !defined(__FILC__)
#include <sys/syscall.h>
#endif
/* glibc's posix_spawn starts a child without copying the parent's address space (clone with
 * CLONE_VM | CLONE_VFORK), reports a failed exec, and can change the child's directory
 * (2.29): Proc.spawn uses it instead of fork, which costs time in proportion to the
 * parent's memory. */
#if defined(__GLIBC__) && !defined(__FILC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 29))
#define GM_POSIX_SPAWN 1
#include <spawn.h>
#endif
#endif
#endif /* GM_RT_PROGRAM */
#endif /* GM_FREESTANDING */

/* GM_API: the linkage of the runtime's functions (and of its variables' definitions);
 * GM_API_VAR: of the declarations of its variables here. */
#ifdef GM_RT_SEPARATE
#define GM_API
#define GM_API_VAR extern
#else
#define GM_API static
#define GM_API_VAR static
#endif

/* GM_HUGE_FN: marks a generated function with an enormous body (a very long chain of
 * operations). GCC's points-to analysis and jump threading take time that grows much
 * faster than such a function, so they are switched off for it. */
#if defined(__GNUC__) && !defined(__clang__)
#define GM_HUGE_FN __attribute__((optimize("no-tree-pta", "no-thread-jumps")))
#else
#define GM_HUGE_FN
#endif

/* GM_LONG_FN: marks a generated function with a very long body (thousands of operations,
 * such as a long chain). From LLVM 19 on, Clang's early common-subexpression pass asks for
 * the known bits of values through the selects and checks of such a body at a cost that
 * grows with its length, so the whole function takes time that grows about with the cube
 * of its length (minutes for ten thousand links, hours for fifty thousand). Such a function
 * is compiled without optimization there. Apple numbers its Clang differently: Apple clang
 * 17 is the first based on LLVM 19. */
#if defined(__clang__) && ((defined(__apple_build_version__) && __clang_major__ >= 17) || \
                           (!defined(__apple_build_version__) && __clang_major__ >= 19))
#define GM_LONG_FN __attribute__((optnone, noinline))
#else
#define GM_LONG_FN
#endif

#define GM_XSTR(x) GM_STR(x)
#define GM_STR(x) #x
/* The assembler name of C symbol `s` (macOS prefixes an underscore). */
#define GM_SYM(s) GM_XSTR(__USER_LABEL_PREFIX__) s

/* ---- byte order ------------------------------------------------------------
 *
 * Integers (of up to 128 bits) are stored as one number in the target's byte order. Code
 * that addresses the bytes of an integer goes through gm_ibp, which names the i-th least
 * significant byte, so it does not depend on the order. */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define GM_BIG_ENDIAN 1
#else
#define GM_BIG_ENDIAN 0
#endif

/* The i-th least significant byte of the integer of `size` bytes at p. */
static inline uint8_t *gm_ibp(const void *p, size_t size, size_t i) {
    uint8_t *b = (uint8_t *)p;
    if (!GM_BIG_ENDIAN) return b + i;
    return b + (size - 1 - i);
}

/* The unsigned integer of `size` (at most 16) bytes at p. */
static inline unsigned __int128 gm_load_u128(const void *p, size_t size) {
    unsigned __int128 v = 0;
    for (size_t i = size; i > 0; i--) v = (v << 8) | *gm_ibp(p, size, i - 1);
    return v;
}

/* Stores the low `size` (at most 16) bytes of v at p. */
static inline void gm_store_u128(void *p, size_t size, unsigned __int128 v) {
    for (size_t i = 0; i < size; i++) *gm_ibp(p, size, i) = (uint8_t)(v >> (8 * i));
}

/* a * b on __int128, wrapping, into *r; returns whether the product overflowed. Clang
 * turns __builtin_mul_overflow on signed __int128 into a call of compiler-rt's __muloti4,
 * which libgcc (the library Linux links) lacks: this uses 64 x 64-bit products only. */
static inline bool gm_mul_ovf_i128(__int128 a, __int128 b, __int128 *r) {
    unsigned __int128 ua = a < 0 ? -(unsigned __int128)a : (unsigned __int128)a;
    unsigned __int128 ub = b < 0 ? -(unsigned __int128)b : (unsigned __int128)b;
    uint64_t ah = (uint64_t)(ua >> 64), al = (uint64_t)ua, bh = (uint64_t)(ub >> 64), bl = (uint64_t)ub;
    bool o = ah != 0 && bh != 0;
    unsigned __int128 c1 = (unsigned __int128)ah * bl, c2 = (unsigned __int128)al * bh;
    unsigned __int128 cross = c1 + c2;
    if ((c1 >> 64) || (c2 >> 64) || (cross >> 64)) o = true;
    unsigned __int128 lo = (unsigned __int128)al * bl;
    unsigned __int128 m = lo + (cross << 64);
    if (m < lo) o = true;
    bool neg = (a < 0) != (b < 0);
    unsigned __int128 lim = (unsigned __int128)1 << 127;
    if (neg ? m > lim : m >= lim) o = true;
    *r = (__int128)(neg ? -m : m);
    return o;
}

/* ---- memory helpers ------------------------------------------------------ */

/* GM_LD loads a value of C type T from address p (any alignment); GM_ST stores the value
 * v of C type T at address p. Under Fil-C a memcpy is a call into its runtime, and one
 * on a local makes the local escape to the heap, so they are typed accesses through a
 * packed struct (alignment 1, any type may alias) instead. */
#ifdef __FILC__
#define GM_UNALIGNED(T, p) (((struct __attribute__((packed, may_alias)) { T v; } *)(p))->v)
#define GM_LD(T, p) ({ T gm_ld_v_ = GM_UNALIGNED(T, p); gm_ld_v_; })
#define GM_ST(T, p, v) do { T gm_st_v_ = (v); GM_UNALIGNED(T, p) = gm_st_v_; } while (0)
#else
#define GM_LD(T, p) ({ T gm_ld_v_; memcpy(&gm_ld_v_, (const void *)(p), sizeof(T)); gm_ld_v_; })
#define GM_ST(T, p, v) do { T gm_st_v_ = (v); memcpy((void *)(p), &gm_st_v_, sizeof(T)); } while (0)
#endif
/* The value v as C type T of the same size: a blob as its mirror or back. */
#define GM_CONV(T, v) ({ union { __typeof__(v) s; T d; } gm_cv_u_; gm_cv_u_.s = (v); gm_cv_u_.d; })
/* Round half to even. The MinGW C library lacks roundeven; rint does it in the default
 * rounding mode, which programs never change. */
#ifdef _WIN32
#define GM_ROUNDEVEN(x) __builtin_rint(x)
#else
#define GM_ROUNDEVEN(x) __builtin_roundeven(x)
#endif
/* `#fast_math` functions: float contraction and reassociation are permitted (§5.3). */
#if defined(__clang__)
#define GM_FAST_MATH_ATTR
#define GM_FAST_MATH_BODY _Pragma("clang fp contract(fast) reassociate(on)")
#else
#define GM_FAST_MATH_ATTR __attribute__((optimize("associative-math", "no-signed-zeros", "no-trapping-math", "fp-contract=fast")))
#define GM_FAST_MATH_BODY
#endif
/* Pointer arithmetic and tagging. Under Fil-C a pointer keeps its capability only while
 * it is computed as a pointer (an integer turned back into a pointer has none), so the
 * address is changed with zmkptr, which keeps the capability; elsewhere through
 * uintptr_t, whose arithmetic wraps. GM_PTR_ADD adds a byte offset (any value: a
 * negative offset wraps around). GM_PTR_TAG and GM_PTR_UNTAG set and clear a mark on a
 * function's address, and GM_PTR_TAGGED tells whether it is set: bit 0, as the generated
 * functions are aligned; on WebAssembly, where a function's "address" is its index in the
 * function table and alignment means nothing, the top bit, as no table is that large. */
#ifdef __FILC__
#include <stdfil.h>
#define GM_PTR_ADD(p, n) zmkptr((void *)(p), (unsigned long)(p) + (unsigned long)(n))
#define GM_PTR_TAG(p) zorptr((void *)(p), 1)
#define GM_PTR_UNTAG(p) zandptr((void *)(p), ~1ul)
/* An address as a pointer (`to_ptr`): it grants no access. The empty asm hides where
 * the address came from: the optimizer could fold a plain cast back into the pointer it
 * was taken from, which keeps its capability. (zmkptr(NULL, n) is no better: filcc 0.686
 * at -O1 and above drops an access through it instead of trapping.) */
#define GM_INT_TO_PTR(n) ({ uintptr_t gm_n_ = (n); __asm__("" : "+r"(gm_n_)); (void *)gm_n_; })
#else
#define GM_INT_TO_PTR(n) ((void *)(uintptr_t)(n))
#define GM_PTR_ADD(p, n) ((void *)((uintptr_t)(p) + (uintptr_t)(n)))
#ifdef __wasm__
#define GM_PTR_TAG_BIT ((uintptr_t)1 << (sizeof(uintptr_t) * 8 - 1))
#else
#define GM_PTR_TAG_BIT ((uintptr_t)1)
#endif
#define GM_PTR_TAG(p) ((void *)((uintptr_t)(p) | GM_PTR_TAG_BIT))
#define GM_PTR_UNTAG(p) ((void *)((uintptr_t)(p) & ~GM_PTR_TAG_BIT))
#endif
#ifdef __FILC__
#define GM_PTR_TAGGED(p) (((uintptr_t)(p) & 1) != 0)
#else
#define GM_PTR_TAGGED(p) (((uintptr_t)(p) & GM_PTR_TAG_BIT) != 0)
#endif
/* Rounds a double to the nearest binary16 (ties to even); returns its bits. */
static inline uint16_t gm_f64_to_f16(double x) {
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

/* The number x converted to f16. Fil-C (as of 0.686) miscompiles the compiler's own
 * routines for converting to _Float16 (__truncdfhf2 and the like), so Fil-C builds round
 * with gm_f64_to_f16 instead (every source value converts to double exactly or, if an
 * integer, beyond f16's range either way); on x86-64 they also have the F16C
 * instructions (-mf16c) do float <-> f16 for arithmetic. */
#ifdef __FILC__
static inline _Float16 gm_f16_from(double d) {
    uint16_t h = gm_f64_to_f16(d);
    _Float16 f;
    memcpy(&f, &h, sizeof f);
    return f;
}
#define GM_F16(x) gm_f16_from((double)(x))
#else
#define GM_F16(x) ((_Float16)(x))
#endif
/* A zero value of C type T (scalar or blob). */
#define GM_ZERO(T) ((T){})

typedef struct { uint8_t b[0]; } gm_unit;

/* ---- traps ---------------------------------------------------------------- */

/* Debug builds keep a chain of the running Gamma functions: each pushes a gm_bt on entry,
 * pops it on exit, and records in it the location of each call it makes. Functions of the
 * core library have no name; backtraces leave them out. Each thread and each fiber has a
 * chain of its own; a fiber's continues into the chain of the `with` that runs it. */
typedef struct gm_bt {
    struct gm_bt *prev;
    const char *fn;
    const char *file;
    uint32_t line, col;
} gm_bt;
/* Records in the gm_bt `b` the location of the call about to be made. */
#define GM_BT_AT(b, f, l, c) ((b).file = (f), (b).line = (l), (b).col = (c))
GM_API_VAR GM_TLS gm_bt *gm_bt_top;

/* Stops the program: flushes stdout, writes "trap: <msg>\n  at <file>:<line>:<col>\n" to
 * stderr (the second line only if file is not NULL), followed in Debug builds by the calls
 * leading there, and exits with status 134. */
GM_API _Noreturn void gm_trap(const char *msg, const char *file, uint32_t line, uint32_t col);
/* As gm_trap, with a message given as bytes (not NUL-terminated). */
GM_API _Noreturn void gm_trap_bytes(const uint8_t *msg, size_t len, const char *file, uint32_t line, uint32_t col);
/* The trap of a pointer cast to a type of alignment `align` that `addr` lacks. */
GM_API _Noreturn void gm_trap_misaligned(uintptr_t addr, unsigned align, const char *file, uint32_t line, uint32_t col);
/* The traps of failed checks, which format their messages here rather than in the
 * generated code: index `i` out of bounds for length `len`; slice bounds lo..hi invalid for
 * length `len`; a shift amount `n` too large for the integer type named `ty`; a vector load
 * or store (`what`) of `lanes` lanes with a slice of `len` elements. */
GM_API _Noreturn void gm_trap_index(uint64_t i, uint64_t len, const char *file, uint32_t line, uint32_t col);
GM_API _Noreturn void gm_trap_slice(uint64_t lo, uint64_t hi, uint64_t len, const char *file, uint32_t line, uint32_t col);
GM_API _Noreturn void gm_trap_shift(uint64_t n, const char *ty, const char *file, uint32_t line, uint32_t col);
GM_API _Noreturn void gm_trap_lanes(const char *what, uint32_t lanes, uint64_t len, const char *file, uint32_t line,
                                    uint32_t col);

/* ---- growable byte buffer --------------------------------------------------- */

typedef struct { uint8_t *data; size_t len, cap; } gm_buf;
GM_API void gm_buf_push(gm_buf *b, const void *data, size_t len);
GM_API void gm_buf_free(gm_buf *b);

/* Traps: "integer overflow in `pow` (<base>^<n>)", for an integer `base` of `bits` bits
 * stored in the size of its type. */
GM_API _Noreturn void gm_trap_pow(const uint8_t *base, uint32_t bits, bool sgn, uint32_t n, const char *file,
                                  uint32_t line, uint32_t col);

/* ---- type descriptors (for formatting, spec §21.6.1) -------------------------- */

typedef enum {
    GM_K_INT,      /* bits, is_signed; storage `size` bytes, target byte order */
    GM_K_FLOAT,    /* bits: 16, 32 or 64 */
    GM_K_BOOL,
    GM_K_PTR,      /* any pointer or function pointer: printed as 0x<hex> */
    GM_K_SLICE,    /* elem; {ptr at 0, len (uintptr_t) at sizeof(void*)} */
    GM_K_ARRAY,    /* elem, len */
    GM_K_TUPLE,    /* fields (named == true: print "name: "); 1 unnamed elem prints "(x,)" */
    GM_K_OPTIONAL, /* elem = payload at offset 0; if flag_offset == GM_NICHE the payload is a
                      pointer and null is address 0, else present iff byte at flag_offset != 0 */
    GM_K_RESULT,   /* elem = ok, err; is_err iff byte at tag_offset != 0; payload at payload_offset */
    GM_K_STRUCT,   /* name, fields; fields named "_" are skipped */
    GM_K_UNION,    /* name, variants; tag (tag_size bytes, tag_signed) at tag_offset;
                      payload at payload_offset */
    GM_K_OPAQUE,   /* printed as `name` verbatim (e.g. "<handler>") */
    GM_K_BITS      /* a #bits struct: name, fields with *bit* offsets, packed into an unsigned
                      integer of `size` bytes. A field's width is 1 for GM_K_BOOL and `bits` for
                      GM_K_INT, GM_K_BITS and GM_K_UNION (the tag's width); fields named "_" are
                      skipped like a struct's */
} gm_kind;

#define GM_NICHE 0xffffffffu

typedef struct gm_type gm_type;

typedef struct {
    const char *name;     /* field / element name, or NULL for unnamed tuple elements */
    uint32_t offset;
    const gm_type *type;
} gm_field;

typedef struct {
    const char *name;
    int64_t tag;          /* the tag value (as a signed 64-bit number) */
    const gm_type *payload; /* NULL if the variant has no payload */
} gm_variant;

struct gm_type {
    gm_kind kind;
    uint32_t size, align;
    const char *name;
    uint16_t bits;
    bool is_signed;
    const gm_type *elem;
    const gm_type *err;
    uint64_t len;
    uint32_t nfields;
    const gm_field *fields;
    bool named;
    uint32_t flag_offset;
    uint32_t tag_offset, payload_offset;
    uint8_t tag_size;
    bool tag_signed;
    uint32_t nvariants;
    const gm_variant *variants;
};

/* Appends the display form of the value of type t at address v (§21.6.1). `top` is true for
 * the argument itself: a []u8 / []const u8 slice is then written raw, otherwise quoted. */
GM_API void gm_format(gm_buf *out, const gm_type *t, const void *v, bool top);
/* The display form of the value of type t at address v, as `gm_format` with `top` gives
 * it, where it already is or can cheaply be put: a string's own bytes, the digits of an
 * integer in `scratch` (GM_VIEW_SCRATCH bytes); any other value is formatted into `b`
 * (empty on entry, to be freed if it has data). Sets *data and returns the length. */
#define GM_VIEW_SCRATCH 48
GM_API size_t gm_format_view(gm_buf *b, uint8_t *scratch, const gm_type *t, const void *v, const void **data);
/* Traps: "unwrap of error value <v>", the error `v` of type `t`. */
GM_API _Noreturn void gm_trap_unwrap(const gm_type *t, const void *v, const char *file, uint32_t line, uint32_t col);

/* A placeholder's format spec (§21.6.1). align: 0 auto, 1 left, 2 right, 3 center; kind:
 * 0 display, 1 x, 2 X, 3 o, 4 b, 5 e; precision -1 when absent. */
typedef struct {
    uint8_t fill, align, plus, alt, zero, kind;
    uint32_t width;
    int32_t precision;
} gm_spec;

/* Appends the value of type t at address v as a placeholder with the spec s. */
GM_API void gm_format_spec(gm_buf *out, const gm_type *t, const void *v, const gm_spec *s);

/* A template known when the write runs (the one body that the instances of a generic
 * function share whatever their templates, §21.6.1): its n placeholders, the n + 1 pieces
 * of text around them, and for each placeholder the argument it writes and its spec (fmt,
 * NULL for the default). The tables are static data of the generated code. The field names
 * are words the runtime uses already: an exported Gamma function may not take a name that
 * the runtime uses (§19.4). */
typedef struct {
    const uint8_t *ptr;
    size_t len;
} gm_tmpl_piece;
typedef struct {
    uint32_t n;
    const gm_tmpl_piece *text;
    const uint32_t *arg;
    const gm_spec *const *fmt;
} gm_tmpl;

/* ---- ambient effects ---------------------------------------------------------- */

GM_API void gm_write_out(const uint8_t *p, size_t len);  /* buffered stdout */
GM_API void gm_write_err(const uint8_t *p, size_t len);  /* stderr, flushed */
GM_API void gm_flush(void);                              /* flushes stdout */

/* Statement coverage: `n` counters, written out at exit to the file `path` (if not NULL). */
GM_API void gm_cover_init(uint32_t n, const char *path);
GM_API_VAR uint32_t *gm_cover_counts;
#define GM_COVER(i) (gm_cover_counts != NULL ? (void)__atomic_fetch_add(&gm_cover_counts[i], 1, __ATOMIC_RELAXED) : (void)0)

/* Process arguments as a Gamma `[]const string`: returns a pointer to `argc` slices
 * ({ptr, len} pairs, each 2 * sizeof(void*) bytes). Set by gm_set_args at startup. */
GM_API void gm_set_args(int argc, char **argv);
/* The index of the test that a test binary is to run: its first argument. Without one,
 * prints its usage and returns -1. */
GM_API long gm_test_index(int argc, char **argv);
GM_API const void *gm_args(size_t *count);
/* Looks up an environment variable; returns false if unset. */
GM_API bool gm_env_var(const uint8_t *name, size_t len, const uint8_t **value, size_t *value_len);

/* ---- system services (behind the core library's default handlers, §12.9) --------- */

/* Operations that can fail return a count, or an error code: -1 - i for the i-th
 * variant of IoError other than Other, and -1000 - errno for Other(errno). */
GM_API int64_t gm_io_error(int err);
GM_API int64_t gm_sys_read(void *p, size_t n);      /* standard input */
GM_API uint64_t gm_clock_now(void);                 /* monotonic nanoseconds */
GM_API int64_t gm_clock_wall(void);                 /* Unix time in nanoseconds */
GM_API void gm_clock_sleep(uint64_t ns);
GM_API void gm_rand_fill(void *p, size_t n);
/* Files and directories: the flags of gm_fs_open, and handles that are file descriptors
 * (files; C runtime descriptors on Windows) or indexes into a table of open directories. */
enum { GM_FS_READ = 1, GM_FS_WRITE = 2, GM_FS_APPEND = 4, GM_FS_CREATE = 8, GM_FS_TRUNCATE = 16, GM_FS_EXCLUSIVE = 32 };
GM_API int64_t gm_fs_open(const void *path, size_t len, uint32_t flags);
GM_API int64_t gm_fs_close(int64_t f);
GM_API int64_t gm_fs_read(int64_t f, void *p, size_t n);
GM_API int64_t gm_fs_write(int64_t f, const void *p, size_t n);
GM_API int64_t gm_fs_seek(int64_t f, int64_t offset, uint32_t whence);
GM_API int64_t gm_fs_stat(const void *path, size_t len, void *out, size_t n);   /* size, kind (0 file, 1 directory, 2 symlink, 3 other), mtime, permission bits */
GM_API int64_t gm_fs_lstat(const void *path, size_t len, void *out, size_t n);   /* as stat; no final symlink followed */
GM_API int64_t gm_fs_remove(const void *path, size_t len);
GM_API int64_t gm_fs_rename(const void *from, size_t from_len, const void *to, size_t to_len);
GM_API int64_t gm_fs_make_dir(const void *path, size_t len);
GM_API int64_t gm_fs_remove_dir(const void *path, size_t len);
GM_API int64_t gm_fs_open_dir(const void *path, size_t len);
GM_API int64_t gm_fs_next_entry(int64_t d, void *p, size_t n);   /* length + kind * 2^32; 0 after the last */
GM_API int64_t gm_fs_close_dir(int64_t d);
GM_API int64_t gm_fs_truncate(int64_t f, uint64_t size);
GM_API int64_t gm_fs_sync(int64_t f);
GM_API int64_t gm_fs_cwd(void *p, size_t n);          /* length; EINVAL if it does not fit */
GM_API int64_t gm_fs_canonical(const void *path, size_t len, void *p, size_t n);   /* length; EINVAL if it does not fit */
GM_API int64_t gm_fs_read_link(const void *path, size_t len, void *p, size_t n);   /* length; EINVAL if it does not fit */
GM_API int64_t gm_fs_symlink(const void *target, size_t tlen, const void *path, size_t len);
GM_API int64_t gm_fs_set_mode(const void *path, size_t len, uint32_t mode);
GM_API int64_t gm_fs_set_modified(const void *path, size_t len, int64_t ns);   /* Unix nanoseconds */
GM_API void gm_env_exit(uint8_t code);                /* ends the process after flushing output */
/* TCP sockets; handles are file descriptors (SOCKET values on Windows). Host names are resolved with getaddrinfo;
 * a failed lookup is Other(0), as in the interpreter. */
GM_API int64_t gm_net_connect(const void *host, size_t len, uint16_t port);
GM_API int64_t gm_net_listen(const void *host, size_t len, uint16_t port);
GM_API int64_t gm_net_local_port(int64_t h);
GM_API int64_t gm_net_accept(int64_t h);
GM_API int64_t gm_net_send(int64_t h, const void *p, size_t n);
GM_API int64_t gm_net_recv(int64_t h, void *p, size_t n);
GM_API int64_t gm_net_close(int64_t h);
GM_API int64_t gm_net_shutdown(int64_t h, uint32_t how);   /* 0 read, 1 write, 2 both */
GM_API int64_t gm_net_set_timeouts(int64_t h, uint64_t recv_ns, uint64_t send_ns);   /* 0: none */
GM_API int64_t gm_net_peer(int64_t h, void *p, size_t n);   /* "host:port"; EINVAL if it does not fit */

/* Child processes. gm_proc_launch takes the argument vector, changes to the environment
 * ("NAME=value" sets a variable, "NAME" removes it; applied to ours, or to an empty environment
 * when flags has GM_PROC_CLEAR_ENV) and the working directory (empty: ours); the flags hold two
 * bits per standard stream (stdin from bit 0, stdout from bit 2, stderr from bit 4: 0 inherit,
 * 1 the null device, 2 a pipe), and GM_PROC_GROUP to start the child in a process group of its
 * own (POSIX; a job object on Windows), which gm_proc_kill then ends as a whole. It returns a
 * handle. gm_proc_read_out waits until a piped output
 * stream has data and reads from it: the length plus 2^32 for stderr, or 0 once both streams
 * have ended. gm_proc_wait returns the exit code, or the signal number plus 2^32. With
 * GM_PROC_PROGRAM the file to run is `program` (searched for in PATH if it has no separator)
 * instead of argv[0], which is then only the name the child sees. gm_proc_try_wait is
 * gm_proc_wait that does not block: 2^33 while the child runs; once it returns a status the
 * child is released as by gm_proc_wait. */
enum { GM_PROC_CLEAR_ENV = 64, GM_PROC_GROUP = 128, GM_PROC_PROGRAM = 256 };
GM_API int64_t gm_proc_launch(const void *argv, size_t argc, const void *env, size_t envc, const void *dir,
                              size_t dir_len, const void *program, size_t program_len, uint32_t flags);
GM_API int64_t gm_proc_write_in(int64_t h, const void *p, size_t n);
GM_API int64_t gm_proc_close_in(int64_t h);
GM_API int64_t gm_proc_read_out(int64_t h, void *p, size_t n);
GM_API int64_t gm_proc_wait(int64_t h);
GM_API int64_t gm_proc_try_wait(int64_t h);
GM_API int64_t gm_proc_kill(int64_t h);
/* gm_proc_fork starts a copy of this process (Linux with glibc; elsewhere IoError.Unsupported) whose
 * stdin is the null device and whose output streams are pipes read with gm_proc_read_out:
 * it returns 0 in the copy, which ends with gm_proc_fork_exit(status), and a handle here. */
GM_API int64_t gm_proc_fork(void);
GM_API void gm_proc_fork_exit(int32_t status);

/* ---- threads (§19.6) ----------------------------------------------------------------
 *
 * A thread runs a plain function `void f(void *arg)`. Fibers, handler contexts and the exit in
 * progress are per thread; the runtime's own shared tables are guarded by gm_rt_lock. */
/* The lock guarding the runtime's shared tables. */
GM_API void gm_rt_acquire(void);
GM_API void gm_rt_release(void);
GM_API int64_t gm_thread_spawn(void *f, void *arg);   /* a handle, or an error code */
GM_API int64_t gm_thread_join(int64_t h);
GM_API int64_t gm_thread_detach(int64_t h);
GM_API void gm_thread_yield(void);
/* Blocks while *addr == expected, until woken or the timeout (ns; negative: none) passes;
 * 0 or an error code. May return early. */
GM_API int64_t gm_futex_wait(uint32_t *addr, uint32_t expected, int64_t timeout_ns);
GM_API int64_t gm_futex_wake(uint32_t *addr, uint32_t n);   /* UINT32_MAX: all */
GM_API uint32_t gm_cpu_count(void);
/* Writes to standard output (fd 1, buffered like Out) or standard error (fd 2). */
GM_API void gm_sys_write(int32_t fd, const void *p, size_t n);

/* ---- heap (behind the core library's @heap_alloc / @heap_free) ------------------ */

/* Returns NULL when out of memory. In debug builds (gm_debug != 0) the block is filled with
 * 0xAA, and gm_heap_free traps on double and invalid frees (it keeps track of live blocks). */
GM_API void *gm_heap_alloc(size_t size, size_t align);
GM_API void gm_heap_free(void *p, size_t size, size_t align, const char *file, uint32_t line, uint32_t col);
/* Debug builds: records the allocator (owner, vt) and site of the block at p (§18.3). */
GM_API void gm_alloc_tag(void *p, void *owner, void *vt, const uint8_t *file, size_t flen, uint32_t line, uint32_t col);
/* Debug builds: traps if the block at p was allocated by another allocator, and forgets it. */
GM_API void gm_alloc_check(void *p, void *owner, void *vt, const uint8_t *file, size_t flen, uint32_t line, uint32_t col,
                           const char *tfile, uint32_t tline, uint32_t tcol);
/* Nonzero in Debug builds (initially 1; set by the generated code). */
GM_API_VAR int gm_debug;
/* memset, called through a volatile pointer so that filling a frame that is about to die
 * (Debug builds) is not removed as a dead store. */
GM_API_VAR void *(*volatile gm_poison)(void *, int, size_t);
/* Nonzero in Debug, Checked and Safe builds; zero in Fast ones (set by the generated code). */
GM_API_VAR int gm_checks;

/* ---- frames, handler contexts and non-local exits ----------------------------------
 *
 * The calling convention of generated code is described in docs/architecture.md. Each
 * function's locals live in a frame struct starting with a gm_frame; handler contexts are
 * linked lists of gm_node; `break`, `continue`, `return`, `Env.exit`, `resume` and fiber
 * cancellation travel as the exit in progress, gm_unw.
 */

typedef struct gm_frame { struct gm_frame *up; uint8_t clause_lit; } gm_frame;
typedef struct gm_with gm_with;
typedef struct gm_node {
    uint32_t eff;
    uint8_t kind;               /* GM_LIT, GM_DECL or GM_NATIVE */
    void *const *clauses;       /* clause functions, by operation */
    void *env;                  /* literal: its frame; declaration: the handler value */
    struct gm_node *hctx;       /* the context clauses run in */
    gm_frame *with_frame;       /* the installing `with`, for clause exits */
    uint32_t with_label;
    void *result;               /* where a `break` from a clause stores the `with`'s value */
    gm_with *fw;                /* the installing `with … on`, or NULL (§14) */
    struct gm_node *next;
} gm_node;
enum { GM_LIT, GM_DECL, GM_NATIVE };
typedef struct { void *code; void *env; gm_node *cx; } gm_closure;
typedef struct { void *env; gm_node *cx; } gm_hval;
/* Exit kinds. GM_RESUME ends a `ctl` clause, after which its driver switches to the fiber
 * named by `resume`; GM_CANCEL unwinds a discarded fiber to its base. */
enum { GM_NONE, GM_BREAK, GM_CONTINUE, GM_RETURN, GM_EXIT, GM_RESUME, GM_CANCEL };
/* C11 memory orders, indexed by the tag of `MemoryOrder` (§19.6). */
static const int gm_ord[] = {__ATOMIC_RELAXED, __ATOMIC_ACQUIRE, __ATOMIC_RELEASE, __ATOMIC_ACQ_REL, __ATOMIC_SEQ_CST};
typedef struct { int kind; gm_frame *frame; uint32_t label; uint8_t is_err; uint8_t code; } gm_unwind;
/* Each thread has its own exit in progress (§19.6). */
GM_API_VAR GM_TLS gm_unwind gm_unw;

/* Runs entry `f` of the program under context `cx` on a thread with a large stack and
 * returns its exit status. */
GM_API int gm_run_main(int (*f)(gm_node *), gm_node *cx);
/* The same for a program that switches fibers: on WASI, that takes the fiber driver at the
 * bottom of the stack (which brings in the code of the switch, see gm_rt.c). */
GM_API int gm_run_main_fibers(int (*f)(gm_node *), gm_node *cx);
/* On WASI, run an exported function (called by `f`, which ignores its argument): the
 * _fibers variant under the fiber driver, unless one is already running below. */
GM_API void gm_run_export(int (*f)(gm_node *));
GM_API void gm_run_export_fibers(int (*f)(gm_node *));

/* The innermost handler of effect `eff` in context `cx`, or NULL. The innermost node, most
 * often the one, is looked at inline: a call is dear, under Fil-C especially. */
GM_API gm_node *gm_find_outer(gm_node *cx, uint32_t eff);
static inline gm_node *gm_find(gm_node *cx, uint32_t eff) {
    return cx == NULL || cx->eff == eff ? cx : gm_find_outer(cx->next, eff);
}
/* Whether errdefers run as the current exit leaves a block of frame `fr` (§8.8). */
GM_API bool gm_errdefer_runs(gm_frame *fr);

/* ---- fibers (§14) ------------------------------------------------------------------------
 *
 * A `with … on` runs its body on a fiber: a gm_fiber with its own stack. The code that runs
 * the `with` is the driver; it loops: switch to a fiber (gm_with_go), and when control comes
 * back, look at why (w->event). A fiber that performs a `ctl` operation suspends in
 * gm_ctl_perform; the driver then calls the clause (gm_with_clause) on its own stack. The
 * clause ends with `resume`, which sets w->next and exits with GM_RESUME, or with another
 * exit, which completes the `with`. A finished fiber switches to its driver for good
 * (gm_fiber_finish).
 *
 * Fiber structs are never freed: a pool reuses them, and each suspension increments the
 * fiber's epoch, so a continuation (fiber, epoch) that was already consumed is detected
 * even after the fiber is reused.
 */

#define GM_MIN_STACK 16384
/* Bytes at the bottom of a fiber stack kept for reporting an overflow. */
#define GM_STACK_RESERVE 8192

typedef struct gm_fiber gm_fiber;
/* A continuation: the suspended fiber and its epoch. The layout of `cont(R)`. */
typedef struct { gm_fiber *fiber; uintptr_t epoch; } gm_cont;
/* Calls a `ctl` clause for a pending perform: `req` holds the node and the arguments. */
typedef void (*gm_clause_thunk)(void *req, gm_cont k);

enum { GM_EV_PERFORM = 1, GM_EV_DONE };

struct gm_with {
    void *driver_sp;            /* the driver's saved context while a fiber runs */
    gm_fiber *suspended;        /* fibers suspended on this `with`'s operations, newest first */
    uint32_t nsuspended;
    gm_fiber *owned;            /* fibers created for this `with` */
    gm_node *body_cx;           /* the body's handler context (for @fiber) */
    uint8_t event;              /* why the driver runs again: GM_EV_PERFORM or GM_EV_DONE */
    gm_fiber *event_fiber;
    gm_fiber *next;             /* the fiber chosen by `resume` */
};

struct gm_fiber {
    void *sp;                   /* saved context while not running */
    uintptr_t limit;            /* the lowest address its frames may use (checked builds) */
    gm_with *owner;             /* the `with` it was created for */
    gm_with *susp_with;         /* while suspended: the `with` whose operation it performed */
    uintptr_t epoch;
    uint8_t state;              /* GM_FB_NEW, GM_FB_RUNNING, GM_FB_SUSPENDED or GM_FB_DONE */
    uint8_t cancel;             /* set by `cancel` before switching to it */
    uint8_t started;
    gm_fiber *susp_next, *susp_prev;
    gm_fiber *own_next;
    gm_fiber *pool_next;
    void (*start)(gm_fiber *);  /* the code it starts with */
    /* the pending perform */
    gm_clause_thunk thunk;
    gm_bt *bt;                  /* its backtrace chain while it is not running */
    void *req;
    void *res;                  /* where `resume` stores the operation's result */
    /* data for `start` */
    void *frame, *env, *fn, *arg;
    gm_node *cx, *inst;
};

enum { GM_FB_NEW, GM_FB_RUNNING, GM_FB_SUSPENDED, GM_FB_DONE };

/* Creates a fiber of `w` on the memory [stack, stack + len), which will run start(fiber).
 * `arg_size` bytes at the top of the stack are reserved for fiber->arg. Traps if the stack
 * is smaller than GM_MIN_STACK (checked). */
GM_API gm_fiber *gm_fiber_new(gm_with *w, uint8_t *stack, size_t len, void (*start)(gm_fiber *), size_t arg_size,
                              const char *file, uint32_t line, uint32_t col);
/* Registers a new fiber as suspended on `w` (made by @fiber) and returns its continuation. */
GM_API gm_cont gm_fiber_cont(gm_with *w, gm_fiber *fb);
/* Checked builds compare each frame's address with the lowest address the running stack
   may use: the fiber's limit, or the thread's (§14.1, §18). */
#if defined(__SANITIZE_ADDRESS__)
#define GM_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define GM_ASAN 1
#endif
#endif
#if defined(GM_ASAN) || defined(__FILC__)
/* AddressSanitizer may place frames on a separate "fake stack", and Fil-C allocates frames
   whose address is taken on its heap and runs fibers on stacks of its own, so frame
   addresses say nothing about the stack's depth; both report overflows themselves. */
#define GM_NO_FRAME_PROBES 1
#define GM_STACK_CHECK(p, n) ((void)(p))
#else
#if defined(GM_FREESTANDING) || defined(GM_WASI)
/* No signal handler or exception port to report an overflow: the probes in each function
   (against a fiber's limit, and on WASI the module's own stack) are all there is. */
#define GM_NO_FRAME_PROBES 1
#endif
/* `n` bounds the bytes of the function's other locals, which may lie below its frame `p`. */
#if defined(GM_FREESTANDING) || defined(GM_WASI)
#define GM_STACK_CHECK(p, n) do { if ((uintptr_t)(p) < gm_stack_limit + (uintptr_t)(n)) gm_stack_overflow(); } while (0)
#else
/* The frame address is above the function's locals where the C compiler keeps the frame
   record at the top of the frame (x86-64, s390x, Clang on AArch64): the frame and the
   temporaries must fit below it. GCC on AArch64 keeps the record at the bottom of the
   frame unless the function has a stack protector: without one, the frame is above the
   frame address already. */
#if defined(__aarch64__) && defined(__GNUC__) && !defined(__clang__) && !defined(__SSP__) && !defined(__SSP_STRONG__) && !defined(__SSP_ALL__)
#define GM_STACK_CHECK(p, n) do { (void)(p); if ((uintptr_t)__builtin_frame_address(0) < gm_stack_limit) gm_stack_overflow(); } while (0)
#else
#define GM_STACK_CHECK(p, n) do { if ((uintptr_t)__builtin_frame_address(0) < gm_stack_limit + (uintptr_t)(n) + sizeof(*(p))) gm_stack_overflow(); } while (0)
#endif
#endif
#endif
/* The probe of a function whose calls nest at most three deep, all of them direct calls of
   functions of the same kind (the code generator decides which are): none if its frame is
   small. At most four such frames, 1 KiB, then lie below the frame of the function that
   probed last, in the space kept below the limit (GM_STACK_RESERVE on a fiber). */
#define GM_SHALLOW_FRAME 256
#define GM_STACK_CHECK_SHALLOW(p, n) do { if (sizeof(*(p)) + (n) > GM_SHALLOW_FRAME) GM_STACK_CHECK(p, n); } while (0)
GM_API_VAR GM_TLS gm_fiber *gm_cur_fiber;   /* the running fiber, or NULL on the thread's stack */
GM_API_VAR GM_TLS uintptr_t gm_stack_limit;        /* of the running stack; 0 if unknown */
GM_API_VAR GM_TLS uintptr_t gm_thread_stack_limit; /* of the thread's own stack */
/* Records the stack limit of the calling thread (at the start of `main` or of a thread). */
GM_API void gm_thread_stack(void);
GM_API _Noreturn void gm_stack_overflow(void);
/* The trap of a call of function `name`, whose frame of `bytes` no stack can hold. */
GM_API _Noreturn void gm_frame_overflow(const char *name, uint64_t bytes);
/* Driver: switches to `fb` until something happens on `w` (an event). */
GM_API void gm_with_go(gm_with *w, gm_fiber *fb);
/* Driver: calls the clause for the pending perform of w->event_fiber. */
GM_API void gm_with_clause(gm_with *w);
/* Fiber: suspends on `w` until resumed (or cancelled: gm_unw.kind is then GM_CANCEL). */
GM_API void gm_ctl_perform(gm_with *w, gm_clause_thunk thunk, void *req, void *res);
/* Fiber: ends the running fiber and switches to its driver. */
GM_API _Noreturn void gm_fiber_finish(gm_fiber *fb);
/* Clause of `w`: consumes continuation *k for `resume`; returns where to store the value. */
GM_API void *gm_resume(gm_with *w, const void *k, const char *file, uint32_t line, uint32_t col);
/* Clause of `w`: consumes continuation *k and cancels its fiber (§14.5). */
GM_API void gm_cancel_cont(gm_with *w, const void *k, const char *file, uint32_t line, uint32_t col);
/* Driver: completes `w`. `crossing`: an exit from a fiber is leaving the `with`, so the
 * suspended fibers are discarded; otherwise any suspended fiber is a checked error (§14.4).
 * Then the fibers return to the pool. */
GM_API void gm_with_end(gm_with *w, bool crossing, const char *file, uint32_t line, uint32_t col);

#endif


#define GM_NOINLINE __attribute__((noinline))
/* The step of the structural hash: the state and the word, multiplied by an odd constant
   to 128 bits, whose halves are folded together, so that every bit of the result depends
   on every bit of both. */
static inline uint64_t gm_mix64(uint64_t h, uint64_t x) {
    unsigned __int128 m = (unsigned __int128)(h ^ x) * 0x9E3779B97F4A7C15ULL;
    return (uint64_t)m ^ (uint64_t)(m >> 64);
}
static inline uint64_t gm_mix128(uint64_t h, unsigned __int128 x) {
    return gm_mix64(gm_mix64(h, (uint64_t)x), (uint64_t)(x >> 64));
}
static inline uint64_t gm_mix_float(uint64_t h, double x) {
    uint64_t bits;
    if (x == 0) x = 0;
    memcpy(&bits, &x, 8);
    return gm_mix64(h, bits);
}
/* Mixes n bytes as little-endian words of eight, the last one of the bytes that are left. */
static uint64_t gm_mix_bytes(uint64_t h, const uint8_t *p, uint64_t n) {
    for (; n >= 8; p += 8, n -= 8) {
        uint64_t w;
        memcpy(&w, p, 8);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        w = __builtin_bswap64(w);
#endif
        h = gm_mix64(h, w);
    }
    if (n > 0) {
        uint64_t w = 0;
        for (uint64_t i = 0; i < n; i++) w |= (uint64_t)p[i] << (8 * i);
        h = gm_mix64(h, w);
    }
    return h;
}
static double gm_fmin(double x, double y) {
    if (x != x) return y;
    if (y != y) return x;
    if (x == 0 && y == 0) return __builtin_signbit(x) ? x : y;
    return x < y ? x : y;
}
static double gm_fmax(double x, double y) {
    if (x != x) return y;
    if (y != y) return x;
    if (x == 0 && y == 0) return __builtin_signbit(x) ? y : x;
    return x > y ? x : y;
}
/* base ^ e by repeated squaring, wrapping at the width of T; *o is set when a product that
 * contributes to the result overflowed T (so the true result does not fit T). */
#define GM_POW_FN(name, T)                                                      \
    static T name(T b, uint64_t e, bool *o) {                                   \
        T r = 1;                                                                \
        for (;;) {                                                              \
            if ((e & 1) && __builtin_mul_overflow(r, b, &r)) *o = true;        \
            e >>= 1;                                                            \
            if (!e) return r;                                                   \
            if (__builtin_mul_overflow(b, b, &b)) *o = true;                    \
        }                                                                       \
    }
GM_POW_FN(gm_pow_u8, uint8_t)
GM_POW_FN(gm_pow_i8, int8_t)
GM_POW_FN(gm_pow_u16, uint16_t)
GM_POW_FN(gm_pow_i16, int16_t)
GM_POW_FN(gm_pow_u32, uint32_t)
GM_POW_FN(gm_pow_i32, int32_t)
GM_POW_FN(gm_pow_u64, uint64_t)
GM_POW_FN(gm_pow_i64, int64_t)
GM_POW_FN(gm_pow_u128, unsigned __int128)
static __int128 gm_pow_i128(__int128 b, uint64_t e, bool *o) {
    __int128 r = 1;
    for (;;) {
        if ((e & 1) && gm_mul_ovf_i128(r, b, &r)) *o = true;
        e >>= 1;
        if (!e) return r;
        if (gm_mul_ovf_i128(b, b, &b)) *o = true;
    }
}
/* Bit counts of the low `bits` bits of x (the caller masks the rest). */
static unsigned __int128 gm_clz(unsigned __int128 x, int bits) {
    uint64_t hi = (uint64_t)(x >> 64), lo = (uint64_t)x;
    int n = hi ? __builtin_clzll(hi) : lo ? 64 + __builtin_clzll(lo) : 128;
    return n - (128 - bits);
}
static unsigned __int128 gm_ctz(unsigned __int128 x, int bits) {
    uint64_t hi = (uint64_t)(x >> 64), lo = (uint64_t)x;
    int n = lo ? __builtin_ctzll(lo) : hi ? 64 + __builtin_ctzll(hi) : 128;
    return n < bits ? n : bits;
}
static unsigned __int128 gm_popcount(unsigned __int128 x) {
    return __builtin_popcountll((uint64_t)x) + __builtin_popcountll((uint64_t)(x >> 64));
}
static unsigned __int128 gm_bswap(unsigned __int128 x, int bits) {
    unsigned __int128 r = ((unsigned __int128)__builtin_bswap64((uint64_t)x) << 64) | __builtin_bswap64((uint64_t)(x >> 64));
    return r >> (128 - bits);
}
static unsigned __int128 gm_bitreverse(unsigned __int128 x, int bits) {
    unsigned __int128 r = 0;
    for (int i = 0; i < 4; i++) {
        uint32_t w = (uint32_t)(x >> (32 * i));
        w = ((w >> 1) & 0x55555555u) | ((w & 0x55555555u) << 1);
        w = ((w >> 2) & 0x33333333u) | ((w & 0x33333333u) << 2);
        w = ((w >> 4) & 0x0f0f0f0fu) | ((w & 0x0f0f0f0fu) << 4);
        w = __builtin_bswap32(w);
        r |= (unsigned __int128)w << (32 * (3 - i));
    }
    return r >> (128 - bits);
}
/* Rotates the low `bits` bits of x left by k (taken modulo bits). */
static unsigned __int128 gm_rotl(unsigned __int128 x, uint64_t k, int bits) {
    unsigned __int128 mask = bits == 128 ? ~(unsigned __int128)0 : (((unsigned __int128)1 << bits) - 1);
    x &= mask;
    k %= (uint64_t)bits;
    if (k == 0) return x;
    return ((x << k) | (x >> ((uint64_t)bits - k))) & mask;
}
typedef struct { _Alignas(1) uint8_t b[0]; } gm_b0_1;
typedef struct { _Alignas(8) uint8_t b[16]; } gm_b16_8;
typedef struct gm_cs9ufftihtbpo2 { uint32_t f0; uint32_t f1; } gm_cs9ufftihtbpo2;
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { void * a[2]; } gm_t5fru0lp11n7t;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { void * a[3]; } gm_thmgeksj5pdth;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { void * a[1]; } gm_tn3r4rvlh0rsv;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cs9ufftihtbpo2) == 8 && _Alignof(gm_cs9ufftihtbpo2) == 4 && offsetof(gm_cs9ufftihtbpo2, f0) == 0 && offsetof(gm_cs9ufftihtbpo2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; } gm_t2i8ne5ibd7v4;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
_Static_assert(sizeof(gm_t5fru0lp11n7t) == 16 && _Alignof(gm_t5fru0lp11n7t) == 8 && offsetof(gm_t5fru0lp11n7t, a) == 0, "layout of `[2]?^Expr`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
typedef struct { uint64_t f0; gm_b16_8 f1; gm_b16_8 f2; } gm_t7neromnu8t62;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; uint8_t v2; gm_t9qkh0rsooc8d v3; gm_t9qkh0rsooc8d v4; gm_tcc1rq4n1avb2 v5; uint32_t v6; uint32_t v7; } f1; } gm_t8588ma2cr9d7;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; gm_b16_8 f2; uint8_t f3; uint8_t f4; uint8_t pe[6]; } gm_t91vm1g0k2rpm;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tammvh1iespi3;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `(e: ^const Expr, l: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; uint64_t f3; } gm_thddbqb6snabm;
_Static_assert(sizeof(gm_thmgeksj5pdth) == 24 && _Alignof(gm_thmgeksj5pdth) == 8 && offsetof(gm_thmgeksj5pdth, a) == 0, "layout of `[3]^Expr`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; } gm_thsh8t5u55gps;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tf5srh4mp4b3i f1; uint8_t f2; uint8_t pe[7]; } gm_tjpqvfd72o62u;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
typedef struct { gm_b16_8 a[27]; } gm_tlg1vmfbpbvtq;
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_tn3r4rvlh0rsv) == 8 && _Alignof(gm_tn3r4rvlh0rsv) == 8 && offsetof(gm_tn3r4rvlh0rsv, a) == 0, "layout of `[1]^Expr`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; } gm_tq3m5qgvkpje8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; gm_t88rcucg1b7uk f3; uint8_t pe[4]; } gm_tv0k2vg3054a6;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t2i8ne5ibd7v4) == 16 && _Alignof(gm_t2i8ne5ibd7v4) == 4 && offsetof(gm_t2i8ne5ibd7v4, f0) == 0 && offsetof(gm_t2i8ne5ibd7v4, f1) == 4, "layout of `(ty: Ty, span: Span)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `(string, usize)`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_tiilmfd16v3fc v1; } f1; } gm_t716e5hdn64ba;
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t7neromnu8t62) == 40 && _Alignof(gm_t7neromnu8t62) == 8 && offsetof(gm_t7neromnu8t62, f0) == 0 && offsetof(gm_t7neromnu8t62, f1) == 8 && offsetof(gm_t7neromnu8t62, f2) == 24, "layout of `(u64, string, string)`");
_Static_assert(sizeof(gm_t8588ma2cr9d7) == 12 && _Alignof(gm_t8588ma2cr9d7) == 4 && offsetof(gm_t8588ma2cr9d7, f0) == 0 && offsetof(gm_t8588ma2cr9d7, f1) == 4, "layout of `Builtin`");
typedef struct { gm_tjpqvfd72o62u f0; uint8_t f1; uint8_t pe[7]; } gm_t8cc1mtbrhlpt;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint64_t f0; gm_t0mglksrkfa5e f1; } gm_t8ta482bl6be0;
_Static_assert(sizeof(gm_t91vm1g0k2rpm) == 40 && _Alignof(gm_t91vm1g0k2rpm) == 8 && offsetof(gm_t91vm1g0k2rpm, f0) == 0 && offsetof(gm_t91vm1g0k2rpm, f1) == 4 && offsetof(gm_t91vm1g0k2rpm, f2) == 16 && offsetof(gm_t91vm1g0k2rpm, f3) == 32 && offsetof(gm_t91vm1g0k2rpm, f4) == 33, "layout of `NumericObligation`");
typedef struct { gm_t8588ma2cr9d7 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_t9q8lhljes74c;
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tammvh1iespi3) == 56 && _Alignof(gm_tammvh1iespi3) == 8 && offsetof(gm_tammvh1iespi3, f0) == 0 && offsetof(gm_tammvh1iespi3, f1) == 16 && offsetof(gm_tammvh1iespi3, f2) == 24 && offsetof(gm_tammvh1iespi3, f3) == 40, "layout of `EffectInfo`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { gm_tlcm4lvu3g1a8 f0; uint8_t f1; uint8_t pe[7]; } gm_tb7u19loo3j87;
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { gm_tep2drsl9408b f0; uint8_t f1; uint8_t pe[7]; } gm_tdlpv0q69lov1;
typedef struct { uint64_t f0; gm_tv0k2vg3054a6 f1; } gm_tdvhflnc2f5o8;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `(addr: string, t: Ty)`");
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thddbqb6snabm) == 48 && _Alignof(gm_thddbqb6snabm) == 8 && offsetof(gm_thddbqb6snabm, f0) == 0 && offsetof(gm_thddbqb6snabm, f1) == 16 && offsetof(gm_thddbqb6snabm, f2) == 24 && offsetof(gm_thddbqb6snabm, f3) == 40, "layout of `(string, u64, string, u64)`");
typedef struct { gm_tbngattl1fo51 f0; uint8_t f1; uint8_t pe[3]; } gm_thetuhtouve7f;
_Static_assert(sizeof(gm_thsh8t5u55gps) == 20 && _Alignof(gm_thsh8t5u55gps) == 4 && offsetof(gm_thsh8t5u55gps, f0) == 0 && offsetof(gm_thsh8t5u55gps, f1) == 4 && offsetof(gm_thsh8t5u55gps, f2) == 8, "layout of `(from: Ty, to: Ty, span: Span)`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
typedef struct { gm_tiilmfd16v3fc f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tj44oivvogvtl;
typedef struct { gm_t5v0c8ia6a5mo f0; uint8_t f1; uint8_t pe[7]; } gm_tj8m6gmlgjilo;
_Static_assert(sizeof(gm_tjpqvfd72o62u) == 32 && _Alignof(gm_tjpqvfd72o62u) == 8 && offsetof(gm_tjpqvfd72o62u, f0) == 0 && offsetof(gm_tjpqvfd72o62u, f1) == 8 && offsetof(gm_tjpqvfd72o62u, f2) == 24, "layout of `SoaInfo`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tlg1vmfbpbvtq) == 432 && _Alignof(gm_tlg1vmfbpbvtq) == 8 && offsetof(gm_tlg1vmfbpbvtq, a) == 0, "layout of `[27]string`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { gm_t8588ma2cr9d7 f0; gm_t8588ma2cr9d7 f1; gm_t8588ma2cr9d7 f2; gm_t8588ma2cr9d7 f3; } gm_tpk661un0rsod;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2i8ne5ibd7v4 v0; gm_thsh8t5u55gps v1; gm_t2i8ne5ibd7v4 v2; gm_t91vm1g0k2rpm v3; } f1; } gm_tpm220j5qhjke;
_Static_assert(sizeof(gm_tq3m5qgvkpje8) == 40 && _Alignof(gm_tq3m5qgvkpje8) == 8 && offsetof(gm_tq3m5qgvkpje8, f0) == 0 && offsetof(gm_tq3m5qgvkpje8, f1) == 16 && offsetof(gm_tq3m5qgvkpje8, f2) == 24, "layout of `(string, usize, string)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tv0k2vg3054a6) == 48 && _Alignof(gm_tv0k2vg3054a6) == 8 && offsetof(gm_tv0k2vg3054a6, f0) == 0 && offsetof(gm_tv0k2vg3054a6, f1) == 16 && offsetof(gm_tv0k2vg3054a6, f2) == 24 && offsetof(gm_tv0k2vg3054a6, f3) == 40, "layout of `OpInfo`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t716e5hdn64ba) == 32 && _Alignof(gm_t716e5hdn64ba) == 8 && offsetof(gm_t716e5hdn64ba, f0) == 0 && offsetof(gm_t716e5hdn64ba, f1) == 8, "layout of `EffectNamed`");
_Static_assert(sizeof(gm_t8cc1mtbrhlpt) == 40 && _Alignof(gm_t8cc1mtbrhlpt) == 8 && offsetof(gm_t8cc1mtbrhlpt, f0) == 0 && offsetof(gm_t8cc1mtbrhlpt, f1) == 32, "layout of `?SoaInfo`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8ta482bl6be0) == 48 && _Alignof(gm_t8ta482bl6be0) == 8 && offsetof(gm_t8ta482bl6be0, f0) == 0 && offsetof(gm_t8ta482bl6be0, f1) == 8, "layout of `(i: usize, x: FieldDef)`");
_Static_assert(sizeof(gm_t9q8lhljes74c) == 32 && _Alignof(gm_t9q8lhljes74c) == 8 && offsetof(gm_t9q8lhljes74c, f0) == 0 && offsetof(gm_t9q8lhljes74c, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tb7u19loo3j87) == 24 && _Alignof(gm_tb7u19loo3j87) == 8 && offsetof(gm_tb7u19loo3j87, f0) == 0 && offsetof(gm_tb7u19loo3j87, f1) == 16, "layout of `?(len: u64, elem: Ty)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdlpv0q69lov1) == 32 && _Alignof(gm_tdlpv0q69lov1) == 8 && offsetof(gm_tdlpv0q69lov1, f0) == 0 && offsetof(gm_tdlpv0q69lov1, f1) == 24, "layout of `?(variants: []VariantDef, tag: Ty)`");
_Static_assert(sizeof(gm_tdvhflnc2f5o8) == 56 && _Alignof(gm_tdvhflnc2f5o8) == 8 && offsetof(gm_tdvhflnc2f5o8, f0) == 0 && offsetof(gm_tdvhflnc2f5o8, f1) == 8, "layout of `(i: usize, x: OpInfo)`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
_Static_assert(sizeof(gm_thetuhtouve7f) == 12 && _Alignof(gm_thetuhtouve7f) == 4 && offsetof(gm_thetuhtouve7f, f0) == 0 && offsetof(gm_thetuhtouve7f, f1) == 8, "layout of `?(inclusive: bool, elem: Ty)`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tj44oivvogvtl) == 40 && _Alignof(gm_tj44oivvogvtl) == 8 && offsetof(gm_tj44oivvogvtl, f0) == 0 && offsetof(gm_tj44oivvogvtl, f1) == 24, "layout of `Performed`");
_Static_assert(sizeof(gm_tj8m6gmlgjilo) == 64 && _Alignof(gm_tj8m6gmlgjilo) == 8 && offsetof(gm_tj8m6gmlgjilo, f0) == 0 && offsetof(gm_tj8m6gmlgjilo, f1) == 56, "layout of `?ParamInfo`");
typedef struct { gm_t716e5hdn64ba f0; uint8_t f1; uint8_t pe[7]; } gm_tlbrdhah49ci5;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tpk661un0rsod) == 48 && _Alignof(gm_tpk661un0rsod) == 4 && offsetof(gm_tpk661un0rsod, f0) == 0 && offsetof(gm_tpk661un0rsod, f1) == 12 && offsetof(gm_tpk661un0rsod, f2) == 24 && offsetof(gm_tpk661un0rsod, f3) == 36, "layout of `(Builtin, Builtin, Builtin, Builtin)`");
_Static_assert(sizeof(gm_tpm220j5qhjke) == 48 && _Alignof(gm_tpm220j5qhjke) == 8 && offsetof(gm_tpm220j5qhjke, f0) == 0 && offsetof(gm_tpm220j5qhjke, f1) == 8, "layout of `Obligation`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; uint64_t f2; gm_b16_8 f3; gm_t88rcucg1b7uk f4; uint8_t p5[4]; gm_tnpifv20irnph f5; gm_b16_8 f6; gm_tqu3m2l3n8mnc f7; uint8_t f8; uint8_t pe[7]; } gm_tqqhoivahirr0;
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tlbrdhah49ci5) == 40 && _Alignof(gm_tlbrdhah49ci5) == 8 && offsetof(gm_tlbrdhah49ci5, f0) == 0 && offsetof(gm_tlbrdhah49ci5, f1) == 32, "layout of `?EffectNamed`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tqqhoivahirr0) == 136 && _Alignof(gm_tqqhoivahirr0) == 8 && offsetof(gm_tqqhoivahirr0, f0) == 0 && offsetof(gm_tqqhoivahirr0, f1) == 16 && offsetof(gm_tqqhoivahirr0, f2) == 32 && offsetof(gm_tqqhoivahirr0, f3) == 40 && offsetof(gm_tqqhoivahirr0, f4) == 56 && offsetof(gm_tqqhoivahirr0, f5) == 64 && offsetof(gm_tqqhoivahirr0, f6) == 80 && offsetof(gm_tqqhoivahirr0, f7) == 96 && offsetof(gm_tqqhoivahirr0, f8) == 128, "layout of `FnSig`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_t9q8lhljes74c v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; void * v55; gm_tandvbt63uk7n v56; gm_t710lsrq1m8gk v57; } f1; } gm_to0f5qn75cgg8;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t0ialelq3ql1k v1; uint32_t v2; gm_ts78ca2amthgu v3; gm_t88rcucg1b7uk v4; } f1; } gm_t930q5l38hdap;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_to0f5qn75cgg8) == 64 && _Alignof(gm_to0f5qn75cgg8) == 8 && offsetof(gm_to0f5qn75cgg8, f0) == 0 && offsetof(gm_to0f5qn75cgg8, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_to0f5qn75cgg8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tsf6i2agfnpva;
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
_Static_assert(sizeof(gm_t930q5l38hdap) == 208 && _Alignof(gm_t930q5l38hdap) == 8 && offsetof(gm_t930q5l38hdap, f0) == 0 && offsetof(gm_t930q5l38hdap, f1) == 8, "layout of `LocalBinding`");
typedef struct { gm_t930q5l38hdap f0; uint8_t f1; uint8_t pe[7]; } gm_tqvu78rpi6q1h;
_Static_assert(sizeof(gm_tsf6i2agfnpva) == 88 && _Alignof(gm_tsf6i2agfnpva) == 8 && offsetof(gm_tsf6i2agfnpva, f0) == 0 && offsetof(gm_tsf6i2agfnpva, f1) == 8 && offsetof(gm_tsf6i2agfnpva, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_tqvu78rpi6q1h) == 216 && _Alignof(gm_tqvu78rpi6q1h) == 8 && offsetof(gm_tqvu78rpi6q1h, f0) == 0 && offsetof(gm_tqvu78rpi6q1h, f1) == 208, "layout of `?LocalBinding`");
struct gm_fr_add_note_46v502s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_adjust_receiver_2j7250l {
    gm_frame h;
    void * ret;
};
struct gm_fr_arity_vmamjfj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_bits_cast_0kaubje {
    gm_frame h;
    void * ret;
};
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_builtin_a7cjd7d {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* vals */
    void * ret;
};
struct gm_fr_builtin_method_jist12a {
    gm_frame h;
    gm_t53at80pfdus3 l10; /* k */
    void * ret;
};
struct gm_fr_check_from_tag_g3el2co {
    gm_frame h;
    gm_tckdobunaulee l7; /* v */
    void * ret;
};
struct gm_fr_collection_method_1a7d15p {
    gm_frame h;
    gm_t4gut2c9fj1kt l28; /* d */
    void * ret;
};
struct gm_fr_core_fn_uts2vhe {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_decl_visible_bd168ti {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_effect_named_b8afdbp {
    gm_frame h;
    gm_tlbrdhah49ci5 ret;
};
struct gm_fr_emit_dph5v1e {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_emit_with_continuation_n_lv5ofqt {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d0 */
    gm_t4gut2c9fj1kt l4; /* d */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fh7svmj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qm5tp1g {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_r81t436 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vhibpt8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vj3ckdj_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_9huj44a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_expr_o6qt7e2 {
    gm_frame h;
    void * ret;
};
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_extend_gig5o6g {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_nkql463 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_finish_call_kjknkfg {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* stmts */
    void * ret;
};
struct gm_fr_fits_receiver_1qqamgd {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_cast_mismatch_e9s21md {
    gm_frame h;
    void * l3; /* tys */
    gm_closure l4; /* pointer */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_format_0cb2lu1 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4frhu1b {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_560qv19 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_6pukbnn {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7b9kq2t {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7f83ds0 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_8fu7rb8 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_agi9f7f {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ahnpa17 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cpioo77 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dir32ml {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ercsatq {
    gm_frame h;
    gm_tq3m5qgvkpje8 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fu8jpss {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hpnf26s {
    gm_frame h;
    gm_thddbqb6snabm l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ilcl0ue {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jfku65e {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lsckir6 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_mpl2gek {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nf4pnfb {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ohgvj26 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_p66k75v {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_p9hcrrd {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_pe22rd6 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qe2mgnq {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qe94i5r {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rn1vtpg {
    gm_frame h;
    gm_t7neromnu8t62 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_uesfu03 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_up1o5ed {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vtr1b5e {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_get_espvf96 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_has_infer_3o82a0b {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_math_vector_09mtuao {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_plain_data_ej4mh2m {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_vector_gkq6b88 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_u4oqjin {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ir0h3tc_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lookup_59juqoo {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_method_call_hebo20o {
    gm_frame h;
    gm_t0mglksrkfa5e l18; /* f */
    gm_t4gut2c9fj1kt l25; /* d */
    void * ret;
};
struct gm_fr_module_method_cb3lmfk {
    gm_frame h;
    gm_tqqhoivahirr0 l14; /* sig */
    gm_tj8m6gmlgjilo l15; /* first */
    gm_t5v0c8ia6a5mo l18; /* p */
    gm_t5v0c8ia6a5mo l19; /* p */
    gm_t5v0c8ia6a5mo l21; /* fp */
    gm_trqfvmjtt4sij l24; /* vals */
    void * ret;
};
struct gm_fr_numeric_conversion_1ovq7at {
    gm_frame h;
    void * ret;
};
struct gm_fr_numeric_method_jo5uq0l {
    gm_frame h;
    void * ret;
};
struct gm_fr_numeric_pair_54urfaa {
    gm_frame h;
    void * ret;
};
struct gm_fr_op_method_82pb0m6 {
    gm_frame h;
    gm_tammvh1iespi3 l13; /* info */
    gm_tv0k2vg3054a6 l17; /* o */
    gm_trqfvmjtt4sij l22; /* vals */
    void * ret;
};
struct gm_fr_optional_rh6okj3 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_param_with_qfvlipt {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_perform_named_h7gr9vi {
    gm_frame h;
    gm_tammvh1iespi3 l9; /* info */
    gm_tv0k2vg3054a6 l13; /* o */
    void * ret;
};
struct gm_fr_pointer_cast_6siqq5h {
    gm_frame h;
    void * l5; /* tys */
    gm_closure l7; /* kind */
    gm_t53at80pfdus3 l13; /* fa */
    gm_t53at80pfdus3 l15; /* fb */
    gm_tddtjfggdat0d l16; /* a */
    gm_tddtjfggdat0d l17; /* b */
    void * ret;
};
struct gm_fr_pointer_gv6lvmk {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_ptr_t1h2m2d {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_push_0mk7udh {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3onuaru {
    gm_frame h;
    gm_tj44oivvogvtl l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_7koe9lk {
    gm_frame h;
    gm_tpm220j5qhjke l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d943vgp_2 {
    gm_frame h;
    gm_tsf6i2agfnpva l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_61fb705_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_emnjbt3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h08har5_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_s9eh08e {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_v0p8ic9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_same_1qdglea {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_same_repr_830dsa9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_show_8uijkbs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_fsnl6fa {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_slice_fn_np9f5vf {
    gm_frame h;
    void * ret;
};
struct gm_fr_takes_receiver_uhu9min {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_template_9o92cvv {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_type_arg_grgffgp {
    gm_frame h;
    gm_tc3oht4ksth9d l4; /* e */
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unsigned_of_n48qlse {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unsupported_r3r7qdh {
    gm_frame h;
    void * ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_value_arg_i5l23i1 {
    gm_frame h;
    gm_tc3oht4ksth9d l5; /* e */
    void * ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq0lfe1s2pin(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq5307j5k9ms(const uint8_t *a, const uint8_t *b);
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqasplg81cl8(const uint8_t *a, const uint8_t *b);
inline bool gm_eqebiqthsuot(const uint8_t *a, const uint8_t *b);
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b);
inline bool gm_eqg63o53ar52(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
inline bool gm_eqov4e3mbi0f(const uint8_t *a, const uint8_t *b);
inline bool gm_eqtje7cegoml(const uint8_t *a, const uint8_t *b);
inline bool gm_equ1rt9sffme(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_adjust_receiver_2j7250l(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, const void * a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) uint8_t gm_f_arity_vmamjfj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_auto_deref_vdb1hdn(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_bind_args_dnih8ih(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4, gm_b16_8 a5);
__attribute__((aligned(4))) void * gm_f_bind_temp_sa5rcbr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void * gm_f_bits_cast_0kaubje(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_struct_ty_l1kjtap(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_block_typed_e7svg7n(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_builtin_a7cjd7d(void *env, gm_node *cx, gm_node *inst, gm_t8588ma2cr9d7 a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_builtin_method_jist12a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, const void * a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_call_value_t2nv21i(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_check_from_tag_g3el2co(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_collection_method_1a7d15p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_construct_variant_o6is81c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2, const void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_core_fn_uts2vhe(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint8_t gm_f_decl_visible_bd168ti(void * a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_effect_named_b8afdbp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_embeds_2jpqm45(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_emit_with_continuation_n_lv5ofqt(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_tl874t2atnlh3 a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qm5tp1g(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vhibpt8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_gig5o6g(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_nkql463(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_ovslcai(void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_finish_call_kjknkfg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, uint8_t a3);
__attribute__((aligned(4))) uint8_t gm_f_fits_receiver_1qqamgd(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) void gm_f_fn_cast_mismatch_e9s21md(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_format_0cb2lu1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_2bh0ost(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_2t66fge(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_3tlgo2e(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_4frhu1b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_560qv19(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_6pukbnn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_7b9kq2t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_7f83ds0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_8fu7rb8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_agi9f7f(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ahnpa17(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_b73b468(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_cpioo77(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_dir32ml(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ercsatq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_fu8jpss(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_hpnf26s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ilcl0ue(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_jfku65e(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_lsckir6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_mpl2gek(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_nf4pnfb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ohgvj26(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_p66k75v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_p9hcrrd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_pe22rd6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_qe2mgnq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_qe94i5r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_rn1vtpg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_uesfu03(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_up1o5ed(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_vtr1b5e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void * gm_f_generic_call_2tlofdi(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_math_vector_09mtuao(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_plain_data_ej4mh2m(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_is_plain_n60iihm(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_vector_gkq6b88(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_kind_u4oqjin(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_cs9ufftihtbpo2 gm_f_line_col_9vn24mu(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_lookup_local_6f38t7p(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_module_name_cukfp7t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void * gm_f_math_vector_method_ko2814s(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void gm_f_math_vector_parts_49c7865(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_method_call_hebo20o(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_member_5fd2qm9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, uint64_t a3);
__attribute__((aligned(4))) void * gm_f_module_method_cb3lmfk(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint8_t a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_of_n0d5f6n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_note_use_from_2lmujsq(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) void * gm_f_numeric_conversion_1ovq7at(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, uint8_t a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_numeric_method_jo5uq0l(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, uint8_t a4, uint8_t a5, gm_b16_8 a6, gm_tl874t2atnlh3 a7);
__attribute__((aligned(4))) void * gm_f_numeric_pair_54urfaa(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_op_method_82pb0m6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, uint64_t a4, const void * a5, gm_b16_8 a6, gm_tl874t2atnlh3 a7);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_optional_rh6okj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_param_has_2r09oc7(void * a0, uint32_t a1, gm_t9qkh0rsooc8d a2);
inline __attribute__((aligned(4))) uint8_t gm_f_param_with_qfvlipt(void * a0, gm_csguankko4lmhd a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) void * gm_f_perform_named_h7gr9vi(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_perform_op_1nih4r3(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_perform_op_as_9ncunsc(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint32_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_place_mutability_0tlm4g8(void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_pointer_cast_6siqq5h(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) uint8_t gm_f_pointer_gv6lvmk(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_ptr_t1h2m2d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3onuaru(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_7koe9lk(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_thetuhtouve7f gm_f_range_of_dmmo768(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_record_effects_0eel5vu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_emnjbt3(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h08har5_2(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_s9eh08e(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_v0p8ic9(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_resolve_effect_ref_bggirs2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_ty_uk6c1lp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_same_1qdglea(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_same_repr_830dsa9(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_shallow_8ur803a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_slice_fn_np9f5vf(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_soa_copy_from_vrsguuh(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_soa_deref_uiefo9t(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_soa_free_9554eha(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void gm_f_soa_info_pq4q43l(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_soa_make_5imjrl1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void gm_f_soa_of_jufdcj2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_struct_fields_chqu3it(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_takes_receiver_uhu9min(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_arg_grgffgp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_of_name_oqtujpn(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unify_operands_5sp7j1j(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_union_variants_a7khaqp(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unsigned_of_n48qlse(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void * gm_f_unsupported_r3r7qdh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_value_arg_i5l23i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_var_kind_1sl5sig(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_vector_load_ssdvomk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_vector_method_hgn4sb1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void gm_f_vector_parts_crrfopm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_visible_ll07dr9(void * a0, gm_t9qkh0rsooc8d a1, uint64_t a2, uint64_t a3);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern uint8_t gm_cukabrstocl[432] __attribute__((aligned(8)));
extern uint8_t gm_kdoa98a1tm0[40] __attribute__((aligned(8)));
extern const uint8_t gm_s01k0p56k10gk[29];
extern const uint8_t gm_s02419ud122hi[8];
extern const uint8_t gm_s03pmf867dcsr[2];
extern const uint8_t gm_s0tt0537if3ve[29];
extern const uint8_t gm_s1eo6mkdbgh2b[38];
extern const uint8_t gm_s1mud7cvvuuon[4];
extern const uint8_t gm_s1pmmpf8t54g4[23];
extern const uint8_t gm_s2gpk5q7mg4o8[5];
extern const uint8_t gm_s2mtoab140jp6[4];
extern const uint8_t gm_s398novfl4jrd[5];
extern const uint8_t gm_s3e818t2qbjbh[5];
extern const uint8_t gm_s3hqs40g2kokp[9];
extern const uint8_t gm_s3k9reqbrnuns[25];
extern const uint8_t gm_s4108vvmvu0gn[70];
extern const uint8_t gm_s420tm9ua7ggg[4];
extern const uint8_t gm_s42aeamnkrchf[31];
extern const uint8_t gm_s4pmpk07ugvq6[5];
extern const uint8_t gm_s541nrvershlm[7];
extern const uint8_t gm_s55kqbt4f7gau[4];
extern const uint8_t gm_s5iv4icrhn1vp[19];
extern const uint8_t gm_s66fk044n1n0k[9];
extern const uint8_t gm_s6a2c11qu9scr[13];
extern const uint8_t gm_s6vvtrmtnt3j1[5];
extern const uint8_t gm_s7f68g16j7a3o[6];
extern const uint8_t gm_s7moa4u0q6e5d[8];
extern const uint8_t gm_s7rl9plavbrfo[17];
extern const uint8_t gm_s7u0p3hthk80r[9];
extern const uint8_t gm_s7uql3407tind[10];
extern const uint8_t gm_s8m12els4res5[26];
extern const uint8_t gm_s9ahj85e3uamt[12];
extern const uint8_t gm_sa92qppdjht4t[100];
extern const uint8_t gm_sadj3uigeodiu[29];
extern const uint8_t gm_sasg9nroe4gk1[4];
extern const uint8_t gm_sbanjlvk3n14i[4];
extern const uint8_t gm_sbb1ri5er9djq[4];
extern const uint8_t gm_sbl3pbs9195ho[4];
extern const uint8_t gm_sbm73uc3nf0c8[6];
extern const uint8_t gm_sbvutvpa8pe3d[3];
extern const uint8_t gm_scn0a8pnba0ho[41];
extern const uint8_t gm_sd2iiq8cuaks3[5];
extern const uint8_t gm_sedq8b22f9agn[91];
extern const uint8_t gm_selatfetsf85i[10];
extern const uint8_t gm_semec5mr3jo2t[73];
extern const uint8_t gm_sf1hl3n20st3g[37];
extern const uint8_t gm_sfr85ju00e9hk[138];
extern const uint8_t gm_sfrjcsmisriv1[3];
extern const uint8_t gm_sg7m4t90kovch[35];
extern const uint8_t gm_sg7of7eks8a5u[7];
extern const uint8_t gm_sgb0oed9c3voo[5];
extern const uint8_t gm_sgemqs85b1iol[9];
extern const uint8_t gm_sgvvkov5kvprt[10];
extern const uint8_t gm_shb5mqv6etf1t[5];
extern const uint8_t gm_shne2m0cj5kgi[6];
extern const uint8_t gm_sjb60mo5hejk2[6];
extern const uint8_t gm_sk4tdtvin0g62[31];
extern const uint8_t gm_sk6ueggk5ji6v[10];
extern const uint8_t gm_skdhvc36gfkb0[47];
extern const uint8_t gm_skgcaidqnnhjr[11];
extern const uint8_t gm_skj7cpsc7j6g9[5];
extern const uint8_t gm_sljj5lb6g47i1[38];
extern const uint8_t gm_sm139pecc5n3p[6];
extern const uint8_t gm_sm1kt2tr7m23b[4];
extern const uint8_t gm_sm2k5tp7dtg07[4];
extern const uint8_t gm_sm3o0uerjumpm[4];
extern const uint8_t gm_smggdkb5phvp0[56];
extern const uint8_t gm_sn6gnsehoh5ru[7];
extern const uint8_t gm_snf4ams51ofsv[81];
extern const uint8_t gm_snivf25e5fme8[6];
extern const uint8_t gm_snlqf7m58jjdu[5];
extern const uint8_t gm_soefp7bosmvt1[12];
extern const uint8_t gm_sou5f0qr4vdgr[8];
extern const uint8_t gm_spducfcn5c083[69];
extern const uint8_t gm_spevct0q3cjgb[5];
extern const uint8_t gm_spskd7qirpoe3[25];
extern const uint8_t gm_sq2ta0ln2qvqo[47];
extern const uint8_t gm_sqfrv3g5v9m2n[5];
extern const uint8_t gm_sqv6e5e2cne7b[59];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const uint8_t gm_sr5j1k0g236qh[68];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src6lm8esmv16p1[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srck5orfpjti60c[];
extern const char gm_srcngug8co99ok1[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_srk226j8lb4rq[9];
extern const uint8_t gm_srltdqer7h3t7[55];
extern const uint8_t gm_srv4cua6q51nq[5];
extern const uint8_t gm_ss226ibsv86g3[15];
extern const uint8_t gm_ss3f61htk015v[7];
extern const uint8_t gm_ss496b65r9omj[6];
extern const uint8_t gm_ssc48vnvhmmnh[9];
extern const uint8_t gm_st4r3sao0kja5[6];
extern const uint8_t gm_st88sqv2u9dqh[8];
extern const uint8_t gm_sttkmg61kqc3k[65];
extern const uint8_t gm_su0h0fbu8l4ri[30];
extern const uint8_t gm_sukj4damah3dd[85];
extern const uint8_t gm_svlfsqdbbpmt2[52];
extern const uint8_t gm_svpofgjtij7vj[4];
extern const gm_tmpl gm_tpl0tr163o4bba1;
extern const gm_tmpl gm_tpl12e8u6hvkejb;
extern const gm_tmpl gm_tpl355ags9581ob;
extern const gm_tmpl gm_tpl3cldid2v21cr;
extern const gm_tmpl gm_tpl5536lp5da6eq;
extern const gm_tmpl gm_tpl5vbp9fc0mpc1;
extern const gm_tmpl gm_tpl9l9rgf8juo7o;
extern const gm_tmpl gm_tpla0ldfomuin4r;
extern const gm_tmpl gm_tplad75mid9f0qe;
extern const gm_tmpl gm_tplbgmktlahcqrn;
extern const gm_tmpl gm_tplbtqgdimqhkr1;
extern const gm_tmpl gm_tplcfgu8t9fq299;
extern const gm_tmpl gm_tplcseelhcfsg75;
extern const gm_tmpl gm_tpldc3nmdc0cit1;
extern const gm_tmpl gm_tpldrfjfgoqe179;
extern const gm_tmpl gm_tpldti7tgsj3l9q;
extern const gm_tmpl gm_tplg0rms0t7302m;
extern const gm_tmpl gm_tplghi16hba81gr;
extern const gm_tmpl gm_tplhr6emralh8q6;
extern const gm_tmpl gm_tplhtkhdjclkudk;
extern const gm_tmpl gm_tplk25d3iiaamil;
extern const gm_tmpl gm_tplkvqpieru7v5v;
extern const gm_tmpl gm_tplm5g4n5l4k6ck;
extern const gm_tmpl gm_tplm7tetbq1snr9;
extern const gm_tmpl gm_tplnitaca1dp5li;
extern const gm_tmpl gm_tplo867268s51oj;
extern const gm_tmpl gm_tplovlajhkvntql;
extern const gm_tmpl gm_tplqd1jqet3gtu1;
extern const gm_tmpl gm_tplsb492k540eai;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq0lfe1s2pin(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    return true;
}
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq5307j5k9ms(const uint8_t *a, const uint8_t *b) {
    if (a[1] != b[1]) return false;
    return !a[1] || gm_equ1rt9sffme(a, b);
}
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b) {
    return (a[0] != 0) == (b[0] != 0);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline bool gm_eqasplg81cl8(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqebiqthsuot(const uint8_t *a, const uint8_t *b) {
    if (a[1] != b[1]) return false;
    return !a[1] || gm_eq929lennfd3(a, b);
}
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqg63o53ar52(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)21) return gm_eqehhdtpeti1(a + 4, b + 4);
    if (ta == (uint8_t)57) return gm_eqasplg81cl8(a + 4, b + 4);
    if (ta == (uint8_t)58) return gm_eqjmccqdb90f(a + 4, b + 4);
    if (ta == (uint8_t)69) return gm_eqov4e3mbi0f(a + 4, b + 4);
    if (ta == (uint8_t)73) return gm_eqtje7cegoml(a + 4, b + 4);
    if (ta == (uint8_t)80) return gm_eq0lfe1s2pin(a + 4, b + 4);
    if (ta == (uint8_t)81) return gm_eq467d5cm1d5(a + 4, b + 4);
    if (ta == (uint8_t)82) return gm_eq467d5cm1d5(a + 4, b + 4);
    return true;
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline bool gm_eqov4e3mbi0f(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqtje7cegoml(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_equ1rt9sffme(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_note_46v502s F;
    void * gm_l0; /* d */
    gm_b16_8 gm_l1; /* note */
    GM_STACK_CHECK(&F, 61u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l2;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 80);
    gm_b16_8 t4 = gm_l1;
    gm_tftb1odc739t5 t5 = gm_f_push_3ef746b(t3, t4);
    gm_l2 = t5;
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srcgfglnv8p059r, 96, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_bool_ty_pn2p277 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)4ULL));
    gm_t88rcucg1b7uk t4 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t2, &t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t4;
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_decl_visible_bd168ti(void * a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_decl_visible_bd168ti F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    uint64_t gm_l2; /* from */
    void * gm_l3; /* decl */
    GM_STACK_CHECK_SHALLOW(&F, 124u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 240));
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t3) + 240) + 8));
    uint64_t t6 = gm_l1;
    if ((uintptr_t)t6 >= (uintptr_t)(t5)) gm_trap_index((uintptr_t)t6, t5, gm_src6lm8esmv16p1, 190, 14);
    void * t7 = (void *)(((uint8_t *)t4) + (uintptr_t)t6 * 216);
    gm_l3 = t7;
    void * t8 = gm_l0;
    void * t9 = gm_l3;
    gm_t9qkh0rsooc8d t10 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t9) + 40) + 56));
    void * t11 = gm_l3;
    uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 32));
    uint64_t t13 = gm_l2;
    uint8_t t14 = gm_f_visible_ll07dr9(t8, t10, t12, t13);
    F.ret = t14;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_emit_dph5v1e F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 8));
    gm_f_push_o4ckdr1(t4, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l1)));
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_5nn7ph0 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * gm_l1;
    void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
    gm_l1 = t2;
    uint8_t t3 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l1)) != 0);
    if (!t3) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
    void * t4 = GM_LD(void *, ((uint8_t *)&gm_l1));
    gm_l0 = t4;
    void * t5 = gm_l0;
    uint64_t t6 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d914i5u F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * gm_l1;
    void * t2 = GM_INT_TO_PTR(((uint64_t)4ULL));
    gm_l1 = t2;
    uint8_t t3 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l1)) != 0);
    if (!t3) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
    void * t4 = GM_LD(void *, ((uint8_t *)&gm_l1));
    gm_l0 = t4;
    void * t5 = gm_l0;
    uint64_t t6 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 4)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fh7svmj F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * gm_l1;
    void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
    gm_l1 = t2;
    uint8_t t3 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l1)) != 0);
    if (!t3) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
    void * t4 = GM_LD(void *, ((uint8_t *)&gm_l1));
    gm_l0 = t4;
    void * t5 = gm_l0;
    uint64_t t6 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 40)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g3tddrb_2 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * gm_l1;
    void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
    gm_l1 = t2;
    uint8_t t3 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l1)) != 0);
    if (!t3) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
    void * t4 = GM_LD(void *, ((uint8_t *)&gm_l1));
    gm_l0 = t4;
    void * t5 = gm_l0;
    uint64_t t6 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 88)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ijtp9lo F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * gm_l1;
    void * t2 = GM_INT_TO_PTR(((uint64_t)1ULL));
    gm_l1 = t2;
    uint8_t t3 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l1)) != 0);
    if (!t3) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
    void * t4 = GM_LD(void *, ((uint8_t *)&gm_l1));
    gm_l0 = t4;
    void * t5 = gm_l0;
    uint64_t t6 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qm5tp1g(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qm5tp1g F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * gm_l1;
    void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
    gm_l1 = t2;
    uint8_t t3 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l1)) != 0);
    if (!t3) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
    void * t4 = GM_LD(void *, ((uint8_t *)&gm_l1));
    gm_l0 = t4;
    void * t5 = gm_l0;
    uint64_t t6 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 48)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_r81t436 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * gm_l1;
    void * t2 = GM_INT_TO_PTR(((uint64_t)4ULL));
    gm_l1 = t2;
    uint8_t t3 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l1)) != 0);
    if (!t3) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
    void * t4 = GM_LD(void *, ((uint8_t *)&gm_l1));
    gm_l0 = t4;
    void * t5 = gm_l0;
    uint64_t t6 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 12)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vhibpt8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vhibpt8 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * gm_l1;
    void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
    gm_l1 = t2;
    uint8_t t3 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l1)) != 0);
    if (!t3) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
    void * t4 = GM_LD(void *, ((uint8_t *)&gm_l1));
    gm_l0 = t4;
    void * t5 = gm_l0;
    uint64_t t6 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 40)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vj3ckdj_2 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * gm_l1;
    void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
    gm_l1 = t2;
    uint8_t t3 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l1)) != 0);
    if (!t3) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
    void * t4 = GM_LD(void *, ((uint8_t *)&gm_l1));
    gm_l0 = t4;
    void * t5 = gm_l0;
    uint64_t t6 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 32)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_error_89glhjr F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 8));
    gm_tl874t2atnlh3 t5 = gm_l1;
    gm_b16_8 t6 = gm_l2;
    gm_b0_1 t7 = gm_f_report_eardf07((void *)NULL, cx, NULL, t4, t5, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_error_9huj44a F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* msg */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    gm_tl874t2atnlh3 t4 = gm_l1;
    gm_b16_8 t5 = gm_l2;
    gm_b0_1 t6 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t3, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_error_expr_o6qt7e2 F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 156u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = gm_f_empty_slice_g3tddrb_2();
    gm_tl874t2atnlh3 t6 = gm_l1;
    gm_to0f5qn75cgg8 t7;
    memset(&t7, 0, sizeof t7);
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)35ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t7 + 8), t5);
    void * t8 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t4, &t7, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t8;
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_error_ki8i3k6 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)26ULL));
    gm_t88rcucg1b7uk t4 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t2, &t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t4;
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_gig5o6g(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_gig5o6g F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* xs */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l3;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    gm_l2 = t3;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 16));
    uint64_t t6 = gm_l2;
    uint64_t r7; bool o8 = __builtin_sub_overflow(t5, t6, &r7);
    if (o8) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 80, 8);
    uint64_t t9 = r7;
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t11 = (t9 < t10);
    if (t11) {
        void * t12 = gm_l0;
        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        gm_b16_8 t14 = gm_l1;
        gm_tftb1odc739t5 t15 = gm_f_regrow_61fb705_2(t12, t13, t14);
        gm_l3 = t15;
        uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
        if (t16) {
        } else {
            gm_t9qkh0rsooc8d t17 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l3) + 1));
            gm_tftb1odc739t5 t18;
            ((uint8_t *)&t18)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t18 + 1), t17);
            F.ret = t18;
            goto gm_fret;
        }
    } else {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, ((uint8_t *)t19));
        uint64_t t21 = gm_l2;
        uint64_t t22 = gm_l2;
        uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint64_t r24; bool o25 = __builtin_add_overflow(t22, t23, &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 84, 36);
        uint64_t t26 = r24;
        uint64_t t27 = ((uintptr_t)t26) - (uintptr_t)t21;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 88)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 88);
    }
    void * t30 = gm_l0;
    void * t31 = gm_l0;
    void * t32 = GM_LD(void *, ((uint8_t *)t31));
    uint64_t t33 = gm_l2;
    uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t r35; bool o36 = __builtin_add_overflow(t33, t34, &r35);
    if (o36) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 86, 30);
    uint64_t t37 = r35;
    uint64_t t38 = ((uintptr_t)t37) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t39;
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 88)));
    GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(t38));
    GM_ST(gm_b16_8, ((uint8_t *)t30), t39);
    gm_tftb1odc739t5 t40;
    GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
    ((uint8_t *)&t40)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t40 + 1), GM_ZERO(gm_b0_1));
    F.ret = t40;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_nkql463(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_nkql463 F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* xs */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l3;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    gm_l2 = t3;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 16));
    uint64_t t6 = gm_l2;
    uint64_t r7; bool o8 = __builtin_sub_overflow(t5, t6, &r7);
    if (o8) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 80, 8);
    uint64_t t9 = r7;
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t11 = (t9 < t10);
    if (t11) {
        void * t12 = gm_l0;
        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        gm_b16_8 t14 = gm_l1;
        gm_tftb1odc739t5 t15 = gm_f_regrow_h08har5_2(t12, t13, t14);
        gm_l3 = t15;
        uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
        if (t16) {
        } else {
            gm_t9qkh0rsooc8d t17 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l3) + 1));
            gm_tftb1odc739t5 t18;
            ((uint8_t *)&t18)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t18 + 1), t17);
            F.ret = t18;
            goto gm_fret;
        }
    } else {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, ((uint8_t *)t19));
        uint64_t t21 = gm_l2;
        uint64_t t22 = gm_l2;
        uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint64_t r24; bool o25 = __builtin_add_overflow(t22, t23, &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 84, 36);
        uint64_t t26 = r24;
        uint64_t t27 = ((uintptr_t)t26) - (uintptr_t)t21;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 32);
    }
    void * t30 = gm_l0;
    void * t31 = gm_l0;
    void * t32 = GM_LD(void *, ((uint8_t *)t31));
    uint64_t t33 = gm_l2;
    uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t r35; bool o36 = __builtin_add_overflow(t33, t34, &r35);
    if (o36) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 86, 30);
    uint64_t t37 = r35;
    uint64_t t38 = ((uintptr_t)t37) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t39;
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 32)));
    GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(t38));
    GM_ST(gm_b16_8, ((uint8_t *)t30), t39);
    gm_tftb1odc739t5 t40;
    GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
    ((uint8_t *)&t40)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t40 + 1), GM_ZERO(gm_b0_1));
    F.ret = t40;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_file_o15fqku F;
    void * gm_l0; /* sm */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_srcv1nri2247d2r, 82, 75);
    void * t7 = (void *)(((uint8_t *)t3) + (uintptr_t)t6 * 80);
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_format_0cb2lu1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_0cb2lu1 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sg7m4t90kovch));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(34));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplad75mid9f0qe), t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_format_4frhu1b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_4frhu1b F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1pmmpf8t54g4));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(22));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplg0rms0t7302m), t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_format_560qv19(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_560qv19 F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s01k0p56k10gk));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(28));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpla0ldfomuin4r), t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_format_6pukbnn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_6pukbnn F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sq2ta0ln2qvqo));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(46));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplghi16hba81gr), t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_format_7b9kq2t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7b9kq2t F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbm73uc3nf0c8));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplkvqpieru7v5v), t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_format_7f83ds0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_7f83ds0 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sedq8b22f9agn));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(90));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl9l9rgf8juo7o), t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_format_8fu7rb8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_8fu7rb8 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s0tt0537if3ve));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(28));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl12e8u6hvkejb), t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_format_agi9f7f(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_agi9f7f F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sou5f0qr4vdgr));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplhr6emralh8q6), t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_format_ahnpa17(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0) {
    struct gm_fr_format_ahnpa17 F;
    gm_t2lqu8jsj511d gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t2lqu8jsj511d t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sk4tdtvin0g62));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(30));
    gm_f_format_2bh0ost((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplbtqgdimqhkr1), t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_format_cpioo77(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_cpioo77 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sr5j1k0g236qh));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(67));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl355ags9581ob), t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_format_dir32ml(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_dir32ml F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sf1hl3n20st3g));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(36));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplo867268s51oj), t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_format_ercsatq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ercsatq F;
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s3k9reqbrnuns));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(24));
    gm_tq3m5qgvkpje8 t3 = GM_LD(gm_tq3m5qgvkpje8, ((uint8_t *)a0));
    gm_f_format_b73b468((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl0tr163o4bba1), t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_format_fu8jpss(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_fu8jpss F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sljj5lb6g47i1));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(37));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplnitaca1dp5li), t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_format_hpnf26s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_hpnf26s F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sttkmg61kqc3k));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(64));
    gm_thddbqb6snabm t3 = GM_LD(gm_thddbqb6snabm, ((uint8_t *)a0));
    gm_f_format_3tlgo2e((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl5536lp5da6eq), t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_format_ilcl0ue(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ilcl0ue F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_su0h0fbu8l4ri));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(29));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplsb492k540eai), t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_format_jfku65e(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_jfku65e F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sqv6e5e2cne7b));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(58));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpldti7tgsj3l9q), t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_format_lsckir6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_lsckir6 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4108vvmvu0gn));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(69));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl5vbp9fc0mpc1), t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_format_mpl2gek(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_mpl2gek F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_snf4ams51ofsv));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(80));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl3cldid2v21cr), t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_format_nf4pnfb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_nf4pnfb F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_scn0a8pnba0ho));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(40));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplcfgu8t9fq299), t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_format_ohgvj26(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ohgvj26 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_svlfsqdbbpmt2));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(51));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplbgmktlahcqrn), t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_format_p66k75v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_p66k75v F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_semec5mr3jo2t));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(72));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplm7tetbq1snr9), t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_format_p9hcrrd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_p9hcrrd F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s1eo6mkdbgh2b));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(37));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplqd1jqet3gtu1), t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_format_pe22rd6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_pe22rd6 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_spducfcn5c083));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(68));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplcseelhcfsg75), t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_format_qe2mgnq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_qe2mgnq F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sa92qppdjht4t));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(99));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpldc3nmdc0cit1), t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_format_qe94i5r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_qe94i5r F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skdhvc36gfkb0));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(46));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplovlajhkvntql), t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_format_rn1vtpg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_rn1vtpg F;
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sukj4damah3dd));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(84));
    gm_t7neromnu8t62 t3 = GM_LD(gm_t7neromnu8t62, ((uint8_t *)a0));
    gm_f_format_2t66fge((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplk25d3iiaamil), t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_format_uesfu03(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_uesfu03 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sadj3uigeodiu));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(28));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplhtkhdjclkudk), t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_format_up1o5ed(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_up1o5ed F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_srltdqer7h3t7));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(54));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplm5g4n5l4k6ck), t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_format_vtr1b5e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_vtr1b5e F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smggdkb5phvp0));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(55));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpldrfjfgoqe179), t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_espvf96 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 193u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t4 = gm_f_find_ovslcai(t2, t3);
    gm_l3 = t4;
    uint8_t t5 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
    if (t5) {
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
        gm_l2 = t6;
    } else {
        gm_tf5srh4mp4b3i t7;
        memset(&t7, 0, sizeof t7);
        F.ret = t7;
        goto gm_fret;
    }
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, ((uint8_t *)t8));
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t8) + 8));
    uint64_t t11 = gm_l2;
    if ((uintptr_t)t11 >= (uintptr_t)(t10)) gm_trap_index((uintptr_t)t11, t10, gm_srcbsc23is03ttm, 139, 12);
    uint64_t t12 = GM_LD(uint64_t, ((((uint8_t *)t9) + (uintptr_t)t11 * 32) + 24));
    gm_tf5srh4mp4b3i t13;
    GM_ST(uint8_t, (uint8_t *)&t13 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t13 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t13 + 12, 0);
    GM_ST(uint64_t, ((uint8_t *)&t13), t12);
    ((uint8_t *)&t13)[8] = 1;
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_infer_3o82a0b F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 53u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 40));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 40) + 8));
    uint32_t t5 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 252, 57);
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)t3) + (uintptr_t)t6 * 1));
    uint8_t t8 = (uint8_t)(t7 & ((uint8_t)2ULL));
    uint8_t t9 = (!(t8 == ((uint8_t)0ULL)));
    F.ret = t9;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_param_u9t0efs F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 53u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 40));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 40) + 8));
    uint32_t t5 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 249, 57);
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)t3) + (uintptr_t)t6 * 1));
    uint8_t t8 = (uint8_t)(t7 & ((uint8_t)1ULL));
    uint8_t t9 = (!(t8 == ((uint8_t)0ULL)));
    F.ret = t9;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2) {
    struct gm_fr_int_ci6airg F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* signed */
    uint16_t gm_l2; /* bits */
    GM_STACK_CHECK(&F, 94u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    uint8_t t3 = gm_l1;
    uint16_t t4 = gm_l2;
    gm_tjcj4j2gbh6c8 t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), t3);
    GM_ST(uint16_t, ((uint8_t *)&t5 + 2), t4);
    gm_t53at80pfdus3 t6;
    memset(&t6, 0, sizeof t6);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tjcj4j2gbh6c8, ((uint8_t *)&t6 + 8), t5);
    gm_t88rcucg1b7uk t7 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t2, &t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t7;
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_error_8k27gd1 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 140u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_t53at80pfdus3 t4;
    gm_f_kind_jcnt4hm((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
    uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    uint8_t t6 = (uint8_t)((t5 == ((uint8_t)26ULL)));
    gm_t53at80pfdus3 t7;
    memset(&t7, 0, sizeof t7);
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)26ULL));
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_math_vector_09mtuao(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_math_vector_09mtuao F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 53u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint8_t gm_l2;
    gm_tb7u19loo3j87 gm_l3;
    void * t5 = gm_l0;
    gm_t88rcucg1b7uk t6 = gm_l1;
    gm_f_math_vector_parts_49c7865((void *)&gm_l3, t5, GM_CONV(gm_csguankko4lmhd, t6));
    {
        {
            uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
            if (t7) {
                goto gm_lb2;
            }
            gm_l2 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l2 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srck5orfpjti60c, 26, 62);
    }
gm_lb1:;
    uint8_t t8 = gm_l2;
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_plain_data_ej4mh2m(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_is_plain_data_ej4mh2m F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 66u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    uint8_t t6 = gm_f_is_error_8k27gd1(t4, GM_CONV(gm_csguankko4lmhd, t5));
    if (t6) {
        gm_l2 = ((uint8_t)1);
    } else {
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, ((uint8_t *)t7));
        gm_t88rcucg1b7uk t9 = gm_l1;
        uint8_t t10 = gm_f_is_plain_n60iihm(t8, GM_CONV(gm_csguankko4lmhd, t9));
        gm_l2 = t10;
    }
    uint8_t t11 = gm_l2;
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_vector_gkq6b88(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_vector_gkq6b88 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 53u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint8_t gm_l2;
    gm_tb7u19loo3j87 gm_l3;
    void * t5 = gm_l0;
    gm_t88rcucg1b7uk t6 = gm_l1;
    gm_f_vector_parts_crrfopm((void *)&gm_l3, t5, GM_CONV(gm_csguankko4lmhd, t6));
    {
        {
            uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
            if (t7) {
                goto gm_lb2;
            }
            gm_l2 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l2 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srck5orfpjti60c, 25, 57);
    }
gm_lb1:;
    uint8_t t8 = gm_l2;
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_jcnt4hm F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    uint32_t t5 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 151, 54);
    memmove(gm_out, (((uint8_t *)t3) + (uintptr_t)t6 * 56), 56);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_go2qg0b F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_d914i5u();
    gm_node *pn3 = gm_find(cx, 587034433);
    if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcrgc9bkquf6nr, 13, 97);
    gm_t3mf58gv9kf3n t4;
    t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 16), ((uint64_t)0ULL));
    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 24), t4);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ir0h3tc_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_vj3ckdj_2();
    gm_node *pn3 = gm_find(cx, 587034433);
    if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcrgc9bkquf6nr, 13, 97);
    gm_t3mf58gv9kf3n t4;
    t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 16), ((uint64_t)0ULL));
    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 24), t4);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_uvfu1k1 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_ijtp9lo();
    gm_node *pn3 = gm_find(cx, 587034433);
    if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcrgc9bkquf6nr, 13, 97);
    gm_t3mf58gv9kf3n t4;
    t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 16), ((uint64_t)0ULL));
    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 24), t4);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vp1djv1_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_g3tddrb_2();
    gm_node *pn3 = gm_find(cx, 587034433);
    if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcrgc9bkquf6nr, 13, 97);
    gm_t3mf58gv9kf3n t4;
    t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 16), ((uint64_t)0ULL));
    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 24), t4);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lookup_59juqoo F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 200));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 200) + 8));
    uint64_t t5 = gm_l1;
    if ((uintptr_t)t5 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t5, t4, gm_srcc2qutubarto1, 547, 78);
    void * t6 = (void *)((((uint8_t *)t3) + (uintptr_t)t5 * 184) + 56);
    gm_b16_8 t7 = gm_l2;
    gm_tf5srh4mp4b3i t8 = gm_f_get_espvf96(t6, t7);
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_optional_rh6okj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_optional_rh6okj3 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* inner */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_t53at80pfdus3 t4;
    memset(&t4, 0, sizeof t4);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)13ULL));
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 8), t3);
    gm_t88rcucg1b7uk t5 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t2, &t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t5;
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_param_with_qfvlipt(void * a0, gm_csguankko4lmhd a1, gm_t9qkh0rsooc8d a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_param_with_qfvlipt F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t9qkh0rsooc8d gm_l2; /* c */
    uint32_t gm_l3; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 131u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l4;
    {
        {
            void * t4 = gm_l0;
            void * t5 = GM_LD(void *, ((uint8_t *)t4));
            void * t6 = (void *)(((uint8_t *)t5) + 8);
            gm_t88rcucg1b7uk t7 = gm_l1;
            gm_f_kind_jcnt4hm((void *)&gm_l4, t6, GM_CONV(gm_csguankko4lmhd, t7));
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            uint8_t t9 = (uint8_t)(t8 == ((uint8_t)19ULL));
            if (!t9) {
                goto gm_lb2;
            }
            uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 8));
            gm_l3 = t10;
            void * t11 = gm_l0;
            void * t12 = GM_LD(void *, ((uint8_t *)t11));
            uint32_t t13 = gm_l3;
            gm_t9qkh0rsooc8d t14 = gm_l2;
            uint8_t t15 = gm_f_param_has_2r09oc7(t12, t13, t14);
            F.ret = t15;
            goto gm_fret;
        }
gm_lb2:;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_printer_h7jigd7 F;
    void * gm_l0; /* t */
    void * gm_l1; /* out */
    GM_STACK_CHECK_SHALLOW(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = gm_l1;
    GM_ST(void *, ((uint8_t *)&(*(gm_tmm67r2c94d5j *)gm_out) + 0), t2);
    GM_ST(void *, ((uint8_t *)&(*(gm_tmm67r2c94d5j *)gm_out) + 8), t3);
    GM_ST(uint32_t, ((uint8_t *)&(*(gm_tmm67r2c94d5j *)gm_out) + 16), ((uint32_t)0ULL));
    GM_ST(uint32_t, ((uint8_t *)&(*(gm_tmm67r2c94d5j *)gm_out) + 20), ((uint32_t)0ULL));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_ptr_t1h2m2d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_ptr_t1h2m2d F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* is_const */
    gm_t88rcucg1b7uk gm_l2; /* pointee */
    GM_STACK_CHECK(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    uint8_t t3 = gm_l1;
    gm_t88rcucg1b7uk t4 = gm_l2;
    gm_tbngattl1fo51 t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), t3);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t5 + 4), t4);
    gm_t53at80pfdus3 t6;
    memset(&t6, 0, sizeof t6);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)8ULL));
    GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t6 + 8), t5);
    gm_t88rcucg1b7uk t7 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t2, &t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t7;
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_0mk7udh F;
    void * gm_l0; /* l */
    gm_tl874t2atnlh3 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_tftb1odc739t5 gm_l3;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    gm_l2 = t3;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 16));
    uint64_t t6 = gm_l2;
    uint8_t t7 = ((t5 == t6));
    if (t7) {
        void * t8 = gm_l0;
        gm_b16_8 t9 = gm_f_empty_slice_r81t436();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_v0p8ic9(t8, ((uint64_t)1ULL), t10);
        gm_l3 = t11;
        uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
        if (t12) {
        } else {
            gm_t9qkh0rsooc8d t13 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l3) + 1));
            gm_tftb1odc739t5 t14;
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 1), t13);
            F.ret = t14;
            goto gm_fret;
        }
    }
    void * t15 = gm_l0;
    void * t16 = GM_LD(void *, ((uint8_t *)t15));
    uint64_t t17 = gm_l2;
    gm_tl874t2atnlh3 t18 = gm_l1;
    GM_ST(gm_tl874t2atnlh3, (((uint8_t *)t16) + (uintptr_t)t17 * 12), t18);
    void * t19 = gm_l0;
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, ((uint8_t *)t20));
    uint64_t t22 = gm_l2;
    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
    uint64_t t25 = r23;
    uint64_t t26 = ((uintptr_t)t25) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t27;
    GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t21 + (uintptr_t)((uint64_t)0ULL) * 12)));
    GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(t26));
    GM_ST(gm_b16_8, ((uint8_t *)t19), t27);
    gm_tftb1odc739t5 t28;
    GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
    ((uint8_t *)&t28)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t28 + 1), GM_ZERO(gm_b0_1));
    F.ret = t28;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_3ef746b F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l3;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    gm_l2 = t3;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 16));
    uint64_t t6 = gm_l2;
    uint8_t t7 = ((t5 == t6));
    if (t7) {
        void * t8 = gm_l0;
        gm_b16_8 t9 = gm_f_empty_slice_5nn7ph0();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_0jmnnpk(t8, ((uint64_t)1ULL), t10);
        gm_l3 = t11;
        uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
        if (t12) {
        } else {
            gm_t9qkh0rsooc8d t13 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l3) + 1));
            gm_tftb1odc739t5 t14;
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 1), t13);
            F.ret = t14;
            goto gm_fret;
        }
    }
    void * t15 = gm_l0;
    void * t16 = GM_LD(void *, ((uint8_t *)t15));
    uint64_t t17 = gm_l2;
    gm_b16_8 t18 = gm_l1;
    GM_ST(gm_b16_8, (((uint8_t *)t16) + (uintptr_t)t17 * 16), t18);
    void * t19 = gm_l0;
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, ((uint8_t *)t20));
    uint64_t t22 = gm_l2;
    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
    uint64_t t25 = r23;
    uint64_t t26 = ((uintptr_t)t25) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t27;
    GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t21 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(t26));
    GM_ST(gm_b16_8, ((uint8_t *)t19), t27);
    gm_tftb1odc739t5 t28;
    GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
    ((uint8_t *)&t28)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t28 + 1), GM_ZERO(gm_b0_1));
    F.ret = t28;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3onuaru(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_3onuaru F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_tftb1odc739t5 gm_l3;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    gm_l2 = t3;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 16));
    uint64_t t6 = gm_l2;
    uint8_t t7 = ((t5 == t6));
    if (t7) {
        void * t8 = gm_l0;
        gm_b16_8 t9 = gm_f_empty_slice_vhibpt8();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_s9eh08e(t8, ((uint64_t)1ULL), t10);
        gm_l3 = t11;
        uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
        if (t12) {
        } else {
            gm_t9qkh0rsooc8d t13 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l3) + 1));
            gm_tftb1odc739t5 t14;
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 1), t13);
            F.ret = t14;
            goto gm_fret;
        }
    }
    void * t15 = gm_l0;
    void * t16 = GM_LD(void *, ((uint8_t *)t15));
    uint64_t t17 = gm_l2;
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 40), ((uint8_t *)&F.l1), 40);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 40)));
    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25));
    GM_ST(gm_b16_8, ((uint8_t *)t18), t26);
    gm_tftb1odc739t5 t27;
    GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
    ((uint8_t *)&t27)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t27 + 1), GM_ZERO(gm_b0_1));
    F.ret = t27;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_7koe9lk(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_7koe9lk F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_tftb1odc739t5 gm_l3;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    gm_l2 = t3;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 16));
    uint64_t t6 = gm_l2;
    uint8_t t7 = ((t5 == t6));
    if (t7) {
        void * t8 = gm_l0;
        gm_b16_8 t9 = gm_f_empty_slice_qm5tp1g();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_emnjbt3(t8, ((uint64_t)1ULL), t10);
        gm_l3 = t11;
        uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
        if (t12) {
        } else {
            gm_t9qkh0rsooc8d t13 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l3) + 1));
            gm_tftb1odc739t5 t14;
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 1), t13);
            F.ret = t14;
            goto gm_fret;
        }
    }
    void * t15 = gm_l0;
    void * t16 = GM_LD(void *, ((uint8_t *)t15));
    uint64_t t17 = gm_l2;
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 48), ((uint8_t *)&F.l1), 48);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 48)));
    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25));
    GM_ST(gm_b16_8, ((uint8_t *)t18), t26);
    gm_tftb1odc739t5 t27;
    GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
    ((uint8_t *)&t27)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t27 + 1), GM_ZERO(gm_b0_1));
    F.ret = t27;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_d943vgp_2 F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_tftb1odc739t5 gm_l3;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    gm_l2 = t3;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 16));
    uint64_t t6 = gm_l2;
    uint8_t t7 = ((t5 == t6));
    if (t7) {
        void * t8 = gm_l0;
        gm_b16_8 t9 = gm_f_empty_slice_g3tddrb_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_61fb705_2(t8, ((uint64_t)1ULL), t10);
        gm_l3 = t11;
        uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
        if (t12) {
        } else {
            gm_t9qkh0rsooc8d t13 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l3) + 1));
            gm_tftb1odc739t5 t14;
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 1), t13);
            F.ret = t14;
            goto gm_fret;
        }
    }
    void * t15 = gm_l0;
    void * t16 = GM_LD(void *, ((uint8_t *)t15));
    uint64_t t17 = gm_l2;
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 88), ((uint8_t *)&F.l1), 88);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 88)));
    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25));
    GM_ST(gm_b16_8, ((uint8_t *)t18), t26);
    gm_tftb1odc739t5 t27;
    GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
    ((uint8_t *)&t27)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t27 + 1), GM_ZERO(gm_b0_1));
    F.ret = t27;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_0jmnnpk F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = t2;
    uint64_t t4 = gm_l1;
    void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
    void * t6 = t5;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)16ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_61fb705_2 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = t2;
    uint64_t t4 = gm_l1;
    void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
    void * t6 = t5;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)88ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_emnjbt3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_emnjbt3 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = t2;
    uint64_t t4 = gm_l1;
    void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
    void * t6 = t5;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)48ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h08har5_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_h08har5_2 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = t2;
    uint64_t t4 = gm_l1;
    void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
    void * t6 = t5;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)32ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_s9eh08e(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_s9eh08e F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = t2;
    uint64_t t4 = gm_l1;
    void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
    void * t6 = t5;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)40ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_v0p8ic9(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_v0p8ic9 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = t2;
    uint64_t t4 = gm_l1;
    void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
    void * t6 = t5;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)12ULL), ((uint64_t)4ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_report_eardf07 F;
    void * gm_l0; /* ds */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 256u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    gm_tl874t2atnlh3 t3 = gm_l1;
    gm_b16_8 t4 = gm_l2;
    gm_t4gut2c9fj1kt t5;
    gm_f_error_s6hn871((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_push_o4ckdr1(t2, &t5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_8uijkbs F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_b16_8 t5 = gm_f_show_kmjeh8s((void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t5;
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_fsnl6fa F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_b16_8 t5 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t5;
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_kmjeh8s F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_tmm67r2c94d5j gm_l3; /* p */
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_trqfvmjtt4sij t2;
        gm_f_list_uvfu1k1((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t2;
    }
    {
        void * t3 = gm_l0;
        void * t4 = (void *)((uint8_t *)&F.l2);
        gm_tmm67r2c94d5j t5;
        gm_f_printer_h7jigd7((void *)&t5, t3, t4);
        gm_l3 = t5;
    }
    void * t6 = (void *)((uint8_t *)&gm_l3);
    gm_t88rcucg1b7uk t7 = gm_l1;
    gm_b0_1 t8 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t6, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
    F.ret = t10;
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_slice_5dt9121 F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* is_const */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    GM_STACK_CHECK(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    uint8_t t3 = gm_l1;
    gm_t88rcucg1b7uk t4 = gm_l2;
    gm_tbngattl1fo51 t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), t3);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t5 + 4), t4);
    gm_t53at80pfdus3 t6;
    memset(&t6, 0, sizeof t6);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)10ULL));
    GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t6 + 8), t5);
    gm_t88rcucg1b7uk t7 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t2, &t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t7;
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_template_9o92cvv F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 176));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 176) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 276, 56);
    uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)t3) + (uintptr_t)t6 * 4));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_u8_ty_afkvj25 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 28u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t2, ((uint8_t)0), ((uint16_t)8ULL));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t3;
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_unsupported_r3r7qdh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_r3r7qdh F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 132u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    gm_tl874t2atnlh3 t4 = gm_l1;
    gm_b16_8 t5 = gm_l2;
    gm_b0_1 t6 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t3, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    gm_tl874t2atnlh3 t9 = gm_l1;
    void * t10 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t8, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t10;
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_usize_ty_qnrd30f F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)1ULL));
    gm_t88rcucg1b7uk t4 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t2, &t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t4;
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_zero_s25l990 F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t2;
    gm_f_list_go2qg0b((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 0), ((uint8_t)0));
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 8), t2);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void * gm_f_adjust_receiver_2j7250l(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, const void * a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_adjust_receiver_2j7250l F;
    void * gm_l0; /* cx */
    void * gm_l1; /* recv */
    gm_t88rcucg1b7uk gm_l2; /* rty */
    gm_t88rcucg1b7uk gm_l3; /* param */
    gm_t08olq97rscn3 gm_l4; /* name */
    gm_tl874t2atnlh3 gm_l5; /* span */
    void * gm_l6; /* tys */
    uint8_t gm_l7; /* c */
    gm_t88rcucg1b7uk gm_l8; /* pointee */
    uint8_t gm_l9; /* m */
    uint8_t gm_l10; /* c1 */
    gm_t88rcucg1b7uk gm_l11; /* pointee */
    uint8_t gm_l12; /* c2 */
    gm_t88rcucg1b7uk gm_l13; /* p2 */
    gm_t88rcucg1b7uk gm_l14; /* pointee */
    GM_STACK_CHECK(&F, 1289u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l5 = a5;
    gm_t53at80pfdus3 gm_l15;
    gm_tqs5p4n2tq19v gm_l16;
    gm_tabb710d9mq2e gm_l17;
    gm_tabb710d9mq2e gm_l18;
    gm_t53at80pfdus3 gm_l19;
    gm_t53at80pfdus3 gm_l20;
    gm_t53at80pfdus3 gm_l21;
    gm_tabb710d9mq2e gm_l22;
    void * t12 = gm_l0;
    void * t13 = GM_LD(void *, ((uint8_t *)t12));
    void * t14 = (void *)(((uint8_t *)t13) + 8);
    gm_l6 = t14;
    void * t15 = gm_l6;
    gm_t88rcucg1b7uk t16 = gm_l3;
    gm_t88rcucg1b7uk t17 = gm_l2;
    uint8_t t18 = gm_f_fits_receiver_1qqamgd(t15, GM_CONV(gm_csguankko4lmhd, t16), GM_CONV(gm_csguankko4lmhd, t17));
    if (t18) {
        void * t19 = gm_l1;
        F.ret = t19;
        goto gm_fret;
    }
    {
        {
            {
                void * t20 = gm_l6;
                gm_t88rcucg1b7uk t21 = gm_l3;
                gm_f_kind_jcnt4hm((void *)&gm_l15, t20, GM_CONV(gm_csguankko4lmhd, t21));
            }
            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 0));
            uint8_t t23 = (uint8_t)(t22 == ((uint8_t)8ULL));
            if (!t23) {
                goto gm_lb2;
            }
            uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 8));
            gm_l7 = t24;
            gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l15) + 8) + 4));
            gm_l8 = t25;
            void * t26 = gm_l6;
            gm_t88rcucg1b7uk t27 = gm_l8;
            gm_t88rcucg1b7uk t28 = gm_l2;
            uint8_t t29 = gm_f_fits_receiver_1qqamgd(t26, GM_CONV(gm_csguankko4lmhd, t27), GM_CONV(gm_csguankko4lmhd, t28));
            if (!t29) {
                goto gm_lb2;
            }
            {
                {
                    void * t30 = gm_l0;
                    void * t31 = gm_l1;
                    gm_tqs5p4n2tq19v t32 = gm_f_place_mutability_0tlm4g8(t30, t31);
                    gm_l16 = t32;
                    uint8_t t33 = (uint8_t)((((uint8_t *)&gm_l16))[1] != 0);
                    if (!t33) {
                        goto gm_lb4;
                    }
                    uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)&gm_l16));
                    gm_l9 = t34;
                    {
                        {
                            uint8_t t35 = gm_l9;
                            uint8_t t36 = (uint8_t)!t35;
                            if (!t36) {
                                goto gm_lb6;
                            }
                            uint8_t t37 = gm_l7;
                            uint8_t t38 = (uint8_t)!t37;
                            if (!t38) {
                                goto gm_lb6;
                            }
                            void * t39 = gm_l0;
                            void * t40 = gm_l1;
                            gm_tl874t2atnlh3 t41 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t40) + 72));
                            gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)a4));
                            gm_tnpifv20irnph t43;
                            GM_ST(gm_b16_8, ((uint8_t *)&t43 + 0), t42);
                            gm_f_format_ilcl0ue((void *)&gm_l17, (void *)NULL, cx, NULL, t43);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t44 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                            if (!t44) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_srcngug8co99ok1, 488, 54);
                            gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
                            gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
                            gm_b0_1 t47 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t39, t41, t46);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb5;
                        }
gm_lb6:;
                    }
gm_lb5:;
                    void * t48 = gm_l6;
                    uint8_t t49 = gm_l7;
                    gm_t88rcucg1b7uk t50 = gm_l2;
                    gm_t88rcucg1b7uk t51 = gm_f_ptr_t1h2m2d((void *)NULL, cx, NULL, t48, t49, t50);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t52 = gm_l1;
                    gm_tl874t2atnlh3 t53 = gm_l5;
                    gm_to0f5qn75cgg8 t54;
                    memset(&t54, 0, sizeof t54);
                    GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)24ULL));
                    GM_ST(void *, ((uint8_t *)&t54 + 8), t52);
                    void * t55 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t51, &t54, t53);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t55;
                    goto gm_fret;
                }
gm_lb4:;
            }
gm_lb3:;
            uint8_t t56 = gm_l7;
            if (t56) {
                void * t57 = gm_l6;
                gm_t88rcucg1b7uk t58 = gm_l2;
                gm_t88rcucg1b7uk t59 = gm_f_ptr_t1h2m2d((void *)NULL, cx, NULL, t57, ((uint8_t)1), t58);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t60 = gm_l1;
                gm_tl874t2atnlh3 t61 = gm_l5;
                gm_to0f5qn75cgg8 t62;
                memset(&t62, 0, sizeof t62);
                GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)25ULL));
                GM_ST(void *, ((uint8_t *)&t62 + 8), t60);
                void * t63 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t59, &t62, t61);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t63;
                goto gm_fret;
            }
            {
                void * t64 = gm_l0;
                void * t65 = gm_l1;
                gm_tl874t2atnlh3 t66 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t65) + 72));
                gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)a4));
                gm_tnpifv20irnph t68;
                GM_ST(gm_b16_8, ((uint8_t *)&t68 + 0), t67);
                gm_f_format_lsckir6((void *)&gm_l18, (void *)NULL, cx, NULL, t68);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t69 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                if (!t69) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_srcngug8co99ok1, 492, 29);
                gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
                gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&t70));
                gm_b0_1 t72 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t64, t66, t71);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            F.ret = ((void *)0);
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            {
                void * t73 = gm_l6;
                gm_t88rcucg1b7uk t74 = gm_l3;
                gm_f_kind_jcnt4hm((void *)&gm_l19, t73, GM_CONV(gm_csguankko4lmhd, t74));
            }
            uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
            uint8_t t76 = (uint8_t)(t75 == ((uint8_t)8ULL));
            if (!t76) {
                goto gm_lb8;
            }
            uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 8));
            gm_l10 = t77;
            gm_t88rcucg1b7uk t78 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l19) + 8) + 4));
            gm_l11 = t78;
            {
                void * t79 = gm_l6;
                gm_t88rcucg1b7uk t80 = gm_l2;
                gm_f_kind_jcnt4hm((void *)&gm_l20, t79, GM_CONV(gm_csguankko4lmhd, t80));
            }
            uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&gm_l20) + 0));
            uint8_t t82 = (uint8_t)(t81 == ((uint8_t)8ULL));
            if (!t82) {
                goto gm_lb8;
            }
            uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&gm_l20) + 8));
            gm_l12 = t83;
            gm_t88rcucg1b7uk t84 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l20) + 8) + 4));
            gm_l13 = t84;
            void * t85 = gm_l6;
            gm_t88rcucg1b7uk t86 = gm_l11;
            gm_t88rcucg1b7uk t87 = gm_l13;
            uint8_t t88 = gm_f_fits_receiver_1qqamgd(t85, GM_CONV(gm_csguankko4lmhd, t86), GM_CONV(gm_csguankko4lmhd, t87));
            if (!t88) {
                goto gm_lb8;
            }
            uint8_t t89 = gm_l10;
            if (t89) {
                void * t90 = gm_l0;
                void * t91 = gm_l1;
                void * t92 = gm_l6;
                gm_t88rcucg1b7uk t93 = gm_l13;
                gm_t88rcucg1b7uk t94 = gm_f_ptr_t1h2m2d((void *)NULL, cx, NULL, t92, ((uint8_t)1), t93);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t95 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t90, t91, t94);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t95;
                goto gm_fret;
            }
            uint8_t t96 = gm_l12;
            uint8_t t97 = (uint8_t)!t96;
            if (t97) {
                void * t98 = gm_l1;
                F.ret = t98;
                goto gm_fret;
            }
            goto gm_lb7;
        }
gm_lb8:;
    }
gm_lb7:;
    {
        {
            {
                void * t99 = gm_l6;
                gm_t88rcucg1b7uk t100 = gm_l2;
                gm_f_kind_jcnt4hm((void *)&gm_l21, t99, GM_CONV(gm_csguankko4lmhd, t100));
            }
            uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
            uint8_t t102 = (uint8_t)(t101 == ((uint8_t)8ULL));
            if (!t102) {
                goto gm_lb10;
            }
            gm_t88rcucg1b7uk t103 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l21) + 8) + 4));
            gm_l14 = t103;
            void * t104 = gm_l6;
            gm_t88rcucg1b7uk t105 = gm_l3;
            gm_t88rcucg1b7uk t106 = gm_l14;
            uint8_t t107 = gm_f_fits_receiver_1qqamgd(t104, GM_CONV(gm_csguankko4lmhd, t105), GM_CONV(gm_csguankko4lmhd, t106));
            if (!t107) {
                goto gm_lb10;
            }
            {
                gm_t88rcucg1b7uk t108 = gm_l14;
                void * t109 = gm_l1;
                gm_tl874t2atnlh3 t110 = gm_l5;
                gm_to0f5qn75cgg8 t111;
                memset(&t111, 0, sizeof t111);
                GM_ST(uint8_t, ((uint8_t *)&t111 + 0), ((uint8_t)18ULL));
                GM_ST(void *, ((uint8_t *)&t111 + 8), t109);
                void * t112 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t108, &t111, t110);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t112;
                goto gm_fret;
            }
        }
gm_lb10:;
    }
gm_lb9:;
    {
        void * t113 = gm_l0;
        gm_tl874t2atnlh3 t114 = gm_l5;
        gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)a4));
        void * t116 = gm_l0;
        gm_t88rcucg1b7uk t117 = gm_l2;
        gm_b16_8 t118 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t116, t117);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6j6smj7qoctu t119;
        GM_ST(gm_b16_8, ((uint8_t *)&t119 + 0), t115);
        GM_ST(gm_b16_8, ((uint8_t *)&t119 + 16), t118);
        gm_f_format_nf4pnfb((void *)&gm_l22, (void *)NULL, cx, NULL, &t119);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t120 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
        if (!t120) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 8, gm_srcngug8co99ok1, 500, 20);
        gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
        gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&t121));
        gm_b0_1 t123 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t113, t114, t122);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    F.ret = ((void *)0);
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_arity_vmamjfj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_arity_vmamjfj F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* args */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* name */
    gm_tl874t2atnlh3 gm_l4; /* span */
    GM_STACK_CHECK(&F, 293u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_b16_8 gm_l5;
    gm_tabb710d9mq2e gm_l6;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t3 = gm_l2;
    uint8_t t4 = (!(t2 == t3));
    if (t4) {
        {
            void * t5 = gm_l0;
            gm_tl874t2atnlh3 t6 = gm_l4;
            gm_b16_8 t7 = gm_l3;
            uint64_t t8 = gm_l2;
            uint64_t t9 = gm_l2;
            uint8_t t10 = ((t9 == ((uint64_t)1ULL)));
            if (t10) {
                gm_b16_8 t11;
                GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(0));
                gm_l5 = t11;
            } else {
                gm_b16_8 t12;
                GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s03pmf867dcsr));
                GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(1));
                gm_l5 = t12;
            }
            gm_b16_8 t13 = gm_l5;
            gm_tq3m5qgvkpje8 t14;
            GM_ST(gm_b16_8, ((uint8_t *)&t14 + 0), t7);
            GM_ST(uint64_t, ((uint8_t *)&t14 + 16), t8);
            GM_ST(gm_b16_8, ((uint8_t *)&t14 + 24), t13);
            gm_f_format_ercsatq((void *)&gm_l6, (void *)NULL, cx, NULL, &t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t15 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
            if (!t15) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_srcngug8co99ok1, 88, 24);
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
            gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
            gm_b0_1 t18 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t5, t6, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    F.ret = ((uint8_t)1);
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_bits_cast_0kaubje(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_bits_cast_0kaubje F;
    void * gm_l0; /* cx */
    void * gm_l1; /* v */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    gm_t88rcucg1b7uk gm_l5; /* src */
    uint64_t gm_l6; /* a */
    uint64_t gm_l7; /* b */
    gm_tn3r4rvlh0rsv gm_l8; /* <temporary array> */
    GM_STACK_CHECK(&F, 751u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tabb710d9mq2e gm_l9;
    gm_tbccvikg6b96e gm_l10;
    gm_tbccvikg6b96e gm_l11;
    gm_tabb710d9mq2e gm_l12;
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, ((uint8_t *)t4));
    void * t6 = (void *)(((uint8_t *)t5) + 8);
    gm_l4 = t6;
    void * t7 = gm_l0;
    void * t8 = (void *)(((uint8_t *)t7) + 96);
    void * t9 = gm_l0;
    void * t10 = GM_LD(void *, ((uint8_t *)t9));
    void * t11 = gm_l1;
    gm_t88rcucg1b7uk t12 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t11));
    gm_t88rcucg1b7uk t13 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t8, t10, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t13;
    void * t14 = gm_l0;
    gm_t88rcucg1b7uk t15 = gm_l2;
    uint8_t t16 = gm_f_is_plain_data_ej4mh2m((void *)NULL, cx, NULL, t14, t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t17 = (uint8_t)!t16;
    if (t17) {
        void * t18 = gm_l0;
        gm_tl874t2atnlh3 t19 = gm_l3;
        void * t20 = gm_l0;
        gm_t88rcucg1b7uk t21 = gm_l2;
        gm_b16_8 t22 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t20, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t23;
        GM_ST(gm_b16_8, ((uint8_t *)&t23 + 0), t22);
        gm_f_format_ohgvj26((void *)&gm_l9, (void *)NULL, cx, NULL, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
        if (!t24) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srcngug8co99ok1, 452, 24);
        gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
        gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
        gm_b0_1 t27 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t18, t19, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    } else {
        {
            {
                void * t28 = gm_l4;
                gm_t88rcucg1b7uk t29 = gm_l5;
                uint8_t t30 = gm_f_has_infer_3o82a0b(t28, GM_CONV(gm_csguankko4lmhd, t29));
                uint8_t t31 = (uint8_t)!t30;
                if (!t31) {
                    goto gm_lb2;
                }
                void * t32 = gm_l4;
                gm_t88rcucg1b7uk t33 = gm_l5;
                uint8_t t34 = gm_f_has_param_u9t0efs(t32, GM_CONV(gm_csguankko4lmhd, t33));
                uint8_t t35 = (uint8_t)!t34;
                if (!t35) {
                    goto gm_lb2;
                }
                void * t36 = gm_l4;
                gm_t88rcucg1b7uk t37 = gm_l2;
                uint8_t t38 = gm_f_has_param_u9t0efs(t36, GM_CONV(gm_csguankko4lmhd, t37));
                uint8_t t39 = (uint8_t)!t38;
                if (!t39) {
                    goto gm_lb2;
                }
                {
                    void * t40 = gm_l0;
                    void * t41 = GM_LD(void *, ((uint8_t *)t40));
                    gm_t88rcucg1b7uk t42 = gm_l5;
                    gm_f_layout_a5ovnrm((void *)&gm_l10, (void *)NULL, cx, NULL, t41, t42);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint64_t t43 = GM_LD(uint64_t, ((uint8_t *)&gm_l10));
                gm_l6 = t43;
                {
                    void * t44 = gm_l0;
                    void * t45 = GM_LD(void *, ((uint8_t *)t44));
                    gm_t88rcucg1b7uk t46 = gm_l2;
                    gm_f_layout_a5ovnrm((void *)&gm_l11, (void *)NULL, cx, NULL, t45, t46);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint64_t t47 = GM_LD(uint64_t, ((uint8_t *)&gm_l11));
                gm_l7 = t47;
                uint64_t t48 = gm_l6;
                uint64_t t49 = gm_l7;
                uint8_t t50 = (!(t48 == t49));
                if (t50) {
                    void * t51 = gm_l0;
                    gm_tl874t2atnlh3 t52 = gm_l3;
                    void * t53 = gm_l0;
                    gm_t88rcucg1b7uk t54 = gm_l5;
                    gm_b16_8 t55 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t53, t54);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t56 = gm_l6;
                    void * t57 = gm_l0;
                    gm_t88rcucg1b7uk t58 = gm_l2;
                    gm_b16_8 t59 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t57, t58);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t60 = gm_l7;
                    gm_thddbqb6snabm t61;
                    GM_ST(gm_b16_8, ((uint8_t *)&t61 + 0), t55);
                    GM_ST(uint64_t, ((uint8_t *)&t61 + 16), t56);
                    GM_ST(gm_b16_8, ((uint8_t *)&t61 + 24), t59);
                    GM_ST(uint64_t, ((uint8_t *)&t61 + 40), t60);
                    gm_f_format_hpnf26s((void *)&gm_l12, (void *)NULL, cx, NULL, &t61);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t62 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                    if (!t62) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_srcngug8co99ok1, 457, 28);
                    gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
                    gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&t63));
                    gm_b0_1 t65 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t51, t52, t64);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto gm_lb1;
            }
gm_lb2:;
        }
gm_lb1:;
    }
    gm_t88rcucg1b7uk t66 = gm_l2;
    void * t67 = gm_l1;
    gm_tn3r4rvlh0rsv t68;
    GM_ST(void *, ((uint8_t *)&t68 + 0), t67);
    gm_t8588ma2cr9d7 t69;
    GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&t69 + 8, 0);
    GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)7ULL));
    gm_l8 = t68;
    void * t70 = (void *)((uint8_t *)&gm_l8);
    uint64_t t71 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t72;
    GM_ST(void *, (uint8_t *)&t72, (void *)(((uint8_t *)t70 + (uintptr_t)((uint64_t)0ULL) * 8)));
    GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(t71));
    gm_tl874t2atnlh3 t73 = gm_l3;
    void * t74 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t69, t66, t72, t73);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t74;
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_builtin_a7cjd7d(void *env, gm_node *cx, gm_node *inst, gm_t8588ma2cr9d7 a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_builtin_a7cjd7d F;
    gm_t8588ma2cr9d7 gm_l0; /* op */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* args */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    void * gm_l7; /* a */
    GM_STACK_CHECK(&F, 382u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tftb1odc739t5 gm_l8;
    {
        gm_trqfvmjtt4sij t7;
        gm_f_list_vp1djv1_2((void *)&t7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l4 = t7;
    }
    gm_b16_8 t8 = gm_l2;
    gm_l5 = t8;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t9 = gm_l6;
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                void * t12 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t14 = gm_l6;
                {
                    {
                        void * t15 = GM_LD(void *, (((uint8_t *)t12) + (uintptr_t)t14 * 8));
                        gm_l7 = t15;
                        void * t16 = (void *)((uint8_t *)&F.l4);
                        void * t17 = gm_l7;
                        gm_tftb1odc739t5 t18 = gm_f_push_d943vgp_2(t16, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t17)));
                        gm_l8 = t18;
                        uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                        if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srcngug8co99ok1, 96, 21);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 96, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t20 = gm_l6;
            uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
            uint64_t t23 = r21;
            gm_l6 = t23;
        }
    }
gm_lb1:;
    {
        gm_t88rcucg1b7uk t24 = gm_l1;
        gm_t8588ma2cr9d7 t25 = gm_l0;
        gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
        gm_t9q8lhljes74c t27;
        GM_ST(uint32_t, (uint8_t *)&t27 + 12, 0);
        GM_ST(gm_t8588ma2cr9d7, ((uint8_t *)&t27 + 0), t25);
        GM_ST(gm_b16_8, ((uint8_t *)&t27 + 16), t26);
        gm_tl874t2atnlh3 t28 = gm_l3;
        gm_to0f5qn75cgg8 t29;
        GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t29 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t29 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t29 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)33ULL));
        GM_ST(gm_t9q8lhljes74c, ((uint8_t *)&t29 + 8), t27);
        void * t30 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t24, &t29, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t30;
        goto gm_fret;
    }
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_builtin_method_jist12a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, const void * a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_builtin_method_jist12a F;
    void * gm_l0; /* cx */
    void * gm_l1; /* recv */
    gm_t88rcucg1b7uk gm_l2; /* rty */
    gm_t08olq97rscn3 gm_l3; /* name */
    gm_b16_8 gm_l4; /* args */
    gm_tl874t2atnlh3 gm_l5; /* span */
    void * gm_l6; /* tys */
    gm_b16_8 gm_l7; /* n */
    uint8_t gm_l8; /* is_int */
    uint8_t gm_l9; /* is_float */
    gm_tn3r4rvlh0rsv gm_l11; /* <temporary array> */
    uint8_t gm_l12; /* is_const */
    gm_t88rcucg1b7uk gm_l13; /* pointee */
    gm_t88rcucg1b7uk gm_l14; /* outer */
    gm_t88rcucg1b7uk gm_l15; /* target */
    gm_t88rcucg1b7uk gm_l16; /* e */
    gm_tn3r4rvlh0rsv gm_l17; /* <temporary array> */
    gm_t88rcucg1b7uk gm_l18; /* t */
    gm_tep2drsl9408b gm_l19; /* uv */
    gm_tn3r4rvlh0rsv gm_l20; /* <temporary array> */
    GM_STACK_CHECK(&F, 2155u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l4 = a4;
    gm_l5 = a5;
    uint8_t gm_l21;
    uint8_t gm_l22;
    uint8_t gm_l23;
    uint8_t gm_l24;
    uint8_t gm_l25;
    gm_t53at80pfdus3 gm_l26;
    uint8_t gm_l27;
    uint8_t gm_l28;
    uint8_t gm_l29;
    gm_tkn42u7na3t81 gm_l30;
    gm_tabb710d9mq2e gm_l31;
    uint8_t gm_l32;
    uint8_t gm_l33;
    uint8_t gm_l34;
    gm_tkn42u7na3t81 gm_l35;
    gm_tkn42u7na3t81 gm_l36;
    uint8_t gm_l37;
    gm_tdlpv0q69lov1 gm_l38;
    {
        void * t38 = gm_l0;
        void * t39 = GM_LD(void *, ((uint8_t *)t38));
        void * t40 = (void *)(((uint8_t *)t39) + 8);
        gm_l6 = t40;
        gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)a3));
        gm_l7 = t41;
        void * t42 = gm_l6;
        gm_t88rcucg1b7uk t43 = gm_l2;
        uint8_t t44 = gm_f_is_integer_8d4f9ek(t42, GM_CONV(gm_csguankko4lmhd, t43));
        if (t44) {
            gm_l22 = ((uint8_t)1);
        } else {
            void * t45 = gm_l0;
            void * t46 = (void *)(((uint8_t *)t45) + 96);
            void * t47 = gm_l6;
            gm_t88rcucg1b7uk t48 = gm_l2;
            gm_taari1s0m51a7 t49 = gm_f_var_kind_1sl5sig((void *)NULL, cx, NULL, t46, t47, t48);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9qkh0rsooc8d t50;
            GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)0ULL));
            gm_taari1s0m51a7 t51;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t51), t50);
            ((uint8_t *)&t51)[1] = 1;
            uint8_t t52 = (gm_eq5307j5k9ms(((uint8_t *)&t49), ((uint8_t *)&t51)));
            gm_l22 = t52;
        }
        uint8_t t53 = gm_l22;
        if (t53) {
            gm_l21 = ((uint8_t)1);
        } else {
            void * t54 = gm_l0;
            gm_t88rcucg1b7uk t55 = gm_l2;
            gm_t9qkh0rsooc8d t56;
            GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)3ULL));
            uint8_t t57 = gm_f_param_with_qfvlipt(t54, GM_CONV(gm_csguankko4lmhd, t55), t56);
            gm_l21 = t57;
        }
        uint8_t t58 = gm_l21;
        gm_l8 = t58;
        void * t59 = gm_l6;
        gm_t88rcucg1b7uk t60 = gm_l2;
        gm_f_kind_jcnt4hm((void *)&gm_l26, t59, GM_CONV(gm_csguankko4lmhd, t60));
        {
            {
                uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                uint8_t t62 = (uint8_t)(t61 == ((uint8_t)3ULL));
                if (!t62) {
                    goto gm_lb2;
                }
                gm_l25 = ((uint8_t)1);
                goto gm_lb1;
            }
gm_lb2:;
            {
                gm_l25 = ((uint8_t)0);
                goto gm_lb1;
            }
gm_lb3:;
            gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 106, 17);
        }
gm_lb1:;
        uint8_t t63 = gm_l25;
        if (t63) {
            gm_l24 = ((uint8_t)1);
        } else {
            void * t64 = gm_l0;
            void * t65 = (void *)(((uint8_t *)t64) + 96);
            void * t66 = gm_l6;
            gm_t88rcucg1b7uk t67 = gm_l2;
            gm_taari1s0m51a7 t68 = gm_f_var_kind_1sl5sig((void *)NULL, cx, NULL, t65, t66, t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9qkh0rsooc8d t69;
            GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)1ULL));
            gm_taari1s0m51a7 t70;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t70), t69);
            ((uint8_t *)&t70)[1] = 1;
            uint8_t t71 = (gm_eq5307j5k9ms(((uint8_t *)&t68), ((uint8_t *)&t70)));
            gm_l24 = t71;
        }
        uint8_t t72 = gm_l24;
        if (t72) {
            gm_l23 = ((uint8_t)1);
        } else {
            void * t73 = gm_l0;
            gm_t88rcucg1b7uk t74 = gm_l2;
            gm_t9qkh0rsooc8d t75;
            GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)5ULL));
            uint8_t t76 = gm_f_param_with_qfvlipt(t73, GM_CONV(gm_csguankko4lmhd, t74), t75);
            gm_l23 = t76;
        }
    }
    {
        uint8_t t77 = gm_l23;
        gm_l9 = t77;
        uint8_t t78 = gm_l8;
        if (t78) {
            gm_l27 = ((uint8_t)1);
        } else {
            uint8_t t79 = gm_l9;
            gm_l27 = t79;
        }
        uint8_t t80 = gm_l27;
        if (t80) {
            void * t81 = gm_l0;
            void * t82 = gm_l1;
            gm_t88rcucg1b7uk t83 = gm_l2;
            gm_b16_8 t84 = gm_l7;
            uint8_t t85 = gm_l8;
            uint8_t t86 = gm_l9;
            gm_b16_8 t87 = gm_l4;
            gm_tl874t2atnlh3 t88 = gm_l5;
            void * t89 = gm_f_numeric_method_jo5uq0l((void *)NULL, cx, NULL, t81, t82, t83, t84, t85, t86, t87, t88);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t89;
            goto gm_fret;
        }
        void * t90 = gm_l6;
        gm_t88rcucg1b7uk t91 = gm_l2;
        gm_f_kind_jcnt4hm((void *)&F.l10, t90, GM_CONV(gm_csguankko4lmhd, t91));
        {
            {
                gm_b16_8 t92 = gm_l7;
                gm_b16_8 t93;
                GM_ST(void *, (uint8_t *)&t93, (void *)(gm_s3e818t2qbjbh));
                GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(4));
                uint8_t t94 = (gm_eq0iqiubfm7e(((uint8_t *)&t92), ((uint8_t *)&t93)));
                if (!t94) {
                    goto gm_lb5;
                }
                {
                    {
                        uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                        uint8_t t96 = (uint8_t)(t95 == ((uint8_t)8ULL));
                        if (!t96) {
                            goto gm_lb7;
                        }
                        gm_l28 = ((uint8_t)1);
                        goto gm_lb6;
                    }
gm_lb7:;
                    {
                        uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                        uint8_t t98 = (uint8_t)(t97 == ((uint8_t)9ULL));
                        if (!t98) {
                            goto gm_lb8;
                        }
                        gm_l28 = ((uint8_t)1);
                        goto gm_lb6;
                    }
gm_lb8:;
                    {
                        uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                        uint8_t t100 = (uint8_t)(t99 == ((uint8_t)7ULL));
                        if (!t100) {
                            goto gm_lb9;
                        }
                        gm_l28 = ((uint8_t)1);
                        goto gm_lb6;
                    }
gm_lb9:;
                    {
                        uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                        uint8_t t102 = (uint8_t)(t101 == ((uint8_t)16ULL));
                        if (!t102) {
                            goto gm_lb10;
                        }
                        gm_l28 = ((uint8_t)1);
                        goto gm_lb6;
                    }
gm_lb10:;
                    {
                        gm_l28 = ((uint8_t)0);
                        goto gm_lb6;
                    }
gm_lb11:;
                    gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 109, 24);
                }
gm_lb6:;
                uint8_t t103 = gm_l28;
                if (!t103) {
                    goto gm_lb5;
                }
                void * t104 = gm_l0;
                gm_b16_8 t105 = gm_l4;
                gm_b16_8 t106 = gm_l7;
                gm_tl874t2atnlh3 t107 = gm_l5;
                uint8_t t108 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t104, t105, ((uint64_t)0ULL), t106, t107);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t109 = gm_l6;
                gm_t8588ma2cr9d7 t110;
                GM_ST(uint8_t, (uint8_t *)&t110 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t110 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t110 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t110 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t110 + 0), ((uint8_t)9ULL));
                gm_t88rcucg1b7uk t111 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t109);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t112 = gm_l1;
                gm_tn3r4rvlh0rsv t113;
                GM_ST(void *, ((uint8_t *)&t113 + 0), t112);
                gm_l11 = t113;
                void * t114 = (void *)((uint8_t *)&gm_l11);
                uint64_t t115 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t116;
                GM_ST(void *, (uint8_t *)&t116, (void *)(((uint8_t *)t114 + (uintptr_t)((uint64_t)0ULL) * 8)));
                GM_ST(uintptr_t, (uint8_t *)&t116 + 8, (uintptr_t)(t115));
                gm_tl874t2atnlh3 t117 = gm_l5;
                void * t118 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t110, t111, t116, t117);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t118;
                goto gm_fret;
            }
gm_lb5:;
        }
gm_lb4:;
    }
    {
        {
            {
                gm_b16_8 t119 = gm_l7;
                gm_b16_8 t120;
                GM_ST(void *, (uint8_t *)&t120, (void *)(gm_snlqf7m58jjdu));
                GM_ST(uintptr_t, (uint8_t *)&t120 + 8, (uintptr_t)(4));
                uint8_t t121 = (gm_eq0iqiubfm7e(((uint8_t *)&t119), ((uint8_t *)&t120)));
                if (!t121) {
                    goto gm_lb13;
                }
                {
                    {
                        uint8_t t122 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                        uint8_t t123 = (uint8_t)(t122 == ((uint8_t)8ULL));
                        if (!t123) {
                            goto gm_lb15;
                        }
                        gm_l29 = ((uint8_t)1);
                        goto gm_lb14;
                    }
gm_lb15:;
                    {
                        uint8_t t124 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                        uint8_t t125 = (uint8_t)(t124 == ((uint8_t)9ULL));
                        if (!t125) {
                            goto gm_lb16;
                        }
                        gm_l29 = ((uint8_t)1);
                        goto gm_lb14;
                    }
gm_lb16:;
                    {
                        uint8_t t126 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                        uint8_t t127 = (uint8_t)(t126 == ((uint8_t)7ULL));
                        if (!t127) {
                            goto gm_lb17;
                        }
                        gm_l29 = ((uint8_t)1);
                        goto gm_lb14;
                    }
gm_lb17:;
                    {
                        uint8_t t128 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                        uint8_t t129 = (uint8_t)(t128 == ((uint8_t)16ULL));
                        if (!t129) {
                            goto gm_lb18;
                        }
                        gm_l29 = ((uint8_t)1);
                        goto gm_lb14;
                    }
gm_lb18:;
                    {
                        uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                        uint8_t t131 = (uint8_t)(t130 == ((uint8_t)13ULL));
                        if (!t131) {
                            goto gm_lb19;
                        }
                        gm_l29 = ((uint8_t)1);
                        goto gm_lb14;
                    }
gm_lb19:;
                    {
                        gm_l29 = ((uint8_t)0);
                        goto gm_lb14;
                    }
gm_lb20:;
                    gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 113, 24);
                }
gm_lb14:;
                uint8_t t132 = gm_l29;
                if (!t132) {
                    goto gm_lb13;
                }
                void * t133 = gm_l0;
                void * t134 = gm_l1;
                gm_t88rcucg1b7uk t135 = gm_l2;
                gm_b16_8 t136 = gm_l4;
                gm_tl874t2atnlh3 t137 = gm_l5;
                void * t138 = gm_f_pointer_cast_6siqq5h((void *)NULL, cx, NULL, t133, t134, t135, t136, t137);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t138;
                goto gm_fret;
            }
gm_lb13:;
        }
gm_lb12:;
        {
            {
                gm_b16_8 t139 = gm_l7;
                gm_b16_8 t140;
                GM_ST(void *, (uint8_t *)&t140, (void *)(gm_s5iv4icrhn1vp));
                GM_ST(uintptr_t, (uint8_t *)&t140 + 8, (uintptr_t)(18));
                uint8_t t141 = (gm_eq0iqiubfm7e(((uint8_t *)&t139), ((uint8_t *)&t140)));
                if (!t141) {
                    goto gm_lb22;
                }
                uint8_t t142 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                uint8_t t143 = (uint8_t)(t142 == ((uint8_t)8ULL));
                if (!t143) {
                    goto gm_lb22;
                }
                uint8_t t144 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 8));
                gm_l12 = t144;
                gm_t88rcucg1b7uk t145 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l10) + 8) + 4));
                gm_l13 = t145;
                void * t146 = gm_l0;
                gm_b16_8 t147 = gm_l4;
                gm_tl874t2atnlh3 t148 = gm_l5;
                gm_tkn42u7na3t81 t149 = gm_f_type_arg_grgffgp((void *)NULL, cx, NULL, t146, t147, ((uint64_t)0ULL), t148);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l30 = t149;
                uint8_t t150 = (uint8_t)((((uint8_t *)&gm_l30))[4] != 0);
                if (t150) {
                    gm_t88rcucg1b7uk t151 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l30));
                    gm_l14 = t151;
                } else {
                    F.ret = ((void *)0);
                    goto gm_fret;
                }
                void * t152 = gm_l6;
                uint8_t t153 = gm_l12;
                gm_t88rcucg1b7uk t154 = gm_l14;
                gm_t88rcucg1b7uk t155 = gm_f_ptr_t1h2m2d((void *)NULL, cx, NULL, t152, t153, t154);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l15 = t155;
                void * t156 = gm_l0;
                gm_t88rcucg1b7uk t157 = gm_l15;
                void * t158 = gm_l6;
                uint8_t t159 = gm_l12;
                gm_t88rcucg1b7uk t160 = gm_l13;
                gm_t88rcucg1b7uk t161 = gm_f_ptr_t1h2m2d((void *)NULL, cx, NULL, t158, t159, t160);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t162 = gm_f_embeds_2jpqm45(t156, GM_CONV(gm_csguankko4lmhd, t157), GM_CONV(gm_csguankko4lmhd, t161));
                uint8_t t163 = (uint8_t)!t162;
                if (t163) {
                    {
                        void * t164 = gm_l0;
                        gm_tl874t2atnlh3 t165 = gm_l5;
                        void * t166 = gm_l0;
                        gm_t88rcucg1b7uk t167 = gm_l14;
                        gm_b16_8 t168 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t166, t167);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t169 = gm_l0;
                        gm_t88rcucg1b7uk t170 = gm_l13;
                        gm_b16_8 t171 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t169, t170);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t6j6smj7qoctu t172;
                        GM_ST(gm_b16_8, ((uint8_t *)&t172 + 0), t168);
                        GM_ST(gm_b16_8, ((uint8_t *)&t172 + 16), t171);
                        gm_f_format_fu8jpss((void *)&gm_l31, (void *)NULL, cx, NULL, &t172);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t173 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                        if (!t173) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 8, gm_srcngug8co99ok1, 119, 28);
                        gm_b16_8 t174 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l31) + 8));
                        gm_b16_8 t175 = GM_LD(gm_b16_8, ((uint8_t *)&t174));
                        gm_b0_1 t176 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t164, t165, t175);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    void * t177 = gm_l0;
                    void * t178 = GM_LD(void *, ((uint8_t *)t177));
                    gm_tl874t2atnlh3 t179 = gm_l5;
                    void * t180 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t178, t179);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t180;
                    goto gm_fret;
                }
                gm_t88rcucg1b7uk t181 = gm_l15;
                void * t182 = gm_l1;
                gm_tmn7n0n9raou5 t183;
                GM_ST(uint8_t, (uint8_t *)&t183 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t183 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t183 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t183 + 0), ((uint8_t)6ULL));
                gm_t8jsc4n9vp2ib t184;
                GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t184 + 0), t183);
                GM_ST(void *, ((uint8_t *)&t184 + 8), t182);
                gm_tl874t2atnlh3 t185 = gm_l5;
                gm_to0f5qn75cgg8 t186;
                memset(&t186, 0, sizeof t186);
                GM_ST(uint8_t, ((uint8_t *)&t186 + 0), ((uint8_t)32ULL));
                GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t186 + 8), t184);
                void * t187 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t181, &t186, t185);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t187;
                goto gm_fret;
            }
gm_lb22:;
        }
gm_lb21:;
    }
    {
        {
            {
                gm_b16_8 t188 = gm_l7;
                gm_b16_8 t189;
                GM_ST(void *, (uint8_t *)&t189, (void *)(gm_ssc48vnvhmmnh));
                GM_ST(uintptr_t, (uint8_t *)&t189 + 8, (uintptr_t)(8));
                uint8_t t190 = (gm_eq0iqiubfm7e(((uint8_t *)&t188), ((uint8_t *)&t189)));
                if (!t190) {
                    goto gm_lb24;
                }
                uint8_t t191 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                uint8_t t192 = (uint8_t)(t191 == ((uint8_t)9ULL));
                if (!t192) {
                    goto gm_lb24;
                }
                gm_t88rcucg1b7uk t193 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l10) + 8) + 4));
                gm_l16 = t193;
                gm_t88rcucg1b7uk t194 = gm_l16;
                void * t195 = gm_l6;
                gm_t88rcucg1b7uk t196 = gm_f_u8_ty_afkvj25((void *)NULL, cx, NULL, t195);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t197 = (gm_eq9m6mvtmgde(((uint8_t *)&t194), ((uint8_t *)&t196)));
                if (!t197) {
                    goto gm_lb24;
                }
                void * t198 = gm_l0;
                gm_b16_8 t199 = gm_l4;
                gm_b16_8 t200 = gm_l7;
                gm_tl874t2atnlh3 t201 = gm_l5;
                uint8_t t202 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t198, t199, ((uint64_t)0ULL), t200, t201);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t203 = gm_l6;
                gm_t8588ma2cr9d7 t204;
                GM_ST(uint8_t, (uint8_t *)&t204 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t204 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t204 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t204 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t204 + 0), ((uint8_t)61ULL));
                gm_t88rcucg1b7uk t205 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t203);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t206 = gm_l1;
                gm_tn3r4rvlh0rsv t207;
                GM_ST(void *, ((uint8_t *)&t207 + 0), t206);
                gm_l17 = t207;
                void * t208 = (void *)((uint8_t *)&gm_l17);
                uint64_t t209 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t210;
                GM_ST(void *, (uint8_t *)&t210, (void *)(((uint8_t *)t208 + (uintptr_t)((uint64_t)0ULL) * 8)));
                GM_ST(uintptr_t, (uint8_t *)&t210 + 8, (uintptr_t)(t209));
                gm_tl874t2atnlh3 t211 = gm_l5;
                void * t212 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t204, t205, t210, t211);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t212;
                goto gm_fret;
            }
gm_lb24:;
        }
gm_lb23:;
        {
            {
                gm_b16_8 t213 = gm_l7;
                gm_b16_8 t214;
                GM_ST(void *, (uint8_t *)&t214, (void *)(gm_s6vvtrmtnt3j1));
                GM_ST(uintptr_t, (uint8_t *)&t214 + 8, (uintptr_t)(4));
                uint8_t t215 = (gm_eq0iqiubfm7e(((uint8_t *)&t213), ((uint8_t *)&t214)));
                if (!t215) {
                    goto gm_lb26;
                }
                void * t216 = gm_l0;
                gm_t88rcucg1b7uk t217 = gm_l2;
                uint8_t t218 = gm_f_is_plain_data_ej4mh2m((void *)NULL, cx, NULL, t216, t217);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (t218) {
                    gm_l33 = ((uint8_t)1);
                } else {
                    uint8_t t219 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                    uint8_t t220 = (uint8_t)((t219 == ((uint8_t)4ULL)));
                    gm_t53at80pfdus3 t221 = GM_LD(gm_t53at80pfdus3, ((uint8_t *)&F.l10));
                    gm_t53at80pfdus3 t222;
                    memset(&t222, 0, sizeof t222);
                    GM_ST(uint8_t, ((uint8_t *)&t222 + 0), ((uint8_t)4ULL));
                    gm_l33 = t220;
                }
                uint8_t t223 = gm_l33;
                if (t223) {
                    gm_l32 = ((uint8_t)1);
                } else {
                    void * t224 = gm_l0;
                    void * t225 = GM_LD(void *, ((uint8_t *)t224));
                    gm_t88rcucg1b7uk t226 = gm_l2;
                    gm_tkn42u7na3t81 t227 = gm_f_bits_struct_ty_l1kjtap(t225, GM_CONV(gm_csguankko4lmhd, t226));
                    gm_l35 = t227;
                    {
                        {
                            uint8_t t228 = (uint8_t)((((uint8_t *)&gm_l35))[4] != 0);
                            if (t228) {
                                goto gm_lb28;
                            }
                            gm_l34 = ((uint8_t)0);
                            goto gm_lb27;
                        }
gm_lb28:;
                        {
                            gm_l34 = ((uint8_t)1);
                            goto gm_lb27;
                        }
gm_lb29:;
                        gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 128, 64);
                    }
gm_lb27:;
                    uint8_t t229 = gm_l34;
                    gm_l32 = t229;
                }
                uint8_t t230 = gm_l32;
                if (!t230) {
                    goto gm_lb26;
                }
                void * t231 = gm_l0;
                gm_b16_8 t232 = gm_l4;
                gm_b16_8 t233 = gm_l7;
                gm_tl874t2atnlh3 t234 = gm_l5;
                uint8_t t235 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t231, t232, ((uint64_t)1ULL), t233, t234);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t236 = (uint8_t)!t235;
                if (t236) {
                    void * t237 = gm_l0;
                    void * t238 = GM_LD(void *, ((uint8_t *)t237));
                    gm_tl874t2atnlh3 t239 = gm_l5;
                    void * t240 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t238, t239);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t240;
                    goto gm_fret;
                }
                void * t241 = gm_l0;
                gm_b16_8 t242 = gm_l4;
                gm_tl874t2atnlh3 t243 = gm_l5;
                gm_tkn42u7na3t81 t244 = gm_f_type_arg_grgffgp((void *)NULL, cx, NULL, t241, t242, ((uint64_t)0ULL), t243);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l36 = t244;
                uint8_t t245 = (uint8_t)((((uint8_t *)&gm_l36))[4] != 0);
                if (t245) {
                    gm_t88rcucg1b7uk t246 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l36));
                    gm_l18 = t246;
                } else {
                    F.ret = ((void *)0);
                    goto gm_fret;
                }
                void * t247 = gm_l0;
                void * t248 = gm_l1;
                gm_t88rcucg1b7uk t249 = gm_l18;
                gm_tl874t2atnlh3 t250 = gm_l5;
                void * t251 = gm_f_bits_cast_0kaubje((void *)NULL, cx, NULL, t247, t248, t249, t250);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t251;
                goto gm_fret;
            }
gm_lb26:;
        }
gm_lb25:;
    }
    {
        {
            {
                gm_b16_8 t252 = gm_l7;
                gm_b16_8 t253;
                GM_ST(void *, (uint8_t *)&t253, (void *)(gm_s420tm9ua7ggg));
                GM_ST(uintptr_t, (uint8_t *)&t253 + 8, (uintptr_t)(3));
                uint8_t t254 = (gm_eq0iqiubfm7e(((uint8_t *)&t252), ((uint8_t *)&t253)));
                if (!t254) {
                    goto gm_lb31;
                }
                {
                    {
                        uint8_t t255 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                        uint8_t t256 = (uint8_t)(t255 == ((uint8_t)17ULL));
                        if (!t256) {
                            goto gm_lb33;
                        }
                        gm_l37 = ((uint8_t)1);
                        goto gm_lb32;
                    }
gm_lb33:;
                    {
                        gm_l37 = ((uint8_t)0);
                        goto gm_lb32;
                    }
gm_lb34:;
                    gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 133, 23);
                }
gm_lb32:;
                uint8_t t257 = gm_l37;
                if (!t257) {
                    goto gm_lb31;
                }
                {
                    {
                        {
                            void * t258 = gm_l0;
                            void * t259 = GM_LD(void *, ((uint8_t *)t258));
                            gm_t88rcucg1b7uk t260 = gm_l2;
                            gm_f_union_variants_a7khaqp((void *)&gm_l38, t259, GM_CONV(gm_csguankko4lmhd, t260));
                        }
                        uint8_t t261 = (uint8_t)((((uint8_t *)&gm_l38))[24] != 0);
                        if (!t261) {
                            goto gm_lb36;
                        }
                        {
                            memmove(((uint8_t *)&gm_l19), ((uint8_t *)&gm_l38), 24);
                        }
                        void * t262 = gm_l0;
                        gm_b16_8 t263 = gm_l4;
                        gm_b16_8 t264 = gm_l7;
                        gm_tl874t2atnlh3 t265 = gm_l5;
                        uint8_t t266 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t262, t263, ((uint64_t)0ULL), t264, t265);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t88rcucg1b7uk t267 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l19) + 16));
                        void * t268 = gm_l1;
                        gm_tn3r4rvlh0rsv t269;
                        GM_ST(void *, ((uint8_t *)&t269 + 0), t268);
                        gm_t8588ma2cr9d7 t270;
                        GM_ST(uint8_t, (uint8_t *)&t270 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t270 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t270 + 4, 0);
                        GM_ST(uint32_t, (uint8_t *)&t270 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t270 + 0), ((uint8_t)14ULL));
                        gm_l20 = t269;
                        void * t271 = (void *)((uint8_t *)&gm_l20);
                        uint64_t t272 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                        gm_b16_8 t273;
                        GM_ST(void *, (uint8_t *)&t273, (void *)(((uint8_t *)t271 + (uintptr_t)((uint64_t)0ULL) * 8)));
                        GM_ST(uintptr_t, (uint8_t *)&t273 + 8, (uintptr_t)(t272));
                        gm_tl874t2atnlh3 t274 = gm_l5;
                        void * t275 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t270, t267, t273, t274);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t275;
                        goto gm_fret;
                    }
gm_lb36:;
                }
gm_lb35:;
                goto gm_lb30;
            }
gm_lb31:;
        }
gm_lb30:;
        void * t276 = gm_l0;
        void * t277 = gm_l1;
        gm_t88rcucg1b7uk t278 = gm_l2;
        gm_b16_8 t279 = gm_l7;
        gm_b16_8 t280 = gm_l4;
        gm_tl874t2atnlh3 t281 = gm_l5;
        void * t282 = gm_f_collection_method_1a7d15p((void *)NULL, cx, NULL, t276, t277, t278, t279, t280, t281);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t282;
        goto gm_fret;
    }
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_check_from_tag_g3el2co(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_check_from_tag_g3el2co F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* u */
    gm_b16_8 gm_l2; /* args */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_tep2drsl9408b gm_l4; /* uv */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    void * gm_l8; /* x */
    gm_tn3r4rvlh0rsv gm_l9; /* <temporary array> */
    GM_STACK_CHECK(&F, 777u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tdlpv0q69lov1 gm_l10;
    gm_tabb710d9mq2e gm_l11;
    uint8_t gm_l12;
    gm_tabb710d9mq2e gm_l13;
    void * t10 = gm_l0;
    void * t11 = GM_LD(void *, ((uint8_t *)t10));
    gm_t88rcucg1b7uk t12 = gm_l1;
    gm_f_union_variants_a7khaqp((void *)&gm_l10, t11, GM_CONV(gm_csguankko4lmhd, t12));
    uint8_t t13 = (uint8_t)((((uint8_t *)&gm_l10))[24] != 0);
    if (t13) {
        memmove(((uint8_t *)&gm_l4), ((uint8_t *)&gm_l10), 24);
    } else {
        void * t14 = gm_l0;
        gm_tl874t2atnlh3 t15 = gm_l3;
        void * t16 = gm_l0;
        gm_t88rcucg1b7uk t17 = gm_l1;
        gm_b16_8 t18 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t19;
        GM_ST(gm_b16_8, ((uint8_t *)&t19 + 0), t18);
        gm_f_format_qe94i5r((void *)&gm_l11, (void *)NULL, cx, NULL, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
        if (!t20) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_srcngug8co99ok1, 466, 24);
        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
        gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
        gm_b0_1 t23 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t14, t15, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t24 = gm_l0;
        void * t25 = GM_LD(void *, ((uint8_t *)t24));
        gm_tl874t2atnlh3 t26 = gm_l3;
        void * t27 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t25, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t27;
        goto gm_fret;
    }
    gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
    gm_l5 = t28;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t29 = gm_l6;
            uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t31 = (t29 < t30);
            if (!t31) {
                goto gm_lb1;
            }
            {
                void * t32 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t34 = gm_l6;
                {
                    {
                        memmove(((uint8_t *)&F.l7), (((uint8_t *)t32) + (uintptr_t)t34 * 72), 72);
                        {
                            {
                                uint8_t t35 = (uint8_t)(((((uint8_t *)&F.l7) + 16))[4] != 0);
                                if (t35) {
                                    goto gm_lb7;
                                }
                                gm_l12 = ((uint8_t)0);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                gm_l12 = ((uint8_t)1);
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 470, 12);
                        }
gm_lb6:;
                        uint8_t t36 = gm_l12;
                        if (t36) {
                            void * t37 = gm_l0;
                            gm_tl874t2atnlh3 t38 = gm_l3;
                            void * t39 = gm_l0;
                            gm_t88rcucg1b7uk t40 = gm_l1;
                            gm_b16_8 t41 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t39, t40);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tnpifv20irnph t42;
                            GM_ST(gm_b16_8, ((uint8_t *)&t42 + 0), t41);
                            gm_f_format_up1o5ed((void *)&gm_l13, (void *)NULL, cx, NULL, t42);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t43 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                            if (!t43) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_srcngug8co99ok1, 471, 28);
                            gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
                            gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
                            gm_b0_1 t46 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t37, t38, t45);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb1;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 469, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t47 = gm_l6;
            uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
            uint64_t t50 = r48;
            gm_l6 = t50;
        }
    }
gm_lb1:;
    void * t51 = gm_l0;
    gm_b16_8 t52 = gm_l2;
    gm_b16_8 t53;
    GM_ST(void *, (uint8_t *)&t53, (void *)(gm_s66fk044n1n0k));
    GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(8));
    gm_tl874t2atnlh3 t54 = gm_l3;
    uint8_t t55 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t51, t52, ((uint64_t)1ULL), t53, t54);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t56 = (uint8_t)!t55;
    if (t56) {
        void * t57 = gm_l0;
        void * t58 = GM_LD(void *, ((uint8_t *)t57));
        gm_tl874t2atnlh3 t59 = gm_l3;
        void * t60 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t58, t59);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t60;
        goto gm_fret;
    }
    void * t61 = gm_l0;
    gm_b16_8 t62 = gm_l2;
    gm_t88rcucg1b7uk t63 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l4) + 16));
    gm_tl874t2atnlh3 t64 = gm_l3;
    void * t65 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t61, t62, ((uint64_t)0ULL), t63, t64);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l8 = t65;
    void * t66 = gm_l0;
    void * t67 = GM_LD(void *, ((uint8_t *)t66));
    void * t68 = (void *)(((uint8_t *)t67) + 8);
    gm_t88rcucg1b7uk t69 = gm_l1;
    gm_t8588ma2cr9d7 t70;
    GM_ST(uint8_t, (uint8_t *)&t70 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t70 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t70 + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&t70 + 8, 0);
    GM_ST(uint8_t, ((uint8_t *)&t70 + 0), ((uint8_t)15ULL));
    gm_t88rcucg1b7uk t71 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t68, t69);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t72 = gm_l8;
    gm_tn3r4rvlh0rsv t73;
    GM_ST(void *, ((uint8_t *)&t73 + 0), t72);
    gm_l9 = t73;
    void * t74 = (void *)((uint8_t *)&gm_l9);
    uint64_t t75 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t76;
    GM_ST(void *, (uint8_t *)&t76, (void *)(((uint8_t *)t74 + (uintptr_t)((uint64_t)0ULL) * 8)));
    GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(t75));
    gm_tl874t2atnlh3 t77 = gm_l3;
    void * t78 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t70, t71, t76, t77);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t78;
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_collection_method_1a7d15p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_collection_method_1a7d15p F;
    void * gm_l0; /* cx */
    void * gm_l1; /* recv */
    gm_t88rcucg1b7uk gm_l2; /* rty */
    gm_b16_8 gm_l3; /* n */
    gm_b16_8 gm_l4; /* args */
    gm_tl874t2atnlh3 gm_l5; /* span */
    void * gm_l6; /* tys */
    gm_t88rcucg1b7uk gm_l7; /* arr */
    gm_t88rcucg1b7uk gm_l8; /* p */
    void * gm_l9; /* v */
    gm_tnnjeerolos0a gm_l10; /* kind */
    void * gm_l11; /* base */
    gm_t88rcucg1b7uk gm_l12; /* elem */
    uint8_t gm_l13; /* is_const */
    uint8_t gm_l14; /* c */
    gm_t88rcucg1b7uk gm_l15; /* e */
    gm_t88rcucg1b7uk gm_l16; /* e */
    gm_t88rcucg1b7uk gm_l17; /* e */
    gm_tqs5p4n2tq19v gm_l18; /* m */
    uint8_t gm_l19; /* c */
    gm_t88rcucg1b7uk gm_l20; /* pointee */
    gm_t88rcucg1b7uk gm_l21; /* e */
    gm_t88rcucg1b7uk gm_l22; /* e */
    void * gm_l23; /* i */
    gm_t5fru0lp11n7t gm_l24; /* <temporary array> */
    gm_t5fru0lp11n7t gm_l25; /* <temporary array> */
    gm_t8588ma2cr9d7 gm_l26; /* op */
    gm_tn3r4rvlh0rsv gm_l27; /* <temporary array> */
    gm_b16_8 gm_l29; /* text */
    GM_STACK_CHECK(&F, 3533u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_t53at80pfdus3 gm_l30;
    uint8_t gm_l31;
    gm_t53at80pfdus3 gm_l32;
    uint8_t gm_l33;
    gm_tabb710d9mq2e gm_l34;
    gm_t53at80pfdus3 gm_l35;
    uint8_t gm_l36;
    gm_tabb710d9mq2e gm_l37;
    gm_t53at80pfdus3 gm_l38;
    uint8_t gm_l39;
    gm_tabb710d9mq2e gm_l40;
    gm_tabb710d9mq2e gm_l41;
    {
        void * t40 = gm_l0;
        void * t41 = GM_LD(void *, ((uint8_t *)t40));
        void * t42 = (void *)(((uint8_t *)t41) + 8);
        gm_l6 = t42;
        gm_t88rcucg1b7uk t43 = gm_l2;
        gm_l7 = t43;
        {
            {
                void * t44 = gm_l6;
                gm_t88rcucg1b7uk t45 = gm_l2;
                gm_f_kind_jcnt4hm((void *)&gm_l30, t44, GM_CONV(gm_csguankko4lmhd, t45));
                uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&gm_l30) + 0));
                uint8_t t47 = (uint8_t)(t46 == ((uint8_t)8ULL));
                if (!t47) {
                    goto gm_lb2;
                }
                gm_t88rcucg1b7uk t48 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l30) + 8) + 4));
                gm_l8 = t48;
                gm_t88rcucg1b7uk t49 = gm_l8;
                gm_l7 = t49;
                goto gm_lb1;
            }
gm_lb2:;
        }
gm_lb1:;
        void * t50 = gm_l6;
        gm_t88rcucg1b7uk t51 = gm_l7;
        uint8_t t52 = gm_f_is_vector_gkq6b88(t50, GM_CONV(gm_csguankko4lmhd, t51));
        if (t52) {
            gm_l31 = ((uint8_t)1);
        } else {
            void * t53 = gm_l6;
            gm_t88rcucg1b7uk t54 = gm_l7;
            uint8_t t55 = gm_f_is_math_vector_09mtuao(t53, GM_CONV(gm_csguankko4lmhd, t54));
            gm_l31 = t55;
        }
        uint8_t t56 = gm_l31;
        if (t56) {
            gm_t88rcucg1b7uk t57 = gm_l7;
            gm_t88rcucg1b7uk t58 = gm_l2;
            uint8_t t59 = (gm_eq9m6mvtmgde(((uint8_t *)&t57), ((uint8_t *)&t58)));
            if (t59) {
                void * t60 = gm_l1;
                gm_l9 = t60;
            } else {
                gm_t88rcucg1b7uk t61 = gm_l7;
                void * t62 = gm_l1;
                gm_tl874t2atnlh3 t63 = gm_l5;
                gm_to0f5qn75cgg8 t64;
                memset(&t64, 0, sizeof t64);
                GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)18ULL));
                GM_ST(void *, ((uint8_t *)&t64 + 8), t62);
                void * t65 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t61, &t64, t63);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l9 = t65;
            }
            void * t66 = gm_l6;
            gm_t88rcucg1b7uk t67 = gm_l7;
            uint8_t t68 = gm_f_is_vector_gkq6b88(t66, GM_CONV(gm_csguankko4lmhd, t67));
            if (t68) {
                void * t69 = gm_l0;
                void * t70 = gm_l9;
                gm_t88rcucg1b7uk t71 = gm_l7;
                gm_b16_8 t72 = gm_l3;
                gm_b16_8 t73 = gm_l4;
                gm_tl874t2atnlh3 t74 = gm_l5;
                void * t75 = gm_f_vector_method_hgn4sb1((void *)NULL, cx, NULL, t69, t70, t71, t72, t73, t74);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t75;
                goto gm_fret;
            }
            void * t76 = gm_l0;
            void * t77 = gm_l9;
            gm_t88rcucg1b7uk t78 = gm_l7;
            gm_b16_8 t79 = gm_l3;
            gm_b16_8 t80 = gm_l4;
            gm_tl874t2atnlh3 t81 = gm_l5;
            void * t82 = gm_f_math_vector_method_ko2814s((void *)NULL, cx, NULL, t76, t77, t78, t79, t80, t81);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t82;
            goto gm_fret;
        }
    }
    {
        {
            {
                {
                    void * t83 = gm_l6;
                    gm_t88rcucg1b7uk t84 = gm_l7;
                    gm_f_kind_jcnt4hm((void *)&gm_l32, t83, GM_CONV(gm_csguankko4lmhd, t84));
                }
                uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&gm_l32) + 0));
                uint8_t t86 = (uint8_t)(t85 == ((uint8_t)11ULL));
                if (!t86) {
                    goto gm_lb4;
                }
                gm_tnnjeerolos0a t87 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&gm_l32) + 8) + 12));
                gm_l10 = t87;
                {
                    {
                        uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                        uint8_t t89 = (uint8_t)(t88 == ((uint8_t)1ULL));
                        if (!t89) {
                            goto gm_lb6;
                        }
                        gm_l33 = ((uint8_t)1);
                        goto gm_lb5;
                    }
gm_lb6:;
                    {
                        gm_l33 = ((uint8_t)0);
                        goto gm_lb5;
                    }
gm_lb7:;
                    gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 345, 48);
                }
gm_lb5:;
                uint8_t t90 = gm_l33;
                if (!t90) {
                    goto gm_lb4;
                }
                {
                    void * t91 = gm_l0;
                    gm_tl874t2atnlh3 t92 = gm_l5;
                    void * t93 = gm_l0;
                    gm_t88rcucg1b7uk t94 = gm_l7;
                    gm_b16_8 t95 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t93, t94);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t96 = gm_l3;
                    gm_t6j6smj7qoctu t97;
                    GM_ST(gm_b16_8, ((uint8_t *)&t97 + 0), t95);
                    GM_ST(gm_b16_8, ((uint8_t *)&t97 + 16), t96);
                    gm_f_format_p66k75v((void *)&gm_l34, (void *)NULL, cx, NULL, &t97);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t98 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                    if (!t98) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 8, gm_srcngug8co99ok1, 346, 24);
                    gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l34) + 8));
                    gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&t99));
                    gm_b0_1 t101 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t91, t92, t100);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                void * t102 = gm_l0;
                void * t103 = GM_LD(void *, ((uint8_t *)t102));
                gm_tl874t2atnlh3 t104 = gm_l5;
                void * t105 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t103, t104);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t105;
                goto gm_fret;
            }
gm_lb4:;
        }
gm_lb3:;
        void * t106 = gm_l1;
        gm_l11 = t106;
        gm_t88rcucg1b7uk t107 = gm_l2;
        gm_l12 = t107;
        gm_l13 = ((uint8_t)0);
        void * t108 = gm_l6;
        gm_t88rcucg1b7uk t109 = gm_l2;
        gm_f_kind_jcnt4hm((void *)&gm_l35, t108, GM_CONV(gm_csguankko4lmhd, t109));
        {
            {
                uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&gm_l35) + 0));
                uint8_t t111 = (uint8_t)(t110 == ((uint8_t)10ULL));
                if (!t111) {
                    goto gm_lb9;
                }
                uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&gm_l35) + 8));
                gm_l14 = t112;
                gm_t88rcucg1b7uk t113 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l35) + 8) + 4));
                gm_l15 = t113;
                gm_t88rcucg1b7uk t114 = gm_l15;
                gm_l12 = t114;
                uint8_t t115 = gm_l14;
                gm_l13 = t115;
                goto gm_lb8;
            }
gm_lb9:;
            {
                {
                    {
                        uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&gm_l35) + 0));
                        uint8_t t117 = (uint8_t)(t116 == ((uint8_t)11ULL));
                        if (!t117) {
                            goto gm_lb12;
                        }
                        gm_t88rcucg1b7uk t118 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l35) + 8) + 8));
                        gm_l16 = t118;
                        goto gm_lb11;
                    }
gm_lb12:;
                    {
                        uint8_t t119 = GM_LD(uint8_t, (((uint8_t *)&gm_l35) + 0));
                        uint8_t t120 = (uint8_t)(t119 == ((uint8_t)20ULL));
                        if (!t120) {
                            goto gm_lb13;
                        }
                        gm_t88rcucg1b7uk t121 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l35) + 8) + 4));
                        gm_l16 = t121;
                        goto gm_lb11;
                    }
gm_lb13:;
                    goto gm_lb10;
                }
gm_lb11:;
                void * t122 = gm_l0;
                void * t123 = gm_l1;
                gm_tqs5p4n2tq19v t124 = gm_f_place_mutability_0tlm4g8(t122, t123);
                gm_l18 = t124;
                {
                    {
                        {
                            {
                                uint8_t t125 = (uint8_t)((((uint8_t *)&gm_l18))[1] != 0);
                                if (t125) {
                                    goto gm_lb17;
                                }
                                gm_l36 = ((uint8_t)1);
                                goto gm_lb16;
                            }
gm_lb17:;
                            {
                                gm_l36 = ((uint8_t)0);
                                goto gm_lb16;
                            }
gm_lb18:;
                            gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 359, 16);
                        }
gm_lb16:;
                        uint8_t t126 = gm_l36;
                        if (!t126) {
                            goto gm_lb15;
                        }
                        {
                            gm_b16_8 t127 = gm_l3;
                            gm_b16_8 t128;
                            GM_ST(void *, (uint8_t *)&t128, (void *)(gm_st88sqv2u9dqh));
                            GM_ST(uintptr_t, (uint8_t *)&t128 + 8, (uintptr_t)(7));
                            gm_b16_8 t129;
                            GM_ST(void *, (uint8_t *)&t129, (void *)(gm_spevct0q3cjgb));
                            GM_ST(uintptr_t, (uint8_t *)&t129 + 8, (uintptr_t)(4));
                            gm_b16_8 t130;
                            GM_ST(void *, (uint8_t *)&t130, (void *)(gm_s2gpk5q7mg4o8));
                            GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(4));
                            gm_b16_8 t131;
                            GM_ST(void *, (uint8_t *)&t131, (void *)(gm_sgvvkov5kvprt));
                            GM_ST(uintptr_t, (uint8_t *)&t131 + 8, (uintptr_t)(9));
                            gm_t0uuhaljsf410 t132;
                            GM_ST(gm_b16_8, ((uint8_t *)&t132 + 0), t128);
                            GM_ST(gm_b16_8, ((uint8_t *)&t132 + 16), t129);
                            GM_ST(gm_b16_8, ((uint8_t *)&t132 + 32), t130);
                            GM_ST(gm_b16_8, ((uint8_t *)&t132 + 48), t131);
                            uint8_t t133 = 0;
                            if (gm_eq0iqiubfm7e(((uint8_t *)&t132) + 0, ((uint8_t *)&t127))) t133 = 1;
                            if (gm_eq0iqiubfm7e(((uint8_t *)&t132) + 16, ((uint8_t *)&t127))) t133 = 1;
                            if (gm_eq0iqiubfm7e(((uint8_t *)&t132) + 32, ((uint8_t *)&t127))) t133 = 1;
                            if (gm_eq0iqiubfm7e(((uint8_t *)&t132) + 48, ((uint8_t *)&t127))) t133 = 1;
                            uint8_t t134 = (uint8_t)(t133);
                            if (!t134) {
                                goto gm_lb15;
                            }
                        }
                        {
                            void * t135 = gm_l0;
                            gm_tl874t2atnlh3 t136 = gm_l5;
                            gm_b16_8 t137 = gm_l3;
                            gm_tnpifv20irnph t138;
                            GM_ST(gm_b16_8, ((uint8_t *)&t138 + 0), t137);
                            gm_f_format_6pukbnn((void *)&gm_l37, (void *)NULL, cx, NULL, t138);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t139 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
                            if (!t139) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 8, gm_srcngug8co99ok1, 360, 32);
                            gm_b16_8 t140 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l37) + 8));
                            gm_b16_8 t141 = GM_LD(gm_b16_8, ((uint8_t *)&t140));
                            gm_b0_1 t142 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t135, t136, t141);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        goto gm_lb14;
                    }
gm_lb15:;
                }
gm_lb14:;
                gm_t88rcucg1b7uk t143 = gm_l16;
                gm_l12 = t143;
                gm_tqs5p4n2tq19v t144 = gm_l18;
                gm_tqs5p4n2tq19v t145;
                GM_ST(uint8_t, ((uint8_t *)&t145), ((uint8_t)1));
                ((uint8_t *)&t145)[1] = 1;
                uint8_t t146 = (!gm_eqebiqthsuot(((uint8_t *)&t144), ((uint8_t *)&t145)));
                gm_l13 = t146;
                goto gm_lb8;
            }
gm_lb10:;
            {
                uint8_t t147 = GM_LD(uint8_t, (((uint8_t *)&gm_l35) + 0));
                uint8_t t148 = (uint8_t)(t147 == ((uint8_t)8ULL));
                if (!t148) {
                    goto gm_lb19;
                }
                uint8_t t149 = GM_LD(uint8_t, (((uint8_t *)&gm_l35) + 8));
                gm_l19 = t149;
                gm_t88rcucg1b7uk t150 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l35) + 8) + 4));
                gm_l20 = t150;
                void * t151 = gm_l6;
                gm_t88rcucg1b7uk t152 = gm_l20;
                gm_f_kind_jcnt4hm((void *)&gm_l38, t151, GM_CONV(gm_csguankko4lmhd, t152));
                {
                    {
                        {
                            {
                                uint8_t t153 = GM_LD(uint8_t, (((uint8_t *)&gm_l38) + 0));
                                uint8_t t154 = (uint8_t)(t153 == ((uint8_t)11ULL));
                                if (!t154) {
                                    goto gm_lb23;
                                }
                                gm_t88rcucg1b7uk t155 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l38) + 8) + 8));
                                gm_l21 = t155;
                                goto gm_lb22;
                            }
gm_lb23:;
                            {
                                uint8_t t156 = GM_LD(uint8_t, (((uint8_t *)&gm_l38) + 0));
                                uint8_t t157 = (uint8_t)(t156 == ((uint8_t)20ULL));
                                if (!t157) {
                                    goto gm_lb24;
                                }
                                gm_t88rcucg1b7uk t158 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l38) + 8) + 4));
                                gm_l21 = t158;
                                goto gm_lb22;
                            }
gm_lb24:;
                            goto gm_lb21;
                        }
gm_lb22:;
                        gm_t88rcucg1b7uk t159 = gm_l20;
                        void * t160 = gm_l1;
                        gm_tl874t2atnlh3 t161 = gm_l5;
                        gm_to0f5qn75cgg8 t162;
                        memset(&t162, 0, sizeof t162);
                        GM_ST(uint8_t, ((uint8_t *)&t162 + 0), ((uint8_t)18ULL));
                        GM_ST(void *, ((uint8_t *)&t162 + 8), t160);
                        void * t163 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t159, &t162, t161);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l11 = t163;
                        gm_t88rcucg1b7uk t164 = gm_l21;
                        gm_l12 = t164;
                        uint8_t t165 = gm_l19;
                        gm_l13 = t165;
                        goto gm_lb20;
                    }
gm_lb21:;
                    {
                        F.ret = ((void *)0);
                        goto gm_fret;
                    }
gm_lb25:;
                    gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 365, 29);
                }
gm_lb20:;
                goto gm_lb8;
            }
gm_lb19:;
            {
                F.ret = ((void *)0);
                goto gm_fret;
            }
gm_lb26:;
            gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 352, 5);
        }
gm_lb8:;
    }
    {
        {
            {
                {
                    {
                        gm_b16_8 t166 = gm_l3;
                        gm_b16_8 t167;
                        GM_ST(void *, (uint8_t *)&t167, (void *)(gm_sbb1ri5er9djq));
                        GM_ST(uintptr_t, (uint8_t *)&t167 + 8, (uintptr_t)(3));
                        uint8_t t168 = (gm_eq0iqiubfm7e(((uint8_t *)&t166), ((uint8_t *)&t167)));
                        if (!t168) {
                            goto gm_lb30;
                        }
                        goto gm_lb29;
                    }
gm_lb30:;
                    {
                        gm_b16_8 t169 = gm_l3;
                        gm_b16_8 t170;
                        GM_ST(void *, (uint8_t *)&t170, (void *)(gm_st88sqv2u9dqh));
                        GM_ST(uintptr_t, (uint8_t *)&t170 + 8, (uintptr_t)(7));
                        uint8_t t171 = (gm_eq0iqiubfm7e(((uint8_t *)&t169), ((uint8_t *)&t170)));
                        if (!t171) {
                            goto gm_lb31;
                        }
                        goto gm_lb29;
                    }
gm_lb31:;
                    goto gm_lb28;
                }
gm_lb29:;
                void * t172 = gm_l0;
                gm_b16_8 t173 = gm_l4;
                gm_b16_8 t174 = gm_l3;
                gm_tl874t2atnlh3 t175 = gm_l5;
                uint8_t t176 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t172, t173, ((uint64_t)1ULL), t174, t175);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t177 = (uint8_t)!t176;
                if (t177) {
                    void * t178 = gm_l0;
                    void * t179 = GM_LD(void *, ((uint8_t *)t178));
                    gm_tl874t2atnlh3 t180 = gm_l5;
                    void * t181 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t179, t180);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t181;
                    goto gm_fret;
                }
                void * t182 = gm_l0;
                gm_b16_8 t183 = gm_l4;
                void * t184 = gm_l6;
                gm_t88rcucg1b7uk t185 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t184);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tl874t2atnlh3 t186 = gm_l5;
                void * t187 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t182, t183, ((uint64_t)0ULL), t185, t186);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l23 = t187;
                gm_b16_8 t188 = gm_l3;
                gm_b16_8 t189;
                GM_ST(void *, (uint8_t *)&t189, (void *)(gm_sbb1ri5er9djq));
                GM_ST(uintptr_t, (uint8_t *)&t189 + 8, (uintptr_t)(3));
                uint8_t t190 = (gm_eq0iqiubfm7e(((uint8_t *)&t188), ((uint8_t *)&t189)));
                if (t190) {
                    void * t191 = gm_l6;
                    gm_t88rcucg1b7uk t192 = gm_l12;
                    gm_t8588ma2cr9d7 t193;
                    GM_ST(uint8_t, (uint8_t *)&t193 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t193 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t193 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t193 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t193 + 0), ((uint8_t)36ULL));
                    gm_t88rcucg1b7uk t194 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t191, t192);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t195 = gm_l11;
                    void * t196 = gm_l23;
                    gm_t5fru0lp11n7t t197;
                    GM_ST(void *, ((uint8_t *)&t197 + 0), t195);
                    GM_ST(void *, ((uint8_t *)&t197 + 8), t196);
                    gm_l24 = t197;
                    void * t198 = (void *)((uint8_t *)&gm_l24);
                    uint64_t t199 = ((uintptr_t)((uint64_t)2ULL)) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t200;
                    GM_ST(void *, (uint8_t *)&t200, (void *)(((uint8_t *)t198 + (uintptr_t)((uint64_t)0ULL) * 8)));
                    GM_ST(uintptr_t, (uint8_t *)&t200 + 8, (uintptr_t)(t199));
                    gm_tl874t2atnlh3 t201 = gm_l5;
                    void * t202 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t193, t194, t200, t201);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t202;
                    goto gm_fret;
                }
                void * t203 = gm_l6;
                void * t204 = gm_l6;
                uint8_t t205 = gm_l13;
                gm_t88rcucg1b7uk t206 = gm_l12;
                gm_t8588ma2cr9d7 t207;
                GM_ST(uint8_t, (uint8_t *)&t207 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t207 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t207 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t207 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t207 + 0), ((uint8_t)37ULL));
                gm_t88rcucg1b7uk t208 = gm_f_ptr_t1h2m2d((void *)NULL, cx, NULL, t204, t205, t206);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t88rcucg1b7uk t209 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t203, t208);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t210 = gm_l11;
                void * t211 = gm_l23;
                gm_t5fru0lp11n7t t212;
                GM_ST(void *, ((uint8_t *)&t212 + 0), t210);
                GM_ST(void *, ((uint8_t *)&t212 + 8), t211);
                gm_l25 = t212;
                void * t213 = (void *)((uint8_t *)&gm_l25);
                uint64_t t214 = ((uintptr_t)((uint64_t)2ULL)) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t215;
                GM_ST(void *, (uint8_t *)&t215, (void *)(((uint8_t *)t213 + (uintptr_t)((uint64_t)0ULL) * 8)));
                GM_ST(uintptr_t, (uint8_t *)&t215 + 8, (uintptr_t)(t214));
                gm_tl874t2atnlh3 t216 = gm_l5;
                void * t217 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t207, t209, t215, t216);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t217;
                goto gm_fret;
            }
gm_lb28:;
            {
                {
                    {
                        gm_b16_8 t218 = gm_l3;
                        gm_b16_8 t219;
                        GM_ST(void *, (uint8_t *)&t219, (void *)(gm_snivf25e5fme8));
                        GM_ST(uintptr_t, (uint8_t *)&t219 + 8, (uintptr_t)(5));
                        uint8_t t220 = (gm_eq0iqiubfm7e(((uint8_t *)&t218), ((uint8_t *)&t219)));
                        if (!t220) {
                            goto gm_lb34;
                        }
                        goto gm_lb33;
                    }
gm_lb34:;
                    {
                        gm_b16_8 t221 = gm_l3;
                        gm_b16_8 t222;
                        GM_ST(void *, (uint8_t *)&t222, (void *)(gm_s4pmpk07ugvq6));
                        GM_ST(uintptr_t, (uint8_t *)&t222 + 8, (uintptr_t)(4));
                        uint8_t t223 = (gm_eq0iqiubfm7e(((uint8_t *)&t221), ((uint8_t *)&t222)));
                        if (!t223) {
                            goto gm_lb35;
                        }
                        goto gm_lb33;
                    }
gm_lb35:;
                    goto gm_lb32;
                }
gm_lb33:;
                void * t224 = gm_l0;
                gm_b16_8 t225 = gm_l4;
                gm_b16_8 t226 = gm_l3;
                gm_tl874t2atnlh3 t227 = gm_l5;
                uint8_t t228 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t224, t225, ((uint64_t)0ULL), t226, t227);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t229 = gm_l3;
                gm_b16_8 t230;
                GM_ST(void *, (uint8_t *)&t230, (void *)(gm_snivf25e5fme8));
                GM_ST(uintptr_t, (uint8_t *)&t230 + 8, (uintptr_t)(5));
                uint8_t t231 = (gm_eq0iqiubfm7e(((uint8_t *)&t229), ((uint8_t *)&t230)));
                if (t231) {
                    gm_t8588ma2cr9d7 t232;
                    GM_ST(uint8_t, (uint8_t *)&t232 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t232 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t232 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t232 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t232 + 0), ((uint8_t)38ULL));
                    gm_l26 = t232;
                } else {
                    gm_t8588ma2cr9d7 t233;
                    GM_ST(uint8_t, (uint8_t *)&t233 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t233 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t233 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t233 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t233 + 0), ((uint8_t)39ULL));
                    gm_l26 = t233;
                }
                gm_t8588ma2cr9d7 t234 = gm_l26;
                void * t235 = gm_l6;
                gm_t88rcucg1b7uk t236 = gm_l12;
                gm_t88rcucg1b7uk t237 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t235, t236);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t238 = gm_l11;
                gm_tn3r4rvlh0rsv t239;
                GM_ST(void *, ((uint8_t *)&t239 + 0), t238);
                gm_l27 = t239;
                void * t240 = (void *)((uint8_t *)&gm_l27);
                uint64_t t241 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t242;
                GM_ST(void *, (uint8_t *)&t242, (void *)(((uint8_t *)t240 + (uintptr_t)((uint64_t)0ULL) * 8)));
                GM_ST(uintptr_t, (uint8_t *)&t242 + 8, (uintptr_t)(t241));
                gm_tl874t2atnlh3 t243 = gm_l5;
                void * t244 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t234, t237, t242, t243);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t244;
                goto gm_fret;
            }
gm_lb32:;
            {
                goto gm_lb27;
            }
gm_lb36:;
            gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 375, 5);
        }
gm_lb27:;
    }
    {
        gm_b16_8 t245 = gm_l3;
        gm_tlg1vmfbpbvtq t246 = GM_LD(gm_tlg1vmfbpbvtq, ((uint8_t *)gm_cukabrstocl));
        uint8_t t247 = 0;
        for (uintptr_t i = 0; i < (uintptr_t)(27); i++) if (gm_eq0iqiubfm7e(((uint8_t *)&t246) + i * 16, ((uint8_t *)&t245))) { t247 = 1; break; }
        uint8_t t248 = (uint8_t)(t247);
        if (t248) {
            void * t249 = gm_l0;
            void * t250 = gm_l11;
            gm_b16_8 t251 = gm_l3;
            gm_b16_8 t252 = gm_l4;
            gm_tl874t2atnlh3 t253 = gm_l5;
            void * t254 = gm_f_slice_fn_np9f5vf((void *)NULL, cx, NULL, t249, t250, t251, t252, t253);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t254;
            goto gm_fret;
        }
        gm_b16_8 t255 = gm_l3;
        gm_b16_8 t256;
        GM_ST(void *, (uint8_t *)&t256, (void *)(gm_sm2k5tp7dtg07));
        GM_ST(uintptr_t, (uint8_t *)&t256 + 8, (uintptr_t)(3));
        uint8_t t257 = (gm_eq0iqiubfm7e(((uint8_t *)&t255), ((uint8_t *)&t256)));
        if (t257) {
            gm_l39 = ((uint8_t)1);
        } else {
            gm_b16_8 t258 = gm_l3;
            gm_b16_8 t259;
            GM_ST(void *, (uint8_t *)&t259, (void *)(gm_s55kqbt4f7gau));
            GM_ST(uintptr_t, (uint8_t *)&t259 + 8, (uintptr_t)(3));
            uint8_t t260 = (gm_eq0iqiubfm7e(((uint8_t *)&t258), ((uint8_t *)&t259)));
            gm_l39 = t260;
        }
        uint8_t t261 = gm_l39;
        if (t261) {
            {
                gm_tl874t2atnlh3 t262 = gm_l5;
                gm_b16_8 t263 = gm_l3;
                gm_b16_8 t264 = gm_l3;
                gm_t6j6smj7qoctu t265;
                GM_ST(gm_b16_8, ((uint8_t *)&t265 + 0), t263);
                GM_ST(gm_b16_8, ((uint8_t *)&t265 + 16), t264);
                gm_f_format_pe22rd6((void *)&gm_l40, (void *)NULL, cx, NULL, &t265);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t266 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
                if (!t266) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l40) + 8, gm_srcngug8co99ok1, 391, 31);
                gm_b16_8 t267 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l40) + 8));
                gm_b16_8 t268 = GM_LD(gm_b16_8, ((uint8_t *)&t267));
                gm_t4gut2c9fj1kt t269;
                gm_f_error_s6hn871((void *)&t269, (void *)NULL, cx, NULL, t262, t268);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l28 = t269;
            }
            void * t270 = gm_l0;
            void * t271 = GM_LD(void *, ((uint8_t *)t270));
            void * t272 = GM_LD(void *, ((uint8_t *)t271));
            void * t273 = GM_LD(void *, ((uint8_t *)t272));
            uint32_t t274 = GM_LD(uint32_t, ((uint8_t *)&gm_l5));
            void * t275 = gm_f_file_o15fqku(t273, t274);
            void * t276 = GM_LD(void *, (((uint8_t *)t275) + 16));
            uint64_t t277 = GM_LD(uint64_t, ((((uint8_t *)t275) + 16) + 8));
            uint32_t t278 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 4));
            uint64_t t279 = (uint64_t)t278;
            uint32_t t280 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 8));
            uint64_t t281 = (uint64_t)t280;
            uint64_t t282 = (uintptr_t)t279;
            uint64_t t283 = (uintptr_t)t281;
            if (t282 > t283 || t283 > (uintptr_t)t277) gm_trap_slice(t282, t283, t277, gm_srcngug8co99ok1, 392, 17);
            uint64_t t284 = ((uintptr_t)t281) - (uintptr_t)t279;
            gm_b16_8 t285;
            GM_ST(void *, (uint8_t *)&t285, (void *)(((uint8_t *)t276 + (uintptr_t)t279 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t285 + 8, (uintptr_t)(t284));
            gm_l29 = t285;
            {
                {
                    uint64_t t286 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint8_t t287 = ((t286 == ((uint64_t)0ULL)));
                    if (!t287) {
                        goto gm_lb38;
                    }
                    gm_b16_8 t288 = gm_l29;
                    gm_b16_8 t289;
                    GM_ST(void *, (uint8_t *)&t289, (void *)(gm_sbvutvpa8pe3d));
                    GM_ST(uintptr_t, (uint8_t *)&t289 + 8, (uintptr_t)(2));
                    uint8_t t290 = gm_f_ends_with_biriu5l(t288, t289);
                    if (!t290) {
                        goto gm_lb38;
                    }
                    void * t291 = (void *)((uint8_t *)&F.l28);
                    gm_b16_8 t292;
                    GM_ST(void *, (uint8_t *)&t292, (void *)(gm_soefp7bosmvt1));
                    GM_ST(uintptr_t, (uint8_t *)&t292 + 8, (uintptr_t)(11));
                    uint32_t t293 = GM_LD(uint32_t, ((uint8_t *)&gm_l5));
                    uint32_t t294 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 8));
                    uint32_t r295; bool o296 = __builtin_sub_overflow(t294, ((uint32_t)2ULL), &r295);
                    if (o296) gm_trap("integer overflow in `-`", gm_srcngug8co99ok1, 394, 71);
                    uint32_t t297 = r295;
                    uint32_t t298 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 8));
                    gm_tl874t2atnlh3 t299;
                    GM_ST(uint32_t, ((uint8_t *)&t299 + 0), t293);
                    GM_ST(uint32_t, ((uint8_t *)&t299 + 4), t297);
                    GM_ST(uint32_t, ((uint8_t *)&t299 + 8), t298);
                    gm_b16_8 t300;
                    GM_ST(void *, (uint8_t *)&t300, (void *)(gm_sr4qbrk6tlgaa));
                    GM_ST(uintptr_t, (uint8_t *)&t300 + 8, (uintptr_t)(0));
                    gm_b0_1 t301 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t291, t292, t299, t300);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb37;
                }
gm_lb38:;
            }
gm_lb37:;
            {
                void * t302 = gm_l0;
                void * t303 = GM_LD(void *, ((uint8_t *)t302));
                gm_b0_1 t304 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t303, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l28)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        } else {
            void * t305 = gm_l0;
            gm_tl874t2atnlh3 t306 = gm_l5;
            gm_b16_8 t307 = gm_l3;
            gm_tnpifv20irnph t308;
            GM_ST(gm_b16_8, ((uint8_t *)&t308 + 0), t307);
            gm_f_format_dir32ml((void *)&gm_l41, (void *)NULL, cx, NULL, t308);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t309 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
            if (!t309) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 8, gm_srcngug8co99ok1, 398, 24);
            gm_b16_8 t310 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l41) + 8));
            gm_b16_8 t311 = GM_LD(gm_b16_8, ((uint8_t *)&t310));
            gm_b0_1 t312 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t305, t306, t311);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        void * t313 = gm_l0;
        void * t314 = GM_LD(void *, ((uint8_t *)t313));
        gm_tl874t2atnlh3 t315 = gm_l5;
        void * t316 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t314, t315);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t316;
        goto gm_fret;
    }
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_core_fn_uts2vhe(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_core_fn_uts2vhe F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    gm_b16_8 gm_l3; /* mname */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* m */
    uint64_t gm_l7; /* d */
    uint32_t gm_l8; /* f */
    GM_STACK_CHECK(&F, 754u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l9;
    gm_tf5srh4mp4b3i gm_l10;
    gm_t6qooom8qa2do gm_l11;
    gm_b16_8 t9 = gm_l1;
    gm_tnpifv20irnph t10;
    GM_ST(gm_b16_8, ((uint8_t *)&t10 + 0), t9);
    gm_f_format_agi9f7f((void *)&gm_l9, (void *)NULL, cx, NULL, t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t11 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (!t11) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srcngug8co99ok1, 437, 14);
    gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
    gm_l3 = t12;
    void * t13 = gm_l0;
    void * t14 = GM_LD(void *, ((uint8_t *)t13));
    void * t15 = GM_LD(void *, ((uint8_t *)t14));
    gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)t15) + 336));
    gm_l4 = t16;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t17 = gm_l5;
            uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t19 = (t17 < t18);
            if (!t19) {
                goto gm_lb1;
            }
            {
                void * t20 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t22 = gm_l5;
                {
                    {
                        uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)t20) + (uintptr_t)t22 * 8));
                        gm_l6 = t23;
                        void * t24 = gm_l0;
                        void * t25 = GM_LD(void *, ((uint8_t *)t24));
                        void * t26 = GM_LD(void *, ((uint8_t *)t25));
                        void * t27 = GM_LD(void *, (((uint8_t *)t26) + 200));
                        uint64_t t28 = GM_LD(uint64_t, ((((uint8_t *)t26) + 200) + 8));
                        uint64_t t29 = gm_l6;
                        if ((uintptr_t)t29 >= (uintptr_t)(t28)) gm_trap_index((uintptr_t)t29, t28, gm_srcngug8co99ok1, 439, 12);
                        gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)t27) + (uintptr_t)t29 * 184));
                        gm_b16_8 t31 = gm_l3;
                        gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
                        uint8_t t33 = (!gm_eq0iqiubfm7e(((uint8_t *)&t30), ((uint8_t *)&t32)));
                        if (t33) {
                            goto gm_lb3;
                        }
                        void * t34 = gm_l0;
                        void * t35 = GM_LD(void *, ((uint8_t *)t34));
                        void * t36 = GM_LD(void *, ((uint8_t *)t35));
                        uint64_t t37 = gm_l6;
                        gm_b16_8 t38 = gm_l2;
                        gm_tf5srh4mp4b3i t39 = gm_f_lookup_59juqoo(t36, t37, t38);
                        gm_l10 = t39;
                        uint8_t t40 = (uint8_t)((((uint8_t *)&gm_l10))[8] != 0);
                        if (t40) {
                            uint64_t t41 = GM_LD(uint64_t, ((uint8_t *)&gm_l10));
                            gm_l7 = t41;
                        } else {
                            gm_t9805qtonbsag t42;
                            memset(&t42, 0, sizeof t42);
                            F.ret = t42;
                            goto gm_fret;
                        }
                        {
                            {
                                void * t43 = gm_l0;
                                void * t44 = GM_LD(void *, ((uint8_t *)t43));
                                uint64_t t45 = gm_l7;
                                gm_f_resolve_decl_pus042v((void *)&gm_l11, (void *)NULL, cx, NULL, t44, t45);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
                                uint8_t t47 = (uint8_t)(t46 == ((uint8_t)5ULL));
                                if (!t47) {
                                    goto gm_lb7;
                                }
                                uint32_t t48 = GM_LD(uint32_t, (((uint8_t *)&gm_l11) + 8));
                                gm_l8 = t48;
                                uint32_t t49 = gm_l8;
                                gm_t9805qtonbsag t50;
                                GM_ST(uint8_t, (uint8_t *)&t50 + 5, 0);
                                GM_ST(uint16_t, (uint8_t *)&t50 + 6, 0);
                                GM_ST(uint32_t, ((uint8_t *)&t50), t49);
                                ((uint8_t *)&t50)[4] = 1;
                                F.ret = t50;
                                goto gm_fret;
                            }
gm_lb7:;
                            gm_t9805qtonbsag t51;
                            memset(&t51, 0, sizeof t51);
                            F.ret = t51;
                            goto gm_fret;
                        }
gm_lb6:;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 438, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t52 = gm_l5;
            uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)1ULL), &r53);
            uint64_t t55 = r53;
            gm_l5 = t55;
        }
    }
gm_lb1:;
    gm_t9805qtonbsag t56;
    memset(&t56, 0, sizeof t56);
    F.ret = t56;
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_effect_named_b8afdbp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_effect_named_b8afdbp F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    uint64_t gm_l2; /* d */
    void * gm_l3; /* callee */
    gm_tiilmfd16v3fc gm_l4; /* r */
    gm_t08olq97rscn3 gm_l5; /* id */
    void * gm_l6; /* base */
    gm_t08olq97rscn3 gm_l7; /* name */
    uint64_t gm_l8; /* m */
    uint32_t gm_l9; /* eff */
    gm_tiilmfd16v3fc gm_l10; /* r */
    GM_STACK_CHECK(&F, 1246u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t716e5hdn64ba gm_l11;
    gm_tlbrdhah49ci5 gm_l12;
    gm_tqu3m2l3n8mnc gm_l13;
    uint8_t gm_l14;
    gm_tqvu78rpi6q1h gm_l15;
    uint64_t gm_l16;
    gm_tf5srh4mp4b3i gm_l17;
    gm_tf5srh4mp4b3i gm_l18;
    uint64_t gm_l19;
    gm_tf5srh4mp4b3i gm_l20;
    gm_t6qooom8qa2do gm_l21;
    gm_l2 = ((uint64_t)0ULL);
    void * t14 = gm_l1;
    {
        {
            uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
            uint8_t t16 = (uint8_t)(t15 == ((uint8_t)19ULL));
            if (!t16) {
                goto gm_lb2;
            }
            void * t17 = GM_LD(void *, ((((uint8_t *)t14) + 8) + 8));
            gm_l3 = t17;
            {
                void * t18 = gm_l0;
                void * t19 = gm_l3;
                gm_f_effect_named_b8afdbp((void *)&gm_l12, (void *)NULL, cx, NULL, t18, t19);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t20 = (uint8_t)((((uint8_t *)&gm_l12))[32] != 0);
            if (t20) {
                memmove(((uint8_t *)&gm_l11), ((uint8_t *)&gm_l12), 32);
            } else {
                memset(&(*(gm_tlbrdhah49ci5 *)gm_out), 0, sizeof (*(gm_tlbrdhah49ci5 *)gm_out));
                goto gm_fdone;
            }
            {
            }
            {
                void * t21 = gm_l0;
                void * t22 = GM_LD(void *, ((uint8_t *)t21));
                void * t23 = gm_l0;
                uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)t23) + 8));
                void * t25 = gm_l1;
                gm_f_resolve_effect_ref_bggirs2((void *)&gm_l13, (void *)NULL, cx, NULL, t22, t24, t25);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t26 = (uint8_t)((((uint8_t *)&gm_l13))[24] != 0);
            if (t26) {
                memmove(((uint8_t *)&gm_l4), ((uint8_t *)&gm_l13), 24);
            } else {
                memset(&(*(gm_tlbrdhah49ci5 *)gm_out), 0, sizeof (*(gm_tlbrdhah49ci5 *)gm_out));
                goto gm_fdone;
            }
            {
                gm_tiilmfd16v3fc t27 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l4));
                gm_t716e5hdn64ba t28;
                GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t28 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t28 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)1ULL));
                GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t28 + 8), t27);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tlbrdhah49ci5 *)gm_out) + 33, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tlbrdhah49ci5 *)gm_out) + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tlbrdhah49ci5 *)gm_out) + 36, 0);
                GM_ST(gm_t716e5hdn64ba, ((uint8_t *)&(*(gm_tlbrdhah49ci5 *)gm_out)), t28);
                ((uint8_t *)&(*(gm_tlbrdhah49ci5 *)gm_out))[32] = 1;
                goto gm_fdone;
            }
        }
gm_lb2:;
        {
            uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
            uint8_t t30 = (uint8_t)(t29 == ((uint8_t)6ULL));
            if (!t30) {
                goto gm_lb3;
            }
            {
                memmove(((uint8_t *)&gm_l5), ((((uint8_t *)t14) + 8) + 8), 32);
            }
            {
                void * t31 = gm_l0;
                gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
                gm_f_lookup_local_6f38t7p((void *)&gm_l15, t31, t32);
            }
            {
                {
                    uint8_t t33 = (uint8_t)((((uint8_t *)&gm_l15))[208] != 0);
                    if (t33) {
                        goto gm_lb5;
                    }
                    gm_l14 = ((uint8_t)0);
                    goto gm_lb4;
                }
gm_lb5:;
                {
                    gm_l14 = ((uint8_t)1);
                    goto gm_lb4;
                }
gm_lb6:;
                gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 624, 16);
            }
gm_lb4:;
            uint8_t t34 = gm_l14;
            if (t34) {
                memset(&(*(gm_tlbrdhah49ci5 *)gm_out), 0, sizeof (*(gm_tlbrdhah49ci5 *)gm_out));
                goto gm_fdone;
            }
            void * t35 = gm_l0;
            void * t36 = GM_LD(void *, ((uint8_t *)t35));
            void * t37 = gm_l0;
            uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)t37) + 8));
            gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
            gm_tf5srh4mp4b3i t40 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t36, t38, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l17 = t40;
            uint8_t t41 = (uint8_t)((((uint8_t *)&gm_l17))[8] != 0);
            if (t41) {
                uint64_t t42 = GM_LD(uint64_t, ((uint8_t *)&gm_l17));
                gm_l16 = t42;
            } else {
                memset(&(*(gm_tlbrdhah49ci5 *)gm_out), 0, sizeof (*(gm_tlbrdhah49ci5 *)gm_out));
                goto gm_fdone;
            }
            uint64_t t43 = gm_l16;
            gm_l2 = t43;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t44 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
            uint8_t t45 = (uint8_t)(t44 == ((uint8_t)22ULL));
            if (!t45) {
                goto gm_lb7;
            }
            void * t46 = GM_LD(void *, ((((uint8_t *)t14) + 8) + 8));
            gm_l6 = t46;
            {
                memmove(((uint8_t *)&gm_l7), (((((uint8_t *)t14) + 8) + 8) + 8), 32);
            }
            void * t47 = gm_l0;
            void * t48 = gm_l6;
            gm_tf5srh4mp4b3i t49 = gm_f_module_of_n0d5f6n((void *)NULL, cx, NULL, t47, t48);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l18 = t49;
            uint8_t t50 = (uint8_t)((((uint8_t *)&gm_l18))[8] != 0);
            if (t50) {
                uint64_t t51 = GM_LD(uint64_t, ((uint8_t *)&gm_l18));
                gm_l8 = t51;
            } else {
                memset(&(*(gm_tlbrdhah49ci5 *)gm_out), 0, sizeof (*(gm_tlbrdhah49ci5 *)gm_out));
                goto gm_fdone;
            }
            {
                void * t52 = gm_l0;
                void * t53 = GM_LD(void *, ((uint8_t *)t52));
                uint64_t t54 = gm_l8;
                void * t55 = gm_l0;
                uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)t55) + 8));
                gm_tf5srh4mp4b3i t57 = gm_f_module_member_5fd2qm9((void *)NULL, cx, NULL, t53, t54, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l7)), t56);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l20 = t57;
            }
            uint8_t t58 = (uint8_t)((((uint8_t *)&gm_l20))[8] != 0);
            if (t58) {
                uint64_t t59 = GM_LD(uint64_t, ((uint8_t *)&gm_l20));
                gm_l19 = t59;
            } else {
                memset(&(*(gm_tlbrdhah49ci5 *)gm_out), 0, sizeof (*(gm_tlbrdhah49ci5 *)gm_out));
                goto gm_fdone;
            }
            uint64_t t60 = gm_l19;
            gm_l2 = t60;
            goto gm_lb1;
        }
gm_lb7:;
        {
            memset(&(*(gm_tlbrdhah49ci5 *)gm_out), 0, sizeof (*(gm_tlbrdhah49ci5 *)gm_out));
            goto gm_fdone;
        }
gm_lb8:;
        gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 617, 5);
    }
gm_lb1:;
    void * t61 = gm_l0;
    void * t62 = GM_LD(void *, ((uint8_t *)t61));
    uint64_t t63 = gm_l2;
    gm_f_resolve_decl_pus042v((void *)&gm_l21, (void *)NULL, cx, NULL, t62, t63);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
            uint8_t t65 = (uint8_t)(t64 == ((uint8_t)2ULL));
            if (!t65) {
                goto gm_lb10;
            }
            uint32_t t66 = GM_LD(uint32_t, (((uint8_t *)&gm_l21) + 8));
            gm_l9 = t66;
            {
                uint32_t t67 = gm_l9;
                gm_t716e5hdn64ba t68;
                GM_ST(uint8_t, (uint8_t *)&t68 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t68 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t68 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t68 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t68 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t68 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)0ULL));
                GM_ST(uint32_t, ((uint8_t *)&t68 + 8), t67);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tlbrdhah49ci5 *)gm_out) + 33, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tlbrdhah49ci5 *)gm_out) + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tlbrdhah49ci5 *)gm_out) + 36, 0);
                GM_ST(gm_t716e5hdn64ba, ((uint8_t *)&(*(gm_tlbrdhah49ci5 *)gm_out)), t68);
                ((uint8_t *)&(*(gm_tlbrdhah49ci5 *)gm_out))[32] = 1;
                goto gm_fdone;
            }
        }
gm_lb10:;
        {
            uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
            uint8_t t70 = (uint8_t)(t69 == ((uint8_t)3ULL));
            if (!t70) {
                goto gm_lb11;
            }
            {
                memmove(((uint8_t *)&gm_l10), (((uint8_t *)&gm_l21) + 8), 24);
            }
            {
                gm_tiilmfd16v3fc t71 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l10));
                gm_t716e5hdn64ba t72;
                GM_ST(uint8_t, (uint8_t *)&t72 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t72 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t72 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t72 + 0), ((uint8_t)1ULL));
                GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t72 + 8), t71);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tlbrdhah49ci5 *)gm_out) + 33, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tlbrdhah49ci5 *)gm_out) + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tlbrdhah49ci5 *)gm_out) + 36, 0);
                GM_ST(gm_t716e5hdn64ba, ((uint8_t *)&(*(gm_tlbrdhah49ci5 *)gm_out)), t72);
                ((uint8_t *)&(*(gm_tlbrdhah49ci5 *)gm_out))[32] = 1;
                goto gm_fdone;
            }
        }
gm_lb11:;
        {
            memset(&(*(gm_tlbrdhah49ci5 *)gm_out), 0, sizeof (*(gm_tlbrdhah49ci5 *)gm_out));
            goto gm_fdone;
        }
gm_lb12:;
        gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 633, 5);
    }
gm_lb9:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_with_continuation_n_lv5ofqt(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_tl874t2atnlh3 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_emit_with_continuation_n_lv5ofqt F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l2; /* base */
    gm_tl874t2atnlh3 gm_l3; /* name */
    void * gm_l5; /* file */
    GM_STACK_CHECK(&F, 198u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tcc1rq4n1avb2 gm_l6;
    gm_tcc1rq4n1avb2 gm_l7;
    {
        memmove(((uint8_t *)&F.l4), ((uint8_t *)a1), 184);
    }
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, ((uint8_t *)t4));
    void * t6 = GM_LD(void *, ((uint8_t *)t5));
    void * t7 = GM_LD(void *, ((uint8_t *)t6));
    uint32_t t8 = GM_LD(uint32_t, ((uint8_t *)&gm_l2));
    void * t9 = gm_f_file_o15fqku(t7, t8);
    gm_l5 = t9;
    {
        {
            uint32_t t10 = GM_LD(uint32_t, ((uint8_t *)&gm_l2));
            uint32_t t11 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
            uint8_t t12 = ((t10 == t11));
            if (!t12) {
                goto gm_lb2;
            }
            void * t13 = gm_l5;
            uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 8));
            gm_tcc1rq4n1avb2 t15 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t13, t14));
            gm_l6 = t15;
            uint32_t t16 = GM_LD(uint32_t, ((uint8_t *)&gm_l6));
            void * t17 = gm_l5;
            uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
            gm_tcc1rq4n1avb2 t19 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t17, t18));
            gm_l7 = t19;
            uint32_t t20 = GM_LD(uint32_t, ((uint8_t *)&gm_l7));
            uint8_t t21 = (t16 < t20);
            if (!t21) {
                goto gm_lb2;
            }
            void * t22 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sfr85ju00e9hk));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(137));
            gm_f_add_note_46v502s(t22, t23);
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        void * t24 = gm_l0;
        void * t25 = GM_LD(void *, ((uint8_t *)t24));
        gm_b0_1 t26 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t25, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l4)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_finish_call_kjknkfg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, uint8_t a3) {
    struct gm_fr_finish_call_kjknkfg F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* pre */
    void * gm_l2; /* call */
    uint8_t gm_l3; /* receiver */
    gm_b16_8 gm_l5; /* args */
    GM_STACK_CHECK(&F, 364u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tq7s98ihonkju gm_l6;
    gm_tftb1odc739t5 gm_l7;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
    if (t5) {
        void * t6 = gm_l2;
        F.ret = t6;
        goto gm_fret;
    }
    gm_trqfvmjtt4sij t7;
    gm_f_list_ir0h3tc_2((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l4 = t7;
    {
        {
            void * t8 = gm_l2;
            uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)34ULL));
            if (!t10) {
                goto gm_lb2;
            }
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((((uint8_t *)t8) + 8) + 8) + 40));
            gm_l5 = t11;
            uint8_t t12 = gm_l3;
            if (!t12) {
                goto gm_lb2;
            }
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t14 = (t13 > ((uint64_t)0ULL));
            if (!t14) {
                goto gm_lb2;
            }
            {
                void * t15 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                void * t17 = gm_l0;
                void * t18 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                gm_tq7s98ihonkju t20 = gm_f_new_ve8bb01_2((void *)NULL, cx, NULL, &(*(gm_tsf6i2agfnpva *)(((uint8_t *)t18) + (uintptr_t)((uint64_t)0ULL) * 88)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kdoa98a1tm0)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l6 = t20;
                uint8_t t21 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                if (!t21) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_srcngug8co99ok1, 603, 32);
                void * t22 = GM_LD(void *, (((uint8_t *)&gm_l6) + 8));
                void * t23 = (void *)((uint8_t *)&F.l4);
                void * t24 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t17, t22, t23);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                memmove((((uint8_t *)t15) + (uintptr_t)((uint64_t)0ULL) * 88), ((uint8_t *)t24), 88);
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t25 = (void *)((uint8_t *)&F.l4);
    gm_b16_8 t26 = gm_l1;
    gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
    gm_tftb1odc739t5 t28 = gm_f_extend_nkql463(t25, t27);
    gm_l7 = t28;
    uint8_t t29 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (!t29) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srcngug8co99ok1, 605, 5);
    void * t30 = gm_l2;
    gm_t88rcucg1b7uk t31 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t30));
    gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
    void * t33 = gm_l2;
    void * t34 = gm_l2;
    gm_tl874t2atnlh3 t35 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t34) + 72));
    void * t36 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t31, t32, t33, t35);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t36;
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_fits_receiver_1qqamgd(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fits_receiver_1qqamgd F;
    void * gm_l0; /* tys */
    gm_t88rcucg1b7uk gm_l1; /* p */
    gm_t88rcucg1b7uk gm_l2; /* a */
    uint32_t gm_l3; /* x */
    uint32_t gm_l4; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 216u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    gm_t53at80pfdus3 gm_l5;
    gm_t53at80pfdus3 gm_l6;
    gm_t88rcucg1b7uk t6 = gm_l1;
    gm_t88rcucg1b7uk t7 = gm_l2;
    uint8_t t8 = (gm_eq9m6mvtmgde(((uint8_t *)&t6), ((uint8_t *)&t7)));
    if (t8) {
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
    {
        {
            void * t9 = gm_l0;
            gm_t88rcucg1b7uk t10 = gm_l1;
            gm_f_kind_jcnt4hm((void *)&gm_l5, t9, GM_CONV(gm_csguankko4lmhd, t10));
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t12 = (uint8_t)(t11 == ((uint8_t)17ULL));
            if (!t12) {
                goto gm_lb2;
            }
            uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 8));
            gm_l3 = t13;
            {
                {
                    void * t14 = gm_l0;
                    gm_t88rcucg1b7uk t15 = gm_l2;
                    gm_f_kind_jcnt4hm((void *)&gm_l6, t14, GM_CONV(gm_csguankko4lmhd, t15));
                    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                    uint8_t t17 = (uint8_t)(t16 == ((uint8_t)17ULL));
                    if (!t17) {
                        goto gm_lb4;
                    }
                    uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 8));
                    gm_l4 = t18;
                    void * t19 = gm_l0;
                    uint32_t t20 = gm_l3;
                    uint32_t t21 = gm_f_template_9o92cvv(t19, t20);
                    void * t22 = gm_l0;
                    uint32_t t23 = gm_l4;
                    uint32_t t24 = gm_f_template_9o92cvv(t22, t23);
                    uint8_t t25 = ((t21 == t24));
                    F.ret = t25;
                    goto gm_fret;
                }
gm_lb4:;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
gm_lb3:;
        }
gm_lb2:;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_fn_cast_mismatch_e9s21md(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_fn_cast_mismatch_e9s21md F;
    void * gm_l0; /* cx */
    void * gm_l1; /* from */
    void * gm_l2; /* to */
    gm_closure gm_l5; /* same_repr */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    gm_t88rcucg1b7uk gm_l9; /* a */
    gm_t88rcucg1b7uk gm_l10; /* b */
    GM_STACK_CHECK(&F, 932u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l11;
    gm_tabb710d9mq2e gm_l12;
    gm_tabb710d9mq2e gm_l13;
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    void * t9 = (void *)(((uint8_t *)t8) + 8);
    F.l3 = t9;
    gm_closure t10 = ((gm_closure){(void *)gm_f_pointer_gv6lvmk, (void *)&F.h, cx});
    F.l4 = t10;
    gm_closure t11 = ((gm_closure){(void *)gm_f_same_repr_830dsa9, (void *)&F.h, cx});
    gm_l5 = t11;
    void * t12 = gm_l1;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)t12) + 40));
    void * t14 = gm_l2;
    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)t14) + 40));
    uint8_t t16 = (!(t13 == t15));
    if (t16) {
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_s42aeamnkrchf));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(30));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t17);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        goto gm_fdone;
    }
    void * t18 = gm_l1;
    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)t18) + 8));
    void * t20 = gm_l2;
    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)t20) + 8));
    uint8_t t22 = (!(t19 == t21));
    if (t22) {
        {
            void * t23 = gm_l1;
            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)t23) + 8));
            void * t25 = gm_l2;
            uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)t25) + 8));
            gm_t2lqu8jsj511d t27;
            GM_ST(uint64_t, ((uint8_t *)&t27 + 0), t24);
            GM_ST(uint64_t, ((uint8_t *)&t27 + 8), t26);
            gm_f_format_ahnpa17((void *)&gm_l11, (void *)NULL, cx, NULL, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t28 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
        if (!t28) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_srcngug8co99ok1, 321, 54);
        {
            gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
            gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t30);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            goto gm_fdone;
        }
    }
    gm_l7 = ((uint64_t)0ULL);
    void * t31 = gm_l1;
    uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)t31) + 8));
    gm_l6 = t32;
    {
        for (;;) {
            uint64_t t34 = gm_l7;
            uint64_t t35 = gm_l6;
            uint8_t t36 = (t34 < t35);
            if (!t36) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t37 = gm_l7;
                        gm_l8 = t37;
                        void * t38 = gm_l1;
                        void * t39 = GM_LD(void *, ((uint8_t *)t38));
                        uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)t38) + 8));
                        uint64_t t41 = gm_l8;
                        if ((uintptr_t)t41 >= (uintptr_t)(t40)) gm_trap_index((uintptr_t)t41, t40, gm_srcngug8co99ok1, 323, 14);
                        gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t39) + (uintptr_t)t41 * 4));
                        gm_l9 = t42;
                        void * t43 = gm_l2;
                        void * t44 = GM_LD(void *, ((uint8_t *)t43));
                        uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)t43) + 8));
                        uint64_t t46 = gm_l8;
                        if ((uintptr_t)t46 >= (uintptr_t)(t45)) gm_trap_index((uintptr_t)t46, t45, gm_srcngug8co99ok1, 324, 14);
                        gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t44) + (uintptr_t)t46 * 4));
                        gm_l10 = t47;
                        gm_closure t48 = gm_l5;
                        gm_t88rcucg1b7uk t49 = gm_l9;
                        gm_t88rcucg1b7uk t50 = gm_l10;
                        gm_closure t51 = t48;
                        gm_node *ccx52 = t51.cx;
                        void * t53 = t51.code;
                        uint8_t t54;
                        if (GM_PTR_TAGGED(t53)) {
                            t54 = ((uint8_t (*)(gm_csguankko4lmhd, gm_csguankko4lmhd))GM_PTR_UNTAG(t53))(GM_CONV(gm_csguankko4lmhd, t49), GM_CONV(gm_csguankko4lmhd, t50));
                        } else {
                            t54 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk, gm_t88rcucg1b7uk))t53)(t51.env, ccx52, NULL, t49, t50);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll33;
                        }
                        uint8_t t55 = (uint8_t)!t54;
                        if (t55) {
                            {
                                uint64_t t56 = gm_l8;
                                uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
                                if (o58) gm_trap("integer overflow in `+`", gm_srcngug8co99ok1, 326, 119);
                                uint64_t t59 = r57;
                                void * t60 = gm_l0;
                                gm_t88rcucg1b7uk t61 = gm_l9;
                                gm_b16_8 t62 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t60, t61);
                                if (gm_unw.kind) {
                                    goto gm_ll33;
                                }
                                void * t63 = gm_l0;
                                gm_t88rcucg1b7uk t64 = gm_l10;
                                gm_b16_8 t65 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t63, t64);
                                if (gm_unw.kind) {
                                    goto gm_ll33;
                                }
                                gm_t7neromnu8t62 t66;
                                GM_ST(uint64_t, ((uint8_t *)&t66 + 0), t59);
                                GM_ST(gm_b16_8, ((uint8_t *)&t66 + 8), t62);
                                GM_ST(gm_b16_8, ((uint8_t *)&t66 + 24), t65);
                                gm_f_format_rn1vtpg((void *)&gm_l12, (void *)NULL, cx, NULL, &t66);
                                if (gm_unw.kind) {
                                    goto gm_ll33;
                                }
                            }
                            uint8_t t67 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                            if (!t67) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_srcngug8co99ok1, 326, 20);
                            {
                                gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
                                gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t69);
                                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                                goto gm_fdone;
                            }
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 322, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t70 = gm_l7;
            uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
            uint64_t t73 = r71;
            gm_l7 = t73;
        }
    }
    goto gm_ls74;
gm_ll33:;
    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb1; }
        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb3; }
    }
    goto gm_lad;
gm_ls74:;
gm_lb1:;
    gm_closure t75 = gm_l5;
    void * t76 = gm_l1;
    gm_t88rcucg1b7uk t77 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t76) + 16));
    void * t78 = gm_l2;
    gm_t88rcucg1b7uk t79 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t78) + 16));
    gm_closure t80 = t75;
    gm_node *ccx81 = t80.cx;
    void * t82 = t80.code;
    uint8_t t83;
    if (GM_PTR_TAGGED(t82)) {
        t83 = ((uint8_t (*)(gm_csguankko4lmhd, gm_csguankko4lmhd))GM_PTR_UNTAG(t82))(GM_CONV(gm_csguankko4lmhd, t77), GM_CONV(gm_csguankko4lmhd, t79));
    } else {
        t83 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk, gm_t88rcucg1b7uk))t82)(t80.env, ccx81, NULL, t77, t79);
    }
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t84 = (uint8_t)!t83;
    if (t84) {
        {
            void * t85 = gm_l0;
            void * t86 = gm_l1;
            gm_t88rcucg1b7uk t87 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t86) + 16));
            gm_b16_8 t88 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t85, t87);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t89 = gm_l0;
            void * t90 = gm_l2;
            gm_t88rcucg1b7uk t91 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t90) + 16));
            gm_b16_8 t92 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t89, t91);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6j6smj7qoctu t93;
            GM_ST(gm_b16_8, ((uint8_t *)&t93 + 0), t88);
            GM_ST(gm_b16_8, ((uint8_t *)&t93 + 16), t92);
            gm_f_format_jfku65e((void *)&gm_l13, (void *)NULL, cx, NULL, &t93);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t94 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
        if (!t94) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_srcngug8co99ok1, 330, 16);
        {
            gm_b16_8 t95 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
            gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&t95));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t96);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            goto gm_fdone;
        }
    }
    memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_kind_u4oqjin(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_kind_u4oqjin F;
    gm_t88rcucg1b7uk gm_l0; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    gm_t53at80pfdus3 gm_l2;
    void * t10 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_pointer_cast_6siqq5h *)((&F.h)->up))->l5));
    gm_t88rcucg1b7uk t11 = gm_l0;
    gm_f_kind_jcnt4hm((void *)&gm_l2, t10, GM_CONV(gm_csguankko4lmhd, t11));
    {
        {
            {
                {
                    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t13 = (uint8_t)(t12 == ((uint8_t)8ULL));
                    if (!t13) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t15 = (uint8_t)(t14 == ((uint8_t)9ULL));
                    if (!t15) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t17 = (uint8_t)(t16 == ((uint8_t)7ULL));
                    if (!t17) {
                        goto gm_lb6;
                    }
                    goto gm_lb3;
                }
gm_lb6:;
                goto gm_lb2;
            }
gm_lb3:;
            gm_l1 = ((uint8_t)1ULL);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t19 = (uint8_t)(t18 == ((uint8_t)16ULL));
            if (!t19) {
                goto gm_lb7;
            }
            gm_l1 = ((uint8_t)2ULL);
            goto gm_lb1;
        }
gm_lb7:;
        {
            gm_l1 = ((uint8_t)0ULL);
            goto gm_lb1;
        }
gm_lb8:;
        gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 275, 32);
    }
gm_lb1:;
    uint8_t t20 = gm_l1;
    F.ret = t20;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_method_call_hebo20o(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_method_call_hebo20o F;
    void * gm_l0; /* cx */
    void * gm_l1; /* base */
    gm_t08olq97rscn3 gm_l2; /* name */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    gm_t88rcucg1b7uk gm_l6; /* t */
    gm_tjpqvfd72o62u gm_l7; /* info */
    void * gm_l8; /* recv */
    gm_t88rcucg1b7uk gm_l9; /* rty */
    void * gm_l10; /* soa_recv */
    gm_tjpqvfd72o62u gm_l11; /* info */
    gm_t88rcucg1b7uk gm_l12; /* derefed_ty */
    gm_t88rcucg1b7uk gm_l13; /* p */
    gm_b16_8 gm_l14; /* fields */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* i */
    void * gm_l19; /* field */
    void * gm_l20; /* e */
    gm_t08olq97rscn3 gm_l21; /* mname */
    uint8_t gm_l22; /* internal */
    gm_tbngattl1fo51 gm_l23; /* r */
    void * gm_l24; /* e */
    GM_STACK_CHECK(&F, 2564u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tkn42u7na3t81 gm_l26;
    gm_t8cc1mtbrhlpt gm_l27;
    gm_t8cc1mtbrhlpt gm_l28;
    gm_tabb710d9mq2e gm_l29;
    gm_t53at80pfdus3 gm_l30;
    gm_th2lqbo77pvsc gm_l31;
    gm_t8ta482bl6be0 gm_l32;
    void * gm_l33;
    gm_thetuhtouve7f gm_l34;
    gm_b16_8 gm_l35;
    void * gm_l36;
    gm_tabb710d9mq2e gm_l37;
    {
        void * t25 = gm_l0;
        void * t26 = GM_LD(void *, ((uint8_t *)t25));
        void * t27 = (void *)(((uint8_t *)t26) + 8);
        gm_l5 = t27;
        {
            {
                void * t28 = gm_l0;
                void * t29 = gm_l1;
                gm_tkn42u7na3t81 t30 = gm_f_type_of_name_oqtujpn((void *)NULL, cx, NULL, t28, t29);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l26 = t30;
                uint8_t t31 = (uint8_t)((((uint8_t *)&gm_l26))[4] != 0);
                if (!t31) {
                    goto gm_lb2;
                }
                gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l26));
                gm_l6 = t32;
                gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                gm_b16_8 t34;
                GM_ST(void *, (uint8_t *)&t34, (void *)(gm_s66fk044n1n0k));
                GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(8));
                uint8_t t35 = (gm_eq0iqiubfm7e(((uint8_t *)&t33), ((uint8_t *)&t34)));
                if (t35) {
                    void * t36 = gm_l0;
                    gm_t88rcucg1b7uk t37 = gm_l6;
                    gm_b16_8 t38 = gm_l3;
                    gm_tl874t2atnlh3 t39 = gm_l4;
                    void * t40 = gm_f_check_from_tag_g3el2co((void *)NULL, cx, NULL, t36, t37, t38, t39);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t40;
                    goto gm_fret;
                }
                {
                    {
                        gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                        gm_b16_8 t42;
                        GM_ST(void *, (uint8_t *)&t42, (void *)(gm_sqfrv3g5v9m2n));
                        GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(4));
                        uint8_t t43 = (gm_eq0iqiubfm7e(((uint8_t *)&t41), ((uint8_t *)&t42)));
                        if (!t43) {
                            goto gm_lb4;
                        }
                        void * t44 = gm_l0;
                        void * t45 = GM_LD(void *, ((uint8_t *)t44));
                        void * t46 = (void *)(((uint8_t *)t45) + 8);
                        gm_t88rcucg1b7uk t47 = gm_l6;
                        uint8_t t48 = gm_f_is_vector_gkq6b88(t46, GM_CONV(gm_csguankko4lmhd, t47));
                        if (!t48) {
                            goto gm_lb4;
                        }
                        void * t49 = gm_l0;
                        gm_t88rcucg1b7uk t50 = gm_l6;
                        gm_b16_8 t51 = gm_l3;
                        gm_tl874t2atnlh3 t52 = gm_l4;
                        void * t53 = gm_f_vector_load_ssdvomk((void *)NULL, cx, NULL, t49, t50, t51, t52);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t53;
                        goto gm_fret;
                    }
gm_lb4:;
                }
gm_lb3:;
                {
                    {
                        {
                            void * t54 = gm_l0;
                            void * t55 = GM_LD(void *, ((uint8_t *)t54));
                            gm_t88rcucg1b7uk t56 = gm_l6;
                            gm_f_soa_info_pq4q43l((void *)&gm_l27, t55, GM_CONV(gm_csguankko4lmhd, t56));
                        }
                        uint8_t t57 = (uint8_t)((((uint8_t *)&gm_l27))[32] != 0);
                        if (!t57) {
                            goto gm_lb6;
                        }
                        {
                            memmove(((uint8_t *)&gm_l7), ((uint8_t *)&gm_l27), 32);
                        }
                        gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                        gm_b16_8 t59;
                        GM_ST(void *, (uint8_t *)&t59, (void *)(gm_sgb0oed9c3voo));
                        GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(4));
                        uint8_t t60 = (gm_eq0iqiubfm7e(((uint8_t *)&t58), ((uint8_t *)&t59)));
                        if (t60) {
                            void * t61 = gm_l0;
                            gm_t88rcucg1b7uk t62 = gm_l6;
                            gm_b16_8 t63 = gm_l3;
                            gm_tl874t2atnlh3 t64 = gm_l4;
                            void * t65 = gm_f_soa_make_5imjrl1((void *)NULL, cx, NULL, t61, t62, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l7)), t63, t64);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t65;
                            goto gm_fret;
                        }
                        goto gm_lb5;
                    }
gm_lb6:;
                }
gm_lb5:;
                {
                    void * t66 = gm_l0;
                    gm_t88rcucg1b7uk t67 = gm_l6;
                    gm_b16_8 t68 = gm_l3;
                    gm_th2lqbo77pvsc t69;
                    GM_ST(uint8_t, (uint8_t *)&t69 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t69 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t69 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t69), t68);
                    ((uint8_t *)&t69)[16] = 1;
                    gm_tl874t2atnlh3 t70 = gm_l4;
                    void * t71 = gm_f_construct_variant_o6is81c((void *)NULL, cx, NULL, t66, t67, &(*(gm_t08olq97rscn3 *)((uint8_t *)a2)), &t69, t70);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t71;
                    goto gm_fret;
                }
            }
gm_lb2:;
        }
gm_lb1:;
    }
    {
        void * t72 = gm_l0;
        void * t73 = gm_l1;
        gm_tkn42u7na3t81 t74;
        memset(&t74, 0, sizeof t74);
        void * t75 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t72, t73, t74);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t75;
        void * t76 = gm_l0;
        void * t77 = (void *)(((uint8_t *)t76) + 96);
        void * t78 = gm_l0;
        void * t79 = GM_LD(void *, ((uint8_t *)t78));
        void * t80 = gm_l8;
        gm_t88rcucg1b7uk t81 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t80));
        gm_t88rcucg1b7uk t82 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t77, t79, t81);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t82;
        void * t83 = gm_l5;
        gm_t88rcucg1b7uk t84 = gm_l9;
        uint8_t t85 = gm_f_is_error_8k27gd1(t83, GM_CONV(gm_csguankko4lmhd, t84));
        if (t85) {
            void * t86 = gm_l0;
            void * t87 = GM_LD(void *, ((uint8_t *)t86));
            gm_tl874t2atnlh3 t88 = gm_l4;
            void * t89 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t87, t88);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t89;
            goto gm_fret;
        }
        void * t90 = gm_l0;
        void * t91 = gm_l8;
        void * t92 = gm_f_soa_deref_uiefo9t((void *)NULL, cx, NULL, t90, t91);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t92;
        {
            {
                {
                    void * t93 = gm_l0;
                    void * t94 = gm_l10;
                    gm_f_soa_of_jufdcj2((void *)&gm_l28, (void *)NULL, cx, NULL, t93, t94);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t95 = (uint8_t)((((uint8_t *)&gm_l28))[32] != 0);
                if (!t95) {
                    goto gm_lb8;
                }
                {
                    memmove(((uint8_t *)&gm_l11), ((uint8_t *)&gm_l28), 32);
                }
                gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                gm_b16_8 t97;
                GM_ST(void *, (uint8_t *)&t97, (void *)(gm_srv4cua6q51nq));
                GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(4));
                uint8_t t98 = (gm_eq0iqiubfm7e(((uint8_t *)&t96), ((uint8_t *)&t97)));
                if (t98) {
                    void * t99 = gm_l0;
                    void * t100 = gm_l10;
                    gm_b16_8 t101 = gm_l3;
                    gm_tl874t2atnlh3 t102 = gm_l4;
                    void * t103 = gm_f_soa_free_9554eha((void *)NULL, cx, NULL, t99, t100, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l11)), t101, t102);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t103;
                    goto gm_fret;
                }
                gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                gm_b16_8 t105;
                GM_ST(void *, (uint8_t *)&t105, (void *)(gm_sgvvkov5kvprt));
                GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)(9));
                uint8_t t106 = (gm_eq0iqiubfm7e(((uint8_t *)&t104), ((uint8_t *)&t105)));
                if (t106) {
                    void * t107 = gm_l0;
                    void * t108 = gm_l10;
                    gm_b16_8 t109 = gm_l3;
                    gm_tl874t2atnlh3 t110 = gm_l4;
                    void * t111 = gm_f_soa_copy_from_vrsguuh((void *)NULL, cx, NULL, t107, t108, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l11)), t109, t110);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t111;
                    goto gm_fret;
                }
                {
                    void * t112 = gm_l0;
                    gm_tl874t2atnlh3 t113 = gm_l4;
                    void * t114 = gm_l0;
                    void * t115 = gm_l0;
                    void * t116 = (void *)(((uint8_t *)t115) + 96);
                    void * t117 = gm_l0;
                    void * t118 = GM_LD(void *, ((uint8_t *)t117));
                    void * t119 = gm_l10;
                    gm_t88rcucg1b7uk t120 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t119));
                    gm_t88rcucg1b7uk t121 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t116, t118, t120);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t122 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t114, t121);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t123 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                    gm_t6j6smj7qoctu t124;
                    GM_ST(gm_b16_8, ((uint8_t *)&t124 + 0), t122);
                    GM_ST(gm_b16_8, ((uint8_t *)&t124 + 16), t123);
                    gm_f_format_mpl2gek((void *)&gm_l29, (void *)NULL, cx, NULL, &t124);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t125 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                    if (!t125) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 8, gm_srcngug8co99ok1, 24, 24);
                    gm_b16_8 t126 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l29) + 8));
                    gm_b16_8 t127 = GM_LD(gm_b16_8, ((uint8_t *)&t126));
                    gm_b0_1 t128 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t112, t113, t127);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                void * t129 = gm_l0;
                void * t130 = GM_LD(void *, ((uint8_t *)t129));
                gm_tl874t2atnlh3 t131 = gm_l4;
                void * t132 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t130, t131);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t132;
                goto gm_fret;
            }
gm_lb8:;
        }
gm_lb7:;
    }
    {
        gm_t88rcucg1b7uk t133 = gm_l9;
        gm_l12 = t133;
        {
            {
                void * t134 = gm_l5;
                void * t135 = gm_l0;
                void * t136 = (void *)(((uint8_t *)t135) + 96);
                void * t137 = gm_l5;
                void * t138 = gm_l8;
                gm_t88rcucg1b7uk t139 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t138));
                gm_t88rcucg1b7uk t140 = gm_f_shallow_8ur803a((void *)NULL, cx, NULL, t136, t137, t139);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_f_kind_jcnt4hm((void *)&gm_l30, t134, GM_CONV(gm_csguankko4lmhd, t140));
                uint8_t t141 = GM_LD(uint8_t, (((uint8_t *)&gm_l30) + 0));
                uint8_t t142 = (uint8_t)(t141 == ((uint8_t)8ULL));
                if (!t142) {
                    goto gm_lb10;
                }
                gm_t88rcucg1b7uk t143 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l30) + 8) + 4));
                gm_l13 = t143;
                gm_t88rcucg1b7uk t144 = gm_l13;
                gm_l12 = t144;
                goto gm_lb9;
            }
gm_lb10:;
        }
gm_lb9:;
        {
            {
                void * t145 = gm_l0;
                void * t146 = GM_LD(void *, ((uint8_t *)t145));
                gm_t88rcucg1b7uk t147 = gm_l12;
                gm_f_struct_fields_chqu3it((void *)&gm_l31, t146, GM_CONV(gm_csguankko4lmhd, t147));
                uint8_t t148 = (uint8_t)((((uint8_t *)&gm_l31))[16] != 0);
                if (!t148) {
                    goto gm_lb12;
                }
                gm_b16_8 t149 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l31));
                gm_l14 = t149;
                gm_b16_8 t150 = gm_l14;
                gm_b16_8 t151 = GM_LD(gm_b16_8, ((uint8_t *)&t150));
                gm_l15 = t151;
                gm_l16 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t152 = gm_l16;
                        uint64_t t153 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                        uint8_t t154 = (t152 < t153);
                        if (!t154) {
                            goto gm_lb13;
                        }
                        {
                            {
                                uint64_t t155 = gm_l16;
                                void * t156 = GM_LD(void *, ((uint8_t *)&gm_l15));
                                uint64_t t157 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                uint64_t t158 = gm_l16;
                                gm_t0mglksrkfa5e t159 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)t156) + (uintptr_t)t158 * 40));
                                GM_ST(uint64_t, ((uint8_t *)&gm_l32 + 0), t155);
                                GM_ST(gm_t0mglksrkfa5e, ((uint8_t *)&gm_l32 + 8), t159);
                            }
                            {
                                {
                                    uint64_t t160 = GM_LD(uint64_t, ((uint8_t *)&gm_l32));
                                    gm_l17 = t160;
                                    memmove(((uint8_t *)&F.l18), (((uint8_t *)&gm_l32) + 8), 40);
                                    gm_b16_8 t161 = GM_LD(gm_b16_8, ((uint8_t *)&F.l18));
                                    gm_b16_8 t162 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                                    uint8_t t163 = (gm_eq0iqiubfm7e(((uint8_t *)&t161), ((uint8_t *)&t162)));
                                    if (t163) {
                                        gm_t88rcucg1b7uk t164 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l18) + 16));
                                        void * t165 = gm_l0;
                                        void * t166 = gm_l8;
                                        void * t167 = gm_f_auto_deref_vdb1hdn((void *)NULL, cx, NULL, t165, t166);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint64_t t168 = gm_l17;
                                        uint32_t t169 = (uint32_t)t168;
                                        gm_tbl8il92l97ut t170;
                                        GM_ST(uint32_t, (uint8_t *)&t170 + 12, 0);
                                        GM_ST(void *, ((uint8_t *)&t170 + 0), t167);
                                        GM_ST(uint32_t, ((uint8_t *)&t170 + 8), t169);
                                        gm_tl874t2atnlh3 t171 = gm_l4;
                                        gm_to0f5qn75cgg8 t172;
                                        memset(&t172, 0, sizeof t172);
                                        GM_ST(uint8_t, ((uint8_t *)&t172 + 0), ((uint8_t)19ULL));
                                        GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t172 + 8), t170);
                                        void * t173 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t164, &t172, t171);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l19 = t173;
                                        void * t174 = gm_l0;
                                        void * t175 = gm_l19;
                                        gm_b16_8 t176 = gm_l3;
                                        gm_tl874t2atnlh3 t177 = gm_l4;
                                        void * t178 = gm_f_call_value_t2nv21i((void *)NULL, cx, NULL, t174, t175, t176, t177);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.ret = t178;
                                        goto gm_fret;
                                    }
                                    goto gm_lb16;
                                }
gm_lb17:;
                                gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 31, 9);
                            }
gm_lb16:;
                        }
gm_lb15:;
                        uint64_t t179 = gm_l16;
                        uint64_t r180; bool o181 = __builtin_add_overflow(t179, ((uint64_t)1ULL), &r180);
                        uint64_t t182 = r180;
                        gm_l16 = t182;
                    }
                }
gm_lb13:;
                goto gm_lb11;
            }
gm_lb12:;
        }
gm_lb11:;
    }
    {
        {
            {
                void * t183 = gm_l0;
                void * t184 = gm_l8;
                gm_t88rcucg1b7uk t185 = gm_l9;
                gm_b16_8 t186 = gm_l3;
                gm_tl874t2atnlh3 t187 = gm_l4;
                void * t188 = gm_f_builtin_method_jist12a((void *)NULL, cx, NULL, t183, t184, t185, &(*(gm_t08olq97rscn3 *)((uint8_t *)a2)), t186, t187);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l33 = t188;
                uint8_t t189 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l33)) != 0);
                if (!t189) {
                    goto gm_lb19;
                }
                void * t190 = GM_LD(void *, ((uint8_t *)&gm_l33));
                gm_l20 = t190;
                void * t191 = gm_l20;
                F.ret = t191;
                goto gm_fret;
            }
gm_lb19:;
        }
gm_lb18:;
        memmove(((uint8_t *)&gm_l21), ((uint8_t *)a2), 32);
        gm_l22 = ((uint8_t)0);
        {
            {
                void * t192 = gm_l0;
                void * t193 = GM_LD(void *, ((uint8_t *)t192));
                gm_t88rcucg1b7uk t194 = gm_l9;
                gm_thetuhtouve7f t195 = gm_f_range_of_dmmo768((void *)NULL, cx, NULL, t193, t194);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l34 = t195;
                uint8_t t196 = (uint8_t)((((uint8_t *)&gm_l34))[8] != 0);
                if (!t196) {
                    goto gm_lb21;
                }
                gm_tbngattl1fo51 t197 = GM_LD(gm_tbngattl1fo51, ((uint8_t *)&gm_l34));
                gm_l23 = t197;
                gm_b16_8 t198 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                gm_b16_8 t199;
                GM_ST(void *, (uint8_t *)&t199, (void *)(gm_sm2k5tp7dtg07));
                GM_ST(uintptr_t, (uint8_t *)&t199 + 8, (uintptr_t)(3));
                uint8_t t200 = (gm_eq0iqiubfm7e(((uint8_t *)&t198), ((uint8_t *)&t199)));
                if (t200) {
                    uint8_t t201 = GM_LD(uint8_t, ((uint8_t *)&gm_l23));
                    if (t201) {
                        gm_b16_8 t202;
                        GM_ST(void *, (uint8_t *)&t202, (void *)(gm_ss226ibsv86g3));
                        GM_ST(uintptr_t, (uint8_t *)&t202 + 8, (uintptr_t)(14));
                        gm_l35 = t202;
                    } else {
                        gm_b16_8 t203;
                        GM_ST(void *, (uint8_t *)&t203, (void *)(gm_selatfetsf85i));
                        GM_ST(uintptr_t, (uint8_t *)&t203 + 8, (uintptr_t)(9));
                        gm_l35 = t203;
                    }
                    gm_b16_8 t204 = gm_l35;
                    gm_tl874t2atnlh3 t205 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
                    GM_ST(uint32_t, (uint8_t *)&gm_l21 + 28, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&gm_l21 + 0), t204);
                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&gm_l21 + 16), t205);
                    gm_l22 = ((uint8_t)1);
                }
                goto gm_lb20;
            }
gm_lb21:;
        }
gm_lb20:;
        {
            {
                void * t206 = gm_l0;
                void * t207 = gm_l8;
                uint8_t t208 = gm_l22;
                gm_b16_8 t209 = gm_l3;
                gm_tl874t2atnlh3 t210 = gm_l4;
                void * t211 = gm_f_module_method_cb3lmfk((void *)NULL, cx, NULL, t206, t207, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l21)), t208, t209, t210);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l36 = t211;
                uint8_t t212 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l36)) != 0);
                if (!t212) {
                    goto gm_lb23;
                }
                void * t213 = GM_LD(void *, ((uint8_t *)&gm_l36));
                gm_l24 = t213;
                void * t214 = gm_l24;
                F.ret = t214;
                goto gm_fret;
            }
gm_lb23:;
        }
gm_lb22:;
        gm_tl874t2atnlh3 t215 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
        void * t216 = gm_l0;
        void * t217 = gm_l0;
        void * t218 = (void *)(((uint8_t *)t217) + 96);
        void * t219 = gm_l0;
        void * t220 = GM_LD(void *, ((uint8_t *)t219));
        gm_t88rcucg1b7uk t221 = gm_l9;
        gm_t88rcucg1b7uk t222 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t218, t220, t221);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t223 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t216, t222);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t224 = GM_LD(gm_b16_8, ((uint8_t *)a2));
        gm_t6j6smj7qoctu t225;
        GM_ST(gm_b16_8, ((uint8_t *)&t225 + 0), t223);
        GM_ST(gm_b16_8, ((uint8_t *)&t225 + 16), t224);
        gm_f_format_uesfu03((void *)&gm_l37, (void *)NULL, cx, NULL, &t225);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t226 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
        if (!t226) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 8, gm_srcngug8co99ok1, 51, 32);
        gm_b16_8 t227 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l37) + 8));
        gm_b16_8 t228 = GM_LD(gm_b16_8, ((uint8_t *)&t227));
        gm_f_error_s6hn871((void *)&F.l25, (void *)NULL, cx, NULL, t215, t228);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t229 = gm_l0;
        void * t230 = gm_l1;
        gm_tl874t2atnlh3 t231 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t230) + 72));
        gm_tl874t2atnlh3 t232 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
        gm_b0_1 t233 = gm_f_emit_with_continuation_n_lv5ofqt((void *)NULL, cx, NULL, t229, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l25)), t231, t232);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t234 = gm_l0;
        void * t235 = GM_LD(void *, ((uint8_t *)t234));
        gm_tl874t2atnlh3 t236 = gm_l4;
        void * t237 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t235, t236);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t237;
        goto gm_fret;
    }
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_module_method_cb3lmfk(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint8_t a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_module_method_cb3lmfk F;
    void * gm_l0; /* cx */
    void * gm_l1; /* recv */
    gm_t08olq97rscn3 gm_l2; /* name */
    uint8_t gm_l3; /* internal */
    gm_b16_8 gm_l4; /* args */
    gm_tl874t2atnlh3 gm_l5; /* span */
    void * gm_l6; /* tys */
    gm_t88rcucg1b7uk gm_l7; /* rty */
    gm_t88rcucg1b7uk gm_l8; /* s */
    gm_t88rcucg1b7uk gm_l9; /* p */
    uint32_t gm_l10; /* id */
    uint64_t gm_l11; /* module */
    uint64_t gm_l12; /* d */
    uint32_t gm_l13; /* f */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    void * gm_l20; /* e */
    void * gm_l22; /* r */
    gm_t6j6smj7qoctu gm_l23; /* bound */
    void * gm_l25; /* call */
    GM_STACK_CHECK(&F, 2319u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&gm_l2, a2, sizeof gm_l2);
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_t53at80pfdus3 gm_l26;
    gm_t53at80pfdus3 gm_l27;
    gm_tf5srh4mp4b3i gm_l28;
    gm_t6qooom8qa2do gm_l29;
    uint8_t gm_l30;
    void * gm_l31;
    gm_tabb710d9mq2e gm_l32;
    void * gm_l33;
    gm_ttu09lij36f8c gm_l34;
    gm_tftb1odc739t5 gm_l35;
    gm_tftb1odc739t5 gm_l36;
    {
        void * t22 = gm_l0;
        void * t23 = GM_LD(void *, ((uint8_t *)t22));
        void * t24 = (void *)(((uint8_t *)t23) + 8);
        gm_l6 = t24;
        void * t25 = gm_l0;
        void * t26 = (void *)(((uint8_t *)t25) + 96);
        void * t27 = gm_l0;
        void * t28 = GM_LD(void *, ((uint8_t *)t27));
        void * t29 = gm_l1;
        gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t29));
        gm_t88rcucg1b7uk t31 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t26, t28, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t31;
        gm_t88rcucg1b7uk t32 = gm_l7;
        gm_l8 = t32;
        {
            {
                void * t33 = gm_l6;
                gm_t88rcucg1b7uk t34 = gm_l7;
                gm_f_kind_jcnt4hm((void *)&gm_l26, t33, GM_CONV(gm_csguankko4lmhd, t34));
                uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                uint8_t t36 = (uint8_t)(t35 == ((uint8_t)8ULL));
                if (!t36) {
                    goto gm_lb2;
                }
                gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l26) + 8) + 4));
                gm_l9 = t37;
                gm_t88rcucg1b7uk t38 = gm_l9;
                gm_l8 = t38;
                goto gm_lb1;
            }
gm_lb2:;
        }
gm_lb1:;
        {
            {
                {
                    {
                        void * t39 = gm_l6;
                        gm_t88rcucg1b7uk t40 = gm_l8;
                        gm_f_kind_jcnt4hm((void *)&gm_l27, t39, GM_CONV(gm_csguankko4lmhd, t40));
                        uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l27) + 0));
                        uint8_t t42 = (uint8_t)(t41 == ((uint8_t)17ULL));
                        if (!t42) {
                            goto gm_lb4;
                        }
                        uint32_t t43 = GM_LD(uint32_t, (((uint8_t *)&gm_l27) + 8));
                        gm_l10 = t43;
                        void * t44 = gm_l0;
                        void * t45 = GM_LD(void *, ((uint8_t *)t44));
                        void * t46 = GM_LD(void *, (((uint8_t *)t45) + 1088));
                        uint64_t t47 = GM_LD(uint64_t, ((((uint8_t *)t45) + 1088) + 8));
                        uint32_t t48 = gm_l10;
                        uint64_t t49 = (uint64_t)t48;
                        if ((uintptr_t)t49 >= (uintptr_t)(t47)) gm_trap_index((uintptr_t)t49, t47, gm_srcngug8co99ok1, 512, 15);
                        uint64_t t50 = GM_LD(uint64_t, ((((uint8_t *)t46) + (uintptr_t)t49 * 184) + 16));
                        gm_l11 = t50;
                        void * t51 = gm_l0;
                        void * t52 = GM_LD(void *, ((uint8_t *)t51));
                        void * t53 = GM_LD(void *, ((uint8_t *)t52));
                        uint64_t t54 = gm_l11;
                        gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
                        gm_tf5srh4mp4b3i t56 = gm_f_lookup_59juqoo(t53, t54, t55);
                        gm_l28 = t56;
                        uint8_t t57 = (uint8_t)((((uint8_t *)&gm_l28))[8] != 0);
                        if (t57) {
                            uint64_t t58 = GM_LD(uint64_t, ((uint8_t *)&gm_l28));
                            gm_l12 = t58;
                        } else {
                            void * t59 = gm_l0;
                            void * t60 = gm_l1;
                            gm_t88rcucg1b7uk t61 = gm_l7;
                            gm_t88rcucg1b7uk t62 = gm_l8;
                            uint64_t t63 = gm_l11;
                            gm_b16_8 t64 = gm_l4;
                            gm_tl874t2atnlh3 t65 = gm_l5;
                            void * t66 = gm_f_op_method_82pb0m6((void *)NULL, cx, NULL, t59, t60, t61, t62, t63, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l2)), t64, t65);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t66;
                            goto gm_fret;
                        }
                        {
                            {
                                {
                                    void * t67 = gm_l0;
                                    void * t68 = GM_LD(void *, ((uint8_t *)t67));
                                    uint64_t t69 = gm_l12;
                                    gm_f_resolve_decl_pus042v((void *)&gm_l29, (void *)NULL, cx, NULL, t68, t69);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l29) + 0));
                                uint8_t t71 = (uint8_t)(t70 == ((uint8_t)5ULL));
                                if (!t71) {
                                    goto gm_lb6;
                                }
                                uint32_t t72 = GM_LD(uint32_t, (((uint8_t *)&gm_l29) + 8));
                                gm_l13 = t72;
                                {
                                    {
                                        uint8_t t73 = gm_l3;
                                        uint8_t t74 = (uint8_t)!t73;
                                        if (!t74) {
                                            goto gm_lb8;
                                        }
                                        void * t75 = gm_l0;
                                        void * t76 = GM_LD(void *, ((uint8_t *)t75));
                                        uint64_t t77 = gm_l12;
                                        void * t78 = gm_l0;
                                        uint64_t t79 = GM_LD(uint64_t, (((uint8_t *)t78) + 8));
                                        uint8_t t80 = gm_f_decl_visible_bd168ti(t76, t77, t79);
                                        uint8_t t81 = (uint8_t)!t80;
                                        if (!t81) {
                                            goto gm_lb8;
                                        }
                                        void * t82 = gm_l0;
                                        void * t83 = GM_LD(void *, ((uint8_t *)t82));
                                        uint64_t t84 = gm_l11;
                                        void * t85 = gm_l0;
                                        uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)t85) + 8));
                                        gm_tf5srh4mp4b3i t87 = gm_f_module_member_5fd2qm9((void *)NULL, cx, NULL, t83, t84, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l2)), t86);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        void * t88 = gm_l0;
                                        void * t89 = GM_LD(void *, ((uint8_t *)t88));
                                        gm_tl874t2atnlh3 t90 = gm_l5;
                                        void * t91 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t89, t90);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.ret = t91;
                                        goto gm_fret;
                                    }
gm_lb8:;
                                }
gm_lb7:;
                                void * t92 = gm_l0;
                                void * t93 = GM_LD(void *, ((uint8_t *)t92));
                                uint64_t t94 = gm_l12;
                                void * t95 = gm_l0;
                                uint64_t t96 = GM_LD(uint64_t, (((uint8_t *)t95) + 8));
                                gm_b0_1 t97 = gm_f_note_use_from_2lmujsq((void *)NULL, cx, NULL, t93, t94, t96);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                {
                                    void * t98 = gm_l0;
                                    void * t99 = GM_LD(void *, ((uint8_t *)t98));
                                    void * t100 = GM_LD(void *, (((uint8_t *)t99) + 344));
                                    uint64_t t101 = GM_LD(uint64_t, ((((uint8_t *)t99) + 344) + 8));
                                    uint32_t t102 = gm_l13;
                                    uint64_t t103 = (uint64_t)t102;
                                    if ((uintptr_t)t103 >= (uintptr_t)(t101)) gm_trap_index((uintptr_t)t103, t101, gm_srcngug8co99ok1, 520, 12);
                                    memmove(((uint8_t *)&F.l14), (((uint8_t *)t100) + (uintptr_t)t103 * 136), 136);
                                }
                                {
                                    memset(&F.l15, 0, sizeof F.l15);
                                }
                                gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 40));
                                gm_l16 = t104;
                                gm_l17 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t105 = gm_l17;
                                        uint64_t t106 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                                        uint8_t t107 = (t105 < t106);
                                        if (!t107) {
                                            goto gm_lb9;
                                        }
                                        {
                                            void * t108 = GM_LD(void *, ((uint8_t *)&gm_l16));
                                            uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                                            uint64_t t110 = gm_l17;
                                            {
                                                {
                                                    memmove(((uint8_t *)&F.l18), (((uint8_t *)t108) + (uintptr_t)t110 * 56), 56);
                                                    {
                                                        {
                                                            uint8_t t111 = (uint8_t)(((((uint8_t *)&F.l18) + 40))[4] != 0);
                                                            if (t111) {
                                                                goto gm_lb15;
                                                            }
                                                            gm_l30 = ((uint8_t)1);
                                                            goto gm_lb14;
                                                        }
gm_lb15:;
                                                        {
                                                            gm_l30 = ((uint8_t)0);
                                                            goto gm_lb14;
                                                        }
gm_lb16:;
                                                        gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 525, 12);
                                                    }
gm_lb14:;
                                                    uint8_t t112 = gm_l30;
                                                    if (t112) {
                                                        {
                                                            gm_t5v0c8ia6a5mo t113 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)&F.l18));
                                                            GM_ST(uint8_t, (uint8_t *)&F.l15 + 57, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&F.l15 + 58, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&F.l15 + 60, 0);
                                                            GM_ST(gm_t5v0c8ia6a5mo, ((uint8_t *)&F.l15), t113);
                                                            ((uint8_t *)&F.l15)[56] = 1;
                                                        }
                                                        goto gm_lb9;
                                                    }
                                                    goto gm_lb12;
                                                }
gm_lb13:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 524, 5);
                                            }
gm_lb12:;
                                        }
gm_lb11:;
                                        uint64_t t114 = gm_l17;
                                        uint64_t r115; bool o116 = __builtin_add_overflow(t114, ((uint64_t)1ULL), &r115);
                                        uint64_t t117 = r115;
                                        gm_l17 = t117;
                                    }
                                }
gm_lb9:;
                                {
                                    {
                                        uint8_t t118 = (uint8_t)((((uint8_t *)&F.l15))[56] != 0);
                                        if (!t118) {
                                            goto gm_lb18;
                                        }
                                        memmove(((uint8_t *)&F.l19), ((uint8_t *)&F.l15), 56);
                                        void * t119 = gm_l6;
                                        gm_t88rcucg1b7uk t120 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l19) + 16));
                                        gm_t88rcucg1b7uk t121 = gm_l8;
                                        uint8_t t122 = gm_f_takes_receiver_uhu9min(t119, GM_CONV(gm_csguankko4lmhd, t120), GM_CONV(gm_csguankko4lmhd, t121));
                                        uint8_t t123 = (uint8_t)!t122;
                                        if (t123) {
                                            {
                                                {
                                                    void * t124 = gm_l0;
                                                    void * t125 = gm_l1;
                                                    gm_t88rcucg1b7uk t126 = gm_l7;
                                                    gm_t88rcucg1b7uk t127 = gm_l8;
                                                    uint64_t t128 = gm_l11;
                                                    gm_b16_8 t129 = gm_l4;
                                                    gm_tl874t2atnlh3 t130 = gm_l5;
                                                    void * t131 = gm_f_op_method_82pb0m6((void *)NULL, cx, NULL, t124, t125, t126, t127, t128, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l2)), t129, t130);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_l31 = t131;
                                                    uint8_t t132 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l31)) != 0);
                                                    if (!t132) {
                                                        goto gm_lb20;
                                                    }
                                                    void * t133 = GM_LD(void *, ((uint8_t *)&gm_l31));
                                                    gm_l20 = t133;
                                                    void * t134 = gm_l20;
                                                    F.ret = t134;
                                                    goto gm_fret;
                                                }
gm_lb20:;
                                            }
gm_lb19:;
                                        }
                                        goto gm_lb17;
                                    }
gm_lb18:;
                                }
gm_lb17:;
                                uint8_t t135 = (uint8_t)((((uint8_t *)&F.l15))[56] != 0);
                                if (t135) {
                                    memmove(((uint8_t *)&F.l21), ((uint8_t *)&F.l15), 56);
                                } else {
                                    void * t136 = gm_l0;
                                    gm_tl874t2atnlh3 t137 = gm_l5;
                                    gm_b16_8 t138 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
                                    gm_tnpifv20irnph t139;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t139 + 0), t138);
                                    gm_f_format_4frhu1b((void *)&gm_l32, (void *)NULL, cx, NULL, t139);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t140 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                                    if (!t140) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 8, gm_srcngug8co99ok1, 536, 24);
                                    gm_b16_8 t141 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l32) + 8));
                                    gm_b16_8 t142 = GM_LD(gm_b16_8, ((uint8_t *)&t141));
                                    gm_b0_1 t143 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t136, t137, t142);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    void * t144 = gm_l0;
                                    void * t145 = GM_LD(void *, ((uint8_t *)t144));
                                    gm_tl874t2atnlh3 t146 = gm_l5;
                                    void * t147 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t145, t146);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.ret = t147;
                                    goto gm_fret;
                                }
                                {
                                    void * t148 = gm_l0;
                                    void * t149 = gm_l1;
                                    gm_t88rcucg1b7uk t150 = gm_l7;
                                    gm_t88rcucg1b7uk t151 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l21) + 16));
                                    gm_tl874t2atnlh3 t152 = gm_l5;
                                    void * t153 = gm_f_adjust_receiver_2j7250l((void *)NULL, cx, NULL, t148, t149, t150, t151, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l2)), t152);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l33 = t153;
                                }
                                uint8_t t154 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l33)) != 0);
                                if (t154) {
                                    void * t155 = GM_LD(void *, ((uint8_t *)&gm_l33));
                                    gm_l22 = t155;
                                } else {
                                    void * t156 = gm_l0;
                                    void * t157 = GM_LD(void *, ((uint8_t *)t156));
                                    gm_tl874t2atnlh3 t158 = gm_l5;
                                    void * t159 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t157, t158);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.ret = t159;
                                    goto gm_fret;
                                }
                                uint64_t t160 = GM_LD(uint64_t, ((((uint8_t *)&F.l14) + 80) + 8));
                                uint8_t t161 = (t160 > ((uint64_t)0ULL));
                                if (t161) {
                                    void * t162 = gm_l0;
                                    uint32_t t163 = gm_l13;
                                    void * t164 = gm_l22;
                                    gm_b16_8 t165 = gm_l4;
                                    gm_tl874t2atnlh3 t166 = gm_l5;
                                    void * t167 = gm_f_generic_call_2tlofdi((void *)NULL, cx, NULL, t162, t163, t164, t165, t166);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.ret = t167;
                                    goto gm_fret;
                                }
                                {
                                    void * t168 = gm_l0;
                                    void * t169 = GM_LD(void *, (((uint8_t *)&F.l14) + 40));
                                    uint64_t t170 = GM_LD(uint64_t, ((((uint8_t *)&F.l14) + 40) + 8));
                                    uint64_t t171 = (uintptr_t)((uint64_t)1ULL);
                                    uint64_t t172 = (uintptr_t)t170;
                                    if (t171 > t172 || t172 > (uintptr_t)t170) gm_trap_slice(t171, t172, t170, gm_srcngug8co99ok1, 541, 27);
                                    uint64_t t173 = ((uintptr_t)t170) - (uintptr_t)((uint64_t)1ULL);
                                    gm_b16_8 t174;
                                    GM_ST(void *, (uint8_t *)&t174, (void *)(((uint8_t *)t169 + (uintptr_t)((uint64_t)1ULL) * 56)));
                                    GM_ST(uintptr_t, (uint8_t *)&t174 + 8, (uintptr_t)(t173));
                                    gm_b16_8 t175 = GM_LD(gm_b16_8, ((uint8_t *)&t174));
                                    uint64_t t176 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 32));
                                    gm_b16_8 t177 = gm_l4;
                                    gm_tl874t2atnlh3 t178 = gm_l5;
                                    gm_b16_8 t179;
                                    GM_ST(void *, (uint8_t *)&t179, (void *)(gm_sg7of7eks8a5u));
                                    GM_ST(uintptr_t, (uint8_t *)&t179 + 8, (uintptr_t)(6));
                                    gm_f_bind_args_dnih8ih((void *)&gm_l34, (void *)NULL, cx, NULL, t168, t175, t176, t177, t178, t179);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                uint8_t t180 = (uint8_t)((((uint8_t *)&gm_l34))[32] != 0);
                                if (t180) {
                                    memmove(((uint8_t *)&gm_l23), ((uint8_t *)&gm_l34), 32);
                                } else {
                                    void * t181 = gm_l0;
                                    void * t182 = GM_LD(void *, ((uint8_t *)t181));
                                    gm_tl874t2atnlh3 t183 = gm_l5;
                                    void * t184 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t182, t183);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.ret = t184;
                                    goto gm_fret;
                                }
                                {
                                    gm_trqfvmjtt4sij t185;
                                    gm_f_list_vp1djv1_2((void *)&t185, (void *)NULL, cx, NULL);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.l24 = t185;
                                }
                                {
                                    void * t186 = (void *)((uint8_t *)&F.l24);
                                    void * t187 = gm_l22;
                                    gm_tftb1odc739t5 t188 = gm_f_push_d943vgp_2(t186, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t187)));
                                    gm_l35 = t188;
                                }
                                uint8_t t189 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
                                if (!t189) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 1, gm_srcngug8co99ok1, 543, 5);
                                void * t190 = (void *)((uint8_t *)&F.l24);
                                gm_b16_8 t191 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l23));
                                gm_b16_8 t192 = GM_LD(gm_b16_8, ((uint8_t *)&t191));
                                gm_tftb1odc739t5 t193 = gm_f_extend_gig5o6g(t190, t192);
                                gm_l36 = t193;
                                uint8_t t194 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
                                if (!t194) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 1, gm_srcngug8co99ok1, 544, 5);
                                void * t195 = gm_l0;
                                void * t196 = (void *)(((uint8_t *)&F.l14) + 64);
                                gm_tl874t2atnlh3 t197 = gm_l5;
                                gm_b0_1 t198 = gm_f_record_effects_0eel5vu((void *)NULL, cx, NULL, t195, t196, t197);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                {
                                    gm_t88rcucg1b7uk t199 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l14) + 56));
                                    uint32_t t200 = gm_l13;
                                    gm_b16_8 t201 = GM_LD(gm_b16_8, ((uint8_t *)&F.l24));
                                    gm_tsth9qu1dv0g3 t202;
                                    memset(&t202, 0, sizeof t202);
                                    GM_ST(uint8_t, ((uint8_t *)&t202 + 0), ((uint8_t)0ULL));
                                    GM_ST(uint32_t, ((uint8_t *)&t202 + 8), t200);
                                    gm_to76jli3bqddq t203;
                                    GM_ST(gm_tsth9qu1dv0g3, ((uint8_t *)&t203 + 0), t202);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t203 + 40), t201);
                                    gm_tl874t2atnlh3 t204 = gm_l5;
                                    gm_to0f5qn75cgg8 t205;
                                    GM_ST(uint8_t, (uint8_t *)&t205 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t205 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t205 + 4, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t205 + 0), ((uint8_t)34ULL));
                                    GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t205 + 8), t203);
                                    void * t206 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t199, &t205, t204);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l25 = t206;
                                }
                                void * t207 = gm_l0;
                                gm_b16_8 t208 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 16));
                                void * t209 = gm_l25;
                                void * t210 = gm_f_finish_call_kjknkfg((void *)NULL, cx, NULL, t207, t208, t209, ((uint8_t)1));
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t210;
                                goto gm_fret;
                            }
gm_lb6:;
                            void * t211 = gm_l0;
                            void * t212 = gm_l1;
                            gm_t88rcucg1b7uk t213 = gm_l7;
                            gm_t88rcucg1b7uk t214 = gm_l8;
                            uint64_t t215 = gm_l11;
                            gm_b16_8 t216 = gm_l4;
                            gm_tl874t2atnlh3 t217 = gm_l5;
                            void * t218 = gm_f_op_method_82pb0m6((void *)NULL, cx, NULL, t211, t212, t213, t214, t215, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l2)), t216, t217);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t218;
                            goto gm_fret;
                        }
gm_lb5:;
                    }
                }
gm_lb4:;
            }
            {
                F.ret = ((void *)0);
                goto gm_fret;
            }
        }
gm_lb3:;
    }
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_numeric_conversion_1ovq7at(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, uint8_t a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_numeric_conversion_1ovq7at F;
    void * gm_l0; /* cx */
    void * gm_l1; /* recv */
    gm_b16_8 gm_l2; /* n */
    uint8_t gm_l3; /* is_float */
    gm_b16_8 gm_l4; /* args */
    gm_tl874t2atnlh3 gm_l5; /* span */
    void * gm_l6; /* tys */
    gm_t88rcucg1b7uk gm_l7; /* t */
    gm_t8588ma2cr9d7 gm_l8; /* op */
    uint8_t gm_l9; /* t_int */
    uint8_t gm_l10; /* t_float */
    uint8_t gm_l11; /* ok */
    gm_t88rcucg1b7uk gm_l12; /* ty */
    gm_tn3r4rvlh0rsv gm_l13; /* <temporary array> */
    GM_STACK_CHECK(&F, 1223u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_tkn42u7na3t81 gm_l14;
    uint8_t gm_l15;
    uint8_t gm_l16;
    uint8_t gm_l17;
    gm_t53at80pfdus3 gm_l18;
    uint8_t gm_l19;
    gm_tabb710d9mq2e gm_l20;
    gm_tabb710d9mq2e gm_l21;
    void * t14 = gm_l0;
    void * t15 = GM_LD(void *, ((uint8_t *)t14));
    void * t16 = (void *)(((uint8_t *)t15) + 8);
    gm_l6 = t16;
    void * t17 = gm_l0;
    gm_b16_8 t18 = gm_l4;
    gm_b16_8 t19 = gm_l2;
    gm_tl874t2atnlh3 t20 = gm_l5;
    uint8_t t21 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t17, t18, ((uint64_t)1ULL), t19, t20);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t22 = (uint8_t)!t21;
    if (t22) {
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, ((uint8_t *)t23));
        gm_tl874t2atnlh3 t25 = gm_l5;
        void * t26 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t24, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t26;
        goto gm_fret;
    }
    void * t27 = gm_l0;
    gm_b16_8 t28 = gm_l4;
    gm_tl874t2atnlh3 t29 = gm_l5;
    gm_tkn42u7na3t81 t30 = gm_f_type_arg_grgffgp((void *)NULL, cx, NULL, t27, t28, ((uint64_t)0ULL), t29);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l14 = t30;
    uint8_t t31 = (uint8_t)((((uint8_t *)&gm_l14))[4] != 0);
    if (t31) {
        gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l14));
        gm_l7 = t32;
    } else {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    {
        {
            gm_b16_8 t33 = gm_l2;
            gm_b16_8 t34;
            GM_ST(void *, (uint8_t *)&t34, (void *)(gm_s7f68g16j7a3o));
            GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(5));
            uint8_t t35 = (gm_eq0iqiubfm7e(((uint8_t *)&t33), ((uint8_t *)&t34)));
            if (!t35) {
                goto gm_lb2;
            }
            gm_t8588ma2cr9d7 t36;
            GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t36 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)1ULL));
            gm_l8 = t36;
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_b16_8 t37 = gm_l2;
            gm_b16_8 t38;
            GM_ST(void *, (uint8_t *)&t38, (void *)(gm_sjb60mo5hejk2));
            GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(5));
            uint8_t t39 = (gm_eq0iqiubfm7e(((uint8_t *)&t37), ((uint8_t *)&t38)));
            if (!t39) {
                goto gm_lb3;
            }
            gm_t8588ma2cr9d7 t40;
            GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t40 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)2ULL));
            gm_l8 = t40;
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_b16_8 t41 = gm_l2;
            gm_b16_8 t42;
            GM_ST(void *, (uint8_t *)&t42, (void *)(gm_shne2m0cj5kgi));
            GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(5));
            uint8_t t43 = (gm_eq0iqiubfm7e(((uint8_t *)&t41), ((uint8_t *)&t42)));
            if (!t43) {
                goto gm_lb4;
            }
            gm_t8588ma2cr9d7 t44;
            GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t44 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)3ULL));
            gm_l8 = t44;
            goto gm_lb1;
        }
gm_lb4:;
        {
            gm_b16_8 t45 = gm_l2;
            gm_b16_8 t46;
            GM_ST(void *, (uint8_t *)&t46, (void *)(gm_shb5mqv6etf1t));
            GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(4));
            uint8_t t47 = (gm_eq0iqiubfm7e(((uint8_t *)&t45), ((uint8_t *)&t46)));
            if (!t47) {
                goto gm_lb5;
            }
            gm_t8588ma2cr9d7 t48;
            GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t48 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t48 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)4ULL));
            gm_l8 = t48;
            goto gm_lb1;
        }
gm_lb5:;
        {
            gm_b16_8 t49 = gm_l2;
            gm_b16_8 t50;
            GM_ST(void *, (uint8_t *)&t50, (void *)(gm_s02419ud122hi));
            GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(7));
            uint8_t t51 = (gm_eq0iqiubfm7e(((uint8_t *)&t49), ((uint8_t *)&t50)));
            if (!t51) {
                goto gm_lb6;
            }
            gm_t8588ma2cr9d7 t52;
            GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t52 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)5ULL));
            gm_l8 = t52;
            goto gm_lb1;
        }
gm_lb6:;
        {
            gm_t8588ma2cr9d7 t53;
            GM_ST(uint8_t, (uint8_t *)&t53 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t53 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t53 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t53 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)6ULL));
            gm_l8 = t53;
            goto gm_lb1;
        }
gm_lb7:;
        gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 224, 23);
    }
gm_lb1:;
    void * t54 = gm_l6;
    gm_t88rcucg1b7uk t55 = gm_l7;
    uint8_t t56 = gm_f_is_integer_8d4f9ek(t54, GM_CONV(gm_csguankko4lmhd, t55));
    if (t56) {
        gm_l15 = ((uint8_t)1);
    } else {
        void * t57 = gm_l0;
        gm_t88rcucg1b7uk t58 = gm_l7;
        gm_t9qkh0rsooc8d t59;
        GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)3ULL));
        uint8_t t60 = gm_f_param_with_qfvlipt(t57, GM_CONV(gm_csguankko4lmhd, t58), t59);
        gm_l15 = t60;
    }
    uint8_t t61 = gm_l15;
    gm_l9 = t61;
    {
        void * t62 = gm_l6;
        gm_t88rcucg1b7uk t63 = gm_l7;
        gm_f_kind_jcnt4hm((void *)&gm_l18, t62, GM_CONV(gm_csguankko4lmhd, t63));
    }
    {
        {
            uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l18) + 0));
            uint8_t t65 = (uint8_t)(t64 == ((uint8_t)3ULL));
            if (!t65) {
                goto gm_lb9;
            }
            gm_l17 = ((uint8_t)1);
            goto gm_lb8;
        }
gm_lb9:;
        {
            gm_l17 = ((uint8_t)0);
            goto gm_lb8;
        }
gm_lb10:;
        gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 233, 16);
    }
gm_lb8:;
    uint8_t t66 = gm_l17;
    if (t66) {
        gm_l16 = ((uint8_t)1);
    } else {
        void * t67 = gm_l0;
        gm_t88rcucg1b7uk t68 = gm_l7;
        gm_t9qkh0rsooc8d t69;
        GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)5ULL));
        uint8_t t70 = gm_f_param_with_qfvlipt(t67, GM_CONV(gm_csguankko4lmhd, t68), t69);
        gm_l16 = t70;
    }
    uint8_t t71 = gm_l16;
    gm_l10 = t71;
    {
        gm_t8588ma2cr9d7 t72 = gm_l8;
        gm_t8588ma2cr9d7 t73;
        GM_ST(uint8_t, (uint8_t *)&t73 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t73 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t73 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t73 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t73 + 0), ((uint8_t)1ULL));
        gm_t8588ma2cr9d7 t74;
        GM_ST(uint8_t, (uint8_t *)&t74 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t74 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t74 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t74 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)5ULL));
        gm_t8588ma2cr9d7 t75;
        GM_ST(uint8_t, (uint8_t *)&t75 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t75 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t75 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t75 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)6ULL));
        gm_t8588ma2cr9d7 t76;
        GM_ST(uint8_t, (uint8_t *)&t76 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t76 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t76 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t76 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t76 + 0), ((uint8_t)2ULL));
        gm_tpk661un0rsod t77;
        GM_ST(gm_t8588ma2cr9d7, ((uint8_t *)&t77 + 0), t73);
        GM_ST(gm_t8588ma2cr9d7, ((uint8_t *)&t77 + 12), t74);
        GM_ST(gm_t8588ma2cr9d7, ((uint8_t *)&t77 + 24), t75);
        GM_ST(gm_t8588ma2cr9d7, ((uint8_t *)&t77 + 36), t76);
        uint8_t t78 = 0;
        if (gm_eqg63o53ar52(((uint8_t *)&t77) + 0, ((uint8_t *)&t72))) t78 = 1;
        if (gm_eqg63o53ar52(((uint8_t *)&t77) + 12, ((uint8_t *)&t72))) t78 = 1;
        if (gm_eqg63o53ar52(((uint8_t *)&t77) + 24, ((uint8_t *)&t72))) t78 = 1;
        if (gm_eqg63o53ar52(((uint8_t *)&t77) + 36, ((uint8_t *)&t72))) t78 = 1;
        uint8_t t79 = (uint8_t)(t78);
        if (t79) {
            uint8_t t80 = gm_l9;
            gm_l11 = t80;
        } else {
            uint8_t t81 = gm_l9;
            if (t81) {
                uint8_t t82 = gm_l3;
                gm_l19 = t82;
            } else {
                gm_l19 = ((uint8_t)0);
            }
            uint8_t t83 = gm_l19;
            gm_l11 = t83;
        }
    }
    {
        {
            gm_t8588ma2cr9d7 t84 = gm_l8;
            uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&t84) + 0));
            uint8_t t86 = (uint8_t)((t85 == ((uint8_t)2ULL)));
            if (!t86) {
                goto gm_lb12;
            }
            uint8_t t87 = gm_l10;
            if (!t87) {
                goto gm_lb12;
            }
            uint8_t t88 = gm_l9;
            uint8_t t89 = (uint8_t)!t88;
            if (!t89) {
                goto gm_lb12;
            }
            {
                void * t90 = gm_l0;
                gm_tl874t2atnlh3 t91 = gm_l5;
                void * t92 = gm_l0;
                gm_t88rcucg1b7uk t93 = gm_l7;
                gm_b16_8 t94 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t92, t93);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t95 = gm_l0;
                gm_t88rcucg1b7uk t96 = gm_l7;
                gm_b16_8 t97 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t95, t96);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t6j6smj7qoctu t98;
                GM_ST(gm_b16_8, ((uint8_t *)&t98 + 0), t94);
                GM_ST(gm_b16_8, ((uint8_t *)&t98 + 16), t97);
                gm_f_format_cpioo77((void *)&gm_l20, (void *)NULL, cx, NULL, &t98);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t99 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                if (!t99) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 8, gm_srcngug8co99ok1, 236, 24);
                gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
                gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)&t100));
                gm_b0_1 t102 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t90, t91, t101);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto gm_lb11;
        }
gm_lb12:;
        uint8_t t103 = gm_l11;
        uint8_t t104 = (uint8_t)!t103;
        if (t104) {
            void * t105 = gm_l0;
            gm_tl874t2atnlh3 t106 = gm_l5;
            gm_b16_8 t107 = gm_l2;
            void * t108 = gm_l0;
            gm_t88rcucg1b7uk t109 = gm_l7;
            gm_b16_8 t110 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t108, t109);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6j6smj7qoctu t111;
            GM_ST(gm_b16_8, ((uint8_t *)&t111 + 0), t107);
            GM_ST(gm_b16_8, ((uint8_t *)&t111 + 16), t110);
            gm_f_format_8fu7rb8((void *)&gm_l21, (void *)NULL, cx, NULL, &t111);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t112 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
            if (!t112) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 8, gm_srcngug8co99ok1, 238, 24);
            gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
            gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
            gm_b0_1 t115 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t105, t106, t114);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
gm_lb11:;
    gm_t8588ma2cr9d7 t116 = gm_l8;
    uint8_t t117 = GM_LD(uint8_t, (((uint8_t *)&t116) + 0));
    uint8_t t118 = (uint8_t)((t117 == ((uint8_t)5ULL)));
    gm_t8588ma2cr9d7 t119;
    GM_ST(uint8_t, (uint8_t *)&t119 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t119 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t119 + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&t119 + 8, 0);
    GM_ST(uint8_t, ((uint8_t *)&t119 + 0), ((uint8_t)5ULL));
    if (t118) {
        void * t120 = gm_l6;
        gm_t88rcucg1b7uk t121 = gm_l7;
        gm_t88rcucg1b7uk t122 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t120, t121);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l12 = t122;
    } else {
        gm_t88rcucg1b7uk t123 = gm_l7;
        gm_l12 = t123;
    }
    gm_t8588ma2cr9d7 t124 = gm_l8;
    gm_t88rcucg1b7uk t125 = gm_l12;
    void * t126 = gm_l1;
    gm_tn3r4rvlh0rsv t127;
    GM_ST(void *, ((uint8_t *)&t127 + 0), t126);
    gm_l13 = t127;
    void * t128 = (void *)((uint8_t *)&gm_l13);
    uint64_t t129 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t130;
    GM_ST(void *, (uint8_t *)&t130, (void *)(((uint8_t *)t128 + (uintptr_t)((uint64_t)0ULL) * 8)));
    GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(t129));
    gm_tl874t2atnlh3 t131 = gm_l5;
    void * t132 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t124, t125, t130, t131);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t132;
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_numeric_method_jo5uq0l(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, uint8_t a4, uint8_t a5, gm_b16_8 a6, gm_tl874t2atnlh3 a7) {
    struct gm_fr_numeric_method_jo5uq0l F;
    void * gm_l0; /* cx */
    void * gm_l1; /* recv */
    gm_t88rcucg1b7uk gm_l2; /* rty */
    gm_b16_8 gm_l3; /* n */
    uint8_t gm_l4; /* is_int */
    uint8_t gm_l5; /* is_float */
    gm_b16_8 gm_l6; /* args */
    gm_tl874t2atnlh3 gm_l7; /* span */
    void * gm_l8; /* tys */
    gm_tn3r4rvlh0rsv gm_l9; /* one */
    gm_t88rcucg1b7uk gm_l10; /* t */
    void * gm_l11; /* zero */
    gm_t5fru0lp11n7t gm_l12; /* <temporary array> */
    gm_t88rcucg1b7uk gm_l13; /* t */
    void * gm_l14; /* lo */
    void * gm_l15; /* hi */
    gm_t88rcucg1b7uk gm_l16; /* ty */
    gm_thmgeksj5pdth gm_l17; /* <temporary array> */
    void * gm_l18; /* e */
    gm_t9qkh0rsooc8d gm_l19; /* flavor */
    gm_t88rcucg1b7uk gm_l20; /* ty */
    gm_t5fru0lp11n7t gm_l21; /* <temporary array> */
    gm_t8588ma2cr9d7 gm_l22; /* op */
    gm_t88rcucg1b7uk gm_l23; /* t */
    GM_STACK_CHECK(&F, 4430u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    gm_l7 = a7;
    gm_tkn42u7na3t81 gm_l24;
    uint8_t gm_l25;
    gm_t53at80pfdus3 gm_l26;
    gm_tabb710d9mq2e gm_l27;
    gm_tftb1odc739t5 gm_l28;
    gm_tkn42u7na3t81 gm_l29;
    gm_tftb1odc739t5 gm_l30;
    gm_tkn42u7na3t81 gm_l31;
    gm_tabb710d9mq2e gm_l32;
    {
        void * t61 = gm_l0;
        void * t62 = GM_LD(void *, ((uint8_t *)t61));
        void * t63 = (void *)(((uint8_t *)t62) + 8);
        gm_l8 = t63;
        void * t64 = gm_l1;
        gm_tn3r4rvlh0rsv t65;
        GM_ST(void *, ((uint8_t *)&t65 + 0), t64);
        gm_l9 = t65;
        {
            {
                {
                    {
                        {
                            gm_b16_8 t66 = gm_l3;
                            gm_b16_8 t67;
                            GM_ST(void *, (uint8_t *)&t67, (void *)(gm_s7f68g16j7a3o));
                            GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(5));
                            uint8_t t68 = (gm_eq0iqiubfm7e(((uint8_t *)&t66), ((uint8_t *)&t67)));
                            if (!t68) {
                                goto gm_lb4;
                            }
                            goto gm_lb3;
                        }
gm_lb4:;
                        {
                            gm_b16_8 t69 = gm_l3;
                            gm_b16_8 t70;
                            GM_ST(void *, (uint8_t *)&t70, (void *)(gm_sjb60mo5hejk2));
                            GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(5));
                            uint8_t t71 = (gm_eq0iqiubfm7e(((uint8_t *)&t69), ((uint8_t *)&t70)));
                            if (!t71) {
                                goto gm_lb5;
                            }
                            goto gm_lb3;
                        }
gm_lb5:;
                        {
                            gm_b16_8 t72 = gm_l3;
                            gm_b16_8 t73;
                            GM_ST(void *, (uint8_t *)&t73, (void *)(gm_shne2m0cj5kgi));
                            GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(5));
                            uint8_t t74 = (gm_eq0iqiubfm7e(((uint8_t *)&t72), ((uint8_t *)&t73)));
                            if (!t74) {
                                goto gm_lb6;
                            }
                            goto gm_lb3;
                        }
gm_lb6:;
                        {
                            gm_b16_8 t75 = gm_l3;
                            gm_b16_8 t76;
                            GM_ST(void *, (uint8_t *)&t76, (void *)(gm_shb5mqv6etf1t));
                            GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(4));
                            uint8_t t77 = (gm_eq0iqiubfm7e(((uint8_t *)&t75), ((uint8_t *)&t76)));
                            if (!t77) {
                                goto gm_lb7;
                            }
                            goto gm_lb3;
                        }
gm_lb7:;
                        {
                            gm_b16_8 t78 = gm_l3;
                            gm_b16_8 t79;
                            GM_ST(void *, (uint8_t *)&t79, (void *)(gm_s02419ud122hi));
                            GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(7));
                            uint8_t t80 = (gm_eq0iqiubfm7e(((uint8_t *)&t78), ((uint8_t *)&t79)));
                            if (!t80) {
                                goto gm_lb8;
                            }
                            goto gm_lb3;
                        }
gm_lb8:;
                        {
                            gm_b16_8 t81 = gm_l3;
                            gm_b16_8 t82;
                            GM_ST(void *, (uint8_t *)&t82, (void *)(gm_sbanjlvk3n14i));
                            GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(3));
                            uint8_t t83 = (gm_eq0iqiubfm7e(((uint8_t *)&t81), ((uint8_t *)&t82)));
                            if (!t83) {
                                goto gm_lb9;
                            }
                            goto gm_lb3;
                        }
gm_lb9:;
                        goto gm_lb2;
                    }
gm_lb3:;
                    void * t84 = gm_l0;
                    void * t85 = gm_l1;
                    gm_b16_8 t86 = gm_l3;
                    uint8_t t87 = gm_l5;
                    gm_b16_8 t88 = gm_l6;
                    gm_tl874t2atnlh3 t89 = gm_l7;
                    void * t90 = gm_f_numeric_conversion_1ovq7at((void *)NULL, cx, NULL, t84, t85, t86, t87, t88, t89);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t90;
                    goto gm_fret;
                }
gm_lb2:;
                {
                    gm_b16_8 t91 = gm_l3;
                    gm_b16_8 t92;
                    GM_ST(void *, (uint8_t *)&t92, (void *)(gm_sfrjcsmisriv1));
                    GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(2));
                    uint8_t t93 = (gm_eq0iqiubfm7e(((uint8_t *)&t91), ((uint8_t *)&t92)));
                    if (!t93) {
                        goto gm_lb10;
                    }
                    void * t94 = gm_l0;
                    gm_b16_8 t95 = gm_l6;
                    gm_b16_8 t96 = gm_l3;
                    gm_tl874t2atnlh3 t97 = gm_l7;
                    uint8_t t98 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t94, t95, ((uint64_t)1ULL), t96, t97);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t99 = (uint8_t)!t98;
                    if (t99) {
                        void * t100 = gm_l0;
                        void * t101 = GM_LD(void *, ((uint8_t *)t100));
                        gm_tl874t2atnlh3 t102 = gm_l7;
                        void * t103 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t101, t102);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t103;
                        goto gm_fret;
                    }
                    void * t104 = gm_l0;
                    gm_b16_8 t105 = gm_l6;
                    gm_tl874t2atnlh3 t106 = gm_l7;
                    gm_tkn42u7na3t81 t107 = gm_f_type_arg_grgffgp((void *)NULL, cx, NULL, t104, t105, ((uint64_t)0ULL), t106);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l24 = t107;
                    uint8_t t108 = (uint8_t)((((uint8_t *)&gm_l24))[4] != 0);
                    if (t108) {
                        gm_t88rcucg1b7uk t109 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l24));
                        gm_l10 = t109;
                    } else {
                        F.ret = ((void *)0);
                        goto gm_fret;
                    }
                    {
                        {
                            {
                                void * t110 = gm_l8;
                                gm_t88rcucg1b7uk t111 = gm_l10;
                                gm_f_kind_jcnt4hm((void *)&gm_l26, t110, GM_CONV(gm_csguankko4lmhd, t111));
                            }
                            {
                                {
                                    uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                                    uint8_t t113 = (uint8_t)(t112 == ((uint8_t)3ULL));
                                    if (!t113) {
                                        goto gm_lb14;
                                    }
                                    gm_l25 = ((uint8_t)0);
                                    goto gm_lb13;
                                }
gm_lb14:;
                                {
                                    gm_l25 = ((uint8_t)1);
                                    goto gm_lb13;
                                }
gm_lb15:;
                                gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 152, 16);
                            }
gm_lb13:;
                            uint8_t t114 = gm_l25;
                            if (!t114) {
                                goto gm_lb12;
                            }
                            void * t115 = gm_l0;
                            gm_t88rcucg1b7uk t116 = gm_l10;
                            gm_t9qkh0rsooc8d t117;
                            GM_ST(uint8_t, ((uint8_t *)&t117 + 0), ((uint8_t)5ULL));
                            uint8_t t118 = gm_f_param_with_qfvlipt(t115, GM_CONV(gm_csguankko4lmhd, t116), t117);
                            uint8_t t119 = (uint8_t)!t118;
                            if (!t119) {
                                goto gm_lb12;
                            }
                            {
                                void * t120 = gm_l0;
                                gm_tl874t2atnlh3 t121 = gm_l7;
                                void * t122 = gm_l0;
                                gm_t88rcucg1b7uk t123 = gm_l10;
                                gm_b16_8 t124 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t122, t123);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tnpifv20irnph t125;
                                GM_ST(gm_b16_8, ((uint8_t *)&t125 + 0), t124);
                                gm_f_format_qe2mgnq((void *)&gm_l27, (void *)NULL, cx, NULL, t125);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t126 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                                if (!t126) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 8, gm_srcngug8co99ok1, 153, 32);
                                gm_b16_8 t127 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l27) + 8));
                                gm_b16_8 t128 = GM_LD(gm_b16_8, ((uint8_t *)&t127));
                                gm_b0_1 t129 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t120, t121, t128);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            goto gm_lb11;
                        }
gm_lb12:;
                    }
gm_lb11:;
                    gm_t88rcucg1b7uk t130 = gm_l10;
                    void * t131 = (void *)((uint8_t *)&gm_l9);
                    uint64_t t132 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t133;
                    GM_ST(void *, (uint8_t *)&t133, (void *)(((uint8_t *)t131 + (uintptr_t)((uint64_t)0ULL) * 8)));
                    GM_ST(uintptr_t, (uint8_t *)&t133 + 8, (uintptr_t)(t132));
                    gm_b16_8 t134 = GM_LD(gm_b16_8, ((uint8_t *)&t133));
                    gm_tl874t2atnlh3 t135 = gm_l7;
                    gm_t8588ma2cr9d7 t136;
                    GM_ST(uint8_t, (uint8_t *)&t136 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t136 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t136 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t136 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t136 + 0), ((uint8_t)2ULL));
                    void * t137 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t136, t130, t134, t135);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t137;
                    goto gm_fret;
                }
gm_lb10:;
            }
            {
                {
                    gm_b16_8 t138 = gm_l3;
                    gm_b16_8 t139;
                    GM_ST(void *, (uint8_t *)&t139, (void *)(gm_s6a2c11qu9scr));
                    GM_ST(uintptr_t, (uint8_t *)&t139 + 8, (uintptr_t)(12));
                    uint8_t t140 = (gm_eq0iqiubfm7e(((uint8_t *)&t138), ((uint8_t *)&t139)));
                    if (!t140) {
                        goto gm_lb16;
                    }
                    uint8_t t141 = gm_l4;
                    if (t141) {
                        void * t142 = gm_l0;
                        gm_b16_8 t143 = gm_l6;
                        gm_b16_8 t144 = gm_l3;
                        gm_tl874t2atnlh3 t145 = gm_l7;
                        uint8_t t146 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t142, t143, ((uint64_t)0ULL), t144, t145);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        {
                            void * t147 = gm_l0;
                            void * t148 = (void *)(((uint8_t *)t147) + 320);
                            void * t149 = gm_l1;
                            gm_t88rcucg1b7uk t150 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t149));
                            gm_tl874t2atnlh3 t151 = gm_l7;
                            gm_b16_8 t152;
                            GM_ST(void *, (uint8_t *)&t152, (void *)(gm_s6a2c11qu9scr));
                            GM_ST(uintptr_t, (uint8_t *)&t152 + 8, (uintptr_t)(12));
                            gm_t91vm1g0k2rpm t153;
                            GM_ST(uint16_t, (uint8_t *)&t153 + 34, 0);
                            GM_ST(uint32_t, (uint8_t *)&t153 + 36, 0);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t153 + 0), t150);
                            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t153 + 4), t151);
                            GM_ST(gm_b16_8, ((uint8_t *)&t153 + 16), t152);
                            GM_ST(uint8_t, ((uint8_t *)&t153 + 32), ((uint8_t)1));
                            GM_ST(uint8_t, ((uint8_t *)&t153 + 33), ((uint8_t)0));
                            gm_tpm220j5qhjke t154;
                            GM_ST(uint8_t, (uint8_t *)&t154 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t154 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t154 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t154 + 0), ((uint8_t)3ULL));
                            GM_ST(gm_t91vm1g0k2rpm, ((uint8_t *)&t154 + 8), t153);
                            gm_tftb1odc739t5 t155 = gm_f_push_7koe9lk(t148, &t154);
                            gm_l28 = t155;
                        }
                        uint8_t t156 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                        if (!t156) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 1, gm_srcngug8co99ok1, 159, 13);
                        {
                            void * t157 = gm_l1;
                            gm_t88rcucg1b7uk t158 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t157));
                            gm_tbci5ospk5nnn t159;
                            gm_f_zero_s25l990((void *)&t159, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tl874t2atnlh3 t160 = gm_l7;
                            gm_to0f5qn75cgg8 t161;
                            GM_ST(uint8_t, (uint8_t *)&t161 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t161 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t161 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t161 + 56, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t161 + 0), ((uint8_t)0ULL));
                            GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t161 + 8), t159);
                            void * t162 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t158, &t161, t160);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l11 = t162;
                        }
                        void * t163 = gm_l8;
                        void * t164 = gm_l0;
                        void * t165 = (void *)(((uint8_t *)t164) + 96);
                        void * t166 = gm_l0;
                        void * t167 = GM_LD(void *, ((uint8_t *)t166));
                        void * t168 = gm_l1;
                        gm_t88rcucg1b7uk t169 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t168));
                        gm_t8588ma2cr9d7 t170;
                        GM_ST(uint8_t, (uint8_t *)&t170 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t170 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t170 + 4, 0);
                        GM_ST(uint32_t, (uint8_t *)&t170 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t170 + 0), ((uint8_t)20ULL));
                        gm_t88rcucg1b7uk t171 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t165, t167, t169);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t88rcucg1b7uk t172 = gm_f_unsigned_of_n48qlse((void *)NULL, cx, NULL, t163, t171);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t173 = gm_l1;
                        void * t174 = gm_l11;
                        gm_t5fru0lp11n7t t175;
                        GM_ST(void *, ((uint8_t *)&t175 + 0), t173);
                        GM_ST(void *, ((uint8_t *)&t175 + 8), t174);
                        gm_l12 = t175;
                        void * t176 = (void *)((uint8_t *)&gm_l12);
                        uint64_t t177 = ((uintptr_t)((uint64_t)2ULL)) - (uintptr_t)((uint64_t)0ULL);
                        gm_b16_8 t178;
                        GM_ST(void *, (uint8_t *)&t178, (void *)(((uint8_t *)t176 + (uintptr_t)((uint64_t)0ULL) * 8)));
                        GM_ST(uintptr_t, (uint8_t *)&t178 + 8, (uintptr_t)(t177));
                        gm_tl874t2atnlh3 t179 = gm_l7;
                        void * t180 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t170, t172, t178, t179);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t180;
                        goto gm_fret;
                    }
                    goto gm_lb1;
                }
gm_lb16:;
                {
                    gm_b16_8 t181 = gm_l3;
                    gm_b16_8 t182;
                    GM_ST(void *, (uint8_t *)&t182, (void *)(gm_s6vvtrmtnt3j1));
                    GM_ST(uintptr_t, (uint8_t *)&t182 + 8, (uintptr_t)(4));
                    uint8_t t183 = (gm_eq0iqiubfm7e(((uint8_t *)&t181), ((uint8_t *)&t182)));
                    if (!t183) {
                        goto gm_lb17;
                    }
                    void * t184 = gm_l0;
                    gm_b16_8 t185 = gm_l6;
                    gm_b16_8 t186 = gm_l3;
                    gm_tl874t2atnlh3 t187 = gm_l7;
                    uint8_t t188 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t184, t185, ((uint64_t)1ULL), t186, t187);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t189 = (uint8_t)!t188;
                    if (t189) {
                        void * t190 = gm_l0;
                        void * t191 = GM_LD(void *, ((uint8_t *)t190));
                        gm_tl874t2atnlh3 t192 = gm_l7;
                        void * t193 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t191, t192);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t193;
                        goto gm_fret;
                    }
                    void * t194 = gm_l0;
                    gm_b16_8 t195 = gm_l6;
                    gm_tl874t2atnlh3 t196 = gm_l7;
                    gm_tkn42u7na3t81 t197 = gm_f_type_arg_grgffgp((void *)NULL, cx, NULL, t194, t195, ((uint64_t)0ULL), t196);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l29 = t197;
                    uint8_t t198 = (uint8_t)((((uint8_t *)&gm_l29))[4] != 0);
                    if (t198) {
                        gm_t88rcucg1b7uk t199 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l29));
                        gm_l13 = t199;
                    } else {
                        F.ret = ((void *)0);
                        goto gm_fret;
                    }
                    void * t200 = gm_l0;
                    void * t201 = gm_l1;
                    gm_t88rcucg1b7uk t202 = gm_l13;
                    gm_tl874t2atnlh3 t203 = gm_l7;
                    void * t204 = gm_f_bits_cast_0kaubje((void *)NULL, cx, NULL, t200, t201, t202, t203);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t204;
                    goto gm_fret;
                }
gm_lb17:;
            }
            {
                {
                    {
                        {
                            gm_b16_8 t205 = gm_l3;
                            gm_b16_8 t206;
                            GM_ST(void *, (uint8_t *)&t206, (void *)(gm_sm3o0uerjumpm));
                            GM_ST(uintptr_t, (uint8_t *)&t206 + 8, (uintptr_t)(3));
                            uint8_t t207 = (gm_eq0iqiubfm7e(((uint8_t *)&t205), ((uint8_t *)&t206)));
                            if (!t207) {
                                goto gm_lb20;
                            }
                            goto gm_lb19;
                        }
gm_lb20:;
                        {
                            gm_b16_8 t208 = gm_l3;
                            gm_b16_8 t209;
                            GM_ST(void *, (uint8_t *)&t209, (void *)(gm_s2mtoab140jp6));
                            GM_ST(uintptr_t, (uint8_t *)&t209 + 8, (uintptr_t)(3));
                            uint8_t t210 = (gm_eq0iqiubfm7e(((uint8_t *)&t208), ((uint8_t *)&t209)));
                            if (!t210) {
                                goto gm_lb21;
                            }
                            goto gm_lb19;
                        }
gm_lb21:;
                        {
                            gm_b16_8 t211 = gm_l3;
                            gm_b16_8 t212;
                            GM_ST(void *, (uint8_t *)&t212, (void *)(gm_s7u0p3hthk80r));
                            GM_ST(uintptr_t, (uint8_t *)&t212 + 8, (uintptr_t)(8));
                            uint8_t t213 = (gm_eq0iqiubfm7e(((uint8_t *)&t211), ((uint8_t *)&t212)));
                            if (!t213) {
                                goto gm_lb22;
                            }
                            goto gm_lb19;
                        }
gm_lb22:;
                        goto gm_lb18;
                    }
gm_lb19:;
                    void * t214 = gm_l0;
                    void * t215 = gm_l1;
                    gm_b16_8 t216 = gm_l3;
                    gm_b16_8 t217 = gm_l6;
                    gm_tl874t2atnlh3 t218 = gm_l7;
                    void * t219 = gm_f_numeric_pair_54urfaa((void *)NULL, cx, NULL, t214, t215, t216, t217, t218);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t219;
                    goto gm_fret;
                }
gm_lb18:;
                {
                    gm_b16_8 t220 = gm_l3;
                    gm_b16_8 t221;
                    GM_ST(void *, (uint8_t *)&t221, (void *)(gm_sm139pecc5n3p));
                    GM_ST(uintptr_t, (uint8_t *)&t221 + 8, (uintptr_t)(5));
                    uint8_t t222 = (gm_eq0iqiubfm7e(((uint8_t *)&t220), ((uint8_t *)&t221)));
                    if (!t222) {
                        goto gm_lb23;
                    }
                    void * t223 = gm_l0;
                    gm_b16_8 t224 = gm_l6;
                    gm_b16_8 t225 = gm_l3;
                    gm_tl874t2atnlh3 t226 = gm_l7;
                    uint8_t t227 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t223, t224, ((uint64_t)2ULL), t225, t226);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t228 = (uint8_t)!t227;
                    if (t228) {
                        void * t229 = gm_l0;
                        void * t230 = GM_LD(void *, ((uint8_t *)t229));
                        gm_tl874t2atnlh3 t231 = gm_l7;
                        void * t232 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t230, t231);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t232;
                        goto gm_fret;
                    }
                    void * t233 = gm_l0;
                    gm_b16_8 t234 = gm_l6;
                    void * t235 = gm_l1;
                    gm_t88rcucg1b7uk t236 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t235));
                    gm_tl874t2atnlh3 t237 = gm_l7;
                    void * t238 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t233, t234, ((uint64_t)0ULL), t236, t237);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l14 = t238;
                    void * t239 = gm_l0;
                    gm_b16_8 t240 = gm_l6;
                    void * t241 = gm_l1;
                    gm_t88rcucg1b7uk t242 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t241));
                    gm_tl874t2atnlh3 t243 = gm_l7;
                    void * t244 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t239, t240, ((uint64_t)1ULL), t242, t243);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l15 = t244;
                    void * t245 = gm_l0;
                    void * t246 = gm_l1;
                    gm_t88rcucg1b7uk t247 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t246));
                    void * t248 = gm_l14;
                    gm_t88rcucg1b7uk t249 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t248));
                    gm_tl874t2atnlh3 t250 = gm_l7;
                    gm_b16_8 t251;
                    GM_ST(void *, (uint8_t *)&t251, (void *)(gm_s8m12els4res5));
                    GM_ST(uintptr_t, (uint8_t *)&t251 + 8, (uintptr_t)(25));
                    gm_t88rcucg1b7uk t252 = gm_f_unify_operands_5sp7j1j((void *)NULL, cx, NULL, t245, t247, t249, t250, t251);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l16 = t252;
                    void * t253 = gm_l0;
                    gm_t88rcucg1b7uk t254 = gm_l16;
                    void * t255 = gm_l15;
                    gm_t88rcucg1b7uk t256 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t255));
                    gm_tl874t2atnlh3 t257 = gm_l7;
                    gm_b16_8 t258;
                    GM_ST(void *, (uint8_t *)&t258, (void *)(gm_s8m12els4res5));
                    GM_ST(uintptr_t, (uint8_t *)&t258 + 8, (uintptr_t)(25));
                    gm_t88rcucg1b7uk t259 = gm_f_unify_operands_5sp7j1j((void *)NULL, cx, NULL, t253, t254, t256, t257, t258);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t88rcucg1b7uk t260 = gm_l16;
                    void * t261 = gm_l1;
                    void * t262 = gm_l14;
                    void * t263 = gm_l15;
                    gm_thmgeksj5pdth t264;
                    GM_ST(void *, ((uint8_t *)&t264 + 0), t261);
                    GM_ST(void *, ((uint8_t *)&t264 + 8), t262);
                    GM_ST(void *, ((uint8_t *)&t264 + 16), t263);
                    gm_t8588ma2cr9d7 t265;
                    GM_ST(uint8_t, (uint8_t *)&t265 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t265 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t265 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t265 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t265 + 0), ((uint8_t)18ULL));
                    gm_l17 = t264;
                    void * t266 = (void *)((uint8_t *)&gm_l17);
                    uint64_t t267 = ((uintptr_t)((uint64_t)3ULL)) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t268;
                    GM_ST(void *, (uint8_t *)&t268, (void *)(((uint8_t *)t266 + (uintptr_t)((uint64_t)0ULL) * 8)));
                    GM_ST(uintptr_t, (uint8_t *)&t268 + 8, (uintptr_t)(t267));
                    gm_tl874t2atnlh3 t269 = gm_l7;
                    void * t270 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t265, t260, t268, t269);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t270;
                    goto gm_fret;
                }
gm_lb23:;
            }
            {
                {
                    gm_b16_8 t271 = gm_l3;
                    gm_b16_8 t272;
                    GM_ST(void *, (uint8_t *)&t272, (void *)(gm_sbl3pbs9195ho));
                    GM_ST(uintptr_t, (uint8_t *)&t272 + 8, (uintptr_t)(3));
                    uint8_t t273 = (gm_eq0iqiubfm7e(((uint8_t *)&t271), ((uint8_t *)&t272)));
                    if (!t273) {
                        goto gm_lb24;
                    }
                    void * t274 = gm_l0;
                    gm_b16_8 t275 = gm_l6;
                    gm_b16_8 t276 = gm_l3;
                    gm_tl874t2atnlh3 t277 = gm_l7;
                    uint8_t t278 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t274, t275, ((uint64_t)0ULL), t276, t277);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    {
                        void * t279 = gm_l0;
                        void * t280 = (void *)(((uint8_t *)t279) + 320);
                        void * t281 = gm_l1;
                        gm_t88rcucg1b7uk t282 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t281));
                        gm_tl874t2atnlh3 t283 = gm_l7;
                        gm_b16_8 t284;
                        GM_ST(void *, (uint8_t *)&t284, (void *)(gm_sbl3pbs9195ho));
                        GM_ST(uintptr_t, (uint8_t *)&t284 + 8, (uintptr_t)(3));
                        gm_t91vm1g0k2rpm t285;
                        GM_ST(uint16_t, (uint8_t *)&t285 + 34, 0);
                        GM_ST(uint32_t, (uint8_t *)&t285 + 36, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t285 + 0), t282);
                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t285 + 4), t283);
                        GM_ST(gm_b16_8, ((uint8_t *)&t285 + 16), t284);
                        GM_ST(uint8_t, ((uint8_t *)&t285 + 32), ((uint8_t)0));
                        GM_ST(uint8_t, ((uint8_t *)&t285 + 33), ((uint8_t)1));
                        gm_tpm220j5qhjke t286;
                        GM_ST(uint8_t, (uint8_t *)&t286 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t286 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t286 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t286 + 0), ((uint8_t)3ULL));
                        GM_ST(gm_t91vm1g0k2rpm, ((uint8_t *)&t286 + 8), t285);
                        gm_tftb1odc739t5 t287 = gm_f_push_7koe9lk(t280, &t286);
                        gm_l30 = t287;
                    }
                    uint8_t t288 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
                    if (!t288) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 1, gm_srcngug8co99ok1, 179, 13);
                    void * t289 = gm_l1;
                    gm_t88rcucg1b7uk t290 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t289));
                    void * t291 = (void *)((uint8_t *)&gm_l9);
                    uint64_t t292 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t293;
                    GM_ST(void *, (uint8_t *)&t293, (void *)(((uint8_t *)t291 + (uintptr_t)((uint64_t)0ULL) * 8)));
                    GM_ST(uintptr_t, (uint8_t *)&t293 + 8, (uintptr_t)(t292));
                    gm_b16_8 t294 = GM_LD(gm_b16_8, ((uint8_t *)&t293));
                    gm_tl874t2atnlh3 t295 = gm_l7;
                    gm_t8588ma2cr9d7 t296;
                    GM_ST(uint8_t, (uint8_t *)&t296 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t296 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t296 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t296 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t296 + 0), ((uint8_t)19ULL));
                    void * t297 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t296, t290, t294, t295);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t297;
                    goto gm_fret;
                }
gm_lb24:;
                {
                    {
                        {
                            gm_b16_8 t298 = gm_l3;
                            gm_b16_8 t299;
                            GM_ST(void *, (uint8_t *)&t299, (void *)(gm_svpofgjtij7vj));
                            GM_ST(uintptr_t, (uint8_t *)&t299 + 8, (uintptr_t)(3));
                            uint8_t t300 = (gm_eq0iqiubfm7e(((uint8_t *)&t298), ((uint8_t *)&t299)));
                            if (!t300) {
                                goto gm_lb27;
                            }
                            goto gm_lb26;
                        }
gm_lb27:;
                        {
                            gm_b16_8 t301 = gm_l3;
                            gm_b16_8 t302;
                            GM_ST(void *, (uint8_t *)&t302, (void *)(gm_sgemqs85b1iol));
                            GM_ST(uintptr_t, (uint8_t *)&t302 + 8, (uintptr_t)(8));
                            uint8_t t303 = (gm_eq0iqiubfm7e(((uint8_t *)&t301), ((uint8_t *)&t302)));
                            if (!t303) {
                                goto gm_lb28;
                            }
                            goto gm_lb26;
                        }
gm_lb28:;
                        {
                            gm_b16_8 t304 = gm_l3;
                            gm_b16_8 t305;
                            GM_ST(void *, (uint8_t *)&t305, (void *)(gm_s7moa4u0q6e5d));
                            GM_ST(uintptr_t, (uint8_t *)&t305 + 8, (uintptr_t)(7));
                            uint8_t t306 = (gm_eq0iqiubfm7e(((uint8_t *)&t304), ((uint8_t *)&t305)));
                            if (!t306) {
                                goto gm_lb29;
                            }
                            goto gm_lb26;
                        }
gm_lb29:;
                        {
                            gm_b16_8 t307 = gm_l3;
                            gm_b16_8 t308;
                            GM_ST(void *, (uint8_t *)&t308, (void *)(gm_s9ahj85e3uamt));
                            GM_ST(uintptr_t, (uint8_t *)&t308 + 8, (uintptr_t)(11));
                            uint8_t t309 = (gm_eq0iqiubfm7e(((uint8_t *)&t307), ((uint8_t *)&t308)));
                            if (!t309) {
                                goto gm_lb30;
                            }
                            goto gm_lb26;
                        }
gm_lb30:;
                        goto gm_lb25;
                    }
gm_lb26:;
                    uint8_t t310 = gm_l4;
                    if (t310) {
                        void * t311 = gm_l0;
                        gm_b16_8 t312 = gm_l6;
                        gm_b16_8 t313 = gm_l3;
                        gm_tl874t2atnlh3 t314 = gm_l7;
                        uint8_t t315 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t311, t312, ((uint64_t)1ULL), t313, t314);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t316 = (uint8_t)!t315;
                        if (t316) {
                            void * t317 = gm_l0;
                            void * t318 = GM_LD(void *, ((uint8_t *)t317));
                            gm_tl874t2atnlh3 t319 = gm_l7;
                            void * t320 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t318, t319);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t320;
                            goto gm_fret;
                        }
                        void * t321 = gm_l0;
                        gm_b16_8 t322 = gm_l6;
                        void * t323 = gm_l8;
                        gm_t88rcucg1b7uk t324 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t323, ((uint8_t)0), ((uint16_t)32ULL));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tl874t2atnlh3 t325 = gm_l7;
                        void * t326 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t321, t322, ((uint64_t)0ULL), t324, t325);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l18 = t326;
                        {
                            {
                                gm_b16_8 t327 = gm_l3;
                                gm_b16_8 t328;
                                GM_ST(void *, (uint8_t *)&t328, (void *)(gm_svpofgjtij7vj));
                                GM_ST(uintptr_t, (uint8_t *)&t328 + 8, (uintptr_t)(3));
                                uint8_t t329 = (gm_eq0iqiubfm7e(((uint8_t *)&t327), ((uint8_t *)&t328)));
                                if (!t329) {
                                    goto gm_lb32;
                                }
                                gm_t9qkh0rsooc8d t330;
                                GM_ST(uint8_t, ((uint8_t *)&t330 + 0), ((uint8_t)0ULL));
                                gm_l19 = t330;
                                goto gm_lb31;
                            }
gm_lb32:;
                            {
                                gm_b16_8 t331 = gm_l3;
                                gm_b16_8 t332;
                                GM_ST(void *, (uint8_t *)&t332, (void *)(gm_sgemqs85b1iol));
                                GM_ST(uintptr_t, (uint8_t *)&t332 + 8, (uintptr_t)(8));
                                uint8_t t333 = (gm_eq0iqiubfm7e(((uint8_t *)&t331), ((uint8_t *)&t332)));
                                if (!t333) {
                                    goto gm_lb33;
                                }
                                gm_t9qkh0rsooc8d t334;
                                GM_ST(uint8_t, ((uint8_t *)&t334 + 0), ((uint8_t)1ULL));
                                gm_l19 = t334;
                                goto gm_lb31;
                            }
gm_lb33:;
                            {
                                gm_b16_8 t335 = gm_l3;
                                gm_b16_8 t336;
                                GM_ST(void *, (uint8_t *)&t336, (void *)(gm_s7moa4u0q6e5d));
                                GM_ST(uintptr_t, (uint8_t *)&t336 + 8, (uintptr_t)(7));
                                uint8_t t337 = (gm_eq0iqiubfm7e(((uint8_t *)&t335), ((uint8_t *)&t336)));
                                if (!t337) {
                                    goto gm_lb34;
                                }
                                gm_t9qkh0rsooc8d t338;
                                GM_ST(uint8_t, ((uint8_t *)&t338 + 0), ((uint8_t)2ULL));
                                gm_l19 = t338;
                                goto gm_lb31;
                            }
gm_lb34:;
                            {
                                gm_t9qkh0rsooc8d t339;
                                GM_ST(uint8_t, ((uint8_t *)&t339 + 0), ((uint8_t)3ULL));
                                gm_l19 = t339;
                                goto gm_lb31;
                            }
gm_lb35:;
                            gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 185, 34);
                        }
gm_lb31:;
                        gm_t9qkh0rsooc8d t340 = gm_l19;
                        uint8_t t341 = GM_LD(uint8_t, (((uint8_t *)&t340) + 0));
                        uint8_t t342 = (uint8_t)((t341 == ((uint8_t)3ULL)));
                        gm_t9qkh0rsooc8d t343;
                        GM_ST(uint8_t, ((uint8_t *)&t343 + 0), ((uint8_t)3ULL));
                        if (t342) {
                            void * t344 = gm_l8;
                            void * t345 = gm_l1;
                            gm_t88rcucg1b7uk t346 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t345));
                            gm_t88rcucg1b7uk t347 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t344, t346);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l20 = t347;
                        } else {
                            void * t348 = gm_l1;
                            gm_t88rcucg1b7uk t349 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t348));
                            gm_l20 = t349;
                        }
                        gm_t9qkh0rsooc8d t350 = gm_l19;
                        gm_t88rcucg1b7uk t351 = gm_l20;
                        void * t352 = gm_l1;
                        void * t353 = gm_l18;
                        gm_t5fru0lp11n7t t354;
                        GM_ST(void *, ((uint8_t *)&t354 + 0), t352);
                        GM_ST(void *, ((uint8_t *)&t354 + 8), t353);
                        gm_t8588ma2cr9d7 t355;
                        GM_ST(uint8_t, (uint8_t *)&t355 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t355 + 2, 0);
                        GM_ST(uint8_t, (uint8_t *)&t355 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t355 + 6, 0);
                        GM_ST(uint32_t, (uint8_t *)&t355 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t355 + 0), ((uint8_t)21ULL));
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t355 + 4), t350);
                        gm_l21 = t354;
                        void * t356 = (void *)((uint8_t *)&gm_l21);
                        uint64_t t357 = ((uintptr_t)((uint64_t)2ULL)) - (uintptr_t)((uint64_t)0ULL);
                        gm_b16_8 t358;
                        GM_ST(void *, (uint8_t *)&t358, (void *)(((uint8_t *)t356 + (uintptr_t)((uint64_t)0ULL) * 8)));
                        GM_ST(uintptr_t, (uint8_t *)&t358 + 8, (uintptr_t)(t357));
                        gm_tl874t2atnlh3 t359 = gm_l7;
                        void * t360 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t355, t351, t358, t359);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t360;
                        goto gm_fret;
                    }
                    goto gm_lb1;
                }
gm_lb25:;
            }
            {
                {
                    {
                        {
                            gm_b16_8 t361 = gm_l3;
                            gm_b16_8 t362;
                            GM_ST(void *, (uint8_t *)&t362, (void *)(gm_ss3f61htk015v));
                            GM_ST(uintptr_t, (uint8_t *)&t362 + 8, (uintptr_t)(6));
                            uint8_t t363 = (gm_eq0iqiubfm7e(((uint8_t *)&t361), ((uint8_t *)&t362)));
                            if (!t363) {
                                goto gm_lb38;
                            }
                            goto gm_lb37;
                        }
gm_lb38:;
                        {
                            gm_b16_8 t364 = gm_l3;
                            gm_b16_8 t365;
                            GM_ST(void *, (uint8_t *)&t365, (void *)(gm_s541nrvershlm));
                            GM_ST(uintptr_t, (uint8_t *)&t365 + 8, (uintptr_t)(6));
                            uint8_t t366 = (gm_eq0iqiubfm7e(((uint8_t *)&t364), ((uint8_t *)&t365)));
                            if (!t366) {
                                goto gm_lb39;
                            }
                            goto gm_lb37;
                        }
gm_lb39:;
                        {
                            gm_b16_8 t367 = gm_l3;
                            gm_b16_8 t368;
                            GM_ST(void *, (uint8_t *)&t368, (void *)(gm_s7uql3407tind));
                            GM_ST(uintptr_t, (uint8_t *)&t368 + 8, (uintptr_t)(9));
                            uint8_t t369 = (gm_eq0iqiubfm7e(((uint8_t *)&t367), ((uint8_t *)&t368)));
                            if (!t369) {
                                goto gm_lb40;
                            }
                            goto gm_lb37;
                        }
gm_lb40:;
                        {
                            gm_b16_8 t370 = gm_l3;
                            gm_b16_8 t371;
                            GM_ST(void *, (uint8_t *)&t371, (void *)(gm_s3hqs40g2kokp));
                            GM_ST(uintptr_t, (uint8_t *)&t371 + 8, (uintptr_t)(8));
                            uint8_t t372 = (gm_eq0iqiubfm7e(((uint8_t *)&t370), ((uint8_t *)&t371)));
                            if (!t372) {
                                goto gm_lb41;
                            }
                            goto gm_lb37;
                        }
gm_lb41:;
                        goto gm_lb36;
                    }
gm_lb37:;
                    uint8_t t373 = gm_l5;
                    if (t373) {
                        void * t374 = gm_l0;
                        gm_b16_8 t375 = gm_l6;
                        gm_b16_8 t376 = gm_l3;
                        gm_tl874t2atnlh3 t377 = gm_l7;
                        uint8_t t378 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t374, t375, ((uint64_t)0ULL), t376, t377);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        {
                            {
                                gm_b16_8 t379 = gm_l3;
                                gm_b16_8 t380;
                                GM_ST(void *, (uint8_t *)&t380, (void *)(gm_ss3f61htk015v));
                                GM_ST(uintptr_t, (uint8_t *)&t380 + 8, (uintptr_t)(6));
                                uint8_t t381 = (gm_eq0iqiubfm7e(((uint8_t *)&t379), ((uint8_t *)&t380)));
                                if (!t381) {
                                    goto gm_lb43;
                                }
                                gm_t8588ma2cr9d7 t382;
                                GM_ST(uint8_t, (uint8_t *)&t382 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t382 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t382 + 4, 0);
                                GM_ST(uint32_t, (uint8_t *)&t382 + 8, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t382 + 0), ((uint8_t)22ULL));
                                gm_l22 = t382;
                                goto gm_lb42;
                            }
gm_lb43:;
                            {
                                gm_b16_8 t383 = gm_l3;
                                gm_b16_8 t384;
                                GM_ST(void *, (uint8_t *)&t384, (void *)(gm_s541nrvershlm));
                                GM_ST(uintptr_t, (uint8_t *)&t384 + 8, (uintptr_t)(6));
                                uint8_t t385 = (gm_eq0iqiubfm7e(((uint8_t *)&t383), ((uint8_t *)&t384)));
                                if (!t385) {
                                    goto gm_lb44;
                                }
                                gm_t8588ma2cr9d7 t386;
                                GM_ST(uint8_t, (uint8_t *)&t386 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t386 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t386 + 4, 0);
                                GM_ST(uint32_t, (uint8_t *)&t386 + 8, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t386 + 0), ((uint8_t)23ULL));
                                gm_l22 = t386;
                                goto gm_lb42;
                            }
gm_lb44:;
                            {
                                gm_b16_8 t387 = gm_l3;
                                gm_b16_8 t388;
                                GM_ST(void *, (uint8_t *)&t388, (void *)(gm_s7uql3407tind));
                                GM_ST(uintptr_t, (uint8_t *)&t388 + 8, (uintptr_t)(9));
                                uint8_t t389 = (gm_eq0iqiubfm7e(((uint8_t *)&t387), ((uint8_t *)&t388)));
                                if (!t389) {
                                    goto gm_lb45;
                                }
                                gm_t8588ma2cr9d7 t390;
                                GM_ST(uint8_t, (uint8_t *)&t390 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t390 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t390 + 4, 0);
                                GM_ST(uint32_t, (uint8_t *)&t390 + 8, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t390 + 0), ((uint8_t)24ULL));
                                gm_l22 = t390;
                                goto gm_lb42;
                            }
gm_lb45:;
                            {
                                gm_t8588ma2cr9d7 t391;
                                GM_ST(uint8_t, (uint8_t *)&t391 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t391 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t391 + 4, 0);
                                GM_ST(uint32_t, (uint8_t *)&t391 + 8, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t391 + 0), ((uint8_t)25ULL));
                                gm_l22 = t391;
                                goto gm_lb42;
                            }
gm_lb46:;
                            gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 196, 31);
                        }
gm_lb42:;
                        gm_t8588ma2cr9d7 t392 = gm_l22;
                        void * t393 = gm_l8;
                        gm_t88rcucg1b7uk t394 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t393);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t395 = (void *)((uint8_t *)&gm_l9);
                        uint64_t t396 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                        gm_b16_8 t397;
                        GM_ST(void *, (uint8_t *)&t397, (void *)(((uint8_t *)t395 + (uintptr_t)((uint64_t)0ULL) * 8)));
                        GM_ST(uintptr_t, (uint8_t *)&t397 + 8, (uintptr_t)(t396));
                        gm_b16_8 t398 = GM_LD(gm_b16_8, ((uint8_t *)&t397));
                        gm_tl874t2atnlh3 t399 = gm_l7;
                        void * t400 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t392, t394, t398, t399);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t400;
                        goto gm_fret;
                    }
                    goto gm_lb1;
                }
gm_lb36:;
            }
            {
                {
                    gm_b16_8 t401 = gm_l3;
                    gm_b16_8 t402;
                    GM_ST(void *, (uint8_t *)&t402, (void *)(gm_s398novfl4jrd));
                    GM_ST(uintptr_t, (uint8_t *)&t402 + 8, (uintptr_t)(4));
                    uint8_t t403 = (gm_eq0iqiubfm7e(((uint8_t *)&t401), ((uint8_t *)&t402)));
                    if (!t403) {
                        goto gm_lb47;
                    }
                    uint8_t t404 = gm_l5;
                    if (t404) {
                        void * t405 = gm_l0;
                        gm_b16_8 t406 = gm_l6;
                        gm_b16_8 t407 = gm_l3;
                        gm_tl874t2atnlh3 t408 = gm_l7;
                        uint8_t t409 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t405, t406, ((uint64_t)0ULL), t407, t408);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t410 = gm_l1;
                        gm_t88rcucg1b7uk t411 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t410));
                        void * t412 = (void *)((uint8_t *)&gm_l9);
                        uint64_t t413 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                        gm_b16_8 t414;
                        GM_ST(void *, (uint8_t *)&t414, (void *)(((uint8_t *)t412 + (uintptr_t)((uint64_t)0ULL) * 8)));
                        GM_ST(uintptr_t, (uint8_t *)&t414 + 8, (uintptr_t)(t413));
                        gm_b16_8 t415 = GM_LD(gm_b16_8, ((uint8_t *)&t414));
                        gm_tl874t2atnlh3 t416 = gm_l7;
                        gm_t8588ma2cr9d7 t417;
                        GM_ST(uint8_t, (uint8_t *)&t417 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t417 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t417 + 4, 0);
                        GM_ST(uint32_t, (uint8_t *)&t417 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t417 + 0), ((uint8_t)26ULL));
                        void * t418 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t417, t411, t415, t416);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t418;
                        goto gm_fret;
                    }
                    goto gm_lb1;
                }
gm_lb47:;
                {
                    gm_b16_8 t419 = gm_l3;
                    gm_b16_8 t420;
                    GM_ST(void *, (uint8_t *)&t420, (void *)(gm_sn6gnsehoh5ru));
                    GM_ST(uintptr_t, (uint8_t *)&t420 + 8, (uintptr_t)(6));
                    uint8_t t421 = (gm_eq0iqiubfm7e(((uint8_t *)&t419), ((uint8_t *)&t420)));
                    if (!t421) {
                        goto gm_lb48;
                    }
                    gm_t88rcucg1b7uk t422 = gm_l2;
                    void * t423 = gm_l8;
                    gm_t88rcucg1b7uk t424 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t423);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t425 = (gm_eq9m6mvtmgde(((uint8_t *)&t422), ((uint8_t *)&t424)));
                    if (t425) {
                        void * t426 = gm_l0;
                        gm_b16_8 t427 = gm_l6;
                        gm_tl874t2atnlh3 t428 = gm_l7;
                        gm_tkn42u7na3t81 t429 = gm_f_type_arg_grgffgp((void *)NULL, cx, NULL, t426, t427, ((uint64_t)0ULL), t428);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l31 = t429;
                        uint8_t t430 = (uint8_t)((((uint8_t *)&gm_l31))[4] != 0);
                        if (t430) {
                            gm_t88rcucg1b7uk t431 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l31));
                            gm_l23 = t431;
                        } else {
                            F.ret = ((void *)0);
                            goto gm_fret;
                        }
                        void * t432 = gm_l8;
                        gm_t88rcucg1b7uk t433 = gm_l23;
                        gm_t8588ma2cr9d7 t434;
                        GM_ST(uint8_t, (uint8_t *)&t434 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t434 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t434 + 4, 0);
                        GM_ST(uint32_t, (uint8_t *)&t434 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t434 + 0), ((uint8_t)13ULL));
                        gm_t88rcucg1b7uk t435 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t432, t433);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t436 = (void *)((uint8_t *)&gm_l9);
                        uint64_t t437 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                        gm_b16_8 t438;
                        GM_ST(void *, (uint8_t *)&t438, (void *)(((uint8_t *)t436 + (uintptr_t)((uint64_t)0ULL) * 8)));
                        GM_ST(uintptr_t, (uint8_t *)&t438 + 8, (uintptr_t)(t437));
                        gm_b16_8 t439 = GM_LD(gm_b16_8, ((uint8_t *)&t438));
                        gm_tl874t2atnlh3 t440 = gm_l7;
                        void * t441 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t434, t435, t439, t440);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t441;
                        goto gm_fret;
                    }
                    goto gm_lb1;
                }
gm_lb48:;
                {
                    {
                        {
                            gm_b16_8 t442 = gm_l3;
                            gm_b16_8 t443;
                            GM_ST(void *, (uint8_t *)&t443, (void *)(gm_sasg9nroe4gk1));
                            GM_ST(uintptr_t, (uint8_t *)&t443 + 8, (uintptr_t)(3));
                            uint8_t t444 = (gm_eq0iqiubfm7e(((uint8_t *)&t442), ((uint8_t *)&t443)));
                            if (!t444) {
                                goto gm_lb51;
                            }
                            goto gm_lb50;
                        }
gm_lb51:;
                        {
                            gm_b16_8 t445 = gm_l3;
                            gm_b16_8 t446;
                            GM_ST(void *, (uint8_t *)&t446, (void *)(gm_sm1kt2tr7m23b));
                            GM_ST(uintptr_t, (uint8_t *)&t446 + 8, (uintptr_t)(3));
                            uint8_t t447 = (gm_eq0iqiubfm7e(((uint8_t *)&t445), ((uint8_t *)&t446)));
                            if (!t447) {
                                goto gm_lb52;
                            }
                            goto gm_lb50;
                        }
gm_lb52:;
                        {
                            gm_b16_8 t448 = gm_l3;
                            gm_b16_8 t449;
                            GM_ST(void *, (uint8_t *)&t449, (void *)(gm_s1mud7cvvuuon));
                            GM_ST(uintptr_t, (uint8_t *)&t449 + 8, (uintptr_t)(3));
                            uint8_t t450 = (gm_eq0iqiubfm7e(((uint8_t *)&t448), ((uint8_t *)&t449)));
                            if (!t450) {
                                goto gm_lb53;
                            }
                            goto gm_lb50;
                        }
gm_lb53:;
                        {
                            gm_b16_8 t451 = gm_l3;
                            gm_b16_8 t452;
                            GM_ST(void *, (uint8_t *)&t452, (void *)(gm_srk226j8lb4rq));
                            GM_ST(uintptr_t, (uint8_t *)&t452 + 8, (uintptr_t)(8));
                            uint8_t t453 = (gm_eq0iqiubfm7e(((uint8_t *)&t451), ((uint8_t *)&t452)));
                            if (!t453) {
                                goto gm_lb54;
                            }
                            goto gm_lb50;
                        }
gm_lb54:;
                        {
                            gm_b16_8 t454 = gm_l3;
                            gm_b16_8 t455;
                            GM_ST(void *, (uint8_t *)&t455, (void *)(gm_st4r3sao0kja5));
                            GM_ST(uintptr_t, (uint8_t *)&t455 + 8, (uintptr_t)(5));
                            uint8_t t456 = (gm_eq0iqiubfm7e(((uint8_t *)&t454), ((uint8_t *)&t455)));
                            if (!t456) {
                                goto gm_lb55;
                            }
                            goto gm_lb50;
                        }
gm_lb55:;
                        {
                            gm_b16_8 t457 = gm_l3;
                            gm_b16_8 t458;
                            GM_ST(void *, (uint8_t *)&t458, (void *)(gm_skgcaidqnnhjr));
                            GM_ST(uintptr_t, (uint8_t *)&t458 + 8, (uintptr_t)(10));
                            uint8_t t459 = (gm_eq0iqiubfm7e(((uint8_t *)&t457), ((uint8_t *)&t458)));
                            if (!t459) {
                                goto gm_lb56;
                            }
                            goto gm_lb50;
                        }
gm_lb56:;
                        {
                            gm_b16_8 t460 = gm_l3;
                            gm_b16_8 t461;
                            GM_ST(void *, (uint8_t *)&t461, (void *)(gm_skj7cpsc7j6g9));
                            GM_ST(uintptr_t, (uint8_t *)&t461 + 8, (uintptr_t)(4));
                            uint8_t t462 = (gm_eq0iqiubfm7e(((uint8_t *)&t460), ((uint8_t *)&t461)));
                            if (!t462) {
                                goto gm_lb57;
                            }
                            goto gm_lb50;
                        }
gm_lb57:;
                        {
                            gm_b16_8 t463 = gm_l3;
                            gm_b16_8 t464;
                            GM_ST(void *, (uint8_t *)&t464, (void *)(gm_sd2iiq8cuaks3));
                            GM_ST(uintptr_t, (uint8_t *)&t464 + 8, (uintptr_t)(4));
                            uint8_t t465 = (gm_eq0iqiubfm7e(((uint8_t *)&t463), ((uint8_t *)&t464)));
                            if (!t465) {
                                goto gm_lb58;
                            }
                            goto gm_lb50;
                        }
gm_lb58:;
                        goto gm_lb49;
                    }
gm_lb50:;
                    void * t466 = gm_l0;
                    gm_tl874t2atnlh3 t467 = gm_l7;
                    gm_b16_8 t468 = gm_l3;
                    gm_tnpifv20irnph t469;
                    GM_ST(gm_b16_8, ((uint8_t *)&t469 + 0), t468);
                    gm_f_format_7b9kq2t((void *)&gm_l32, (void *)NULL, cx, NULL, t469);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t470 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                    if (!t470) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 8, gm_srcngug8co99ok1, 212, 111);
                    gm_b16_8 t471 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l32) + 8));
                    gm_b16_8 t472 = GM_LD(gm_b16_8, ((uint8_t *)&t471));
                    void * t473 = gm_f_unsupported_r3r7qdh((void *)NULL, cx, NULL, t466, t467, t472);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t473;
                    goto gm_fret;
                }
gm_lb49:;
            }
            {
                {
                    goto gm_lb1;
                }
gm_lb59:;
                gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 146, 5);
            }
        }
gm_lb1:;
    }
    {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_numeric_pair_54urfaa(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_numeric_pair_54urfaa F;
    void * gm_l0; /* cx */
    void * gm_l1; /* recv */
    gm_b16_8 gm_l2; /* n */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* y */
    gm_t88rcucg1b7uk gm_l6; /* ty */
    gm_t8588ma2cr9d7 gm_l7; /* op */
    gm_t88rcucg1b7uk gm_l8; /* rty */
    gm_t5fru0lp11n7t gm_l9; /* <temporary array> */
    GM_STACK_CHECK(&F, 818u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tabb710d9mq2e gm_l10;
    gm_tftb1odc739t5 gm_l11;
    void * t6 = gm_l0;
    gm_b16_8 t7 = gm_l3;
    gm_b16_8 t8 = gm_l2;
    gm_tl874t2atnlh3 t9 = gm_l4;
    uint8_t t10 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t6, t7, ((uint64_t)1ULL), t8, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t11 = (uint8_t)!t10;
    if (t11) {
        void * t12 = gm_l0;
        void * t13 = GM_LD(void *, ((uint8_t *)t12));
        gm_tl874t2atnlh3 t14 = gm_l4;
        void * t15 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t15;
        goto gm_fret;
    }
    void * t16 = gm_l0;
    gm_b16_8 t17 = gm_l3;
    void * t18 = gm_l1;
    gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t18));
    gm_tl874t2atnlh3 t20 = gm_l4;
    void * t21 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t16, t17, ((uint64_t)0ULL), t19, t20);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t21;
    void * t22 = gm_l0;
    void * t23 = gm_l1;
    gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t23));
    void * t25 = gm_l5;
    gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t25));
    gm_tl874t2atnlh3 t27 = gm_l4;
    gm_b16_8 t28 = gm_l2;
    gm_tnpifv20irnph t29;
    GM_ST(gm_b16_8, ((uint8_t *)&t29 + 0), t28);
    gm_f_format_0cb2lu1((void *)&gm_l10, (void *)NULL, cx, NULL, t29);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_srcngug8co99ok1, 248, 50);
    gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
    gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
    gm_t88rcucg1b7uk t33 = gm_f_unify_operands_5sp7j1j((void *)NULL, cx, NULL, t22, t24, t26, t27, t32);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l6 = t33;
    gm_t8588ma2cr9d7 t34;
    GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t34 + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&t34 + 8, 0);
    GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)16ULL));
    gm_l7 = t34;
    gm_t88rcucg1b7uk t35 = gm_l6;
    gm_l8 = t35;
    {
        {
            gm_b16_8 t36 = gm_l2;
            gm_b16_8 t37;
            GM_ST(void *, (uint8_t *)&t37, (void *)(gm_sm3o0uerjumpm));
            GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(3));
            uint8_t t38 = (gm_eq0iqiubfm7e(((uint8_t *)&t36), ((uint8_t *)&t37)));
            if (!t38) {
                goto gm_lb2;
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_b16_8 t39 = gm_l2;
            gm_b16_8 t40;
            GM_ST(void *, (uint8_t *)&t40, (void *)(gm_s2mtoab140jp6));
            GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(3));
            uint8_t t41 = (gm_eq0iqiubfm7e(((uint8_t *)&t39), ((uint8_t *)&t40)));
            if (!t41) {
                goto gm_lb3;
            }
            gm_t8588ma2cr9d7 t42;
            GM_ST(uint8_t, (uint8_t *)&t42 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t42 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t42 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t42 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)17ULL));
            gm_l7 = t42;
            goto gm_lb1;
        }
gm_lb3:;
        {
            {
                void * t43 = gm_l0;
                void * t44 = (void *)(((uint8_t *)t43) + 320);
                gm_t88rcucg1b7uk t45 = gm_l6;
                gm_tl874t2atnlh3 t46 = gm_l4;
                gm_b16_8 t47;
                GM_ST(void *, (uint8_t *)&t47, (void *)(gm_s7u0p3hthk80r));
                GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(8));
                gm_t91vm1g0k2rpm t48;
                GM_ST(uint16_t, (uint8_t *)&t48 + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&t48 + 36, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t48 + 0), t45);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t48 + 4), t46);
                GM_ST(gm_b16_8, ((uint8_t *)&t48 + 16), t47);
                GM_ST(uint8_t, ((uint8_t *)&t48 + 32), ((uint8_t)1));
                GM_ST(uint8_t, ((uint8_t *)&t48 + 33), ((uint8_t)0));
                gm_tpm220j5qhjke t49;
                GM_ST(uint8_t, (uint8_t *)&t49 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t49 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t49 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)3ULL));
                GM_ST(gm_t91vm1g0k2rpm, ((uint8_t *)&t49 + 8), t48);
                gm_tftb1odc739t5 t50 = gm_f_push_7koe9lk(t44, &t49);
                gm_l11 = t50;
            }
            uint8_t t51 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
            if (!t51) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srcngug8co99ok1, 255, 13);
            gm_t8588ma2cr9d7 t52;
            GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t52 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)20ULL));
            gm_l7 = t52;
            void * t53 = gm_l0;
            void * t54 = GM_LD(void *, ((uint8_t *)t53));
            void * t55 = (void *)(((uint8_t *)t54) + 8);
            void * t56 = gm_l0;
            void * t57 = (void *)(((uint8_t *)t56) + 96);
            void * t58 = gm_l0;
            void * t59 = GM_LD(void *, ((uint8_t *)t58));
            gm_t88rcucg1b7uk t60 = gm_l6;
            gm_t88rcucg1b7uk t61 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t57, t59, t60);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t62 = gm_f_unsigned_of_n48qlse((void *)NULL, cx, NULL, t55, t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t62;
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 251, 5);
    }
gm_lb1:;
    gm_t8588ma2cr9d7 t63 = gm_l7;
    gm_t88rcucg1b7uk t64 = gm_l8;
    void * t65 = gm_l1;
    void * t66 = gm_l5;
    gm_t5fru0lp11n7t t67;
    GM_ST(void *, ((uint8_t *)&t67 + 0), t65);
    GM_ST(void *, ((uint8_t *)&t67 + 8), t66);
    gm_l9 = t67;
    void * t68 = (void *)((uint8_t *)&gm_l9);
    uint64_t t69 = ((uintptr_t)((uint64_t)2ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t70;
    GM_ST(void *, (uint8_t *)&t70, (void *)(((uint8_t *)t68 + (uintptr_t)((uint64_t)0ULL) * 8)));
    GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(t69));
    gm_tl874t2atnlh3 t71 = gm_l4;
    void * t72 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t63, t64, t70, t71);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t72;
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_op_method_82pb0m6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, uint64_t a4, const void * a5, gm_b16_8 a6, gm_tl874t2atnlh3 a7) {
    struct gm_fr_op_method_82pb0m6 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* recv */
    gm_t88rcucg1b7uk gm_l2; /* rty */
    gm_t88rcucg1b7uk gm_l3; /* s */
    uint64_t gm_l4; /* module */
    gm_t08olq97rscn3 gm_l5; /* name */
    gm_b16_8 gm_l6; /* args */
    gm_tl874t2atnlh3 gm_l7; /* span */
    void * gm_l8; /* tys */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_t3877pk7lv748 gm_l11; /* n */
    uint32_t gm_l12; /* eff */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* op_i */
    gm_t88rcucg1b7uk gm_l18; /* t */
    gm_t88rcucg1b7uk gm_l19; /* x */
    void * gm_l20; /* r */
    gm_t6j6smj7qoctu gm_l21; /* bound */
    gm_tiilmfd16v3fc gm_l23; /* eref */
    void * gm_l24; /* call */
    GM_STACK_CHECK(&F, 2083u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l6 = a6;
    gm_l7 = a7;
    gm_t6qooom8qa2do gm_l25;
    uint8_t gm_l26;
    gm_tdvhflnc2f5o8 gm_l27;
    uint8_t gm_l28;
    gm_t53at80pfdus3 gm_l29;
    void * gm_l30;
    gm_ttu09lij36f8c gm_l31;
    gm_tftb1odc739t5 gm_l32;
    gm_tftb1odc739t5 gm_l33;
    gm_tftb1odc739t5 gm_l34;
    gm_tftb1odc739t5 gm_l35;
    void * t16 = gm_l0;
    void * t17 = GM_LD(void *, ((uint8_t *)t16));
    void * t18 = (void *)(((uint8_t *)t17) + 8);
    gm_l8 = t18;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    void * t21 = GM_LD(void *, ((uint8_t *)t20));
    void * t22 = GM_LD(void *, (((uint8_t *)t21) + 200));
    uint64_t t23 = GM_LD(uint64_t, ((((uint8_t *)t21) + 200) + 8));
    uint64_t t24 = gm_l4;
    if ((uintptr_t)t24 >= (uintptr_t)(t23)) gm_trap_index((uintptr_t)t24, t23, gm_srcngug8co99ok1, 571, 14);
    gm_b16_8 t25 = GM_LD(gm_b16_8, ((((uint8_t *)t22) + (uintptr_t)t24 * 184) + 16));
    gm_l9 = t25;
    gm_l10 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t26 = gm_l10;
            uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
            uint8_t t28 = (t26 < t27);
            if (!t28) {
                goto gm_lb1;
            }
            {
                void * t29 = GM_LD(void *, ((uint8_t *)&gm_l9));
                uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                uint64_t t31 = gm_l10;
                {
                    {
                        memmove(((uint8_t *)&gm_l11), (((uint8_t *)t29) + (uintptr_t)t31 * 24), 24);
                        {
                            {
                                {
                                    void * t32 = gm_l0;
                                    void * t33 = GM_LD(void *, ((uint8_t *)t32));
                                    uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 16));
                                    gm_f_resolve_decl_pus042v((void *)&gm_l25, (void *)NULL, cx, NULL, t33, t34);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
                                uint8_t t36 = (uint8_t)(t35 == ((uint8_t)2ULL));
                                if (!t36) {
                                    goto gm_lb7;
                                }
                                uint32_t t37 = GM_LD(uint32_t, (((uint8_t *)&gm_l25) + 8));
                                gm_l12 = t37;
                                {
                                    void * t38 = gm_l0;
                                    void * t39 = GM_LD(void *, ((uint8_t *)t38));
                                    void * t40 = GM_LD(void *, (((uint8_t *)t39) + 1048));
                                    uint64_t t41 = GM_LD(uint64_t, ((((uint8_t *)t39) + 1048) + 8));
                                    uint32_t t42 = gm_l12;
                                    uint64_t t43 = (uint64_t)t42;
                                    if ((uintptr_t)t43 >= (uintptr_t)(t41)) gm_trap_index((uintptr_t)t43, t41, gm_srcngug8co99ok1, 573, 17);
                                    memmove(((uint8_t *)&F.l13), (((uint8_t *)t40) + (uintptr_t)t43 * 56), 56);
                                }
                                uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&F.l13) + 16));
                                uint64_t t45 = gm_l4;
                                uint8_t t46 = (!(t44 == t45));
                                if (t46) {
                                    gm_l26 = ((uint8_t)1);
                                } else {
                                    uint64_t t47 = GM_LD(uint64_t, ((((uint8_t *)&F.l13) + 24) + 8));
                                    uint8_t t48 = (t47 > ((uint64_t)0ULL));
                                    gm_l26 = t48;
                                }
                                uint8_t t49 = gm_l26;
                                if (t49) {
                                    goto gm_lb3;
                                }
                                void * t50 = gm_l0;
                                void * t51 = GM_LD(void *, ((uint8_t *)t50));
                                uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 16));
                                void * t53 = gm_l0;
                                uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)t53) + 8));
                                uint8_t t55 = gm_f_decl_visible_bd168ti(t51, t52, t54);
                                uint8_t t56 = (uint8_t)!t55;
                                if (t56) {
                                    goto gm_lb3;
                                }
                                gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 40));
                                gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
                                gm_l14 = t58;
                                gm_l15 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t59 = gm_l15;
                                        uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                                        uint8_t t61 = (t59 < t60);
                                        if (!t61) {
                                            goto gm_lb8;
                                        }
                                        {
                                            {
                                                uint64_t t62 = gm_l15;
                                                void * t63 = GM_LD(void *, ((uint8_t *)&gm_l14));
                                                uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                                                uint64_t t65 = gm_l15;
                                                gm_tv0k2vg3054a6 t66 = GM_LD(gm_tv0k2vg3054a6, (((uint8_t *)t63) + (uintptr_t)t65 * 48));
                                                GM_ST(uint64_t, ((uint8_t *)&gm_l27 + 0), t62);
                                                GM_ST(gm_tv0k2vg3054a6, ((uint8_t *)&gm_l27 + 8), t66);
                                            }
                                            {
                                                {
                                                    uint64_t t67 = GM_LD(uint64_t, ((uint8_t *)&gm_l27));
                                                    gm_l16 = t67;
                                                    {
                                                        memmove(((uint8_t *)&F.l17), (((uint8_t *)&gm_l27) + 8), 48);
                                                    }
                                                    gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&F.l17));
                                                    gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)a5));
                                                    uint8_t t70 = (!gm_eq0iqiubfm7e(((uint8_t *)&t68), ((uint8_t *)&t69)));
                                                    if (t70) {
                                                        gm_l28 = ((uint8_t)1);
                                                    } else {
                                                        uint64_t t71 = GM_LD(uint64_t, ((((uint8_t *)&F.l17) + 24) + 8));
                                                        uint8_t t72 = ((t71 == ((uint64_t)0ULL)));
                                                        gm_l28 = t72;
                                                    }
                                                    uint8_t t73 = gm_l28;
                                                    if (t73) {
                                                        goto gm_lb10;
                                                    }
                                                    void * t74 = GM_LD(void *, (((uint8_t *)&F.l17) + 24));
                                                    uint64_t t75 = GM_LD(uint64_t, ((((uint8_t *)&F.l17) + 24) + 8));
                                                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t75)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t75, gm_srcngug8co99ok1, 578, 18);
                                                    gm_t88rcucg1b7uk t76 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t74) + (uintptr_t)((uint64_t)0ULL) * 56) + 16));
                                                    gm_l18 = t76;
                                                    {
                                                        {
                                                            void * t77 = gm_l8;
                                                            gm_t88rcucg1b7uk t78 = gm_l18;
                                                            gm_f_kind_jcnt4hm((void *)&gm_l29, t77, GM_CONV(gm_csguankko4lmhd, t78));
                                                            uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&gm_l29) + 0));
                                                            uint8_t t80 = (uint8_t)(t79 == ((uint8_t)8ULL));
                                                            if (!t80) {
                                                                goto gm_lb14;
                                                            }
                                                            gm_t88rcucg1b7uk t81 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l29) + 8) + 4));
                                                            gm_l19 = t81;
                                                            gm_t88rcucg1b7uk t82 = gm_l19;
                                                            gm_l18 = t82;
                                                            goto gm_lb13;
                                                        }
gm_lb14:;
                                                    }
gm_lb13:;
                                                    gm_t88rcucg1b7uk t83 = gm_l18;
                                                    gm_t88rcucg1b7uk t84 = gm_l3;
                                                    uint8_t t85 = (!gm_eq9m6mvtmgde(((uint8_t *)&t83), ((uint8_t *)&t84)));
                                                    if (t85) {
                                                        goto gm_lb10;
                                                    }
                                                    void * t86 = gm_l0;
                                                    void * t87 = GM_LD(void *, ((uint8_t *)t86));
                                                    uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 16));
                                                    void * t89 = gm_l0;
                                                    uint64_t t90 = GM_LD(uint64_t, (((uint8_t *)t89) + 8));
                                                    gm_b0_1 t91 = gm_f_note_use_from_2lmujsq((void *)NULL, cx, NULL, t87, t88, t90);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    {
                                                        void * t92 = gm_l0;
                                                        void * t93 = gm_l1;
                                                        gm_t88rcucg1b7uk t94 = gm_l2;
                                                        void * t95 = GM_LD(void *, (((uint8_t *)&F.l17) + 24));
                                                        uint64_t t96 = GM_LD(uint64_t, ((((uint8_t *)&F.l17) + 24) + 8));
                                                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t96)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t96, gm_srcngug8co99ok1, 582, 48);
                                                        gm_t88rcucg1b7uk t97 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t95) + (uintptr_t)((uint64_t)0ULL) * 56) + 16));
                                                        gm_tl874t2atnlh3 t98 = gm_l7;
                                                        void * t99 = gm_f_adjust_receiver_2j7250l((void *)NULL, cx, NULL, t92, t93, t94, t97, &(*(gm_t08olq97rscn3 *)((uint8_t *)a5)), t98);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_l30 = t99;
                                                    }
                                                    uint8_t t100 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l30)) != 0);
                                                    if (t100) {
                                                        void * t101 = GM_LD(void *, ((uint8_t *)&gm_l30));
                                                        gm_l20 = t101;
                                                    } else {
                                                        void * t102 = gm_l0;
                                                        void * t103 = GM_LD(void *, ((uint8_t *)t102));
                                                        gm_tl874t2atnlh3 t104 = gm_l7;
                                                        void * t105 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t103, t104);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        F.ret = t105;
                                                        goto gm_fret;
                                                    }
                                                    {
                                                        void * t106 = gm_l0;
                                                        void * t107 = GM_LD(void *, (((uint8_t *)&F.l17) + 24));
                                                        uint64_t t108 = GM_LD(uint64_t, ((((uint8_t *)&F.l17) + 24) + 8));
                                                        uint64_t t109 = (uintptr_t)((uint64_t)1ULL);
                                                        uint64_t t110 = (uintptr_t)t108;
                                                        if (t109 > t110 || t110 > (uintptr_t)t108) gm_trap_slice(t109, t110, t108, gm_srcngug8co99ok1, 583, 35);
                                                        uint64_t t111 = ((uintptr_t)t108) - (uintptr_t)((uint64_t)1ULL);
                                                        gm_b16_8 t112;
                                                        GM_ST(void *, (uint8_t *)&t112, (void *)(((uint8_t *)t107 + (uintptr_t)((uint64_t)1ULL) * 56)));
                                                        GM_ST(uintptr_t, (uint8_t *)&t112 + 8, (uintptr_t)(t111));
                                                        gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)&t112));
                                                        uint64_t t114 = gm_l4;
                                                        gm_b16_8 t115 = gm_l6;
                                                        gm_tl874t2atnlh3 t116 = gm_l7;
                                                        gm_b16_8 t117;
                                                        GM_ST(void *, (uint8_t *)&t117, (void *)(gm_sk6ueggk5ji6v));
                                                        GM_ST(uintptr_t, (uint8_t *)&t117 + 8, (uintptr_t)(9));
                                                        gm_f_bind_args_dnih8ih((void *)&gm_l31, (void *)NULL, cx, NULL, t106, t113, t114, t115, t116, t117);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                    }
                                                    uint8_t t118 = (uint8_t)((((uint8_t *)&gm_l31))[32] != 0);
                                                    if (t118) {
                                                        memmove(((uint8_t *)&gm_l21), ((uint8_t *)&gm_l31), 32);
                                                    } else {
                                                        void * t119 = gm_l0;
                                                        void * t120 = GM_LD(void *, ((uint8_t *)t119));
                                                        gm_tl874t2atnlh3 t121 = gm_l7;
                                                        void * t122 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t120, t121);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        F.ret = t122;
                                                        goto gm_fret;
                                                    }
                                                    {
                                                        gm_trqfvmjtt4sij t123;
                                                        gm_f_list_vp1djv1_2((void *)&t123, (void *)NULL, cx, NULL);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        F.l22 = t123;
                                                    }
                                                    {
                                                        void * t124 = (void *)((uint8_t *)&F.l22);
                                                        void * t125 = gm_l20;
                                                        gm_tftb1odc739t5 t126 = gm_f_push_d943vgp_2(t124, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t125)));
                                                        gm_l32 = t126;
                                                    }
                                                    uint8_t t127 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                                                    if (!t127) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 1, gm_srcngug8co99ok1, 585, 13);
                                                    void * t128 = (void *)((uint8_t *)&F.l22);
                                                    gm_b16_8 t129 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l21));
                                                    gm_b16_8 t130 = GM_LD(gm_b16_8, ((uint8_t *)&t129));
                                                    gm_tftb1odc739t5 t131 = gm_f_extend_gig5o6g(t128, t130);
                                                    gm_l33 = t131;
                                                    uint8_t t132 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                                                    if (!t132) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 1, gm_srcngug8co99ok1, 586, 13);
                                                    {
                                                        uint32_t t133 = gm_l12;
                                                        gm_b16_8 t134 = gm_f_empty_slice_fh7svmj();
                                                        gm_b16_8 t135 = GM_LD(gm_b16_8, ((uint8_t *)&t134));
                                                        GM_ST(uint32_t, (uint8_t *)&gm_l23 + 4, 0);
                                                        GM_ST(uint32_t, ((uint8_t *)&gm_l23 + 0), t133);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&gm_l23 + 8), t135);
                                                    }
                                                    {
                                                        void * t136 = gm_l0;
                                                        void * t137 = (void *)(((uint8_t *)t136) + 192);
                                                        gm_tl874t2atnlh3 t138 = gm_l7;
                                                        gm_tiilmfd16v3fc t139 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l23));
                                                        gm_tj44oivvogvtl t140;
                                                        GM_ST(uint32_t, (uint8_t *)&t140 + 36, 0);
                                                        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t140 + 0), t139);
                                                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t140 + 24), t138);
                                                        gm_tftb1odc739t5 t141 = gm_f_push_3onuaru(t137, &t140);
                                                        gm_l34 = t141;
                                                    }
                                                    uint8_t t142 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                                                    if (!t142) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 1, gm_srcngug8co99ok1, 588, 13);
                                                    gm_t9qkh0rsooc8d t143 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l17) + 16));
                                                    uint8_t t144 = GM_LD(uint8_t, (((uint8_t *)&t143) + 0));
                                                    uint8_t t145 = (uint8_t)((t144 == ((uint8_t)0ULL)));
                                                    gm_t9qkh0rsooc8d t146;
                                                    GM_ST(uint8_t, ((uint8_t *)&t146 + 0), ((uint8_t)0ULL));
                                                    if (t145) {
                                                        void * t147 = gm_l0;
                                                        void * t148 = (void *)(((uint8_t *)t147) + 536);
                                                        gm_tl874t2atnlh3 t149 = gm_l7;
                                                        gm_tftb1odc739t5 t150 = gm_f_push_0mk7udh(t148, GM_CONV(gm_cseahvg8v7p0vs, t149));
                                                        gm_l35 = t150;
                                                        uint8_t t151 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
                                                        if (!t151) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 1, gm_srcngug8co99ok1, 589, 32);
                                                    }
                                                    {
                                                        gm_t88rcucg1b7uk t152 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l17) + 40));
                                                        uint64_t t153 = gm_l16;
                                                        gm_tiilmfd16v3fc t154 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l23));
                                                        uint32_t t155 = (uint32_t)t153;
                                                        gm_t419mo45g7j0a t156;
                                                        GM_ST(uint32_t, (uint8_t *)&t156 + 28, 0);
                                                        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t156 + 0), t154);
                                                        GM_ST(uint32_t, ((uint8_t *)&t156 + 24), t155);
                                                        gm_b16_8 t157 = GM_LD(gm_b16_8, ((uint8_t *)&F.l22));
                                                        gm_tsth9qu1dv0g3 t158;
                                                        GM_ST(uint8_t, (uint8_t *)&t158 + 1, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t158 + 2, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&t158 + 4, 0);
                                                        GM_ST(uint8_t, ((uint8_t *)&t158 + 0), ((uint8_t)1ULL));
                                                        GM_ST(gm_t419mo45g7j0a, ((uint8_t *)&t158 + 8), t156);
                                                        gm_to76jli3bqddq t159;
                                                        GM_ST(gm_tsth9qu1dv0g3, ((uint8_t *)&t159 + 0), t158);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t159 + 40), t157);
                                                        gm_tl874t2atnlh3 t160 = gm_l7;
                                                        gm_to0f5qn75cgg8 t161;
                                                        GM_ST(uint8_t, (uint8_t *)&t161 + 1, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t161 + 2, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&t161 + 4, 0);
                                                        GM_ST(uint8_t, ((uint8_t *)&t161 + 0), ((uint8_t)34ULL));
                                                        GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t161 + 8), t159);
                                                        void * t162 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t152, &t161, t160);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_l24 = t162;
                                                    }
                                                    void * t163 = gm_l0;
                                                    gm_b16_8 t164 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 16));
                                                    void * t165 = gm_l24;
                                                    void * t166 = gm_f_finish_call_kjknkfg((void *)NULL, cx, NULL, t163, t164, t165, ((uint8_t)1));
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    F.ret = t166;
                                                    goto gm_fret;
                                                }
gm_lb12:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 576, 9);
                                            }
gm_lb11:;
                                        }
gm_lb10:;
                                        uint64_t t167 = gm_l15;
                                        uint64_t r168; bool o169 = __builtin_add_overflow(t167, ((uint64_t)1ULL), &r168);
                                        uint64_t t170 = r168;
                                        gm_l15 = t170;
                                    }
                                }
gm_lb8:;
                                goto gm_lb6;
                            }
gm_lb7:;
                            goto gm_lb3;
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 571, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t171 = gm_l10;
            uint64_t r172; bool o173 = __builtin_add_overflow(t171, ((uint64_t)1ULL), &r172);
            uint64_t t174 = r172;
            gm_l10 = t174;
        }
    }
gm_lb1:;
    F.ret = ((void *)0);
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_param_with_qfvlipt(void * a0, gm_csguankko4lmhd a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) void * gm_f_perform_named_h7gr9vi(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_perform_named_h7gr9vi F;
    void * gm_l0; /* cx */
    gm_t716e5hdn64ba gm_l1; /* named */
    gm_t08olq97rscn3 gm_l2; /* name */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    uint32_t gm_l5; /* eff */
    gm_tqu3m2l3n8mnc gm_l6; /* inst */
    uint32_t gm_l7; /* e */
    gm_tiilmfd16v3fc gm_l8; /* r */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    gm_tiilmfd16v3fc gm_l14; /* r */
    GM_STACK_CHECK(&F, 880u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tdvhflnc2f5o8 gm_l15;
    gm_tabb710d9mq2e gm_l16;
    gm_l5 = ((uint32_t)0ULL);
    {
        memset(&gm_l6, 0, sizeof gm_l6);
    }
    {
        {
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t13 = (uint8_t)(t12 == ((uint8_t)0ULL));
            if (!t13) {
                goto gm_lb2;
            }
            uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)a1) + 8));
            gm_l7 = t14;
            uint32_t t15 = gm_l7;
            gm_l5 = t15;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t17 = (uint8_t)(t16 == ((uint8_t)1ULL));
            if (!t17) {
                goto gm_lb3;
            }
            {
                memmove(((uint8_t *)&gm_l8), (((uint8_t *)a1) + 8), 24);
            }
            uint32_t t18 = GM_LD(uint32_t, ((uint8_t *)&gm_l8));
            gm_l5 = t18;
            {
                gm_tiilmfd16v3fc t19 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l8));
                GM_ST(uint8_t, (uint8_t *)&gm_l6 + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&gm_l6 + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&gm_l6 + 28, 0);
                GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l6), t19);
                ((uint8_t *)&gm_l6)[24] = 1;
            }
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 644, 5);
    }
gm_lb1:;
    {
        void * t20 = gm_l0;
        void * t21 = GM_LD(void *, ((uint8_t *)t20));
        void * t22 = GM_LD(void *, (((uint8_t *)t21) + 1048));
        uint64_t t23 = GM_LD(uint64_t, ((((uint8_t *)t21) + 1048) + 8));
        uint32_t t24 = gm_l5;
        uint64_t t25 = (uint64_t)t24;
        if ((uintptr_t)t25 >= (uintptr_t)(t23)) gm_trap_index((uintptr_t)t25, t23, gm_srcngug8co99ok1, 651, 13);
        memmove(((uint8_t *)&F.l9), (((uint8_t *)t22) + (uintptr_t)t25 * 56), 56);
    }
    gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 40));
    gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
    gm_l10 = t27;
    gm_l11 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t28 = gm_l11;
            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
            uint8_t t30 = (t28 < t29);
            if (!t30) {
                goto gm_lb4;
            }
            {
                {
                    uint64_t t31 = gm_l11;
                    void * t32 = GM_LD(void *, ((uint8_t *)&gm_l10));
                    uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                    uint64_t t34 = gm_l11;
                    gm_tv0k2vg3054a6 t35 = GM_LD(gm_tv0k2vg3054a6, (((uint8_t *)t32) + (uintptr_t)t34 * 48));
                    GM_ST(uint64_t, ((uint8_t *)&gm_l15 + 0), t31);
                    GM_ST(gm_tv0k2vg3054a6, ((uint8_t *)&gm_l15 + 8), t35);
                }
                {
                    {
                        uint64_t t36 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
                        gm_l12 = t36;
                        memmove(((uint8_t *)&F.l13), (((uint8_t *)&gm_l15) + 8), 48);
                        gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                        gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                        uint8_t t39 = (!gm_eq0iqiubfm7e(((uint8_t *)&t37), ((uint8_t *)&t38)));
                        if (t39) {
                            goto gm_lb6;
                        }
                        {
                            {
                                uint8_t t40 = (uint8_t)((((uint8_t *)&gm_l6))[24] != 0);
                                if (!t40) {
                                    goto gm_lb10;
                                }
                                {
                                    memmove(((uint8_t *)&gm_l14), ((uint8_t *)&gm_l6), 24);
                                }
                                {
                                    void * t41 = gm_l0;
                                    uint64_t t42 = gm_l12;
                                    gm_tiilmfd16v3fc t43 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l14));
                                    uint32_t t44 = (uint32_t)t42;
                                    gm_b16_8 t45 = gm_l3;
                                    gm_tl874t2atnlh3 t46 = gm_l4;
                                    void * t47 = gm_f_perform_op_as_9ncunsc((void *)NULL, cx, NULL, t41, &t43, t44, t45, t46);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.ret = t47;
                                    goto gm_fret;
                                }
                            }
gm_lb10:;
                        }
gm_lb9:;
                        void * t48 = gm_l0;
                        uint32_t t49 = gm_l5;
                        uint64_t t50 = gm_l12;
                        uint32_t t51 = (uint32_t)t50;
                        gm_b16_8 t52 = gm_l3;
                        gm_tl874t2atnlh3 t53 = gm_l4;
                        void * t54 = gm_f_perform_op_1nih4r3((void *)NULL, cx, NULL, t48, t49, t51, t52, t53);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t54;
                        goto gm_fret;
                    }
gm_lb8:;
                    gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 652, 5);
                }
gm_lb7:;
            }
gm_lb6:;
            uint64_t t55 = gm_l11;
            uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
            uint64_t t58 = r56;
            gm_l11 = t58;
        }
    }
gm_lb4:;
    {
        void * t59 = gm_l0;
        gm_tl874t2atnlh3 t60 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
        gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
        gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)a2));
        gm_t6j6smj7qoctu t63;
        GM_ST(gm_b16_8, ((uint8_t *)&t63 + 0), t61);
        GM_ST(gm_b16_8, ((uint8_t *)&t63 + 16), t62);
        gm_f_format_p9hcrrd((void *)&gm_l16, (void *)NULL, cx, NULL, &t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t64 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
        if (!t64) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_srcngug8co99ok1, 657, 25);
        gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
        gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
        gm_b0_1 t67 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t59, t60, t66);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    void * t68 = gm_l0;
    void * t69 = GM_LD(void *, ((uint8_t *)t68));
    gm_tl874t2atnlh3 t70 = gm_l4;
    void * t71 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t69, t70);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t71;
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_pointer_cast_6siqq5h(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_pointer_cast_6siqq5h F;
    void * gm_l0; /* cx */
    void * gm_l1; /* recv */
    gm_t88rcucg1b7uk gm_l2; /* rty */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_t88rcucg1b7uk gm_l6; /* t */
    gm_closure gm_l8; /* same */
    uint8_t gm_l9; /* ok */
    gm_t88rcucg1b7uk gm_l10; /* a */
    gm_t88rcucg1b7uk gm_l11; /* b */
    gm_t88rcucg1b7uk gm_l12; /* i */
    gm_t88rcucg1b7uk gm_l14; /* i */
    gm_b16_8 gm_l18; /* why */
    gm_tn3r4rvlh0rsv gm_l19; /* <temporary array> */
    GM_STACK_CHECK(&F, 1397u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tkn42u7na3t81 gm_l20;
    gm_t53at80pfdus3 gm_l21;
    gm_t53at80pfdus3 gm_l22;
    uint8_t gm_l23;
    gm_t53at80pfdus3 gm_l24;
    gm_tabb710d9mq2e gm_l25;
    gm_t53at80pfdus3 gm_l26;
    gm_t53at80pfdus3 gm_l27;
    gm_th2lqbo77pvsc gm_l28;
    gm_tabb710d9mq2e gm_l29;
    void * t17 = gm_l0;
    void * t18 = GM_LD(void *, ((uint8_t *)t17));
    void * t19 = (void *)(((uint8_t *)t18) + 8);
    F.l5 = t19;
    void * t20 = gm_l0;
    gm_b16_8 t21 = gm_l3;
    gm_tl874t2atnlh3 t22 = gm_l4;
    gm_tkn42u7na3t81 t23 = gm_f_type_arg_grgffgp((void *)NULL, cx, NULL, t20, t21, ((uint64_t)0ULL), t22);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l20 = t23;
    uint8_t t24 = (uint8_t)((((uint8_t *)&gm_l20))[4] != 0);
    if (t24) {
        gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l20));
        gm_l6 = t25;
    } else {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    gm_closure t26 = ((gm_closure){(void *)gm_f_kind_u4oqjin, (void *)&F.h, cx});
    F.l7 = t26;
    gm_closure t27 = ((gm_closure){(void *)gm_f_same_1qdglea, (void *)&F.h, cx});
    gm_l8 = t27;
    gm_l9 = ((uint8_t)0);
    {
        {
            void * t28 = F.l5;
            gm_t88rcucg1b7uk t29 = gm_l2;
            gm_f_kind_jcnt4hm((void *)&gm_l21, t28, GM_CONV(gm_csguankko4lmhd, t29));
            uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
            uint8_t t31 = (uint8_t)(t30 == ((uint8_t)13ULL));
            if (!t31) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l21) + 8));
            gm_l10 = t32;
            {
                {
                    void * t33 = F.l5;
                    gm_t88rcucg1b7uk t34 = gm_l6;
                    gm_f_kind_jcnt4hm((void *)&gm_l22, t33, GM_CONV(gm_csguankko4lmhd, t34));
                    uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                    uint8_t t36 = (uint8_t)(t35 == ((uint8_t)13ULL));
                    if (!t36) {
                        goto gm_lb4;
                    }
                    gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l22) + 8));
                    gm_l11 = t37;
                    gm_closure t38 = gm_l8;
                    gm_t88rcucg1b7uk t39 = gm_l10;
                    gm_t88rcucg1b7uk t40 = gm_l11;
                    gm_closure t41 = t38;
                    gm_node *ccx42 = t41.cx;
                    void * t43 = t41.code;
                    uint8_t t44;
                    if (GM_PTR_TAGGED(t43)) {
                        t44 = ((uint8_t (*)(gm_csguankko4lmhd, gm_csguankko4lmhd))GM_PTR_UNTAG(t43))(GM_CONV(gm_csguankko4lmhd, t39), GM_CONV(gm_csguankko4lmhd, t40));
                    } else {
                        t44 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk, gm_t88rcucg1b7uk))t43)(t41.env, ccx42, NULL, t39, t40);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l9 = t44;
                    goto gm_lb3;
                }
gm_lb4:;
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
        void * t45 = F.l5;
        gm_t88rcucg1b7uk t46 = gm_l6;
        gm_f_kind_jcnt4hm((void *)&gm_l24, t45, GM_CONV(gm_csguankko4lmhd, t46));
        {
            {
                uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                uint8_t t48 = (uint8_t)(t47 == ((uint8_t)13ULL));
                if (!t48) {
                    goto gm_lb6;
                }
                gm_l23 = ((uint8_t)0);
                goto gm_lb5;
            }
gm_lb6:;
            {
                gm_l23 = ((uint8_t)1);
                goto gm_lb5;
            }
gm_lb7:;
            gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 284, 15);
        }
gm_lb5:;
        uint8_t t49 = gm_l23;
        if (t49) {
            gm_closure t50 = gm_l8;
            gm_t88rcucg1b7uk t51 = gm_l2;
            gm_t88rcucg1b7uk t52 = gm_l6;
            gm_closure t53 = t50;
            gm_node *ccx54 = t53.cx;
            void * t55 = t53.code;
            uint8_t t56;
            if (GM_PTR_TAGGED(t55)) {
                t56 = ((uint8_t (*)(gm_csguankko4lmhd, gm_csguankko4lmhd))GM_PTR_UNTAG(t55))(GM_CONV(gm_csguankko4lmhd, t51), GM_CONV(gm_csguankko4lmhd, t52));
            } else {
                t56 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk, gm_t88rcucg1b7uk))t55)(t53.env, ccx54, NULL, t51, t52);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t56;
        }
    }
gm_lb1:;
    uint8_t t57 = gm_l9;
    uint8_t t58 = (uint8_t)!t57;
    if (t58) {
        {
            void * t59 = gm_l0;
            gm_tl874t2atnlh3 t60 = gm_l4;
            void * t61 = gm_l0;
            gm_t88rcucg1b7uk t62 = gm_l2;
            gm_b16_8 t63 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t61, t62);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t64 = gm_l0;
            gm_t88rcucg1b7uk t65 = gm_l6;
            gm_b16_8 t66 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t64, t65);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6j6smj7qoctu t67;
            GM_ST(gm_b16_8, ((uint8_t *)&t67 + 0), t63);
            GM_ST(gm_b16_8, ((uint8_t *)&t67 + 16), t66);
            gm_f_format_7f83ds0((void *)&gm_l25, (void *)NULL, cx, NULL, &t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t68 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
            if (!t68) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 8, gm_srcngug8co99ok1, 288, 24);
            gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l25) + 8));
            gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
            gm_b0_1 t71 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t59, t60, t70);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        void * t72 = gm_l0;
        void * t73 = GM_LD(void *, ((uint8_t *)t72));
        gm_tl874t2atnlh3 t74 = gm_l4;
        void * t75 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t73, t74);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t75;
        goto gm_fret;
    }
    {
        {
            {
                void * t76 = F.l5;
                gm_t88rcucg1b7uk t77 = gm_l2;
                gm_f_kind_jcnt4hm((void *)&gm_l26, t76, GM_CONV(gm_csguankko4lmhd, t77));
            }
            uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
            uint8_t t79 = (uint8_t)(t78 == ((uint8_t)13ULL));
            if (!t79) {
                goto gm_lb9;
            }
            gm_t88rcucg1b7uk t80 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l26) + 8));
            gm_l12 = t80;
            {
                void * t81 = F.l5;
                gm_t88rcucg1b7uk t82 = gm_l12;
                gm_f_kind_jcnt4hm((void *)&F.l13, t81, GM_CONV(gm_csguankko4lmhd, t82));
            }
            goto gm_lb8;
        }
gm_lb9:;
        void * t83 = F.l5;
        gm_t88rcucg1b7uk t84 = gm_l2;
        gm_f_kind_jcnt4hm((void *)&F.l13, t83, GM_CONV(gm_csguankko4lmhd, t84));
    }
gm_lb8:;
    {
        {
            {
                void * t85 = F.l5;
                gm_t88rcucg1b7uk t86 = gm_l6;
                gm_f_kind_jcnt4hm((void *)&gm_l27, t85, GM_CONV(gm_csguankko4lmhd, t86));
            }
            uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&gm_l27) + 0));
            uint8_t t88 = (uint8_t)(t87 == ((uint8_t)13ULL));
            if (!t88) {
                goto gm_lb11;
            }
            gm_t88rcucg1b7uk t89 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l27) + 8));
            gm_l14 = t89;
            {
                void * t90 = F.l5;
                gm_t88rcucg1b7uk t91 = gm_l14;
                gm_f_kind_jcnt4hm((void *)&F.l15, t90, GM_CONV(gm_csguankko4lmhd, t91));
            }
            goto gm_lb10;
        }
gm_lb11:;
        void * t92 = F.l5;
        gm_t88rcucg1b7uk t93 = gm_l6;
        gm_f_kind_jcnt4hm((void *)&F.l15, t92, GM_CONV(gm_csguankko4lmhd, t93));
    }
gm_lb10:;
    {
        {
            uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&F.l13) + 0));
            uint8_t t95 = (uint8_t)(t94 == ((uint8_t)16ULL));
            if (!t95) {
                goto gm_lb13;
            }
            {
                memmove(((uint8_t *)&F.l16), (((uint8_t *)&F.l13) + 8), 48);
            }
            uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&F.l15) + 0));
            uint8_t t97 = (uint8_t)(t96 == ((uint8_t)16ULL));
            if (!t97) {
                goto gm_lb13;
            }
            {
                memmove(((uint8_t *)&F.l17), (((uint8_t *)&F.l15) + 8), 48);
            }
            {
                {
                    {
                        void * t98 = gm_l0;
                        void * t99 = (void *)((uint8_t *)&F.l16);
                        void * t100 = (void *)((uint8_t *)&F.l17);
                        gm_f_fn_cast_mismatch_e9s21md((void *)&gm_l28, (void *)NULL, cx, NULL, t98, t99, t100);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t101 = (uint8_t)((((uint8_t *)&gm_l28))[16] != 0);
                    if (!t101) {
                        goto gm_lb15;
                    }
                    gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l28));
                    gm_l18 = t102;
                    {
                        void * t103 = gm_l0;
                        gm_tl874t2atnlh3 t104 = gm_l4;
                        void * t105 = gm_l0;
                        gm_t88rcucg1b7uk t106 = gm_l2;
                        gm_b16_8 t107 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t105, t106);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t108 = gm_l0;
                        gm_t88rcucg1b7uk t109 = gm_l6;
                        gm_b16_8 t110 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t108, t109);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t111 = gm_l18;
                        gm_tfeab279c16rr t112;
                        GM_ST(gm_b16_8, ((uint8_t *)&t112 + 0), t107);
                        GM_ST(gm_b16_8, ((uint8_t *)&t112 + 16), t110);
                        GM_ST(gm_b16_8, ((uint8_t *)&t112 + 32), t111);
                        gm_f_format_560qv19((void *)&gm_l29, (void *)NULL, cx, NULL, &t112);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t113 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                        if (!t113) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 8, gm_srcngug8co99ok1, 296, 28);
                        gm_b16_8 t114 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l29) + 8));
                        gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)&t114));
                        gm_b0_1 t116 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t103, t104, t115);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    void * t117 = gm_l0;
                    void * t118 = GM_LD(void *, ((uint8_t *)t117));
                    gm_tl874t2atnlh3 t119 = gm_l4;
                    void * t120 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t118, t119);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t120;
                    goto gm_fret;
                }
gm_lb15:;
            }
gm_lb14:;
            goto gm_lb12;
        }
gm_lb13:;
    }
gm_lb12:;
    gm_t88rcucg1b7uk t121 = gm_l6;
    void * t122 = gm_l1;
    gm_tn3r4rvlh0rsv t123;
    GM_ST(void *, ((uint8_t *)&t123 + 0), t122);
    gm_t8588ma2cr9d7 t124;
    GM_ST(uint8_t, (uint8_t *)&t124 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t124 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t124 + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&t124 + 8, 0);
    GM_ST(uint8_t, ((uint8_t *)&t124 + 0), ((uint8_t)8ULL));
    gm_l19 = t123;
    void * t125 = (void *)((uint8_t *)&gm_l19);
    uint64_t t126 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t127;
    GM_ST(void *, (uint8_t *)&t127, (void *)(((uint8_t *)t125 + (uintptr_t)((uint64_t)0ULL) * 8)));
    GM_ST(uintptr_t, (uint8_t *)&t127 + 8, (uintptr_t)(t126));
    gm_tl874t2atnlh3 t128 = gm_l4;
    void * t129 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t124, t121, t127, t128);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t129;
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_pointer_gv6lvmk(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_pointer_gv6lvmk F;
    gm_t88rcucg1b7uk gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* i */
    gm_t88rcucg1b7uk gm_l2; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 174u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t53at80pfdus3 gm_l3;
    uint8_t gm_l4;
    gm_t53at80pfdus3 gm_l5;
    {
        {
            void * t12 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_fn_cast_mismatch_e9s21md *)((&F.h)->up))->l3));
            gm_t88rcucg1b7uk t13 = gm_l0;
            gm_f_kind_jcnt4hm((void *)&gm_l3, t12, GM_CONV(gm_csguankko4lmhd, t13));
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t15 = (uint8_t)(t14 == ((uint8_t)13ULL));
            if (!t15) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l3) + 8));
            gm_l1 = t16;
            gm_t88rcucg1b7uk t17 = gm_l1;
            gm_l2 = t17;
            goto gm_lb1;
        }
gm_lb2:;
        gm_t88rcucg1b7uk t18 = gm_l0;
        gm_l2 = t18;
    }
gm_lb1:;
    void * t19 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_fn_cast_mismatch_e9s21md *)((&F.h)->up))->l3));
    gm_t88rcucg1b7uk t20 = gm_l2;
    gm_f_kind_jcnt4hm((void *)&gm_l5, t19, GM_CONV(gm_csguankko4lmhd, t20));
    {
        {
            {
                {
                    uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                    uint8_t t22 = (uint8_t)(t21 == ((uint8_t)8ULL));
                    if (!t22) {
                        goto gm_lb6;
                    }
                    goto gm_lb5;
                }
gm_lb6:;
                {
                    uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                    uint8_t t24 = (uint8_t)(t23 == ((uint8_t)9ULL));
                    if (!t24) {
                        goto gm_lb7;
                    }
                    goto gm_lb5;
                }
gm_lb7:;
                {
                    uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                    uint8_t t26 = (uint8_t)(t25 == ((uint8_t)7ULL));
                    if (!t26) {
                        goto gm_lb8;
                    }
                    goto gm_lb5;
                }
gm_lb8:;
                goto gm_lb4;
            }
gm_lb5:;
            gm_l4 = ((uint8_t)1ULL);
            goto gm_lb3;
        }
gm_lb4:;
        {
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t28 = (uint8_t)(t27 == ((uint8_t)16ULL));
            if (!t28) {
                goto gm_lb9;
            }
            gm_l4 = ((uint8_t)2ULL);
            goto gm_lb3;
        }
gm_lb9:;
        {
            gm_l4 = ((uint8_t)0ULL);
            goto gm_lb3;
        }
gm_lb10:;
        gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 310, 16);
    }
gm_lb3:;
    uint8_t t29 = gm_l4;
    F.ret = t29;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_same_1qdglea(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_same_1qdglea F;
    gm_t88rcucg1b7uk gm_l0; /* a */
    gm_t88rcucg1b7uk gm_l1; /* b */
    GM_STACK_CHECK(&F, 197u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    gm_closure t2 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_pointer_cast_6siqq5h *)((&F.h)->up))->l7));
    gm_t88rcucg1b7uk t3 = gm_l0;
    gm_closure t4 = t2;
    gm_node *ccx5 = t4.cx;
    void * t6 = t4.code;
    uint8_t t7;
    if (GM_PTR_TAGGED(t6)) {
        t7 = ((uint8_t (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t6))(GM_CONV(gm_csguankko4lmhd, t3));
    } else {
        t7 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t6)(t4.env, ccx5, NULL, t3);
    }
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t8 = (!(t7 == ((uint8_t)0ULL)));
    if (t8) {
        gm_closure t9 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_pointer_cast_6siqq5h *)((&F.h)->up))->l7));
        gm_t88rcucg1b7uk t10 = gm_l0;
        gm_closure t11 = t9;
        gm_node *ccx12 = t11.cx;
        void * t13 = t11.code;
        uint8_t t14;
        if (GM_PTR_TAGGED(t13)) {
            t14 = ((uint8_t (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t13))(GM_CONV(gm_csguankko4lmhd, t10));
        } else {
            t14 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t13)(t11.env, ccx12, NULL, t10);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_closure t15 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_pointer_cast_6siqq5h *)((&F.h)->up))->l7));
        gm_t88rcucg1b7uk t16 = gm_l1;
        gm_closure t17 = t15;
        gm_node *ccx18 = t17.cx;
        void * t19 = t17.code;
        uint8_t t20;
        if (GM_PTR_TAGGED(t19)) {
            t20 = ((uint8_t (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t19))(GM_CONV(gm_csguankko4lmhd, t16));
        } else {
            t20 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t19)(t17.env, ccx18, NULL, t16);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t21 = ((t14 == t20));
        gm_l2 = t21;
    } else {
        gm_l2 = ((uint8_t)0);
    }
    uint8_t t22 = gm_l2;
    F.ret = t22;
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_same_repr_830dsa9(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_same_repr_830dsa9 F;
    gm_t88rcucg1b7uk gm_l0; /* a */
    gm_t88rcucg1b7uk gm_l1; /* b */
    GM_STACK_CHECK(&F, 353u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    uint8_t gm_l3;
    uint8_t gm_l4;
    gm_t53at80pfdus3 gm_l5;
    uint8_t gm_l6;
    gm_t53at80pfdus3 gm_l7;
    gm_t88rcucg1b7uk t8 = gm_l0;
    gm_t88rcucg1b7uk t9 = gm_l1;
    uint8_t t10 = (gm_eq9m6mvtmgde(((uint8_t *)&t8), ((uint8_t *)&t9)));
    if (t10) {
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
    gm_closure t11 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_fn_cast_mismatch_e9s21md *)((&F.h)->up))->l4));
    gm_t88rcucg1b7uk t12 = gm_l0;
    gm_closure t13 = t11;
    gm_node *ccx14 = t13.cx;
    void * t15 = t13.code;
    uint8_t t16;
    if (GM_PTR_TAGGED(t15)) {
        t16 = ((uint8_t (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t15))(GM_CONV(gm_csguankko4lmhd, t12));
    } else {
        t16 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t15)(t13.env, ccx14, NULL, t12);
    }
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t17 = (!(t16 == ((uint8_t)0ULL)));
    if (t17) {
        gm_closure t18 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_fn_cast_mismatch_e9s21md *)((&F.h)->up))->l4));
        gm_t88rcucg1b7uk t19 = gm_l0;
        gm_closure t20 = t18;
        gm_node *ccx21 = t20.cx;
        void * t22 = t20.code;
        uint8_t t23;
        if (GM_PTR_TAGGED(t22)) {
            t23 = ((uint8_t (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t22))(GM_CONV(gm_csguankko4lmhd, t19));
        } else {
            t23 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t22)(t20.env, ccx21, NULL, t19);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_closure t24 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_fn_cast_mismatch_e9s21md *)((&F.h)->up))->l4));
        gm_t88rcucg1b7uk t25 = gm_l1;
        gm_closure t26 = t24;
        gm_node *ccx27 = t26.cx;
        void * t28 = t26.code;
        uint8_t t29;
        if (GM_PTR_TAGGED(t28)) {
            t29 = ((uint8_t (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t28))(GM_CONV(gm_csguankko4lmhd, t25));
        } else {
            t29 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t28)(t26.env, ccx27, NULL, t25);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t30 = ((t23 == t29));
        gm_l3 = t30;
    } else {
        gm_l3 = ((uint8_t)0);
    }
    uint8_t t31 = gm_l3;
    if (t31) {
        {
            void * t32 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_fn_cast_mismatch_e9s21md *)((&F.h)->up))->l3));
            gm_t88rcucg1b7uk t33 = gm_l0;
            gm_f_kind_jcnt4hm((void *)&gm_l5, t32, GM_CONV(gm_csguankko4lmhd, t33));
        }
        {
            {
                uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                uint8_t t35 = (uint8_t)(t34 == ((uint8_t)13ULL));
                if (!t35) {
                    goto gm_lb2;
                }
                gm_l4 = ((uint8_t)1);
                goto gm_lb1;
            }
gm_lb2:;
            {
                gm_l4 = ((uint8_t)0);
                goto gm_lb1;
            }
gm_lb3:;
            gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 318, 70);
        }
gm_lb1:;
        {
            uint8_t t36 = gm_l4;
            void * t37 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_fn_cast_mismatch_e9s21md *)((&F.h)->up))->l3));
            gm_t88rcucg1b7uk t38 = gm_l1;
            gm_f_kind_jcnt4hm((void *)&gm_l7, t37, GM_CONV(gm_csguankko4lmhd, t38));
            {
                {
                    uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                    uint8_t t40 = (uint8_t)(t39 == ((uint8_t)13ULL));
                    if (!t40) {
                        goto gm_lb5;
                    }
                    gm_l6 = ((uint8_t)1);
                    goto gm_lb4;
                }
gm_lb5:;
                {
                    gm_l6 = ((uint8_t)0);
                    goto gm_lb4;
                }
gm_lb6:;
                gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 318, 103);
            }
gm_lb4:;
            uint8_t t41 = gm_l6;
            uint8_t t42 = ((t36 == t41));
            gm_l2 = t42;
        }
    } else {
        gm_l2 = ((uint8_t)0);
    }
    uint8_t t43 = gm_l2;
    F.ret = t43;
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_slice_fn_np9f5vf(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_slice_fn_np9f5vf F;
    void * gm_l0; /* cx */
    void * gm_l1; /* base */
    gm_b16_8 gm_l2; /* n */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    uint32_t gm_l5; /* f */
    void * gm_l6; /* tys */
    gm_t88rcucg1b7uk gm_l7; /* bt */
    void * gm_l8; /* recv */
    gm_t88rcucg1b7uk gm_l9; /* elem */
    gm_t88rcucg1b7uk gm_l10; /* elem */
    uint8_t gm_l11; /* m */
    void * gm_l12; /* p */
    GM_STACK_CHECK(&F, 858u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_t9805qtonbsag gm_l13;
    gm_tabb710d9mq2e gm_l14;
    gm_t53at80pfdus3 gm_l15;
    gm_tqs5p4n2tq19v gm_l16;
    gm_tabb710d9mq2e gm_l17;
    void * t8 = gm_l0;
    gm_b16_8 t9;
    GM_ST(void *, (uint8_t *)&t9, (void *)(gm_ss496b65r9omj));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(5));
    gm_b16_8 t10 = gm_l2;
    gm_t9805qtonbsag t11 = gm_f_core_fn_uts2vhe((void *)NULL, cx, NULL, t8, t9, t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l13 = t11;
    uint8_t t12 = (uint8_t)((((uint8_t *)&gm_l13))[4] != 0);
    if (t12) {
        uint32_t t13 = GM_LD(uint32_t, ((uint8_t *)&gm_l13));
        gm_l5 = t13;
    } else {
        void * t14 = gm_l0;
        gm_tl874t2atnlh3 t15 = gm_l4;
        gm_b16_8 t16 = gm_l2;
        gm_tnpifv20irnph t17;
        GM_ST(gm_b16_8, ((uint8_t *)&t17 + 0), t16);
        gm_f_format_vtr1b5e((void *)&gm_l14, (void *)NULL, cx, NULL, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
        if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_srcngug8co99ok1, 415, 24);
        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
        gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
        gm_b0_1 t21 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t14, t15, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t22 = gm_l0;
        void * t23 = GM_LD(void *, ((uint8_t *)t22));
        gm_tl874t2atnlh3 t24 = gm_l4;
        void * t25 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t23, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t25;
        goto gm_fret;
    }
    void * t26 = gm_l0;
    void * t27 = GM_LD(void *, ((uint8_t *)t26));
    void * t28 = (void *)(((uint8_t *)t27) + 8);
    gm_l6 = t28;
    void * t29 = gm_l0;
    void * t30 = (void *)(((uint8_t *)t29) + 96);
    void * t31 = gm_l0;
    void * t32 = GM_LD(void *, ((uint8_t *)t31));
    void * t33 = gm_l1;
    gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t33));
    gm_t88rcucg1b7uk t35 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t30, t32, t34);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t35;
    void * t36 = gm_l1;
    gm_l8 = t36;
    void * t37 = gm_l6;
    gm_t88rcucg1b7uk t38 = gm_l7;
    gm_f_kind_jcnt4hm((void *)&gm_l15, t37, GM_CONV(gm_csguankko4lmhd, t38));
    {
        {
            {
                {
                    uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 0));
                    uint8_t t40 = (uint8_t)(t39 == ((uint8_t)11ULL));
                    if (!t40) {
                        goto gm_lb4;
                    }
                    gm_t88rcucg1b7uk t41 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l15) + 8) + 8));
                    gm_l9 = t41;
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 0));
                    uint8_t t43 = (uint8_t)(t42 == ((uint8_t)20ULL));
                    if (!t43) {
                        goto gm_lb5;
                    }
                    gm_t88rcucg1b7uk t44 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l15) + 8) + 4));
                    gm_l9 = t44;
                    goto gm_lb3;
                }
gm_lb5:;
                goto gm_lb2;
            }
gm_lb3:;
            void * t45 = gm_l0;
            void * t46 = gm_l1;
            gm_tqs5p4n2tq19v t47 = gm_f_place_mutability_0tlm4g8(t45, t46);
            gm_l16 = t47;
            uint8_t t48 = (uint8_t)((((uint8_t *)&gm_l16))[1] != 0);
            if (t48) {
                uint8_t t49 = GM_LD(uint8_t, ((uint8_t *)&gm_l16));
                gm_l11 = t49;
            } else {
                void * t50 = gm_l0;
                gm_tl874t2atnlh3 t51 = gm_l4;
                gm_b16_8 t52 = gm_l2;
                gm_tnpifv20irnph t53;
                GM_ST(gm_b16_8, ((uint8_t *)&t53 + 0), t52);
                gm_f_format_6pukbnn((void *)&gm_l17, (void *)NULL, cx, NULL, t53);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t54 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                if (!t54) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_srcngug8co99ok1, 424, 32);
                gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
                gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&t55));
                gm_b0_1 t57 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t50, t51, t56);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t58 = gm_l0;
                void * t59 = GM_LD(void *, ((uint8_t *)t58));
                gm_tl874t2atnlh3 t60 = gm_l4;
                void * t61 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t59, t60);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t61;
                goto gm_fret;
            }
            void * t62 = gm_l6;
            uint8_t t63 = gm_l11;
            uint8_t t64 = (uint8_t)!t63;
            gm_t88rcucg1b7uk t65 = gm_l7;
            gm_t88rcucg1b7uk t66 = gm_f_ptr_t1h2m2d((void *)NULL, cx, NULL, t62, t64, t65);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t67 = gm_l1;
            gm_tl874t2atnlh3 t68 = gm_l4;
            gm_to0f5qn75cgg8 t69;
            memset(&t69, 0, sizeof t69);
            GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)24ULL));
            GM_ST(void *, ((uint8_t *)&t69 + 8), t67);
            void * t70 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t66, &t69, t68);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l12 = t70;
            void * t71 = gm_l0;
            void * t72 = gm_l12;
            void * t73 = gm_l6;
            uint8_t t74 = gm_l11;
            uint8_t t75 = (uint8_t)!t74;
            gm_t88rcucg1b7uk t76 = gm_l9;
            gm_t88rcucg1b7uk t77 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t73, t75, t76);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t78 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t71, t72, t77);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t78;
            goto gm_lb1;
        }
gm_lb2:;
        {
            goto gm_lb1;
        }
gm_lb6:;
        gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 421, 5);
    }
gm_lb1:;
    void * t79 = gm_l0;
    uint32_t t80 = gm_l5;
    void * t81 = gm_l8;
    gm_b16_8 t82 = gm_l3;
    gm_tl874t2atnlh3 t83 = gm_l4;
    void * t84 = gm_f_generic_call_2tlofdi((void *)NULL, cx, NULL, t79, t80, t81, t82, t83);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t84;
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_takes_receiver_uhu9min(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_takes_receiver_uhu9min F;
    void * gm_l0; /* tys */
    gm_t88rcucg1b7uk gm_l1; /* param */
    gm_t88rcucg1b7uk gm_l2; /* s */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_t88rcucg1b7uk gm_l4; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 125u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    gm_t53at80pfdus3 gm_l5;
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_l3 = t4;
    {
        {
            void * t5 = gm_l0;
            gm_t88rcucg1b7uk t6 = gm_l3;
            gm_f_kind_jcnt4hm((void *)&gm_l5, t5, GM_CONV(gm_csguankko4lmhd, t6));
            uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)8ULL));
            if (!t8) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t9 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l5) + 8) + 4));
            gm_l4 = t9;
            gm_t88rcucg1b7uk t10 = gm_l4;
            gm_l3 = t10;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t11 = gm_l0;
    gm_t88rcucg1b7uk t12 = gm_l3;
    gm_t88rcucg1b7uk t13 = gm_l2;
    uint8_t t14 = gm_f_fits_receiver_1qqamgd(t11, GM_CONV(gm_csguankko4lmhd, t12), GM_CONV(gm_csguankko4lmhd, t13));
    F.ret = t14;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_arg_grgffgp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_type_arg_grgffgp F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* args */
    uint64_t gm_l2; /* i */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_t88rcucg1b7uk gm_l5; /* t */
    GM_STACK_CHECK(&F, 220u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        {
            uint64_t t4 = gm_l2;
            uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t6 = (t4 < t5);
            if (!t6) {
                goto gm_lb2;
            }
            void * t7 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint64_t t9 = gm_l2;
            uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t7) + (uintptr_t)t9 * 144) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)0ULL));
            if (!t11) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l4), ((((uint8_t *)t7) + (uintptr_t)t9 * 144) + 8), 88);
            void * t12 = gm_l0;
            void * t13 = (void *)((uint8_t *)&F.l4);
            gm_t88rcucg1b7uk t14 = gm_f_resolve_ty_uk6c1lp((void *)NULL, cx, NULL, t12, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t14;
            void * t15 = gm_l0;
            void * t16 = GM_LD(void *, ((uint8_t *)t15));
            void * t17 = (void *)(((uint8_t *)t16) + 8);
            gm_t88rcucg1b7uk t18 = gm_l5;
            uint8_t t19 = gm_f_is_error_8k27gd1(t17, GM_CONV(gm_csguankko4lmhd, t18));
            if (t19) {
                gm_tkn42u7na3t81 t20;
                memset(&t20, 0, sizeof t20);
                F.ret = t20;
                goto gm_fret;
            }
            gm_t88rcucg1b7uk t21 = gm_l5;
            gm_tkn42u7na3t81 t22;
            GM_ST(uint8_t, (uint8_t *)&t22 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t22 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t22), t21);
            ((uint8_t *)&t22)[4] = 1;
            F.ret = t22;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t23 = gm_l0;
    gm_tl874t2atnlh3 t24 = gm_l3;
    gm_b16_8 t25;
    GM_ST(void *, (uint8_t *)&t25, (void *)(gm_spskd7qirpoe3));
    GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(24));
    gm_b0_1 t26 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t23, t24, t25);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tkn42u7na3t81 t27;
    memset(&t27, 0, sizeof t27);
    F.ret = t27;
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unsigned_of_n48qlse(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_unsigned_of_n48qlse F;
    void * gm_l0; /* tys */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint16_t gm_l2; /* bits */
    GM_STACK_CHECK(&F, 134u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t88rcucg1b7uk gm_l3;
    gm_t53at80pfdus3 gm_l4;
    void * t6 = gm_l0;
    gm_t88rcucg1b7uk t7 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l4, t6, GM_CONV(gm_csguankko4lmhd, t7));
    {
        {
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            uint8_t t9 = (uint8_t)(t8 == ((uint8_t)0ULL));
            if (!t9) {
                goto gm_lb2;
            }
            uint16_t t10 = GM_LD(uint16_t, ((((uint8_t *)&gm_l4) + 8) + 2));
            gm_l2 = t10;
            void * t11 = gm_l0;
            uint16_t t12 = gm_l2;
            gm_t88rcucg1b7uk t13 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t11, ((uint8_t)0), t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l3 = t13;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            uint8_t t15 = (uint8_t)(t14 == ((uint8_t)2ULL));
            if (!t15) {
                goto gm_lb3;
            }
            void * t16 = gm_l0;
            gm_t88rcucg1b7uk t17 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l3 = t17;
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_t88rcucg1b7uk t18 = gm_l1;
            gm_l3 = t18;
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 264, 66);
    }
gm_lb1:;
    gm_t88rcucg1b7uk t19 = gm_l3;
    F.ret = t19;
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_value_arg_i5l23i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_value_arg_i5l23i1 F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* args */
    uint64_t gm_l2; /* i */
    gm_t88rcucg1b7uk gm_l3; /* ty */
    gm_tl874t2atnlh3 gm_l4; /* span */
    GM_STACK_CHECK(&F, 235u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        {
            uint64_t t4 = gm_l2;
            uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t6 = (t4 < t5);
            if (!t6) {
                goto gm_lb2;
            }
            void * t7 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint64_t t9 = gm_l2;
            uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t7) + (uintptr_t)t9 * 144) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)0ULL));
            if (!t11) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l5), ((((uint8_t *)t7) + (uintptr_t)t9 * 144) + 8), 88);
            void * t12 = gm_l0;
            void * t13 = gm_l0;
            void * t14 = (void *)((uint8_t *)&F.l5);
            gm_t88rcucg1b7uk t15 = gm_l3;
            gm_tkn42u7na3t81 t16;
            GM_ST(uint8_t, (uint8_t *)&t16 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t16 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t16), t15);
            ((uint8_t *)&t16)[4] = 1;
            void * t17 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t13, t14, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t18 = gm_l3;
            void * t19 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t12, t17, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t19;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t20 = gm_l0;
    gm_tl874t2atnlh3 t21 = gm_l4;
    gm_b16_8 t22;
    GM_ST(void *, (uint8_t *)&t22, (void *)(gm_s7rl9plavbrfo));
    GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(16));
    gm_b0_1 t23 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t20, t21, t22);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t24 = gm_l0;
    void * t25 = GM_LD(void *, ((uint8_t *)t24));
    gm_tl874t2atnlh3 t26 = gm_l4;
    void * t27 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t25, t26);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t27;
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}

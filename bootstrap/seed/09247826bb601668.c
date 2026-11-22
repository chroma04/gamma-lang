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
typedef struct gm_cs8r2qbercod2k { uint32_t f0; uint8_t f1; uint8_t f2; } gm_cs8r2qbercod2k;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint32_t f0; uint8_t f1; uint8_t f2; uint8_t pe[2]; } gm_t0n7rigt5nh46;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cs8r2qbercod2k) == 8 && _Alignof(gm_cs8r2qbercod2k) == 4 && offsetof(gm_cs8r2qbercod2k, f0) == 0 && offsetof(gm_cs8r2qbercod2k, f1) == 4 && offsetof(gm_cs8r2qbercod2k, f2) == 5, "layout of `LoopState`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
_Static_assert(sizeof(gm_t0n7rigt5nh46) == 8 && _Alignof(gm_t0n7rigt5nh46) == 4 && offsetof(gm_t0n7rigt5nh46, f0) == 0 && offsetof(gm_t0n7rigt5nh46, f1) == 4 && offsetof(gm_t0n7rigt5nh46, f2) == 5, "layout of `LoopState`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { gm_t0n7rigt5nh46 f0; uint8_t f1; uint8_t pe[3]; } gm_t72g7o4mtivnc;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; uint8_t v2; gm_t9qkh0rsooc8d v3; gm_t9qkh0rsooc8d v4; gm_tcc1rq4n1avb2 v5; uint32_t v6; uint32_t v7; } f1; } gm_t8588ma2cr9d7;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `(e: ^const Expr, l: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_b16_8 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_tdgfduqjheqdm;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `(string, usize)`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_tpe1hv3fekn05 v1; } f1; } gm_t4lnf71mruqpt;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t72g7o4mtivnc) == 12 && _Alignof(gm_t72g7o4mtivnc) == 4 && offsetof(gm_t72g7o4mtivnc, f0) == 0 && offsetof(gm_t72g7o4mtivnc, f1) == 8, "layout of `?LoopState`");
_Static_assert(sizeof(gm_t8588ma2cr9d7) == 12 && _Alignof(gm_t8588ma2cr9d7) == 4 && offsetof(gm_t8588ma2cr9d7, f0) == 0 && offsetof(gm_t8588ma2cr9d7, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { gm_t8588ma2cr9d7 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_t9q8lhljes74c;
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
typedef struct { void * f0; uint32_t f1; uint8_t p2[4]; gm_trqfvmjtt4sij f2; gm_trqfvmjtt4sij f3; } gm_tbliclsb5tiqp;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tdgfduqjheqdm) == 64 && _Alignof(gm_tdgfduqjheqdm) == 8 && offsetof(gm_tdgfduqjheqdm, f0) == 0 && offsetof(gm_tdgfduqjheqdm, f1) == 8 && offsetof(gm_tdgfduqjheqdm, f2) == 24 && offsetof(gm_tdgfduqjheqdm, f3) == 40 && offsetof(gm_tdgfduqjheqdm, f4) == 48 && offsetof(gm_tdgfduqjheqdm, f5) == 56, "layout of `FnDef`");
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
typedef struct { uint64_t f0; gm_tdgfduqjheqdm f1; } gm_tks8t13utjt96;
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_t4lnf71mruqpt) == 32 && _Alignof(gm_t4lnf71mruqpt) == 8 && offsetof(gm_t4lnf71mruqpt, f0) == 0 && offsetof(gm_t4lnf71mruqpt, f1) == 8, "layout of `HandlerValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t9q8lhljes74c) == 32 && _Alignof(gm_t9q8lhljes74c) == 8 && offsetof(gm_t9q8lhljes74c, f0) == 0 && offsetof(gm_t9q8lhljes74c, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tbliclsb5tiqp) == 96 && _Alignof(gm_tbliclsb5tiqp) == 8 && offsetof(gm_tbliclsb5tiqp, f0) == 0 && offsetof(gm_tbliclsb5tiqp, f1) == 8 && offsetof(gm_tbliclsb5tiqp, f2) == 16 && offsetof(gm_tbliclsb5tiqp, f3) == 56, "layout of `ContCheck`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tks8t13utjt96) == 72 && _Alignof(gm_tks8t13utjt96) == 8 && offsetof(gm_tks8t13utjt96, f0) == 0 && offsetof(gm_tks8t13utjt96, f1) == 8, "layout of `(i: usize, x: FnDef)`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_t9q8lhljes74c v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; void * v55; gm_tandvbt63uk7n v56; gm_t710lsrq1m8gk v57; } f1; } gm_to0f5qn75cgg8;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_to0f5qn75cgg8) == 64 && _Alignof(gm_to0f5qn75cgg8) == 8 && offsetof(gm_to0f5qn75cgg8, f0) == 0 && offsetof(gm_to0f5qn75cgg8, f1) == 8, "layout of `ExprKind`");
struct gm_fr_any_child_ffc8h1k {
    gm_frame h;
    gm_closure l1; /* pred */
    uint8_t l2; /* found */
    uint8_t ret;
};
struct gm_fr_check_continuations_ukkdj9c {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* errors */
    gm_tdgfduqjheqdm l5; /* def */
    gm_tdgfduqjheqdm l11; /* f */
    gm_tbliclsb5tiqp l13; /* cx */
    gm_trqfvmjtt4sij l14; /* seen */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_9ne58t9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_oakc5n8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_extend_jk8kame {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_fn_4f453lc {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_7u87bne {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_clkgu5i {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_it95hr1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_j9invmr {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_k_09a6ffc {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_k_block_87r6qua {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_k_children_3gr7c2u {
    gm_frame h;
    void * l0; /* cx */
    uint8_t l3; /* sc */
    uint8_t ret;
};
struct gm_fr_k_cond_d97airn {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_k_consume_nr50dr5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_k_exit_ttiloj7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_k_expr_hdusp9d {
    gm_frame h;
    gm_t710lsrq1m8gk l35; /* l */
    gm_t710lsrq1m8gk l36; /* w */
    uint8_t ret;
};
struct gm_fr_k_jump_41ae4gj {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_k_loop_ot156p6 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_1hi08ho {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_6ldri0d {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mentions_local_6sbsseu {
    gm_frame h;
    uint32_t l1; /* k */
    uint8_t ret;
};
struct gm_fr_mentions_upvar_f852a0h {
    gm_frame h;
    uint32_t l1; /* k */
    uint8_t ret;
};
struct gm_fr_only_consumed_pk7topa {
    gm_frame h;
    uint32_t l1; /* k */
    uint8_t l11; /* ok */
    uint8_t ret;
};
struct gm_fr_pop_7fqs7gj {
    gm_frame h;
    gm_t72g7o4mtivnc ret;
};
struct gm_fr_push_dqg4p50 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_j129uo3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8bl3rpf {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_at32hfa {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_0s6rl4q {
    gm_frame h;
    gm_b0_1 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
inline bool gm_eqkricceou4u(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) uint8_t gm_f_any_child_ffc8h1k(void * a0, gm_closure a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_continuations_ukkdj9c(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_9ne58t9(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oakc5n8(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_jk8kame(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_4f453lc(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7u87bne(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_clkgu5i(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_it95hr1(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_j9invmr(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_k_09a6ffc(void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_k_block_87r6qua(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
inline __attribute__((aligned(4))) uint8_t gm_f_k_children_3gr7c2u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_k_cond_d97airn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_k_consume_nr50dr5(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_k_exit_ttiloj7(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) uint8_t gm_f_k_expr_hdusp9d(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_k_jump_41ae4gj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_te9hrk0u35rev a1, uint8_t a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) uint8_t gm_f_k_loop_ot156p6(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, void * a3, void * a4, uint8_t a5);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_1hi08ho(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_6ldri0d(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint8_t gm_f_mentions_local_6sbsseu(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_mentions_upvar_f852a0h(void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_only_consumed_pk7topa(void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t72g7o4mtivnc gm_f_pop_7fqs7gj(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_dqg4p50(void * a0, gm_cs8r2qbercod2k a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_j129uo3(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8bl3rpf(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_at32hfa(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_truncate_0s6rl4q(void * a0, uint64_t a1);
extern uint8_t gm_c3seud9n9be[1] __attribute__((aligned(1)));
extern uint8_t gm_ckpkn5umvr0[1] __attribute__((aligned(1)));
extern const uint8_t gm_siho9h2ssfvv9[62];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcfa3vclj60ege[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_svhflm4d80ub1[62];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    if (!gm_eq467d5cm1d5(a + 8, b + 8)) return false;
    return true;
}
inline bool gm_eqkricceou4u(const uint8_t *a, const uint8_t *b) {
    if (a[4] != b[4]) return false;
    return !a[4] || gm_eq467d5cm1d5(a, b);
}
inline __attribute__((aligned(4))) uint8_t gm_f_any_child_ffc8h1k(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_any_child_ffc8h1k F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 43u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    F.l2 = ((uint8_t)0);
    void * t2 = gm_l0;
    gm_closure t3 = ((gm_closure){(void *)gm_f_fn_7u87bne, (void *)&F.h, cx});
    gm_f_for_each_child_fm49s73(t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t4 = F.l2;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_9ne58t9(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_9ne58t9 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 8)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oakc5n8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_oakc5n8 F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_jk8kame(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_jk8kame F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_at32hfa(t12, t13, t14);
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_4f453lc(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_4f453lc F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 23u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_mentions_local_6sbsseu *)((&F.h)->up))->l1));
    uint8_t t4 = gm_f_mentions_local_6sbsseu(t2, t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7u87bne(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_7u87bne F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_any_child_ffc8h1k *)((&F.h)->up))->l2));
    uint8_t t3 = (uint8_t)!t2;
    if (t3) {
        gm_closure t4 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_any_child_ffc8h1k *)((&F.h)->up))->l1));
        void * t5 = gm_l0;
        gm_closure t6 = t4;
        gm_node *ccx7 = t6.cx;
        void * t8 = t6.code;
        uint8_t t9;
        if (GM_PTR_TAGGED(t8)) {
            t9 = ((uint8_t (*)(void *))GM_PTR_UNTAG(t8))(t5);
        } else {
            t9 = ((uint8_t (*)(void *, gm_node *, gm_node *, void *))t8)(t6.env, ccx7, NULL, t5);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_any_child_ffc8h1k *)((&F.h)->up))->l2), t9);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_clkgu5i(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_clkgu5i F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_only_consumed_pk7topa *)((&F.h)->up))->l11));
    if (t2) {
        void * t3 = gm_l0;
        uint32_t t4 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_only_consumed_pk7topa *)((&F.h)->up))->l1));
        uint8_t t5 = gm_f_only_consumed_pk7topa(t3, t4);
        gm_l1 = t5;
    } else {
        gm_l1 = ((uint8_t)0);
    }
    uint8_t t6 = gm_l1;
    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_only_consumed_pk7topa *)((&F.h)->up))->l11), t6);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_it95hr1(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_it95hr1 F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 26u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_k_children_3gr7c2u *)((&F.h)->up))->l0));
    void * t3 = gm_l0;
    uint8_t t4 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_k_children_3gr7c2u *)((&F.h)->up))->l3));
    uint8_t t5 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t2, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_k_children_3gr7c2u *)((&F.h)->up))->l3), t5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_j9invmr(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_j9invmr F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 23u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_mentions_upvar_f852a0h *)((&F.h)->up))->l1));
    uint8_t t4 = gm_f_mentions_upvar_f852a0h(t2, t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_k_09a6ffc(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_k_09a6ffc F;
    void * gm_l0; /* e */
    uint32_t gm_l1; /* k */
    uint32_t gm_l2; /* l */
    GM_STACK_CHECK_SHALLOW(&F, 41u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t4 = gm_l0;
            uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)15ULL));
            if (!t6) {
                goto gm_lb2;
            }
            uint32_t t7 = GM_LD(uint32_t, ((((uint8_t *)t4) + 8) + 8));
            gm_l2 = t7;
            uint32_t t8 = gm_l2;
            uint32_t t9 = gm_l1;
            uint8_t t10 = ((t8 == t9));
            F.ret = t10;
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
inline __attribute__((aligned(4))) uint8_t gm_f_k_children_3gr7c2u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2) {
    struct gm_fr_k_children_3gr7c2u F;
    void * gm_l1; /* e */
    uint8_t gm_l2; /* st */
    GM_STACK_CHECK(&F, 45u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t t2 = gm_l2;
    F.l3 = t2;
    void * t3 = gm_l1;
    gm_closure t4 = ((gm_closure){(void *)gm_f_fn_it95hr1, (void *)&F.h, cx});
    gm_f_for_each_child_fm49s73(t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t5 = F.l3;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_k_consume_nr50dr5(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_k_consume_nr50dr5 F;
    void * gm_l0; /* cx */
    uint8_t gm_l1; /* st */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 106u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l3;
    uint8_t t2 = gm_l1;
    uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)gm_ckpkn5umvr0));
    uint8_t t4 = (uint8_t)(t2 & t3);
    uint8_t t5 = (!(t4 == ((uint8_t)0ULL)));
    if (t5) {
        void * t6 = gm_l0;
        void * t7 = (void *)(((uint8_t *)t6) + 56);
        gm_tl874t2atnlh3 t8 = gm_l2;
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_svhflm4d80ub1));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(61));
        gm_tbgd14esf0jvi t10;
        GM_ST(uint32_t, (uint8_t *)&t10 + 12, 0);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t10 + 0), t8);
        GM_ST(gm_b16_8, ((uint8_t *)&t10 + 16), t9);
        gm_tftb1odc739t5 t11 = gm_f_push_j129uo3(t7, &t10);
        gm_l3 = t11;
        uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
        if (!t12) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srcfa3vclj60ege, 104, 29);
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_k_exit_ttiloj7(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_k_exit_ttiloj7 F;
    void * gm_l0; /* cx */
    uint8_t gm_l1; /* st */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 106u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l3;
    uint8_t t2 = gm_l1;
    uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)gm_c3seud9n9be));
    uint8_t t4 = (uint8_t)(t2 & t3);
    uint8_t t5 = (!(t4 == ((uint8_t)0ULL)));
    if (t5) {
        void * t6 = gm_l0;
        void * t7 = (void *)(((uint8_t *)t6) + 56);
        gm_tl874t2atnlh3 t8 = gm_l2;
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_siho9h2ssfvv9));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(61));
        gm_tbgd14esf0jvi t10;
        GM_ST(uint32_t, (uint8_t *)&t10 + 12, 0);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t10 + 0), t8);
        GM_ST(gm_b16_8, ((uint8_t *)&t10 + 16), t9);
        gm_tftb1odc739t5 t11 = gm_f_push_j129uo3(t7, &t10);
        gm_l3 = t11;
        uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
        if (!t12) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srcfa3vclj60ege, 100, 29);
    }
    F.ret = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) void gm_f_list_1hi08ho(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_1hi08ho F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_oakc5n8();
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
inline __attribute__((aligned(4))) void gm_f_list_6ldri0d(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_6ldri0d F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_9ne58t9();
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
inline __attribute__((aligned(4))) uint8_t gm_f_mentions_local_6sbsseu(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mentions_local_6sbsseu F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    void * t2 = gm_l0;
    uint32_t t3 = F.l1;
    uint8_t t4 = gm_f_is_k_09a6ffc(t2, t3);
    if (t4) {
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
    void * t5 = gm_l0;
    gm_closure t6 = ((gm_closure){(void *)gm_f_fn_4f453lc, (void *)&F.h, cx});
    uint8_t t7 = gm_f_any_child_ffc8h1k(t5, t6);
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
inline __attribute__((aligned(4))) uint8_t gm_f_mentions_upvar_f852a0h(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mentions_upvar_f852a0h F;
    void * gm_l0; /* e */
    uint32_t gm_l2; /* l */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    {
        {
            void * t4 = gm_l0;
            uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)16ULL));
            if (!t6) {
                goto gm_lb2;
            }
            uint32_t t7 = GM_LD(uint32_t, (((((uint8_t *)t4) + 8) + 8) + 4));
            gm_l2 = t7;
            uint32_t t8 = gm_l2;
            uint32_t t9 = F.l1;
            uint8_t t10 = ((t8 == t9));
            if (!t10) {
                goto gm_lb2;
            }
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t11 = gm_l0;
    gm_closure t12 = ((gm_closure){(void *)gm_f_fn_j9invmr, (void *)&F.h, cx});
    uint8_t t13 = gm_f_any_child_ffc8h1k(t11, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t13;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t72g7o4mtivnc gm_f_pop_7fqs7gj(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_7fqs7gj F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_t0n7rigt5nh46 gm_l2; /* x */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    gm_l1 = t3;
    uint64_t t4 = gm_l1;
    uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
    if (t5) {
        gm_t72g7o4mtivnc t6;
        memset(&t6, 0, sizeof t6);
        F.ret = t6;
        goto gm_fret;
    }
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
    uint64_t t10 = gm_l1;
    uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)1ULL), &r11);
    if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
    uint64_t t13 = r11;
    if ((uintptr_t)t13 >= (uintptr_t)(t9)) gm_trap_index((uintptr_t)t13, t9, gm_srcrgc9bkquf6nr, 133, 10);
    gm_t0n7rigt5nh46 t14 = GM_LD(gm_t0n7rigt5nh46, (((uint8_t *)t8) + (uintptr_t)t13 * 8));
    gm_l2 = t14;
    void * t15 = gm_l0;
    void * t16 = gm_l0;
    void * t17 = GM_LD(void *, ((uint8_t *)t16));
    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)t16) + 8));
    uint64_t t19 = gm_l1;
    uint64_t r20; bool o21 = __builtin_sub_overflow(t19, ((uint64_t)1ULL), &r20);
    if (o21) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
    uint64_t t22 = r20;
    uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t24 = (uintptr_t)t22;
    if (t23 > t24 || t24 > (uintptr_t)t18) gm_trap_slice(t23, t24, t18, gm_srcrgc9bkquf6nr, 134, 15);
    uint64_t t25 = ((uintptr_t)t22) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t17 + (uintptr_t)((uint64_t)0ULL) * 8)));
    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25));
    GM_ST(gm_b16_8, ((uint8_t *)t15), t26);
    gm_t0n7rigt5nh46 t27 = gm_l2;
    gm_t72g7o4mtivnc t28;
    GM_ST(uint8_t, (uint8_t *)&t28 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t28 + 10, 0);
    GM_ST(gm_t0n7rigt5nh46, ((uint8_t *)&t28), t27);
    ((uint8_t *)&t28)[8] = 1;
    F.ret = t28;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_dqg4p50(void * a0, gm_cs8r2qbercod2k a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_dqg4p50 F;
    void * gm_l0; /* l */
    gm_t0n7rigt5nh46 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t0n7rigt5nh46, a1);
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
        gm_b16_8 t9 = gm_f_empty_slice_9ne58t9();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_8bl3rpf(t8, ((uint64_t)1ULL), t10);
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
    gm_t0n7rigt5nh46 t18 = gm_l1;
    GM_ST(gm_t0n7rigt5nh46, (((uint8_t *)t16) + (uintptr_t)t17 * 8), t18);
    void * t19 = gm_l0;
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, ((uint8_t *)t20));
    uint64_t t22 = gm_l2;
    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
    uint64_t t25 = r23;
    uint64_t t26 = ((uintptr_t)t25) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t27;
    GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t21 + (uintptr_t)((uint64_t)0ULL) * 8)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_j129uo3(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_j129uo3 F;
    void * gm_l0; /* l */
    gm_tbgd14esf0jvi gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
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
        gm_b16_8 t9 = gm_f_empty_slice_oakc5n8();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_at32hfa(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 32), ((uint8_t *)a1), 32);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 32)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8bl3rpf(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8bl3rpf F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)8ULL), ((uint64_t)4ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_at32hfa(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_at32hfa F;
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
inline __attribute__((aligned(4))) void gm_f_truncate_0s6rl4q(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_0s6rl4q F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t2 = gm_l1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 8));
    uint8_t t5 = (t2 < t4);
    if (t5) {
        void * t6 = gm_l0;
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, ((uint8_t *)t7));
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
        uint64_t t10 = gm_l1;
        uint64_t t11 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t12 = (uintptr_t)t10;
        if (t11 > t12 || t12 > (uintptr_t)t9) gm_trap_slice(t11, t12, t9, gm_srcrgc9bkquf6nr, 125, 36);
        uint64_t t13 = ((uintptr_t)t10) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13));
        GM_ST(gm_b16_8, ((uint8_t *)t6), t14);
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_check_continuations_ukkdj9c(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_check_continuations_ukkdj9c F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    void * gm_l6; /* body */
    uint32_t gm_l7; /* k */
    uint8_t gm_l8; /* captured */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    void * gm_l12; /* fb */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_tbgd14esf0jvi gm_l17; /* e */
    uint8_t gm_l18; /* dup */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    gm_tbgd14esf0jvi gm_l21; /* x */
    GM_STACK_CHECK(&F, 1380u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tks8t13utjt96 gm_l22;
    gm_t9805qtonbsag gm_l23;
    gm_t9805qtonbsag gm_l24;
    uint8_t gm_l25;
    gm_t53at80pfdus3 gm_l26;
    uint8_t gm_l27;
    gm_tftb1odc739t5 gm_l28;
    gm_tftb1odc739t5 gm_l29;
    {
        gm_trqfvmjtt4sij t29;
        gm_f_list_1hi08ho((void *)&t29, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t29;
    }
    void * t30 = gm_l0;
    gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)t30) + 384));
    gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
    gm_l2 = t32;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t33 = gm_l3;
            uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t35 = (t33 < t34);
            if (!t35) {
                goto gm_lb1;
            }
            {
                {
                    uint64_t t36 = gm_l3;
                    void * t37 = GM_LD(void *, ((uint8_t *)&gm_l2));
                    uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                    uint64_t t39 = gm_l3;
                    gm_tdgfduqjheqdm t40 = GM_LD(gm_tdgfduqjheqdm, (((uint8_t *)t37) + (uintptr_t)t39 * 64));
                    GM_ST(uint64_t, ((uint8_t *)&gm_l22 + 0), t36);
                    GM_ST(gm_tdgfduqjheqdm, ((uint8_t *)&gm_l22 + 8), t40);
                }
                {
                    {
                        uint64_t t41 = GM_LD(uint64_t, ((uint8_t *)&gm_l22));
                        gm_l4 = t41;
                        {
                            memmove(((uint8_t *)&F.l5), (((uint8_t *)&gm_l22) + 8), 64);
                        }
                        uint8_t t42 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l5) + 40)) != 0);
                        if (t42) {
                            void * t43 = GM_LD(void *, (((uint8_t *)&F.l5) + 40));
                            gm_l6 = t43;
                        } else {
                            goto gm_lb3;
                        }
                        void * t44 = GM_LD(void *, (((uint8_t *)&F.l5) + 8));
                        uint64_t t45 = GM_LD(uint64_t, ((((uint8_t *)&F.l5) + 8) + 8));
                        uint64_t r46; bool o47 = __builtin_sub_overflow(t45, ((uint64_t)1ULL), &r46);
                        uint64_t t48 = r46;
                        uint8_t t49 = (t48 < t45);
                        if (t49) {
                            uint32_t t50 = GM_LD(uint32_t, (((uint8_t *)t44) + (uintptr_t)t48 * 4));
                            gm_t9805qtonbsag t51;
                            GM_ST(uint8_t, (uint8_t *)&t51 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t51 + 6, 0);
                            GM_ST(uint32_t, ((uint8_t *)&t51), t50);
                            ((uint8_t *)&t51)[4] = 1;
                            gm_l23 = t51;
                        } else {
                            gm_t9805qtonbsag t52;
                            memset(&t52, 0, sizeof t52);
                            gm_l23 = t52;
                        }
                        gm_t9805qtonbsag t53 = gm_l23;
                        gm_l24 = t53;
                        uint8_t t54 = (uint8_t)((((uint8_t *)&gm_l24))[4] != 0);
                        if (t54) {
                            uint32_t t55 = GM_LD(uint32_t, ((uint8_t *)&gm_l24));
                            gm_l7 = t55;
                        } else {
                            goto gm_lb3;
                        }
                        {
                            void * t56 = gm_l0;
                            void * t57 = (void *)(((uint8_t *)t56) + 8);
                            void * t58 = GM_LD(void *, (((uint8_t *)&F.l5) + 24));
                            uint64_t t59 = GM_LD(uint64_t, ((((uint8_t *)&F.l5) + 24) + 8));
                            uint32_t t60 = gm_l7;
                            uint64_t t61 = (uint64_t)t60;
                            if ((uintptr_t)t61 >= (uintptr_t)(t59)) gm_trap_index((uintptr_t)t61, t59, gm_srcfa3vclj60ege, 22, 23);
                            gm_t88rcucg1b7uk t62 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t58) + (uintptr_t)t61 * 40) + 16));
                            gm_f_kind_jcnt4hm((void *)&gm_l26, t57, GM_CONV(gm_csguankko4lmhd, t62));
                        }
                        {
                            {
                                uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                                uint8_t t64 = (uint8_t)(t63 == ((uint8_t)18ULL));
                                if (!t64) {
                                    goto gm_lb7;
                                }
                                gm_l25 = ((uint8_t)0);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                gm_l25 = ((uint8_t)1);
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srcfa3vclj60ege, 22, 12);
                        }
gm_lb6:;
                        uint8_t t65 = gm_l25;
                        if (t65) {
                            goto gm_lb3;
                        }
                        gm_l8 = ((uint8_t)0);
                        void * t66 = gm_l0;
                        gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)t66) + 384));
                        gm_l9 = t67;
                        gm_l10 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t68 = gm_l10;
                                uint64_t t69 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                uint8_t t70 = (t68 < t69);
                                if (!t70) {
                                    goto gm_lb9;
                                }
                                {
                                    void * t71 = GM_LD(void *, ((uint8_t *)&gm_l9));
                                    uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                    uint64_t t73 = gm_l10;
                                    {
                                        {
                                            memmove(((uint8_t *)&F.l11), (((uint8_t *)t71) + (uintptr_t)t73 * 64), 64);
                                            {
                                                {
                                                    gm_t9805qtonbsag t74 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&F.l11) + 48));
                                                    uint64_t t75 = gm_l4;
                                                    uint32_t t76 = (uint32_t)t75;
                                                    gm_t9805qtonbsag t77;
                                                    GM_ST(uint8_t, (uint8_t *)&t77 + 5, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t77 + 6, 0);
                                                    GM_ST(uint32_t, ((uint8_t *)&t77), t76);
                                                    ((uint8_t *)&t77)[4] = 1;
                                                    uint8_t t78 = (gm_eqkricceou4u(((uint8_t *)&t74), ((uint8_t *)&t77)));
                                                    if (!t78) {
                                                        goto gm_lb15;
                                                    }
                                                    uint8_t t79 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l11) + 40)) != 0);
                                                    if (!t79) {
                                                        goto gm_lb15;
                                                    }
                                                    void * t80 = GM_LD(void *, (((uint8_t *)&F.l11) + 40));
                                                    gm_l12 = t80;
                                                    void * t81 = gm_l12;
                                                    uint32_t t82 = gm_l7;
                                                    uint8_t t83 = gm_f_mentions_upvar_f852a0h(t81, t82);
                                                    if (!t83) {
                                                        goto gm_lb15;
                                                    }
                                                    gm_l8 = ((uint8_t)1);
                                                    goto gm_lb14;
                                                }
gm_lb15:;
                                            }
gm_lb14:;
                                            goto gm_lb12;
                                        }
gm_lb13:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcfa3vclj60ege, 25, 9);
                                    }
gm_lb12:;
                                }
gm_lb11:;
                                uint64_t t84 = gm_l10;
                                uint64_t r85; bool o86 = __builtin_add_overflow(t84, ((uint64_t)1ULL), &r85);
                                uint64_t t87 = r85;
                                gm_l10 = t87;
                            }
                        }
gm_lb9:;
                        uint8_t t88 = gm_l8;
                        if (t88) {
                            gm_l27 = ((uint8_t)1);
                        } else {
                            void * t89 = gm_l6;
                            uint32_t t90 = gm_l7;
                            uint8_t t91 = gm_f_only_consumed_pk7topa(t89, t90);
                            uint8_t t92 = (uint8_t)!t91;
                            gm_l27 = t92;
                        }
                        uint8_t t93 = gm_l27;
                        if (t93) {
                            goto gm_lb3;
                        }
                        {
                            void * t94 = gm_l0;
                            void * t95 = (void *)(((uint8_t *)t94) + 8);
                            uint32_t t96 = gm_l7;
                            gm_trqfvmjtt4sij t97;
                            gm_f_list_6ldri0d((void *)&t97, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_trqfvmjtt4sij t98;
                            gm_f_list_1hi08ho((void *)&t98, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tbliclsb5tiqp t99;
                            GM_ST(uint32_t, (uint8_t *)&t99 + 12, 0);
                            GM_ST(void *, ((uint8_t *)&t99 + 0), t95);
                            GM_ST(uint32_t, ((uint8_t *)&t99 + 8), t96);
                            GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t99 + 16), t97);
                            GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t99 + 56), t98);
                            F.l13 = t99;
                        }
                        void * t100 = (void *)((uint8_t *)&F.l13);
                        void * t101 = gm_l6;
                        uint8_t t102 = GM_LD(uint8_t, ((uint8_t *)gm_c3seud9n9be));
                        uint8_t t103 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t100, t101, t102);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t104 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 56));
                        gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)&t105));
                        gm_tftb1odc739t5 t107 = gm_f_extend_jk8kame(t104, t106);
                        gm_l28 = t107;
                        uint8_t t108 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                        if (!t108) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 1, gm_srcfa3vclj60ege, 32, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfa3vclj60ege, 19, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t109 = gm_l3;
            uint64_t r110; bool o111 = __builtin_add_overflow(t109, ((uint64_t)1ULL), &r110);
            uint64_t t112 = r110;
            gm_l3 = t112;
        }
    }
gm_lb1:;
    {
        gm_trqfvmjtt4sij t113;
        gm_f_list_1hi08ho((void *)&t113, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l14 = t113;
    }
    gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    gm_l15 = t114;
    gm_l16 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t115 = gm_l16;
            uint64_t t116 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
            uint8_t t117 = (t115 < t116);
            if (!t117) {
                goto gm_lb16;
            }
            {
                void * t118 = GM_LD(void *, ((uint8_t *)&gm_l15));
                uint64_t t119 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                uint64_t t120 = gm_l16;
                {
                    {
                        {
                            memmove(((uint8_t *)&gm_l17), (((uint8_t *)t118) + (uintptr_t)t120 * 32), 32);
                        }
                        gm_l18 = ((uint8_t)0);
                        gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
                        gm_l19 = t121;
                        gm_l20 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t122 = gm_l20;
                                uint64_t t123 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                                uint8_t t124 = (t122 < t123);
                                if (!t124) {
                                    goto gm_lb21;
                                }
                                {
                                    void * t125 = GM_LD(void *, ((uint8_t *)&gm_l19));
                                    uint64_t t126 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                                    uint64_t t127 = gm_l20;
                                    {
                                        {
                                            memmove(((uint8_t *)&gm_l21), (((uint8_t *)t125) + (uintptr_t)t127 * 32), 32);
                                            {
                                                {
                                                    gm_tl874t2atnlh3 t128 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l21));
                                                    gm_tl874t2atnlh3 t129 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l17));
                                                    uint8_t t130 = (gm_eqff85ac10o3(((uint8_t *)&t128), ((uint8_t *)&t129)));
                                                    if (!t130) {
                                                        goto gm_lb27;
                                                    }
                                                    gm_b16_8 t131 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 16));
                                                    gm_b16_8 t132 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 16));
                                                    uint8_t t133 = (gm_eq0iqiubfm7e(((uint8_t *)&t131), ((uint8_t *)&t132)));
                                                    if (!t133) {
                                                        goto gm_lb27;
                                                    }
                                                    gm_l18 = ((uint8_t)1);
                                                    goto gm_lb26;
                                                }
gm_lb27:;
                                            }
gm_lb26:;
                                            goto gm_lb24;
                                        }
gm_lb25:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcfa3vclj60ege, 37, 9);
                                    }
gm_lb24:;
                                }
gm_lb23:;
                                uint64_t t134 = gm_l20;
                                uint64_t r135; bool o136 = __builtin_add_overflow(t134, ((uint64_t)1ULL), &r135);
                                uint64_t t137 = r135;
                                gm_l20 = t137;
                            }
                        }
gm_lb21:;
                        uint8_t t138 = gm_l18;
                        if (t138) {
                            goto gm_lb18;
                        }
                        {
                            void * t139 = (void *)((uint8_t *)&F.l14);
                            gm_tftb1odc739t5 t140 = gm_f_push_j129uo3(t139, &(*(gm_tbgd14esf0jvi *)((uint8_t *)&gm_l17)));
                            gm_l29 = t140;
                        }
                        uint8_t t141 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                        if (!t141) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_srcfa3vclj60ege, 41, 9);
                        void * t142 = gm_l0;
                        gm_tl874t2atnlh3 t143 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l17));
                        gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 16));
                        gm_b0_1 t145 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t142, t143, t144);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb19;
                    }
gm_lb20:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfa3vclj60ege, 35, 5);
                }
gm_lb19:;
            }
gm_lb18:;
            uint64_t t146 = gm_l16;
            uint64_t r147; bool o148 = __builtin_add_overflow(t146, ((uint64_t)1ULL), &r147);
            uint64_t t149 = r147;
            gm_l16 = t149;
        }
    }
gm_lb16:;
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
extern __attribute__((aligned(4))) uint8_t gm_f_fn_4f453lc(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_clkgu5i(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_it95hr1(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) uint8_t gm_f_fn_j9invmr(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) uint8_t gm_f_is_k_09a6ffc(void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_k_block_87r6qua(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2) {
    struct gm_fr_k_block_87r6qua F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    uint8_t gm_l2; /* st */
    uint8_t gm_l3; /* sb */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_trcmmd12cefb1 gm_l6; /* x */
    void * gm_l7; /* i */
    void * gm_l8; /* i */
    void * gm_l9; /* place */
    void * gm_l10; /* value */
    gm_b16_8 gm_l11; /* places */
    void * gm_l12; /* value */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    void * gm_l15; /* p */
    void * gm_l16; /* px */
    void * gm_l17; /* e */
    void * gm_l18; /* t */
    GM_STACK_CHECK(&F, 602u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t t24 = gm_l2;
    gm_l3 = t24;
    void * t25 = gm_l1;
    gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)t25));
    gm_l4 = t26;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t27 = gm_l5;
            uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t29 = (t27 < t28);
            if (!t29) {
                goto gm_lb1;
            }
            {
                void * t30 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t32 = gm_l5;
                {
                    {
                        memmove(((uint8_t *)&gm_l6), (((uint8_t *)t30) + (uintptr_t)t32 * 32), 32);
                        {
                            {
                                uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                                uint8_t t34 = (uint8_t)(t33 == ((uint8_t)0ULL));
                                if (!t34) {
                                    goto gm_lb7;
                                }
                                uint8_t t35 = (uint8_t)(GM_LD(void *, ((((uint8_t *)&gm_l6) + 8) + 8)) != 0);
                                if (t35) {
                                    goto gm_lb7;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                                uint8_t t37 = (uint8_t)(t36 == ((uint8_t)0ULL));
                                if (!t37) {
                                    goto gm_lb8;
                                }
                                uint8_t t38 = (uint8_t)(GM_LD(void *, ((((uint8_t *)&gm_l6) + 8) + 8)) != 0);
                                if (!t38) {
                                    goto gm_lb8;
                                }
                                void * t39 = GM_LD(void *, ((((uint8_t *)&gm_l6) + 8) + 8));
                                gm_l7 = t39;
                                void * t40 = gm_l0;
                                void * t41 = gm_l7;
                                uint8_t t42 = gm_l3;
                                uint8_t t43 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t40, t41, t42);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l3 = t43;
                                goto gm_lb6;
                            }
gm_lb8:;
                            {
                                uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                                uint8_t t45 = (uint8_t)(t44 == ((uint8_t)1ULL));
                                if (!t45) {
                                    goto gm_lb9;
                                }
                                void * t46 = GM_LD(void *, ((((uint8_t *)&gm_l6) + 8) + 16));
                                gm_l8 = t46;
                                void * t47 = gm_l0;
                                void * t48 = gm_l8;
                                uint8_t t49 = gm_l3;
                                uint8_t t50 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t47, t48, t49);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l3 = t50;
                                goto gm_lb6;
                            }
gm_lb9:;
                            {
                                uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                                uint8_t t52 = (uint8_t)(t51 == ((uint8_t)2ULL));
                                if (!t52) {
                                    goto gm_lb10;
                                }
                                void * t53 = GM_LD(void *, (((uint8_t *)&gm_l6) + 8));
                                gm_l9 = t53;
                                void * t54 = GM_LD(void *, ((((uint8_t *)&gm_l6) + 8) + 8));
                                gm_l10 = t54;
                                void * t55 = gm_l0;
                                void * t56 = gm_l9;
                                uint8_t t57 = gm_l3;
                                uint8_t t58 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t55, t56, t57);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l3 = t58;
                                void * t59 = gm_l0;
                                void * t60 = gm_l10;
                                uint8_t t61 = gm_l3;
                                uint8_t t62 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t59, t60, t61);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l3 = t62;
                                goto gm_lb6;
                            }
gm_lb10:;
                            {
                                uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                                uint8_t t64 = (uint8_t)(t63 == ((uint8_t)3ULL));
                                if (!t64) {
                                    goto gm_lb11;
                                }
                                gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
                                gm_l11 = t65;
                                void * t66 = GM_LD(void *, ((((uint8_t *)&gm_l6) + 8) + 16));
                                gm_l12 = t66;
                                gm_b16_8 t67 = gm_l11;
                                gm_l13 = t67;
                                gm_l14 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t68 = gm_l14;
                                        uint64_t t69 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                                        uint8_t t70 = (t68 < t69);
                                        if (!t70) {
                                            goto gm_lb12;
                                        }
                                        {
                                            void * t71 = GM_LD(void *, ((uint8_t *)&gm_l13));
                                            uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                                            uint64_t t73 = gm_l14;
                                            {
                                                {
                                                    void * t74 = GM_LD(void *, (((uint8_t *)t71) + (uintptr_t)t73 * 8));
                                                    gm_l15 = t74;
                                                    {
                                                        {
                                                            uint8_t t75 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l15)) != 0);
                                                            if (!t75) {
                                                                goto gm_lb18;
                                                            }
                                                            void * t76 = GM_LD(void *, ((uint8_t *)&gm_l15));
                                                            gm_l16 = t76;
                                                            void * t77 = gm_l0;
                                                            void * t78 = gm_l16;
                                                            uint8_t t79 = gm_l3;
                                                            uint8_t t80 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t77, t78, t79);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            gm_l3 = t80;
                                                            goto gm_lb17;
                                                        }
gm_lb18:;
                                                    }
gm_lb17:;
                                                    goto gm_lb15;
                                                }
gm_lb16:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcfa3vclj60ege, 277, 17);
                                            }
gm_lb15:;
                                        }
gm_lb14:;
                                        uint64_t t81 = gm_l14;
                                        uint64_t r82; bool o83 = __builtin_add_overflow(t81, ((uint64_t)1ULL), &r82);
                                        uint64_t t84 = r82;
                                        gm_l14 = t84;
                                    }
                                }
gm_lb12:;
                                void * t85 = gm_l0;
                                void * t86 = gm_l12;
                                uint8_t t87 = gm_l3;
                                uint8_t t88 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t85, t86, t87);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l3 = t88;
                                goto gm_lb6;
                            }
gm_lb11:;
                            {
                                uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                                uint8_t t90 = (uint8_t)(t89 == ((uint8_t)4ULL));
                                if (!t90) {
                                    goto gm_lb19;
                                }
                                void * t91 = GM_LD(void *, (((uint8_t *)&gm_l6) + 8));
                                gm_l17 = t91;
                                void * t92 = gm_l0;
                                void * t93 = gm_l17;
                                uint8_t t94 = gm_l3;
                                uint8_t t95 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t92, t93, t94);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l3 = t95;
                                goto gm_lb6;
                            }
gm_lb19:;
                            {
                                uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                                uint8_t t97 = (uint8_t)(t96 == ((uint8_t)5ULL));
                                if (!t97) {
                                    goto gm_lb20;
                                }
                                goto gm_lb6;
                            }
gm_lb20:;
                            gm_trap("no match arm matched the scrutinee", gm_srcfa3vclj60ege, 268, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfa3vclj60ege, 267, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t98 = gm_l5;
            uint64_t r99; bool o100 = __builtin_add_overflow(t98, ((uint64_t)1ULL), &r99);
            uint64_t t101 = r99;
            gm_l5 = t101;
        }
    }
gm_lb1:;
    {
        {
            void * t102 = gm_l1;
            uint8_t t103 = (uint8_t)(GM_LD(void *, (((uint8_t *)t102) + 16)) != 0);
            if (!t103) {
                goto gm_lb22;
            }
            void * t104 = GM_LD(void *, (((uint8_t *)t102) + 16));
            gm_l18 = t104;
            void * t105 = gm_l0;
            void * t106 = gm_l18;
            uint8_t t107 = gm_l3;
            uint8_t t108 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t105, t106, t107);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l3 = t108;
            goto gm_lb21;
        }
gm_lb22:;
    }
gm_lb21:;
    uint8_t t109 = gm_l3;
    F.ret = t109;
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
extern __attribute__((aligned(4))) uint8_t gm_f_k_children_3gr7c2u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_k_cond_d97airn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2) {
    struct gm_fr_k_cond_d97airn F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* cond */
    uint8_t gm_l2; /* st */
    uint8_t gm_l3; /* pass */
    uint8_t gm_l4; /* fail */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_tlr9dp305htm7 gm_l7; /* c */
    void * gm_l8; /* x */
    void * gm_l9; /* value */
    GM_STACK_CHECK(&F, 375u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t t10 = gm_l2;
    gm_l3 = t10;
    gm_l4 = ((uint8_t)0ULL);
    gm_b16_8 t11 = gm_l1;
    gm_l5 = t11;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t12 = gm_l6;
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t14 = (t12 < t13);
            if (!t14) {
                goto gm_lb1;
            }
            {
                void * t15 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t17 = gm_l6;
                {
                    {
                        memmove(((uint8_t *)&gm_l7), (((uint8_t *)t15) + (uintptr_t)t17 * 24), 24);
                        {
                            {
                                uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                                uint8_t t19 = (uint8_t)(t18 == ((uint8_t)0ULL));
                                if (!t19) {
                                    goto gm_lb7;
                                }
                                void * t20 = GM_LD(void *, (((uint8_t *)&gm_l7) + 8));
                                gm_l8 = t20;
                                void * t21 = gm_l0;
                                void * t22 = gm_l8;
                                uint8_t t23 = gm_l3;
                                uint8_t t24 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t21, t22, t23);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l3 = t24;
                                {
                                    void * t25 = gm_l8;
                                    uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t25) + 8) + 0));
                                    uint8_t t27 = (uint8_t)(t26 == ((uint8_t)4ULL));
                                    if (t27) {
                                        uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t25) + 8) + 8));
                                        t27 = (uint8_t)(t28 == ((uint8_t)1));
                                    }
                                    uint8_t t29 = (uint8_t)(!t27);
                                    gm_to0f5qn75cgg8 t30 = GM_LD(gm_to0f5qn75cgg8, (((uint8_t *)t25) + 8));
                                    gm_to0f5qn75cgg8 t31;
                                    memset(&t31, 0, sizeof t31);
                                    GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)4ULL));
                                    GM_ST(uint8_t, ((uint8_t *)&t31 + 8), ((uint8_t)1));
                                    if (t29) {
                                        uint8_t t32 = gm_l4;
                                        uint8_t t33 = gm_l3;
                                        uint8_t t34 = (uint8_t)(t32 | t33);
                                        gm_l4 = t34;
                                    }
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                                uint8_t t36 = (uint8_t)(t35 == ((uint8_t)1ULL));
                                if (!t36) {
                                    goto gm_lb8;
                                }
                                void * t37 = GM_LD(void *, ((((uint8_t *)&gm_l7) + 8) + 8));
                                gm_l9 = t37;
                                void * t38 = gm_l0;
                                void * t39 = gm_l9;
                                uint8_t t40 = gm_l3;
                                uint8_t t41 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t38, t39, t40);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l3 = t41;
                                uint8_t t42 = gm_l4;
                                uint8_t t43 = gm_l3;
                                uint8_t t44 = (uint8_t)(t42 | t43);
                                gm_l4 = t44;
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srcfa3vclj60ege, 233, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfa3vclj60ege, 232, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t45 = gm_l6;
            uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
            uint64_t t48 = r46;
            gm_l6 = t48;
        }
    }
gm_lb1:;
    uint8_t t49 = gm_l3;
    uint8_t t50 = gm_l4;
    gm_tqs5p4n2tq19v t51;
    GM_ST(uint8_t, ((uint8_t *)&t51 + 0), t49);
    GM_ST(uint8_t, ((uint8_t *)&t51 + 1), t50);
    F.ret = t51;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_k_consume_nr50dr5(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_tl874t2atnlh3 a2);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_k_exit_ttiloj7(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) uint8_t gm_f_k_expr_hdusp9d(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2) {
    struct gm_fr_k_expr_hdusp9d F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    uint8_t gm_l2; /* st */
    uint8_t gm_l3; /* out */
    void * gm_l4; /* c */
    void * gm_l5; /* v */
    uint8_t gm_l6; /* sv */
    uint8_t gm_l7; /* sc */
    void * gm_l8; /* c */
    gm_teo9d4mncjfdu gm_l9; /* b */
    uint8_t gm_l10; /* sv */
    void * gm_l11; /* v */
    gm_te9hrk0u35rev gm_l12; /* t */
    void * gm_l13; /* v */
    void * gm_l14; /* b */
    gm_trl2khmuphncd gm_l15; /* x */
    gm_tqs5p4n2tq19v gm_l16; /* r */
    uint8_t gm_l17; /* t */
    uint8_t gm_l18; /* f */
    void * gm_l19; /* el */
    void * gm_l20; /* scrutinee */
    gm_b16_8 gm_l21; /* arms */
    uint8_t gm_l22; /* ss */
    uint8_t gm_l23; /* guard_fail */
    gm_b16_8 gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    gm_tbbqefuhv2r8h gm_l26; /* a */
    uint8_t gm_l27; /* entry */
    uint8_t gm_l28; /* g */
    void * gm_l29; /* gx */
    void * gm_l30; /* lhs */
    void * gm_l31; /* rhs */
    uint8_t gm_l32; /* l */
    gm_t3dcoc7077t1q gm_l33; /* x */
    uint8_t gm_l34; /* l */
    uint8_t gm_l37; /* sw */
    gm_b16_8 gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    gm_t4lnf71mruqpt gm_l40; /* h */
    void * gm_l41; /* x */
    void * gm_l42; /* x */
    void * gm_l43; /* x */
    void * gm_l44; /* o */
    uint8_t gm_l45; /* b */
    gm_t0n7rigt5nh46 gm_l46; /* top */
    void * gm_l47; /* m */
    GM_STACK_CHECK(&F, 1632u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l48;
    gm_t72g7o4mtivnc gm_l49;
    {
        uint8_t t38 = gm_l2;
        uint8_t t39 = ((t38 == ((uint8_t)0ULL)));
        if (t39) {
            F.ret = ((uint8_t)0ULL);
            goto gm_fret;
        }
        gm_l3 = ((uint8_t)0ULL);
        void * t40 = gm_l1;
        {
            {
                {
                    uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
                    uint8_t t42 = (uint8_t)(t41 == ((uint8_t)53ULL));
                    if (!t42) {
                        goto gm_lb2;
                    }
                    void * t43 = GM_LD(void *, ((((uint8_t *)t40) + 8) + 8));
                    gm_l4 = t43;
                    void * t44 = GM_LD(void *, (((((uint8_t *)t40) + 8) + 8) + 8));
                    gm_l5 = t44;
                    void * t45 = gm_l0;
                    void * t46 = gm_l5;
                    uint8_t t47 = gm_l2;
                    uint8_t t48 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t45, t46, t47);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l6 = t48;
                    uint8_t t49 = gm_l6;
                    uint8_t t50 = (!(t49 == ((uint8_t)0ULL)));
                    if (t50) {
                        void * t51 = gm_l4;
                        void * t52 = gm_l0;
                        uint32_t t53 = GM_LD(uint32_t, (((uint8_t *)t52) + 8));
                        uint8_t t54 = gm_f_is_k_09a6ffc(t51, t53);
                        if (t54) {
                            void * t55 = gm_l0;
                            uint8_t t56 = gm_l6;
                            void * t57 = gm_l1;
                            gm_tl874t2atnlh3 t58 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t57) + 72));
                            gm_b0_1 t59 = gm_f_k_consume_nr50dr5((void *)NULL, cx, NULL, t55, t56, t58);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        } else {
                            void * t60 = gm_l0;
                            void * t61 = gm_l4;
                            uint8_t t62 = gm_l6;
                            uint8_t t63 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t60, t61, t62);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l7 = t63;
                            void * t64 = gm_l0;
                            uint8_t t65 = gm_l7;
                            void * t66 = gm_l1;
                            gm_tl874t2atnlh3 t67 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t66) + 72));
                            gm_b0_1 t68 = gm_f_k_exit_ttiloj7((void *)NULL, cx, NULL, t64, t65, t67);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                    }
                    gm_l3 = ((uint8_t)0ULL);
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    uint8_t t69 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
                    uint8_t t70 = (uint8_t)(t69 == ((uint8_t)54ULL));
                    if (!t70) {
                        goto gm_lb3;
                    }
                    void * t71 = GM_LD(void *, ((((uint8_t *)t40) + 8) + 8));
                    gm_l8 = t71;
                    void * t72 = gm_l8;
                    void * t73 = gm_l0;
                    uint32_t t74 = GM_LD(uint32_t, (((uint8_t *)t73) + 8));
                    uint8_t t75 = gm_f_is_k_09a6ffc(t72, t74);
                    if (t75) {
                        void * t76 = gm_l0;
                        uint8_t t77 = gm_l2;
                        void * t78 = gm_l1;
                        gm_tl874t2atnlh3 t79 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t78) + 72));
                        gm_b0_1 t80 = gm_f_k_consume_nr50dr5((void *)NULL, cx, NULL, t76, t77, t79);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t81 = GM_LD(uint8_t, ((uint8_t *)gm_ckpkn5umvr0));
                        gm_l3 = t81;
                    } else {
                        void * t82 = gm_l0;
                        void * t83 = gm_l1;
                        uint8_t t84 = gm_l2;
                        uint8_t t85 = gm_f_k_children_3gr7c2u((void *)NULL, cx, NULL, t82, t83, t84);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l3 = t85;
                    }
                    goto gm_lb1;
                }
gm_lb3:;
                {
                    uint8_t t86 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
                    uint8_t t87 = (uint8_t)(t86 == ((uint8_t)49ULL));
                    if (!t87) {
                        goto gm_lb4;
                    }
                    memmove(((uint8_t *)&gm_l9), ((((uint8_t *)t40) + 8) + 8), 24);
                    uint8_t t88 = gm_l2;
                    gm_l10 = t88;
                    {
                        {
                            uint8_t t89 = (uint8_t)(GM_LD(void *, (((uint8_t *)&gm_l9) + 16)) != 0);
                            if (!t89) {
                                goto gm_lb6;
                            }
                            void * t90 = GM_LD(void *, (((uint8_t *)&gm_l9) + 16));
                            gm_l11 = t90;
                            void * t91 = gm_l0;
                            void * t92 = gm_l11;
                            uint8_t t93 = gm_l2;
                            uint8_t t94 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t91, t92, t93);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l10 = t94;
                            goto gm_lb5;
                        }
gm_lb6:;
                    }
gm_lb5:;
                    void * t95 = gm_l0;
                    gm_te9hrk0u35rev t96 = GM_LD(gm_te9hrk0u35rev, ((uint8_t *)&gm_l9));
                    uint8_t t97 = gm_l10;
                    void * t98 = gm_l1;
                    gm_tl874t2atnlh3 t99 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t98) + 72));
                    gm_b0_1 t100 = gm_f_k_jump_41ae4gj((void *)NULL, cx, NULL, t95, t96, t97, ((uint8_t)1), t99);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l3 = ((uint8_t)0ULL);
                    goto gm_lb1;
                }
gm_lb4:;
            }
            {
                {
                    uint8_t t101 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
                    uint8_t t102 = (uint8_t)(t101 == ((uint8_t)50ULL));
                    if (!t102) {
                        goto gm_lb7;
                    }
                    gm_te9hrk0u35rev t103 = GM_LD(gm_te9hrk0u35rev, ((((uint8_t *)t40) + 8) + 8));
                    gm_l12 = t103;
                    void * t104 = gm_l0;
                    gm_te9hrk0u35rev t105 = gm_l12;
                    uint8_t t106 = gm_l2;
                    void * t107 = gm_l1;
                    gm_tl874t2atnlh3 t108 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t107) + 72));
                    gm_b0_1 t109 = gm_f_k_jump_41ae4gj((void *)NULL, cx, NULL, t104, t105, t106, ((uint8_t)0), t108);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l3 = ((uint8_t)0ULL);
                    goto gm_lb1;
                }
gm_lb7:;
                {
                    uint8_t t110 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
                    uint8_t t111 = (uint8_t)(t110 == ((uint8_t)51ULL));
                    if (!t111) {
                        goto gm_lb8;
                    }
                    void * t112 = GM_LD(void *, (((((uint8_t *)t40) + 8) + 8) + 8));
                    gm_l13 = t112;
                    void * t113 = gm_l0;
                    void * t114 = gm_l13;
                    uint8_t t115 = gm_l2;
                    uint8_t t116 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t113, t114, t115);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l3 = ((uint8_t)0ULL);
                    goto gm_lb1;
                }
gm_lb8:;
                {
                    uint8_t t117 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
                    uint8_t t118 = (uint8_t)(t117 == ((uint8_t)46ULL));
                    if (!t118) {
                        goto gm_lb9;
                    }
                    void * t119 = GM_LD(void *, ((((uint8_t *)t40) + 8) + 8));
                    gm_l14 = t119;
                    void * t120 = gm_l0;
                    void * t121 = gm_l14;
                    uint8_t t122 = gm_l2;
                    uint8_t t123 = gm_f_k_block_87r6qua((void *)NULL, cx, NULL, t120, t121, t122);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l3 = t123;
                    goto gm_lb1;
                }
gm_lb9:;
                {
                    uint8_t t124 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
                    uint8_t t125 = (uint8_t)(t124 == ((uint8_t)47ULL));
                    if (!t125) {
                        goto gm_lb10;
                    }
                    memmove(((uint8_t *)&gm_l15), ((((uint8_t *)t40) + 8) + 8), 32);
                    void * t126 = gm_l0;
                    gm_b16_8 t127 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l15));
                    uint8_t t128 = gm_l2;
                    gm_tqs5p4n2tq19v t129 = gm_f_k_cond_d97airn((void *)NULL, cx, NULL, t126, t127, t128);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l16 = t129;
                    void * t130 = gm_l0;
                    void * t131 = GM_LD(void *, (((uint8_t *)&gm_l15) + 16));
                    uint8_t t132 = GM_LD(uint8_t, ((uint8_t *)&gm_l16));
                    uint8_t t133 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t130, t131, t132);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l17 = t133;
                    uint8_t t134 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 1));
                    gm_l18 = t134;
                    {
                        {
                            uint8_t t135 = (uint8_t)(GM_LD(void *, (((uint8_t *)&gm_l15) + 24)) != 0);
                            if (!t135) {
                                goto gm_lb12;
                            }
                            void * t136 = GM_LD(void *, (((uint8_t *)&gm_l15) + 24));
                            gm_l19 = t136;
                            void * t137 = gm_l0;
                            void * t138 = gm_l19;
                            uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 1));
                            uint8_t t140 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t137, t138, t139);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l18 = t140;
                            goto gm_lb11;
                        }
gm_lb12:;
                    }
gm_lb11:;
                    uint8_t t141 = gm_l17;
                    uint8_t t142 = gm_l18;
                    uint8_t t143 = (uint8_t)(t141 | t142);
                    gm_l3 = t143;
                    goto gm_lb1;
                }
gm_lb10:;
            }
            {
                {
                    uint8_t t144 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
                    uint8_t t145 = (uint8_t)(t144 == ((uint8_t)44ULL));
                    if (!t145) {
                        goto gm_lb13;
                    }
                    void * t146 = GM_LD(void *, ((((uint8_t *)t40) + 8) + 8));
                    gm_l20 = t146;
                    gm_b16_8 t147 = GM_LD(gm_b16_8, (((((uint8_t *)t40) + 8) + 8) + 8));
                    gm_l21 = t147;
                    void * t148 = gm_l0;
                    void * t149 = gm_l20;
                    uint8_t t150 = gm_l2;
                    uint8_t t151 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t148, t149, t150);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l22 = t151;
                    uint8_t t152 = gm_l22;
                    gm_l23 = t152;
                    gm_b16_8 t153 = gm_l21;
                    gm_l24 = t153;
                    gm_l25 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t154 = gm_l25;
                            uint64_t t155 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                            uint8_t t156 = (t154 < t155);
                            if (!t156) {
                                goto gm_lb14;
                            }
                            {
                                void * t157 = GM_LD(void *, ((uint8_t *)&gm_l24));
                                uint64_t t158 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                                uint64_t t159 = gm_l25;
                                {
                                    {
                                        memmove(((uint8_t *)&gm_l26), (((uint8_t *)t157) + (uintptr_t)t159 * 24), 24);
                                        uint8_t t160 = gm_l23;
                                        uint8_t t161 = gm_l22;
                                        uint8_t t162 = (uint8_t)(t160 | t161);
                                        gm_l27 = t162;
                                        uint8_t t163 = gm_l27;
                                        gm_l28 = t163;
                                        {
                                            {
                                                uint8_t t164 = (uint8_t)(GM_LD(void *, (((uint8_t *)&gm_l26) + 8)) != 0);
                                                if (!t164) {
                                                    goto gm_lb20;
                                                }
                                                void * t165 = GM_LD(void *, (((uint8_t *)&gm_l26) + 8));
                                                gm_l29 = t165;
                                                void * t166 = gm_l0;
                                                void * t167 = gm_l29;
                                                uint8_t t168 = gm_l27;
                                                uint8_t t169 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t166, t167, t168);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_l28 = t169;
                                                goto gm_lb19;
                                            }
gm_lb20:;
                                        }
gm_lb19:;
                                        uint8_t t170 = gm_l23;
                                        uint8_t t171 = gm_l28;
                                        uint8_t t172 = (uint8_t)(t170 | t171);
                                        gm_l23 = t172;
                                        uint8_t t173 = gm_l3;
                                        void * t174 = gm_l0;
                                        void * t175 = GM_LD(void *, (((uint8_t *)&gm_l26) + 16));
                                        uint8_t t176 = gm_l28;
                                        uint8_t t177 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t174, t175, t176);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t178 = (uint8_t)(t173 | t177);
                                        gm_l3 = t178;
                                        goto gm_lb17;
                                    }
gm_lb18:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcfa3vclj60ege, 159, 13);
                                }
gm_lb17:;
                            }
gm_lb16:;
                            uint64_t t179 = gm_l25;
                            uint64_t r180; bool o181 = __builtin_add_overflow(t179, ((uint64_t)1ULL), &r180);
                            uint64_t t182 = r180;
                            gm_l25 = t182;
                        }
                    }
gm_lb14:;
                    goto gm_lb1;
                }
gm_lb13:;
            }
            {
                {
                    uint8_t t183 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
                    uint8_t t184 = (uint8_t)(t183 == ((uint8_t)31ULL));
                    if (!t184) {
                        goto gm_lb21;
                    }
                    void * t185 = GM_LD(void *, (((((uint8_t *)t40) + 8) + 8) + 8));
                    gm_l30 = t185;
                    void * t186 = GM_LD(void *, (((((uint8_t *)t40) + 8) + 8) + 16));
                    gm_l31 = t186;
                    void * t187 = gm_l0;
                    void * t188 = gm_l30;
                    uint8_t t189 = gm_l2;
                    uint8_t t190 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t187, t188, t189);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l32 = t190;
                    uint8_t t191 = gm_l32;
                    void * t192 = gm_l0;
                    void * t193 = gm_l31;
                    uint8_t t194 = gm_l32;
                    uint8_t t195 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t192, t193, t194);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t196 = (uint8_t)(t191 | t195);
                    gm_l3 = t196;
                    goto gm_lb1;
                }
gm_lb21:;
                {
                    uint8_t t197 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
                    uint8_t t198 = (uint8_t)(t197 == ((uint8_t)41ULL));
                    if (!t198) {
                        goto gm_lb22;
                    }
                    memmove(((uint8_t *)&gm_l33), ((((uint8_t *)t40) + 8) + 8), 24);
                    void * t199 = gm_l0;
                    void * t200 = GM_LD(void *, ((uint8_t *)&gm_l33));
                    uint8_t t201 = gm_l2;
                    uint8_t t202 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t199, t200, t201);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l34 = t202;
                    uint8_t t203 = gm_l34;
                    void * t204 = gm_l0;
                    void * t205 = GM_LD(void *, (((uint8_t *)&gm_l33) + 16));
                    uint8_t t206 = gm_l34;
                    uint8_t t207 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t204, t205, t206);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t208 = (uint8_t)(t203 | t207);
                    gm_l3 = t208;
                    goto gm_lb1;
                }
gm_lb22:;
                {
                    uint8_t t209 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
                    uint8_t t210 = (uint8_t)(t209 == ((uint8_t)48ULL));
                    if (!t210) {
                        goto gm_lb23;
                    }
                    memmove(((uint8_t *)&F.l35), ((((uint8_t *)t40) + 8) + 8), 40);
                    void * t211 = gm_l0;
                    uint32_t t212 = GM_LD(uint32_t, ((uint8_t *)&F.l35));
                    gm_b16_8 t213 = GM_LD(gm_b16_8, (((uint8_t *)&F.l35) + 8));
                    void * t214 = GM_LD(void *, (((uint8_t *)&F.l35) + 24));
                    void * t215 = GM_LD(void *, (((uint8_t *)&F.l35) + 32));
                    uint8_t t216 = gm_l2;
                    uint8_t t217 = gm_f_k_loop_ot156p6((void *)NULL, cx, NULL, t211, t212, t213, t214, t215, t216);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l3 = t217;
                    goto gm_lb1;
                }
gm_lb23:;
                {
                    uint8_t t218 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
                    uint8_t t219 = (uint8_t)(t218 == ((uint8_t)59ULL));
                    if (!t219) {
                        goto gm_lb24;
                    }
                    memmove(((uint8_t *)&F.l36), ((((uint8_t *)t40) + 8) + 8), 40);
                    uint8_t t220 = gm_l2;
                    gm_l37 = t220;
                    gm_b16_8 t221 = GM_LD(gm_b16_8, (((uint8_t *)&F.l36) + 8));
                    gm_l38 = t221;
                    gm_l39 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t222 = gm_l39;
                            uint64_t t223 = GM_LD(uint64_t, (((uint8_t *)&gm_l38) + 8));
                            uint8_t t224 = (t222 < t223);
                            if (!t224) {
                                goto gm_lb25;
                            }
                            {
                                void * t225 = GM_LD(void *, ((uint8_t *)&gm_l38));
                                uint64_t t226 = GM_LD(uint64_t, (((uint8_t *)&gm_l38) + 8));
                                uint64_t t227 = gm_l39;
                                {
                                    {
                                        memmove(((uint8_t *)&gm_l40), (((uint8_t *)t225) + (uintptr_t)t227 * 32), 32);
                                        {
                                            {
                                                uint8_t t228 = GM_LD(uint8_t, (((uint8_t *)&gm_l40) + 0));
                                                uint8_t t229 = (uint8_t)(t228 == ((uint8_t)0ULL));
                                                if (!t229) {
                                                    goto gm_lb31;
                                                }
                                                void * t230 = GM_LD(void *, (((uint8_t *)&gm_l40) + 8));
                                                gm_l41 = t230;
                                                void * t231 = gm_l41;
                                                gm_l43 = t231;
                                                goto gm_lb30;
                                            }
gm_lb31:;
                                            {
                                                uint8_t t232 = GM_LD(uint8_t, (((uint8_t *)&gm_l40) + 0));
                                                uint8_t t233 = (uint8_t)(t232 == ((uint8_t)1ULL));
                                                if (!t233) {
                                                    goto gm_lb32;
                                                }
                                                void * t234 = GM_LD(void *, ((((uint8_t *)&gm_l40) + 8) + 16));
                                                gm_l42 = t234;
                                                void * t235 = gm_l42;
                                                gm_l43 = t235;
                                                goto gm_lb30;
                                            }
gm_lb32:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcfa3vclj60ege, 179, 22);
                                        }
gm_lb30:;
                                        void * t236 = gm_l0;
                                        void * t237 = gm_l43;
                                        uint8_t t238 = gm_l37;
                                        uint8_t t239 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t236, t237, t238);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l37 = t239;
                                        goto gm_lb28;
                                    }
gm_lb29:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcfa3vclj60ege, 178, 13);
                                }
gm_lb28:;
                            }
gm_lb27:;
                            uint64_t t240 = gm_l39;
                            uint64_t r241; bool o242 = __builtin_add_overflow(t240, ((uint64_t)1ULL), &r241);
                            uint64_t t243 = r241;
                            gm_l39 = t243;
                        }
                    }
gm_lb25:;
                    {
                        {
                            uint8_t t244 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l36) + 24)) != 0);
                            if (!t244) {
                                goto gm_lb34;
                            }
                            void * t245 = GM_LD(void *, (((uint8_t *)&F.l36) + 24));
                            gm_l44 = t245;
                            void * t246 = gm_l0;
                            void * t247 = gm_l44;
                            uint8_t t248 = gm_l37;
                            uint8_t t249 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t246, t247, t248);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l37 = t249;
                            goto gm_lb33;
                        }
gm_lb34:;
                    }
gm_lb33:;
                    void * t250 = gm_l0;
                    void * t251 = (void *)(((uint8_t *)t250) + 16);
                    uint32_t t252 = GM_LD(uint32_t, ((uint8_t *)&F.l36));
                    gm_t0n7rigt5nh46 t253;
                    GM_ST(uint16_t, (uint8_t *)&t253 + 6, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t253 + 0), t252);
                    GM_ST(uint8_t, ((uint8_t *)&t253 + 4), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t253 + 5), ((uint8_t)0ULL));
                    gm_tftb1odc739t5 t254 = gm_f_push_dqg4p50(t251, GM_CONV(gm_cs8r2qbercod2k, t253));
                    gm_l48 = t254;
                    uint8_t t255 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                    if (!t255) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l48) + 1, gm_srcfa3vclj60ege, 186, 13);
                    void * t256 = gm_l0;
                    void * t257 = GM_LD(void *, (((uint8_t *)&F.l36) + 32));
                    uint8_t t258 = gm_l37;
                    uint8_t t259 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t256, t257, t258);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l45 = t259;
                    void * t260 = gm_l0;
                    void * t261 = (void *)(((uint8_t *)t260) + 16);
                    gm_t72g7o4mtivnc t262 = gm_f_pop_7fqs7gj(t261);
                    gm_l49 = t262;
                    uint8_t t263 = (uint8_t)((((uint8_t *)&gm_l49))[8] != 0);
                    if (t263) {
                        gm_t0n7rigt5nh46 t264 = GM_LD(gm_t0n7rigt5nh46, ((uint8_t *)&gm_l49));
                        gm_l46 = t264;
                    } else {
                        F.ret = ((uint8_t)0ULL);
                        goto gm_fret;
                    }
                    uint8_t t265 = gm_l45;
                    uint8_t t266 = GM_LD(uint8_t, (((uint8_t *)&gm_l46) + 5));
                    uint8_t t267 = (uint8_t)(t265 | t266);
                    gm_l3 = t267;
                    goto gm_lb1;
                }
gm_lb24:;
            }
            {
                {
                    uint8_t t268 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
                    uint8_t t269 = (uint8_t)(t268 == ((uint8_t)52ULL));
                    if (!t269) {
                        goto gm_lb35;
                    }
                    void * t270 = GM_LD(void *, ((((uint8_t *)t40) + 8) + 8));
                    gm_l47 = t270;
                    void * t271 = gm_l0;
                    void * t272 = gm_l47;
                    uint8_t t273 = gm_l2;
                    uint8_t t274 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t271, t272, t273);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l3 = ((uint8_t)0ULL);
                    goto gm_lb1;
                }
gm_lb35:;
                {
                    void * t275 = gm_l0;
                    void * t276 = gm_l1;
                    uint8_t t277 = gm_l2;
                    uint8_t t278 = gm_f_k_children_3gr7c2u((void *)NULL, cx, NULL, t275, t276, t277);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l3 = t278;
                    goto gm_lb1;
                }
gm_lb36:;
                gm_trap("no match arm matched the scrutinee", gm_srcfa3vclj60ege, 112, 5);
            }
        }
gm_lb1:;
    }
    {
        void * t279 = gm_l0;
        void * t280 = GM_LD(void *, ((uint8_t *)t279));
        void * t281 = gm_l1;
        gm_t88rcucg1b7uk t282 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t281));
        gm_t53at80pfdus3 t283;
        gm_f_kind_jcnt4hm((void *)&t283, t280, GM_CONV(gm_csguankko4lmhd, t282));
        uint8_t t284 = GM_LD(uint8_t, (((uint8_t *)&t283) + 0));
        uint8_t t285 = (uint8_t)((t284 == ((uint8_t)5ULL)));
        gm_t53at80pfdus3 t286;
        memset(&t286, 0, sizeof t286);
        GM_ST(uint8_t, ((uint8_t *)&t286 + 0), ((uint8_t)5ULL));
        if (t285) {
            F.ret = ((uint8_t)0ULL);
            goto gm_fret;
        }
        uint8_t t287 = gm_l3;
        F.ret = t287;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_k_jump_41ae4gj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_te9hrk0u35rev a1, uint8_t a2, uint8_t a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_k_jump_41ae4gj F;
    void * gm_l0; /* cx */
    gm_te9hrk0u35rev gm_l1; /* target */
    uint8_t gm_l2; /* st */
    uint8_t gm_l3; /* is_break */
    gm_tl874t2atnlh3 gm_l4; /* span */
    uint32_t gm_l5; /* levels */
    uint32_t gm_l6; /* label */
    uint64_t gm_l7; /* i */
    void * gm_l8; /* l */
    GM_STACK_CHECK(&F, 257u);
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
            uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)0ULL));
            if (!t8) {
                goto gm_lb2;
            }
            uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
            gm_l5 = t9;
            uint32_t t10 = GM_LD(uint32_t, ((((uint8_t *)&gm_l1) + 4) + 4));
            gm_l6 = t10;
            uint32_t t11 = gm_l5;
            uint8_t t12 = ((t11 == ((uint32_t)0ULL)));
            if (!t12) {
                goto gm_lb2;
            }
            void * t13 = gm_l0;
            uint64_t t14 = GM_LD(uint64_t, ((((uint8_t *)t13) + 16) + 8));
            gm_l7 = t14;
            {
                for (;;) {
                    uint64_t t15 = gm_l7;
                    uint8_t t16 = (t15 > ((uint64_t)0ULL));
                    if (!t16) {
                        goto gm_lb3;
                    }
                    {
                        uint64_t t17 = gm_l7;
                        uint64_t r18; bool o19 = __builtin_sub_overflow(t17, ((uint64_t)1ULL), &r18);
                        if (o19) gm_trap("integer overflow in `-`", gm_srcfa3vclj60ege, 212, 13);
                        uint64_t t20 = r18;
                        gm_l7 = t20;
                        void * t21 = gm_l0;
                        void * t22 = GM_LD(void *, (((uint8_t *)t21) + 16));
                        uint64_t t23 = GM_LD(uint64_t, ((((uint8_t *)t21) + 16) + 8));
                        uint64_t t24 = gm_l7;
                        if ((uintptr_t)t24 >= (uintptr_t)(t23)) gm_trap_index((uintptr_t)t24, t23, gm_srcfa3vclj60ege, 213, 19);
                        void * t25 = (void *)(((uint8_t *)t22) + (uintptr_t)t24 * 8);
                        gm_l8 = t25;
                        void * t26 = gm_l8;
                        uint32_t t27 = GM_LD(uint32_t, ((uint8_t *)t26));
                        uint32_t t28 = gm_l6;
                        uint8_t t29 = (!(t27 == t28));
                        if (t29) {
                            goto gm_lb5;
                        }
                        uint8_t t30 = gm_l3;
                        if (t30) {
                            void * t31 = gm_l8;
                            void * t32 = gm_l8;
                            uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)t32) + 5));
                            uint8_t t34 = gm_l2;
                            uint8_t t35 = (uint8_t)(t33 | t34);
                            GM_ST(uint8_t, (((uint8_t *)t31) + 5), t35);
                        } else {
                            void * t36 = gm_l8;
                            void * t37 = gm_l8;
                            uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)t37) + 4));
                            uint8_t t39 = gm_l2;
                            uint8_t t40 = (uint8_t)(t38 | t39);
                            GM_ST(uint8_t, (((uint8_t *)t36) + 4), t40);
                        }
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
gm_lb5:;
                }
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_te9hrk0u35rev t41 = gm_l1;
    uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&t41) + 0));
    uint8_t t43 = (uint8_t)((t42 == ((uint8_t)1ULL)));
    gm_te9hrk0u35rev t44;
    GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&t44 + 8, 0);
    GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)1ULL));
    if (t43) {
        void * t45 = gm_l0;
        uint8_t t46 = gm_l2;
        gm_tl874t2atnlh3 t47 = gm_l4;
        gm_b0_1 t48 = gm_f_k_exit_ttiloj7((void *)NULL, cx, NULL, t45, t46, t47);
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
inline __attribute__((aligned(4))) uint8_t gm_f_k_loop_ot156p6(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, void * a3, void * a4, uint8_t a5) {
    struct gm_fr_k_loop_ot156p6 F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* label */
    gm_b16_8 gm_l2; /* cond */
    void * gm_l3; /* body */
    void * gm_l4; /* step */
    uint8_t gm_l5; /* st */
    uint8_t gm_l6; /* entry */
    uint64_t gm_l7; /* saved */
    gm_tqs5p4n2tq19v gm_l8; /* r */
    uint8_t gm_l9; /* b */
    gm_t0n7rigt5nh46 gm_l10; /* top */
    uint8_t gm_l11; /* back */
    void * gm_l12; /* x */
    uint8_t gm_l13; /* next */
    GM_STACK_CHECK(&F, 285u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_tftb1odc739t5 gm_l14;
    gm_t72g7o4mtivnc gm_l15;
    uint8_t t7 = gm_l5;
    gm_l6 = t7;
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb1;
            }
            {
                void * t8 = gm_l0;
                uint64_t t9 = GM_LD(uint64_t, ((((uint8_t *)t8) + 56) + 8));
                gm_l7 = t9;
                void * t10 = gm_l0;
                gm_b16_8 t11 = gm_l2;
                uint8_t t12 = gm_l6;
                gm_tqs5p4n2tq19v t13 = gm_f_k_cond_d97airn((void *)NULL, cx, NULL, t10, t11, t12);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l8 = t13;
                void * t14 = gm_l0;
                void * t15 = (void *)(((uint8_t *)t14) + 16);
                uint32_t t16 = gm_l1;
                gm_t0n7rigt5nh46 t17;
                GM_ST(uint16_t, (uint8_t *)&t17 + 6, 0);
                GM_ST(uint32_t, ((uint8_t *)&t17 + 0), t16);
                GM_ST(uint8_t, ((uint8_t *)&t17 + 4), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t17 + 5), ((uint8_t)0ULL));
                gm_tftb1odc739t5 t18 = gm_f_push_dqg4p50(t15, GM_CONV(gm_cs8r2qbercod2k, t17));
                gm_l14 = t18;
                uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_srcfa3vclj60ege, 252, 9);
                void * t20 = gm_l0;
                void * t21 = gm_l3;
                uint8_t t22 = GM_LD(uint8_t, ((uint8_t *)&gm_l8));
                uint8_t t23 = gm_f_k_block_87r6qua((void *)NULL, cx, NULL, t20, t21, t22);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l9 = t23;
                void * t24 = gm_l0;
                void * t25 = (void *)(((uint8_t *)t24) + 16);
                gm_t72g7o4mtivnc t26 = gm_f_pop_7fqs7gj(t25);
                gm_l15 = t26;
                uint8_t t27 = (uint8_t)((((uint8_t *)&gm_l15))[8] != 0);
                if (t27) {
                    gm_t0n7rigt5nh46 t28 = GM_LD(gm_t0n7rigt5nh46, ((uint8_t *)&gm_l15));
                    gm_l10 = t28;
                } else {
                    F.ret = ((uint8_t)0ULL);
                    goto gm_fret;
                }
                uint8_t t29 = gm_l9;
                uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 4));
                uint8_t t31 = (uint8_t)(t29 | t30);
                gm_l11 = t31;
                {
                    {
                        uint8_t t32 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l4)) != 0);
                        if (!t32) {
                            goto gm_lb5;
                        }
                        void * t33 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        gm_l12 = t33;
                        void * t34 = gm_l0;
                        void * t35 = gm_l12;
                        uint8_t t36 = gm_l11;
                        uint8_t t37 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t34, t35, t36);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l11 = t37;
                        goto gm_lb4;
                    }
gm_lb5:;
                }
gm_lb4:;
                uint8_t t38 = gm_l6;
                uint8_t t39 = gm_l11;
                uint8_t t40 = (uint8_t)(t38 | t39);
                gm_l13 = t40;
                uint8_t t41 = gm_l13;
                uint8_t t42 = gm_l6;
                uint8_t t43 = ((t41 == t42));
                if (t43) {
                    uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 1));
                    uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 5));
                    uint8_t t46 = (uint8_t)(t44 | t45);
                    F.ret = t46;
                    goto gm_fret;
                }
                void * t47 = gm_l0;
                void * t48 = (void *)(((uint8_t *)t47) + 56);
                uint64_t t49 = gm_l7;
                gm_f_truncate_0s6rl4q(t48, t49);
                uint8_t t50 = gm_l13;
                gm_l6 = t50;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_mentions_local_6sbsseu(void * a0, uint32_t a1);
extern __attribute__((aligned(4))) uint8_t gm_f_mentions_upvar_f852a0h(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_only_consumed_pk7topa(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_only_consumed_pk7topa F;
    void * gm_l0; /* e */
    uint32_t gm_l2; /* l */
    void * gm_l3; /* b */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_trcmmd12cefb1 gm_l6; /* st */
    void * gm_l7; /* body */
    void * gm_l8; /* c */
    void * gm_l9; /* v */
    void * gm_l10; /* c */
    GM_STACK_CHECK(&F, 347u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    void * t15 = gm_l0;
    {
        {
            uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t15) + 8) + 0));
            uint8_t t17 = (uint8_t)(t16 == ((uint8_t)15ULL));
            if (!t17) {
                goto gm_lb2;
            }
            uint32_t t18 = GM_LD(uint32_t, ((((uint8_t *)t15) + 8) + 8));
            gm_l2 = t18;
            uint32_t t19 = gm_l2;
            uint32_t t20 = F.l1;
            uint8_t t21 = (!(t19 == t20));
            F.ret = t21;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t15) + 8) + 0));
            uint8_t t23 = (uint8_t)(t22 == ((uint8_t)46ULL));
            if (!t23) {
                goto gm_lb3;
            }
            void * t24 = GM_LD(void *, ((((uint8_t *)t15) + 8) + 8));
            gm_l3 = t24;
            void * t25 = gm_l3;
            gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)t25));
            gm_l4 = t26;
            gm_l5 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t28 = gm_l5;
                    uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint8_t t30 = (t28 < t29);
                    if (!t30) {
                        goto gm_lb4;
                    }
                    {
                        void * t31 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        uint64_t t33 = gm_l5;
                        {
                            {
                                memmove(((uint8_t *)&gm_l6), (((uint8_t *)t31) + (uintptr_t)t33 * 32), 32);
                                {
                                    {
                                        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                                        uint8_t t35 = (uint8_t)(t34 == ((uint8_t)5ULL));
                                        if (!t35) {
                                            goto gm_lb10;
                                        }
                                        void * t36 = GM_LD(void *, ((((uint8_t *)&gm_l6) + 8) + 8));
                                        gm_l7 = t36;
                                        void * t37 = gm_l7;
                                        uint32_t t38 = F.l1;
                                        uint8_t t39 = gm_f_mentions_local_6sbsseu(t37, t38);
                                        if (!t39) {
                                            goto gm_lb10;
                                        }
                                        F.ret = ((uint8_t)0);
                                        goto gm_fret;
                                    }
gm_lb10:;
                                }
gm_lb9:;
                                goto gm_lb7;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srcfa3vclj60ege, 71, 22);
                        }
gm_lb7:;
                    }
gm_lb6:;
                    uint64_t t40 = gm_l5;
                    uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                    uint64_t t43 = r41;
                    gm_l5 = t43;
                }
            }
gm_lb4:;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t44 = GM_LD(uint8_t, ((((uint8_t *)t15) + 8) + 0));
            uint8_t t45 = (uint8_t)(t44 == ((uint8_t)53ULL));
            if (!t45) {
                goto gm_lb11;
            }
            void * t46 = GM_LD(void *, ((((uint8_t *)t15) + 8) + 8));
            gm_l8 = t46;
            void * t47 = GM_LD(void *, (((((uint8_t *)t15) + 8) + 8) + 8));
            gm_l9 = t47;
            void * t48 = gm_l8;
            uint32_t t49 = F.l1;
            uint8_t t50 = gm_f_is_k_09a6ffc(t48, t49);
            if (t50) {
                void * t51 = gm_l9;
                uint32_t t52 = F.l1;
                uint8_t t53 = gm_f_only_consumed_pk7topa(t51, t52);
                F.ret = t53;
                goto gm_fret;
            }
            goto gm_lb1;
        }
gm_lb11:;
        {
            uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)t15) + 8) + 0));
            uint8_t t55 = (uint8_t)(t54 == ((uint8_t)54ULL));
            if (!t55) {
                goto gm_lb12;
            }
            void * t56 = GM_LD(void *, ((((uint8_t *)t15) + 8) + 8));
            gm_l10 = t56;
            void * t57 = gm_l10;
            uint32_t t58 = F.l1;
            uint8_t t59 = gm_f_is_k_09a6ffc(t57, t58);
            if (t59) {
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto gm_lb1;
        }
gm_lb12:;
        {
            goto gm_lb1;
        }
gm_lb13:;
        gm_trap("no match arm matched the scrutinee", gm_srcfa3vclj60ege, 69, 5);
    }
gm_lb1:;
    F.l11 = ((uint8_t)1);
    void * t60 = gm_l0;
    gm_closure t61 = ((gm_closure){(void *)gm_f_fn_clkgu5i, (void *)&F.h, cx});
    gm_f_for_each_child_fm49s73(t60, t61);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t62 = F.l11;
    F.ret = t62;
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

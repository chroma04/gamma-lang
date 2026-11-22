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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint8_t f2; uint8_t pe[3]; } gm_tilqfqdpj446h;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_t9805qtonbsag f0; void * f1; } gm_t12ib7gg43lh0;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; uint8_t v2; gm_t9qkh0rsooc8d v3; gm_t9qkh0rsooc8d v4; gm_tcc1rq4n1avb2 v5; uint32_t v6; uint32_t v7; } f1; } gm_t8588ma2cr9d7;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; uint64_t f1; } gm_tfbnciphf3261;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_b16_8 f1; } gm_thervm8j23qet;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tilqfqdpj446h) == 12 && _Alignof(gm_tilqfqdpj446h) == 4 && offsetof(gm_tilqfqdpj446h, f0) == 0 && offsetof(gm_tilqfqdpj446h, f1) == 4 && offsetof(gm_tilqfqdpj446h, f2) == 8, "layout of `(dst: u32, f: u32, second: bool)`");
typedef struct { gm_b16_8 f0; gm_tf5srh4mp4b3i f1; } gm_tj64ui6kruni6;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[12]; unsigned __int128 f1; } gm_tl5jbgvjjt15g;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_t88rcucg1b7uk f2; } gm_tpfol4ocdvoij;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_b16_8 f2; gm_t88rcucg1b7uk f3; uint8_t p4[4]; gm_b16_8 f4; uint32_t f5; uint8_t p6[4]; gm_b16_8 f6; uint32_t f7; uint8_t pe[4]; } gm_tqh25f25b69k5;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_trt0mvq732stt;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t12ib7gg43lh0) == 16 && _Alignof(gm_t12ib7gg43lh0) == 8 && offsetof(gm_t12ib7gg43lh0, f0) == 0 && offsetof(gm_t12ib7gg43lh0, f1) == 8, "layout of `(dst: ?u32, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_tj64ui6kruni6 f1; } gm_t19us6e77elqj;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `(string, usize)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { uint32_t v0; gm_tfbnciphf3261 v1; gm_tl5jbgvjjt15g v2; gm_t88rcucg1b7uk v3; gm_t88rcucg1b7uk v4; } f1; } gm_t7b7esi3n9r90;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_te9hrk0u35rev v0; gm_te9hrk0u35rev v1; uint32_t v2; } f1; } gm_t7pnmbsgd9mnq;
_Static_assert(sizeof(gm_t8588ma2cr9d7) == 12 && _Alignof(gm_t8588ma2cr9d7) == 4 && offsetof(gm_t8588ma2cr9d7, f0) == 0 && offsetof(gm_t8588ma2cr9d7, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_t9805qtonbsag f0; gm_t8588ma2cr9d7 f1; uint8_t p2[4]; gm_b16_8 f2; } gm_te4bkgqfi01mk;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
_Static_assert(sizeof(gm_tfbnciphf3261) == 16 && _Alignof(gm_tfbnciphf3261) == 8 && offsetof(gm_tfbnciphf3261, f0) == 0 && offsetof(gm_tfbnciphf3261, f1) == 8, "layout of `(ty: Ty, bits: u64)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_thervm8j23qet) == 24 && _Alignof(gm_thervm8j23qet) == 8 && offsetof(gm_thervm8j23qet, f0) == 0 && offsetof(gm_thervm8j23qet, f1) == 8, "layout of `Slot(string, ())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tj64ui6kruni6) == 32 && _Alignof(gm_tj64ui6kruni6) == 8 && offsetof(gm_tj64ui6kruni6, f0) == 0 && offsetof(gm_tj64ui6kruni6, f1) == 16, "layout of `(string, ?usize)`");
_Static_assert(sizeof(gm_tl5jbgvjjt15g) == 32 && _Alignof(gm_tl5jbgvjjt15g) == 16 && offsetof(gm_tl5jbgvjjt15g, f0) == 0 && offsetof(gm_tl5jbgvjjt15g, f1) == 16, "layout of `(ty: Ty, bits: u128)`");
typedef struct { uint64_t f0; gm_trqfvmjtt4sij f1; } gm_tmupb6k0asuti;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_tpfol4ocdvoij) == 32 && _Alignof(gm_tpfol4ocdvoij) == 8 && offsetof(gm_tpfol4ocdvoij, f0) == 0 && offsetof(gm_tpfol4ocdvoij, f1) == 16 && offsetof(gm_tpfol4ocdvoij, f2) == 28, "layout of `(name: string, span: Span, ty: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; } f1; } gm_tqgtd5gtgj65s;
_Static_assert(sizeof(gm_tqh25f25b69k5) == 96 && _Alignof(gm_tqh25f25b69k5) == 8 && offsetof(gm_tqh25f25b69k5, f0) == 0 && offsetof(gm_tqh25f25b69k5, f1) == 8 && offsetof(gm_tqh25f25b69k5, f2) == 24 && offsetof(gm_tqh25f25b69k5, f3) == 40 && offsetof(gm_tqh25f25b69k5, f4) == 48 && offsetof(gm_tqh25f25b69k5, f5) == 64 && offsetof(gm_tqh25f25b69k5, f6) == 72 && offsetof(gm_tqh25f25b69k5, f7) == 88, "layout of `Func`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_trt0mvq732stt) == 32 && _Alignof(gm_trt0mvq732stt) == 8 && offsetof(gm_trt0mvq732stt, f0) == 0 && offsetof(gm_trt0mvq732stt, f1) == 8 && offsetof(gm_trt0mvq732stt, f2) == 24, "layout of `(dst: u32, bytes: string, c: bool)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_tcc1rq4n1avb2 v1; uint64_t v2; gm_tpfol4ocdvoij v3; uint32_t v4; } f1; } gm_tu78tssgij60g;
_Static_assert(sizeof(gm_t19us6e77elqj) == 40 && _Alignof(gm_t19us6e77elqj) == 8 && offsetof(gm_t19us6e77elqj, f0) == 0 && offsetof(gm_t19us6e77elqj, f1) == 8, "layout of `(i: usize, x: RunExit)`");
typedef struct { uint32_t f0; gm_ts6oqppjh0ba3 f1; uint8_t p2[10]; gm_t7b7esi3n9r90 f2; gm_t7b7esi3n9r90 f3; } gm_t1ctgp8j1v2as;
typedef struct { gm_t7b7esi3n9r90 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_t1s3s9ds8udn4;
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
typedef struct { uint32_t f0; gm_tmn7n0n9raou5 f1; uint8_t p2[4]; gm_t7b7esi3n9r90 f2; } gm_t5hdm76ui2uro;
_Static_assert(sizeof(gm_t7b7esi3n9r90) == 48 && _Alignof(gm_t7b7esi3n9r90) == 16 && offsetof(gm_t7b7esi3n9r90, f0) == 0 && offsetof(gm_t7b7esi3n9r90, f1) == 16, "layout of `Value`");
_Static_assert(sizeof(gm_t7pnmbsgd9mnq) == 16 && _Alignof(gm_t7pnmbsgd9mnq) == 4 && offsetof(gm_t7pnmbsgd9mnq, f0) == 0 && offsetof(gm_t7pnmbsgd9mnq, f1) == 4, "layout of `Exit`");
typedef struct { uint32_t f0; uint8_t p1[12]; gm_t7b7esi3n9r90 f1; uint32_t f2; uint8_t p3[12]; gm_t7b7esi3n9r90 f3; } gm_t8rrrcunvsibs;
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t419mo45g7j0a v0; gm_t7b7esi3n9r90 v1; } f1; } gm_t9i04n6j2d1gi;
typedef struct { uint32_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[11]; gm_t7b7esi3n9r90 f2; } gm_tbto4k5jg9fve;
typedef struct { uint32_t f0; uint8_t p1[12]; gm_t7b7esi3n9r90 f1; uint32_t f2; uint8_t pe[12]; } gm_tcpisgbff1dqv;
_Static_assert(sizeof(gm_te4bkgqfi01mk) == 40 && _Alignof(gm_te4bkgqfi01mk) == 8 && offsetof(gm_te4bkgqfi01mk, f0) == 0 && offsetof(gm_te4bkgqfi01mk, f1) == 8 && offsetof(gm_te4bkgqfi01mk, f2) == 24, "layout of `(dst: ?u32, op: Builtin, args: []Value)`");
typedef struct { gm_t7b7esi3n9r90 f0; uint8_t f1; uint8_t p2[3]; uint32_t f2; uint8_t pe[8]; } gm_tejc6vv5sqno9;
typedef struct { uint32_t f0; uint8_t p1[12]; gm_t7b7esi3n9r90 f1; } gm_tf4lk4crue8vt;
typedef struct { gm_t7b7esi3n9r90 f0; uint8_t f1; uint8_t pe[15]; } gm_tf8kfhcnval78;
typedef struct { uint32_t f0; uint8_t p1[12]; gm_t7b7esi3n9r90 f1; gm_t7b7esi3n9r90 f2; gm_t7b7esi3n9r90 f3; uint8_t f4; uint8_t p5[7]; uint64_t f5; } gm_tgroihbjsmdvm;
typedef struct { gm_tmupb6k0asuti f0; uint8_t f1; uint8_t pe[7]; } gm_tj6rff10m69j7;
typedef struct { gm_tu78tssgij60g f0; gm_b16_8 f1; } gm_tlg23kb3oj23s;
typedef struct { uint8_t f0; uint8_t p1[15]; union { uint32_t v0; gm_t7b7esi3n9r90 v1; gm_t419mo45g7j0a v2; } f1; } gm_tllthq21h2f0k;
_Static_assert(sizeof(gm_tmupb6k0asuti) == 48 && _Alignof(gm_tmupb6k0asuti) == 8 && offsetof(gm_tmupb6k0asuti, f0) == 0 && offsetof(gm_tmupb6k0asuti, f1) == 8, "layout of `(i: usize, x: List((name: string, def: string)))`");
typedef struct { gm_b16_8 f0; gm_t7b7esi3n9r90 f1; } gm_tmvmieab1acso;
_Static_assert(sizeof(gm_tqgtd5gtgj65s) == 40 && _Alignof(gm_tqgtd5gtgj65s) == 8 && offsetof(gm_tqgtd5gtgj65s, f0) == 0 && offsetof(gm_tqgtd5gtgj65s, f1) == 8, "layout of `Unsup!()`");
typedef struct { uint32_t f0; uint8_t f1; uint8_t p2[11]; gm_t7b7esi3n9r90 f2; gm_t7b7esi3n9r90 f3; } gm_tr6ll1dg9imrl;
typedef struct { gm_t7b7esi3n9r90 f0; gm_t7b7esi3n9r90 f1; } gm_tthvfhsvm2g0s;
_Static_assert(sizeof(gm_tu78tssgij60g) == 40 && _Alignof(gm_tu78tssgij60g) == 8 && offsetof(gm_tu78tssgij60g, f0) == 0 && offsetof(gm_tu78tssgij60g, f1) == 8, "layout of `Base`");
typedef struct { gm_t9805qtonbsag f0; uint8_t p1[8]; gm_tllthq21h2f0k f1; gm_b16_8 f2; } gm_t116nudii5c54;
_Static_assert(sizeof(gm_t1ctgp8j1v2as) == 112 && _Alignof(gm_t1ctgp8j1v2as) == 16 && offsetof(gm_t1ctgp8j1v2as, f0) == 0 && offsetof(gm_t1ctgp8j1v2as, f1) == 4 && offsetof(gm_t1ctgp8j1v2as, f2) == 16 && offsetof(gm_t1ctgp8j1v2as, f3) == 64, "layout of `(dst: u32, op: BinOp, a: Value, b: Value)`");
_Static_assert(sizeof(gm_t1s3s9ds8udn4) == 80 && _Alignof(gm_t1s3s9ds8udn4) == 16 && offsetof(gm_t1s3s9ds8udn4, f0) == 0 && offsetof(gm_t1s3s9ds8udn4, f1) == 48 && offsetof(gm_t1s3s9ds8udn4, f2) == 64, "layout of `(cond: Value, then: []Inst, else_: []Inst)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_tlg23kb3oj23s f1; uint8_t f2; uint8_t pe[7]; } gm_t3349g713hmcr;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_tlg23kb3oj23s f1; uint32_t f2; uint8_t pe[4]; } gm_t4j77n1ssj077;
_Static_assert(sizeof(gm_t5hdm76ui2uro) == 64 && _Alignof(gm_t5hdm76ui2uro) == 16 && offsetof(gm_t5hdm76ui2uro, f0) == 0 && offsetof(gm_t5hdm76ui2uro, f1) == 4 && offsetof(gm_t5hdm76ui2uro, f2) == 16, "layout of `(dst: u32, kind: Coercion, v: Value)`");
typedef struct { gm_tlg23kb3oj23s f0; uint8_t p1[8]; gm_t7b7esi3n9r90 f1; } gm_t6fp3q71vjrmk;
typedef struct { gm_t7b7esi3n9r90 f0; gm_tlg23kb3oj23s f1; uint8_t f2; uint8_t pe[7]; } gm_t6j9ipvj0d6k5;
typedef struct { gm_t7pnmbsgd9mnq f0; gm_tf8kfhcnval78 f1; } gm_t7dcjsiqi95h3;
_Static_assert(sizeof(gm_t8rrrcunvsibs) == 128 && _Alignof(gm_t8rrrcunvsibs) == 16 && offsetof(gm_t8rrrcunvsibs, f0) == 0 && offsetof(gm_t8rrrcunvsibs, f1) == 16 && offsetof(gm_t8rrrcunvsibs, f2) == 64 && offsetof(gm_t8rrrcunvsibs, f3) == 80, "layout of `(dst: u32, whole: Value, index: u32, v: Value)`");
_Static_assert(sizeof(gm_t9i04n6j2d1gi) == 64 && _Alignof(gm_t9i04n6j2d1gi) == 16 && offsetof(gm_t9i04n6j2d1gi, f0) == 0 && offsetof(gm_t9i04n6j2d1gi, f1) == 16, "layout of `Sink`");
_Static_assert(sizeof(gm_tbto4k5jg9fve) == 64 && _Alignof(gm_tbto4k5jg9fve) == 16 && offsetof(gm_tbto4k5jg9fve, f0) == 0 && offsetof(gm_tbto4k5jg9fve, f1) == 4 && offsetof(gm_tbto4k5jg9fve, f2) == 16, "layout of `(dst: u32, op: UnOp, a: Value)`");
_Static_assert(sizeof(gm_tcpisgbff1dqv) == 80 && _Alignof(gm_tcpisgbff1dqv) == 16 && offsetof(gm_tcpisgbff1dqv, f0) == 0 && offsetof(gm_tcpisgbff1dqv, f1) == 16 && offsetof(gm_tcpisgbff1dqv, f2) == 64, "layout of `(dst: u32, v: Value, index: u32)`");
_Static_assert(sizeof(gm_tejc6vv5sqno9) == 64 && _Alignof(gm_tejc6vv5sqno9) == 16 && offsetof(gm_tejc6vv5sqno9, f0) == 0 && offsetof(gm_tejc6vv5sqno9, f1) == 48 && offsetof(gm_tejc6vv5sqno9, f2) == 52, "layout of `(cond: Value, negated: bool, label: u32)`");
_Static_assert(sizeof(gm_tf4lk4crue8vt) == 64 && _Alignof(gm_tf4lk4crue8vt) == 16 && offsetof(gm_tf4lk4crue8vt, f0) == 0 && offsetof(gm_tf4lk4crue8vt, f1) == 16, "layout of `(dst: u32, v: Value)`");
_Static_assert(sizeof(gm_tf8kfhcnval78) == 64 && _Alignof(gm_tf8kfhcnval78) == 16 && offsetof(gm_tf8kfhcnval78, f0) == 0 && offsetof(gm_tf8kfhcnval78, f1) == 48, "layout of `?Value`");
typedef struct { uint32_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[11]; gm_tf8kfhcnval78 f2; } gm_tg85uffmq8b38;
_Static_assert(sizeof(gm_tgroihbjsmdvm) == 176 && _Alignof(gm_tgroihbjsmdvm) == 16 && offsetof(gm_tgroihbjsmdvm, f0) == 0 && offsetof(gm_tgroihbjsmdvm, f1) == 16 && offsetof(gm_tgroihbjsmdvm, f2) == 64 && offsetof(gm_tgroihbjsmdvm, f3) == 112 && offsetof(gm_tgroihbjsmdvm, f4) == 160 && offsetof(gm_tgroihbjsmdvm, f5) == 168, "layout of `(dst: u32, ptr: Value, lo: Value, hi: Value, inclusive: bool, size: u64)`");
typedef struct { gm_t7b7esi3n9r90 f0; gm_t7b7esi3n9r90 f1; gm_tf8kfhcnval78 f2; uint8_t f3; uint8_t pe[15]; } gm_thvv7nb91atip;
typedef struct { gm_t9i04n6j2d1gi f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_tf8kfhcnval78 f4; } gm_tihh2qes0hpd4;
_Static_assert(sizeof(gm_tj6rff10m69j7) == 56 && _Alignof(gm_tj6rff10m69j7) == 8 && offsetof(gm_tj6rff10m69j7, f0) == 0 && offsetof(gm_tj6rff10m69j7, f1) == 48, "layout of `?Run`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { uint32_t v0; gm_tf4lk4crue8vt v1; gm_tmvmieab1acso v2; } f1; } gm_tkgci6o4gktbf;
_Static_assert(sizeof(gm_tlg23kb3oj23s) == 56 && _Alignof(gm_tlg23kb3oj23s) == 8 && offsetof(gm_tlg23kb3oj23s, f0) == 0 && offsetof(gm_tlg23kb3oj23s, f1) == 40, "layout of `Place`");
_Static_assert(sizeof(gm_tllthq21h2f0k) == 64 && _Alignof(gm_tllthq21h2f0k) == 16 && offsetof(gm_tllthq21h2f0k, f0) == 0 && offsetof(gm_tllthq21h2f0k, f1) == 16, "layout of `Callee`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_tlg23kb3oj23s f1; } gm_tlpcrb3vu6o2d;
_Static_assert(sizeof(gm_tmvmieab1acso) == 64 && _Alignof(gm_tmvmieab1acso) == 16 && offsetof(gm_tmvmieab1acso, f0) == 0 && offsetof(gm_tmvmieab1acso, f1) == 16, "layout of `(decls: []u32, v: Value)`");
_Static_assert(sizeof(gm_tr6ll1dg9imrl) == 112 && _Alignof(gm_tr6ll1dg9imrl) == 16 && offsetof(gm_tr6ll1dg9imrl, f0) == 0 && offsetof(gm_tr6ll1dg9imrl, f1) == 4 && offsetof(gm_tr6ll1dg9imrl, f2) == 16 && offsetof(gm_tr6ll1dg9imrl, f3) == 64, "layout of `(dst: u32, negated: bool, a: Value, b: Value)`");
_Static_assert(sizeof(gm_tthvfhsvm2g0s) == 96 && _Alignof(gm_tthvfhsvm2g0s) == 16 && offsetof(gm_tthvfhsvm2g0s, f0) == 0 && offsetof(gm_tthvfhsvm2g0s, f1) == 48, "layout of `(index: Value, len: Value)`");
typedef struct { uint32_t f0; uint32_t f1; gm_b16_8 f2; uint8_t p3[8]; gm_tf8kfhcnval78 f3; gm_b16_8 f4; } gm_tvdggnhat0k99;
typedef struct { uint32_t f0; uint32_t f1; uint8_t p2[8]; gm_tf8kfhcnval78 f2; } gm_tvv8lnu6a3ppj;
_Static_assert(sizeof(gm_t116nudii5c54) == 96 && _Alignof(gm_t116nudii5c54) == 16 && offsetof(gm_t116nudii5c54, f0) == 0 && offsetof(gm_t116nudii5c54, f1) == 16 && offsetof(gm_t116nudii5c54, f2) == 80, "layout of `(dst: ?u32, callee: Callee, args: []Value)`");
_Static_assert(sizeof(gm_t3349g713hmcr) == 72 && _Alignof(gm_t3349g713hmcr) == 8 && offsetof(gm_t3349g713hmcr, f0) == 0 && offsetof(gm_t3349g713hmcr, f1) == 8 && offsetof(gm_t3349g713hmcr, f2) == 64, "layout of `(dst: u32, place: Place, result: bool)`");
_Static_assert(sizeof(gm_t4j77n1ssj077) == 72 && _Alignof(gm_t4j77n1ssj077) == 8 && offsetof(gm_t4j77n1ssj077, f0) == 0 && offsetof(gm_t4j77n1ssj077, f1) == 8 && offsetof(gm_t4j77n1ssj077, f2) == 64, "layout of `(dst: u32, place: Place, variant: u32)`");
_Static_assert(sizeof(gm_t6fp3q71vjrmk) == 112 && _Alignof(gm_t6fp3q71vjrmk) == 16 && offsetof(gm_t6fp3q71vjrmk, f0) == 0 && offsetof(gm_t6fp3q71vjrmk, f1) == 64, "layout of `(place: Place, v: Value)`");
_Static_assert(sizeof(gm_t6j9ipvj0d6k5) == 112 && _Alignof(gm_t6j9ipvj0d6k5) == 16 && offsetof(gm_t6j9ipvj0d6k5, f0) == 0 && offsetof(gm_t6j9ipvj0d6k5, f1) == 48 && offsetof(gm_t6j9ipvj0d6k5, f2) == 104, "layout of `(present: Value, place: Place, result: bool)`");
_Static_assert(sizeof(gm_t7dcjsiqi95h3) == 80 && _Alignof(gm_t7dcjsiqi95h3) == 16 && offsetof(gm_t7dcjsiqi95h3, f0) == 0 && offsetof(gm_t7dcjsiqi95h3, f1) == 16, "layout of `(kind: Exit, v: ?Value)`");
_Static_assert(sizeof(gm_tg85uffmq8b38) == 80 && _Alignof(gm_tg85uffmq8b38) == 16 && offsetof(gm_tg85uffmq8b38, f0) == 0 && offsetof(gm_tg85uffmq8b38, f1) == 4 && offsetof(gm_tg85uffmq8b38, f2) == 16, "layout of `(dst: u32, kind: Wrap, v: ?Value)`");
_Static_assert(sizeof(gm_thvv7nb91atip) == 176 && _Alignof(gm_thvv7nb91atip) == 16 && offsetof(gm_thvv7nb91atip, f0) == 0 && offsetof(gm_thvv7nb91atip, f1) == 48 && offsetof(gm_thvv7nb91atip, f2) == 96 && offsetof(gm_thvv7nb91atip, f3) == 160, "layout of `(lo: Value, hi: Value, len: ?Value, inclusive: bool)`");
_Static_assert(sizeof(gm_tihh2qes0hpd4) == 176 && _Alignof(gm_tihh2qes0hpd4) == 16 && offsetof(gm_tihh2qes0hpd4, f0) == 0 && offsetof(gm_tihh2qes0hpd4, f1) == 64 && offsetof(gm_tihh2qes0hpd4, f2) == 80 && offsetof(gm_tihh2qes0hpd4, f3) == 96 && offsetof(gm_tihh2qes0hpd4, f4) == 112, "layout of `(sink: Sink, pieces: []const string, specs: []const FmtSpec, args: []Value, table: ?Value)`");
_Static_assert(sizeof(gm_tkgci6o4gktbf) == 80 && _Alignof(gm_tkgci6o4gktbf) == 16 && offsetof(gm_tkgci6o4gktbf, f0) == 0 && offsetof(gm_tkgci6o4gktbf, f1) == 16, "layout of `Handler`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_tthvfhsvm2g0s v0; gm_thvv7nb91atip v1; gm_t6j9ipvj0d6k5 v2; } f1; } gm_tkj0hg83g38nr;
_Static_assert(sizeof(gm_tlpcrb3vu6o2d) == 64 && _Alignof(gm_tlpcrb3vu6o2d) == 8 && offsetof(gm_tlpcrb3vu6o2d, f0) == 0 && offsetof(gm_tlpcrb3vu6o2d, f1) == 8, "layout of `(dst: u32, place: Place)`");
_Static_assert(sizeof(gm_tvdggnhat0k99) == 112 && _Alignof(gm_tvdggnhat0k99) == 16 && offsetof(gm_tvdggnhat0k99, f0) == 0 && offsetof(gm_tvdggnhat0k99, f1) == 4 && offsetof(gm_tvdggnhat0k99, f2) == 8 && offsetof(gm_tvdggnhat0k99, f3) == 32 && offsetof(gm_tvdggnhat0k99, f4) == 96, "layout of `(label: u32, tir_label: u32, install: []Inst, on: ?Value, body: []Inst)`");
_Static_assert(sizeof(gm_tvv8lnu6a3ppj) == 80 && _Alignof(gm_tvv8lnu6a3ppj) == 16 && offsetof(gm_tvv8lnu6a3ppj, f0) == 0 && offsetof(gm_tvv8lnu6a3ppj, f1) == 4 && offsetof(gm_tvv8lnu6a3ppj, f2) == 16, "layout of `(dst: u32, variant: u32, payload: ?Value)`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_tf4lk4crue8vt v0; gm_tlpcrb3vu6o2d v1; gm_tlpcrb3vu6o2d v2; gm_t1ctgp8j1v2as v3; gm_tbto4k5jg9fve v4; gm_tr6ll1dg9imrl v5; gm_t5hdm76ui2uro v6; gm_te4bkgqfi01mk v7; gm_t116nudii5c54 v8; gm_tiilmfd16v3fc v9; gm_trt0mvq732stt v10; gm_tf4lk4crue8vt v11; gm_tgroihbjsmdvm v12; gm_tihh2qes0hpd4 v13; gm_tvv8lnu6a3ppj v14; gm_tg85uffmq8b38 v15; gm_tilqfqdpj446h v16; gm_tcc1rq4n1avb2 v17; gm_tcc1rq4n1avb2 v18; gm_ttggq7ng4kp3p v19; gm_tr6ll1dg9imrl v20; gm_tcpisgbff1dqv v21; gm_t8rrrcunvsibs v22; gm_t3349g713hmcr v23; gm_t4j77n1ssj077 v24; gm_t6fp3q71vjrmk v25; gm_tkj0hg83g38nr v26; gm_tiilmfd16v3fc v27; gm_tiilmfd16v3fc v28; gm_t1s3s9ds8udn4 v29; uint32_t v30; gm_tejc6vv5sqno9 v31; gm_tf8kfhcnval78 v32; gm_thervm8j23qet v33; gm_t7b7esi3n9r90 v34; gm_tf8kfhcnval78 v35; gm_b16_8 v36; gm_tvdggnhat0k99 v37; gm_tkgci6o4gktbf v38; gm_t7dcjsiqi95h3 v39; gm_tthvfhsvm2g0s v40; gm_t7b7esi3n9r90 v41; gm_t12ib7gg43lh0 v42; } f1; } gm_t8658kcqovtpe;
_Static_assert(sizeof(gm_tkj0hg83g38nr) == 192 && _Alignof(gm_tkj0hg83g38nr) == 16 && offsetof(gm_tkj0hg83g38nr, f0) == 0 && offsetof(gm_tkj0hg83g38nr, f1) == 16, "layout of `Check`");
_Static_assert(sizeof(gm_t8658kcqovtpe) == 208 && _Alignof(gm_t8658kcqovtpe) == 16 && offsetof(gm_t8658kcqovtpe, f0) == 0 && offsetof(gm_t8658kcqovtpe, f1) == 16, "layout of `Op`");
typedef struct { gm_t8658kcqovtpe f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t8g064jsjmhum;
_Static_assert(sizeof(gm_t8g064jsjmhum) == 224 && _Alignof(gm_t8g064jsjmhum) == 16 && offsetof(gm_t8g064jsjmhum, f0) == 0 && offsetof(gm_t8g064jsjmhum, f1) == 208, "layout of `Inst`");
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_93nt26c {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_e8istsh {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qc81hdv {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_end_lines_99v0bhd {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_fn_egshfcm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_5erjuiq {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_a09tbhs {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_eq87866 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hc3p1hh {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_i5sf3rq {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l05am3t {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ti4oun0 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_frame_name_stddmgg {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fresh_5kokjmg {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_has_long_list_75nlnqn {
    gm_frame h;
    gm_t8g064jsjmhum l3; /* i */
    uint8_t ret;
};
struct gm_fr_jump_pvnkvme {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_line_l6u87p9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_j9naoa6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_njoe3l6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_q6e8rn6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_v355qja {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_vepvkou {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_list_6sh7i1h {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_long_body_qgvuu1c {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_outline_code_odk2g51 {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* saved_out */
    gm_tqgtd5gtgj65s l9; /* r */
    gm_tmupb6k0asuti l10; /* run */
    gm_trqfvmjtt4sij l11; /* body */
    gm_trqfvmjtt4sij l14; /* params */
    gm_trqfvmjtt4sij l15; /* args */
    gm_trqfvmjtt4sij l22; /* attrs */
    gm_trqfvmjtt4sij l24; /* text */
    gm_tqgtd5gtgj65s ret;
};
struct gm_fr_outlines_2kvbvit {
    gm_frame h;
    gm_tqh25f25b69k5 l2; /* func */
    uint8_t ret;
};
struct gm_fr_param_cty_e8tvhrm {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_pop_03sdim1 {
    gm_frame h;
    gm_tmupb6k0asuti l2; /* x */
    gm_tj6rff10m69j7 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_849ebnd {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_bs5eajd {
    gm_frame h;
    gm_tmupb6k0asuti l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_kcrf85d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_t83rg2m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_4l3ibqn {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_6dj25eq {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ontu3di {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_size_attrs_abk1l3m {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_weight_nu7ub4o {
    gm_frame h;
    uint32_t l1; /* cap */
    uint32_t l2; /* n */
    uint32_t ret;
};
struct gm_fr_write_2j0ojke {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_cdouvhd {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_i2vvij8 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) uint8_t gm_f_by_ref_fi0o860(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_cty_66ee0kf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_93nt26c(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_e8istsh(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qc81hdv(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_end_lines_99v0bhd(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_egshfcm(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_tag_okd90ep(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t9805qtonbsag a2);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_format_2kt92vg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_5erjuiq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_a09tbhs(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_dq39vq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_dq6oc41(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_eq87866(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_hc3p1hh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_i5sf3rq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_l05am3t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_lo2svpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_t22qvc1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_ti4oun0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_frame_name_stddmgg(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_5kokjmg(void * a0);
__attribute__((aligned(4))) void * gm_f_func_in17r5n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_has_long_list_75nlnqn(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_indent_line_b3mckqa(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_jump_pvnkvme(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tf5srh4mp4b3i a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_line_l6u87p9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_4p6ovir(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t4ce0jd3tdro4 a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_7afcl2l(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_9379f24(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_j9naoa6(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t4ce0jd3tdro4 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_njoe3l6(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_q6e8rn6(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_v355qja(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_vepvkou(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) void gm_f_lir_of_60vujj2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_list_6sh7i1h(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint32_t gm_f_list_weight_ocafiov(gm_b16_8 a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_long_body_qgvuu1c(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_outline_code_odk2g51(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_closure a1);
inline __attribute__((aligned(4))) uint8_t gm_f_outlines_2kvbvit(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_cty_e8tvhrm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_03sdim1(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_849ebnd(void * a0, gm_tqs5p4n2tq19v a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bs5eajd(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kcrf85d(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4l3ibqn(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_6dj25eq(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ontu3di(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_size_attrs_abk1l3m(uint32_t a0);
inline __attribute__((aligned(4))) uint32_t gm_f_weight_nu7ub4o(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2j0ojke(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_cdouvhd(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_i2vvij8(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jog5i62(void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_vq1ifl4(void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
extern uint8_t gm_c6on0097prt[4] __attribute__((aligned(4)));
extern uint8_t gm_c9pef0g9fng[4] __attribute__((aligned(4)));
extern uint8_t gm_ccmgjp6th6r[16] __attribute__((aligned(8)));
extern uint8_t gm_ctq6t8frtor[4] __attribute__((aligned(4)));
extern uint8_t gm_cvpsnk4jni3[4] __attribute__((aligned(4)));
extern const uint8_t gm_s01fuch54uo3k[11];
extern const uint8_t gm_s10iv34fu7qe7[3];
extern const uint8_t gm_s1g2ac815dje3[15];
extern const uint8_t gm_s1s3m1fet79ge[7];
extern const uint8_t gm_s205esok2u8pk[4];
extern const uint8_t gm_s2cuflhqkkuaq[2];
extern const uint8_t gm_s2kvk8bnjk52t[12];
extern const uint8_t gm_s4i05mf7gcfb7[17];
extern const uint8_t gm_s4ql65vkv7h9p[13];
extern const uint8_t gm_s5bohs6opthjm[24];
extern const uint8_t gm_s5k0s6vffccsf[9];
extern const uint8_t gm_s6db4pm3k5olv[3];
extern const uint8_t gm_s84k1uo35omt8[10];
extern const uint8_t gm_s867gr7sqck87[26];
extern const uint8_t gm_sc5aaou69jljp[10];
extern const uint8_t gm_sdi44r3kug6n6[14];
extern const uint8_t gm_sdvlm6i5s2j37[23];
extern const uint8_t gm_secrjke11f3ph[13];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sh16qm7deqse5[7];
extern const uint8_t gm_si731m8b0sr6s[4];
extern const uint8_t gm_siji3oo7ud53t[10];
extern const uint8_t gm_sikpqtja8pqpv[3];
extern const uint8_t gm_sj53lcu5ne531[20];
extern const uint8_t gm_sjl4446pn0dup[3];
extern const uint8_t gm_sk04qg20jmac0[4];
extern const uint8_t gm_slnuijmj5p1vk[19];
extern const uint8_t gm_sloeu62tmfgqn[9];
extern const uint8_t gm_smhvmmegaur89[7];
extern const uint8_t gm_sp1g56u3ibrtp[23];
extern const uint8_t gm_sq9pusqm0kq2f[7];
extern const uint8_t gm_sqtc9fffut4s2[13];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3m73omiakl12[];
extern const char gm_srcht195tfrfuqo[];
extern const char gm_srcmok80ho8hh01[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_ssa0pabeurppq[20];
extern const uint8_t gm_stb0ck7v8hutn[8];
extern const uint8_t gm_std40ghjh6qib[13];
extern const uint8_t gm_stsk5ch7ehebo[5];
extern const uint8_t gm_su20i5iji4ao3[4];
extern const uint8_t gm_suqchf5vh2hqp[5];
extern const uint8_t gm_sura2ofp16u7l[16];
extern const uint8_t gm_suuphrufuvh4m[12];
extern const gm_tmpl gm_tpl3tkekujl97kv;
extern const gm_tmpl gm_tpl6ptbf3l6su1b;
extern const gm_tmpl gm_tpl7pm1mtuteh6j;
extern const gm_tmpl gm_tpl7t589bv3t4us;
extern const gm_tmpl gm_tpl9cnfdsdh5t3p;
extern const gm_tmpl gm_tpla1fscog446dv;
extern const gm_tmpl gm_tplbu8sthucqj1b;
extern const gm_tmpl gm_tpleth0e0pd50mr;
extern const gm_tmpl gm_tplhkjcpttbsqt0;
extern const gm_tmpl gm_tplidcjkf79g8le;
extern const gm_tmpl gm_tplli6rlvq5vnvb;
extern const gm_tmpl gm_tplm01msh3jahhc;
extern const gm_tmpl gm_tplnv6l3ecgreid;
extern const gm_tmpl gm_tplptgaia3s19c9;
extern const gm_tmpl gm_tplsf6r3t44km8l;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_93nt26c(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_93nt26c F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_e8istsh(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_e8istsh F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 2)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qc81hdv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qc81hdv F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_end_lines_99v0bhd(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_end_lines_99v0bhd F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 78u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 1136) + 2));
    if (t4) {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 128);
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)gm_ccmgjp6th6r));
        gm_tnpifv20irnph t8;
        GM_ST(gm_b16_8, ((uint8_t *)&t8 + 0), t7);
        gm_tftb1odc739t5 t9 = gm_f_write_cdouvhd(t6, t8);
        gm_l1 = t9;
        uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
        if (!t10) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l1) + 1, gm_srcmok80ho8hh01, 430, 31);
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_0cs85gs F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_3cp9o8d(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 1);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 1)));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_egshfcm(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_egshfcm F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 49u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint32_t t2 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_weight_nu7ub4o *)((&F.h)->up))->l2));
    uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_weight_nu7ub4o *)((&F.h)->up))->l1));
    uint8_t t4 = (t2 < t3);
    if (t4) {
        uint32_t t5 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_weight_nu7ub4o *)((&F.h)->up))->l2));
        void * t6 = gm_l0;
        uint32_t t7 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_weight_nu7ub4o *)((&F.h)->up))->l1));
        uint32_t t8 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_weight_nu7ub4o *)((&F.h)->up))->l2));
        uint32_t r9; bool o10 = __builtin_sub_overflow(t7, t8, &r9);
        if (o10) gm_trap("integer overflow in `-`", gm_src3m73omiakl12, 56, 37);
        uint32_t t11 = r9;
        uint32_t t12 = gm_f_weight_nu7ub4o(t6, t11);
        uint32_t r13; bool o14 = __builtin_add_overflow(t5, t12, &r13);
        if (o14) gm_trap("integer overflow in `+`", gm_src3m73omiakl12, 56, 22);
        uint32_t t15 = r13;
        GM_ST(uint32_t, ((uint8_t *)&((struct gm_fr_weight_nu7ub4o *)((&F.h)->up))->l2), t15);
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_format_5erjuiq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_5erjuiq F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_stb0ck7v8hutn));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplptgaia3s19c9), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_a09tbhs(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_a09tbhs F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_smhvmmegaur89));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_t22qvc1((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplnv6l3ecgreid), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_eq87866(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_eq87866 F;
    gm_t3877pk7lv748 gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sh16qm7deqse5));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_2kt92vg((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplsf6r3t44km8l), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_hc3p1hh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_hc3p1hh F;
    gm_tommf4j9upomc gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sc5aaou69jljp));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(9));
    gm_tommf4j9upomc t3 = GM_LD(gm_tommf4j9upomc, ((uint8_t *)a0));
    gm_f_format_dq6oc41((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl9cnfdsdh5t3p), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_i5sf3rq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_i5sf3rq F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4i05mf7gcfb7));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(16));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl3tkekujl97kv), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_l05am3t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_l05am3t F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4ce0jd3tdro4 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_su20i5iji4ao3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_lo2svpj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl7t589bv3t4us), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ti4oun0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ti4oun0 F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_ssa0pabeurppq));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(19));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_dq39vq2((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplli6rlvq5vnvb), t2);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_frame_name_stddmgg(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_frame_name_stddmgg F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l2;
    void * t2 = gm_l0;
    uint32_t t3 = gm_l1;
    gm_t9805qtonbsag t4;
    memset(&t4, 0, sizeof t4);
    gm_b16_8 t5 = gm_f_fn_tag_okd90ep((void *)NULL, cx, NULL, t2, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tnpifv20irnph t6;
    GM_ST(gm_b16_8, ((uint8_t *)&t6 + 0), t5);
    gm_f_format_5erjuiq((void *)&gm_l2, (void *)NULL, cx, NULL, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t7) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 8, gm_srcht195tfrfuqo, 169, 60);
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
    F.ret = t9;
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
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_5kokjmg(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fresh_5kokjmg F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK_SHALLOW(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 176));
    uint32_t r5; bool o6 = __builtin_add_overflow(t4, ((uint32_t)1ULL), &r5);
    if (o6) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 465, 5);
    uint32_t t7 = r5;
    GM_ST(uint32_t, (((uint8_t *)t2) + 176), t7);
    void * t8 = gm_l0;
    void * t9 = gm_l0;
    uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)t9) + 304));
    uint32_t r11; bool o12 = __builtin_add_overflow(t10, ((uint32_t)1ULL), &r11);
    if (o12) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 466, 5);
    uint32_t t13 = r11;
    GM_ST(uint32_t, (((uint8_t *)t8) + 304), t13);
    void * t14 = gm_l0;
    uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)t14) + 176));
    F.ret = t15;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_line_l6u87p9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_line_l6u87p9 F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* s */
    GM_STACK_CHECK(&F, 90u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l2;
    gm_tftb1odc739t5 gm_l3;
    void * t2 = gm_l0;
    gm_b0_1 t3 = gm_f_indent_line_b3mckqa((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t4 = gm_l0;
    void * t5 = (void *)(((uint8_t *)t4) + 128);
    gm_b16_8 t6 = gm_l1;
    gm_tftb1odc739t5 t7 = gm_f_extend_0cs85gs(t5, t6);
    gm_l2 = t7;
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srcmok80ho8hh01, 449, 5);
    void * t9 = gm_l0;
    void * t10 = (void *)(((uint8_t *)t9) + 128);
    gm_tftb1odc739t5 t11 = gm_f_push_t83rg2m(t10, ((uint8_t)10ULL));
    gm_l3 = t11;
    uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t12) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srcmok80ho8hh01, 450, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_j9naoa6(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t4ce0jd3tdro4 a1) {
    struct gm_fr_linef_j9naoa6 F;
    void * gm_l0; /* fg */
    gm_t4ce0jd3tdro4 gm_l1; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t4ce0jd3tdro4 t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sloeu62tmfgqn));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(8));
    gm_b0_1 t5 = gm_f_linef_4p6ovir((void *)NULL, cx, NULL, t2, t3, ((void *)&gm_tplm01msh3jahhc), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_njoe3l6(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1) {
    struct gm_fr_linef_njoe3l6 F;
    void * gm_l0; /* fg */
    gm_tnpifv20irnph gm_l1; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tnpifv20irnph t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s5k0s6vffccsf));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(8));
    gm_b0_1 t5 = gm_f_linef_7afcl2l((void *)NULL, cx, NULL, t2, t3, ((void *)&gm_tplbu8sthucqj1b), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_q6e8rn6(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1) {
    struct gm_fr_linef_q6e8rn6 F;
    void * gm_l0; /* fg */
    gm_tnpifv20irnph gm_l1; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tnpifv20irnph t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s1g2ac815dje3));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(14));
    gm_b0_1 t5 = gm_f_linef_9379f24((void *)NULL, cx, NULL, t2, t3, ((void *)&gm_tpl6ptbf3l6su1b), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_v355qja(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1) {
    struct gm_fr_linef_v355qja F;
    void * gm_l0; /* fg */
    gm_tnpifv20irnph gm_l1; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tnpifv20irnph t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_si731m8b0sr6s));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(3));
    gm_b0_1 t5 = gm_f_linef_9379f24((void *)NULL, cx, NULL, t2, t3, ((void *)&gm_tpla1fscog446dv), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_vepvkou(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t4ce0jd3tdro4 a1) {
    struct gm_fr_linef_vepvkou F;
    void * gm_l0; /* fg */
    gm_t4ce0jd3tdro4 gm_l1; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t4ce0jd3tdro4 t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s01fuch54uo3k));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(10));
    gm_b0_1 t5 = gm_f_linef_4p6ovir((void *)NULL, cx, NULL, t2, t3, ((void *)&gm_tpl7pm1mtuteh6j), t4);
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
inline __attribute__((aligned(4))) void gm_f_list_6sh7i1h(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_6sh7i1h F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_qc81hdv();
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
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_epvducg F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_5nn7ph0();
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
inline __attribute__((aligned(4))) uint8_t gm_f_outlines_2kvbvit(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_outlines_2kvbvit F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK(&F, 73u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint32_t t3 = gm_l1;
    uint8_t t4 = gm_f_long_body_qgvuu1c((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t5 = (uint8_t)!t4;
    if (t5) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t6 = gm_l0;
    uint32_t t7 = gm_l1;
    gm_f_lir_of_60vujj2((void *)&F.l2, (void *)NULL, cx, NULL, t6, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 48));
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
    uint8_t t10 = gm_f_has_long_list_75nlnqn(t9);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_cty_e8tvhrm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_param_cty_e8tvhrm F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 133u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 gm_l2;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint8_t t4 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (t4) {
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sqtc9fffut4s2));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(12));
        gm_l2 = t5;
    } else {
        void * t6 = gm_l0;
        gm_t88rcucg1b7uk t7 = gm_l1;
        gm_b16_8 t8 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t8;
    }
    gm_b16_8 t9 = gm_l2;
    F.ret = t9;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_03sdim1(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_03sdim1 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
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
        memset(&(*(gm_tj6rff10m69j7 *)gm_out), 0, sizeof (*(gm_tj6rff10m69j7 *)gm_out));
        goto gm_fdone;
    }
    {
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, ((uint8_t *)t6));
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t6) + 8));
        uint64_t t9 = gm_l1;
        uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
        uint64_t t12 = r10;
        if ((uintptr_t)t12 >= (uintptr_t)(t8)) gm_trap_index((uintptr_t)t12, t8, gm_srcrgc9bkquf6nr, 133, 10);
        memmove(((uint8_t *)&F.l2), (((uint8_t *)t7) + (uintptr_t)t12 * 48), 48);
    }
    void * t13 = gm_l0;
    void * t14 = gm_l0;
    void * t15 = GM_LD(void *, ((uint8_t *)t14));
    uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t14) + 8));
    uint64_t t17 = gm_l1;
    uint64_t r18; bool o19 = __builtin_sub_overflow(t17, ((uint64_t)1ULL), &r18);
    if (o19) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
    uint64_t t20 = r18;
    uint64_t t21 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t22 = (uintptr_t)t20;
    if (t21 > t22 || t22 > (uintptr_t)t16) gm_trap_slice(t21, t22, t16, gm_srcrgc9bkquf6nr, 134, 15);
    uint64_t t23 = ((uintptr_t)t20) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t24;
    GM_ST(void *, (uint8_t *)&t24, (void *)(((uint8_t *)t15 + (uintptr_t)((uint64_t)0ULL) * 48)));
    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(t23));
    GM_ST(gm_b16_8, ((uint8_t *)t13), t24);
    {
        gm_tmupb6k0asuti t25 = GM_LD(gm_tmupb6k0asuti, ((uint8_t *)&F.l2));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tj6rff10m69j7 *)gm_out) + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tj6rff10m69j7 *)gm_out) + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tj6rff10m69j7 *)gm_out) + 52, 0);
        GM_ST(gm_tmupb6k0asuti, ((uint8_t *)&(*(gm_tj6rff10m69j7 *)gm_out)), t25);
        ((uint8_t *)&(*(gm_tj6rff10m69j7 *)gm_out))[48] = 1;
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_849ebnd(void * a0, gm_tqs5p4n2tq19v a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_849ebnd F;
    void * gm_l0; /* l */
    gm_tqs5p4n2tq19v gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_e8istsh();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_4l3ibqn(t8, ((uint64_t)1ULL), t10);
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
    gm_tqs5p4n2tq19v t18 = gm_l1;
    GM_ST(gm_tqs5p4n2tq19v, (((uint8_t *)t16) + (uintptr_t)t17 * 2), t18);
    void * t19 = gm_l0;
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, ((uint8_t *)t20));
    uint64_t t22 = gm_l2;
    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
    uint64_t t25 = r23;
    uint64_t t26 = ((uintptr_t)t25) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t27;
    GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t21 + (uintptr_t)((uint64_t)0ULL) * 2)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bs5eajd(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_bs5eajd F;
    void * gm_l0; /* l */
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
        gm_b16_8 t9 = gm_f_empty_slice_93nt26c();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_ontu3di(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 48), ((uint8_t *)a1), 48);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kcrf85d(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_kcrf85d F;
    void * gm_l0; /* l */
    gm_tj64ui6kruni6 gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_qc81hdv();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_6dj25eq(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_t83rg2m F;
    void * gm_l0; /* l */
    uint8_t gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_ijtp9lo();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_3cp9o8d(t8, ((uint64_t)1ULL), t10);
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
    uint8_t t18 = gm_l1;
    GM_ST(uint8_t, (((uint8_t *)t16) + (uintptr_t)t17 * 1), t18);
    void * t19 = gm_l0;
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, ((uint8_t *)t20));
    uint64_t t22 = gm_l2;
    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
    uint64_t t25 = r23;
    uint64_t t26 = ((uintptr_t)t25) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t27;
    GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t21 + (uintptr_t)((uint64_t)0ULL) * 1)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_3cp9o8d F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)1ULL), ((uint64_t)1ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4l3ibqn(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_4l3ibqn F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)2ULL), ((uint64_t)1ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_6dj25eq(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_6dj25eq F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ontu3di(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ontu3di F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_size_attrs_abk1l3m(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_size_attrs_abk1l3m F;
    uint32_t gm_l0; /* names */
    GM_STACK_CHECK_SHALLOW(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint32_t t2 = gm_l0;
    uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)gm_cvpsnk4jni3));
    uint8_t t4 = (t2 > t3);
    if (t4) {
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sp1g56u3ibrtp));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(22));
        F.ret = t5;
        goto gm_fret;
    }
    uint32_t t6 = gm_l0;
    uint32_t t7 = GM_LD(uint32_t, ((uint8_t *)gm_ctq6t8frtor));
    uint8_t t8 = (t6 > t7);
    if (t8) {
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_suuphrufuvh4m));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(11));
        F.ret = t9;
        goto gm_fret;
    }
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(0));
    F.ret = t10;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_weight_nu7ub4o(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_weight_nu7ub4o F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    F.l2 = ((uint32_t)1ULL);
    void * t2 = gm_l0;
    gm_closure t3 = ((gm_closure){(void *)gm_f_fn_egshfcm, (void *)&F.h, cx});
    gm_f_for_each_child_fm49s73(t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint32_t t4 = F.l2;
    uint32_t t5 = F.l1;
    uint32_t t6 = (t4 < t5 ? t4 : t5);
    F.ret = t6;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2j0ojke(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_2j0ojke F;
    void * gm_l0; /* l */
    gm_tnpifv20irnph gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tnpifv20irnph t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s867gr7sqck87));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(25));
    gm_tftb1odc739t5 t5 = gm_f_write_vq1ifl4(t2, t3, ((void *)&gm_tplidcjkf79g8le), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_cdouvhd(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_cdouvhd F;
    void * gm_l0; /* l */
    gm_tnpifv20irnph gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tnpifv20irnph t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sk04qg20jmac0));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(3));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplhkjcpttbsqt0), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_i2vvij8(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_i2vvij8 F;
    void * gm_l0; /* l */
    gm_tnpifv20irnph gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tnpifv20irnph t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_stsk5ch7ehebo));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(4));
    gm_tftb1odc739t5 t5 = gm_f_write_vq1ifl4(t2, t3, ((void *)&gm_tpleth0e0pd50mr), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_egshfcm(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_has_long_list_75nlnqn(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_long_list_75nlnqn F;
    gm_b16_8 gm_l0; /* insts */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    gm_b16_8 gm_l4; /* b */
    gm_b16_8 gm_l5; /* b */
    gm_b16_8 gm_l6; /* b */
    gm_b16_8 gm_l7; /* x */
    gm_b16_8 gm_l8; /* y */
    gm_b16_8 gm_l9; /* x */
    gm_b16_8 gm_l10; /* y */
    GM_STACK_CHECK(&F, 492u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l11;
    {
        {
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t21 = (t20 > ((uint64_t)1ULL));
            if (!t21) {
                goto gm_lb2;
            }
            gm_b16_8 t22 = gm_l0;
            uint32_t t23 = GM_LD(uint32_t, ((uint8_t *)gm_c6on0097prt));
            uint32_t r24; bool o25 = __builtin_add_overflow(t23, ((uint32_t)1ULL), &r24);
            if (o25) gm_trap("integer overflow in `+`", gm_src3m73omiakl12, 94, 45);
            uint32_t t26 = r24;
            uint32_t t27 = gm_f_list_weight_ocafiov(t22, t26);
            uint32_t t28 = GM_LD(uint32_t, ((uint8_t *)gm_c6on0097prt));
            uint8_t t29 = (t27 > t28);
            if (!t29) {
                goto gm_lb2;
            }
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_b16_8 t30 = gm_l0;
    gm_l1 = t30;
    gm_l2 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t31 = gm_l2;
            uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t33 = (t31 < t32);
            if (!t33) {
                goto gm_lb3;
            }
            {
                void * t34 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t36 = gm_l2;
                {
                    {
                        memmove(((uint8_t *)&F.l3), (((uint8_t *)t34) + (uintptr_t)t36 * 224), 224);
                        {
                            {
                                {
                                    {
                                        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                                        uint8_t t38 = (uint8_t)(t37 == ((uint8_t)27ULL));
                                        if (!t38) {
                                            goto gm_lb11;
                                        }
                                        gm_b16_8 t39 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 16) + 8));
                                        gm_l4 = t39;
                                        goto gm_lb10;
                                    }
gm_lb11:;
                                    {
                                        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                                        uint8_t t41 = (uint8_t)(t40 == ((uint8_t)28ULL));
                                        if (!t41) {
                                            goto gm_lb12;
                                        }
                                        gm_b16_8 t42 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 16) + 8));
                                        gm_l4 = t42;
                                        goto gm_lb10;
                                    }
gm_lb12:;
                                    {
                                        uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                                        uint8_t t44 = (uint8_t)(t43 == ((uint8_t)36ULL));
                                        if (!t44) {
                                            goto gm_lb13;
                                        }
                                        gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
                                        gm_l4 = t45;
                                        goto gm_lb10;
                                    }
gm_lb13:;
                                    goto gm_lb9;
                                }
gm_lb10:;
                                gm_b16_8 t46 = gm_l4;
                                gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
                                uint8_t t48 = gm_f_has_long_list_75nlnqn(t47);
                                if (t48) {
                                    F.ret = ((uint8_t)1);
                                    goto gm_fret;
                                }
                                goto gm_lb8;
                            }
gm_lb9:;
                            {
                                {
                                    {
                                        uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                                        uint8_t t50 = (uint8_t)(t49 == ((uint8_t)29ULL));
                                        if (!t50) {
                                            goto gm_lb16;
                                        }
                                        gm_b16_8 t51 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 16) + 48));
                                        gm_l7 = t51;
                                        gm_b16_8 t52 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 16) + 64));
                                        gm_l8 = t52;
                                        goto gm_lb15;
                                    }
gm_lb16:;
                                    {
                                        uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                                        uint8_t t54 = (uint8_t)(t53 == ((uint8_t)37ULL));
                                        if (!t54) {
                                            goto gm_lb17;
                                        }
                                        gm_b16_8 t55 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 16) + 8));
                                        gm_l7 = t55;
                                        gm_b16_8 t56 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 16) + 96));
                                        gm_l8 = t56;
                                        goto gm_lb15;
                                    }
gm_lb17:;
                                    goto gm_lb14;
                                }
gm_lb15:;
                                gm_b16_8 t57 = gm_l7;
                                gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
                                uint8_t t59 = gm_f_has_long_list_75nlnqn(t58);
                                if (t59) {
                                    gm_l11 = ((uint8_t)1);
                                } else {
                                    gm_b16_8 t60 = gm_l8;
                                    gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
                                    uint8_t t62 = gm_f_has_long_list_75nlnqn(t61);
                                    gm_l11 = t62;
                                }
                                uint8_t t63 = gm_l11;
                                if (t63) {
                                    F.ret = ((uint8_t)1);
                                    goto gm_fret;
                                }
                                goto gm_lb8;
                            }
gm_lb14:;
                            {
                                goto gm_lb8;
                            }
gm_lb18:;
                            gm_trap("no match arm matched the scrutinee", gm_src3m73omiakl12, 96, 9);
                        }
gm_lb8:;
                        goto gm_lb6;
                    }
gm_lb7:;
                    gm_trap("no match arm matched the scrutinee", gm_src3m73omiakl12, 95, 5);
                }
gm_lb6:;
            }
gm_lb5:;
            uint64_t t64 = gm_l2;
            uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
            uint64_t t67 = r65;
            gm_l2 = t67;
        }
    }
gm_lb3:;
    F.ret = ((uint8_t)0);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_jump_pvnkvme(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tf5srh4mp4b3i a2) {
    struct gm_fr_jump_pvnkvme F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* label */
    gm_tf5srh4mp4b3i gm_l2; /* scope */
    void * gm_l3; /* run */
    uint8_t gm_l4; /* outside */
    uint64_t gm_l5; /* i */
    uint64_t gm_l6; /* k */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* j */
    GM_STACK_CHECK(&F, 525u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l10;
    void * t9 = gm_l0;
    uint64_t t10 = GM_LD(uint64_t, ((((uint8_t *)t9) + 312) + 8));
    uint8_t t11 = (t10 > ((uint64_t)0ULL));
    if (t11) {
        void * t12 = gm_l0;
        void * t13 = GM_LD(void *, (((uint8_t *)t12) + 312));
        uint64_t t14 = GM_LD(uint64_t, ((((uint8_t *)t12) + 312) + 8));
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uint64_t, ((((uint8_t *)t15) + 312) + 8));
        uint64_t r17; bool o18 = __builtin_sub_overflow(t16, ((uint64_t)1ULL), &r17);
        if (o18) gm_trap("integer overflow in `-`", gm_src3m73omiakl12, 65, 31);
        uint64_t t19 = r17;
        if ((uintptr_t)t19 >= (uintptr_t)(t14)) gm_trap_index((uintptr_t)t19, t14, gm_src3m73omiakl12, 65, 17);
        void * t20 = (void *)(((uint8_t *)t13) + (uintptr_t)t19 * 48);
        gm_l3 = t20;
        gm_l4 = ((uint8_t)1);
        {
            {
                uint8_t t21 = (uint8_t)((((uint8_t *)&gm_l2))[8] != 0);
                if (!t21) {
                    goto gm_lb2;
                }
                uint64_t t22 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
                gm_l5 = t22;
                uint64_t t23 = gm_l5;
                void * t24 = gm_l3;
                uint64_t t25 = GM_LD(uint64_t, ((uint8_t *)t24));
                uint8_t t26 = (t23 < t25);
                gm_l4 = t26;
                goto gm_lb1;
            }
gm_lb2:;
        }
gm_lb1:;
        uint8_t t27 = gm_l4;
        if (t27) {
            void * t28 = gm_l3;
            uint64_t t29 = GM_LD(uint64_t, ((((uint8_t *)t28) + 8) + 8));
            gm_l6 = t29;
            gm_l8 = ((uint64_t)0ULL);
            void * t30 = gm_l3;
            uint64_t t31 = GM_LD(uint64_t, ((((uint8_t *)t30) + 8) + 8));
            gm_l7 = t31;
            {
                for (;;) {
                    uint64_t t32 = gm_l8;
                    uint64_t t33 = gm_l7;
                    uint8_t t34 = (t32 < t33);
                    if (!t34) {
                        goto gm_lb3;
                    }
                    {
                        {
                            {
                                uint64_t t35 = gm_l8;
                                gm_l9 = t35;
                                void * t36 = gm_l3;
                                void * t37 = GM_LD(void *, (((uint8_t *)t36) + 8));
                                uint64_t t38 = GM_LD(uint64_t, ((((uint8_t *)t36) + 8) + 8));
                                uint64_t t39 = gm_l9;
                                if ((uintptr_t)t39 >= (uintptr_t)(t38)) gm_trap_index((uintptr_t)t39, t38, gm_src3m73omiakl12, 71, 20);
                                gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)t37) + (uintptr_t)t39 * 32));
                                gm_b16_8 t41 = gm_l1;
                                uint8_t t42 = (gm_eq0iqiubfm7e(((uint8_t *)&t40), ((uint8_t *)&t41)));
                                if (t42) {
                                    uint64_t t43 = gm_l9;
                                    gm_l6 = t43;
                                    goto gm_lb3;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_src3m73omiakl12, 70, 13);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t44 = gm_l8;
                    uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                    uint64_t t47 = r45;
                    gm_l8 = t47;
                }
            }
gm_lb3:;
            uint64_t t48 = gm_l6;
            void * t49 = gm_l3;
            uint64_t t50 = GM_LD(uint64_t, ((((uint8_t *)t49) + 8) + 8));
            uint8_t t51 = ((t48 == t50));
            if (t51) {
                void * t52 = gm_l3;
                void * t53 = (void *)(((uint8_t *)t52) + 8);
                gm_b16_8 t54 = gm_l1;
                gm_tf5srh4mp4b3i t55 = gm_l2;
                gm_tj64ui6kruni6 t56;
                GM_ST(gm_b16_8, ((uint8_t *)&t56 + 0), t54);
                GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t56 + 16), t55);
                gm_tftb1odc739t5 t57 = gm_f_push_kcrf85d(t53, &t56);
                gm_l10 = t57;
                uint8_t t58 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                if (!t58) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_src3m73omiakl12, 76, 43);
            }
            void * t59 = gm_l0;
            uint64_t t60 = gm_l6;
            uint64_t r61; bool o62 = __builtin_add_overflow(t60, ((uint64_t)1ULL), &r61);
            if (o62) gm_trap("integer overflow in `+`", gm_src3m73omiakl12, 77, 36);
            uint64_t t63 = r61;
            gm_t4ce0jd3tdro4 t64;
            GM_ST(uint64_t, ((uint8_t *)&t64 + 0), t63);
            gm_b0_1 t65 = gm_f_linef_vepvkou((void *)NULL, cx, NULL, t59, t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
    }
    void * t66 = gm_l0;
    gm_b16_8 t67 = gm_l1;
    gm_tnpifv20irnph t68;
    GM_ST(gm_b16_8, ((uint8_t *)&t68 + 0), t67);
    gm_b0_1 t69 = gm_f_linef_njoe3l6((void *)NULL, cx, NULL, t66, t68);
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
inline __attribute__((aligned(4))) uint8_t gm_f_long_body_qgvuu1c(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_long_body_qgvuu1c F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    uint64_t gm_l2; /* i */
    uint8_t gm_l3; /* known */
    uint8_t gm_l4; /* long */
    void * gm_l5; /* b */
    GM_STACK_CHECK(&F, 225u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l6;
    uint32_t t9 = gm_l1;
    uint64_t t10 = (uint64_t)t9;
    gm_l2 = t10;
    {
        for (;;) {
            void * t11 = gm_l0;
            uint64_t t12 = GM_LD(uint64_t, ((((uint8_t *)t11) + 3000) + 8));
            uint64_t t13 = gm_l2;
            uint8_t t14 = (t12 <= t13);
            if (!t14) {
                goto gm_lb1;
            }
            {
                void * t15 = gm_l0;
                void * t16 = (void *)(((uint8_t *)t15) + 3000);
                gm_tqs5p4n2tq19v t17;
                memset(&t17, 0, sizeof t17);
                gm_tftb1odc739t5 t18 = gm_f_push_849ebnd(t16, t17);
                gm_l6 = t18;
                uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_src3m73omiakl12, 44, 40);
            }
gm_lb3:;
        }
    }
gm_lb1:;
    {
        {
            void * t20 = gm_l0;
            void * t21 = GM_LD(void *, (((uint8_t *)t20) + 3000));
            uint64_t t22 = GM_LD(uint64_t, ((((uint8_t *)t20) + 3000) + 8));
            uint64_t t23 = gm_l2;
            if ((uintptr_t)t23 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t23, t22, gm_src3m73omiakl12, 45, 17);
            uint8_t t24 = (uint8_t)(((((uint8_t *)t21) + (uintptr_t)t23 * 2))[1] != 0);
            if (!t24) {
                goto gm_lb5;
            }
            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)t21) + (uintptr_t)t23 * 2));
            gm_l3 = t25;
            uint8_t t26 = gm_l3;
            F.ret = t26;
            goto gm_fret;
        }
gm_lb5:;
    }
gm_lb4:;
    gm_l4 = ((uint8_t)0);
    {
        {
            void * t27 = gm_l0;
            void * t28 = GM_LD(void *, ((uint8_t *)t27));
            uint32_t t29 = gm_l1;
            void * t30 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t28, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t31 = (uint8_t)(GM_LD(void *, (((uint8_t *)t30) + 152)) != 0);
            if (!t31) {
                goto gm_lb7;
            }
            void * t32 = GM_LD(void *, (((uint8_t *)t30) + 152));
            gm_l5 = t32;
            void * t33 = gm_l5;
            uint32_t t34 = GM_LD(uint32_t, ((uint8_t *)gm_c9pef0g9fng));
            uint32_t r35; bool o36 = __builtin_add_overflow(t34, ((uint32_t)1ULL), &r35);
            if (o36) gm_trap("integer overflow in `+`", gm_src3m73omiakl12, 47, 52);
            uint32_t t37 = r35;
            uint32_t t38 = gm_f_weight_nu7ub4o(t33, t37);
            uint32_t t39 = GM_LD(uint32_t, ((uint8_t *)gm_c9pef0g9fng));
            uint8_t t40 = (t38 > t39);
            gm_l4 = t40;
            goto gm_lb6;
        }
gm_lb7:;
    }
gm_lb6:;
    void * t41 = gm_l0;
    void * t42 = GM_LD(void *, (((uint8_t *)t41) + 3000));
    uint64_t t43 = GM_LD(uint64_t, ((((uint8_t *)t41) + 3000) + 8));
    uint64_t t44 = gm_l2;
    if ((uintptr_t)t44 >= (uintptr_t)(t43)) gm_trap_index((uintptr_t)t44, t43, gm_src3m73omiakl12, 48, 5);
    uint8_t t45 = gm_l4;
    gm_tqs5p4n2tq19v t46;
    GM_ST(uint8_t, ((uint8_t *)&t46), t45);
    ((uint8_t *)&t46)[1] = 1;
    GM_ST(gm_tqs5p4n2tq19v, (((uint8_t *)t42) + (uintptr_t)t44 * 2), t46);
    uint8_t t47 = gm_l4;
    F.ret = t47;
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
__attribute__((aligned(4))) void gm_f_outline_code_odk2g51(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_closure a1) {
    struct gm_fr_outline_code_odk2g51 F;
    void * gm_l0; /* fg */
    gm_closure gm_l1; /* emit */
    uint32_t gm_l2; /* k */
    gm_b16_8 gm_l3; /* name */
    gm_b16_8 gm_l4; /* outer_cx */
    uint8_t gm_l5; /* own_cx */
    uint64_t gm_l7; /* saved_indent */
    uint32_t gm_l8; /* saved_names */
    uint32_t gm_l12; /* names */
    uint32_t gm_l13; /* fid */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* i */
    uint32_t gm_l19; /* p */
    gm_t88rcucg1b7uk gm_l20; /* t */
    uint8_t gm_l21; /* debug */
    gm_b16_8 gm_l23; /* head */
    gm_b16_8 gm_l25; /* call */
    gm_b16_8 gm_l26; /* <tmp> */
    uint64_t gm_l27; /* <tmp> */
    uint64_t gm_l28; /* j */
    gm_tj64ui6kruni6 gm_l29; /* x */
    GM_STACK_CHECK(&F, 3447u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l30;
    gm_tftb1odc739t5 gm_l31;
    gm_tj6rff10m69j7 gm_l32;
    gm_tabb710d9mq2e gm_l33;
    gm_tftb1odc739t5 gm_l34;
    gm_tftb1odc739t5 gm_l35;
    gm_tftb1odc739t5 gm_l36;
    gm_tftb1odc739t5 gm_l37;
    gm_tftb1odc739t5 gm_l38;
    gm_tftb1odc739t5 gm_l39;
    gm_tftb1odc739t5 gm_l40;
    gm_tftb1odc739t5 gm_l41;
    gm_tftb1odc739t5 gm_l42;
    gm_tftb1odc739t5 gm_l43;
    gm_tn68el8d14vku gm_l44;
    gm_tabb710d9mq2e gm_l45;
    gm_tftb1odc739t5 gm_l46;
    gm_tabb710d9mq2e gm_l47;
    gm_tftb1odc739t5 gm_l48;
    gm_tftb1odc739t5 gm_l49;
    gm_tftb1odc739t5 gm_l50;
    gm_tftb1odc739t5 gm_l51;
    gm_tftb1odc739t5 gm_l52;
    gm_tftb1odc739t5 gm_l53;
    gm_tftb1odc739t5 gm_l54;
    gm_tftb1odc739t5 gm_l55;
    gm_tabb710d9mq2e gm_l56;
    gm_tabb710d9mq2e gm_l57;
    gm_tftb1odc739t5 gm_l58;
    gm_tftb1odc739t5 gm_l59;
    gm_tftb1odc739t5 gm_l60;
    gm_tftb1odc739t5 gm_l61;
    gm_tftb1odc739t5 gm_l62;
    gm_tftb1odc739t5 gm_l63;
    gm_tftb1odc739t5 gm_l64;
    gm_tftb1odc739t5 gm_l65;
    gm_tftb1odc739t5 gm_l66;
    gm_tabb710d9mq2e gm_l67;
    gm_tabb710d9mq2e gm_l68;
    gm_t19us6e77elqj gm_l69;
    {
        void * t12 = gm_l0;
        uint32_t t13 = gm_f_fresh_5kokjmg(t12);
        gm_l2 = t13;
        void * t14 = gm_l0;
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)t14) + 16));
        uint32_t t16 = gm_l2;
        gm_tommf4j9upomc t17;
        GM_ST(uint32_t, (uint8_t *)&t17 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t17 + 0), t15);
        GM_ST(uint32_t, ((uint8_t *)&t17 + 16), t16);
        gm_f_format_hc3p1hh((void *)&gm_l30, (void *)NULL, cx, NULL, &t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
        if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 8, gm_src3m73omiakl12, 108, 13);
        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 8));
        gm_l3 = t19;
        void * t20 = gm_l0;
        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)t20) + 224));
        gm_l4 = t21;
        gm_b16_8 t22 = gm_l4;
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(gm_s10iv34fu7qe7));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(2));
        uint8_t t24 = (gm_eq0iqiubfm7e(((uint8_t *)&t22), ((uint8_t *)&t23)));
        gm_l5 = t24;
        void * t25 = gm_l0;
        memmove(((uint8_t *)&F.l6), (((uint8_t *)t25) + 128), 40);
        void * t26 = gm_l0;
        gm_trqfvmjtt4sij t27;
        gm_f_list_uvfu1k1((void *)&t27, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t26) + 128), t27);
        void * t28 = gm_l0;
        uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)t28) + 168));
        gm_l7 = t29;
        void * t30 = gm_l0;
        GM_ST(uint64_t, (((uint8_t *)t30) + 168), ((uint64_t)1ULL));
        void * t31 = gm_l0;
        uint32_t t32 = GM_LD(uint32_t, (((uint8_t *)t31) + 304));
        gm_l8 = t32;
        void * t33 = gm_l0;
        GM_ST(uint32_t, (((uint8_t *)t33) + 304), ((uint32_t)0ULL));
        uint8_t t34 = gm_l5;
        uint8_t t35 = (uint8_t)!t34;
        if (t35) {
            void * t36 = gm_l0;
            gm_b16_8 t37;
            GM_ST(void *, (uint8_t *)&t37, (void *)(gm_s1s3m1fet79ge));
            GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(6));
            GM_ST(gm_b16_8, (((uint8_t *)t36) + 224), t37);
        }
        void * t38 = gm_l0;
        void * t39 = (void *)(((uint8_t *)t38) + 312);
        void * t40 = gm_l0;
        uint64_t t41 = GM_LD(uint64_t, ((((uint8_t *)t40) + 184) + 8));
        gm_trqfvmjtt4sij t42;
        gm_f_list_6sh7i1h((void *)&t42, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tmupb6k0asuti t43;
        GM_ST(uint64_t, ((uint8_t *)&t43 + 0), t41);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t43 + 8), t42);
        gm_tftb1odc739t5 t44 = gm_f_push_bs5eajd(t39, &t43);
        gm_l31 = t44;
        uint8_t t45 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
        if (!t45) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 1, gm_src3m73omiakl12, 118, 5);
        gm_closure t46 = gm_l1;
        gm_closure t47 = t46;
        gm_node *ccx48 = t47.cx;
        gm_node cn49; { gm_node *f = gm_find(cx, 587034433); if (f) { cn49 = *f; cn49.next = ccx48; ccx48 = &cn49; } }
        gm_node cn50; { gm_node *f = gm_find(cx, 2126215853); if (f) { cn50 = *f; cn50.next = ccx48; ccx48 = &cn50; } }
        void * t51 = t47.code;
        gm_tqgtd5gtgj65s t52;
        if (GM_PTR_TAGGED(t51)) {
            t52 = ((gm_tqgtd5gtgj65s (*)(void))GM_PTR_UNTAG(t51))();
        } else {
            ((void (*)(void *, void *, gm_node *, gm_node *))t51)((void *)&t52, t47.env, ccx48, NULL);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l9 = t52;
        void * t53 = gm_l0;
        gm_b0_1 t54 = gm_f_end_lines_99v0bhd((void *)NULL, cx, NULL, t53);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t55 = gm_l0;
        void * t56 = (void *)(((uint8_t *)t55) + 312);
        gm_f_pop_03sdim1((void *)&gm_l32, t56);
        uint8_t t57 = (uint8_t)((((uint8_t *)&gm_l32))[48] != 0);
        if (!t57) gm_trap("unwrap of null", gm_src3m73omiakl12, 121, 12);
        memmove(((uint8_t *)&F.l10), ((uint8_t *)&gm_l32), 48);
    }
    {
        void * t58 = gm_l0;
        memmove(((uint8_t *)&F.l11), (((uint8_t *)t58) + 128), 40);
        void * t59 = gm_l0;
        memmove((((uint8_t *)t59) + 128), ((uint8_t *)&F.l6), 40);
        void * t60 = gm_l0;
        uint32_t t61 = GM_LD(uint32_t, (((uint8_t *)t60) + 304));
        gm_l12 = t61;
        void * t62 = gm_l0;
        uint32_t t63 = gm_l8;
        GM_ST(uint32_t, (((uint8_t *)t62) + 304), t63);
        void * t64 = gm_l0;
        uint64_t t65 = gm_l7;
        GM_ST(uint64_t, (((uint8_t *)t64) + 168), t65);
        void * t66 = gm_l0;
        gm_b16_8 t67 = gm_l4;
        GM_ST(gm_b16_8, (((uint8_t *)t66) + 224), t67);
        uint8_t t68 = (uint8_t)!((((uint8_t *)&F.l9))[0] != 0);
        if (t68) {
        } else {
            gm_t08olq97rscn3 t69 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&F.l9) + 8));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8), t69);
            goto gm_fdone;
        }
        void * t70 = gm_l0;
        uint32_t t71 = GM_LD(uint32_t, (((uint8_t *)t70) + 8));
        gm_l13 = t71;
        gm_trqfvmjtt4sij t72;
        gm_f_list_epvducg((void *)&t72, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l14 = t72;
        void * t73 = (void *)((uint8_t *)&F.l14);
        void * t74 = gm_l0;
        void * t75 = GM_LD(void *, ((uint8_t *)t74));
        uint32_t t76 = gm_l13;
        gm_b16_8 t77 = gm_f_frame_name_stddmgg((void *)NULL, cx, NULL, t75, t76);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t78;
        GM_ST(gm_b16_8, ((uint8_t *)&t78 + 0), t77);
        gm_f_format_i5sf3rq((void *)&gm_l33, (void *)NULL, cx, NULL, t78);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t79 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
        if (!t79) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 8, gm_src3m73omiakl12, 132, 17);
        gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l33) + 8));
        gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&t80));
        gm_tftb1odc739t5 t82 = gm_f_push_3ef746b(t73, t81);
        gm_l34 = t82;
        uint8_t t83 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
        if (!t83) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 1, gm_src3m73omiakl12, 132, 5);
        void * t84 = (void *)((uint8_t *)&F.l14);
        gm_b16_8 t85;
        GM_ST(void *, (uint8_t *)&t85, (void *)(gm_siji3oo7ud53t));
        GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(9));
        gm_tftb1odc739t5 t86 = gm_f_push_3ef746b(t84, t85);
        gm_l35 = t86;
        uint8_t t87 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
        if (!t87) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 1, gm_src3m73omiakl12, 133, 5);
        void * t88 = (void *)((uint8_t *)&F.l14);
        gm_b16_8 t89;
        GM_ST(void *, (uint8_t *)&t89, (void *)(gm_s2kvk8bnjk52t));
        GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(11));
        gm_tftb1odc739t5 t90 = gm_f_push_3ef746b(t88, t89);
        gm_l36 = t90;
        uint8_t t91 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
        if (!t91) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 1, gm_src3m73omiakl12, 134, 5);
        void * t92 = (void *)((uint8_t *)&F.l14);
        gm_b16_8 t93;
        GM_ST(void *, (uint8_t *)&t93, (void *)(gm_sdi44r3kug6n6));
        GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(13));
        gm_tftb1odc739t5 t94 = gm_f_push_3ef746b(t92, t93);
        gm_l37 = t94;
        uint8_t t95 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
        if (!t95) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 1, gm_src3m73omiakl12, 135, 5);
        gm_trqfvmjtt4sij t96;
        gm_f_list_epvducg((void *)&t96, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l15 = t96;
    }
    {
        void * t97 = (void *)((uint8_t *)&F.l15);
        gm_b16_8 t98;
        GM_ST(void *, (uint8_t *)&t98, (void *)(gm_sjl4446pn0dup));
        GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(2));
        gm_tftb1odc739t5 t99 = gm_f_push_3ef746b(t97, t98);
        gm_l38 = t99;
        uint8_t t100 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
        if (!t100) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 1, gm_src3m73omiakl12, 137, 5);
        void * t101 = (void *)((uint8_t *)&F.l15);
        gm_b16_8 t102;
        GM_ST(void *, (uint8_t *)&t102, (void *)(gm_s205esok2u8pk));
        GM_ST(uintptr_t, (uint8_t *)&t102 + 8, (uintptr_t)(3));
        gm_tftb1odc739t5 t103 = gm_f_push_3ef746b(t101, t102);
        gm_l39 = t103;
        uint8_t t104 = (uint8_t)!((((uint8_t *)&gm_l39))[0] != 0);
        if (!t104) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l39) + 1, gm_src3m73omiakl12, 138, 5);
        void * t105 = (void *)((uint8_t *)&F.l15);
        gm_b16_8 t106;
        GM_ST(void *, (uint8_t *)&t106, (void *)(gm_s10iv34fu7qe7));
        GM_ST(uintptr_t, (uint8_t *)&t106 + 8, (uintptr_t)(2));
        gm_tftb1odc739t5 t107 = gm_f_push_3ef746b(t105, t106);
        gm_l40 = t107;
        uint8_t t108 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
        if (!t108) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l40) + 1, gm_src3m73omiakl12, 139, 5);
        void * t109 = (void *)((uint8_t *)&F.l15);
        gm_b16_8 t110;
        GM_ST(void *, (uint8_t *)&t110, (void *)(gm_suqchf5vh2hqp));
        GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(4));
        gm_tftb1odc739t5 t111 = gm_f_push_3ef746b(t109, t110);
        gm_l41 = t111;
        uint8_t t112 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
        if (!t112) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 1, gm_src3m73omiakl12, 140, 5);
        void * t113 = gm_l0;
        uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)t113) + 242));
        uint8_t t115 = (uint8_t)!t114;
        if (t115) {
            void * t116 = gm_l0;
            void * t117 = GM_LD(void *, ((uint8_t *)t116));
            void * t118 = (void *)(((uint8_t *)t117) + 8);
            void * t119 = gm_l0;
            void * t120 = GM_LD(void *, (((uint8_t *)t119) + 120));
            gm_t88rcucg1b7uk t121 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t120) + 64));
            uint8_t t122 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t118, t121);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (t122) {
                void * t123 = (void *)((uint8_t *)&F.l14);
                gm_b16_8 t124;
                GM_ST(void *, (uint8_t *)&t124, (void *)(gm_s4ql65vkv7h9p));
                GM_ST(uintptr_t, (uint8_t *)&t124 + 8, (uintptr_t)(12));
                gm_tftb1odc739t5 t125 = gm_f_push_3ef746b(t123, t124);
                gm_l42 = t125;
                uint8_t t126 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
                if (!t126) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l42) + 1, gm_src3m73omiakl12, 144, 13);
                void * t127 = (void *)((uint8_t *)&F.l15);
                gm_b16_8 t128;
                GM_ST(void *, (uint8_t *)&t128, (void *)(gm_sq9pusqm0kq2f));
                GM_ST(uintptr_t, (uint8_t *)&t128 + 8, (uintptr_t)(6));
                gm_tftb1odc739t5 t129 = gm_f_push_3ef746b(t127, t128);
                gm_l43 = t129;
                uint8_t t130 = (uint8_t)!((((uint8_t *)&gm_l43))[0] != 0);
                if (!t130) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l43) + 1, gm_src3m73omiakl12, 145, 13);
            }
            void * t131 = gm_l0;
            void * t132 = GM_LD(void *, (((uint8_t *)t131) + 120));
            gm_b16_8 t133 = GM_LD(gm_b16_8, (((uint8_t *)t132) + 32));
            gm_l16 = t133;
            gm_l17 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t134 = gm_l17;
                    uint64_t t135 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                    uint8_t t136 = (t134 < t135);
                    if (!t136) {
                        goto gm_lb1;
                    }
                    {
                        uint64_t t137 = gm_l17;
                        void * t138 = GM_LD(void *, ((uint8_t *)&gm_l16));
                        uint64_t t139 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                        uint64_t t140 = gm_l17;
                        uint32_t t141 = GM_LD(uint32_t, (((uint8_t *)t138) + (uintptr_t)t140 * 4));
                        gm_tn68el8d14vku t142;
                        GM_ST(uint32_t, (uint8_t *)&t142 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t142 + 0), t137);
                        GM_ST(uint32_t, ((uint8_t *)&t142 + 8), t141);
                        gm_l44 = t142;
                        {
                            {
                                uint64_t t143 = GM_LD(uint64_t, ((uint8_t *)&gm_l44));
                                gm_l18 = t143;
                                uint32_t t144 = GM_LD(uint32_t, (((uint8_t *)&gm_l44) + 8));
                                gm_l19 = t144;
                                void * t145 = gm_l0;
                                void * t146 = GM_LD(void *, (((uint8_t *)t145) + 272));
                                uint64_t t147 = GM_LD(uint64_t, ((((uint8_t *)t145) + 272) + 8));
                                uint64_t t148 = gm_l18;
                                if ((uintptr_t)t148 >= (uintptr_t)(t147)) gm_trap_index((uintptr_t)t148, t147, gm_src3m73omiakl12, 148, 16);
                                uint8_t t149 = GM_LD(uint8_t, (((uint8_t *)t146) + (uintptr_t)t148 * 1));
                                if (t149) {
                                    void * t150 = gm_l0;
                                    void * t151 = GM_LD(void *, (((uint8_t *)t150) + 120));
                                    void * t152 = GM_LD(void *, (((uint8_t *)t151) + 48));
                                    uint64_t t153 = GM_LD(uint64_t, ((((uint8_t *)t151) + 48) + 8));
                                    uint32_t t154 = gm_l19;
                                    uint64_t t155 = (uint64_t)t154;
                                    if ((uintptr_t)t155 >= (uintptr_t)(t153)) gm_trap_index((uintptr_t)t155, t153, gm_src3m73omiakl12, 149, 22);
                                    gm_t88rcucg1b7uk t156 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t152) + (uintptr_t)t155 * 40) + 16));
                                    gm_l20 = t156;
                                    {
                                        void * t157 = (void *)((uint8_t *)&F.l14);
                                        void * t158 = gm_l0;
                                        void * t159 = GM_LD(void *, ((uint8_t *)t158));
                                        void * t160 = (void *)(((uint8_t *)t159) + 8);
                                        gm_t88rcucg1b7uk t161 = gm_l20;
                                        gm_b16_8 t162 = gm_f_param_cty_e8tvhrm((void *)NULL, cx, NULL, t160, t161);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint64_t t163 = gm_l18;
                                        gm_t3877pk7lv748 t164;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t164 + 0), t162);
                                        GM_ST(uint64_t, ((uint8_t *)&t164 + 16), t163);
                                        gm_f_format_eq87866((void *)&gm_l45, (void *)NULL, cx, NULL, &t164);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t165 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
                                        if (!t165) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l45) + 8, gm_src3m73omiakl12, 150, 29);
                                        gm_b16_8 t166 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l45) + 8));
                                        gm_b16_8 t167 = GM_LD(gm_b16_8, ((uint8_t *)&t166));
                                        gm_tftb1odc739t5 t168 = gm_f_push_3ef746b(t157, t167);
                                        gm_l46 = t168;
                                    }
                                    uint8_t t169 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
                                    if (!t169) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l46) + 1, gm_src3m73omiakl12, 150, 17);
                                    {
                                        void * t170 = (void *)((uint8_t *)&F.l15);
                                        uint64_t t171 = gm_l18;
                                        gm_t4ce0jd3tdro4 t172;
                                        GM_ST(uint64_t, ((uint8_t *)&t172 + 0), t171);
                                        gm_f_format_l05am3t((void *)&gm_l47, (void *)NULL, cx, NULL, t172);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t173 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
                                        if (!t173) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l47) + 8, gm_src3m73omiakl12, 151, 27);
                                        gm_b16_8 t174 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l47) + 8));
                                        gm_b16_8 t175 = GM_LD(gm_b16_8, ((uint8_t *)&t174));
                                        gm_tftb1odc739t5 t176 = gm_f_push_3ef746b(t170, t175);
                                        gm_l48 = t176;
                                    }
                                    uint8_t t177 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                                    if (!t177) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l48) + 1, gm_src3m73omiakl12, 151, 17);
                                }
                                goto gm_lb4;
                            }
gm_lb5:;
                            gm_trap("no match arm matched the scrutinee", gm_src3m73omiakl12, 147, 9);
                        }
gm_lb4:;
                    }
gm_lb3:;
                    uint64_t t178 = gm_l17;
                    uint64_t r179; bool o180 = __builtin_add_overflow(t178, ((uint64_t)1ULL), &r179);
                    uint64_t t181 = r179;
                    gm_l17 = t181;
                }
            }
gm_lb1:;
        }
    }
    {
        void * t182 = gm_l0;
        void * t183 = GM_LD(void *, ((uint8_t *)t182));
        uint8_t t184 = GM_LD(uint8_t, ((((uint8_t *)t183) + 1136) + 1));
        gm_l21 = t184;
        uint8_t t185 = gm_l21;
        if (t185) {
            void * t186 = (void *)((uint8_t *)&F.l14);
            gm_b16_8 t187;
            GM_ST(void *, (uint8_t *)&t187, (void *)(gm_std40ghjh6qib));
            GM_ST(uintptr_t, (uint8_t *)&t187 + 8, (uintptr_t)(12));
            gm_tftb1odc739t5 t188 = gm_f_push_3ef746b(t186, t187);
            gm_l49 = t188;
            uint8_t t189 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
            if (!t189) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l49) + 1, gm_src3m73omiakl12, 157, 9);
            void * t190 = (void *)((uint8_t *)&F.l15);
            gm_b16_8 t191;
            GM_ST(void *, (uint8_t *)&t191, (void *)(gm_s6db4pm3k5olv));
            GM_ST(uintptr_t, (uint8_t *)&t191 + 8, (uintptr_t)(2));
            gm_tftb1odc739t5 t192 = gm_f_push_3ef746b(t190, t191);
            gm_l50 = t192;
            uint8_t t193 = (uint8_t)!((((uint8_t *)&gm_l50))[0] != 0);
            if (!t193) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l50) + 1, gm_src3m73omiakl12, 158, 9);
        }
        uint8_t t194 = gm_l5;
        uint8_t t195 = (uint8_t)!t194;
        if (t195) {
            void * t196 = (void *)((uint8_t *)&F.l14);
            gm_b16_8 t197;
            GM_ST(void *, (uint8_t *)&t197, (void *)(gm_sura2ofp16u7l));
            GM_ST(uintptr_t, (uint8_t *)&t197 + 8, (uintptr_t)(15));
            gm_tftb1odc739t5 t198 = gm_f_push_3ef746b(t196, t197);
            gm_l51 = t198;
            uint8_t t199 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
            if (!t199) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l51) + 1, gm_src3m73omiakl12, 161, 9);
            void * t200 = (void *)((uint8_t *)&F.l15);
            gm_b16_8 t201 = gm_l4;
            gm_tftb1odc739t5 t202 = gm_f_push_3ef746b(t200, t201);
            gm_l52 = t202;
            uint8_t t203 = (uint8_t)!((((uint8_t *)&gm_l52))[0] != 0);
            if (!t203) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l52) + 1, gm_src3m73omiakl12, 162, 9);
        }
        gm_trqfvmjtt4sij t204;
        gm_f_list_uvfu1k1((void *)&t204, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l22 = t204;
        void * t205 = (void *)((uint8_t *)&F.l22);
        gm_b16_8 t206;
        GM_ST(void *, (uint8_t *)&t206, (void *)(gm_secrjke11f3ph));
        GM_ST(uintptr_t, (uint8_t *)&t206 + 8, (uintptr_t)(12));
        gm_tftb1odc739t5 t207 = gm_f_extend_0cs85gs(t205, t206);
        gm_l53 = t207;
        uint8_t t208 = (uint8_t)!((((uint8_t *)&gm_l53))[0] != 0);
        if (!t208) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l53) + 1, gm_src3m73omiakl12, 165, 5);
        void * t209 = gm_l0;
        void * t210 = GM_LD(void *, (((uint8_t *)t209) + 120));
        uint8_t t211 = GM_LD(uint8_t, (((uint8_t *)t210) + 145));
        if (t211) {
            void * t212 = (void *)((uint8_t *)&F.l22);
            gm_b16_8 t213;
            GM_ST(void *, (uint8_t *)&t213, (void *)(gm_slnuijmj5p1vk));
            GM_ST(uintptr_t, (uint8_t *)&t213 + 8, (uintptr_t)(18));
            gm_tftb1odc739t5 t214 = gm_f_extend_0cs85gs(t212, t213);
            gm_l54 = t214;
            uint8_t t215 = (uint8_t)!((((uint8_t *)&gm_l54))[0] != 0);
            if (!t215) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l54) + 1, gm_src3m73omiakl12, 166, 33);
        }
        void * t216 = (void *)((uint8_t *)&F.l22);
        uint32_t t217 = gm_l12;
        gm_b16_8 t218 = gm_f_size_attrs_abk1l3m(t217);
        gm_tftb1odc739t5 t219 = gm_f_extend_0cs85gs(t216, t218);
        gm_l55 = t219;
        uint8_t t220 = (uint8_t)!((((uint8_t *)&gm_l55))[0] != 0);
        if (!t220) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l55) + 1, gm_src3m73omiakl12, 167, 5);
        gm_b16_8 t221 = GM_LD(gm_b16_8, ((uint8_t *)&F.l22));
        gm_b16_8 t222 = GM_LD(gm_b16_8, ((uint8_t *)&t221));
        gm_b16_8 t223 = gm_l3;
        gm_b16_8 t224 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
        gm_b16_8 t225 = GM_LD(gm_b16_8, ((uint8_t *)&t224));
        gm_b16_8 t226;
        GM_ST(void *, (uint8_t *)&t226, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t226 + 8, (uintptr_t)(2));
        gm_f_join_sfknljb((void *)&gm_l56, (void *)NULL, cx, NULL, t225, t226);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t227 = (uint8_t)!((((uint8_t *)&gm_l56))[0] != 0);
        if (!t227) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l56) + 8, gm_src3m73omiakl12, 168, 74);
        gm_b16_8 t228 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l56) + 8));
        gm_tfeab279c16rr t229;
        GM_ST(gm_b16_8, ((uint8_t *)&t229 + 0), t222);
        GM_ST(gm_b16_8, ((uint8_t *)&t229 + 16), t223);
        GM_ST(gm_b16_8, ((uint8_t *)&t229 + 32), t228);
        gm_f_format_ti4oun0((void *)&gm_l57, (void *)NULL, cx, NULL, &t229);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t230 = (uint8_t)!((((uint8_t *)&gm_l57))[0] != 0);
        if (!t230) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l57) + 8, gm_src3m73omiakl12, 168, 13);
        gm_b16_8 t231 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l57) + 8));
        gm_l23 = t231;
        void * t232 = gm_l0;
        void * t233 = GM_LD(void *, ((uint8_t *)t232));
        void * t234 = (void *)(((uint8_t *)t233) + 1944);
        gm_b16_8 t235 = gm_l23;
        gm_tnpifv20irnph t236;
        GM_ST(gm_b16_8, ((uint8_t *)&t236 + 0), t235);
        gm_tftb1odc739t5 t237 = gm_f_write_i2vvij8(t234, t236);
        gm_l58 = t237;
        uint8_t t238 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
        if (!t238) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l58) + 1, gm_src3m73omiakl12, 169, 5);
        gm_trqfvmjtt4sij t239;
        gm_f_list_uvfu1k1((void *)&t239, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l24 = t239;
        void * t240 = (void *)((uint8_t *)&F.l24);
        gm_b16_8 t241 = gm_l23;
        gm_tnpifv20irnph t242;
        GM_ST(gm_b16_8, ((uint8_t *)&t242 + 0), t241);
        gm_tftb1odc739t5 t243 = gm_f_write_2j0ojke(t240, t242);
        gm_l59 = t243;
        uint8_t t244 = (uint8_t)!((((uint8_t *)&gm_l59))[0] != 0);
        if (!t244) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l59) + 1, gm_src3m73omiakl12, 171, 5);
        uint8_t t245 = gm_l21;
        if (t245) {
            void * t246 = (void *)((uint8_t *)&F.l24);
            gm_b16_8 t247;
            GM_ST(void *, (uint8_t *)&t247, (void *)(gm_sj53lcu5ne531));
            GM_ST(uintptr_t, (uint8_t *)&t247 + 8, (uintptr_t)(19));
            gm_tftb1odc739t5 t248 = gm_f_extend_0cs85gs(t246, t247);
            gm_l60 = t248;
            uint8_t t249 = (uint8_t)!((((uint8_t *)&gm_l60))[0] != 0);
            if (!t249) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l60) + 1, gm_src3m73omiakl12, 172, 16);
        }
        void * t250 = gm_l0;
        void * t251 = GM_LD(void *, (((uint8_t *)t250) + 120));
        uint8_t t252 = GM_LD(uint8_t, (((uint8_t *)t251) + 145));
        if (t252) {
            void * t253 = (void *)((uint8_t *)&F.l24);
            gm_b16_8 t254;
            GM_ST(void *, (uint8_t *)&t254, (void *)(gm_sdvlm6i5s2j37));
            GM_ST(uintptr_t, (uint8_t *)&t254 + 8, (uintptr_t)(22));
            gm_tftb1odc739t5 t255 = gm_f_extend_0cs85gs(t253, t254);
            gm_l61 = t255;
            uint8_t t256 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
            if (!t256) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l61) + 1, gm_src3m73omiakl12, 173, 33);
        }
        void * t257 = (void *)((uint8_t *)&F.l24);
        gm_b16_8 t258 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
        gm_b16_8 t259 = GM_LD(gm_b16_8, ((uint8_t *)&t258));
        gm_tftb1odc739t5 t260 = gm_f_extend_0cs85gs(t257, t259);
        gm_l62 = t260;
        uint8_t t261 = (uint8_t)!((((uint8_t *)&gm_l62))[0] != 0);
        if (!t261) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l62) + 1, gm_src3m73omiakl12, 174, 5);
        void * t262 = (void *)((uint8_t *)&F.l24);
        gm_b16_8 t263;
        GM_ST(void *, (uint8_t *)&t263, (void *)(gm_s5bohs6opthjm));
        GM_ST(uintptr_t, (uint8_t *)&t263 + 8, (uintptr_t)(23));
        gm_tftb1odc739t5 t264 = gm_f_extend_0cs85gs(t262, t263);
        gm_l63 = t264;
        uint8_t t265 = (uint8_t)!((((uint8_t *)&gm_l63))[0] != 0);
        if (!t265) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l63) + 1, gm_src3m73omiakl12, 175, 5);
        uint8_t t266 = gm_l21;
        if (t266) {
            void * t267 = (void *)((uint8_t *)&F.l24);
            gm_b16_8 t268;
            GM_ST(void *, (uint8_t *)&t268, (void *)(gm_s84k1uo35omt8));
            GM_ST(uintptr_t, (uint8_t *)&t268 + 8, (uintptr_t)(9));
            gm_tftb1odc739t5 t269 = gm_f_extend_0cs85gs(t267, t268);
            gm_l64 = t269;
            uint8_t t270 = (uint8_t)!((((uint8_t *)&gm_l64))[0] != 0);
            if (!t270) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l64) + 1, gm_src3m73omiakl12, 176, 16);
        }
        void * t271 = (void *)((uint8_t *)&F.l24);
        gm_b16_8 t272;
        GM_ST(void *, (uint8_t *)&t272, (void *)(gm_sikpqtja8pqpv));
        GM_ST(uintptr_t, (uint8_t *)&t272 + 8, (uintptr_t)(2));
        gm_tftb1odc739t5 t273 = gm_f_extend_0cs85gs(t271, t272);
        gm_l65 = t273;
    }
    {
        uint8_t t274 = (uint8_t)!((((uint8_t *)&gm_l65))[0] != 0);
        if (!t274) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l65) + 1, gm_src3m73omiakl12, 177, 5);
        void * t275 = gm_l0;
        void * t276 = GM_LD(void *, ((uint8_t *)t275));
        void * t277 = (void *)(((uint8_t *)t276) + 1984);
        gm_b16_8 t278 = GM_LD(gm_b16_8, ((uint8_t *)&F.l24));
        gm_b16_8 t279 = GM_LD(gm_b16_8, ((uint8_t *)&t278));
        gm_tftb1odc739t5 t280 = gm_f_extend_0cs85gs(t277, t279);
        gm_l66 = t280;
        uint8_t t281 = (uint8_t)!((((uint8_t *)&gm_l66))[0] != 0);
        if (!t281) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l66) + 1, gm_src3m73omiakl12, 178, 5);
        gm_b16_8 t282 = gm_l3;
        gm_b16_8 t283 = GM_LD(gm_b16_8, ((uint8_t *)&F.l15));
        gm_b16_8 t284 = GM_LD(gm_b16_8, ((uint8_t *)&t283));
        gm_b16_8 t285;
        GM_ST(void *, (uint8_t *)&t285, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t285 + 8, (uintptr_t)(2));
        gm_f_join_sfknljb((void *)&gm_l67, (void *)NULL, cx, NULL, t284, t285);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t286 = (uint8_t)!((((uint8_t *)&gm_l67))[0] != 0);
        if (!t286) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l67) + 8, gm_src3m73omiakl12, 180, 40);
        gm_b16_8 t287 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l67) + 8));
        gm_t6j6smj7qoctu t288;
        GM_ST(gm_b16_8, ((uint8_t *)&t288 + 0), t282);
        GM_ST(gm_b16_8, ((uint8_t *)&t288 + 16), t287);
        gm_f_format_a09tbhs((void *)&gm_l68, (void *)NULL, cx, NULL, &t288);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t289 = (uint8_t)!((((uint8_t *)&gm_l68))[0] != 0);
        if (!t289) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l68) + 8, gm_src3m73omiakl12, 180, 13);
        gm_b16_8 t290 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l68) + 8));
        gm_l25 = t290;
        uint64_t t291 = GM_LD(uint64_t, ((((uint8_t *)&F.l10) + 8) + 8));
        uint8_t t292 = ((t291 == ((uint64_t)0ULL)));
        if (t292) {
            void * t293 = gm_l0;
            gm_b16_8 t294 = gm_l25;
            gm_tnpifv20irnph t295;
            GM_ST(gm_b16_8, ((uint8_t *)&t295 + 0), t294);
            gm_b0_1 t296 = gm_f_linef_v355qja((void *)NULL, cx, NULL, t293, t295);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        } else {
            void * t297 = gm_l0;
            gm_b16_8 t298 = gm_l25;
            gm_tnpifv20irnph t299;
            GM_ST(gm_b16_8, ((uint8_t *)&t299 + 0), t298);
            gm_b0_1 t300 = gm_f_linef_q6e8rn6((void *)NULL, cx, NULL, t297, t299);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t301 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 8));
            gm_b16_8 t302 = GM_LD(gm_b16_8, ((uint8_t *)&t301));
            gm_l26 = t302;
            gm_l27 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t303 = gm_l27;
                    uint64_t t304 = GM_LD(uint64_t, (((uint8_t *)&gm_l26) + 8));
                    uint8_t t305 = (t303 < t304);
                    if (!t305) {
                        goto gm_lb6;
                    }
                    {
                        {
                            uint64_t t306 = gm_l27;
                            void * t307 = GM_LD(void *, ((uint8_t *)&gm_l26));
                            uint64_t t308 = GM_LD(uint64_t, (((uint8_t *)&gm_l26) + 8));
                            uint64_t t309 = gm_l27;
                            gm_tj64ui6kruni6 t310 = GM_LD(gm_tj64ui6kruni6, (((uint8_t *)t307) + (uintptr_t)t309 * 32));
                            GM_ST(uint64_t, ((uint8_t *)&gm_l69 + 0), t306);
                            GM_ST(gm_tj64ui6kruni6, ((uint8_t *)&gm_l69 + 8), t310);
                        }
                        {
                            {
                                uint64_t t311 = GM_LD(uint64_t, ((uint8_t *)&gm_l69));
                                gm_l28 = t311;
                                memmove(((uint8_t *)&gm_l29), (((uint8_t *)&gm_l69) + 8), 32);
                                void * t312 = gm_l0;
                                uint64_t t313 = gm_l28;
                                uint64_t r314; bool o315 = __builtin_add_overflow(t313, ((uint64_t)1ULL), &r314);
                                if (o315) gm_trap("integer overflow in `+`", gm_src3m73omiakl12, 186, 34);
                                uint64_t t316 = r314;
                                gm_t4ce0jd3tdro4 t317;
                                GM_ST(uint64_t, ((uint8_t *)&t317 + 0), t316);
                                gm_b0_1 t318 = gm_f_linef_j9naoa6((void *)NULL, cx, NULL, t312, t317);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t319 = gm_l0;
                                void * t320 = gm_l0;
                                uint64_t t321 = GM_LD(uint64_t, (((uint8_t *)t320) + 168));
                                uint64_t r322; bool o323 = __builtin_add_overflow(t321, ((uint64_t)1ULL), &r322);
                                if (o323) gm_trap("integer overflow in `+`", gm_src3m73omiakl12, 187, 13);
                                uint64_t t324 = r322;
                                GM_ST(uint64_t, (((uint8_t *)t319) + 168), t324);
                                void * t325 = gm_l0;
                                gm_b16_8 t326 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l29));
                                gm_tf5srh4mp4b3i t327 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&gm_l29) + 16));
                                gm_b0_1 t328 = gm_f_jump_pvnkvme((void *)NULL, cx, NULL, t325, t326, t327);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t329 = gm_l0;
                                void * t330 = gm_l0;
                                uint64_t t331 = GM_LD(uint64_t, (((uint8_t *)t330) + 168));
                                uint64_t r332; bool o333 = __builtin_sub_overflow(t331, ((uint64_t)1ULL), &r332);
                                if (o333) gm_trap("integer overflow in `-`", gm_src3m73omiakl12, 189, 13);
                                uint64_t t334 = r332;
                                GM_ST(uint64_t, (((uint8_t *)t329) + 168), t334);
                                goto gm_lb9;
                            }
gm_lb10:;
                            gm_trap("no match arm matched the scrutinee", gm_src3m73omiakl12, 185, 9);
                        }
gm_lb9:;
                    }
gm_lb8:;
                    uint64_t t335 = gm_l27;
                    uint64_t r336; bool o337 = __builtin_add_overflow(t335, ((uint64_t)1ULL), &r336);
                    uint64_t t338 = r336;
                    gm_l27 = t338;
                }
            }
gm_lb6:;
            void * t339 = gm_l0;
            gm_b16_8 t340;
            GM_ST(void *, (uint8_t *)&t340, (void *)(gm_s2cuflhqkkuaq));
            GM_ST(uintptr_t, (uint8_t *)&t340 + 8, (uintptr_t)(1));
            gm_b0_1 t341 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t339, t340);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        memset(&(*(gm_tqgtd5gtgj65s *)gm_out), 0, sizeof (*(gm_tqgtd5gtgj65s *)gm_out));
        ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8), GM_ZERO(gm_b0_1));
        goto gm_fdone;
    }
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
extern __attribute__((aligned(4))) uint8_t gm_f_outlines_2kvbvit(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
extern __attribute__((aligned(4))) uint32_t gm_f_weight_nu7ub4o(void * a0, uint32_t a1);

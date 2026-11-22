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
typedef struct { int32_t f0; } gm_t19enj90vg8bd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; } gm_tb31k5l794t7p;
typedef struct { int64_t f0; } gm_tm98ap4112hee;
typedef struct { uint8_t f0; uint8_t p1[7]; uint64_t f1; } gm_tphtgnn9vgkcv;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t19enj90vg8bd) == 4 && _Alignof(gm_t19enj90vg8bd) == 4 && offsetof(gm_t19enj90vg8bd, f0) == 0, "layout of `(i32,)`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tb31k5l794t7p) == 16 && _Alignof(gm_tb31k5l794t7p) == 8 && offsetof(gm_tb31k5l794t7p, f0) == 0 && offsetof(gm_tb31k5l794t7p, f1) == 8, "layout of `Char`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tcrdb1inj22os;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint32_t f3; uint8_t p4[4]; gm_b16_8 f4; } gm_td11sh93t44su;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; } gm_tgbgti82mf879;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; } gm_th2d9c7aju8hp;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; uint64_t f3; } gm_thddbqb6snabm;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; } gm_tif3elqk01bdl;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { gm_t9qkh0rsooc8d f0; } gm_tjj2cncin95t1;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; gm_b16_8 f8; gm_b16_8 f9; } gm_tk6rvhh54g86p;
_Static_assert(sizeof(gm_tm98ap4112hee) == 8 && _Alignof(gm_tm98ap4112hee) == 8 && offsetof(gm_tm98ap4112hee, f0) == 0, "layout of `File`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
_Static_assert(sizeof(gm_tphtgnn9vgkcv) == 16 && _Alignof(gm_tphtgnn9vgkcv) == 8 && offsetof(gm_tphtgnn9vgkcv, f0) == 0 && offsetof(gm_tphtgnn9vgkcv, f1) == 8, "layout of `(found: bool, at: usize)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t4ce0jd3tdro4 v0; } f1; } gm_ttiiv5vhiuvvr;
typedef struct { int32_t f0; uint8_t p1[4]; uint64_t f1; gm_b16_8 f2; uint64_t f3; } gm_ttld71ltp94ka;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; uint32_t f2; uint8_t p3[4]; gm_b16_8 f3; } gm_tvi24gafvcime;
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `(string, usize)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttiiv5vhiuvvr v0; gm_tphtgnn9vgkcv v1; } f1; } gm_t4mjne9lcbiv4;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tcrdb1inj22os) == 72 && _Alignof(gm_tcrdb1inj22os) == 8 && offsetof(gm_tcrdb1inj22os, f0) == 0 && offsetof(gm_tcrdb1inj22os, f1) == 16 && offsetof(gm_tcrdb1inj22os, f2) == 24 && offsetof(gm_tcrdb1inj22os, f3) == 40 && offsetof(gm_tcrdb1inj22os, f4) == 56, "layout of `(string, usize, string, string, string)`");
_Static_assert(sizeof(gm_td11sh93t44su) == 72 && _Alignof(gm_td11sh93t44su) == 8 && offsetof(gm_td11sh93t44su, f0) == 0 && offsetof(gm_td11sh93t44su, f1) == 16 && offsetof(gm_td11sh93t44su, f2) == 32 && offsetof(gm_td11sh93t44su, f3) == 48 && offsetof(gm_td11sh93t44su, f4) == 56, "layout of `(string, string, string, u32, string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgbgti82mf879) == 32 && _Alignof(gm_tgbgti82mf879) == 8 && offsetof(gm_tgbgti82mf879, f0) == 0 && offsetof(gm_tgbgti82mf879, f1) == 16 && offsetof(gm_tgbgti82mf879, f2) == 24, "layout of `PayloadUnion`");
_Static_assert(sizeof(gm_th2d9c7aju8hp) == 112 && _Alignof(gm_th2d9c7aju8hp) == 8 && offsetof(gm_th2d9c7aju8hp, f0) == 0 && offsetof(gm_th2d9c7aju8hp, f1) == 16 && offsetof(gm_th2d9c7aju8hp, f2) == 32 && offsetof(gm_th2d9c7aju8hp, f3) == 48 && offsetof(gm_th2d9c7aju8hp, f4) == 64 && offsetof(gm_th2d9c7aju8hp, f5) == 80 && offsetof(gm_th2d9c7aju8hp, f6) == 96, "layout of `(string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_thddbqb6snabm) == 48 && _Alignof(gm_thddbqb6snabm) == 8 && offsetof(gm_thddbqb6snabm, f0) == 0 && offsetof(gm_thddbqb6snabm, f1) == 16 && offsetof(gm_thddbqb6snabm, f2) == 24 && offsetof(gm_thddbqb6snabm, f3) == 40, "layout of `(string, u64, string, u64)`");
_Static_assert(sizeof(gm_tif3elqk01bdl) == 96 && _Alignof(gm_tif3elqk01bdl) == 8 && offsetof(gm_tif3elqk01bdl, f0) == 0 && offsetof(gm_tif3elqk01bdl, f1) == 16 && offsetof(gm_tif3elqk01bdl, f2) == 32 && offsetof(gm_tif3elqk01bdl, f3) == 48 && offsetof(gm_tif3elqk01bdl, f4) == 64 && offsetof(gm_tif3elqk01bdl, f5) == 80, "layout of `(string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tjj2cncin95t1) == 1 && _Alignof(gm_tjj2cncin95t1) == 1 && offsetof(gm_tjj2cncin95t1, f0) == 0, "layout of `(Wrap,)`");
_Static_assert(sizeof(gm_tk6rvhh54g86p) == 160 && _Alignof(gm_tk6rvhh54g86p) == 8 && offsetof(gm_tk6rvhh54g86p, f0) == 0 && offsetof(gm_tk6rvhh54g86p, f1) == 16 && offsetof(gm_tk6rvhh54g86p, f2) == 32 && offsetof(gm_tk6rvhh54g86p, f3) == 48 && offsetof(gm_tk6rvhh54g86p, f4) == 64 && offsetof(gm_tk6rvhh54g86p, f5) == 80 && offsetof(gm_tk6rvhh54g86p, f6) == 96 && offsetof(gm_tk6rvhh54g86p, f7) == 112 && offsetof(gm_tk6rvhh54g86p, f8) == 128 && offsetof(gm_tk6rvhh54g86p, f9) == 144, "layout of `(string, string, string, string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ttiiv5vhiuvvr) == 16 && _Alignof(gm_ttiiv5vhiuvvr) == 8 && offsetof(gm_ttiiv5vhiuvvr, f0) == 0 && offsetof(gm_ttiiv5vhiuvvr, f1) == 8, "layout of `ParseError`");
_Static_assert(sizeof(gm_ttld71ltp94ka) == 40 && _Alignof(gm_ttld71ltp94ka) == 8 && offsetof(gm_ttld71ltp94ka, f0) == 0 && offsetof(gm_ttld71ltp94ka, f1) == 8 && offsetof(gm_ttld71ltp94ka, f2) == 16 && offsetof(gm_ttld71ltp94ka, f3) == 32, "layout of `(i32, u64, string, u64)`");
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
_Static_assert(sizeof(gm_tvi24gafvcime) == 48 && _Alignof(gm_tvi24gafvcime) == 8 && offsetof(gm_tvi24gafvcime, f0) == 0 && offsetof(gm_tvi24gafvcime, f1) == 8 && offsetof(gm_tvi24gafvcime, f2) == 24 && offsetof(gm_tvi24gafvcime, f3) == 32, "layout of `(u32, string, u32, string)`");
_Static_assert(sizeof(gm_t4mjne9lcbiv4) == 24 && _Alignof(gm_t4mjne9lcbiv4) == 8 && offsetof(gm_t4mjne9lcbiv4, f0) == 0 && offsetof(gm_t4mjne9lcbiv4, f1) == 8, "layout of `ParseError!(neg: bool, at: usize)`");
struct gm_fr_deinit_r70pji5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_fn_1trbst9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_802et9b {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ko2ljtu {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_o51ej4r {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_5029eea {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_506scju {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_51h4888 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_535vc4d {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_53mv29j {
    gm_frame h;
    gm_tk6rvhh54g86p l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5496f10 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_54ifjd1 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_54mn8jj {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_554rdam {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_556kaa4 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_55igra0 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_560qv19 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_570tujl {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_57u5svl {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5823bl2 {
    gm_frame h;
    gm_tcrdb1inj22os l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tcrdb1inj22os l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5a05gmg {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5au36p4 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5b908c8 {
    gm_frame h;
    gm_tud7oovfvnf43 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5bcbbmm {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5bo63p7 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5erjuiq {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5f2vpdc {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5htdr0o {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5iu9hua {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5j9lqr8 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5k1v0hr {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5k9iv40 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5lfjq97 {
    gm_frame h;
    gm_tvi24gafvcime l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5lk0c41 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5m6a3eh {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5mel2nn {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5mja5ge {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5nk8afu {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5p0qm94 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5pe4c2v {
    gm_frame h;
    gm_tud7oovfvnf43 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5pl0mkc {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5qcf83g {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5r372o6 {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5rrmec0 {
    gm_frame h;
    gm_tud7oovfvnf43 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5tk5m6v {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5up8eqa {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5uun4vl {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5v26vgi {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l002mid {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l035o11 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l05am3t {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l0aimgq {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l0bpsnf {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l0gkulr {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l0pq86n {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l1ii34d {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l21cpr6 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l251doo {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l2fmok5 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l53b7vj {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l5e62u0 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l7c15sv {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l7hthr8 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l7u3cq2 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l821hfj {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l93mv4o {
    gm_frame h;
    gm_td11sh93t44su l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_td11sh93t44su l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l9df9v3 {
    gm_frame h;
    gm_th2d9c7aju8hp l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_la70vrc {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lac0oi2 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_las1g7o {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lb0inss {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lbar0n6 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lc07prd {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lc73epv {
    gm_frame h;
    gm_tif3elqk01bdl l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lcm4k53 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lde77u1 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lderfbr {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ldnd7h1 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_le00do2 {
    gm_frame h;
    gm_thddbqb6snabm l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lefnhqh {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_les73rv {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lfjb9aq {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lflgtrt {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lhid49k {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_li1975k {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_licpkoa {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ljka1bn {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ljsk7ji {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lk5ln6l {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lkeo172 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ll295o3 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_llf38gp {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_llhsr0b {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ln6s15q {
    gm_frame h;
    gm_ttld71ltp94ka l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_ttld71ltp94ka l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lo2svpj {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_loa1toi {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_loh175m {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lovbvbi {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lpj4rro {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lpm1a8i {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lqnh3lt {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lrgko01 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lsckir6 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lsmcccn {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ltbcfgo {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ltp901v {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lu3aj0k {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lu93b1k {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lufljqc {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lveqqdp {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_reserve_d1qo8mf {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_sign_50jle9l {
    gm_frame h;
    gm_t4mjne9lcbiv4 ret;
};
inline __attribute__((aligned(4))) void gm_f_deinit_r70pji5(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1trbst9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_802et9b(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ko2ljtu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_o51ej4r(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_format_01lon97(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_0dva8tv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_2eptqlp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tb31k5l794t7p a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_2kt92vg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_3tlgo2e(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_5029eea(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_506scju(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_51h4888(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_535vc4d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_53mv29j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_5496f10(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_54ifjd1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_54mn8jj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_554rdam(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t19enj90vg8bd a0);
inline __attribute__((aligned(4))) void gm_f_format_556kaa4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_55igra0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_560qv19(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_570tujl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_57u5svl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_5823bl2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_5a05gmg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_5au36p4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_5b908c8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_5bcbbmm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tb31k5l794t7p a0);
inline __attribute__((aligned(4))) void gm_f_format_5bo63p7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_5erjuiq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_5f2vpdc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_5htdr0o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_5iu9hua(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_5j9lqr8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_5k1v0hr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_5k9iv40(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_5lfjq97(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_5lk0c41(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_5m6a3eh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_5mel2nn(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_5mja5ge(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) void gm_f_format_5nk8afu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_5p0qm94(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_5pe4c2v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_5pl0mkc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_5qcf83g(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_5r372o6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_5rrmec0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_5tk5m6v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_5up8eqa(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_5uun4vl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_5v26vgi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_65ee4ra(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_6hrn7j8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_74pd3in(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_abcvqrv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_c97t5s6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_dq6oc41(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_eara94q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_ee7lhc3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_ig8f8pl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_ilrvlso(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tjj2cncin95t1 a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_l002mid(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_l035o11(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_l05am3t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_l0aimgq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_l0bpsnf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_l0gkulr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_l0pq86n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_l1ii34d(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_l21cpr6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_l251doo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_l2fmok5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_l53b7vj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_l5e62u0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_l7c15sv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
inline __attribute__((aligned(4))) void gm_f_format_l7hthr8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_l7u3cq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_l821hfj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_l93mv4o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_l9df9v3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_la70vrc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_lac0oi2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_las1g7o(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_lb0inss(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_lbar0n6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_lc07prd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_lc73epv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_lcm4k53(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_lde77u1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_lderfbr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ldnd7h1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_le00do2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_lefnhqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_les73rv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_lfjb9aq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_lflgtrt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_lhid49k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_li1975k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_licpkoa(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ljka1bn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tjj2cncin95t1 a0);
inline __attribute__((aligned(4))) void gm_f_format_ljsk7ji(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_lk5ln6l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a0);
inline __attribute__((aligned(4))) void gm_f_format_lkeo172(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) void gm_f_format_ll295o3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_llf38gp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_llhsr0b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void gm_f_format_ln6s15q(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_lo2svpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_loa1toi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_loh175m(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_lovbvbi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_lpj4rro(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_lpm1a8i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_lqnh3lt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_lrgko01(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_lsckir6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_lsmcccn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ltbcfgo(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ltp901v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_lu3aj0k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_lu93b1k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_lufljqc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_lveqqdp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_mccr87v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_n3u30k9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_ncqd055(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_otin3sf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_q4qso5a(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_rc5a5cm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_rqlkk21(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_sc59up9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t19enj90vg8bd a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_vepf78n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_reserve_d1qo8mf(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_sign_50jle9l(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_t4mjne9lcbiv4 gm_f_sign_50jle9l_c(gm_b16_8 a0);
GM_NOINLINE bool gm_pf0aqhubr4up(void *code, void *env, gm_node *cx, const void *d, uintptr_t n);
GM_NOINLINE bool gm_pfk8csn7us23(void *code, void *env, gm_node *cx, const gm_type *t, const void *v, const gm_spec *s);
extern const uint8_t gm_s01k0p56k10gk[29];
extern const uint8_t gm_s0uum3co6vapt[23];
extern const uint8_t gm_s1ctgkvj02ag0[34];
extern const uint8_t gm_s1ti14uv3rrcf[65];
extern const uint8_t gm_s2dog0ijtvmd2[32];
extern const uint8_t gm_s2jmrvqu8r8aq[58];
extern const uint8_t gm_s2r03t2n892pm[27];
extern const uint8_t gm_s3fj8brek97co[86];
extern const uint8_t gm_s4108vvmvu0gn[70];
extern const uint8_t gm_s41sp7ht9l505[67];
extern const uint8_t gm_s42b9o00en8pl[34];
extern const uint8_t gm_s46khuklhbvqd[21];
extern const uint8_t gm_s4pru1na1k33s[3];
extern const uint8_t gm_s4r2qm241merr[12];
extern const uint8_t gm_s589bsqckia3s[8];
extern const uint8_t gm_s5cbc2v9e254r[37];
extern const uint8_t gm_s5j2jdmako4im[37];
extern const uint8_t gm_s5u07ahpa78ud[27];
extern const uint8_t gm_s658vrmfgnecg[28];
extern const uint8_t gm_s71434ub9jp42[11];
extern const uint8_t gm_s7aoh8j3iph16[51];
extern const uint8_t gm_s87g1ufoaqi74[8];
extern const uint8_t gm_s8n858pjjep3e[10];
extern const uint8_t gm_s8olahjn69at6[21];
extern const uint8_t gm_s95cbsclgo7gk[36];
extern const uint8_t gm_s9ap8gk0oj12e[73];
extern const uint8_t gm_s9hqndl9se5nc[31];
extern const uint8_t gm_sac01n562d8ve[59];
extern const uint8_t gm_savtbnmf9bjnr[17];
extern const uint8_t gm_sb7jq1p8shp96[113];
extern const uint8_t gm_sb8qu1qqed6pt[85];
extern const uint8_t gm_sbj6flp3i82cp[32];
extern const uint8_t gm_sc3pq8972q5i0[15];
extern const uint8_t gm_sc9hsbhe0l538[7];
extern const uint8_t gm_sd2ioiubqiqs5[46];
extern const uint8_t gm_sd5iqorsg0rc4[73];
extern const uint8_t gm_sd667aplubiqu[34];
extern const uint8_t gm_sd9r0l17bbuoe[5];
extern const uint8_t gm_sebk4v8jac58q[43];
extern const uint8_t gm_sekb1d43i0ft2[10];
extern const uint8_t gm_seplhq3i7l38b[195];
extern const uint8_t gm_sfhct1r6m1qqo[11];
extern const uint8_t gm_sfl0m0vifcu3m[82];
extern const uint8_t gm_sfqr2pc72qt8e[23];
extern const uint8_t gm_sg3a8rmfq3j12[50];
extern const uint8_t gm_sgkuqlapm8vur[12];
extern const uint8_t gm_sglgjrjci6t1d[25];
extern const uint8_t gm_sgme8q1t414rd[19];
extern const uint8_t gm_shmsiqao0jnvn[42];
extern const uint8_t gm_si3s44ll3u1p2[42];
extern const uint8_t gm_si731m8b0sr6s[4];
extern const uint8_t gm_sidgpvfsv94q1[56];
extern const uint8_t gm_siq422vplpm2h[54];
extern const uint8_t gm_sjc9ao9066c7n[33];
extern const uint8_t gm_sjrtlfuv3os00[75];
extern const uint8_t gm_skbosgm06mmhc[57];
extern const uint8_t gm_skvqkn3gte5mk[11];
extern const uint8_t gm_sl5h9isb3bijl[5];
extern const uint8_t gm_sl84tf4h2e7v7[29];
extern const uint8_t gm_sl9i9hcgkt8fd[97];
extern const uint8_t gm_slk5snj328gko[45];
extern const uint8_t gm_sm6jfloe4be09[27];
extern const uint8_t gm_sn9b7u92b8vpk[15];
extern const uint8_t gm_snbn61h56c5fj[52];
extern const uint8_t gm_snm19vfbec4q3[19];
extern const uint8_t gm_so52klh2mgoce[23];
extern const uint8_t gm_soi6iv1hnej40[13];
extern const uint8_t gm_sojpmv6gl0jac[12];
extern const uint8_t gm_sopln0aep28du[47];
extern const uint8_t gm_sovg8lhrcqaa0[34];
extern const uint8_t gm_sp1u9v27v4c4d[24];
extern const uint8_t gm_sp7351fukevmv[29];
extern const uint8_t gm_spbvs3aa34vln[15];
extern const uint8_t gm_spuk9sqnoo3ai[13];
extern const uint8_t gm_sq2es9bij87b7[14];
extern const uint8_t gm_sq58pm227uuk8[53];
extern const uint8_t gm_sqg1k42secsds[29];
extern const uint8_t gm_sqk2n9kn6vnik[25];
extern const uint8_t gm_sqlpqagi4dpk1[38];
extern const uint8_t gm_sqmpncmbmfe90[8];
extern const uint8_t gm_sqn4pcenn3reh[63];
extern const uint8_t gm_sqqlqus41v6vq[12];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_ss3lpq4se4ir5[5];
extern const uint8_t gm_ssa5e93p23oqi[90];
extern const uint8_t gm_ssao50s66pj11[8];
extern const uint8_t gm_sso7iusdc4q0g[11];
extern const uint8_t gm_ssu2vb7lj6d81[21];
extern const uint8_t gm_stb0ck7v8hutn[8];
extern const uint8_t gm_stcivomuinkhh[21];
extern const uint8_t gm_stocunr1upos5[9];
extern const uint8_t gm_su20i5iji4ao3[4];
extern const uint8_t gm_su43dt5mihp55[22];
extern const uint8_t gm_su6il7ugmnq2g[48];
extern const uint8_t gm_su88ia0hdam4d[66];
extern const uint8_t gm_sutk9gh8f6gmt[53];
extern const uint8_t gm_sv6dkmq38siqt[27];
extern const uint8_t gm_sv8qamggba7e1[18];
extern const uint8_t gm_svghqphscbkj1[7];
extern const uint8_t gm_svkrg5dctlh9m[7];
extern const uint8_t gm_svqapkbdfenmd[54];
extern const gm_tmpl gm_tpl00nmcu166crh;
extern const gm_tmpl gm_tpl1f7c1o805fpf;
extern const gm_tmpl gm_tpl1lvejpvpq0qh;
extern const gm_tmpl gm_tpl1nqa0en23gvp;
extern const gm_tmpl gm_tpl23mh8eh9cef6;
extern const gm_tmpl gm_tpl265m7jpg5oed;
extern const gm_tmpl gm_tpl29pahnav2feq;
extern const gm_tmpl gm_tpl2d7ij9b2sa2u;
extern const gm_tmpl gm_tpl32o7ct8vibbi;
extern const gm_tmpl gm_tpl3sv6aqbksrr5;
extern const gm_tmpl gm_tpl53ihvkdedd31;
extern const gm_tmpl gm_tpl54tnh29jjfju;
extern const gm_tmpl gm_tpl58k6ai39p40h;
extern const gm_tmpl gm_tpl5uko75p49g56;
extern const gm_tmpl gm_tpl5vbp9fc0mpc1;
extern const gm_tmpl gm_tpl6104ee3qfshr;
extern const gm_tmpl gm_tpl6boglt2rkmh4;
extern const gm_tmpl gm_tpl6lpvgmmqlalm;
extern const gm_tmpl gm_tpl6u21hsinpli4;
extern const gm_tmpl gm_tpl7bbfeces83t7;
extern const gm_tmpl gm_tpl7dh44jmr11le;
extern const gm_tmpl gm_tpl7jau285qbbrq;
extern const gm_tmpl gm_tpl7kgo5i3aumd2;
extern const gm_tmpl gm_tpl7t589bv3t4us;
extern const gm_tmpl gm_tpl7umtk166uvmr;
extern const gm_tmpl gm_tpl81uel28fst2a;
extern const gm_tmpl gm_tpl9d622ubds6jn;
extern const gm_tmpl gm_tpl9iaqf5bbsv8o;
extern const gm_tmpl gm_tpl9vu8tqcv1d9s;
extern const gm_tmpl gm_tpla0ldfomuin4r;
extern const gm_tmpl gm_tpla1fscog446dv;
extern const gm_tmpl gm_tpla5rqmr49dnnk;
extern const gm_tmpl gm_tpla843kfhqtvit;
extern const gm_tmpl gm_tplagt25ql42kpb;
extern const gm_tmpl gm_tplc47d0vivqs48;
extern const gm_tmpl gm_tplc5cre0uqposd;
extern const gm_tmpl gm_tplcb1495nujsb7;
extern const gm_tmpl gm_tplcb5rgmp3lpr2;
extern const gm_tmpl gm_tplcilp3mvhfo4n;
extern const gm_tmpl gm_tplck20jih4okbk;
extern const gm_tmpl gm_tplcpt7hf5iscqd;
extern const gm_tmpl gm_tpld3o5iqghffqq;
extern const gm_tmpl gm_tpldsui47p71ms3;
extern const gm_tmpl gm_tple7msnncsq0hr;
extern const gm_tmpl gm_tple87ca4est7gh;
extern const gm_tmpl gm_tplei8v65ors6jc;
extern const gm_tmpl gm_tplemu1bmibr9ve;
extern const gm_tmpl gm_tplft0ecs37svcn;
extern const gm_tmpl gm_tplg05c24rh8peq;
extern const gm_tmpl gm_tplg21rd4as3ph6;
extern const gm_tmpl gm_tplg575kk57c4i8;
extern const gm_tmpl gm_tplgcrr0tu9874k;
extern const gm_tmpl gm_tplgemvjdfjljaa;
extern const gm_tmpl gm_tplggnuionimjk9;
extern const gm_tmpl gm_tplgp08d1iih1si;
extern const gm_tmpl gm_tplh05i67kfcnal;
extern const gm_tmpl gm_tplh7ffbsfnitkh;
extern const gm_tmpl gm_tpli8blk453jgvg;
extern const gm_tmpl gm_tplj0t0s73b2v4j;
extern const gm_tmpl gm_tplj0vaqlfn7orc;
extern const gm_tmpl gm_tplj451rojbvr15;
extern const gm_tmpl gm_tplj7j0vuugbh2u;
extern const gm_tmpl gm_tplja7aur1oiv70;
extern const gm_tmpl gm_tpljcso3h5g4o29;
extern const gm_tmpl gm_tpljkde9cibn2er;
extern const gm_tmpl gm_tpljnlsd7076tqd;
extern const gm_tmpl gm_tplk34ojqiaho84;
extern const gm_tmpl gm_tplkaran1m5lpqm;
extern const gm_tmpl gm_tplkik9smkp1eb6;
extern const gm_tmpl gm_tpllm39516ci1mu;
extern const gm_tmpl gm_tpllmofqkiu75lh;
extern const gm_tmpl gm_tpllovel7a2ips1;
extern const gm_tmpl gm_tpllsre3itruefn;
extern const gm_tmpl gm_tplm9hj31tk9ip4;
extern const gm_tmpl gm_tplmm8hld32929h;
extern const gm_tmpl gm_tpln2i759e19cgt;
extern const gm_tmpl gm_tpln65gnioheijm;
extern const gm_tmpl gm_tpln7ptjfs3pum5;
extern const gm_tmpl gm_tplnd5opdv87prb;
extern const gm_tmpl gm_tplnndmi09f8nur;
extern const gm_tmpl gm_tplnr2qhie4t5ta;
extern const gm_tmpl gm_tplnr9pmjldkl35;
extern const gm_tmpl gm_tplo203818ggfa8;
extern const gm_tmpl gm_tplopbg40ai4id4;
extern const gm_tmpl gm_tplp0abp7005sqm;
extern const gm_tmpl gm_tplp7faum694g3u;
extern const gm_tmpl gm_tplpj1vfgivuj13;
extern const gm_tmpl gm_tplpkvu14cg936d;
extern const gm_tmpl gm_tplptgaia3s19c9;
extern const gm_tmpl gm_tplqnufra7h2p1s;
extern const gm_tmpl gm_tplr7pcqj5621b9;
extern const gm_tmpl gm_tplrkvt0j8tqlqg;
extern const gm_tmpl gm_tplrsdjn4cifhr9;
extern const gm_tmpl gm_tplu58b6rd4mq28;
extern const gm_tmpl gm_tpluc8r0n3peqad;
extern const gm_tmpl gm_tpludgprff7jihr;
extern const gm_tmpl gm_tplv1i7e9gejv6a;
extern const gm_tmpl gm_tplv8s8fgl2lggu;
extern const gm_tmpl gm_tplvle6f1rmlmjh;
extern const gm_tmpl gm_tplvpehcchgo971;
extern const gm_type gm_ty0iqiubfm7e;
extern const gm_type gm_ty467d5cm1d5;
extern const gm_type gm_ty6groq37ram;
extern const gm_type gm_tyb2nno7n67r;
extern const gm_type gm_tyqa6gfa8omk;
inline __attribute__((aligned(4))) void gm_f_deinit_r70pji5(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_r70pji5 F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 16));
    uint8_t t4 = (t3 > ((uint64_t)0ULL));
    if (t4) {
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, ((((uint8_t *)t5) + 24) + 8));
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 16));
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 24));
        void * t10 = gm_l0;
        void * t11 = GM_LD(void *, ((uint8_t *)t10));
        void * t12 = t11;
        void * t13 = gm_l0;
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 16));
        uint64_t r15; bool o16 = __builtin_mul_overflow(((uint64_t)1ULL), t14, &r15);
        if (o16) gm_trap("integer overflow in `*`", gm_srcrgc9bkquf6nr, 146, 64);
        uint64_t t17 = r15;
        void * t18 = t7;
        gm_b0_1 t19;
        ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t18))(t9, t12, t17, ((uint64_t)1ULL));
    }
    void * t20 = gm_l0;
    gm_b16_8 t21 = gm_f_empty_slice_ijtp9lo();
    GM_ST(gm_b16_8, ((uint8_t *)t20), t21);
    void * t22 = gm_l0;
    GM_ST(uint64_t, (((uint8_t *)t22) + 16), ((uint64_t)0ULL));
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1trbst9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_1trbst9 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 47u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l1;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_5823bl2 *)((&F.h)->up))->l2));
    uint8_t t3 = (uint8_t)!t2;
    if (t3) {
        void * t4 = (void *)((uint8_t *)&((struct gm_fr_format_5823bl2 *)((&F.h)->up))->l1);
        gm_b16_8 t5 = gm_l0;
        gm_tftb1odc739t5 t6 = gm_f_extend_0cs85gs(t4, t5);
        gm_l1 = t6;
        uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
        if (t7) {
        } else {
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_5823bl2 *)((&F.h)->up))->l2), ((uint8_t)1));
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_802et9b(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_802et9b F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 47u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l1;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_ln6s15q *)((&F.h)->up))->l2));
    uint8_t t3 = (uint8_t)!t2;
    if (t3) {
        void * t4 = (void *)((uint8_t *)&((struct gm_fr_format_ln6s15q *)((&F.h)->up))->l1);
        gm_b16_8 t5 = gm_l0;
        gm_tftb1odc739t5 t6 = gm_f_extend_0cs85gs(t4, t5);
        gm_l1 = t6;
        uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
        if (t7) {
        } else {
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_ln6s15q *)((&F.h)->up))->l2), ((uint8_t)1));
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ko2ljtu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ko2ljtu F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 47u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l1;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_l93mv4o *)((&F.h)->up))->l2));
    uint8_t t3 = (uint8_t)!t2;
    if (t3) {
        void * t4 = (void *)((uint8_t *)&((struct gm_fr_format_l93mv4o *)((&F.h)->up))->l1);
        gm_b16_8 t5 = gm_l0;
        gm_tftb1odc739t5 t6 = gm_f_extend_0cs85gs(t4, t5);
        gm_l1 = t6;
        uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
        if (t7) {
        } else {
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_l93mv4o *)((&F.h)->up))->l2), ((uint8_t)1));
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_o51ej4r(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_o51ej4r F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 47u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l1;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_lo2svpj *)((&F.h)->up))->l2));
    uint8_t t3 = (uint8_t)!t2;
    if (t3) {
        void * t4 = (void *)((uint8_t *)&((struct gm_fr_format_lo2svpj *)((&F.h)->up))->l1);
        gm_b16_8 t5 = gm_l0;
        gm_tftb1odc739t5 t6 = gm_f_extend_0cs85gs(t4, t5);
        gm_l1 = t6;
        uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
        if (t7) {
        } else {
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_lo2svpj *)((&F.h)->up))->l2), ((uint8_t)1));
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_format_5029eea(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_5029eea F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sl5h9isb3bijl));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(4));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_74pd3in((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplei8v65ors6jc), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_506scju(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_506scju F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sq58pm227uuk8));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(52));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpljcso3h5g4o29), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_51h4888(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_51h4888 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sjc9ao9066c7n));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(32));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplj7j0vuugbh2u), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_535vc4d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_535vc4d F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5j2jdmako4im));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(36));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplopbg40ai4id4), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_53mv29j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_53mv29j F;
    GM_STACK_CHECK(&F, 176u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sd2ioiubqiqs5));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(45));
    gm_tk6rvhh54g86p t3 = GM_LD(gm_tk6rvhh54g86p, ((uint8_t *)a0));
    gm_f_format_abcvqrv((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplck20jih4okbk), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_5496f10(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_5496f10 F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_stcivomuinkhh));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(20));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl265m7jpg5oed), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_54ifjd1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_54ifjd1 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_seplhq3i7l38b));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(194));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpllovel7a2ips1), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_54mn8jj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_54mn8jj F;
    gm_t3877pk7lv748 gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s71434ub9jp42));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(10));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_2kt92vg((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpljkde9cibn2er), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_554rdam(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t19enj90vg8bd a0) {
    struct gm_fr_format_554rdam F;
    gm_t19enj90vg8bd gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t19enj90vg8bd t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sfqr2pc72qt8e));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(22));
    gm_f_format_sc59up9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpln2i759e19cgt), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_556kaa4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_556kaa4 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s589bsqckia3s));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl53ihvkdedd31), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_55igra0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_55igra0 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s41sp7ht9l505));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(66));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl2d7ij9b2sa2u), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_570tujl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_570tujl F;
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s2r03t2n892pm));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(26));
    gm_t0uuhaljsf410 t3 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
    gm_f_format_rqlkk21((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplnr2qhie4t5ta), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_57u5svl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_57u5svl F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssa5e93p23oqi));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(89));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl6lpvgmmqlalm), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_5a05gmg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_5a05gmg F;
    gm_tommf4j9upomc gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_svghqphscbkj1));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    gm_tommf4j9upomc t3 = GM_LD(gm_tommf4j9upomc, ((uint8_t *)a0));
    gm_f_format_dq6oc41((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplagt25ql42kpb), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_5au36p4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_5au36p4 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sekb1d43i0ft2));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(9));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl81uel28fst2a), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_5b908c8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_5b908c8 F;
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sgme8q1t414rd));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(18));
    gm_tud7oovfvnf43 t3 = GM_LD(gm_tud7oovfvnf43, ((uint8_t *)a0));
    gm_f_format_65ee4ra((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl7bbfeces83t7), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_5bcbbmm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tb31k5l794t7p a0) {
    struct gm_fr_format_5bcbbmm F;
    gm_tb31k5l794t7p gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tb31k5l794t7p t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sfhct1r6m1qqo));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
    gm_f_format_2eptqlp((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl7jau285qbbrq), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_5bo63p7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_5bo63p7 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjrtlfuv3os00));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(74));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplqnufra7h2p1s), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_5f2vpdc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_5f2vpdc F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sopln0aep28du));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(46));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpluc8r0n3peqad), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_5htdr0o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_5htdr0o F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s1ti14uv3rrcf));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(64));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplgemvjdfjljaa), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_5iu9hua(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_5iu9hua F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sebk4v8jac58q));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(42));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl9d622ubds6jn), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_5j9lqr8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_5j9lqr8 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sn9b7u92b8vpk));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(14));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_01lon97((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplp7faum694g3u), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_5k1v0hr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_5k1v0hr F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4ce0jd3tdro4 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_savtbnmf9bjnr));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(16));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplvle6f1rmlmjh), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_5k9iv40(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_5k9iv40 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s8n858pjjep3e));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(9));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpla843kfhqtvit), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_5lfjq97(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_5lfjq97 F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sp1u9v27v4c4d));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(23));
    gm_tvi24gafvcime t3 = GM_LD(gm_tvi24gafvcime, ((uint8_t *)a0));
    gm_f_format_otin3sf((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplp0abp7005sqm), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_5lk0c41(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_5lk0c41 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s658vrmfgnecg));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(27));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplemu1bmibr9ve), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_5m6a3eh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_5m6a3eh F;
    gm_tommf4j9upomc gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_snm19vfbec4q3));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(18));
    gm_tommf4j9upomc t3 = GM_LD(gm_tommf4j9upomc, ((uint8_t *)a0));
    gm_f_format_dq6oc41((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpllsre3itruefn), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_5mel2nn(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_5mel2nn F;
    gm_tgbgti82mf879 gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sac01n562d8ve));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(58));
    gm_tgbgti82mf879 t3 = GM_LD(gm_tgbgti82mf879, ((uint8_t *)a0));
    gm_f_format_vepf78n((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl29pahnav2feq), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_5mja5ge(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0) {
    struct gm_fr_format_5mja5ge F;
    gm_t9qkh0rsooc8d gm_l0; /* args */
    GM_STACK_CHECK(&F, 18u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9qkh0rsooc8d t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sidgpvfsv94q1));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(55));
    gm_f_format_c97t5s6((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpludgprff7jihr), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_5nk8afu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_5nk8afu F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s42b9o00en8pl));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(33));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplft0ecs37svcn), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_5p0qm94(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_5p0qm94 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sl84tf4h2e7v7));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(28));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl32o7ct8vibbi), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_5pe4c2v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_5pe4c2v F;
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sqk2n9kn6vnik));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(24));
    gm_tud7oovfvnf43 t3 = GM_LD(gm_tud7oovfvnf43, ((uint8_t *)a0));
    gm_f_format_65ee4ra((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplgp08d1iih1si), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_5pl0mkc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_5pl0mkc F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sutk9gh8f6gmt));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(52));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplnr9pmjldkl35), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_5qcf83g(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_5qcf83g F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_si3s44ll3u1p2));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(41));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl58k6ai39p40h), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_5r372o6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_5r372o6 F;
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s8olahjn69at6));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(20));
    gm_t0uuhaljsf410 t3 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
    gm_f_format_rqlkk21((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplu58b6rd4mq28), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_5rrmec0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_5rrmec0 F;
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_snbn61h56c5fj));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(51));
    gm_tud7oovfvnf43 t3 = GM_LD(gm_tud7oovfvnf43, ((uint8_t *)a0));
    gm_f_format_ee7lhc3((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplpkvu14cg936d), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_5tk5m6v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_5tk5m6v F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4ce0jd3tdro4 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_svqapkbdfenmd));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(53));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl7umtk166uvmr), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_5up8eqa(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_5up8eqa F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s4r2qm241merr));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(11));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplja7aur1oiv70), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_5uun4vl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_5uun4vl F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s46khuklhbvqd));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(20));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl1lvejpvpq0qh), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_5v26vgi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_5v26vgi F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1ctgkvj02ag0));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(33));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl3sv6aqbksrr5), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_l002mid(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_l002mid F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sglgjrjci6t1d));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(24));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplg575kk57c4i8), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_l035o11(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_l035o11 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2jmrvqu8r8aq));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(57));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplv8s8fgl2lggu), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_l0aimgq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_l0aimgq F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_su6il7ugmnq2g));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(47));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpld3o5iqghffqq), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_l0bpsnf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_l0bpsnf F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqmpncmbmfe90));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplnndmi09f8nur), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_l0gkulr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_l0gkulr F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_so52klh2mgoce));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(22));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplh7ffbsfnitkh), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_l0pq86n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_l0pq86n F;
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sv8qamggba7e1));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(17));
    gm_t0uuhaljsf410 t3 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
    gm_f_format_rqlkk21((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplv1i7e9gejv6a), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_l1ii34d(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_l1ii34d F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sd667aplubiqu));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(33));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplc5cre0uqposd), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_l21cpr6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_l21cpr6 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_svkrg5dctlh9m));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl7kgo5i3aumd2), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_l251doo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_l251doo F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sg3a8rmfq3j12));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(49));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpli8blk453jgvg), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_l2fmok5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_l2fmok5 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqn4pcenn3reh));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(62));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl6boglt2rkmh4), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_l53b7vj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_l53b7vj F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sb8qu1qqed6pt));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(84));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpllm39516ci1mu), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_l5e62u0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_l5e62u0 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sm6jfloe4be09));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(26));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplj0vaqlfn7orc), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_l7c15sv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0) {
    struct gm_fr_format_l7c15sv F;
    gm_t2lqu8jsj511d gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t2lqu8jsj511d t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_su88ia0hdam4d));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(65));
    gm_f_format_ig8f8pl((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplmm8hld32929h), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_l7hthr8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_l7hthr8 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3fj8brek97co));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(85));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl6u21hsinpli4), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_l7u3cq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_l7u3cq2 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sovg8lhrcqaa0));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(33));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplgcrr0tu9874k), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_l821hfj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_l821hfj F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sqlpqagi4dpk1));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(37));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl54tnh29jjfju), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_l9df9v3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_l9df9v3 F;
    GM_STACK_CHECK(&F, 128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s5cbc2v9e254r));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(36));
    gm_th2d9c7aju8hp t3 = GM_LD(gm_th2d9c7aju8hp, ((uint8_t *)a0));
    gm_f_format_q4qso5a((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplcb1495nujsb7), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_la70vrc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_la70vrc F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slk5snj328gko));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(44));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplj451rojbvr15), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_lac0oi2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_lac0oi2 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9hqndl9se5nc));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(30));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl00nmcu166crh), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_las1g7o(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_las1g7o F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sq2es9bij87b7));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(13));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl9iaqf5bbsv8o), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_lb0inss(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_lb0inss F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_stocunr1upos5));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl1f7c1o805fpf), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_lbar0n6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_lbar0n6 F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_shmsiqao0jnvn));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(41));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tple7msnncsq0hr), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_lc07prd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_lc07prd F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s0uum3co6vapt));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(22));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl9vu8tqcv1d9s), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_lc73epv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_lc73epv F;
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s5u07ahpa78ud));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(26));
    gm_tif3elqk01bdl t3 = GM_LD(gm_tif3elqk01bdl, ((uint8_t *)a0));
    gm_f_format_6hrn7j8((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl7dh44jmr11le), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_lcm4k53(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_lcm4k53 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_siq422vplpm2h));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(53));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl6104ee3qfshr), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_lde77u1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_lde77u1 F;
    gm_tiilmfd16v3fc gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_soi6iv1hnej40));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(12));
    gm_tiilmfd16v3fc t3 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)a0));
    gm_f_format_rc5a5cm((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpljnlsd7076tqd), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_lderfbr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_lderfbr F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s87g1ufoaqi74));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpln7ptjfs3pum5), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ldnd7h1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ldnd7h1 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sb7jq1p8shp96));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(112));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplj0t0s73b2v4j), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_le00do2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_le00do2 F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sl9i9hcgkt8fd));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(96));
    gm_thddbqb6snabm t3 = GM_LD(gm_thddbqb6snabm, ((uint8_t *)a0));
    gm_f_format_3tlgo2e((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpla5rqmr49dnnk), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_lefnhqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_lefnhqh F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skvqkn3gte5mk));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplpj1vfgivuj13), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_les73rv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_les73rv F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sfl0m0vifcu3m));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(81));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl1nqa0en23gvp), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_lfjb9aq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_lfjb9aq F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ss3lpq4se4ir5));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplg05c24rh8peq), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_lflgtrt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_lflgtrt F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_spbvs3aa34vln));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(14));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl23mh8eh9cef6), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_lhid49k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_lhid49k F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s95cbsclgo7gk));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(35));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplh05i67kfcnal), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_li1975k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_li1975k F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s7aoh8j3iph16));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(50));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplcpt7hf5iscqd), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_licpkoa(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_licpkoa F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_svkrg5dctlh9m));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_01lon97((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl7kgo5i3aumd2), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ljka1bn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tjj2cncin95t1 a0) {
    struct gm_fr_format_ljka1bn F;
    gm_tjj2cncin95t1 gm_l0; /* args */
    GM_STACK_CHECK(&F, 18u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tjj2cncin95t1 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4pru1na1k33s));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(2));
    gm_f_format_ilrvlso((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplk34ojqiaho84), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ljsk7ji(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ljsk7ji F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sp7351fukevmv));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(28));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpldsui47p71ms3), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_lk5ln6l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a0) {
    struct gm_fr_format_lk5ln6l F;
    gm_tm98ap4112hee gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tm98ap4112hee t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2dog0ijtvmd2));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(31));
    gm_f_format_eara94q((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplc47d0vivqs48), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_lkeo172(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0) {
    struct gm_fr_format_lkeo172 F;
    gm_t9qkh0rsooc8d gm_l0; /* args */
    GM_STACK_CHECK(&F, 18u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9qkh0rsooc8d t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sc3pq8972q5i0));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(14));
    gm_f_format_c97t5s6((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplcb5rgmp3lpr2), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ll295o3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ll295o3 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skbosgm06mmhc));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(56));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplo203818ggfa8), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_llf38gp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_llf38gp F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_ssu2vb7lj6d81));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(20));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplkaran1m5lpqm), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_llhsr0b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0) {
    struct gm_fr_format_llhsr0b F;
    gm_t9qkh0rsooc8d gm_l0; /* args */
    GM_STACK_CHECK(&F, 18u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9qkh0rsooc8d t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_spuk9sqnoo3ai));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(12));
    gm_f_format_0dva8tv((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplcilp3mvhfo4n), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_loa1toi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_loa1toi F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sgkuqlapm8vur));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(11));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplnd5opdv87prb), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_loh175m(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_loh175m F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqqlqus41v6vq));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(11));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplm9hj31tk9ip4), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_lovbvbi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_lovbvbi F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sqg1k42secsds));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(28));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplg21rd4as3ph6), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_lpj4rro(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_lpj4rro F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sojpmv6gl0jac));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(11));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tple87ca4est7gh), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_lpm1a8i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_lpm1a8i F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sc9hsbhe0l538));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplrsdjn4cifhr9), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_lqnh3lt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_lqnh3lt F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sv6dkmq38siqt));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(26));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpllmofqkiu75lh), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_lrgko01(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_lrgko01 F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4ce0jd3tdro4 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssao50s66pj11));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_lo2svpj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplrkvt0j8tqlqg), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_lsmcccn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_lsmcccn F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_si731m8b0sr6s));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpla1fscog446dv), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ltbcfgo(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ltbcfgo F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sd5iqorsg0rc4));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(72));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpln65gnioheijm), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ltp901v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ltp901v F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbj6flp3i82cp));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(31));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplkik9smkp1eb6), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_lu3aj0k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_lu3aj0k F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sd9r0l17bbuoe));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplvpehcchgo971), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_lu93b1k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_lu93b1k F;
    gm_tommf4j9upomc gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sso7iusdc4q0g));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(10));
    gm_tommf4j9upomc t3 = GM_LD(gm_tommf4j9upomc, ((uint8_t *)a0));
    gm_f_format_dq6oc41((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl5uko75p49g56), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_lufljqc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_lufljqc F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_su43dt5mihp55));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(21));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplr7pcqj5621b9), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_lveqqdp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_lveqqdp F;
    gm_t3877pk7lv748 gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s9ap8gk0oj12e));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(72));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_2kt92vg((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplggnuionimjk9), t2);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_reserve_d1qo8mf(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_reserve_d1qo8mf F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK(&F, 137u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    gm_l2 = t3;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 16));
    uint64_t t6 = gm_l2;
    uint64_t r7; bool o8 = __builtin_sub_overflow(t5, t6, &r7);
    if (o8) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 18, 8);
    uint64_t t9 = r7;
    uint64_t t10 = gm_l1;
    uint8_t t11 = (t9 >= t10);
    if (t11) {
        gm_tftb1odc739t5 t12;
        GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
        ((uint8_t *)&t12)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t12 + 1), GM_ZERO(gm_b0_1));
        F.ret = t12;
        goto gm_fret;
    }
    void * t13 = gm_l0;
    uint64_t t14 = gm_l1;
    gm_b16_8 t15 = gm_f_empty_slice_ijtp9lo();
    gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
    gm_tftb1odc739t5 t17 = gm_f_regrow_3cp9o8d(t13, t14, t16);
    F.ret = t17;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1trbst9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_802et9b(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ko2ljtu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_o51ej4r(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) void gm_f_format_5029eea(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_506scju(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_51h4888(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_535vc4d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_53mv29j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_5496f10(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
extern __attribute__((aligned(4))) void gm_f_format_54ifjd1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_54mn8jj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_554rdam(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t19enj90vg8bd a0);
extern __attribute__((aligned(4))) void gm_f_format_556kaa4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_55igra0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_560qv19(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_570tujl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_57u5svl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_5823bl2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_format_5823bl2 F;
    void * gm_l4; /* template */
    gm_b16_8 gm_l5; /* template_text */
    GM_STACK_CHECK(&F, 240u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l4 = a1;
    gm_l5 = a2;
    gm_tftb1odc739t5 gm_l6;
    {
        gm_trqfvmjtt4sij t2;
        gm_f_list_uvfu1k1((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t2;
    }
    void * t3 = (void *)((uint8_t *)&F.l1);
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
    uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)72ULL), &r5);
    if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
    uint64_t t7 = r5;
    gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t3, t7);
    gm_l6 = t8;
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (t9) {
    } else {
        gm_t9qkh0rsooc8d t10;
        GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8), t10);
        goto gm_fdone;
    }
    F.l2 = ((uint8_t)0);
    {
        memmove(((uint8_t *)&F.l3), ((uint8_t *)a0), 72);
    }
    gm_closure t11 = ((gm_closure){(void *)gm_f_fn_1trbst9, (void *)&F.h, cx});
    void * t12 = gm_l4;
    gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 16));
    gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 24));
    gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 40));
    gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 56));
    const gm_tmpl *tp18 = (const gm_tmpl *)(t12);
    const void *vp18[] = {(const void *)(((uint8_t *)&t13)), (const void *)(((uint8_t *)&t14)), (const void *)(((uint8_t *)&t15)), (const void *)(((uint8_t *)&t16)), (const void *)(((uint8_t *)&t17))};
    const gm_type *vd18[] = {&gm_ty0iqiubfm7e, &gm_ty6groq37ram, &gm_ty0iqiubfm7e, &gm_ty0iqiubfm7e, &gm_ty0iqiubfm7e};
    static const uint8_t vs18[] = {1, 0, 1, 1, 1};
    gm_closure t19 = t11;
    gm_node *ccx20 = t19.cx;
    for (uint32_t k18 = 0;; k18++) {
        if ((tp18->text[k18].len != 0 && gm_pf0aqhubr4up(t19.code, t19.env, ccx20, tp18->text[k18].ptr, tp18->text[k18].len))) {
            goto gm_lad;
        }
        if (k18 == tp18->n) break;
        uint32_t j18 = tp18->arg[k18];
        if (((!tp18->fmt[k18] && vs18[j18]) ? (GM_LD(uintptr_t, (const uint8_t *)vp18[j18] + 8) != 0 && gm_pf0aqhubr4up(t19.code, t19.env, ccx20, GM_LD(void *, vp18[j18]), GM_LD(uintptr_t, (const uint8_t *)vp18[j18] + 8))) : gm_pfk8csn7us23(t19.code, t19.env, ccx20, vd18[j18], vp18[j18], tp18->fmt[k18]))) {
            goto gm_lad;
        }
    }
    uint8_t t21 = F.l2;
    if (t21) {
        void * t22 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_r70pji5(t22);
        gm_t9qkh0rsooc8d t23;
        GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8), t23);
        goto gm_fdone;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
extern __attribute__((aligned(4))) void gm_f_format_5a05gmg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_5au36p4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_5b908c8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_5bcbbmm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tb31k5l794t7p a0);
extern __attribute__((aligned(4))) void gm_f_format_5bo63p7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_5erjuiq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_5f2vpdc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_5htdr0o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_5iu9hua(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_5j9lqr8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_5k1v0hr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
extern __attribute__((aligned(4))) void gm_f_format_5k9iv40(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_5lfjq97(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_5lk0c41(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_5m6a3eh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_5mel2nn(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_5mja5ge(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
extern __attribute__((aligned(4))) void gm_f_format_5nk8afu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_5p0qm94(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_5pe4c2v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_5pl0mkc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_5qcf83g(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_5r372o6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_5rrmec0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_5tk5m6v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
extern __attribute__((aligned(4))) void gm_f_format_5up8eqa(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_5uun4vl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_5v26vgi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_l002mid(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_l035o11(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_l05am3t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
extern __attribute__((aligned(4))) void gm_f_format_l0aimgq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_l0bpsnf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_l0gkulr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_l0pq86n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_l1ii34d(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_l21cpr6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_l251doo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_l2fmok5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_l53b7vj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_l5e62u0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_l7c15sv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
extern __attribute__((aligned(4))) void gm_f_format_l7hthr8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_l7u3cq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_l821hfj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_l93mv4o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_format_l93mv4o F;
    void * gm_l4; /* template */
    gm_b16_8 gm_l5; /* template_text */
    GM_STACK_CHECK(&F, 236u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l4 = a1;
    gm_l5 = a2;
    gm_tftb1odc739t5 gm_l6;
    {
        gm_trqfvmjtt4sij t2;
        gm_f_list_uvfu1k1((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t2;
    }
    void * t3 = (void *)((uint8_t *)&F.l1);
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
    uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)72ULL), &r5);
    if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
    uint64_t t7 = r5;
    gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t3, t7);
    gm_l6 = t8;
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (t9) {
    } else {
        gm_t9qkh0rsooc8d t10;
        GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8), t10);
        goto gm_fdone;
    }
    F.l2 = ((uint8_t)0);
    {
        memmove(((uint8_t *)&F.l3), ((uint8_t *)a0), 72);
    }
    gm_closure t11 = ((gm_closure){(void *)gm_f_fn_ko2ljtu, (void *)&F.h, cx});
    void * t12 = gm_l4;
    gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
    gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
    uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)&F.l3) + 48));
    gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 56));
    const gm_tmpl *tp18 = (const gm_tmpl *)(t12);
    const void *vp18[] = {(const void *)(((uint8_t *)&t13)), (const void *)(((uint8_t *)&t14)), (const void *)(((uint8_t *)&t15)), (const void *)(((uint8_t *)&t16)), (const void *)(((uint8_t *)&t17))};
    const gm_type *vd18[] = {&gm_ty0iqiubfm7e, &gm_ty0iqiubfm7e, &gm_ty0iqiubfm7e, &gm_ty467d5cm1d5, &gm_ty0iqiubfm7e};
    static const uint8_t vs18[] = {1, 1, 1, 0, 1};
    gm_closure t19 = t11;
    gm_node *ccx20 = t19.cx;
    for (uint32_t k18 = 0;; k18++) {
        if ((tp18->text[k18].len != 0 && gm_pf0aqhubr4up(t19.code, t19.env, ccx20, tp18->text[k18].ptr, tp18->text[k18].len))) {
            goto gm_lad;
        }
        if (k18 == tp18->n) break;
        uint32_t j18 = tp18->arg[k18];
        if (((!tp18->fmt[k18] && vs18[j18]) ? (GM_LD(uintptr_t, (const uint8_t *)vp18[j18] + 8) != 0 && gm_pf0aqhubr4up(t19.code, t19.env, ccx20, GM_LD(void *, vp18[j18]), GM_LD(uintptr_t, (const uint8_t *)vp18[j18] + 8))) : gm_pfk8csn7us23(t19.code, t19.env, ccx20, vd18[j18], vp18[j18], tp18->fmt[k18]))) {
            goto gm_lad;
        }
    }
    uint8_t t21 = F.l2;
    if (t21) {
        void * t22 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_r70pji5(t22);
        gm_t9qkh0rsooc8d t23;
        GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8), t23);
        goto gm_fdone;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
extern __attribute__((aligned(4))) void gm_f_format_l9df9v3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_la70vrc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
extern __attribute__((aligned(4))) void gm_f_format_lac0oi2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_las1g7o(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_lb0inss(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_lbar0n6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_lc07prd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_lc73epv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_lcm4k53(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_lde77u1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_lderfbr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_ldnd7h1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_le00do2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_lefnhqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
extern __attribute__((aligned(4))) void gm_f_format_les73rv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_lfjb9aq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_lflgtrt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_lhid49k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_li1975k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_licpkoa(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_ljka1bn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tjj2cncin95t1 a0);
extern __attribute__((aligned(4))) void gm_f_format_ljsk7ji(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_lk5ln6l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a0);
extern __attribute__((aligned(4))) void gm_f_format_lkeo172(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
extern __attribute__((aligned(4))) void gm_f_format_ll295o3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_llf38gp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_llhsr0b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) void gm_f_format_ln6s15q(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_format_ln6s15q F;
    void * gm_l4; /* template */
    gm_b16_8 gm_l5; /* template_text */
    GM_STACK_CHECK(&F, 204u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l4 = a1;
    gm_l5 = a2;
    gm_tftb1odc739t5 gm_l6;
    {
        gm_trqfvmjtt4sij t2;
        gm_f_list_uvfu1k1((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t2;
    }
    void * t3 = (void *)((uint8_t *)&F.l1);
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
    uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)40ULL), &r5);
    if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
    uint64_t t7 = r5;
    gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t3, t7);
    gm_l6 = t8;
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (t9) {
    } else {
        gm_t9qkh0rsooc8d t10;
        GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8), t10);
        goto gm_fdone;
    }
    F.l2 = ((uint8_t)0);
    {
        memmove(((uint8_t *)&F.l3), ((uint8_t *)a0), 40);
    }
    gm_closure t11 = ((gm_closure){(void *)gm_f_fn_802et9b, (void *)&F.h, cx});
    void * t12 = gm_l4;
    int32_t t13 = GM_LD(int32_t, ((uint8_t *)&F.l3));
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
    gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
    uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 32));
    const gm_tmpl *tp17 = (const gm_tmpl *)(t12);
    const void *vp17[] = {(const void *)(((uint8_t *)&t13)), (const void *)(((uint8_t *)&t14)), (const void *)(((uint8_t *)&t15)), (const void *)(((uint8_t *)&t16))};
    const gm_type *vd17[] = {&gm_tyqa6gfa8omk, &gm_tyb2nno7n67r, &gm_ty0iqiubfm7e, &gm_tyb2nno7n67r};
    static const uint8_t vs17[] = {0, 0, 1, 0};
    gm_closure t18 = t11;
    gm_node *ccx19 = t18.cx;
    for (uint32_t k17 = 0;; k17++) {
        if ((tp17->text[k17].len != 0 && gm_pf0aqhubr4up(t18.code, t18.env, ccx19, tp17->text[k17].ptr, tp17->text[k17].len))) {
            goto gm_lad;
        }
        if (k17 == tp17->n) break;
        uint32_t j17 = tp17->arg[k17];
        if (((!tp17->fmt[k17] && vs17[j17]) ? (GM_LD(uintptr_t, (const uint8_t *)vp17[j17] + 8) != 0 && gm_pf0aqhubr4up(t18.code, t18.env, ccx19, GM_LD(void *, vp17[j17]), GM_LD(uintptr_t, (const uint8_t *)vp17[j17] + 8))) : gm_pfk8csn7us23(t18.code, t18.env, ccx19, vd17[j17], vp17[j17], tp17->fmt[k17]))) {
            goto gm_lad;
        }
    }
    uint8_t t20 = F.l2;
    if (t20) {
        void * t21 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_r70pji5(t21);
        gm_t9qkh0rsooc8d t22;
        GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8), t22);
        goto gm_fdone;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_lo2svpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_format_lo2svpj F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    void * gm_l4; /* template */
    gm_b16_8 gm_l5; /* template_text */
    GM_STACK_CHECK(&F, 200u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l4 = a1;
    gm_l5 = a2;
    gm_tftb1odc739t5 gm_l6;
    {
        gm_trqfvmjtt4sij t2;
        gm_f_list_uvfu1k1((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t2;
    }
    void * t3 = (void *)((uint8_t *)&F.l1);
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
    uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)8ULL), &r5);
    if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
    uint64_t t7 = r5;
    gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t3, t7);
    gm_l6 = t8;
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (t9) {
    } else {
        gm_t9qkh0rsooc8d t10;
        GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8), t10);
        goto gm_fdone;
    }
    F.l2 = ((uint8_t)0);
    gm_t4ce0jd3tdro4 t11 = gm_l0;
    gm_l3 = t11;
    gm_closure t12 = ((gm_closure){(void *)gm_f_fn_o51ej4r, (void *)&F.h, cx});
    void * t13 = gm_l4;
    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
    const gm_tmpl *tp15 = (const gm_tmpl *)(t13);
    const void *vp15[] = {(const void *)(((uint8_t *)&t14))};
    const gm_type *vd15[] = {&gm_ty6groq37ram};
    static const uint8_t vs15[] = {0};
    gm_closure t16 = t12;
    gm_node *ccx17 = t16.cx;
    for (uint32_t k15 = 0;; k15++) {
        if ((tp15->text[k15].len != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, tp15->text[k15].ptr, tp15->text[k15].len))) {
            goto gm_lad;
        }
        if (k15 == tp15->n) break;
        uint32_t j15 = tp15->arg[k15];
        if (((!tp15->fmt[k15] && vs15[j15]) ? (GM_LD(uintptr_t, (const uint8_t *)vp15[j15] + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, vp15[j15]), GM_LD(uintptr_t, (const uint8_t *)vp15[j15] + 8))) : gm_pfk8csn7us23(t16.code, t16.env, ccx17, vd15[j15], vp15[j15], tp15->fmt[k15]))) {
            goto gm_lad;
        }
    }
    uint8_t t18 = F.l2;
    if (t18) {
        void * t19 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_r70pji5(t19);
        gm_t9qkh0rsooc8d t20;
        GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8), t20);
        goto gm_fdone;
    }
    {
        void * t21 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t21);
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
extern __attribute__((aligned(4))) void gm_f_format_loa1toi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_loh175m(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_lovbvbi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_lpj4rro(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_lpm1a8i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_lqnh3lt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_lrgko01(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
extern __attribute__((aligned(4))) void gm_f_format_lsckir6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_lsmcccn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_ltbcfgo(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_ltp901v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_lu3aj0k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
extern __attribute__((aligned(4))) void gm_f_format_lu93b1k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_lufljqc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_format_lveqqdp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_sign_50jle9l(void *gm_out, gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sign_50jle9l F;
    gm_b16_8 gm_l0; /* s */
    uint32_t gm_l1; /* zero */
    uint8_t gm_l2; /* signed */
    GM_STACK_CHECK_SHALLOW(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = ((uint32_t)0ULL);
    uint32_t t2 = gm_l1;
    uint32_t r3; bool o4 = __builtin_sub_overflow(t2, ((uint32_t)1ULL), &r3);
    uint32_t t5 = r3;
    uint32_t t6 = gm_l1;
    uint8_t t7 = (t5 < t6);
    gm_l2 = t7;
    void * t8 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t9)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t9, gm_src3d0v42kavhv7, 318, 8);
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)t8) + (uintptr_t)((uint64_t)0ULL) * 1));
    uint8_t t11 = ((t10 == ((uint8_t)45ULL)));
    if (t11) {
        uint8_t t12 = gm_l2;
        uint8_t t13 = (uint8_t)!t12;
        if (t13) {
            gm_t4ce0jd3tdro4 t14;
            GM_ST(uint64_t, ((uint8_t *)&t14 + 0), ((uint64_t)0ULL));
            gm_ttiiv5vhiuvvr t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t4ce0jd3tdro4, ((uint8_t *)&t15 + 8), t14);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t4mjne9lcbiv4 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t4mjne9lcbiv4 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t4mjne9lcbiv4 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t4mjne9lcbiv4 *)gm_out))[0] = 1;
            GM_ST(gm_ttiiv5vhiuvvr, ((uint8_t *)&(*(gm_t4mjne9lcbiv4 *)gm_out) + 8), t15);
            goto gm_fdone;
        }
        gm_tphtgnn9vgkcv t16;
        GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t16 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t16 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)1));
        GM_ST(uint64_t, ((uint8_t *)&t16 + 8), ((uint64_t)1ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t4mjne9lcbiv4 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t4mjne9lcbiv4 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t4mjne9lcbiv4 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t4mjne9lcbiv4 *)gm_out))[0] = 0;
        GM_ST(gm_tphtgnn9vgkcv, ((uint8_t *)&(*(gm_t4mjne9lcbiv4 *)gm_out) + 8), t16);
        goto gm_fdone;
    }
    void * t17 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t18)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t18, gm_src3d0v42kavhv7, 322, 8);
    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)t17) + (uintptr_t)((uint64_t)0ULL) * 1));
    uint8_t t20 = ((t19 == ((uint8_t)43ULL)));
    if (t20) {
        gm_tphtgnn9vgkcv t21;
        GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)0));
        GM_ST(uint64_t, ((uint8_t *)&t21 + 8), ((uint64_t)1ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t4mjne9lcbiv4 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t4mjne9lcbiv4 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t4mjne9lcbiv4 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t4mjne9lcbiv4 *)gm_out))[0] = 0;
        GM_ST(gm_tphtgnn9vgkcv, ((uint8_t *)&(*(gm_t4mjne9lcbiv4 *)gm_out) + 8), t21);
        goto gm_fdone;
    }
    gm_tphtgnn9vgkcv t22;
    GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0));
    GM_ST(uint64_t, ((uint8_t *)&t22 + 8), ((uint64_t)0ULL));
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t4mjne9lcbiv4 *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_t4mjne9lcbiv4 *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_t4mjne9lcbiv4 *)gm_out) + 4, 0);
    ((uint8_t *)&(*(gm_t4mjne9lcbiv4 *)gm_out))[0] = 0;
    GM_ST(gm_tphtgnn9vgkcv, ((uint8_t *)&(*(gm_t4mjne9lcbiv4 *)gm_out) + 8), t22);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t4mjne9lcbiv4 gm_f_sign_50jle9l_c(gm_b16_8 a0) { gm_t4mjne9lcbiv4 gm_r; gm_f_sign_50jle9l(&gm_r, a0); return gm_r; }

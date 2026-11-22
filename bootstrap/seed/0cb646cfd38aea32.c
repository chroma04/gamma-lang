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
typedef struct gm_cs2lqoac0a5ng4 { _Bool f0; uint32_t f1; uint64_t f2; } gm_cs2lqoac0a5ng4;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { unsigned __int128 f0; unsigned __int128 f1; } gm_t793vlgpa3b88;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; uint64_t f2; } gm_tuglhjjmj2g58;
_Static_assert(sizeof(gm_cs2lqoac0a5ng4) == 16 && _Alignof(gm_cs2lqoac0a5ng4) == 8 && offsetof(gm_cs2lqoac0a5ng4, f0) == 0 && offsetof(gm_cs2lqoac0a5ng4, f1) == 4 && offsetof(gm_cs2lqoac0a5ng4, f2) == 8, "layout of `IntInfo`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuglhjjmj2g58 v0; uint16_t v1; } f1; } gm_t5v8n6qrn4rh4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t793vlgpa3b88) == 32 && _Alignof(gm_t793vlgpa3b88) == 16 && offsetof(gm_t793vlgpa3b88, f0) == 0 && offsetof(gm_t793vlgpa3b88, f1) == 16, "layout of `(lo: u128, hi: u128)`");
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint64_t f3; uint64_t f4; } gm_tg23pq9a29gh1;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; } gm_th2d9c7aju8hp;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; } gm_tif3elqk01bdl;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint32_t f3; uint8_t pe[4]; } gm_tj4tbafenpsjm;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t p2[4]; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tp6khiphboddr;
typedef struct { gm_tuglhjjmj2g58 f0; uint8_t f1; uint8_t pe[7]; } gm_tqcc6dmlne11b;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
_Static_assert(sizeof(gm_tuglhjjmj2g58) == 16 && _Alignof(gm_tuglhjjmj2g58) == 8 && offsetof(gm_tuglhjjmj2g58, f0) == 0 && offsetof(gm_tuglhjjmj2g58, f1) == 4 && offsetof(gm_tuglhjjmj2g58, f2) == 8, "layout of `(signed: bool, bits: u32, storage: u64)`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_b16_8 v1; } f1; } gm_t5o0tpv5jka2l;
_Static_assert(sizeof(gm_t5v8n6qrn4rh4) == 24 && _Alignof(gm_t5v8n6qrn4rh4) == 8 && offsetof(gm_t5v8n6qrn4rh4, f0) == 0 && offsetof(gm_t5v8n6qrn4rh4, f1) == 8, "layout of `Repr`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tg23pq9a29gh1) == 64 && _Alignof(gm_tg23pq9a29gh1) == 8 && offsetof(gm_tg23pq9a29gh1, f0) == 0 && offsetof(gm_tg23pq9a29gh1, f1) == 16 && offsetof(gm_tg23pq9a29gh1, f2) == 32 && offsetof(gm_tg23pq9a29gh1, f3) == 48 && offsetof(gm_tg23pq9a29gh1, f4) == 56, "layout of `(string, string, string, u64, u64)`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2d9c7aju8hp) == 112 && _Alignof(gm_th2d9c7aju8hp) == 8 && offsetof(gm_th2d9c7aju8hp, f0) == 0 && offsetof(gm_th2d9c7aju8hp, f1) == 16 && offsetof(gm_th2d9c7aju8hp, f2) == 32 && offsetof(gm_th2d9c7aju8hp, f3) == 48 && offsetof(gm_th2d9c7aju8hp, f4) == 64 && offsetof(gm_th2d9c7aju8hp, f5) == 80 && offsetof(gm_th2d9c7aju8hp, f6) == 96, "layout of `(string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tif3elqk01bdl) == 96 && _Alignof(gm_tif3elqk01bdl) == 8 && offsetof(gm_tif3elqk01bdl, f0) == 0 && offsetof(gm_tif3elqk01bdl, f1) == 16 && offsetof(gm_tif3elqk01bdl, f2) == 32 && offsetof(gm_tif3elqk01bdl, f3) == 48 && offsetof(gm_tif3elqk01bdl, f4) == 64 && offsetof(gm_tif3elqk01bdl, f5) == 80, "layout of `(string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tj4tbafenpsjm) == 56 && _Alignof(gm_tj4tbafenpsjm) == 8 && offsetof(gm_tj4tbafenpsjm, f0) == 0 && offsetof(gm_tj4tbafenpsjm, f1) == 16 && offsetof(gm_tj4tbafenpsjm, f2) == 32 && offsetof(gm_tj4tbafenpsjm, f3) == 48, "layout of `(string, string, string, u32)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_tp6khiphboddr) == 72 && _Alignof(gm_tp6khiphboddr) == 8 && offsetof(gm_tp6khiphboddr, f0) == 0 && offsetof(gm_tp6khiphboddr, f1) == 16 && offsetof(gm_tp6khiphboddr, f2) == 24 && offsetof(gm_tp6khiphboddr, f3) == 40 && offsetof(gm_tp6khiphboddr, f4) == 56, "layout of `([]u8, u32, []u8, string, string)`");
_Static_assert(sizeof(gm_tqcc6dmlne11b) == 24 && _Alignof(gm_tqcc6dmlne11b) == 8 && offsetof(gm_tqcc6dmlne11b, f0) == 0 && offsetof(gm_tqcc6dmlne11b, f1) == 16, "layout of `?IntInfo`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t5o0tpv5jka2l) == 40 && _Alignof(gm_t5o0tpv5jka2l) == 8 && offsetof(gm_t5o0tpv5jka2l, f0) == 0 && offsetof(gm_t5o0tpv5jka2l, f1) == 8, "layout of `Unsup!string`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
struct gm_fr_align_of_k6eqqrn {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_align_uu2harl {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_arith_fits_7degt3e {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_arith_flavor_e1m6u89 {
    gm_frame h;
    gm_taari1s0m51a7 ret;
};
struct gm_fr_arith_int_to_f64_tiud1hu {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* v */
    double ret;
};
struct gm_fr_arith_pointee_align_vqoa1mk {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_finish_int_cr0p55i {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_float_from_r6b8fu0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_format_0p6a28s {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_1ck6tsm {
    gm_frame h;
    gm_tj4tbafenpsjm l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3vtl3te {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4q9e8cc {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5pe4c2v {
    gm_frame h;
    gm_tud7oovfvnf43 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5r372o6 {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_67eb57q {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_9221qv6 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dlemvkb {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_e0d41cg {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_e6ar907 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_eb9vbgb {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fsfutor {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hb3htfv {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jagvq3q {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kr44ep8 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_las1g7o {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_loa1toi {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_mp5vmqe {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_norassj {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_o8nngn3 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_oe49e3n {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_q6prg09 {
    gm_frame h;
    gm_tud7oovfvnf43 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_sflh0jm {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tth3l1d {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_u9gq54a {
    gm_frame h;
    gm_tg23pq9a29gh1 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_uhjjs86 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_v2sfrt0 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_fresh_5kokjmg {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_ii_cty_i2b4e03 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_ii_exact_g6a19dp {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_ii_max_gtp5h90 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_ii_min_lku7pdi {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_ii_ucty_b40h5ot {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_int_binary_kp3u7i8 {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_int_info_2tcdf1n {
    gm_frame h;
    gm_tqcc6dmlne11b ret;
};
struct gm_fr_kind_ghvav22 {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_line_l6u87p9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_13gk771 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_2lhkgdf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_a8sab3a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_bq6igl9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_calnga5 {
    gm_frame h;
    gm_tif3elqk01bdl l1; /* args */
    gm_b0_1 ret;
};
struct gm_fr_linef_dpq84co {
    gm_frame h;
    gm_tfeab279c16rr l1; /* args */
    gm_b0_1 ret;
};
struct gm_fr_linef_fdhsfa1 {
    gm_frame h;
    gm_tfeab279c16rr l1; /* args */
    gm_b0_1 ret;
};
struct gm_fr_linef_g6msml8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_gh0ah67 {
    gm_frame h;
    gm_tud7oovfvnf43 l1; /* args */
    gm_b0_1 ret;
};
struct gm_fr_linef_pamii31 {
    gm_frame h;
    gm_tfeab279c16rr l1; /* args */
    gm_b0_1 ret;
};
struct gm_fr_linef_qi8elfs {
    gm_frame h;
    gm_tif3elqk01bdl l1; /* args */
    gm_b0_1 ret;
};
struct gm_fr_linef_r08knlm {
    gm_frame h;
    gm_th2d9c7aju8hp l1; /* args */
    gm_b0_1 ret;
};
struct gm_fr_linef_rf3vkph {
    gm_frame h;
    gm_tp6khiphboddr l1; /* args */
    gm_b0_1 ret;
};
struct gm_fr_linef_v6ea1vv {
    gm_frame h;
    gm_tif3elqk01bdl l1; /* args */
    gm_b0_1 ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_make_r4pbu77 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* m */
    gm_trqfvmjtt4sij l2; /* l */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_neg_agd5c5k {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_normalize_jch381e {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_t83rg2m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_shl_qqecl81 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_show_5qbi623 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_size_dmhtkib {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_size_of_k0fluj2 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_split_u128_vrmr6en {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* v */
    gm_t793vlgpa3b88 ret;
};
struct gm_fr_sub_pktm5cr {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_unary_value_0v9h22p {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_unsupported_531q51r {
    gm_frame h;
    gm_t08olq97rscn3 ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_uu2harl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_arith_fits_7degt3e(void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_arith_flavor_e1m6u89(gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) double gm_f_arith_int_to_f64_tiud1hu(const void * a0);
__attribute__((aligned(4))) double gm_f_arith_int_to_f64_tiud1hu_c(gm_tbci5ospk5nnn a0);
__attribute__((aligned(4))) uint64_t gm_f_arith_pointee_align_vqoa1mk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_binop_symbol_9ijp0eo(gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) unsigned __int128 gm_f_bits128_jigkui4(const void * a0);
__attribute__((aligned(4))) void gm_f_copy_pmkg67r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_cty_66ee0kf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_finish_int_cr0p55i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_tuglhjjmj2g58 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6, gm_b16_8 a7, gm_b16_8 a8, gm_b16_8 a9);
__attribute__((aligned(4))) gm_b16_8 gm_f_float_from_r6b8fu0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_01lon97(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_0p6a28s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_15426cv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_1ck6tsm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_3vtl3te(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_4q9e8cc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_5pe4c2v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_5r372o6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_65ee4ra(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_67eb57q(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_8ghesn9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_9221qv6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_dlemvkb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_e0d41cg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_e6ar907(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
inline __attribute__((aligned(4))) void gm_f_format_eb9vbgb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_fsfutor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_hb3htfv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ig8f8pl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_jagvq3q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_kgp4bin(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_kr44ep8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_las1g7o(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_loa1toi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_mervo2q(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_mp5vmqe(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ncqd055(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_norassj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_o8nngn3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_oe49e3n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_q6prg09(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_rjuhqv1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_rqlkk21(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_sflh0jm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
inline __attribute__((aligned(4))) void gm_f_format_tth3l1d(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_u9gq54a(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_uhjjs86(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_v2sfrt0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_5kokjmg(void * a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_ii_cty_i2b4e03(gm_cs2lqoac0a5ng4 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_ii_exact_g6a19dp(gm_cs2lqoac0a5ng4 a0);
__attribute__((aligned(4))) void gm_f_ii_max_gtp5h90(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0);
__attribute__((aligned(4))) void gm_f_ii_min_lku7pdi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_ii_ucty_b40h5ot(gm_cs2lqoac0a5ng4 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_indent_line_b3mckqa(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_int_binary_kp3u7i8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_ts6oqppjh0ba3 a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6);
__attribute__((aligned(4))) gm_b16_8 gm_f_int_cty_s62af0i(uint8_t a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_int_info_2tcdf1n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_int_lit_ai37456(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_line_l6u87p9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_13gk771(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_2iuqifd(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_2lhkgdf(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_6n9epdm(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_7afcl2l(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_8v6p2qr(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_a8sab3a(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_ao39327(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_bq6igl9(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_calnga5(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_dib3cle(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_dpq84co(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_evtoou6(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_fa323qa(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_fdhsfa1(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_g6msml8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_gh0ah67(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_m9jdfi1(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_onigi7o(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_pamii31(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_qi8elfs(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_qk177qj(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_r08knlm(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_rf3vkph(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_slpvrc7(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_v6ea1vv(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_mag_shl_5df643p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_none_v2s8s0l(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_make_r4pbu77(void *gm_out, uint8_t a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_some_9aqq928(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_neg_agd5c5k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_normalize_jch381e(void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) void gm_f_repr_53quqki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_shl_qqecl81(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_signed_add_ek0dpmo(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint8_t a0, gm_b16_8 a1, uint8_t a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_split_u128_vrmr6en(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_src_loc_lrhtj7i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) void gm_f_sub_pktm5cr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uninit_jr39s5u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uohsptq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_trim_8rdppmk(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_unary_value_0v9h22p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5);
inline __attribute__((aligned(4))) void gm_f_unsupported_531q51r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern const uint8_t gm_s02uuedsru1qu[9];
extern const uint8_t gm_s1a1r1qe3ssot[9];
extern const uint8_t gm_s1lomal6al3te[71];
extern const uint8_t gm_s1qml6papvbci[11];
extern const uint8_t gm_s22lkrbtp01c5[23];
extern const uint8_t gm_s25h5btu4ug6a[12];
extern const uint8_t gm_s28ggq4c4bg4u[32];
extern const uint8_t gm_s2cuflhqkkuaq[2];
extern const uint8_t gm_s2u5mf7vjj6ie[24];
extern const uint8_t gm_s2u5posfkbgnq[2];
extern const uint8_t gm_s4o70d89183c2[4];
extern const uint8_t gm_s619rbci7p06v[20];
extern const uint8_t gm_s8olahjn69at6[21];
extern const uint8_t gm_s946k82tp96q3[46];
extern const uint8_t gm_s9kudvjpckcsm[10];
extern const uint8_t gm_saqhrs072atsq[10];
extern const uint8_t gm_sb8b1gf0gtt49[43];
extern const uint8_t gm_sc96am1bplmoh[80];
extern const uint8_t gm_sckj2gedlq7u6[55];
extern const uint8_t gm_sd70ob31745fn[4];
extern const uint8_t gm_sdsb9longvn22[40];
extern const uint8_t gm_seak1jbr9qmd1[9];
extern const uint8_t gm_sfbb5kv3rbn0v[29];
extern const uint8_t gm_sfltedqje18on[9];
extern const uint8_t gm_sgkuqlapm8vur[12];
extern const uint8_t gm_shjbutqe8bgfp[49];
extern const uint8_t gm_sht50491g5jgu[31];
extern const uint8_t gm_si62ejtp0vbct[3];
extern const uint8_t gm_sica240oidke0[8];
extern const uint8_t gm_sicgmpca63jln[2];
extern const uint8_t gm_sidk0bupbfuvu[11];
extern const uint8_t gm_sifvl6bvepiob[9];
extern const uint8_t gm_siv1vjr1gaenb[13];
extern const uint8_t gm_sivr8g8c8o0kq[13];
extern const uint8_t gm_sjl0s4joed61f[15];
extern const uint8_t gm_sjr44k79a48mq[4];
extern const uint8_t gm_sjtp8ip8t28uh[7];
extern const uint8_t gm_sl9pujidpnv0r[4];
extern const uint8_t gm_sm89ieaqiarhc[54];
extern const uint8_t gm_smerqm8g793l6[2];
extern const uint8_t gm_smgvt83efirng[53];
extern const uint8_t gm_smpmbscbko3gk[4];
extern const uint8_t gm_snqbu1qqmr5lp[4];
extern const uint8_t gm_so43rnba8j5pb[35];
extern const uint8_t gm_sq0pl92g4hdc6[47];
extern const uint8_t gm_sq2es9bij87b7[14];
extern const uint8_t gm_sqk2n9kn6vnik[25];
extern const uint8_t gm_sr1lb1m0kmmrh[46];
extern const char gm_src0gtuh94b1fce[];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src4pc97o88riqc[];
extern const char gm_srcd5arorrf8jkg[];
extern const char gm_srcmok80ho8hh01[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srs1vrn2uaams[2];
extern const uint8_t gm_ss2v2ursd8pms[4];
extern const uint8_t gm_sso181di0dbb3[10];
extern const uint8_t gm_ssoik29mo3fp0[53];
extern const uint8_t gm_ssscpugolo2j1[37];
extern const uint8_t gm_svubtddeidu1d[2];
extern const gm_tmpl gm_tpl1f1njss89nj4;
extern const gm_tmpl gm_tpl1irt5kietk82;
extern const gm_tmpl gm_tpl1sdu3l536hkg;
extern const gm_tmpl gm_tpl2r84u6vnc58k;
extern const gm_tmpl gm_tpl324tc5bgmdkb;
extern const gm_tmpl gm_tpl3bdcsso3qmi0;
extern const gm_tmpl gm_tpl3t5edr01qp5c;
extern const gm_tmpl gm_tpl570sljg0ivt5;
extern const gm_tmpl gm_tpl5c6n9e2iidjc;
extern const gm_tmpl gm_tpl9iaqf5bbsv8o;
extern const gm_tmpl gm_tpl9j9ok1j4nn18;
extern const gm_tmpl gm_tpl9qg0hlqqke7e;
extern const gm_tmpl gm_tpl9rm8d4b25q2c;
extern const gm_tmpl gm_tplaoelja80vfud;
extern const gm_tmpl gm_tplbdrnrpalngql;
extern const gm_tmpl gm_tplc9tn3pkjdpf4;
extern const gm_tmpl gm_tplchsi5ghu9772;
extern const gm_tmpl gm_tpld9ts015fint3;
extern const gm_tmpl gm_tplda38mm1025ge;
extern const gm_tmpl gm_tplgp08d1iih1si;
extern const gm_tmpl gm_tplgursfbit7uc3;
extern const gm_tmpl gm_tpli4rfoi75onhj;
extern const gm_tmpl gm_tplio7v2odie8p1;
extern const gm_tmpl gm_tplj78420ela1cg;
extern const gm_tmpl gm_tplkkvn546crm4n;
extern const gm_tmpl gm_tplmekan10d5buf;
extern const gm_tmpl gm_tplnd5opdv87prb;
extern const gm_tmpl gm_tplnm352386bmep;
extern const gm_tmpl gm_tplo66949vs5pfo;
extern const gm_tmpl gm_tplolsc5tfmth66;
extern const gm_tmpl gm_tplplkqcb01ocrd;
extern const gm_tmpl gm_tplpv6nab1nov2l;
extern const gm_tmpl gm_tplqt9e8rihgogv;
extern const gm_tmpl gm_tpls2fio2pf4rdq;
extern const gm_tmpl gm_tpls80s4u4ujepk;
extern const gm_tmpl gm_tplt3knh0ehirt7;
extern const gm_tmpl gm_tpltlq1bga4cgul;
extern const gm_tmpl gm_tplttoavo0o7ugf;
extern const gm_tmpl gm_tplu58b6rd4mq28;
extern const gm_tmpl gm_tplu7gq2cm8aanh;
extern const gm_tmpl gm_tplviqo6u6r0sr3;
extern const gm_tmpl gm_tplvm52cg2hs7v4;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_align_of_k6eqqrn F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tbccvikg6b96e gm_l2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 64));
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_layout_a5ovnrm((void *)&gm_l2, (void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
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
inline __attribute__((aligned(4))) uint64_t gm_f_align_uu2harl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_align_uu2harl F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint64_t t5 = gm_f_align_of_k6eqqrn((void *)NULL, cx, NULL, t3, t4);
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
inline __attribute__((aligned(4))) void gm_f_format_0p6a28s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_0p6a28s F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1qml6papvbci));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplbdrnrpalngql), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_1ck6tsm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_1ck6tsm F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_so43rnba8j5pb));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(34));
    gm_tj4tbafenpsjm t3 = GM_LD(gm_tj4tbafenpsjm, ((uint8_t *)a0));
    gm_f_format_mervo2q((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplolsc5tfmth66), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_3vtl3te(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_3vtl3te F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sjtp8ip8t28uh));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplqt9e8rihgogv), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_4q9e8cc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_4q9e8cc F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s9kudvjpckcsm));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(9));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl9j9ok1j4nn18), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_67eb57q(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_67eb57q F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s28ggq4c4bg4u));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(31));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_01lon97((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplgursfbit7uc3), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_9221qv6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_9221qv6 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sivr8g8c8o0kq));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(12));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplplkqcb01ocrd), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_dlemvkb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_dlemvkb F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sd70ob31745fn));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl9qg0hlqqke7e), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_e0d41cg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_e0d41cg F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_saqhrs072atsq));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(9));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplo66949vs5pfo), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_e6ar907(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0) {
    struct gm_fr_format_e6ar907 F;
    gm_t2lqu8jsj511d gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t2lqu8jsj511d t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sb8b1gf0gtt49));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(42));
    gm_f_format_ig8f8pl((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplc9tn3pkjdpf4), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_eb9vbgb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_eb9vbgb F;
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s2u5mf7vjj6ie));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(23));
    gm_t0uuhaljsf410 t3 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
    gm_f_format_8ghesn9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplkkvn546crm4n), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_fsfutor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_fsfutor F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdsb9longvn22));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(39));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplu7gq2cm8aanh), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_hb3htfv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_hb3htfv F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s619rbci7p06v));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(19));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_15426cv((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl570sljg0ivt5), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_jagvq3q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_jagvq3q F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjr44k79a48mq));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplj78420ela1cg), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_kr44ep8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_kr44ep8 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s22lkrbtp01c5));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(22));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplio7v2odie8p1), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_mp5vmqe(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_mp5vmqe F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_seak1jbr9qmd1));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(8));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplnm352386bmep), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_norassj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_norassj F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ss2v2ursd8pms));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplmekan10d5buf), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_o8nngn3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_o8nngn3 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sifvl6bvepiob));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplpv6nab1nov2l), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_oe49e3n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_oe49e3n F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sica240oidke0));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(7));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplviqo6u6r0sr3), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_q6prg09(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_q6prg09 F;
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sc96am1bplmoh));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(79));
    gm_tud7oovfvnf43 t3 = GM_LD(gm_tud7oovfvnf43, ((uint8_t *)a0));
    gm_f_format_rjuhqv1((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl2r84u6vnc58k), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_sflh0jm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0) {
    struct gm_fr_format_sflh0jm F;
    gm_t2lqu8jsj511d gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t2lqu8jsj511d t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sckj2gedlq7u6));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(54));
    gm_f_format_ig8f8pl((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpli4rfoi75onhj), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_tth3l1d(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_tth3l1d F;
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sjl0s4joed61f));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(14));
    gm_t0uuhaljsf410 t3 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
    gm_f_format_rqlkk21((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplttoavo0o7ugf), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_u9gq54a(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_u9gq54a F;
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sfbb5kv3rbn0v));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(28));
    gm_tg23pq9a29gh1 t3 = GM_LD(gm_tg23pq9a29gh1, ((uint8_t *)a0));
    gm_f_format_kgp4bin((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpls2fio2pf4rdq), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_uhjjs86(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_uhjjs86 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sfltedqje18on));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpld9ts015fint3), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_v2sfrt0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_v2sfrt0 F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sl9pujidpnv0r));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplt3knh0ehirt7), t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_ii_cty_i2b4e03(gm_cs2lqoac0a5ng4 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ii_cty_i2b4e03 F;
    gm_tuglhjjmj2g58 gm_l0; /* ii */
    GM_STACK_CHECK_SHALLOW(&F, 73u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_tuglhjjmj2g58, a0);
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&gm_l0));
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_b16_8 t4 = gm_f_int_cty_s62af0i(t2, t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_ii_exact_g6a19dp(gm_cs2lqoac0a5ng4 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ii_exact_g6a19dp F;
    gm_tuglhjjmj2g58 gm_l0; /* ii */
    GM_STACK_CHECK_SHALLOW(&F, 47u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_tuglhjjmj2g58, a0);
    uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
    uint64_t t3 = (uint64_t)t2;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t r5; bool o6 = __builtin_mul_overflow(t4, ((uint64_t)8ULL), &r5);
    if (o6) gm_trap("integer overflow in `*`", gm_src4pc97o88riqc, 22, 56);
    uint64_t t7 = r5;
    uint8_t t8 = ((t3 == t7));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_ii_ucty_b40h5ot(gm_cs2lqoac0a5ng4 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ii_ucty_b40h5ot F;
    gm_tuglhjjmj2g58 gm_l0; /* ii */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_tuglhjjmj2g58, a0);
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_b16_8 t3 = gm_f_int_cty_s62af0i(((uint8_t)0), t2);
    F.ret = t3;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_ghvav22 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&(*(gm_t53at80pfdus3 *)gm_out), t4, GM_CONV(gm_csguankko4lmhd, t5));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_13gk771(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_13gk771 F;
    void * gm_l0; /* fg */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s946k82tp96q3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(45));
    gm_b0_1 t4 = gm_f_linef_fa323qa((void *)NULL, cx, NULL, t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tplchsi5ghu9772), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_2lhkgdf(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_2lhkgdf F;
    void * gm_l0; /* fg */
    gm_tommf4j9upomc gm_l1; /* args */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sso181di0dbb3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
    gm_b0_1 t4 = gm_f_linef_dib3cle((void *)NULL, cx, NULL, t2, &(*(gm_tommf4j9upomc *)((uint8_t *)a1)), ((void *)&gm_tpl324tc5bgmdkb), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_a8sab3a(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_a8sab3a F;
    void * gm_l0; /* fg */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sr1lb1m0kmmrh));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(45));
    gm_b0_1 t4 = gm_f_linef_slpvrc7((void *)NULL, cx, NULL, t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tpl3t5edr01qp5c), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_bq6igl9(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_bq6igl9 F;
    void * gm_l0; /* fg */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_siv1vjr1gaenb));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(12));
    gm_b0_1 t4 = gm_f_linef_slpvrc7((void *)NULL, cx, NULL, t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tpl1f1njss89nj4), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_calnga5(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_calnga5 F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sht50491g5jgu));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(30));
    gm_b0_1 t4 = gm_f_linef_8v6p2qr((void *)NULL, cx, NULL, t2, &(*(gm_tif3elqk01bdl *)((uint8_t *)a1)), ((void *)&gm_tplda38mm1025ge), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_dpq84co(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_dpq84co F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s25h5btu4ug6a));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(11));
    gm_b0_1 t4 = gm_f_linef_qk177qj((void *)NULL, cx, NULL, t2, &(*(gm_tfeab279c16rr *)((uint8_t *)a1)), ((void *)&gm_tpl3bdcsso3qmi0), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_fdhsfa1(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_fdhsfa1 F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sm89ieaqiarhc));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(53));
    gm_b0_1 t4 = gm_f_linef_ao39327((void *)NULL, cx, NULL, t2, &(*(gm_tfeab279c16rr *)((uint8_t *)a1)), ((void *)&gm_tplvm52cg2hs7v4), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_g6msml8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1) {
    struct gm_fr_linef_g6msml8 F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sidk0bupbfuvu));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(10));
    gm_b0_1 t5 = gm_f_linef_7afcl2l((void *)NULL, cx, NULL, t2, t3, ((void *)&gm_tpl9rm8d4b25q2c), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_gh0ah67(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_gh0ah67 F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssscpugolo2j1));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(36));
    gm_b0_1 t4 = gm_f_linef_m9jdfi1((void *)NULL, cx, NULL, t2, &(*(gm_tud7oovfvnf43 *)((uint8_t *)a1)), ((void *)&gm_tpl1irt5kietk82), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_pamii31(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_pamii31 F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shjbutqe8bgfp));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(48));
    gm_b0_1 t4 = gm_f_linef_ao39327((void *)NULL, cx, NULL, t2, &(*(gm_tfeab279c16rr *)((uint8_t *)a1)), ((void *)&gm_tpl5c6n9e2iidjc), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_qi8elfs(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_qi8elfs F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sq0pl92g4hdc6));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(46));
    gm_b0_1 t4 = gm_f_linef_onigi7o((void *)NULL, cx, NULL, t2, &(*(gm_tif3elqk01bdl *)((uint8_t *)a1)), ((void *)&gm_tpltlq1bga4cgul), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_r08knlm(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_r08knlm F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssoik29mo3fp0));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(52));
    gm_b0_1 t4 = gm_f_linef_6n9epdm((void *)NULL, cx, NULL, t2, &(*(gm_th2d9c7aju8hp *)((uint8_t *)a1)), ((void *)&gm_tpls80s4u4ujepk), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_rf3vkph(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_rf3vkph F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smgvt83efirng));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(52));
    gm_b0_1 t4 = gm_f_linef_evtoou6((void *)NULL, cx, NULL, t2, &(*(gm_tp6khiphboddr *)((uint8_t *)a1)), ((void *)&gm_tpl1sdu3l536hkg), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_v6ea1vv(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_v6ea1vv F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1lomal6al3te));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(70));
    gm_b0_1 t4 = gm_f_linef_2iuqifd((void *)NULL, cx, NULL, t2, &(*(gm_tif3elqk01bdl *)((uint8_t *)a1)), ((void *)&gm_tplaoelja80vfud), t3);
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
inline __attribute__((aligned(4))) void gm_f_make_r4pbu77(void *gm_out, uint8_t a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_make_r4pbu77 F;
    uint8_t gm_l0; /* negative */
    GM_STACK_CHECK_SHALLOW(&F, 61u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    uint8_t gm_l3;
    {
        memmove(((uint8_t *)&F.l2), ((uint8_t *)&F.l1), 40);
    }
    void * t2 = (void *)((uint8_t *)&F.l2);
    gm_f_trim_8rdppmk(t2);
    uint8_t t3 = gm_l0;
    if (t3) {
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
        uint8_t t5 = (t4 > ((uint64_t)0ULL));
        gm_l3 = t5;
    } else {
        gm_l3 = ((uint8_t)0);
    }
    {
        uint8_t t6 = gm_l3;
        gm_trqfvmjtt4sij t7 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l2));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 0), t6);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 8), t7);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_neg_agd5c5k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_neg_agd5c5k F;
    GM_STACK_CHECK(&F, 61u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t gm_l2;
    {
        gm_tbci5ospk5nnn t2 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l0));
        gm_f_copy_pmkg67r((void *)&F.l1, (void *)NULL, cx, NULL, &t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&F.l0));
    uint8_t t4 = (uint8_t)!t3;
    if (t4) {
        uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)&F.l0) + 8) + 8));
        uint8_t t6 = (t5 > ((uint64_t)0ULL));
        gm_l2 = t6;
    } else {
        gm_l2 = ((uint8_t)0);
    }
    uint8_t t7 = gm_l2;
    GM_ST(uint8_t, ((uint8_t *)&F.l1), t7);
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 48);
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
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_one_08qg1op F;
    GM_STACK_CHECK(&F, 0u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_f_from_u64_ud13q5v((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, ((uint64_t)1ULL));
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
inline __attribute__((aligned(4))) void gm_f_shl_qqecl81(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1) {
    struct gm_fr_shl_qqecl81 F;
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_l1 = a1;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&F.l0));
    gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 8));
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    uint64_t t5 = gm_l1;
    gm_trqfvmjtt4sij t6;
    gm_f_mag_shl_5df643p((void *)&t6, (void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_make_r4pbu77((void *)&(*(gm_tbci5ospk5nnn *)gm_out), t2, &t6);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_5qbi623 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    gm_b16_8 t6 = gm_f_show_kmjeh8s((void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_dmhtkib F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint64_t t5 = gm_f_size_of_k0fluj2((void *)NULL, cx, NULL, t3, t4);
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
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_of_k0fluj2 F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tbccvikg6b96e gm_l2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 64));
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_layout_a5ovnrm((void *)&gm_l2, (void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t5 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
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
inline __attribute__((aligned(4))) void gm_f_split_u128_vrmr6en(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_split_u128_vrmr6en F;
    unsigned __int128 gm_l1; /* m */
    GM_STACK_CHECK_SHALLOW(&F, 128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        unsigned __int128 t2 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
        gm_l1 = t2;
    }
    {
        unsigned __int128 t3 = gm_l1;
        unsigned __int128 t4 = (unsigned __int128)(t3 & ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 18446744073709551615ULL)));
        unsigned __int128 t5 = gm_l1;
        unsigned __int128 n6 = (unsigned __int128)((uint64_t)64ULL);
        if (n6 >= 128) gm_trap_shift((uint64_t)n6, "u128", gm_src0gtuh94b1fce, 761, 40);
        unsigned __int128 t7 = (unsigned __int128)(t5 >> (int)n6);
        gm_t793vlgpa3b88 t8;
        GM_ST(unsigned __int128, ((uint8_t *)&t8 + 0), t4);
        GM_ST(unsigned __int128, ((uint8_t *)&t8 + 16), t7);
        memmove(gm_out, (((uint8_t *)&t8) + 0), 32);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_sub_pktm5cr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_sub_pktm5cr F;
    GM_STACK_CHECK(&F, 67u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&F.l0));
    gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 8));
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    uint8_t t5 = GM_LD(uint8_t, ((uint8_t *)a1));
    uint8_t t6 = (uint8_t)!t5;
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    gm_f_signed_add_ek0dpmo((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t2, t4, t6, t8);
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
inline __attribute__((aligned(4))) void gm_f_unsupported_531q51r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1) {
    struct gm_fr_unsupported_531q51r F;
    gm_tl874t2atnlh3 gm_l0; /* span */
    gm_b16_8 gm_l1; /* what */
    GM_STACK_CHECK(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l2;
    {
        gm_b16_8 t2 = gm_l1;
        gm_tnpifv20irnph t3;
        GM_ST(gm_b16_8, ((uint8_t *)&t3 + 0), t2);
        gm_f_format_fsfutor((void *)&gm_l2, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t4 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t4) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 8, gm_srcd5arorrf8jkg, 38, 27);
    {
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_tl874t2atnlh3 t7 = gm_l0;
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 28, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 0), t6);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 16), t7);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_arith_fits_7degt3e(void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0, gm_b16_8 a1, uint8_t a2) {
    struct gm_fr_arith_fits_7degt3e F;
    gm_tuglhjjmj2g58 gm_l0; /* ii */
    gm_b16_8 gm_l1; /* v */
    uint8_t gm_l2; /* signed */
    gm_t793vlgpa3b88 gm_l3; /* max */
    gm_b16_8 gm_l4; /* max_lit */
    gm_t793vlgpa3b88 gm_l5; /* min */
    gm_b16_8 gm_l6; /* min_lit */
    GM_STACK_CHECK(&F, 838u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l7;
    gm_tabb710d9mq2e gm_l8;
    gm_tabb710d9mq2e gm_l9;
    gm_tabb710d9mq2e gm_l10;
    {
        gm_tuglhjjmj2g58 t2 = gm_l0;
        gm_tbci5ospk5nnn t3;
        gm_f_ii_max_gtp5h90((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_split_u128_vrmr6en((void *)&gm_l3, &t3);
    }
    {
        unsigned __int128 t4 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l3) + 16));
        uint64_t t5 = (uint64_t)t4;
        unsigned __int128 t6 = GM_LD(unsigned __int128, ((uint8_t *)&gm_l3));
        uint64_t t7 = (uint64_t)t6;
        gm_t2lqu8jsj511d t8;
        GM_ST(uint64_t, ((uint8_t *)&t8 + 0), t5);
        GM_ST(uint64_t, ((uint8_t *)&t8 + 8), t7);
        gm_f_format_e6ar907((void *)&gm_l7, (void *)NULL, cx, NULL, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (!t9) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_src4pc97o88riqc, 213, 16);
    gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
    gm_l4 = t10;
    uint8_t t11 = gm_l2;
    if (t11) {
        {
            gm_tuglhjjmj2g58 t12 = gm_l0;
            gm_tbci5ospk5nnn t13;
            gm_f_ii_min_lku7pdi((void *)&t13, (void *)NULL, cx, NULL, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_split_u128_vrmr6en((void *)&gm_l5, &t13);
        }
        {
            unsigned __int128 t14 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l5) + 16));
            uint64_t t15 = (uint64_t)t14;
            unsigned __int128 t16 = GM_LD(unsigned __int128, ((uint8_t *)&gm_l5));
            uint64_t t17 = (uint64_t)t16;
            gm_t2lqu8jsj511d t18;
            GM_ST(uint64_t, ((uint8_t *)&t18 + 0), t15);
            GM_ST(uint64_t, ((uint8_t *)&t18 + 8), t17);
            gm_f_format_sflh0jm((void *)&gm_l8, (void *)NULL, cx, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
        if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_src4pc97o88riqc, 216, 20);
        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
        gm_l6 = t20;
        {
            gm_b16_8 t21 = gm_l1;
            gm_b16_8 t22 = gm_l6;
            gm_b16_8 t23 = gm_l1;
            gm_b16_8 t24 = gm_l1;
            gm_b16_8 t25 = gm_l4;
            gm_tud7oovfvnf43 t26;
            GM_ST(gm_b16_8, ((uint8_t *)&t26 + 0), t21);
            GM_ST(gm_b16_8, ((uint8_t *)&t26 + 16), t22);
            GM_ST(gm_b16_8, ((uint8_t *)&t26 + 32), t23);
            GM_ST(gm_b16_8, ((uint8_t *)&t26 + 48), t24);
            GM_ST(gm_b16_8, ((uint8_t *)&t26 + 64), t25);
            gm_f_format_q6prg09((void *)&gm_l9, (void *)NULL, cx, NULL, &t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
        if (!t27) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_src4pc97o88riqc, 217, 16);
        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
        gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
        F.ret = t29;
        goto gm_fret;
    }
    {
        gm_b16_8 t30 = gm_l1;
        gm_b16_8 t31 = gm_l4;
        gm_t6j6smj7qoctu t32;
        GM_ST(gm_b16_8, ((uint8_t *)&t32 + 0), t30);
        GM_ST(gm_b16_8, ((uint8_t *)&t32 + 16), t31);
        gm_f_format_67eb57q((void *)&gm_l10, (void *)NULL, cx, NULL, &t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t33) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_src4pc97o88riqc, 219, 12);
    gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
    gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
    F.ret = t35;
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
inline __attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_arith_flavor_e1m6u89(gm_ts6oqppjh0ba3 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_arith_flavor_e1m6u89 F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    gm_t9qkh0rsooc8d gm_l1; /* f */
    gm_t9qkh0rsooc8d gm_l2; /* f */
    gm_t9qkh0rsooc8d gm_l3; /* f */
    gm_t9qkh0rsooc8d gm_l4; /* f */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_taari1s0m51a7 gm_l5;
    {
        {
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t9 = (uint8_t)(t8 == ((uint8_t)0ULL));
            if (!t9) {
                goto gm_lb2;
            }
            gm_t9qkh0rsooc8d t10 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
            gm_l1 = t10;
            gm_t9qkh0rsooc8d t11 = gm_l1;
            gm_taari1s0m51a7 t12;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t12), t11);
            ((uint8_t *)&t12)[1] = 1;
            gm_l5 = t12;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)1ULL));
            if (!t14) {
                goto gm_lb3;
            }
            gm_t9qkh0rsooc8d t15 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
            gm_l2 = t15;
            gm_t9qkh0rsooc8d t16 = gm_l2;
            gm_taari1s0m51a7 t17;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17), t16);
            ((uint8_t *)&t17)[1] = 1;
            gm_l5 = t17;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t19 = (uint8_t)(t18 == ((uint8_t)2ULL));
            if (!t19) {
                goto gm_lb4;
            }
            gm_t9qkh0rsooc8d t20 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
            gm_l3 = t20;
            gm_t9qkh0rsooc8d t21 = gm_l3;
            gm_taari1s0m51a7 t22;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t22), t21);
            ((uint8_t *)&t22)[1] = 1;
            gm_l5 = t22;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)3ULL));
            if (!t24) {
                goto gm_lb5;
            }
            gm_t9qkh0rsooc8d t25 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
            gm_l4 = t25;
            gm_t9qkh0rsooc8d t26 = gm_l4;
            gm_taari1s0m51a7 t27;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t27), t26);
            ((uint8_t *)&t27)[1] = 1;
            gm_l5 = t27;
            goto gm_lb1;
        }
gm_lb5:;
        {
            gm_taari1s0m51a7 t28;
            memset(&t28, 0, sizeof t28);
            gm_l5 = t28;
            goto gm_lb1;
        }
gm_lb6:;
        gm_trap("no match arm matched the scrutinee", gm_src4pc97o88riqc, 41, 53);
    }
gm_lb1:;
    gm_taari1s0m51a7 t29 = gm_l5;
    F.ret = t29;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) double gm_f_arith_int_to_f64_tiud1hu(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_arith_int_to_f64_tiud1hu F;
    double gm_l1; /* acc */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 166u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    double gm_l3;
    gm_l1 = ((double)0e0);
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 8));
    gm_l2 = t5;
    {
        for (;;) {
            uint64_t t6 = gm_l2;
            uint8_t t7 = (t6 > ((uint64_t)0ULL));
            if (!t7) {
                goto gm_lb1;
            }
            {
                uint64_t t8 = gm_l2;
                uint64_t r9; bool o10 = __builtin_sub_overflow(t8, ((uint64_t)1ULL), &r9);
                if (o10) gm_trap("integer overflow in `-`", gm_src4pc97o88riqc, 238, 9);
                uint64_t t11 = r9;
                gm_l2 = t11;
                double t12 = gm_l1;
                double t13 = (t12 * ((double)4.294967296e9));
                void * t14 = GM_LD(void *, (((uint8_t *)a0) + 8));
                uint64_t t15 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 8));
                uint64_t t16 = gm_l2;
                if ((uintptr_t)t16 >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)t16, t15, gm_src4pc97o88riqc, 239, 40);
                uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)t14) + (uintptr_t)t16 * 4));
                double t18 = (double)(t17);
                double t19 = (t13 + t18);
                gm_l1 = t19;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    uint8_t t20 = GM_LD(uint8_t, ((uint8_t *)a0));
    if (t20) {
        double t21 = gm_l1;
        double t22 = -t21;
        gm_l3 = t22;
    } else {
        double t23 = gm_l1;
        gm_l3 = t23;
    }
    double t24 = gm_l3;
    F.ret = t24;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) double gm_f_arith_int_to_f64_tiud1hu_c(gm_tbci5ospk5nnn a0) { return gm_f_arith_int_to_f64_tiud1hu(&a0); }
inline __attribute__((aligned(4))) uint64_t gm_f_arith_pointee_align_vqoa1mk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_arith_pointee_align_vqoa1mk F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* p */
    gm_t88rcucg1b7uk gm_l3; /* p */
    gm_t88rcucg1b7uk gm_l4; /* x */
    GM_STACK_CHECK(&F, 235u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t53at80pfdus3 gm_l5;
    void * t9 = gm_l0;
    void * t10 = GM_LD(void *, ((uint8_t *)t9));
    void * t11 = (void *)(((uint8_t *)t10) + 8);
    gm_t88rcucg1b7uk t12 = gm_l1;
    gm_f_kind_ghvav22((void *)&gm_l5, t11, GM_CONV(gm_csguankko4lmhd, t12));
    {
        {
            {
                {
                    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                    uint8_t t14 = (uint8_t)(t13 == ((uint8_t)8ULL));
                    if (!t14) {
                        goto gm_lb4;
                    }
                    gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l5) + 8) + 4));
                    gm_l2 = t15;
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                    uint8_t t17 = (uint8_t)(t16 == ((uint8_t)9ULL));
                    if (!t17) {
                        goto gm_lb5;
                    }
                    gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l5) + 8) + 4));
                    gm_l2 = t18;
                    goto gm_lb3;
                }
gm_lb5:;
                goto gm_lb2;
            }
gm_lb3:;
            void * t19 = gm_l0;
            void * t20 = GM_LD(void *, ((uint8_t *)t19));
            void * t21 = (void *)(((uint8_t *)t20) + 8);
            gm_t88rcucg1b7uk t22 = gm_l2;
            uint64_t t23 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t21, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t24 = ((t23 == ((uint64_t)0ULL)));
            if (t24) {
                F.ret = ((uint64_t)1ULL);
                goto gm_fret;
            }
            void * t25 = gm_l0;
            void * t26 = GM_LD(void *, ((uint8_t *)t25));
            void * t27 = (void *)(((uint8_t *)t26) + 8);
            gm_t88rcucg1b7uk t28 = gm_l2;
            uint64_t t29 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t27, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t29;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t31 = (uint8_t)(t30 == ((uint8_t)13ULL));
            if (!t31) {
                goto gm_lb6;
            }
            gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
            gm_l4 = t32;
            void * t33 = gm_l0;
            gm_t88rcucg1b7uk t34 = gm_l4;
            uint64_t t35 = gm_f_arith_pointee_align_vqoa1mk((void *)NULL, cx, NULL, t33, t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t35;
            goto gm_fret;
        }
gm_lb6:;
        {
            F.ret = ((uint64_t)1ULL);
            goto gm_fret;
        }
gm_lb7:;
        gm_trap("no match arm matched the scrutinee", gm_src4pc97o88riqc, 223, 5);
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
__attribute__((aligned(4))) void gm_f_finish_int_cr0p55i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_tuglhjjmj2g58 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6, gm_b16_8 a7, gm_b16_8 a8, gm_b16_8 a9) {
    struct gm_fr_finish_int_cr0p55i F;
    void * gm_l0; /* fg */
    gm_t9qkh0rsooc8d gm_l1; /* fl */
    gm_tuglhjjmj2g58 gm_l2; /* ii */
    gm_t88rcucg1b7uk gm_l3; /* lt */
    gm_t88rcucg1b7uk gm_l4; /* t */
    gm_b16_8 gm_l5; /* r */
    gm_b16_8 gm_l6; /* o */
    gm_b16_8 gm_l7; /* sat_up */
    gm_b16_8 gm_l8; /* loc */
    gm_b16_8 gm_l9; /* sym */
    gm_b16_8 gm_l10; /* wrapped */
    gm_b16_8 gm_l11; /* min */
    gm_b16_8 gm_l12; /* max */
    gm_b16_8 gm_l13; /* res */
    gm_b16_8 gm_l14; /* none */
    gm_b16_8 gm_l15; /* w */
    gm_b16_8 gm_l16; /* some */
    GM_STACK_CHECK(&F, 1311u);
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
    gm_l8 = a8;
    gm_l9 = a9;
    gm_tabb710d9mq2e gm_l17;
    gm_tuglhjjmj2g58 t7 = gm_l2;
    gm_b16_8 t8 = gm_l5;
    gm_b16_8 t9 = gm_f_normalize_jch381e((void *)NULL, cx, NULL, t7, t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l10 = t9;
    {
        {
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)0ULL));
            if (!t11) {
                goto gm_lb2;
            }
            void * t12 = gm_l0;
            void * t13 = GM_LD(void *, ((uint8_t *)t12));
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)t13) + 1136));
            if (t14) {
                void * t15 = gm_l0;
                gm_b16_8 t16 = gm_l6;
                gm_b16_8 t17 = gm_l9;
                gm_b16_8 t18 = gm_l8;
                gm_tfeab279c16rr t19;
                GM_ST(gm_b16_8, ((uint8_t *)&t19 + 0), t16);
                GM_ST(gm_b16_8, ((uint8_t *)&t19 + 16), t17);
                GM_ST(gm_b16_8, ((uint8_t *)&t19 + 32), t18);
                gm_b0_1 t20 = gm_f_linef_pamii31((void *)NULL, cx, NULL, t15, &t19);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t21 = gm_l0;
            gm_t88rcucg1b7uk t22 = gm_l4;
            gm_b16_8 t23 = gm_l10;
            gm_b16_8 t24 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t21, t22, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t24);
            goto gm_fdone;
        }
gm_lb2:;
        {
            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)1ULL));
            if (!t26) {
                goto gm_lb3;
            }
            void * t27 = gm_l0;
            gm_t88rcucg1b7uk t28 = gm_l4;
            gm_b16_8 t29 = gm_l10;
            gm_b16_8 t30 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t27, t28, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t30);
            goto gm_fdone;
        }
gm_lb3:;
        {
            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t32 = (uint8_t)(t31 == ((uint8_t)2ULL));
            if (!t32) {
                goto gm_lb4;
            }
            {
                void * t33 = gm_l0;
                gm_t88rcucg1b7uk t34 = gm_l3;
                gm_tuglhjjmj2g58 t35 = gm_l2;
                gm_tbci5ospk5nnn t36;
                gm_f_ii_min_lku7pdi((void *)&t36, (void *)NULL, cx, NULL, t35);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t37 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t33, t34, &t36);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l11 = t37;
            }
            {
                void * t38 = gm_l0;
                gm_t88rcucg1b7uk t39 = gm_l3;
                gm_tuglhjjmj2g58 t40 = gm_l2;
                gm_tbci5ospk5nnn t41;
                gm_f_ii_max_gtp5h90((void *)&t41, (void *)NULL, cx, NULL, t40);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t42 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t38, t39, &t41);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l12 = t42;
            }
            {
                void * t43 = gm_l0;
                gm_t88rcucg1b7uk t44 = gm_l4;
                gm_b16_8 t45 = gm_l6;
                gm_b16_8 t46 = gm_l7;
                gm_b16_8 t47 = gm_l12;
                gm_b16_8 t48 = gm_l11;
                gm_b16_8 t49 = gm_l10;
                gm_tud7oovfvnf43 t50;
                GM_ST(gm_b16_8, ((uint8_t *)&t50 + 0), t45);
                GM_ST(gm_b16_8, ((uint8_t *)&t50 + 16), t46);
                GM_ST(gm_b16_8, ((uint8_t *)&t50 + 32), t47);
                GM_ST(gm_b16_8, ((uint8_t *)&t50 + 48), t48);
                GM_ST(gm_b16_8, ((uint8_t *)&t50 + 64), t49);
                gm_f_format_5pe4c2v((void *)&gm_l17, (void *)NULL, cx, NULL, &t50);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t51 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                if (!t51) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_src4pc97o88riqc, 193, 30);
                gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
                gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
                gm_b16_8 t54 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t43, t44, t53);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t54);
                goto gm_fdone;
            }
        }
gm_lb4:;
        {
            uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t56 = (uint8_t)(t55 == ((uint8_t)3ULL));
            if (!t56) {
                goto gm_lb5;
            }
            void * t57 = gm_l0;
            gm_t88rcucg1b7uk t58 = gm_l4;
            gm_b16_8 t59 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t57, t58);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t59;
            void * t60 = gm_l0;
            gm_b16_8 t61 = gm_l6;
            gm_tnpifv20irnph t62;
            GM_ST(gm_b16_8, ((uint8_t *)&t62 + 0), t61);
            gm_b0_1 t63 = gm_f_linef_g6msml8((void *)NULL, cx, NULL, t60, t62);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t64 = gm_l0;
            gm_t88rcucg1b7uk t65 = gm_l4;
            gm_b16_8 t66 = gm_f_make_none_v2s8s0l((void *)NULL, cx, NULL, t64, t65);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l14 = t66;
            {
                void * t67 = gm_l0;
                gm_b16_8 t68 = gm_l13;
                gm_b16_8 t69 = gm_l14;
                gm_t6j6smj7qoctu t70;
                GM_ST(gm_b16_8, ((uint8_t *)&t70 + 0), t68);
                GM_ST(gm_b16_8, ((uint8_t *)&t70 + 16), t69);
                gm_b0_1 t71 = gm_f_linef_bq6igl9((void *)NULL, cx, NULL, t67, &t70);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t72 = gm_l0;
            gm_b16_8 t73;
            GM_ST(void *, (uint8_t *)&t73, (void *)(gm_s02uuedsru1qu));
            GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(8));
            gm_b0_1 t74 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t72, t73);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t75 = gm_l0;
            gm_t88rcucg1b7uk t76 = gm_l3;
            gm_b16_8 t77 = gm_l10;
            gm_b16_8 t78 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t75, t76, t77);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t78;
            void * t79 = gm_l0;
            gm_t88rcucg1b7uk t80 = gm_l4;
            gm_b16_8 t81 = gm_l15;
            gm_b16_8 t82 = gm_f_make_some_9aqq928((void *)NULL, cx, NULL, t79, t80, t81);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l16 = t82;
            {
                void * t83 = gm_l0;
                gm_b16_8 t84 = gm_l13;
                gm_b16_8 t85 = gm_l16;
                gm_t6j6smj7qoctu t86;
                GM_ST(gm_b16_8, ((uint8_t *)&t86 + 0), t84);
                GM_ST(gm_b16_8, ((uint8_t *)&t86 + 16), t85);
                gm_b0_1 t87 = gm_f_linef_bq6igl9((void *)NULL, cx, NULL, t83, &t86);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t88 = gm_l0;
            gm_b16_8 t89;
            GM_ST(void *, (uint8_t *)&t89, (void *)(gm_s2cuflhqkkuaq));
            GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(1));
            gm_b0_1 t90 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t88, t89);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                gm_b16_8 t91 = gm_l13;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t91);
                goto gm_fdone;
            }
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_src4pc97o88riqc, 182, 5);
    }
gm_lb1:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_float_from_r6b8fu0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_float_from_r6b8fu0 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* v */
    GM_STACK_CHECK(&F, 443u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l3;
    gm_tabb710d9mq2e gm_l4;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, ((uint8_t *)t2));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_t53at80pfdus3 t6;
        gm_f_kind_ghvav22((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        uint8_t t8 = (uint8_t)(t7 == ((uint8_t)3ULL));
        if (t8) {
            uint16_t t9 = GM_LD(uint16_t, (((uint8_t *)&t6) + 8));
            t8 = (uint8_t)(t9 == ((uint16_t)16ULL));
        }
        uint8_t t10 = (uint8_t)(t8);
        gm_t53at80pfdus3 t11;
        memset(&t11, 0, sizeof t11);
        GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)3ULL));
        GM_ST(uint16_t, ((uint8_t *)&t11 + 8), ((uint16_t)16ULL));
        if (t10) {
            gm_b16_8 t12 = gm_l2;
            gm_tnpifv20irnph t13;
            GM_ST(gm_b16_8, ((uint8_t *)&t13 + 0), t12);
            gm_f_format_0p6a28s((void *)&gm_l3, (void *)NULL, cx, NULL, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
            if (!t14) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_src4pc97o88riqc, 94, 47);
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
            gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
            F.ret = t16;
            goto gm_fret;
        }
    }
    {
        void * t17 = gm_l0;
        void * t18 = GM_LD(void *, ((uint8_t *)t17));
        void * t19 = (void *)(((uint8_t *)t18) + 8);
        gm_t88rcucg1b7uk t20 = gm_l1;
        gm_b16_8 t21 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t22 = gm_l2;
        gm_t6j6smj7qoctu t23;
        GM_ST(gm_b16_8, ((uint8_t *)&t23 + 0), t21);
        GM_ST(gm_b16_8, ((uint8_t *)&t23 + 16), t22);
        gm_f_format_mp5vmqe((void *)&gm_l4, (void *)NULL, cx, NULL, &t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t24) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 8, gm_src4pc97o88riqc, 95, 12);
    gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
    gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
    F.ret = t26;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_ii_cty_i2b4e03(gm_cs2lqoac0a5ng4 a0);
extern __attribute__((aligned(4))) uint8_t gm_f_ii_exact_g6a19dp(gm_cs2lqoac0a5ng4 a0);
inline __attribute__((aligned(4))) void gm_f_ii_max_gtp5h90(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0) {
    struct gm_fr_ii_max_gtp5h90 F;
    gm_tuglhjjmj2g58 gm_l0; /* ii */
    GM_STACK_CHECK(&F, 337u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&gm_l0));
    if (t2) {
        gm_tbci5ospk5nnn t3;
        gm_f_one_08qg1op((void *)&t3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
        uint64_t t5 = (uint64_t)t4;
        uint64_t r6; bool o7 = __builtin_sub_overflow(t5, ((uint64_t)1ULL), &r6);
        if (o7) gm_trap("integer overflow in `-`", gm_src4pc97o88riqc, 36, 63);
        uint64_t t8 = r6;
        gm_tbci5ospk5nnn t9;
        gm_f_shl_qqecl81((void *)&t9, (void *)NULL, cx, NULL, &t3, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbci5ospk5nnn t10;
        gm_f_one_08qg1op((void *)&t10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_sub_pktm5cr((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, &t9, &t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
    }
    {
        gm_tbci5ospk5nnn t11;
        gm_f_one_08qg1op((void *)&t11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
        uint64_t t13 = (uint64_t)t12;
        gm_tbci5ospk5nnn t14;
        gm_f_shl_qqecl81((void *)&t14, (void *)NULL, cx, NULL, &t11, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbci5ospk5nnn t15;
        gm_f_one_08qg1op((void *)&t15, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_sub_pktm5cr((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, &t14, &t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
inline __attribute__((aligned(4))) void gm_f_ii_min_lku7pdi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0) {
    struct gm_fr_ii_min_lku7pdi F;
    gm_tuglhjjmj2g58 gm_l0; /* ii */
    GM_STACK_CHECK(&F, 133u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&gm_l0));
    if (t2) {
        gm_tbci5ospk5nnn t3;
        gm_f_one_08qg1op((void *)&t3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
        uint64_t t5 = (uint64_t)t4;
        uint64_t r6; bool o7 = __builtin_sub_overflow(t5, ((uint64_t)1ULL), &r6);
        if (o7) gm_trap("integer overflow in `-`", gm_src4pc97o88riqc, 31, 63);
        uint64_t t8 = r6;
        gm_tbci5ospk5nnn t9;
        gm_f_shl_qqecl81((void *)&t9, (void *)NULL, cx, NULL, &t3, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_neg_agd5c5k((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, &t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
    }
    gm_f_zero_s25l990((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL);
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_ii_ucty_b40h5ot(gm_cs2lqoac0a5ng4 a0);
__attribute__((aligned(4))) void gm_f_int_binary_kp3u7i8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_ts6oqppjh0ba3 a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6) {
    struct gm_fr_int_binary_kp3u7i8 F;
    void * gm_l0; /* fg */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_ts6oqppjh0ba3 gm_l3; /* op */
    gm_t88rcucg1b7uk gm_l4; /* lt */
    gm_b16_8 gm_l5; /* a */
    gm_b16_8 gm_l6; /* b */
    gm_tuglhjjmj2g58 gm_l7; /* ii */
    gm_b16_8 gm_l8; /* c */
    gm_b16_8 gm_l9; /* loc */
    gm_t9qkh0rsooc8d gm_l10; /* fl */
    gm_t9qkh0rsooc8d gm_l11; /* fl */
    gm_t9qkh0rsooc8d gm_l12; /* fl */
    gm_b16_8 gm_l13; /* name */
    gm_b16_8 gm_l14; /* r */
    gm_b16_8 gm_l15; /* o */
    gm_b16_8 gm_l16; /* min */
    gm_b16_8 gm_l17; /* max */
    gm_b16_8 gm_l18; /* sat */
    gm_t9qkh0rsooc8d gm_l19; /* fl */
    gm_b16_8 gm_l20; /* n */
    gm_b16_8 gm_l21; /* r */
    gm_b16_8 gm_l22; /* raw */
    gm_b16_8 gm_l23; /* norm */
    gm_b16_8 gm_l24; /* o */
    gm_b16_8 gm_l25; /* sat */
    gm_b16_8 gm_l26; /* sym */
    gm_b16_8 gm_l27; /* min */
    gm_b16_8 gm_l28; /* r */
    gm_b16_8 gm_l29; /* s */
    GM_STACK_CHECK(&F, 4738u);
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
    gm_tqcc6dmlne11b gm_l30;
    gm_tabb710d9mq2e gm_l31;
    gm_tabb710d9mq2e gm_l32;
    gm_tabb710d9mq2e gm_l33;
    gm_tabb710d9mq2e gm_l34;
    gm_tabb710d9mq2e gm_l35;
    uint8_t gm_l36;
    gm_t5o0tpv5jka2l gm_l37;
    gm_taari1s0m51a7 gm_l38;
    gm_tabb710d9mq2e gm_l39;
    gm_tabb710d9mq2e gm_l40;
    gm_tabb710d9mq2e gm_l41;
    gm_tabb710d9mq2e gm_l42;
    gm_tabb710d9mq2e gm_l43;
    gm_tabb710d9mq2e gm_l44;
    gm_t5o0tpv5jka2l gm_l45;
    gm_tabb710d9mq2e gm_l46;
    gm_tabb710d9mq2e gm_l47;
    gm_tabb710d9mq2e gm_l48;
    gm_tabb710d9mq2e gm_l49;
    {
        void * t39 = gm_l0;
        gm_t88rcucg1b7uk t40 = gm_l4;
        gm_f_int_info_2tcdf1n((void *)&gm_l30, (void *)NULL, cx, NULL, t39, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t41 = (uint8_t)((((uint8_t *)&gm_l30))[16] != 0);
        if (!t41) gm_trap("unwrap of null", gm_src4pc97o88riqc, 101, 11);
        gm_tuglhjjmj2g58 t42 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&gm_l30));
        gm_l7 = t42;
        gm_tuglhjjmj2g58 t43 = gm_l7;
        gm_b16_8 t44 = gm_f_ii_cty_i2b4e03(GM_CONV(gm_cs2lqoac0a5ng4, t43));
        gm_l8 = t44;
        void * t45 = gm_l0;
        void * t46 = GM_LD(void *, ((uint8_t *)t45));
        gm_tl874t2atnlh3 t47 = gm_l1;
        gm_b16_8 t48 = gm_f_src_loc_lrhtj7i((void *)NULL, cx, NULL, t46, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t48;
        {
            {
                {
                    {
                        {
                            uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t50 = (uint8_t)(t49 == ((uint8_t)0ULL));
                            if (!t50) {
                                goto gm_lb4;
                            }
                            gm_t9qkh0rsooc8d t51 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l3) + 1));
                            gm_l10 = t51;
                            goto gm_lb3;
                        }
gm_lb4:;
                        {
                            uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t53 = (uint8_t)(t52 == ((uint8_t)1ULL));
                            if (!t53) {
                                goto gm_lb5;
                            }
                            gm_t9qkh0rsooc8d t54 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l3) + 1));
                            gm_l10 = t54;
                            goto gm_lb3;
                        }
gm_lb5:;
                        {
                            uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t56 = (uint8_t)(t55 == ((uint8_t)2ULL));
                            if (!t56) {
                                goto gm_lb6;
                            }
                            gm_t9qkh0rsooc8d t57 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l3) + 1));
                            gm_l10 = t57;
                            goto gm_lb3;
                        }
gm_lb6:;
                        goto gm_lb2;
                    }
gm_lb3:;
                    {
                        {
                            uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t59 = (uint8_t)(t58 == ((uint8_t)0ULL));
                            if (!t59) {
                                goto gm_lb8;
                            }
                            gm_b16_8 t60;
                            GM_ST(void *, (uint8_t *)&t60, (void *)(gm_s4o70d89183c2));
                            GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(3));
                            gm_l13 = t60;
                            goto gm_lb7;
                        }
gm_lb8:;
                        {
                            uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t62 = (uint8_t)(t61 == ((uint8_t)1ULL));
                            if (!t62) {
                                goto gm_lb9;
                            }
                            gm_b16_8 t63;
                            GM_ST(void *, (uint8_t *)&t63, (void *)(gm_snqbu1qqmr5lp));
                            GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(3));
                            gm_l13 = t63;
                            goto gm_lb7;
                        }
gm_lb9:;
                        {
                            gm_b16_8 t64;
                            GM_ST(void *, (uint8_t *)&t64, (void *)(gm_smpmbscbko3gk));
                            GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(3));
                            gm_l13 = t64;
                            goto gm_lb7;
                        }
gm_lb10:;
                        gm_trap("no match arm matched the scrutinee", gm_src4pc97o88riqc, 106, 28);
                    }
gm_lb7:;
                    {
                        void * t65 = gm_l0;
                        uint32_t t66 = gm_f_fresh_5kokjmg(t65);
                        gm_t88rcucg1b7uk t67;
                        GM_ST(uint32_t, ((uint8_t *)&t67 + 0), t66);
                        gm_f_format_jagvq3q((void *)&gm_l31, (void *)NULL, cx, NULL, t67);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t68 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                    if (!t68) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 8, gm_src4pc97o88riqc, 111, 18);
                    gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l31) + 8));
                    gm_l14 = t69;
                    {
                        void * t70 = gm_l0;
                        uint32_t t71 = gm_f_fresh_5kokjmg(t70);
                        gm_t88rcucg1b7uk t72;
                        GM_ST(uint32_t, ((uint8_t *)&t72 + 0), t71);
                        gm_f_format_v2sfrt0((void *)&gm_l32, (void *)NULL, cx, NULL, t72);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t73 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                    if (!t73) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 8, gm_src4pc97o88riqc, 112, 18);
                    gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l32) + 8));
                    gm_l15 = t74;
                    {
                        {
                            gm_b16_8 t75 = gm_l13;
                            gm_b16_8 t76;
                            GM_ST(void *, (uint8_t *)&t76, (void *)(gm_smpmbscbko3gk));
                            GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(3));
                            uint8_t t77 = (gm_eq0iqiubfm7e(((uint8_t *)&t75), ((uint8_t *)&t76)));
                            if (!t77) {
                                goto gm_lb12;
                            }
                            uint8_t t78 = GM_LD(uint8_t, ((uint8_t *)&gm_l7));
                            if (!t78) {
                                goto gm_lb12;
                            }
                            gm_b16_8 t79 = gm_l8;
                            gm_b16_8 t80;
                            GM_ST(void *, (uint8_t *)&t80, (void *)(gm_s1a1r1qe3ssot));
                            GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(8));
                            uint8_t t81 = (gm_eq0iqiubfm7e(((uint8_t *)&t79), ((uint8_t *)&t80)));
                            if (!t81) {
                                goto gm_lb12;
                            }
                            void * t82 = gm_l0;
                            gm_b16_8 t83 = gm_l8;
                            gm_b16_8 t84 = gm_l14;
                            gm_b16_8 t85 = gm_l15;
                            gm_b16_8 t86 = gm_l5;
                            gm_b16_8 t87 = gm_l6;
                            gm_b16_8 t88 = gm_l14;
                            gm_tif3elqk01bdl t89;
                            GM_ST(gm_b16_8, ((uint8_t *)&t89 + 0), t83);
                            GM_ST(gm_b16_8, ((uint8_t *)&t89 + 16), t84);
                            GM_ST(gm_b16_8, ((uint8_t *)&t89 + 32), t85);
                            GM_ST(gm_b16_8, ((uint8_t *)&t89 + 48), t86);
                            GM_ST(gm_b16_8, ((uint8_t *)&t89 + 64), t87);
                            GM_ST(gm_b16_8, ((uint8_t *)&t89 + 80), t88);
                            gm_b0_1 t90 = gm_f_linef_qi8elfs((void *)NULL, cx, NULL, t82, &t89);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb11;
                        }
gm_lb12:;
                        void * t91 = gm_l0;
                        gm_b16_8 t92 = gm_l8;
                        gm_b16_8 t93 = gm_l14;
                        gm_b16_8 t94 = gm_l15;
                        gm_b16_8 t95 = gm_l13;
                        gm_b16_8 t96 = gm_l5;
                        gm_b16_8 t97 = gm_l6;
                        gm_b16_8 t98 = gm_l14;
                        gm_th2d9c7aju8hp t99;
                        GM_ST(gm_b16_8, ((uint8_t *)&t99 + 0), t92);
                        GM_ST(gm_b16_8, ((uint8_t *)&t99 + 16), t93);
                        GM_ST(gm_b16_8, ((uint8_t *)&t99 + 32), t94);
                        GM_ST(gm_b16_8, ((uint8_t *)&t99 + 48), t95);
                        GM_ST(gm_b16_8, ((uint8_t *)&t99 + 64), t96);
                        GM_ST(gm_b16_8, ((uint8_t *)&t99 + 80), t97);
                        GM_ST(gm_b16_8, ((uint8_t *)&t99 + 96), t98);
                        gm_b0_1 t100 = gm_f_linef_r08knlm((void *)NULL, cx, NULL, t91, &t99);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
gm_lb11:;
                    gm_tuglhjjmj2g58 t101 = gm_l7;
                    uint8_t t102 = gm_f_ii_exact_g6a19dp(GM_CONV(gm_cs2lqoac0a5ng4, t101));
                    uint8_t t103 = (uint8_t)!t102;
                    if (t103) {
                        {
                            void * t104 = gm_l0;
                            gm_t88rcucg1b7uk t105 = gm_l4;
                            gm_tuglhjjmj2g58 t106 = gm_l7;
                            gm_tbci5ospk5nnn t107;
                            gm_f_ii_min_lku7pdi((void *)&t107, (void *)NULL, cx, NULL, t106);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_b16_8 t108 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t104, t105, &t107);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l16 = t108;
                        }
                        {
                            void * t109 = gm_l0;
                            gm_t88rcucg1b7uk t110 = gm_l4;
                            gm_tuglhjjmj2g58 t111 = gm_l7;
                            gm_tbci5ospk5nnn t112;
                            gm_f_ii_max_gtp5h90((void *)&t112, (void *)NULL, cx, NULL, t111);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_b16_8 t113 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t109, t110, &t112);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l17 = t113;
                        }
                        {
                            void * t114 = gm_l0;
                            gm_b16_8 t115 = gm_l15;
                            gm_b16_8 t116 = gm_l15;
                            gm_b16_8 t117 = gm_l14;
                            gm_b16_8 t118 = gm_l16;
                            gm_b16_8 t119 = gm_l14;
                            gm_b16_8 t120 = gm_l17;
                            gm_tif3elqk01bdl t121;
                            GM_ST(gm_b16_8, ((uint8_t *)&t121 + 0), t115);
                            GM_ST(gm_b16_8, ((uint8_t *)&t121 + 16), t116);
                            GM_ST(gm_b16_8, ((uint8_t *)&t121 + 32), t117);
                            GM_ST(gm_b16_8, ((uint8_t *)&t121 + 48), t118);
                            GM_ST(gm_b16_8, ((uint8_t *)&t121 + 64), t119);
                            GM_ST(gm_b16_8, ((uint8_t *)&t121 + 80), t120);
                            gm_b0_1 t122 = gm_f_linef_calnga5((void *)NULL, cx, NULL, t114, &t121);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                    }
                    gm_b16_8 t123;
                    GM_ST(void *, (uint8_t *)&t123, (void *)(gm_svubtddeidu1d));
                    GM_ST(uintptr_t, (uint8_t *)&t123 + 8, (uintptr_t)(1));
                    gm_l18 = t123;
                    uint8_t t124 = GM_LD(uint8_t, ((uint8_t *)&gm_l7));
                    if (t124) {
                        {
                            {
                                uint8_t t125 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                                uint8_t t126 = (uint8_t)(t125 == ((uint8_t)0ULL));
                                if (!t126) {
                                    goto gm_lb14;
                                }
                                gm_b16_8 t127 = gm_l6;
                                gm_tnpifv20irnph t128;
                                GM_ST(gm_b16_8, ((uint8_t *)&t128 + 0), t127);
                                gm_f_format_o8nngn3((void *)&gm_l33, (void *)NULL, cx, NULL, t128);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t129 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                                if (!t129) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 8, gm_src4pc97o88riqc, 127, 40);
                                gm_b16_8 t130 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l33) + 8));
                                gm_b16_8 t131 = GM_LD(gm_b16_8, ((uint8_t *)&t130));
                                gm_l18 = t131;
                                goto gm_lb13;
                            }
gm_lb14:;
                            {
                                uint8_t t132 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                                uint8_t t133 = (uint8_t)(t132 == ((uint8_t)1ULL));
                                if (!t133) {
                                    goto gm_lb15;
                                }
                                gm_b16_8 t134 = gm_l6;
                                gm_tnpifv20irnph t135;
                                GM_ST(gm_b16_8, ((uint8_t *)&t135 + 0), t134);
                                gm_f_format_uhjjs86((void *)&gm_l34, (void *)NULL, cx, NULL, t135);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t136 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                                if (!t136) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 8, gm_src4pc97o88riqc, 128, 40);
                                gm_b16_8 t137 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l34) + 8));
                                gm_b16_8 t138 = GM_LD(gm_b16_8, ((uint8_t *)&t137));
                                gm_l18 = t138;
                                goto gm_lb13;
                            }
gm_lb15:;
                            {
                                {
                                    gm_b16_8 t139 = gm_l5;
                                    gm_b16_8 t140 = gm_l6;
                                    gm_t6j6smj7qoctu t141;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t141 + 0), t139);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t141 + 16), t140);
                                    gm_f_format_kr44ep8((void *)&gm_l35, (void *)NULL, cx, NULL, &t141);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                uint8_t t142 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
                                if (!t142) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 8, gm_src4pc97o88riqc, 129, 34);
                                gm_b16_8 t143 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l35) + 8));
                                gm_b16_8 t144 = GM_LD(gm_b16_8, ((uint8_t *)&t143));
                                gm_l18 = t144;
                                goto gm_lb13;
                            }
gm_lb16:;
                            gm_trap("no match arm matched the scrutinee", gm_src4pc97o88riqc, 126, 17);
                        }
gm_lb13:;
                    } else {
                        {
                            {
                                uint8_t t145 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                                uint8_t t146 = (uint8_t)(t145 == ((uint8_t)1ULL));
                                if (!t146) {
                                    goto gm_lb18;
                                }
                                gm_l36 = ((uint8_t)1);
                                goto gm_lb17;
                            }
gm_lb18:;
                            {
                                gm_l36 = ((uint8_t)0);
                                goto gm_lb17;
                            }
gm_lb19:;
                            gm_trap("no match arm matched the scrutinee", gm_src4pc97o88riqc, 131, 23);
                        }
gm_lb17:;
                        uint8_t t147 = gm_l36;
                        if (t147) {
                            gm_b16_8 t148;
                            GM_ST(void *, (uint8_t *)&t148, (void *)(gm_sicgmpca63jln));
                            GM_ST(uintptr_t, (uint8_t *)&t148 + 8, (uintptr_t)(1));
                            gm_l18 = t148;
                        }
                    }
                    {
                        void * t149 = gm_l0;
                        gm_t9qkh0rsooc8d t150 = gm_l10;
                        gm_tuglhjjmj2g58 t151 = gm_l7;
                        gm_t88rcucg1b7uk t152 = gm_l4;
                        gm_t88rcucg1b7uk t153 = gm_l2;
                        gm_b16_8 t154 = gm_l14;
                        gm_b16_8 t155 = GM_LD(gm_b16_8, ((uint8_t *)&t154));
                        gm_b16_8 t156 = gm_l15;
                        gm_b16_8 t157 = GM_LD(gm_b16_8, ((uint8_t *)&t156));
                        gm_b16_8 t158 = gm_l18;
                        gm_b16_8 t159 = gm_l9;
                        gm_ts6oqppjh0ba3 t160 = gm_l3;
                        gm_b16_8 t161 = gm_f_binop_symbol_9ijp0eo(t160);
                        gm_f_finish_int_cr0p55i((void *)&gm_l37, (void *)NULL, cx, NULL, t149, t150, t151, t152, t153, t155, t157, t158, t159, t161);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t162 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
                    if (t162) {
                    } else {
                        gm_t08olq97rscn3 t163 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l37) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t163);
                        goto gm_fdone;
                    }
                    {
                        gm_b16_8 t164 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l37) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t164);
                        goto gm_fdone;
                    }
                }
gm_lb2:;
            }
            {
                {
                    {
                        {
                            uint8_t t165 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t166 = (uint8_t)(t165 == ((uint8_t)3ULL));
                            if (!t166) {
                                goto gm_lb22;
                            }
                            goto gm_lb21;
                        }
gm_lb22:;
                        {
                            uint8_t t167 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t168 = (uint8_t)(t167 == ((uint8_t)6ULL));
                            if (!t168) {
                                goto gm_lb23;
                            }
                            goto gm_lb21;
                        }
gm_lb23:;
                        goto gm_lb20;
                    }
gm_lb21:;
                    gm_ts6oqppjh0ba3 t169 = gm_l3;
                    gm_taari1s0m51a7 t170 = gm_f_arith_flavor_e1m6u89(t169);
                    gm_l38 = t170;
                    uint8_t t171 = (uint8_t)((((uint8_t *)&gm_l38))[1] != 0);
                    if (t171) {
                        gm_t9qkh0rsooc8d t172 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l38));
                        gm_l19 = t172;
                    } else {
                        gm_t9qkh0rsooc8d t173;
                        GM_ST(uint8_t, ((uint8_t *)&t173 + 0), ((uint8_t)0ULL));
                        gm_l19 = t173;
                    }
                    {
                        void * t174 = gm_l0;
                        uint32_t t175 = gm_f_fresh_5kokjmg(t174);
                        gm_t88rcucg1b7uk t176;
                        GM_ST(uint32_t, ((uint8_t *)&t176 + 0), t175);
                        gm_f_format_norassj((void *)&gm_l39, (void *)NULL, cx, NULL, t176);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t177 = (uint8_t)!((((uint8_t *)&gm_l39))[0] != 0);
                    if (!t177) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l39) + 8, gm_src4pc97o88riqc, 138, 18);
                    gm_b16_8 t178 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l39) + 8));
                    gm_l20 = t178;
                    {
                        void * t179 = gm_l0;
                        gm_b16_8 t180 = gm_l20;
                        gm_b16_8 t181 = gm_l6;
                        gm_t6j6smj7qoctu t182;
                        GM_ST(gm_b16_8, ((uint8_t *)&t182 + 0), t180);
                        GM_ST(gm_b16_8, ((uint8_t *)&t182 + 16), t181);
                        gm_b0_1 t183 = gm_f_linef_13gk771((void *)NULL, cx, NULL, t179, &t182);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    void * t184 = gm_l0;
                    void * t185 = GM_LD(void *, ((uint8_t *)t184));
                    uint8_t t186 = GM_LD(uint8_t, (((uint8_t *)t185) + 1136));
                    if (t186) {
                        void * t187 = gm_l0;
                        gm_b16_8 t188 = gm_l20;
                        uint32_t t189 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 4));
                        gm_b16_8 t190 = gm_l20;
                        void * t191 = gm_l0;
                        void * t192 = GM_LD(void *, ((uint8_t *)t191));
                        void * t193 = (void *)(((uint8_t *)t192) + 8);
                        gm_t88rcucg1b7uk t194 = gm_l4;
                        gm_b16_8 t195 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t193, t194);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t196 = gm_l9;
                        gm_tp6khiphboddr t197;
                        GM_ST(uint32_t, (uint8_t *)&t197 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t197 + 0), t188);
                        GM_ST(uint32_t, ((uint8_t *)&t197 + 16), t189);
                        GM_ST(gm_b16_8, ((uint8_t *)&t197 + 24), t190);
                        GM_ST(gm_b16_8, ((uint8_t *)&t197 + 40), t195);
                        GM_ST(gm_b16_8, ((uint8_t *)&t197 + 56), t196);
                        gm_b0_1 t198 = gm_f_linef_rf3vkph((void *)NULL, cx, NULL, t187, &t197);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    } else {
                        void * t199 = gm_l0;
                        gm_b16_8 t200 = gm_l20;
                        uint32_t t201 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 4));
                        gm_tommf4j9upomc t202;
                        GM_ST(uint32_t, (uint8_t *)&t202 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t202 + 0), t200);
                        GM_ST(uint32_t, ((uint8_t *)&t202 + 16), t201);
                        gm_b0_1 t203 = gm_f_linef_2lhkgdf((void *)NULL, cx, NULL, t199, &t202);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    gm_ts6oqppjh0ba3 t204 = gm_l3;
                    uint8_t t205 = GM_LD(uint8_t, (((uint8_t *)&t204) + 0));
                    uint8_t t206 = (uint8_t)((t205 == ((uint8_t)6ULL)));
                    gm_ts6oqppjh0ba3 t207;
                    GM_ST(uint8_t, (uint8_t *)&t207 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t207 + 0), ((uint8_t)6ULL));
                    if (t206) {
                        void * t208 = gm_l0;
                        gm_t88rcucg1b7uk t209 = gm_l2;
                        gm_b16_8 t210 = gm_l8;
                        gm_b16_8 t211 = gm_l5;
                        gm_b16_8 t212 = gm_l20;
                        gm_tfeab279c16rr t213;
                        GM_ST(gm_b16_8, ((uint8_t *)&t213 + 0), t210);
                        GM_ST(gm_b16_8, ((uint8_t *)&t213 + 16), t211);
                        GM_ST(gm_b16_8, ((uint8_t *)&t213 + 32), t212);
                        gm_f_format_hb3htfv((void *)&gm_l40, (void *)NULL, cx, NULL, &t213);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t214 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
                        if (!t214) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l40) + 8, gm_src4pc97o88riqc, 145, 46);
                        gm_b16_8 t215 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l40) + 8));
                        gm_b16_8 t216 = GM_LD(gm_b16_8, ((uint8_t *)&t215));
                        gm_b16_8 t217 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t208, t209, t216);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t217);
                        goto gm_fdone;
                    }
                    {
                        void * t218 = gm_l0;
                        uint32_t t219 = gm_f_fresh_5kokjmg(t218);
                        gm_t88rcucg1b7uk t220;
                        GM_ST(uint32_t, ((uint8_t *)&t220 + 0), t219);
                        gm_f_format_jagvq3q((void *)&gm_l41, (void *)NULL, cx, NULL, t220);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t221 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                    if (!t221) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 8, gm_src4pc97o88riqc, 146, 18);
                    gm_b16_8 t222 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l41) + 8));
                    gm_l21 = t222;
                    {
                        gm_b16_8 t223 = gm_l8;
                        gm_tuglhjjmj2g58 t224 = gm_l7;
                        gm_b16_8 t225 = gm_f_ii_ucty_b40h5ot(GM_CONV(gm_cs2lqoac0a5ng4, t224));
                        gm_b16_8 t226 = gm_l5;
                        gm_b16_8 t227 = gm_l20;
                        gm_t0uuhaljsf410 t228;
                        GM_ST(gm_b16_8, ((uint8_t *)&t228 + 0), t223);
                        GM_ST(gm_b16_8, ((uint8_t *)&t228 + 16), t225);
                        GM_ST(gm_b16_8, ((uint8_t *)&t228 + 32), t226);
                        GM_ST(gm_b16_8, ((uint8_t *)&t228 + 48), t227);
                        gm_f_format_eb9vbgb((void *)&gm_l42, (void *)NULL, cx, NULL, &t228);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t229 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
                    if (!t229) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l42) + 8, gm_src4pc97o88riqc, 147, 20);
                    gm_b16_8 t230 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l42) + 8));
                    gm_l22 = t230;
                    gm_tuglhjjmj2g58 t231 = gm_l7;
                    gm_b16_8 t232 = gm_l22;
                    gm_b16_8 t233 = GM_LD(gm_b16_8, ((uint8_t *)&t232));
                    gm_b16_8 t234 = gm_f_normalize_jch381e((void *)NULL, cx, NULL, t231, t233);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l23 = t234;
                    {
                        void * t235 = gm_l0;
                        gm_b16_8 t236 = gm_l8;
                        gm_b16_8 t237 = gm_l21;
                        gm_b16_8 t238 = gm_l23;
                        gm_tfeab279c16rr t239;
                        GM_ST(gm_b16_8, ((uint8_t *)&t239 + 0), t236);
                        GM_ST(gm_b16_8, ((uint8_t *)&t239 + 16), t237);
                        GM_ST(gm_b16_8, ((uint8_t *)&t239 + 32), t238);
                        gm_b0_1 t240 = gm_f_linef_dpq84co((void *)NULL, cx, NULL, t235, &t239);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t241 = gm_l0;
                        uint32_t t242 = gm_f_fresh_5kokjmg(t241);
                        gm_t88rcucg1b7uk t243;
                        GM_ST(uint32_t, ((uint8_t *)&t243 + 0), t242);
                        gm_f_format_v2sfrt0((void *)&gm_l43, (void *)NULL, cx, NULL, t243);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t244 = (uint8_t)!((((uint8_t *)&gm_l43))[0] != 0);
                    if (!t244) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l43) + 8, gm_src4pc97o88riqc, 150, 18);
                    gm_b16_8 t245 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l43) + 8));
                    gm_l24 = t245;
                    {
                        void * t246 = gm_l0;
                        gm_b16_8 t247 = gm_l24;
                        gm_b16_8 t248 = gm_l8;
                        gm_b16_8 t249 = gm_l21;
                        gm_b16_8 t250 = gm_l20;
                        gm_b16_8 t251 = gm_l5;
                        gm_tud7oovfvnf43 t252;
                        GM_ST(gm_b16_8, ((uint8_t *)&t252 + 0), t247);
                        GM_ST(gm_b16_8, ((uint8_t *)&t252 + 16), t248);
                        GM_ST(gm_b16_8, ((uint8_t *)&t252 + 32), t249);
                        GM_ST(gm_b16_8, ((uint8_t *)&t252 + 48), t250);
                        GM_ST(gm_b16_8, ((uint8_t *)&t252 + 64), t251);
                        gm_b0_1 t253 = gm_f_linef_gh0ah67((void *)NULL, cx, NULL, t246, &t252);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        gm_b16_8 t254 = gm_l5;
                        gm_tnpifv20irnph t255;
                        GM_ST(gm_b16_8, ((uint8_t *)&t255 + 0), t254);
                        gm_f_format_o8nngn3((void *)&gm_l44, (void *)NULL, cx, NULL, t255);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t256 = (uint8_t)!((((uint8_t *)&gm_l44))[0] != 0);
                    if (!t256) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l44) + 8, gm_src4pc97o88riqc, 152, 20);
                    gm_b16_8 t257 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l44) + 8));
                    gm_l25 = t257;
                    {
                        void * t258 = gm_l0;
                        gm_t9qkh0rsooc8d t259 = gm_l19;
                        gm_tuglhjjmj2g58 t260 = gm_l7;
                        gm_t88rcucg1b7uk t261 = gm_l4;
                        gm_t88rcucg1b7uk t262 = gm_l2;
                        gm_b16_8 t263 = gm_l21;
                        gm_b16_8 t264 = GM_LD(gm_b16_8, ((uint8_t *)&t263));
                        gm_b16_8 t265 = gm_l24;
                        gm_b16_8 t266 = GM_LD(gm_b16_8, ((uint8_t *)&t265));
                        gm_b16_8 t267 = gm_l25;
                        gm_b16_8 t268 = GM_LD(gm_b16_8, ((uint8_t *)&t267));
                        gm_b16_8 t269 = gm_l9;
                        gm_b16_8 t270;
                        GM_ST(void *, (uint8_t *)&t270, (void *)(gm_si62ejtp0vbct));
                        GM_ST(uintptr_t, (uint8_t *)&t270 + 8, (uintptr_t)(2));
                        gm_f_finish_int_cr0p55i((void *)&gm_l45, (void *)NULL, cx, NULL, t258, t259, t260, t261, t262, t264, t266, t268, t269, t270);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t271 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
                    if (t271) {
                    } else {
                        gm_t08olq97rscn3 t272 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l45) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t272);
                        goto gm_fdone;
                    }
                    {
                        gm_b16_8 t273 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l45) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t273);
                        goto gm_fdone;
                    }
                }
gm_lb20:;
            }
            {
                {
                    {
                        {
                            uint8_t t274 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t275 = (uint8_t)(t274 == ((uint8_t)4ULL));
                            if (!t275) {
                                goto gm_lb26;
                            }
                            goto gm_lb25;
                        }
gm_lb26:;
                        {
                            uint8_t t276 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t277 = (uint8_t)(t276 == ((uint8_t)5ULL));
                            if (!t277) {
                                goto gm_lb27;
                            }
                            goto gm_lb25;
                        }
gm_lb27:;
                        goto gm_lb24;
                    }
gm_lb25:;
                    gm_ts6oqppjh0ba3 t278 = gm_l3;
                    gm_b16_8 t279 = gm_f_binop_symbol_9ijp0eo(t278);
                    gm_l26 = t279;
                    {
                        void * t280 = gm_l0;
                        gm_b16_8 t281 = gm_l6;
                        gm_b16_8 t282 = gm_l9;
                        gm_t6j6smj7qoctu t283;
                        GM_ST(gm_b16_8, ((uint8_t *)&t283 + 0), t281);
                        GM_ST(gm_b16_8, ((uint8_t *)&t283 + 16), t282);
                        gm_b0_1 t284 = gm_f_linef_a8sab3a((void *)NULL, cx, NULL, t280, &t283);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t285 = GM_LD(uint8_t, ((uint8_t *)&gm_l7));
                    if (t285) {
                        {
                            void * t286 = gm_l0;
                            gm_t88rcucg1b7uk t287 = gm_l4;
                            gm_tuglhjjmj2g58 t288 = gm_l7;
                            gm_tbci5ospk5nnn t289;
                            gm_f_ii_min_lku7pdi((void *)&t289, (void *)NULL, cx, NULL, t288);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_b16_8 t290 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t286, t287, &t289);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l27 = t290;
                        }
                        {
                            void * t291 = gm_l0;
                            gm_b16_8 t292 = gm_l5;
                            gm_b16_8 t293 = gm_l27;
                            gm_b16_8 t294 = gm_l6;
                            gm_b16_8 t295 = gm_l8;
                            gm_b16_8 t296 = gm_l26;
                            gm_b16_8 t297 = gm_l9;
                            gm_tif3elqk01bdl t298;
                            GM_ST(gm_b16_8, ((uint8_t *)&t298 + 0), t292);
                            GM_ST(gm_b16_8, ((uint8_t *)&t298 + 16), t293);
                            GM_ST(gm_b16_8, ((uint8_t *)&t298 + 32), t294);
                            GM_ST(gm_b16_8, ((uint8_t *)&t298 + 48), t295);
                            GM_ST(gm_b16_8, ((uint8_t *)&t298 + 64), t296);
                            GM_ST(gm_b16_8, ((uint8_t *)&t298 + 80), t297);
                            gm_b0_1 t299 = gm_f_linef_v6ea1vv((void *)NULL, cx, NULL, t291, &t298);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                    }
                    gm_ts6oqppjh0ba3 t300 = gm_l3;
                    uint8_t t301 = GM_LD(uint8_t, (((uint8_t *)&t300) + 0));
                    uint8_t t302 = (uint8_t)((t301 == ((uint8_t)4ULL)));
                    gm_ts6oqppjh0ba3 t303;
                    GM_ST(uint8_t, (uint8_t *)&t303 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t303 + 0), ((uint8_t)4ULL));
                    if (t302) {
                        {
                            gm_b16_8 t304 = gm_l5;
                            gm_b16_8 t305 = gm_l6;
                            gm_t6j6smj7qoctu t306;
                            GM_ST(gm_b16_8, ((uint8_t *)&t306 + 0), t304);
                            GM_ST(gm_b16_8, ((uint8_t *)&t306 + 16), t305);
                            gm_f_format_e0d41cg((void *)&gm_l46, (void *)NULL, cx, NULL, &t306);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t307 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
                        if (!t307) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l46) + 8, gm_src4pc97o88riqc, 162, 41);
                        gm_b16_8 t308 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l46) + 8));
                        gm_b16_8 t309 = GM_LD(gm_b16_8, ((uint8_t *)&t308));
                        gm_l28 = t309;
                    } else {
                        {
                            gm_b16_8 t310 = gm_l5;
                            gm_b16_8 t311 = gm_l6;
                            gm_t6j6smj7qoctu t312;
                            GM_ST(gm_b16_8, ((uint8_t *)&t312 + 0), t310);
                            GM_ST(gm_b16_8, ((uint8_t *)&t312 + 16), t311);
                            gm_f_format_4q9e8cc((void *)&gm_l47, (void *)NULL, cx, NULL, &t312);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t313 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
                        if (!t313) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l47) + 8, gm_src4pc97o88riqc, 162, 81);
                        gm_b16_8 t314 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l47) + 8));
                        gm_b16_8 t315 = GM_LD(gm_b16_8, ((uint8_t *)&t314));
                        gm_l28 = t315;
                    }
                    {
                        void * t316 = gm_l0;
                        gm_t88rcucg1b7uk t317 = gm_l2;
                        gm_b16_8 t318 = gm_l8;
                        gm_b16_8 t319 = gm_l28;
                        gm_t6j6smj7qoctu t320;
                        GM_ST(gm_b16_8, ((uint8_t *)&t320 + 0), t318);
                        GM_ST(gm_b16_8, ((uint8_t *)&t320 + 16), t319);
                        gm_f_format_3vtl3te((void *)&gm_l48, (void *)NULL, cx, NULL, &t320);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t321 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                        if (!t321) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l48) + 8, gm_src4pc97o88riqc, 163, 30);
                        gm_b16_8 t322 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l48) + 8));
                        gm_b16_8 t323 = GM_LD(gm_b16_8, ((uint8_t *)&t322));
                        gm_b16_8 t324 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t316, t317, t323);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t324);
                        goto gm_fdone;
                    }
                }
gm_lb24:;
            }
            {
                {
                    {
                        {
                            uint8_t t325 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t326 = (uint8_t)(t325 == ((uint8_t)7ULL));
                            if (!t326) {
                                goto gm_lb30;
                            }
                            goto gm_lb29;
                        }
gm_lb30:;
                        {
                            uint8_t t327 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t328 = (uint8_t)(t327 == ((uint8_t)8ULL));
                            if (!t328) {
                                goto gm_lb31;
                            }
                            goto gm_lb29;
                        }
gm_lb31:;
                        {
                            uint8_t t329 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t330 = (uint8_t)(t329 == ((uint8_t)9ULL));
                            if (!t330) {
                                goto gm_lb32;
                            }
                            goto gm_lb29;
                        }
gm_lb32:;
                        goto gm_lb28;
                    }
gm_lb29:;
                    {
                        {
                            uint8_t t331 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t332 = (uint8_t)(t331 == ((uint8_t)7ULL));
                            if (!t332) {
                                goto gm_lb34;
                            }
                            gm_b16_8 t333;
                            GM_ST(void *, (uint8_t *)&t333, (void *)(gm_smerqm8g793l6));
                            GM_ST(uintptr_t, (uint8_t *)&t333 + 8, (uintptr_t)(1));
                            gm_l29 = t333;
                            goto gm_lb33;
                        }
gm_lb34:;
                        {
                            uint8_t t334 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t335 = (uint8_t)(t334 == ((uint8_t)8ULL));
                            if (!t335) {
                                goto gm_lb35;
                            }
                            gm_b16_8 t336;
                            GM_ST(void *, (uint8_t *)&t336, (void *)(gm_s2u5posfkbgnq));
                            GM_ST(uintptr_t, (uint8_t *)&t336 + 8, (uintptr_t)(1));
                            gm_l29 = t336;
                            goto gm_lb33;
                        }
gm_lb35:;
                        {
                            gm_b16_8 t337;
                            GM_ST(void *, (uint8_t *)&t337, (void *)(gm_srs1vrn2uaams));
                            GM_ST(uintptr_t, (uint8_t *)&t337 + 8, (uintptr_t)(1));
                            gm_l29 = t337;
                            goto gm_lb33;
                        }
gm_lb36:;
                        gm_trap("no match arm matched the scrutinee", gm_src4pc97o88riqc, 166, 25);
                    }
gm_lb33:;
                    {
                        void * t338 = gm_l0;
                        gm_t88rcucg1b7uk t339 = gm_l2;
                        gm_b16_8 t340 = gm_l8;
                        gm_b16_8 t341 = gm_l5;
                        gm_b16_8 t342 = gm_l29;
                        gm_b16_8 t343 = gm_l6;
                        gm_t0uuhaljsf410 t344;
                        GM_ST(gm_b16_8, ((uint8_t *)&t344 + 0), t340);
                        GM_ST(gm_b16_8, ((uint8_t *)&t344 + 16), t341);
                        GM_ST(gm_b16_8, ((uint8_t *)&t344 + 32), t342);
                        GM_ST(gm_b16_8, ((uint8_t *)&t344 + 48), t343);
                        gm_f_format_tth3l1d((void *)&gm_l49, (void *)NULL, cx, NULL, &t344);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t345 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
                        if (!t345) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l49) + 8, gm_src4pc97o88riqc, 171, 30);
                        gm_b16_8 t346 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l49) + 8));
                        gm_b16_8 t347 = GM_LD(gm_b16_8, ((uint8_t *)&t346));
                        gm_b16_8 t348 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t338, t339, t347);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t348);
                        goto gm_fdone;
                    }
                }
gm_lb28:;
                {
                    gm_trap("reached `@unreachable()`", gm_src4pc97o88riqc, 173, 14);
                }
gm_lb37:;
                gm_trap("no match arm matched the scrutinee", gm_src4pc97o88riqc, 104, 5);
            }
        }
gm_lb1:;
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
inline __attribute__((aligned(4))) void gm_f_int_info_2tcdf1n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_int_info_2tcdf1n F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint8_t gm_l2; /* signed */
    uint32_t gm_l3; /* bits */
    uint64_t gm_l4; /* storage */
    GM_STACK_CHECK(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t5v8n6qrn4rh4 gm_l5;
    {
        {
            {
                void * t4 = gm_l0;
                void * t5 = GM_LD(void *, ((uint8_t *)t4));
                void * t6 = (void *)(((uint8_t *)t5) + 8);
                gm_t88rcucg1b7uk t7 = gm_l1;
                gm_f_repr_53quqki((void *)&gm_l5, (void *)NULL, cx, NULL, t6, t7);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t9 = (uint8_t)(t8 == ((uint8_t)0ULL));
            if (!t9) {
                goto gm_lb2;
            }
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 8));
            gm_l2 = t10;
            uint32_t t11 = GM_LD(uint32_t, ((((uint8_t *)&gm_l5) + 8) + 4));
            gm_l3 = t11;
            uint64_t t12 = GM_LD(uint64_t, ((((uint8_t *)&gm_l5) + 8) + 8));
            gm_l4 = t12;
            {
                uint8_t t13 = gm_l2;
                uint32_t t14 = gm_l3;
                uint64_t t15 = gm_l4;
                gm_tuglhjjmj2g58 t16;
                GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t16 + 2, 0);
                GM_ST(uint8_t, ((uint8_t *)&t16 + 0), t13);
                GM_ST(uint32_t, ((uint8_t *)&t16 + 4), t14);
                GM_ST(uint64_t, ((uint8_t *)&t16 + 8), t15);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tqcc6dmlne11b *)gm_out) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tqcc6dmlne11b *)gm_out) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tqcc6dmlne11b *)gm_out) + 20, 0);
                GM_ST(gm_tuglhjjmj2g58, ((uint8_t *)&(*(gm_tqcc6dmlne11b *)gm_out)), t16);
                ((uint8_t *)&(*(gm_tqcc6dmlne11b *)gm_out))[16] = 1;
                goto gm_fdone;
            }
        }
gm_lb2:;
        memset(&(*(gm_tqcc6dmlne11b *)gm_out), 0, sizeof (*(gm_tqcc6dmlne11b *)gm_out));
        goto gm_fdone;
    }
gm_lb1:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_normalize_jch381e(void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0, gm_b16_8 a1) {
    struct gm_fr_normalize_jch381e F;
    gm_tuglhjjmj2g58 gm_l0; /* ii */
    gm_b16_8 gm_l1; /* v */
    uint64_t gm_l2; /* shift */
    GM_STACK_CHECK(&F, 556u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l3;
    gm_tabb710d9mq2e gm_l4;
    gm_tuglhjjmj2g58 t2 = gm_l0;
    uint8_t t3 = gm_f_ii_exact_g6a19dp(GM_CONV(gm_cs2lqoac0a5ng4, t2));
    if (t3) {
        gm_b16_8 t4 = gm_l1;
        F.ret = t4;
        goto gm_fret;
    }
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t r6; bool o7 = __builtin_mul_overflow(t5, ((uint64_t)8ULL), &r6);
    if (o7) gm_trap("integer overflow in `*`", gm_src4pc97o88riqc, 57, 14);
    uint64_t t8 = r6;
    uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
    uint64_t t10 = (uint64_t)t9;
    uint64_t r11; bool o12 = __builtin_sub_overflow(t8, t10, &r11);
    if (o12) gm_trap("integer overflow in `-`", gm_src4pc97o88riqc, 57, 14);
    uint64_t t13 = r11;
    gm_l2 = t13;
    uint8_t t14 = GM_LD(uint8_t, ((uint8_t *)&gm_l0));
    if (t14) {
        {
            gm_tuglhjjmj2g58 t15 = gm_l0;
            gm_b16_8 t16 = gm_f_ii_cty_i2b4e03(GM_CONV(gm_cs2lqoac0a5ng4, t15));
            gm_tuglhjjmj2g58 t17 = gm_l0;
            gm_b16_8 t18 = gm_f_ii_ucty_b40h5ot(GM_CONV(gm_cs2lqoac0a5ng4, t17));
            gm_b16_8 t19 = gm_l1;
            uint64_t t20 = gm_l2;
            uint64_t t21 = gm_l2;
            gm_tg23pq9a29gh1 t22;
            GM_ST(gm_b16_8, ((uint8_t *)&t22 + 0), t16);
            GM_ST(gm_b16_8, ((uint8_t *)&t22 + 16), t18);
            GM_ST(gm_b16_8, ((uint8_t *)&t22 + 32), t19);
            GM_ST(uint64_t, ((uint8_t *)&t22 + 48), t20);
            GM_ST(uint64_t, ((uint8_t *)&t22 + 56), t21);
            gm_f_format_u9gq54a((void *)&gm_l3, (void *)NULL, cx, NULL, &t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t23 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
        if (!t23) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_src4pc97o88riqc, 59, 16);
        gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
        gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
        F.ret = t25;
        goto gm_fret;
    }
    {
        gm_tuglhjjmj2g58 t26 = gm_l0;
        gm_b16_8 t27 = gm_f_ii_cty_i2b4e03(GM_CONV(gm_cs2lqoac0a5ng4, t26));
        gm_b16_8 t28 = gm_l1;
        gm_tuglhjjmj2g58 t29 = gm_l0;
        gm_b16_8 t30 = gm_f_ii_cty_i2b4e03(GM_CONV(gm_cs2lqoac0a5ng4, t29));
        uint32_t t31 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
        gm_tj4tbafenpsjm t32;
        GM_ST(uint32_t, (uint8_t *)&t32 + 52, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t32 + 0), t27);
        GM_ST(gm_b16_8, ((uint8_t *)&t32 + 16), t28);
        GM_ST(gm_b16_8, ((uint8_t *)&t32 + 32), t30);
        GM_ST(uint32_t, ((uint8_t *)&t32 + 48), t31);
        gm_f_format_1ck6tsm((void *)&gm_l4, (void *)NULL, cx, NULL, &t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t33) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 8, gm_src4pc97o88riqc, 61, 12);
    gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
    gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
    F.ret = t35;
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
__attribute__((aligned(4))) void gm_f_unary_value_0v9h22p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5) {
    struct gm_fr_unary_value_0v9h22p F;
    void * gm_l0; /* fg */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_t9qkh0rsooc8d gm_l3; /* op */
    gm_t88rcucg1b7uk gm_l4; /* xt */
    gm_b16_8 gm_l5; /* v */
    gm_tuglhjjmj2g58 gm_l6; /* ii */
    gm_b16_8 gm_l7; /* min */
    gm_b16_8 gm_l8; /* loc */
    gm_b16_8 gm_l9; /* r */
    gm_b16_8 gm_l10; /* r2 */
    gm_tuglhjjmj2g58 gm_l11; /* ii */
    gm_b16_8 gm_l12; /* r */
    GM_STACK_CHECK(&F, 1676u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_tabb710d9mq2e gm_l13;
    gm_t5v8n6qrn4rh4 gm_l14;
    gm_tabb710d9mq2e gm_l15;
    gm_tqcc6dmlne11b gm_l16;
    gm_tabb710d9mq2e gm_l17;
    gm_tabb710d9mq2e gm_l18;
    gm_tqcc6dmlne11b gm_l19;
    gm_tabb710d9mq2e gm_l20;
    gm_tabb710d9mq2e gm_l21;
    {
        {
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)2ULL));
            if (!t11) {
                goto gm_lb2;
            }
            {
                void * t12 = gm_l0;
                gm_t88rcucg1b7uk t13 = gm_l2;
                gm_b16_8 t14 = gm_l5;
                gm_tnpifv20irnph t15;
                GM_ST(gm_b16_8, ((uint8_t *)&t15 + 0), t14);
                gm_f_format_9221qv6((void *)&gm_l13, (void *)NULL, cx, NULL, t15);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                if (!t16) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_src4pc97o88riqc, 67, 34);
                gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
                gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
                gm_b16_8 t19 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t12, t13, t18);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t19);
                goto gm_fdone;
            }
        }
gm_lb2:;
        {
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t21 = (uint8_t)(t20 == ((uint8_t)0ULL));
            if (!t21) {
                goto gm_lb3;
            }
            void * t22 = gm_l0;
            void * t23 = GM_LD(void *, ((uint8_t *)t22));
            void * t24 = (void *)(((uint8_t *)t23) + 8);
            gm_t88rcucg1b7uk t25 = gm_l4;
            gm_f_repr_53quqki((void *)&gm_l14, (void *)NULL, cx, NULL, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                {
                    uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
                    uint8_t t27 = (uint8_t)(t26 == ((uint8_t)1ULL));
                    if (!t27) {
                        goto gm_lb5;
                    }
                    {
                        void * t28 = gm_l0;
                        gm_t88rcucg1b7uk t29 = gm_l2;
                        gm_b16_8 t30 = gm_l5;
                        gm_tnpifv20irnph t31;
                        GM_ST(gm_b16_8, ((uint8_t *)&t31 + 0), t30);
                        gm_f_format_dlemvkb((void *)&gm_l15, (void *)NULL, cx, NULL, t31);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                        if (!t32) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_src4pc97o88riqc, 69, 43);
                        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
                        gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
                        gm_b16_8 t35 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t28, t29, t34);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t35);
                        goto gm_fdone;
                    }
                }
gm_lb5:;
                {
                    uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
                    uint8_t t37 = (uint8_t)(t36 == ((uint8_t)0ULL));
                    if (!t37) {
                        goto gm_lb6;
                    }
                    {
                        void * t38 = gm_l0;
                        gm_t88rcucg1b7uk t39 = gm_l4;
                        gm_f_int_info_2tcdf1n((void *)&gm_l16, (void *)NULL, cx, NULL, t38, t39);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t40 = (uint8_t)((((uint8_t *)&gm_l16))[16] != 0);
                    if (!t40) gm_trap("unwrap of null", gm_src4pc97o88riqc, 71, 23);
                    gm_tuglhjjmj2g58 t41 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&gm_l16));
                    gm_l6 = t41;
                    {
                        void * t42 = gm_l0;
                        gm_t88rcucg1b7uk t43 = gm_l4;
                        gm_tuglhjjmj2g58 t44 = gm_l6;
                        gm_tbci5ospk5nnn t45;
                        gm_f_ii_min_lku7pdi((void *)&t45, (void *)NULL, cx, NULL, t44);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t46 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t42, t43, &t45);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l7 = t46;
                    }
                    void * t47 = gm_l0;
                    void * t48 = GM_LD(void *, ((uint8_t *)t47));
                    uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)t48) + 1136));
                    if (t49) {
                        void * t50 = gm_l0;
                        void * t51 = GM_LD(void *, ((uint8_t *)t50));
                        gm_tl874t2atnlh3 t52 = gm_l1;
                        gm_b16_8 t53 = gm_f_src_loc_lrhtj7i((void *)NULL, cx, NULL, t51, t52);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l8 = t53;
                        void * t54 = gm_l0;
                        gm_b16_8 t55 = gm_l5;
                        gm_b16_8 t56 = gm_l7;
                        gm_b16_8 t57 = gm_l8;
                        gm_tfeab279c16rr t58;
                        GM_ST(gm_b16_8, ((uint8_t *)&t58 + 0), t55);
                        GM_ST(gm_b16_8, ((uint8_t *)&t58 + 16), t56);
                        GM_ST(gm_b16_8, ((uint8_t *)&t58 + 32), t57);
                        gm_b0_1 t59 = gm_f_linef_fdhsfa1((void *)NULL, cx, NULL, t54, &t58);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        gm_tuglhjjmj2g58 t60 = gm_l6;
                        gm_b16_8 t61 = gm_f_ii_cty_i2b4e03(GM_CONV(gm_cs2lqoac0a5ng4, t60));
                        gm_tuglhjjmj2g58 t62 = gm_l6;
                        gm_b16_8 t63 = gm_f_ii_ucty_b40h5ot(GM_CONV(gm_cs2lqoac0a5ng4, t62));
                        gm_tuglhjjmj2g58 t64 = gm_l6;
                        gm_b16_8 t65 = gm_f_ii_ucty_b40h5ot(GM_CONV(gm_cs2lqoac0a5ng4, t64));
                        gm_b16_8 t66 = gm_l5;
                        gm_t0uuhaljsf410 t67;
                        GM_ST(gm_b16_8, ((uint8_t *)&t67 + 0), t61);
                        GM_ST(gm_b16_8, ((uint8_t *)&t67 + 16), t63);
                        GM_ST(gm_b16_8, ((uint8_t *)&t67 + 32), t65);
                        GM_ST(gm_b16_8, ((uint8_t *)&t67 + 48), t66);
                        gm_f_format_5r372o6((void *)&gm_l17, (void *)NULL, cx, NULL, &t67);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t68 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                    if (!t68) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_src4pc97o88riqc, 77, 22);
                    gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
                    gm_l9 = t69;
                    gm_tuglhjjmj2g58 t70 = gm_l6;
                    gm_b16_8 t71 = gm_l9;
                    gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
                    gm_b16_8 t73 = gm_f_normalize_jch381e((void *)NULL, cx, NULL, t70, t72);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l10 = t73;
                    {
                        void * t74 = gm_l0;
                        gm_t88rcucg1b7uk t75 = gm_l2;
                        gm_b16_8 t76 = gm_l10;
                        gm_b16_8 t77 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t74, t75, t76);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t77);
                        goto gm_fdone;
                    }
                }
gm_lb6:;
                {
                    gm_tl874t2atnlh3 t78 = gm_l1;
                    void * t79 = gm_l0;
                    void * t80 = GM_LD(void *, ((uint8_t *)t79));
                    void * t81 = (void *)(((uint8_t *)t80) + 8);
                    gm_t88rcucg1b7uk t82 = gm_l4;
                    gm_b16_8 t83 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t81, t82);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t84;
                    GM_ST(gm_b16_8, ((uint8_t *)&t84 + 0), t83);
                    gm_f_format_las1g7o((void *)&gm_l18, (void *)NULL, cx, NULL, t84);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t85 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                    if (!t85) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_src4pc97o88riqc, 81, 44);
                    gm_b16_8 t86 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
                    gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&t86));
                    gm_t08olq97rscn3 t88;
                    gm_f_unsupported_531q51r((void *)&t88, (void *)NULL, cx, NULL, t78, t87);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t88);
                    goto gm_fdone;
                }
gm_lb7:;
                gm_trap("no match arm matched the scrutinee", gm_src4pc97o88riqc, 68, 17);
            }
gm_lb4:;
        }
gm_lb3:;
        {
            uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t90 = (uint8_t)(t89 == ((uint8_t)1ULL));
            if (!t90) {
                goto gm_lb8;
            }
            {
                void * t91 = gm_l0;
                gm_t88rcucg1b7uk t92 = gm_l4;
                gm_f_int_info_2tcdf1n((void *)&gm_l19, (void *)NULL, cx, NULL, t91, t92);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t93 = (uint8_t)((((uint8_t *)&gm_l19))[16] != 0);
            if (t93) {
                gm_tuglhjjmj2g58 t94 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&gm_l19));
                gm_l11 = t94;
            } else {
                gm_tl874t2atnlh3 t95 = gm_l1;
                void * t96 = gm_l0;
                void * t97 = GM_LD(void *, ((uint8_t *)t96));
                void * t98 = (void *)(((uint8_t *)t97) + 8);
                gm_t88rcucg1b7uk t99 = gm_l4;
                gm_b16_8 t100 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t98, t99);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t101;
                GM_ST(gm_b16_8, ((uint8_t *)&t101 + 0), t100);
                gm_f_format_loa1toi((void *)&gm_l20, (void *)NULL, cx, NULL, t101);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t102 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                if (!t102) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 8, gm_src4pc97o88riqc, 84, 66);
                gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
                gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&t103));
                gm_t08olq97rscn3 t105;
                gm_f_unsupported_531q51r((void *)&t105, (void *)NULL, cx, NULL, t95, t104);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t105);
                goto gm_fdone;
            }
            {
                gm_tuglhjjmj2g58 t106 = gm_l11;
                gm_tuglhjjmj2g58 t107 = gm_l11;
                gm_b16_8 t108 = gm_f_ii_cty_i2b4e03(GM_CONV(gm_cs2lqoac0a5ng4, t107));
                gm_b16_8 t109 = gm_l5;
                gm_t6j6smj7qoctu t110;
                GM_ST(gm_b16_8, ((uint8_t *)&t110 + 0), t108);
                GM_ST(gm_b16_8, ((uint8_t *)&t110 + 16), t109);
                gm_f_format_oe49e3n((void *)&gm_l21, (void *)NULL, cx, NULL, &t110);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t111 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                if (!t111) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 8, gm_src4pc97o88riqc, 85, 32);
                gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
                gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)&t112));
                gm_b16_8 t114 = gm_f_normalize_jch381e((void *)NULL, cx, NULL, t106, t113);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l12 = t114;
            }
            {
                void * t115 = gm_l0;
                gm_t88rcucg1b7uk t116 = gm_l2;
                gm_b16_8 t117 = gm_l12;
                gm_b16_8 t118 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t115, t116, t117);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t118);
                goto gm_fdone;
            }
        }
gm_lb8:;
        gm_trap("no match arm matched the scrutinee", gm_src4pc97o88riqc, 66, 5);
    }
gm_lb1:;
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

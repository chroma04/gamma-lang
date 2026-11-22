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
typedef struct gm_csc7lc6jlkqkuq { uint64_t f0; uint64_t f1; } gm_csc7lc6jlkqkuq;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
_Static_assert(sizeof(gm_csc7lc6jlkqkuq) == 16 && _Alignof(gm_csc7lc6jlkqkuq) == 8 && offsetof(gm_csc7lc6jlkqkuq, f0) == 0 && offsetof(gm_csc7lc6jlkqkuq, f1) == 8, "layout of `BlobPart`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; uint8_t v2; gm_t9qkh0rsooc8d v3; gm_t9qkh0rsooc8d v4; gm_tcc1rq4n1avb2 v5; uint32_t v6; uint32_t v7; } f1; } gm_t8588ma2cr9d7;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 a[1]; } gm_tpmdr2hmdhsqr;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; uint8_t v1; } f1; } gm_t1ec8br9ddkav;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
_Static_assert(sizeof(gm_t8588ma2cr9d7) == 12 && _Alignof(gm_t8588ma2cr9d7) == 4 && offsetof(gm_t8588ma2cr9d7, f0) == 0 && offsetof(gm_t8588ma2cr9d7, f1) == 4, "layout of `Builtin`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpmdr2hmdhsqr) == 16 && _Alignof(gm_tpmdr2hmdhsqr) == 8 && offsetof(gm_tpmdr2hmdhsqr, a) == 0, "layout of `[1]string`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_t1ec8br9ddkav) == 32 && _Alignof(gm_t1ec8br9ddkav) == 8 && offsetof(gm_t1ec8br9ddkav, f0) == 0 && offsetof(gm_t1ec8br9ddkav, f1) == 8, "layout of `Flow!bool`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
struct gm_fr_copy_from_f7j6p05 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_deinit_tq06jtm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_2p7d2kv {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fill_abmf5g5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_46el73v {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_get_u64_tgpb2vh {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_kind_htv6093 {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_fufbgf7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mark_kpppm1j {
    gm_frame h;
    gm_t2lqu8jsj511d ret;
};
struct gm_fr_push_rluch9r {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_rd_word_8flcq6o {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_regrow_bslikk9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_release_jn5kqdi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_tmp_8k6u9c9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_truncate_j7f0pmi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_unsupported_70vvk1e {
    gm_frame h;
    gm_t76123gfet73g ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_vector_builtin_3fn933h {
    gm_frame h;
    gm_trqfvmjtt4sij l23; /* args */
    gm_trqfvmjtt4sij l51; /* args */
    gm_tcfa6l7reoosn ret;
};
__attribute__((aligned(4))) void gm_f_apply_builtin_41dffti(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t8588ma2cr9d7 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7);
__attribute__((aligned(4))) void gm_f_binop_nrsq3mp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_ts6oqppjh0ba3 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7, gm_b16_8 a8);
__attribute__((aligned(4))) void gm_f_cmp_op_mflafbf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_ts6oqppjh0ba3 a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_convert_is6an2k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t8588ma2cr9d7 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_deinit_tq06jtm(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2p7d2kv(void);
__attribute__((aligned(4))) void gm_f_equal_e5vf82h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1);
inline __attribute__((aligned(4))) void gm_f_format_46el73v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t8588ma2cr9d7 gm_f_lane_scalar_hgnqjfe(gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) void gm_f_list_fufbgf7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_rluch9r(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_bslikk9(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1);
__attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b16_8 gm_f_tmp_next_dalgo1f(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_truncate_j7f0pmi(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_unsupported_70vvk1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) void gm_f_vector_builtin_3fn933h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t8588ma2cr9d7 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7);
extern const uint8_t gm_s1tv0n9hk7497[58];
extern const uint8_t gm_s4lpfn3di4rp9[66];
extern const uint8_t gm_s5e5797b1jurs[20];
extern const uint8_t gm_scqqkvgm1j3cs[65];
extern const uint8_t gm_slnkfiltk5b43[57];
extern const uint8_t gm_sm94b3smoinpd[60];
extern const uint8_t gm_smsrao7h22muv[61];
extern const uint8_t gm_so6lu2e8ofi9v[69];
extern const uint8_t gm_spnr4ftjbm9m9[61];
extern const uint8_t gm_spqlqjii789ln[57];
extern const uint8_t gm_sqofr58g480ri[39];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcnqtpfma6qe4a[];
extern const char gm_srcns60k96jj1mt[];
extern const char gm_srcpit8mdn0781q[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_ssvci9220kuoj[45];
extern const gm_tmpl gm_tplav8bdvvt8rem;
extern const gm_type gm_tyo86ks9qkvt;
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_copy_from_f7j6p05 F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* src */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t4 = ((t2 == t3));
        if (t4) {
        } else {
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sqofr58g480ri));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(38));
            { gm_buf mb = {0}; size_t ml = GM_LD(uintptr_t, ((uint8_t *)&t5) + 8); const uint8_t *mp = GM_LD(void *, ((uint8_t *)&t5)); if (ml == 16 && memcmp(mp, "assertion failed", 16) == 0) gm_buf_push(&mb, mp, ml); else { gm_buf_push(&mb, "assertion failed: ", 18); gm_buf_push(&mb, mp, ml); } gm_trap_bytes(mb.data, mb.len, gm_srcc24pnihhgkug, 24, 5); }
        }
    }
    gm_b16_8 t6 = gm_l0;
    gm_b16_8 t7 = gm_l1;
    if (GM_LD(uintptr_t, ((uint8_t *)&t6) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t6)), GM_LD(void *, ((uint8_t *)&t7)), GM_LD(uintptr_t, ((uint8_t *)&t6) + 8) * 1);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_deinit_tq06jtm(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_tq06jtm F;
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
        uint64_t r15; bool o16 = __builtin_mul_overflow(((uint64_t)16ULL), t14, &r15);
        if (o16) gm_trap("integer overflow in `*`", gm_srcrgc9bkquf6nr, 146, 64);
        uint64_t t17 = r15;
        void * t18 = t7;
        gm_b0_1 t19;
        ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t18))(t9, t12, t17, ((uint64_t)8ULL));
    }
    void * t20 = gm_l0;
    gm_b16_8 t21 = gm_f_empty_slice_2p7d2kv();
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2p7d2kv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_2p7d2kv F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fill_abmf5g5 F;
    gm_b16_8 gm_l0; /* s */
    uint8_t gm_l1; /* x */
    void * gm_l2; /* p */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t7 = GM_LD(void *, ((uint8_t *)&gm_l0));
    gm_l2 = t7;
    gm_l4 = ((uint64_t)0ULL);
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_l3 = t8;
    {
        for (;;) {
            uint64_t t9 = gm_l4;
            uint64_t t10 = gm_l3;
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t12 = gm_l4;
                        gm_l5 = t12;
                        void * t13 = gm_l2;
                        uint64_t t14 = gm_l5;
                        uint8_t t15 = gm_l1;
                        GM_ST(uint8_t, (((uint8_t *)t13) + (uintptr_t)t14 * 1), t15);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc24pnihhgkug, 18, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t16 = gm_l4;
            uint64_t r17; bool o18 = __builtin_add_overflow(t16, ((uint64_t)1ULL), &r17);
            uint64_t t19 = r17;
            gm_l4 = t19;
        }
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_format_46el73v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_46el73v F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slnkfiltk5b43));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(56));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplav8bdvvt8rem), t3);
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
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_u64_tgpb2vh F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t gm_l2;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t3 = (t2 <= ((uint64_t)8ULL));
    if (t3) {
        void * t4 = gm_l0;
        gm_b16_8 t5 = gm_l1;
        uint64_t t6 = gm_f_rd_word_8flcq6o(t4, t5);
        gm_l2 = t6;
    } else {
        void * t7 = gm_l0;
        gm_b16_8 t8 = gm_l1;
        unsigned __int128 t9 = gm_f_get_uint_bq8c4b7(t7, t8);
        uint64_t t10 = (uint64_t)t9;
        gm_l2 = t10;
    }
    uint64_t t11 = gm_l2;
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_htv6093 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 8));
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
inline __attribute__((aligned(4))) void gm_f_list_fufbgf7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_fufbgf7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_2p7d2kv();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mark_kpppm1j F;
    void * gm_l0; /* m */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, ((((uint8_t *)t2) + 360) + 40));
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t4) + 360) + 48));
    gm_t2lqu8jsj511d t6;
    GM_ST(uint64_t, ((uint8_t *)&t6 + 0), t3);
    GM_ST(uint64_t, ((uint8_t *)&t6 + 8), t5);
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
gm_fprop:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_rluch9r(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_rluch9r F;
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
        gm_b16_8 t9 = gm_f_empty_slice_2p7d2kv();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_bslikk9(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_rd_word_8flcq6o F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)t2) + 89));
    if (t3) {
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t5 = ((t4 == ((uint64_t)8ULL)));
        if (t5) {
            void * t6 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t7)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t7, gm_srcnqtpfma6qe4a, 316, 36);
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)t6) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint64_t t9 = (uint64_t)t8;
            void * t10 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t11, gm_srcnqtpfma6qe4a, 316, 49);
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)t10) + (uintptr_t)((uint64_t)1ULL) * 1));
            uint64_t t13 = (uint64_t)t12;
            unsigned __int128 n14 = (unsigned __int128)((uint64_t)8ULL);
            if (n14 >= 64) gm_trap_shift((uint64_t)n14, "u64", gm_srcnqtpfma6qe4a, 316, 45);
            uint64_t r15 = (uint64_t)((uint64_t)t13 << (int)n14);
            bool o16 = (uint64_t)(r15 >> (int)n14) != t13;
            if (o16) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 45);
            uint64_t t17 = r15;
            uint64_t t18 = (uint64_t)(t9 | t17);
            void * t19 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t20)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t20, gm_srcnqtpfma6qe4a, 316, 68);
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)t19) + (uintptr_t)((uint64_t)2ULL) * 1));
            uint64_t t22 = (uint64_t)t21;
            unsigned __int128 n23 = (unsigned __int128)((uint64_t)16ULL);
            if (n23 >= 64) gm_trap_shift((uint64_t)n23, "u64", gm_srcnqtpfma6qe4a, 316, 64);
            uint64_t r24 = (uint64_t)((uint64_t)t22 << (int)n23);
            bool o25 = (uint64_t)(r24 >> (int)n23) != t22;
            if (o25) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 64);
            uint64_t t26 = r24;
            uint64_t t27 = (uint64_t)(t18 | t26);
            void * t28 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)3ULL) >= (uintptr_t)(t29)) gm_trap_index((uintptr_t)((uint64_t)3ULL), t29, gm_srcnqtpfma6qe4a, 316, 88);
            uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)t28) + (uintptr_t)((uint64_t)3ULL) * 1));
            uint64_t t31 = (uint64_t)t30;
            unsigned __int128 n32 = (unsigned __int128)((uint64_t)24ULL);
            if (n32 >= 64) gm_trap_shift((uint64_t)n32, "u64", gm_srcnqtpfma6qe4a, 316, 84);
            uint64_t r33 = (uint64_t)((uint64_t)t31 << (int)n32);
            bool o34 = (uint64_t)(r33 >> (int)n32) != t31;
            if (o34) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 84);
            uint64_t t35 = r33;
            uint64_t t36 = (uint64_t)(t27 | t35);
            void * t37 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)4ULL) >= (uintptr_t)(t38)) gm_trap_index((uintptr_t)((uint64_t)4ULL), t38, gm_srcnqtpfma6qe4a, 316, 108);
            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)t37) + (uintptr_t)((uint64_t)4ULL) * 1));
            uint64_t t40 = (uint64_t)t39;
            unsigned __int128 n41 = (unsigned __int128)((uint64_t)32ULL);
            if (n41 >= 64) gm_trap_shift((uint64_t)n41, "u64", gm_srcnqtpfma6qe4a, 316, 104);
            uint64_t r42 = (uint64_t)((uint64_t)t40 << (int)n41);
            bool o43 = (uint64_t)(r42 >> (int)n41) != t40;
            if (o43) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 104);
            uint64_t t44 = r42;
            uint64_t t45 = (uint64_t)(t36 | t44);
            void * t46 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)5ULL) >= (uintptr_t)(t47)) gm_trap_index((uintptr_t)((uint64_t)5ULL), t47, gm_srcnqtpfma6qe4a, 316, 128);
            uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)t46) + (uintptr_t)((uint64_t)5ULL) * 1));
            uint64_t t49 = (uint64_t)t48;
            unsigned __int128 n50 = (unsigned __int128)((uint64_t)40ULL);
            if (n50 >= 64) gm_trap_shift((uint64_t)n50, "u64", gm_srcnqtpfma6qe4a, 316, 124);
            uint64_t r51 = (uint64_t)((uint64_t)t49 << (int)n50);
            bool o52 = (uint64_t)(r51 >> (int)n50) != t49;
            if (o52) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 124);
            uint64_t t53 = r51;
            uint64_t t54 = (uint64_t)(t45 | t53);
            void * t55 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)6ULL) >= (uintptr_t)(t56)) gm_trap_index((uintptr_t)((uint64_t)6ULL), t56, gm_srcnqtpfma6qe4a, 316, 148);
            uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)t55) + (uintptr_t)((uint64_t)6ULL) * 1));
            uint64_t t58 = (uint64_t)t57;
            unsigned __int128 n59 = (unsigned __int128)((uint64_t)48ULL);
            if (n59 >= 64) gm_trap_shift((uint64_t)n59, "u64", gm_srcnqtpfma6qe4a, 316, 144);
            uint64_t r60 = (uint64_t)((uint64_t)t58 << (int)n59);
            bool o61 = (uint64_t)(r60 >> (int)n59) != t58;
            if (o61) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 144);
            uint64_t t62 = r60;
            uint64_t t63 = (uint64_t)(t54 | t62);
            void * t64 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)7ULL) >= (uintptr_t)(t65)) gm_trap_index((uintptr_t)((uint64_t)7ULL), t65, gm_srcnqtpfma6qe4a, 316, 168);
            uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)t64) + (uintptr_t)((uint64_t)7ULL) * 1));
            uint64_t t67 = (uint64_t)t66;
            unsigned __int128 n68 = (unsigned __int128)((uint64_t)56ULL);
            if (n68 >= 64) gm_trap_shift((uint64_t)n68, "u64", gm_srcnqtpfma6qe4a, 316, 164);
            uint64_t r69 = (uint64_t)((uint64_t)t67 << (int)n68);
            bool o70 = (uint64_t)(r69 >> (int)n68) != t67;
            if (o70) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 164);
            uint64_t t71 = r69;
            uint64_t t72 = (uint64_t)(t63 | t71);
            F.ret = t72;
            goto gm_fret;
        }
        uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t74 = ((t73 == ((uint64_t)4ULL)));
        if (t74) {
            void * t75 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t76)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t76, gm_srcnqtpfma6qe4a, 317, 36);
            uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)t75) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint64_t t78 = (uint64_t)t77;
            void * t79 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t80)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t80, gm_srcnqtpfma6qe4a, 317, 49);
            uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)t79) + (uintptr_t)((uint64_t)1ULL) * 1));
            uint64_t t82 = (uint64_t)t81;
            unsigned __int128 n83 = (unsigned __int128)((uint64_t)8ULL);
            if (n83 >= 64) gm_trap_shift((uint64_t)n83, "u64", gm_srcnqtpfma6qe4a, 317, 45);
            uint64_t r84 = (uint64_t)((uint64_t)t82 << (int)n83);
            bool o85 = (uint64_t)(r84 >> (int)n83) != t82;
            if (o85) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 45);
            uint64_t t86 = r84;
            uint64_t t87 = (uint64_t)(t78 | t86);
            void * t88 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t89)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t89, gm_srcnqtpfma6qe4a, 317, 68);
            uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)t88) + (uintptr_t)((uint64_t)2ULL) * 1));
            uint64_t t91 = (uint64_t)t90;
            unsigned __int128 n92 = (unsigned __int128)((uint64_t)16ULL);
            if (n92 >= 64) gm_trap_shift((uint64_t)n92, "u64", gm_srcnqtpfma6qe4a, 317, 64);
            uint64_t r93 = (uint64_t)((uint64_t)t91 << (int)n92);
            bool o94 = (uint64_t)(r93 >> (int)n92) != t91;
            if (o94) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 64);
            uint64_t t95 = r93;
            uint64_t t96 = (uint64_t)(t87 | t95);
            void * t97 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)3ULL) >= (uintptr_t)(t98)) gm_trap_index((uintptr_t)((uint64_t)3ULL), t98, gm_srcnqtpfma6qe4a, 317, 88);
            uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)t97) + (uintptr_t)((uint64_t)3ULL) * 1));
            uint64_t t100 = (uint64_t)t99;
            unsigned __int128 n101 = (unsigned __int128)((uint64_t)24ULL);
            if (n101 >= 64) gm_trap_shift((uint64_t)n101, "u64", gm_srcnqtpfma6qe4a, 317, 84);
            uint64_t r102 = (uint64_t)((uint64_t)t100 << (int)n101);
            bool o103 = (uint64_t)(r102 >> (int)n101) != t100;
            if (o103) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 84);
            uint64_t t104 = r102;
            uint64_t t105 = (uint64_t)(t96 | t104);
            F.ret = t105;
            goto gm_fret;
        }
        uint64_t t106 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t107 = ((t106 == ((uint64_t)1ULL)));
        if (t107) {
            void * t108 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t109)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t109, gm_srcnqtpfma6qe4a, 318, 36);
            uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)t108) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint64_t t111 = (uint64_t)t110;
            F.ret = t111;
            goto gm_fret;
        }
        uint64_t t112 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t113 = ((t112 == ((uint64_t)2ULL)));
        if (t113) {
            void * t114 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t115)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t115, gm_srcnqtpfma6qe4a, 319, 36);
            uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)t114) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint64_t t117 = (uint64_t)t116;
            void * t118 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t119 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t119)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t119, gm_srcnqtpfma6qe4a, 319, 49);
            uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)t118) + (uintptr_t)((uint64_t)1ULL) * 1));
            uint64_t t121 = (uint64_t)t120;
            unsigned __int128 n122 = (unsigned __int128)((uint64_t)8ULL);
            if (n122 >= 64) gm_trap_shift((uint64_t)n122, "u64", gm_srcnqtpfma6qe4a, 319, 45);
            uint64_t r123 = (uint64_t)((uint64_t)t121 << (int)n122);
            bool o124 = (uint64_t)(r123 >> (int)n122) != t121;
            if (o124) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 319, 45);
            uint64_t t125 = r123;
            uint64_t t126 = (uint64_t)(t117 | t125);
            F.ret = t126;
            goto gm_fret;
        }
    }
    void * t127 = gm_l0;
    gm_b16_8 t128 = gm_l1;
    unsigned __int128 t129 = gm_f_get_uint_bq8c4b7(t127, t128);
    uint64_t t130 = (uint64_t)t129;
    F.ret = t130;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_bslikk9(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_bslikk9 F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_release_jn5kqdi F;
    void * gm_l0; /* m */
    gm_t2lqu8jsj511d gm_l1; /* mk */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t2lqu8jsj511d, a1);
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, ((uint8_t *)&gm_l1));
    GM_ST(uint64_t, ((((uint8_t *)t2) + 360) + 40), t3);
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    GM_ST(uint64_t, ((((uint8_t *)t4) + 360) + 48), t5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_tmp_8k6u9c9 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* n */
    void * gm_l2; /* sc */
    gm_b16_8 gm_l3; /* chunk */
    uint64_t gm_l4; /* base */
    uint64_t gm_l5; /* top */
    uint64_t gm_l6; /* size */
    gm_b16_8 gm_l7; /* b */
    GM_STACK_CHECK(&F, 524u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l8;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 360);
    gm_l2 = t3;
    void * t4 = gm_l2;
    void * t5 = GM_LD(void *, ((uint8_t *)t4));
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t4) + 8));
    void * t7 = gm_l2;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 40));
    if ((uintptr_t)t8 >= (uintptr_t)(t6)) gm_trap_index((uintptr_t)t8, t6, gm_srcpit8mdn0781q, 387, 14);
    gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)t5) + (uintptr_t)t8 * 16));
    gm_l3 = t9;
    void * t10 = GM_LD(void *, ((uint8_t *)&gm_l3));
    uint64_t t11 = (uintptr_t)t10;
    gm_l4 = t11;
    uint64_t t12 = gm_l4;
    void * t13 = gm_l2;
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 48));
    uint64_t r15; bool o16 = __builtin_add_overflow(t12, t14, &r15);
    if (o16) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 391, 14);
    uint64_t t17 = r15;
    uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)7ULL), &r18);
    if (o19) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 391, 14);
    uint64_t t20 = r18;
    uint64_t t21 = gm_f_us_cuv01fp(((uint64_t)7ULL));
    uint64_t t22 = (uint64_t)~t21;
    uint64_t t23 = (uint64_t)(t20 & t22);
    uint64_t t24 = gm_l4;
    uint64_t r25; bool o26 = __builtin_sub_overflow(t23, t24, &r25);
    if (o26) gm_trap("integer overflow in `-`", gm_srcpit8mdn0781q, 391, 12);
    uint64_t t27 = r25;
    gm_l5 = t27;
    uint64_t t28 = gm_l1;
    uint64_t t29 = gm_f_us_cuv01fp(t28);
    gm_l6 = t29;
    uint64_t t30 = gm_l1;
    uint8_t t31 = ((t30 == ((uint64_t)0ULL)));
    if (t31) {
        gm_l8 = ((uint8_t)1);
    } else {
        uint64_t t32 = gm_l5;
        uint64_t t33 = gm_l6;
        uint64_t r34; bool o35 = __builtin_add_overflow(t32, t33, &r34);
        if (o35) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 393, 18);
        uint64_t t36 = r34;
        uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        uint8_t t38 = (t36 > t37);
        gm_l8 = t38;
    }
    uint8_t t39 = gm_l8;
    if (t39) {
        void * t40 = gm_l0;
        uint64_t t41 = gm_l1;
        gm_b16_8 t42 = gm_f_tmp_next_dalgo1f((void *)NULL, cx, NULL, t40, t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t42;
        goto gm_fret;
    }
    void * t43 = GM_LD(void *, ((uint8_t *)&gm_l3));
    uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint64_t t45 = gm_l5;
    uint64_t t46 = gm_l5;
    uint64_t t47 = gm_l6;
    uint64_t r48; bool o49 = __builtin_add_overflow(t46, t47, &r48);
    if (o49) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 394, 21);
    uint64_t t50 = r48;
    uint64_t t51 = (uintptr_t)t45;
    uint64_t t52 = (uintptr_t)t50;
    if (t51 > t52 || t52 > (uintptr_t)t44) gm_trap_slice(t51, t52, t44, gm_srcpit8mdn0781q, 394, 10);
    uint64_t t53 = ((uintptr_t)t50) - (uintptr_t)t45;
    gm_b16_8 t54;
    GM_ST(void *, (uint8_t *)&t54, (void *)(((uint8_t *)t43 + (uintptr_t)t45 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(t53));
    gm_l7 = t54;
    void * t55 = gm_l2;
    uint64_t t56 = gm_l5;
    uint64_t t57 = gm_l6;
    uint64_t r58; bool o59 = __builtin_add_overflow(t56, t57, &r58);
    if (o59) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 395, 14);
    uint64_t t60 = r58;
    GM_ST(uint64_t, (((uint8_t *)t55) + 48), t60);
    gm_b16_8 t61 = gm_l7;
    gm_f_fill_abmf5g5(t61, ((uint8_t)0ULL));
    gm_b16_8 t62 = gm_l7;
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
inline __attribute__((aligned(4))) void gm_f_truncate_j7f0pmi(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_j7f0pmi F;
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
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 16)));
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
inline __attribute__((aligned(4))) void gm_f_unsupported_70vvk1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_70vvk1e F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l3;
    void * t2 = gm_l0;
    gm_tl874t2atnlh3 t3 = gm_l1;
    gm_b16_8 t4 = gm_l2;
    gm_tnpifv20irnph t5;
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t4);
    gm_f_format_46el73v((void *)&gm_l3, (void *)NULL, cx, NULL, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srcpit8mdn0781q, 438, 107);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)gm_out), (void *)NULL, cx, NULL, t2, t3, t8);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_us_cuv01fp F;
    uint64_t gm_l0; /* x */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t2 = gm_l0;
    uint64_t t3 = (uint64_t)t2;
    F.ret = t3;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_vector_builtin_3fn933h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t8588ma2cr9d7 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7) {
    struct gm_fr_vector_builtin_3fn933h F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t8588ma2cr9d7 gm_l2; /* op */
    gm_t88rcucg1b7uk gm_l3; /* res_ty */
    gm_t88rcucg1b7uk gm_l4; /* t0 */
    gm_t88rcucg1b7uk gm_l5; /* t1 */
    gm_b16_8 gm_l6; /* v */
    gm_b16_8 gm_l7; /* dst */
    uint64_t gm_l8; /* n */
    gm_t88rcucg1b7uk gm_l9; /* lane */
    uint64_t gm_l10; /* sz */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* i */
    gm_b16_8 gm_l14; /* x */
    gm_b16_8 gm_l15; /* y */
    uint8_t gm_l16; /* r */
    uint64_t gm_l17; /* n */
    gm_t88rcucg1b7uk gm_l18; /* lane */
    uint64_t gm_l19; /* sz */
    gm_t2lqu8jsj511d gm_l20; /* mk */
    gm_b16_8 gm_l21; /* acc */
    gm_b16_8 gm_l22; /* next */
    uint64_t gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    uint64_t gm_l26; /* i */
    gm_b16_8 gm_l27; /* x */
    gm_t8588ma2cr9d7 gm_l28; /* scalar */
    uint8_t gm_l29; /* r */
    gm_b16_8 gm_l30; /* <tmp> */
    uint64_t gm_l31; /* <tmp> */
    uint8_t gm_l32; /* x */
    uint8_t gm_l33; /* r */
    gm_b16_8 gm_l34; /* <tmp> */
    uint64_t gm_l35; /* <tmp> */
    uint8_t gm_l36; /* x */
    uint64_t gm_l37; /* c */
    gm_b16_8 gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    uint8_t gm_l40; /* x */
    uint64_t gm_l41; /* n */
    gm_t88rcucg1b7uk gm_l42; /* lane */
    uint64_t gm_l43; /* sz */
    uint64_t gm_l44; /* <tmp> */
    uint64_t gm_l45; /* <tmp> */
    uint64_t gm_l46; /* i */
    uint64_t gm_l47; /* n */
    gm_t88rcucg1b7uk gm_l48; /* lane */
    uint64_t gm_l49; /* sz */
    gm_t8588ma2cr9d7 gm_l50; /* scalar */
    uint64_t gm_l52; /* <tmp> */
    uint64_t gm_l53; /* <tmp> */
    uint64_t gm_l54; /* i */
    gm_b16_8 gm_l55; /* <tmp> */
    uint64_t gm_l56; /* <tmp> */
    gm_b16_8 gm_l57; /* x */
    uint64_t gm_l58; /* n */
    gm_t88rcucg1b7uk gm_l59; /* from */
    gm_t88rcucg1b7uk gm_l60; /* to */
    uint64_t gm_l61; /* fsz */
    uint64_t gm_l62; /* tsz */
    gm_t8588ma2cr9d7 gm_l63; /* how */
    uint64_t gm_l64; /* <tmp> */
    uint64_t gm_l65; /* <tmp> */
    uint64_t gm_l66; /* i */
    gm_t9qkh0rsooc8d gm_l67; /* lop */
    uint64_t gm_l68; /* n */
    gm_t88rcucg1b7uk gm_l69; /* from */
    gm_t88rcucg1b7uk gm_l70; /* to */
    uint64_t gm_l71; /* fsz */
    uint64_t gm_l72; /* tsz */
    gm_t8588ma2cr9d7 gm_l73; /* scalar */
    uint64_t gm_l74; /* <tmp> */
    uint64_t gm_l75; /* <tmp> */
    uint64_t gm_l76; /* i */
    gm_b16_8 gm_l77; /* x */
    gm_b16_8 gm_l78; /* d */
    gm_tpmdr2hmdhsqr gm_l79; /* args */
    uint8_t gm_l80; /* nv */
    uint64_t gm_l81; /* n */
    gm_t88rcucg1b7uk gm_l82; /* lane */
    uint64_t gm_l83; /* sz */
    uint64_t gm_l84; /* ops */
    uint64_t gm_l85; /* <tmp> */
    uint64_t gm_l86; /* <tmp> */
    uint64_t gm_l87; /* j */
    uint64_t gm_l88; /* k */
    gm_b16_8 gm_l89; /* src */
    uint64_t gm_l90; /* r */
    GM_STACK_CHECK(&F, 7587u);
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
    gm_t53at80pfdus3 gm_l91;
    gm_t1ec8br9ddkav gm_l92;
    gm_t1ec8br9ddkav gm_l93;
    gm_t1ec8br9ddkav gm_l94;
    gm_t1ec8br9ddkav gm_l95;
    gm_t1ec8br9ddkav gm_l96;
    gm_t1ec8br9ddkav gm_l97;
    uint8_t gm_l98;
    gm_t53at80pfdus3 gm_l99;
    gm_tcfa6l7reoosn gm_l100;
    gm_tftb1odc739t5 gm_l101;
    gm_tftb1odc739t5 gm_l102;
    gm_tcfa6l7reoosn gm_l103;
    uint8_t gm_l104;
    uint8_t gm_l105;
    gm_t53at80pfdus3 gm_l106;
    gm_t53at80pfdus3 gm_l107;
    gm_tftb1odc739t5 gm_l108;
    gm_tcfa6l7reoosn gm_l109;
    gm_t53at80pfdus3 gm_l110;
    gm_t53at80pfdus3 gm_l111;
    uint8_t gm_l112;
    gm_t53at80pfdus3 gm_l113;
    gm_tcfa6l7reoosn gm_l114;
    gm_t53at80pfdus3 gm_l115;
    gm_t53at80pfdus3 gm_l116;
    gm_tcfa6l7reoosn gm_l117;
    gm_tcfa6l7reoosn gm_l118;
    gm_t53at80pfdus3 gm_l119;
    {
        {
            {
                {
                    {
                        {
                            uint8_t t119 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t120 = (uint8_t)(t119 == ((uint8_t)40ULL));
                            if (!t120) {
                                goto gm_lb4;
                            }
                            goto gm_lb3;
                        }
gm_lb4:;
                        {
                            uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t122 = (uint8_t)(t121 == ((uint8_t)41ULL));
                            if (!t122) {
                                goto gm_lb5;
                            }
                            goto gm_lb3;
                        }
gm_lb5:;
                        {
                            uint8_t t123 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t124 = (uint8_t)(t123 == ((uint8_t)42ULL));
                            if (!t124) {
                                goto gm_lb6;
                            }
                            goto gm_lb3;
                        }
gm_lb6:;
                        {
                            uint8_t t125 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t126 = (uint8_t)(t125 == ((uint8_t)43ULL));
                            if (!t126) {
                                goto gm_lb7;
                            }
                            goto gm_lb3;
                        }
gm_lb7:;
                        {
                            uint8_t t127 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t128 = (uint8_t)(t127 == ((uint8_t)44ULL));
                            if (!t128) {
                                goto gm_lb8;
                            }
                            goto gm_lb3;
                        }
gm_lb8:;
                        {
                            uint8_t t129 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t130 = (uint8_t)(t129 == ((uint8_t)45ULL));
                            if (!t130) {
                                goto gm_lb9;
                            }
                            goto gm_lb3;
                        }
gm_lb9:;
                        goto gm_lb2;
                    }
gm_lb3:;
                    {
                        {
                            void * t131 = gm_l0;
                            gm_t88rcucg1b7uk t132 = gm_l4;
                            gm_f_kind_htv6093((void *)&gm_l91, t131, GM_CONV(gm_csguankko4lmhd, t132));
                            uint8_t t133 = GM_LD(uint8_t, (((uint8_t *)&gm_l91) + 0));
                            uint8_t t134 = (uint8_t)(t133 == ((uint8_t)11ULL));
                            if (!t134) {
                                goto gm_lb11;
                            }
                            uint64_t t135 = GM_LD(uint64_t, (((uint8_t *)&gm_l91) + 8));
                            gm_l8 = t135;
                            gm_t88rcucg1b7uk t136 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l91) + 8) + 8));
                            gm_l9 = t136;
                            void * t137 = gm_l0;
                            gm_t88rcucg1b7uk t138 = gm_l9;
                            uint64_t t139 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t137, t138);
                            if (gm_unw.kind) {
                                goto gm_ll118;
                            }
                            uint64_t t140 = gm_f_us_cuv01fp(t139);
                            gm_l10 = t140;
                            gm_l12 = ((uint64_t)0ULL);
                            uint64_t t141 = gm_l8;
                            uint64_t t142 = gm_f_us_cuv01fp(t141);
                            gm_l11 = t142;
                            {
                                for (;;) {
                                    uint64_t t143 = gm_l12;
                                    uint64_t t144 = gm_l11;
                                    uint8_t t145 = (t143 < t144);
                                    if (!t145) {
                                        goto gm_lb12;
                                    }
                                    {
                                        {
                                            {
                                                uint64_t t146 = gm_l12;
                                                gm_l13 = t146;
                                                void * t147 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                                uint64_t t148 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t148)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t148, gm_srcns60k96jj1mt, 12, 22);
                                                void * t149 = GM_LD(void *, (((uint8_t *)t147) + (uintptr_t)((uint64_t)0ULL) * 16));
                                                uint64_t t150 = GM_LD(uint64_t, ((((uint8_t *)t147) + (uintptr_t)((uint64_t)0ULL) * 16) + 8));
                                                uint64_t t151 = gm_l13;
                                                uint64_t t152 = gm_l10;
                                                uint64_t r153; bool o154 = __builtin_mul_overflow(t151, t152, &r153);
                                                if (o154) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 12, 27);
                                                uint64_t t155 = r153;
                                                uint64_t t156 = gm_l13;
                                                uint64_t r157; bool o158 = __builtin_add_overflow(t156, ((uint64_t)1ULL), &r157);
                                                if (o158) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 12, 36);
                                                uint64_t t159 = r157;
                                                uint64_t t160 = gm_l10;
                                                uint64_t r161; bool o162 = __builtin_mul_overflow(t159, t160, &r161);
                                                if (o162) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 12, 35);
                                                uint64_t t163 = r161;
                                                uint64_t t164 = (uintptr_t)t155;
                                                uint64_t t165 = (uintptr_t)t163;
                                                if (t164 > t165 || t165 > (uintptr_t)t150) gm_trap_slice(t164, t165, t150, gm_srcns60k96jj1mt, 12, 22);
                                                uint64_t t166 = ((uintptr_t)t163) - (uintptr_t)t155;
                                                gm_b16_8 t167;
                                                GM_ST(void *, (uint8_t *)&t167, (void *)(((uint8_t *)t149 + (uintptr_t)t155 * 1)));
                                                GM_ST(uintptr_t, (uint8_t *)&t167 + 8, (uintptr_t)(t166));
                                                gm_l14 = t167;
                                                void * t168 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                                uint64_t t169 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                                if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t169)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t169, gm_srcns60k96jj1mt, 13, 22);
                                                void * t170 = GM_LD(void *, (((uint8_t *)t168) + (uintptr_t)((uint64_t)1ULL) * 16));
                                                uint64_t t171 = GM_LD(uint64_t, ((((uint8_t *)t168) + (uintptr_t)((uint64_t)1ULL) * 16) + 8));
                                                uint64_t t172 = gm_l13;
                                                uint64_t t173 = gm_l10;
                                                uint64_t r174; bool o175 = __builtin_mul_overflow(t172, t173, &r174);
                                                if (o175) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 13, 27);
                                                uint64_t t176 = r174;
                                                uint64_t t177 = gm_l13;
                                                uint64_t r178; bool o179 = __builtin_add_overflow(t177, ((uint64_t)1ULL), &r178);
                                                if (o179) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 13, 36);
                                                uint64_t t180 = r178;
                                                uint64_t t181 = gm_l10;
                                                uint64_t r182; bool o183 = __builtin_mul_overflow(t180, t181, &r182);
                                                if (o183) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 13, 35);
                                                uint64_t t184 = r182;
                                                uint64_t t185 = (uintptr_t)t176;
                                                uint64_t t186 = (uintptr_t)t184;
                                                if (t185 > t186 || t186 > (uintptr_t)t171) gm_trap_slice(t185, t186, t171, gm_srcns60k96jj1mt, 13, 22);
                                                uint64_t t187 = ((uintptr_t)t184) - (uintptr_t)t176;
                                                gm_b16_8 t188;
                                                GM_ST(void *, (uint8_t *)&t188, (void *)(((uint8_t *)t170 + (uintptr_t)t176 * 1)));
                                                GM_ST(uintptr_t, (uint8_t *)&t188 + 8, (uintptr_t)(t187));
                                                gm_l15 = t188;
                                                {
                                                    {
                                                        uint8_t t189 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                                        uint8_t t190 = (uint8_t)(t189 == ((uint8_t)40ULL));
                                                        if (!t190) {
                                                            goto gm_lb18;
                                                        }
                                                        void * t191 = gm_l0;
                                                        gm_tl874t2atnlh3 t192 = gm_l1;
                                                        gm_t88rcucg1b7uk t193 = gm_l9;
                                                        gm_b16_8 t194 = gm_l14;
                                                        gm_b16_8 t195 = gm_l15;
                                                        gm_f_equal_e5vf82h((void *)&gm_l92, (void *)NULL, cx, NULL, t191, t192, t193, t194, t195);
                                                        if (gm_unw.kind) {
                                                            goto gm_ll118;
                                                        }
                                                        uint8_t t196 = (uint8_t)!((((uint8_t *)&gm_l92))[0] != 0);
                                                        if (t196) {
                                                        } else {
                                                            gm_t76123gfet73g t197 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l92) + 8));
                                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t197);
                                                            goto gm_fdone;
                                                        }
                                                        uint8_t t198 = GM_LD(uint8_t, (((uint8_t *)&gm_l92) + 8));
                                                        gm_l16 = t198;
                                                        goto gm_lb17;
                                                    }
gm_lb18:;
                                                    {
                                                        uint8_t t199 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                                        uint8_t t200 = (uint8_t)(t199 == ((uint8_t)41ULL));
                                                        if (!t200) {
                                                            goto gm_lb19;
                                                        }
                                                        void * t201 = gm_l0;
                                                        gm_tl874t2atnlh3 t202 = gm_l1;
                                                        gm_t88rcucg1b7uk t203 = gm_l9;
                                                        gm_b16_8 t204 = gm_l14;
                                                        gm_b16_8 t205 = gm_l15;
                                                        gm_f_equal_e5vf82h((void *)&gm_l93, (void *)NULL, cx, NULL, t201, t202, t203, t204, t205);
                                                        if (gm_unw.kind) {
                                                            goto gm_ll118;
                                                        }
                                                        uint8_t t206 = (uint8_t)!((((uint8_t *)&gm_l93))[0] != 0);
                                                        if (t206) {
                                                        } else {
                                                            gm_t76123gfet73g t207 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l93) + 8));
                                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t207);
                                                            goto gm_fdone;
                                                        }
                                                        uint8_t t208 = GM_LD(uint8_t, (((uint8_t *)&gm_l93) + 8));
                                                        uint8_t t209 = (uint8_t)!t208;
                                                        gm_l16 = t209;
                                                        goto gm_lb17;
                                                    }
gm_lb19:;
                                                    {
                                                        uint8_t t210 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                                        uint8_t t211 = (uint8_t)(t210 == ((uint8_t)42ULL));
                                                        if (!t211) {
                                                            goto gm_lb20;
                                                        }
                                                        void * t212 = gm_l0;
                                                        gm_tl874t2atnlh3 t213 = gm_l1;
                                                        gm_t88rcucg1b7uk t214 = gm_l9;
                                                        gm_b16_8 t215 = gm_l14;
                                                        gm_b16_8 t216 = gm_l15;
                                                        gm_ts6oqppjh0ba3 t217;
                                                        GM_ST(uint8_t, (uint8_t *)&t217 + 1, 0);
                                                        GM_ST(uint8_t, ((uint8_t *)&t217 + 0), ((uint8_t)10ULL));
                                                        gm_f_cmp_op_mflafbf((void *)&gm_l94, (void *)NULL, cx, NULL, t212, t213, t217, t214, t215, t216);
                                                        if (gm_unw.kind) {
                                                            goto gm_ll118;
                                                        }
                                                        uint8_t t218 = (uint8_t)!((((uint8_t *)&gm_l94))[0] != 0);
                                                        if (t218) {
                                                        } else {
                                                            gm_t76123gfet73g t219 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l94) + 8));
                                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t219);
                                                            goto gm_fdone;
                                                        }
                                                        uint8_t t220 = GM_LD(uint8_t, (((uint8_t *)&gm_l94) + 8));
                                                        gm_l16 = t220;
                                                        goto gm_lb17;
                                                    }
gm_lb20:;
                                                    {
                                                        uint8_t t221 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                                        uint8_t t222 = (uint8_t)(t221 == ((uint8_t)43ULL));
                                                        if (!t222) {
                                                            goto gm_lb21;
                                                        }
                                                        void * t223 = gm_l0;
                                                        gm_tl874t2atnlh3 t224 = gm_l1;
                                                        gm_t88rcucg1b7uk t225 = gm_l9;
                                                        gm_b16_8 t226 = gm_l14;
                                                        gm_b16_8 t227 = gm_l15;
                                                        gm_ts6oqppjh0ba3 t228;
                                                        GM_ST(uint8_t, (uint8_t *)&t228 + 1, 0);
                                                        GM_ST(uint8_t, ((uint8_t *)&t228 + 0), ((uint8_t)11ULL));
                                                        gm_f_cmp_op_mflafbf((void *)&gm_l95, (void *)NULL, cx, NULL, t223, t224, t228, t225, t226, t227);
                                                        if (gm_unw.kind) {
                                                            goto gm_ll118;
                                                        }
                                                        uint8_t t229 = (uint8_t)!((((uint8_t *)&gm_l95))[0] != 0);
                                                        if (t229) {
                                                        } else {
                                                            gm_t76123gfet73g t230 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l95) + 8));
                                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t230);
                                                            goto gm_fdone;
                                                        }
                                                        uint8_t t231 = GM_LD(uint8_t, (((uint8_t *)&gm_l95) + 8));
                                                        gm_l16 = t231;
                                                        goto gm_lb17;
                                                    }
gm_lb21:;
                                                    {
                                                        uint8_t t232 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                                        uint8_t t233 = (uint8_t)(t232 == ((uint8_t)44ULL));
                                                        if (!t233) {
                                                            goto gm_lb22;
                                                        }
                                                        void * t234 = gm_l0;
                                                        gm_tl874t2atnlh3 t235 = gm_l1;
                                                        gm_t88rcucg1b7uk t236 = gm_l9;
                                                        gm_b16_8 t237 = gm_l14;
                                                        gm_b16_8 t238 = gm_l15;
                                                        gm_ts6oqppjh0ba3 t239;
                                                        GM_ST(uint8_t, (uint8_t *)&t239 + 1, 0);
                                                        GM_ST(uint8_t, ((uint8_t *)&t239 + 0), ((uint8_t)12ULL));
                                                        gm_f_cmp_op_mflafbf((void *)&gm_l96, (void *)NULL, cx, NULL, t234, t235, t239, t236, t237, t238);
                                                        if (gm_unw.kind) {
                                                            goto gm_ll118;
                                                        }
                                                        uint8_t t240 = (uint8_t)!((((uint8_t *)&gm_l96))[0] != 0);
                                                        if (t240) {
                                                        } else {
                                                            gm_t76123gfet73g t241 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l96) + 8));
                                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t241);
                                                            goto gm_fdone;
                                                        }
                                                        uint8_t t242 = GM_LD(uint8_t, (((uint8_t *)&gm_l96) + 8));
                                                        gm_l16 = t242;
                                                        goto gm_lb17;
                                                    }
gm_lb22:;
                                                    {
                                                        void * t243 = gm_l0;
                                                        gm_tl874t2atnlh3 t244 = gm_l1;
                                                        gm_t88rcucg1b7uk t245 = gm_l9;
                                                        gm_b16_8 t246 = gm_l14;
                                                        gm_b16_8 t247 = gm_l15;
                                                        gm_ts6oqppjh0ba3 t248;
                                                        GM_ST(uint8_t, (uint8_t *)&t248 + 1, 0);
                                                        GM_ST(uint8_t, ((uint8_t *)&t248 + 0), ((uint8_t)13ULL));
                                                        gm_f_cmp_op_mflafbf((void *)&gm_l97, (void *)NULL, cx, NULL, t243, t244, t248, t245, t246, t247);
                                                        if (gm_unw.kind) {
                                                            goto gm_ll118;
                                                        }
                                                        uint8_t t249 = (uint8_t)!((((uint8_t *)&gm_l97))[0] != 0);
                                                        if (t249) {
                                                        } else {
                                                            gm_t76123gfet73g t250 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l97) + 8));
                                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t250);
                                                            goto gm_fdone;
                                                        }
                                                        uint8_t t251 = GM_LD(uint8_t, (((uint8_t *)&gm_l97) + 8));
                                                        gm_l16 = t251;
                                                        goto gm_lb17;
                                                    }
gm_lb23:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 14, 22);
                                                }
gm_lb17:;
                                                void * t252 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                                uint64_t t253 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                                uint64_t t254 = gm_l13;
                                                if ((uintptr_t)t254 >= (uintptr_t)(t253)) gm_trap_index((uintptr_t)t254, t253, gm_srcns60k96jj1mt, 22, 17);
                                                uint8_t t255 = gm_l16;
                                                if (t255) {
                                                    gm_l98 = ((uint8_t)1ULL);
                                                } else {
                                                    gm_l98 = ((uint8_t)0ULL);
                                                }
                                                uint8_t t256 = gm_l98;
                                                GM_ST(uint8_t, (((uint8_t *)t252) + (uintptr_t)t254 * 1), t256);
                                                goto gm_lb15;
                                            }
gm_lb16:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 11, 13);
                                        }
gm_lb15:;
                                    }
gm_lb14:;
                                    uint64_t t257 = gm_l12;
                                    uint64_t r258; bool o259 = __builtin_add_overflow(t257, ((uint64_t)1ULL), &r258);
                                    uint64_t t260 = r258;
                                    gm_l12 = t260;
                                }
                            }
gm_lb12:;
                            goto gm_lb10;
                        }
gm_lb11:;
                        {
                            void * t261 = gm_l0;
                            gm_tl874t2atnlh3 t262 = gm_l1;
                            gm_b16_8 t263;
                            GM_ST(void *, (uint8_t *)&t263, (void *)(gm_s4lpfn3di4rp9));
                            GM_ST(uintptr_t, (uint8_t *)&t263 + 8, (uintptr_t)(65));
                            gm_t76123gfet73g t264;
                            gm_f_trap_qh84l9d((void *)&t264, (void *)NULL, cx, NULL, t261, t262, t263);
                            if (gm_unw.kind) {
                                goto gm_ll118;
                            }
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t264);
                            goto gm_fdone;
                        }
                    }
gm_lb10:;
                    goto gm_lb1;
                }
gm_lb2:;
            }
            {
                {
                    {
                        {
                            uint8_t t266 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t267 = (uint8_t)(t266 == ((uint8_t)46ULL));
                            if (!t267) {
                                goto gm_lb26;
                            }
                            goto gm_lb25;
                        }
gm_lb26:;
                        {
                            uint8_t t268 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t269 = (uint8_t)(t268 == ((uint8_t)47ULL));
                            if (!t269) {
                                goto gm_lb27;
                            }
                            goto gm_lb25;
                        }
gm_lb27:;
                        {
                            uint8_t t270 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t271 = (uint8_t)(t270 == ((uint8_t)48ULL));
                            if (!t271) {
                                goto gm_lb28;
                            }
                            goto gm_lb25;
                        }
gm_lb28:;
                        goto gm_lb24;
                    }
gm_lb25:;
                    {
                        {
                            void * t274 = gm_l0;
                            gm_t88rcucg1b7uk t275 = gm_l4;
                            gm_f_kind_htv6093((void *)&gm_l99, t274, GM_CONV(gm_csguankko4lmhd, t275));
                            uint8_t t276 = GM_LD(uint8_t, (((uint8_t *)&gm_l99) + 0));
                            uint8_t t277 = (uint8_t)(t276 == ((uint8_t)11ULL));
                            if (!t277) {
                                goto gm_lb30;
                            }
                            uint64_t t278 = GM_LD(uint64_t, (((uint8_t *)&gm_l99) + 8));
                            gm_l17 = t278;
                            gm_t88rcucg1b7uk t279 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l99) + 8) + 8));
                            gm_l18 = t279;
                            {
                                void * t281 = gm_l0;
                                gm_t88rcucg1b7uk t282 = gm_l18;
                                uint64_t t283 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t281, t282);
                                if (gm_unw.kind) {
                                    goto gm_ll273;
                                }
                                uint64_t t284 = gm_f_us_cuv01fp(t283);
                                gm_l19 = t284;
                                void * t285 = gm_l0;
                                gm_t2lqu8jsj511d t286 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t285));
                                gm_l20 = t286;
                                void * t287 = gm_l0;
                                uint64_t t288 = gm_l19;
                                uint64_t t289 = (uint64_t)t288;
                                gm_b16_8 t290 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t287, t289);
                                if (gm_unw.kind) {
                                    goto gm_ll273;
                                }
                                gm_l21 = t290;
                                gm_b16_8 t291 = gm_l21;
                                void * t292 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                uint64_t t293 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t293)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t293, gm_srcns60k96jj1mt, 30, 27);
                                void * t294 = GM_LD(void *, (((uint8_t *)t292) + (uintptr_t)((uint64_t)0ULL) * 16));
                                uint64_t t295 = GM_LD(uint64_t, ((((uint8_t *)t292) + (uintptr_t)((uint64_t)0ULL) * 16) + 8));
                                uint64_t t296 = gm_l19;
                                uint64_t t297 = (uintptr_t)((uint64_t)0ULL);
                                uint64_t t298 = (uintptr_t)t296;
                                if (t297 > t298 || t298 > (uintptr_t)t295) gm_trap_slice(t297, t298, t295, gm_srcns60k96jj1mt, 30, 27);
                                uint64_t t299 = ((uintptr_t)t296) - (uintptr_t)((uint64_t)0ULL);
                                gm_b16_8 t300;
                                GM_ST(void *, (uint8_t *)&t300, (void *)(((uint8_t *)t294 + (uintptr_t)((uint64_t)0ULL) * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t300 + 8, (uintptr_t)(t299));
                                gm_b16_8 t301 = GM_LD(gm_b16_8, ((uint8_t *)&t300));
                                gm_f_copy_from_f7j6p05(t291, t301);
                                void * t302 = gm_l0;
                                uint64_t t303 = gm_l19;
                                uint64_t t304 = (uint64_t)t303;
                                gm_b16_8 t305 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t302, t304);
                                if (gm_unw.kind) {
                                    goto gm_ll273;
                                }
                                gm_l22 = t305;
                                gm_trqfvmjtt4sij t306;
                                gm_f_list_fufbgf7((void *)&t306, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_ll273;
                                }
                                F.l23 = t306;
                                gm_l25 = ((uint64_t)1ULL);
                                uint64_t t307 = gm_l17;
                                uint64_t t308 = gm_f_us_cuv01fp(t307);
                                gm_l24 = t308;
                                {
                                    for (;;) {
                                        uint64_t t309 = gm_l25;
                                        uint64_t t310 = gm_l24;
                                        uint8_t t311 = (t309 < t310);
                                        if (!t311) {
                                            goto gm_lb32;
                                        }
                                        {
                                            {
                                                {
                                                    uint64_t t312 = gm_l25;
                                                    gm_l26 = t312;
                                                    void * t313 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                                    uint64_t t314 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t314)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t314, gm_srcns60k96jj1mt, 35, 22);
                                                    void * t315 = GM_LD(void *, (((uint8_t *)t313) + (uintptr_t)((uint64_t)0ULL) * 16));
                                                    uint64_t t316 = GM_LD(uint64_t, ((((uint8_t *)t313) + (uintptr_t)((uint64_t)0ULL) * 16) + 8));
                                                    uint64_t t317 = gm_l26;
                                                    uint64_t t318 = gm_l19;
                                                    uint64_t r319; bool o320 = __builtin_mul_overflow(t317, t318, &r319);
                                                    if (o320) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 35, 27);
                                                    uint64_t t321 = r319;
                                                    uint64_t t322 = gm_l26;
                                                    uint64_t r323; bool o324 = __builtin_add_overflow(t322, ((uint64_t)1ULL), &r323);
                                                    if (o324) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 35, 36);
                                                    uint64_t t325 = r323;
                                                    uint64_t t326 = gm_l19;
                                                    uint64_t r327; bool o328 = __builtin_mul_overflow(t325, t326, &r327);
                                                    if (o328) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 35, 35);
                                                    uint64_t t329 = r327;
                                                    uint64_t t330 = (uintptr_t)t321;
                                                    uint64_t t331 = (uintptr_t)t329;
                                                    if (t330 > t331 || t331 > (uintptr_t)t316) gm_trap_slice(t330, t331, t316, gm_srcns60k96jj1mt, 35, 22);
                                                    uint64_t t332 = ((uintptr_t)t329) - (uintptr_t)t321;
                                                    gm_b16_8 t333;
                                                    GM_ST(void *, (uint8_t *)&t333, (void *)(((uint8_t *)t315 + (uintptr_t)t321 * 1)));
                                                    GM_ST(uintptr_t, (uint8_t *)&t333 + 8, (uintptr_t)(t332));
                                                    gm_l27 = t333;
                                                    gm_t8588ma2cr9d7 t334 = gm_l2;
                                                    uint8_t t335 = GM_LD(uint8_t, (((uint8_t *)&t334) + 0));
                                                    uint8_t t336 = (uint8_t)((t335 == ((uint8_t)46ULL)));
                                                    gm_t8588ma2cr9d7 t337;
                                                    GM_ST(uint8_t, (uint8_t *)&t337 + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t337 + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t337 + 4, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t337 + 8, 0);
                                                    GM_ST(uint8_t, ((uint8_t *)&t337 + 0), ((uint8_t)46ULL));
                                                    if (t336) {
                                                        void * t338 = gm_l0;
                                                        gm_tl874t2atnlh3 t339 = gm_l1;
                                                        gm_t9qkh0rsooc8d t340;
                                                        GM_ST(uint8_t, ((uint8_t *)&t340 + 0), ((uint8_t)0ULL));
                                                        gm_t88rcucg1b7uk t341 = gm_l18;
                                                        gm_t88rcucg1b7uk t342 = gm_l18;
                                                        gm_t88rcucg1b7uk t343 = gm_l18;
                                                        gm_b16_8 t344 = gm_l21;
                                                        gm_b16_8 t345 = gm_l27;
                                                        gm_b16_8 t346 = gm_l22;
                                                        gm_ts6oqppjh0ba3 t347;
                                                        GM_ST(uint8_t, ((uint8_t *)&t347 + 0), ((uint8_t)0ULL));
                                                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t347 + 1), t340);
                                                        gm_f_binop_nrsq3mp((void *)&gm_l100, (void *)NULL, cx, NULL, t338, t339, t347, t341, t342, t343, t344, t345, t346);
                                                        if (gm_unw.kind) {
                                                            goto gm_ld280_1;
                                                        }
                                                        uint8_t t348 = (uint8_t)!((((uint8_t *)&gm_l100))[0] != 0);
                                                        if (t348) {
                                                        } else {
                                                            gm_t76123gfet73g t349 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l100) + 8));
                                                            GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                                                            ((uint8_t *)&F.ret)[0] = 1;
                                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&F.ret + 8), t349);
                                                            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                                                            goto gm_ld280_1;
                                                        }
                                                    } else {
                                                        void * t350 = (void *)((uint8_t *)&F.l23);
                                                        gm_f_truncate_j7f0pmi(t350, ((uint64_t)0ULL));
                                                        void * t351 = (void *)((uint8_t *)&F.l23);
                                                        gm_b16_8 t352 = gm_l21;
                                                        gm_tftb1odc739t5 t353 = gm_f_push_rluch9r(t351, t352);
                                                        gm_l101 = t353;
                                                        uint8_t t354 = (uint8_t)!((((uint8_t *)&gm_l101))[0] != 0);
                                                        if (!t354) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l101) + 1, gm_srcns60k96jj1mt, 40, 21);
                                                        void * t355 = (void *)((uint8_t *)&F.l23);
                                                        gm_b16_8 t356 = gm_l27;
                                                        gm_tftb1odc739t5 t357 = gm_f_push_rluch9r(t355, t356);
                                                        gm_l102 = t357;
                                                        uint8_t t358 = (uint8_t)!((((uint8_t *)&gm_l102))[0] != 0);
                                                        if (!t358) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l102) + 1, gm_srcns60k96jj1mt, 41, 21);
                                                        gm_t8588ma2cr9d7 t359 = gm_l2;
                                                        uint8_t t360 = GM_LD(uint8_t, (((uint8_t *)&t359) + 0));
                                                        uint8_t t361 = (uint8_t)((t360 == ((uint8_t)47ULL)));
                                                        gm_t8588ma2cr9d7 t362;
                                                        GM_ST(uint8_t, (uint8_t *)&t362 + 1, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t362 + 2, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&t362 + 4, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&t362 + 8, 0);
                                                        GM_ST(uint8_t, ((uint8_t *)&t362 + 0), ((uint8_t)47ULL));
                                                        if (t361) {
                                                            gm_t8588ma2cr9d7 t363;
                                                            GM_ST(uint8_t, (uint8_t *)&t363 + 1, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&t363 + 2, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&t363 + 4, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&t363 + 8, 0);
                                                            GM_ST(uint8_t, ((uint8_t *)&t363 + 0), ((uint8_t)16ULL));
                                                            gm_l28 = t363;
                                                        } else {
                                                            gm_t8588ma2cr9d7 t364;
                                                            GM_ST(uint8_t, (uint8_t *)&t364 + 1, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&t364 + 2, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&t364 + 4, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&t364 + 8, 0);
                                                            GM_ST(uint8_t, ((uint8_t *)&t364 + 0), ((uint8_t)17ULL));
                                                            gm_l28 = t364;
                                                        }
                                                        void * t365 = gm_l0;
                                                        gm_tl874t2atnlh3 t366 = gm_l1;
                                                        gm_t8588ma2cr9d7 t367 = gm_l28;
                                                        gm_t88rcucg1b7uk t368 = gm_l18;
                                                        gm_t88rcucg1b7uk t369 = gm_l18;
                                                        gm_t88rcucg1b7uk t370 = gm_l18;
                                                        gm_b16_8 t371 = GM_LD(gm_b16_8, ((uint8_t *)&F.l23));
                                                        gm_b16_8 t372 = gm_l22;
                                                        gm_f_apply_builtin_41dffti((void *)&gm_l103, (void *)NULL, cx, NULL, t365, t366, t367, t368, t369, t370, t371, t372);
                                                        if (gm_unw.kind) {
                                                            goto gm_ld280_1;
                                                        }
                                                        uint8_t t373 = (uint8_t)!((((uint8_t *)&gm_l103))[0] != 0);
                                                        if (t373) {
                                                        } else {
                                                            gm_t76123gfet73g t374 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l103) + 8));
                                                            GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                                                            ((uint8_t *)&F.ret)[0] = 1;
                                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&F.ret + 8), t374);
                                                            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                                                            goto gm_ld280_1;
                                                        }
                                                    }
                                                    gm_b16_8 t375 = gm_l21;
                                                    gm_b16_8 t376 = gm_l22;
                                                    gm_b16_8 t377 = GM_LD(gm_b16_8, ((uint8_t *)&t376));
                                                    gm_f_copy_from_f7j6p05(t375, t377);
                                                    goto gm_lb35;
                                                }
gm_lb36:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 34, 13);
                                            }
gm_lb35:;
                                        }
gm_lb34:;
                                        uint64_t t378 = gm_l25;
                                        uint64_t r379; bool o380 = __builtin_add_overflow(t378, ((uint64_t)1ULL), &r379);
                                        uint64_t t381 = r379;
                                        gm_l25 = t381;
                                    }
                                }
gm_lb32:;
                                gm_b16_8 t382 = gm_l7;
                                gm_b16_8 t383 = gm_l21;
                                gm_b16_8 t384 = GM_LD(gm_b16_8, ((uint8_t *)&t383));
                                gm_f_copy_from_f7j6p05(t382, t384);
                                void * t385 = gm_l0;
                                gm_t2lqu8jsj511d t386 = gm_l20;
                                gm_f_release_jn5kqdi(t385, GM_CONV(gm_csc7lc6jlkqkuq, t386));
                            }
                            {
                                void * t387 = (void *)((uint8_t *)&F.l23);
                                gm_f_deinit_tq06jtm(t387);
                            }
                            goto gm_ls388;
gm_ld280_1:;
                            {
                                gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
                                void * t389 = (void *)((uint8_t *)&F.l23);
                                gm_f_deinit_tq06jtm(t389);
                                gm_unw = gm_saved;
                            }
                            goto gm_ll273;
gm_ls388:;
gm_lb31:;
                            goto gm_lb29;
                        }
                        goto gm_ls390;
gm_ll273:;
                        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1073741853) { gm_unw.kind = GM_NONE; goto gm_lb30; }
                        goto gm_ll272;
gm_ls390:;
gm_lb30:;
                        {
                            void * t391 = gm_l0;
                            gm_tl874t2atnlh3 t392 = gm_l1;
                            gm_b16_8 t393;
                            GM_ST(void *, (uint8_t *)&t393, (void *)(gm_sm94b3smoinpd));
                            GM_ST(uintptr_t, (uint8_t *)&t393 + 8, (uintptr_t)(59));
                            gm_t76123gfet73g t394;
                            gm_f_trap_qh84l9d((void *)&t394, (void *)NULL, cx, NULL, t391, t392, t393);
                            if (gm_unw.kind) {
                                goto gm_ll272;
                            }
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t394);
                            goto gm_fdone;
                        }
                    }
                    goto gm_ls395;
gm_ll272:;
                    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1073741852) { gm_unw.kind = GM_NONE; goto gm_lb29; }
                    goto gm_ll265;
gm_ls395:;
gm_lb29:;
                    goto gm_lb1;
                }
                goto gm_ls396;
gm_ll265:;
                if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1073741847) { gm_unw.kind = GM_NONE; goto gm_lb24; }
                goto gm_ll118;
gm_ls396:;
gm_lb24:;
            }
            {
                {
                    uint8_t t397 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t398 = (uint8_t)(t397 == ((uint8_t)49ULL));
                    if (!t398) {
                        goto gm_lb37;
                    }
                    gm_l29 = ((uint8_t)1);
                    void * t399 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t400 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t400)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t400, gm_srcns60k96jj1mt, 52, 22);
                    gm_b16_8 t401 = GM_LD(gm_b16_8, (((uint8_t *)t399) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_l30 = t401;
                    gm_l31 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t402 = gm_l31;
                            uint64_t t403 = GM_LD(uint64_t, (((uint8_t *)&gm_l30) + 8));
                            uint8_t t404 = (t402 < t403);
                            if (!t404) {
                                goto gm_lb38;
                            }
                            {
                                void * t405 = GM_LD(void *, ((uint8_t *)&gm_l30));
                                uint64_t t406 = GM_LD(uint64_t, (((uint8_t *)&gm_l30) + 8));
                                uint64_t t407 = gm_l31;
                                {
                                    {
                                        uint8_t t408 = GM_LD(uint8_t, (((uint8_t *)t405) + (uintptr_t)t407 * 1));
                                        gm_l32 = t408;
                                        uint8_t t409 = gm_l32;
                                        uint8_t t410 = ((t409 == ((uint8_t)0ULL)));
                                        if (t410) {
                                            gm_l29 = ((uint8_t)0);
                                        }
                                        goto gm_lb41;
                                    }
gm_lb42:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 52, 13);
                                }
gm_lb41:;
                            }
gm_lb40:;
                            uint64_t t411 = gm_l31;
                            uint64_t r412; bool o413 = __builtin_add_overflow(t411, ((uint64_t)1ULL), &r412);
                            uint64_t t414 = r412;
                            gm_l31 = t414;
                        }
                    }
gm_lb38:;
                    void * t415 = GM_LD(void *, ((uint8_t *)&gm_l7));
                    uint64_t t416 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t416)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t416, gm_srcns60k96jj1mt, 55, 13);
                    uint8_t t417 = gm_l29;
                    if (t417) {
                        gm_l104 = ((uint8_t)1ULL);
                    } else {
                        gm_l104 = ((uint8_t)0ULL);
                    }
                    uint8_t t418 = gm_l104;
                    GM_ST(uint8_t, (((uint8_t *)t415) + (uintptr_t)((uint64_t)0ULL) * 1), t418);
                    goto gm_lb1;
                }
gm_lb37:;
                {
                    uint8_t t419 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t420 = (uint8_t)(t419 == ((uint8_t)50ULL));
                    if (!t420) {
                        goto gm_lb43;
                    }
                    gm_l33 = ((uint8_t)0);
                    void * t421 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t422 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t422)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t422, gm_srcns60k96jj1mt, 59, 22);
                    gm_b16_8 t423 = GM_LD(gm_b16_8, (((uint8_t *)t421) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_l34 = t423;
                    gm_l35 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t424 = gm_l35;
                            uint64_t t425 = GM_LD(uint64_t, (((uint8_t *)&gm_l34) + 8));
                            uint8_t t426 = (t424 < t425);
                            if (!t426) {
                                goto gm_lb44;
                            }
                            {
                                void * t427 = GM_LD(void *, ((uint8_t *)&gm_l34));
                                uint64_t t428 = GM_LD(uint64_t, (((uint8_t *)&gm_l34) + 8));
                                uint64_t t429 = gm_l35;
                                {
                                    {
                                        uint8_t t430 = GM_LD(uint8_t, (((uint8_t *)t427) + (uintptr_t)t429 * 1));
                                        gm_l36 = t430;
                                        uint8_t t431 = gm_l36;
                                        uint8_t t432 = (!(t431 == ((uint8_t)0ULL)));
                                        if (t432) {
                                            gm_l33 = ((uint8_t)1);
                                        }
                                        goto gm_lb47;
                                    }
gm_lb48:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 59, 13);
                                }
gm_lb47:;
                            }
gm_lb46:;
                            uint64_t t433 = gm_l35;
                            uint64_t r434; bool o435 = __builtin_add_overflow(t433, ((uint64_t)1ULL), &r434);
                            uint64_t t436 = r434;
                            gm_l35 = t436;
                        }
                    }
gm_lb44:;
                    void * t437 = GM_LD(void *, ((uint8_t *)&gm_l7));
                    uint64_t t438 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t438)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t438, gm_srcns60k96jj1mt, 62, 13);
                    uint8_t t439 = gm_l33;
                    if (t439) {
                        gm_l105 = ((uint8_t)1ULL);
                    } else {
                        gm_l105 = ((uint8_t)0ULL);
                    }
                    uint8_t t440 = gm_l105;
                    GM_ST(uint8_t, (((uint8_t *)t437) + (uintptr_t)((uint64_t)0ULL) * 1), t440);
                    goto gm_lb1;
                }
gm_lb43:;
            }
            {
                {
                    uint8_t t441 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t442 = (uint8_t)(t441 == ((uint8_t)51ULL));
                    if (!t442) {
                        goto gm_lb49;
                    }
                    gm_l37 = ((uint64_t)0ULL);
                    void * t443 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t444 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t444)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t444, gm_srcns60k96jj1mt, 66, 22);
                    gm_b16_8 t445 = GM_LD(gm_b16_8, (((uint8_t *)t443) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_l38 = t445;
                    gm_l39 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t446 = gm_l39;
                            uint64_t t447 = GM_LD(uint64_t, (((uint8_t *)&gm_l38) + 8));
                            uint8_t t448 = (t446 < t447);
                            if (!t448) {
                                goto gm_lb50;
                            }
                            {
                                void * t449 = GM_LD(void *, ((uint8_t *)&gm_l38));
                                uint64_t t450 = GM_LD(uint64_t, (((uint8_t *)&gm_l38) + 8));
                                uint64_t t451 = gm_l39;
                                {
                                    {
                                        uint8_t t452 = GM_LD(uint8_t, (((uint8_t *)t449) + (uintptr_t)t451 * 1));
                                        gm_l40 = t452;
                                        uint8_t t453 = gm_l40;
                                        uint8_t t454 = (!(t453 == ((uint8_t)0ULL)));
                                        if (t454) {
                                            uint64_t t455 = gm_l37;
                                            uint64_t r456; bool o457 = __builtin_add_overflow(t455, ((uint64_t)1ULL), &r456);
                                            if (o457) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 67, 33);
                                            uint64_t t458 = r456;
                                            gm_l37 = t458;
                                        }
                                        goto gm_lb53;
                                    }
gm_lb54:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 66, 13);
                                }
gm_lb53:;
                            }
gm_lb52:;
                            uint64_t t459 = gm_l39;
                            uint64_t r460; bool o461 = __builtin_add_overflow(t459, ((uint64_t)1ULL), &r460);
                            uint64_t t462 = r460;
                            gm_l39 = t462;
                        }
                    }
gm_lb50:;
                    void * t463 = gm_l0;
                    gm_b16_8 t464 = gm_l7;
                    uint64_t t465 = gm_l37;
                    unsigned __int128 t466 = (unsigned __int128)t465;
                    gm_f_put_uint_cddin99(t463, t464, t466);
                    goto gm_lb1;
                }
gm_lb49:;
                {
                    uint8_t t467 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t468 = (uint8_t)(t467 == ((uint8_t)52ULL));
                    if (!t468) {
                        goto gm_lb55;
                    }
                    {
                        {
                            void * t469 = gm_l0;
                            gm_t88rcucg1b7uk t470 = gm_l5;
                            gm_f_kind_htv6093((void *)&gm_l106, t469, GM_CONV(gm_csguankko4lmhd, t470));
                            uint8_t t471 = GM_LD(uint8_t, (((uint8_t *)&gm_l106) + 0));
                            uint8_t t472 = (uint8_t)(t471 == ((uint8_t)11ULL));
                            if (!t472) {
                                goto gm_lb57;
                            }
                            uint64_t t473 = GM_LD(uint64_t, (((uint8_t *)&gm_l106) + 8));
                            gm_l41 = t473;
                            gm_t88rcucg1b7uk t474 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l106) + 8) + 8));
                            gm_l42 = t474;
                            void * t475 = gm_l0;
                            gm_t88rcucg1b7uk t476 = gm_l42;
                            uint64_t t477 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t475, t476);
                            if (gm_unw.kind) {
                                goto gm_ll118;
                            }
                            uint64_t t478 = gm_f_us_cuv01fp(t477);
                            gm_l43 = t478;
                            gm_b16_8 t479 = gm_l7;
                            void * t480 = GM_LD(void *, ((uint8_t *)&gm_l6));
                            uint64_t t481 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t481)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t481, gm_srcns60k96jj1mt, 74, 27);
                            gm_b16_8 t482 = GM_LD(gm_b16_8, (((uint8_t *)t480) + (uintptr_t)((uint64_t)2ULL) * 16));
                            gm_b16_8 t483 = GM_LD(gm_b16_8, ((uint8_t *)&t482));
                            gm_f_copy_from_f7j6p05(t479, t483);
                            gm_l45 = ((uint64_t)0ULL);
                            uint64_t t484 = gm_l41;
                            uint64_t t485 = gm_f_us_cuv01fp(t484);
                            gm_l44 = t485;
                            {
                                for (;;) {
                                    uint64_t t486 = gm_l45;
                                    uint64_t t487 = gm_l44;
                                    uint8_t t488 = (t486 < t487);
                                    if (!t488) {
                                        goto gm_lb58;
                                    }
                                    {
                                        {
                                            {
                                                uint64_t t489 = gm_l45;
                                                gm_l46 = t489;
                                                void * t490 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                                uint64_t t491 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t491)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t491, gm_srcns60k96jj1mt, 76, 20);
                                                void * t492 = GM_LD(void *, (((uint8_t *)t490) + (uintptr_t)((uint64_t)0ULL) * 16));
                                                uint64_t t493 = GM_LD(uint64_t, ((((uint8_t *)t490) + (uintptr_t)((uint64_t)0ULL) * 16) + 8));
                                                uint64_t t494 = gm_l46;
                                                if ((uintptr_t)t494 >= (uintptr_t)(t493)) gm_trap_index((uintptr_t)t494, t493, gm_srcns60k96jj1mt, 76, 20);
                                                uint8_t t495 = GM_LD(uint8_t, (((uint8_t *)t492) + (uintptr_t)t494 * 1));
                                                uint8_t t496 = (!(t495 == ((uint8_t)0ULL)));
                                                if (t496) {
                                                    void * t497 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                                    uint64_t t498 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                                    uint64_t t499 = gm_l46;
                                                    uint64_t t500 = gm_l43;
                                                    uint64_t r501; bool o502 = __builtin_mul_overflow(t499, t500, &r501);
                                                    if (o502) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 76, 43);
                                                    uint64_t t503 = r501;
                                                    uint64_t t504 = gm_l46;
                                                    uint64_t r505; bool o506 = __builtin_add_overflow(t504, ((uint64_t)1ULL), &r505);
                                                    if (o506) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 76, 52);
                                                    uint64_t t507 = r505;
                                                    uint64_t t508 = gm_l43;
                                                    uint64_t r509; bool o510 = __builtin_mul_overflow(t507, t508, &r509);
                                                    if (o510) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 76, 51);
                                                    uint64_t t511 = r509;
                                                    uint64_t t512 = (uintptr_t)t503;
                                                    uint64_t t513 = (uintptr_t)t511;
                                                    if (t512 > t513 || t513 > (uintptr_t)t498) gm_trap_slice(t512, t513, t498, gm_srcns60k96jj1mt, 76, 39);
                                                    uint64_t t514 = ((uintptr_t)t511) - (uintptr_t)t503;
                                                    gm_b16_8 t515;
                                                    GM_ST(void *, (uint8_t *)&t515, (void *)(((uint8_t *)t497 + (uintptr_t)t503 * 1)));
                                                    GM_ST(uintptr_t, (uint8_t *)&t515 + 8, (uintptr_t)(t514));
                                                    void * t516 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                                    uint64_t t517 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t517)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t517, gm_srcns60k96jj1mt, 76, 75);
                                                    void * t518 = GM_LD(void *, (((uint8_t *)t516) + (uintptr_t)((uint64_t)1ULL) * 16));
                                                    uint64_t t519 = GM_LD(uint64_t, ((((uint8_t *)t516) + (uintptr_t)((uint64_t)1ULL) * 16) + 8));
                                                    uint64_t t520 = gm_l46;
                                                    uint64_t t521 = gm_l43;
                                                    uint64_t r522; bool o523 = __builtin_mul_overflow(t520, t521, &r522);
                                                    if (o523) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 76, 80);
                                                    uint64_t t524 = r522;
                                                    uint64_t t525 = gm_l46;
                                                    uint64_t r526; bool o527 = __builtin_add_overflow(t525, ((uint64_t)1ULL), &r526);
                                                    if (o527) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 76, 89);
                                                    uint64_t t528 = r526;
                                                    uint64_t t529 = gm_l43;
                                                    uint64_t r530; bool o531 = __builtin_mul_overflow(t528, t529, &r530);
                                                    if (o531) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 76, 88);
                                                    uint64_t t532 = r530;
                                                    uint64_t t533 = (uintptr_t)t524;
                                                    uint64_t t534 = (uintptr_t)t532;
                                                    if (t533 > t534 || t534 > (uintptr_t)t519) gm_trap_slice(t533, t534, t519, gm_srcns60k96jj1mt, 76, 75);
                                                    uint64_t t535 = ((uintptr_t)t532) - (uintptr_t)t524;
                                                    gm_b16_8 t536;
                                                    GM_ST(void *, (uint8_t *)&t536, (void *)(((uint8_t *)t518 + (uintptr_t)t524 * 1)));
                                                    GM_ST(uintptr_t, (uint8_t *)&t536 + 8, (uintptr_t)(t535));
                                                    gm_b16_8 t537 = GM_LD(gm_b16_8, ((uint8_t *)&t536));
                                                    gm_f_copy_from_f7j6p05(t515, t537);
                                                }
                                                goto gm_lb61;
                                            }
gm_lb62:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 75, 13);
                                        }
gm_lb61:;
                                    }
gm_lb60:;
                                    uint64_t t538 = gm_l45;
                                    uint64_t r539; bool o540 = __builtin_add_overflow(t538, ((uint64_t)1ULL), &r539);
                                    uint64_t t541 = r539;
                                    gm_l45 = t541;
                                }
                            }
gm_lb58:;
                            goto gm_lb56;
                        }
gm_lb57:;
                        {
                            void * t542 = gm_l0;
                            gm_tl874t2atnlh3 t543 = gm_l1;
                            gm_b16_8 t544;
                            GM_ST(void *, (uint8_t *)&t544, (void *)(gm_spqlqjii789ln));
                            GM_ST(uintptr_t, (uint8_t *)&t544 + 8, (uintptr_t)(56));
                            gm_t76123gfet73g t545;
                            gm_f_trap_qh84l9d((void *)&t545, (void *)NULL, cx, NULL, t542, t543, t544);
                            if (gm_unw.kind) {
                                goto gm_ll118;
                            }
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t545);
                            goto gm_fdone;
                        }
                    }
gm_lb56:;
                    goto gm_lb1;
                }
gm_lb55:;
            }
            {
                {
                    {
                        {
                            uint8_t t547 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t548 = (uint8_t)(t547 == ((uint8_t)53ULL));
                            if (!t548) {
                                goto gm_lb65;
                            }
                            goto gm_lb64;
                        }
gm_lb65:;
                        {
                            uint8_t t549 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t550 = (uint8_t)(t549 == ((uint8_t)54ULL));
                            if (!t550) {
                                goto gm_lb66;
                            }
                            goto gm_lb64;
                        }
gm_lb66:;
                        {
                            uint8_t t551 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t552 = (uint8_t)(t551 == ((uint8_t)55ULL));
                            if (!t552) {
                                goto gm_lb67;
                            }
                            goto gm_lb64;
                        }
gm_lb67:;
                        goto gm_lb63;
                    }
gm_lb64:;
                    {
                        {
                            void * t555 = gm_l0;
                            gm_t88rcucg1b7uk t556 = gm_l4;
                            gm_f_kind_htv6093((void *)&gm_l107, t555, GM_CONV(gm_csguankko4lmhd, t556));
                            uint8_t t557 = GM_LD(uint8_t, (((uint8_t *)&gm_l107) + 0));
                            uint8_t t558 = (uint8_t)(t557 == ((uint8_t)11ULL));
                            if (!t558) {
                                goto gm_lb69;
                            }
                            uint64_t t559 = GM_LD(uint64_t, (((uint8_t *)&gm_l107) + 8));
                            gm_l47 = t559;
                            gm_t88rcucg1b7uk t560 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l107) + 8) + 8));
                            gm_l48 = t560;
                            {
                                void * t562 = gm_l0;
                                gm_t88rcucg1b7uk t563 = gm_l48;
                                uint64_t t564 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t562, t563);
                                if (gm_unw.kind) {
                                    goto gm_ll554;
                                }
                                uint64_t t565 = gm_f_us_cuv01fp(t564);
                                gm_l49 = t565;
                                {
                                    {
                                        uint8_t t566 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                        uint8_t t567 = (uint8_t)(t566 == ((uint8_t)53ULL));
                                        if (!t567) {
                                            goto gm_lb72;
                                        }
                                        gm_t8588ma2cr9d7 t568;
                                        GM_ST(uint8_t, (uint8_t *)&t568 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t568 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t568 + 4, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t568 + 8, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t568 + 0), ((uint8_t)16ULL));
                                        gm_l50 = t568;
                                        goto gm_lb71;
                                    }
gm_lb72:;
                                    {
                                        uint8_t t569 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                        uint8_t t570 = (uint8_t)(t569 == ((uint8_t)54ULL));
                                        if (!t570) {
                                            goto gm_lb73;
                                        }
                                        gm_t8588ma2cr9d7 t571;
                                        GM_ST(uint8_t, (uint8_t *)&t571 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t571 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t571 + 4, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t571 + 8, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t571 + 0), ((uint8_t)17ULL));
                                        gm_l50 = t571;
                                        goto gm_lb71;
                                    }
gm_lb73:;
                                    {
                                        gm_t8588ma2cr9d7 t572;
                                        GM_ST(uint8_t, (uint8_t *)&t572 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t572 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t572 + 4, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t572 + 8, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t572 + 0), ((uint8_t)19ULL));
                                        gm_l50 = t572;
                                        goto gm_lb71;
                                    }
gm_lb74:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 82, 35);
                                }
gm_lb71:;
                                gm_trqfvmjtt4sij t573;
                                gm_f_list_fufbgf7((void *)&t573, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_ll554;
                                }
                                F.l51 = t573;
                                gm_l53 = ((uint64_t)0ULL);
                                uint64_t t574 = gm_l47;
                                uint64_t t575 = gm_f_us_cuv01fp(t574);
                                gm_l52 = t575;
                                {
                                    for (;;) {
                                        uint64_t t576 = gm_l53;
                                        uint64_t t577 = gm_l52;
                                        uint8_t t578 = (t576 < t577);
                                        if (!t578) {
                                            goto gm_lb75;
                                        }
                                        {
                                            {
                                                {
                                                    uint64_t t579 = gm_l53;
                                                    gm_l54 = t579;
                                                    void * t580 = (void *)((uint8_t *)&F.l51);
                                                    gm_f_truncate_j7f0pmi(t580, ((uint64_t)0ULL));
                                                    gm_b16_8 t581 = gm_l6;
                                                    gm_l55 = t581;
                                                    gm_l56 = ((uint64_t)0ULL);
                                                    {
                                                        for (;;) {
                                                            uint64_t t582 = gm_l56;
                                                            uint64_t t583 = GM_LD(uint64_t, (((uint8_t *)&gm_l55) + 8));
                                                            uint8_t t584 = (t582 < t583);
                                                            if (!t584) {
                                                                goto gm_lb80;
                                                            }
                                                            {
                                                                void * t585 = GM_LD(void *, ((uint8_t *)&gm_l55));
                                                                uint64_t t586 = GM_LD(uint64_t, (((uint8_t *)&gm_l55) + 8));
                                                                uint64_t t587 = gm_l56;
                                                                {
                                                                    {
                                                                        gm_b16_8 t588 = GM_LD(gm_b16_8, (((uint8_t *)t585) + (uintptr_t)t587 * 16));
                                                                        gm_l57 = t588;
                                                                        void * t589 = (void *)((uint8_t *)&F.l51);
                                                                        void * t590 = GM_LD(void *, ((uint8_t *)&gm_l57));
                                                                        uint64_t t591 = GM_LD(uint64_t, (((uint8_t *)&gm_l57) + 8));
                                                                        uint64_t t592 = gm_l54;
                                                                        uint64_t t593 = gm_l49;
                                                                        uint64_t r594; bool o595 = __builtin_mul_overflow(t592, t593, &r594);
                                                                        if (o595) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 91, 42);
                                                                        uint64_t t596 = r594;
                                                                        uint64_t t597 = gm_l54;
                                                                        uint64_t r598; bool o599 = __builtin_add_overflow(t597, ((uint64_t)1ULL), &r598);
                                                                        if (o599) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 91, 51);
                                                                        uint64_t t600 = r598;
                                                                        uint64_t t601 = gm_l49;
                                                                        uint64_t r602; bool o603 = __builtin_mul_overflow(t600, t601, &r602);
                                                                        if (o603) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 91, 50);
                                                                        uint64_t t604 = r602;
                                                                        uint64_t t605 = (uintptr_t)t596;
                                                                        uint64_t t606 = (uintptr_t)t604;
                                                                        if (t605 > t606 || t606 > (uintptr_t)t591) gm_trap_slice(t605, t606, t591, gm_srcns60k96jj1mt, 91, 40);
                                                                        uint64_t t607 = ((uintptr_t)t604) - (uintptr_t)t596;
                                                                        gm_b16_8 t608;
                                                                        GM_ST(void *, (uint8_t *)&t608, (void *)(((uint8_t *)t590 + (uintptr_t)t596 * 1)));
                                                                        GM_ST(uintptr_t, (uint8_t *)&t608 + 8, (uintptr_t)(t607));
                                                                        gm_tftb1odc739t5 t609 = gm_f_push_rluch9r(t589, t608);
                                                                        gm_l108 = t609;
                                                                        uint8_t t610 = (uint8_t)!((((uint8_t *)&gm_l108))[0] != 0);
                                                                        if (!t610) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l108) + 1, gm_srcns60k96jj1mt, 91, 30);
                                                                        goto gm_lb83;
                                                                    }
gm_lb84:;
                                                                    gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 91, 17);
                                                                }
gm_lb83:;
                                                            }
gm_lb82:;
                                                            uint64_t t611 = gm_l56;
                                                            uint64_t r612; bool o613 = __builtin_add_overflow(t611, ((uint64_t)1ULL), &r612);
                                                            uint64_t t614 = r612;
                                                            gm_l56 = t614;
                                                        }
                                                    }
gm_lb80:;
                                                    void * t615 = gm_l0;
                                                    gm_tl874t2atnlh3 t616 = gm_l1;
                                                    gm_t8588ma2cr9d7 t617 = gm_l50;
                                                    gm_t88rcucg1b7uk t618 = gm_l48;
                                                    gm_t88rcucg1b7uk t619 = gm_l48;
                                                    gm_t88rcucg1b7uk t620 = gm_l48;
                                                    gm_b16_8 t621 = GM_LD(gm_b16_8, ((uint8_t *)&F.l51));
                                                    void * t622 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                                    uint64_t t623 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                                    uint64_t t624 = gm_l54;
                                                    uint64_t t625 = gm_l49;
                                                    uint64_t r626; bool o627 = __builtin_mul_overflow(t624, t625, &r626);
                                                    if (o627) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 92, 85);
                                                    uint64_t t628 = r626;
                                                    uint64_t t629 = gm_l54;
                                                    uint64_t r630; bool o631 = __builtin_add_overflow(t629, ((uint64_t)1ULL), &r630);
                                                    if (o631) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 92, 94);
                                                    uint64_t t632 = r630;
                                                    uint64_t t633 = gm_l49;
                                                    uint64_t r634; bool o635 = __builtin_mul_overflow(t632, t633, &r634);
                                                    if (o635) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 92, 93);
                                                    uint64_t t636 = r634;
                                                    uint64_t t637 = (uintptr_t)t628;
                                                    uint64_t t638 = (uintptr_t)t636;
                                                    if (t637 > t638 || t638 > (uintptr_t)t623) gm_trap_slice(t637, t638, t623, gm_srcns60k96jj1mt, 92, 81);
                                                    uint64_t t639 = ((uintptr_t)t636) - (uintptr_t)t628;
                                                    gm_b16_8 t640;
                                                    GM_ST(void *, (uint8_t *)&t640, (void *)(((uint8_t *)t622 + (uintptr_t)t628 * 1)));
                                                    GM_ST(uintptr_t, (uint8_t *)&t640 + 8, (uintptr_t)(t639));
                                                    gm_f_apply_builtin_41dffti((void *)&gm_l109, (void *)NULL, cx, NULL, t615, t616, t617, t618, t619, t620, t621, t640);
                                                    if (gm_unw.kind) {
                                                        goto gm_ld561_1;
                                                    }
                                                    uint8_t t641 = (uint8_t)!((((uint8_t *)&gm_l109))[0] != 0);
                                                    if (t641) {
                                                    } else {
                                                        gm_t76123gfet73g t642 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l109) + 8));
                                                        GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                                                        ((uint8_t *)&F.ret)[0] = 1;
                                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&F.ret + 8), t642);
                                                        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                                                        goto gm_ld561_1;
                                                    }
                                                    goto gm_lb78;
                                                }
gm_lb79:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 89, 13);
                                            }
gm_lb78:;
                                        }
gm_lb77:;
                                        uint64_t t643 = gm_l53;
                                        uint64_t r644; bool o645 = __builtin_add_overflow(t643, ((uint64_t)1ULL), &r644);
                                        uint64_t t646 = r644;
                                        gm_l53 = t646;
                                    }
                                }
gm_lb75:;
                            }
                            {
                                void * t647 = (void *)((uint8_t *)&F.l51);
                                gm_f_deinit_tq06jtm(t647);
                            }
                            goto gm_ls648;
gm_ld561_1:;
                            {
                                gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
                                void * t649 = (void *)((uint8_t *)&F.l51);
                                gm_f_deinit_tq06jtm(t649);
                                gm_unw = gm_saved;
                            }
                            goto gm_ll554;
gm_ls648:;
gm_lb70:;
                            goto gm_lb68;
                        }
                        goto gm_ls650;
gm_ll554:;
                        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1073741892) { gm_unw.kind = GM_NONE; goto gm_lb69; }
                        goto gm_ll553;
gm_ls650:;
gm_lb69:;
                        {
                            void * t651 = gm_l0;
                            gm_tl874t2atnlh3 t652 = gm_l1;
                            gm_b16_8 t653;
                            GM_ST(void *, (uint8_t *)&t653, (void *)(gm_scqqkvgm1j3cs));
                            GM_ST(uintptr_t, (uint8_t *)&t653 + 8, (uintptr_t)(64));
                            gm_t76123gfet73g t654;
                            gm_f_trap_qh84l9d((void *)&t654, (void *)NULL, cx, NULL, t651, t652, t653);
                            if (gm_unw.kind) {
                                goto gm_ll553;
                            }
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t654);
                            goto gm_fdone;
                        }
                    }
                    goto gm_ls655;
gm_ll553:;
                    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1073741891) { gm_unw.kind = GM_NONE; goto gm_lb68; }
                    goto gm_ll546;
gm_ls655:;
gm_lb68:;
                    goto gm_lb1;
                }
                goto gm_ls656;
gm_ll546:;
                if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1073741886) { gm_unw.kind = GM_NONE; goto gm_lb63; }
                goto gm_ll118;
gm_ls656:;
gm_lb63:;
            }
            {
                {
                    uint8_t t657 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t658 = (uint8_t)(t657 == ((uint8_t)56ULL));
                    if (!t658) {
                        goto gm_lb85;
                    }
                    {
                        {
                            void * t659 = gm_l0;
                            gm_t88rcucg1b7uk t660 = gm_l4;
                            gm_f_kind_htv6093((void *)&gm_l110, t659, GM_CONV(gm_csguankko4lmhd, t660));
                            uint8_t t661 = GM_LD(uint8_t, (((uint8_t *)&gm_l110) + 0));
                            uint8_t t662 = (uint8_t)(t661 == ((uint8_t)11ULL));
                            if (!t662) {
                                goto gm_lb87;
                            }
                            uint64_t t663 = GM_LD(uint64_t, (((uint8_t *)&gm_l110) + 8));
                            gm_l58 = t663;
                            gm_t88rcucg1b7uk t664 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l110) + 8) + 8));
                            gm_l59 = t664;
                            {
                                {
                                    {
                                        void * t665 = gm_l0;
                                        gm_t88rcucg1b7uk t666 = gm_l3;
                                        gm_f_kind_htv6093((void *)&gm_l111, t665, GM_CONV(gm_csguankko4lmhd, t666));
                                    }
                                    uint8_t t667 = GM_LD(uint8_t, (((uint8_t *)&gm_l111) + 0));
                                    uint8_t t668 = (uint8_t)(t667 == ((uint8_t)11ULL));
                                    if (!t668) {
                                        goto gm_lb89;
                                    }
                                    gm_t88rcucg1b7uk t669 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l111) + 8) + 8));
                                    gm_l60 = t669;
                                    void * t670 = gm_l0;
                                    gm_t88rcucg1b7uk t671 = gm_l59;
                                    uint64_t t672 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t670, t671);
                                    if (gm_unw.kind) {
                                        goto gm_ll118;
                                    }
                                    uint64_t t673 = gm_f_us_cuv01fp(t672);
                                    gm_l61 = t673;
                                    void * t674 = gm_l0;
                                    gm_t88rcucg1b7uk t675 = gm_l60;
                                    uint64_t t676 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t674, t675);
                                    if (gm_unw.kind) {
                                        goto gm_ll118;
                                    }
                                    uint64_t t677 = gm_f_us_cuv01fp(t676);
                                    gm_l62 = t677;
                                    {
                                        void * t678 = gm_l0;
                                        gm_t88rcucg1b7uk t679 = gm_l60;
                                        gm_f_kind_htv6093((void *)&gm_l113, t678, GM_CONV(gm_csguankko4lmhd, t679));
                                    }
                                    {
                                        {
                                            uint8_t t680 = GM_LD(uint8_t, (((uint8_t *)&gm_l113) + 0));
                                            uint8_t t681 = (uint8_t)(t680 == ((uint8_t)3ULL));
                                            if (!t681) {
                                                goto gm_lb91;
                                            }
                                            gm_l112 = ((uint8_t)1);
                                            goto gm_lb90;
                                        }
gm_lb91:;
                                        {
                                            gm_l112 = ((uint8_t)0);
                                            goto gm_lb90;
                                        }
gm_lb92:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 100, 35);
                                    }
gm_lb90:;
                                    uint8_t t682 = gm_l112;
                                    if (t682) {
                                        gm_t8588ma2cr9d7 t683;
                                        GM_ST(uint8_t, (uint8_t *)&t683 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t683 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t683 + 4, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t683 + 8, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t683 + 0), ((uint8_t)2ULL));
                                        gm_l63 = t683;
                                    } else {
                                        gm_t8588ma2cr9d7 t684;
                                        GM_ST(uint8_t, (uint8_t *)&t684 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t684 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t684 + 4, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t684 + 8, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t684 + 0), ((uint8_t)0ULL));
                                        gm_l63 = t684;
                                    }
                                    gm_l65 = ((uint64_t)0ULL);
                                    uint64_t t685 = gm_l58;
                                    uint64_t t686 = gm_f_us_cuv01fp(t685);
                                    gm_l64 = t686;
                                    {
                                        for (;;) {
                                            uint64_t t687 = gm_l65;
                                            uint64_t t688 = gm_l64;
                                            uint8_t t689 = (t687 < t688);
                                            if (!t689) {
                                                goto gm_lb93;
                                            }
                                            {
                                                {
                                                    {
                                                        uint64_t t690 = gm_l65;
                                                        gm_l66 = t690;
                                                        void * t691 = gm_l0;
                                                        gm_tl874t2atnlh3 t692 = gm_l1;
                                                        gm_t8588ma2cr9d7 t693 = gm_l63;
                                                        gm_t88rcucg1b7uk t694 = gm_l59;
                                                        gm_t88rcucg1b7uk t695 = gm_l60;
                                                        void * t696 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                                        uint64_t t697 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t697)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t697, gm_srcns60k96jj1mt, 101, 68);
                                                        void * t698 = GM_LD(void *, (((uint8_t *)t696) + (uintptr_t)((uint64_t)0ULL) * 16));
                                                        uint64_t t699 = GM_LD(uint64_t, ((((uint8_t *)t696) + (uintptr_t)((uint64_t)0ULL) * 16) + 8));
                                                        uint64_t t700 = gm_l66;
                                                        uint64_t t701 = gm_l61;
                                                        uint64_t r702; bool o703 = __builtin_mul_overflow(t700, t701, &r702);
                                                        if (o703) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 101, 73);
                                                        uint64_t t704 = r702;
                                                        uint64_t t705 = gm_l66;
                                                        uint64_t r706; bool o707 = __builtin_add_overflow(t705, ((uint64_t)1ULL), &r706);
                                                        if (o707) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 101, 83);
                                                        uint64_t t708 = r706;
                                                        uint64_t t709 = gm_l61;
                                                        uint64_t r710; bool o711 = __builtin_mul_overflow(t708, t709, &r710);
                                                        if (o711) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 101, 82);
                                                        uint64_t t712 = r710;
                                                        uint64_t t713 = (uintptr_t)t704;
                                                        uint64_t t714 = (uintptr_t)t712;
                                                        if (t713 > t714 || t714 > (uintptr_t)t699) gm_trap_slice(t713, t714, t699, gm_srcns60k96jj1mt, 101, 68);
                                                        uint64_t t715 = ((uintptr_t)t712) - (uintptr_t)t704;
                                                        gm_b16_8 t716;
                                                        GM_ST(void *, (uint8_t *)&t716, (void *)(((uint8_t *)t698 + (uintptr_t)t704 * 1)));
                                                        GM_ST(uintptr_t, (uint8_t *)&t716 + 8, (uintptr_t)(t715));
                                                        gm_b16_8 t717 = GM_LD(gm_b16_8, ((uint8_t *)&t716));
                                                        void * t718 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                                        uint64_t t719 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                                        uint64_t t720 = gm_l66;
                                                        uint64_t t721 = gm_l62;
                                                        uint64_t r722; bool o723 = __builtin_mul_overflow(t720, t721, &r722);
                                                        if (o723) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 101, 102);
                                                        uint64_t t724 = r722;
                                                        uint64_t t725 = gm_l66;
                                                        uint64_t r726; bool o727 = __builtin_add_overflow(t725, ((uint64_t)1ULL), &r726);
                                                        if (o727) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 101, 112);
                                                        uint64_t t728 = r726;
                                                        uint64_t t729 = gm_l62;
                                                        uint64_t r730; bool o731 = __builtin_mul_overflow(t728, t729, &r730);
                                                        if (o731) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 101, 111);
                                                        uint64_t t732 = r730;
                                                        uint64_t t733 = (uintptr_t)t724;
                                                        uint64_t t734 = (uintptr_t)t732;
                                                        if (t733 > t734 || t734 > (uintptr_t)t719) gm_trap_slice(t733, t734, t719, gm_srcns60k96jj1mt, 101, 98);
                                                        uint64_t t735 = ((uintptr_t)t732) - (uintptr_t)t724;
                                                        gm_b16_8 t736;
                                                        GM_ST(void *, (uint8_t *)&t736, (void *)(((uint8_t *)t718 + (uintptr_t)t724 * 1)));
                                                        GM_ST(uintptr_t, (uint8_t *)&t736 + 8, (uintptr_t)(t735));
                                                        gm_f_convert_is6an2k((void *)&gm_l114, (void *)NULL, cx, NULL, t691, t692, t693, t694, t695, t717, t736);
                                                        if (gm_unw.kind) {
                                                            goto gm_ll118;
                                                        }
                                                        uint8_t t737 = (uint8_t)!((((uint8_t *)&gm_l114))[0] != 0);
                                                        if (t737) {
                                                        } else {
                                                            gm_t76123gfet73g t738 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l114) + 8));
                                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t738);
                                                            goto gm_fdone;
                                                        }
                                                        goto gm_lb96;
                                                    }
gm_lb97:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 101, 13);
                                                }
gm_lb96:;
                                            }
gm_lb95:;
                                            uint64_t t739 = gm_l65;
                                            uint64_t r740; bool o741 = __builtin_add_overflow(t739, ((uint64_t)1ULL), &r740);
                                            uint64_t t742 = r740;
                                            gm_l65 = t742;
                                        }
                                    }
gm_lb93:;
                                    goto gm_lb88;
                                }
gm_lb89:;
                                {
                                    void * t743 = gm_l0;
                                    gm_tl874t2atnlh3 t744 = gm_l1;
                                    gm_b16_8 t745;
                                    GM_ST(void *, (uint8_t *)&t745, (void *)(gm_smsrao7h22muv));
                                    GM_ST(uintptr_t, (uint8_t *)&t745 + 8, (uintptr_t)(60));
                                    gm_t76123gfet73g t746;
                                    gm_f_trap_qh84l9d((void *)&t746, (void *)NULL, cx, NULL, t743, t744, t745);
                                    if (gm_unw.kind) {
                                        goto gm_ll118;
                                    }
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t746);
                                    goto gm_fdone;
                                }
                            }
gm_lb88:;
                            goto gm_lb86;
                        }
gm_lb87:;
                        {
                            void * t747 = gm_l0;
                            gm_tl874t2atnlh3 t748 = gm_l1;
                            gm_b16_8 t749;
                            GM_ST(void *, (uint8_t *)&t749, (void *)(gm_spnr4ftjbm9m9));
                            GM_ST(uintptr_t, (uint8_t *)&t749 + 8, (uintptr_t)(60));
                            gm_t76123gfet73g t750;
                            gm_f_trap_qh84l9d((void *)&t750, (void *)NULL, cx, NULL, t747, t748, t749);
                            if (gm_unw.kind) {
                                goto gm_ll118;
                            }
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t750);
                            goto gm_fdone;
                        }
                    }
gm_lb86:;
                    goto gm_lb1;
                }
gm_lb85:;
            }
            {
                {
                    uint8_t t751 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t752 = (uint8_t)(t751 == ((uint8_t)57ULL));
                    if (!t752) {
                        goto gm_lb98;
                    }
                    gm_t9qkh0rsooc8d t753 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l2) + 4));
                    gm_l67 = t753;
                    {
                        {
                            void * t754 = gm_l0;
                            gm_t88rcucg1b7uk t755 = gm_l4;
                            gm_f_kind_htv6093((void *)&gm_l115, t754, GM_CONV(gm_csguankko4lmhd, t755));
                            uint8_t t756 = GM_LD(uint8_t, (((uint8_t *)&gm_l115) + 0));
                            uint8_t t757 = (uint8_t)(t756 == ((uint8_t)11ULL));
                            if (!t757) {
                                goto gm_lb100;
                            }
                            uint64_t t758 = GM_LD(uint64_t, (((uint8_t *)&gm_l115) + 8));
                            gm_l68 = t758;
                            gm_t88rcucg1b7uk t759 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l115) + 8) + 8));
                            gm_l69 = t759;
                            {
                                {
                                    void * t760 = gm_l0;
                                    gm_t88rcucg1b7uk t761 = gm_l3;
                                    gm_f_kind_htv6093((void *)&gm_l116, t760, GM_CONV(gm_csguankko4lmhd, t761));
                                    uint8_t t762 = GM_LD(uint8_t, (((uint8_t *)&gm_l116) + 0));
                                    uint8_t t763 = (uint8_t)(t762 == ((uint8_t)11ULL));
                                    if (!t763) {
                                        goto gm_lb102;
                                    }
                                    gm_t88rcucg1b7uk t764 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l116) + 8) + 8));
                                    gm_l70 = t764;
                                    void * t765 = gm_l0;
                                    gm_t88rcucg1b7uk t766 = gm_l69;
                                    uint64_t t767 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t765, t766);
                                    if (gm_unw.kind) {
                                        goto gm_ll118;
                                    }
                                    uint64_t t768 = gm_f_us_cuv01fp(t767);
                                    gm_l71 = t768;
                                    void * t769 = gm_l0;
                                    gm_t88rcucg1b7uk t770 = gm_l70;
                                    uint64_t t771 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t769, t770);
                                    if (gm_unw.kind) {
                                        goto gm_ll118;
                                    }
                                    uint64_t t772 = gm_f_us_cuv01fp(t771);
                                    gm_l72 = t772;
                                    gm_t9qkh0rsooc8d t773 = gm_l67;
                                    gm_t8588ma2cr9d7 t774 = gm_f_lane_scalar_hgnqjfe(t773);
                                    gm_l73 = t774;
                                    gm_l75 = ((uint64_t)0ULL);
                                    uint64_t t775 = gm_l68;
                                    uint64_t t776 = gm_f_us_cuv01fp(t775);
                                    gm_l74 = t776;
                                    {
                                        for (;;) {
                                            uint64_t t777 = gm_l75;
                                            uint64_t t778 = gm_l74;
                                            uint8_t t779 = (t777 < t778);
                                            if (!t779) {
                                                goto gm_lb103;
                                            }
                                            {
                                                {
                                                    {
                                                        uint64_t t780 = gm_l75;
                                                        gm_l76 = t780;
                                                        void * t781 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                                        uint64_t t782 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t782)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t782, gm_srcns60k96jj1mt, 110, 22);
                                                        void * t783 = GM_LD(void *, (((uint8_t *)t781) + (uintptr_t)((uint64_t)0ULL) * 16));
                                                        uint64_t t784 = GM_LD(uint64_t, ((((uint8_t *)t781) + (uintptr_t)((uint64_t)0ULL) * 16) + 8));
                                                        uint64_t t785 = gm_l76;
                                                        uint64_t t786 = gm_l71;
                                                        uint64_t r787; bool o788 = __builtin_mul_overflow(t785, t786, &r787);
                                                        if (o788) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 110, 27);
                                                        uint64_t t789 = r787;
                                                        uint64_t t790 = gm_l76;
                                                        uint64_t r791; bool o792 = __builtin_add_overflow(t790, ((uint64_t)1ULL), &r791);
                                                        if (o792) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 110, 37);
                                                        uint64_t t793 = r791;
                                                        uint64_t t794 = gm_l71;
                                                        uint64_t r795; bool o796 = __builtin_mul_overflow(t793, t794, &r795);
                                                        if (o796) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 110, 36);
                                                        uint64_t t797 = r795;
                                                        uint64_t t798 = (uintptr_t)t789;
                                                        uint64_t t799 = (uintptr_t)t797;
                                                        if (t798 > t799 || t799 > (uintptr_t)t784) gm_trap_slice(t798, t799, t784, gm_srcns60k96jj1mt, 110, 22);
                                                        uint64_t t800 = ((uintptr_t)t797) - (uintptr_t)t789;
                                                        gm_b16_8 t801;
                                                        GM_ST(void *, (uint8_t *)&t801, (void *)(((uint8_t *)t783 + (uintptr_t)t789 * 1)));
                                                        GM_ST(uintptr_t, (uint8_t *)&t801 + 8, (uintptr_t)(t800));
                                                        gm_l77 = t801;
                                                        void * t802 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                                        uint64_t t803 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                                        uint64_t t804 = gm_l76;
                                                        uint64_t t805 = gm_l72;
                                                        uint64_t r806; bool o807 = __builtin_mul_overflow(t804, t805, &r806);
                                                        if (o807) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 111, 26);
                                                        uint64_t t808 = r806;
                                                        uint64_t t809 = gm_l76;
                                                        uint64_t r810; bool o811 = __builtin_add_overflow(t809, ((uint64_t)1ULL), &r810);
                                                        if (o811) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 111, 36);
                                                        uint64_t t812 = r810;
                                                        uint64_t t813 = gm_l72;
                                                        uint64_t r814; bool o815 = __builtin_mul_overflow(t812, t813, &r814);
                                                        if (o815) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 111, 35);
                                                        uint64_t t816 = r814;
                                                        uint64_t t817 = (uintptr_t)t808;
                                                        uint64_t t818 = (uintptr_t)t816;
                                                        if (t817 > t818 || t818 > (uintptr_t)t803) gm_trap_slice(t817, t818, t803, gm_srcns60k96jj1mt, 111, 22);
                                                        uint64_t t819 = ((uintptr_t)t816) - (uintptr_t)t808;
                                                        gm_b16_8 t820;
                                                        GM_ST(void *, (uint8_t *)&t820, (void *)(((uint8_t *)t802 + (uintptr_t)t808 * 1)));
                                                        GM_ST(uintptr_t, (uint8_t *)&t820 + 8, (uintptr_t)(t819));
                                                        gm_l78 = t820;
                                                        gm_t9qkh0rsooc8d t821 = gm_l67;
                                                        uint8_t t822 = GM_LD(uint8_t, (((uint8_t *)&t821) + 0));
                                                        uint8_t t823 = (uint8_t)((t822 == ((uint8_t)0ULL)));
                                                        gm_t9qkh0rsooc8d t824;
                                                        GM_ST(uint8_t, ((uint8_t *)&t824 + 0), ((uint8_t)0ULL));
                                                        if (t823) {
                                                            gm_b16_8 t825 = gm_l77;
                                                            gm_tpmdr2hmdhsqr t826;
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t826 + 0), t825);
                                                            gm_l79 = t826;
                                                            void * t827 = gm_l0;
                                                            gm_tl874t2atnlh3 t828 = gm_l1;
                                                            gm_t8588ma2cr9d7 t829 = gm_l73;
                                                            gm_t88rcucg1b7uk t830 = gm_l70;
                                                            gm_t88rcucg1b7uk t831 = gm_l69;
                                                            gm_t88rcucg1b7uk t832 = gm_l69;
                                                            void * t833 = (void *)((uint8_t *)&gm_l79);
                                                            uint64_t t834 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                                                            gm_b16_8 t835;
                                                            GM_ST(void *, (uint8_t *)&t835, (void *)(((uint8_t *)t833 + (uintptr_t)((uint64_t)0ULL) * 16)));
                                                            GM_ST(uintptr_t, (uint8_t *)&t835 + 8, (uintptr_t)(t834));
                                                            gm_b16_8 t836 = gm_l78;
                                                            gm_f_apply_builtin_41dffti((void *)&gm_l117, (void *)NULL, cx, NULL, t827, t828, t829, t830, t831, t832, t835, t836);
                                                            if (gm_unw.kind) {
                                                                goto gm_ll118;
                                                            }
                                                            uint8_t t837 = (uint8_t)!((((uint8_t *)&gm_l117))[0] != 0);
                                                            if (t837) {
                                                            } else {
                                                                gm_t76123gfet73g t838 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l117) + 8));
                                                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                                                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t838);
                                                                goto gm_fdone;
                                                            }
                                                        } else {
                                                            void * t839 = gm_l0;
                                                            gm_tl874t2atnlh3 t840 = gm_l1;
                                                            gm_t8588ma2cr9d7 t841 = gm_l73;
                                                            gm_t88rcucg1b7uk t842 = gm_l69;
                                                            gm_t88rcucg1b7uk t843 = gm_l70;
                                                            gm_b16_8 t844 = gm_l77;
                                                            gm_b16_8 t845 = GM_LD(gm_b16_8, ((uint8_t *)&t844));
                                                            gm_b16_8 t846 = gm_l78;
                                                            gm_f_convert_is6an2k((void *)&gm_l118, (void *)NULL, cx, NULL, t839, t840, t841, t842, t843, t845, t846);
                                                            if (gm_unw.kind) {
                                                                goto gm_ll118;
                                                            }
                                                            uint8_t t847 = (uint8_t)!((((uint8_t *)&gm_l118))[0] != 0);
                                                            if (t847) {
                                                            } else {
                                                                gm_t76123gfet73g t848 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l118) + 8));
                                                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                                                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t848);
                                                                goto gm_fdone;
                                                            }
                                                        }
                                                        goto gm_lb106;
                                                    }
gm_lb107:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 109, 13);
                                                }
gm_lb106:;
                                            }
gm_lb105:;
                                            uint64_t t849 = gm_l75;
                                            uint64_t r850; bool o851 = __builtin_add_overflow(t849, ((uint64_t)1ULL), &r850);
                                            uint64_t t852 = r850;
                                            gm_l75 = t852;
                                        }
                                    }
gm_lb103:;
                                    goto gm_lb101;
                                }
gm_lb102:;
                                {
                                    void * t853 = gm_l0;
                                    gm_tl874t2atnlh3 t854 = gm_l1;
                                    gm_b16_8 t855;
                                    GM_ST(void *, (uint8_t *)&t855, (void *)(gm_so6lu2e8ofi9v));
                                    GM_ST(uintptr_t, (uint8_t *)&t855 + 8, (uintptr_t)(68));
                                    gm_t76123gfet73g t856;
                                    gm_f_trap_qh84l9d((void *)&t856, (void *)NULL, cx, NULL, t853, t854, t855);
                                    if (gm_unw.kind) {
                                        goto gm_ll118;
                                    }
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t856);
                                    goto gm_fdone;
                                }
                            }
gm_lb101:;
                            goto gm_lb99;
                        }
gm_lb100:;
                        {
                            void * t857 = gm_l0;
                            gm_tl874t2atnlh3 t858 = gm_l1;
                            gm_b16_8 t859;
                            GM_ST(void *, (uint8_t *)&t859, (void *)(gm_scqqkvgm1j3cs));
                            GM_ST(uintptr_t, (uint8_t *)&t859 + 8, (uintptr_t)(64));
                            gm_t76123gfet73g t860;
                            gm_f_trap_qh84l9d((void *)&t860, (void *)NULL, cx, NULL, t857, t858, t859);
                            if (gm_unw.kind) {
                                goto gm_ll118;
                            }
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t860);
                            goto gm_fdone;
                        }
                    }
gm_lb99:;
                    goto gm_lb1;
                }
gm_lb98:;
            }
            {
                {
                    uint8_t t861 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t862 = (uint8_t)(t861 == ((uint8_t)58ULL));
                    if (!t862) {
                        goto gm_lb108;
                    }
                    uint8_t t863 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 4));
                    gm_l80 = t863;
                    {
                        {
                            void * t864 = gm_l0;
                            gm_t88rcucg1b7uk t865 = gm_l4;
                            gm_f_kind_htv6093((void *)&gm_l119, t864, GM_CONV(gm_csguankko4lmhd, t865));
                            uint8_t t866 = GM_LD(uint8_t, (((uint8_t *)&gm_l119) + 0));
                            uint8_t t867 = (uint8_t)(t866 == ((uint8_t)11ULL));
                            if (!t867) {
                                goto gm_lb110;
                            }
                            uint64_t t868 = GM_LD(uint64_t, (((uint8_t *)&gm_l119) + 8));
                            gm_l81 = t868;
                            gm_t88rcucg1b7uk t869 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l119) + 8) + 8));
                            gm_l82 = t869;
                            void * t870 = gm_l0;
                            gm_t88rcucg1b7uk t871 = gm_l82;
                            uint64_t t872 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t870, t871);
                            if (gm_unw.kind) {
                                goto gm_ll118;
                            }
                            uint64_t t873 = gm_f_us_cuv01fp(t872);
                            gm_l83 = t873;
                            uint8_t t874 = gm_l80;
                            uint64_t t875 = (uint64_t)t874;
                            uint64_t t876 = gm_f_us_cuv01fp(t875);
                            gm_l84 = t876;
                            uint64_t t877 = gm_l84;
                            gm_l86 = t877;
                            uint64_t t878 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            gm_l85 = t878;
                            {
                                for (;;) {
                                    uint64_t t879 = gm_l86;
                                    uint64_t t880 = gm_l85;
                                    uint8_t t881 = (t879 < t880);
                                    if (!t881) {
                                        goto gm_lb111;
                                    }
                                    {
                                        {
                                            {
                                                uint64_t t882 = gm_l86;
                                                gm_l87 = t882;
                                                void * t883 = gm_l0;
                                                void * t884 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                                uint64_t t885 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                                uint64_t t886 = gm_l87;
                                                gm_b16_8 t887 = GM_LD(gm_b16_8, (((uint8_t *)t884) + (uintptr_t)t886 * 16));
                                                gm_b16_8 t888 = GM_LD(gm_b16_8, ((uint8_t *)&t887));
                                                uint64_t t889 = gm_f_get_u64_tgpb2vh(t883, t888);
                                                uint64_t t890 = gm_f_us_cuv01fp(t889);
                                                gm_l88 = t890;
                                                uint64_t t891 = gm_l88;
                                                uint64_t t892 = gm_l84;
                                                uint64_t t893 = gm_l81;
                                                uint64_t t894 = gm_f_us_cuv01fp(t893);
                                                uint64_t r895; bool o896 = __builtin_mul_overflow(t892, t894, &r895);
                                                if (o896) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 126, 25);
                                                uint64_t t897 = r895;
                                                uint8_t t898 = (t891 >= t897);
                                                if (t898) {
                                                    void * t899 = gm_l0;
                                                    gm_tl874t2atnlh3 t900 = gm_l1;
                                                    gm_b16_8 t901;
                                                    GM_ST(void *, (uint8_t *)&t901, (void *)(gm_ssvci9220kuoj));
                                                    GM_ST(uintptr_t, (uint8_t *)&t901 + 8, (uintptr_t)(44));
                                                    gm_t76123gfet73g t902;
                                                    gm_f_trap_qh84l9d((void *)&t902, (void *)NULL, cx, NULL, t899, t900, t901);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll118;
                                                    }
                                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t902);
                                                    goto gm_fdone;
                                                }
                                                void * t903 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                                uint64_t t904 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                                uint64_t t905 = gm_l88;
                                                uint64_t t906 = gm_l81;
                                                uint64_t t907 = gm_f_us_cuv01fp(t906);
                                                if (t907 == 0) gm_trap("division by zero", gm_srcns60k96jj1mt, 127, 26);
                                                uint64_t t908 = (uint64_t)(t905 / t907);
                                                if ((uintptr_t)t908 >= (uintptr_t)(t904)) gm_trap_index((uintptr_t)t908, t904, gm_srcns60k96jj1mt, 127, 24);
                                                gm_b16_8 t909 = GM_LD(gm_b16_8, (((uint8_t *)t903) + (uintptr_t)t908 * 16));
                                                gm_l89 = t909;
                                                uint64_t t910 = gm_l88;
                                                uint64_t t911 = gm_l81;
                                                uint64_t t912 = gm_f_us_cuv01fp(t911);
                                                if (t912 == 0) gm_trap("division by zero", gm_srcns60k96jj1mt, 128, 22);
                                                uint64_t t913 = (uint64_t)(t910 % t912);
                                                gm_l90 = t913;
                                                void * t914 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                                uint64_t t915 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                                uint64_t t916 = gm_l87;
                                                uint64_t t917 = gm_l84;
                                                uint64_t r918; bool o919 = __builtin_sub_overflow(t916, t917, &r918);
                                                if (o919) gm_trap("integer overflow in `-`", gm_srcns60k96jj1mt, 129, 22);
                                                uint64_t t920 = r918;
                                                uint64_t t921 = gm_l83;
                                                uint64_t r922; bool o923 = __builtin_mul_overflow(t920, t921, &r922);
                                                if (o923) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 129, 21);
                                                uint64_t t924 = r922;
                                                uint64_t t925 = gm_l87;
                                                uint64_t t926 = gm_l84;
                                                uint64_t r927; bool o928 = __builtin_sub_overflow(t925, t926, &r927);
                                                if (o928) gm_trap("integer overflow in `-`", gm_srcns60k96jj1mt, 129, 38);
                                                uint64_t t929 = r927;
                                                uint64_t r930; bool o931 = __builtin_add_overflow(t929, ((uint64_t)1ULL), &r930);
                                                if (o931) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 129, 38);
                                                uint64_t t932 = r930;
                                                uint64_t t933 = gm_l83;
                                                uint64_t r934; bool o935 = __builtin_mul_overflow(t932, t933, &r934);
                                                if (o935) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 129, 37);
                                                uint64_t t936 = r934;
                                                uint64_t t937 = (uintptr_t)t924;
                                                uint64_t t938 = (uintptr_t)t936;
                                                if (t937 > t938 || t938 > (uintptr_t)t915) gm_trap_slice(t937, t938, t915, gm_srcns60k96jj1mt, 129, 17);
                                                uint64_t t939 = ((uintptr_t)t936) - (uintptr_t)t924;
                                                gm_b16_8 t940;
                                                GM_ST(void *, (uint8_t *)&t940, (void *)(((uint8_t *)t914 + (uintptr_t)t924 * 1)));
                                                GM_ST(uintptr_t, (uint8_t *)&t940 + 8, (uintptr_t)(t939));
                                                void * t941 = GM_LD(void *, ((uint8_t *)&gm_l89));
                                                uint64_t t942 = GM_LD(uint64_t, (((uint8_t *)&gm_l89) + 8));
                                                uint64_t t943 = gm_l90;
                                                uint64_t t944 = gm_l83;
                                                uint64_t r945; bool o946 = __builtin_mul_overflow(t943, t944, &r945);
                                                if (o946) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 129, 71);
                                                uint64_t t947 = r945;
                                                uint64_t t948 = gm_l90;
                                                uint64_t r949; bool o950 = __builtin_add_overflow(t948, ((uint64_t)1ULL), &r949);
                                                if (o950) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 129, 80);
                                                uint64_t t951 = r949;
                                                uint64_t t952 = gm_l83;
                                                uint64_t r953; bool o954 = __builtin_mul_overflow(t951, t952, &r953);
                                                if (o954) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 129, 79);
                                                uint64_t t955 = r953;
                                                uint64_t t956 = (uintptr_t)t947;
                                                uint64_t t957 = (uintptr_t)t955;
                                                if (t956 > t957 || t957 > (uintptr_t)t942) gm_trap_slice(t956, t957, t942, gm_srcns60k96jj1mt, 129, 67);
                                                uint64_t t958 = ((uintptr_t)t955) - (uintptr_t)t947;
                                                gm_b16_8 t959;
                                                GM_ST(void *, (uint8_t *)&t959, (void *)(((uint8_t *)t941 + (uintptr_t)t947 * 1)));
                                                GM_ST(uintptr_t, (uint8_t *)&t959 + 8, (uintptr_t)(t958));
                                                gm_b16_8 t960 = GM_LD(gm_b16_8, ((uint8_t *)&t959));
                                                gm_f_copy_from_f7j6p05(t940, t960);
                                                goto gm_lb114;
                                            }
gm_lb115:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 124, 13);
                                        }
gm_lb114:;
                                    }
gm_lb113:;
                                    uint64_t t961 = gm_l86;
                                    uint64_t r962; bool o963 = __builtin_add_overflow(t961, ((uint64_t)1ULL), &r962);
                                    uint64_t t964 = r962;
                                    gm_l86 = t964;
                                }
                            }
gm_lb111:;
                            goto gm_lb109;
                        }
gm_lb110:;
                        {
                            void * t965 = gm_l0;
                            gm_tl874t2atnlh3 t966 = gm_l1;
                            gm_b16_8 t967;
                            GM_ST(void *, (uint8_t *)&t967, (void *)(gm_s1tv0n9hk7497));
                            GM_ST(uintptr_t, (uint8_t *)&t967 + 8, (uintptr_t)(57));
                            gm_t76123gfet73g t968;
                            gm_f_trap_qh84l9d((void *)&t968, (void *)NULL, cx, NULL, t965, t966, t967);
                            if (gm_unw.kind) {
                                goto gm_ll118;
                            }
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t968);
                            goto gm_fdone;
                        }
                    }
gm_lb109:;
                    goto gm_lb1;
                }
gm_lb108:;
            }
            {
                {
                    void * t969 = gm_l0;
                    gm_tl874t2atnlh3 t970 = gm_l1;
                    gm_b16_8 t971;
                    GM_ST(void *, (uint8_t *)&t971, (void *)(gm_s5e5797b1jurs));
                    GM_ST(uintptr_t, (uint8_t *)&t971 + 8, (uintptr_t)(19));
                    gm_t76123gfet73g t972;
                    gm_f_unsupported_70vvk1e((void *)&t972, (void *)NULL, cx, NULL, t969, t970, t971);
                    if (gm_unw.kind) {
                        goto gm_ll118;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t972);
                    goto gm_fdone;
                }
gm_lb116:;
                gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 7, 5);
            }
        }
        goto gm_ls973;
gm_ll118:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1073741824) { gm_unw.kind = GM_NONE; goto gm_lb1; }
        goto gm_lad;
gm_ls973:;
gm_lb1:;
    }
    {
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), GM_ZERO(gm_b0_1));
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

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
typedef struct gm_cs6mnftt383dnt { void * f0; void * f1; } gm_cs6mnftt383dnt;
typedef struct gm_cs7g99776jhl5b { int64_t f0; uint64_t f1; } gm_cs7g99776jhl5b;
typedef struct gm_csfq64m1m1rt6m { int64_t f0; } gm_csfq64m1m1rt6m;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { int64_t f0; uint64_t f1; } gm_tcva7d32r7rjb;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { double f0; uint8_t f1; uint8_t pe[7]; } gm_th2bqc58ss467;
typedef struct { int64_t f0; } gm_tm98ap4112hee;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t a[4096]; } gm_tpbv7nqggnvqr;
typedef struct { uint8_t f1; } gm_ttsm01t0d8srg;
_Static_assert(sizeof(gm_cs6mnftt383dnt) == 16 && _Alignof(gm_cs6mnftt383dnt) == 8 && offsetof(gm_cs6mnftt383dnt, f0) == 0 && offsetof(gm_cs6mnftt383dnt, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_cs7g99776jhl5b) == 16 && _Alignof(gm_cs7g99776jhl5b) == 8 && offsetof(gm_cs7g99776jhl5b, f0) == 0 && offsetof(gm_cs7g99776jhl5b, f1) == 8, "layout of `Forked`");
_Static_assert(sizeof(gm_csfq64m1m1rt6m) == 8 && _Alignof(gm_csfq64m1m1rt6m) == 8 && offsetof(gm_csfq64m1m1rt6m, f0) == 0, "layout of `Dir`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { uint64_t f0; uint8_t f1; uint8_t p2[7]; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; uint8_t f5; uint8_t f6; uint8_t pe[6]; } gm_t0rfe1ou14h21;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_b16_8 v1; } f1; } gm_t5uv522f0p612;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tm98ap4112hee v1; } f1; } gm_t62ebbi2i5rkd;
typedef struct { gm_tcva7d32r7rjb f0; uint8_t f1; uint8_t pe[7]; } gm_t6did6ki92he6;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tcva7d32r7rjb) == 16 && _Alignof(gm_tcva7d32r7rjb) == 8 && offsetof(gm_tcva7d32r7rjb, f0) == 0 && offsetof(gm_tcva7d32r7rjb, f1) == 8, "layout of `Forked`");
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_th2bqc58ss467) == 16 && _Alignof(gm_th2bqc58ss467) == 8 && offsetof(gm_th2bqc58ss467, f0) == 0 && offsetof(gm_th2bqc58ss467, f1) == 8, "layout of `?f64`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_t9qkh0rsooc8d v1; } f1; } gm_tj5jn79kq9pnm;
typedef struct { gm_t9805qtonbsag f0; gm_t9805qtonbsag f1; gm_t9805qtonbsag f2; gm_t9805qtonbsag f3; } gm_tk8ebsa8coi4p;
_Static_assert(sizeof(gm_tm98ap4112hee) == 8 && _Alignof(gm_tm98ap4112hee) == 8 && offsetof(gm_tm98ap4112hee, f0) == 0, "layout of `File`");
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
_Static_assert(sizeof(gm_tpbv7nqggnvqr) == 4096 && _Alignof(gm_tpbv7nqggnvqr) == 1 && offsetof(gm_tpbv7nqggnvqr, a) == 0, "layout of `[4096]u8`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint8_t f3; uint8_t pe[7]; } gm_ts1lka1v5g5ua;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; } f1; } gm_ttln0d0hd70en;
_Static_assert(sizeof(gm_ttsm01t0d8srg) == 1 && _Alignof(gm_ttsm01t0d8srg) == 1 && offsetof(gm_ttsm01t0d8srg, f1) == 0, "layout of `?()`");
typedef struct { uint8_t f0; uint8_t p1[7]; uint64_t f1; uint64_t f2; gm_tf5srh4mp4b3i f3; uint8_t f4; uint8_t f5; uint8_t p6[6]; uint64_t f6; } gm_tutaanatuv9ov;
_Static_assert(sizeof(gm_t0rfe1ou14h21) == 64 && _Alignof(gm_t0rfe1ou14h21) == 8 && offsetof(gm_t0rfe1ou14h21, f0) == 0 && offsetof(gm_t0rfe1ou14h21, f1) == 8 && offsetof(gm_t0rfe1ou14h21, f2) == 16 && offsetof(gm_t0rfe1ou14h21, f3) == 24 && offsetof(gm_t0rfe1ou14h21, f4) == 40 && offsetof(gm_t0rfe1ou14h21, f5) == 56 && offsetof(gm_t0rfe1ou14h21, f6) == 57, "layout of `Target`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_trqfvmjtt4sij v1; } f1; } gm_t470aqkia5212;
_Static_assert(sizeof(gm_t5uv522f0p612) == 24 && _Alignof(gm_t5uv522f0p612) == 8 && offsetof(gm_t5uv522f0p612, f0) == 0 && offsetof(gm_t5uv522f0p612, f1) == 8, "layout of `IoError!string`");
_Static_assert(sizeof(gm_t62ebbi2i5rkd) == 16 && _Alignof(gm_t62ebbi2i5rkd) == 8 && offsetof(gm_t62ebbi2i5rkd, f0) == 0 && offsetof(gm_t62ebbi2i5rkd, f1) == 8, "layout of `IoError!File`");
_Static_assert(sizeof(gm_t6did6ki92he6) == 24 && _Alignof(gm_t6did6ki92he6) == 8 && offsetof(gm_t6did6ki92he6, f0) == 0 && offsetof(gm_t6did6ki92he6, f1) == 16, "layout of `?Forked`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { void * f0; gm_trqfvmjtt4sij f1; gm_tk8ebsa8coi4p f2; gm_t0rfe1ou14h21 f3; } gm_ta4jakbus8leg;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; uint32_t f2; uint8_t pe[4]; } gm_tbah3mk9eqfec;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tj5jn79kq9pnm v0; gm_b16_8 v1; } f1; } gm_tgvh15qshf643;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
_Static_assert(sizeof(gm_tj5jn79kq9pnm) == 12 && _Alignof(gm_tj5jn79kq9pnm) == 4 && offsetof(gm_tj5jn79kq9pnm, f0) == 0 && offsetof(gm_tj5jn79kq9pnm, f1) == 4, "layout of `ReadError`");
_Static_assert(sizeof(gm_tk8ebsa8coi4p) == 32 && _Alignof(gm_tk8ebsa8coi4p) == 4 && offsetof(gm_tk8ebsa8coi4p, f0) == 0 && offsetof(gm_tk8ebsa8coi4p, f1) == 8 && offsetof(gm_tk8ebsa8coi4p, f2) == 16 && offsetof(gm_tk8ebsa8coi4p, f3) == 24, "layout of `Ambient`");
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t p3[5]; gm_trqfvmjtt4sij f3; } gm_tm4vhr17lpr3p;
typedef struct { gm_th2lqbo77pvsc f0; gm_b16_8 f1; uint64_t f2; } gm_tp3p9lj444cp7;
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; } gm_tr8dvlu9ik4gj;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts1lka1v5g5ua) == 56 && _Alignof(gm_ts1lka1v5g5ua) == 8 && offsetof(gm_ts1lka1v5g5ua, f0) == 0 && offsetof(gm_ts1lka1v5g5ua, f1) == 16 && offsetof(gm_ts1lka1v5g5ua, f2) == 32 && offsetof(gm_ts1lka1v5g5ua, f3) == 48, "layout of `Toolchain`");
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; void * f2; gm_th2bqc58ss467 f3; gm_th2lqbo77pvsc f4; uint8_t f5; uint8_t f6; uint8_t p7[6]; gm_tf5srh4mp4b3i f7; gm_t6did6ki92he6 f8; uint64_t f9; uint64_t f10; gm_trqfvmjtt4sij f11; gm_t88rcucg1b7uk f12; gm_t88rcucg1b7uk f13; } gm_tsli1i0j8bcrr;
typedef struct { gm_ts1lka1v5g5ua f0; uint8_t f1; uint8_t pe[7]; } gm_ttfsv7cb524b2;
_Static_assert(sizeof(gm_ttln0d0hd70en) == 12 && _Alignof(gm_ttln0d0hd70en) == 4 && offsetof(gm_ttln0d0hd70en, f0) == 0 && offsetof(gm_ttln0d0hd70en, f1) == 4, "layout of `IoError!()`");
_Static_assert(sizeof(gm_tutaanatuv9ov) == 56 && _Alignof(gm_tutaanatuv9ov) == 8 && offsetof(gm_tutaanatuv9ov, f0) == 0 && offsetof(gm_tutaanatuv9ov, f1) == 8 && offsetof(gm_tutaanatuv9ov, f2) == 16 && offsetof(gm_tutaanatuv9ov, f3) == 24 && offsetof(gm_tutaanatuv9ov, f4) == 40 && offsetof(gm_tutaanatuv9ov, f5) == 41 && offsetof(gm_tutaanatuv9ov, f6) == 48, "layout of `Report`");
typedef struct { gm_t6j6smj7qoctu f0; gm_tin97tt4ud0s1 f1; gm_trqfvmjtt4sij f2; } gm_t31ebhnaiodh3;
typedef struct { gm_tp3p9lj444cp7 f0; uint8_t f1; uint8_t pe[7]; } gm_t33ki4t2edp58;
_Static_assert(sizeof(gm_t470aqkia5212) == 48 && _Alignof(gm_t470aqkia5212) == 8 && offsetof(gm_t470aqkia5212, f0) == 0 && offsetof(gm_t470aqkia5212, f1) == 8, "layout of `string!List(string)`");
_Static_assert(sizeof(gm_ta4jakbus8leg) == 144 && _Alignof(gm_ta4jakbus8leg) == 8 && offsetof(gm_ta4jakbus8leg, f0) == 0 && offsetof(gm_ta4jakbus8leg, f1) == 8 && offsetof(gm_ta4jakbus8leg, f2) == 48 && offsetof(gm_ta4jakbus8leg, f3) == 80, "layout of `Prog`");
_Static_assert(sizeof(gm_tbah3mk9eqfec) == 48 && _Alignof(gm_tbah3mk9eqfec) == 8 && offsetof(gm_tbah3mk9eqfec, f0) == 0 && offsetof(gm_tbah3mk9eqfec, f1) == 16 && offsetof(gm_tbah3mk9eqfec, f2) == 40, "layout of `TestCase`");
typedef struct { gm_tr8dvlu9ik4gj f0; uint8_t f1; uint8_t pe[7]; } gm_tctputthhrk9k;
_Static_assert(sizeof(gm_tgvh15qshf643) == 24 && _Alignof(gm_tgvh15qshf643) == 8 && offsetof(gm_tgvh15qshf643, f0) == 0 && offsetof(gm_tgvh15qshf643, f1) == 8, "layout of `ReadError![]u8`");
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
_Static_assert(sizeof(gm_tm4vhr17lpr3p) == 48 && _Alignof(gm_tm4vhr17lpr3p) == 8 && offsetof(gm_tm4vhr17lpr3p, f0) == 0 && offsetof(gm_tm4vhr17lpr3p, f1) == 1 && offsetof(gm_tm4vhr17lpr3p, f2) == 2 && offsetof(gm_tm4vhr17lpr3p, f3) == 8, "layout of `Options`");
_Static_assert(sizeof(gm_tp3p9lj444cp7) == 48 && _Alignof(gm_tp3p9lj444cp7) == 8 && offsetof(gm_tp3p9lj444cp7, f0) == 0 && offsetof(gm_tp3p9lj444cp7, f1) == 24 && offsetof(gm_tp3p9lj444cp7, f2) == 40, "layout of `Finished`");
_Static_assert(sizeof(gm_tr8dvlu9ik4gj) == 120 && _Alignof(gm_tr8dvlu9ik4gj) == 8 && offsetof(gm_tr8dvlu9ik4gj, f0) == 0 && offsetof(gm_tr8dvlu9ik4gj, f1) == 40 && offsetof(gm_tr8dvlu9ik4gj, f2) == 80, "layout of `Extras`");
_Static_assert(sizeof(gm_tsli1i0j8bcrr) == 184 && _Alignof(gm_tsli1i0j8bcrr) == 8 && offsetof(gm_tsli1i0j8bcrr, f0) == 0 && offsetof(gm_tsli1i0j8bcrr, f1) == 16 && offsetof(gm_tsli1i0j8bcrr, f2) == 24 && offsetof(gm_tsli1i0j8bcrr, f3) == 32 && offsetof(gm_tsli1i0j8bcrr, f4) == 48 && offsetof(gm_tsli1i0j8bcrr, f5) == 72 && offsetof(gm_tsli1i0j8bcrr, f6) == 73 && offsetof(gm_tsli1i0j8bcrr, f7) == 80 && offsetof(gm_tsli1i0j8bcrr, f8) == 96 && offsetof(gm_tsli1i0j8bcrr, f9) == 120 && offsetof(gm_tsli1i0j8bcrr, f10) == 128 && offsetof(gm_tsli1i0j8bcrr, f11) == 136 && offsetof(gm_tsli1i0j8bcrr, f12) == 176 && offsetof(gm_tsli1i0j8bcrr, f13) == 180, "layout of `Pool`");
_Static_assert(sizeof(gm_ttfsv7cb524b2) == 64 && _Alignof(gm_ttfsv7cb524b2) == 8 && offsetof(gm_ttfsv7cb524b2, f0) == 0 && offsetof(gm_ttfsv7cb524b2, f1) == 56, "layout of `?Toolchain`");
typedef struct { uint64_t f0; gm_tbah3mk9eqfec f1; } gm_tu5ilovikalsj;
_Static_assert(sizeof(gm_t31ebhnaiodh3) == 112 && _Alignof(gm_t31ebhnaiodh3) == 8 && offsetof(gm_t31ebhnaiodh3, f0) == 0 && offsetof(gm_t31ebhnaiodh3, f1) == 32 && offsetof(gm_t31ebhnaiodh3, f2) == 72, "layout of `Cover`");
_Static_assert(sizeof(gm_t33ki4t2edp58) == 56 && _Alignof(gm_t33ki4t2edp58) == 8 && offsetof(gm_t33ki4t2edp58, f0) == 0 && offsetof(gm_t33ki4t2edp58, f1) == 48, "layout of `?Finished`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; uint8_t f2; uint8_t f3; gm_t9qkh0rsooc8d f4; uint8_t p5[3]; gm_th2lqbo77pvsc f5; uint8_t f6; uint8_t f7; uint8_t f8; uint8_t p9[5]; gm_b16_8 f9; uint8_t f10; uint8_t p11[7]; gm_tf5srh4mp4b3i f11; uint8_t f12; uint8_t p13[7]; gm_th2lqbo77pvsc f13; uint8_t f14; uint8_t f15; uint8_t p16[6]; uint64_t f16; gm_th2bqc58ss467 f17; uint8_t f18; uint8_t f19; uint8_t p20[6]; gm_th2lqbo77pvsc f20; gm_th2lqbo77pvsc f21; uint8_t f22; uint8_t f23; uint8_t p24[6]; gm_th2lqbo77pvsc f24; uint8_t f25; uint8_t p26[7]; gm_b16_8 f26; gm_b16_8 f27; gm_tctputthhrk9k f28; gm_th2lqbo77pvsc f29; gm_t0rfe1ou14h21 f30; gm_ttfsv7cb524b2 f31; gm_th2lqbo77pvsc f32; gm_tm4vhr17lpr3p f33; } gm_tcn5ttqhv05g6;
_Static_assert(sizeof(gm_tctputthhrk9k) == 128 && _Alignof(gm_tctputthhrk9k) == 8 && offsetof(gm_tctputthhrk9k, f0) == 0 && offsetof(gm_tctputthhrk9k, f1) == 120, "layout of `?Extras`");
_Static_assert(sizeof(gm_tu5ilovikalsj) == 56 && _Alignof(gm_tu5ilovikalsj) == 8 && offsetof(gm_tu5ilovikalsj, f0) == 0 && offsetof(gm_tu5ilovikalsj, f1) == 8, "layout of `(i: usize, x: TestCase)`");
_Static_assert(sizeof(gm_tcn5ttqhv05g6) == 624 && _Alignof(gm_tcn5ttqhv05g6) == 8 && offsetof(gm_tcn5ttqhv05g6, f0) == 0 && offsetof(gm_tcn5ttqhv05g6, f1) == 1 && offsetof(gm_tcn5ttqhv05g6, f2) == 2 && offsetof(gm_tcn5ttqhv05g6, f3) == 3 && offsetof(gm_tcn5ttqhv05g6, f4) == 4 && offsetof(gm_tcn5ttqhv05g6, f5) == 8 && offsetof(gm_tcn5ttqhv05g6, f6) == 32 && offsetof(gm_tcn5ttqhv05g6, f7) == 33 && offsetof(gm_tcn5ttqhv05g6, f8) == 34 && offsetof(gm_tcn5ttqhv05g6, f9) == 40 && offsetof(gm_tcn5ttqhv05g6, f10) == 56 && offsetof(gm_tcn5ttqhv05g6, f11) == 64 && offsetof(gm_tcn5ttqhv05g6, f12) == 80 && offsetof(gm_tcn5ttqhv05g6, f13) == 88 && offsetof(gm_tcn5ttqhv05g6, f14) == 112 && offsetof(gm_tcn5ttqhv05g6, f15) == 113 && offsetof(gm_tcn5ttqhv05g6, f16) == 120 && offsetof(gm_tcn5ttqhv05g6, f17) == 128 && offsetof(gm_tcn5ttqhv05g6, f18) == 144 && offsetof(gm_tcn5ttqhv05g6, f19) == 145 && offsetof(gm_tcn5ttqhv05g6, f20) == 152 && offsetof(gm_tcn5ttqhv05g6, f21) == 176 && offsetof(gm_tcn5ttqhv05g6, f22) == 200 && offsetof(gm_tcn5ttqhv05g6, f23) == 201 && offsetof(gm_tcn5ttqhv05g6, f24) == 208 && offsetof(gm_tcn5ttqhv05g6, f25) == 232 && offsetof(gm_tcn5ttqhv05g6, f26) == 240 && offsetof(gm_tcn5ttqhv05g6, f27) == 256 && offsetof(gm_tcn5ttqhv05g6, f28) == 272 && offsetof(gm_tcn5ttqhv05g6, f29) == 400 && offsetof(gm_tcn5ttqhv05g6, f30) == 424 && offsetof(gm_tcn5ttqhv05g6, f31) == 488 && offsetof(gm_tcn5ttqhv05g6, f32) == 552 && offsetof(gm_tcn5ttqhv05g6, f33) == 576, "layout of `Options`");
typedef struct { void * f0; gm_b16_8 f1; gm_tcn5ttqhv05g6 f2; gm_b16_8 f3; } gm_tp6ao262lsfmo;
_Static_assert(sizeof(gm_tp6ao262lsfmo) == 664 && _Alignof(gm_tp6ao262lsfmo) == 8 && offsetof(gm_tp6ao262lsfmo, f0) == 0 && offsetof(gm_tp6ao262lsfmo, f1) == 8 && offsetof(gm_tp6ao262lsfmo, f2) == 24 && offsetof(gm_tp6ao262lsfmo, f3) == 648, "layout of `InProcess`");
struct gm_fr_add_counts_hprtdd9 {
    gm_frame h;
    void * l0; /* counts */
    uint64_t l2; /* i */
    gm_b0_1 ret;
    gm_b0_1 w0;
};
struct gm_fr_ask_fork_fsiu08u {
    gm_frame h;
    gm_tcva7d32r7rjb ret;
};
struct gm_fr_broadcast_bfak2oh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_build_tests_mmafq15 {
    gm_frame h;
    gm_tcn5ttqhv05g6 l1; /* opts */
    gm_trqfvmjtt4sij l6; /* runner */
    gm_trqfvmjtt4sij l7; /* cmd */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_condvar_rp5e3qr {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_cover_file_bhhubi9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_iv39hj3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_j0iglrj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_n0agqu3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_otc7v7a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_entry_of_si4ihfm {
    gm_frame h;
    gm_tbah3mk9eqfec l1; /* c */
    uint32_t ret;
};
struct gm_fr_extend_7hs67uj {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_vogsd72 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_fork_test_2u613h7 {
    gm_frame h;
    gm_tcva7d32r7rjb ret;
};
struct gm_fr_format_map1fa6 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_heap_allocator_gihki37 {
    gm_frame h;
    gm_t3mf58gv9kf3n ret;
};
struct gm_fr_join_r8j2dp1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_launch_h699ulv {
    gm_frame h;
    gm_tp3p9lj444cp7 ret;
};
struct gm_fr_list_1qnmubu {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7vplt15 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_86kk1bu {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_fsi5en1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mutex_t7if00c {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_natively_smocg9v {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_6ct7n6b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_im9d4ro {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_s4q134v {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_snfdb36 {
    gm_frame h;
    gm_t33ki4t2edp58 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ucgmlo2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_2onqfca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_gfp647l {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_k4j2gj3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_skbsn8n {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_vm8t3ct {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_run_6mplkoc {
    gm_frame h;
    gm_tcn5ttqhv05g6 l2; /* opts */
    gm_trqfvmjtt4sij l5; /* chosen */
    gm_tbah3mk9eqfec l9; /* c */
    gm_tutaanatuv9ov l12; /* report */
    gm_t31ebhnaiodh3 l13; /* cover */
    uint8_t ret;
};
struct gm_fr_run_all_fj42v4f {
    gm_frame h;
    gm_trqfvmjtt4sij l11; /* threads */
    gm_tsli1i0j8bcrr l12; /* pool */
    gm_tp3p9lj444cp7 l29; /* f */
    gm_b0_1 ret;
};
struct gm_fr_run_interp_tests_hdiaknr {
    gm_frame h;
    gm_tcn5ttqhv05g6 l2; /* opts */
    gm_trqfvmjtt4sij l8; /* cmd */
    gm_trqfvmjtt4sij l10; /* roots */
    gm_tp6ao262lsfmo l14; /* ip */
    gm_trqfvmjtt4sij l15; /* native */
    gm_trqfvmjtt4sij l16; /* at */
    gm_ta4jakbus8leg l17; /* p */
    gm_tpbv7nqggnvqr l21; /* buf */
    gm_ttsm01t0d8srg ret;
};
struct gm_fr_run_native_ije4jng {
    gm_frame h;
    gm_tcn5ttqhv05g6 l1; /* opts */
    gm_trqfvmjtt4sij l7; /* entries */
    gm_tpbv7nqggnvqr l11; /* buf */
    gm_trqfvmjtt4sij l14; /* roots */
    gm_ttsm01t0d8srg ret;
};
struct gm_fr_selected_8kh2cc4 {
    gm_frame h;
    gm_tbah3mk9eqfec l0; /* t */
    uint8_t ret;
};
struct gm_fr_system_io_mcjcl9m {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_system_output_eg72lom {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_unlock_9f1vcl6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_wait_k1r8nlm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_worker_s2irjfh {
    gm_frame h;
    gm_tp3p9lj444cp7 l3; /* f */
    gm_b0_1 ret;
    gm_b0_1 w0;
};
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_add_counts_hprtdd9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_cs7g99776jhl5b gm_f_ask_fork_fsiu08u(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_broadcast_bfak2oh(void * a0);
__attribute__((aligned(4))) void gm_f_build_tests_mmafq15(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_collect_rp6p3ji(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a0, gm_b16_8 a1, uint64_t a2, gm_th2bqc58ss467 a3);
__attribute__((aligned(4))) uint8_t gm_f_compile_entries_l9qlniu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2, gm_b16_8 a3, const void * a4);
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_condvar_rp5e3qr(void);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_cover_file_bhhubi9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) uint8_t gm_f_coverage_report_hrhbf22(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3, const void * a4);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_iv39hj3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_j0iglrj(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n0agqu3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_otc7v7a(void);
inline __attribute__((aligned(4))) uint32_t gm_f_entry_of_si4ihfm(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint8_t a2);
__attribute__((aligned(4))) uint32_t gm_f_entry_point_n2gu79u(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_executable_lr5dqch(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_7hs67uj(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_vogsd72(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_finish_2t60fs7(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tcva7d32r7rjb gm_f_fork_test_2u613h7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_format_lo2svpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_map1fa6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_heap_allocator_gihki37(void);
__attribute__((aligned(4))) void gm_f_instrument_mk8clvl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_qveh5b2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_join_r8j2dp1(gm_csfq64m1m1rt6m a0);
__attribute__((aligned(4))) void gm_f_launch_h699ulv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1, void * a2, const void * a3, uint64_t a4, gm_th2bqc58ss467 a5, const void * a6);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_sqdm19s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_1qnmubu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7vplt15(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_86kk1bu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_fsi5en1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_lock_u5lks01(void * a0);
__attribute__((aligned(4))) void gm_f_make_temp_dir_vi8f88d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_mutex_t7if00c(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_native_exe_name_rqchv3n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_natively_smocg9v(void * a0, uint64_t a1);
__attribute__((aligned(4))) uint8_t gm_f_needs_native_kvptfrn(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_prepare_codegen_4hnlio1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_prog_61arbs1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_6ct7n6b(void * a0, gm_csfq64m1m1rt6m a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_s4q134v(void * a0, gm_tf5srh4mp4b3i a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_snfdb36(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_read_all_r9rh9qd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_record_abrqivi(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_gfp647l(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_skbsn8n(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_vm8t3ct(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_remove_all_h0k1qkh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_run_6mplkoc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_run_all_fj42v4f(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1, void * a2, void * a3, gm_b16_8 a4, gm_b16_8 a5, uint64_t a6, gm_th2bqc58ss467 a7, const void * a8, void * a9);
__attribute__((aligned(4))) void gm_f_run_exe_4182ncf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1, uint64_t a2, gm_th2bqc58ss467 a3, const void * a4);
__attribute__((aligned(4))) uint8_t gm_f_run_interp_entry_ssof80s(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, const void * a3);
__attribute__((aligned(4))) gm_ttsm01t0d8srg gm_f_run_interp_tests_hdiaknr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, gm_b16_8 a3, gm_b16_8 a4, uint8_t a5, void * a6);
__attribute__((aligned(4))) gm_ttsm01t0d8srg gm_f_run_native_ije4jng(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2, gm_b16_8 a3, uint8_t a4, void * a5, void * a6);
__attribute__((aligned(4))) void gm_f_runner_for_79o4h4m(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_scope_4n0q0tq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_selected_8kh2cc4(const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_selected_8kh2cc4_c(gm_tbah3mk9eqfec a0, gm_th2lqbo77pvsc a1);
__attribute__((aligned(4))) gm_t62ebbi2i5rkd gm_f_spawn_534m93a(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_system_io_mcjcl9m(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_system_output_eg72lom(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_temp_dir_i6apmsd(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_unlock_9f1vcl6(void * a0);
inline __attribute__((aligned(4))) void gm_f_wait_k1r8nlm(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_worker_s2irjfh(void * a0);
GM_NOINLINE bool gm_pf454qfrpt6r(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE bool gm_pfu8dq5bikbh(gm_node *cx, const gm_type *d, const void *v, const char *file, uint32_t line, uint32_t col);
extern int64_t gm_x_gm_proc_fork_800kh6p(void) __asm__(GM_SYM("gm_proc_fork"));
extern void gm_x_gm_proc_fork_exit_rdgcnpn(int32_t) __asm__(GM_SYM("gm_proc_fork_exit"));
extern uint8_t gm_cgd1khc5sal[24] __attribute__((aligned(8)));
extern void *const gm_hd3g3ujqvah4[];
extern void *const gm_hd3t1u951rul[];
extern void *const gm_hdaqemfrshlp[];
extern void *const gm_hddfeetqn95b[];
extern void *const gm_hdf4dspmusp5[];
extern void *const gm_hdffkeesmui4[];
extern void *const gm_hdu50j4i95ke[];
extern void *const gm_hdu8v6n5i2qd[];
extern void *const gm_hdvslis4o40u[];
extern void *const gm_hl9hta9cqrqu[];
extern const uint8_t gm_s06m3met1mb9v[3];
extern const uint8_t gm_s5lqhiu2bn6of[8];
extern const uint8_t gm_s63emi69ethib[50];
extern const uint8_t gm_s79el5a3kii78[12];
extern const uint8_t gm_sb49lg0fssjs7[19];
extern const uint8_t gm_sbqjqj9ajcalm[80];
extern const uint8_t gm_sdv4asmg9feh5[12];
extern const uint8_t gm_shg1pn1r3sjgi[13];
extern const uint8_t gm_sjbhqu0o8ebub[6];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_soka1k58t5da3[16];
extern const uint8_t gm_sq0r7ud0kv45f[8];
extern const uint8_t gm_sql5f30410gqn[20];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcrq1gl2e1lsg5[];
extern const uint8_t gm_sru5s20jprgc9[12];
extern const uint8_t gm_svaibtfuu0gf2[48];
extern const gm_tmpl gm_tpl2egb012obd94;
extern const gm_type gm_ty6groq37ram;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_add_counts_hprtdd9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_add_counts_hprtdd9 F;
    gm_b16_8 gm_l1; /* text */
    gm_b16_8 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    F.l2 = ((uint64_t)0ULL);
    gm_b16_8 t3 = gm_l1;
    gm_l3 = t3;
    {
        gm_node wn4 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hl9hta9cqrqu, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t7 = gm_l3;
        gm_b0_1 t8 = gm_f_lines_9uns0k2_sqdm19s((void *)NULL, (&wn4), NULL, t7);
        if (gm_unw.kind) {
            goto gm_ll6;
        }
    }
    goto gm_ls9;
gm_ll6:;
    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we5; }
    goto gm_lad;
gm_ls9:;
gm_lb1:;
we5:;
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
inline __attribute__((aligned(4))) void gm_f_broadcast_bfak2oh(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_broadcast_bfak2oh F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 53u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)t2);
    gm_t9qkh0rsooc8d t4;
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)2ULL));
    uint32_t t5 = __atomic_fetch_add(((uint32_t *)t3), ((uint32_t)1ULL), gm_ord[GM_LD(uint8_t, ((uint8_t *)&t4))]);
    void * t6 = gm_l0;
    void * t7 = (void *)((uint8_t *)t6);
    int64_t t8 = gm_futex_wake(t7, ((uint32_t)4294967295ULL));
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_condvar_rp5e3qr(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_condvar_rp5e3qr F;
    GM_STACK_CHECK_SHALLOW(&F, 12u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t88rcucg1b7uk t2;
    GM_ST(uint32_t, ((uint8_t *)&t2 + 0), ((uint32_t)0ULL));
    F.ret = t2;
    goto gm_fret;
gm_fret:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
gm_fprop:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
}
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_gkki4kq F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* needle */
    GM_STACK_CHECK_SHALLOW(&F, 101u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    gm_tf5srh4mp4b3i gm_l3;
    gm_b16_8 t5 = gm_l0;
    gm_b16_8 t6 = gm_l1;
    gm_tf5srh4mp4b3i t7 = gm_f_find_nab29co(t5, t6);
    gm_l3 = t7;
    {
        {
            uint8_t t8 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
            if (t8) {
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
        gm_trap("no match arm matched the scrutinee", gm_src3d0v42kavhv7, 84, 50);
    }
gm_lb1:;
    uint8_t t9 = gm_l2;
    F.ret = t9;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_cover_file_bhhubi9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1) {
    struct gm_fr_cover_file_bhhubi9 F;
    gm_b16_8 gm_l0; /* dir */
    uint64_t gm_l1; /* j */
    GM_STACK_CHECK(&F, 161u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l2;
    gm_b16_8 t2 = gm_l0;
    uint64_t t3 = gm_l1;
    gm_t4ce0jd3tdro4 t4;
    GM_ST(uint64_t, ((uint8_t *)&t4 + 0), t3);
    gm_f_format_map1fa6((void *)&gm_l2, (void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t5 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t5) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 8, gm_srcrq1gl2e1lsg5, 267, 81);
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
    gm_b16_8 t8 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t2, t7);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_73v4ttb F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 8)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_iv39hj3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_iv39hj3 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 8)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_j0iglrj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_j0iglrj F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 56)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n0agqu3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_n0agqu3 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_otc7v7a(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_otc7v7a F;
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
inline __attribute__((aligned(4))) uint32_t gm_f_entry_of_si4ihfm(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint8_t a2) {
    struct gm_fr_entry_of_si4ihfm F;
    void * gm_l0; /* s */
    uint8_t gm_l2; /* bench */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    uint32_t gm_l3;
    uint8_t t2 = gm_l2;
    if (t2) {
        void * t3 = gm_l0;
        uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)&F.l1) + 40));
        uint32_t t5 = gm_f_entry_point_n2gu79u((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t5;
    } else {
        uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)&F.l1) + 40));
        gm_l3 = t6;
    }
    uint32_t t7 = gm_l3;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_7hs67uj(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_7hs67uj F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_2onqfca(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 4)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 4);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 4)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_vogsd72(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_vogsd72 F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_0jmnnpk(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 16);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 16)));
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
inline __attribute__((aligned(4))) void gm_f_format_map1fa6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_map1fa6 F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4ce0jd3tdro4 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sru5s20jprgc9));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(11));
    gm_f_format_lo2svpj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl2egb012obd94), t3);
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
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_heap_allocator_gihki37(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_heap_allocator_gihki37 F;
    GM_STACK_CHECK_SHALLOW(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t2 = (void *)((uint8_t *)gm_cgd1khc5sal);
    void * t3 = (void *)((uint8_t *)gm_cgd1khc5sal);
    gm_t3mf58gv9kf3n t4;
    GM_ST(void *, ((uint8_t *)&t4 + 0), t2);
    GM_ST(void *, ((uint8_t *)&t4 + 8), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return GM_CONV(gm_cs6mnftt383dnt, F.ret);
gm_fprop:
    return GM_CONV(gm_cs6mnftt383dnt, F.ret);
}
inline __attribute__((aligned(4))) void gm_f_join_r8j2dp1(gm_csfq64m1m1rt6m a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_join_r8j2dp1 F;
    gm_tm98ap4112hee gm_l0; /* t */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_tm98ap4112hee, a0);
    int64_t t2 = GM_LD(int64_t, ((uint8_t *)&gm_l0));
    int64_t t3 = gm_thread_join(t2);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_list_1qnmubu(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_1qnmubu F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_n0agqu3();
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
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_5dhgg4i F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_73v4ttb();
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
inline __attribute__((aligned(4))) void gm_f_list_7vplt15(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7vplt15 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_iv39hj3();
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
inline __attribute__((aligned(4))) void gm_f_list_86kk1bu(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_86kk1bu F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_j0iglrj();
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
inline __attribute__((aligned(4))) void gm_f_list_fsi5en1(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_fsi5en1 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_otc7v7a();
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
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_mutex_t7if00c(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mutex_t7if00c F;
    GM_STACK_CHECK_SHALLOW(&F, 12u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t88rcucg1b7uk t2;
    GM_ST(uint32_t, ((uint8_t *)&t2 + 0), ((uint32_t)0ULL));
    F.ret = t2;
    goto gm_fret;
gm_fret:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
gm_fprop:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
}
inline __attribute__((aligned(4))) uint8_t gm_f_natively_smocg9v(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_natively_smocg9v F;
    void * gm_l0; /* fallback */
    uint64_t gm_l1; /* j */
    void * gm_l2; /* fb */
    GM_STACK_CHECK_SHALLOW(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l3;
    uint8_t t5 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l0)) != 0);
    if (t5) {
        void * t6 = GM_LD(void *, ((uint8_t *)&gm_l0));
        gm_l2 = t6;
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t7 = gm_l2;
    void * t8 = GM_LD(void *, (((uint8_t *)t7) + 16));
    uint64_t t9 = GM_LD(uint64_t, ((((uint8_t *)t7) + 16) + 8));
    uint64_t t10 = gm_l1;
    if ((uintptr_t)t10 >= (uintptr_t)(t9)) gm_trap_index((uintptr_t)t10, t9, gm_srcrq1gl2e1lsg5, 236, 12);
    {
        {
            uint8_t t11 = (uint8_t)(((((uint8_t *)t8) + (uintptr_t)t10 * 16))[8] != 0);
            if (t11) {
                goto gm_lb2;
            }
            gm_l3 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l3 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcrq1gl2e1lsg5, 236, 12);
    }
gm_lb1:;
    uint8_t t12 = gm_l3;
    F.ret = t12;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_6ct7n6b(void * a0, gm_csfq64m1m1rt6m a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_6ct7n6b F;
    void * gm_l0; /* l */
    gm_tm98ap4112hee gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tm98ap4112hee, a1);
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
        gm_b16_8 t9 = gm_f_empty_slice_iv39hj3();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_skbsn8n(t8, ((uint64_t)1ULL), t10);
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
    gm_tm98ap4112hee t18 = gm_l1;
    GM_ST(gm_tm98ap4112hee, (((uint8_t *)t16) + (uintptr_t)t17 * 8), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_im9d4ro F;
    void * gm_l0; /* l */
    uint32_t gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_d914i5u();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_2onqfca(t8, ((uint64_t)1ULL), t10);
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
    uint32_t t18 = gm_l1;
    GM_ST(uint32_t, (((uint8_t *)t16) + (uintptr_t)t17 * 4), t18);
    void * t19 = gm_l0;
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, ((uint8_t *)t20));
    uint64_t t22 = gm_l2;
    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
    uint64_t t25 = r23;
    uint64_t t26 = ((uintptr_t)t25) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t27;
    GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t21 + (uintptr_t)((uint64_t)0ULL) * 4)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_s4q134v(void * a0, gm_tf5srh4mp4b3i a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_s4q134v F;
    void * gm_l0; /* l */
    gm_tf5srh4mp4b3i gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_otc7v7a();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_vm8t3ct(t8, ((uint64_t)1ULL), t10);
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
    gm_tf5srh4mp4b3i t18 = gm_l1;
    GM_ST(gm_tf5srh4mp4b3i, (((uint8_t *)t16) + (uintptr_t)t17 * 16), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_snfdb36(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_snfdb36 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_j0iglrj();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_gfp647l(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 56), ((uint8_t *)&F.l1), 56);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 56)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_ucgmlo2 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_73v4ttb();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_k4j2gj3(t8, ((uint64_t)1ULL), t10);
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
    uint64_t t18 = gm_l1;
    GM_ST(uint64_t, (((uint8_t *)t16) + (uintptr_t)t17 * 8), t18);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_2onqfca F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)4ULL), ((uint64_t)4ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_gfp647l(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_gfp647l F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)56ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_k4j2gj3 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)8ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_skbsn8n(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_skbsn8n F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)8ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_vm8t3ct(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_vm8t3ct F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_selected_8kh2cc4(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_selected_8kh2cc4 F;
    gm_th2lqbo77pvsc gm_l1; /* filter */
    gm_b16_8 gm_l2; /* f */
    gm_b16_8 gm_l3; /* m */
    GM_STACK_CHECK_SHALLOW(&F, 158u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t2 = (uint8_t)((((uint8_t *)a1))[16] != 0);
    if (t2) {
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)a1));
        gm_l2 = t3;
    } else {
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)a0));
    gm_b16_8 t5 = gm_l2;
    uint8_t t6 = gm_f_contains_gkki4kq(t4, t5);
    if (t6) {
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
    uint8_t t7 = (uint8_t)(((((uint8_t *)a0) + 16))[16] != 0);
    if (t7) {
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 16));
        gm_l3 = t8;
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    gm_b16_8 t9 = gm_l3;
    gm_b16_8 t10 = gm_l2;
    uint8_t t11 = gm_f_contains_gkki4kq(t9, t10);
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_system_io_mcjcl9m(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_system_io_mcjcl9m F;
    GM_STACK_CHECK_SHALLOW(&F, 0u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_system_output_eg72lom(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_system_output_eg72lom F;
    GM_STACK_CHECK_SHALLOW(&F, 0u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_unlock_9f1vcl6(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_unlock_9f1vcl6 F;
    void * gm_l0; /* m */
    GM_STACK_CHECK(&F, 54u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)t2);
    gm_t9qkh0rsooc8d t4;
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)2ULL));
    uint32_t t5 = __atomic_exchange_n(((uint32_t *)t3), ((uint32_t)0ULL), gm_ord[GM_LD(uint8_t, ((uint8_t *)&t4))]);
    uint8_t t6 = ((t5 == ((uint32_t)2ULL)));
    if (t6) {
        void * t7 = gm_l0;
        void * t8 = (void *)((uint8_t *)t7);
        int64_t t9 = gm_futex_wake(t8, ((uint32_t)1ULL));
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_wait_k1r8nlm(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_wait_k1r8nlm F;
    void * gm_l0; /* c */
    void * gm_l1; /* m */
    uint32_t gm_l2; /* seen */
    GM_STACK_CHECK(&F, 85u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)t2);
    gm_t9qkh0rsooc8d t4;
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)1ULL));
    uint32_t t5 = __atomic_load_n(((uint32_t *)t3), gm_ord[GM_LD(uint8_t, ((uint8_t *)&t4))]);
    gm_l2 = t5;
    void * t6 = gm_l1;
    gm_f_unlock_9f1vcl6(t6);
    void * t7 = gm_l0;
    void * t8 = (void *)((uint8_t *)t7);
    uint32_t t9 = gm_l2;
    int64_t t10 = gm_futex_wait(t8, t9, ((int64_t)-1LL));
    void * t11 = gm_l1;
    gm_f_lock_u5lks01(t11);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_cs7g99776jhl5b gm_f_ask_fork_fsiu08u(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ask_fork_fsiu08u F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* j */
    gm_tcva7d32r7rjb gm_l2; /* f */
    GM_STACK_CHECK(&F, 333u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l3;
    void * t11 = gm_l0;
    void * t12 = (void *)(((uint8_t *)t11) + 176);
    gm_f_lock_u5lks01(t12);
    {
        for (;;) {
            void * t13 = gm_l0;
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)t13) + 73));
            if (!t14) {
                goto gm_lb1;
            }
            {
                void * t15 = gm_l0;
                void * t16 = (void *)(((uint8_t *)t15) + 180);
                void * t17 = gm_l0;
                void * t18 = (void *)(((uint8_t *)t17) + 176);
                gm_f_wait_k1r8nlm(t16, t18);
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t19 = gm_l0;
    GM_ST(uint8_t, (((uint8_t *)t19) + 73), ((uint8_t)1));
    void * t20 = gm_l0;
    uint64_t t21 = gm_l1;
    gm_tf5srh4mp4b3i t22;
    GM_ST(uint8_t, (uint8_t *)&t22 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t22 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t22 + 12, 0);
    GM_ST(uint64_t, ((uint8_t *)&t22), t21);
    ((uint8_t *)&t22)[8] = 1;
    GM_ST(gm_tf5srh4mp4b3i, (((uint8_t *)t20) + 80), t22);
    void * t23 = gm_l0;
    void * t24 = (void *)(((uint8_t *)t23) + 180);
    gm_f_broadcast_bfak2oh(t24);
    {
        for (;;) {
            void * t25 = gm_l0;
            {
                {
                    uint8_t t26 = (uint8_t)(((((uint8_t *)t25) + 96))[16] != 0);
                    if (t26) {
                        goto gm_lb8;
                    }
                    gm_l3 = ((uint8_t)1);
                    goto gm_lb7;
                }
gm_lb8:;
                {
                    gm_l3 = ((uint8_t)0);
                    goto gm_lb7;
                }
gm_lb9:;
                gm_trap("no match arm matched the scrutinee", gm_srcrq1gl2e1lsg5, 367, 11);
            }
gm_lb7:;
            uint8_t t27 = gm_l3;
            if (!t27) {
                goto gm_lb4;
            }
            {
                void * t28 = gm_l0;
                void * t29 = (void *)(((uint8_t *)t28) + 180);
                void * t30 = gm_l0;
                void * t31 = (void *)(((uint8_t *)t30) + 176);
                gm_f_wait_k1r8nlm(t29, t31);
            }
gm_lb6:;
        }
    }
gm_lb4:;
    void * t32 = gm_l0;
    uint8_t t33 = (uint8_t)(((((uint8_t *)t32) + 96))[16] != 0);
    if (t33) {
        gm_tcva7d32r7rjb t34 = GM_LD(gm_tcva7d32r7rjb, (((uint8_t *)t32) + 96));
        gm_l2 = t34;
    } else {
        gm_trap("reached `@unreachable()`", gm_srcrq1gl2e1lsg5, 368, 29);
    }
    void * t35 = gm_l0;
    gm_t6did6ki92he6 t36;
    memset(&t36, 0, sizeof t36);
    GM_ST(gm_t6did6ki92he6, (((uint8_t *)t35) + 96), t36);
    void * t37 = gm_l0;
    GM_ST(uint8_t, (((uint8_t *)t37) + 73), ((uint8_t)0));
    void * t38 = gm_l0;
    void * t39 = (void *)(((uint8_t *)t38) + 180);
    gm_f_broadcast_bfak2oh(t39);
    void * t40 = gm_l0;
    void * t41 = (void *)(((uint8_t *)t40) + 176);
    gm_f_unlock_9f1vcl6(t41);
    gm_tcva7d32r7rjb t42 = gm_l2;
    F.ret = t42;
    goto gm_fret;
gm_fret:
    return GM_CONV(gm_cs7g99776jhl5b, F.ret);
gm_fprop:
    return GM_CONV(gm_cs7g99776jhl5b, F.ret);
}
inline __attribute__((aligned(4))) void gm_f_build_tests_mmafq15(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_build_tests_mmafq15 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l2; /* entries */
    gm_b16_8 gm_l3; /* dir */
    gm_b16_8 gm_l4; /* exe */
    gm_b16_8 gm_l5; /* e */
    GM_STACK_CHECK(&F, 429u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t470aqkia5212 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_b16_8 t2 = gm_l3;
    void * t3 = (void *)((uint8_t *)&F.l1);
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sjbhqu0o8ebub));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(5));
    gm_b16_8 t5 = gm_f_native_exe_name_rqchv3n((void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t6 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t2, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t6;
    {
        void * t7 = gm_l0;
        gm_b16_8 t8 = gm_l2;
        gm_b16_8 t9 = gm_l4;
        uint8_t t10 = gm_f_compile_entries_l9qlniu((void *)NULL, cx, NULL, t7, t8, ((uint8_t)1), t9, &(*(gm_tcn5ttqhv05g6 *)((uint8_t *)&F.l1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t11 = (uint8_t)!t10;
        if (t11) {
            memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
            goto gm_fdone;
        }
    }
    {
        gm_b16_8 t12 = gm_l4;
        gm_f_runner_for_79o4h4m((void *)&gm_l8, (void *)NULL, cx, NULL, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t13 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (t13) {
        memmove(((uint8_t *)&F.l6), (((uint8_t *)&gm_l8) + 8), 40);
    } else {
        gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
        gm_l5 = t14;
        gm_b16_8 t15 = gm_l5;
        if (gm_pf454qfrpt6r(cx, gm_s5lqhiu2bn6of, 7, gm_srcrq1gl2e1lsg5, 146, 9)) {
            goto gm_lad;
        }
        if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8), gm_srcrq1gl2e1lsg5, 146, 9))) {
            goto gm_lad;
        }
        if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srcrq1gl2e1lsg5, 146, 9)) {
            goto gm_lad;
        }
        gm_node *pn16 = gm_find(cx, 621690230);
        if (!pn16) gm_trap("no handler installed for effect `Env`", gm_srcrq1gl2e1lsg5, 147, 9);
        if (pn16->kind == GM_NATIVE) {
            gm_unw.code = (uint8_t)((uint8_t)2ULL);
            gm_unw.kind = GM_EXIT; gm_unw.frame = NULL; gm_unw.label = 0; gm_unw.is_err = 1;
            goto gm_lad;
        } else {
            ((gm_b0_1 (*)(void *, gm_node *, gm_node *, uint8_t))pn16->clauses[2])(pn16->env, pn16->hctx, pn16, ((uint8_t)2ULL));
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        __builtin_unreachable();
    }
    {
        gm_trqfvmjtt4sij t17;
        gm_f_list_epvducg((void *)&t17, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l7 = t17;
    }
    void * t18 = (void *)((uint8_t *)&F.l7);
    gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
    gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
    gm_tftb1odc739t5 t21 = gm_f_extend_vogsd72(t18, t20);
    gm_l9 = t21;
    uint8_t t22 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (!t22) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcrq1gl2e1lsg5, 150, 5);
    void * t23 = (void *)((uint8_t *)&F.l7);
    gm_b16_8 t24 = gm_l4;
    gm_tftb1odc739t5 t25 = gm_f_push_3ef746b(t23, t24);
    gm_l10 = t25;
    uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t26) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srcrq1gl2e1lsg5, 151, 5);
    {
        gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
        gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t28);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_cover_file_bhhubi9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
extern __attribute__((aligned(4))) uint32_t gm_f_entry_of_si4ihfm(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint8_t a2);
inline __attribute__((aligned(4))) gm_tcva7d32r7rjb gm_f_fork_test_2u613h7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_fork_test_2u613h7 F;
    void * gm_l0; /* ip */
    uint64_t gm_l1; /* j */
    uint64_t gm_l2; /* started */
    int64_t gm_l3; /* h */
    uint8_t gm_l4; /* status */
    GM_STACK_CHECK(&F, 228u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t4ce0jd3tdro4 gm_l5;
    gm_node *pn2 = gm_find(cx, 1579066750);
    if (!pn2) gm_trap("no handler installed for effect `Clock`", gm_srcrq1gl2e1lsg5, 250, 16);
    gm_t4ce0jd3tdro4 t3;
    t3 = ((gm_t4ce0jd3tdro4 (*)(void *, gm_node *, gm_node *))pn2->clauses[0])(pn2->env, pn2->hctx, pn2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t3;
    uint64_t t4 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
    gm_l2 = t4;
    int64_t t5 = gm_x_gm_proc_fork_800kh6p();
    gm_l3 = t5;
    int64_t t6 = gm_l3;
    uint8_t t7 = (!(t6 == ((int64_t)0LL)));
    if (t7) {
        int64_t t8 = gm_l3;
        uint64_t t9 = gm_l2;
        gm_tcva7d32r7rjb t10;
        GM_ST(int64_t, ((uint8_t *)&t10 + 0), t8);
        GM_ST(uint64_t, ((uint8_t *)&t10 + 8), t9);
        F.ret = t10;
        goto gm_fret;
    }
    void * t11 = gm_l0;
    void * t12 = GM_LD(void *, ((uint8_t *)t11));
    void * t13 = gm_l0;
    void * t14 = GM_LD(void *, (((uint8_t *)t13) + 648));
    uint64_t t15 = GM_LD(uint64_t, ((((uint8_t *)t13) + 648) + 8));
    uint64_t t16 = gm_l1;
    if ((uintptr_t)t16 >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)t16, t15, gm_srcrq1gl2e1lsg5, 253, 45);
    uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)t14) + (uintptr_t)t16 * 4));
    void * t18 = gm_l0;
    gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)t18) + 8));
    void * t20 = gm_l0;
    uint8_t t21 = gm_f_run_interp_entry_ssof80s((void *)NULL, cx, NULL, t12, t17, t19, &(*(gm_tcn5ttqhv05g6 *)(((uint8_t *)t20) + 24)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t21;
    uint8_t t22 = gm_l4;
    int32_t t23 = (int32_t)t22;
    gm_x_gm_proc_fork_exit_rdgcnpn(t23);
    gm_trap("reached `@unreachable()`", gm_srcrq1gl2e1lsg5, 255, 5);
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_launch_h699ulv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1, void * a2, const void * a3, uint64_t a4, gm_th2bqc58ss467 a5, const void * a6) {
    struct gm_fr_launch_h699ulv F;
    gm_b16_8 gm_l0; /* cmd */
    gm_t9qkh0rsooc8d gm_l1; /* selector */
    void * gm_l2; /* fallback */
    gm_t6did6ki92he6 gm_l3; /* forked */
    uint64_t gm_l4; /* j */
    gm_th2bqc58ss467 gm_l5; /* timeout */
    gm_th2lqbo77pvsc gm_l6; /* cover_dir */
    void * gm_l7; /* fb */
    uint64_t gm_l8; /* k */
    gm_tcva7d32r7rjb gm_l9; /* f */
    GM_STACK_CHECK(&F, 399u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    memcpy(&gm_l3, a3, sizeof gm_l3);
    gm_l4 = a4;
    gm_l5 = a5;
    memcpy(&gm_l6, a6, sizeof gm_l6);
    {
        {
            uint8_t t8 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l2)) != 0);
            if (!t8) {
                goto gm_lb2;
            }
            void * t9 = GM_LD(void *, ((uint8_t *)&gm_l2));
            gm_l7 = t9;
            {
                {
                    void * t10 = gm_l7;
                    void * t11 = GM_LD(void *, (((uint8_t *)t10) + 16));
                    uint64_t t12 = GM_LD(uint64_t, ((((uint8_t *)t10) + 16) + 8));
                    uint64_t t13 = gm_l4;
                    if ((uintptr_t)t13 >= (uintptr_t)(t12)) gm_trap_index((uintptr_t)t13, t12, gm_srcrq1gl2e1lsg5, 225, 17);
                    uint8_t t14 = (uint8_t)(((((uint8_t *)t11) + (uintptr_t)t13 * 16))[8] != 0);
                    if (!t14) {
                        goto gm_lb4;
                    }
                    uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)t11) + (uintptr_t)t13 * 16));
                    gm_l8 = t15;
                    {
                        void * t16 = gm_l7;
                        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)t16));
                        uint64_t t18 = gm_l8;
                        gm_th2bqc58ss467 t19 = gm_l5;
                        gm_t9qkh0rsooc8d t20;
                        GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)0ULL));
                        gm_th2lqbo77pvsc t21 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l6));
                        gm_f_run_exe_4182ncf((void *)&(*(gm_tp3p9lj444cp7 *)gm_out), (void *)NULL, cx, NULL, t17, t20, t18, t19, &t21);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                }
gm_lb4:;
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            uint8_t t22 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
            if (!t22) {
                goto gm_lb6;
            }
            gm_tcva7d32r7rjb t23 = GM_LD(gm_tcva7d32r7rjb, ((uint8_t *)&gm_l3));
            gm_l9 = t23;
            int64_t t24 = GM_LD(int64_t, ((uint8_t *)&gm_l9));
            uint8_t t25 = (t24 > ((int64_t)0LL));
            if (t25) {
                int64_t t26 = GM_LD(int64_t, ((uint8_t *)&gm_l9));
                gm_tm98ap4112hee t27;
                GM_ST(int64_t, ((uint8_t *)&t27 + 0), t26);
                gm_b16_8 t28;
                GM_ST(void *, (uint8_t *)&t28, (void *)(gm_soka1k58t5da3));
                GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(15));
                uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                gm_th2bqc58ss467 t30 = gm_l5;
                gm_f_collect_rp6p3ji((void *)&(*(gm_tp3p9lj444cp7 *)gm_out), (void *)NULL, cx, NULL, t27, t28, t29, t30);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto gm_lb5;
        }
gm_lb6:;
    }
gm_lb5:;
    {
        gm_b16_8 t31 = gm_l0;
        gm_t9qkh0rsooc8d t32 = gm_l1;
        uint64_t t33 = gm_l4;
        gm_th2bqc58ss467 t34 = gm_l5;
        gm_th2lqbo77pvsc t35 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l6));
        gm_f_run_exe_4182ncf((void *)&(*(gm_tp3p9lj444cp7 *)gm_out), (void *)NULL, cx, NULL, t31, t32, t33, t34, &t35);
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
extern __attribute__((aligned(4))) uint8_t gm_f_natively_smocg9v(void * a0, uint64_t a1);
__attribute__((aligned(4))) uint8_t gm_f_run_6mplkoc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2) {
    struct gm_fr_run_6mplkoc F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* input */
    uint8_t gm_l3; /* bench */
    gm_b16_8 gm_l4; /* cases */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    uint64_t gm_l10; /* j */
    gm_tf5srh4mp4b3i gm_l11; /* filtered_out */
    uint8_t gm_l14; /* status */
    GM_STACK_CHECK(&F, 1361u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_tu5ilovikalsj gm_l15;
    gm_tftb1odc739t5 gm_l16;
    uint8_t gm_l17;
    gm_t4ce0jd3tdro4 gm_l18;
    gm_ttsm01t0d8srg gm_l19;
    gm_ttsm01t0d8srg gm_l20;
    uint8_t gm_l21;
    gm_ttsm01t0d8srg gm_l22;
    gm_t9qkh0rsooc8d t19 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l2));
    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&t19) + 0));
    uint8_t t21 = (uint8_t)((t20 == ((uint8_t)12ULL)));
    gm_t9qkh0rsooc8d t22;
    GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)12ULL));
    gm_l3 = t21;
    {
        {
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 145));
            if (!t23) {
                goto gm_lb2;
            }
            uint8_t t24 = gm_l3;
            if (!t24) {
                goto gm_lb2;
            }
            if (gm_pf454qfrpt6r(cx, gm_s63emi69ethib, 49, gm_srcrq1gl2e1lsg5, 56, 9)) {
                goto gm_lad;
            }
            F.ret = ((uint8_t)2ULL);
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 145));
            if (!t25) {
                goto gm_lb4;
            }
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 56));
            if (!t26) {
                goto gm_lb4;
            }
            if (gm_pf454qfrpt6r(cx, gm_sbqjqj9ajcalm, 79, gm_srcrq1gl2e1lsg5, 60, 9)) {
                goto gm_lad;
            }
            F.ret = ((uint8_t)2ULL);
            goto gm_fret;
        }
gm_lb4:;
    }
gm_lb3:;
    uint8_t t27 = gm_l3;
    if (t27) {
        void * t28 = gm_l0;
        gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)t28) + 1008));
        gm_l4 = t29;
    } else {
        void * t30 = gm_l0;
        gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)t30) + 968));
        gm_l4 = t31;
    }
    {
        gm_trqfvmjtt4sij t32;
        gm_f_list_5dhgg4i((void *)&t32, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l5 = t32;
    }
    gm_b16_8 t33 = gm_l4;
    gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
    gm_l6 = t34;
    gm_l7 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t35 = gm_l7;
            uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint8_t t37 = (t35 < t36);
            if (!t37) {
                goto gm_lb5;
            }
            {
                {
                    uint64_t t38 = gm_l7;
                    void * t39 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    uint64_t t41 = gm_l7;
                    gm_tbah3mk9eqfec t42 = GM_LD(gm_tbah3mk9eqfec, (((uint8_t *)t39) + (uintptr_t)t41 * 48));
                    GM_ST(uint64_t, ((uint8_t *)&gm_l15 + 0), t38);
                    GM_ST(gm_tbah3mk9eqfec, ((uint8_t *)&gm_l15 + 8), t42);
                }
                {
                    {
                        uint64_t t43 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
                        gm_l8 = t43;
                        {
                            memmove(((uint8_t *)&F.l9), (((uint8_t *)&gm_l15) + 8), 48);
                        }
                        {
                            uint8_t t44 = gm_f_selected_8kh2cc4(&(*(gm_tbah3mk9eqfec *)((uint8_t *)&F.l9)), &(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l2) + 88)));
                            if (t44) {
                                void * t45 = (void *)((uint8_t *)&F.l5);
                                uint64_t t46 = gm_l8;
                                gm_tftb1odc739t5 t47 = gm_f_push_ucgmlo2(t45, t46);
                                gm_l16 = t47;
                                uint8_t t48 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                                if (!t48) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_srcrq1gl2e1lsg5, 66, 39);
                            }
                        }
                        goto gm_lb8;
                    }
gm_lb9:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrq1gl2e1lsg5, 65, 5);
                }
gm_lb8:;
            }
gm_lb7:;
            uint64_t t49 = gm_l7;
            uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
            uint64_t t52 = r50;
            gm_l7 = t52;
        }
    }
gm_lb5:;
    {
        {
            uint8_t t53 = (uint8_t)(((((uint8_t *)&F.l2) + 64))[8] != 0);
            if (!t53) {
                goto gm_lb11;
            }
            uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 64));
            gm_l10 = t54;
            uint64_t t55 = gm_l10;
            uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
            uint8_t t57 = (t55 >= t56);
            if (t57) {
                uint64_t t58 = gm_l10;
                uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
                if (gm_pf454qfrpt6r(cx, gm_sql5f30410gqn, 19, gm_srcrq1gl2e1lsg5, 71, 13)) {
                    goto gm_lad;
                }
                if (gm_pfu8dq5bikbh(cx, &gm_ty6groq37ram, ((uint8_t *)&t58), gm_srcrq1gl2e1lsg5, 71, 13)) {
                    goto gm_lad;
                }
                if (gm_pf454qfrpt6r(cx, gm_sb49lg0fssjs7, 18, gm_srcrq1gl2e1lsg5, 71, 13)) {
                    goto gm_lad;
                }
                if (gm_pfu8dq5bikbh(cx, &gm_ty6groq37ram, ((uint8_t *)&t59), gm_srcrq1gl2e1lsg5, 71, 13)) {
                    goto gm_lad;
                }
                if (gm_pf454qfrpt6r(cx, gm_sq0r7ud0kv45f, 7, gm_srcrq1gl2e1lsg5, 71, 13)) {
                    goto gm_lad;
                }
                F.ret = ((uint8_t)2ULL);
                goto gm_fret;
            }
            {
                void * t60 = gm_l0;
                void * t61 = gm_l0;
                void * t62 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                void * t64 = GM_LD(void *, ((uint8_t *)&F.l5));
                uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
                uint64_t t66 = gm_l10;
                if ((uintptr_t)t66 >= (uintptr_t)(t65)) gm_trap_index((uintptr_t)t66, t65, gm_srcrq1gl2e1lsg5, 74, 61);
                uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)t64) + (uintptr_t)t66 * 8));
                if ((uintptr_t)t67 >= (uintptr_t)(t63)) gm_trap_index((uintptr_t)t67, t63, gm_srcrq1gl2e1lsg5, 74, 55);
                uint8_t t68 = gm_l3;
                uint32_t t69 = gm_f_entry_of_si4ihfm((void *)NULL, cx, NULL, t61, &(*(gm_tbah3mk9eqfec *)(((uint8_t *)t62) + (uintptr_t)t67 * 48)), t68);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t70 = gm_l1;
                uint8_t t71 = gm_f_run_interp_entry_ssof80s((void *)NULL, cx, NULL, t60, t69, t70, &(*(gm_tcn5ttqhv05g6 *)((uint8_t *)&F.l2)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t71;
                goto gm_fret;
            }
        }
gm_lb11:;
    }
gm_lb10:;
    gm_tf5srh4mp4b3i t72;
    memset(&t72, 0, sizeof t72);
    gm_l11 = t72;
    {
        {
            uint8_t t73 = (uint8_t)(((((uint8_t *)&F.l2) + 88))[16] != 0);
            if (t73) {
                goto gm_lb13;
            }
            gm_l17 = ((uint8_t)0);
            goto gm_lb12;
        }
gm_lb13:;
        {
            gm_l17 = ((uint8_t)1);
            goto gm_lb12;
        }
gm_lb14:;
        gm_trap("no match arm matched the scrutinee", gm_srcrq1gl2e1lsg5, 77, 8);
    }
gm_lb12:;
    uint8_t t74 = gm_l17;
    if (t74) {
        uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
        uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
        uint64_t r77; bool o78 = __builtin_sub_overflow(t75, t76, &r77);
        if (o78) gm_trap("integer overflow in `-`", gm_srcrq1gl2e1lsg5, 77, 49);
        uint64_t t79 = r77;
        gm_tf5srh4mp4b3i t80;
        GM_ST(uint8_t, (uint8_t *)&t80 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t80 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t80 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t80), t79);
        ((uint8_t *)&t80)[8] = 1;
        gm_l11 = t80;
    }
    {
        uint8_t t81 = gm_l3;
        gm_tf5srh4mp4b3i t82 = gm_l11;
        uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 113));
        uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 144));
        gm_node *pn85 = gm_find(cx, 1579066750);
        if (!pn85) gm_trap("no handler installed for effect `Clock`", gm_srcrq1gl2e1lsg5, 78, 121);
        gm_t4ce0jd3tdro4 t86;
        t86 = ((gm_t4ce0jd3tdro4 (*)(void *, gm_node *, gm_node *))pn85->clauses[0])(pn85->env, pn85->hctx, pn85);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l18 = t86;
        uint64_t t87 = GM_LD(uint64_t, ((uint8_t *)&gm_l18));
        gm_tutaanatuv9ov t88;
        GM_ST(uint8_t, (uint8_t *)&t88 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t88 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t88 + 4, 0);
        GM_ST(uint16_t, (uint8_t *)&t88 + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&t88 + 44, 0);
        GM_ST(uint8_t, ((uint8_t *)&t88 + 0), t81);
        GM_ST(uint64_t, ((uint8_t *)&t88 + 8), ((uint64_t)0ULL));
        GM_ST(uint64_t, ((uint8_t *)&t88 + 16), ((uint64_t)0ULL));
        GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t88 + 24), t82);
        GM_ST(uint8_t, ((uint8_t *)&t88 + 40), t83);
        GM_ST(uint8_t, ((uint8_t *)&t88 + 41), t84);
        GM_ST(uint64_t, ((uint8_t *)&t88 + 48), t87);
        F.l12 = t88;
    }
    uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 56));
    if (t89) {
        void * t90 = gm_l0;
        gm_b16_8 t91 = gm_l1;
        gm_b16_8 t92 = gm_l4;
        gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&t92));
        gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
        gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&t94));
        uint8_t t96 = gm_l3;
        void * t97 = (void *)((uint8_t *)&F.l12);
        gm_ttsm01t0d8srg t98 = gm_f_run_interp_tests_hdiaknr((void *)NULL, cx, NULL, t90, t91, &(*(gm_tcn5ttqhv05g6 *)((uint8_t *)&F.l2)), t93, t95, t96, t97);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l19 = t98;
        uint8_t t99 = (uint8_t)((((uint8_t *)&gm_l19))[0] != 0);
        if (t99) {
        } else {
            F.ret = ((uint8_t)1ULL);
            goto gm_fret;
        }
        void * t100 = (void *)((uint8_t *)&F.l12);
        uint8_t t101 = gm_f_finish_2t60fs7((void *)NULL, cx, NULL, t100);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t101;
        goto gm_fret;
    }
    uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 145));
    uint8_t t103 = (uint8_t)!t102;
    if (t103) {
        void * t104 = gm_l0;
        gm_b16_8 t105 = gm_l4;
        gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)&t105));
        gm_b16_8 t107 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
        gm_b16_8 t108 = GM_LD(gm_b16_8, ((uint8_t *)&t107));
        uint8_t t109 = gm_l3;
        void * t110 = (void *)((uint8_t *)&F.l12);
        gm_ttsm01t0d8srg t111 = gm_f_run_native_ije4jng((void *)NULL, cx, NULL, t104, &(*(gm_tcn5ttqhv05g6 *)((uint8_t *)&F.l2)), t106, t108, t109, t110, ((void *)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l20 = t111;
        uint8_t t112 = (uint8_t)((((uint8_t *)&gm_l20))[0] != 0);
        if (t112) {
        } else {
            F.ret = ((uint8_t)1ULL);
            goto gm_fret;
        }
        void * t113 = (void *)((uint8_t *)&F.l12);
        uint8_t t114 = gm_f_finish_2t60fs7((void *)NULL, cx, NULL, t113);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t114;
        goto gm_fret;
    }
    {
        gm_b16_8 t115 = gm_l1;
        gm_t6j6smj7qoctu t116;
        gm_f_scope_4n0q0tq((void *)&t116, (void *)NULL, cx, NULL, t115);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t117;
        gm_f_list_1qnmubu((void *)&t117, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tin97tt4ud0s1 t118;
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t118 + 0), t117);
        gm_trqfvmjtt4sij t119;
        gm_f_list_go2qg0b((void *)&t119, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t31ebhnaiodh3 t120;
        GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t120 + 0), t116);
        GM_ST(gm_tin97tt4ud0s1, ((uint8_t *)&t120 + 32), t118);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t120 + 72), t119);
        F.l13 = t120;
    }
    gm_l14 = ((uint8_t)1ULL);
    {
        void * t121 = gm_l0;
        gm_b16_8 t122 = gm_l4;
        gm_b16_8 t123 = GM_LD(gm_b16_8, ((uint8_t *)&t122));
        gm_b16_8 t124 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
        gm_b16_8 t125 = GM_LD(gm_b16_8, ((uint8_t *)&t124));
        uint8_t t126 = gm_l3;
        void * t127 = (void *)((uint8_t *)&F.l12);
        void * t128 = (void *)((uint8_t *)&F.l13);
        gm_ttsm01t0d8srg t129 = gm_f_run_native_ije4jng((void *)NULL, cx, NULL, t121, &(*(gm_tcn5ttqhv05g6 *)((uint8_t *)&F.l2)), t123, t125, t126, t127, t128);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l22 = t129;
    }
    {
        {
            uint8_t t130 = (uint8_t)((((uint8_t *)&gm_l22))[0] != 0);
            if (t130) {
                goto gm_lb16;
            }
            gm_l21 = ((uint8_t)0);
            goto gm_lb15;
        }
gm_lb16:;
        {
            gm_l21 = ((uint8_t)1);
            goto gm_lb15;
        }
gm_lb17:;
        gm_trap("no match arm matched the scrutinee", gm_srcrq1gl2e1lsg5, 89, 8);
    }
gm_lb15:;
    uint8_t t131 = gm_l21;
    if (t131) {
        void * t132 = (void *)((uint8_t *)&F.l12);
        uint8_t t133 = gm_f_finish_2t60fs7((void *)NULL, cx, NULL, t132);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l14 = t133;
    }
    {
        void * t134 = gm_l0;
        void * t135 = (void *)((uint8_t *)&F.l13);
        void * t136 = (void *)(((uint8_t *)&F.l13) + 32);
        gm_b16_8 t137 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 72));
        gm_b16_8 t138 = GM_LD(gm_b16_8, ((uint8_t *)&t137));
        uint8_t t139 = gm_f_coverage_report_hrhbf22((void *)NULL, cx, NULL, t134, t135, t136, t138, &(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l2) + 152)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t140 = (uint8_t)!t139;
        if (t140) {
            F.ret = ((uint8_t)1ULL);
            goto gm_fret;
        }
    }
    uint8_t t141 = gm_l14;
    F.ret = t141;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_run_all_fj42v4f(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1, void * a2, void * a3, gm_b16_8 a4, gm_b16_8 a5, uint64_t a6, gm_th2bqc58ss467 a7, const void * a8, void * a9) {
    struct gm_fr_run_all_fj42v4f F;
    gm_b16_8 gm_l0; /* cmd */
    gm_t9qkh0rsooc8d gm_l1; /* selector */
    void * gm_l2; /* fallback */
    void * gm_l3; /* ip */
    gm_b16_8 gm_l4; /* cases */
    gm_b16_8 gm_l5; /* chosen */
    uint64_t gm_l6; /* jobs */
    gm_th2bqc58ss467 gm_l7; /* timeout */
    gm_th2lqbo77pvsc gm_l8; /* cover_dir */
    void * gm_l9; /* report */
    uint64_t gm_l10; /* n */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_tm98ap4112hee gm_l17; /* t */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* j */
    gm_t6did6ki92he6 gm_l21; /* forked */
    void * gm_l22; /* x */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    uint64_t gm_l25; /* j */
    uint64_t gm_l26; /* k */
    void * gm_l27; /* x */
    gm_tcva7d32r7rjb gm_l28; /* f */
    gm_b16_8 gm_l30; /* <tmp> */
    uint64_t gm_l31; /* <tmp> */
    gm_tm98ap4112hee gm_l32; /* t */
    GM_STACK_CHECK(&F, 1699u);
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
    memcpy(&gm_l8, a8, sizeof gm_l8);
    gm_l9 = a9;
    uint8_t gm_l33;
    gm_tftb1odc739t5 gm_l34;
    gm_t62ebbi2i5rkd gm_l35;
    gm_tftb1odc739t5 gm_l36;
    uint8_t gm_l37;
    {
        uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
        gm_l10 = t44;
        gm_trqfvmjtt4sij t45;
        gm_f_list_7vplt15((void *)&t45, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l11 = t45;
        gm_b16_8 t46 = gm_l0;
        gm_t9qkh0rsooc8d t47 = gm_l1;
        void * t48 = gm_l2;
        gm_th2bqc58ss467 t49 = gm_l7;
        gm_th2lqbo77pvsc t50 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l8));
        {
            {
                uint8_t t51 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l3)) != 0);
                if (t51) {
                    goto gm_lb2;
                }
                gm_l33 = ((uint8_t)0);
                goto gm_lb1;
            }
gm_lb2:;
            {
                gm_l33 = ((uint8_t)1);
                goto gm_lb1;
            }
gm_lb3:;
            gm_trap("no match arm matched the scrutinee", gm_srcrq1gl2e1lsg5, 298, 117);
        }
gm_lb1:;
        uint8_t t52 = gm_l33;
        gm_tf5srh4mp4b3i t53;
        memset(&t53, 0, sizeof t53);
        gm_t6did6ki92he6 t54;
        memset(&t54, 0, sizeof t54);
        uint64_t t55 = gm_l10;
        gm_trqfvmjtt4sij t56;
        gm_f_list_86kk1bu((void *)&t56, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t57 = GM_CONV(gm_t88rcucg1b7uk, gm_f_mutex_t7if00c());
        gm_t88rcucg1b7uk t58 = GM_CONV(gm_t88rcucg1b7uk, gm_f_condvar_rp5e3qr());
        gm_tsli1i0j8bcrr t59;
        GM_ST(uint8_t, (uint8_t *)&t59 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t59 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t59 + 20, 0);
        GM_ST(uint16_t, (uint8_t *)&t59 + 74, 0);
        GM_ST(uint32_t, (uint8_t *)&t59 + 76, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t59 + 0), t46);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t59 + 16), t47);
        GM_ST(void *, ((uint8_t *)&t59 + 24), t48);
        GM_ST(gm_th2bqc58ss467, ((uint8_t *)&t59 + 32), t49);
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t59 + 48), t50);
        GM_ST(uint8_t, ((uint8_t *)&t59 + 72), t52);
        GM_ST(uint8_t, ((uint8_t *)&t59 + 73), ((uint8_t)0));
        GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t59 + 80), t53);
        GM_ST(gm_t6did6ki92he6, ((uint8_t *)&t59 + 96), t54);
        GM_ST(uint64_t, ((uint8_t *)&t59 + 120), t55);
        GM_ST(uint64_t, ((uint8_t *)&t59 + 128), ((uint64_t)0ULL));
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t59 + 136), t56);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t59 + 176), t57);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t59 + 180), t58);
        F.l12 = t59;
        {
            {
                uint64_t t60 = gm_l6;
                uint8_t t61 = (t60 > ((uint64_t)1ULL));
                if (!t61) {
                    goto gm_lb5;
                }
                uint64_t t62 = gm_l10;
                uint8_t t63 = (t62 > ((uint64_t)1ULL));
                if (!t63) {
                    goto gm_lb5;
                }
                gm_l14 = ((uint64_t)0ULL);
                uint64_t t64 = gm_l10;
                gm_l13 = t64;
                {
                    for (;;) {
                        uint64_t t65 = gm_l14;
                        uint64_t t66 = gm_l13;
                        uint8_t t67 = (t65 < t66);
                        if (!t67) {
                            goto gm_lb6;
                        }
                        {
                            {
                                {
                                    void * t68 = (void *)(((uint8_t *)&F.l12) + 136);
                                    gm_t33ki4t2edp58 t69;
                                    memset(&t69, 0, sizeof t69);
                                    gm_tftb1odc739t5 t70 = gm_f_push_snfdb36(t68, &t69);
                                    gm_l34 = t70;
                                    uint8_t t71 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                                    if (!t71) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 1, gm_srcrq1gl2e1lsg5, 300, 25);
                                    goto gm_lb9;
                                }
gm_lb10:;
                                gm_trap("no match arm matched the scrutinee", gm_srcrq1gl2e1lsg5, 300, 9);
                            }
gm_lb9:;
                        }
gm_lb8:;
                        uint64_t t72 = gm_l14;
                        uint64_t r73; bool o74 = __builtin_add_overflow(t72, ((uint64_t)1ULL), &r73);
                        uint64_t t75 = r73;
                        gm_l14 = t75;
                    }
                }
gm_lb6:;
                gm_l16 = ((uint64_t)0ULL);
                uint64_t t76 = gm_l6;
                uint64_t t77 = gm_l10;
                uint8_t t78 = (t76 < t77);
                if (t78) {
                    uint64_t t79 = gm_l6;
                    gm_l15 = t79;
                } else {
                    uint64_t t80 = gm_l10;
                    gm_l15 = t80;
                }
                {
                    for (;;) {
                        uint64_t t81 = gm_l16;
                        uint64_t t82 = gm_l15;
                        uint8_t t83 = (t81 < t82);
                        if (!t83) {
                            goto gm_lb11;
                        }
                        {
                            {
                                {
                                    void * t84 = (void *)((uint8_t *)&F.l12);
                                    gm_t62ebbi2i5rkd t85 = gm_f_spawn_534m93a(((void *)gm_f_worker_s2irjfh), t84);
                                    gm_l35 = t85;
                                    uint8_t t86 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
                                    if (t86) {
                                        gm_tm98ap4112hee t87 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&gm_l35) + 8));
                                        gm_l17 = t87;
                                    } else {
                                        goto gm_lb11;
                                    }
                                    void * t88 = (void *)((uint8_t *)&F.l11);
                                    gm_tm98ap4112hee t89 = gm_l17;
                                    gm_tftb1odc739t5 t90 = gm_f_push_6ct7n6b(t88, GM_CONV(gm_csfq64m1m1rt6m, t89));
                                    gm_l36 = t90;
                                    uint8_t t91 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
                                    if (!t91) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 1, gm_srcrq1gl2e1lsg5, 303, 13);
                                    goto gm_lb14;
                                }
gm_lb15:;
                                gm_trap("no match arm matched the scrutinee", gm_srcrq1gl2e1lsg5, 301, 9);
                            }
gm_lb14:;
                        }
gm_lb13:;
                        uint64_t t92 = gm_l16;
                        uint64_t r93; bool o94 = __builtin_add_overflow(t92, ((uint64_t)1ULL), &r93);
                        uint64_t t95 = r93;
                        gm_l16 = t95;
                    }
                }
gm_lb11:;
                goto gm_lb4;
            }
gm_lb5:;
        }
gm_lb4:;
    }
    {
        uint64_t t96 = GM_LD(uint64_t, (((uint8_t *)&F.l11) + 8));
        uint8_t t97 = ((t96 == ((uint64_t)0ULL)));
        if (t97) {
            gm_l19 = ((uint64_t)0ULL);
            uint64_t t98 = gm_l10;
            gm_l18 = t98;
            {
                for (;;) {
                    uint64_t t99 = gm_l19;
                    uint64_t t100 = gm_l18;
                    uint8_t t101 = (t99 < t100);
                    if (!t101) {
                        goto gm_lb16;
                    }
                    {
                        {
                            {
                                uint64_t t102 = gm_l19;
                                gm_l20 = t102;
                                {
                                    memset(&gm_l21, 0, sizeof gm_l21);
                                }
                                {
                                    {
                                        uint8_t t103 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l3)) != 0);
                                        if (!t103) {
                                            goto gm_lb22;
                                        }
                                        void * t104 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                        gm_l22 = t104;
                                        void * t105 = gm_l2;
                                        uint64_t t106 = gm_l20;
                                        uint8_t t107 = gm_f_natively_smocg9v(t105, t106);
                                        uint8_t t108 = (uint8_t)!t107;
                                        if (t108) {
                                            void * t109 = gm_l22;
                                            uint64_t t110 = gm_l20;
                                            gm_tcva7d32r7rjb t111 = gm_f_fork_test_2u613h7((void *)NULL, cx, NULL, t109, t110);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            GM_ST(uint8_t, (uint8_t *)&gm_l21 + 17, 0);
                                            GM_ST(uint16_t, (uint8_t *)&gm_l21 + 18, 0);
                                            GM_ST(uint32_t, (uint8_t *)&gm_l21 + 20, 0);
                                            GM_ST(gm_tcva7d32r7rjb, ((uint8_t *)&gm_l21), t111);
                                            ((uint8_t *)&gm_l21)[16] = 1;
                                        }
                                        goto gm_lb21;
                                    }
gm_lb22:;
                                }
gm_lb21:;
                                {
                                    void * t112 = gm_l9;
                                    void * t113 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                    uint64_t t114 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                    void * t115 = GM_LD(void *, ((uint8_t *)&gm_l5));
                                    uint64_t t116 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                    uint64_t t117 = gm_l20;
                                    uint64_t t118 = GM_LD(uint64_t, (((uint8_t *)t115) + (uintptr_t)t117 * 8));
                                    if ((uintptr_t)t118 >= (uintptr_t)(t114)) gm_trap_index((uintptr_t)t118, t114, gm_srcrq1gl2e1lsg5, 312, 27);
                                    gm_b16_8 t119 = gm_l0;
                                    gm_t9qkh0rsooc8d t120 = gm_l1;
                                    void * t121 = gm_l2;
                                    uint64_t t122 = gm_l20;
                                    gm_th2bqc58ss467 t123 = gm_l7;
                                    gm_tbah3mk9eqfec t124 = GM_LD(gm_tbah3mk9eqfec, (((uint8_t *)t113) + (uintptr_t)t118 * 48));
                                    gm_tp3p9lj444cp7 t125;
                                    gm_f_launch_h699ulv((void *)&t125, (void *)NULL, cx, NULL, t119, t120, t121, &(*(gm_t6did6ki92he6 *)((uint8_t *)&gm_l21)), t122, t123, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l8)));
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_b0_1 t126 = gm_f_record_abrqivi((void *)NULL, cx, NULL, t112, &t124, &t125);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                goto gm_lb19;
                            }
gm_lb20:;
                            gm_trap("no match arm matched the scrutinee", gm_srcrq1gl2e1lsg5, 307, 9);
                        }
gm_lb19:;
                    }
gm_lb18:;
                    uint64_t t127 = gm_l19;
                    uint64_t r128; bool o129 = __builtin_add_overflow(t127, ((uint64_t)1ULL), &r128);
                    uint64_t t130 = r128;
                    gm_l19 = t130;
                }
            }
gm_lb16:;
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
        gm_l24 = ((uint64_t)0ULL);
        uint64_t t131 = gm_l10;
        gm_l23 = t131;
    }
    {
        {
            for (;;) {
                uint64_t t132 = gm_l24;
                uint64_t t133 = gm_l23;
                uint8_t t134 = (t132 < t133);
                if (!t134) {
                    goto gm_lb23;
                }
                {
                    {
                        {
                            uint64_t t135 = gm_l24;
                            gm_l25 = t135;
                            void * t136 = (void *)(((uint8_t *)&F.l12) + 176);
                            gm_f_lock_u5lks01(t136);
                            {
                                for (;;) {
                                    void * t137 = GM_LD(void *, (((uint8_t *)&F.l12) + 136));
                                    uint64_t t138 = GM_LD(uint64_t, ((((uint8_t *)&F.l12) + 136) + 8));
                                    uint64_t t139 = gm_l25;
                                    if ((uintptr_t)t139 >= (uintptr_t)(t138)) gm_trap_index((uintptr_t)t139, t138, gm_srcrq1gl2e1lsg5, 318, 15);
                                    {
                                        {
                                            uint8_t t140 = (uint8_t)(((((uint8_t *)t137) + (uintptr_t)t139 * 56))[48] != 0);
                                            if (t140) {
                                                goto gm_lb32;
                                            }
                                            gm_l37 = ((uint8_t)1);
                                            goto gm_lb31;
                                        }
gm_lb32:;
                                        {
                                            gm_l37 = ((uint8_t)0);
                                            goto gm_lb31;
                                        }
gm_lb33:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcrq1gl2e1lsg5, 318, 15);
                                    }
gm_lb31:;
                                    uint8_t t141 = gm_l37;
                                    if (!t141) {
                                        goto gm_lb28;
                                    }
                                    {
                                        {
                                            {
                                                uint8_t t142 = (uint8_t)(((((uint8_t *)&F.l12) + 80))[8] != 0);
                                                if (!t142) {
                                                    goto gm_lb35;
                                                }
                                                uint64_t t143 = GM_LD(uint64_t, (((uint8_t *)&F.l12) + 80));
                                                gm_l26 = t143;
                                                {
                                                    {
                                                        uint8_t t144 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l3)) != 0);
                                                        if (!t144) {
                                                            goto gm_lb37;
                                                        }
                                                        void * t145 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                                        gm_l27 = t145;
                                                        gm_tf5srh4mp4b3i t146;
                                                        memset(&t146, 0, sizeof t146);
                                                        GM_ST(gm_tf5srh4mp4b3i, (((uint8_t *)&F.l12) + 80), t146);
                                                        void * t147 = (void *)(((uint8_t *)&F.l12) + 176);
                                                        gm_f_unlock_9f1vcl6(t147);
                                                        void * t148 = gm_l27;
                                                        uint64_t t149 = gm_l26;
                                                        gm_tcva7d32r7rjb t150 = gm_f_fork_test_2u613h7((void *)NULL, cx, NULL, t148, t149);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_l28 = t150;
                                                        void * t151 = (void *)(((uint8_t *)&F.l12) + 176);
                                                        gm_f_lock_u5lks01(t151);
                                                        gm_tcva7d32r7rjb t152 = gm_l28;
                                                        gm_t6did6ki92he6 t153;
                                                        GM_ST(uint8_t, (uint8_t *)&t153 + 17, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t153 + 18, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&t153 + 20, 0);
                                                        GM_ST(gm_tcva7d32r7rjb, ((uint8_t *)&t153), t152);
                                                        ((uint8_t *)&t153)[16] = 1;
                                                        GM_ST(gm_t6did6ki92he6, (((uint8_t *)&F.l12) + 96), t153);
                                                        void * t154 = (void *)(((uint8_t *)&F.l12) + 180);
                                                        gm_f_broadcast_bfak2oh(t154);
                                                        goto gm_lb30;
                                                    }
gm_lb37:;
                                                }
gm_lb36:;
                                                goto gm_lb34;
                                            }
gm_lb35:;
                                        }
gm_lb34:;
                                        void * t155 = (void *)(((uint8_t *)&F.l12) + 180);
                                        void * t156 = (void *)(((uint8_t *)&F.l12) + 176);
                                        gm_f_wait_k1r8nlm(t155, t156);
                                    }
gm_lb30:;
                                }
                            }
gm_lb28:;
                            void * t157 = GM_LD(void *, (((uint8_t *)&F.l12) + 136));
                            uint64_t t158 = GM_LD(uint64_t, ((((uint8_t *)&F.l12) + 136) + 8));
                            uint64_t t159 = gm_l25;
                            if ((uintptr_t)t159 >= (uintptr_t)(t158)) gm_trap_index((uintptr_t)t159, t158, gm_srcrq1gl2e1lsg5, 333, 14);
                            uint8_t t160 = (uint8_t)(((((uint8_t *)t157) + (uintptr_t)t159 * 56))[48] != 0);
                            if (t160) {
                                memmove(((uint8_t *)&F.l29), (((uint8_t *)t157) + (uintptr_t)t159 * 56), 48);
                            } else {
                                gm_trap("reached `@unreachable()`", gm_srcrq1gl2e1lsg5, 333, 41);
                            }
                            void * t161 = (void *)(((uint8_t *)&F.l12) + 176);
                            gm_f_unlock_9f1vcl6(t161);
                            {
                                void * t162 = gm_l9;
                                void * t163 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                uint64_t t164 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                void * t165 = GM_LD(void *, ((uint8_t *)&gm_l5));
                                uint64_t t166 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                uint64_t t167 = gm_l25;
                                uint64_t t168 = GM_LD(uint64_t, (((uint8_t *)t165) + (uintptr_t)t167 * 8));
                                if ((uintptr_t)t168 >= (uintptr_t)(t164)) gm_trap_index((uintptr_t)t168, t164, gm_srcrq1gl2e1lsg5, 335, 23);
                                gm_b0_1 t169 = gm_f_record_abrqivi((void *)NULL, cx, NULL, t162, &(*(gm_tbah3mk9eqfec *)(((uint8_t *)t163) + (uintptr_t)t168 * 48)), &(*(gm_tp3p9lj444cp7 *)((uint8_t *)&F.l29)));
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            goto gm_lb26;
                        }
gm_lb27:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrq1gl2e1lsg5, 316, 5);
                    }
gm_lb26:;
                }
gm_lb25:;
                uint64_t t170 = gm_l24;
                uint64_t r171; bool o172 = __builtin_add_overflow(t170, ((uint64_t)1ULL), &r171);
                uint64_t t173 = r171;
                gm_l24 = t173;
            }
        }
gm_lb23:;
    }
    {
        gm_b16_8 t174 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
        gm_l30 = t174;
        gm_l31 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t175 = gm_l31;
                uint64_t t176 = GM_LD(uint64_t, (((uint8_t *)&gm_l30) + 8));
                uint8_t t177 = (t175 < t176);
                if (!t177) {
                    goto gm_lb38;
                }
                {
                    void * t178 = GM_LD(void *, ((uint8_t *)&gm_l30));
                    uint64_t t179 = GM_LD(uint64_t, (((uint8_t *)&gm_l30) + 8));
                    uint64_t t180 = gm_l31;
                    {
                        {
                            gm_tm98ap4112hee t181 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)t178) + (uintptr_t)t180 * 8));
                            gm_l32 = t181;
                            gm_tm98ap4112hee t182 = gm_l32;
                            gm_f_join_r8j2dp1(GM_CONV(gm_csfq64m1m1rt6m, t182));
                            goto gm_lb41;
                        }
gm_lb42:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrq1gl2e1lsg5, 337, 5);
                    }
gm_lb41:;
                }
gm_lb40:;
                uint64_t t183 = gm_l31;
                uint64_t r184; bool o185 = __builtin_add_overflow(t183, ((uint64_t)1ULL), &r184);
                uint64_t t186 = r184;
                gm_l31 = t186;
            }
        }
gm_lb38:;
        F.ret = GM_ZERO(gm_b0_1);
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
__attribute__((aligned(4))) gm_ttsm01t0d8srg gm_f_run_interp_tests_hdiaknr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, gm_b16_8 a3, gm_b16_8 a4, uint8_t a5, void * a6) {
    struct gm_fr_run_interp_tests_hdiaknr F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* input */
    gm_b16_8 gm_l3; /* cases */
    gm_b16_8 gm_l4; /* chosen */
    uint8_t gm_l5; /* bench */
    void * gm_l6; /* report */
    gm_b16_8 gm_l7; /* args */
    uint64_t gm_l9; /* jobs */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* i */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint32_t gm_l20; /* e */
    gm_b16_8 gm_l22; /* dir */
    gm_b16_8 gm_l23; /* native_cmd */
    gm_t6j6smj7qoctu gm_l24; /* fallback */
    GM_STACK_CHECK(&F, 3738u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    gm_tftb1odc739t5 gm_l25;
    gm_tftb1odc739t5 gm_l26;
    gm_tftb1odc739t5 gm_l27;
    gm_tftb1odc739t5 gm_l28;
    gm_tftb1odc739t5 gm_l29;
    gm_tftb1odc739t5 gm_l30;
    gm_tftb1odc739t5 gm_l31;
    gm_tftb1odc739t5 gm_l32;
    gm_b16_8 gm_l33;
    gm_t5uv522f0p612 gm_l34;
    gm_th2lqbo77pvsc gm_l35;
    {
        gm_node *pn14 = gm_find(cx, 621690230);
        if (!pn14) gm_trap("no handler installed for effect `Env`", gm_srcrq1gl2e1lsg5, 171, 13);
        gm_b16_8 t15;
        if (pn14->kind == GM_NATIVE) {
            size_t argn; const void *argp = gm_args(&argn);
            gm_b16_8 t16;
            GM_ST(void *, (uint8_t *)&t16, (void *)(argp));
            GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(argn));
            t15 = t16;
        } else {
            t15 = ((gm_b16_8 (*)(void *, gm_node *, gm_node *))pn14->clauses[0])(pn14->env, pn14->hctx, pn14);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t15;
        {
            gm_trqfvmjtt4sij t17;
            gm_f_list_epvducg((void *)&t17, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l8 = t17;
        }
        void * t18 = (void *)((uint8_t *)&F.l8);
        gm_b16_8 t19 = gm_f_executable_lr5dqch((void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t20 = gm_f_push_3ef746b(t18, t19);
        gm_l25 = t20;
        uint8_t t21 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
        if (!t21) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_srcrq1gl2e1lsg5, 173, 5);
        void * t22 = (void *)((uint8_t *)&F.l8);
        void * t23 = GM_LD(void *, ((uint8_t *)&gm_l7));
        uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t24)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t24, gm_srcrq1gl2e1lsg5, 174, 14);
        gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)t23) + (uintptr_t)((uint64_t)1ULL) * 16));
        gm_tftb1odc739t5 t26 = gm_f_push_3ef746b(t22, t25);
        gm_l26 = t26;
        uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
        if (!t27) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_srcrq1gl2e1lsg5, 174, 5);
        void * t28 = (void *)((uint8_t *)&F.l8);
        gm_b16_8 t29;
        GM_ST(void *, (uint8_t *)&t29, (void *)(gm_sdv4asmg9feh5));
        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(11));
        gm_tftb1odc739t5 t30 = gm_f_push_3ef746b(t28, t29);
        gm_l27 = t30;
        uint8_t t31 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
        if (!t31) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 1, gm_srcrq1gl2e1lsg5, 175, 5);
        void * t32 = (void *)((uint8_t *)&F.l8);
        void * t33 = GM_LD(void *, ((uint8_t *)&gm_l7));
        uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
        uint64_t t35 = (uintptr_t)((uint64_t)2ULL);
        uint64_t t36 = (uintptr_t)t34;
        if (t35 > t36 || t36 > (uintptr_t)t34) gm_trap_slice(t35, t36, t34, gm_srcrq1gl2e1lsg5, 176, 16);
        uint64_t t37 = ((uintptr_t)t34) - (uintptr_t)((uint64_t)2ULL);
        gm_b16_8 t38;
        GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t33 + (uintptr_t)((uint64_t)2ULL) * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37));
        gm_tftb1odc739t5 t39 = gm_f_extend_vogsd72(t32, t38);
        gm_l28 = t39;
        uint8_t t40 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
        if (!t40) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 1, gm_srcrq1gl2e1lsg5, 176, 5);
        uint8_t t41 = gm_l5;
        if (t41) {
            gm_l9 = ((uint64_t)1ULL);
        } else {
            uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)a2) + 120));
            gm_l9 = t42;
        }
        {
            gm_trqfvmjtt4sij t43;
            gm_f_list_go2qg0b((void *)&t43, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l10 = t43;
        }
        gm_b16_8 t44 = gm_l4;
        gm_l11 = t44;
        gm_l12 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t45 = gm_l12;
                uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                uint8_t t47 = (t45 < t46);
                if (!t47) {
                    goto gm_lb2;
                }
                {
                    void * t48 = GM_LD(void *, ((uint8_t *)&gm_l11));
                    uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                    uint64_t t50 = gm_l12;
                    {
                        {
                            uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)t48) + (uintptr_t)t50 * 8));
                            gm_l13 = t51;
                            void * t52 = (void *)((uint8_t *)&F.l10);
                            void * t53 = gm_l0;
                            void * t54 = GM_LD(void *, ((uint8_t *)&gm_l3));
                            uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                            uint64_t t56 = gm_l13;
                            if ((uintptr_t)t56 >= (uintptr_t)(t55)) gm_trap_index((uintptr_t)t56, t55, gm_srcrq1gl2e1lsg5, 179, 46);
                            uint8_t t57 = gm_l5;
                            uint32_t t58 = gm_f_entry_of_si4ihfm((void *)NULL, cx, NULL, t53, &(*(gm_tbah3mk9eqfec *)(((uint8_t *)t54) + (uintptr_t)t56 * 48)), t57);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tftb1odc739t5 t59 = gm_f_push_im9d4ro(t52, t58);
                            gm_l29 = t59;
                            uint8_t t60 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                            if (!t60) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_srcrq1gl2e1lsg5, 179, 23);
                            goto gm_lb5;
                        }
gm_lb6:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrq1gl2e1lsg5, 179, 5);
                    }
gm_lb5:;
                }
gm_lb4:;
                uint64_t t61 = gm_l12;
                uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                uint64_t t64 = r62;
                gm_l12 = t64;
            }
        }
gm_lb2:;
        void * t65 = gm_l0;
        gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
        gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&t66));
        uint8_t t68 = gm_f_prepare_codegen_4hnlio1((void *)NULL, cx, NULL, t65, t67);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t69 = (uint8_t)!t68;
        if (t69) {
            gm_ttsm01t0d8srg t70;
            memset(&t70, 0, sizeof t70);
            F.ret = t70;
            goto gm_fret;
        }
        {
            void * t71 = gm_l0;
            gm_b16_8 t72 = gm_l1;
            gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
            gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
            gm_tcn5ttqhv05g6 t75 = GM_LD(gm_tcn5ttqhv05g6, ((uint8_t *)a2));
            gm_tp6ao262lsfmo t76;
            GM_ST(void *, ((uint8_t *)&t76 + 0), t71);
            GM_ST(gm_b16_8, ((uint8_t *)&t76 + 8), t72);
            GM_ST(gm_tcn5ttqhv05g6, ((uint8_t *)&t76 + 24), t75);
            GM_ST(gm_b16_8, ((uint8_t *)&t76 + 648), t74);
            F.l14 = t76;
        }
        {
            gm_trqfvmjtt4sij t77;
            gm_f_list_go2qg0b((void *)&t77, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l15 = t77;
        }
        {
            gm_trqfvmjtt4sij t78;
            gm_f_list_fsi5en1((void *)&t78, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l16 = t78;
        }
        uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)a2) + 80));
        if (t79) {
            void * t80 = gm_l0;
            gm_ta4jakbus8leg t81;
            gm_f_prog_61arbs1((void *)&t81, (void *)NULL, cx, NULL, t80);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l17 = t81;
            gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
            gm_l18 = t82;
            gm_l19 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t83 = gm_l19;
                    uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                    uint8_t t85 = (t83 < t84);
                    if (!t85) {
                        goto gm_lb7;
                    }
                    {
                        void * t86 = GM_LD(void *, ((uint8_t *)&gm_l18));
                        uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                        uint64_t t88 = gm_l19;
                        {
                            {
                                uint32_t t89 = GM_LD(uint32_t, (((uint8_t *)t86) + (uintptr_t)t88 * 4));
                                gm_l20 = t89;
                                void * t90 = (void *)((uint8_t *)&F.l17);
                                uint32_t t91 = gm_l20;
                                uint8_t t92 = gm_f_needs_native_kvptfrn((void *)NULL, cx, NULL, t90, t91);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                if (t92) {
                                    void * t93 = (void *)((uint8_t *)&F.l16);
                                    uint64_t t94 = GM_LD(uint64_t, (((uint8_t *)&F.l15) + 8));
                                    gm_tf5srh4mp4b3i t95;
                                    GM_ST(uint8_t, (uint8_t *)&t95 + 9, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t95 + 10, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t95 + 12, 0);
                                    GM_ST(uint64_t, ((uint8_t *)&t95), t94);
                                    ((uint8_t *)&t95)[8] = 1;
                                    gm_tftb1odc739t5 t96 = gm_f_push_s4q134v(t93, t95);
                                    gm_l30 = t96;
                                    uint8_t t97 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
                                    if (!t97) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 1, gm_srcrq1gl2e1lsg5, 189, 17);
                                    void * t98 = (void *)((uint8_t *)&F.l15);
                                    uint32_t t99 = gm_l20;
                                    gm_tftb1odc739t5 t100 = gm_f_push_im9d4ro(t98, t99);
                                    gm_l31 = t100;
                                    uint8_t t101 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                                    if (!t101) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 1, gm_srcrq1gl2e1lsg5, 190, 17);
                                } else {
                                    void * t102 = (void *)((uint8_t *)&F.l16);
                                    gm_tf5srh4mp4b3i t103;
                                    memset(&t103, 0, sizeof t103);
                                    gm_tftb1odc739t5 t104 = gm_f_push_s4q134v(t102, t103);
                                    gm_l32 = t104;
                                    uint8_t t105 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                                    if (!t105) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 1, gm_srcrq1gl2e1lsg5, 192, 17);
                                }
                                goto gm_lb10;
                            }
gm_lb11:;
                            gm_trap("no match arm matched the scrutinee", gm_srcrq1gl2e1lsg5, 187, 9);
                        }
gm_lb10:;
                    }
gm_lb9:;
                    uint64_t t106 = gm_l19;
                    uint64_t r107; bool o108 = __builtin_add_overflow(t106, ((uint64_t)1ULL), &r107);
                    uint64_t t109 = r107;
                    gm_l19 = t109;
                }
            }
gm_lb7:;
        }
        uint64_t t110 = GM_LD(uint64_t, (((uint8_t *)&F.l15) + 8));
        uint8_t t111 = ((t110 == ((uint64_t)0ULL)));
        if (t111) {
            gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
            gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)&t112));
            void * t114 = (void *)((uint8_t *)&F.l14);
            gm_b16_8 t115 = gm_l3;
            gm_b16_8 t116 = gm_l4;
            uint64_t t117 = gm_l9;
            gm_th2bqc58ss467 t118 = GM_LD(gm_th2bqc58ss467, (((uint8_t *)a2) + 128));
            gm_th2lqbo77pvsc t119;
            memset(&t119, 0, sizeof t119);
            void * t120 = gm_l6;
            gm_t9qkh0rsooc8d t121;
            GM_ST(uint8_t, ((uint8_t *)&t121 + 0), ((uint8_t)1ULL));
            gm_b0_1 t122 = gm_f_run_all_fj42v4f((void *)NULL, cx, NULL, t113, t121, ((void *)0), t114, t115, t116, t117, t118, &t119, t120);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_ttsm01t0d8srg t123;
            GM_ST(gm_b0_1, ((uint8_t *)&t123), GM_ZERO(gm_b0_1));
            ((uint8_t *)&t123)[0] = 1;
            F.ret = t123;
            goto gm_fret;
        }
        {
            gm_b16_8 t124 = gm_f_temp_dir_i6apmsd((void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t125 = gm_l5;
            if (t125) {
                gm_b16_8 t126;
                GM_ST(void *, (uint8_t *)&t126, (void *)(gm_shg1pn1r3sjgi));
                GM_ST(uintptr_t, (uint8_t *)&t126 + 8, (uintptr_t)(12));
                gm_l33 = t126;
            } else {
                gm_b16_8 t127;
                GM_ST(void *, (uint8_t *)&t127, (void *)(gm_s79el5a3kii78));
                GM_ST(uintptr_t, (uint8_t *)&t127 + 8, (uintptr_t)(11));
                gm_l33 = t127;
            }
            gm_b16_8 t128 = gm_l33;
            void * t129 = (void *)((uint8_t *)&F.l21);
            uint64_t t130 = ((uintptr_t)((uint64_t)4096ULL)) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t131;
            GM_ST(void *, (uint8_t *)&t131, (void *)(((uint8_t *)t129 + (uintptr_t)((uint64_t)0ULL) * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t131 + 8, (uintptr_t)(t130));
            gm_f_make_temp_dir_vi8f88d((void *)&gm_l34, (void *)NULL, cx, NULL, t124, t128, t131);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t132 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
        if (t132) {
            gm_b16_8 t133 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l34) + 8));
            gm_l22 = t133;
        } else {
            gm_b16_8 t134 = gm_f_temp_dir_i6apmsd((void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pf454qfrpt6r(cx, gm_svaibtfuu0gf2, 47, gm_srcrq1gl2e1lsg5, 202, 9)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t134) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t134)), GM_LD(uintptr_t, ((uint8_t *)&t134) + 8), gm_srcrq1gl2e1lsg5, 202, 9))) {
                goto gm_lad;
            }
            if (gm_pf454qfrpt6r(cx, gm_s06m3met1mb9v, 2, gm_srcrq1gl2e1lsg5, 202, 9)) {
                goto gm_lad;
            }
            gm_ttsm01t0d8srg t135;
            memset(&t135, 0, sizeof t135);
            F.ret = t135;
            goto gm_fret;
        }
        {
            void * t136 = gm_l0;
            gm_b16_8 t137 = GM_LD(gm_b16_8, ((uint8_t *)&F.l15));
            gm_b16_8 t138 = GM_LD(gm_b16_8, ((uint8_t *)&t137));
            gm_b16_8 t139 = gm_l22;
            gm_tcn5ttqhv05g6 t140 = GM_LD(gm_tcn5ttqhv05g6, ((uint8_t *)a2));
            gm_f_build_tests_mmafq15((void *)&gm_l35, (void *)NULL, cx, NULL, t136, &t140, t138, t139);
            if (gm_unw.kind) {
                goto gm_ld13_1;
            }
        }
        uint8_t t141 = (uint8_t)((((uint8_t *)&gm_l35))[16] != 0);
        if (t141) {
            gm_b16_8 t142 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l35));
            gm_l23 = t142;
        } else {
            gm_ttsm01t0d8srg t143;
            memset(&t143, 0, sizeof t143);
            F.ret = t143;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
            goto gm_ld13_1;
        }
        {
            gm_b16_8 t144 = gm_l23;
            gm_b16_8 t145 = GM_LD(gm_b16_8, ((uint8_t *)&F.l16));
            gm_b16_8 t146 = GM_LD(gm_b16_8, ((uint8_t *)&t145));
            gm_t6j6smj7qoctu t147;
            GM_ST(gm_b16_8, ((uint8_t *)&t147 + 0), t144);
            GM_ST(gm_b16_8, ((uint8_t *)&t147 + 16), t146);
            gm_l24 = t147;
        }
        {
            gm_b16_8 t148 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
            gm_b16_8 t149 = GM_LD(gm_b16_8, ((uint8_t *)&t148));
            void * t150 = (void *)((uint8_t *)&gm_l24);
            void * t151 = (void *)((uint8_t *)&F.l14);
            gm_b16_8 t152 = gm_l3;
            gm_b16_8 t153 = gm_l4;
            uint64_t t154 = gm_l9;
            gm_th2bqc58ss467 t155 = GM_LD(gm_th2bqc58ss467, (((uint8_t *)a2) + 128));
            gm_th2lqbo77pvsc t156;
            memset(&t156, 0, sizeof t156);
            void * t157 = gm_l6;
            gm_t9qkh0rsooc8d t158;
            GM_ST(uint8_t, ((uint8_t *)&t158 + 0), ((uint8_t)1ULL));
            gm_b0_1 t159 = gm_f_run_all_fj42v4f((void *)NULL, cx, NULL, t149, t158, t150, t151, t152, t153, t154, t155, &t156, t157);
            if (gm_unw.kind) {
                goto gm_ld13_1;
            }
        }
        gm_ttsm01t0d8srg t160;
        GM_ST(gm_b0_1, ((uint8_t *)&t160), GM_ZERO(gm_b0_1));
        ((uint8_t *)&t160)[0] = 1;
        F.ret = t160;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
        goto gm_ld13_1;
    }
    {
        gm_b16_8 t161 = gm_l22;
        gm_ttln0d0hd70en t162 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t161);
    }
    goto gm_ls163;
gm_ld13_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        gm_b16_8 t164 = gm_l22;
        gm_ttln0d0hd70en t165 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t164);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls163:;
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
__attribute__((aligned(4))) gm_ttsm01t0d8srg gm_f_run_native_ije4jng(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2, gm_b16_8 a3, uint8_t a4, void * a5, void * a6) {
    struct gm_fr_run_native_ije4jng F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l2; /* cases */
    gm_b16_8 gm_l3; /* chosen */
    uint8_t gm_l4; /* bench */
    void * gm_l5; /* report */
    void * gm_l6; /* cover */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    gm_b16_8 gm_l12; /* dir */
    void * gm_l13; /* c */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint32_t gm_l17; /* f */
    gm_b16_8 gm_l18; /* cmd */
    uint64_t gm_l19; /* jobs */
    gm_th2lqbo77pvsc gm_l20; /* cover_dir */
    void * gm_l21; /* c */
    uint64_t gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* j */
    gm_b16_8 gm_l25; /* text */
    GM_STACK_CHECK(&F, 1989u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    gm_tftb1odc739t5 gm_l26;
    gm_b16_8 gm_l27;
    gm_t5uv522f0p612 gm_l28;
    gm_tftb1odc739t5 gm_l29;
    gm_tftb1odc739t5 gm_l30;
    gm_th2lqbo77pvsc gm_l31;
    uint8_t gm_l32;
    gm_tgvh15qshf643 gm_l33;
    {
        {
            gm_trqfvmjtt4sij t26;
            gm_f_list_go2qg0b((void *)&t26, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l7 = t26;
        }
        gm_b16_8 t27 = gm_l3;
        gm_l8 = t27;
        gm_l9 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t28 = gm_l9;
                uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                uint8_t t30 = (t28 < t29);
                if (!t30) {
                    goto gm_lb2;
                }
                {
                    void * t31 = GM_LD(void *, ((uint8_t *)&gm_l8));
                    uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                    uint64_t t33 = gm_l9;
                    {
                        {
                            uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)t31) + (uintptr_t)t33 * 8));
                            gm_l10 = t34;
                            void * t35 = (void *)((uint8_t *)&F.l7);
                            void * t36 = gm_l0;
                            void * t37 = GM_LD(void *, ((uint8_t *)&gm_l2));
                            uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                            uint64_t t39 = gm_l10;
                            if ((uintptr_t)t39 >= (uintptr_t)(t38)) gm_trap_index((uintptr_t)t39, t38, gm_srcrq1gl2e1lsg5, 108, 48);
                            uint8_t t40 = gm_l4;
                            uint32_t t41 = gm_f_entry_of_si4ihfm((void *)NULL, cx, NULL, t36, &(*(gm_tbah3mk9eqfec *)(((uint8_t *)t37) + (uintptr_t)t39 * 48)), t40);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tftb1odc739t5 t42 = gm_f_push_im9d4ro(t35, t41);
                            gm_l26 = t42;
                            uint8_t t43 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                            if (!t43) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_srcrq1gl2e1lsg5, 108, 23);
                            goto gm_lb5;
                        }
gm_lb6:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrq1gl2e1lsg5, 108, 5);
                    }
gm_lb5:;
                }
gm_lb4:;
                uint64_t t44 = gm_l9;
                uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                uint64_t t47 = r45;
                gm_l9 = t47;
            }
        }
gm_lb2:;
        {
            gm_b16_8 t48 = gm_f_temp_dir_i6apmsd((void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t49 = gm_l4;
            if (t49) {
                gm_b16_8 t50;
                GM_ST(void *, (uint8_t *)&t50, (void *)(gm_shg1pn1r3sjgi));
                GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(12));
                gm_l27 = t50;
            } else {
                gm_b16_8 t51;
                GM_ST(void *, (uint8_t *)&t51, (void *)(gm_s79el5a3kii78));
                GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(11));
                gm_l27 = t51;
            }
            gm_b16_8 t52 = gm_l27;
            void * t53 = (void *)((uint8_t *)&F.l11);
            uint64_t t54 = ((uintptr_t)((uint64_t)4096ULL)) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t55;
            GM_ST(void *, (uint8_t *)&t55, (void *)(((uint8_t *)t53 + (uintptr_t)((uint64_t)0ULL) * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(t54));
            gm_f_make_temp_dir_vi8f88d((void *)&gm_l28, (void *)NULL, cx, NULL, t48, t52, t55);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t56 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
        if (t56) {
            gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l28) + 8));
            gm_l12 = t57;
        } else {
            gm_b16_8 t58 = gm_f_temp_dir_i6apmsd((void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pf454qfrpt6r(cx, gm_svaibtfuu0gf2, 47, gm_srcrq1gl2e1lsg5, 111, 9)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t58) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t58)), GM_LD(uintptr_t, ((uint8_t *)&t58) + 8), gm_srcrq1gl2e1lsg5, 111, 9))) {
                goto gm_lad;
            }
            if (gm_pf454qfrpt6r(cx, gm_s06m3met1mb9v, 2, gm_srcrq1gl2e1lsg5, 111, 9)) {
                goto gm_lad;
            }
            gm_ttsm01t0d8srg t59;
            memset(&t59, 0, sizeof t59);
            F.ret = t59;
            goto gm_fret;
        }
        {
            {
                uint8_t t60 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l6)) != 0);
                if (!t60) {
                    goto gm_lb8;
                }
                void * t61 = GM_LD(void *, ((uint8_t *)&gm_l6));
                gm_l13 = t61;
                {
                    gm_trqfvmjtt4sij t62;
                    gm_f_list_go2qg0b((void *)&t62, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_ld25_1;
                    }
                    F.l14 = t62;
                }
                void * t63 = (void *)((uint8_t *)&F.l14);
                gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
                gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
                gm_tftb1odc739t5 t66 = gm_f_extend_7hs67uj(t63, t65);
                gm_l29 = t66;
                uint8_t t67 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                if (!t67) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_srcrq1gl2e1lsg5, 118, 9);
                void * t68 = gm_l0;
                gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)t68) + 920));
                gm_l15 = t69;
                gm_l16 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t70 = gm_l16;
                        uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                        uint8_t t72 = (t70 < t71);
                        if (!t72) {
                            goto gm_lb9;
                        }
                        {
                            void * t73 = GM_LD(void *, ((uint8_t *)&gm_l15));
                            uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                            uint64_t t75 = gm_l16;
                            {
                                {
                                    uint32_t t76 = GM_LD(uint32_t, (((uint8_t *)t73) + (uintptr_t)t75 * 4));
                                    gm_l17 = t76;
                                    uint32_t t77 = gm_l17;
                                    gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
                                    uint8_t t79 = 0;
                                    for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t78) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t78))) + i * 4, ((uint8_t *)&t77))) { t79 = 1; break; }
                                    uint8_t t80 = (uint8_t)(!t79);
                                    if (t80) {
                                        void * t81 = (void *)((uint8_t *)&F.l14);
                                        uint32_t t82 = gm_l17;
                                        gm_tftb1odc739t5 t83 = gm_f_push_im9d4ro(t81, t82);
                                        gm_l30 = t83;
                                        uint8_t t84 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
                                        if (!t84) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 1, gm_srcrq1gl2e1lsg5, 119, 65);
                                    }
                                    goto gm_lb12;
                                }
gm_lb13:;
                                gm_trap("no match arm matched the scrutinee", gm_srcrq1gl2e1lsg5, 119, 9);
                            }
gm_lb12:;
                        }
gm_lb11:;
                        uint64_t t85 = gm_l16;
                        uint64_t r86; bool o87 = __builtin_add_overflow(t85, ((uint64_t)1ULL), &r86);
                        uint64_t t88 = r86;
                        gm_l16 = t88;
                    }
                }
gm_lb9:;
                void * t89 = gm_l0;
                gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
                gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&t90));
                uint8_t t92 = gm_f_prepare_codegen_4hnlio1((void *)NULL, cx, NULL, t89, t91);
                if (gm_unw.kind) {
                    goto gm_ld25_1;
                }
                uint8_t t93 = (uint8_t)!t92;
                if (t93) {
                    gm_ttsm01t0d8srg t94;
                    memset(&t94, 0, sizeof t94);
                    F.ret = t94;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
                    goto gm_ld25_1;
                }
                {
                    void * t95 = gm_l13;
                    void * t96 = gm_l0;
                    void * t97 = gm_l13;
                    void * t98 = (void *)((uint8_t *)t97);
                    gm_tin97tt4ud0s1 t99;
                    gm_f_instrument_mk8clvl((void *)&t99, (void *)NULL, cx, NULL, t96, t98);
                    if (gm_unw.kind) {
                        goto gm_ld25_1;
                    }
                    GM_ST(gm_tin97tt4ud0s1, (((uint8_t *)t95) + 32), t99);
                }
                goto gm_lb7;
            }
gm_lb8:;
        }
gm_lb7:;
        {
            void * t100 = gm_l0;
            gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
            gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&t101));
            gm_b16_8 t103 = gm_l12;
            gm_tcn5ttqhv05g6 t104 = GM_LD(gm_tcn5ttqhv05g6, ((uint8_t *)a1));
            gm_f_build_tests_mmafq15((void *)&gm_l31, (void *)NULL, cx, NULL, t100, &t104, t102, t103);
            if (gm_unw.kind) {
                goto gm_ld25_1;
            }
        }
        uint8_t t105 = (uint8_t)((((uint8_t *)&gm_l31))[16] != 0);
        if (t105) {
            gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l31));
            gm_l18 = t106;
        } else {
            gm_ttsm01t0d8srg t107;
            memset(&t107, 0, sizeof t107);
            F.ret = t107;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
            goto gm_ld25_1;
        }
        uint8_t t108 = gm_l4;
        if (t108) {
            gm_l19 = ((uint64_t)1ULL);
        } else {
            uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)a1) + 120));
            gm_l19 = t109;
        }
        {
            {
                uint8_t t110 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l6)) != 0);
                if (t110) {
                    goto gm_lb15;
                }
                gm_l32 = ((uint8_t)0);
                goto gm_lb14;
            }
gm_lb15:;
            {
                gm_l32 = ((uint8_t)1);
                goto gm_lb14;
            }
gm_lb16:;
            gm_trap("no match arm matched the scrutinee", gm_srcrq1gl2e1lsg5, 127, 29);
        }
gm_lb14:;
        uint8_t t111 = gm_l32;
        if (t111) {
            gm_b16_8 t112 = gm_l12;
            GM_ST(uint8_t, (uint8_t *)&gm_l20 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&gm_l20 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&gm_l20 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&gm_l20), t112);
            ((uint8_t *)&gm_l20)[16] = 1;
        } else {
            memset(&gm_l20, 0, sizeof gm_l20);
        }
        {
            gm_b16_8 t113 = gm_l18;
            gm_b16_8 t114 = gm_l2;
            gm_b16_8 t115 = gm_l3;
            uint64_t t116 = gm_l19;
            gm_th2bqc58ss467 t117 = GM_LD(gm_th2bqc58ss467, (((uint8_t *)a1) + 128));
            void * t118 = gm_l5;
            gm_t9qkh0rsooc8d t119;
            GM_ST(uint8_t, ((uint8_t *)&t119 + 0), ((uint8_t)0ULL));
            gm_b0_1 t120 = gm_f_run_all_fj42v4f((void *)NULL, cx, NULL, t113, t119, ((void *)0), ((void *)0), t114, t115, t116, t117, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l20)), t118);
            if (gm_unw.kind) {
                goto gm_ld25_1;
            }
        }
        {
            {
                uint8_t t121 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l6)) != 0);
                if (!t121) {
                    goto gm_lb18;
                }
                void * t122 = GM_LD(void *, ((uint8_t *)&gm_l6));
                gm_l21 = t122;
                gm_l23 = ((uint64_t)0ULL);
                uint64_t t123 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                gm_l22 = t123;
                {
                    for (;;) {
                        uint64_t t124 = gm_l23;
                        uint64_t t125 = gm_l22;
                        uint8_t t126 = (t124 < t125);
                        if (!t126) {
                            goto gm_lb19;
                        }
                        {
                            {
                                {
                                    uint64_t t127 = gm_l23;
                                    gm_l24 = t127;
                                    gm_b16_8 t128 = gm_l12;
                                    uint64_t t129 = gm_l24;
                                    gm_b16_8 t130 = gm_f_cover_file_bhhubi9((void *)NULL, cx, NULL, t128, t129);
                                    if (gm_unw.kind) {
                                        goto gm_ld25_1;
                                    }
                                    gm_f_read_all_r9rh9qd((void *)&gm_l33, (void *)NULL, cx, NULL, t130);
                                    if (gm_unw.kind) {
                                        goto gm_ld25_1;
                                    }
                                    uint8_t t131 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                                    if (t131) {
                                        gm_b16_8 t132 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l33) + 8));
                                        gm_l25 = t132;
                                    } else {
                                        goto gm_lb21;
                                    }
                                    void * t133 = gm_l21;
                                    void * t134 = (void *)(((uint8_t *)t133) + 72);
                                    gm_b16_8 t135 = gm_l25;
                                    gm_b16_8 t136 = GM_LD(gm_b16_8, ((uint8_t *)&t135));
                                    gm_b0_1 t137 = gm_f_add_counts_hprtdd9((void *)NULL, cx, NULL, t134, t136);
                                    if (gm_unw.kind) {
                                        goto gm_ld25_1;
                                    }
                                    goto gm_lb22;
                                }
gm_lb23:;
                                gm_trap("no match arm matched the scrutinee", gm_srcrq1gl2e1lsg5, 130, 9);
                            }
gm_lb22:;
                        }
gm_lb21:;
                        uint64_t t138 = gm_l23;
                        uint64_t r139; bool o140 = __builtin_add_overflow(t138, ((uint64_t)1ULL), &r139);
                        uint64_t t141 = r139;
                        gm_l23 = t141;
                    }
                }
gm_lb19:;
                goto gm_lb17;
            }
gm_lb18:;
        }
gm_lb17:;
        gm_ttsm01t0d8srg t142;
        GM_ST(gm_b0_1, ((uint8_t *)&t142), GM_ZERO(gm_b0_1));
        ((uint8_t *)&t142)[0] = 1;
        F.ret = t142;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
        goto gm_ld25_1;
    }
    {
        gm_b16_8 t143 = gm_l12;
        gm_ttln0d0hd70en t144 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t143);
    }
    goto gm_ls145;
gm_ld25_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        gm_b16_8 t146 = gm_l12;
        gm_ttln0d0hd70en t147 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t146);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls145:;
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
extern __attribute__((aligned(4))) uint8_t gm_f_selected_8kh2cc4(const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_selected_8kh2cc4_c(gm_tbah3mk9eqfec a0, gm_th2lqbo77pvsc a1) { return gm_f_selected_8kh2cc4(&a0, &a1); }
__attribute__((aligned(4))) void gm_f_worker_s2irjfh(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_worker_s2irjfh F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* j */
    gm_t6did6ki92he6 gm_l2; /* forked */
    GM_STACK_CHECK(&F, 565u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_t3mf58gv9kf3n t8 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_heap_allocator_gihki37());
        gm_t3mf58gv9kf3n t9 = t8;
        gm_node wn10 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t9, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b0_1 t11 = gm_f_system_io_mcjcl9m();
        gm_b0_1 t12 = GM_ZERO(gm_b0_1);
        gm_node wn13 = {.eff = 1018230276, .kind = GM_DECL, .clauses = gm_hd3g3ujqvah4, .env = (void *)&t12, .hctx = (&wn10), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn10)};
        gm_node wn14 = {.eff = 1579066750, .kind = GM_DECL, .clauses = gm_hdf4dspmusp5, .env = (void *)&t12, .hctx = (&wn10), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn13)};
        gm_node wn15 = {.eff = 1087599154, .kind = GM_DECL, .clauses = gm_hdvslis4o40u, .env = (void *)&t12, .hctx = (&wn10), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn14)};
        gm_node wn16 = {.eff = 2126215853, .kind = GM_DECL, .clauses = gm_hdu8v6n5i2qd, .env = (void *)&t12, .hctx = (&wn10), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn15)};
        gm_node wn17 = {.eff = 163658498, .kind = GM_DECL, .clauses = gm_hdffkeesmui4, .env = (void *)&t12, .hctx = (&wn10), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn16)};
        gm_node wn18 = {.eff = 2119230, .kind = GM_DECL, .clauses = gm_hdaqemfrshlp, .env = (void *)&t12, .hctx = (&wn10), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn17)};
        gm_b0_1 t19 = gm_f_system_output_eg72lom();
        gm_b0_1 t20 = GM_ZERO(gm_b0_1);
        gm_node wn21 = {.eff = 515083575, .kind = GM_DECL, .clauses = gm_hdu50j4i95ke, .env = (void *)&t20, .hctx = (&wn18), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn18)};
        gm_node wn22 = {.eff = 2063121243, .kind = GM_DECL, .clauses = gm_hddfeetqn95b, .env = (void *)&t20, .hctx = (&wn18), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn21)};
        {
            for (;;) {
                if (!((uint8_t)1)) {
                    goto gm_lb2;
                }
                {
                    void * t25 = gm_l0;
                    void * t26 = (void *)(((uint8_t *)t25) + 176);
                    gm_f_lock_u5lks01(t26);
                    void * t27 = gm_l0;
                    uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)t27) + 128));
                    gm_l1 = t28;
                    void * t29 = gm_l0;
                    void * t30 = gm_l0;
                    uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)t30) + 128));
                    uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                    if (o33) gm_trap("integer overflow in `+`", gm_srcrq1gl2e1lsg5, 346, 13);
                    uint64_t t34 = r32;
                    GM_ST(uint64_t, (((uint8_t *)t29) + 128), t34);
                    void * t35 = gm_l0;
                    void * t36 = (void *)(((uint8_t *)t35) + 176);
                    gm_f_unlock_9f1vcl6(t36);
                    uint64_t t37 = gm_l1;
                    void * t38 = gm_l0;
                    uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)t38) + 120));
                    uint8_t t40 = (t37 >= t39);
                    if (t40) {
                        goto gm_lb2;
                    }
                    {
                        memset(&gm_l2, 0, sizeof gm_l2);
                    }
                    {
                        {
                            void * t41 = gm_l0;
                            uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)t41) + 72));
                            if (!t42) {
                                goto gm_lb6;
                            }
                            void * t43 = gm_l0;
                            void * t44 = GM_LD(void *, (((uint8_t *)t43) + 24));
                            uint64_t t45 = gm_l1;
                            uint8_t t46 = gm_f_natively_smocg9v(t44, t45);
                            uint8_t t47 = (uint8_t)!t46;
                            if (!t47) {
                                goto gm_lb6;
                            }
                            void * t48 = gm_l0;
                            uint64_t t49 = gm_l1;
                            gm_tcva7d32r7rjb t50 = GM_CONV(gm_tcva7d32r7rjb, gm_f_ask_fork_fsiu08u(t48, t49));
                            GM_ST(uint8_t, (uint8_t *)&gm_l2 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&gm_l2 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&gm_l2 + 20, 0);
                            GM_ST(gm_tcva7d32r7rjb, ((uint8_t *)&gm_l2), t50);
                            ((uint8_t *)&gm_l2)[16] = 1;
                            goto gm_lb5;
                        }
gm_lb6:;
                    }
gm_lb5:;
                    {
                        void * t51 = gm_l0;
                        gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)t51));
                        void * t53 = gm_l0;
                        gm_t9qkh0rsooc8d t54 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t53) + 16));
                        void * t55 = gm_l0;
                        void * t56 = GM_LD(void *, (((uint8_t *)t55) + 24));
                        uint64_t t57 = gm_l1;
                        void * t58 = gm_l0;
                        gm_th2bqc58ss467 t59 = GM_LD(gm_th2bqc58ss467, (((uint8_t *)t58) + 32));
                        void * t60 = gm_l0;
                        gm_t6did6ki92he6 t61 = GM_LD(gm_t6did6ki92he6, ((uint8_t *)&gm_l2));
                        gm_th2lqbo77pvsc t62 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)t60) + 48));
                        gm_f_launch_h699ulv((void *)&F.l3, (void *)NULL, (&wn22), NULL, t52, t54, t56, &t61, t57, t59, &t62);
                        if (gm_unw.kind) {
                            goto gm_ll24;
                        }
                    }
                    void * t63 = gm_l0;
                    void * t64 = (void *)(((uint8_t *)t63) + 176);
                    gm_f_lock_u5lks01(t64);
                    {
                        void * t65 = gm_l0;
                        void * t66 = GM_LD(void *, (((uint8_t *)t65) + 136));
                        uint64_t t67 = GM_LD(uint64_t, ((((uint8_t *)t65) + 136) + 8));
                        uint64_t t68 = gm_l1;
                        if ((uintptr_t)t68 >= (uintptr_t)(t67)) gm_trap_index((uintptr_t)t68, t67, gm_srcrq1gl2e1lsg5, 353, 13);
                        gm_tp3p9lj444cp7 t69 = GM_LD(gm_tp3p9lj444cp7, ((uint8_t *)&F.l3));
                        gm_t33ki4t2edp58 t70;
                        GM_ST(uint8_t, (uint8_t *)&t70 + 49, 0);
                        GM_ST(uint16_t, (uint8_t *)&t70 + 50, 0);
                        GM_ST(uint32_t, (uint8_t *)&t70 + 52, 0);
                        GM_ST(gm_tp3p9lj444cp7, ((uint8_t *)&t70), t69);
                        ((uint8_t *)&t70)[48] = 1;
                        GM_ST(gm_t33ki4t2edp58, (((uint8_t *)t66) + (uintptr_t)t68 * 56), t70);
                    }
                    void * t71 = gm_l0;
                    void * t72 = (void *)(((uint8_t *)t71) + 180);
                    gm_f_broadcast_bfak2oh(t72);
                    void * t73 = gm_l0;
                    void * t74 = (void *)(((uint8_t *)t73) + 176);
                    gm_f_unlock_9f1vcl6(t74);
                }
gm_lb4:;
            }
        }
gm_lb2:;
    }
    goto gm_ls75;
gm_ll24:;
    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we23; }
    goto gm_lad;
gm_ls75:;
gm_lb1:;
we23:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return;
gm_fprop:
    return;
}

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
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_b16_8 f3; } gm_t26d2uc07m5o2;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { gm_tcc1rq4n1avb2 f0; uint8_t f1; uint8_t pe[3]; } gm_t736djc66po0l;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_b16_8 f3; uint8_t f4; uint8_t pe[7]; } gm_tg9l3k9j9m9b9;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; gm_tl874t2atnlh3 f3; } gm_tis7ttivmj935;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t f3; uint8_t pe[3]; } gm_tkrkmsnb6m1b7;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_b16_8 f3; } gm_tllos8achgacq;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t f2; uint8_t f3; uint8_t pe[2]; } gm_tppdoi1slt2pr;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t p3[3]; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tsoie1ppgdvcg;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_tl874t2atnlh3 f2; gm_b16_8 f3; } gm_tt384v6n8ojib;
typedef struct { gm_tl874t2atnlh3 f0; gm_tl874t2atnlh3 f1; uint8_t f2; uint8_t pe[3]; } gm_tt7dbdq4n1egc;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t26d2uc07m5o2) == 48 && _Alignof(gm_t26d2uc07m5o2) == 8 && offsetof(gm_t26d2uc07m5o2, f0) == 0 && offsetof(gm_t26d2uc07m5o2, f1) == 16 && offsetof(gm_t26d2uc07m5o2, f2) == 28 && offsetof(gm_t26d2uc07m5o2, f3) == 32, "layout of `(name: string, span: Span, vis: Visibility, what: string)`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
typedef struct { uint64_t f0; gm_tsoie1ppgdvcg f1; } gm_t5im2v50m3hk6;
typedef struct { gm_ts6oqppjh0ba3 f0; gm_ts6oqppjh0ba3 f1; gm_ts6oqppjh0ba3 f2; gm_ts6oqppjh0ba3 f3; } gm_t6htphat2tk14;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t736djc66po0l) == 12 && _Alignof(gm_t736djc66po0l) == 4 && offsetof(gm_t736djc66po0l, f0) == 0 && offsetof(gm_t736djc66po0l, f1) == 8, "layout of `?(u32, u32)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { gm_ts6oqppjh0ba3 f0; gm_ts6oqppjh0ba3 f1; } gm_t9ldr1q5hn9fq;
typedef struct { void * f0; uint8_t f1; uint8_t p2[7]; gm_t4c7ipjj9s3fp f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_t4c7ipjj9s3fp f5; gm_trqfvmjtt4sij f6; } gm_tad8216qc9dro;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tg9l3k9j9m9b9) == 72 && _Alignof(gm_tg9l3k9j9m9b9) == 8 && offsetof(gm_tg9l3k9j9m9b9, f0) == 0 && offsetof(gm_tg9l3k9j9m9b9, f1) == 16 && offsetof(gm_tg9l3k9j9m9b9, f2) == 32 && offsetof(gm_tg9l3k9j9m9b9, f3) == 48 && offsetof(gm_tg9l3k9j9m9b9, f4) == 64, "layout of `Dangling`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tis7ttivmj935) == 56 && _Alignof(gm_tis7ttivmj935) == 8 && offsetof(gm_tis7ttivmj935, f0) == 0 && offsetof(gm_tis7ttivmj935, f1) == 16 && offsetof(gm_tis7ttivmj935, f2) == 32 && offsetof(gm_tis7ttivmj935, f3) == 44, "layout of `(span: Span, name: string, local: Span, origin: Span)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tppdoi1slt2pr v0; gm_t08olq97rscn3 v1; gm_tt384v6n8ojib v2; gm_tt7dbdq4n1egc v3; gm_toceffb697p0u v4; gm_tl874t2atnlh3 v5; gm_t26d2uc07m5o2 v6; gm_tllos8achgacq v7; gm_tkrkmsnb6m1b7 v8; gm_tis7ttivmj935 v9; gm_tis7ttivmj935 v10; } f1; } gm_tj5f8vajf35o7;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tkacitepg5fk0;
_Static_assert(sizeof(gm_tkrkmsnb6m1b7) == 48 && _Alignof(gm_tkrkmsnb6m1b7) == 8 && offsetof(gm_tkrkmsnb6m1b7, f0) == 0 && offsetof(gm_tkrkmsnb6m1b7, f1) == 16 && offsetof(gm_tkrkmsnb6m1b7, f2) == 32 && offsetof(gm_tkrkmsnb6m1b7, f3) == 44, "layout of `(span: Span, name: string, catch_all: Span, is_result: bool)`");
_Static_assert(sizeof(gm_tllos8achgacq) == 64 && _Alignof(gm_tllos8achgacq) == 8 && offsetof(gm_tllos8achgacq, f0) == 0 && offsetof(gm_tllos8achgacq, f1) == 16 && offsetof(gm_tllos8achgacq, f2) == 32 && offsetof(gm_tllos8achgacq, f3) == 48, "layout of `(span: Span, name: string, local: Span, why: string)`");
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tppdoi1slt2pr) == 32 && _Alignof(gm_tppdoi1slt2pr) == 8 && offsetof(gm_tppdoi1slt2pr, f0) == 0 && offsetof(gm_tppdoi1slt2pr, f1) == 16 && offsetof(gm_tppdoi1slt2pr, f2) == 28 && offsetof(gm_tppdoi1slt2pr, f3) == 29, "layout of `(name: string, span: Span, param: bool, assigned: bool)`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tsoie1ppgdvcg) == 40 && _Alignof(gm_tsoie1ppgdvcg) == 8 && offsetof(gm_tsoie1ppgdvcg, f0) == 0 && offsetof(gm_tsoie1ppgdvcg, f1) == 16 && offsetof(gm_tsoie1ppgdvcg, f2) == 20 && offsetof(gm_tsoie1ppgdvcg, f3) == 24, "layout of `LocalDecl`");
_Static_assert(sizeof(gm_tt384v6n8ojib) == 56 && _Alignof(gm_tt384v6n8ojib) == 8 && offsetof(gm_tt384v6n8ojib, f0) == 0 && offsetof(gm_tt384v6n8ojib, f1) == 16 && offsetof(gm_tt384v6n8ojib, f2) == 28 && offsetof(gm_tt384v6n8ojib, f3) == 40, "layout of `(name: string, span: Span, item: Span, what: string)`");
_Static_assert(sizeof(gm_tt7dbdq4n1egc) == 28 && _Alignof(gm_tt7dbdq4n1egc) == 4 && offsetof(gm_tt7dbdq4n1egc, f0) == 0 && offsetof(gm_tt7dbdq4n1egc, f1) == 12 && offsetof(gm_tt7dbdq4n1egc, f2) == 24, "layout of `(span: Span, operand: Span, same: bool)`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t5im2v50m3hk6) == 48 && _Alignof(gm_t5im2v50m3hk6) == 8 && offsetof(gm_t5im2v50m3hk6, f0) == 0 && offsetof(gm_t5im2v50m3hk6, f1) == 8, "layout of `(i: usize, x: LocalDecl)`");
_Static_assert(sizeof(gm_t6htphat2tk14) == 8 && _Alignof(gm_t6htphat2tk14) == 1 && offsetof(gm_t6htphat2tk14, f0) == 0 && offsetof(gm_t6htphat2tk14, f1) == 2 && offsetof(gm_t6htphat2tk14, f2) == 4 && offsetof(gm_t6htphat2tk14, f3) == 6, "layout of `(BinOp, BinOp, BinOp, BinOp)`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t9ldr1q5hn9fq) == 4 && _Alignof(gm_t9ldr1q5hn9fq) == 1 && offsetof(gm_t9ldr1q5hn9fq, f0) == 0 && offsetof(gm_t9ldr1q5hn9fq, f1) == 2, "layout of `(BinOp, BinOp)`");
_Static_assert(sizeof(gm_tad8216qc9dro) == 248 && _Alignof(gm_tad8216qc9dro) == 8 && offsetof(gm_tad8216qc9dro, f0) == 0 && offsetof(gm_tad8216qc9dro, f1) == 8 && offsetof(gm_tad8216qc9dro, f2) == 16 && offsetof(gm_tad8216qc9dro, f3) == 72 && offsetof(gm_tad8216qc9dro, f4) == 112 && offsetof(gm_tad8216qc9dro, f5) == 152 && offsetof(gm_tad8216qc9dro, f6) == 208, "layout of `Checked`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tj5f8vajf35o7) == 72 && _Alignof(gm_tj5f8vajf35o7) == 8 && offsetof(gm_tj5f8vajf35o7, f0) == 0 && offsetof(gm_tj5f8vajf35o7, f1) == 8, "layout of `Finding`");
_Static_assert(sizeof(gm_tkacitepg5fk0) == 72 && _Alignof(gm_tkacitepg5fk0) == 8 && offsetof(gm_tkacitepg5fk0, f0) == 0 && offsetof(gm_tkacitepg5fk0, f1) == 32 && offsetof(gm_tkacitepg5fk0, f2) == 56, "layout of `Attribute`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t6qooom8qa2do v0; } f1; } gm_tagrkbqh054h7;
_Static_assert(sizeof(gm_tagrkbqh054h7) == 216 && _Alignof(gm_tagrkbqh054h7) == 8 && offsetof(gm_tagrkbqh054h7, f0) == 0 && offsetof(gm_tagrkbqh054h7, f1) == 8, "layout of `DeclState`");
struct gm_fr_analyze_np1m50h {
    gm_frame h;
    gm_tad8216qc9dro l4; /* c */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_assigned_53ipnmg {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_binds_busqeo0 {
    gm_frame h;
    void * l0; /* c */
    uint32_t l1; /* f */
    gm_b0_1 ret;
};
struct gm_fr_block_stmts_c2nnk6m {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_bool_lit_q44074e {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_contains_14ai471 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_empty_slice_behvl9q {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_gk4cjgd {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_gm6v230 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_otc7v7a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_r81t436 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_eq_60fjn21 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    uint8_t ret;
};
struct gm_fr_file_ok_4j31vl0 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_259eng6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_o40bhnh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_generic_of_1861hm9 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_get_ptr_1a4voen {
    gm_frame h;
    void * ret;
};
struct gm_fr_get_q3uemcr {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_get_rlnefnq {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_hash_map_t9h7o8p {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_tvpcibu {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_is_array_p7ehais {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_decl_used_oldf2ja {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_used_outside_tbk81ks {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_1aqn740 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7s4om10 {
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
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mutate_oi1s4vs {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_needless_pub_jqsnq13 {
    gm_frame h;
    gm_tkacitepg5fk0 l11; /* a */
    gm_tv2uf4leil5hq l12; /* b */
    gm_tagrkbqh054h7 l13; /* state */
    gm_t6qooom8qa2do l14; /* v */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_path_eq_l3qbb7v {
    gm_frame h;
    gm_tbci5ospk5nnn l20; /* x */
    gm_tbci5ospk5nnn l21; /* y */
    uint8_t ret;
};
struct gm_fr_push_0mk7udh {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_im9d4ro {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_rqlbv1e {
    gm_frame h;
    gm_tj5f8vajf35o7 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_s4q134v {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_vd1290p {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_2onqfca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3n5udep {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_hkmvuvc {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_v0p8ic9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_vm8t3ct {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_root_oq23mha {
    gm_frame h;
    gm_t736djc66po0l ret;
};
struct gm_fr_run_checks_c17ks6r {
    gm_frame h;
    gm_b16_8 l1; /* scoped */
    gm_trqfvmjtt4sij l4; /* fn_decl */
    gm_t6qooom8qa2do l10; /* v */
    gm_trqfvmjtt4sij l17; /* calls */
    gm_trqfvmjtt4sij l29; /* calls */
    gm_trqfvmjtt4sij l37; /* growth */
    gm_trqfvmjtt4sij l42; /* dangling */
    gm_tg9l3k9j9m9b9 l45; /* d */
    gm_tsoie1ppgdvcg l51; /* l */
    gm_tv2uf4leil5hq l65; /* b */
    gm_tkacitepg5fk0 l69; /* a */
    gm_tagrkbqh054h7 l70; /* state */
    gm_t6qooom8qa2do l73; /* v */
    gm_trqfvmjtt4sij l74; /* kept */
    gm_tj5f8vajf35o7 l77; /* f */
    gm_b0_1 ret;
};
struct gm_fr_same_value_7pehjnc {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_use_of_q9agaul {
    gm_frame h;
    void * ret;
};
struct gm_fr_visit_unu9ms6 {
    gm_frame h;
    void * l0; /* c */
    uint32_t l1; /* f */
    void * l3; /* calls */
    gm_tsth9qu1dv0g3 l9; /* callee */
    gm_t710lsrq1m8gk l23; /* x */
    gm_t710lsrq1m8gk l40; /* x */
    gm_b0_1 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq6groq37ram(const uint8_t *a, const uint8_t *b);
inline bool gm_eq7mri15hk78(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbqb4gb1d92_2(const uint8_t *a, const uint8_t *b);
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void gm_f_analyze_np1m50h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_assigned_53ipnmg(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_binds_busqeo0(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_block_stmts_c2nnk6m(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_bool_lit_q44074e(void * a0);
__attribute__((aligned(4))) void * gm_f_const_value_fr4coi4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_14ai471(void * a0, gm_tcc1rq4n1avb2 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_behvl9q(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_gk4cjgd(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_gm6v230(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_otc7v7a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_file_ok_4j31vl0(void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_c753csd(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_dlnutct(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_efqiqfc(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_iegifbl(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_259eng6(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_o40bhnh(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_for_each_subpat_arlrtrm(void * a0, gm_closure a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_frame_up_h0ddp1d(void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) uint32_t gm_f_generic_of_1861hm9(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void * gm_f_get_ptr_1a4voen(void * a0, gm_tcc1rq4n1avb2 a1);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_q3uemcr(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_rlnefnq(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) void gm_f_growth_summaries_dguakce(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_hash_map_t9h7o8p(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_tvpcibu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_is_array_p7ehais(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_decl_used_oldf2ja(void * a0, uint64_t a1);
__attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_used_outside_tbk81ks(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_1aqn740(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_fsi5en1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) int32_t gm_f_mag_cmp_s39veq6(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_mutate_oi1s4vs(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_mutated_copies_vc6lpss(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_needless_pub_jqsnq13(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_path_eq_l3qbb7v(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_possible_dangling_addres_d403vr0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_present_only_bindings_ug2i6dq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_rqlbv1e(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_s4q134v(void * a0, gm_tf5srh4mp4b3i a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_3g1tdpr(void * a0, gm_tcc1rq4n1avb2 a1, uint8_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_he2q4rq(void * a0, gm_tcc1rq4n1avb2 a1, const void * a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3n5udep(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hkmvuvc(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_v0p8ic9(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_vm8t3ct(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t736djc66po0l gm_f_root_oq23mha(void * a0, uint32_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_run_checks_c17ks6r(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_same_value_7pehjnc(void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_stale_element_pointers_jjgn0ll(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_state_a5bnsjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_to_c2nvemb(gm_cseahvg8v7p0vs a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) void * gm_f_use_of_q9agaul(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tcc1rq4n1avb2 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_visit_unu9ms6(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, void * a3);
extern uint8_t gm_c9b4lh7gft4[4] __attribute__((aligned(4)));
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern const uint8_t gm_s1f440st4mnkp[2];
extern const uint8_t gm_s1ft2nrvnhcbp[10];
extern const uint8_t gm_s2737i1iq57nf[5];
extern const uint8_t gm_sa0amdcmvej1l[14];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sg3kpa3h16no3[7];
extern const uint8_t gm_skr6p9lfc5ftp[11];
extern const uint8_t gm_sog4rksrmsqd2[5];
extern const uint8_t gm_spe8t3e79vnal[7];
extern const uint8_t gm_sptnm8c6c52dr[7];
extern const uint8_t gm_sq0dqiq83b924[9];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src6lm8esmv16p1[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srco9vbah234t6e[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_svklio0qebpog[9];
extern const uint8_t gm_svqdrhms9i231[7];
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
inline bool gm_eq6groq37ram(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint64_t, a) == GM_LD(uint64_t, b);
}
inline bool gm_eq7mri15hk78(const uint8_t *a, const uint8_t *b) {
    if (a[8] != b[8]) return false;
    return !a[8] || gm_eq6groq37ram(a, b);
}
inline bool gm_eqbqb4gb1d92_2(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)0) return gm_eqehhdtpeti1(a + 1, b + 1);
    if (ta == (uint8_t)1) return gm_eqehhdtpeti1(a + 1, b + 1);
    if (ta == (uint8_t)2) return gm_eqehhdtpeti1(a + 1, b + 1);
    if (ta == (uint8_t)3) return gm_eqehhdtpeti1(a + 1, b + 1);
    return true;
}
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    if (!gm_eq467d5cm1d5(a + 8, b + 8)) return false;
    return true;
}
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_bool_lit_q44074e(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bool_lit_q44074e F;
    void * gm_l0; /* x */
    uint8_t gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 29u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t4 = gm_l0;
            uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)4ULL));
            if (!t6) {
                goto gm_lb2;
            }
            uint8_t t7 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 8));
            gm_l1 = t7;
            uint8_t t8 = gm_l1;
            gm_tqs5p4n2tq19v t9;
            GM_ST(uint8_t, ((uint8_t *)&t9), t8);
            ((uint8_t *)&t9)[1] = 1;
            F.ret = t9;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_tqs5p4n2tq19v t10;
    memset(&t10, 0, sizeof t10);
    F.ret = t10;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_contains_14ai471(void * a0, gm_tcc1rq4n1avb2 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_14ai471 F;
    void * gm_l0; /* m */
    gm_tcc1rq4n1avb2 gm_l1; /* k */
    GM_STACK_CHECK(&F, 69u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    gm_tf5srh4mp4b3i gm_l3;
    void * t5 = gm_l0;
    void * t6 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t7 = gm_f_find_efqiqfc(t5, t6);
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
        gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 173, 12);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_behvl9q(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_behvl9q F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 72)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_gk4cjgd(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_gk4cjgd F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 64)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_gm6v230(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_gm6v230 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 16)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_s1lbe5s F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eq_60fjn21 F;
    GM_STACK_CHECK_SHALLOW(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t gm_l2;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)a0));
    uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)a1));
    uint8_t t4 = ((t2 == t3));
    if (t4) {
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        int32_t t9 = gm_f_mag_cmp_s39veq6(t6, t8);
        uint8_t t10 = ((t9 == ((int32_t)0LL)));
        gm_l2 = t10;
    } else {
        gm_l2 = ((uint8_t)0);
    }
    uint8_t t11 = gm_l2;
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_259eng6(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_259eng6 F;
    void * gm_l0; /* child */
    GM_STACK_CHECK(&F, 36u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_visit_unu9ms6 *)((&F.h)->up))->l0));
    uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_visit_unu9ms6 *)((&F.h)->up))->l1));
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_visit_unu9ms6 *)((&F.h)->up))->l3));
    gm_b0_1 t6 = gm_f_visit_unu9ms6((void *)NULL, cx, NULL, t2, t3, t4, t5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_o40bhnh(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_o40bhnh F;
    void * gm_l0; /* q */
    GM_STACK_CHECK(&F, 28u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_binds_busqeo0 *)((&F.h)->up))->l0));
    uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_binds_busqeo0 *)((&F.h)->up))->l1));
    void * t4 = gm_l0;
    gm_b0_1 t5 = gm_f_binds_busqeo0((void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) uint32_t gm_f_generic_of_1861hm9(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_generic_of_1861hm9 F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    gm_tiilmfd16v3fc gm_l2; /* inst */
    GM_STACK_CHECK_SHALLOW(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t4 = gm_l0;
            void * t5 = GM_LD(void *, (((uint8_t *)t4) + 344));
            uint64_t t6 = GM_LD(uint64_t, ((((uint8_t *)t4) + 344) + 8));
            uint32_t t7 = gm_l1;
            uint64_t t8 = (uint64_t)t7;
            if ((uintptr_t)t8 >= (uintptr_t)(t6)) gm_trap_index((uintptr_t)t8, t6, gm_srclv0dcbbfhves, 598, 16);
            uint8_t t9 = (uint8_t)((((((uint8_t *)t5) + (uintptr_t)t8 * 136) + 96))[24] != 0);
            if (!t9) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l2), ((((uint8_t *)t5) + (uintptr_t)t8 * 136) + 96), 24);
            uint32_t t10 = GM_LD(uint32_t, ((uint8_t *)&gm_l2));
            F.ret = t10;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    uint32_t t11 = gm_l1;
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_get_ptr_1a4voen(void * a0, gm_tcc1rq4n1avb2 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_ptr_1a4voen F;
    void * gm_l0; /* m */
    gm_tcc1rq4n1avb2 gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 137u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t4 = gm_f_find_c753csd(t2, t3);
    gm_l3 = t4;
    uint8_t t5 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
    if (t5) {
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
        gm_l2 = t6;
    } else {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
    uint64_t t10 = gm_l2;
    if ((uintptr_t)t10 >= (uintptr_t)(t9)) gm_trap_index((uintptr_t)t10, t9, gm_srcbsc23is03ttm, 147, 13);
    void * t11 = (void *)((((uint8_t *)t8) + (uintptr_t)t10 * 64) + 16);
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_q3uemcr(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_q3uemcr F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t4 = gm_f_find_iegifbl(t2, t3);
    gm_l3 = t4;
    uint8_t t5 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
    if (t5) {
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
        gm_l2 = t6;
    } else {
        gm_t9805qtonbsag t7;
        memset(&t7, 0, sizeof t7);
        F.ret = t7;
        goto gm_fret;
    }
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, ((uint8_t *)t8));
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t8) + 8));
    uint64_t t11 = gm_l2;
    if ((uintptr_t)t11 >= (uintptr_t)(t10)) gm_trap_index((uintptr_t)t11, t10, gm_srcbsc23is03ttm, 139, 12);
    uint32_t t12 = GM_LD(uint32_t, ((((uint8_t *)t9) + (uintptr_t)t11 * 12) + 8));
    gm_t9805qtonbsag t13;
    GM_ST(uint8_t, (uint8_t *)&t13 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t13 + 6, 0);
    GM_ST(uint32_t, ((uint8_t *)&t13), t12);
    ((uint8_t *)&t13)[4] = 1;
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_rlnefnq(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_rlnefnq F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_tf5srh4mp4b3i gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t4 = gm_f_find_dlnutct(t2, t3);
    gm_l3 = t4;
    uint8_t t5 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
    if (t5) {
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
        gm_l2 = t6;
    } else {
        gm_t9805qtonbsag t7;
        memset(&t7, 0, sizeof t7);
        F.ret = t7;
        goto gm_fret;
    }
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, ((uint8_t *)t8));
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t8) + 8));
    uint64_t t11 = gm_l2;
    if ((uintptr_t)t11 >= (uintptr_t)(t10)) gm_trap_index((uintptr_t)t11, t10, gm_srcbsc23is03ttm, 139, 12);
    uint32_t t12 = GM_LD(uint32_t, ((((uint8_t *)t9) + (uintptr_t)t11 * 20) + 16));
    gm_t9805qtonbsag t13;
    GM_ST(uint8_t, (uint8_t *)&t13 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t13 + 6, 0);
    GM_ST(uint32_t, ((uint8_t *)&t13), t12);
    ((uint8_t *)&t13)[4] = 1;
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_map_t9h7o8p(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_t9h7o8p F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_gm6v230();
    gm_node *pn3 = gm_find(cx, 587034433);
    if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
    gm_t3mf58gv9kf3n t4;
    t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 16), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 24), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 32), ((uint64_t)11400714819323198485ULL));
    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 40), t4);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_tvpcibu(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_tvpcibu F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_gk4cjgd();
    gm_node *pn3 = gm_find(cx, 587034433);
    if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
    gm_t3mf58gv9kf3n t4;
    t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 16), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 24), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 32), ((uint64_t)11400714819323198485ULL));
    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 40), t4);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_decl_used_oldf2ja(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_decl_used_oldf2ja F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    GM_STACK_CHECK_SHALLOW(&F, 78u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    uint64_t t2 = gm_l1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t3) + 2664) + 8));
    uint8_t t5 = (t2 < t4);
    if (t5) {
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 2664));
        uint64_t t8 = GM_LD(uint64_t, ((((uint8_t *)t6) + 2664) + 8));
        uint64_t t9 = gm_l1;
        if ((uintptr_t)t9 >= (uintptr_t)(t8)) gm_trap_index((uintptr_t)t9, t8, gm_src6lm8esmv16p1, 61, 91);
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)t7) + (uintptr_t)t9 * 1));
        gm_l2 = t10;
    } else {
        gm_l2 = ((uint8_t)0);
    }
    uint8_t t11 = gm_l2;
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_used_outside_tbk81ks(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_used_outside_tbk81ks F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    GM_STACK_CHECK_SHALLOW(&F, 78u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    uint64_t t2 = gm_l1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t3) + 2704) + 8));
    uint8_t t5 = (t2 < t4);
    if (t5) {
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 2704));
        uint64_t t8 = GM_LD(uint64_t, ((((uint8_t *)t6) + 2704) + 8));
        uint64_t t9 = gm_l1;
        if ((uintptr_t)t9 >= (uintptr_t)(t8)) gm_trap_index((uintptr_t)t9, t8, gm_src6lm8esmv16p1, 64, 97);
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)t7) + (uintptr_t)t9 * 1));
        gm_l2 = t10;
    } else {
        gm_l2 = ((uint8_t)0);
    }
    uint8_t t11 = gm_l2;
    F.ret = t11;
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
inline __attribute__((aligned(4))) void gm_f_list_1aqn740(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_1aqn740 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_behvl9q();
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
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7s4om10 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_r81t436();
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
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_s83jn2q F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_s1lbe5s();
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_mutate_oi1s4vs(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2) {
    struct gm_fr_mutate_oi1s4vs F;
    void * gm_l0; /* c */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* place */
    gm_tcc1rq4n1avb2 gm_l3; /* k */
    GM_STACK_CHECK(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t736djc66po0l gm_l4;
    {
        {
            void * t4 = gm_l0;
            uint32_t t5 = gm_l1;
            void * t6 = gm_l2;
            gm_t736djc66po0l t7 = gm_f_root_oq23mha(t4, t5, t6);
            gm_l4 = t7;
            uint8_t t8 = (uint8_t)((((uint8_t *)&gm_l4))[8] != 0);
            if (!t8) {
                goto gm_lb2;
            }
            gm_tcc1rq4n1avb2 t9 = GM_LD(gm_tcc1rq4n1avb2, ((uint8_t *)&gm_l4));
            gm_l3 = t9;
            void * t10 = gm_l0;
            gm_tcc1rq4n1avb2 t11 = gm_l3;
            void * t12 = gm_f_use_of_q9agaul((void *)NULL, cx, NULL, t10, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (((uint8_t *)t12) + 40), ((uint8_t)1));
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_rqlbv1e(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_rqlbv1e F;
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
        gm_b16_8 t9 = gm_f_empty_slice_behvl9q();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_3n5udep(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 72), ((uint8_t *)&F.l1), 72);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 72)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_vd1290p F;
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
        gm_b16_8 t9 = gm_f_empty_slice_s1lbe5s();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_hkmvuvc(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3n5udep(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_3n5udep F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)72ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hkmvuvc(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_hkmvuvc F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_starts_with_6vvt01e F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 116u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t4 = (t2 > t3);
    if (t4) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t5 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t8 = ((uintptr_t)t7) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t9;
    GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8));
    gm_b16_8 t10 = gm_l1;
    uint8_t t11 = (gm_eq0iqiubfm7e(((uint8_t *)&t9), ((uint8_t *)&t10)));
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_analyze_np1m50h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_b16_8 a2) {
    struct gm_fr_analyze_np1m50h F;
    void * gm_l0; /* s */
    uint8_t gm_l1; /* program */
    gm_b16_8 gm_l2; /* scoped */
    uint64_t gm_l3; /* cn */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    GM_STACK_CHECK(&F, 883u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t15 = gm_l3;
            void * t16 = gm_l0;
            uint64_t t17 = GM_LD(uint64_t, ((((uint8_t *)t16) + 2088) + 8));
            uint8_t t18 = (t15 < t17);
            if (!t18) {
                goto gm_lb1;
            }
            {
                void * t19 = gm_l0;
                void * t20 = GM_LD(void *, (((uint8_t *)t19) + 2088));
                uint64_t t21 = GM_LD(uint64_t, ((((uint8_t *)t19) + 2088) + 8));
                uint64_t t22 = gm_l3;
                if ((uintptr_t)t22 >= (uintptr_t)(t21)) gm_trap_index((uintptr_t)t22, t21, gm_srco9vbah234t6e, 77, 12);
                gm_t9qkh0rsooc8d t23 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t20) + (uintptr_t)t22 * 88) + 72));
                uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t23) + 0));
                uint8_t t25 = (uint8_t)((t24 == ((uint8_t)0ULL)));
                gm_t9qkh0rsooc8d t26;
                GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
                if (t25) {
                    void * t27 = gm_l0;
                    void * t28 = gm_l0;
                    void * t29 = GM_LD(void *, (((uint8_t *)t28) + 2088));
                    uint64_t t30 = GM_LD(uint64_t, ((((uint8_t *)t28) + 2088) + 8));
                    uint64_t t31 = gm_l3;
                    if ((uintptr_t)t31 >= (uintptr_t)(t30)) gm_trap_index((uintptr_t)t31, t30, gm_srco9vbah234t6e, 78, 38);
                    gm_tl874t2atnlh3 t32 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t29) + (uintptr_t)t31 * 88) + 16));
                    void * t33 = gm_l0;
                    void * t34 = GM_LD(void *, (((uint8_t *)t33) + 2088));
                    uint64_t t35 = GM_LD(uint64_t, ((((uint8_t *)t33) + 2088) + 8));
                    uint64_t t36 = gm_l3;
                    if ((uintptr_t)t36 >= (uintptr_t)(t35)) gm_trap_index((uintptr_t)t36, t35, gm_srco9vbah234t6e, 78, 63);
                    gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t34) + (uintptr_t)t36 * 88) + 28));
                    void * t38 = gm_f_const_value_fr4coi4((void *)NULL, cx, NULL, t27, t32, t37);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint64_t t39 = gm_l3;
                uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
                if (o41) gm_trap("integer overflow in `+`", gm_srco9vbah234t6e, 80, 9);
                uint64_t t42 = r40;
                gm_l3 = t42;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    {
        void * t43 = gm_l0;
        uint8_t t44 = gm_l1;
        gm_t4c7ipjj9s3fp t45;
        gm_f_hash_map_tvpcibu((void *)&t45, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t46;
        gm_f_list_s83jn2q((void *)&t46, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t47;
        gm_f_list_s83jn2q((void *)&t47, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4c7ipjj9s3fp t48;
        gm_f_hash_map_t9h7o8p((void *)&t48, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t49;
        gm_f_list_1aqn740((void *)&t49, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tad8216qc9dro t50;
        GM_ST(uint8_t, (uint8_t *)&t50 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t50 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t50 + 12, 0);
        GM_ST(void *, ((uint8_t *)&t50 + 0), t43);
        GM_ST(uint8_t, ((uint8_t *)&t50 + 8), t44);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t50 + 16), t45);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t50 + 72), t46);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t50 + 112), t47);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t50 + 152), t48);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t50 + 208), t49);
        F.l4 = t50;
    }
    gm_l6 = ((uint64_t)0ULL);
    void * t51 = gm_l0;
    void * t52 = GM_LD(void *, ((uint8_t *)t51));
    uint64_t t53 = GM_LD(uint64_t, ((((uint8_t *)t52) + 240) + 8));
    gm_l5 = t53;
    {
        for (;;) {
            uint64_t t54 = gm_l6;
            uint64_t t55 = gm_l5;
            uint8_t t56 = (t54 < t55);
            if (!t56) {
                goto gm_lb4;
            }
            {
                {
                    {
                        void * t57 = (void *)(((uint8_t *)&F.l4) + 72);
                        gm_tftb1odc739t5 t58 = gm_f_push_vd1290p(t57, ((uint8_t)0));
                        gm_l9 = t58;
                        uint8_t t59 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                        if (!t59) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srco9vbah234t6e, 91, 39);
                        goto gm_lb7;
                    }
gm_lb8:;
                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 91, 5);
                }
gm_lb7:;
            }
gm_lb6:;
            uint64_t t60 = gm_l6;
            uint64_t r61; bool o62 = __builtin_add_overflow(t60, ((uint64_t)1ULL), &r61);
            uint64_t t63 = r61;
            gm_l6 = t63;
        }
    }
gm_lb4:;
    gm_l8 = ((uint64_t)0ULL);
    void * t64 = gm_l0;
    uint64_t t65 = GM_LD(uint64_t, ((((uint8_t *)t64) + 344) + 8));
    gm_l7 = t65;
    {
        for (;;) {
            uint64_t t66 = gm_l8;
            uint64_t t67 = gm_l7;
            uint8_t t68 = (t66 < t67);
            if (!t68) {
                goto gm_lb9;
            }
            {
                {
                    {
                        void * t69 = (void *)(((uint8_t *)&F.l4) + 112);
                        gm_tftb1odc739t5 t70 = gm_f_push_vd1290p(t69, ((uint8_t)0));
                        gm_l10 = t70;
                        uint8_t t71 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                        if (!t71) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srco9vbah234t6e, 92, 35);
                        goto gm_lb12;
                    }
gm_lb13:;
                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 92, 5);
                }
gm_lb12:;
            }
gm_lb11:;
            uint64_t t72 = gm_l8;
            uint64_t r73; bool o74 = __builtin_add_overflow(t72, ((uint64_t)1ULL), &r73);
            uint64_t t75 = r73;
            gm_l8 = t75;
        }
    }
gm_lb9:;
    void * t76 = (void *)((uint8_t *)&F.l4);
    gm_b16_8 t77 = gm_l2;
    gm_b0_1 t78 = gm_f_run_checks_c17ks6r((void *)NULL, cx, NULL, t76, t77);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        memmove(gm_out, (((uint8_t *)&F.l4) + 208), 40);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_assigned_53ipnmg(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2) {
    struct gm_fr_assigned_53ipnmg F;
    void * gm_l0; /* c */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* place */
    gm_tcc1rq4n1avb2 gm_l3; /* k */
    void * gm_l4; /* u */
    uint8_t gm_l5; /* seen */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_tl874t2atnlh3 gm_l8; /* w */
    GM_STACK_CHECK(&F, 365u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l9;
    uint8_t gm_l10;
    uint8_t gm_l11;
    gm_t736djc66po0l gm_l12;
    gm_tftb1odc739t5 gm_l13;
    void * t15 = gm_l0;
    uint32_t t16 = gm_l1;
    void * t17 = gm_l2;
    gm_b0_1 t18 = gm_f_mutate_oi1s4vs((void *)NULL, cx, NULL, t15, t16, t17);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t19 = gm_l2;
    {
        {
            uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
            uint8_t t21 = (uint8_t)(t20 == ((uint8_t)15ULL));
            if (!t21) {
                goto gm_lb2;
            }
            gm_l10 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l10 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 378, 8);
    }
gm_lb1:;
    uint8_t t22 = gm_l10;
    if (t22) {
        gm_l9 = ((uint8_t)1);
    } else {
        void * t23 = gm_l2;
        {
            {
                uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t23) + 8) + 0));
                uint8_t t25 = (uint8_t)(t24 == ((uint8_t)16ULL));
                if (!t25) {
                    goto gm_lb5;
                }
                gm_l11 = ((uint8_t)1);
                goto gm_lb4;
            }
gm_lb5:;
            {
                gm_l11 = ((uint8_t)0);
                goto gm_lb4;
            }
gm_lb6:;
            gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 378, 35);
        }
gm_lb4:;
        uint8_t t26 = gm_l11;
        gm_l9 = t26;
    }
    uint8_t t27 = gm_l9;
    if (t27) {
        {
            {
                void * t28 = gm_l0;
                uint32_t t29 = gm_l1;
                void * t30 = gm_l2;
                gm_t736djc66po0l t31 = gm_f_root_oq23mha(t28, t29, t30);
                gm_l12 = t31;
                uint8_t t32 = (uint8_t)((((uint8_t *)&gm_l12))[8] != 0);
                if (!t32) {
                    goto gm_lb8;
                }
                gm_tcc1rq4n1avb2 t33 = GM_LD(gm_tcc1rq4n1avb2, ((uint8_t *)&gm_l12));
                gm_l3 = t33;
                void * t34 = gm_l0;
                gm_tcc1rq4n1avb2 t35 = gm_l3;
                void * t36 = gm_f_use_of_q9agaul((void *)NULL, cx, NULL, t34, t35);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l4 = t36;
                gm_l5 = ((uint8_t)0);
                void * t37 = gm_l4;
                gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)t37));
                gm_l6 = t38;
                gm_l7 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t39 = gm_l7;
                        uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                        uint8_t t41 = (t39 < t40);
                        if (!t41) {
                            goto gm_lb9;
                        }
                        {
                            void * t42 = GM_LD(void *, ((uint8_t *)&gm_l6));
                            uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            uint64_t t44 = gm_l7;
                            {
                                {
                                    gm_tl874t2atnlh3 t45 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t42) + (uintptr_t)t44 * 12));
                                    gm_l8 = t45;
                                    gm_tl874t2atnlh3 t46 = gm_l8;
                                    void * t47 = gm_l2;
                                    gm_tl874t2atnlh3 t48 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t47) + 72));
                                    uint8_t t49 = (gm_eqff85ac10o3(((uint8_t *)&t46), ((uint8_t *)&t48)));
                                    if (t49) {
                                        gm_l5 = ((uint8_t)1);
                                    }
                                    goto gm_lb12;
                                }
gm_lb13:;
                                gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 382, 13);
                            }
gm_lb12:;
                        }
gm_lb11:;
                        uint64_t t50 = gm_l7;
                        uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                        uint64_t t53 = r51;
                        gm_l7 = t53;
                    }
                }
gm_lb9:;
                uint8_t t54 = gm_l5;
                uint8_t t55 = (uint8_t)!t54;
                if (t55) {
                    void * t56 = gm_l4;
                    void * t57 = (void *)((uint8_t *)t56);
                    void * t58 = gm_l2;
                    gm_tl874t2atnlh3 t59 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t58) + 72));
                    gm_tftb1odc739t5 t60 = gm_f_push_0mk7udh(t57, GM_CONV(gm_cseahvg8v7p0vs, t59));
                    gm_l13 = t60;
                    uint8_t t61 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                    if (!t61) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srco9vbah234t6e, 385, 27);
                }
                goto gm_lb7;
            }
gm_lb8:;
        }
gm_lb7:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_binds_busqeo0(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2) {
    struct gm_fr_binds_busqeo0 F;
    void * gm_l2; /* p */
    uint32_t gm_l3; /* l */
    GM_STACK_CHECK(&F, 95u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l4;
    void * t5 = gm_l2;
    {
        {
            uint8_t t6 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)1ULL));
            if (!t7) {
                goto gm_lb2;
            }
            uint32_t t8 = GM_LD(uint32_t, ((((uint8_t *)t5) + 8) + 8));
            gm_l3 = t8;
            void * t9 = F.l0;
            void * t10 = (void *)(((uint8_t *)t9) + 152);
            uint32_t t11 = F.l1;
            uint32_t t12 = gm_l3;
            gm_tcc1rq4n1avb2 t13;
            GM_ST(uint32_t, ((uint8_t *)&t13 + 0), t11);
            GM_ST(uint32_t, ((uint8_t *)&t13 + 4), t12);
            gm_tftb1odc739t5 t14 = gm_f_put_3g1tdpr(t10, t13, ((uint8_t)1));
            gm_l4 = t14;
            uint8_t t15 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
            if (!t15) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srco9vbah234t6e, 345, 21);
            goto gm_lb1;
        }
gm_lb2:;
        {
            void * t16 = gm_l2;
            gm_closure t17 = ((gm_closure){(void *)gm_f_fn_o40bhnh, (void *)&F.h, cx});
            gm_f_for_each_subpat_arlrtrm(t16, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 344, 5);
    }
gm_lb1:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_block_stmts_c2nnk6m(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2) {
    struct gm_fr_block_stmts_c2nnk6m F;
    void * gm_l0; /* c */
    uint32_t gm_l1; /* f */
    gm_b16_8 gm_l2; /* stmts */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_trcmmd12cefb1 gm_l5; /* st */
    uint32_t gm_l6; /* local */
    gm_b16_8 gm_l7; /* locals */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_t9805qtonbsag gm_l10; /* l */
    uint32_t gm_l11; /* id */
    void * gm_l12; /* place */
    void * gm_l13; /* value */
    gm_b16_8 gm_l14; /* places */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    void * gm_l17; /* p */
    void * gm_l18; /* x */
    GM_STACK_CHECK(&F, 871u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l19;
    gm_tftb1odc739t5 gm_l20;
    gm_tftb1odc739t5 gm_l21;
    gm_b16_8 t29 = gm_l2;
    gm_l3 = t29;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t30 = gm_l4;
            uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t32 = (t30 < t31);
            if (!t32) {
                goto gm_lb1;
            }
            {
                void * t33 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t35 = gm_l4;
                {
                    {
                        memmove(((uint8_t *)&gm_l5), (((uint8_t *)t33) + (uintptr_t)t35 * 32), 32);
                        {
                            {
                                uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                uint8_t t37 = (uint8_t)(t36 == ((uint8_t)0ULL));
                                if (!t37) {
                                    goto gm_lb7;
                                }
                                uint32_t t38 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 8));
                                gm_l6 = t38;
                                void * t39 = gm_l0;
                                void * t40 = (void *)(((uint8_t *)t39) + 152);
                                uint32_t t41 = gm_l1;
                                uint32_t t42 = gm_l6;
                                gm_tcc1rq4n1avb2 t43;
                                GM_ST(uint32_t, ((uint8_t *)&t43 + 0), t41);
                                GM_ST(uint32_t, ((uint8_t *)&t43 + 4), t42);
                                gm_tftb1odc739t5 t44 = gm_f_put_3g1tdpr(t40, t43, ((uint8_t)1));
                                gm_l19 = t44;
                                uint8_t t45 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                                if (!t45) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_srco9vbah234t6e, 354, 31);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                uint8_t t47 = (uint8_t)(t46 == ((uint8_t)1ULL));
                                if (!t47) {
                                    goto gm_lb8;
                                }
                                gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
                                gm_l7 = t48;
                                gm_b16_8 t49 = gm_l7;
                                gm_l8 = t49;
                                gm_l9 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t50 = gm_l9;
                                        uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                        uint8_t t52 = (t50 < t51);
                                        if (!t52) {
                                            goto gm_lb9;
                                        }
                                        {
                                            void * t53 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                            uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                            uint64_t t55 = gm_l9;
                                            {
                                                {
                                                    gm_t9805qtonbsag t56 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)t53) + (uintptr_t)t55 * 8));
                                                    gm_l10 = t56;
                                                    {
                                                        {
                                                            uint8_t t57 = (uint8_t)((((uint8_t *)&gm_l10))[4] != 0);
                                                            if (!t57) {
                                                                goto gm_lb15;
                                                            }
                                                            uint32_t t58 = GM_LD(uint32_t, ((uint8_t *)&gm_l10));
                                                            gm_l11 = t58;
                                                            void * t59 = gm_l0;
                                                            void * t60 = (void *)(((uint8_t *)t59) + 152);
                                                            uint32_t t61 = gm_l1;
                                                            uint32_t t62 = gm_l11;
                                                            gm_tcc1rq4n1avb2 t63;
                                                            GM_ST(uint32_t, ((uint8_t *)&t63 + 0), t61);
                                                            GM_ST(uint32_t, ((uint8_t *)&t63 + 4), t62);
                                                            gm_tftb1odc739t5 t64 = gm_f_put_3g1tdpr(t60, t63, ((uint8_t)1));
                                                            gm_l20 = t64;
                                                            uint8_t t65 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                                                            if (!t65) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_srco9vbah234t6e, 357, 34);
                                                            goto gm_lb14;
                                                        }
gm_lb15:;
                                                    }
gm_lb14:;
                                                    goto gm_lb12;
                                                }
gm_lb13:;
                                                gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 356, 17);
                                            }
gm_lb12:;
                                        }
gm_lb11:;
                                        uint64_t t66 = gm_l9;
                                        uint64_t r67; bool o68 = __builtin_add_overflow(t66, ((uint64_t)1ULL), &r67);
                                        uint64_t t69 = r67;
                                        gm_l9 = t69;
                                    }
                                }
gm_lb9:;
                                goto gm_lb6;
                            }
gm_lb8:;
                            {
                                uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                uint8_t t71 = (uint8_t)(t70 == ((uint8_t)2ULL));
                                if (!t71) {
                                    goto gm_lb16;
                                }
                                void * t72 = GM_LD(void *, (((uint8_t *)&gm_l5) + 8));
                                gm_l12 = t72;
                                void * t73 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 8));
                                gm_l13 = t73;
                                void * t74 = gm_l0;
                                uint32_t t75 = gm_l1;
                                void * t76 = gm_l12;
                                gm_b0_1 t77 = gm_f_assigned_53ipnmg((void *)NULL, cx, NULL, t74, t75, t76);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                {
                                    {
                                        void * t78 = gm_l0;
                                        void * t79 = gm_l12;
                                        void * t80 = gm_l13;
                                        uint8_t t81 = gm_f_same_value_7pehjnc(t78, t79, t80);
                                        if (!t81) {
                                            goto gm_lb18;
                                        }
                                        void * t82 = gm_l12;
                                        gm_tl874t2atnlh3 t83 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t82) + 72));
                                        gm_tl874t2atnlh3 t84 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                                        uint8_t t85 = (!gm_eqff85ac10o3(((uint8_t *)&t83), ((uint8_t *)&t84)));
                                        if (!t85) {
                                            goto gm_lb18;
                                        }
                                        void * t86 = gm_l13;
                                        gm_tl874t2atnlh3 t87 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t86) + 72));
                                        gm_tl874t2atnlh3 t88 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                                        uint8_t t89 = (!gm_eqff85ac10o3(((uint8_t *)&t87), ((uint8_t *)&t88)));
                                        if (!t89) {
                                            goto gm_lb18;
                                        }
                                        void * t90 = gm_l0;
                                        void * t91 = (void *)(((uint8_t *)t90) + 208);
                                        void * t92 = gm_l12;
                                        gm_tl874t2atnlh3 t93 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t92) + 72));
                                        void * t94 = gm_l13;
                                        gm_tl874t2atnlh3 t95 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t94) + 72));
                                        gm_tl874t2atnlh3 t96 = GM_CONV(gm_tl874t2atnlh3, gm_f_to_c2nvemb(GM_CONV(gm_cseahvg8v7p0vs, t93), GM_CONV(gm_cseahvg8v7p0vs, t95)));
                                        gm_tj5f8vajf35o7 t97;
                                        memset(&t97, 0, sizeof t97);
                                        GM_ST(uint8_t, ((uint8_t *)&t97 + 0), ((uint8_t)5ULL));
                                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t97 + 8), t96);
                                        gm_tftb1odc739t5 t98 = gm_f_push_rqlbv1e(t91, &t97);
                                        gm_l21 = t98;
                                        uint8_t t99 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                                        if (!t99) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srco9vbah234t6e, 363, 21);
                                        goto gm_lb17;
                                    }
gm_lb18:;
                                }
gm_lb17:;
                                goto gm_lb6;
                            }
gm_lb16:;
                            {
                                uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                uint8_t t101 = (uint8_t)(t100 == ((uint8_t)3ULL));
                                if (!t101) {
                                    goto gm_lb19;
                                }
                                gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
                                gm_l14 = t102;
                                gm_b16_8 t103 = gm_l14;
                                gm_l15 = t103;
                                gm_l16 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t104 = gm_l16;
                                        uint64_t t105 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                        uint8_t t106 = (t104 < t105);
                                        if (!t106) {
                                            goto gm_lb20;
                                        }
                                        {
                                            void * t107 = GM_LD(void *, ((uint8_t *)&gm_l15));
                                            uint64_t t108 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                            uint64_t t109 = gm_l16;
                                            {
                                                {
                                                    void * t110 = GM_LD(void *, (((uint8_t *)t107) + (uintptr_t)t109 * 8));
                                                    gm_l17 = t110;
                                                    {
                                                        {
                                                            uint8_t t111 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l17)) != 0);
                                                            if (!t111) {
                                                                goto gm_lb26;
                                                            }
                                                            void * t112 = GM_LD(void *, ((uint8_t *)&gm_l17));
                                                            gm_l18 = t112;
                                                            void * t113 = gm_l0;
                                                            uint32_t t114 = gm_l1;
                                                            void * t115 = gm_l18;
                                                            gm_b0_1 t116 = gm_f_assigned_53ipnmg((void *)NULL, cx, NULL, t113, t114, t115);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            goto gm_lb25;
                                                        }
gm_lb26:;
                                                    }
gm_lb25:;
                                                    goto gm_lb23;
                                                }
gm_lb24:;
                                                gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 367, 17);
                                            }
gm_lb23:;
                                        }
gm_lb22:;
                                        uint64_t t117 = gm_l16;
                                        uint64_t r118; bool o119 = __builtin_add_overflow(t117, ((uint64_t)1ULL), &r118);
                                        uint64_t t120 = r118;
                                        gm_l16 = t120;
                                    }
                                }
gm_lb20:;
                                goto gm_lb6;
                            }
gm_lb19:;
                            {
                                goto gm_lb6;
                            }
gm_lb27:;
                            gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 353, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 352, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t121 = gm_l4;
            uint64_t r122; bool o123 = __builtin_add_overflow(t121, ((uint64_t)1ULL), &r122);
            uint64_t t124 = r122;
            gm_l4 = t124;
        }
    }
gm_lb1:;
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
extern __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_bool_lit_q44074e(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_file_ok_4j31vl0(void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0) {
    struct gm_fr_file_ok_4j31vl0 F;
    gm_tl874t2atnlh3 gm_l0; /* span */
    GM_STACK_CHECK_SHALLOW(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    uint8_t gm_l2;
    gm_tl874t2atnlh3 t2 = gm_l0;
    gm_tl874t2atnlh3 t3 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
    uint8_t t4 = (!gm_eqff85ac10o3(((uint8_t *)&t2), ((uint8_t *)&t3)));
    if (t4) {
        uint32_t t5 = GM_LD(uint32_t, ((uint8_t *)&gm_l0));
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&((struct gm_fr_run_checks_c17ks6r *)((&F.h)->up))->l1) + 8));
        uint32_t t7 = (uint32_t)t6;
        uint8_t t8 = (t5 < t7);
        gm_l2 = t8;
    } else {
        gm_l2 = ((uint8_t)0);
    }
    uint8_t t9 = gm_l2;
    if (t9) {
        void * t10 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_run_checks_c17ks6r *)((&F.h)->up))->l1));
        uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&((struct gm_fr_run_checks_c17ks6r *)((&F.h)->up))->l1) + 8));
        uint32_t t12 = GM_LD(uint32_t, ((uint8_t *)&gm_l0));
        uint64_t t13 = (uint64_t)t12;
        if ((uintptr_t)t13 >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)t13, t11, gm_srco9vbah234t6e, 99, 116);
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)t10) + (uintptr_t)t13 * 1));
        gm_l1 = t14;
    } else {
        gm_l1 = ((uint8_t)0);
    }
    uint8_t t15 = gm_l1;
    F.ret = t15;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_259eng6(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_o40bhnh(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_array_p7ehais(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_array_p7ehais F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint8_t gm_l2;
    gm_t53at80pfdus3 gm_l3;
    void * t8 = gm_l0;
    void * t9 = (void *)(((uint8_t *)t8) + 8);
    gm_t88rcucg1b7uk t10 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l3, t9, GM_CONV(gm_csguankko4lmhd, t10));
    {
        {
            {
                {
                    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t12 = (uint8_t)(t11 == ((uint8_t)11ULL));
                    if (!t12) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t14 = (uint8_t)(t13 == ((uint8_t)20ULL));
                    if (!t14) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                goto gm_lb2;
            }
gm_lb3:;
            gm_l2 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l2 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb6:;
        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 265, 61);
    }
gm_lb1:;
    uint8_t t15 = gm_l2;
    F.ret = t15;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_mutate_oi1s4vs(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_needless_pub_jqsnq13(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2) {
    struct gm_fr_needless_pub_jqsnq13 F;
    void * gm_l0; /* c */
    uint64_t gm_l1; /* d */
    gm_b16_8 gm_l2; /* scoped */
    void * gm_l3; /* s */
    void * gm_l4; /* decl */
    gm_b16_8 gm_l5; /* name */
    uint8_t gm_l6; /* candidate */
    gm_tl874t2atnlh3 gm_l7; /* span */
    uint8_t gm_l8; /* file_ok */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    GM_STACK_CHECK(&F, 668u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l15;
    uint8_t gm_l16;
    uint8_t gm_l17;
    uint8_t gm_l18;
    uint8_t gm_l19;
    uint8_t gm_l20;
    uint8_t gm_l21;
    uint8_t gm_l22;
    uint8_t gm_l23;
    uint8_t gm_l24;
    uint8_t gm_l25;
    void * t27 = gm_l0;
    void * t28 = GM_LD(void *, ((uint8_t *)t27));
    gm_l3 = t28;
    void * t29 = gm_l3;
    void * t30 = GM_LD(void *, ((uint8_t *)t29));
    void * t31 = GM_LD(void *, (((uint8_t *)t30) + 240));
    uint64_t t32 = GM_LD(uint64_t, ((((uint8_t *)t30) + 240) + 8));
    uint64_t t33 = gm_l1;
    if ((uintptr_t)t33 >= (uintptr_t)(t32)) gm_trap_index((uintptr_t)t33, t32, gm_srco9vbah234t6e, 223, 14);
    void * t34 = (void *)(((uint8_t *)t31) + (uintptr_t)t33 * 216);
    gm_l4 = t34;
    void * t35 = gm_l4;
    gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)t35));
    gm_l5 = t36;
    void * t37 = gm_l4;
    {
        {
            uint8_t t38 = GM_LD(uint8_t, (((((uint8_t *)t37) + 40) + 56) + 0));
            uint8_t t39 = (uint8_t)(t38 == ((uint8_t)0ULL));
            if (!t39) {
                goto gm_lb2;
            }
            gm_l6 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t40 = GM_LD(uint8_t, (((((uint8_t *)t37) + 40) + 56) + 0));
            uint8_t t41 = (uint8_t)(t40 == ((uint8_t)1ULL));
            if (!t41) {
                goto gm_lb3;
            }
            gm_l6 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t42 = GM_LD(uint8_t, (((((uint8_t *)t37) + 40) + 56) + 0));
            uint8_t t43 = (uint8_t)(t42 == ((uint8_t)2ULL));
            if (!t43) {
                goto gm_lb4;
            }
            void * t44 = gm_l0;
            uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)t44) + 8));
            gm_l6 = t45;
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 225, 18);
    }
gm_lb1:;
    void * t46 = gm_l4;
    gm_tl874t2atnlh3 t47 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t46) + 16));
    gm_l7 = t47;
    gm_tl874t2atnlh3 t48 = gm_l7;
    gm_tl874t2atnlh3 t49 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
    uint8_t t50 = (!gm_eqff85ac10o3(((uint8_t *)&t48), ((uint8_t *)&t49)));
    if (t50) {
        uint32_t t51 = GM_LD(uint32_t, ((uint8_t *)&gm_l7));
        uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint32_t t53 = (uint32_t)t52;
        uint8_t t54 = (t51 < t53);
        gm_l16 = t54;
    } else {
        gm_l16 = ((uint8_t)0);
    }
    uint8_t t55 = gm_l16;
    if (t55) {
        void * t56 = GM_LD(void *, ((uint8_t *)&gm_l2));
        uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint32_t t58 = GM_LD(uint32_t, ((uint8_t *)&gm_l7));
        uint64_t t59 = (uint64_t)t58;
        if ((uintptr_t)t59 >= (uintptr_t)(t57)) gm_trap_index((uintptr_t)t59, t57, gm_srco9vbah234t6e, 231, 83);
        uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)t56) + (uintptr_t)t59 * 1));
        gm_l15 = t60;
    } else {
        gm_l15 = ((uint8_t)0);
    }
    uint8_t t61 = gm_l15;
    gm_l8 = t61;
    uint8_t t62 = gm_l6;
    uint8_t t63 = (uint8_t)!t62;
    if (t63) {
        gm_l20 = ((uint8_t)1);
    } else {
        gm_b16_8 t64 = gm_l5;
        gm_b16_8 t65;
        GM_ST(void *, (uint8_t *)&t65, (void *)(gm_sog4rksrmsqd2));
        GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(4));
        uint8_t t66 = (gm_eq0iqiubfm7e(((uint8_t *)&t64), ((uint8_t *)&t65)));
        gm_l20 = t66;
    }
    uint8_t t67 = gm_l20;
    if (t67) {
        gm_l19 = ((uint8_t)1);
    } else {
        void * t68 = gm_l3;
        void * t69 = GM_LD(void *, ((uint8_t *)t68));
        void * t70 = GM_LD(void *, (((uint8_t *)t69) + 200));
        uint64_t t71 = GM_LD(uint64_t, ((((uint8_t *)t69) + 200) + 8));
        void * t72 = gm_l4;
        uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)t72) + 32));
        if ((uintptr_t)t73 >= (uintptr_t)(t71)) gm_trap_index((uintptr_t)t73, t71, gm_srco9vbah234t6e, 232, 43);
        uint8_t t74 = GM_LD(uint8_t, ((((uint8_t *)t70) + (uintptr_t)t73 * 184) + 112));
        gm_l19 = t74;
    }
    uint8_t t75 = gm_l19;
    if (t75) {
        gm_l18 = ((uint8_t)1);
    } else {
        uint8_t t76 = gm_l8;
        uint8_t t77 = (uint8_t)!t76;
        gm_l18 = t77;
    }
    uint8_t t78 = gm_l18;
    if (t78) {
        gm_l17 = ((uint8_t)1);
    } else {
        void * t79 = gm_l3;
        uint64_t t80 = gm_l1;
        uint8_t t81 = gm_f_is_used_outside_tbk81ks(t79, t80);
        gm_l17 = t81;
    }
    uint8_t t82 = gm_l17;
    if (t82) {
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
    void * t83 = gm_l4;
    gm_b16_8 t84 = GM_LD(gm_b16_8, ((((uint8_t *)t83) + 40) + 40));
    gm_l9 = t84;
    gm_l10 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t85 = gm_l10;
            uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
            uint8_t t87 = (t85 < t86);
            if (!t87) {
                goto gm_lb5;
            }
            {
                void * t88 = GM_LD(void *, ((uint8_t *)&gm_l9));
                uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                uint64_t t90 = gm_l10;
                {
                    {
                        {
                            memmove(((uint8_t *)&F.l11), (((uint8_t *)t88) + (uintptr_t)t90 * 72), 72);
                        }
                        {
                            gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
                            gm_b16_8 t92;
                            GM_ST(void *, (uint8_t *)&t92, (void *)(gm_spe8t3e79vnal));
                            GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(6));
                            gm_b16_8 t93;
                            GM_ST(void *, (uint8_t *)&t93, (void *)(gm_s1ft2nrvnhcbp));
                            GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(9));
                            gm_t6j6smj7qoctu t94;
                            GM_ST(gm_b16_8, ((uint8_t *)&t94 + 0), t92);
                            GM_ST(gm_b16_8, ((uint8_t *)&t94 + 16), t93);
                            uint8_t t95 = 0;
                            if (gm_eq0iqiubfm7e(((uint8_t *)&t94) + 0, ((uint8_t *)&t91))) t95 = 1;
                            if (gm_eq0iqiubfm7e(((uint8_t *)&t94) + 16, ((uint8_t *)&t91))) t95 = 1;
                            uint8_t t96 = (uint8_t)(t95);
                            if (t96) {
                                memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
                                goto gm_fdone;
                            }
                        }
                        goto gm_lb8;
                    }
gm_lb9:;
                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 235, 5);
                }
gm_lb8:;
            }
gm_lb7:;
            uint64_t t97 = gm_l10;
            uint64_t r98; bool o99 = __builtin_add_overflow(t97, ((uint64_t)1ULL), &r98);
            uint64_t t100 = r98;
            gm_l10 = t100;
        }
    }
gm_lb5:;
    {
        {
            {
                void * t101 = gm_l4;
                uint8_t t102 = GM_LD(uint8_t, (((((uint8_t *)t101) + 40) + 64) + 0));
                uint8_t t103 = (uint8_t)(t102 == ((uint8_t)0ULL));
                if (!t103) {
                    goto gm_lb11;
                }
                memmove(((uint8_t *)&F.l12), (((((uint8_t *)t101) + 40) + 64) + 8), 64);
            }
            {
                void * t104 = gm_l3;
                uint64_t t105 = gm_l1;
                gm_f_state_a5bnsjb((void *)&F.l13, (void *)NULL, cx, NULL, t104, t105);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)&F.l13) + 0));
                    uint8_t t107 = (uint8_t)(t106 == ((uint8_t)2ULL));
                    if (!t107) {
                        goto gm_lb13;
                    }
                    {
                        memmove(((uint8_t *)&F.l14), (((uint8_t *)&F.l13) + 8), 208);
                    }
                    void * t108 = GM_LD(void *, (((uint8_t *)&F.l12) + 40));
                    {
                        {
                            uint8_t t109 = GM_LD(uint8_t, ((((uint8_t *)t108) + 8) + 0));
                            uint8_t t110 = (uint8_t)(t109 == ((uint8_t)36ULL));
                            if (!t110) {
                                goto gm_lb15;
                            }
                            gm_l21 = ((uint8_t)1);
                            goto gm_lb14;
                        }
gm_lb15:;
                        {
                            gm_l21 = ((uint8_t)0);
                            goto gm_lb14;
                        }
gm_lb16:;
                        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 241, 8);
                    }
gm_lb14:;
                    uint8_t t111 = gm_l21;
                    if (t111) {
                        {
                            {
                                uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&F.l14) + 0));
                                uint8_t t113 = (uint8_t)(t112 == ((uint8_t)5ULL));
                                if (!t113) {
                                    goto gm_lb18;
                                }
                                gm_l22 = ((uint8_t)1);
                                goto gm_lb17;
                            }
gm_lb18:;
                            {
                                gm_l22 = ((uint8_t)0);
                                goto gm_lb17;
                            }
gm_lb19:;
                            gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 242, 12);
                        }
gm_lb17:;
                        uint8_t t114 = gm_l22;
                        if (t114) {
                            gm_b16_8 t115;
                            GM_ST(void *, (uint8_t *)&t115, (void *)(gm_sq0dqiq83b924));
                            GM_ST(uintptr_t, (uint8_t *)&t115 + 8, (uintptr_t)(8));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t115);
                            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                            goto gm_fdone;
                        }
                    }
                    {
                        {
                            uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&F.l14) + 0));
                            uint8_t t117 = (uint8_t)(t116 == ((uint8_t)7ULL));
                            if (!t117) {
                                goto gm_lb21;
                            }
                            gm_l24 = ((uint8_t)1);
                            goto gm_lb20;
                        }
gm_lb21:;
                        {
                            gm_l24 = ((uint8_t)0);
                            goto gm_lb20;
                        }
gm_lb22:;
                        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 244, 8);
                    }
gm_lb20:;
                    uint8_t t118 = gm_l24;
                    if (t118) {
                        gm_l23 = ((uint8_t)1);
                    } else {
                        {
                            {
                                uint8_t t119 = GM_LD(uint8_t, (((uint8_t *)&F.l14) + 0));
                                uint8_t t120 = (uint8_t)(t119 == ((uint8_t)8ULL));
                                if (!t120) {
                                    goto gm_lb24;
                                }
                                gm_l25 = ((uint8_t)1);
                                goto gm_lb23;
                            }
gm_lb24:;
                            {
                                gm_l25 = ((uint8_t)0);
                                goto gm_lb23;
                            }
gm_lb25:;
                            gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 244, 26);
                        }
gm_lb23:;
                        uint8_t t121 = gm_l25;
                        gm_l23 = t121;
                    }
                    uint8_t t122 = gm_l23;
                    if (t122) {
                        gm_b16_8 t123;
                        GM_ST(void *, (uint8_t *)&t123, (void *)(gm_svklio0qebpog));
                        GM_ST(uintptr_t, (uint8_t *)&t123 + 8, (uintptr_t)(8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t123);
                        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                        goto gm_fdone;
                    }
                    {
                        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
                        goto gm_fdone;
                    }
                }
gm_lb13:;
                memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
                goto gm_fdone;
            }
gm_lb12:;
        }
gm_lb11:;
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
gm_lb10:;
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
__attribute__((aligned(4))) uint8_t gm_f_path_eq_l3qbb7v(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_path_eq_l3qbb7v F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    uint32_t gm_l2; /* x */
    uint32_t gm_l3; /* y */
    uint32_t gm_l4; /* l1 */
    uint32_t gm_l5; /* x */
    uint32_t gm_l6; /* l2 */
    uint32_t gm_l7; /* y */
    uint64_t gm_l8; /* x */
    uint64_t gm_l9; /* y */
    void * gm_l10; /* b1 */
    uint32_t gm_l11; /* x1 */
    void * gm_l12; /* b2 */
    uint32_t gm_l13; /* x2 */
    void * gm_l14; /* x */
    void * gm_l15; /* y */
    void * gm_l16; /* b1 */
    void * gm_l17; /* x1 */
    void * gm_l18; /* b2 */
    void * gm_l19; /* x2 */
    GM_STACK_CHECK(&F, 469u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l22;
    uint8_t gm_l23;
    void * t26 = gm_l0;
    {
        {
            uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
            uint8_t t28 = (uint8_t)(t27 == ((uint8_t)15ULL));
            if (!t28) {
                goto gm_lb2;
            }
            uint32_t t29 = GM_LD(uint32_t, ((((uint8_t *)t26) + 8) + 8));
            gm_l2 = t29;
            {
                {
                    void * t30 = gm_l1;
                    uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t30) + 8) + 0));
                    uint8_t t32 = (uint8_t)(t31 == ((uint8_t)15ULL));
                    if (!t32) {
                        goto gm_lb4;
                    }
                    uint32_t t33 = GM_LD(uint32_t, ((((uint8_t *)t30) + 8) + 8));
                    gm_l3 = t33;
                    uint32_t t34 = gm_l2;
                    uint32_t t35 = gm_l3;
                    uint8_t t36 = ((t34 == t35));
                    F.ret = t36;
                    goto gm_fret;
                }
gm_lb4:;
            }
gm_lb3:;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
            uint8_t t38 = (uint8_t)(t37 == ((uint8_t)16ULL));
            if (!t38) {
                goto gm_lb5;
            }
            uint32_t t39 = GM_LD(uint32_t, ((((uint8_t *)t26) + 8) + 8));
            gm_l4 = t39;
            uint32_t t40 = GM_LD(uint32_t, (((((uint8_t *)t26) + 8) + 8) + 4));
            gm_l5 = t40;
            {
                {
                    void * t41 = gm_l1;
                    uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
                    uint8_t t43 = (uint8_t)(t42 == ((uint8_t)16ULL));
                    if (!t43) {
                        goto gm_lb7;
                    }
                    uint32_t t44 = GM_LD(uint32_t, ((((uint8_t *)t41) + 8) + 8));
                    gm_l6 = t44;
                    uint32_t t45 = GM_LD(uint32_t, (((((uint8_t *)t41) + 8) + 8) + 4));
                    gm_l7 = t45;
                    uint32_t t46 = gm_l4;
                    uint32_t t47 = gm_l6;
                    uint8_t t48 = ((t46 == t47));
                    if (t48) {
                        uint32_t t49 = gm_l5;
                        uint32_t t50 = gm_l7;
                        uint8_t t51 = ((t49 == t50));
                        gm_l22 = t51;
                    } else {
                        gm_l22 = ((uint8_t)0);
                    }
                    uint8_t t52 = gm_l22;
                    F.ret = t52;
                    goto gm_fret;
                }
gm_lb7:;
            }
gm_lb6:;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb5:;
        {
            uint8_t t53 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
            uint8_t t54 = (uint8_t)(t53 == ((uint8_t)17ULL));
            if (!t54) {
                goto gm_lb8;
            }
            uint64_t t55 = GM_LD(uint64_t, (((((uint8_t *)t26) + 8) + 8) + 16));
            gm_l8 = t55;
            {
                {
                    void * t56 = gm_l1;
                    uint8_t t57 = GM_LD(uint8_t, ((((uint8_t *)t56) + 8) + 0));
                    uint8_t t58 = (uint8_t)(t57 == ((uint8_t)17ULL));
                    if (!t58) {
                        goto gm_lb10;
                    }
                    uint64_t t59 = GM_LD(uint64_t, (((((uint8_t *)t56) + 8) + 8) + 16));
                    gm_l9 = t59;
                    uint64_t t60 = gm_l8;
                    uint64_t t61 = gm_l9;
                    uint8_t t62 = ((t60 == t61));
                    F.ret = t62;
                    goto gm_fret;
                }
gm_lb10:;
            }
gm_lb9:;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb8:;
        {
            uint8_t t63 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
            uint8_t t64 = (uint8_t)(t63 == ((uint8_t)19ULL));
            if (!t64) {
                goto gm_lb11;
            }
            void * t65 = GM_LD(void *, ((((uint8_t *)t26) + 8) + 8));
            gm_l10 = t65;
            uint32_t t66 = GM_LD(uint32_t, (((((uint8_t *)t26) + 8) + 8) + 8));
            gm_l11 = t66;
            {
                {
                    void * t67 = gm_l1;
                    uint8_t t68 = GM_LD(uint8_t, ((((uint8_t *)t67) + 8) + 0));
                    uint8_t t69 = (uint8_t)(t68 == ((uint8_t)19ULL));
                    if (!t69) {
                        goto gm_lb13;
                    }
                    void * t70 = GM_LD(void *, ((((uint8_t *)t67) + 8) + 8));
                    gm_l12 = t70;
                    uint32_t t71 = GM_LD(uint32_t, (((((uint8_t *)t67) + 8) + 8) + 8));
                    gm_l13 = t71;
                    uint32_t t72 = gm_l11;
                    uint32_t t73 = gm_l13;
                    uint8_t t74 = ((t72 == t73));
                    if (t74) {
                        void * t75 = gm_l10;
                        void * t76 = gm_l12;
                        uint8_t t77 = gm_f_path_eq_l3qbb7v(t75, t76);
                        gm_l23 = t77;
                    } else {
                        gm_l23 = ((uint8_t)0);
                    }
                    uint8_t t78 = gm_l23;
                    F.ret = t78;
                    goto gm_fret;
                }
gm_lb13:;
            }
gm_lb12:;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb11:;
        {
            uint8_t t79 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
            uint8_t t80 = (uint8_t)(t79 == ((uint8_t)18ULL));
            if (!t80) {
                goto gm_lb14;
            }
            void * t81 = GM_LD(void *, ((((uint8_t *)t26) + 8) + 8));
            gm_l14 = t81;
            {
                {
                    void * t82 = gm_l1;
                    uint8_t t83 = GM_LD(uint8_t, ((((uint8_t *)t82) + 8) + 0));
                    uint8_t t84 = (uint8_t)(t83 == ((uint8_t)18ULL));
                    if (!t84) {
                        goto gm_lb16;
                    }
                    void * t85 = GM_LD(void *, ((((uint8_t *)t82) + 8) + 8));
                    gm_l15 = t85;
                    void * t86 = gm_l14;
                    void * t87 = gm_l15;
                    uint8_t t88 = gm_f_path_eq_l3qbb7v(t86, t87);
                    F.ret = t88;
                    goto gm_fret;
                }
gm_lb16:;
            }
gm_lb15:;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb14:;
        {
            uint8_t t89 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
            uint8_t t90 = (uint8_t)(t89 == ((uint8_t)20ULL));
            if (!t90) {
                goto gm_lb17;
            }
            void * t91 = GM_LD(void *, ((((uint8_t *)t26) + 8) + 8));
            gm_l16 = t91;
            void * t92 = GM_LD(void *, (((((uint8_t *)t26) + 8) + 8) + 8));
            gm_l17 = t92;
            {
                {
                    void * t93 = gm_l1;
                    uint8_t t94 = GM_LD(uint8_t, ((((uint8_t *)t93) + 8) + 0));
                    uint8_t t95 = (uint8_t)(t94 == ((uint8_t)20ULL));
                    if (!t95) {
                        goto gm_lb19;
                    }
                    void * t96 = GM_LD(void *, ((((uint8_t *)t93) + 8) + 8));
                    gm_l18 = t96;
                    void * t97 = GM_LD(void *, (((((uint8_t *)t93) + 8) + 8) + 8));
                    gm_l19 = t97;
                    void * t98 = gm_l16;
                    void * t99 = gm_l18;
                    uint8_t t100 = gm_f_path_eq_l3qbb7v(t98, t99);
                    uint8_t t101 = (uint8_t)!t100;
                    if (t101) {
                        F.ret = ((uint8_t)0);
                        goto gm_fret;
                    }
                    {
                        {
                            void * t102 = gm_l17;
                            uint8_t t103 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t104 = (uint8_t)(t103 == ((uint8_t)0ULL));
                            if (!t104) {
                                goto gm_lb21;
                            }
                            memmove(((uint8_t *)&F.l20), ((((uint8_t *)t102) + 8) + 8), 48);
                            {
                                {
                                    {
                                        void * t105 = gm_l19;
                                        uint8_t t106 = GM_LD(uint8_t, ((((uint8_t *)t105) + 8) + 0));
                                        uint8_t t107 = (uint8_t)(t106 == ((uint8_t)0ULL));
                                        if (!t107) {
                                            goto gm_lb23;
                                        }
                                        memmove(((uint8_t *)&F.l21), ((((uint8_t *)t105) + 8) + 8), 48);
                                    }
                                    {
                                        uint8_t t108 = gm_f_eq_60fjn21(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l20)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l21)));
                                        F.ret = t108;
                                        goto gm_fret;
                                    }
                                }
gm_lb23:;
                            }
gm_lb22:;
                            goto gm_lb20;
                        }
gm_lb21:;
                    }
gm_lb20:;
                    void * t109 = gm_l17;
                    void * t110 = gm_l19;
                    uint8_t t111 = gm_f_path_eq_l3qbb7v(t109, t110);
                    F.ret = t111;
                    goto gm_fret;
                }
gm_lb19:;
            }
gm_lb18:;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb17:;
        {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb24:;
        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 404, 5);
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t736djc66po0l gm_f_root_oq23mha(void * a0, uint32_t a1, void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_root_oq23mha F;
    void * gm_l0; /* c */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* e */
    uint32_t gm_l3; /* l */
    uint32_t gm_l4; /* levels */
    uint32_t gm_l5; /* local */
    uint32_t gm_l6; /* fr */
    void * gm_l7; /* base */
    void * gm_l8; /* base */
    gm_tao63pk3r18v5 gm_l9; /* sl */
    GM_STACK_CHECK(&F, 433u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t9805qtonbsag gm_l10;
    void * t9 = gm_l2;
    {
        {
            uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)15ULL));
            if (!t11) {
                goto gm_lb2;
            }
            uint32_t t12 = GM_LD(uint32_t, ((((uint8_t *)t9) + 8) + 8));
            gm_l3 = t12;
            uint32_t t13 = gm_l1;
            uint32_t t14 = gm_l3;
            gm_tcc1rq4n1avb2 t15;
            GM_ST(uint32_t, ((uint8_t *)&t15 + 0), t13);
            GM_ST(uint32_t, ((uint8_t *)&t15 + 4), t14);
            gm_t736djc66po0l t16;
            GM_ST(uint8_t, (uint8_t *)&t16 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t16 + 10, 0);
            GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t16), t15);
            ((uint8_t *)&t16)[8] = 1;
            F.ret = t16;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)16ULL));
            if (!t18) {
                goto gm_lb3;
            }
            uint32_t t19 = GM_LD(uint32_t, ((((uint8_t *)t9) + 8) + 8));
            gm_l4 = t19;
            uint32_t t20 = GM_LD(uint32_t, (((((uint8_t *)t9) + 8) + 8) + 4));
            gm_l5 = t20;
            void * t21 = gm_l0;
            void * t22 = GM_LD(void *, ((uint8_t *)t21));
            uint32_t t23 = gm_l1;
            uint32_t t24 = gm_l4;
            gm_t9805qtonbsag t25 = gm_f_frame_up_h0ddp1d(t22, t23, t24);
            gm_l10 = t25;
            uint8_t t26 = (uint8_t)((((uint8_t *)&gm_l10))[4] != 0);
            if (t26) {
                uint32_t t27 = GM_LD(uint32_t, ((uint8_t *)&gm_l10));
                gm_l6 = t27;
            } else {
                gm_t736djc66po0l t28;
                memset(&t28, 0, sizeof t28);
                F.ret = t28;
                goto gm_fret;
            }
            uint32_t t29 = gm_l6;
            uint32_t t30 = gm_l5;
            gm_tcc1rq4n1avb2 t31;
            GM_ST(uint32_t, ((uint8_t *)&t31 + 0), t29);
            GM_ST(uint32_t, ((uint8_t *)&t31 + 4), t30);
            gm_t736djc66po0l t32;
            GM_ST(uint8_t, (uint8_t *)&t32 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t32 + 10, 0);
            GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t32), t31);
            ((uint8_t *)&t32)[8] = 1;
            F.ret = t32;
            goto gm_fret;
        }
gm_lb3:;
        {
            uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            uint8_t t34 = (uint8_t)(t33 == ((uint8_t)19ULL));
            if (!t34) {
                goto gm_lb4;
            }
            void * t35 = GM_LD(void *, ((((uint8_t *)t9) + 8) + 8));
            gm_l7 = t35;
            void * t36 = gm_l0;
            uint32_t t37 = gm_l1;
            void * t38 = gm_l7;
            gm_t736djc66po0l t39 = gm_f_root_oq23mha(t36, t37, t38);
            F.ret = t39;
            goto gm_fret;
        }
gm_lb4:;
        {
            uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            uint8_t t41 = (uint8_t)(t40 == ((uint8_t)20ULL));
            if (!t41) {
                goto gm_lb5;
            }
            void * t42 = GM_LD(void *, ((((uint8_t *)t9) + 8) + 8));
            gm_l8 = t42;
            void * t43 = gm_l0;
            void * t44 = GM_LD(void *, ((uint8_t *)t43));
            void * t45 = gm_l8;
            gm_t88rcucg1b7uk t46 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t45));
            uint8_t t47 = gm_f_is_array_p7ehais(t44, GM_CONV(gm_csguankko4lmhd, t46));
            if (t47) {
                void * t48 = gm_l0;
                uint32_t t49 = gm_l1;
                void * t50 = gm_l8;
                gm_t736djc66po0l t51 = gm_f_root_oq23mha(t48, t49, t50);
                F.ret = t51;
                goto gm_fret;
            }
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t52 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            uint8_t t53 = (uint8_t)(t52 == ((uint8_t)21ULL));
            if (!t53) {
                goto gm_lb6;
            }
            memmove(((uint8_t *)&gm_l9), ((((uint8_t *)t9) + 8) + 8), 32);
            void * t54 = gm_l0;
            void * t55 = GM_LD(void *, ((uint8_t *)t54));
            void * t56 = GM_LD(void *, ((uint8_t *)&gm_l9));
            gm_t88rcucg1b7uk t57 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t56));
            uint8_t t58 = gm_f_is_array_p7ehais(t55, GM_CONV(gm_csguankko4lmhd, t57));
            if (t58) {
                void * t59 = gm_l0;
                uint32_t t60 = gm_l1;
                void * t61 = GM_LD(void *, ((uint8_t *)&gm_l9));
                gm_t736djc66po0l t62 = gm_f_root_oq23mha(t59, t60, t61);
                F.ret = t62;
                goto gm_fret;
            }
            goto gm_lb1;
        }
gm_lb6:;
        {
            goto gm_lb1;
        }
gm_lb7:;
        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 251, 5);
    }
gm_lb1:;
    gm_t736djc66po0l t63;
    memset(&t63, 0, sizeof t63);
    F.ret = t63;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_run_checks_c17ks6r(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_run_checks_c17ks6r F;
    void * gm_l0; /* c */
    void * gm_l2; /* s */
    gm_closure gm_l3; /* file_ok */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint32_t gm_l11; /* f */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    uint32_t gm_l15; /* f */
    void * gm_l16; /* body */
    uint32_t gm_l18; /* outer */
    gm_tf5srh4mp4b3i gm_l19; /* me */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    uint32_t gm_l22; /* callee */
    uint32_t gm_l23; /* g */
    uint64_t gm_l24; /* d */
    uint64_t gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    uint64_t gm_l27; /* k */
    void * gm_l28; /* init */
    gm_b16_8 gm_l30; /* <tmp> */
    uint64_t gm_l31; /* <tmp> */
    uint32_t gm_l32; /* callee */
    uint64_t gm_l33; /* d */
    gm_b16_8 gm_l34; /* <tmp> */
    uint64_t gm_l35; /* <tmp> */
    uint32_t gm_l36; /* f */
    uint64_t gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    uint64_t gm_l40; /* i */
    uint32_t gm_l41; /* f */
    gm_b16_8 gm_l43; /* <tmp> */
    uint64_t gm_l44; /* <tmp> */
    void * gm_l46; /* def */
    uint8_t gm_l47; /* own_params */
    gm_b16_8 gm_l48; /* <tmp> */
    uint64_t gm_l49; /* <tmp> */
    uint64_t gm_l50; /* j */
    uint32_t gm_l52; /* lid */
    uint8_t gm_l53; /* param */
    uint64_t gm_l54; /* writes */
    uint8_t gm_l55; /* mutated */
    void * gm_l56; /* u */
    uint64_t gm_l57; /* uses */
    uint32_t gm_l58; /* n */
    uint64_t gm_l59; /* <tmp> */
    uint64_t gm_l60; /* <tmp> */
    uint64_t gm_l61; /* i */
    void * gm_l62; /* d */
    gm_b16_8 gm_l63; /* name */
    gm_b16_8 gm_l64; /* what */
    uint8_t gm_l66; /* skip */
    gm_b16_8 gm_l67; /* <tmp> */
    uint64_t gm_l68; /* <tmp> */
    gm_b16_8 gm_l71; /* what */
    void * gm_l72; /* inner */
    gm_b16_8 gm_l75; /* <tmp> */
    uint64_t gm_l76; /* <tmp> */
    gm_tl874t2atnlh3 gm_l78; /* span */
    gm_tl874t2atnlh3 gm_l79; /* span */
    gm_tl874t2atnlh3 gm_l80; /* span */
    uint8_t gm_l81; /* keep */
    GM_STACK_CHECK(&F, 5568u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    gm_tftb1odc739t5 gm_l82;
    gm_tagrkbqh054h7 gm_l83;
    uint8_t gm_l84;
    uint8_t gm_l85;
    gm_t9805qtonbsag gm_l86;
    uint8_t gm_l87;
    uint8_t gm_l88;
    uint8_t gm_l89;
    uint8_t gm_l90;
    gm_tftb1odc739t5 gm_l91;
    uint8_t gm_l92;
    uint8_t gm_l93;
    gm_t5im2v50m3hk6 gm_l94;
    uint8_t gm_l95;
    uint8_t gm_l96;
    uint8_t gm_l97;
    void * gm_l98;
    gm_t9805qtonbsag gm_l99;
    gm_tftb1odc739t5 gm_l100;
    gm_tftb1odc739t5 gm_l101;
    gm_th2lqbo77pvsc gm_l102;
    gm_tftb1odc739t5 gm_l103;
    uint8_t gm_l104;
    uint8_t gm_l105;
    uint8_t gm_l106;
    uint8_t gm_l107;
    uint8_t gm_l108;
    uint8_t gm_l109;
    uint8_t gm_l110;
    uint8_t gm_l111;
    uint8_t gm_l112;
    uint8_t gm_l113;
    gm_tftb1odc739t5 gm_l114;
    gm_tftb1odc739t5 gm_l115;
    {
        void * t140 = gm_l0;
        void * t141 = GM_LD(void *, ((uint8_t *)t140));
        gm_l2 = t141;
        gm_closure t142 = ((gm_closure){(void *)gm_f_file_ok_4j31vl0, (void *)&F.h, cx});
        gm_l3 = t142;
        gm_trqfvmjtt4sij t143;
        gm_f_list_fsi5en1((void *)&t143, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l4 = t143;
        gm_l6 = ((uint64_t)0ULL);
        void * t144 = gm_l2;
        uint64_t t145 = GM_LD(uint64_t, ((((uint8_t *)t144) + 344) + 8));
        gm_l5 = t145;
        {
            for (;;) {
                uint64_t t147 = gm_l6;
                uint64_t t148 = gm_l5;
                uint8_t t149 = (t147 < t148);
                if (!t149) {
                    goto gm_lb1;
                }
                {
                    {
                        {
                            void * t150 = (void *)((uint8_t *)&F.l4);
                            gm_tf5srh4mp4b3i t151;
                            memset(&t151, 0, sizeof t151);
                            gm_tftb1odc739t5 t152 = gm_f_push_s4q134v(t150, t151);
                            gm_l82 = t152;
                            uint8_t t153 = (uint8_t)!((((uint8_t *)&gm_l82))[0] != 0);
                            if (!t153) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l82) + 1, gm_srco9vbah234t6e, 101, 35);
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 101, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t154 = gm_l6;
                uint64_t r155; bool o156 = __builtin_add_overflow(t154, ((uint64_t)1ULL), &r155);
                uint64_t t157 = r155;
                gm_l6 = t157;
            }
        }
gm_lb1:;
        gm_l8 = ((uint64_t)0ULL);
        void * t158 = gm_l2;
        void * t159 = GM_LD(void *, ((uint8_t *)t158));
        uint64_t t160 = GM_LD(uint64_t, ((((uint8_t *)t159) + 240) + 8));
        gm_l7 = t160;
        {
            for (;;) {
                uint64_t t162 = gm_l8;
                uint64_t t163 = gm_l7;
                uint8_t t164 = (t162 < t163);
                if (!t164) {
                    goto gm_lb6;
                }
                {
                    {
                        {
                            uint64_t t165 = gm_l8;
                            gm_l9 = t165;
                            {
                                {
                                    {
                                        void * t166 = gm_l2;
                                        uint64_t t167 = gm_l9;
                                        gm_f_state_a5bnsjb((void *)&gm_l83, (void *)NULL, cx, NULL, t166, t167);
                                        if (gm_unw.kind) {
                                            goto gm_ll161;
                                        }
                                    }
                                    uint8_t t168 = GM_LD(uint8_t, (((uint8_t *)&gm_l83) + 0));
                                    uint8_t t169 = (uint8_t)(t168 == ((uint8_t)2ULL));
                                    if (!t169) {
                                        goto gm_lb12;
                                    }
                                    {
                                        memmove(((uint8_t *)&F.l10), (((uint8_t *)&gm_l83) + 8), 208);
                                    }
                                    {
                                        {
                                            uint8_t t170 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                                            uint8_t t171 = (uint8_t)(t170 == ((uint8_t)5ULL));
                                            if (!t171) {
                                                goto gm_lb14;
                                            }
                                            uint32_t t172 = GM_LD(uint32_t, (((uint8_t *)&F.l10) + 8));
                                            gm_l11 = t172;
                                            void * t173 = GM_LD(void *, ((uint8_t *)&F.l4));
                                            uint64_t t174 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
                                            uint32_t t175 = gm_l11;
                                            uint64_t t176 = (uint64_t)t175;
                                            if ((uintptr_t)t176 >= (uintptr_t)(t174)) gm_trap_index((uintptr_t)t176, t174, gm_srco9vbah234t6e, 105, 20);
                                            {
                                                {
                                                    uint8_t t177 = (uint8_t)(((((uint8_t *)t173) + (uintptr_t)t176 * 16))[8] != 0);
                                                    if (t177) {
                                                        goto gm_lb16;
                                                    }
                                                    gm_l84 = ((uint8_t)1);
                                                    goto gm_lb15;
                                                }
gm_lb16:;
                                                {
                                                    gm_l84 = ((uint8_t)0);
                                                    goto gm_lb15;
                                                }
gm_lb17:;
                                                gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 105, 20);
                                            }
gm_lb15:;
                                            uint8_t t178 = gm_l84;
                                            if (t178) {
                                                void * t179 = GM_LD(void *, ((uint8_t *)&F.l4));
                                                uint64_t t180 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
                                                uint32_t t181 = gm_l11;
                                                uint64_t t182 = (uint64_t)t181;
                                                if ((uintptr_t)t182 >= (uintptr_t)(t180)) gm_trap_index((uintptr_t)t182, t180, gm_srco9vbah234t6e, 105, 54);
                                                uint64_t t183 = gm_l9;
                                                gm_tf5srh4mp4b3i t184;
                                                GM_ST(uint8_t, (uint8_t *)&t184 + 9, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t184 + 10, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t184 + 12, 0);
                                                GM_ST(uint64_t, ((uint8_t *)&t184), t183);
                                                ((uint8_t *)&t184)[8] = 1;
                                                GM_ST(gm_tf5srh4mp4b3i, (((uint8_t *)t179) + (uintptr_t)t182 * 16), t184);
                                            }
                                            goto gm_lb13;
                                        }
gm_lb14:;
                                    }
gm_lb13:;
                                    goto gm_lb11;
                                }
gm_lb12:;
                            }
gm_lb11:;
                            goto gm_lb9;
                        }
gm_lb10:;
                        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 102, 5);
                    }
gm_lb9:;
                }
gm_lb8:;
                uint64_t t185 = gm_l8;
                uint64_t r186; bool o187 = __builtin_add_overflow(t185, ((uint64_t)1ULL), &r186);
                uint64_t t188 = r186;
                gm_l8 = t188;
            }
        }
        goto gm_ls189;
gm_ll161:;
        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb6; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb8; }
        }
        goto gm_lad;
gm_ls189:;
gm_lb6:;
    }
    {
        gm_l13 = ((uint64_t)0ULL);
        void * t190 = gm_l2;
        uint64_t t191 = GM_LD(uint64_t, ((((uint8_t *)t190) + 384) + 8));
        gm_l12 = t191;
        {
            for (;;) {
                uint64_t t193 = gm_l13;
                uint64_t t194 = gm_l12;
                uint8_t t195 = (t193 < t194);
                if (!t195) {
                    goto gm_lb18;
                }
                {
                    {
                        {
                            uint64_t t196 = gm_l13;
                            gm_l14 = t196;
                            uint64_t t197 = gm_l14;
                            uint32_t t198 = (uint32_t)t197;
                            gm_l15 = t198;
                            void * t199 = gm_l2;
                            void * t200 = GM_LD(void *, (((uint8_t *)t199) + 344));
                            uint64_t t201 = GM_LD(uint64_t, ((((uint8_t *)t199) + 344) + 8));
                            uint64_t t202 = gm_l14;
                            if ((uintptr_t)t202 >= (uintptr_t)(t201)) gm_trap_index((uintptr_t)t202, t201, gm_srco9vbah234t6e, 112, 12);
                            {
                                {
                                    uint8_t t203 = (uint8_t)((((((uint8_t *)t200) + (uintptr_t)t202 * 136) + 96))[24] != 0);
                                    if (t203) {
                                        goto gm_lb24;
                                    }
                                    gm_l85 = ((uint8_t)0);
                                    goto gm_lb23;
                                }
gm_lb24:;
                                {
                                    gm_l85 = ((uint8_t)1);
                                    goto gm_lb23;
                                }
gm_lb25:;
                                gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 112, 12);
                            }
gm_lb23:;
                            uint8_t t204 = gm_l85;
                            if (t204) {
                                goto gm_lb20;
                            }
                            void * t205 = gm_l2;
                            void * t206 = GM_LD(void *, (((uint8_t *)t205) + 384));
                            uint64_t t207 = GM_LD(uint64_t, ((((uint8_t *)t205) + 384) + 8));
                            uint64_t t208 = gm_l14;
                            if ((uintptr_t)t208 >= (uintptr_t)(t207)) gm_trap_index((uintptr_t)t208, t207, gm_srco9vbah234t6e, 113, 17);
                            uint8_t t209 = (uint8_t)(GM_LD(void *, ((((uint8_t *)t206) + (uintptr_t)t208 * 64) + 40)) != 0);
                            if (t209) {
                                void * t210 = GM_LD(void *, ((((uint8_t *)t206) + (uintptr_t)t208 * 64) + 40));
                                gm_l16 = t210;
                            } else {
                                goto gm_lb20;
                            }
                            gm_trqfvmjtt4sij t211;
                            gm_f_list_go2qg0b((void *)&t211, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll192;
                            }
                            F.l17 = t211;
                            void * t212 = gm_l0;
                            uint32_t t213 = gm_l15;
                            void * t214 = gm_l16;
                            void * t215 = (void *)((uint8_t *)&F.l17);
                            gm_b0_1 t216 = gm_f_visit_unu9ms6((void *)NULL, cx, NULL, t212, t213, t214, t215);
                            if (gm_unw.kind) {
                                goto gm_ll192;
                            }
                            void * t217 = gm_l2;
                            void * t218 = (void *)(((uint8_t *)t217) + 2856);
                            uint32_t t219 = gm_l15;
                            gm_t9805qtonbsag t220 = gm_f_get_q3uemcr(t218, t219);
                            gm_l86 = t220;
                            uint8_t t221 = (uint8_t)((((uint8_t *)&gm_l86))[4] != 0);
                            if (t221) {
                                uint32_t t222 = GM_LD(uint32_t, ((uint8_t *)&gm_l86));
                                gm_l18 = t222;
                            } else {
                                uint32_t t223 = gm_l15;
                                gm_l18 = t223;
                            }
                            void * t224 = GM_LD(void *, ((uint8_t *)&F.l4));
                            uint64_t t225 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
                            uint32_t t226 = gm_l18;
                            uint64_t t227 = (uint64_t)t226;
                            if ((uintptr_t)t227 >= (uintptr_t)(t225)) gm_trap_index((uintptr_t)t227, t225, gm_srco9vbah234t6e, 117, 15);
                            gm_tf5srh4mp4b3i t228 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)t224) + (uintptr_t)t227 * 16));
                            gm_l19 = t228;
                            gm_b16_8 t229 = GM_LD(gm_b16_8, ((uint8_t *)&F.l17));
                            gm_l20 = t229;
                            gm_l21 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t231 = gm_l21;
                                    uint64_t t232 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                                    uint8_t t233 = (t231 < t232);
                                    if (!t233) {
                                        goto gm_lb26;
                                    }
                                    {
                                        void * t234 = GM_LD(void *, ((uint8_t *)&gm_l20));
                                        uint64_t t235 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                                        uint64_t t236 = gm_l21;
                                        {
                                            {
                                                uint32_t t237 = GM_LD(uint32_t, (((uint8_t *)t234) + (uintptr_t)t236 * 4));
                                                gm_l22 = t237;
                                                void * t238 = gm_l2;
                                                uint32_t t239 = gm_l22;
                                                uint32_t t240 = gm_f_generic_of_1861hm9(t238, t239);
                                                gm_l23 = t240;
                                                {
                                                    {
                                                        void * t241 = GM_LD(void *, ((uint8_t *)&F.l4));
                                                        uint64_t t242 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
                                                        uint32_t t243 = gm_l23;
                                                        uint64_t t244 = (uint64_t)t243;
                                                        if ((uintptr_t)t244 >= (uintptr_t)(t242)) gm_trap_index((uintptr_t)t244, t242, gm_srco9vbah234t6e, 120, 21);
                                                        uint8_t t245 = (uint8_t)(((((uint8_t *)t241) + (uintptr_t)t244 * 16))[8] != 0);
                                                        if (!t245) {
                                                            goto gm_lb32;
                                                        }
                                                        uint64_t t246 = GM_LD(uint64_t, (((uint8_t *)t241) + (uintptr_t)t244 * 16));
                                                        gm_l24 = t246;
                                                        gm_tf5srh4mp4b3i t247 = gm_l19;
                                                        uint64_t t248 = gm_l24;
                                                        gm_tf5srh4mp4b3i t249;
                                                        GM_ST(uint8_t, (uint8_t *)&t249 + 9, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t249 + 10, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&t249 + 12, 0);
                                                        GM_ST(uint64_t, ((uint8_t *)&t249), t248);
                                                        ((uint8_t *)&t249)[8] = 1;
                                                        uint8_t t250 = (!gm_eq7mri15hk78(((uint8_t *)&t247), ((uint8_t *)&t249)));
                                                        if (t250) {
                                                            void * t251 = gm_l0;
                                                            void * t252 = GM_LD(void *, (((uint8_t *)t251) + 72));
                                                            uint64_t t253 = GM_LD(uint64_t, ((((uint8_t *)t251) + 72) + 8));
                                                            uint64_t t254 = gm_l24;
                                                            if ((uintptr_t)t254 >= (uintptr_t)(t253)) gm_trap_index((uintptr_t)t254, t253, gm_srco9vbah234t6e, 121, 34);
                                                            GM_ST(uint8_t, (((uint8_t *)t252) + (uintptr_t)t254 * 1), ((uint8_t)1));
                                                        }
                                                        goto gm_lb31;
                                                    }
gm_lb32:;
                                                }
gm_lb31:;
                                                goto gm_lb29;
                                            }
gm_lb30:;
                                            gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 118, 9);
                                        }
gm_lb29:;
                                    }
gm_lb28:;
                                    uint64_t t255 = gm_l21;
                                    uint64_t r256; bool o257 = __builtin_add_overflow(t255, ((uint64_t)1ULL), &r256);
                                    uint64_t t258 = r256;
                                    gm_l21 = t258;
                                }
                            }
gm_lb26:;
                            goto gm_lb21;
                        }
gm_lb22:;
                        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 110, 5);
                    }
gm_lb21:;
                }
gm_lb20:;
                uint64_t t259 = gm_l13;
                uint64_t r260; bool o261 = __builtin_add_overflow(t259, ((uint64_t)1ULL), &r260);
                uint64_t t262 = r260;
                gm_l13 = t262;
            }
        }
        goto gm_ls263;
gm_ll192:;
        if (gm_unw.frame == &F.h && gm_unw.label == 2) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb18; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb20; }
        }
        goto gm_lad;
gm_ls263:;
gm_lb18:;
    }
    {
        gm_l26 = ((uint64_t)0ULL);
        void * t264 = gm_l2;
        uint64_t t265 = GM_LD(uint64_t, ((((uint8_t *)t264) + 2088) + 8));
        gm_l25 = t265;
        {
            for (;;) {
                uint64_t t267 = gm_l26;
                uint64_t t268 = gm_l25;
                uint8_t t269 = (t267 < t268);
                if (!t269) {
                    goto gm_lb33;
                }
                {
                    {
                        {
                            uint64_t t270 = gm_l26;
                            gm_l27 = t270;
                            void * t271 = gm_l2;
                            void * t272 = GM_LD(void *, (((uint8_t *)t271) + 2088));
                            uint64_t t273 = GM_LD(uint64_t, ((((uint8_t *)t271) + 2088) + 8));
                            uint64_t t274 = gm_l27;
                            if ((uintptr_t)t274 >= (uintptr_t)(t273)) gm_trap_index((uintptr_t)t274, t273, gm_srco9vbah234t6e, 126, 17);
                            uint8_t t275 = (uint8_t)(GM_LD(void *, ((((uint8_t *)t272) + (uintptr_t)t274 * 88) + 64)) != 0);
                            if (t275) {
                                void * t276 = GM_LD(void *, ((((uint8_t *)t272) + (uintptr_t)t274 * 88) + 64));
                                gm_l28 = t276;
                            } else {
                                goto gm_lb35;
                            }
                            gm_trqfvmjtt4sij t277;
                            gm_f_list_go2qg0b((void *)&t277, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll266;
                            }
                            F.l29 = t277;
                            void * t278 = gm_l0;
                            uint32_t t279 = GM_LD(uint32_t, ((uint8_t *)gm_c9b4lh7gft4));
                            void * t280 = gm_l28;
                            void * t281 = (void *)((uint8_t *)&F.l29);
                            gm_b0_1 t282 = gm_f_visit_unu9ms6((void *)NULL, cx, NULL, t278, t279, t280, t281);
                            if (gm_unw.kind) {
                                goto gm_ll266;
                            }
                            gm_b16_8 t283 = GM_LD(gm_b16_8, ((uint8_t *)&F.l29));
                            gm_l30 = t283;
                            gm_l31 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t285 = gm_l31;
                                    uint64_t t286 = GM_LD(uint64_t, (((uint8_t *)&gm_l30) + 8));
                                    uint8_t t287 = (t285 < t286);
                                    if (!t287) {
                                        goto gm_lb38;
                                    }
                                    {
                                        void * t288 = GM_LD(void *, ((uint8_t *)&gm_l30));
                                        uint64_t t289 = GM_LD(uint64_t, (((uint8_t *)&gm_l30) + 8));
                                        uint64_t t290 = gm_l31;
                                        {
                                            {
                                                uint32_t t291 = GM_LD(uint32_t, (((uint8_t *)t288) + (uintptr_t)t290 * 4));
                                                gm_l32 = t291;
                                                {
                                                    {
                                                        void * t292 = GM_LD(void *, ((uint8_t *)&F.l4));
                                                        uint64_t t293 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
                                                        void * t294 = gm_l2;
                                                        uint32_t t295 = gm_l32;
                                                        uint32_t t296 = gm_f_generic_of_1861hm9(t294, t295);
                                                        uint64_t t297 = (uint64_t)t296;
                                                        if ((uintptr_t)t297 >= (uintptr_t)(t293)) gm_trap_index((uintptr_t)t297, t293, gm_srco9vbah234t6e, 130, 21);
                                                        uint8_t t298 = (uint8_t)(((((uint8_t *)t292) + (uintptr_t)t297 * 16))[8] != 0);
                                                        if (!t298) {
                                                            goto gm_lb44;
                                                        }
                                                        uint64_t t299 = GM_LD(uint64_t, (((uint8_t *)t292) + (uintptr_t)t297 * 16));
                                                        gm_l33 = t299;
                                                        void * t300 = gm_l0;
                                                        void * t301 = GM_LD(void *, (((uint8_t *)t300) + 72));
                                                        uint64_t t302 = GM_LD(uint64_t, ((((uint8_t *)t300) + 72) + 8));
                                                        uint64_t t303 = gm_l33;
                                                        if ((uintptr_t)t303 >= (uintptr_t)(t302)) gm_trap_index((uintptr_t)t303, t302, gm_srco9vbah234t6e, 130, 72);
                                                        GM_ST(uint8_t, (((uint8_t *)t301) + (uintptr_t)t303 * 1), ((uint8_t)1));
                                                        goto gm_lb43;
                                                    }
gm_lb44:;
                                                }
gm_lb43:;
                                                goto gm_lb41;
                                            }
gm_lb42:;
                                            gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 129, 9);
                                        }
gm_lb41:;
                                    }
gm_lb40:;
                                    uint64_t t304 = gm_l31;
                                    uint64_t r305; bool o306 = __builtin_add_overflow(t304, ((uint64_t)1ULL), &r305);
                                    uint64_t t307 = r305;
                                    gm_l31 = t307;
                                }
                            }
gm_lb38:;
                            goto gm_lb36;
                        }
gm_lb37:;
                        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 125, 5);
                    }
gm_lb36:;
                }
gm_lb35:;
                uint64_t t308 = gm_l26;
                uint64_t r309; bool o310 = __builtin_add_overflow(t308, ((uint64_t)1ULL), &r309);
                uint64_t t311 = r309;
                gm_l26 = t311;
            }
        }
        goto gm_ls312;
gm_ll266:;
        if (gm_unw.frame == &F.h && gm_unw.label == 4) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb33; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb35; }
        }
        goto gm_lad;
gm_ls312:;
gm_lb33:;
    }
    {
        void * t313 = gm_l2;
        gm_b16_8 t314 = GM_LD(gm_b16_8, (((uint8_t *)t313) + 920));
        gm_l34 = t314;
        gm_l35 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t316 = gm_l35;
                uint64_t t317 = GM_LD(uint64_t, (((uint8_t *)&gm_l34) + 8));
                uint8_t t318 = (t316 < t317);
                if (!t318) {
                    goto gm_lb45;
                }
                {
                    void * t319 = GM_LD(void *, ((uint8_t *)&gm_l34));
                    uint64_t t320 = GM_LD(uint64_t, (((uint8_t *)&gm_l34) + 8));
                    uint64_t t321 = gm_l35;
                    {
                        {
                            uint32_t t322 = GM_LD(uint32_t, (((uint8_t *)t319) + (uintptr_t)t321 * 4));
                            gm_l36 = t322;
                            void * t323 = gm_l0;
                            void * t324 = GM_LD(void *, (((uint8_t *)t323) + 112));
                            uint64_t t325 = GM_LD(uint64_t, ((((uint8_t *)t323) + 112) + 8));
                            uint32_t t326 = gm_l36;
                            uint64_t t327 = (uint64_t)t326;
                            if ((uintptr_t)t327 >= (uintptr_t)(t325)) gm_trap_index((uintptr_t)t327, t325, gm_srco9vbah234t6e, 133, 35);
                            GM_ST(uint8_t, (((uint8_t *)t324) + (uintptr_t)t327 * 1), ((uint8_t)1));
                            goto gm_lb48;
                        }
gm_lb49:;
                        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 133, 5);
                    }
gm_lb48:;
                }
gm_lb47:;
                uint64_t t328 = gm_l35;
                uint64_t r329; bool o330 = __builtin_add_overflow(t328, ((uint64_t)1ULL), &r329);
                uint64_t t331 = r329;
                gm_l35 = t331;
            }
        }
gm_lb45:;
        void * t332 = gm_l2;
        gm_f_growth_summaries_dguakce((void *)&F.l37, (void *)NULL, cx, NULL, t332);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l39 = ((uint64_t)0ULL);
        void * t333 = gm_l2;
        uint64_t t334 = GM_LD(uint64_t, ((((uint8_t *)t333) + 384) + 8));
        gm_l38 = t334;
        {
            for (;;) {
                {
                    uint64_t t336 = gm_l39;
                    uint64_t t337 = gm_l38;
                    uint8_t t338 = (t336 < t337);
                    if (!t338) {
                        goto gm_lb50;
                    }
                    {
                        {
                            {
                                {
                                    {
                                        uint64_t t339 = gm_l39;
                                        gm_l40 = t339;
                                        uint64_t t340 = gm_l40;
                                        uint32_t t341 = (uint32_t)t340;
                                        gm_l41 = t341;
                                        void * t342 = gm_l2;
                                        void * t343 = GM_LD(void *, (((uint8_t *)t342) + 344));
                                        uint64_t t344 = GM_LD(uint64_t, ((((uint8_t *)t342) + 344) + 8));
                                        uint64_t t345 = gm_l40;
                                        if ((uintptr_t)t345 >= (uintptr_t)(t344)) gm_trap_index((uintptr_t)t345, t344, gm_srco9vbah234t6e, 138, 12);
                                        {
                                            {
                                                uint8_t t346 = (uint8_t)((((((uint8_t *)t343) + (uintptr_t)t345 * 136) + 96))[24] != 0);
                                                if (t346) {
                                                    goto gm_lb56;
                                                }
                                                gm_l89 = ((uint8_t)0);
                                                goto gm_lb55;
                                            }
gm_lb56:;
                                            {
                                                gm_l89 = ((uint8_t)1);
                                                goto gm_lb55;
                                            }
gm_lb57:;
                                            gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 138, 12);
                                        }
gm_lb55:;
                                        uint8_t t347 = gm_l89;
                                        if (t347) {
                                            gm_l88 = ((uint8_t)1);
                                        } else {
                                            void * t348 = gm_l2;
                                            void * t349 = GM_LD(void *, (((uint8_t *)t348) + 384));
                                            uint64_t t350 = GM_LD(uint64_t, ((((uint8_t *)t348) + 384) + 8));
                                            uint64_t t351 = gm_l40;
                                            if ((uintptr_t)t351 >= (uintptr_t)(t350)) gm_trap_index((uintptr_t)t351, t350, gm_srco9vbah234t6e, 138, 51);
                                            {
                                                {
                                                    uint8_t t352 = (uint8_t)(GM_LD(void *, ((((uint8_t *)t349) + (uintptr_t)t351 * 64) + 40)) != 0);
                                                    if (t352) {
                                                        goto gm_lb59;
                                                    }
                                                    gm_l90 = ((uint8_t)1);
                                                    goto gm_lb58;
                                                }
gm_lb59:;
                                                {
                                                    gm_l90 = ((uint8_t)0);
                                                    goto gm_lb58;
                                                }
gm_lb60:;
                                                gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 138, 51);
                                            }
gm_lb58:;
                                            uint8_t t353 = gm_l90;
                                            gm_l88 = t353;
                                        }
                                        uint8_t t354 = gm_l88;
                                        if (t354) {
                                            gm_l87 = ((uint8_t)1);
                                        } else {
                                            gm_closure t355 = gm_l3;
                                            void * t356 = gm_l2;
                                            void * t357 = GM_LD(void *, (((uint8_t *)t356) + 344));
                                            uint64_t t358 = GM_LD(uint64_t, ((((uint8_t *)t356) + 344) + 8));
                                            uint64_t t359 = gm_l40;
                                            if ((uintptr_t)t359 >= (uintptr_t)(t358)) gm_trap_index((uintptr_t)t359, t358, gm_srco9vbah234t6e, 138, 95);
                                            gm_tl874t2atnlh3 t360 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t357) + (uintptr_t)t359 * 136) + 16));
                                            gm_closure t361 = t355;
                                            gm_node *ccx362 = t361.cx;
                                            void * t363 = t361.code;
                                            uint8_t t364;
                                            if (GM_PTR_TAGGED(t363)) {
                                                t364 = ((uint8_t (*)(gm_cseahvg8v7p0vs))GM_PTR_UNTAG(t363))(GM_CONV(gm_cseahvg8v7p0vs, t360));
                                            } else {
                                                t364 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_tl874t2atnlh3))t363)(t361.env, ccx362, NULL, t360);
                                            }
                                            if (gm_unw.kind) {
                                                goto gm_ll335;
                                            }
                                            uint8_t t365 = (uint8_t)!t364;
                                            gm_l87 = t365;
                                        }
                                        uint8_t t366 = gm_l87;
                                        if (t366) {
                                            goto gm_lb52;
                                        }
                                        void * t367 = gm_l2;
                                        uint64_t t368 = gm_l40;
                                        gm_f_possible_dangling_addres_d403vr0((void *)&F.l42, (void *)NULL, cx, NULL, t367, t368);
                                        if (gm_unw.kind) {
                                            goto gm_ll335;
                                        }
                                        gm_b16_8 t369 = GM_LD(gm_b16_8, ((uint8_t *)&F.l42));
                                        gm_l43 = t369;
                                        gm_l44 = ((uint64_t)0ULL);
                                        {
                                            for (;;) {
                                                uint64_t t371 = gm_l44;
                                                uint64_t t372 = GM_LD(uint64_t, (((uint8_t *)&gm_l43) + 8));
                                                uint8_t t373 = (t371 < t372);
                                                if (!t373) {
                                                    goto gm_lb61;
                                                }
                                                {
                                                    void * t374 = GM_LD(void *, ((uint8_t *)&gm_l43));
                                                    uint64_t t375 = GM_LD(uint64_t, (((uint8_t *)&gm_l43) + 8));
                                                    uint64_t t376 = gm_l44;
                                                    {
                                                        {
                                                            memmove(((uint8_t *)&F.l45), (((uint8_t *)t374) + (uintptr_t)t376 * 72), 72);
                                                            uint8_t t377 = GM_LD(uint8_t, (((uint8_t *)&F.l45) + 64));
                                                            uint8_t t378 = (uint8_t)!t377;
                                                            if (t378) {
                                                                {
                                                                    void * t379 = gm_l0;
                                                                    void * t380 = (void *)(((uint8_t *)t379) + 208);
                                                                    gm_tl874t2atnlh3 t381 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&F.l45));
                                                                    gm_b16_8 t382 = GM_LD(gm_b16_8, (((uint8_t *)&F.l45) + 16));
                                                                    gm_tl874t2atnlh3 t383 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l45) + 32));
                                                                    gm_b16_8 t384 = GM_LD(gm_b16_8, (((uint8_t *)&F.l45) + 48));
                                                                    gm_tllos8achgacq t385;
                                                                    GM_ST(uint32_t, (uint8_t *)&t385 + 12, 0);
                                                                    GM_ST(uint32_t, (uint8_t *)&t385 + 44, 0);
                                                                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t385 + 0), t381);
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t385 + 16), t382);
                                                                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t385 + 32), t383);
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t385 + 48), t384);
                                                                    gm_tj5f8vajf35o7 t386;
                                                                    GM_ST(uint8_t, (uint8_t *)&t386 + 1, 0);
                                                                    GM_ST(uint16_t, (uint8_t *)&t386 + 2, 0);
                                                                    GM_ST(uint32_t, (uint8_t *)&t386 + 4, 0);
                                                                    GM_ST(uint8_t, ((uint8_t *)&t386 + 0), ((uint8_t)7ULL));
                                                                    GM_ST(gm_tllos8achgacq, ((uint8_t *)&t386 + 8), t385);
                                                                    gm_tftb1odc739t5 t387 = gm_f_push_rqlbv1e(t380, &t386);
                                                                    gm_l91 = t387;
                                                                }
                                                                uint8_t t388 = (uint8_t)!((((uint8_t *)&gm_l91))[0] != 0);
                                                                if (!t388) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l91) + 1, gm_srco9vbah234t6e, 141, 32);
                                                            }
                                                            goto gm_lb64;
                                                        }
gm_lb65:;
                                                        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 140, 9);
                                                    }
gm_lb64:;
                                                }
gm_lb63:;
                                                uint64_t t389 = gm_l44;
                                                uint64_t r390; bool o391 = __builtin_add_overflow(t389, ((uint64_t)1ULL), &r390);
                                                uint64_t t392 = r390;
                                                gm_l44 = t392;
                                            }
                                        }
gm_lb61:;
                                    }
                                    {
                                        void * t393 = gm_l2;
                                        uint64_t t394 = gm_l40;
                                        void * t395 = gm_l0;
                                        void * t396 = (void *)(((uint8_t *)t395) + 208);
                                        gm_b0_1 t397 = gm_f_mutated_copies_vc6lpss((void *)NULL, cx, NULL, t393, t394, t396);
                                        if (gm_unw.kind) {
                                            goto gm_ll335;
                                        }
                                        void * t398 = gm_l2;
                                        uint64_t t399 = gm_l40;
                                        gm_b16_8 t400 = GM_LD(gm_b16_8, ((uint8_t *)&F.l37));
                                        gm_b16_8 t401 = GM_LD(gm_b16_8, ((uint8_t *)&t400));
                                        void * t402 = gm_l0;
                                        void * t403 = (void *)(((uint8_t *)t402) + 208);
                                        gm_b0_1 t404 = gm_f_stale_element_pointers_jjgn0ll((void *)NULL, cx, NULL, t398, t399, t401, t403);
                                        if (gm_unw.kind) {
                                            goto gm_ll335;
                                        }
                                        void * t405 = gm_l2;
                                        void * t406 = GM_LD(void *, (((uint8_t *)t405) + 384));
                                        uint64_t t407 = GM_LD(uint64_t, ((((uint8_t *)t405) + 384) + 8));
                                        uint64_t t408 = gm_l40;
                                        if ((uintptr_t)t408 >= (uintptr_t)(t407)) gm_trap_index((uintptr_t)t408, t407, gm_srco9vbah234t6e, 145, 17);
                                        void * t409 = (void *)(((uint8_t *)t406) + (uintptr_t)t408 * 64);
                                        gm_l46 = t409;
                                        void * t410 = gm_l46;
                                        gm_t9qkh0rsooc8d t411 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)t410));
                                        uint8_t t412 = GM_LD(uint8_t, (((uint8_t *)&t411) + 0));
                                        uint8_t t413 = (uint8_t)((t412 == ((uint8_t)0ULL)));
                                        gm_t9qkh0rsooc8d t414;
                                        GM_ST(uint8_t, ((uint8_t *)&t414 + 0), ((uint8_t)0ULL));
                                        if (t413) {
                                            void * t415 = gm_l0;
                                            void * t416 = GM_LD(void *, (((uint8_t *)t415) + 112));
                                            uint64_t t417 = GM_LD(uint64_t, ((((uint8_t *)t415) + 112) + 8));
                                            uint64_t t418 = gm_l40;
                                            if ((uintptr_t)t418 >= (uintptr_t)(t417)) gm_trap_index((uintptr_t)t418, t417, gm_srco9vbah234t6e, 146, 50);
                                            uint8_t t419 = GM_LD(uint8_t, (((uint8_t *)t416) + (uintptr_t)t418 * 1));
                                            uint8_t t420 = (uint8_t)!t419;
                                            gm_l93 = t420;
                                        } else {
                                            gm_l93 = ((uint8_t)0);
                                        }
                                        uint8_t t421 = gm_l93;
                                        if (t421) {
                                            void * t422 = gm_l2;
                                            void * t423 = GM_LD(void *, (((uint8_t *)t422) + 344));
                                            uint64_t t424 = GM_LD(uint64_t, ((((uint8_t *)t422) + 344) + 8));
                                            uint64_t t425 = gm_l40;
                                            if ((uintptr_t)t425 >= (uintptr_t)(t424)) gm_trap_index((uintptr_t)t425, t424, gm_srco9vbah234t6e, 146, 74);
                                            gm_b16_8 t426 = GM_LD(gm_b16_8, (((uint8_t *)t423) + (uintptr_t)t425 * 136));
                                            gm_b16_8 t427;
                                            GM_ST(void *, (uint8_t *)&t427, (void *)(gm_sog4rksrmsqd2));
                                            GM_ST(uintptr_t, (uint8_t *)&t427 + 8, (uintptr_t)(4));
                                            uint8_t t428 = (!gm_eq0iqiubfm7e(((uint8_t *)&t426), ((uint8_t *)&t427)));
                                            gm_l92 = t428;
                                        } else {
                                            gm_l92 = ((uint8_t)0);
                                        }
                                        uint8_t t429 = gm_l92;
                                        gm_l47 = t429;
                                        void * t430 = gm_l46;
                                        gm_b16_8 t431 = GM_LD(gm_b16_8, (((uint8_t *)t430) + 24));
                                        gm_b16_8 t432 = GM_LD(gm_b16_8, ((uint8_t *)&t431));
                                        gm_l48 = t432;
                                        gm_l49 = ((uint64_t)0ULL);
                                        {
                                            for (;;) {
                                                uint64_t t434 = gm_l49;
                                                uint64_t t435 = GM_LD(uint64_t, (((uint8_t *)&gm_l48) + 8));
                                                uint8_t t436 = (t434 < t435);
                                                if (!t436) {
                                                    goto gm_lb66;
                                                }
                                                {
                                                    {
                                                        uint64_t t437 = gm_l49;
                                                        void * t438 = GM_LD(void *, ((uint8_t *)&gm_l48));
                                                        uint64_t t439 = GM_LD(uint64_t, (((uint8_t *)&gm_l48) + 8));
                                                        uint64_t t440 = gm_l49;
                                                        gm_tsoie1ppgdvcg t441 = GM_LD(gm_tsoie1ppgdvcg, (((uint8_t *)t438) + (uintptr_t)t440 * 40));
                                                        GM_ST(uint64_t, ((uint8_t *)&gm_l94 + 0), t437);
                                                        GM_ST(gm_tsoie1ppgdvcg, ((uint8_t *)&gm_l94 + 8), t441);
                                                    }
                                                    {
                                                        {
                                                            uint64_t t442 = GM_LD(uint64_t, ((uint8_t *)&gm_l94));
                                                            gm_l50 = t442;
                                                            memmove(((uint8_t *)&F.l51), (((uint8_t *)&gm_l94) + 8), 40);
                                                            uint64_t t443 = gm_l50;
                                                            uint32_t t444 = (uint32_t)t443;
                                                            gm_l52 = t444;
                                                            gm_tl874t2atnlh3 t445 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l51) + 24));
                                                            gm_tl874t2atnlh3 t446 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                                                            uint8_t t447 = (gm_eqff85ac10o3(((uint8_t *)&t445), ((uint8_t *)&t446)));
                                                            if (t447) {
                                                                gm_l97 = ((uint8_t)1);
                                                            } else {
                                                                gm_b16_8 t448 = GM_LD(gm_b16_8, ((uint8_t *)&F.l51));
                                                                gm_b16_8 t449;
                                                                GM_ST(void *, (uint8_t *)&t449, (void *)(gm_s1f440st4mnkp));
                                                                GM_ST(uintptr_t, (uint8_t *)&t449 + 8, (uintptr_t)(1));
                                                                uint8_t t450 = gm_f_starts_with_6vvt01e(t448, t449);
                                                                gm_l97 = t450;
                                                            }
                                                            uint8_t t451 = gm_l97;
                                                            if (t451) {
                                                                gm_l96 = ((uint8_t)1);
                                                            } else {
                                                                gm_b16_8 t452 = GM_LD(gm_b16_8, ((uint8_t *)&F.l51));
                                                                gm_b16_8 t453;
                                                                GM_ST(void *, (uint8_t *)&t453, (void *)(gm_sfdbfrv46lihd));
                                                                GM_ST(uintptr_t, (uint8_t *)&t453 + 8, (uintptr_t)(1));
                                                                uint8_t t454 = gm_f_starts_with_6vvt01e(t452, t453);
                                                                gm_l96 = t454;
                                                            }
                                                            uint8_t t455 = gm_l96;
                                                            if (t455) {
                                                                gm_l95 = ((uint8_t)1);
                                                            } else {
                                                                gm_closure t456 = gm_l3;
                                                                gm_tl874t2atnlh3 t457 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l51) + 24));
                                                                gm_closure t458 = t456;
                                                                gm_node *ccx459 = t458.cx;
                                                                void * t460 = t458.code;
                                                                uint8_t t461;
                                                                if (GM_PTR_TAGGED(t460)) {
                                                                    t461 = ((uint8_t (*)(gm_cseahvg8v7p0vs))GM_PTR_UNTAG(t460))(GM_CONV(gm_cseahvg8v7p0vs, t457));
                                                                } else {
                                                                    t461 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_tl874t2atnlh3))t460)(t458.env, ccx459, NULL, t457);
                                                                }
                                                                if (gm_unw.kind) {
                                                                    goto gm_ll433;
                                                                }
                                                                uint8_t t462 = (uint8_t)!t461;
                                                                gm_l95 = t462;
                                                            }
                                                            uint8_t t463 = gm_l95;
                                                            if (t463) {
                                                                goto gm_lb68;
                                                            }
                                                            uint32_t t464 = gm_l52;
                                                            void * t465 = gm_l46;
                                                            gm_b16_8 t466 = GM_LD(gm_b16_8, (((uint8_t *)t465) + 8));
                                                            uint8_t t467 = 0;
                                                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t466) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t466))) + i * 4, ((uint8_t *)&t464))) { t467 = 1; break; }
                                                            uint8_t t468 = (uint8_t)(t467);
                                                            gm_l53 = t468;
                                                            {
                                                                {
                                                                    uint8_t t469 = gm_l53;
                                                                    if (!t469) {
                                                                        goto gm_lb72;
                                                                    }
                                                                    uint8_t t470 = gm_l47;
                                                                    uint8_t t471 = (uint8_t)!t470;
                                                                    if (!t471) {
                                                                        goto gm_lb72;
                                                                    }
                                                                    goto gm_lb68;
                                                                }
gm_lb72:;
                                                            }
gm_lb71:;
                                                            {
                                                                {
                                                                    uint8_t t472 = gm_l53;
                                                                    uint8_t t473 = (uint8_t)!t472;
                                                                    if (!t473) {
                                                                        goto gm_lb74;
                                                                    }
                                                                    void * t474 = gm_l0;
                                                                    void * t475 = (void *)(((uint8_t *)t474) + 152);
                                                                    uint32_t t476 = gm_l41;
                                                                    uint32_t t477 = gm_l52;
                                                                    gm_tcc1rq4n1avb2 t478;
                                                                    GM_ST(uint32_t, ((uint8_t *)&t478 + 0), t476);
                                                                    GM_ST(uint32_t, ((uint8_t *)&t478 + 4), t477);
                                                                    uint8_t t479 = gm_f_contains_14ai471(t475, t478);
                                                                    uint8_t t480 = (uint8_t)!t479;
                                                                    if (!t480) {
                                                                        goto gm_lb74;
                                                                    }
                                                                    goto gm_lb68;
                                                                }
gm_lb74:;
                                                            }
gm_lb73:;
                                                            gm_l54 = ((uint64_t)0ULL);
                                                            gm_l55 = ((uint8_t)0);
                                                            {
                                                                {
                                                                    void * t481 = gm_l0;
                                                                    void * t482 = (void *)(((uint8_t *)t481) + 16);
                                                                    uint32_t t483 = gm_l41;
                                                                    uint32_t t484 = gm_l52;
                                                                    gm_tcc1rq4n1avb2 t485;
                                                                    GM_ST(uint32_t, ((uint8_t *)&t485 + 0), t483);
                                                                    GM_ST(uint32_t, ((uint8_t *)&t485 + 4), t484);
                                                                    void * t486 = gm_f_get_ptr_1a4voen(t482, t485);
                                                                    gm_l98 = t486;
                                                                    uint8_t t487 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l98)) != 0);
                                                                    if (!t487) {
                                                                        goto gm_lb76;
                                                                    }
                                                                    void * t488 = GM_LD(void *, ((uint8_t *)&gm_l98));
                                                                    gm_l56 = t488;
                                                                    void * t489 = gm_l56;
                                                                    uint64_t t490 = GM_LD(uint64_t, (((uint8_t *)t489) + 8));
                                                                    gm_l54 = t490;
                                                                    void * t491 = gm_l56;
                                                                    uint8_t t492 = GM_LD(uint8_t, (((uint8_t *)t491) + 40));
                                                                    gm_l55 = t492;
                                                                    goto gm_lb75;
                                                                }
gm_lb76:;
                                                            }
gm_lb75:;
                                                            gm_l57 = ((uint64_t)0ULL);
                                                            {
                                                                {
                                                                    void * t493 = gm_l2;
                                                                    void * t494 = (void *)(((uint8_t *)t493) + 2800);
                                                                    gm_tl874t2atnlh3 t495 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l51) + 24));
                                                                    gm_t9805qtonbsag t496 = gm_f_get_rlnefnq(t494, GM_CONV(gm_cseahvg8v7p0vs, t495));
                                                                    gm_l99 = t496;
                                                                    uint8_t t497 = (uint8_t)((((uint8_t *)&gm_l99))[4] != 0);
                                                                    if (!t497) {
                                                                        goto gm_lb78;
                                                                    }
                                                                    uint32_t t498 = GM_LD(uint32_t, ((uint8_t *)&gm_l99));
                                                                    gm_l58 = t498;
                                                                    uint32_t t499 = gm_l58;
                                                                    uint64_t t500 = (uint64_t)t499;
                                                                    gm_l57 = t500;
                                                                    goto gm_lb77;
                                                                }
gm_lb78:;
                                                            }
gm_lb77:;
                                                            uint64_t t501 = gm_l57;
                                                            uint64_t t502 = gm_l54;
                                                            uint8_t t503 = ((t501 == t502));
                                                            if (t503) {
                                                                {
                                                                    void * t504 = gm_l0;
                                                                    void * t505 = (void *)(((uint8_t *)t504) + 208);
                                                                    gm_b16_8 t506 = GM_LD(gm_b16_8, ((uint8_t *)&F.l51));
                                                                    gm_tl874t2atnlh3 t507 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l51) + 24));
                                                                    uint8_t t508 = gm_l53;
                                                                    uint64_t t509 = gm_l54;
                                                                    uint8_t t510 = (t509 > ((uint64_t)0ULL));
                                                                    gm_tppdoi1slt2pr t511;
                                                                    GM_ST(uint16_t, (uint8_t *)&t511 + 30, 0);
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t511 + 0), t506);
                                                                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t511 + 16), t507);
                                                                    GM_ST(uint8_t, ((uint8_t *)&t511 + 28), t508);
                                                                    GM_ST(uint8_t, ((uint8_t *)&t511 + 29), t510);
                                                                    gm_tj5f8vajf35o7 t512;
                                                                    memset(&t512, 0, sizeof t512);
                                                                    GM_ST(uint8_t, ((uint8_t *)&t512 + 0), ((uint8_t)0ULL));
                                                                    GM_ST(gm_tppdoi1slt2pr, ((uint8_t *)&t512 + 8), t511);
                                                                    gm_tftb1odc739t5 t513 = gm_f_push_rqlbv1e(t505, &t512);
                                                                    gm_l100 = t513;
                                                                }
                                                                uint8_t t514 = (uint8_t)!((((uint8_t *)&gm_l100))[0] != 0);
                                                                if (!t514) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l100) + 1, gm_srco9vbah234t6e, 162, 17);
                                                            } else {
                                                                {
                                                                    {
                                                                        uint8_t t515 = GM_LD(uint8_t, (((uint8_t *)&F.l51) + 20));
                                                                        if (!t515) {
                                                                            goto gm_lb80;
                                                                        }
                                                                        uint8_t t516 = gm_l53;
                                                                        uint8_t t517 = (uint8_t)!t516;
                                                                        if (!t517) {
                                                                            goto gm_lb80;
                                                                        }
                                                                        uint8_t t518 = gm_l55;
                                                                        uint8_t t519 = (uint8_t)!t518;
                                                                        if (!t519) {
                                                                            goto gm_lb80;
                                                                        }
                                                                        uint64_t t520 = gm_l54;
                                                                        uint8_t t521 = ((t520 == ((uint64_t)0ULL)));
                                                                        if (!t521) {
                                                                            goto gm_lb80;
                                                                        }
                                                                        {
                                                                            void * t522 = gm_l0;
                                                                            void * t523 = (void *)(((uint8_t *)t522) + 208);
                                                                            gm_b16_8 t524 = GM_LD(gm_b16_8, ((uint8_t *)&F.l51));
                                                                            gm_tl874t2atnlh3 t525 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l51) + 24));
                                                                            gm_t08olq97rscn3 t526;
                                                                            GM_ST(uint32_t, (uint8_t *)&t526 + 28, 0);
                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t526 + 0), t524);
                                                                            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t526 + 16), t525);
                                                                            gm_tj5f8vajf35o7 t527;
                                                                            memset(&t527, 0, sizeof t527);
                                                                            GM_ST(uint8_t, ((uint8_t *)&t527 + 0), ((uint8_t)1ULL));
                                                                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t527 + 8), t526);
                                                                            gm_tftb1odc739t5 t528 = gm_f_push_rqlbv1e(t523, &t527);
                                                                            gm_l101 = t528;
                                                                        }
                                                                        uint8_t t529 = (uint8_t)!((((uint8_t *)&gm_l101))[0] != 0);
                                                                        if (!t529) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l101) + 1, gm_srco9vbah234t6e, 164, 17);
                                                                        goto gm_lb79;
                                                                    }
gm_lb80:;
                                                                }
gm_lb79:;
                                                            }
                                                            goto gm_lb69;
                                                        }
gm_lb70:;
                                                        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 147, 9);
                                                    }
gm_lb69:;
                                                }
gm_lb68:;
                                                uint64_t t530 = gm_l49;
                                                uint64_t r531; bool o532 = __builtin_add_overflow(t530, ((uint64_t)1ULL), &r531);
                                                uint64_t t533 = r531;
                                                gm_l49 = t533;
                                            }
                                        }
                                        goto gm_ls534;
gm_ll433:;
                                        if (gm_unw.frame == &F.h && gm_unw.label == 9) {
                                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb66; }
                                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb68; }
                                        }
                                        goto gm_ll335;
gm_ls534:;
gm_lb66:;
                                    }
                                    {
                                        goto gm_lb53;
                                    }
                                }
gm_lb54:;
                            }
                            {
                                gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 136, 5);
                            }
                        }
gm_lb53:;
                    }
gm_lb52:;
                }
                {
                    uint64_t t535 = gm_l39;
                    uint64_t r536; bool o537 = __builtin_add_overflow(t535, ((uint64_t)1ULL), &r536);
                    uint64_t t538 = r536;
                    gm_l39 = t538;
                }
            }
        }
        goto gm_ls539;
gm_ll335:;
        if (gm_unw.frame == &F.h && gm_unw.label == 7) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb50; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb52; }
        }
        goto gm_lad;
gm_ls539:;
gm_lb50:;
    }
    {
        gm_l60 = ((uint64_t)0ULL);
        void * t540 = gm_l2;
        void * t541 = GM_LD(void *, ((uint8_t *)t540));
        uint64_t t542 = GM_LD(uint64_t, ((((uint8_t *)t541) + 240) + 8));
        gm_l59 = t542;
        {
            for (;;) {
                {
                    uint64_t t544 = gm_l60;
                    uint64_t t545 = gm_l59;
                    uint8_t t546 = (t544 < t545);
                    if (!t546) {
                        goto gm_lb81;
                    }
                    {
                        {
                            {
                                {
                                    {
                                        uint64_t t547 = gm_l60;
                                        gm_l61 = t547;
                                        void * t548 = gm_l2;
                                        void * t549 = GM_LD(void *, ((uint8_t *)t548));
                                        void * t550 = GM_LD(void *, (((uint8_t *)t549) + 240));
                                        uint64_t t551 = GM_LD(uint64_t, ((((uint8_t *)t549) + 240) + 8));
                                        uint64_t t552 = gm_l61;
                                        if ((uintptr_t)t552 >= (uintptr_t)(t551)) gm_trap_index((uintptr_t)t552, t551, gm_srco9vbah234t6e, 170, 15);
                                        void * t553 = (void *)(((uint8_t *)t550) + (uintptr_t)t552 * 216);
                                        gm_l62 = t553;
                                        void * t554 = gm_l62;
                                        gm_b16_8 t555 = GM_LD(gm_b16_8, ((uint8_t *)t554));
                                        gm_l63 = t555;
                                        {
                                            {
                                                {
                                                    void * t556 = gm_l0;
                                                    uint64_t t557 = gm_l61;
                                                    gm_b16_8 t558 = F.l1;
                                                    gm_f_needless_pub_jqsnq13((void *)&gm_l102, (void *)NULL, cx, NULL, t556, t557, t558);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll543;
                                                    }
                                                }
                                                uint8_t t559 = (uint8_t)((((uint8_t *)&gm_l102))[16] != 0);
                                                if (!t559) {
                                                    goto gm_lb87;
                                                }
                                                gm_b16_8 t560 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l102));
                                                gm_l64 = t560;
                                                {
                                                    void * t561 = gm_l0;
                                                    void * t562 = (void *)(((uint8_t *)t561) + 208);
                                                    void * t563 = gm_l62;
                                                    gm_b16_8 t564 = GM_LD(gm_b16_8, ((uint8_t *)t563));
                                                    void * t565 = gm_l62;
                                                    gm_tl874t2atnlh3 t566 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t565) + 16));
                                                    void * t567 = gm_l62;
                                                    gm_t9qkh0rsooc8d t568 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t567) + 40) + 56));
                                                    gm_b16_8 t569 = gm_l64;
                                                    gm_t26d2uc07m5o2 t570;
                                                    GM_ST(uint8_t, (uint8_t *)&t570 + 29, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t570 + 30, 0);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t570 + 0), t564);
                                                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t570 + 16), t566);
                                                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t570 + 28), t568);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t570 + 32), t569);
                                                    gm_tj5f8vajf35o7 t571;
                                                    GM_ST(uint8_t, (uint8_t *)&t571 + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t571 + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t571 + 4, 0);
                                                    GM_ST(uint64_t, (uint8_t *)&t571 + 56, 0);
                                                    GM_ST(uint64_t, (uint8_t *)&t571 + 64, 0);
                                                    GM_ST(uint8_t, ((uint8_t *)&t571 + 0), ((uint8_t)6ULL));
                                                    GM_ST(gm_t26d2uc07m5o2, ((uint8_t *)&t571 + 8), t570);
                                                    gm_tftb1odc739t5 t572 = gm_f_push_rqlbv1e(t562, &t571);
                                                    gm_l103 = t572;
                                                }
                                                uint8_t t573 = (uint8_t)!((((uint8_t *)&gm_l103))[0] != 0);
                                                if (!t573) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l103) + 1, gm_srco9vbah234t6e, 173, 13);
                                                goto gm_lb86;
                                            }
gm_lb87:;
                                        }
gm_lb86:;
                                        void * t574 = gm_l62;
                                        gm_t9qkh0rsooc8d t575 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t574) + 40) + 56));
                                        uint8_t t576 = GM_LD(uint8_t, (((uint8_t *)&t575) + 0));
                                        uint8_t t577 = (uint8_t)((t576 == ((uint8_t)2ULL)));
                                        gm_t9qkh0rsooc8d t578;
                                        GM_ST(uint8_t, ((uint8_t *)&t578 + 0), ((uint8_t)2ULL));
                                        if (t577) {
                                            gm_l109 = ((uint8_t)1);
                                        } else {
                                            gm_b16_8 t579 = gm_l63;
                                            gm_b16_8 t580;
                                            GM_ST(void *, (uint8_t *)&t580, (void *)(gm_sog4rksrmsqd2));
                                            GM_ST(uintptr_t, (uint8_t *)&t580 + 8, (uintptr_t)(4));
                                            uint8_t t581 = (gm_eq0iqiubfm7e(((uint8_t *)&t579), ((uint8_t *)&t580)));
                                            gm_l109 = t581;
                                        }
                                        uint8_t t582 = gm_l109;
                                        if (t582) {
                                            gm_l108 = ((uint8_t)1);
                                        } else {
                                            gm_b16_8 t583 = gm_l63;
                                            gm_b16_8 t584;
                                            GM_ST(void *, (uint8_t *)&t584, (void *)(gm_sfdbfrv46lihd));
                                            GM_ST(uintptr_t, (uint8_t *)&t584 + 8, (uintptr_t)(1));
                                            uint8_t t585 = gm_f_starts_with_6vvt01e(t583, t584);
                                            gm_l108 = t585;
                                        }
                                        uint8_t t586 = gm_l108;
                                        if (t586) {
                                            gm_l107 = ((uint8_t)1);
                                        } else {
                                            void * t587 = gm_l2;
                                            void * t588 = GM_LD(void *, ((uint8_t *)t587));
                                            void * t589 = GM_LD(void *, (((uint8_t *)t588) + 200));
                                            uint64_t t590 = GM_LD(uint64_t, ((((uint8_t *)t588) + 200) + 8));
                                            void * t591 = gm_l62;
                                            uint64_t t592 = GM_LD(uint64_t, (((uint8_t *)t591) + 32));
                                            if ((uintptr_t)t592 >= (uintptr_t)(t590)) gm_trap_index((uintptr_t)t592, t590, gm_srco9vbah234t6e, 175, 80);
                                            uint8_t t593 = GM_LD(uint8_t, ((((uint8_t *)t589) + (uintptr_t)t592 * 184) + 112));
                                            gm_l107 = t593;
                                        }
                                    }
                                    {
                                        uint8_t t594 = gm_l107;
                                        if (t594) {
                                            gm_l106 = ((uint8_t)1);
                                        } else {
                                            gm_closure t595 = gm_l3;
                                            void * t596 = gm_l62;
                                            gm_tl874t2atnlh3 t597 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t596) + 16));
                                            gm_closure t598 = t595;
                                            gm_node *ccx599 = t598.cx;
                                            void * t600 = t598.code;
                                            uint8_t t601;
                                            if (GM_PTR_TAGGED(t600)) {
                                                t601 = ((uint8_t (*)(gm_cseahvg8v7p0vs))GM_PTR_UNTAG(t600))(GM_CONV(gm_cseahvg8v7p0vs, t597));
                                            } else {
                                                t601 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_tl874t2atnlh3))t600)(t598.env, ccx599, NULL, t597);
                                            }
                                            if (gm_unw.kind) {
                                                goto gm_ll543;
                                            }
                                            uint8_t t602 = (uint8_t)!t601;
                                            gm_l106 = t602;
                                        }
                                        uint8_t t603 = gm_l106;
                                        if (t603) {
                                            gm_l105 = ((uint8_t)1);
                                        } else {
                                            void * t604 = gm_l2;
                                            uint64_t t605 = gm_l61;
                                            uint8_t t606 = gm_f_is_decl_used_oldf2ja(t604, t605);
                                            gm_l105 = t606;
                                        }
                                        uint8_t t607 = gm_l105;
                                        if (t607) {
                                            gm_l104 = ((uint8_t)1);
                                        } else {
                                            void * t608 = gm_l0;
                                            void * t609 = GM_LD(void *, (((uint8_t *)t608) + 72));
                                            uint64_t t610 = GM_LD(uint64_t, ((((uint8_t *)t608) + 72) + 8));
                                            uint64_t t611 = gm_l61;
                                            if ((uintptr_t)t611 >= (uintptr_t)(t610)) gm_trap_index((uintptr_t)t611, t610, gm_srco9vbah234t6e, 175, 171);
                                            uint8_t t612 = GM_LD(uint8_t, (((uint8_t *)t609) + (uintptr_t)t611 * 1));
                                            gm_l104 = t612;
                                        }
                                        uint8_t t613 = gm_l104;
                                        if (t613) {
                                            goto gm_lb83;
                                        }
                                        {
                                            {
                                                {
                                                    void * t614 = gm_l62;
                                                    uint8_t t615 = GM_LD(uint8_t, (((((uint8_t *)t614) + 40) + 64) + 0));
                                                    uint8_t t616 = (uint8_t)(t615 == ((uint8_t)0ULL));
                                                    if (!t616) {
                                                        goto gm_lb89;
                                                    }
                                                    memmove(((uint8_t *)&F.l65), (((((uint8_t *)t614) + 40) + 64) + 8), 64);
                                                }
                                                gm_l66 = ((uint8_t)0);
                                                void * t617 = gm_l62;
                                                gm_b16_8 t618 = GM_LD(gm_b16_8, ((((uint8_t *)t617) + 40) + 40));
                                                gm_l67 = t618;
                                                gm_l68 = ((uint64_t)0ULL);
                                                {
                                                    for (;;) {
                                                        uint64_t t620 = gm_l68;
                                                        uint64_t t621 = GM_LD(uint64_t, (((uint8_t *)&gm_l67) + 8));
                                                        uint8_t t622 = (t620 < t621);
                                                        if (!t622) {
                                                            goto gm_lb90;
                                                        }
                                                        {
                                                            void * t623 = GM_LD(void *, ((uint8_t *)&gm_l67));
                                                            uint64_t t624 = GM_LD(uint64_t, (((uint8_t *)&gm_l67) + 8));
                                                            uint64_t t625 = gm_l68;
                                                            {
                                                                {
                                                                    {
                                                                        memmove(((uint8_t *)&F.l69), (((uint8_t *)t623) + (uintptr_t)t625 * 72), 72);
                                                                    }
                                                                    {
                                                                        gm_b16_8 t626 = GM_LD(gm_b16_8, ((uint8_t *)&F.l69));
                                                                        gm_b16_8 t627;
                                                                        GM_ST(void *, (uint8_t *)&t627, (void *)(gm_spe8t3e79vnal));
                                                                        GM_ST(uintptr_t, (uint8_t *)&t627 + 8, (uintptr_t)(6));
                                                                        gm_b16_8 t628;
                                                                        GM_ST(void *, (uint8_t *)&t628, (void *)(gm_s1ft2nrvnhcbp));
                                                                        GM_ST(uintptr_t, (uint8_t *)&t628 + 8, (uintptr_t)(9));
                                                                        gm_t6j6smj7qoctu t629;
                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t629 + 0), t627);
                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t629 + 16), t628);
                                                                        uint8_t t630 = 0;
                                                                        if (gm_eq0iqiubfm7e(((uint8_t *)&t629) + 0, ((uint8_t *)&t626))) t630 = 1;
                                                                        if (gm_eq0iqiubfm7e(((uint8_t *)&t629) + 16, ((uint8_t *)&t626))) t630 = 1;
                                                                        uint8_t t631 = (uint8_t)(t630);
                                                                        if (t631) {
                                                                            gm_l66 = ((uint8_t)1);
                                                                        }
                                                                    }
                                                                    goto gm_lb93;
                                                                }
gm_lb94:;
                                                                gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 178, 9);
                                                            }
gm_lb93:;
                                                        }
gm_lb92:;
                                                        uint64_t t632 = gm_l68;
                                                        uint64_t r633; bool o634 = __builtin_add_overflow(t632, ((uint64_t)1ULL), &r633);
                                                        uint64_t t635 = r633;
                                                        gm_l68 = t635;
                                                    }
                                                }
gm_lb90:;
                                                uint8_t t636 = gm_l66;
                                                if (t636) {
                                                    goto gm_lb83;
                                                }
                                                {
                                                    void * t637 = gm_l2;
                                                    uint64_t t638 = gm_l61;
                                                    gm_f_state_a5bnsjb((void *)&F.l70, (void *)NULL, cx, NULL, t637, t638);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll543;
                                                    }
                                                }
                                                {
                                                    {
                                                        uint8_t t639 = GM_LD(uint8_t, (((uint8_t *)&F.l70) + 0));
                                                        uint8_t t640 = (uint8_t)(t639 == ((uint8_t)2ULL));
                                                        if (!t640) {
                                                            goto gm_lb96;
                                                        }
                                                        gm_l110 = ((uint8_t)0);
                                                        goto gm_lb95;
                                                    }
gm_lb96:;
                                                    {
                                                        gm_l110 = ((uint8_t)1);
                                                        goto gm_lb95;
                                                    }
gm_lb97:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 183, 12);
                                                }
gm_lb95:;
                                                uint8_t t641 = gm_l110;
                                                if (t641) {
                                                    goto gm_lb83;
                                                }
                                                gm_b16_8 t642;
                                                GM_ST(void *, (uint8_t *)&t642, (void *)(gm_svklio0qebpog));
                                                GM_ST(uintptr_t, (uint8_t *)&t642 + 8, (uintptr_t)(8));
                                                gm_l71 = t642;
                                                void * t643 = GM_LD(void *, (((uint8_t *)&F.l65) + 40));
                                                {
                                                    {
                                                        uint8_t t644 = GM_LD(uint8_t, ((((uint8_t *)t643) + 8) + 0));
                                                        uint8_t t645 = (uint8_t)(t644 == ((uint8_t)36ULL));
                                                        if (!t645) {
                                                            goto gm_lb99;
                                                        }
                                                        gm_b16_8 t646;
                                                        GM_ST(void *, (uint8_t *)&t646, (void *)(gm_sq0dqiq83b924));
                                                        GM_ST(uintptr_t, (uint8_t *)&t646 + 8, (uintptr_t)(8));
                                                        gm_l71 = t646;
                                                        goto gm_lb98;
                                                    }
gm_lb99:;
                                                    {
                                                        uint8_t t647 = GM_LD(uint8_t, ((((uint8_t *)t643) + 8) + 0));
                                                        uint8_t t648 = (uint8_t)(t647 == ((uint8_t)38ULL));
                                                        if (!t648) {
                                                            goto gm_lb100;
                                                        }
                                                        gm_b16_8 t649;
                                                        GM_ST(void *, (uint8_t *)&t649, (void *)(gm_sg3kpa3h16no3));
                                                        GM_ST(uintptr_t, (uint8_t *)&t649 + 8, (uintptr_t)(6));
                                                        gm_l71 = t649;
                                                        goto gm_lb98;
                                                    }
gm_lb100:;
                                                    {
                                                        uint8_t t650 = GM_LD(uint8_t, ((((uint8_t *)t643) + 8) + 0));
                                                        uint8_t t651 = (uint8_t)(t650 == ((uint8_t)39ULL));
                                                        if (!t651) {
                                                            goto gm_lb101;
                                                        }
                                                        gm_b16_8 t652;
                                                        GM_ST(void *, (uint8_t *)&t652, (void *)(gm_sa0amdcmvej1l));
                                                        GM_ST(uintptr_t, (uint8_t *)&t652 + 8, (uintptr_t)(13));
                                                        gm_l71 = t652;
                                                        goto gm_lb98;
                                                    }
gm_lb101:;
                                                    {
                                                        {
                                                            {
                                                                uint8_t t653 = GM_LD(uint8_t, ((((uint8_t *)t643) + 8) + 0));
                                                                uint8_t t654 = (uint8_t)(t653 == ((uint8_t)32ULL));
                                                                if (!t654) {
                                                                    goto gm_lb104;
                                                                }
                                                                goto gm_lb103;
                                                            }
gm_lb104:;
                                                            {
                                                                uint8_t t655 = GM_LD(uint8_t, ((((uint8_t *)t643) + 8) + 0));
                                                                uint8_t t656 = (uint8_t)(t655 == ((uint8_t)33ULL));
                                                                if (!t656) {
                                                                    goto gm_lb105;
                                                                }
                                                                goto gm_lb103;
                                                            }
gm_lb105:;
                                                            goto gm_lb102;
                                                        }
gm_lb103:;
                                                        gm_b16_8 t657;
                                                        GM_ST(void *, (uint8_t *)&t657, (void *)(gm_s2737i1iq57nf));
                                                        GM_ST(uintptr_t, (uint8_t *)&t657 + 8, (uintptr_t)(4));
                                                        gm_l71 = t657;
                                                        goto gm_lb98;
                                                    }
gm_lb102:;
                                                    {
                                                        uint8_t t658 = GM_LD(uint8_t, ((((uint8_t *)t643) + 8) + 0));
                                                        uint8_t t659 = (uint8_t)(t658 == ((uint8_t)34ULL));
                                                        if (!t659) {
                                                            goto gm_lb106;
                                                        }
                                                        gm_b16_8 t660;
                                                        GM_ST(void *, (uint8_t *)&t660, (void *)(gm_svqdrhms9i231));
                                                        GM_ST(uintptr_t, (uint8_t *)&t660 + 8, (uintptr_t)(6));
                                                        gm_l71 = t660;
                                                        goto gm_lb98;
                                                    }
gm_lb106:;
                                                    {
                                                        uint8_t t661 = GM_LD(uint8_t, ((((uint8_t *)t643) + 8) + 0));
                                                        uint8_t t662 = (uint8_t)(t661 == ((uint8_t)35ULL));
                                                        if (!t662) {
                                                            goto gm_lb107;
                                                        }
                                                        void * t663 = GM_LD(void *, (((((uint8_t *)t643) + 8) + 8) + 16));
                                                        gm_l72 = t663;
                                                        void * t664 = gm_l72;
                                                        {
                                                            {
                                                                uint8_t t665 = GM_LD(uint8_t, ((((uint8_t *)t664) + 8) + 0));
                                                                uint8_t t666 = (uint8_t)(t665 == ((uint8_t)32ULL));
                                                                if (!t666) {
                                                                    goto gm_lb109;
                                                                }
                                                                gm_l112 = ((uint8_t)1);
                                                                goto gm_lb108;
                                                            }
gm_lb109:;
                                                            {
                                                                gm_l112 = ((uint8_t)0);
                                                                goto gm_lb108;
                                                            }
gm_lb110:;
                                                            gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 191, 38);
                                                        }
gm_lb108:;
                                                        uint8_t t667 = gm_l112;
                                                        if (t667) {
                                                            gm_l111 = ((uint8_t)1);
                                                        } else {
                                                            void * t668 = gm_l72;
                                                            {
                                                                {
                                                                    uint8_t t669 = GM_LD(uint8_t, ((((uint8_t *)t668) + 8) + 0));
                                                                    uint8_t t670 = (uint8_t)(t669 == ((uint8_t)33ULL));
                                                                    if (!t670) {
                                                                        goto gm_lb112;
                                                                    }
                                                                    gm_l113 = ((uint8_t)1);
                                                                    goto gm_lb111;
                                                                }
gm_lb112:;
                                                                {
                                                                    gm_l113 = ((uint8_t)0);
                                                                    goto gm_lb111;
                                                                }
gm_lb113:;
                                                                gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 191, 66);
                                                            }
gm_lb111:;
                                                            uint8_t t671 = gm_l113;
                                                            gm_l111 = t671;
                                                        }
                                                        uint8_t t672 = gm_l111;
                                                        if (!t672) {
                                                            goto gm_lb107;
                                                        }
                                                        gm_b16_8 t673;
                                                        GM_ST(void *, (uint8_t *)&t673, (void *)(gm_s2737i1iq57nf));
                                                        GM_ST(uintptr_t, (uint8_t *)&t673 + 8, (uintptr_t)(4));
                                                        gm_l71 = t673;
                                                        goto gm_lb98;
                                                    }
gm_lb107:;
                                                    {
                                                        {
                                                            {
                                                                uint8_t t674 = GM_LD(uint8_t, (((uint8_t *)&F.l70) + 0));
                                                                uint8_t t675 = (uint8_t)(t674 == ((uint8_t)2ULL));
                                                                if (!t675) {
                                                                    goto gm_lb116;
                                                                }
                                                                memmove(((uint8_t *)&F.l73), (((uint8_t *)&F.l70) + 8), 208);
                                                                {
                                                                    {
                                                                        {
                                                                            {
                                                                                uint8_t t676 = GM_LD(uint8_t, (((uint8_t *)&F.l73) + 0));
                                                                                uint8_t t677 = (uint8_t)(t676 == ((uint8_t)0ULL));
                                                                                if (!t677) {
                                                                                    goto gm_lb120;
                                                                                }
                                                                                goto gm_lb119;
                                                                            }
gm_lb120:;
                                                                            {
                                                                                uint8_t t678 = GM_LD(uint8_t, (((uint8_t *)&F.l73) + 0));
                                                                                uint8_t t679 = (uint8_t)(t678 == ((uint8_t)1ULL));
                                                                                if (!t679) {
                                                                                    goto gm_lb121;
                                                                                }
                                                                                goto gm_lb119;
                                                                            }
gm_lb121:;
                                                                            goto gm_lb118;
                                                                        }
gm_lb119:;
                                                                        gm_b16_8 t680;
                                                                        GM_ST(void *, (uint8_t *)&t680, (void *)(gm_skr6p9lfc5ftp));
                                                                        GM_ST(uintptr_t, (uint8_t *)&t680 + 8, (uintptr_t)(10));
                                                                        gm_l71 = t680;
                                                                        goto gm_lb117;
                                                                    }
gm_lb118:;
                                                                    {
                                                                        {
                                                                            {
                                                                                uint8_t t681 = GM_LD(uint8_t, (((uint8_t *)&F.l73) + 0));
                                                                                uint8_t t682 = (uint8_t)(t681 == ((uint8_t)4ULL));
                                                                                if (!t682) {
                                                                                    goto gm_lb124;
                                                                                }
                                                                                goto gm_lb123;
                                                                            }
gm_lb124:;
                                                                            {
                                                                                uint8_t t683 = GM_LD(uint8_t, (((uint8_t *)&F.l73) + 0));
                                                                                uint8_t t684 = (uint8_t)(t683 == ((uint8_t)3ULL));
                                                                                if (!t684) {
                                                                                    goto gm_lb125;
                                                                                }
                                                                                goto gm_lb123;
                                                                            }
gm_lb125:;
                                                                            goto gm_lb122;
                                                                        }
gm_lb123:;
                                                                        gm_b16_8 t685;
                                                                        GM_ST(void *, (uint8_t *)&t685, (void *)(gm_svqdrhms9i231));
                                                                        GM_ST(uintptr_t, (uint8_t *)&t685 + 8, (uintptr_t)(6));
                                                                        gm_l71 = t685;
                                                                        goto gm_lb117;
                                                                    }
gm_lb122:;
                                                                    {
                                                                        uint8_t t686 = GM_LD(uint8_t, (((uint8_t *)&F.l73) + 0));
                                                                        uint8_t t687 = (uint8_t)(t686 == ((uint8_t)10ULL));
                                                                        if (!t687) {
                                                                            goto gm_lb126;
                                                                        }
                                                                        gm_b16_8 t688;
                                                                        GM_ST(void *, (uint8_t *)&t688, (void *)(gm_sptnm8c6c52dr));
                                                                        GM_ST(uintptr_t, (uint8_t *)&t688 + 8, (uintptr_t)(6));
                                                                        gm_l71 = t688;
                                                                        goto gm_lb117;
                                                                    }
gm_lb126:;
                                                                    {
                                                                        uint8_t t689 = GM_LD(uint8_t, (((uint8_t *)&F.l73) + 0));
                                                                        uint8_t t690 = (uint8_t)(t689 == ((uint8_t)9ULL));
                                                                        if (!t690) {
                                                                            goto gm_lb127;
                                                                        }
                                                                        gm_b16_8 t691;
                                                                        GM_ST(void *, (uint8_t *)&t691, (void *)(gm_sg3kpa3h16no3));
                                                                        GM_ST(uintptr_t, (uint8_t *)&t691 + 8, (uintptr_t)(6));
                                                                        gm_l71 = t691;
                                                                        goto gm_lb117;
                                                                    }
gm_lb127:;
                                                                    {
                                                                        goto gm_lb117;
                                                                    }
gm_lb128:;
                                                                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 194, 21);
                                                                }
gm_lb117:;
                                                                goto gm_lb115;
                                                            }
gm_lb116:;
                                                        }
gm_lb115:;
                                                        goto gm_lb98;
                                                    }
gm_lb114:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 185, 9);
                                                }
gm_lb98:;
                                                {
                                                    void * t692 = gm_l0;
                                                    void * t693 = (void *)(((uint8_t *)t692) + 208);
                                                    void * t694 = gm_l62;
                                                    gm_b16_8 t695 = GM_LD(gm_b16_8, ((uint8_t *)t694));
                                                    void * t696 = gm_l62;
                                                    gm_tl874t2atnlh3 t697 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t696) + 16));
                                                    void * t698 = gm_l62;
                                                    gm_tl874t2atnlh3 t699 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t698) + 40) + 4));
                                                    gm_b16_8 t700 = gm_l71;
                                                    gm_tt384v6n8ojib t701;
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t701 + 0), t695);
                                                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t701 + 16), t697);
                                                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t701 + 28), t699);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t701 + 40), t700);
                                                    gm_tj5f8vajf35o7 t702;
                                                    GM_ST(uint8_t, (uint8_t *)&t702 + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t702 + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t702 + 4, 0);
                                                    GM_ST(uint64_t, (uint8_t *)&t702 + 64, 0);
                                                    GM_ST(uint8_t, ((uint8_t *)&t702 + 0), ((uint8_t)2ULL));
                                                    GM_ST(gm_tt384v6n8ojib, ((uint8_t *)&t702 + 8), t701);
                                                    gm_tftb1odc739t5 t703 = gm_f_push_rqlbv1e(t693, &t702);
                                                    gm_l114 = t703;
                                                }
                                                uint8_t t704 = (uint8_t)!((((uint8_t *)&gm_l114))[0] != 0);
                                                if (!t704) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l114) + 1, gm_srco9vbah234t6e, 204, 9);
                                                goto gm_lb88;
                                            }
gm_lb89:;
                                            goto gm_lb83;
                                        }
gm_lb88:;
                                    }
                                    {
                                        goto gm_lb84;
                                    }
                                }
gm_lb85:;
                            }
                            {
                                gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 169, 5);
                            }
                        }
gm_lb84:;
                    }
gm_lb83:;
                }
                {
                    uint64_t t705 = gm_l60;
                    uint64_t r706; bool o707 = __builtin_add_overflow(t705, ((uint64_t)1ULL), &r706);
                    uint64_t t708 = r706;
                    gm_l60 = t708;
                }
            }
        }
        goto gm_ls709;
gm_ll543:;
        if (gm_unw.frame == &F.h && gm_unw.label == 10) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb81; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb83; }
        }
        goto gm_lad;
gm_ls709:;
gm_lb81:;
    }
    {
        gm_trqfvmjtt4sij t710;
        gm_f_list_1aqn740((void *)&t710, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l74 = t710;
        void * t711 = gm_l0;
        gm_b16_8 t712 = GM_LD(gm_b16_8, (((uint8_t *)t711) + 208));
        gm_l75 = t712;
        gm_l76 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t714 = gm_l76;
                uint64_t t715 = GM_LD(uint64_t, (((uint8_t *)&gm_l75) + 8));
                uint8_t t716 = (t714 < t715);
                if (!t716) {
                    goto gm_lb129;
                }
                {
                    void * t717 = GM_LD(void *, ((uint8_t *)&gm_l75));
                    uint64_t t718 = GM_LD(uint64_t, (((uint8_t *)&gm_l75) + 8));
                    uint64_t t719 = gm_l76;
                    {
                        {
                            memmove(((uint8_t *)&F.l77), (((uint8_t *)t717) + (uintptr_t)t719 * 72), 72);
                            {
                                {
                                    uint8_t t720 = GM_LD(uint8_t, (((uint8_t *)&F.l77) + 0));
                                    uint8_t t721 = (uint8_t)(t720 == ((uint8_t)3ULL));
                                    if (!t721) {
                                        goto gm_lb135;
                                    }
                                    gm_tl874t2atnlh3 t722 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l77) + 8));
                                    gm_l78 = t722;
                                    gm_closure t723 = gm_l3;
                                    gm_tl874t2atnlh3 t724 = gm_l78;
                                    gm_closure t725 = t723;
                                    gm_node *ccx726 = t725.cx;
                                    void * t727 = t725.code;
                                    uint8_t t728;
                                    if (GM_PTR_TAGGED(t727)) {
                                        t728 = ((uint8_t (*)(gm_cseahvg8v7p0vs))GM_PTR_UNTAG(t727))(GM_CONV(gm_cseahvg8v7p0vs, t724));
                                    } else {
                                        t728 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_tl874t2atnlh3))t727)(t725.env, ccx726, NULL, t724);
                                    }
                                    if (gm_unw.kind) {
                                        goto gm_ll713;
                                    }
                                    gm_l81 = t728;
                                    goto gm_lb134;
                                }
gm_lb135:;
                                {
                                    uint8_t t729 = GM_LD(uint8_t, (((uint8_t *)&F.l77) + 0));
                                    uint8_t t730 = (uint8_t)(t729 == ((uint8_t)4ULL));
                                    if (!t730) {
                                        goto gm_lb136;
                                    }
                                    gm_tl874t2atnlh3 t731 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l77) + 8));
                                    gm_l79 = t731;
                                    gm_closure t732 = gm_l3;
                                    gm_tl874t2atnlh3 t733 = gm_l79;
                                    gm_closure t734 = t732;
                                    gm_node *ccx735 = t734.cx;
                                    void * t736 = t734.code;
                                    uint8_t t737;
                                    if (GM_PTR_TAGGED(t736)) {
                                        t737 = ((uint8_t (*)(gm_cseahvg8v7p0vs))GM_PTR_UNTAG(t736))(GM_CONV(gm_cseahvg8v7p0vs, t733));
                                    } else {
                                        t737 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_tl874t2atnlh3))t736)(t734.env, ccx735, NULL, t733);
                                    }
                                    if (gm_unw.kind) {
                                        goto gm_ll713;
                                    }
                                    gm_l81 = t737;
                                    goto gm_lb134;
                                }
gm_lb136:;
                                {
                                    uint8_t t738 = GM_LD(uint8_t, (((uint8_t *)&F.l77) + 0));
                                    uint8_t t739 = (uint8_t)(t738 == ((uint8_t)5ULL));
                                    if (!t739) {
                                        goto gm_lb137;
                                    }
                                    gm_tl874t2atnlh3 t740 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l77) + 8));
                                    gm_l80 = t740;
                                    gm_closure t741 = gm_l3;
                                    gm_tl874t2atnlh3 t742 = gm_l80;
                                    gm_closure t743 = t741;
                                    gm_node *ccx744 = t743.cx;
                                    void * t745 = t743.code;
                                    uint8_t t746;
                                    if (GM_PTR_TAGGED(t745)) {
                                        t746 = ((uint8_t (*)(gm_cseahvg8v7p0vs))GM_PTR_UNTAG(t745))(GM_CONV(gm_cseahvg8v7p0vs, t742));
                                    } else {
                                        t746 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_tl874t2atnlh3))t745)(t743.env, ccx744, NULL, t742);
                                    }
                                    if (gm_unw.kind) {
                                        goto gm_ll713;
                                    }
                                    gm_l81 = t746;
                                    goto gm_lb134;
                                }
gm_lb137:;
                                {
                                    gm_l81 = ((uint8_t)1);
                                    goto gm_lb134;
                                }
gm_lb138:;
                                gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 208, 17);
                            }
gm_lb134:;
                            uint8_t t747 = gm_l81;
                            if (t747) {
                                void * t748 = (void *)((uint8_t *)&F.l74);
                                gm_tftb1odc739t5 t749 = gm_f_push_rqlbv1e(t748, &(*(gm_tj5f8vajf35o7 *)((uint8_t *)&F.l77)));
                                gm_l115 = t749;
                                uint8_t t750 = (uint8_t)!((((uint8_t *)&gm_l115))[0] != 0);
                                if (!t750) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l115) + 1, gm_srco9vbah234t6e, 214, 19);
                            }
                            goto gm_lb132;
                        }
gm_lb133:;
                        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 207, 5);
                    }
gm_lb132:;
                }
gm_lb131:;
                uint64_t t751 = gm_l76;
                uint64_t r752; bool o753 = __builtin_add_overflow(t751, ((uint64_t)1ULL), &r752);
                uint64_t t754 = r752;
                gm_l76 = t754;
            }
        }
        goto gm_ls755;
gm_ll713:;
        if (gm_unw.frame == &F.h && gm_unw.label == 12) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb129; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb131; }
        }
        goto gm_lad;
gm_ls755:;
gm_lb129:;
    }
    {
        void * t756 = gm_l0;
        memmove((((uint8_t *)t756) + 208), ((uint8_t *)&F.l74), 40);
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
inline __attribute__((aligned(4))) uint8_t gm_f_same_value_7pehjnc(void * a0, void * a1, void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_same_value_7pehjnc F;
    void * gm_l0; /* c */
    void * gm_l1; /* a */
    void * gm_l2; /* b */
    void * gm_l3; /* tys */
    gm_t88rcucg1b7uk gm_l4; /* t */
    uint8_t gm_l5; /* plain */
    GM_STACK_CHECK(&F, 225u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l6;
    uint8_t gm_l7;
    uint8_t gm_l8;
    void * t9 = gm_l0;
    void * t10 = GM_LD(void *, ((uint8_t *)t9));
    void * t11 = (void *)(((uint8_t *)t10) + 8);
    gm_l3 = t11;
    void * t12 = gm_l1;
    gm_t88rcucg1b7uk t13 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t12));
    gm_l4 = t13;
    void * t14 = gm_l3;
    gm_t88rcucg1b7uk t15 = gm_l4;
    uint8_t t16 = gm_f_is_integer_8d4f9ek(t14, GM_CONV(gm_csguankko4lmhd, t15));
    gm_l5 = t16;
    void * t17 = gm_l3;
    gm_t88rcucg1b7uk t18 = gm_l4;
    gm_f_kind_jcnt4hm((void *)&gm_l6, t17, GM_CONV(gm_csguankko4lmhd, t18));
    {
        {
            {
                {
                    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                    uint8_t t20 = (uint8_t)(t19 == ((uint8_t)4ULL));
                    if (!t20) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                    uint8_t t22 = (uint8_t)(t21 == ((uint8_t)8ULL));
                    if (!t22) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                    uint8_t t24 = (uint8_t)(t23 == ((uint8_t)9ULL));
                    if (!t24) {
                        goto gm_lb6;
                    }
                    goto gm_lb3;
                }
gm_lb6:;
                goto gm_lb2;
            }
gm_lb3:;
            gm_l5 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            goto gm_lb1;
        }
gm_lb7:;
        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 396, 5);
    }
gm_lb1:;
    uint8_t t25 = gm_l5;
    if (t25) {
        void * t26 = gm_l1;
        gm_tl874t2atnlh3 t27 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t26) + 72));
        void * t28 = gm_l2;
        gm_tl874t2atnlh3 t29 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t28) + 72));
        uint8_t t30 = (!gm_eqff85ac10o3(((uint8_t *)&t27), ((uint8_t *)&t29)));
        gm_l8 = t30;
    } else {
        gm_l8 = ((uint8_t)0);
    }
    uint8_t t31 = gm_l8;
    if (t31) {
        void * t32 = gm_l1;
        void * t33 = gm_l2;
        uint8_t t34 = gm_f_path_eq_l3qbb7v(t32, t33);
        gm_l7 = t34;
    } else {
        gm_l7 = ((uint8_t)0);
    }
    uint8_t t35 = gm_l7;
    F.ret = t35;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_use_of_q9agaul(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tcc1rq4n1avb2 a1) {
    struct gm_fr_use_of_q9agaul F;
    void * gm_l0; /* c */
    gm_tcc1rq4n1avb2 gm_l1; /* k */
    GM_STACK_CHECK(&F, 241u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    void * gm_l3;
    gm_tftb1odc739t5 gm_l4;
    void * gm_l5;
    void * t5 = gm_l0;
    void * t6 = (void *)(((uint8_t *)t5) + 16);
    gm_tcc1rq4n1avb2 t7 = gm_l1;
    void * t8 = gm_f_get_ptr_1a4voen(t6, t7);
    gm_l3 = t8;
    {
        {
            uint8_t t9 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l3)) != 0);
            if (t9) {
                goto gm_lb2;
            }
            gm_l2 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l2 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 276, 8);
    }
gm_lb1:;
    uint8_t t10 = gm_l2;
    if (t10) {
        {
            void * t11 = gm_l0;
            void * t12 = (void *)(((uint8_t *)t11) + 16);
            gm_tcc1rq4n1avb2 t13 = gm_l1;
            gm_trqfvmjtt4sij t14;
            gm_f_list_7s4om10((void *)&t14, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tqetrgo0q24b5 t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 41, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 42, 0);
            GM_ST(uint32_t, (uint8_t *)&t15 + 44, 0);
            GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t15 + 0), t14);
            GM_ST(uint8_t, ((uint8_t *)&t15 + 40), ((uint8_t)0));
            gm_tftb1odc739t5 t16 = gm_f_put_he2q4rq(t12, t13, &t15);
            gm_l4 = t16;
        }
        uint8_t t17 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
        if (!t17) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srco9vbah234t6e, 276, 36);
    }
    void * t18 = gm_l0;
    void * t19 = (void *)(((uint8_t *)t18) + 16);
    gm_tcc1rq4n1avb2 t20 = gm_l1;
    void * t21 = gm_f_get_ptr_1a4voen(t19, t20);
    gm_l5 = t21;
    uint8_t t22 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l5)) != 0);
    if (!t22) gm_trap("unwrap of null", gm_srco9vbah234t6e, 277, 12);
    void * t23 = GM_LD(void *, ((uint8_t *)&gm_l5));
    F.ret = t23;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_visit_unu9ms6(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, void * a3) {
    struct gm_fr_visit_unu9ms6 F;
    void * gm_l2; /* e */
    void * gm_l4; /* tys */
    void * gm_l5; /* p */
    uint8_t gm_l6; /* is_const */
    gm_tao63pk3r18v5 gm_l7; /* sl */
    uint8_t gm_l8; /* is_const */
    uint32_t gm_l10; /* g */
    uint32_t gm_l11; /* g */
    uint32_t gm_l12; /* g */
    void * gm_l13; /* b */
    gm_b16_8 gm_l14; /* arms */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_tbbqefuhv2r8h gm_l17; /* a */
    gm_trl2khmuphncd gm_l18; /* x */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    gm_tlr9dp305htm7 gm_l21; /* cp */
    void * gm_l22; /* pat */
    gm_b16_8 gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    gm_tlr9dp305htm7 gm_l26; /* cp */
    void * gm_l27; /* pat */
    gm_t3dcoc7077t1q gm_l28; /* x */
    uint32_t gm_l29; /* l */
    void * gm_l30; /* lhs */
    void * gm_l31; /* rhs */
    uint8_t gm_l32; /* negated */
    gm_tqs5p4n2tq19v gm_l33; /* lb */
    gm_tqs5p4n2tq19v gm_l34; /* rb */
    uint8_t gm_l35; /* b */
    void * gm_l36; /* other */
    gm_ts6oqppjh0ba3 gm_l37; /* op */
    void * gm_l38; /* lhs */
    void * gm_l39; /* rhs */
    GM_STACK_CHECK(&F, 2082u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    gm_l2 = a2;
    F.l3 = a3;
    gm_t53at80pfdus3 gm_l41;
    gm_t53at80pfdus3 gm_l42;
    gm_tftb1odc739t5 gm_l43;
    gm_tftb1odc739t5 gm_l44;
    gm_tftb1odc739t5 gm_l45;
    uint8_t gm_l46;
    uint8_t gm_l47;
    uint8_t gm_l48;
    gm_tftb1odc739t5 gm_l49;
    gm_tftb1odc739t5 gm_l50;
    gm_tftb1odc739t5 gm_l51;
    {
        void * t60 = F.l0;
        void * t61 = GM_LD(void *, ((uint8_t *)t60));
        void * t62 = (void *)(((uint8_t *)t61) + 8);
        gm_l4 = t62;
        void * t63 = gm_l2;
        {
            {
                {
                    uint8_t t64 = GM_LD(uint8_t, ((((uint8_t *)t63) + 8) + 0));
                    uint8_t t65 = (uint8_t)(t64 == ((uint8_t)24ULL));
                    if (!t65) {
                        goto gm_lb2;
                    }
                    void * t66 = GM_LD(void *, ((((uint8_t *)t63) + 8) + 8));
                    gm_l5 = t66;
                    {
                        {
                            void * t67 = gm_l4;
                            void * t68 = gm_l2;
                            gm_t88rcucg1b7uk t69 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t68));
                            gm_f_kind_jcnt4hm((void *)&gm_l41, t67, GM_CONV(gm_csguankko4lmhd, t69));
                            uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l41) + 0));
                            uint8_t t71 = (uint8_t)(t70 == ((uint8_t)8ULL));
                            if (!t71) {
                                goto gm_lb4;
                            }
                            uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&gm_l41) + 8));
                            gm_l6 = t72;
                            uint8_t t73 = gm_l6;
                            uint8_t t74 = (uint8_t)!t73;
                            if (t74) {
                                void * t75 = F.l0;
                                uint32_t t76 = F.l1;
                                void * t77 = gm_l5;
                                gm_b0_1 t78 = gm_f_mutate_oi1s4vs((void *)NULL, cx, NULL, t75, t76, t77);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            goto gm_lb3;
                        }
gm_lb4:;
                    }
gm_lb3:;
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    uint8_t t79 = GM_LD(uint8_t, ((((uint8_t *)t63) + 8) + 0));
                    uint8_t t80 = (uint8_t)(t79 == ((uint8_t)21ULL));
                    if (!t80) {
                        goto gm_lb5;
                    }
                    memmove(((uint8_t *)&gm_l7), ((((uint8_t *)t63) + 8) + 8), 32);
                    {
                        {
                            void * t81 = gm_l4;
                            void * t82 = gm_l2;
                            gm_t88rcucg1b7uk t83 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t82));
                            gm_f_kind_jcnt4hm((void *)&gm_l42, t81, GM_CONV(gm_csguankko4lmhd, t83));
                            uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&gm_l42) + 0));
                            uint8_t t85 = (uint8_t)(t84 == ((uint8_t)10ULL));
                            if (!t85) {
                                goto gm_lb7;
                            }
                            uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&gm_l42) + 8));
                            gm_l8 = t86;
                            uint8_t t87 = gm_l8;
                            uint8_t t88 = (uint8_t)!t87;
                            if (t88) {
                                void * t89 = F.l0;
                                uint32_t t90 = F.l1;
                                void * t91 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                gm_b0_1 t92 = gm_f_mutate_oi1s4vs((void *)NULL, cx, NULL, t89, t90, t91);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            goto gm_lb6;
                        }
gm_lb7:;
                    }
gm_lb6:;
                    goto gm_lb1;
                }
gm_lb5:;
                {
                    uint8_t t93 = GM_LD(uint8_t, ((((uint8_t *)t63) + 8) + 0));
                    uint8_t t94 = (uint8_t)(t93 == ((uint8_t)34ULL));
                    if (!t94) {
                        goto gm_lb8;
                    }
                    memmove(((uint8_t *)&F.l9), ((((uint8_t *)t63) + 8) + 8), 40);
                    {
                        {
                            uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 0));
                            uint8_t t96 = (uint8_t)(t95 == ((uint8_t)0ULL));
                            if (!t96) {
                                goto gm_lb10;
                            }
                            uint32_t t97 = GM_LD(uint32_t, (((uint8_t *)&F.l9) + 8));
                            gm_l10 = t97;
                            void * t98 = F.l3;
                            uint32_t t99 = gm_l10;
                            gm_tftb1odc739t5 t100 = gm_f_push_im9d4ro(t98, t99);
                            gm_l43 = t100;
                            uint8_t t101 = (uint8_t)!((((uint8_t *)&gm_l43))[0] != 0);
                            if (!t101) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l43) + 1, gm_srco9vbah234t6e, 291, 51);
                            goto gm_lb9;
                        }
gm_lb10:;
                    }
gm_lb9:;
                    goto gm_lb1;
                }
gm_lb8:;
            }
            {
                {
                    {
                        {
                            uint8_t t102 = GM_LD(uint8_t, ((((uint8_t *)t63) + 8) + 0));
                            uint8_t t103 = (uint8_t)(t102 == ((uint8_t)26ULL));
                            if (!t103) {
                                goto gm_lb13;
                            }
                            uint32_t t104 = GM_LD(uint32_t, ((((uint8_t *)t63) + 8) + 8));
                            gm_l11 = t104;
                            goto gm_lb12;
                        }
gm_lb13:;
                        {
                            uint8_t t105 = GM_LD(uint8_t, ((((uint8_t *)t63) + 8) + 0));
                            uint8_t t106 = (uint8_t)(t105 == ((uint8_t)27ULL));
                            if (!t106) {
                                goto gm_lb14;
                            }
                            uint32_t t107 = GM_LD(uint32_t, ((((uint8_t *)t63) + 8) + 8));
                            gm_l11 = t107;
                            goto gm_lb12;
                        }
gm_lb14:;
                        goto gm_lb11;
                    }
gm_lb12:;
                    void * t108 = F.l3;
                    uint32_t t109 = gm_l11;
                    gm_tftb1odc739t5 t110 = gm_f_push_im9d4ro(t108, t109);
                    gm_l44 = t110;
                    uint8_t t111 = (uint8_t)!((((uint8_t *)&gm_l44))[0] != 0);
                    if (!t111) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l44) + 1, gm_srco9vbah234t6e, 293, 13);
                    void * t112 = F.l0;
                    void * t113 = GM_LD(void *, (((uint8_t *)t112) + 112));
                    uint64_t t114 = GM_LD(uint64_t, ((((uint8_t *)t112) + 112) + 8));
                    void * t115 = F.l0;
                    void * t116 = GM_LD(void *, ((uint8_t *)t115));
                    uint32_t t117 = gm_l11;
                    uint32_t t118 = gm_f_generic_of_1861hm9(t116, t117);
                    uint64_t t119 = (uint64_t)t118;
                    if ((uintptr_t)t119 >= (uintptr_t)(t114)) gm_trap_index((uintptr_t)t119, t114, gm_srco9vbah234t6e, 294, 13);
                    GM_ST(uint8_t, (((uint8_t *)t113) + (uintptr_t)t119 * 1), ((uint8_t)1));
                    goto gm_lb1;
                }
gm_lb11:;
                {
                    uint8_t t120 = GM_LD(uint8_t, ((((uint8_t *)t63) + 8) + 0));
                    uint8_t t121 = (uint8_t)(t120 == ((uint8_t)46ULL));
                    if (!t121) {
                        goto gm_lb15;
                    }
                    void * t122 = GM_LD(void *, ((((uint8_t *)t63) + 8) + 8));
                    gm_l13 = t122;
                    void * t123 = F.l0;
                    uint32_t t124 = F.l1;
                    void * t125 = gm_l13;
                    gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)t125));
                    gm_b16_8 t127 = GM_LD(gm_b16_8, ((uint8_t *)&t126));
                    gm_b0_1 t128 = gm_f_block_stmts_c2nnk6m((void *)NULL, cx, NULL, t123, t124, t127);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb15:;
                {
                    uint8_t t129 = GM_LD(uint8_t, ((((uint8_t *)t63) + 8) + 0));
                    uint8_t t130 = (uint8_t)(t129 == ((uint8_t)44ULL));
                    if (!t130) {
                        goto gm_lb16;
                    }
                    gm_b16_8 t131 = GM_LD(gm_b16_8, (((((uint8_t *)t63) + 8) + 8) + 8));
                    gm_l14 = t131;
                    gm_b16_8 t132 = gm_l14;
                    gm_l15 = t132;
                    gm_l16 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t134 = gm_l16;
                            uint64_t t135 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                            uint8_t t136 = (t134 < t135);
                            if (!t136) {
                                goto gm_lb17;
                            }
                            {
                                void * t137 = GM_LD(void *, ((uint8_t *)&gm_l15));
                                uint64_t t138 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                uint64_t t139 = gm_l16;
                                {
                                    {
                                        memmove(((uint8_t *)&gm_l17), (((uint8_t *)t137) + (uintptr_t)t139 * 24), 24);
                                        void * t140 = F.l0;
                                        uint32_t t141 = F.l1;
                                        void * t142 = GM_LD(void *, ((uint8_t *)&gm_l17));
                                        gm_b0_1 t143 = gm_f_binds_busqeo0((void *)NULL, cx, NULL, t140, t141, t142);
                                        if (gm_unw.kind) {
                                            goto gm_ll133;
                                        }
                                        goto gm_lb20;
                                    }
gm_lb21:;
                                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 298, 13);
                                }
gm_lb20:;
                            }
gm_lb19:;
                            uint64_t t144 = gm_l16;
                            uint64_t r145; bool o146 = __builtin_add_overflow(t144, ((uint64_t)1ULL), &r145);
                            uint64_t t147 = r145;
                            gm_l16 = t147;
                        }
                    }
                    goto gm_ls148;
gm_ll133:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb17; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb19; }
                    }
                    goto gm_lad;
gm_ls148:;
gm_lb17:;
                    uint32_t t149 = F.l1;
                    uint64_t t150 = (uint64_t)t149;
                    void * t151 = F.l0;
                    void * t152 = GM_LD(void *, ((uint8_t *)t151));
                    uint64_t t153 = GM_LD(uint64_t, ((((uint8_t *)t152) + 384) + 8));
                    uint8_t t154 = (t150 < t153);
                    if (t154) {
                        void * t155 = F.l0;
                        void * t156 = GM_LD(void *, ((uint8_t *)t155));
                        void * t157 = GM_LD(void *, (((uint8_t *)t156) + 384));
                        uint64_t t158 = GM_LD(uint64_t, ((((uint8_t *)t156) + 384) + 8));
                        uint32_t t159 = F.l1;
                        uint64_t t160 = (uint64_t)t159;
                        if ((uintptr_t)t160 >= (uintptr_t)(t158)) gm_trap_index((uintptr_t)t160, t158, gm_srco9vbah234t6e, 299, 71);
                        void * t161 = (void *)(((uint8_t *)t157) + (uintptr_t)t160 * 64);
                        gm_b16_8 t162 = gm_l14;
                        gm_b16_8 t163 = GM_LD(gm_b16_8, ((uint8_t *)&t162));
                        void * t164 = F.l0;
                        void * t165 = (void *)(((uint8_t *)t164) + 208);
                        gm_b0_1 t166 = gm_f_present_only_bindings_ug2i6dq((void *)NULL, cx, NULL, t161, t163, t165);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto gm_lb1;
                }
gm_lb16:;
            }
            {
                {
                    uint8_t t167 = GM_LD(uint8_t, ((((uint8_t *)t63) + 8) + 0));
                    uint8_t t168 = (uint8_t)(t167 == ((uint8_t)47ULL));
                    if (!t168) {
                        goto gm_lb22;
                    }
                    memmove(((uint8_t *)&gm_l18), ((((uint8_t *)t63) + 8) + 8), 32);
                    gm_b16_8 t169 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l18));
                    gm_l19 = t169;
                    gm_l20 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t171 = gm_l20;
                            uint64_t t172 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                            uint8_t t173 = (t171 < t172);
                            if (!t173) {
                                goto gm_lb23;
                            }
                            {
                                void * t174 = GM_LD(void *, ((uint8_t *)&gm_l19));
                                uint64_t t175 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                                uint64_t t176 = gm_l20;
                                {
                                    {
                                        memmove(((uint8_t *)&gm_l21), (((uint8_t *)t174) + (uintptr_t)t176 * 24), 24);
                                        {
                                            {
                                                uint8_t t177 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
                                                uint8_t t178 = (uint8_t)(t177 == ((uint8_t)1ULL));
                                                if (!t178) {
                                                    goto gm_lb29;
                                                }
                                                void * t179 = GM_LD(void *, (((uint8_t *)&gm_l21) + 8));
                                                gm_l22 = t179;
                                                void * t180 = F.l0;
                                                uint32_t t181 = F.l1;
                                                void * t182 = gm_l22;
                                                gm_b0_1 t183 = gm_f_binds_busqeo0((void *)NULL, cx, NULL, t180, t181, t182);
                                                if (gm_unw.kind) {
                                                    goto gm_ll170;
                                                }
                                                goto gm_lb28;
                                            }
gm_lb29:;
                                        }
gm_lb28:;
                                        goto gm_lb26;
                                    }
gm_lb27:;
                                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 302, 13);
                                }
gm_lb26:;
                            }
gm_lb25:;
                            uint64_t t184 = gm_l20;
                            uint64_t r185; bool o186 = __builtin_add_overflow(t184, ((uint64_t)1ULL), &r185);
                            uint64_t t187 = r185;
                            gm_l20 = t187;
                        }
                    }
                    goto gm_ls188;
gm_ll170:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb23; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb25; }
                    }
                    goto gm_lad;
gm_ls188:;
gm_lb23:;
                    goto gm_lb1;
                }
gm_lb22:;
                {
                    uint8_t t189 = GM_LD(uint8_t, ((((uint8_t *)t63) + 8) + 0));
                    uint8_t t190 = (uint8_t)(t189 == ((uint8_t)48ULL));
                    if (!t190) {
                        goto gm_lb30;
                    }
                    memmove(((uint8_t *)&F.l23), ((((uint8_t *)t63) + 8) + 8), 40);
                    gm_b16_8 t191 = GM_LD(gm_b16_8, (((uint8_t *)&F.l23) + 8));
                    gm_l24 = t191;
                    gm_l25 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t193 = gm_l25;
                            uint64_t t194 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                            uint8_t t195 = (t193 < t194);
                            if (!t195) {
                                goto gm_lb31;
                            }
                            {
                                void * t196 = GM_LD(void *, ((uint8_t *)&gm_l24));
                                uint64_t t197 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                                uint64_t t198 = gm_l25;
                                {
                                    {
                                        memmove(((uint8_t *)&gm_l26), (((uint8_t *)t196) + (uintptr_t)t198 * 24), 24);
                                        {
                                            {
                                                uint8_t t199 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                                                uint8_t t200 = (uint8_t)(t199 == ((uint8_t)1ULL));
                                                if (!t200) {
                                                    goto gm_lb37;
                                                }
                                                void * t201 = GM_LD(void *, (((uint8_t *)&gm_l26) + 8));
                                                gm_l27 = t201;
                                                void * t202 = F.l0;
                                                uint32_t t203 = F.l1;
                                                void * t204 = gm_l27;
                                                gm_b0_1 t205 = gm_f_binds_busqeo0((void *)NULL, cx, NULL, t202, t203, t204);
                                                if (gm_unw.kind) {
                                                    goto gm_ll192;
                                                }
                                                goto gm_lb36;
                                            }
gm_lb37:;
                                        }
gm_lb36:;
                                        goto gm_lb34;
                                    }
gm_lb35:;
                                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 307, 13);
                                }
gm_lb34:;
                            }
gm_lb33:;
                            uint64_t t206 = gm_l25;
                            uint64_t r207; bool o208 = __builtin_add_overflow(t206, ((uint64_t)1ULL), &r207);
                            uint64_t t209 = r207;
                            gm_l25 = t209;
                        }
                    }
                    goto gm_ls210;
gm_ll192:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb31; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb33; }
                    }
                    goto gm_lad;
gm_ls210:;
gm_lb31:;
                    goto gm_lb1;
                }
gm_lb30:;
            }
            {
                {
                    uint8_t t211 = GM_LD(uint8_t, ((((uint8_t *)t63) + 8) + 0));
                    uint8_t t212 = (uint8_t)(t211 == ((uint8_t)41ULL));
                    if (!t212) {
                        goto gm_lb38;
                    }
                    memmove(((uint8_t *)&gm_l28), ((((uint8_t *)t63) + 8) + 8), 24);
                    {
                        {
                            uint8_t t213 = (uint8_t)(((((uint8_t *)&gm_l28) + 8))[4] != 0);
                            if (!t213) {
                                goto gm_lb40;
                            }
                            uint32_t t214 = GM_LD(uint32_t, (((uint8_t *)&gm_l28) + 8));
                            gm_l29 = t214;
                            void * t215 = F.l0;
                            void * t216 = (void *)(((uint8_t *)t215) + 152);
                            uint32_t t217 = F.l1;
                            uint32_t t218 = gm_l29;
                            gm_tcc1rq4n1avb2 t219;
                            GM_ST(uint32_t, ((uint8_t *)&t219 + 0), t217);
                            GM_ST(uint32_t, ((uint8_t *)&t219 + 4), t218);
                            gm_tftb1odc739t5 t220 = gm_f_put_3g1tdpr(t216, t219, ((uint8_t)1));
                            gm_l45 = t220;
                            uint8_t t221 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
                            if (!t221) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l45) + 1, gm_srco9vbah234t6e, 311, 41);
                            goto gm_lb39;
                        }
gm_lb40:;
                    }
gm_lb39:;
                    goto gm_lb1;
                }
gm_lb38:;
                {
                    uint8_t t222 = GM_LD(uint8_t, ((((uint8_t *)t63) + 8) + 0));
                    uint8_t t223 = (uint8_t)(t222 == ((uint8_t)43ULL));
                    if (!t223) {
                        goto gm_lb41;
                    }
                    void * t224 = GM_LD(void *, ((((uint8_t *)t63) + 8) + 8));
                    gm_l30 = t224;
                    void * t225 = GM_LD(void *, (((((uint8_t *)t63) + 8) + 8) + 8));
                    gm_l31 = t225;
                    uint8_t t226 = GM_LD(uint8_t, (((((uint8_t *)t63) + 8) + 8) + 16));
                    gm_l32 = t226;
                    void * t227 = gm_l30;
                    gm_tqs5p4n2tq19v t228 = gm_f_bool_lit_q44074e(t227);
                    gm_l33 = t228;
                    void * t229 = gm_l31;
                    gm_tqs5p4n2tq19v t230 = gm_f_bool_lit_q44074e(t229);
                    gm_l34 = t230;
                    {
                        {
                            uint8_t t231 = (uint8_t)((((uint8_t *)&gm_l33))[1] != 0);
                            if (t231) {
                                goto gm_lb43;
                            }
                            gm_l46 = ((uint8_t)0);
                            goto gm_lb42;
                        }
gm_lb43:;
                        {
                            gm_l46 = ((uint8_t)1);
                            goto gm_lb42;
                        }
gm_lb44:;
                        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 315, 17);
                    }
gm_lb42:;
                    uint8_t t232 = gm_l46;
                    {
                        {
                            uint8_t t233 = (uint8_t)((((uint8_t *)&gm_l34))[1] != 0);
                            if (t233) {
                                goto gm_lb46;
                            }
                            gm_l47 = ((uint8_t)0);
                            goto gm_lb45;
                        }
gm_lb46:;
                        {
                            gm_l47 = ((uint8_t)1);
                            goto gm_lb45;
                        }
gm_lb47:;
                        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 315, 41);
                    }
gm_lb45:;
                    uint8_t t234 = gm_l47;
                    uint8_t t235 = (!(t232 == t234));
                    if (t235) {
                        uint8_t t236 = (uint8_t)((((uint8_t *)&gm_l33))[1] != 0);
                        if (t236) {
                            uint8_t t237 = GM_LD(uint8_t, ((uint8_t *)&gm_l33));
                            gm_l35 = t237;
                        } else {
                            uint8_t t238 = (uint8_t)((((uint8_t *)&gm_l34))[1] != 0);
                            if (!t238) gm_trap("unwrap of null", gm_srco9vbah234t6e, 316, 30);
                            uint8_t t239 = GM_LD(uint8_t, ((uint8_t *)&gm_l34));
                            gm_l35 = t239;
                        }
                        {
                            {
                                uint8_t t240 = (uint8_t)((((uint8_t *)&gm_l33))[1] != 0);
                                if (t240) {
                                    goto gm_lb49;
                                }
                                gm_l48 = ((uint8_t)0);
                                goto gm_lb48;
                            }
gm_lb49:;
                            {
                                gm_l48 = ((uint8_t)1);
                                goto gm_lb48;
                            }
gm_lb50:;
                            gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 317, 29);
                        }
gm_lb48:;
                        uint8_t t241 = gm_l48;
                        if (t241) {
                            void * t242 = gm_l31;
                            gm_l36 = t242;
                        } else {
                            void * t243 = gm_l30;
                            gm_l36 = t243;
                        }
                        {
                            {
                                {
                                    void * t244 = gm_l4;
                                    void * t245 = gm_l36;
                                    gm_t88rcucg1b7uk t246 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t245));
                                    gm_t53at80pfdus3 t247;
                                    gm_f_kind_jcnt4hm((void *)&t247, t244, GM_CONV(gm_csguankko4lmhd, t246));
                                    uint8_t t248 = GM_LD(uint8_t, (((uint8_t *)&t247) + 0));
                                    uint8_t t249 = (uint8_t)((t248 == ((uint8_t)4ULL)));
                                    if (!t249) {
                                        goto gm_lb52;
                                    }
                                }
                                void * t250 = gm_l2;
                                gm_tl874t2atnlh3 t251 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t250) + 72));
                                gm_tl874t2atnlh3 t252 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                                uint8_t t253 = (!gm_eqff85ac10o3(((uint8_t *)&t251), ((uint8_t *)&t252)));
                                if (!t253) {
                                    goto gm_lb52;
                                }
                                {
                                    void * t254 = F.l0;
                                    void * t255 = (void *)(((uint8_t *)t254) + 208);
                                    void * t256 = gm_l2;
                                    gm_tl874t2atnlh3 t257 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t256) + 72));
                                    void * t258 = gm_l36;
                                    gm_tl874t2atnlh3 t259 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t258) + 72));
                                    uint8_t t260 = gm_l35;
                                    uint8_t t261 = gm_l32;
                                    uint8_t t262 = (!(t260 == t261));
                                    gm_tt7dbdq4n1egc t263;
                                    GM_ST(uint8_t, (uint8_t *)&t263 + 25, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t263 + 26, 0);
                                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t263 + 0), t257);
                                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t263 + 12), t259);
                                    GM_ST(uint8_t, ((uint8_t *)&t263 + 24), t262);
                                    gm_tj5f8vajf35o7 t264;
                                    memset(&t264, 0, sizeof t264);
                                    GM_ST(uint8_t, ((uint8_t *)&t264 + 0), ((uint8_t)3ULL));
                                    GM_ST(gm_tt7dbdq4n1egc, ((uint8_t *)&t264 + 8), t263);
                                    gm_tftb1odc739t5 t265 = gm_f_push_rqlbv1e(t255, &t264);
                                    gm_l49 = t265;
                                }
                                uint8_t t266 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
                                if (!t266) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l49) + 1, gm_srco9vbah234t6e, 319, 21);
                                goto gm_lb51;
                            }
gm_lb52:;
                        }
gm_lb51:;
                    } else {
                        void * t267 = F.l0;
                        void * t268 = gm_l30;
                        void * t269 = gm_l31;
                        uint8_t t270 = gm_f_same_value_7pehjnc(t267, t268, t269);
                        if (t270) {
                            void * t271 = F.l0;
                            void * t272 = (void *)(((uint8_t *)t271) + 208);
                            void * t273 = gm_l2;
                            gm_tl874t2atnlh3 t274 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t273) + 72));
                            uint8_t t275 = gm_l32;
                            uint8_t t276 = (uint8_t)!t275;
                            gm_toceffb697p0u t277;
                            GM_ST(uint8_t, (uint8_t *)&t277 + 13, 0);
                            GM_ST(uint16_t, (uint8_t *)&t277 + 14, 0);
                            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t277 + 0), t274);
                            GM_ST(uint8_t, ((uint8_t *)&t277 + 12), t276);
                            gm_tj5f8vajf35o7 t278;
                            memset(&t278, 0, sizeof t278);
                            GM_ST(uint8_t, ((uint8_t *)&t278 + 0), ((uint8_t)4ULL));
                            GM_ST(gm_toceffb697p0u, ((uint8_t *)&t278 + 8), t277);
                            gm_tftb1odc739t5 t279 = gm_f_push_rqlbv1e(t272, &t278);
                            gm_l50 = t279;
                            uint8_t t280 = (uint8_t)!((((uint8_t *)&gm_l50))[0] != 0);
                            if (!t280) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l50) + 1, gm_srco9vbah234t6e, 322, 17);
                        }
                    }
                    goto gm_lb1;
                }
gm_lb41:;
            }
            {
                {
                    uint8_t t281 = GM_LD(uint8_t, ((((uint8_t *)t63) + 8) + 0));
                    uint8_t t282 = (uint8_t)(t281 == ((uint8_t)30ULL));
                    if (!t282) {
                        goto gm_lb53;
                    }
                    gm_ts6oqppjh0ba3 t283 = GM_LD(gm_ts6oqppjh0ba3, ((((uint8_t *)t63) + 8) + 8));
                    gm_l37 = t283;
                    void * t284 = GM_LD(void *, (((((uint8_t *)t63) + 8) + 8) + 8));
                    gm_l38 = t284;
                    void * t285 = GM_LD(void *, (((((uint8_t *)t63) + 8) + 8) + 16));
                    gm_l39 = t285;
                    {
                        {
                            gm_ts6oqppjh0ba3 t286 = gm_l37;
                            gm_ts6oqppjh0ba3 t287;
                            GM_ST(uint8_t, (uint8_t *)&t287 + 1, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t287 + 0), ((uint8_t)10ULL));
                            gm_ts6oqppjh0ba3 t288;
                            GM_ST(uint8_t, (uint8_t *)&t288 + 1, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t288 + 0), ((uint8_t)11ULL));
                            gm_ts6oqppjh0ba3 t289;
                            GM_ST(uint8_t, (uint8_t *)&t289 + 1, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t289 + 0), ((uint8_t)12ULL));
                            gm_ts6oqppjh0ba3 t290;
                            GM_ST(uint8_t, (uint8_t *)&t290 + 1, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t290 + 0), ((uint8_t)13ULL));
                            gm_t6htphat2tk14 t291;
                            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t291 + 0), t287);
                            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t291 + 2), t288);
                            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t291 + 4), t289);
                            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t291 + 6), t290);
                            uint8_t t292 = 0;
                            if (gm_eqbqb4gb1d92_2(((uint8_t *)&t291) + 0, ((uint8_t *)&t286))) t292 = 1;
                            if (gm_eqbqb4gb1d92_2(((uint8_t *)&t291) + 2, ((uint8_t *)&t286))) t292 = 1;
                            if (gm_eqbqb4gb1d92_2(((uint8_t *)&t291) + 4, ((uint8_t *)&t286))) t292 = 1;
                            if (gm_eqbqb4gb1d92_2(((uint8_t *)&t291) + 6, ((uint8_t *)&t286))) t292 = 1;
                            uint8_t t293 = (uint8_t)(t292);
                            if (!t293) {
                                goto gm_lb55;
                            }
                            void * t294 = F.l0;
                            void * t295 = gm_l38;
                            void * t296 = gm_l39;
                            uint8_t t297 = gm_f_same_value_7pehjnc(t294, t295, t296);
                            if (!t297) {
                                goto gm_lb55;
                            }
                            void * t298 = F.l0;
                            void * t299 = (void *)(((uint8_t *)t298) + 208);
                            void * t300 = gm_l2;
                            gm_tl874t2atnlh3 t301 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t300) + 72));
                            gm_ts6oqppjh0ba3 t302 = gm_l37;
                            gm_ts6oqppjh0ba3 t303;
                            GM_ST(uint8_t, (uint8_t *)&t303 + 1, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t303 + 0), ((uint8_t)11ULL));
                            gm_ts6oqppjh0ba3 t304;
                            GM_ST(uint8_t, (uint8_t *)&t304 + 1, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t304 + 0), ((uint8_t)13ULL));
                            gm_t9ldr1q5hn9fq t305;
                            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t305 + 0), t303);
                            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t305 + 2), t304);
                            uint8_t t306 = 0;
                            if (gm_eqbqb4gb1d92_2(((uint8_t *)&t305) + 0, ((uint8_t *)&t302))) t306 = 1;
                            if (gm_eqbqb4gb1d92_2(((uint8_t *)&t305) + 2, ((uint8_t *)&t302))) t306 = 1;
                            uint8_t t307 = (uint8_t)(t306);
                            gm_toceffb697p0u t308;
                            GM_ST(uint8_t, (uint8_t *)&t308 + 13, 0);
                            GM_ST(uint16_t, (uint8_t *)&t308 + 14, 0);
                            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t308 + 0), t301);
                            GM_ST(uint8_t, ((uint8_t *)&t308 + 12), t307);
                            gm_tj5f8vajf35o7 t309;
                            memset(&t309, 0, sizeof t309);
                            GM_ST(uint8_t, ((uint8_t *)&t309 + 0), ((uint8_t)4ULL));
                            GM_ST(gm_toceffb697p0u, ((uint8_t *)&t309 + 8), t308);
                            gm_tftb1odc739t5 t310 = gm_f_push_rqlbv1e(t299, &t309);
                            gm_l51 = t310;
                            uint8_t t311 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
                            if (!t311) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l51) + 1, gm_srco9vbah234t6e, 327, 17);
                            goto gm_lb54;
                        }
gm_lb55:;
                    }
gm_lb54:;
                    goto gm_lb1;
                }
gm_lb53:;
                {
                    goto gm_lb1;
                }
gm_lb56:;
                gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 284, 5);
            }
        }
gm_lb1:;
    }
    {
        {
            {
                void * t312 = gm_l2;
                uint8_t t313 = GM_LD(uint8_t, ((((uint8_t *)t312) + 8) + 0));
                uint8_t t314 = (uint8_t)(t313 == ((uint8_t)48ULL));
                if (!t314) {
                    goto gm_lb58;
                }
                memmove(((uint8_t *)&F.l40), ((((uint8_t *)t312) + 8) + 8), 40);
                void * t315 = F.l0;
                uint32_t t316 = F.l1;
                void * t317 = GM_LD(void *, (((uint8_t *)&F.l40) + 24));
                gm_b16_8 t318 = GM_LD(gm_b16_8, ((uint8_t *)t317));
                gm_b16_8 t319 = GM_LD(gm_b16_8, ((uint8_t *)&t318));
                gm_b0_1 t320 = gm_f_block_stmts_c2nnk6m((void *)NULL, cx, NULL, t315, t316, t319);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_lb57;
            }
gm_lb58:;
        }
gm_lb57:;
        void * t321 = gm_l2;
        gm_closure t322 = ((gm_closure){(void *)gm_f_fn_259eng6, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t321, t322);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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

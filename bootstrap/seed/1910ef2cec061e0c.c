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
typedef struct gm_csqbqksqco2bm6 { uint32_t f0; void * f1; } gm_csqbqksqco2bm6;
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
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_csqbqksqco2bm6) == 16 && _Alignof(gm_csqbqksqco2bm6) == 8 && offsetof(gm_csqbqksqco2bm6, f0) == 0 && offsetof(gm_csqbqksqco2bm6, f1) == 8, "layout of `FieldPat`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; uint8_t v2; gm_t9qkh0rsooc8d v3; gm_t9qkh0rsooc8d v4; gm_tcc1rq4n1avb2 v5; uint32_t v6; uint32_t v7; } f1; } gm_t8588ma2cr9d7;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `(e: ^const Expr, l: u32)`");
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; void * f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tgabaikr08mjg;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
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
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_tpe1hv3fekn05 v1; } f1; } gm_t4lnf71mruqpt;
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t8588ma2cr9d7) == 12 && _Alignof(gm_t8588ma2cr9d7) == 4 && offsetof(gm_t8588ma2cr9d7, f0) == 0 && offsetof(gm_t8588ma2cr9d7, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { gm_t8588ma2cr9d7 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_t9q8lhljes74c;
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgabaikr08mjg) == 40 && _Alignof(gm_tgabaikr08mjg) == 8 && offsetof(gm_tgabaikr08mjg, f0) == 0 && offsetof(gm_tgabaikr08mjg, f1) == 16 && offsetof(gm_tgabaikr08mjg, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t4lnf71mruqpt) == 32 && _Alignof(gm_t4lnf71mruqpt) == 8 && offsetof(gm_t4lnf71mruqpt, f0) == 0 && offsetof(gm_t4lnf71mruqpt, f1) == 8, "layout of `HandlerValue`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; uint8_t f2; uint8_t pe[7]; } gm_t59juu943t8sl;
_Static_assert(sizeof(gm_t9q8lhljes74c) == 32 && _Alignof(gm_t9q8lhljes74c) == 8 && offsetof(gm_t9q8lhljes74c, f0) == 0 && offsetof(gm_t9q8lhljes74c, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t419mo45g7j0a v0; void * v1; } f1; } gm_th1g69d7vpipg;
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
typedef struct { gm_th1g69d7vpipg f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; void * f4; } gm_t150deeftog49;
_Static_assert(sizeof(gm_t59juu943t8sl) == 104 && _Alignof(gm_t59juu943t8sl) == 8 && offsetof(gm_t59juu943t8sl, f0) == 0 && offsetof(gm_t59juu943t8sl, f1) == 48 && offsetof(gm_t59juu943t8sl, f2) == 96, "layout of `(lo: Int, hi: Int, inclusive: bool)`");
typedef struct { gm_th1g69d7vpipg f0; void * f1; void * f2; uint32_t f3; uint8_t pe[4]; } gm_t9i38ih07bojq;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; void * v1; gm_t59juu943t8sl v2; void * v3; void * v4; void * v5; gm_ttggq7ng4kp3p v6; gm_b16_8 v7; gm_b16_8 v8; } f1; } gm_tb38v5s9j1r32;
_Static_assert(sizeof(gm_th1g69d7vpipg) == 40 && _Alignof(gm_th1g69d7vpipg) == 8 && offsetof(gm_th1g69d7vpipg, f0) == 0 && offsetof(gm_th1g69d7vpipg, f1) == 8, "layout of `WriteSink`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
_Static_assert(sizeof(gm_t150deeftog49) == 96 && _Alignof(gm_t150deeftog49) == 8 && offsetof(gm_t150deeftog49, f0) == 0 && offsetof(gm_t150deeftog49, f1) == 40 && offsetof(gm_t150deeftog49, f2) == 56 && offsetof(gm_t150deeftog49, f3) == 72 && offsetof(gm_t150deeftog49, f4) == 88, "layout of `WriteExpr`");
_Static_assert(sizeof(gm_t9i38ih07bojq) == 64 && _Alignof(gm_t9i38ih07bojq) == 8 && offsetof(gm_t9i38ih07bojq, f0) == 0 && offsetof(gm_t9i38ih07bojq, f1) == 40 && offsetof(gm_t9i38ih07bojq, f2) == 48 && offsetof(gm_t9i38ih07bojq, f3) == 56, "layout of `WriteDeferredExpr`");
_Static_assert(sizeof(gm_tb38v5s9j1r32) == 112 && _Alignof(gm_tb38v5s9j1r32) == 8 && offsetof(gm_tb38v5s9j1r32, f0) == 0 && offsetof(gm_tb38v5s9j1r32, f1) == 8, "layout of `PatKind`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_t9q8lhljes74c v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; void * v55; gm_tandvbt63uk7n v56; gm_t710lsrq1m8gk v57; } f1; } gm_to0f5qn75cgg8;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_to0f5qn75cgg8) == 64 && _Alignof(gm_to0f5qn75cgg8) == 8 && offsetof(gm_to0f5qn75cgg8, f0) == 0 && offsetof(gm_to0f5qn75cgg8, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_to0f5qn75cgg8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tsf6i2agfnpva;
_Static_assert(sizeof(gm_tsf6i2agfnpva) == 88 && _Alignof(gm_tsf6i2agfnpva) == 8 && offsetof(gm_tsf6i2agfnpva, f0) == 0 && offsetof(gm_tsf6i2agfnpva, f1) == 8 && offsetof(gm_tsf6i2agfnpva, f2) == 72, "layout of `Expr`");
struct gm_fr_clone_block_5tu7v2v {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* stmts */
    gm_trqfvmjtt4sij l14; /* out */
    void * ret;
};
struct gm_fr_clone_conds_ljrasoa {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_clone_expr_af0u1qc {
    gm_frame h;
    gm_tsth9qu1dv0g3 l25; /* callee */
    gm_tsth9qu1dv0g3 l27; /* c */
    gm_trqfvmjtt4sij l43; /* out */
    gm_t710lsrq1m8gk l52; /* x */
    gm_th1g69d7vpipg l61; /* sink */
    gm_th1g69d7vpipg l64; /* sink */
    gm_t710lsrq1m8gk l66; /* w */
    gm_trqfvmjtt4sij l67; /* hs */
    gm_to0f5qn75cgg8 l74; /* k */
    void * ret;
};
struct gm_fr_clone_exprs_umh77j3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_clone_opt_plt4ceg {
    gm_frame h;
    void * ret;
};
struct gm_fr_clone_pat_cq2uu68 {
    gm_frame h;
    gm_trqfvmjtt4sij l9; /* out */
    gm_trqfvmjtt4sij l14; /* out */
    gm_tb38v5s9j1r32 l18; /* k */
    void * ret;
};
struct gm_fr_empty_slice_3v2gjd4_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8q3kspo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lmbdkhb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qlbp545 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rtces00 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_uda0n0p {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vj3ckdj_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_list_58nr42v_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_i33ccm0 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ir0h3tc_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nh3d5t2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_p0pjtrh {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_pmp06kq {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_v5d1o9b_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_push_31airv7 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_bpovr2k_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d6c76ud_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d943vgp_2 {
    gm_frame h;
    gm_tsf6i2agfnpva l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_eb7kbhp_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_f5fsh94 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_r2k6rjf {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_u2jka7o {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_4bta8ne_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_61fb705_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_94j8fj1 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_br5lm9l {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h08har5_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h9tbucc_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j7fb9h1 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_sqitcho {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
__attribute__((aligned(4))) void * gm_f_clone_block_5tu7v2v(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_clone_conds_ljrasoa(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void * gm_f_clone_expr_af0u1qc(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_clone_exprs_umh77j3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void * gm_f_clone_opt_plt4ceg(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_clone_pat_cq2uu68(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lmbdkhb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qlbp545(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rtces00(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_uda0n0p(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_i33ccm0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nh3d5t2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_p0pjtrh(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_pmp06kq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_v5d1o9b_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_mk_pat_hr2l6c4(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_2pnaa87(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_dsbahke(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ges19gd(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_31airv7(void * a0, gm_csqbqksqco2bm6 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bpovr2k_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_eb7kbhp_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_f5fsh94(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_r2k6rjf(void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_u2jka7o(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4bta8ne_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_94j8fj1(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_br5lm9l(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h08har5_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h9tbucc_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j7fb9h1(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_sqitcho(void * a0, uint64_t a1, gm_b16_8 a2);
extern uint8_t gm_kg8c0kikv62[40] __attribute__((aligned(8)));
extern uint8_t gm_kl8b626vj63[40] __attribute__((aligned(8)));
extern uint8_t gm_kmlih2j5ae5[40] __attribute__((aligned(8)));
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcgksioc7kn741[];
extern const char gm_srcrgc9bkquf6nr[];
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) void * gm_f_clone_opt_plt4ceg(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_clone_opt_plt4ceg F;
    void * gm_l0; /* e */
    void * gm_l1; /* x */
    GM_STACK_CHECK(&F, 57u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t2 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l0)) != 0);
    if (t2) {
        void * t3 = GM_LD(void *, ((uint8_t *)&gm_l0));
        gm_l1 = t3;
    } else {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    void * t4 = gm_l1;
    void * t5 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t4);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3v2gjd4_2 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 24)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8q3kspo F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lmbdkhb_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lmbdkhb_2 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 24)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qlbp545(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qlbp545 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rtces00(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_rtces00 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_uda0n0p(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_uda0n0p F;
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
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_58nr42v_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_3v2gjd4_2();
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
inline __attribute__((aligned(4))) void gm_f_list_i33ccm0(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_i33ccm0 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_uda0n0p();
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
inline __attribute__((aligned(4))) void gm_f_list_nh3d5t2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_nh3d5t2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_rtces00();
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
inline __attribute__((aligned(4))) void gm_f_list_p0pjtrh(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_p0pjtrh F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_qlbp545();
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
inline __attribute__((aligned(4))) void gm_f_list_pmp06kq(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_pmp06kq F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_8q3kspo();
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
inline __attribute__((aligned(4))) void gm_f_list_v5d1o9b_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_v5d1o9b_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_lmbdkhb_2();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_31airv7(void * a0, gm_csqbqksqco2bm6 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_31airv7 F;
    void * gm_l0; /* l */
    gm_ttggq7ng4kp3p gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_ttggq7ng4kp3p, a1);
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
        gm_b16_8 t9 = gm_f_empty_slice_uda0n0p();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_br5lm9l(t8, ((uint64_t)1ULL), t10);
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
    gm_ttggq7ng4kp3p t18 = gm_l1;
    GM_ST(gm_ttggq7ng4kp3p, (((uint8_t *)t16) + (uintptr_t)t17 * 16), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bpovr2k_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_bpovr2k_2 F;
    void * gm_l0; /* l */
    gm_tbbqefuhv2r8h gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_3v2gjd4_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_h9tbucc_2(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 24), ((uint8_t *)a1), 24);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 24)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_d6c76ud_2 F;
    void * gm_l0; /* l */
    gm_trcmmd12cefb1 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
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
        gm_b16_8 t9 = gm_f_empty_slice_vj3ckdj_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_h08har5_2(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 32), ((uint8_t *)&gm_l1), 32);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_eb7kbhp_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_eb7kbhp_2 F;
    void * gm_l0; /* l */
    gm_tlr9dp305htm7 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
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
        gm_b16_8 t9 = gm_f_empty_slice_lmbdkhb_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_4bta8ne_2(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 24), ((uint8_t *)&gm_l1), 24);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 24)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_f5fsh94(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_f5fsh94 F;
    void * gm_l0; /* l */
    gm_t4lnf71mruqpt gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
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
        gm_b16_8 t9 = gm_f_empty_slice_qlbp545();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_sqitcho(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 32), ((uint8_t *)&gm_l1), 32);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_r2k6rjf(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_r2k6rjf F;
    void * gm_l0; /* l */
    void * gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_8q3kspo();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_j7fb9h1(t8, ((uint64_t)1ULL), t10);
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
    void * t18 = gm_l1;
    GM_ST(void *, (((uint8_t *)t16) + (uintptr_t)t17 * 8), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_u2jka7o(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_u2jka7o F;
    void * gm_l0; /* l */
    void * gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_rtces00();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_94j8fj1(t8, ((uint64_t)1ULL), t10);
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
    void * t18 = gm_l1;
    GM_ST(void *, (((uint8_t *)t16) + (uintptr_t)t17 * 8), t18);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4bta8ne_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_4bta8ne_2 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)24ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_94j8fj1(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_94j8fj1 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_br5lm9l(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_br5lm9l F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h9tbucc_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_h9tbucc_2 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)24ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j7fb9h1(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_j7fb9h1 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_sqitcho(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_sqitcho F;
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
__attribute__((aligned(4))) void * gm_f_clone_block_5tu7v2v(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_clone_block_5tu7v2v F;
    void * gm_l0; /* b */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_trcmmd12cefb1 gm_l4; /* s */
    uint32_t gm_l5; /* l */
    uint32_t gm_l6; /* l */
    void * gm_l7; /* x */
    gm_b16_8 gm_l8; /* ls */
    void * gm_l9; /* x */
    void * gm_l10; /* p */
    void * gm_l11; /* v */
    gm_b16_8 gm_l12; /* ps */
    void * gm_l13; /* v */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    void * gm_l17; /* p */
    void * gm_l18; /* x */
    uint8_t gm_l19; /* is_err */
    void * gm_l20; /* x */
    GM_STACK_CHECK(&F, 1314u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l21;
    gm_tftb1odc739t5 gm_l22;
    gm_tftb1odc739t5 gm_l23;
    gm_tftb1odc739t5 gm_l24;
    gm_tftb1odc739t5 gm_l25;
    gm_tftb1odc739t5 gm_l26;
    gm_tftb1odc739t5 gm_l27;
    gm_tftb1odc739t5 gm_l28;
    gm_tq7s98ihonkju gm_l29;
    {
        gm_trqfvmjtt4sij t20;
        gm_f_list_ir0h3tc_2((void *)&t20, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t20;
    }
    void * t21 = gm_l0;
    gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)t21));
    gm_l2 = t22;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t23 = gm_l3;
            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t25 = (t23 < t24);
            if (!t25) {
                goto gm_lb1;
            }
            {
                void * t26 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t28 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&gm_l4), (((uint8_t *)t26) + (uintptr_t)t28 * 32), 32);
                        {
                            {
                                uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t30 = (uint8_t)(t29 == ((uint8_t)0ULL));
                                if (!t30) {
                                    goto gm_lb7;
                                }
                                uint32_t t31 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 8));
                                gm_l5 = t31;
                                uint8_t t32 = (uint8_t)(GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8)) != 0);
                                if (t32) {
                                    goto gm_lb7;
                                }
                                void * t33 = (void *)((uint8_t *)&F.l1);
                                uint32_t t34 = gm_l5;
                                gm_ttggq7ng4kp3p t35;
                                GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
                                GM_ST(uint32_t, ((uint8_t *)&t35 + 0), t34);
                                GM_ST(void *, ((uint8_t *)&t35 + 8), ((void *)0));
                                gm_trcmmd12cefb1 t36;
                                GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t36 + 24, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)0ULL));
                                GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t36 + 8), t35);
                                gm_tftb1odc739t5 t37 = gm_f_push_d6c76ud_2(t33, &t36);
                                gm_l21 = t37;
                                uint8_t t38 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                                if (!t38) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srcgksioc7kn741, 90, 30);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t40 = (uint8_t)(t39 == ((uint8_t)0ULL));
                                if (!t40) {
                                    goto gm_lb8;
                                }
                                uint32_t t41 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 8));
                                gm_l6 = t41;
                                uint8_t t42 = (uint8_t)(GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8)) != 0);
                                if (!t42) {
                                    goto gm_lb8;
                                }
                                void * t43 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                                gm_l7 = t43;
                                void * t44 = (void *)((uint8_t *)&F.l1);
                                uint32_t t45 = gm_l6;
                                void * t46 = gm_l7;
                                void * t47 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t46);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_ttggq7ng4kp3p t48;
                                GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
                                GM_ST(uint32_t, ((uint8_t *)&t48 + 0), t45);
                                GM_ST(void *, ((uint8_t *)&t48 + 8), t47);
                                gm_trcmmd12cefb1 t49;
                                GM_ST(uint8_t, (uint8_t *)&t49 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t49 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t49 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t49 + 24, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)0ULL));
                                GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t49 + 8), t48);
                                gm_tftb1odc739t5 t50 = gm_f_push_d6c76ud_2(t44, &t49);
                                gm_l22 = t50;
                                uint8_t t51 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                                if (!t51) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 1, gm_srcgksioc7kn741, 91, 27);
                                goto gm_lb6;
                            }
gm_lb8:;
                            {
                                uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t53 = (uint8_t)(t52 == ((uint8_t)1ULL));
                                if (!t53) {
                                    goto gm_lb9;
                                }
                                gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
                                gm_l8 = t54;
                                void * t55 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 16));
                                gm_l9 = t55;
                                {
                                    void * t56 = (void *)((uint8_t *)&F.l1);
                                    gm_b16_8 t57 = gm_l8;
                                    void * t58 = gm_l9;
                                    void * t59 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t58);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tpe1hv3fekn05 t60;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t60 + 0), t57);
                                    GM_ST(void *, ((uint8_t *)&t60 + 16), t59);
                                    gm_trcmmd12cefb1 t61;
                                    GM_ST(uint8_t, (uint8_t *)&t61 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t61 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t61 + 4, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)1ULL));
                                    GM_ST(gm_tpe1hv3fekn05, ((uint8_t *)&t61 + 8), t60);
                                    gm_tftb1odc739t5 t62 = gm_f_push_d6c76ud_2(t56, &t61);
                                    gm_l23 = t62;
                                }
                                uint8_t t63 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                                if (!t63) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_srcgksioc7kn741, 92, 33);
                                goto gm_lb6;
                            }
gm_lb9:;
                            {
                                uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t65 = (uint8_t)(t64 == ((uint8_t)2ULL));
                                if (!t65) {
                                    goto gm_lb10;
                                }
                                void * t66 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                                gm_l10 = t66;
                                void * t67 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                                gm_l11 = t67;
                                void * t68 = (void *)((uint8_t *)&F.l1);
                                void * t69 = gm_l10;
                                void * t70 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t69);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t71 = gm_l11;
                                void * t72 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t71);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_t3mf58gv9kf3n t73;
                                GM_ST(void *, ((uint8_t *)&t73 + 0), t70);
                                GM_ST(void *, ((uint8_t *)&t73 + 8), t72);
                                gm_trcmmd12cefb1 t74;
                                GM_ST(uint8_t, (uint8_t *)&t74 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t74 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t74 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t74 + 24, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)2ULL));
                                GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t74 + 8), t73);
                                gm_tftb1odc739t5 t75 = gm_f_push_d6c76ud_2(t68, &t74);
                                gm_l24 = t75;
                                uint8_t t76 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                                if (!t76) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_srcgksioc7kn741, 93, 30);
                                goto gm_lb6;
                            }
gm_lb10:;
                            {
                                uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t78 = (uint8_t)(t77 == ((uint8_t)3ULL));
                                if (!t78) {
                                    goto gm_lb11;
                                }
                                gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
                                gm_l12 = t79;
                                void * t80 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 16));
                                gm_l13 = t80;
                                {
                                    gm_trqfvmjtt4sij t81;
                                    gm_f_list_pmp06kq((void *)&t81, (void *)NULL, cx, NULL);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.l14 = t81;
                                }
                                gm_b16_8 t82 = gm_l12;
                                gm_l15 = t82;
                                gm_l16 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t83 = gm_l16;
                                        uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                        uint8_t t85 = (t83 < t84);
                                        if (!t85) {
                                            goto gm_lb12;
                                        }
                                        {
                                            void * t86 = GM_LD(void *, ((uint8_t *)&gm_l15));
                                            uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                            uint64_t t88 = gm_l16;
                                            {
                                                {
                                                    void * t89 = GM_LD(void *, (((uint8_t *)t86) + (uintptr_t)t88 * 8));
                                                    gm_l17 = t89;
                                                    void * t90 = (void *)((uint8_t *)&F.l14);
                                                    void * t91 = gm_l17;
                                                    void * t92 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t91);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_tftb1odc739t5 t93 = gm_f_push_r2k6rjf(t90, t92);
                                                    gm_l25 = t93;
                                                    uint8_t t94 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                                                    if (!t94) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_srcgksioc7kn741, 96, 31);
                                                    goto gm_lb15;
                                                }
gm_lb16:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 96, 17);
                                            }
gm_lb15:;
                                        }
gm_lb14:;
                                        uint64_t t95 = gm_l16;
                                        uint64_t r96; bool o97 = __builtin_add_overflow(t95, ((uint64_t)1ULL), &r96);
                                        uint64_t t98 = r96;
                                        gm_l16 = t98;
                                    }
                                }
gm_lb12:;
                                {
                                    void * t99 = (void *)((uint8_t *)&F.l1);
                                    gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
                                    void * t101 = gm_l13;
                                    void * t102 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t101);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tpe1hv3fekn05 t103;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t103 + 0), t100);
                                    GM_ST(void *, ((uint8_t *)&t103 + 16), t102);
                                    gm_trcmmd12cefb1 t104;
                                    GM_ST(uint8_t, (uint8_t *)&t104 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t104 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t104 + 4, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t104 + 0), ((uint8_t)3ULL));
                                    GM_ST(gm_tpe1hv3fekn05, ((uint8_t *)&t104 + 8), t103);
                                    gm_tftb1odc739t5 t105 = gm_f_push_d6c76ud_2(t99, &t104);
                                    gm_l26 = t105;
                                }
                                uint8_t t106 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                                if (!t106) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_srcgksioc7kn741, 97, 17);
                                goto gm_lb6;
                            }
gm_lb11:;
                            {
                                uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t108 = (uint8_t)(t107 == ((uint8_t)4ULL));
                                if (!t108) {
                                    goto gm_lb17;
                                }
                                void * t109 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                                gm_l18 = t109;
                                void * t110 = (void *)((uint8_t *)&F.l1);
                                void * t111 = gm_l18;
                                void * t112 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t111);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_trcmmd12cefb1 t113;
                                GM_ST(uint8_t, (uint8_t *)&t113 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t113 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t113 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t113 + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&t113 + 24, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t113 + 0), ((uint8_t)4ULL));
                                GM_ST(void *, ((uint8_t *)&t113 + 8), t112);
                                gm_tftb1odc739t5 t114 = gm_f_push_d6c76ud_2(t110, &t113);
                                gm_l27 = t114;
                                uint8_t t115 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                                if (!t115) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 1, gm_srcgksioc7kn741, 99, 25);
                                goto gm_lb6;
                            }
gm_lb17:;
                            {
                                uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t117 = (uint8_t)(t116 == ((uint8_t)5ULL));
                                if (!t117) {
                                    goto gm_lb18;
                                }
                                uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 8));
                                gm_l19 = t118;
                                void * t119 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                                gm_l20 = t119;
                                void * t120 = (void *)((uint8_t *)&F.l1);
                                uint8_t t121 = gm_l19;
                                void * t122 = gm_l20;
                                void * t123 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t122);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tgtub0v94d5u5 t124;
                                GM_ST(uint8_t, (uint8_t *)&t124 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t124 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t124 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t124 + 0), t121);
                                GM_ST(void *, ((uint8_t *)&t124 + 8), t123);
                                gm_trcmmd12cefb1 t125;
                                GM_ST(uint8_t, (uint8_t *)&t125 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t125 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t125 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t125 + 24, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t125 + 0), ((uint8_t)5ULL));
                                GM_ST(gm_tgtub0v94d5u5, ((uint8_t *)&t125 + 8), t124);
                                gm_tftb1odc739t5 t126 = gm_f_push_d6c76ud_2(t120, &t125);
                                gm_l28 = t126;
                                uint8_t t127 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                                if (!t127) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 1, gm_srcgksioc7kn741, 100, 34);
                                goto gm_lb6;
                            }
gm_lb18:;
                            gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 89, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 88, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t128 = gm_l3;
            uint64_t r129; bool o130 = __builtin_add_overflow(t128, ((uint64_t)1ULL), &r129);
            uint64_t t131 = r129;
            gm_l3 = t131;
        }
    }
gm_lb1:;
    {
        gm_b16_8 t132 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
        void * t133 = gm_l0;
        void * t134 = GM_LD(void *, (((uint8_t *)t133) + 16));
        void * t135 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t134);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t136 = gm_l0;
        gm_tl874t2atnlh3 t137 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t136) + 24));
        gm_tgabaikr08mjg t138;
        GM_ST(uint32_t, (uint8_t *)&t138 + 36, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t138 + 0), t132);
        GM_ST(void *, ((uint8_t *)&t138 + 16), t135);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t138 + 24), t137);
        gm_tq7s98ihonkju t139 = gm_f_new_dsbahke((void *)NULL, cx, NULL, &t138, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kmlih2j5ae5)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l29 = t139;
    }
    uint8_t t140 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
    if (!t140) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 8, gm_srcgksioc7kn741, 103, 12);
    void * t141 = GM_LD(void *, (((uint8_t *)&gm_l29) + 8));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_clone_conds_ljrasoa(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_clone_conds_ljrasoa F;
    gm_b16_8 gm_l0; /* cond */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tlr9dp305htm7 gm_l4; /* c */
    void * gm_l5; /* x */
    void * gm_l6; /* p */
    void * gm_l7; /* v */
    GM_STACK_CHECK(&F, 415u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_trqfvmjtt4sij t10;
    gm_f_list_v5d1o9b_2((void *)&t10, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t10;
    gm_b16_8 t11 = gm_l0;
    gm_l2 = t11;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t12 = gm_l3;
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t14 = (t12 < t13);
            if (!t14) {
                goto gm_lb1;
            }
            {
                void * t15 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t17 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&gm_l4), (((uint8_t *)t15) + (uintptr_t)t17 * 24), 24);
                        {
                            {
                                uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t19 = (uint8_t)(t18 == ((uint8_t)0ULL));
                                if (!t19) {
                                    goto gm_lb7;
                                }
                                void * t20 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                                gm_l5 = t20;
                                void * t21 = (void *)((uint8_t *)&F.l1);
                                void * t22 = gm_l5;
                                void * t23 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t22);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tlr9dp305htm7 t24;
                                GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t24 + 16, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
                                GM_ST(void *, ((uint8_t *)&t24 + 8), t23);
                                gm_tftb1odc739t5 t25 = gm_f_push_eb7kbhp_2(t21, &t24);
                                gm_l8 = t25;
                                uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                                if (!t26) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srcgksioc7kn741, 110, 25);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t28 = (uint8_t)(t27 == ((uint8_t)1ULL));
                                if (!t28) {
                                    goto gm_lb8;
                                }
                                void * t29 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                                gm_l6 = t29;
                                void * t30 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                                gm_l7 = t30;
                                void * t31 = (void *)((uint8_t *)&F.l1);
                                void * t32 = gm_l6;
                                void * t33 = gm_f_clone_pat_cq2uu68((void *)NULL, cx, NULL, t32);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t34 = gm_l7;
                                void * t35 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t34);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_t3mf58gv9kf3n t36;
                                GM_ST(void *, ((uint8_t *)&t36 + 0), t33);
                                GM_ST(void *, ((uint8_t *)&t36 + 8), t35);
                                gm_tlr9dp305htm7 t37;
                                GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)1ULL));
                                GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t37 + 8), t36);
                                gm_tftb1odc739t5 t38 = gm_f_push_eb7kbhp_2(t31, &t37);
                                gm_l9 = t38;
                                uint8_t t39 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                                if (!t39) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcgksioc7kn741, 111, 29);
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 109, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 108, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t40 = gm_l3;
            uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
            uint64_t t43 = r41;
            gm_l3 = t43;
        }
    }
gm_lb1:;
    gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    F.ret = t44;
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
__attribute__((aligned(4))) void * gm_f_clone_expr_af0u1qc(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_clone_expr_af0u1qc F;
    void * gm_l0; /* e */
    void * gm_l1; /* a */
    void * gm_l2; /* b */
    uint32_t gm_l3; /* i */
    void * gm_l4; /* b */
    void * gm_l5; /* i */
    uint8_t gm_l6; /* c */
    gm_tao63pk3r18v5 gm_l7; /* sl */
    void * gm_l8; /* a */
    void * gm_l9; /* a */
    void * gm_l10; /* a */
    void * gm_l11; /* a */
    gm_t9qkh0rsooc8d gm_l12; /* op */
    void * gm_l13; /* a */
    gm_ts6oqppjh0ba3 gm_l14; /* op */
    void * gm_l15; /* a */
    void * gm_l16; /* b */
    uint8_t gm_l17; /* and_ */
    void * gm_l18; /* a */
    void * gm_l19; /* b */
    gm_tmn7n0n9raou5 gm_l20; /* c */
    void * gm_l21; /* a */
    gm_t8588ma2cr9d7 gm_l22; /* op */
    gm_b16_8 gm_l23; /* args */
    gm_tandvbt63uk7n gm_l24; /* x */
    gm_b16_8 gm_l26; /* args */
    void * gm_l28; /* x */
    gm_b16_8 gm_l29; /* xs */
    gm_b16_8 gm_l30; /* xs */
    void * gm_l31; /* a */
    gm_ttggq7ng4kp3p gm_l32; /* v */
    void * gm_l33; /* a */
    void * gm_l34; /* a */
    uint8_t gm_l35; /* r */
    gm_t3dcoc7077t1q gm_l36; /* x */
    gm_t083kd7b1kkag gm_l37; /* x */
    void * gm_l38; /* a */
    void * gm_l39; /* b */
    uint8_t gm_l40; /* n */
    void * gm_l41; /* sc */
    gm_b16_8 gm_l42; /* arms */
    gm_b16_8 gm_l44; /* <tmp> */
    uint64_t gm_l45; /* <tmp> */
    gm_tbbqefuhv2r8h gm_l46; /* a */
    void * gm_l47; /* a */
    void * gm_l48; /* b */
    uint8_t gm_l49; /* n */
    void * gm_l50; /* b */
    gm_trl2khmuphncd gm_l51; /* x */
    gm_teo9d4mncjfdu gm_l53; /* x */
    uint32_t gm_l54; /* l */
    void * gm_l55; /* v */
    void * gm_l56; /* a */
    void * gm_l57; /* k */
    void * gm_l58; /* v */
    void * gm_l59; /* k */
    void * gm_l60; /* w */
    void * gm_l62; /* x */
    void * gm_l63; /* w */
    void * gm_l65; /* x */
    gm_b16_8 gm_l68; /* <tmp> */
    uint64_t gm_l69; /* <tmp> */
    gm_t4lnf71mruqpt gm_l70; /* h */
    void * gm_l71; /* x */
    gm_b16_8 gm_l72; /* decls */
    void * gm_l73; /* x */
    GM_STACK_CHECK(&F, 4096u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l75;
    gm_tq7s98ihonkju gm_l76;
    gm_tq7s98ihonkju gm_l77;
    gm_tftb1odc739t5 gm_l78;
    gm_tftb1odc739t5 gm_l79;
    {
        void * t60 = gm_l0;
        {
            {
                {
                    uint8_t t61 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t62 = (uint8_t)(t61 == ((uint8_t)18ULL));
                    if (!t62) {
                        goto gm_lb2;
                    }
                    void * t63 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l1 = t63;
                    void * t64 = gm_l1;
                    void * t65 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t64);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    memset(&F.l74, 0, sizeof F.l74);
                    GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)18ULL));
                    GM_ST(void *, ((uint8_t *)&F.l74 + 8), t65);
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    uint8_t t66 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t67 = (uint8_t)(t66 == ((uint8_t)19ULL));
                    if (!t67) {
                        goto gm_lb3;
                    }
                    void * t68 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l2 = t68;
                    uint32_t t69 = GM_LD(uint32_t, (((((uint8_t *)t60) + 8) + 8) + 8));
                    gm_l3 = t69;
                    void * t70 = gm_l2;
                    void * t71 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t70);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint32_t t72 = gm_l3;
                    gm_tbl8il92l97ut t73;
                    GM_ST(uint32_t, (uint8_t *)&t73 + 12, 0);
                    GM_ST(void *, ((uint8_t *)&t73 + 0), t71);
                    GM_ST(uint32_t, ((uint8_t *)&t73 + 8), t72);
                    memset(&F.l74, 0, sizeof F.l74);
                    GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)19ULL));
                    GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&F.l74 + 8), t73);
                    goto gm_lb1;
                }
gm_lb3:;
                {
                    uint8_t t74 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t75 = (uint8_t)(t74 == ((uint8_t)20ULL));
                    if (!t75) {
                        goto gm_lb4;
                    }
                    void * t76 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l4 = t76;
                    void * t77 = GM_LD(void *, (((((uint8_t *)t60) + 8) + 8) + 8));
                    gm_l5 = t77;
                    uint8_t t78 = GM_LD(uint8_t, (((((uint8_t *)t60) + 8) + 8) + 16));
                    gm_l6 = t78;
                    {
                        void * t79 = gm_l4;
                        void * t80 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t79);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t81 = gm_l5;
                        void * t82 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t81);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t83 = gm_l6;
                        gm_tlcl4u558l6p5 t84;
                        GM_ST(uint8_t, (uint8_t *)&t84 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&t84 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&t84 + 20, 0);
                        GM_ST(void *, ((uint8_t *)&t84 + 0), t80);
                        GM_ST(void *, ((uint8_t *)&t84 + 8), t82);
                        GM_ST(uint8_t, ((uint8_t *)&t84 + 16), t83);
                        memset(&F.l74, 0, sizeof F.l74);
                        GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)20ULL));
                        GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&F.l74 + 8), t84);
                    }
                    goto gm_lb1;
                }
gm_lb4:;
                {
                    uint8_t t85 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t86 = (uint8_t)(t85 == ((uint8_t)21ULL));
                    if (!t86) {
                        goto gm_lb5;
                    }
                    {
                        memmove(((uint8_t *)&gm_l7), ((((uint8_t *)t60) + 8) + 8), 32);
                    }
                    {
                        void * t87 = GM_LD(void *, ((uint8_t *)&gm_l7));
                        void * t88 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t87);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t89 = GM_LD(void *, (((uint8_t *)&gm_l7) + 8));
                        void * t90 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t89);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t91 = GM_LD(void *, (((uint8_t *)&gm_l7) + 16));
                        void * t92 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t91);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t93 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 24));
                        gm_tao63pk3r18v5 t94;
                        GM_ST(uint8_t, (uint8_t *)&t94 + 25, 0);
                        GM_ST(uint16_t, (uint8_t *)&t94 + 26, 0);
                        GM_ST(uint32_t, (uint8_t *)&t94 + 28, 0);
                        GM_ST(void *, ((uint8_t *)&t94 + 0), t88);
                        GM_ST(void *, ((uint8_t *)&t94 + 8), t90);
                        GM_ST(void *, ((uint8_t *)&t94 + 16), t92);
                        GM_ST(uint8_t, ((uint8_t *)&t94 + 24), t93);
                        GM_ST(uint8_t, (uint8_t *)&F.l74 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&F.l74 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&F.l74 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&F.l74 + 40, 0);
                        GM_ST(uint64_t, (uint8_t *)&F.l74 + 48, 0);
                        GM_ST(uint64_t, (uint8_t *)&F.l74 + 56, 0);
                        GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)21ULL));
                        GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&F.l74 + 8), t94);
                    }
                    goto gm_lb1;
                }
gm_lb5:;
                {
                    uint8_t t95 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t96 = (uint8_t)(t95 == ((uint8_t)22ULL));
                    if (!t96) {
                        goto gm_lb6;
                    }
                    void * t97 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l8 = t97;
                    void * t98 = gm_l8;
                    void * t99 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t98);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    memset(&F.l74, 0, sizeof F.l74);
                    GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)22ULL));
                    GM_ST(void *, ((uint8_t *)&F.l74 + 8), t99);
                    goto gm_lb1;
                }
gm_lb6:;
            }
            {
                {
                    uint8_t t100 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t101 = (uint8_t)(t100 == ((uint8_t)23ULL));
                    if (!t101) {
                        goto gm_lb7;
                    }
                    void * t102 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l9 = t102;
                    void * t103 = gm_l9;
                    void * t104 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t103);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    memset(&F.l74, 0, sizeof F.l74);
                    GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)23ULL));
                    GM_ST(void *, ((uint8_t *)&F.l74 + 8), t104);
                    goto gm_lb1;
                }
gm_lb7:;
                {
                    uint8_t t105 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t106 = (uint8_t)(t105 == ((uint8_t)24ULL));
                    if (!t106) {
                        goto gm_lb8;
                    }
                    void * t107 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l10 = t107;
                    void * t108 = gm_l10;
                    void * t109 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t108);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    memset(&F.l74, 0, sizeof F.l74);
                    GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)24ULL));
                    GM_ST(void *, ((uint8_t *)&F.l74 + 8), t109);
                    goto gm_lb1;
                }
gm_lb8:;
                {
                    uint8_t t110 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t111 = (uint8_t)(t110 == ((uint8_t)25ULL));
                    if (!t111) {
                        goto gm_lb9;
                    }
                    void * t112 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l11 = t112;
                    void * t113 = gm_l11;
                    void * t114 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t113);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    memset(&F.l74, 0, sizeof F.l74);
                    GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)25ULL));
                    GM_ST(void *, ((uint8_t *)&F.l74 + 8), t114);
                    goto gm_lb1;
                }
gm_lb9:;
                {
                    uint8_t t115 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t116 = (uint8_t)(t115 == ((uint8_t)29ULL));
                    if (!t116) {
                        goto gm_lb10;
                    }
                    gm_t9qkh0rsooc8d t117 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t60) + 8) + 8));
                    gm_l12 = t117;
                    void * t118 = GM_LD(void *, (((((uint8_t *)t60) + 8) + 8) + 8));
                    gm_l13 = t118;
                    gm_t9qkh0rsooc8d t119 = gm_l12;
                    void * t120 = gm_l13;
                    void * t121 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t120);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t8ghgl2jj0qha t122;
                    GM_ST(uint8_t, (uint8_t *)&t122 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t122 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t122 + 4, 0);
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t122 + 0), t119);
                    GM_ST(void *, ((uint8_t *)&t122 + 8), t121);
                    memset(&F.l74, 0, sizeof F.l74);
                    GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)29ULL));
                    GM_ST(gm_t8ghgl2jj0qha, ((uint8_t *)&F.l74 + 8), t122);
                    goto gm_lb1;
                }
gm_lb10:;
                {
                    uint8_t t123 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t124 = (uint8_t)(t123 == ((uint8_t)30ULL));
                    if (!t124) {
                        goto gm_lb11;
                    }
                    gm_ts6oqppjh0ba3 t125 = GM_LD(gm_ts6oqppjh0ba3, ((((uint8_t *)t60) + 8) + 8));
                    gm_l14 = t125;
                    void * t126 = GM_LD(void *, (((((uint8_t *)t60) + 8) + 8) + 8));
                    gm_l15 = t126;
                    void * t127 = GM_LD(void *, (((((uint8_t *)t60) + 8) + 8) + 16));
                    gm_l16 = t127;
                    {
                        gm_ts6oqppjh0ba3 t128 = gm_l14;
                        void * t129 = gm_l15;
                        void * t130 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t129);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t131 = gm_l16;
                        void * t132 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t131);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_trl9d7m1k5lbd t133;
                        GM_ST(uint16_t, (uint8_t *)&t133 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t133 + 4, 0);
                        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t133 + 0), t128);
                        GM_ST(void *, ((uint8_t *)&t133 + 8), t130);
                        GM_ST(void *, ((uint8_t *)&t133 + 16), t132);
                        memset(&F.l74, 0, sizeof F.l74);
                        GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)30ULL));
                        GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&F.l74 + 8), t133);
                    }
                    goto gm_lb1;
                }
gm_lb11:;
            }
            {
                {
                    uint8_t t134 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t135 = (uint8_t)(t134 == ((uint8_t)31ULL));
                    if (!t135) {
                        goto gm_lb12;
                    }
                    uint8_t t136 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 8));
                    gm_l17 = t136;
                    void * t137 = GM_LD(void *, (((((uint8_t *)t60) + 8) + 8) + 8));
                    gm_l18 = t137;
                    void * t138 = GM_LD(void *, (((((uint8_t *)t60) + 8) + 8) + 16));
                    gm_l19 = t138;
                    {
                        uint8_t t139 = gm_l17;
                        void * t140 = gm_l18;
                        void * t141 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t140);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t142 = gm_l19;
                        void * t143 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t142);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_ta612fil812pm t144;
                        GM_ST(uint8_t, (uint8_t *)&t144 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t144 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t144 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t144 + 0), t139);
                        GM_ST(void *, ((uint8_t *)&t144 + 8), t141);
                        GM_ST(void *, ((uint8_t *)&t144 + 16), t143);
                        memset(&F.l74, 0, sizeof F.l74);
                        GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)31ULL));
                        GM_ST(gm_ta612fil812pm, ((uint8_t *)&F.l74 + 8), t144);
                    }
                    goto gm_lb1;
                }
gm_lb12:;
                {
                    uint8_t t145 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t146 = (uint8_t)(t145 == ((uint8_t)32ULL));
                    if (!t146) {
                        goto gm_lb13;
                    }
                    gm_tmn7n0n9raou5 t147 = GM_LD(gm_tmn7n0n9raou5, ((((uint8_t *)t60) + 8) + 8));
                    gm_l20 = t147;
                    void * t148 = GM_LD(void *, (((((uint8_t *)t60) + 8) + 8) + 8));
                    gm_l21 = t148;
                    gm_tmn7n0n9raou5 t149 = gm_l20;
                    void * t150 = gm_l21;
                    void * t151 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t150);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t8jsc4n9vp2ib t152;
                    GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t152 + 0), t149);
                    GM_ST(void *, ((uint8_t *)&t152 + 8), t151);
                    memset(&F.l74, 0, sizeof F.l74);
                    GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)32ULL));
                    GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&F.l74 + 8), t152);
                    goto gm_lb1;
                }
gm_lb13:;
                {
                    uint8_t t153 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t154 = (uint8_t)(t153 == ((uint8_t)33ULL));
                    if (!t154) {
                        goto gm_lb14;
                    }
                    gm_t8588ma2cr9d7 t155 = GM_LD(gm_t8588ma2cr9d7, ((((uint8_t *)t60) + 8) + 8));
                    gm_l22 = t155;
                    gm_b16_8 t156 = GM_LD(gm_b16_8, (((((uint8_t *)t60) + 8) + 8) + 16));
                    gm_l23 = t156;
                    {
                        gm_t8588ma2cr9d7 t157 = gm_l22;
                        gm_b16_8 t158 = gm_l23;
                        gm_b16_8 t159 = GM_LD(gm_b16_8, ((uint8_t *)&t158));
                        gm_b16_8 t160 = gm_f_clone_exprs_umh77j3((void *)NULL, cx, NULL, t159);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t9q8lhljes74c t161;
                        GM_ST(uint32_t, (uint8_t *)&t161 + 12, 0);
                        GM_ST(gm_t8588ma2cr9d7, ((uint8_t *)&t161 + 0), t157);
                        GM_ST(gm_b16_8, ((uint8_t *)&t161 + 16), t160);
                        GM_ST(uint8_t, (uint8_t *)&F.l74 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&F.l74 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&F.l74 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&F.l74 + 40, 0);
                        GM_ST(uint64_t, (uint8_t *)&F.l74 + 48, 0);
                        GM_ST(uint64_t, (uint8_t *)&F.l74 + 56, 0);
                        GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)33ULL));
                        GM_ST(gm_t9q8lhljes74c, ((uint8_t *)&F.l74 + 8), t161);
                    }
                    goto gm_lb1;
                }
gm_lb14:;
                {
                    uint8_t t162 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t163 = (uint8_t)(t162 == ((uint8_t)58ULL));
                    if (!t163) {
                        goto gm_lb15;
                    }
                    {
                        memmove(((uint8_t *)&gm_l24), ((((uint8_t *)t60) + 8) + 8), 24);
                    }
                    {
                        gm_tesaltf0mhcki t164 = GM_LD(gm_tesaltf0mhcki, ((uint8_t *)&gm_l24));
                        gm_b16_8 t165 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 8));
                        gm_b16_8 t166 = GM_LD(gm_b16_8, ((uint8_t *)&t165));
                        gm_b16_8 t167 = gm_f_clone_exprs_umh77j3((void *)NULL, cx, NULL, t166);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tandvbt63uk7n t168;
                        GM_ST(uint32_t, (uint8_t *)&t168 + 4, 0);
                        GM_ST(gm_tesaltf0mhcki, ((uint8_t *)&t168 + 0), t164);
                        GM_ST(gm_b16_8, ((uint8_t *)&t168 + 8), t167);
                        memset(&F.l74, 0, sizeof F.l74);
                        GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)58ULL));
                        GM_ST(gm_tandvbt63uk7n, ((uint8_t *)&F.l74 + 8), t168);
                    }
                    goto gm_lb1;
                }
gm_lb15:;
                {
                    uint8_t t169 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t170 = (uint8_t)(t169 == ((uint8_t)34ULL));
                    if (!t170) {
                        goto gm_lb16;
                    }
                    {
                        memmove(((uint8_t *)&F.l25), ((((uint8_t *)t60) + 8) + 8), 40);
                    }
                    gm_b16_8 t171 = GM_LD(gm_b16_8, (((((uint8_t *)t60) + 8) + 8) + 40));
                    gm_l26 = t171;
                    {
                        memmove(((uint8_t *)&F.l27), ((uint8_t *)&F.l25), 40);
                    }
                    {
                        {
                            uint8_t t172 = GM_LD(uint8_t, (((uint8_t *)&F.l25) + 0));
                            uint8_t t173 = (uint8_t)(t172 == ((uint8_t)2ULL));
                            if (!t173) {
                                goto gm_lb18;
                            }
                            void * t174 = GM_LD(void *, (((uint8_t *)&F.l25) + 8));
                            gm_l28 = t174;
                            void * t175 = gm_l28;
                            void * t176 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t175);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            GM_ST(uint8_t, (uint8_t *)&F.l27 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&F.l27 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&F.l27 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&F.l27 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&F.l27 + 24, 0);
                            GM_ST(uint64_t, (uint8_t *)&F.l27 + 32, 0);
                            GM_ST(uint8_t, ((uint8_t *)&F.l27 + 0), ((uint8_t)2ULL));
                            GM_ST(void *, ((uint8_t *)&F.l27 + 8), t176);
                            goto gm_lb17;
                        }
gm_lb18:;
                    }
gm_lb17:;
                    {
                        gm_b16_8 t177 = gm_l26;
                        gm_b16_8 t178 = GM_LD(gm_b16_8, ((uint8_t *)&t177));
                        gm_tsth9qu1dv0g3 t179 = GM_LD(gm_tsth9qu1dv0g3, ((uint8_t *)&F.l27));
                        gm_b16_8 t180 = gm_f_clone_exprs_umh77j3((void *)NULL, cx, NULL, t178);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_to76jli3bqddq t181;
                        GM_ST(gm_tsth9qu1dv0g3, ((uint8_t *)&t181 + 0), t179);
                        GM_ST(gm_b16_8, ((uint8_t *)&t181 + 40), t180);
                        GM_ST(uint8_t, (uint8_t *)&F.l74 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&F.l74 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&F.l74 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)34ULL));
                        GM_ST(gm_to76jli3bqddq, ((uint8_t *)&F.l74 + 8), t181);
                    }
                    goto gm_lb1;
                }
gm_lb16:;
            }
            {
                {
                    uint8_t t182 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t183 = (uint8_t)(t182 == ((uint8_t)35ULL));
                    if (!t183) {
                        goto gm_lb19;
                    }
                    gm_b16_8 t184 = GM_LD(gm_b16_8, ((((uint8_t *)t60) + 8) + 8));
                    gm_l29 = t184;
                    gm_b16_8 t185 = gm_l29;
                    gm_b16_8 t186 = GM_LD(gm_b16_8, ((uint8_t *)&t185));
                    gm_b16_8 t187 = gm_f_clone_exprs_umh77j3((void *)NULL, cx, NULL, t186);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    memset(&F.l74, 0, sizeof F.l74);
                    GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)35ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&F.l74 + 8), t187);
                    goto gm_lb1;
                }
gm_lb19:;
                {
                    uint8_t t188 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t189 = (uint8_t)(t188 == ((uint8_t)36ULL));
                    if (!t189) {
                        goto gm_lb20;
                    }
                    gm_b16_8 t190 = GM_LD(gm_b16_8, ((((uint8_t *)t60) + 8) + 8));
                    gm_l30 = t190;
                    gm_b16_8 t191 = gm_l30;
                    gm_b16_8 t192 = GM_LD(gm_b16_8, ((uint8_t *)&t191));
                    gm_b16_8 t193 = gm_f_clone_exprs_umh77j3((void *)NULL, cx, NULL, t192);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    memset(&F.l74, 0, sizeof F.l74);
                    GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)36ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&F.l74 + 8), t193);
                    goto gm_lb1;
                }
gm_lb20:;
                {
                    uint8_t t194 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t195 = (uint8_t)(t194 == ((uint8_t)37ULL));
                    if (!t195) {
                        goto gm_lb21;
                    }
                    void * t196 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l31 = t196;
                    void * t197 = gm_l31;
                    void * t198 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t197);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    memset(&F.l74, 0, sizeof F.l74);
                    GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)37ULL));
                    GM_ST(void *, ((uint8_t *)&F.l74 + 8), t198);
                    goto gm_lb1;
                }
gm_lb21:;
                {
                    uint8_t t199 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t200 = (uint8_t)(t199 == ((uint8_t)38ULL));
                    if (!t200) {
                        goto gm_lb22;
                    }
                    gm_ttggq7ng4kp3p t201 = GM_LD(gm_ttggq7ng4kp3p, ((((uint8_t *)t60) + 8) + 8));
                    gm_l32 = t201;
                    uint32_t t202 = GM_LD(uint32_t, ((uint8_t *)&gm_l32));
                    void * t203 = GM_LD(void *, (((uint8_t *)&gm_l32) + 8));
                    void * t204 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t203);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_ttggq7ng4kp3p t205;
                    GM_ST(uint32_t, (uint8_t *)&t205 + 4, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t205 + 0), t202);
                    GM_ST(void *, ((uint8_t *)&t205 + 8), t204);
                    memset(&F.l74, 0, sizeof F.l74);
                    GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)38ULL));
                    GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&F.l74 + 8), t205);
                    goto gm_lb1;
                }
gm_lb22:;
                {
                    uint8_t t206 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t207 = (uint8_t)(t206 == ((uint8_t)39ULL));
                    if (!t207) {
                        goto gm_lb23;
                    }
                    void * t208 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l33 = t208;
                    void * t209 = gm_l33;
                    void * t210 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t209);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    memset(&F.l74, 0, sizeof F.l74);
                    GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)39ULL));
                    GM_ST(void *, ((uint8_t *)&F.l74 + 8), t210);
                    goto gm_lb1;
                }
gm_lb23:;
                {
                    uint8_t t211 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t212 = (uint8_t)(t211 == ((uint8_t)40ULL));
                    if (!t212) {
                        goto gm_lb24;
                    }
                    void * t213 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l34 = t213;
                    uint8_t t214 = GM_LD(uint8_t, (((((uint8_t *)t60) + 8) + 8) + 8));
                    gm_l35 = t214;
                    void * t215 = gm_l34;
                    void * t216 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t215);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t217 = gm_l35;
                    gm_tsplfr33dsvji t218;
                    GM_ST(uint8_t, (uint8_t *)&t218 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t218 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t218 + 12, 0);
                    GM_ST(void *, ((uint8_t *)&t218 + 0), t216);
                    GM_ST(uint8_t, ((uint8_t *)&t218 + 8), t217);
                    memset(&F.l74, 0, sizeof F.l74);
                    GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)40ULL));
                    GM_ST(gm_tsplfr33dsvji, ((uint8_t *)&F.l74 + 8), t218);
                    goto gm_lb1;
                }
gm_lb24:;
            }
            {
                {
                    uint8_t t219 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t220 = (uint8_t)(t219 == ((uint8_t)41ULL));
                    if (!t220) {
                        goto gm_lb25;
                    }
                    {
                        memmove(((uint8_t *)&gm_l36), ((((uint8_t *)t60) + 8) + 8), 24);
                    }
                    {
                        void * t221 = GM_LD(void *, ((uint8_t *)&gm_l36));
                        void * t222 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t221);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t9805qtonbsag t223 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l36) + 8));
                        void * t224 = GM_LD(void *, (((uint8_t *)&gm_l36) + 16));
                        void * t225 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t224);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t3dcoc7077t1q t226;
                        GM_ST(void *, ((uint8_t *)&t226 + 0), t222);
                        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t226 + 8), t223);
                        GM_ST(void *, ((uint8_t *)&t226 + 16), t225);
                        memset(&F.l74, 0, sizeof F.l74);
                        GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)41ULL));
                        GM_ST(gm_t3dcoc7077t1q, ((uint8_t *)&F.l74 + 8), t226);
                    }
                    goto gm_lb1;
                }
gm_lb25:;
                {
                    uint8_t t227 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t228 = (uint8_t)(t227 == ((uint8_t)42ULL));
                    if (!t228) {
                        goto gm_lb26;
                    }
                    {
                        memmove(((uint8_t *)&gm_l37), ((((uint8_t *)t60) + 8) + 8), 24);
                    }
                    {
                        void * t229 = GM_LD(void *, ((uint8_t *)&gm_l37));
                        void * t230 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t229);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t9805qtonbsag t231 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l37) + 8));
                        uint32_t t232 = GM_LD(uint32_t, (((uint8_t *)&gm_l37) + 16));
                        gm_t083kd7b1kkag t233;
                        GM_ST(uint32_t, (uint8_t *)&t233 + 20, 0);
                        GM_ST(void *, ((uint8_t *)&t233 + 0), t230);
                        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t233 + 8), t231);
                        GM_ST(uint32_t, ((uint8_t *)&t233 + 16), t232);
                        memset(&F.l74, 0, sizeof F.l74);
                        GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)42ULL));
                        GM_ST(gm_t083kd7b1kkag, ((uint8_t *)&F.l74 + 8), t233);
                    }
                    goto gm_lb1;
                }
gm_lb26:;
                {
                    uint8_t t234 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t235 = (uint8_t)(t234 == ((uint8_t)43ULL));
                    if (!t235) {
                        goto gm_lb27;
                    }
                    void * t236 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l38 = t236;
                    void * t237 = GM_LD(void *, (((((uint8_t *)t60) + 8) + 8) + 8));
                    gm_l39 = t237;
                    uint8_t t238 = GM_LD(uint8_t, (((((uint8_t *)t60) + 8) + 8) + 16));
                    gm_l40 = t238;
                    {
                        void * t239 = gm_l38;
                        void * t240 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t239);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t241 = gm_l39;
                        void * t242 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t241);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t243 = gm_l40;
                        gm_tlcl4u558l6p5 t244;
                        GM_ST(uint8_t, (uint8_t *)&t244 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&t244 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&t244 + 20, 0);
                        GM_ST(void *, ((uint8_t *)&t244 + 0), t240);
                        GM_ST(void *, ((uint8_t *)&t244 + 8), t242);
                        GM_ST(uint8_t, ((uint8_t *)&t244 + 16), t243);
                        memset(&F.l74, 0, sizeof F.l74);
                        GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)43ULL));
                        GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&F.l74 + 8), t244);
                    }
                    goto gm_lb1;
                }
gm_lb27:;
                {
                    uint8_t t245 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t246 = (uint8_t)(t245 == ((uint8_t)44ULL));
                    if (!t246) {
                        goto gm_lb28;
                    }
                    void * t247 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l41 = t247;
                    gm_b16_8 t248 = GM_LD(gm_b16_8, (((((uint8_t *)t60) + 8) + 8) + 8));
                    gm_l42 = t248;
                    {
                        gm_trqfvmjtt4sij t249;
                        gm_f_list_58nr42v_2((void *)&t249, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l43 = t249;
                    }
                    gm_b16_8 t250 = gm_l42;
                    gm_l44 = t250;
                    gm_l45 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t251 = gm_l45;
                            uint64_t t252 = GM_LD(uint64_t, (((uint8_t *)&gm_l44) + 8));
                            uint8_t t253 = (t251 < t252);
                            if (!t253) {
                                goto gm_lb29;
                            }
                            {
                                void * t254 = GM_LD(void *, ((uint8_t *)&gm_l44));
                                uint64_t t255 = GM_LD(uint64_t, (((uint8_t *)&gm_l44) + 8));
                                uint64_t t256 = gm_l45;
                                {
                                    {
                                        {
                                            memmove(((uint8_t *)&gm_l46), (((uint8_t *)t254) + (uintptr_t)t256 * 24), 24);
                                        }
                                        {
                                            void * t257 = (void *)((uint8_t *)&F.l43);
                                            void * t258 = GM_LD(void *, ((uint8_t *)&gm_l46));
                                            void * t259 = gm_f_clone_pat_cq2uu68((void *)NULL, cx, NULL, t258);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            void * t260 = GM_LD(void *, (((uint8_t *)&gm_l46) + 8));
                                            void * t261 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t260);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            void * t262 = GM_LD(void *, (((uint8_t *)&gm_l46) + 16));
                                            void * t263 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t262);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_tbbqefuhv2r8h t264;
                                            GM_ST(void *, ((uint8_t *)&t264 + 0), t259);
                                            GM_ST(void *, ((uint8_t *)&t264 + 8), t261);
                                            GM_ST(void *, ((uint8_t *)&t264 + 16), t263);
                                            gm_tftb1odc739t5 t265 = gm_f_push_bpovr2k_2(t257, &t264);
                                            gm_l75 = t265;
                                        }
                                        uint8_t t266 = (uint8_t)!((((uint8_t *)&gm_l75))[0] != 0);
                                        if (!t266) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l75) + 1, gm_srcgksioc7kn741, 38, 29);
                                        goto gm_lb32;
                                    }
gm_lb33:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 38, 13);
                                }
gm_lb32:;
                            }
gm_lb31:;
                            uint64_t t267 = gm_l45;
                            uint64_t r268; bool o269 = __builtin_add_overflow(t267, ((uint64_t)1ULL), &r268);
                            uint64_t t270 = r268;
                            gm_l45 = t270;
                        }
                    }
gm_lb29:;
                    {
                        void * t271 = gm_l41;
                        void * t272 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t271);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t273 = GM_LD(gm_b16_8, ((uint8_t *)&F.l43));
                        gm_tkaapcii86udk t274;
                        GM_ST(void *, ((uint8_t *)&t274 + 0), t272);
                        GM_ST(gm_b16_8, ((uint8_t *)&t274 + 8), t273);
                        memset(&F.l74, 0, sizeof F.l74);
                        GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)44ULL));
                        GM_ST(gm_tkaapcii86udk, ((uint8_t *)&F.l74 + 8), t274);
                    }
                    goto gm_lb1;
                }
gm_lb28:;
            }
            {
                {
                    uint8_t t275 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t276 = (uint8_t)(t275 == ((uint8_t)45ULL));
                    if (!t276) {
                        goto gm_lb34;
                    }
                    void * t277 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l47 = t277;
                    void * t278 = GM_LD(void *, (((((uint8_t *)t60) + 8) + 8) + 8));
                    gm_l48 = t278;
                    uint8_t t279 = GM_LD(uint8_t, (((((uint8_t *)t60) + 8) + 8) + 16));
                    gm_l49 = t279;
                    {
                        void * t280 = gm_l47;
                        void * t281 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t280);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t282 = gm_l48;
                        void * t283 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t282);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t284 = gm_l49;
                        gm_tlcl4u558l6p5 t285;
                        GM_ST(uint8_t, (uint8_t *)&t285 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&t285 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&t285 + 20, 0);
                        GM_ST(void *, ((uint8_t *)&t285 + 0), t281);
                        GM_ST(void *, ((uint8_t *)&t285 + 8), t283);
                        GM_ST(uint8_t, ((uint8_t *)&t285 + 16), t284);
                        memset(&F.l74, 0, sizeof F.l74);
                        GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)45ULL));
                        GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&F.l74 + 8), t285);
                    }
                    goto gm_lb1;
                }
gm_lb34:;
                {
                    uint8_t t286 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t287 = (uint8_t)(t286 == ((uint8_t)46ULL));
                    if (!t287) {
                        goto gm_lb35;
                    }
                    void * t288 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l50 = t288;
                    void * t289 = gm_l50;
                    void * t290 = gm_f_clone_block_5tu7v2v((void *)NULL, cx, NULL, t289);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    memset(&F.l74, 0, sizeof F.l74);
                    GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)46ULL));
                    GM_ST(void *, ((uint8_t *)&F.l74 + 8), t290);
                    goto gm_lb1;
                }
gm_lb35:;
                {
                    uint8_t t291 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t292 = (uint8_t)(t291 == ((uint8_t)47ULL));
                    if (!t292) {
                        goto gm_lb36;
                    }
                    {
                        memmove(((uint8_t *)&gm_l51), ((((uint8_t *)t60) + 8) + 8), 32);
                    }
                    {
                        gm_b16_8 t293 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l51));
                        gm_b16_8 t294 = GM_LD(gm_b16_8, ((uint8_t *)&t293));
                        gm_b16_8 t295 = gm_f_clone_conds_ljrasoa((void *)NULL, cx, NULL, t294);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t296 = GM_LD(void *, (((uint8_t *)&gm_l51) + 16));
                        void * t297 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t296);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t298 = GM_LD(void *, (((uint8_t *)&gm_l51) + 24));
                        void * t299 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t298);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_trl2khmuphncd t300;
                        GM_ST(gm_b16_8, ((uint8_t *)&t300 + 0), t295);
                        GM_ST(void *, ((uint8_t *)&t300 + 16), t297);
                        GM_ST(void *, ((uint8_t *)&t300 + 24), t299);
                        GM_ST(uint8_t, (uint8_t *)&F.l74 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&F.l74 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&F.l74 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&F.l74 + 40, 0);
                        GM_ST(uint64_t, (uint8_t *)&F.l74 + 48, 0);
                        GM_ST(uint64_t, (uint8_t *)&F.l74 + 56, 0);
                        GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)47ULL));
                        GM_ST(gm_trl2khmuphncd, ((uint8_t *)&F.l74 + 8), t300);
                    }
                    goto gm_lb1;
                }
gm_lb36:;
                {
                    uint8_t t301 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t302 = (uint8_t)(t301 == ((uint8_t)48ULL));
                    if (!t302) {
                        goto gm_lb37;
                    }
                    {
                        memmove(((uint8_t *)&F.l52), ((((uint8_t *)t60) + 8) + 8), 40);
                    }
                    {
                        uint32_t t303 = GM_LD(uint32_t, ((uint8_t *)&F.l52));
                        gm_b16_8 t304 = GM_LD(gm_b16_8, (((uint8_t *)&F.l52) + 8));
                        gm_b16_8 t305 = GM_LD(gm_b16_8, ((uint8_t *)&t304));
                        gm_b16_8 t306 = gm_f_clone_conds_ljrasoa((void *)NULL, cx, NULL, t305);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t307 = GM_LD(void *, (((uint8_t *)&F.l52) + 24));
                        void * t308 = gm_f_clone_block_5tu7v2v((void *)NULL, cx, NULL, t307);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t309 = GM_LD(void *, (((uint8_t *)&F.l52) + 32));
                        void * t310 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t309);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t710lsrq1m8gk t311;
                        GM_ST(uint32_t, (uint8_t *)&t311 + 4, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t311 + 0), t303);
                        GM_ST(gm_b16_8, ((uint8_t *)&t311 + 8), t306);
                        GM_ST(void *, ((uint8_t *)&t311 + 24), t308);
                        GM_ST(void *, ((uint8_t *)&t311 + 32), t310);
                        GM_ST(uint8_t, (uint8_t *)&F.l74 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&F.l74 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&F.l74 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&F.l74 + 48, 0);
                        GM_ST(uint64_t, (uint8_t *)&F.l74 + 56, 0);
                        GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)48ULL));
                        GM_ST(gm_t710lsrq1m8gk, ((uint8_t *)&F.l74 + 8), t311);
                    }
                    goto gm_lb1;
                }
gm_lb37:;
            }
            {
                {
                    uint8_t t312 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t313 = (uint8_t)(t312 == ((uint8_t)49ULL));
                    if (!t313) {
                        goto gm_lb38;
                    }
                    {
                        memmove(((uint8_t *)&gm_l53), ((((uint8_t *)t60) + 8) + 8), 24);
                    }
                    {
                        gm_te9hrk0u35rev t314 = GM_LD(gm_te9hrk0u35rev, ((uint8_t *)&gm_l53));
                        void * t315 = GM_LD(void *, (((uint8_t *)&gm_l53) + 16));
                        void * t316 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t315);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_teo9d4mncjfdu t317;
                        GM_ST(uint32_t, (uint8_t *)&t317 + 12, 0);
                        GM_ST(gm_te9hrk0u35rev, ((uint8_t *)&t317 + 0), t314);
                        GM_ST(void *, ((uint8_t *)&t317 + 16), t316);
                        memset(&F.l74, 0, sizeof F.l74);
                        GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)49ULL));
                        GM_ST(gm_teo9d4mncjfdu, ((uint8_t *)&F.l74 + 8), t317);
                    }
                    goto gm_lb1;
                }
gm_lb38:;
                {
                    uint8_t t318 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t319 = (uint8_t)(t318 == ((uint8_t)51ULL));
                    if (!t319) {
                        goto gm_lb39;
                    }
                    uint32_t t320 = GM_LD(uint32_t, ((((uint8_t *)t60) + 8) + 8));
                    gm_l54 = t320;
                    void * t321 = GM_LD(void *, (((((uint8_t *)t60) + 8) + 8) + 8));
                    gm_l55 = t321;
                    uint32_t t322 = gm_l54;
                    void * t323 = gm_l55;
                    void * t324 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t323);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_ttggq7ng4kp3p t325;
                    GM_ST(uint32_t, (uint8_t *)&t325 + 4, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t325 + 0), t322);
                    GM_ST(void *, ((uint8_t *)&t325 + 8), t324);
                    memset(&F.l74, 0, sizeof F.l74);
                    GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)51ULL));
                    GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&F.l74 + 8), t325);
                    goto gm_lb1;
                }
gm_lb39:;
                {
                    uint8_t t326 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t327 = (uint8_t)(t326 == ((uint8_t)52ULL));
                    if (!t327) {
                        goto gm_lb40;
                    }
                    void * t328 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l56 = t328;
                    void * t329 = gm_l56;
                    void * t330 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t329);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    memset(&F.l74, 0, sizeof F.l74);
                    GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)52ULL));
                    GM_ST(void *, ((uint8_t *)&F.l74 + 8), t330);
                    goto gm_lb1;
                }
gm_lb40:;
                {
                    uint8_t t331 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t332 = (uint8_t)(t331 == ((uint8_t)53ULL));
                    if (!t332) {
                        goto gm_lb41;
                    }
                    void * t333 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l57 = t333;
                    void * t334 = GM_LD(void *, (((((uint8_t *)t60) + 8) + 8) + 8));
                    gm_l58 = t334;
                    void * t335 = gm_l57;
                    void * t336 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t335);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t337 = gm_l58;
                    void * t338 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t337);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t3mf58gv9kf3n t339;
                    GM_ST(void *, ((uint8_t *)&t339 + 0), t336);
                    GM_ST(void *, ((uint8_t *)&t339 + 8), t338);
                    memset(&F.l74, 0, sizeof F.l74);
                    GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)53ULL));
                    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&F.l74 + 8), t339);
                    goto gm_lb1;
                }
gm_lb41:;
                {
                    uint8_t t340 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t341 = (uint8_t)(t340 == ((uint8_t)54ULL));
                    if (!t341) {
                        goto gm_lb42;
                    }
                    void * t342 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l59 = t342;
                    void * t343 = gm_l59;
                    void * t344 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t343);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    memset(&F.l74, 0, sizeof F.l74);
                    GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)54ULL));
                    GM_ST(void *, ((uint8_t *)&F.l74 + 8), t344);
                    goto gm_lb1;
                }
gm_lb42:;
            }
            {
                {
                    uint8_t t345 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t346 = (uint8_t)(t345 == ((uint8_t)55ULL));
                    if (!t346) {
                        goto gm_lb43;
                    }
                    void * t347 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l60 = t347;
                    {
                        void * t348 = gm_l60;
                        memmove(((uint8_t *)&F.l61), ((uint8_t *)t348), 40);
                    }
                    {
                        {
                            void * t349 = gm_l60;
                            uint8_t t350 = GM_LD(uint8_t, (((uint8_t *)t349) + 0));
                            uint8_t t351 = (uint8_t)(t350 == ((uint8_t)1ULL));
                            if (!t351) {
                                goto gm_lb45;
                            }
                            void * t352 = GM_LD(void *, (((uint8_t *)t349) + 8));
                            gm_l62 = t352;
                            void * t353 = gm_l62;
                            void * t354 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t353);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            GM_ST(uint8_t, (uint8_t *)&F.l61 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&F.l61 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&F.l61 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&F.l61 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&F.l61 + 24, 0);
                            GM_ST(uint64_t, (uint8_t *)&F.l61 + 32, 0);
                            GM_ST(uint8_t, ((uint8_t *)&F.l61 + 0), ((uint8_t)1ULL));
                            GM_ST(void *, ((uint8_t *)&F.l61 + 8), t354);
                            goto gm_lb44;
                        }
gm_lb45:;
                    }
gm_lb44:;
                    {
                        void * t355 = gm_l60;
                        gm_b16_8 t356 = GM_LD(gm_b16_8, (((uint8_t *)t355) + 40));
                        void * t357 = gm_l60;
                        gm_b16_8 t358 = GM_LD(gm_b16_8, (((uint8_t *)t357) + 56));
                        void * t359 = gm_l60;
                        gm_b16_8 t360 = GM_LD(gm_b16_8, (((uint8_t *)t359) + 72));
                        gm_b16_8 t361 = GM_LD(gm_b16_8, ((uint8_t *)&t360));
                        gm_th1g69d7vpipg t362 = GM_LD(gm_th1g69d7vpipg, ((uint8_t *)&F.l61));
                        gm_b16_8 t363 = gm_f_clone_exprs_umh77j3((void *)NULL, cx, NULL, t361);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t364 = gm_l60;
                        void * t365 = GM_LD(void *, (((uint8_t *)t364) + 88));
                        void * t366 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t365);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t150deeftog49 t367;
                        GM_ST(gm_th1g69d7vpipg, ((uint8_t *)&t367 + 0), t362);
                        GM_ST(gm_b16_8, ((uint8_t *)&t367 + 40), t356);
                        GM_ST(gm_b16_8, ((uint8_t *)&t367 + 56), t358);
                        GM_ST(gm_b16_8, ((uint8_t *)&t367 + 72), t363);
                        GM_ST(void *, ((uint8_t *)&t367 + 88), t366);
                        gm_tq7s98ihonkju t368 = gm_f_new_2pnaa87((void *)NULL, cx, NULL, &t367, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kl8b626vj63)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l76 = t368;
                    }
                    uint8_t t369 = (uint8_t)!((((uint8_t *)&gm_l76))[0] != 0);
                    if (!t369) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l76) + 8, gm_srcgksioc7kn741, 53, 28);
                    {
                        void * t370 = GM_LD(void *, (((uint8_t *)&gm_l76) + 8));
                        memset(&F.l74, 0, sizeof F.l74);
                        GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)55ULL));
                        GM_ST(void *, ((uint8_t *)&F.l74 + 8), t370);
                    }
                    goto gm_lb1;
                }
gm_lb43:;
                {
                    uint8_t t371 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t372 = (uint8_t)(t371 == ((uint8_t)56ULL));
                    if (!t372) {
                        goto gm_lb46;
                    }
                    void * t373 = GM_LD(void *, ((((uint8_t *)t60) + 8) + 8));
                    gm_l63 = t373;
                    {
                        void * t374 = gm_l63;
                        memmove(((uint8_t *)&F.l64), ((uint8_t *)t374), 40);
                    }
                    {
                        {
                            void * t375 = gm_l63;
                            uint8_t t376 = GM_LD(uint8_t, (((uint8_t *)t375) + 0));
                            uint8_t t377 = (uint8_t)(t376 == ((uint8_t)1ULL));
                            if (!t377) {
                                goto gm_lb48;
                            }
                            void * t378 = GM_LD(void *, (((uint8_t *)t375) + 8));
                            gm_l65 = t378;
                            void * t379 = gm_l65;
                            void * t380 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t379);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            GM_ST(uint8_t, (uint8_t *)&F.l64 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&F.l64 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&F.l64 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&F.l64 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&F.l64 + 24, 0);
                            GM_ST(uint64_t, (uint8_t *)&F.l64 + 32, 0);
                            GM_ST(uint8_t, ((uint8_t *)&F.l64 + 0), ((uint8_t)1ULL));
                            GM_ST(void *, ((uint8_t *)&F.l64 + 8), t380);
                            goto gm_lb47;
                        }
gm_lb48:;
                    }
gm_lb47:;
                    {
                        void * t381 = gm_l63;
                        void * t382 = GM_LD(void *, (((uint8_t *)t381) + 40));
                        gm_th1g69d7vpipg t383 = GM_LD(gm_th1g69d7vpipg, ((uint8_t *)&F.l64));
                        void * t384 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t382);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t385 = gm_l63;
                        void * t386 = GM_LD(void *, (((uint8_t *)t385) + 48));
                        void * t387 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t386);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t388 = gm_l63;
                        uint32_t t389 = GM_LD(uint32_t, (((uint8_t *)t388) + 56));
                        gm_t9i38ih07bojq t390;
                        GM_ST(uint32_t, (uint8_t *)&t390 + 60, 0);
                        GM_ST(gm_th1g69d7vpipg, ((uint8_t *)&t390 + 0), t383);
                        GM_ST(void *, ((uint8_t *)&t390 + 40), t384);
                        GM_ST(void *, ((uint8_t *)&t390 + 48), t387);
                        GM_ST(uint32_t, ((uint8_t *)&t390 + 56), t389);
                        gm_tq7s98ihonkju t391 = gm_f_new_ges19gd((void *)NULL, cx, NULL, &t390, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kg8c0kikv62)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l77 = t391;
                    }
                    uint8_t t392 = (uint8_t)!((((uint8_t *)&gm_l77))[0] != 0);
                    if (!t392) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l77) + 8, gm_srcgksioc7kn741, 58, 36);
                    {
                        void * t393 = GM_LD(void *, (((uint8_t *)&gm_l77) + 8));
                        memset(&F.l74, 0, sizeof F.l74);
                        GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)56ULL));
                        GM_ST(void *, ((uint8_t *)&F.l74 + 8), t393);
                    }
                    goto gm_lb1;
                }
gm_lb46:;
            }
            {
                {
                    uint8_t t394 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
                    uint8_t t395 = (uint8_t)(t394 == ((uint8_t)59ULL));
                    if (!t395) {
                        goto gm_lb49;
                    }
                    {
                        memmove(((uint8_t *)&F.l66), ((((uint8_t *)t60) + 8) + 8), 40);
                    }
                    {
                        gm_trqfvmjtt4sij t396;
                        gm_f_list_p0pjtrh((void *)&t396, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l67 = t396;
                    }
                    gm_b16_8 t397 = GM_LD(gm_b16_8, (((uint8_t *)&F.l66) + 8));
                    gm_l68 = t397;
                    gm_l69 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t398 = gm_l69;
                            uint64_t t399 = GM_LD(uint64_t, (((uint8_t *)&gm_l68) + 8));
                            uint8_t t400 = (t398 < t399);
                            if (!t400) {
                                goto gm_lb50;
                            }
                            {
                                void * t401 = GM_LD(void *, ((uint8_t *)&gm_l68));
                                uint64_t t402 = GM_LD(uint64_t, (((uint8_t *)&gm_l68) + 8));
                                uint64_t t403 = gm_l69;
                                {
                                    {
                                        memmove(((uint8_t *)&gm_l70), (((uint8_t *)t401) + (uintptr_t)t403 * 32), 32);
                                        {
                                            {
                                                uint8_t t404 = GM_LD(uint8_t, (((uint8_t *)&gm_l70) + 0));
                                                uint8_t t405 = (uint8_t)(t404 == ((uint8_t)0ULL));
                                                if (!t405) {
                                                    goto gm_lb56;
                                                }
                                                void * t406 = GM_LD(void *, (((uint8_t *)&gm_l70) + 8));
                                                gm_l71 = t406;
                                                void * t407 = (void *)((uint8_t *)&F.l67);
                                                void * t408 = gm_l71;
                                                void * t409 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t408);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_t4lnf71mruqpt t410;
                                                GM_ST(uint8_t, (uint8_t *)&t410 + 1, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t410 + 2, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t410 + 4, 0);
                                                GM_ST(uint64_t, (uint8_t *)&t410 + 16, 0);
                                                GM_ST(uint64_t, (uint8_t *)&t410 + 24, 0);
                                                GM_ST(uint8_t, ((uint8_t *)&t410 + 0), ((uint8_t)0ULL));
                                                GM_ST(void *, ((uint8_t *)&t410 + 8), t409);
                                                gm_tftb1odc739t5 t411 = gm_f_push_f5fsh94(t407, &t410);
                                                gm_l78 = t411;
                                                uint8_t t412 = (uint8_t)!((((uint8_t *)&gm_l78))[0] != 0);
                                                if (!t412) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l78) + 1, gm_srcgksioc7kn741, 64, 36);
                                                goto gm_lb55;
                                            }
gm_lb56:;
                                            {
                                                uint8_t t413 = GM_LD(uint8_t, (((uint8_t *)&gm_l70) + 0));
                                                uint8_t t414 = (uint8_t)(t413 == ((uint8_t)1ULL));
                                                if (!t414) {
                                                    goto gm_lb57;
                                                }
                                                gm_b16_8 t415 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l70) + 8));
                                                gm_l72 = t415;
                                                void * t416 = GM_LD(void *, ((((uint8_t *)&gm_l70) + 8) + 16));
                                                gm_l73 = t416;
                                                {
                                                    void * t417 = (void *)((uint8_t *)&F.l67);
                                                    gm_b16_8 t418 = gm_l72;
                                                    void * t419 = gm_l73;
                                                    void * t420 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t419);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_tpe1hv3fekn05 t421;
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t421 + 0), t418);
                                                    GM_ST(void *, ((uint8_t *)&t421 + 16), t420);
                                                    gm_t4lnf71mruqpt t422;
                                                    GM_ST(uint8_t, (uint8_t *)&t422 + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t422 + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t422 + 4, 0);
                                                    GM_ST(uint8_t, ((uint8_t *)&t422 + 0), ((uint8_t)1ULL));
                                                    GM_ST(gm_tpe1hv3fekn05, ((uint8_t *)&t422 + 8), t421);
                                                    gm_tftb1odc739t5 t423 = gm_f_push_f5fsh94(t417, &t422);
                                                    gm_l79 = t423;
                                                }
                                                uint8_t t424 = (uint8_t)!((((uint8_t *)&gm_l79))[0] != 0);
                                                if (!t424) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l79) + 1, gm_srcgksioc7kn741, 65, 40);
                                                goto gm_lb55;
                                            }
gm_lb57:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 63, 17);
                                        }
gm_lb55:;
                                        goto gm_lb53;
                                    }
gm_lb54:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 62, 13);
                                }
gm_lb53:;
                            }
gm_lb52:;
                            uint64_t t425 = gm_l69;
                            uint64_t r426; bool o427 = __builtin_add_overflow(t425, ((uint64_t)1ULL), &r426);
                            uint64_t t428 = r426;
                            gm_l69 = t428;
                        }
                    }
gm_lb50:;
                    {
                        uint32_t t429 = GM_LD(uint32_t, ((uint8_t *)&F.l66));
                        gm_b16_8 t430 = GM_LD(gm_b16_8, ((uint8_t *)&F.l67));
                        void * t431 = GM_LD(void *, (((uint8_t *)&F.l66) + 24));
                        void * t432 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t431);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t433 = GM_LD(void *, (((uint8_t *)&F.l66) + 32));
                        void * t434 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t433);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t710lsrq1m8gk t435;
                        GM_ST(uint32_t, (uint8_t *)&t435 + 4, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t435 + 0), t429);
                        GM_ST(gm_b16_8, ((uint8_t *)&t435 + 8), t430);
                        GM_ST(void *, ((uint8_t *)&t435 + 24), t432);
                        GM_ST(void *, ((uint8_t *)&t435 + 32), t434);
                        GM_ST(uint8_t, (uint8_t *)&F.l74 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&F.l74 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&F.l74 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&F.l74 + 48, 0);
                        GM_ST(uint64_t, (uint8_t *)&F.l74 + 56, 0);
                        GM_ST(uint8_t, ((uint8_t *)&F.l74 + 0), ((uint8_t)59ULL));
                        GM_ST(gm_t710lsrq1m8gk, ((uint8_t *)&F.l74 + 8), t435);
                    }
                    goto gm_lb1;
                }
gm_lb49:;
            }
            {
                {
                    void * t436 = gm_l0;
                    memmove(((uint8_t *)&F.l74), (((uint8_t *)t436) + 8), 64);
                    goto gm_lb1;
                }
gm_lb58:;
                gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 7, 19);
            }
        }
gm_lb1:;
    }
    {
        memmove(((uint8_t *)&F.l74), ((uint8_t *)&F.l74), 64);
        void * t437 = gm_l0;
        gm_t88rcucg1b7uk t438 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t437));
        void * t439 = gm_l0;
        gm_tl874t2atnlh3 t440 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t439) + 72));
        void * t441 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t438, &(*(gm_to0f5qn75cgg8 *)((uint8_t *)&F.l74)), t440);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t441;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_clone_exprs_umh77j3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_clone_exprs_umh77j3 F;
    gm_b16_8 gm_l0; /* xs */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK(&F, 230u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l5;
    gm_trqfvmjtt4sij t7;
    gm_f_list_vp1djv1_2((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t7;
    gm_l3 = ((uint64_t)0ULL);
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_l2 = t8;
    {
        for (;;) {
            uint64_t t9 = gm_l3;
            uint64_t t10 = gm_l2;
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t12 = gm_l3;
                        gm_l4 = t12;
                        void * t13 = (void *)((uint8_t *)&F.l1);
                        void * t14 = GM_LD(void *, ((uint8_t *)&gm_l0));
                        uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                        uint64_t t16 = gm_l4;
                        void * t17 = (void *)(((uint8_t *)t14) + (uintptr_t)t16 * 88);
                        void * t18 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t17);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t19 = gm_f_push_d943vgp_2(t13, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t18)));
                        gm_l5 = t19;
                        uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
                        if (!t20) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srcgksioc7kn741, 82, 26);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 82, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t21 = gm_l3;
            uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
            uint64_t t24 = r22;
            gm_l3 = t24;
        }
    }
gm_lb1:;
    gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    F.ret = t25;
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
extern __attribute__((aligned(4))) void * gm_f_clone_opt_plt4ceg(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_clone_pat_cq2uu68(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_clone_pat_cq2uu68 F;
    void * gm_l0; /* p */
    void * gm_l1; /* e */
    void * gm_l2; /* q */
    void * gm_l3; /* q */
    void * gm_l4; /* q */
    uint32_t gm_l5; /* v */
    uint32_t gm_l6; /* v */
    void * gm_l7; /* q */
    gm_b16_8 gm_l8; /* fs */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_ttggq7ng4kp3p gm_l12; /* f */
    gm_b16_8 gm_l13; /* ps */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    void * gm_l17; /* q */
    GM_STACK_CHECK(&F, 810u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l19;
    gm_tftb1odc739t5 gm_l20;
    void * t22 = gm_l0;
    {
        {
            uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)2ULL));
            if (!t24) {
                goto gm_lb2;
            }
            void * t25 = GM_LD(void *, ((((uint8_t *)t22) + 8) + 8));
            gm_l1 = t25;
            void * t26 = gm_l1;
            void * t27 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            memset(&F.l18, 0, sizeof F.l18);
            GM_ST(uint8_t, ((uint8_t *)&F.l18 + 0), ((uint8_t)2ULL));
            GM_ST(void *, ((uint8_t *)&F.l18 + 8), t27);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t29 = (uint8_t)(t28 == ((uint8_t)5ULL));
            if (!t29) {
                goto gm_lb3;
            }
            void * t30 = GM_LD(void *, ((((uint8_t *)t22) + 8) + 8));
            gm_l2 = t30;
            void * t31 = gm_l2;
            void * t32 = gm_f_clone_pat_cq2uu68((void *)NULL, cx, NULL, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            memset(&F.l18, 0, sizeof F.l18);
            GM_ST(uint8_t, ((uint8_t *)&F.l18 + 0), ((uint8_t)5ULL));
            GM_ST(void *, ((uint8_t *)&F.l18 + 8), t32);
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t34 = (uint8_t)(t33 == ((uint8_t)6ULL));
            if (!t34) {
                goto gm_lb4;
            }
            void * t35 = GM_LD(void *, ((((uint8_t *)t22) + 8) + 8));
            gm_l3 = t35;
            void * t36 = gm_l3;
            void * t37 = gm_f_clone_pat_cq2uu68((void *)NULL, cx, NULL, t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            memset(&F.l18, 0, sizeof F.l18);
            GM_ST(uint8_t, ((uint8_t *)&F.l18 + 0), ((uint8_t)6ULL));
            GM_ST(void *, ((uint8_t *)&F.l18 + 8), t37);
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t39 = (uint8_t)(t38 == ((uint8_t)7ULL));
            if (!t39) {
                goto gm_lb5;
            }
            void * t40 = GM_LD(void *, ((((uint8_t *)t22) + 8) + 8));
            gm_l4 = t40;
            void * t41 = gm_l4;
            void * t42 = gm_f_clone_pat_cq2uu68((void *)NULL, cx, NULL, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            memset(&F.l18, 0, sizeof F.l18);
            GM_ST(uint8_t, ((uint8_t *)&F.l18 + 0), ((uint8_t)7ULL));
            GM_ST(void *, ((uint8_t *)&F.l18 + 8), t42);
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t44 = (uint8_t)(t43 == ((uint8_t)8ULL));
            if (!t44) {
                goto gm_lb6;
            }
            uint32_t t45 = GM_LD(uint32_t, ((((uint8_t *)t22) + 8) + 8));
            gm_l5 = t45;
            uint8_t t46 = (uint8_t)(GM_LD(void *, (((((uint8_t *)t22) + 8) + 8) + 8)) != 0);
            if (t46) {
                goto gm_lb6;
            }
            uint32_t t47 = gm_l5;
            gm_ttggq7ng4kp3p t48;
            GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t48 + 0), t47);
            GM_ST(void *, ((uint8_t *)&t48 + 8), ((void *)0));
            memset(&F.l18, 0, sizeof F.l18);
            GM_ST(uint8_t, ((uint8_t *)&F.l18 + 0), ((uint8_t)8ULL));
            GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&F.l18 + 8), t48);
            goto gm_lb1;
        }
gm_lb6:;
        {
            uint8_t t49 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t50 = (uint8_t)(t49 == ((uint8_t)8ULL));
            if (!t50) {
                goto gm_lb7;
            }
            uint32_t t51 = GM_LD(uint32_t, ((((uint8_t *)t22) + 8) + 8));
            gm_l6 = t51;
            uint8_t t52 = (uint8_t)(GM_LD(void *, (((((uint8_t *)t22) + 8) + 8) + 8)) != 0);
            if (!t52) {
                goto gm_lb7;
            }
            void * t53 = GM_LD(void *, (((((uint8_t *)t22) + 8) + 8) + 8));
            gm_l7 = t53;
            uint32_t t54 = gm_l6;
            void * t55 = gm_l7;
            void * t56 = gm_f_clone_pat_cq2uu68((void *)NULL, cx, NULL, t55);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_ttggq7ng4kp3p t57;
            GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t57 + 0), t54);
            GM_ST(void *, ((uint8_t *)&t57 + 8), t56);
            memset(&F.l18, 0, sizeof F.l18);
            GM_ST(uint8_t, ((uint8_t *)&F.l18 + 0), ((uint8_t)8ULL));
            GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&F.l18 + 8), t57);
            goto gm_lb1;
        }
gm_lb7:;
        {
            uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t59 = (uint8_t)(t58 == ((uint8_t)9ULL));
            if (!t59) {
                goto gm_lb8;
            }
            gm_b16_8 t60 = GM_LD(gm_b16_8, ((((uint8_t *)t22) + 8) + 8));
            gm_l8 = t60;
            {
                gm_trqfvmjtt4sij t61;
                gm_f_list_i33ccm0((void *)&t61, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l9 = t61;
            }
            gm_b16_8 t62 = gm_l8;
            gm_l10 = t62;
            gm_l11 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t63 = gm_l11;
                    uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                    uint8_t t65 = (t63 < t64);
                    if (!t65) {
                        goto gm_lb9;
                    }
                    {
                        void * t66 = GM_LD(void *, ((uint8_t *)&gm_l10));
                        uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                        uint64_t t68 = gm_l11;
                        {
                            {
                                gm_ttggq7ng4kp3p t69 = GM_LD(gm_ttggq7ng4kp3p, (((uint8_t *)t66) + (uintptr_t)t68 * 16));
                                gm_l12 = t69;
                                void * t70 = (void *)((uint8_t *)&F.l9);
                                uint32_t t71 = GM_LD(uint32_t, ((uint8_t *)&gm_l12));
                                void * t72 = GM_LD(void *, (((uint8_t *)&gm_l12) + 8));
                                void * t73 = gm_f_clone_pat_cq2uu68((void *)NULL, cx, NULL, t72);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_ttggq7ng4kp3p t74;
                                GM_ST(uint32_t, (uint8_t *)&t74 + 4, 0);
                                GM_ST(uint32_t, ((uint8_t *)&t74 + 0), t71);
                                GM_ST(void *, ((uint8_t *)&t74 + 8), t73);
                                gm_tftb1odc739t5 t75 = gm_f_push_31airv7(t70, GM_CONV(gm_csqbqksqco2bm6, t74));
                                gm_l19 = t75;
                                uint8_t t76 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                                if (!t76) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_srcgksioc7kn741, 127, 27);
                                goto gm_lb12;
                            }
gm_lb13:;
                            gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 127, 13);
                        }
gm_lb12:;
                    }
gm_lb11:;
                    uint64_t t77 = gm_l11;
                    uint64_t r78; bool o79 = __builtin_add_overflow(t77, ((uint64_t)1ULL), &r78);
                    uint64_t t80 = r78;
                    gm_l11 = t80;
                }
            }
gm_lb9:;
            {
                gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
                memset(&F.l18, 0, sizeof F.l18);
                GM_ST(uint8_t, ((uint8_t *)&F.l18 + 0), ((uint8_t)9ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&F.l18 + 8), t81);
            }
            goto gm_lb1;
        }
gm_lb8:;
        {
            uint8_t t82 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t83 = (uint8_t)(t82 == ((uint8_t)10ULL));
            if (!t83) {
                goto gm_lb14;
            }
            gm_b16_8 t84 = GM_LD(gm_b16_8, ((((uint8_t *)t22) + 8) + 8));
            gm_l13 = t84;
            {
                gm_trqfvmjtt4sij t85;
                gm_f_list_nh3d5t2((void *)&t85, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l14 = t85;
            }
            gm_b16_8 t86 = gm_l13;
            gm_l15 = t86;
            gm_l16 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t87 = gm_l16;
                    uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                    uint8_t t89 = (t87 < t88);
                    if (!t89) {
                        goto gm_lb15;
                    }
                    {
                        void * t90 = GM_LD(void *, ((uint8_t *)&gm_l15));
                        uint64_t t91 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                        uint64_t t92 = gm_l16;
                        {
                            {
                                void * t93 = GM_LD(void *, (((uint8_t *)t90) + (uintptr_t)t92 * 8));
                                gm_l17 = t93;
                                void * t94 = (void *)((uint8_t *)&F.l14);
                                void * t95 = gm_l17;
                                void * t96 = gm_f_clone_pat_cq2uu68((void *)NULL, cx, NULL, t95);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tftb1odc739t5 t97 = gm_f_push_u2jka7o(t94, t96);
                                gm_l20 = t97;
                                uint8_t t98 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                                if (!t98) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_srcgksioc7kn741, 132, 27);
                                goto gm_lb18;
                            }
gm_lb19:;
                            gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 132, 13);
                        }
gm_lb18:;
                    }
gm_lb17:;
                    uint64_t t99 = gm_l16;
                    uint64_t r100; bool o101 = __builtin_add_overflow(t99, ((uint64_t)1ULL), &r100);
                    uint64_t t102 = r100;
                    gm_l16 = t102;
                }
            }
gm_lb15:;
            {
                gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
                memset(&F.l18, 0, sizeof F.l18);
                GM_ST(uint8_t, ((uint8_t *)&F.l18 + 0), ((uint8_t)10ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&F.l18 + 8), t103);
            }
            goto gm_lb1;
        }
gm_lb14:;
        {
            void * t104 = gm_l0;
            memmove(((uint8_t *)&F.l18), (((uint8_t *)t104) + 8), 112);
            goto gm_lb1;
        }
gm_lb20:;
        gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 118, 18);
    }
gm_lb1:;
    {
        memmove(((uint8_t *)&F.l18), ((uint8_t *)&F.l18), 112);
    }
    {
        void * t105 = gm_l0;
        gm_t88rcucg1b7uk t106 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t105));
        void * t107 = gm_l0;
        gm_tl874t2atnlh3 t108 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t107) + 120));
        void * t109 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t106, &(*(gm_tb38v5s9j1r32 *)((uint8_t *)&F.l18)), t108);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t109;
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

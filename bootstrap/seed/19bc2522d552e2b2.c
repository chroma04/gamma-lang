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
typedef struct { int64_t f0; uint8_t f1; uint8_t pe[7]; } gm_t913bb735pvt5;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t f3; uint8_t f4; } gm_tb1msgo1fu1n3;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint8_t f3; uint8_t pe[7]; } gm_t4kmic8080rrj;
typedef struct { gm_tqs5p4n2tq19v f0; gm_tqs5p4n2tq19v f1; } gm_t5r6k0fdcfsb3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t913bb735pvt5) == 16 && _Alignof(gm_t913bb735pvt5) == 8 && offsetof(gm_t913bb735pvt5, f0) == 0 && offsetof(gm_t913bb735pvt5, f1) == 8, "layout of `?i64`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tb1msgo1fu1n3) == 5 && _Alignof(gm_tb1msgo1fu1n3) == 1 && offsetof(gm_tb1msgo1fu1n3, f0) == 0 && offsetof(gm_tb1msgo1fu1n3, f1) == 1 && offsetof(gm_tb1msgo1fu1n3, f2) == 2 && offsetof(gm_tb1msgo1fu1n3, f3) == 3 && offsetof(gm_tb1msgo1fu1n3, f4) == 4, "layout of `(u8, u8, u8, u8, u8)`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_tqs5p4n2tq19v f0; gm_tqs5p4n2tq19v f1; gm_tqs5p4n2tq19v f2; gm_tqs5p4n2tq19v f3; gm_tqs5p4n2tq19v f4; } gm_tcdtdmenq84v9;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint64_t f0; gm_b16_8 f1; uint64_t f2; } gm_tob1snuii5bme;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tob1snuii5bme v0; gm_trqfvmjtt4sij v1; } f1; } gm_t21m1eu5ta01n;
_Static_assert(sizeof(gm_t4kmic8080rrj) == 40 && _Alignof(gm_t4kmic8080rrj) == 8 && offsetof(gm_t4kmic8080rrj, f0) == 0 && offsetof(gm_t4kmic8080rrj, f1) == 16 && offsetof(gm_t4kmic8080rrj, f2) == 24 && offsetof(gm_t4kmic8080rrj, f3) == 32, "layout of `Reader`");
_Static_assert(sizeof(gm_t5r6k0fdcfsb3) == 4 && _Alignof(gm_t5r6k0fdcfsb3) == 1 && offsetof(gm_t5r6k0fdcfsb3, f0) == 0 && offsetof(gm_t5r6k0fdcfsb3, f1) == 2, "layout of `(?u8, ?u8)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tcdtdmenq84v9) == 10 && _Alignof(gm_tcdtdmenq84v9) == 1 && offsetof(gm_tcdtdmenq84v9, f0) == 0 && offsetof(gm_tcdtdmenq84v9, f1) == 2 && offsetof(gm_tcdtdmenq84v9, f2) == 4 && offsetof(gm_tcdtdmenq84v9, f3) == 6 && offsetof(gm_tcdtdmenq84v9, f4) == 8, "layout of `(?u8, ?u8, ?u8, ?u8, ?u8)`");
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; int64_t v1; uint8_t v2; gm_trqfvmjtt4sij v3; void * v4; gm_b16_8 v5; } f1; } gm_tktr4rmbindgv;
_Static_assert(sizeof(gm_tob1snuii5bme) == 32 && _Alignof(gm_tob1snuii5bme) == 8 && offsetof(gm_tob1snuii5bme, f0) == 0 && offsetof(gm_tob1snuii5bme, f1) == 8 && offsetof(gm_tob1snuii5bme, f2) == 24, "layout of `Reloc`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tob1snuii5bme v0; } f1; } gm_tr1k5nl69lhjo;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tob1snuii5bme v0; void * v1; } f1; } gm_trssmtuv8jr0v;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tob1snuii5bme v0; gm_b16_8 v1; } f1; } gm_tu615l4qlr8b2;
_Static_assert(sizeof(gm_t21m1eu5ta01n) == 48 && _Alignof(gm_t21m1eu5ta01n) == 8 && offsetof(gm_t21m1eu5ta01n, f0) == 0 && offsetof(gm_t21m1eu5ta01n, f1) == 8, "layout of `TomlError!List(string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tob1snuii5bme v0; gm_tktr4rmbindgv v1; } f1; } gm_tefknldmnh2si;
_Static_assert(sizeof(gm_tktr4rmbindgv) == 48 && _Alignof(gm_tktr4rmbindgv) == 8 && offsetof(gm_tktr4rmbindgv, f0) == 0 && offsetof(gm_tktr4rmbindgv, f1) == 8, "layout of `Value`");
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_tr1k5nl69lhjo) == 40 && _Alignof(gm_tr1k5nl69lhjo) == 8 && offsetof(gm_tr1k5nl69lhjo, f0) == 0 && offsetof(gm_tr1k5nl69lhjo, f1) == 8, "layout of `TomlError!()`");
typedef struct { gm_b16_8 f0; gm_tktr4rmbindgv f1; uint64_t f2; } gm_trc3jdv2g5qre;
_Static_assert(sizeof(gm_trssmtuv8jr0v) == 40 && _Alignof(gm_trssmtuv8jr0v) == 8 && offsetof(gm_trssmtuv8jr0v, f0) == 0 && offsetof(gm_trssmtuv8jr0v, f1) == 8, "layout of `TomlError!^Table`");
_Static_assert(sizeof(gm_tu615l4qlr8b2) == 40 && _Alignof(gm_tu615l4qlr8b2) == 8 && offsetof(gm_tu615l4qlr8b2, f0) == 0 && offsetof(gm_tu615l4qlr8b2, f1) == 8, "layout of `TomlError!string`");
_Static_assert(sizeof(gm_tefknldmnh2si) == 56 && _Alignof(gm_tefknldmnh2si) == 8 && offsetof(gm_tefknldmnh2si, f0) == 0 && offsetof(gm_tefknldmnh2si, f1) == 8, "layout of `TomlError!Value`");
_Static_assert(sizeof(gm_trc3jdv2g5qre) == 72 && _Alignof(gm_trc3jdv2g5qre) == 8 && offsetof(gm_trc3jdv2g5qre, f0) == 0 && offsetof(gm_trc3jdv2g5qre, f1) == 16 && offsetof(gm_trc3jdv2g5qre, f2) == 64, "layout of `Entry`");
struct gm_fr_array_canpt3e {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* items */
    gm_tefknldmnh2si ret;
};
struct gm_fr_as_str_982o16v {
    gm_frame h;
    gm_tktr4rmbindgv l0; /* v */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_as_table_4f7n31g {
    gm_frame h;
    gm_tktr4rmbindgv l0; /* v */
    void * ret;
};
struct gm_fr_at_line_6uoncqb {
    gm_frame h;
    gm_tob1snuii5bme ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_dheabhi_3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_mja1o69 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_end_of_line_056kej4 {
    gm_frame h;
    gm_tr1k5nl69lhjo ret;
};
struct gm_fr_error_96ci5lu {
    gm_frame h;
    gm_tob1snuii5bme ret;
};
struct gm_fr_format_0pvgsic {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_u10ruvh {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_get_9rme0gb {
    gm_frame h;
    void * ret;
};
struct gm_fr_hex_escape_eoc96dk {
    gm_frame h;
    gm_tr1k5nl69lhjo ret;
};
struct gm_fr_inline_table_cvvpgel {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* path */
    gm_tktr4rmbindgv l4; /* v */
    gm_tefknldmnh2si ret;
};
struct gm_fr_insert_path_cvuqfkc {
    gm_frame h;
    gm_tktr4rmbindgv l2; /* v */
    gm_tr1k5nl69lhjo ret;
};
struct gm_fr_key_5clrcin {
    gm_frame h;
    gm_tu615l4qlr8b2 ret;
};
struct gm_fr_key_path_7spb050 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* path */
    gm_t21m1eu5ta01n ret;
};
struct gm_fr_list_0202sdh_3 {
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
struct gm_fr_list_vvcbrjf {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_new_table_hh6cki7 {
    gm_frame h;
    void * ret;
};
struct gm_fr_number_4l95pjb {
    gm_frame h;
    gm_tefknldmnh2si ret;
};
struct gm_fr_parse_string_80niubc {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* out */
    gm_tu615l4qlr8b2 ret;
};
struct gm_fr_parse_toml_gjtmf79 {
    gm_frame h;
    gm_t4kmic8080rrj l1; /* r */
    gm_trqfvmjtt4sij l6; /* path */
    gm_tktr4rmbindgv l7; /* v */
    gm_trssmtuv8jr0v ret;
};
struct gm_fr_peek_bkdvl65 {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_push_0pgacsj_3 {
    gm_frame h;
    gm_tktr4rmbindgv l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_2pp1ri6 {
    gm_frame h;
    gm_trc3jdv2g5qre l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
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
struct gm_fr_regrow_2o1229b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ddqa9t6_3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_skip_space_and_newlines_qagb1bk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_skip_space_voq272o {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_table_header_j60mb6n {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* path */
    gm_trqfvmjtt4sij l13; /* items */
    gm_trqfvmjtt4sij l15; /* grown */
    gm_trqfvmjtt4sij l17; /* items */
    gm_trqfvmjtt4sij l20; /* items */
    gm_trssmtuv8jr0v ret;
};
struct gm_fr_value_oq952o2 {
    gm_frame h;
    gm_tefknldmnh2si ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void gm_f_array_canpt3e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_as_str_982o16v(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_as_str_982o16v_c(gm_tktr4rmbindgv a0);
inline __attribute__((aligned(4))) void * gm_f_as_table_4f7n31g(const void * a0);
__attribute__((aligned(4))) void * gm_f_as_table_4f7n31g_c(gm_tktr4rmbindgv a0);
inline __attribute__((aligned(4))) void gm_f_at_line_6uoncqb(void *gm_out, const void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tob1snuii5bme gm_f_at_line_6uoncqb_c(gm_tob1snuii5bme a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dheabhi_3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_mja1o69(void);
__attribute__((aligned(4))) void gm_f_end_of_line_056kej4(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tr1k5nl69lhjo gm_f_end_of_line_056kej4_c(void * a0);
__attribute__((aligned(4))) void gm_f_error_96ci5lu(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tob1snuii5bme gm_f_error_96ci5lu_c(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_format_0pvgsic(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_u10ruvh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void * gm_f_get_9rme0gb(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_hex_escape_eoc96dk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_inline_table_cvvpgel(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_insert_path_cvuqfkc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, uint64_t a3);
__attribute__((aligned(4))) void gm_f_key_5clrcin(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_key_path_7spb050(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_list_0202sdh_3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vvcbrjf(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_9ani70g(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void * gm_f_new_table_hh6cki7(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_number_4l95pjb(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tefknldmnh2si gm_f_number_4l95pjb_c(void * a0);
__attribute__((aligned(4))) void gm_f_parse_string_80niubc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_toml_gjtmf79(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_bkdvl65(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0pgacsj_3(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_2pp1ri6(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2o1229b(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ddqa9t6_3(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) void gm_f_skip_space_and_newlines_qagb1bk(void * a0);
__attribute__((aligned(4))) void gm_f_skip_space_voq272o(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_table_header_j60mb6n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_value_oq952o2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
extern uint8_t gm_cal7njs7es4[16] __attribute__((aligned(8)));
extern uint8_t gm_cn9bf8d1ktm[16] __attribute__((aligned(8)));
extern uint8_t gm_kt2nhjs8rv5[40] __attribute__((aligned(8)));
extern const uint8_t gm_s2a4d8jj74v6o[49];
extern const uint8_t gm_s4lo69slu4igt[8];
extern const uint8_t gm_s4pfhdi3f6f58[44];
extern const uint8_t gm_s70iivik802q4[26];
extern const uint8_t gm_s8lbhmq10rt7i[42];
extern const uint8_t gm_s8rm42n03sfss[4];
extern const uint8_t gm_scvuqmspcpb1m[6];
extern const uint8_t gm_sdq87al6fhs9i[35];
extern const uint8_t gm_sehko5bb96s6p[5];
extern const uint8_t gm_sfa86erpod86s[4];
extern const uint8_t gm_shlilv0hq3bqu[33];
extern const uint8_t gm_sithisfj5qgfv[67];
extern const uint8_t gm_sj294dvmirbmr[47];
extern const uint8_t gm_skiffh4urhl7e[14];
extern const uint8_t gm_sl2lo3hki3t4d[36];
extern const uint8_t gm_sne7l93pr2tmb[34];
extern const uint8_t gm_sngfc91643s6a[55];
extern const uint8_t gm_sns75reqq32d2[4];
extern const uint8_t gm_soakgb2h3g40i[4];
extern const uint8_t gm_sogehh8t1m7tn[6];
extern const uint8_t gm_spl83f693c752[32];
extern const uint8_t gm_sq5stv2m4p0h1[37];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3nt8pf0qk5gn[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srjddrdnspo96[29];
extern const uint8_t gm_srsr8fvlin2g6[65];
extern const uint8_t gm_std877jr2ed05[29];
extern const uint8_t gm_suonmnnb7u0t1[55];
extern const uint8_t gm_sv5o4caljvbmj[28];
extern const uint8_t gm_svfa039r0e6te[33];
extern const gm_tmpl gm_tpl0td8lr3f4ven;
extern const gm_tmpl gm_tplvvfpp9olf6cl;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b) {
    if (a[1] != b[1]) return false;
    return !a[1] || gm_eqjmccqdb90f(a, b);
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline __attribute__((aligned(4))) void gm_f_as_str_982o16v(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_as_str_982o16v F;
    gm_b16_8 gm_l1; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 74u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_th2lqbo77pvsc gm_l2;
    {
        {
            uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)0ULL));
            if (!t6) {
                goto gm_lb2;
            }
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
            gm_l1 = t7;
            gm_b16_8 t8 = gm_l1;
            GM_ST(uint8_t, (uint8_t *)&gm_l2 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&gm_l2 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&gm_l2 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&gm_l2), t8);
            ((uint8_t *)&gm_l2)[16] = 1;
            goto gm_lb1;
        }
gm_lb2:;
        {
            memset(&gm_l2, 0, sizeof gm_l2);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 50, 51);
    }
gm_lb1:;
    memmove(gm_out, ((uint8_t *)&gm_l2), 24);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void * gm_f_as_table_4f7n31g(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_as_table_4f7n31g F;
    void * gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 58u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * gm_l2;
    {
        {
            uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)4ULL));
            if (!t6) {
                goto gm_lb2;
            }
            void * t7 = GM_LD(void *, (((uint8_t *)a0) + 8));
            gm_l1 = t7;
            void * t8 = gm_l1;
            gm_l2 = t8;
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l2 = ((void *)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 56, 53);
    }
gm_lb1:;
    void * t9 = gm_l2;
    F.ret = t9;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_at_line_6uoncqb(void *gm_out, const void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_at_line_6uoncqb F;
    gm_tob1snuii5bme gm_l0; /* e */
    uint64_t gm_l1; /* line */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    uint64_t t2 = gm_l1;
    gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)a0) + 24));
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_tob1snuii5bme *)gm_out) + 0), t2);
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tob1snuii5bme *)gm_out) + 8), t3);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_tob1snuii5bme *)gm_out) + 24), t4);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dheabhi_3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_dheabhi_3 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_mja1o69(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_mja1o69 F;
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
inline __attribute__((aligned(4))) void gm_f_format_0pvgsic(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_0pvgsic F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_svfa039r0e6te));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(32));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplvvfpp9olf6cl), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_u10ruvh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_u10ruvh F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s4pfhdi3f6f58));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(43));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl0td8lr3f4ven), t2);
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
inline __attribute__((aligned(4))) void gm_f_list_0202sdh_3(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0202sdh_3 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_dheabhi_3();
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
inline __attribute__((aligned(4))) void gm_f_list_vvcbrjf(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vvcbrjf F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_mja1o69();
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
inline __attribute__((aligned(4))) void * gm_f_new_table_hh6cki7(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_new_table_hh6cki7 F;
    GM_STACK_CHECK(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tq7s98ihonkju gm_l0;
    {
        gm_trqfvmjtt4sij t2;
        gm_f_list_vvcbrjf((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tqetrgo0q24b5 t3;
        GM_ST(uint8_t, (uint8_t *)&t3 + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&t3 + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&t3 + 44, 0);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t3 + 0), t2);
        GM_ST(uint8_t, ((uint8_t *)&t3 + 40), ((uint8_t)0));
        gm_tq7s98ihonkju t4 = gm_f_new_9ani70g((void *)NULL, cx, NULL, &t3, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kt2nhjs8rv5)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l0 = t4;
    }
    uint8_t t5 = (uint8_t)!((((uint8_t *)&gm_l0))[0] != 0);
    if (!t5) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l0) + 8, gm_src3nt8pf0qk5gn, 39, 44);
    void * t6 = GM_LD(void *, (((uint8_t *)&gm_l0) + 8));
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
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_bkdvl65(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_peek_bkdvl65 F;
    void * gm_l0; /* r */
    GM_STACK_CHECK_SHALLOW(&F, 62u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tqs5p4n2tq19v gm_l1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
    uint8_t t7 = (t6 < t4);
    if (t7) {
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)t3) + (uintptr_t)t6 * 1));
        gm_tqs5p4n2tq19v t9;
        GM_ST(uint8_t, ((uint8_t *)&t9), t8);
        ((uint8_t *)&t9)[1] = 1;
        gm_l1 = t9;
    } else {
        gm_tqs5p4n2tq19v t10;
        memset(&t10, 0, sizeof t10);
        gm_l1 = t10;
    }
    gm_tqs5p4n2tq19v t11 = gm_l1;
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0pgacsj_3(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_0pgacsj_3 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_dheabhi_3();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_ddqa9t6_3(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_2pp1ri6(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_2pp1ri6 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_mja1o69();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_2o1229b(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 72), ((uint8_t *)a1), 72);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2o1229b(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_2o1229b F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ddqa9t6_3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ddqa9t6_3 F;
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
__attribute__((aligned(4))) void gm_f_array_canpt3e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_array_canpt3e F;
    void * gm_l0; /* r */
    uint64_t gm_l1; /* first_line */
    uint64_t gm_l3; /* before */
    GM_STACK_CHECK(&F, 890u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l4;
    gm_tqs5p4n2tq19v gm_l5;
    gm_tefknldmnh2si gm_l6;
    gm_tftb1odc739t5 gm_l7;
    uint8_t gm_l8;
    gm_tqs5p4n2tq19v gm_l9;
    void * t13 = gm_l0;
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 24));
    gm_l1 = t14;
    void * t15 = gm_l0;
    void * t16 = gm_l0;
    uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t16) + 16));
    uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)1ULL), &r18);
    if (o19) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 473, 5);
    uint64_t t20 = r18;
    GM_ST(uint64_t, (((uint8_t *)t15) + 16), t20);
    {
        gm_trqfvmjtt4sij t21;
        gm_f_list_0202sdh_3((void *)&t21, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t21;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb1;
            }
            {
                void * t22 = gm_l0;
                gm_f_skip_space_and_newlines_qagb1bk(t22);
                void * t23 = gm_l0;
                gm_tqs5p4n2tq19v t24 = gm_f_peek_bkdvl65(t23);
                gm_tqs5p4n2tq19v t25;
                GM_ST(uint8_t, ((uint8_t *)&t25), ((uint8_t)93ULL));
                ((uint8_t *)&t25)[1] = 1;
                uint8_t t26 = (gm_eqb44cb3cf1n(((uint8_t *)&t24), ((uint8_t *)&t25)));
                if (t26) {
                    goto gm_lb1;
                }
                void * t27 = gm_l0;
                gm_tqs5p4n2tq19v t28 = gm_f_peek_bkdvl65(t27);
                gm_l5 = t28;
                {
                    {
                        uint8_t t29 = (uint8_t)((((uint8_t *)&gm_l5))[1] != 0);
                        if (t29) {
                            goto gm_lb5;
                        }
                        gm_l4 = ((uint8_t)1);
                        goto gm_lb4;
                    }
gm_lb5:;
                    {
                        gm_l4 = ((uint8_t)0);
                        goto gm_lb4;
                    }
gm_lb6:;
                    gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 478, 12);
                }
gm_lb4:;
                uint8_t t30 = gm_l4;
                if (t30) {
                    void * t31 = gm_l0;
                    gm_b16_8 t32;
                    GM_ST(void *, (uint8_t *)&t32, (void *)(gm_srjddrdnspo96));
                    GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(28));
                    gm_tob1snuii5bme t33;
                    gm_f_error_96ci5lu((void *)&t33, t31, t32);
                    uint64_t t34 = gm_l1;
                    gm_tob1snuii5bme t35;
                    gm_f_at_line_6uoncqb((void *)&t35, &t33, t34);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                    ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t35);
                    goto gm_fdone;
                }
                void * t36 = gm_l0;
                gm_tqs5p4n2tq19v t37 = gm_f_peek_bkdvl65(t36);
                gm_tqs5p4n2tq19v t38;
                GM_ST(uint8_t, ((uint8_t *)&t38), ((uint8_t)44ULL));
                ((uint8_t *)&t38)[1] = 1;
                uint8_t t39 = (gm_eqb44cb3cf1n(((uint8_t *)&t37), ((uint8_t *)&t38)));
                if (t39) {
                    void * t40 = gm_l0;
                    gm_b16_8 t41;
                    GM_ST(void *, (uint8_t *)&t41, (void *)(gm_sq5stv2m4p0h1));
                    GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(36));
                    gm_tob1snuii5bme t42;
                    gm_f_error_96ci5lu((void *)&t42, t40, t41);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                    ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t42);
                    goto gm_fdone;
                }
                {
                    void * t43 = (void *)((uint8_t *)&F.l2);
                    void * t44 = gm_l0;
                    gm_f_value_oq952o2((void *)&gm_l6, (void *)NULL, cx, NULL, t44);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t45 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                    if (t45) {
                    } else {
                        gm_tob1snuii5bme t46 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&gm_l6) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                        ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t46);
                        goto gm_fdone;
                    }
                    gm_tftb1odc739t5 t47 = gm_f_push_0pgacsj_3(t43, &(*(gm_tktr4rmbindgv *)(((uint8_t *)&gm_l6) + 8)));
                    gm_l7 = t47;
                }
                uint8_t t48 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                if (!t48) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_src3nt8pf0qk5gn, 480, 9);
                void * t49 = gm_l0;
                uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)t49) + 16));
                gm_l3 = t50;
                void * t51 = gm_l0;
                gm_f_skip_space_and_newlines_qagb1bk(t51);
                void * t52 = gm_l0;
                gm_tqs5p4n2tq19v t53 = gm_f_peek_bkdvl65(t52);
                gm_tqs5p4n2tq19v t54;
                GM_ST(uint8_t, ((uint8_t *)&t54), ((uint8_t)44ULL));
                ((uint8_t *)&t54)[1] = 1;
                uint8_t t55 = (gm_eqb44cb3cf1n(((uint8_t *)&t53), ((uint8_t *)&t54)));
                if (t55) {
                    void * t56 = gm_l0;
                    void * t57 = gm_l0;
                    uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)t57) + 16));
                    uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
                    if (o60) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 484, 13);
                    uint64_t t61 = r59;
                    GM_ST(uint64_t, (((uint8_t *)t56) + 16), t61);
                } else {
                    void * t62 = gm_l0;
                    gm_tqs5p4n2tq19v t63 = gm_f_peek_bkdvl65(t62);
                    gm_tqs5p4n2tq19v t64;
                    GM_ST(uint8_t, ((uint8_t *)&t64), ((uint8_t)93ULL));
                    ((uint8_t *)&t64)[1] = 1;
                    uint8_t t65 = (!gm_eqb44cb3cf1n(((uint8_t *)&t63), ((uint8_t *)&t64)));
                    if (t65) {
                        void * t66 = gm_l0;
                        gm_tqs5p4n2tq19v t67 = gm_f_peek_bkdvl65(t66);
                        gm_l9 = t67;
                        {
                            {
                                uint8_t t68 = (uint8_t)((((uint8_t *)&gm_l9))[1] != 0);
                                if (t68) {
                                    goto gm_lb8;
                                }
                                gm_l8 = ((uint8_t)1);
                                goto gm_lb7;
                            }
gm_lb8:;
                            {
                                gm_l8 = ((uint8_t)0);
                                goto gm_lb7;
                            }
gm_lb9:;
                            gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 486, 16);
                        }
gm_lb7:;
                        uint8_t t69 = gm_l8;
                        if (t69) {
                            void * t70 = gm_l0;
                            gm_b16_8 t71;
                            GM_ST(void *, (uint8_t *)&t71, (void *)(gm_srjddrdnspo96));
                            GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(28));
                            gm_tob1snuii5bme t72;
                            gm_f_error_96ci5lu((void *)&t72, t70, t71);
                            uint64_t t73 = gm_l1;
                            gm_tob1snuii5bme t74;
                            gm_f_at_line_6uoncqb((void *)&t74, &t72, t73);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                            ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                            GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t74);
                            goto gm_fdone;
                        }
                        {
                            {
                                void * t75 = gm_l0;
                                uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)t75) + 32));
                                if (!t76) {
                                    goto gm_lb11;
                                }
                                void * t77 = gm_l0;
                                uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)t77) + 16));
                                uint64_t t79 = gm_l3;
                                uint8_t t80 = (t78 > t79);
                                if (!t80) {
                                    goto gm_lb11;
                                }
                                {
                                    void * t81 = gm_l0;
                                    gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)gm_cal7njs7es4));
                                    gm_tob1snuii5bme t83;
                                    gm_f_error_96ci5lu((void *)&t83, t81, t82);
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                                    ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t83);
                                    goto gm_fdone;
                                }
                            }
gm_lb11:;
                        }
gm_lb10:;
                        {
                            void * t84 = gm_l0;
                            gm_b16_8 t85;
                            GM_ST(void *, (uint8_t *)&t85, (void *)(gm_srjddrdnspo96));
                            GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(28));
                            gm_tob1snuii5bme t86;
                            gm_f_error_96ci5lu((void *)&t86, t84, t85);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                            ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                            GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t86);
                            goto gm_fdone;
                        }
                    }
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t87 = gm_l0;
    void * t88 = gm_l0;
    uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)t88) + 16));
    uint64_t r90; bool o91 = __builtin_add_overflow(t89, ((uint64_t)1ULL), &r90);
    if (o91) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 491, 5);
    uint64_t t92 = r90;
    GM_ST(uint64_t, (((uint8_t *)t87) + 16), t92);
    void * t93 = gm_l0;
    GM_ST(uint8_t, (((uint8_t *)t93) + 32), ((uint8_t)0));
    {
        gm_trqfvmjtt4sij t94 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l2));
        gm_tktr4rmbindgv t95;
        GM_ST(uint8_t, (uint8_t *)&t95 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t95 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t95 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)3ULL));
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t95 + 8), t94);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 0;
        GM_ST(gm_tktr4rmbindgv, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t95);
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
extern __attribute__((aligned(4))) void gm_f_as_str_982o16v(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_as_str_982o16v_c(gm_tktr4rmbindgv a0) { gm_th2lqbo77pvsc gm_r; gm_f_as_str_982o16v(&gm_r, &a0); return gm_r; }
extern __attribute__((aligned(4))) void * gm_f_as_table_4f7n31g(const void * a0);
__attribute__((aligned(4))) void * gm_f_as_table_4f7n31g_c(gm_tktr4rmbindgv a0) { return gm_f_as_table_4f7n31g(&a0); }
extern __attribute__((aligned(4))) void gm_f_at_line_6uoncqb(void *gm_out, const void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tob1snuii5bme gm_f_at_line_6uoncqb_c(gm_tob1snuii5bme a0, uint64_t a1) { gm_tob1snuii5bme gm_r; gm_f_at_line_6uoncqb(&gm_r, &a0, a1); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_end_of_line_056kej4(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_end_of_line_056kej4 F;
    void * gm_l0; /* r */
    uint64_t gm_l1; /* before */
    GM_STACK_CHECK_SHALLOW(&F, 332u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l2;
    gm_tqs5p4n2tq19v gm_l3;
    uint8_t gm_l4;
    gm_tqs5p4n2tq19v gm_l5;
    void * t13 = gm_l0;
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 16));
    gm_l1 = t14;
    void * t15 = gm_l0;
    gm_f_skip_space_voq272o(t15);
    void * t16 = gm_l0;
    gm_tqs5p4n2tq19v t17 = gm_f_peek_bkdvl65(t16);
    gm_tqs5p4n2tq19v t18;
    GM_ST(uint8_t, ((uint8_t *)&t18), ((uint8_t)35ULL));
    ((uint8_t *)&t18)[1] = 1;
    uint8_t t19 = (gm_eqb44cb3cf1n(((uint8_t *)&t17), ((uint8_t *)&t18)));
    if (t19) {
        {
            for (;;) {
                void * t20 = gm_l0;
                gm_tqs5p4n2tq19v t21 = gm_f_peek_bkdvl65(t20);
                gm_l3 = t21;
                {
                    {
                        uint8_t t22 = (uint8_t)((((uint8_t *)&gm_l3))[1] != 0);
                        if (t22) {
                            goto gm_lb5;
                        }
                        gm_l2 = ((uint8_t)0);
                        goto gm_lb4;
                    }
gm_lb5:;
                    {
                        gm_l2 = ((uint8_t)1);
                        goto gm_lb4;
                    }
gm_lb6:;
                    gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 135, 15);
                }
gm_lb4:;
                uint8_t t23 = gm_l2;
                if (!t23) {
                    goto gm_lb1;
                }
                void * t24 = gm_l0;
                gm_tqs5p4n2tq19v t25 = gm_f_peek_bkdvl65(t24);
                gm_tqs5p4n2tq19v t26;
                GM_ST(uint8_t, ((uint8_t *)&t26), ((uint8_t)10ULL));
                ((uint8_t *)&t26)[1] = 1;
                uint8_t t27 = (!gm_eqb44cb3cf1n(((uint8_t *)&t25), ((uint8_t *)&t26)));
                if (!t27) {
                    goto gm_lb1;
                }
                {
                    void * t28 = gm_l0;
                    void * t29 = gm_l0;
                    uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)t29) + 16));
                    uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
                    if (o32) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 135, 63);
                    uint64_t t33 = r31;
                    GM_ST(uint64_t, (((uint8_t *)t28) + 16), t33);
                }
gm_lb3:;
            }
        }
gm_lb1:;
    }
    void * t34 = gm_l0;
    gm_tqs5p4n2tq19v t35 = gm_f_peek_bkdvl65(t34);
    gm_tqs5p4n2tq19v t36;
    GM_ST(uint8_t, ((uint8_t *)&t36), ((uint8_t)13ULL));
    ((uint8_t *)&t36)[1] = 1;
    uint8_t t37 = (gm_eqb44cb3cf1n(((uint8_t *)&t35), ((uint8_t *)&t36)));
    if (t37) {
        void * t38 = gm_l0;
        void * t39 = gm_l0;
        uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)t39) + 16));
        uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
        if (o42) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 137, 27);
        uint64_t t43 = r41;
        GM_ST(uint64_t, (((uint8_t *)t38) + 16), t43);
    }
    void * t44 = gm_l0;
    gm_tqs5p4n2tq19v t45 = gm_f_peek_bkdvl65(t44);
    gm_l5 = t45;
    {
        {
            uint8_t t46 = (uint8_t)((((uint8_t *)&gm_l5))[1] != 0);
            if (t46) {
                goto gm_lb8;
            }
            gm_l4 = ((uint8_t)1);
            goto gm_lb7;
        }
gm_lb8:;
        {
            gm_l4 = ((uint8_t)0);
            goto gm_lb7;
        }
gm_lb9:;
        gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 138, 8);
    }
gm_lb7:;
    uint8_t t47 = gm_l4;
    if (t47) {
        memset(&(*(gm_tr1k5nl69lhjo *)gm_out), 0, sizeof (*(gm_tr1k5nl69lhjo *)gm_out));
        ((uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 8), GM_ZERO(gm_b0_1));
        goto gm_fdone;
    }
    void * t48 = gm_l0;
    gm_tqs5p4n2tq19v t49 = gm_f_peek_bkdvl65(t48);
    gm_tqs5p4n2tq19v t50;
    GM_ST(uint8_t, ((uint8_t *)&t50), ((uint8_t)10ULL));
    ((uint8_t *)&t50)[1] = 1;
    uint8_t t51 = (!gm_eqb44cb3cf1n(((uint8_t *)&t49), ((uint8_t *)&t50)));
    if (t51) {
        {
            {
                void * t52 = gm_l0;
                uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)t52) + 32));
                if (!t53) {
                    goto gm_lb11;
                }
                void * t54 = gm_l0;
                uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)t54) + 16));
                uint64_t t56 = gm_l1;
                uint8_t t57 = (t55 > t56);
                if (!t57) {
                    goto gm_lb11;
                }
                {
                    void * t58 = gm_l0;
                    gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)gm_cal7njs7es4));
                    gm_tob1snuii5bme t60;
                    gm_f_error_96ci5lu((void *)&t60, t58, t59);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out))[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 8), t60);
                    goto gm_fdone;
                }
            }
gm_lb11:;
        }
gm_lb10:;
        {
            void * t61 = gm_l0;
            gm_b16_8 t62;
            GM_ST(void *, (uint8_t *)&t62, (void *)(gm_sj294dvmirbmr));
            GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(46));
            gm_tob1snuii5bme t63;
            gm_f_error_96ci5lu((void *)&t63, t61, t62);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out))[0] = 1;
            GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 8), t63);
            goto gm_fdone;
        }
    }
    memset(&(*(gm_tr1k5nl69lhjo *)gm_out), 0, sizeof (*(gm_tr1k5nl69lhjo *)gm_out));
    ((uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 8), GM_ZERO(gm_b0_1));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tr1k5nl69lhjo gm_f_end_of_line_056kej4_c(void * a0) { gm_tr1k5nl69lhjo gm_r; gm_f_end_of_line_056kej4(&gm_r, a0); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_error_96ci5lu(void *gm_out, void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_error_96ci5lu F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* message */
    uint64_t gm_l2; /* start */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 203u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = ((uint64_t)0ULL);
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
    gm_l3 = t6;
    {
        for (;;) {
            uint64_t t7 = gm_l3;
            uint8_t t8 = (t7 > ((uint64_t)0ULL));
            if (!t8) {
                goto gm_lb1;
            }
            {
                void * t9 = gm_l0;
                void * t10 = GM_LD(void *, ((uint8_t *)t9));
                uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)t9) + 8));
                uint64_t t12 = gm_l3;
                uint64_t r13; bool o14 = __builtin_sub_overflow(t12, ((uint64_t)1ULL), &r13);
                if (o14) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 99, 19);
                uint64_t t15 = r13;
                if ((uintptr_t)t15 >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)t15, t11, gm_src3nt8pf0qk5gn, 99, 12);
                uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)t10) + (uintptr_t)t15 * 1));
                uint8_t t17 = ((t16 == ((uint8_t)10ULL)));
                if (t17) {
                    uint64_t t18 = gm_l3;
                    gm_l2 = t18;
                    goto gm_lb1;
                }
                uint64_t t19 = gm_l3;
                uint64_t r20; bool o21 = __builtin_sub_overflow(t19, ((uint64_t)1ULL), &r20);
                if (o21) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 103, 9);
                uint64_t t22 = r20;
                gm_l3 = t22;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t23 = gm_l0;
    uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)t23) + 24));
    gm_b16_8 t25 = gm_l1;
    void * t26 = gm_l0;
    uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)t26) + 16));
    uint64_t t28 = gm_l2;
    uint64_t r29; bool o30 = __builtin_sub_overflow(t27, t28, &r29);
    if (o30) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 105, 59);
    uint64_t t31 = r29;
    uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
    if (o33) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 105, 59);
    uint64_t t34 = r32;
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_tob1snuii5bme *)gm_out) + 0), t24);
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tob1snuii5bme *)gm_out) + 8), t25);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_tob1snuii5bme *)gm_out) + 24), t34);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tob1snuii5bme gm_f_error_96ci5lu_c(void * a0, gm_b16_8 a1) { gm_tob1snuii5bme gm_r; gm_f_error_96ci5lu(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) void * gm_f_get_9rme0gb(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_9rme0gb F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* key */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 226u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = ((uint64_t)0ULL);
    void * t7 = gm_l0;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
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
                        void * t13 = gm_l0;
                        void * t14 = GM_LD(void *, ((uint8_t *)t13));
                        uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)t13) + 8));
                        uint64_t t16 = gm_l4;
                        if ((uintptr_t)t16 >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)t16, t15, gm_src3nt8pf0qk5gn, 44, 12);
                        gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)t14) + (uintptr_t)t16 * 72));
                        gm_b16_8 t18 = gm_l1;
                        uint8_t t19 = (gm_eq0iqiubfm7e(((uint8_t *)&t17), ((uint8_t *)&t18)));
                        if (t19) {
                            void * t20 = gm_l0;
                            void * t21 = GM_LD(void *, ((uint8_t *)t20));
                            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)t20) + 8));
                            uint64_t t23 = gm_l4;
                            if ((uintptr_t)t23 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t23, t22, gm_src3nt8pf0qk5gn, 44, 52);
                            void * t24 = (void *)(((uint8_t *)t21) + (uintptr_t)t23 * 72);
                            F.ret = t24;
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 43, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t25 = gm_l3;
            uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
            uint64_t t28 = r26;
            gm_l3 = t28;
        }
    }
gm_lb1:;
    F.ret = ((void *)0);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_hex_escape_eoc96dk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2) {
    struct gm_fr_hex_escape_eoc96dk F;
    void * gm_l0; /* r */
    void * gm_l1; /* out */
    uint64_t gm_l2; /* digits */
    uint32_t gm_l3; /* code */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint8_t gm_l6; /* d */
    uint8_t gm_l7; /* <tmp> */
    uint8_t gm_l8; /* <tmp> */
    uint8_t gm_l9; /* <tmp> */
    uint32_t gm_l10; /* v */
    GM_STACK_CHECK(&F, 688u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tqs5p4n2tq19v gm_l11;
    uint8_t gm_l12;
    uint32_t gm_l13;
    uint8_t gm_l14;
    uint32_t gm_l15;
    uint8_t gm_l16;
    uint8_t gm_l17;
    uint8_t gm_l18;
    gm_tftb1odc739t5 gm_l19;
    gm_tftb1odc739t5 gm_l20;
    gm_tftb1odc739t5 gm_l21;
    gm_tftb1odc739t5 gm_l22;
    gm_tftb1odc739t5 gm_l23;
    gm_tftb1odc739t5 gm_l24;
    gm_tftb1odc739t5 gm_l25;
    gm_tftb1odc739t5 gm_l26;
    gm_tftb1odc739t5 gm_l27;
    gm_tftb1odc739t5 gm_l28;
    gm_l3 = ((uint32_t)0ULL);
    gm_l5 = ((uint64_t)0ULL);
    uint64_t t7 = gm_l2;
    gm_l4 = t7;
    {
        for (;;) {
            uint64_t t8 = gm_l5;
            uint64_t t9 = gm_l4;
            uint8_t t10 = (t8 < t9);
            if (!t10) {
                goto gm_lb1;
            }
            {
                {
                    {
                        void * t11 = gm_l0;
                        gm_tqs5p4n2tq19v t12 = gm_f_peek_bkdvl65(t11);
                        gm_l11 = t12;
                        uint8_t t13 = (uint8_t)((((uint8_t *)&gm_l11))[1] != 0);
                        if (t13) {
                            uint8_t t14 = GM_LD(uint8_t, ((uint8_t *)&gm_l11));
                            gm_l6 = t14;
                        } else {
                            void * t15 = gm_l0;
                            gm_b16_8 t16;
                            GM_ST(void *, (uint8_t *)&t16, (void *)(gm_srsr8fvlin2g6));
                            GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(64));
                            gm_tob1snuii5bme t17;
                            gm_f_error_96ci5lu((void *)&t17, t15, t16);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out))[0] = 1;
                            GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 8), t17);
                            goto gm_fdone;
                        }
                        uint8_t t18 = gm_l6;
                        gm_l7 = t18;
                        uint8_t t19 = gm_l7;
                        uint8_t t20 = (((uint8_t)48ULL) <= t19);
                        if (t20) {
                            uint8_t t21 = gm_l7;
                            uint8_t t22 = (t21 <= ((uint8_t)57ULL));
                            gm_l12 = t22;
                        } else {
                            gm_l12 = ((uint8_t)0);
                        }
                        uint8_t t23 = gm_l12;
                        if (t23) {
                            uint8_t t24 = gm_l6;
                            uint8_t r25; bool o26 = __builtin_sub_overflow(t24, ((uint8_t)48ULL), &r25);
                            if (o26) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 437, 17);
                            uint8_t t27 = r25;
                            uint32_t t28 = (uint32_t)t27;
                            gm_l10 = t28;
                        } else {
                            uint8_t t29 = gm_l6;
                            gm_l8 = t29;
                            uint8_t t30 = gm_l8;
                            uint8_t t31 = (((uint8_t)97ULL) <= t30);
                            if (t31) {
                                uint8_t t32 = gm_l8;
                                uint8_t t33 = (t32 <= ((uint8_t)102ULL));
                                gm_l14 = t33;
                            } else {
                                gm_l14 = ((uint8_t)0);
                            }
                            uint8_t t34 = gm_l14;
                            if (t34) {
                                uint8_t t35 = gm_l6;
                                uint8_t r36; bool o37 = __builtin_sub_overflow(t35, ((uint8_t)97ULL), &r36);
                                if (o37) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 439, 17);
                                uint8_t t38 = r36;
                                uint32_t t39 = (uint32_t)t38;
                                uint32_t r40; bool o41 = __builtin_add_overflow(t39, ((uint32_t)10ULL), &r40);
                                if (o41) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 439, 13);
                                uint32_t t42 = r40;
                                gm_l13 = t42;
                            } else {
                                uint8_t t43 = gm_l6;
                                gm_l9 = t43;
                                uint8_t t44 = gm_l9;
                                uint8_t t45 = (((uint8_t)65ULL) <= t44);
                                if (t45) {
                                    uint8_t t46 = gm_l9;
                                    uint8_t t47 = (t46 <= ((uint8_t)70ULL));
                                    gm_l16 = t47;
                                } else {
                                    gm_l16 = ((uint8_t)0);
                                }
                                uint8_t t48 = gm_l16;
                                if (t48) {
                                    uint8_t t49 = gm_l6;
                                    uint8_t r50; bool o51 = __builtin_sub_overflow(t49, ((uint8_t)65ULL), &r50);
                                    if (o51) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 441, 17);
                                    uint8_t t52 = r50;
                                    uint32_t t53 = (uint32_t)t52;
                                    uint32_t r54; bool o55 = __builtin_add_overflow(t53, ((uint32_t)10ULL), &r54);
                                    if (o55) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 441, 13);
                                    uint32_t t56 = r54;
                                    gm_l15 = t56;
                                } else {
                                    {
                                        void * t57 = gm_l0;
                                        gm_b16_8 t58;
                                        GM_ST(void *, (uint8_t *)&t58, (void *)(gm_srsr8fvlin2g6));
                                        GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(64));
                                        gm_tob1snuii5bme t59;
                                        gm_f_error_96ci5lu((void *)&t59, t57, t58);
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 4, 0);
                                        ((uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out))[0] = 1;
                                        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 8), t59);
                                        goto gm_fdone;
                                    }
                                    gm_l15 = ((uint32_t)0);
                                }
                                uint32_t t60 = gm_l15;
                                gm_l13 = t60;
                            }
                            uint32_t t61 = gm_l13;
                            gm_l10 = t61;
                        }
                        uint32_t t62 = gm_l10;
                        gm_l10 = t62;
                        uint32_t t63 = gm_l3;
                        uint32_t r64; bool o65 = __builtin_mul_overflow(t63, ((uint32_t)16ULL), &r64);
                        if (o65) gm_trap("integer overflow in `*`", gm_src3nt8pf0qk5gn, 445, 16);
                        uint32_t t66 = r64;
                        uint32_t t67 = gm_l10;
                        uint32_t r68; bool o69 = __builtin_add_overflow(t66, t67, &r68);
                        if (o69) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 445, 16);
                        uint32_t t70 = r68;
                        gm_l3 = t70;
                        void * t71 = gm_l0;
                        void * t72 = gm_l0;
                        uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)t72) + 16));
                        uint64_t r74; bool o75 = __builtin_add_overflow(t73, ((uint64_t)1ULL), &r74);
                        if (o75) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 446, 9);
                        uint64_t t76 = r74;
                        GM_ST(uint64_t, (((uint8_t *)t71) + 16), t76);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 434, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t77 = gm_l5;
            uint64_t r78; bool o79 = __builtin_add_overflow(t77, ((uint64_t)1ULL), &r78);
            uint64_t t80 = r78;
            gm_l5 = t80;
        }
    }
gm_lb1:;
    uint32_t t81 = gm_l3;
    uint8_t t82 = (t81 > ((uint32_t)1114111ULL));
    if (t82) {
        gm_l17 = ((uint8_t)1);
    } else {
        uint32_t t83 = gm_l3;
        uint8_t t84 = (t83 >= ((uint32_t)55296ULL));
        if (t84) {
            uint32_t t85 = gm_l3;
            uint8_t t86 = (t85 < ((uint32_t)57344ULL));
            gm_l18 = t86;
        } else {
            gm_l18 = ((uint8_t)0);
        }
        uint8_t t87 = gm_l18;
        gm_l17 = t87;
    }
    uint8_t t88 = gm_l17;
    if (t88) {
        void * t89 = gm_l0;
        gm_b16_8 t90;
        GM_ST(void *, (uint8_t *)&t90, (void *)(gm_sngfc91643s6a));
        GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)(54));
        gm_tob1snuii5bme t91;
        gm_f_error_96ci5lu((void *)&t91, t89, t90);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out))[0] = 1;
        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 8), t91);
        goto gm_fdone;
    }
    uint32_t t92 = gm_l3;
    uint8_t t93 = (t92 < ((uint32_t)128ULL));
    if (t93) {
        void * t94 = gm_l1;
        uint32_t t95 = gm_l3;
        uint8_t t96 = (uint8_t)t95;
        gm_tftb1odc739t5 t97 = gm_f_push_t83rg2m(t94, t96);
        gm_l19 = t97;
        uint8_t t98 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
        if (!t98) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_src3nt8pf0qk5gn, 450, 9);
    } else {
        uint32_t t99 = gm_l3;
        uint8_t t100 = (t99 < ((uint32_t)2048ULL));
        if (t100) {
            void * t101 = gm_l1;
            uint32_t t102 = gm_l3;
            unsigned __int128 n103 = (unsigned __int128)((uint64_t)6ULL);
            if (n103 >= 32) gm_trap_shift((uint64_t)n103, "u32", gm_src3nt8pf0qk5gn, 452, 27);
            uint32_t t104 = (uint32_t)(t102 >> (int)n103);
            uint32_t t105 = (uint32_t)(((uint32_t)192ULL) | t104);
            uint8_t t106 = (uint8_t)t105;
            gm_tftb1odc739t5 t107 = gm_f_push_t83rg2m(t101, t106);
            gm_l20 = t107;
            uint8_t t108 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
            if (!t108) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_src3nt8pf0qk5gn, 452, 9);
            void * t109 = gm_l1;
            uint32_t t110 = gm_l3;
            uint32_t t111 = (uint32_t)(t110 & ((uint32_t)63ULL));
            uint32_t t112 = (uint32_t)(((uint32_t)128ULL) | t111);
            uint8_t t113 = (uint8_t)t112;
            gm_tftb1odc739t5 t114 = gm_f_push_t83rg2m(t109, t113);
            gm_l21 = t114;
            uint8_t t115 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
            if (!t115) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_src3nt8pf0qk5gn, 453, 9);
        } else {
            uint32_t t116 = gm_l3;
            uint8_t t117 = (t116 < ((uint32_t)65536ULL));
            if (t117) {
                void * t118 = gm_l1;
                uint32_t t119 = gm_l3;
                unsigned __int128 n120 = (unsigned __int128)((uint64_t)12ULL);
                if (n120 >= 32) gm_trap_shift((uint64_t)n120, "u32", gm_src3nt8pf0qk5gn, 455, 27);
                uint32_t t121 = (uint32_t)(t119 >> (int)n120);
                uint32_t t122 = (uint32_t)(((uint32_t)224ULL) | t121);
                uint8_t t123 = (uint8_t)t122;
                gm_tftb1odc739t5 t124 = gm_f_push_t83rg2m(t118, t123);
                gm_l22 = t124;
                uint8_t t125 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                if (!t125) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 1, gm_src3nt8pf0qk5gn, 455, 9);
                void * t126 = gm_l1;
                uint32_t t127 = gm_l3;
                unsigned __int128 n128 = (unsigned __int128)((uint64_t)6ULL);
                if (n128 >= 32) gm_trap_shift((uint64_t)n128, "u32", gm_src3nt8pf0qk5gn, 456, 28);
                uint32_t t129 = (uint32_t)(t127 >> (int)n128);
                uint32_t t130 = (uint32_t)(t129 & ((uint32_t)63ULL));
                uint32_t t131 = (uint32_t)(((uint32_t)128ULL) | t130);
                uint8_t t132 = (uint8_t)t131;
                gm_tftb1odc739t5 t133 = gm_f_push_t83rg2m(t126, t132);
                gm_l23 = t133;
                uint8_t t134 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                if (!t134) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_src3nt8pf0qk5gn, 456, 9);
                void * t135 = gm_l1;
                uint32_t t136 = gm_l3;
                uint32_t t137 = (uint32_t)(t136 & ((uint32_t)63ULL));
                uint32_t t138 = (uint32_t)(((uint32_t)128ULL) | t137);
                uint8_t t139 = (uint8_t)t138;
                gm_tftb1odc739t5 t140 = gm_f_push_t83rg2m(t135, t139);
                gm_l24 = t140;
                uint8_t t141 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                if (!t141) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_src3nt8pf0qk5gn, 457, 9);
            } else {
                void * t142 = gm_l1;
                uint32_t t143 = gm_l3;
                unsigned __int128 n144 = (unsigned __int128)((uint64_t)18ULL);
                if (n144 >= 32) gm_trap_shift((uint64_t)n144, "u32", gm_src3nt8pf0qk5gn, 459, 27);
                uint32_t t145 = (uint32_t)(t143 >> (int)n144);
                uint32_t t146 = (uint32_t)(((uint32_t)240ULL) | t145);
                uint8_t t147 = (uint8_t)t146;
                gm_tftb1odc739t5 t148 = gm_f_push_t83rg2m(t142, t147);
                gm_l25 = t148;
                uint8_t t149 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                if (!t149) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_src3nt8pf0qk5gn, 459, 9);
                void * t150 = gm_l1;
                uint32_t t151 = gm_l3;
                unsigned __int128 n152 = (unsigned __int128)((uint64_t)12ULL);
                if (n152 >= 32) gm_trap_shift((uint64_t)n152, "u32", gm_src3nt8pf0qk5gn, 460, 28);
                uint32_t t153 = (uint32_t)(t151 >> (int)n152);
                uint32_t t154 = (uint32_t)(t153 & ((uint32_t)63ULL));
                uint32_t t155 = (uint32_t)(((uint32_t)128ULL) | t154);
                uint8_t t156 = (uint8_t)t155;
                gm_tftb1odc739t5 t157 = gm_f_push_t83rg2m(t150, t156);
                gm_l26 = t157;
                uint8_t t158 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                if (!t158) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_src3nt8pf0qk5gn, 460, 9);
                void * t159 = gm_l1;
                uint32_t t160 = gm_l3;
                unsigned __int128 n161 = (unsigned __int128)((uint64_t)6ULL);
                if (n161 >= 32) gm_trap_shift((uint64_t)n161, "u32", gm_src3nt8pf0qk5gn, 461, 28);
                uint32_t t162 = (uint32_t)(t160 >> (int)n161);
                uint32_t t163 = (uint32_t)(t162 & ((uint32_t)63ULL));
                uint32_t t164 = (uint32_t)(((uint32_t)128ULL) | t163);
                uint8_t t165 = (uint8_t)t164;
                gm_tftb1odc739t5 t166 = gm_f_push_t83rg2m(t159, t165);
                gm_l27 = t166;
                uint8_t t167 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                if (!t167) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 1, gm_src3nt8pf0qk5gn, 461, 9);
                void * t168 = gm_l1;
                uint32_t t169 = gm_l3;
                uint32_t t170 = (uint32_t)(t169 & ((uint32_t)63ULL));
                uint32_t t171 = (uint32_t)(((uint32_t)128ULL) | t170);
                uint8_t t172 = (uint8_t)t171;
                gm_tftb1odc739t5 t173 = gm_f_push_t83rg2m(t168, t172);
                gm_l28 = t173;
                uint8_t t174 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                if (!t174) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 1, gm_src3nt8pf0qk5gn, 462, 9);
            }
        }
    }
    memset(&(*(gm_tr1k5nl69lhjo *)gm_out), 0, sizeof (*(gm_tr1k5nl69lhjo *)gm_out));
    ((uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 8), GM_ZERO(gm_b0_1));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_inline_table_cvvpgel(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_inline_table_cvvpgel F;
    void * gm_l0; /* r */
    void * gm_l1; /* t */
    uint64_t gm_l2; /* line */
    GM_STACK_CHECK(&F, 941u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l5;
    gm_tqs5p4n2tq19v gm_l6;
    gm_t21m1eu5ta01n gm_l7;
    gm_tefknldmnh2si gm_l8;
    gm_tr1k5nl69lhjo gm_l9;
    uint8_t gm_l10;
    gm_tqs5p4n2tq19v gm_l11;
    void * t11 = gm_l0;
    void * t12 = gm_l0;
    uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t12) + 16));
    uint64_t r14; bool o15 = __builtin_add_overflow(t13, ((uint64_t)1ULL), &r14);
    if (o15) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 498, 5);
    uint64_t t16 = r14;
    GM_ST(uint64_t, (((uint8_t *)t11) + 16), t16);
    void * t17 = gm_f_new_table_hh6cki7((void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l1 = t17;
    void * t18 = gm_l1;
    GM_ST(uint8_t, (((uint8_t *)t18) + 40), ((uint8_t)1));
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb1;
            }
            {
                void * t19 = gm_l0;
                gm_f_skip_space_and_newlines_qagb1bk(t19);
                void * t20 = gm_l0;
                gm_tqs5p4n2tq19v t21 = gm_f_peek_bkdvl65(t20);
                gm_tqs5p4n2tq19v t22;
                GM_ST(uint8_t, ((uint8_t *)&t22), ((uint8_t)125ULL));
                ((uint8_t *)&t22)[1] = 1;
                uint8_t t23 = (gm_eqb44cb3cf1n(((uint8_t *)&t21), ((uint8_t *)&t22)));
                if (t23) {
                    goto gm_lb1;
                }
                void * t24 = gm_l0;
                gm_tqs5p4n2tq19v t25 = gm_f_peek_bkdvl65(t24);
                gm_l6 = t25;
                {
                    {
                        uint8_t t26 = (uint8_t)((((uint8_t *)&gm_l6))[1] != 0);
                        if (t26) {
                            goto gm_lb5;
                        }
                        gm_l5 = ((uint8_t)1);
                        goto gm_lb4;
                    }
gm_lb5:;
                    {
                        gm_l5 = ((uint8_t)0);
                        goto gm_lb4;
                    }
gm_lb6:;
                    gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 504, 12);
                }
gm_lb4:;
                uint8_t t27 = gm_l5;
                if (t27) {
                    void * t28 = gm_l0;
                    gm_b16_8 t29;
                    GM_ST(void *, (uint8_t *)&t29, (void *)(gm_sl2lo3hki3t4d));
                    GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(35));
                    gm_tob1snuii5bme t30;
                    gm_f_error_96ci5lu((void *)&t30, t28, t29);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                    ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t30);
                    goto gm_fdone;
                }
                void * t31 = gm_l0;
                uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)t31) + 24));
                gm_l2 = t32;
                {
                    void * t33 = gm_l0;
                    gm_f_key_path_7spb050((void *)&gm_l7, (void *)NULL, cx, NULL, t33);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t34 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                if (t34) {
                } else {
                    gm_tob1snuii5bme t35 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&gm_l7) + 8));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                    ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t35);
                    goto gm_fdone;
                }
                {
                    memmove(((uint8_t *)&F.l3), (((uint8_t *)&gm_l7) + 8), 40);
                }
                void * t36 = gm_l0;
                gm_f_skip_space_and_newlines_qagb1bk(t36);
                void * t37 = gm_l0;
                gm_tqs5p4n2tq19v t38 = gm_f_peek_bkdvl65(t37);
                gm_tqs5p4n2tq19v t39;
                GM_ST(uint8_t, ((uint8_t *)&t39), ((uint8_t)61ULL));
                ((uint8_t *)&t39)[1] = 1;
                uint8_t t40 = (!gm_eqb44cb3cf1n(((uint8_t *)&t38), ((uint8_t *)&t39)));
                if (t40) {
                    void * t41 = gm_l0;
                    gm_b16_8 t42;
                    GM_ST(void *, (uint8_t *)&t42, (void *)(gm_spl83f693c752));
                    GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(31));
                    gm_tob1snuii5bme t43;
                    gm_f_error_96ci5lu((void *)&t43, t41, t42);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                    ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t43);
                    goto gm_fdone;
                }
                void * t44 = gm_l0;
                void * t45 = gm_l0;
                uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)t45) + 16));
                uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
                if (o48) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 509, 9);
                uint64_t t49 = r47;
                GM_ST(uint64_t, (((uint8_t *)t44) + 16), t49);
                void * t50 = gm_l0;
                gm_f_skip_space_and_newlines_qagb1bk(t50);
                {
                    void * t51 = gm_l0;
                    gm_f_value_oq952o2((void *)&gm_l8, (void *)NULL, cx, NULL, t51);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t52 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                if (t52) {
                } else {
                    gm_tob1snuii5bme t53 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&gm_l8) + 8));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                    ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t53);
                    goto gm_fdone;
                }
                {
                    memmove(((uint8_t *)&F.l4), (((uint8_t *)&gm_l8) + 8), 48);
                }
                {
                    void * t54 = gm_l1;
                    gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
                    gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&t55));
                    uint64_t t57 = gm_l2;
                    gm_tktr4rmbindgv t58 = GM_LD(gm_tktr4rmbindgv, ((uint8_t *)&F.l4));
                    gm_f_insert_path_cvuqfkc((void *)&gm_l9, (void *)NULL, cx, NULL, t54, t56, &t58, t57);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t59 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                if (t59) {
                } else {
                    gm_tob1snuii5bme t60 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&gm_l9) + 8));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                    ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t60);
                    goto gm_fdone;
                }
                void * t61 = gm_l0;
                gm_f_skip_space_and_newlines_qagb1bk(t61);
                void * t62 = gm_l0;
                gm_tqs5p4n2tq19v t63 = gm_f_peek_bkdvl65(t62);
                gm_tqs5p4n2tq19v t64;
                GM_ST(uint8_t, ((uint8_t *)&t64), ((uint8_t)125ULL));
                ((uint8_t *)&t64)[1] = 1;
                uint8_t t65 = (gm_eqb44cb3cf1n(((uint8_t *)&t63), ((uint8_t *)&t64)));
                if (t65) {
                    goto gm_lb1;
                }
                void * t66 = gm_l0;
                gm_tqs5p4n2tq19v t67 = gm_f_peek_bkdvl65(t66);
                gm_l11 = t67;
                {
                    {
                        uint8_t t68 = (uint8_t)((((uint8_t *)&gm_l11))[1] != 0);
                        if (t68) {
                            goto gm_lb8;
                        }
                        gm_l10 = ((uint8_t)1);
                        goto gm_lb7;
                    }
gm_lb8:;
                    {
                        gm_l10 = ((uint8_t)0);
                        goto gm_lb7;
                    }
gm_lb9:;
                    gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 515, 12);
                }
gm_lb7:;
                uint8_t t69 = gm_l10;
                if (t69) {
                    void * t70 = gm_l0;
                    gm_b16_8 t71;
                    GM_ST(void *, (uint8_t *)&t71, (void *)(gm_sl2lo3hki3t4d));
                    GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(35));
                    gm_tob1snuii5bme t72;
                    gm_f_error_96ci5lu((void *)&t72, t70, t71);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                    ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t72);
                    goto gm_fdone;
                }
                void * t73 = gm_l0;
                gm_tqs5p4n2tq19v t74 = gm_f_peek_bkdvl65(t73);
                gm_tqs5p4n2tq19v t75;
                GM_ST(uint8_t, ((uint8_t *)&t75), ((uint8_t)44ULL));
                ((uint8_t *)&t75)[1] = 1;
                uint8_t t76 = (!gm_eqb44cb3cf1n(((uint8_t *)&t74), ((uint8_t *)&t75)));
                if (t76) {
                    void * t77 = gm_l0;
                    gm_b16_8 t78;
                    GM_ST(void *, (uint8_t *)&t78, (void *)(gm_suonmnnb7u0t1));
                    GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(54));
                    gm_tob1snuii5bme t79;
                    gm_f_error_96ci5lu((void *)&t79, t77, t78);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                    ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t79);
                    goto gm_fdone;
                }
                void * t80 = gm_l0;
                void * t81 = gm_l0;
                uint64_t t82 = GM_LD(uint64_t, (((uint8_t *)t81) + 16));
                uint64_t r83; bool o84 = __builtin_add_overflow(t82, ((uint64_t)1ULL), &r83);
                if (o84) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 517, 9);
                uint64_t t85 = r83;
                GM_ST(uint64_t, (((uint8_t *)t80) + 16), t85);
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t86 = gm_l0;
    void * t87 = gm_l0;
    uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)t87) + 16));
    uint64_t r89; bool o90 = __builtin_add_overflow(t88, ((uint64_t)1ULL), &r89);
    if (o90) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 519, 5);
    uint64_t t91 = r89;
    GM_ST(uint64_t, (((uint8_t *)t86) + 16), t91);
    void * t92 = gm_l0;
    GM_ST(uint8_t, (((uint8_t *)t92) + 32), ((uint8_t)0));
    {
        void * t93 = gm_l1;
        gm_tktr4rmbindgv t94;
        memset(&t94, 0, sizeof t94);
        GM_ST(uint8_t, ((uint8_t *)&t94 + 0), ((uint8_t)4ULL));
        GM_ST(void *, ((uint8_t *)&t94 + 8), t93);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 0;
        GM_ST(gm_tktr4rmbindgv, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t94);
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
__attribute__((aligned(4))) void gm_f_insert_path_cvuqfkc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, uint64_t a3) {
    struct gm_fr_insert_path_cvuqfkc F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* path */
    uint64_t gm_l3; /* line */
    void * gm_l4; /* target */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_b16_8 gm_l7; /* name */
    void * gm_l8; /* e */
    void * gm_l9; /* sub */
    void * gm_l10; /* fresh */
    gm_b16_8 gm_l11; /* name */
    GM_STACK_CHECK(&F, 920u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    void * gm_l12;
    void * gm_l13;
    gm_tftb1odc739t5 gm_l14;
    uint8_t gm_l15;
    void * gm_l16;
    gm_tftb1odc739t5 gm_l17;
    void * t12 = gm_l0;
    gm_l4 = t12;
    void * t13 = GM_LD(void *, ((uint8_t *)&gm_l1));
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t r16; bool o17 = __builtin_sub_overflow(t15, ((uint64_t)1ULL), &r16);
    if (o17) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 234, 25);
    uint64_t t18 = r16;
    uint64_t t19 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t20 = (uintptr_t)t18;
    if (t19 > t20 || t20 > (uintptr_t)t14) gm_trap_slice(t19, t20, t14, gm_src3nt8pf0qk5gn, 234, 17);
    uint64_t t21 = ((uintptr_t)t18) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t22;
    GM_ST(void *, (uint8_t *)&t22, (void *)(((uint8_t *)t13 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(t21));
    gm_l5 = t22;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t23 = gm_l6;
            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t25 = (t23 < t24);
            if (!t25) {
                goto gm_lb1;
            }
            {
                void * t26 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t28 = gm_l6;
                {
                    {
                        gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)t26) + (uintptr_t)t28 * 16));
                        gm_l7 = t29;
                        {
                            {
                                void * t30 = gm_l4;
                                gm_b16_8 t31 = gm_l7;
                                void * t32 = gm_f_get_9rme0gb(t30, t31);
                                gm_l12 = t32;
                                uint8_t t33 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l12)) != 0);
                                if (!t33) {
                                    goto gm_lb7;
                                }
                                void * t34 = GM_LD(void *, ((uint8_t *)&gm_l12));
                                gm_l8 = t34;
                                void * t35 = gm_l8;
                                void * t36 = gm_f_as_table_4f7n31g(&(*(gm_tktr4rmbindgv *)(((uint8_t *)t35) + 16)));
                                gm_l13 = t36;
                                uint8_t t37 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l13)) != 0);
                                if (t37) {
                                    void * t38 = GM_LD(void *, ((uint8_t *)&gm_l13));
                                    gm_l9 = t38;
                                } else {
                                    uint64_t t39 = gm_l3;
                                    gm_b16_8 t40;
                                    GM_ST(void *, (uint8_t *)&t40, (void *)(gm_skiffh4urhl7e));
                                    GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(13));
                                    gm_tob1snuii5bme t41;
                                    GM_ST(uint64_t, ((uint8_t *)&t41 + 0), t39);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t41 + 8), t40);
                                    GM_ST(uint64_t, ((uint8_t *)&t41 + 24), ((uint64_t)1ULL));
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 4, 0);
                                    ((uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out))[0] = 1;
                                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 8), t41);
                                    goto gm_fdone;
                                }
                                void * t42 = gm_l9;
                                gm_l4 = t42;
                                goto gm_lb6;
                            }
gm_lb7:;
                            void * t43 = gm_f_new_table_hh6cki7((void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l10 = t43;
                            {
                                void * t44 = gm_l4;
                                void * t45 = (void *)((uint8_t *)t44);
                                gm_b16_8 t46 = gm_l7;
                                void * t47 = gm_l10;
                                uint64_t t48 = gm_l3;
                                gm_tktr4rmbindgv t49;
                                memset(&t49, 0, sizeof t49);
                                GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)4ULL));
                                GM_ST(void *, ((uint8_t *)&t49 + 8), t47);
                                gm_trc3jdv2g5qre t50;
                                GM_ST(gm_b16_8, ((uint8_t *)&t50 + 0), t46);
                                GM_ST(gm_tktr4rmbindgv, ((uint8_t *)&t50 + 16), t49);
                                GM_ST(uint64_t, ((uint8_t *)&t50 + 64), t48);
                                gm_tftb1odc739t5 t51 = gm_f_push_2pp1ri6(t45, &t50);
                                gm_l14 = t51;
                            }
                            uint8_t t52 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                            if (!t52) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_src3nt8pf0qk5gn, 240, 13);
                            void * t53 = gm_l10;
                            gm_l4 = t53;
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 234, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t54 = gm_l6;
            uint64_t r55; bool o56 = __builtin_add_overflow(t54, ((uint64_t)1ULL), &r55);
            uint64_t t57 = r55;
            gm_l6 = t57;
        }
    }
gm_lb1:;
    void * t58 = GM_LD(void *, ((uint8_t *)&gm_l1));
    uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t r61; bool o62 = __builtin_sub_overflow(t60, ((uint64_t)1ULL), &r61);
    if (o62) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 244, 18);
    uint64_t t63 = r61;
    if ((uintptr_t)t63 >= (uintptr_t)(t59)) gm_trap_index((uintptr_t)t63, t59, gm_src3nt8pf0qk5gn, 244, 13);
    gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)t58) + (uintptr_t)t63 * 16));
    gm_l11 = t64;
    void * t65 = gm_l4;
    gm_b16_8 t66 = gm_l11;
    void * t67 = gm_f_get_9rme0gb(t65, t66);
    gm_l16 = t67;
    {
        {
            uint8_t t68 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l16)) != 0);
            if (t68) {
                goto gm_lb9;
            }
            gm_l15 = ((uint8_t)0);
            goto gm_lb8;
        }
gm_lb9:;
        {
            gm_l15 = ((uint8_t)1);
            goto gm_lb8;
        }
gm_lb10:;
        gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 245, 8);
    }
gm_lb8:;
    uint8_t t69 = gm_l15;
    if (t69) {
        uint64_t t70 = gm_l3;
        gm_b16_8 t71;
        GM_ST(void *, (uint8_t *)&t71, (void *)(gm_skiffh4urhl7e));
        GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(13));
        gm_tob1snuii5bme t72;
        GM_ST(uint64_t, ((uint8_t *)&t72 + 0), t70);
        GM_ST(gm_b16_8, ((uint8_t *)&t72 + 8), t71);
        GM_ST(uint64_t, ((uint8_t *)&t72 + 24), ((uint64_t)1ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out))[0] = 1;
        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 8), t72);
        goto gm_fdone;
    }
    {
        void * t73 = gm_l4;
        void * t74 = (void *)((uint8_t *)t73);
        gm_b16_8 t75 = gm_l11;
        uint64_t t76 = gm_l3;
        gm_tktr4rmbindgv t77 = GM_LD(gm_tktr4rmbindgv, ((uint8_t *)a2));
        gm_trc3jdv2g5qre t78;
        GM_ST(gm_b16_8, ((uint8_t *)&t78 + 0), t75);
        GM_ST(gm_tktr4rmbindgv, ((uint8_t *)&t78 + 16), t77);
        GM_ST(uint64_t, ((uint8_t *)&t78 + 64), t76);
        gm_tftb1odc739t5 t79 = gm_f_push_2pp1ri6(t74, &t78);
        gm_l17 = t79;
    }
    uint8_t t80 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
    if (!t80) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_src3nt8pf0qk5gn, 246, 5);
    {
        memset(&(*(gm_tr1k5nl69lhjo *)gm_out), 0, sizeof (*(gm_tr1k5nl69lhjo *)gm_out));
        ((uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 8), GM_ZERO(gm_b0_1));
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
__attribute__((aligned(4))) void gm_f_key_5clrcin(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_key_5clrcin F;
    void * gm_l0; /* r */
    uint8_t gm_l1; /* c */
    gm_tob1snuii5bme gm_l2; /* e */
    uint64_t gm_l3; /* start */
    uint8_t gm_l4; /* k */
    uint8_t gm_l5; /* <tmp> */
    uint8_t gm_l6; /* <tmp> */
    uint8_t gm_l7; /* <tmp> */
    GM_STACK_CHECK(&F, 545u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tqs5p4n2tq19v gm_l8;
    gm_b16_8 gm_l9;
    gm_tu615l4qlr8b2 gm_l10;
    gm_tqs5p4n2tq19v gm_l11;
    uint8_t gm_l12;
    uint8_t gm_l13;
    uint8_t gm_l14;
    uint8_t gm_l15;
    uint8_t gm_l16;
    uint8_t gm_l17;
    void * t5 = gm_l0;
    gm_tqs5p4n2tq19v t6 = gm_f_peek_bkdvl65(t5);
    gm_l8 = t6;
    uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l8))[1] != 0);
    if (t7) {
        uint8_t t8 = GM_LD(uint8_t, ((uint8_t *)&gm_l8));
        gm_l1 = t8;
    } else {
        void * t9 = gm_l0;
        gm_b16_8 t10;
        GM_ST(void *, (uint8_t *)&t10, (void *)(gm_spl83f693c752));
        GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(31));
        gm_tob1snuii5bme t11;
        gm_f_error_96ci5lu((void *)&t11, t9, t10);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out))[0] = 1;
        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 8), t11);
        goto gm_fdone;
    }
    uint8_t t12 = gm_l1;
    gm_tqs5p4n2tq19v t13;
    GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)34ULL));
    GM_ST(uint8_t, ((uint8_t *)&t13 + 1), ((uint8_t)39ULL));
    uint8_t t14 = 0;
    if (gm_eqjmccqdb90f(((uint8_t *)&t13) + 0, ((uint8_t *)&t12))) t14 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t13) + 1, ((uint8_t *)&t12))) t14 = 1;
    uint8_t t15 = (uint8_t)(t14);
    if (t15) {
        {
            void * t16 = gm_l0;
            gm_f_parse_string_80niubc((void *)&gm_l10, (void *)NULL, cx, NULL, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t17 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
        if (t17) {
            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
            gm_l9 = t18;
        } else {
            {
                memmove(((uint8_t *)&gm_l2), (((uint8_t *)&gm_l10) + 8), 32);
            }
            {
                uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
                gm_b16_8 t20;
                GM_ST(void *, (uint8_t *)&t20, (void *)(gm_spl83f693c752));
                GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(31));
                gm_tob1snuii5bme t21;
                GM_ST(uint64_t, ((uint8_t *)&t21 + 0), t19);
                GM_ST(gm_b16_8, ((uint8_t *)&t21 + 8), t20);
                GM_ST(uint64_t, ((uint8_t *)&t21 + 24), ((uint64_t)1ULL));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out))[0] = 1;
                GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 8), t21);
                goto gm_fdone;
            }
        }
        {
            gm_b16_8 t22 = gm_l9;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 8), t22);
            goto gm_fdone;
        }
    }
    void * t23 = gm_l0;
    uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)t23) + 16));
    gm_l3 = t24;
    {
        for (;;) {
            void * t25 = gm_l0;
            gm_tqs5p4n2tq19v t26 = gm_f_peek_bkdvl65(t25);
            gm_l11 = t26;
            uint8_t t27 = (uint8_t)((((uint8_t *)&gm_l11))[1] != 0);
            if (!t27) {
                goto gm_lb1;
            }
            uint8_t t28 = GM_LD(uint8_t, ((uint8_t *)&gm_l11));
            gm_l4 = t28;
            {
                uint8_t t29 = gm_l4;
                gm_l5 = t29;
                uint8_t t30 = gm_l5;
                uint8_t t31 = (((uint8_t)97ULL) <= t30);
                if (t31) {
                    uint8_t t32 = gm_l5;
                    uint8_t t33 = (t32 <= ((uint8_t)122ULL));
                    gm_l15 = t33;
                } else {
                    gm_l15 = ((uint8_t)0);
                }
                uint8_t t34 = gm_l15;
                if (t34) {
                    gm_l14 = ((uint8_t)1);
                } else {
                    uint8_t t35 = gm_l4;
                    gm_l6 = t35;
                    uint8_t t36 = gm_l6;
                    uint8_t t37 = (((uint8_t)65ULL) <= t36);
                    if (t37) {
                        uint8_t t38 = gm_l6;
                        uint8_t t39 = (t38 <= ((uint8_t)90ULL));
                        gm_l16 = t39;
                    } else {
                        gm_l16 = ((uint8_t)0);
                    }
                    uint8_t t40 = gm_l16;
                    gm_l14 = t40;
                }
                uint8_t t41 = gm_l14;
                if (t41) {
                    gm_l13 = ((uint8_t)1);
                } else {
                    uint8_t t42 = gm_l4;
                    gm_l7 = t42;
                    uint8_t t43 = gm_l7;
                    uint8_t t44 = (((uint8_t)48ULL) <= t43);
                    if (t44) {
                        uint8_t t45 = gm_l7;
                        uint8_t t46 = (t45 <= ((uint8_t)57ULL));
                        gm_l17 = t46;
                    } else {
                        gm_l17 = ((uint8_t)0);
                    }
                    uint8_t t47 = gm_l17;
                    gm_l13 = t47;
                }
                uint8_t t48 = gm_l13;
                if (t48) {
                    gm_l12 = ((uint8_t)1);
                } else {
                    uint8_t t49 = gm_l4;
                    gm_tqs5p4n2tq19v t50;
                    GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)95ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t50 + 1), ((uint8_t)45ULL));
                    uint8_t t51 = 0;
                    if (gm_eqjmccqdb90f(((uint8_t *)&t50) + 0, ((uint8_t *)&t49))) t51 = 1;
                    if (gm_eqjmccqdb90f(((uint8_t *)&t50) + 1, ((uint8_t *)&t49))) t51 = 1;
                    uint8_t t52 = (uint8_t)(t51);
                    gm_l12 = t52;
                }
                uint8_t t53 = gm_l12;
                uint8_t t54 = (uint8_t)!t53;
                if (t54) {
                    goto gm_lb1;
                }
                void * t55 = gm_l0;
                void * t56 = gm_l0;
                uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)t56) + 16));
                uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
                if (o59) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 225, 9);
                uint64_t t60 = r58;
                GM_ST(uint64_t, (((uint8_t *)t55) + 16), t60);
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t61 = gm_l0;
    uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)t61) + 16));
    uint64_t t63 = gm_l3;
    uint8_t t64 = ((t62 == t63));
    if (t64) {
        void * t65 = gm_l0;
        gm_b16_8 t66;
        GM_ST(void *, (uint8_t *)&t66, (void *)(gm_sithisfj5qgfv));
        GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(66));
        gm_tob1snuii5bme t67;
        gm_f_error_96ci5lu((void *)&t67, t65, t66);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out))[0] = 1;
        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 8), t67);
        goto gm_fdone;
    }
    void * t68 = gm_l0;
    void * t69 = GM_LD(void *, ((uint8_t *)t68));
    uint64_t t70 = GM_LD(uint64_t, (((uint8_t *)t68) + 8));
    uint64_t t71 = gm_l3;
    void * t72 = gm_l0;
    uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)t72) + 16));
    uint64_t t74 = (uintptr_t)t71;
    uint64_t t75 = (uintptr_t)t73;
    if (t74 > t75 || t75 > (uintptr_t)t70) gm_trap_slice(t74, t75, t70, gm_src3nt8pf0qk5gn, 228, 12);
    uint64_t t76 = ((uintptr_t)t73) - (uintptr_t)t71;
    gm_b16_8 t77;
    GM_ST(void *, (uint8_t *)&t77, (void *)(((uint8_t *)t69 + (uintptr_t)t71 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(t76));
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 24, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 32, 0);
    ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out))[0] = 0;
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 8), t77);
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
inline __attribute__((aligned(4))) void gm_f_key_path_7spb050(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_key_path_7spb050 F;
    void * gm_l0; /* r */
    GM_STACK_CHECK(&F, 259u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tu615l4qlr8b2 gm_l2;
    gm_tftb1odc739t5 gm_l3;
    {
        gm_trqfvmjtt4sij t5;
        gm_f_list_epvducg((void *)&t5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t5;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb1;
            }
            {
                void * t6 = gm_l0;
                gm_f_skip_space_voq272o(t6);
                void * t7 = (void *)((uint8_t *)&F.l1);
                void * t8 = gm_l0;
                gm_f_key_5clrcin((void *)&gm_l2, (void *)NULL, cx, NULL, t8);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
                if (t9) {
                } else {
                    gm_tob1snuii5bme t10 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&gm_l2) + 8));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t21m1eu5ta01n *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t21m1eu5ta01n *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t21m1eu5ta01n *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t21m1eu5ta01n *)gm_out) + 40, 0);
                    ((uint8_t *)&(*(gm_t21m1eu5ta01n *)gm_out))[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_t21m1eu5ta01n *)gm_out) + 8), t10);
                    goto gm_fdone;
                }
                gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
                gm_tftb1odc739t5 t12 = gm_f_push_3ef746b(t7, t11);
                gm_l3 = t12;
                uint8_t t13 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
                if (!t13) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_src3nt8pf0qk5gn, 209, 9);
                void * t14 = gm_l0;
                gm_f_skip_space_voq272o(t14);
                void * t15 = gm_l0;
                gm_tqs5p4n2tq19v t16 = gm_f_peek_bkdvl65(t15);
                gm_tqs5p4n2tq19v t17;
                GM_ST(uint8_t, ((uint8_t *)&t17), ((uint8_t)46ULL));
                ((uint8_t *)&t17)[1] = 1;
                uint8_t t18 = (!gm_eqb44cb3cf1n(((uint8_t *)&t16), ((uint8_t *)&t17)));
                if (t18) {
                    goto gm_lb1;
                }
                void * t19 = gm_l0;
                void * t20 = gm_l0;
                uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)t20) + 16));
                uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
                if (o23) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 212, 9);
                uint64_t t24 = r22;
                GM_ST(uint64_t, (((uint8_t *)t19) + 16), t24);
            }
gm_lb3:;
        }
    }
gm_lb1:;
    {
        gm_trqfvmjtt4sij t25 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t21m1eu5ta01n *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t21m1eu5ta01n *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t21m1eu5ta01n *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t21m1eu5ta01n *)gm_out))[0] = 0;
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_t21m1eu5ta01n *)gm_out) + 8), t25);
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
extern __attribute__((aligned(4))) void * gm_f_new_table_hh6cki7(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_number_4l95pjb(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_number_4l95pjb F;
    void * gm_l0; /* r */
    uint64_t gm_l1; /* start */
    uint8_t gm_l2; /* negative */
    uint8_t gm_l3; /* c */
    uint8_t gm_l4; /* <tmp> */
    int64_t gm_l5; /* radix */
    uint8_t gm_l6; /* n */
    int64_t gm_l7; /* n */
    int32_t gm_l8; /* digits */
    uint8_t gm_l9; /* last_underscore */
    uint64_t gm_l10; /* first */
    uint8_t gm_l11; /* d */
    uint8_t gm_l12; /* <tmp> */
    uint8_t gm_l13; /* <tmp> */
    uint8_t gm_l14; /* <tmp> */
    int64_t gm_l15; /* v */
    gm_t913bb735pvt5 gm_l16; /* <tmp> */
    int64_t gm_l17; /* <tmp> */
    int64_t gm_l18; /* <tmp> */
    uint8_t gm_l19; /* k */
    uint8_t gm_l20; /* <tmp> */
    uint8_t gm_l21; /* <tmp> */
    uint8_t gm_l22; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 1981u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l23;
    gm_tqs5p4n2tq19v gm_l24;
    uint8_t gm_l25;
    gm_tqs5p4n2tq19v gm_l26;
    gm_tqs5p4n2tq19v gm_l27;
    gm_tqs5p4n2tq19v gm_l28;
    uint8_t gm_l29;
    uint8_t gm_l30;
    int64_t gm_l31;
    uint8_t gm_l32;
    int64_t gm_l33;
    uint8_t gm_l34;
    int64_t gm_l35;
    gm_t913bb735pvt5 gm_l36;
    gm_t913bb735pvt5 gm_l37;
    gm_tqs5p4n2tq19v gm_l38;
    uint8_t gm_l39;
    uint8_t gm_l40;
    uint8_t gm_l41;
    uint8_t gm_l42;
    uint8_t gm_l43;
    uint8_t gm_l44;
    int64_t gm_l45;
    {
        void * t20 = gm_l0;
        uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)t20) + 16));
        gm_l1 = t21;
        void * t22 = gm_l0;
        GM_ST(uint8_t, (((uint8_t *)t22) + 32), ((uint8_t)1));
        void * t23 = gm_l0;
        gm_tqs5p4n2tq19v t24 = gm_f_peek_bkdvl65(t23);
        gm_tqs5p4n2tq19v t25;
        GM_ST(uint8_t, ((uint8_t *)&t25), ((uint8_t)45ULL));
        ((uint8_t *)&t25)[1] = 1;
        uint8_t t26 = (gm_eqb44cb3cf1n(((uint8_t *)&t24), ((uint8_t *)&t25)));
        gm_l2 = t26;
        void * t27 = gm_l0;
        gm_tqs5p4n2tq19v t28 = gm_f_peek_bkdvl65(t27);
        gm_tqs5p4n2tq19v t29;
        GM_ST(uint8_t, ((uint8_t *)&t29), ((uint8_t)43ULL));
        ((uint8_t *)&t29)[1] = 1;
        gm_tqs5p4n2tq19v t30;
        GM_ST(uint8_t, ((uint8_t *)&t30), ((uint8_t)45ULL));
        ((uint8_t *)&t30)[1] = 1;
        gm_t5r6k0fdcfsb3 t31;
        GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t31 + 0), t29);
        GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t31 + 2), t30);
        uint8_t t32 = 0;
        if (gm_eqb44cb3cf1n(((uint8_t *)&t31) + 0, ((uint8_t *)&t28))) t32 = 1;
        if (gm_eqb44cb3cf1n(((uint8_t *)&t31) + 2, ((uint8_t *)&t28))) t32 = 1;
        uint8_t t33 = (uint8_t)(t32);
        if (t33) {
            void * t34 = gm_l0;
            void * t35 = gm_l0;
            uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)t35) + 16));
            uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
            if (o38) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 289, 33);
            uint64_t t39 = r37;
            GM_ST(uint64_t, (((uint8_t *)t34) + 16), t39);
        }
        void * t40 = gm_l0;
        void * t41 = GM_LD(void *, ((uint8_t *)t40));
        uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)t40) + 8));
        void * t43 = gm_l0;
        uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)t43) + 16));
        uint64_t t45 = (uintptr_t)t44;
        uint64_t t46 = (uintptr_t)t42;
        if (t45 > t46 || t46 > (uintptr_t)t42) gm_trap_slice(t45, t46, t42, gm_src3nt8pf0qk5gn, 290, 8);
        uint64_t t47 = ((uintptr_t)t42) - (uintptr_t)t44;
        gm_b16_8 t48;
        GM_ST(void *, (uint8_t *)&t48, (void *)(((uint8_t *)t41 + (uintptr_t)t44 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(t47));
        gm_b16_8 t49;
        GM_ST(void *, (uint8_t *)&t49, (void *)(gm_sns75reqq32d2));
        GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(3));
        uint8_t t50 = gm_f_starts_with_6vvt01e(t48, t49);
        if (t50) {
            gm_l23 = ((uint8_t)1);
        } else {
            void * t51 = gm_l0;
            void * t52 = GM_LD(void *, ((uint8_t *)t51));
            uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)t51) + 8));
            void * t54 = gm_l0;
            uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)t54) + 16));
            uint64_t t56 = (uintptr_t)t55;
            uint64_t t57 = (uintptr_t)t53;
            if (t56 > t57 || t57 > (uintptr_t)t53) gm_trap_slice(t56, t57, t53, gm_src3nt8pf0qk5gn, 290, 46);
            uint64_t t58 = ((uintptr_t)t53) - (uintptr_t)t55;
            gm_b16_8 t59;
            GM_ST(void *, (uint8_t *)&t59, (void *)(((uint8_t *)t52 + (uintptr_t)t55 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(t58));
            gm_b16_8 t60;
            GM_ST(void *, (uint8_t *)&t60, (void *)(gm_sfa86erpod86s));
            GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(3));
            uint8_t t61 = gm_f_starts_with_6vvt01e(t59, t60);
            gm_l23 = t61;
        }
        uint8_t t62 = gm_l23;
        if (t62) {
            void * t63 = gm_l0;
            void * t64 = gm_l0;
            uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)t64) + 16));
            uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)3ULL), &r66);
            if (o67) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 291, 9);
            uint64_t t68 = r66;
            GM_ST(uint64_t, (((uint8_t *)t63) + 16), t68);
            {
                void * t69 = gm_l0;
                void * t70 = GM_LD(void *, ((uint8_t *)t69));
                uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)t69) + 8));
                uint64_t t72 = gm_l1;
                void * t73 = gm_l0;
                uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)t73) + 16));
                uint64_t t75 = (uintptr_t)t72;
                uint64_t t76 = (uintptr_t)t74;
                if (t75 > t76 || t76 > (uintptr_t)t71) gm_trap_slice(t75, t76, t71, gm_src3nt8pf0qk5gn, 292, 28);
                uint64_t t77 = ((uintptr_t)t74) - (uintptr_t)t72;
                gm_b16_8 t78;
                GM_ST(void *, (uint8_t *)&t78, (void *)(((uint8_t *)t70 + (uintptr_t)t72 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(t77));
                gm_tktr4rmbindgv t79;
                GM_ST(uint8_t, (uint8_t *)&t79 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t79 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t79 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t79 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t79 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t79 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t79 + 0), ((uint8_t)5ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t79 + 8), t78);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 0;
                GM_ST(gm_tktr4rmbindgv, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t79);
                goto gm_fdone;
            }
        }
        void * t80 = gm_l0;
        gm_tqs5p4n2tq19v t81 = gm_f_peek_bkdvl65(t80);
        gm_l24 = t81;
    }
    {
        uint8_t t82 = (uint8_t)((((uint8_t *)&gm_l24))[1] != 0);
        if (t82) {
            uint8_t t83 = GM_LD(uint8_t, ((uint8_t *)&gm_l24));
            gm_l3 = t83;
        } else {
            void * t84 = gm_l0;
            gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)gm_cal7njs7es4));
            gm_tob1snuii5bme t86;
            gm_f_error_96ci5lu((void *)&t86, t84, t85);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
            ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
            GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t86);
            goto gm_fdone;
        }
        uint8_t t87 = gm_l3;
        gm_l4 = t87;
        uint8_t t88 = gm_l4;
        uint8_t t89 = (((uint8_t)48ULL) <= t88);
        if (t89) {
            uint8_t t90 = gm_l4;
            uint8_t t91 = (t90 <= ((uint8_t)57ULL));
            gm_l25 = t91;
        } else {
            gm_l25 = ((uint8_t)0);
        }
        uint8_t t92 = gm_l25;
        uint8_t t93 = (uint8_t)!t92;
        if (t93) {
            void * t94 = gm_l0;
            gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)gm_cal7njs7es4));
            gm_tob1snuii5bme t96;
            gm_f_error_96ci5lu((void *)&t96, t94, t95);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
            ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
            GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t96);
            goto gm_fdone;
        }
        gm_l5 = ((int64_t)10LL);
        uint8_t t97 = gm_l3;
        uint8_t t98 = ((t97 == ((uint8_t)48ULL)));
        if (t98) {
            {
                {
                    void * t99 = gm_l0;
                    void * t100 = GM_LD(void *, ((uint8_t *)t99));
                    uint64_t t101 = GM_LD(uint64_t, (((uint8_t *)t99) + 8));
                    void * t102 = gm_l0;
                    uint64_t t103 = GM_LD(uint64_t, (((uint8_t *)t102) + 16));
                    uint64_t r104; bool o105 = __builtin_add_overflow(t103, ((uint64_t)1ULL), &r104);
                    if (o105) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 298, 28);
                    uint64_t t106 = r104;
                    uint8_t t107 = (t106 < t101);
                    if (t107) {
                        uint8_t t108 = GM_LD(uint8_t, (((uint8_t *)t100) + (uintptr_t)t106 * 1));
                        gm_tqs5p4n2tq19v t109;
                        GM_ST(uint8_t, ((uint8_t *)&t109), t108);
                        ((uint8_t *)&t109)[1] = 1;
                        gm_l26 = t109;
                    } else {
                        gm_tqs5p4n2tq19v t110;
                        memset(&t110, 0, sizeof t110);
                        gm_l26 = t110;
                    }
                    gm_tqs5p4n2tq19v t111 = gm_l26;
                    gm_l27 = t111;
                    uint8_t t112 = (uint8_t)((((uint8_t *)&gm_l27))[1] != 0);
                    if (!t112) {
                        goto gm_lb2;
                    }
                    uint8_t t113 = GM_LD(uint8_t, ((uint8_t *)&gm_l27));
                    gm_l6 = t113;
                    uint8_t t114 = gm_l6;
                    uint8_t t115 = ((t114 == ((uint8_t)120ULL)));
                    if (t115) {
                        gm_l5 = ((int64_t)16LL);
                    } else {
                        uint8_t t116 = gm_l6;
                        uint8_t t117 = ((t116 == ((uint8_t)111ULL)));
                        if (t117) {
                            gm_l5 = ((int64_t)8LL);
                        } else {
                            uint8_t t118 = gm_l6;
                            uint8_t t119 = ((t118 == ((uint8_t)98ULL)));
                            if (t119) {
                                gm_l5 = ((int64_t)2LL);
                            }
                        }
                    }
                    goto gm_lb1;
                }
gm_lb2:;
            }
gm_lb1:;
        }
    }
    {
        int64_t t120 = gm_l5;
        uint8_t t121 = (!(t120 == ((int64_t)10LL)));
        if (t121) {
            void * t122 = gm_l0;
            void * t123 = gm_l0;
            uint64_t t124 = GM_LD(uint64_t, (((uint8_t *)t123) + 16));
            uint64_t r125; bool o126 = __builtin_add_overflow(t124, ((uint64_t)2ULL), &r125);
            if (o126) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 302, 26);
            uint64_t t127 = r125;
            GM_ST(uint64_t, (((uint8_t *)t122) + 16), t127);
        }
        gm_l7 = ((int64_t)0LL);
        gm_l8 = ((int32_t)0LL);
        gm_l9 = ((uint8_t)0);
        void * t128 = gm_l0;
        uint64_t t129 = GM_LD(uint64_t, (((uint8_t *)t128) + 16));
        gm_l10 = t129;
        {
            for (;;) {
                void * t130 = gm_l0;
                gm_tqs5p4n2tq19v t131 = gm_f_peek_bkdvl65(t130);
                gm_l28 = t131;
                uint8_t t132 = (uint8_t)((((uint8_t *)&gm_l28))[1] != 0);
                if (!t132) {
                    goto gm_lb3;
                }
                uint8_t t133 = GM_LD(uint8_t, ((uint8_t *)&gm_l28));
                gm_l11 = t133;
                {
                    uint8_t t134 = gm_l11;
                    uint8_t t135 = ((t134 == ((uint8_t)95ULL)));
                    if (t135) {
                        int32_t t136 = gm_l8;
                        uint8_t t137 = ((t136 == ((int32_t)0LL)));
                        if (t137) {
                            gm_l29 = ((uint8_t)1);
                        } else {
                            uint8_t t138 = gm_l9;
                            gm_l29 = t138;
                        }
                        uint8_t t139 = gm_l29;
                        if (t139) {
                            void * t140 = gm_l0;
                            gm_b16_8 t141;
                            GM_ST(void *, (uint8_t *)&t141, (void *)(gm_s2a4d8jj74v6o));
                            GM_ST(uintptr_t, (uint8_t *)&t141 + 8, (uintptr_t)(48));
                            gm_tob1snuii5bme t142;
                            gm_f_error_96ci5lu((void *)&t142, t140, t141);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                            ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                            GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t142);
                            goto gm_fdone;
                        }
                        gm_l9 = ((uint8_t)1);
                        void * t143 = gm_l0;
                        void * t144 = gm_l0;
                        uint64_t t145 = GM_LD(uint64_t, (((uint8_t *)t144) + 16));
                        uint64_t r146; bool o147 = __builtin_add_overflow(t145, ((uint64_t)1ULL), &r146);
                        if (o147) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 311, 13);
                        uint64_t t148 = r146;
                        GM_ST(uint64_t, (((uint8_t *)t143) + 16), t148);
                        goto gm_lb5;
                    }
                    uint8_t t149 = gm_l11;
                    gm_l12 = t149;
                    uint8_t t150 = gm_l12;
                    uint8_t t151 = (((uint8_t)48ULL) <= t150);
                    if (t151) {
                        uint8_t t152 = gm_l12;
                        uint8_t t153 = (t152 <= ((uint8_t)57ULL));
                        gm_l30 = t153;
                    } else {
                        gm_l30 = ((uint8_t)0);
                    }
                    uint8_t t154 = gm_l30;
                    if (t154) {
                        uint8_t t155 = gm_l11;
                        uint8_t r156; bool o157 = __builtin_sub_overflow(t155, ((uint8_t)48ULL), &r156);
                        if (o157) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 315, 17);
                        uint8_t t158 = r156;
                        int64_t t159 = (int64_t)t158;
                        gm_l15 = t159;
                    } else {
                        {
                            {
                                int64_t t160 = gm_l5;
                                uint8_t t161 = ((t160 == ((int64_t)16LL)));
                                if (!t161) {
                                    goto gm_lb7;
                                }
                                uint8_t t162 = gm_l11;
                                gm_l13 = t162;
                                uint8_t t163 = gm_l13;
                                uint8_t t164 = (((uint8_t)97ULL) <= t163);
                                if (t164) {
                                    uint8_t t165 = gm_l13;
                                    uint8_t t166 = (t165 <= ((uint8_t)102ULL));
                                    gm_l32 = t166;
                                } else {
                                    gm_l32 = ((uint8_t)0);
                                }
                                uint8_t t167 = gm_l32;
                                if (!t167) {
                                    goto gm_lb7;
                                }
                                uint8_t t168 = gm_l11;
                                uint8_t r169; bool o170 = __builtin_sub_overflow(t168, ((uint8_t)97ULL), &r169);
                                if (o170) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 317, 17);
                                uint8_t t171 = r169;
                                int64_t t172 = (int64_t)t171;
                                int64_t r173; bool o174 = __builtin_add_overflow(t172, ((int64_t)10LL), &r173);
                                if (o174) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 317, 13);
                                int64_t t175 = r173;
                                gm_l31 = t175;
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                {
                                    int64_t t176 = gm_l5;
                                    uint8_t t177 = ((t176 == ((int64_t)16LL)));
                                    if (!t177) {
                                        goto gm_lb9;
                                    }
                                    uint8_t t178 = gm_l11;
                                    gm_l14 = t178;
                                    uint8_t t179 = gm_l14;
                                    uint8_t t180 = (((uint8_t)65ULL) <= t179);
                                    if (t180) {
                                        uint8_t t181 = gm_l14;
                                        uint8_t t182 = (t181 <= ((uint8_t)70ULL));
                                        gm_l34 = t182;
                                    } else {
                                        gm_l34 = ((uint8_t)0);
                                    }
                                    uint8_t t183 = gm_l34;
                                    if (!t183) {
                                        goto gm_lb9;
                                    }
                                    uint8_t t184 = gm_l11;
                                    uint8_t r185; bool o186 = __builtin_sub_overflow(t184, ((uint8_t)65ULL), &r185);
                                    if (o186) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 319, 17);
                                    uint8_t t187 = r185;
                                    int64_t t188 = (int64_t)t187;
                                    int64_t r189; bool o190 = __builtin_add_overflow(t188, ((int64_t)10LL), &r189);
                                    if (o190) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 319, 13);
                                    int64_t t191 = r189;
                                    gm_l33 = t191;
                                    goto gm_lb8;
                                }
gm_lb9:;
                                gm_l33 = ((int64_t)99LL);
                            }
gm_lb8:;
                            int64_t t192 = gm_l33;
                            gm_l31 = t192;
                        }
gm_lb6:;
                        int64_t t193 = gm_l31;
                        gm_l15 = t193;
                    }
                    int64_t t194 = gm_l15;
                    gm_l15 = t194;
                    int64_t t195 = gm_l15;
                    int64_t t196 = gm_l5;
                    uint8_t t197 = (t195 >= t196);
                    if (t197) {
                        goto gm_lb3;
                    }
                    {
                        {
                            int32_t t198 = gm_l8;
                            uint8_t t199 = ((t198 == ((int32_t)1LL)));
                            if (!t199) {
                                goto gm_lb11;
                            }
                            void * t200 = gm_l0;
                            void * t201 = GM_LD(void *, ((uint8_t *)t200));
                            uint64_t t202 = GM_LD(uint64_t, (((uint8_t *)t200) + 8));
                            uint64_t t203 = gm_l10;
                            if ((uintptr_t)t203 >= (uintptr_t)(t202)) gm_trap_index((uintptr_t)t203, t202, gm_src3nt8pf0qk5gn, 324, 28);
                            uint8_t t204 = GM_LD(uint8_t, (((uint8_t *)t201) + (uintptr_t)t203 * 1));
                            uint8_t t205 = ((t204 == ((uint8_t)48ULL)));
                            if (!t205) {
                                goto gm_lb11;
                            }
                            int64_t t206 = gm_l5;
                            uint8_t t207 = ((t206 == ((int64_t)10LL)));
                            if (!t207) {
                                goto gm_lb11;
                            }
                            {
                                void * t208 = gm_l0;
                                gm_b16_8 t209;
                                GM_ST(void *, (uint8_t *)&t209, (void *)(gm_s8lbhmq10rt7i));
                                GM_ST(uintptr_t, (uint8_t *)&t209 + 8, (uintptr_t)(41));
                                gm_tob1snuii5bme t210;
                                gm_f_error_96ci5lu((void *)&t210, t208, t209);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                                ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                                GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t210);
                                goto gm_fdone;
                            }
                        }
gm_lb11:;
                    }
gm_lb10:;
                    int64_t t211 = gm_l7;
                    int64_t t212 = gm_l5;
                    int64_t r213; bool o214 = __builtin_mul_overflow(t211, t212, &r213);
                    gm_t913bb735pvt5 t215;
                    if (o214) {
                    gm_t913bb735pvt5 t216;
                    memset(&t216, 0, sizeof t216);
                        t215 = t216;
                    } else {
                    int64_t t217 = r213;
                    gm_t913bb735pvt5 t218;
                    GM_ST(uint8_t, (uint8_t *)&t218 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t218 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t218 + 12, 0);
                    GM_ST(int64_t, ((uint8_t *)&t218), t217);
                    ((uint8_t *)&t218)[8] = 1;
                        t215 = t218;
                    }
                    gm_l16 = t215;
                    int64_t t219 = gm_l15;
                    gm_l18 = t219;
                    {
                        {
                            uint8_t t220 = (uint8_t)((((uint8_t *)&gm_l16))[8] != 0);
                            if (!t220) {
                                goto gm_lb13;
                            }
                            int64_t t221 = GM_LD(int64_t, ((uint8_t *)&gm_l16));
                            gm_l17 = t221;
                            int64_t t222 = gm_l17;
                            int64_t t223 = gm_l18;
                            int64_t r224; bool o225 = __builtin_add_overflow(t222, t223, &r224);
                            gm_t913bb735pvt5 t226;
                            if (o225) {
                            gm_t913bb735pvt5 t227;
                            memset(&t227, 0, sizeof t227);
                                t226 = t227;
                            } else {
                            int64_t t228 = r224;
                            gm_t913bb735pvt5 t229;
                            GM_ST(uint8_t, (uint8_t *)&t229 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t229 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t229 + 12, 0);
                            GM_ST(int64_t, ((uint8_t *)&t229), t228);
                            ((uint8_t *)&t229)[8] = 1;
                                t226 = t229;
                            }
                            gm_l36 = t226;
                            goto gm_lb12;
                        }
gm_lb13:;
                        gm_t913bb735pvt5 t230;
                        memset(&t230, 0, sizeof t230);
                        gm_l36 = t230;
                    }
gm_lb12:;
                    gm_t913bb735pvt5 t231 = gm_l36;
                    gm_l37 = t231;
                    uint8_t t232 = (uint8_t)((((uint8_t *)&gm_l37))[8] != 0);
                    if (t232) {
                        int64_t t233 = GM_LD(int64_t, ((uint8_t *)&gm_l37));
                        gm_l35 = t233;
                    } else {
                        void * t234 = gm_l0;
                        gm_b16_8 t235;
                        GM_ST(void *, (uint8_t *)&t235, (void *)(gm_s70iivik802q4));
                        GM_ST(uintptr_t, (uint8_t *)&t235 + 8, (uintptr_t)(25));
                        gm_tob1snuii5bme t236;
                        gm_f_error_96ci5lu((void *)&t236, t234, t235);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                        ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t236);
                        goto gm_fdone;
                    }
                    int64_t t237 = gm_l35;
                    gm_l7 = t237;
                    int32_t t238 = gm_l8;
                    int32_t r239; bool o240 = __builtin_add_overflow(t238, ((int32_t)1LL), &r239);
                    if (o240) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 326, 9);
                    int32_t t241 = r239;
                    gm_l8 = t241;
                    gm_l9 = ((uint8_t)0);
                    void * t242 = gm_l0;
                    void * t243 = gm_l0;
                    uint64_t t244 = GM_LD(uint64_t, (((uint8_t *)t243) + 16));
                    uint64_t r245; bool o246 = __builtin_add_overflow(t244, ((uint64_t)1ULL), &r245);
                    if (o246) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 328, 9);
                    uint64_t t247 = r245;
                    GM_ST(uint64_t, (((uint8_t *)t242) + 16), t247);
                }
gm_lb5:;
            }
        }
gm_lb3:;
    }
    {
        uint8_t t248 = gm_l9;
        if (t248) {
            void * t249 = gm_l0;
            gm_b16_8 t250;
            GM_ST(void *, (uint8_t *)&t250, (void *)(gm_s2a4d8jj74v6o));
            GM_ST(uintptr_t, (uint8_t *)&t250 + 8, (uintptr_t)(48));
            gm_tob1snuii5bme t251;
            gm_f_error_96ci5lu((void *)&t251, t249, t250);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
            ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
            GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t251);
            goto gm_fdone;
        }
        int32_t t252 = gm_l8;
        uint8_t t253 = ((t252 == ((int32_t)0LL)));
        if (t253) {
            void * t254 = gm_l0;
            gm_b16_8 t255;
            GM_ST(void *, (uint8_t *)&t255, (void *)(gm_s70iivik802q4));
            GM_ST(uintptr_t, (uint8_t *)&t255 + 8, (uintptr_t)(25));
            gm_tob1snuii5bme t256;
            gm_f_error_96ci5lu((void *)&t256, t254, t255);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
            ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
            GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t256);
            goto gm_fdone;
        }
        {
            {
                int64_t t257 = gm_l5;
                uint8_t t258 = ((t257 == ((int64_t)10LL)));
                if (!t258) {
                    goto gm_lb15;
                }
                void * t259 = gm_l0;
                gm_tqs5p4n2tq19v t260 = gm_f_peek_bkdvl65(t259);
                gm_tqs5p4n2tq19v t261;
                GM_ST(uint8_t, ((uint8_t *)&t261), ((uint8_t)46ULL));
                ((uint8_t *)&t261)[1] = 1;
                gm_tqs5p4n2tq19v t262;
                GM_ST(uint8_t, ((uint8_t *)&t262), ((uint8_t)101ULL));
                ((uint8_t *)&t262)[1] = 1;
                gm_tqs5p4n2tq19v t263;
                GM_ST(uint8_t, ((uint8_t *)&t263), ((uint8_t)69ULL));
                ((uint8_t *)&t263)[1] = 1;
                gm_tqs5p4n2tq19v t264;
                GM_ST(uint8_t, ((uint8_t *)&t264), ((uint8_t)45ULL));
                ((uint8_t *)&t264)[1] = 1;
                gm_tqs5p4n2tq19v t265;
                GM_ST(uint8_t, ((uint8_t *)&t265), ((uint8_t)58ULL));
                ((uint8_t *)&t265)[1] = 1;
                gm_tcdtdmenq84v9 t266;
                GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t266 + 0), t261);
                GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t266 + 2), t262);
                GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t266 + 4), t263);
                GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t266 + 6), t264);
                GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t266 + 8), t265);
                uint8_t t267 = 0;
                if (gm_eqb44cb3cf1n(((uint8_t *)&t266) + 0, ((uint8_t *)&t260))) t267 = 1;
                if (gm_eqb44cb3cf1n(((uint8_t *)&t266) + 2, ((uint8_t *)&t260))) t267 = 1;
                if (gm_eqb44cb3cf1n(((uint8_t *)&t266) + 4, ((uint8_t *)&t260))) t267 = 1;
                if (gm_eqb44cb3cf1n(((uint8_t *)&t266) + 6, ((uint8_t *)&t260))) t267 = 1;
                if (gm_eqb44cb3cf1n(((uint8_t *)&t266) + 8, ((uint8_t *)&t260))) t267 = 1;
                uint8_t t268 = (uint8_t)(t267);
                if (!t268) {
                    goto gm_lb15;
                }
                {
                    for (;;) {
                        void * t269 = gm_l0;
                        gm_tqs5p4n2tq19v t270 = gm_f_peek_bkdvl65(t269);
                        gm_l38 = t270;
                        uint8_t t271 = (uint8_t)((((uint8_t *)&gm_l38))[1] != 0);
                        if (!t271) {
                            goto gm_lb16;
                        }
                        uint8_t t272 = GM_LD(uint8_t, ((uint8_t *)&gm_l38));
                        gm_l19 = t272;
                        {
                            uint8_t t273 = gm_l19;
                            gm_l20 = t273;
                            uint8_t t274 = gm_l20;
                            uint8_t t275 = (((uint8_t)97ULL) <= t274);
                            if (t275) {
                                uint8_t t276 = gm_l20;
                                uint8_t t277 = (t276 <= ((uint8_t)122ULL));
                                gm_l42 = t277;
                            } else {
                                gm_l42 = ((uint8_t)0);
                            }
                            uint8_t t278 = gm_l42;
                            if (t278) {
                                gm_l41 = ((uint8_t)1);
                            } else {
                                uint8_t t279 = gm_l19;
                                gm_l21 = t279;
                                uint8_t t280 = gm_l21;
                                uint8_t t281 = (((uint8_t)65ULL) <= t280);
                                if (t281) {
                                    uint8_t t282 = gm_l21;
                                    uint8_t t283 = (t282 <= ((uint8_t)90ULL));
                                    gm_l43 = t283;
                                } else {
                                    gm_l43 = ((uint8_t)0);
                                }
                                uint8_t t284 = gm_l43;
                                gm_l41 = t284;
                            }
                            uint8_t t285 = gm_l41;
                            if (t285) {
                                gm_l40 = ((uint8_t)1);
                            } else {
                                uint8_t t286 = gm_l19;
                                gm_l22 = t286;
                                uint8_t t287 = gm_l22;
                                uint8_t t288 = (((uint8_t)48ULL) <= t287);
                                if (t288) {
                                    uint8_t t289 = gm_l22;
                                    uint8_t t290 = (t289 <= ((uint8_t)57ULL));
                                    gm_l44 = t290;
                                } else {
                                    gm_l44 = ((uint8_t)0);
                                }
                                uint8_t t291 = gm_l44;
                                gm_l40 = t291;
                            }
                            uint8_t t292 = gm_l40;
                            if (t292) {
                                gm_l39 = ((uint8_t)1);
                            } else {
                                uint8_t t293 = gm_l19;
                                gm_tb1msgo1fu1n3 t294;
                                GM_ST(uint8_t, ((uint8_t *)&t294 + 0), ((uint8_t)46ULL));
                                GM_ST(uint8_t, ((uint8_t *)&t294 + 1), ((uint8_t)58ULL));
                                GM_ST(uint8_t, ((uint8_t *)&t294 + 2), ((uint8_t)45ULL));
                                GM_ST(uint8_t, ((uint8_t *)&t294 + 3), ((uint8_t)43ULL));
                                GM_ST(uint8_t, ((uint8_t *)&t294 + 4), ((uint8_t)95ULL));
                                uint8_t t295 = 0;
                                if (gm_eqjmccqdb90f(((uint8_t *)&t294) + 0, ((uint8_t *)&t293))) t295 = 1;
                                if (gm_eqjmccqdb90f(((uint8_t *)&t294) + 1, ((uint8_t *)&t293))) t295 = 1;
                                if (gm_eqjmccqdb90f(((uint8_t *)&t294) + 2, ((uint8_t *)&t293))) t295 = 1;
                                if (gm_eqjmccqdb90f(((uint8_t *)&t294) + 3, ((uint8_t *)&t293))) t295 = 1;
                                if (gm_eqjmccqdb90f(((uint8_t *)&t294) + 4, ((uint8_t *)&t293))) t295 = 1;
                                uint8_t t296 = (uint8_t)(t295);
                                gm_l39 = t296;
                            }
                            uint8_t t297 = gm_l39;
                            uint8_t t298 = (uint8_t)!t297;
                            if (t298) {
                                goto gm_lb16;
                            }
                            void * t299 = gm_l0;
                            void * t300 = gm_l0;
                            uint64_t t301 = GM_LD(uint64_t, (((uint8_t *)t300) + 16));
                            uint64_t r302; bool o303 = __builtin_add_overflow(t301, ((uint64_t)1ULL), &r302);
                            if (o303) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 336, 13);
                            uint64_t t304 = r302;
                            GM_ST(uint64_t, (((uint8_t *)t299) + 16), t304);
                        }
gm_lb18:;
                    }
                }
gm_lb16:;
                {
                    void * t305 = gm_l0;
                    void * t306 = GM_LD(void *, ((uint8_t *)t305));
                    uint64_t t307 = GM_LD(uint64_t, (((uint8_t *)t305) + 8));
                    uint64_t t308 = gm_l1;
                    void * t309 = gm_l0;
                    uint64_t t310 = GM_LD(uint64_t, (((uint8_t *)t309) + 16));
                    uint64_t t311 = (uintptr_t)t308;
                    uint64_t t312 = (uintptr_t)t310;
                    if (t311 > t312 || t312 > (uintptr_t)t307) gm_trap_slice(t311, t312, t307, gm_src3nt8pf0qk5gn, 338, 28);
                    uint64_t t313 = ((uintptr_t)t310) - (uintptr_t)t308;
                    gm_b16_8 t314;
                    GM_ST(void *, (uint8_t *)&t314, (void *)(((uint8_t *)t306 + (uintptr_t)t308 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t314 + 8, (uintptr_t)(t313));
                    gm_tktr4rmbindgv t315;
                    GM_ST(uint8_t, (uint8_t *)&t315 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t315 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t315 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t315 + 24, 0);
                    GM_ST(uint64_t, (uint8_t *)&t315 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t315 + 40, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t315 + 0), ((uint8_t)5ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t315 + 8), t314);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 0;
                    GM_ST(gm_tktr4rmbindgv, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t315);
                    goto gm_fdone;
                }
            }
gm_lb15:;
        }
gm_lb14:;
    }
    {
        uint8_t t316 = gm_l2;
        if (t316) {
            int64_t t317 = gm_l7;
            if (t317 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 340, 36);
            int64_t t318 = (int64_t)((uint64_t)0 - (uint64_t)t317);
            gm_l45 = t318;
        } else {
            int64_t t319 = gm_l7;
            gm_l45 = t319;
        }
        int64_t t320 = gm_l45;
        gm_tktr4rmbindgv t321;
        memset(&t321, 0, sizeof t321);
        GM_ST(uint8_t, ((uint8_t *)&t321 + 0), ((uint8_t)1ULL));
        GM_ST(int64_t, ((uint8_t *)&t321 + 8), t320);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 0;
        GM_ST(gm_tktr4rmbindgv, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t321);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tefknldmnh2si gm_f_number_4l95pjb_c(void * a0) { gm_tefknldmnh2si gm_r; gm_f_number_4l95pjb(&gm_r, a0); return gm_r; }
__attribute__((aligned(4))) void gm_f_parse_string_80niubc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_string_80niubc F;
    void * gm_l0; /* r */
    uint8_t gm_l1; /* quote */
    uint8_t gm_l2; /* triple */
    gm_b16_8 gm_l3; /* kind */
    gm_b16_8 gm_l4; /* unclosed */
    uint8_t gm_l6; /* c */
    int32_t gm_l7; /* extra */
    uint8_t gm_l8; /* e */
    uint8_t gm_l9; /* broke */
    uint8_t gm_l10; /* k */
    GM_STACK_CHECK(&F, 2340u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l11;
    uint64_t gm_l12;
    gm_tabb710d9mq2e gm_l13;
    gm_tabb710d9mq2e gm_l14;
    gm_tqs5p4n2tq19v gm_l15;
    gm_tqs5p4n2tq19v gm_l16;
    gm_b16_8 gm_l17;
    gm_tftb1odc739t5 gm_l18;
    gm_tftb1odc739t5 gm_l19;
    gm_tqs5p4n2tq19v gm_l20;
    gm_tftb1odc739t5 gm_l21;
    gm_tftb1odc739t5 gm_l22;
    gm_tftb1odc739t5 gm_l23;
    gm_tftb1odc739t5 gm_l24;
    gm_tftb1odc739t5 gm_l25;
    gm_tftb1odc739t5 gm_l26;
    gm_tftb1odc739t5 gm_l27;
    gm_tr1k5nl69lhjo gm_l28;
    gm_tr1k5nl69lhjo gm_l29;
    gm_tr1k5nl69lhjo gm_l30;
    gm_tqs5p4n2tq19v gm_l31;
    gm_tftb1odc739t5 gm_l32;
    {
        void * t36 = gm_l0;
        void * t37 = GM_LD(void *, ((uint8_t *)t36));
        uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)t36) + 8));
        void * t39 = gm_l0;
        uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)t39) + 16));
        if ((uintptr_t)t40 >= (uintptr_t)(t38)) gm_trap_index((uintptr_t)t40, t38, gm_src3nt8pf0qk5gn, 345, 14);
        uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)t37) + (uintptr_t)t40 * 1));
        gm_l1 = t41;
        void * t42 = gm_l0;
        void * t43 = GM_LD(void *, ((uint8_t *)t42));
        uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)t42) + 8));
        void * t45 = gm_l0;
        uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)t45) + 16));
        uint64_t t47 = (uintptr_t)t46;
        uint64_t t48 = (uintptr_t)t44;
        if (t47 > t48 || t48 > (uintptr_t)t44) gm_trap_slice(t47, t48, t44, gm_src3nt8pf0qk5gn, 346, 15);
        uint64_t t49 = ((uintptr_t)t44) - (uintptr_t)t46;
        gm_b16_8 t50;
        GM_ST(void *, (uint8_t *)&t50, (void *)(((uint8_t *)t43 + (uintptr_t)t46 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(t49));
        uint8_t t51 = gm_l1;
        uint8_t t52 = ((t51 == ((uint8_t)34ULL)));
        if (t52) {
            gm_b16_8 t53;
            GM_ST(void *, (uint8_t *)&t53, (void *)(gm_s8rm42n03sfss));
            GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(3));
            gm_l11 = t53;
        } else {
            gm_b16_8 t54;
            GM_ST(void *, (uint8_t *)&t54, (void *)(gm_soakgb2h3g40i));
            GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(3));
            gm_l11 = t54;
        }
        gm_b16_8 t55 = gm_l11;
        uint8_t t56 = gm_f_starts_with_6vvt01e(t50, t55);
        gm_l2 = t56;
        void * t57 = gm_l0;
        void * t58 = gm_l0;
        uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)t58) + 16));
        uint8_t t60 = gm_l2;
        if (t60) {
            gm_l12 = ((uint64_t)3ULL);
        } else {
            gm_l12 = ((uint64_t)1ULL);
        }
        uint64_t t61 = gm_l12;
        uint64_t r62; bool o63 = __builtin_add_overflow(t59, t61, &r62);
        if (o63) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 347, 5);
        uint64_t t64 = r62;
        GM_ST(uint64_t, (((uint8_t *)t57) + 16), t64);
        uint8_t t65 = gm_l1;
        uint8_t t66 = ((t65 == ((uint8_t)34ULL)));
        if (t66) {
            gm_b16_8 t67;
            GM_ST(void *, (uint8_t *)&t67, (void *)(gm_scvuqmspcpb1m));
            GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(5));
            gm_l3 = t67;
        } else {
            gm_b16_8 t68;
            GM_ST(void *, (uint8_t *)&t68, (void *)(gm_s4lo69slu4igt));
            GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(7));
            gm_l3 = t68;
        }
        uint8_t t69 = gm_l2;
        if (t69) {
            {
                gm_b16_8 t70 = gm_l3;
                void * t71 = gm_l0;
                void * t72 = GM_LD(void *, ((uint8_t *)t71));
                uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)t71) + 8));
                void * t74 = gm_l0;
                uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)t74) + 16));
                uint64_t r76; bool o77 = __builtin_sub_overflow(t75, ((uint64_t)1ULL), &r76);
                if (o77) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 350, 80);
                uint64_t t78 = r76;
                void * t79 = gm_l0;
                uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)t79) + 16));
                uint64_t t81 = (uintptr_t)t78;
                uint64_t t82 = (uintptr_t)t80;
                if (t81 > t82 || t82 > (uintptr_t)t73) gm_trap_slice(t81, t82, t73, gm_src3nt8pf0qk5gn, 350, 73);
                uint64_t t83 = ((uintptr_t)t80) - (uintptr_t)t78;
                gm_b16_8 t84;
                GM_ST(void *, (uint8_t *)&t84, (void *)(((uint8_t *)t72 + (uintptr_t)t78 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(t83));
                gm_t6j6smj7qoctu t85;
                GM_ST(gm_b16_8, ((uint8_t *)&t85 + 0), t70);
                GM_ST(gm_b16_8, ((uint8_t *)&t85 + 16), t84);
                gm_f_format_u10ruvh((void *)&gm_l13, (void *)NULL, cx, NULL, &t85);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t86 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
            if (!t86) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_src3nt8pf0qk5gn, 350, 9);
            gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
            gm_l4 = t87;
        } else {
            {
                gm_b16_8 t88 = gm_l3;
                void * t89 = gm_l0;
                void * t90 = GM_LD(void *, ((uint8_t *)t89));
                uint64_t t91 = GM_LD(uint64_t, (((uint8_t *)t89) + 8));
                void * t92 = gm_l0;
                uint64_t t93 = GM_LD(uint64_t, (((uint8_t *)t92) + 16));
                uint64_t r94; bool o95 = __builtin_sub_overflow(t93, ((uint64_t)1ULL), &r94);
                if (o95) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 352, 69);
                uint64_t t96 = r94;
                void * t97 = gm_l0;
                uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)t97) + 16));
                uint64_t t99 = (uintptr_t)t96;
                uint64_t t100 = (uintptr_t)t98;
                if (t99 > t100 || t100 > (uintptr_t)t91) gm_trap_slice(t99, t100, t91, gm_src3nt8pf0qk5gn, 352, 62);
                uint64_t t101 = ((uintptr_t)t98) - (uintptr_t)t96;
                gm_b16_8 t102;
                GM_ST(void *, (uint8_t *)&t102, (void *)(((uint8_t *)t90 + (uintptr_t)t96 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t102 + 8, (uintptr_t)(t101));
                gm_t6j6smj7qoctu t103;
                GM_ST(gm_b16_8, ((uint8_t *)&t103 + 0), t88);
                GM_ST(gm_b16_8, ((uint8_t *)&t103 + 16), t102);
                gm_f_format_0pvgsic((void *)&gm_l14, (void *)NULL, cx, NULL, &t103);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t104 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
            if (!t104) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_src3nt8pf0qk5gn, 352, 9);
            gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
            gm_l4 = t105;
        }
    }
    {
        gm_trqfvmjtt4sij t106;
        gm_f_list_uvfu1k1((void *)&t106, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l5 = t106;
        uint8_t t107 = gm_l2;
        if (t107) {
            {
                {
                    void * t108 = gm_l0;
                    gm_tqs5p4n2tq19v t109 = gm_f_peek_bkdvl65(t108);
                    gm_tqs5p4n2tq19v t110;
                    GM_ST(uint8_t, ((uint8_t *)&t110), ((uint8_t)13ULL));
                    ((uint8_t *)&t110)[1] = 1;
                    uint8_t t111 = (gm_eqb44cb3cf1n(((uint8_t *)&t109), ((uint8_t *)&t110)));
                    if (!t111) {
                        goto gm_lb2;
                    }
                    void * t112 = gm_l0;
                    void * t113 = GM_LD(void *, ((uint8_t *)t112));
                    uint64_t t114 = GM_LD(uint64_t, (((uint8_t *)t112) + 8));
                    void * t115 = gm_l0;
                    uint64_t t116 = GM_LD(uint64_t, (((uint8_t *)t115) + 16));
                    uint64_t r117; bool o118 = __builtin_add_overflow(t116, ((uint64_t)1ULL), &r117);
                    if (o118) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 357, 44);
                    uint64_t t119 = r117;
                    uint8_t t120 = (t119 < t114);
                    if (t120) {
                        uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)t113) + (uintptr_t)t119 * 1));
                        gm_tqs5p4n2tq19v t122;
                        GM_ST(uint8_t, ((uint8_t *)&t122), t121);
                        ((uint8_t *)&t122)[1] = 1;
                        gm_l15 = t122;
                    } else {
                        gm_tqs5p4n2tq19v t123;
                        memset(&t123, 0, sizeof t123);
                        gm_l15 = t123;
                    }
                    gm_tqs5p4n2tq19v t124 = gm_l15;
                    gm_tqs5p4n2tq19v t125;
                    GM_ST(uint8_t, ((uint8_t *)&t125), ((uint8_t)10ULL));
                    ((uint8_t *)&t125)[1] = 1;
                    uint8_t t126 = (gm_eqb44cb3cf1n(((uint8_t *)&t124), ((uint8_t *)&t125)));
                    if (!t126) {
                        goto gm_lb2;
                    }
                    void * t127 = gm_l0;
                    void * t128 = gm_l0;
                    uint64_t t129 = GM_LD(uint64_t, (((uint8_t *)t128) + 16));
                    uint64_t r130; bool o131 = __builtin_add_overflow(t129, ((uint64_t)1ULL), &r130);
                    if (o131) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 357, 65);
                    uint64_t t132 = r130;
                    GM_ST(uint64_t, (((uint8_t *)t127) + 16), t132);
                    goto gm_lb1;
                }
gm_lb2:;
            }
gm_lb1:;
            void * t133 = gm_l0;
            gm_tqs5p4n2tq19v t134 = gm_f_peek_bkdvl65(t133);
            gm_tqs5p4n2tq19v t135;
            GM_ST(uint8_t, ((uint8_t *)&t135), ((uint8_t)10ULL));
            ((uint8_t *)&t135)[1] = 1;
            uint8_t t136 = (gm_eqb44cb3cf1n(((uint8_t *)&t134), ((uint8_t *)&t135)));
            if (t136) {
                void * t137 = gm_l0;
                void * t138 = gm_l0;
                uint64_t t139 = GM_LD(uint64_t, (((uint8_t *)t138) + 16));
                uint64_t r140; bool o141 = __builtin_add_overflow(t139, ((uint64_t)1ULL), &r140);
                if (o141) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 359, 13);
                uint64_t t142 = r140;
                GM_ST(uint64_t, (((uint8_t *)t137) + 16), t142);
                void * t143 = gm_l0;
                void * t144 = gm_l0;
                uint64_t t145 = GM_LD(uint64_t, (((uint8_t *)t144) + 24));
                uint64_t r146; bool o147 = __builtin_add_overflow(t145, ((uint64_t)1ULL), &r146);
                if (o147) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 360, 13);
                uint64_t t148 = r146;
                GM_ST(uint64_t, (((uint8_t *)t143) + 24), t148);
            }
        }
        {
            for (;;) {
                {
                    if (!((uint8_t)1)) {
                        goto gm_lb3;
                    }
                    {
                        {
                            void * t149 = gm_l0;
                            gm_tqs5p4n2tq19v t150 = gm_f_peek_bkdvl65(t149);
                            gm_l16 = t150;
                            uint8_t t151 = (uint8_t)((((uint8_t *)&gm_l16))[1] != 0);
                            if (t151) {
                                uint8_t t152 = GM_LD(uint8_t, ((uint8_t *)&gm_l16));
                                gm_l6 = t152;
                            } else {
                                void * t153 = gm_l0;
                                gm_b16_8 t154 = gm_l4;
                                gm_b16_8 t155 = GM_LD(gm_b16_8, ((uint8_t *)&t154));
                                gm_tob1snuii5bme t156;
                                gm_f_error_96ci5lu((void *)&t156, t153, t155);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out))[0] = 1;
                                GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 8), t156);
                                goto gm_fdone;
                            }
                            uint8_t t157 = gm_l6;
                            uint8_t t158 = gm_l1;
                            uint8_t t159 = ((t157 == t158));
                            if (t159) {
                                uint8_t t160 = gm_l2;
                                uint8_t t161 = (uint8_t)!t160;
                                if (t161) {
                                    void * t162 = gm_l0;
                                    void * t163 = gm_l0;
                                    uint64_t t164 = GM_LD(uint64_t, (((uint8_t *)t163) + 16));
                                    uint64_t r165; bool o166 = __builtin_add_overflow(t164, ((uint64_t)1ULL), &r165);
                                    if (o166) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 367, 17);
                                    uint64_t t167 = r165;
                                    GM_ST(uint64_t, (((uint8_t *)t162) + 16), t167);
                                    goto gm_lb3;
                                }
                                void * t168 = gm_l0;
                                void * t169 = GM_LD(void *, ((uint8_t *)t168));
                                uint64_t t170 = GM_LD(uint64_t, (((uint8_t *)t168) + 8));
                                void * t171 = gm_l0;
                                uint64_t t172 = GM_LD(uint64_t, (((uint8_t *)t171) + 16));
                                uint64_t t173 = (uintptr_t)t172;
                                uint64_t t174 = (uintptr_t)t170;
                                if (t173 > t174 || t174 > (uintptr_t)t170) gm_trap_slice(t173, t174, t170, gm_src3nt8pf0qk5gn, 370, 16);
                                uint64_t t175 = ((uintptr_t)t170) - (uintptr_t)t172;
                                gm_b16_8 t176;
                                GM_ST(void *, (uint8_t *)&t176, (void *)(((uint8_t *)t169 + (uintptr_t)t172 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t176 + 8, (uintptr_t)(t175));
                                uint8_t t177 = gm_l1;
                                uint8_t t178 = ((t177 == ((uint8_t)34ULL)));
                                if (t178) {
                                    gm_b16_8 t179;
                                    GM_ST(void *, (uint8_t *)&t179, (void *)(gm_s8rm42n03sfss));
                                    GM_ST(uintptr_t, (uint8_t *)&t179 + 8, (uintptr_t)(3));
                                    gm_l17 = t179;
                                } else {
                                    gm_b16_8 t180;
                                    GM_ST(void *, (uint8_t *)&t180, (void *)(gm_soakgb2h3g40i));
                                    GM_ST(uintptr_t, (uint8_t *)&t180 + 8, (uintptr_t)(3));
                                    gm_l17 = t180;
                                }
                                gm_b16_8 t181 = gm_l17;
                                uint8_t t182 = gm_f_starts_with_6vvt01e(t176, t181);
                                if (t182) {
                                    void * t183 = gm_l0;
                                    void * t184 = gm_l0;
                                    uint64_t t185 = GM_LD(uint64_t, (((uint8_t *)t184) + 16));
                                    uint64_t r186; bool o187 = __builtin_add_overflow(t185, ((uint64_t)3ULL), &r186);
                                    if (o187) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 371, 17);
                                    uint64_t t188 = r186;
                                    GM_ST(uint64_t, (((uint8_t *)t183) + 16), t188);
                                    gm_l7 = ((int32_t)0LL);
                                    {
                                        for (;;) {
                                            int32_t t189 = gm_l7;
                                            uint8_t t190 = (t189 < ((int32_t)2LL));
                                            if (!t190) {
                                                goto gm_lb6;
                                            }
                                            void * t191 = gm_l0;
                                            gm_tqs5p4n2tq19v t192 = gm_f_peek_bkdvl65(t191);
                                            uint8_t t193 = gm_l1;
                                            gm_tqs5p4n2tq19v t194;
                                            GM_ST(uint8_t, ((uint8_t *)&t194), t193);
                                            ((uint8_t *)&t194)[1] = 1;
                                            uint8_t t195 = (gm_eqb44cb3cf1n(((uint8_t *)&t192), ((uint8_t *)&t194)));
                                            if (!t195) {
                                                goto gm_lb6;
                                            }
                                            {
                                                void * t196 = (void *)((uint8_t *)&F.l5);
                                                uint8_t t197 = gm_l1;
                                                gm_tftb1odc739t5 t198 = gm_f_push_t83rg2m(t196, t197);
                                                gm_l18 = t198;
                                                uint8_t t199 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                                                if (!t199) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_src3nt8pf0qk5gn, 375, 21);
                                                void * t200 = gm_l0;
                                                void * t201 = gm_l0;
                                                uint64_t t202 = GM_LD(uint64_t, (((uint8_t *)t201) + 16));
                                                uint64_t r203; bool o204 = __builtin_add_overflow(t202, ((uint64_t)1ULL), &r203);
                                                if (o204) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 376, 21);
                                                uint64_t t205 = r203;
                                                GM_ST(uint64_t, (((uint8_t *)t200) + 16), t205);
                                                int32_t t206 = gm_l7;
                                                int32_t r207; bool o208 = __builtin_add_overflow(t206, ((int32_t)1LL), &r207);
                                                if (o208) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 377, 21);
                                                int32_t t209 = r207;
                                                gm_l7 = t209;
                                            }
gm_lb8:;
                                        }
                                    }
gm_lb6:;
                                    goto gm_lb3;
                                }
                                void * t210 = (void *)((uint8_t *)&F.l5);
                                uint8_t t211 = gm_l6;
                                gm_tftb1odc739t5 t212 = gm_f_push_t83rg2m(t210, t211);
                                gm_l19 = t212;
                                uint8_t t213 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                                if (!t213) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_src3nt8pf0qk5gn, 381, 13);
                                void * t214 = gm_l0;
                                void * t215 = gm_l0;
                                uint64_t t216 = GM_LD(uint64_t, (((uint8_t *)t215) + 16));
                                uint64_t r217; bool o218 = __builtin_add_overflow(t216, ((uint64_t)1ULL), &r217);
                                if (o218) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 382, 13);
                                uint64_t t219 = r217;
                                GM_ST(uint64_t, (((uint8_t *)t214) + 16), t219);
                                goto gm_lb5;
                            }
                        }
                        {
                            uint8_t t220 = gm_l6;
                            uint8_t t221 = ((t220 == ((uint8_t)10ULL)));
                            if (t221) {
                                uint8_t t222 = gm_l2;
                                uint8_t t223 = (uint8_t)!t222;
                                if (t223) {
                                    void * t224 = gm_l0;
                                    gm_b16_8 t225 = gm_l4;
                                    gm_b16_8 t226 = GM_LD(gm_b16_8, ((uint8_t *)&t225));
                                    gm_tob1snuii5bme t227;
                                    gm_f_error_96ci5lu((void *)&t227, t224, t226);
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 4, 0);
                                    ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out))[0] = 1;
                                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 8), t227);
                                    goto gm_fdone;
                                }
                                void * t228 = gm_l0;
                                void * t229 = gm_l0;
                                uint64_t t230 = GM_LD(uint64_t, (((uint8_t *)t229) + 24));
                                uint64_t r231; bool o232 = __builtin_add_overflow(t230, ((uint64_t)1ULL), &r231);
                                if (o232) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 387, 13);
                                uint64_t t233 = r231;
                                GM_ST(uint64_t, (((uint8_t *)t228) + 24), t233);
                            }
                            void * t234 = gm_l0;
                            void * t235 = gm_l0;
                            uint64_t t236 = GM_LD(uint64_t, (((uint8_t *)t235) + 16));
                            uint64_t r237; bool o238 = __builtin_add_overflow(t236, ((uint64_t)1ULL), &r237);
                            if (o238) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 389, 9);
                            uint64_t t239 = r237;
                            GM_ST(uint64_t, (((uint8_t *)t234) + 16), t239);
                            {
                                {
                                    uint8_t t240 = gm_l6;
                                    uint8_t t241 = ((t240 == ((uint8_t)92ULL)));
                                    if (!t241) {
                                        goto gm_lb10;
                                    }
                                    uint8_t t242 = gm_l1;
                                    uint8_t t243 = ((t242 == ((uint8_t)34ULL)));
                                    if (!t243) {
                                        goto gm_lb10;
                                    }
                                    void * t244 = gm_l0;
                                    gm_tqs5p4n2tq19v t245 = gm_f_peek_bkdvl65(t244);
                                    gm_l20 = t245;
                                    uint8_t t246 = (uint8_t)((((uint8_t *)&gm_l20))[1] != 0);
                                    if (t246) {
                                        uint8_t t247 = GM_LD(uint8_t, ((uint8_t *)&gm_l20));
                                        gm_l8 = t247;
                                    } else {
                                        void * t248 = gm_l0;
                                        gm_b16_8 t249 = gm_l4;
                                        gm_b16_8 t250 = GM_LD(gm_b16_8, ((uint8_t *)&t249));
                                        gm_tob1snuii5bme t251;
                                        gm_f_error_96ci5lu((void *)&t251, t248, t250);
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 4, 0);
                                        ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out))[0] = 1;
                                        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 8), t251);
                                        goto gm_fdone;
                                    }
                                    void * t252 = gm_l0;
                                    void * t253 = gm_l0;
                                    uint64_t t254 = GM_LD(uint64_t, (((uint8_t *)t253) + 16));
                                    uint64_t r255; bool o256 = __builtin_add_overflow(t254, ((uint64_t)1ULL), &r255);
                                    if (o256) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 392, 13);
                                    uint64_t t257 = r255;
                                    GM_ST(uint64_t, (((uint8_t *)t252) + 16), t257);
                                    {
                                        {
                                            uint8_t t258 = gm_l8;
                                            uint8_t t259 = ((t258 == ((uint8_t)98ULL)));
                                            if (!t259) {
                                                goto gm_lb12;
                                            }
                                            void * t260 = (void *)((uint8_t *)&F.l5);
                                            gm_tftb1odc739t5 t261 = gm_f_push_t83rg2m(t260, ((uint8_t)8ULL));
                                            gm_l21 = t261;
                                            uint8_t t262 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                                            if (!t262) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_src3nt8pf0qk5gn, 394, 24);
                                            goto gm_lb11;
                                        }
gm_lb12:;
                                        {
                                            uint8_t t263 = gm_l8;
                                            uint8_t t264 = ((t263 == ((uint8_t)101ULL)));
                                            if (!t264) {
                                                goto gm_lb13;
                                            }
                                            void * t265 = (void *)((uint8_t *)&F.l5);
                                            gm_tftb1odc739t5 t266 = gm_f_push_t83rg2m(t265, ((uint8_t)27ULL));
                                            gm_l22 = t266;
                                            uint8_t t267 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                                            if (!t267) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 1, gm_src3nt8pf0qk5gn, 395, 24);
                                            goto gm_lb11;
                                        }
gm_lb13:;
                                        {
                                            uint8_t t268 = gm_l8;
                                            uint8_t t269 = ((t268 == ((uint8_t)102ULL)));
                                            if (!t269) {
                                                goto gm_lb14;
                                            }
                                            void * t270 = (void *)((uint8_t *)&F.l5);
                                            gm_tftb1odc739t5 t271 = gm_f_push_t83rg2m(t270, ((uint8_t)12ULL));
                                            gm_l23 = t271;
                                            uint8_t t272 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                                            if (!t272) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_src3nt8pf0qk5gn, 396, 24);
                                            goto gm_lb11;
                                        }
gm_lb14:;
                                        {
                                            uint8_t t273 = gm_l8;
                                            uint8_t t274 = ((t273 == ((uint8_t)110ULL)));
                                            if (!t274) {
                                                goto gm_lb15;
                                            }
                                            void * t275 = (void *)((uint8_t *)&F.l5);
                                            gm_tftb1odc739t5 t276 = gm_f_push_t83rg2m(t275, ((uint8_t)10ULL));
                                            gm_l24 = t276;
                                            uint8_t t277 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                                            if (!t277) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_src3nt8pf0qk5gn, 397, 24);
                                            goto gm_lb11;
                                        }
gm_lb15:;
                                        {
                                            uint8_t t278 = gm_l8;
                                            uint8_t t279 = ((t278 == ((uint8_t)114ULL)));
                                            if (!t279) {
                                                goto gm_lb16;
                                            }
                                            void * t280 = (void *)((uint8_t *)&F.l5);
                                            gm_tftb1odc739t5 t281 = gm_f_push_t83rg2m(t280, ((uint8_t)13ULL));
                                            gm_l25 = t281;
                                            uint8_t t282 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                                            if (!t282) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_src3nt8pf0qk5gn, 398, 24);
                                            goto gm_lb11;
                                        }
gm_lb16:;
                                        {
                                            uint8_t t283 = gm_l8;
                                            uint8_t t284 = ((t283 == ((uint8_t)116ULL)));
                                            if (!t284) {
                                                goto gm_lb17;
                                            }
                                            void * t285 = (void *)((uint8_t *)&F.l5);
                                            gm_tftb1odc739t5 t286 = gm_f_push_t83rg2m(t285, ((uint8_t)9ULL));
                                            gm_l26 = t286;
                                            uint8_t t287 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                                            if (!t287) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_src3nt8pf0qk5gn, 399, 24);
                                            goto gm_lb11;
                                        }
gm_lb17:;
                                        {
                                            {
                                                {
                                                    uint8_t t288 = gm_l8;
                                                    uint8_t t289 = ((t288 == ((uint8_t)34ULL)));
                                                    if (!t289) {
                                                        goto gm_lb20;
                                                    }
                                                    goto gm_lb19;
                                                }
gm_lb20:;
                                                {
                                                    uint8_t t290 = gm_l8;
                                                    uint8_t t291 = ((t290 == ((uint8_t)92ULL)));
                                                    if (!t291) {
                                                        goto gm_lb21;
                                                    }
                                                    goto gm_lb19;
                                                }
gm_lb21:;
                                                goto gm_lb18;
                                            }
gm_lb19:;
                                            void * t292 = (void *)((uint8_t *)&F.l5);
                                            uint8_t t293 = gm_l8;
                                            gm_tftb1odc739t5 t294 = gm_f_push_t83rg2m(t292, t293);
                                            gm_l27 = t294;
                                            uint8_t t295 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                                            if (!t295) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 1, gm_src3nt8pf0qk5gn, 400, 30);
                                            goto gm_lb11;
                                        }
gm_lb18:;
                                        {
                                            uint8_t t296 = gm_l8;
                                            uint8_t t297 = ((t296 == ((uint8_t)120ULL)));
                                            if (!t297) {
                                                goto gm_lb22;
                                            }
                                            void * t298 = gm_l0;
                                            void * t299 = (void *)((uint8_t *)&F.l5);
                                            gm_f_hex_escape_eoc96dk((void *)&gm_l28, (void *)NULL, cx, NULL, t298, t299, ((uint64_t)2ULL));
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            uint8_t t300 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                                            if (t300) {
                                            } else {
                                                gm_tob1snuii5bme t301 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&gm_l28) + 8));
                                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 1, 0);
                                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 2, 0);
                                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 4, 0);
                                                ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out))[0] = 1;
                                                GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 8), t301);
                                                goto gm_fdone;
                                            }
                                            goto gm_lb11;
                                        }
gm_lb22:;
                                        {
                                            uint8_t t302 = gm_l8;
                                            uint8_t t303 = ((t302 == ((uint8_t)117ULL)));
                                            if (!t303) {
                                                goto gm_lb23;
                                            }
                                            void * t304 = gm_l0;
                                            void * t305 = (void *)((uint8_t *)&F.l5);
                                            gm_f_hex_escape_eoc96dk((void *)&gm_l29, (void *)NULL, cx, NULL, t304, t305, ((uint64_t)4ULL));
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            uint8_t t306 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                                            if (t306) {
                                            } else {
                                                gm_tob1snuii5bme t307 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&gm_l29) + 8));
                                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 1, 0);
                                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 2, 0);
                                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 4, 0);
                                                ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out))[0] = 1;
                                                GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 8), t307);
                                                goto gm_fdone;
                                            }
                                            goto gm_lb11;
                                        }
gm_lb23:;
                                        {
                                            uint8_t t308 = gm_l8;
                                            uint8_t t309 = ((t308 == ((uint8_t)85ULL)));
                                            if (!t309) {
                                                goto gm_lb24;
                                            }
                                            void * t310 = gm_l0;
                                            void * t311 = (void *)((uint8_t *)&F.l5);
                                            gm_f_hex_escape_eoc96dk((void *)&gm_l30, (void *)NULL, cx, NULL, t310, t311, ((uint64_t)8ULL));
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            uint8_t t312 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
                                            if (t312) {
                                            } else {
                                                gm_tob1snuii5bme t313 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&gm_l30) + 8));
                                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 1, 0);
                                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 2, 0);
                                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 4, 0);
                                                ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out))[0] = 1;
                                                GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 8), t313);
                                                goto gm_fdone;
                                            }
                                            goto gm_lb11;
                                        }
gm_lb24:;
                                        {
                                            {
                                                {
                                                    uint8_t t314 = gm_l8;
                                                    uint8_t t315 = ((t314 == ((uint8_t)32ULL)));
                                                    if (!t315) {
                                                        goto gm_lb27;
                                                    }
                                                    goto gm_lb26;
                                                }
gm_lb27:;
                                                {
                                                    uint8_t t316 = gm_l8;
                                                    uint8_t t317 = ((t316 == ((uint8_t)9ULL)));
                                                    if (!t317) {
                                                        goto gm_lb28;
                                                    }
                                                    goto gm_lb26;
                                                }
gm_lb28:;
                                                {
                                                    uint8_t t318 = gm_l8;
                                                    uint8_t t319 = ((t318 == ((uint8_t)13ULL)));
                                                    if (!t319) {
                                                        goto gm_lb29;
                                                    }
                                                    goto gm_lb26;
                                                }
gm_lb29:;
                                                {
                                                    uint8_t t320 = gm_l8;
                                                    uint8_t t321 = ((t320 == ((uint8_t)10ULL)));
                                                    if (!t321) {
                                                        goto gm_lb30;
                                                    }
                                                    goto gm_lb26;
                                                }
gm_lb30:;
                                                goto gm_lb25;
                                            }
gm_lb26:;
                                            uint8_t t322 = gm_l2;
                                            uint8_t t323 = (uint8_t)!t322;
                                            if (t323) {
                                                void * t324 = gm_l0;
                                                gm_b16_8 t325 = GM_LD(gm_b16_8, ((uint8_t *)gm_cn9bf8d1ktm));
                                                gm_tob1snuii5bme t326;
                                                gm_f_error_96ci5lu((void *)&t326, t324, t325);
                                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 1, 0);
                                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 2, 0);
                                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 4, 0);
                                                ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out))[0] = 1;
                                                GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 8), t326);
                                                goto gm_fdone;
                                            }
                                            void * t327 = gm_l0;
                                            void * t328 = gm_l0;
                                            uint64_t t329 = GM_LD(uint64_t, (((uint8_t *)t328) + 16));
                                            uint64_t r330; bool o331 = __builtin_sub_overflow(t329, ((uint64_t)1ULL), &r330);
                                            if (o331) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 407, 21);
                                            uint64_t t332 = r330;
                                            GM_ST(uint64_t, (((uint8_t *)t327) + 16), t332);
                                            gm_l9 = ((uint8_t)0);
                                            {
                                                for (;;) {
                                                    void * t333 = gm_l0;
                                                    gm_tqs5p4n2tq19v t334 = gm_f_peek_bkdvl65(t333);
                                                    gm_l31 = t334;
                                                    uint8_t t335 = (uint8_t)((((uint8_t *)&gm_l31))[1] != 0);
                                                    if (!t335) {
                                                        goto gm_lb31;
                                                    }
                                                    uint8_t t336 = GM_LD(uint8_t, ((uint8_t *)&gm_l31));
                                                    gm_l10 = t336;
                                                    {
                                                        uint8_t t337 = gm_l10;
                                                        uint8_t t338 = ((t337 == ((uint8_t)10ULL)));
                                                        if (t338) {
                                                            void * t339 = gm_l0;
                                                            void * t340 = gm_l0;
                                                            uint64_t t341 = GM_LD(uint64_t, (((uint8_t *)t340) + 24));
                                                            uint64_t r342; bool o343 = __builtin_add_overflow(t341, ((uint64_t)1ULL), &r342);
                                                            if (o343) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 411, 29);
                                                            uint64_t t344 = r342;
                                                            GM_ST(uint64_t, (((uint8_t *)t339) + 24), t344);
                                                            gm_l9 = ((uint8_t)1);
                                                        } else {
                                                            uint8_t t345 = gm_l10;
                                                            gm_toh0ootodhjue t346;
                                                            GM_ST(uint8_t, ((uint8_t *)&t346 + 0), ((uint8_t)32ULL));
                                                            GM_ST(uint8_t, ((uint8_t *)&t346 + 1), ((uint8_t)9ULL));
                                                            GM_ST(uint8_t, ((uint8_t *)&t346 + 2), ((uint8_t)13ULL));
                                                            uint8_t t347 = 0;
                                                            if (gm_eqjmccqdb90f(((uint8_t *)&t346) + 0, ((uint8_t *)&t345))) t347 = 1;
                                                            if (gm_eqjmccqdb90f(((uint8_t *)&t346) + 1, ((uint8_t *)&t345))) t347 = 1;
                                                            if (gm_eqjmccqdb90f(((uint8_t *)&t346) + 2, ((uint8_t *)&t345))) t347 = 1;
                                                            uint8_t t348 = (uint8_t)(!t347);
                                                            if (t348) {
                                                                goto gm_lb31;
                                                            }
                                                        }
                                                        void * t349 = gm_l0;
                                                        void * t350 = gm_l0;
                                                        uint64_t t351 = GM_LD(uint64_t, (((uint8_t *)t350) + 16));
                                                        uint64_t r352; bool o353 = __builtin_add_overflow(t351, ((uint64_t)1ULL), &r352);
                                                        if (o353) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 416, 25);
                                                        uint64_t t354 = r352;
                                                        GM_ST(uint64_t, (((uint8_t *)t349) + 16), t354);
                                                    }
gm_lb33:;
                                                }
                                            }
gm_lb31:;
                                            uint8_t t355 = gm_l9;
                                            uint8_t t356 = (uint8_t)!t355;
                                            if (t356) {
                                                void * t357 = gm_l0;
                                                gm_b16_8 t358 = GM_LD(gm_b16_8, ((uint8_t *)gm_cn9bf8d1ktm));
                                                gm_tob1snuii5bme t359;
                                                gm_f_error_96ci5lu((void *)&t359, t357, t358);
                                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 1, 0);
                                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 2, 0);
                                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 4, 0);
                                                ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out))[0] = 1;
                                                GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 8), t359);
                                                goto gm_fdone;
                                            }
                                            goto gm_lb11;
                                        }
gm_lb25:;
                                        {
                                            void * t360 = gm_l0;
                                            gm_b16_8 t361 = GM_LD(gm_b16_8, ((uint8_t *)gm_cn9bf8d1ktm));
                                            gm_tob1snuii5bme t362;
                                            gm_f_error_96ci5lu((void *)&t362, t360, t361);
                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 4, 0);
                                            ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out))[0] = 1;
                                            GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 8), t362);
                                            goto gm_fdone;
                                        }
gm_lb34:;
                                        gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 393, 13);
                                    }
gm_lb11:;
                                    goto gm_lb9;
                                }
gm_lb10:;
                                void * t363 = (void *)((uint8_t *)&F.l5);
                                uint8_t t364 = gm_l6;
                                gm_tftb1odc739t5 t365 = gm_f_push_t83rg2m(t363, t364);
                                gm_l32 = t365;
                                uint8_t t366 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                                if (!t366) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 1, gm_src3nt8pf0qk5gn, 423, 13);
                            }
gm_lb9:;
                        }
                    }
gm_lb5:;
                }
            }
        }
gm_lb3:;
    }
    {
        gm_b16_8 t367 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
        gm_b16_8 t368 = GM_LD(gm_b16_8, ((uint8_t *)&t367));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 8), t368);
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
inline __attribute__((aligned(4))) void gm_f_parse_toml_gjtmf79(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_parse_toml_gjtmf79 F;
    gm_b16_8 gm_l0; /* text */
    void * gm_l2; /* root */
    void * gm_l3; /* current */
    uint8_t gm_l4; /* c */
    uint64_t gm_l5; /* line */
    GM_STACK_CHECK(&F, 787u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tqs5p4n2tq19v gm_l8;
    gm_trssmtuv8jr0v gm_l9;
    gm_t21m1eu5ta01n gm_l10;
    gm_tefknldmnh2si gm_l11;
    gm_tr1k5nl69lhjo gm_l12;
    gm_tr1k5nl69lhjo gm_l13;
    {
        gm_b16_8 t5 = gm_l0;
        gm_t4kmic8080rrj t6;
        GM_ST(uint8_t, (uint8_t *)&t6 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t6 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t6 + 36, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t6 + 0), t5);
        GM_ST(uint64_t, ((uint8_t *)&t6 + 16), ((uint64_t)0ULL));
        GM_ST(uint64_t, ((uint8_t *)&t6 + 24), ((uint64_t)1ULL));
        GM_ST(uint8_t, ((uint8_t *)&t6 + 32), ((uint8_t)0));
        F.l1 = t6;
    }
    void * t7 = gm_f_new_table_hh6cki7((void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t7;
    void * t8 = gm_l2;
    gm_l3 = t8;
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb1;
            }
            {
                void * t9 = (void *)((uint8_t *)&F.l1);
                gm_f_skip_space_and_newlines_qagb1bk(t9);
                void * t10 = (void *)((uint8_t *)&F.l1);
                gm_tqs5p4n2tq19v t11 = gm_f_peek_bkdvl65(t10);
                gm_l8 = t11;
                uint8_t t12 = (uint8_t)((((uint8_t *)&gm_l8))[1] != 0);
                if (t12) {
                    uint8_t t13 = GM_LD(uint8_t, ((uint8_t *)&gm_l8));
                    gm_l4 = t13;
                } else {
                    goto gm_lb1;
                }
                uint8_t t14 = gm_l4;
                uint8_t t15 = ((t14 == ((uint8_t)91ULL)));
                if (t15) {
                    void * t16 = (void *)((uint8_t *)&F.l1);
                    void * t17 = gm_l2;
                    gm_f_table_header_j60mb6n((void *)&gm_l9, (void *)NULL, cx, NULL, t16, t17);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                    if (t18) {
                    } else {
                        gm_tob1snuii5bme t19 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&gm_l9) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out))[0] = 1;
                        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 8), t19);
                        goto gm_fdone;
                    }
                    void * t20 = GM_LD(void *, (((uint8_t *)&gm_l9) + 8));
                    gm_l3 = t20;
                } else {
                    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&F.l1) + 24));
                    gm_l5 = t21;
                    {
                        void * t22 = (void *)((uint8_t *)&F.l1);
                        gm_f_key_path_7spb050((void *)&gm_l10, (void *)NULL, cx, NULL, t22);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t23 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                    if (t23) {
                    } else {
                        gm_tob1snuii5bme t24 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&gm_l10) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out))[0] = 1;
                        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 8), t24);
                        goto gm_fdone;
                    }
                    {
                        memmove(((uint8_t *)&F.l6), (((uint8_t *)&gm_l10) + 8), 40);
                    }
                    void * t25 = (void *)((uint8_t *)&F.l1);
                    gm_f_skip_space_voq272o(t25);
                    void * t26 = (void *)((uint8_t *)&F.l1);
                    gm_tqs5p4n2tq19v t27 = gm_f_peek_bkdvl65(t26);
                    gm_tqs5p4n2tq19v t28;
                    GM_ST(uint8_t, ((uint8_t *)&t28), ((uint8_t)61ULL));
                    ((uint8_t *)&t28)[1] = 1;
                    uint8_t t29 = (!gm_eqb44cb3cf1n(((uint8_t *)&t27), ((uint8_t *)&t28)));
                    if (t29) {
                        void * t30 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t31;
                        GM_ST(void *, (uint8_t *)&t31, (void *)(gm_spl83f693c752));
                        GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(31));
                        gm_tob1snuii5bme t32;
                        gm_f_error_96ci5lu((void *)&t32, t30, t31);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out))[0] = 1;
                        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 8), t32);
                        goto gm_fdone;
                    }
                    uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&F.l1) + 16));
                    uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
                    if (o35) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 85, 13);
                    uint64_t t36 = r34;
                    GM_ST(uint64_t, (((uint8_t *)&F.l1) + 16), t36);
                    void * t37 = (void *)((uint8_t *)&F.l1);
                    gm_f_skip_space_voq272o(t37);
                    {
                        void * t38 = (void *)((uint8_t *)&F.l1);
                        gm_f_value_oq952o2((void *)&gm_l11, (void *)NULL, cx, NULL, t38);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t39 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                    if (t39) {
                    } else {
                        gm_tob1snuii5bme t40 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&gm_l11) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out))[0] = 1;
                        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 8), t40);
                        goto gm_fdone;
                    }
                    {
                        memmove(((uint8_t *)&F.l7), (((uint8_t *)&gm_l11) + 8), 48);
                    }
                    {
                        void * t41 = gm_l3;
                        gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
                        gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
                        uint64_t t44 = gm_l5;
                        gm_tktr4rmbindgv t45 = GM_LD(gm_tktr4rmbindgv, ((uint8_t *)&F.l7));
                        gm_f_insert_path_cvuqfkc((void *)&gm_l12, (void *)NULL, cx, NULL, t41, t43, &t45, t44);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t46 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                    if (t46) {
                    } else {
                        gm_tob1snuii5bme t47 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&gm_l12) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out))[0] = 1;
                        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 8), t47);
                        goto gm_fdone;
                    }
                }
                void * t48 = (void *)((uint8_t *)&F.l1);
                gm_f_end_of_line_056kej4((void *)&gm_l13, t48);
                uint8_t t49 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                if (t49) {
                } else {
                    gm_tob1snuii5bme t50 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&gm_l13) + 8));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out))[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 8), t50);
                    goto gm_fdone;
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    {
        void * t51 = gm_l2;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out))[0] = 0;
        GM_ST(void *, ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 8), t51);
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
extern __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_bkdvl65(void * a0);
inline __attribute__((aligned(4))) void gm_f_skip_space_and_newlines_qagb1bk(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_skip_space_and_newlines_qagb1bk F;
    void * gm_l0; /* r */
    uint8_t gm_l1; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 189u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tqs5p4n2tq19v gm_l2;
    uint8_t gm_l3;
    gm_tqs5p4n2tq19v gm_l4;
    {
        for (;;) {
            void * t11 = gm_l0;
            gm_tqs5p4n2tq19v t12 = gm_f_peek_bkdvl65(t11);
            gm_l2 = t12;
            uint8_t t13 = (uint8_t)((((uint8_t *)&gm_l2))[1] != 0);
            if (!t13) {
                goto gm_lb1;
            }
            uint8_t t14 = GM_LD(uint8_t, ((uint8_t *)&gm_l2));
            gm_l1 = t14;
            {
                uint8_t t15 = gm_l1;
                gm_toh0ootodhjue t16;
                GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)32ULL));
                GM_ST(uint8_t, ((uint8_t *)&t16 + 1), ((uint8_t)9ULL));
                GM_ST(uint8_t, ((uint8_t *)&t16 + 2), ((uint8_t)13ULL));
                uint8_t t17 = 0;
                if (gm_eqjmccqdb90f(((uint8_t *)&t16) + 0, ((uint8_t *)&t15))) t17 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t16) + 1, ((uint8_t *)&t15))) t17 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t16) + 2, ((uint8_t *)&t15))) t17 = 1;
                uint8_t t18 = (uint8_t)(t17);
                if (t18) {
                    void * t19 = gm_l0;
                    void * t20 = gm_l0;
                    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)t20) + 16));
                    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
                    if (o23) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 118, 13);
                    uint64_t t24 = r22;
                    GM_ST(uint64_t, (((uint8_t *)t19) + 16), t24);
                } else {
                    uint8_t t25 = gm_l1;
                    uint8_t t26 = ((t25 == ((uint8_t)10ULL)));
                    if (t26) {
                        void * t27 = gm_l0;
                        void * t28 = gm_l0;
                        uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)t28) + 16));
                        uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                        if (o31) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 120, 13);
                        uint64_t t32 = r30;
                        GM_ST(uint64_t, (((uint8_t *)t27) + 16), t32);
                        void * t33 = gm_l0;
                        void * t34 = gm_l0;
                        uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)t34) + 24));
                        uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                        if (o37) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 121, 13);
                        uint64_t t38 = r36;
                        GM_ST(uint64_t, (((uint8_t *)t33) + 24), t38);
                    } else {
                        uint8_t t39 = gm_l1;
                        uint8_t t40 = ((t39 == ((uint8_t)35ULL)));
                        if (t40) {
                            {
                                for (;;) {
                                    void * t41 = gm_l0;
                                    gm_tqs5p4n2tq19v t42 = gm_f_peek_bkdvl65(t41);
                                    gm_l4 = t42;
                                    {
                                        {
                                            uint8_t t43 = (uint8_t)((((uint8_t *)&gm_l4))[1] != 0);
                                            if (t43) {
                                                goto gm_lb8;
                                            }
                                            gm_l3 = ((uint8_t)0);
                                            goto gm_lb7;
                                        }
gm_lb8:;
                                        {
                                            gm_l3 = ((uint8_t)1);
                                            goto gm_lb7;
                                        }
gm_lb9:;
                                        gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 123, 19);
                                    }
gm_lb7:;
                                    uint8_t t44 = gm_l3;
                                    if (!t44) {
                                        goto gm_lb4;
                                    }
                                    void * t45 = gm_l0;
                                    gm_tqs5p4n2tq19v t46 = gm_f_peek_bkdvl65(t45);
                                    gm_tqs5p4n2tq19v t47;
                                    GM_ST(uint8_t, ((uint8_t *)&t47), ((uint8_t)10ULL));
                                    ((uint8_t *)&t47)[1] = 1;
                                    uint8_t t48 = (!gm_eqb44cb3cf1n(((uint8_t *)&t46), ((uint8_t *)&t47)));
                                    if (!t48) {
                                        goto gm_lb4;
                                    }
                                    {
                                        void * t49 = gm_l0;
                                        void * t50 = gm_l0;
                                        uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)t50) + 16));
                                        uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
                                        if (o53) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 123, 67);
                                        uint64_t t54 = r52;
                                        GM_ST(uint64_t, (((uint8_t *)t49) + 16), t54);
                                    }
gm_lb6:;
                                }
                            }
gm_lb4:;
                        } else {
                            F.ret = GM_ZERO(gm_b0_1);
                            goto gm_fret;
                        }
                    }
                }
            }
gm_lb3:;
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
inline __attribute__((aligned(4))) void gm_f_skip_space_voq272o(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_skip_space_voq272o F;
    void * gm_l0; /* r */
    GM_STACK_CHECK_SHALLOW(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        for (;;) {
            void * t5 = gm_l0;
            gm_tqs5p4n2tq19v t6 = gm_f_peek_bkdvl65(t5);
            gm_tqs5p4n2tq19v t7;
            GM_ST(uint8_t, ((uint8_t *)&t7), ((uint8_t)32ULL));
            ((uint8_t *)&t7)[1] = 1;
            gm_tqs5p4n2tq19v t8;
            GM_ST(uint8_t, ((uint8_t *)&t8), ((uint8_t)9ULL));
            ((uint8_t *)&t8)[1] = 1;
            gm_t5r6k0fdcfsb3 t9;
            GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t9 + 0), t7);
            GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t9 + 2), t8);
            uint8_t t10 = 0;
            if (gm_eqb44cb3cf1n(((uint8_t *)&t9) + 0, ((uint8_t *)&t6))) t10 = 1;
            if (gm_eqb44cb3cf1n(((uint8_t *)&t9) + 2, ((uint8_t *)&t6))) t10 = 1;
            uint8_t t11 = (uint8_t)(t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                void * t12 = gm_l0;
                void * t13 = gm_l0;
                uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 16));
                uint64_t r15; bool o16 = __builtin_add_overflow(t14, ((uint64_t)1ULL), &r15);
                if (o16) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 111, 37);
                uint64_t t17 = r15;
                GM_ST(uint64_t, (((uint8_t *)t12) + 16), t17);
            }
gm_lb3:;
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
__attribute__((aligned(4))) void gm_f_table_header_j60mb6n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_table_header_j60mb6n F;
    void * gm_l0; /* r */
    void * gm_l1; /* root */
    uint8_t gm_l2; /* array */
    uint64_t gm_l3; /* line */
    gm_b16_8 gm_l5; /* names */
    void * gm_l6; /* t */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    gm_b16_8 gm_l10; /* name */
    uint8_t gm_l11; /* last */
    void * gm_l12; /* e */
    void * gm_l14; /* fresh */
    void * gm_l16; /* sub */
    void * gm_l18; /* sub */
    void * gm_l19; /* fresh */
    GM_STACK_CHECK(&F, 1944u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t21m1eu5ta01n gm_l21;
    gm_b16_8 gm_l22;
    gm_tf1bjg81u0glv gm_l23;
    void * gm_l24;
    gm_tftb1odc739t5 gm_l25;
    gm_tftb1odc739t5 gm_l26;
    gm_tftb1odc739t5 gm_l27;
    gm_tftb1odc739t5 gm_l28;
    {
        void * t25 = gm_l0;
        void * t26 = gm_l0;
        uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)t26) + 16));
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        if (o29) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 150, 5);
        uint64_t t30 = r28;
        GM_ST(uint64_t, (((uint8_t *)t25) + 16), t30);
        void * t31 = gm_l0;
        gm_tqs5p4n2tq19v t32 = gm_f_peek_bkdvl65(t31);
        gm_tqs5p4n2tq19v t33;
        GM_ST(uint8_t, ((uint8_t *)&t33), ((uint8_t)91ULL));
        ((uint8_t *)&t33)[1] = 1;
        uint8_t t34 = (gm_eqb44cb3cf1n(((uint8_t *)&t32), ((uint8_t *)&t33)));
        gm_l2 = t34;
        uint8_t t35 = gm_l2;
        if (t35) {
            void * t36 = gm_l0;
            void * t37 = gm_l0;
            uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)t37) + 16));
            uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
            if (o40) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 152, 16);
            uint64_t t41 = r39;
            GM_ST(uint64_t, (((uint8_t *)t36) + 16), t41);
        }
        void * t42 = gm_l0;
        gm_f_skip_space_voq272o(t42);
        void * t43 = gm_l0;
        uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)t43) + 24));
        gm_l3 = t44;
        void * t45 = gm_l0;
        gm_f_key_path_7spb050((void *)&gm_l21, (void *)NULL, cx, NULL, t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t46 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
        if (t46) {
        } else {
            gm_tob1snuii5bme t47 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&gm_l21) + 8));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out))[0] = 1;
            GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 8), t47);
            goto gm_fdone;
        }
        memmove(((uint8_t *)&F.l4), (((uint8_t *)&gm_l21) + 8), 40);
        void * t48 = gm_l0;
        gm_f_skip_space_voq272o(t48);
        void * t49 = gm_l0;
        gm_tqs5p4n2tq19v t50 = gm_f_peek_bkdvl65(t49);
        gm_tqs5p4n2tq19v t51;
        GM_ST(uint8_t, ((uint8_t *)&t51), ((uint8_t)93ULL));
        ((uint8_t *)&t51)[1] = 1;
        uint8_t t52 = (!gm_eqb44cb3cf1n(((uint8_t *)&t50), ((uint8_t *)&t51)));
        if (t52) {
            void * t53 = gm_l0;
            uint8_t t54 = gm_l2;
            if (t54) {
                gm_b16_8 t55;
                GM_ST(void *, (uint8_t *)&t55, (void *)(gm_sdq87al6fhs9i));
                GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(34));
                gm_l22 = t55;
            } else {
                gm_b16_8 t56;
                GM_ST(void *, (uint8_t *)&t56, (void *)(gm_std877jr2ed05));
                GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(28));
                gm_l22 = t56;
            }
            gm_b16_8 t57 = gm_l22;
            gm_tob1snuii5bme t58;
            gm_f_error_96ci5lu((void *)&t58, t53, t57);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out))[0] = 1;
            GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 8), t58);
            goto gm_fdone;
        }
        void * t59 = gm_l0;
        void * t60 = gm_l0;
        uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)t60) + 16));
        uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
        if (o63) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 158, 5);
        uint64_t t64 = r62;
        GM_ST(uint64_t, (((uint8_t *)t59) + 16), t64);
        uint8_t t65 = gm_l2;
        if (t65) {
            void * t66 = gm_l0;
            gm_tqs5p4n2tq19v t67 = gm_f_peek_bkdvl65(t66);
            gm_tqs5p4n2tq19v t68;
            GM_ST(uint8_t, ((uint8_t *)&t68), ((uint8_t)93ULL));
            ((uint8_t *)&t68)[1] = 1;
            uint8_t t69 = (!gm_eqb44cb3cf1n(((uint8_t *)&t67), ((uint8_t *)&t68)));
            if (t69) {
                void * t70 = gm_l0;
                gm_b16_8 t71;
                GM_ST(void *, (uint8_t *)&t71, (void *)(gm_sdq87al6fhs9i));
                GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(34));
                gm_tob1snuii5bme t72;
                gm_f_error_96ci5lu((void *)&t72, t70, t71);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out))[0] = 1;
                GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 8), t72);
                goto gm_fdone;
            }
            void * t73 = gm_l0;
            void * t74 = gm_l0;
            uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)t74) + 16));
            uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)1ULL), &r76);
            if (o77) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 161, 9);
            uint64_t t78 = r76;
            GM_ST(uint64_t, (((uint8_t *)t73) + 16), t78);
        }
    }
    {
        gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
        gm_l5 = t79;
        void * t80 = gm_l1;
        gm_l6 = t80;
        gm_b16_8 t81 = gm_l5;
        gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&t81));
        gm_l7 = t82;
        gm_l8 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t83 = gm_l8;
                uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint8_t t85 = (t83 < t84);
                if (!t85) {
                    goto gm_lb1;
                }
                {
                    uint64_t t86 = gm_l8;
                    void * t87 = GM_LD(void *, ((uint8_t *)&gm_l7));
                    uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    uint64_t t89 = gm_l8;
                    gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)t87) + (uintptr_t)t89 * 16));
                    GM_ST(uint64_t, ((uint8_t *)&gm_l23 + 0), t86);
                    GM_ST(gm_b16_8, ((uint8_t *)&gm_l23 + 8), t90);
                    {
                        {
                            uint64_t t91 = GM_LD(uint64_t, ((uint8_t *)&gm_l23));
                            gm_l9 = t91;
                            gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 8));
                            gm_l10 = t92;
                            uint64_t t93 = gm_l9;
                            uint64_t t94 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                            uint64_t r95; bool o96 = __builtin_sub_overflow(t94, ((uint64_t)1ULL), &r95);
                            if (o96) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 166, 22);
                            uint64_t t97 = r95;
                            uint8_t t98 = ((t93 == t97));
                            gm_l11 = t98;
                            {
                                {
                                    void * t99 = gm_l6;
                                    gm_b16_8 t100 = gm_l10;
                                    void * t101 = gm_f_get_9rme0gb(t99, t100);
                                    gm_l24 = t101;
                                    uint8_t t102 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l24)) != 0);
                                    if (!t102) {
                                        goto gm_lb7;
                                    }
                                    void * t103 = GM_LD(void *, ((uint8_t *)&gm_l24));
                                    gm_l12 = t103;
                                    {
                                        {
                                            uint8_t t104 = gm_l11;
                                            if (!t104) {
                                                goto gm_lb9;
                                            }
                                            uint8_t t105 = gm_l2;
                                            if (!t105) {
                                                goto gm_lb9;
                                            }
                                            {
                                                {
                                                    {
                                                        void * t106 = gm_l12;
                                                        uint8_t t107 = GM_LD(uint8_t, ((((uint8_t *)t106) + 16) + 0));
                                                        uint8_t t108 = (uint8_t)(t107 == ((uint8_t)3ULL));
                                                        if (!t108) {
                                                            goto gm_lb11;
                                                        }
                                                        memmove(((uint8_t *)&F.l13), ((((uint8_t *)t106) + 16) + 8), 40);
                                                    }
                                                    void * t109 = gm_f_new_table_hh6cki7((void *)NULL, cx, NULL);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_l14 = t109;
                                                    {
                                                        memmove(((uint8_t *)&F.l15), ((uint8_t *)&F.l13), 40);
                                                    }
                                                    {
                                                        void * t110 = (void *)((uint8_t *)&F.l15);
                                                        void * t111 = gm_l14;
                                                        gm_tktr4rmbindgv t112;
                                                        memset(&t112, 0, sizeof t112);
                                                        GM_ST(uint8_t, ((uint8_t *)&t112 + 0), ((uint8_t)4ULL));
                                                        GM_ST(void *, ((uint8_t *)&t112 + 8), t111);
                                                        gm_tftb1odc739t5 t113 = gm_f_push_0pgacsj_3(t110, &t112);
                                                        gm_l25 = t113;
                                                    }
                                                    uint8_t t114 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                                                    if (!t114) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_src3nt8pf0qk5gn, 172, 17);
                                                    {
                                                        void * t115 = gm_l12;
                                                        gm_trqfvmjtt4sij t116 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l15));
                                                        gm_tktr4rmbindgv t117;
                                                        GM_ST(uint8_t, (uint8_t *)&t117 + 1, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t117 + 2, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&t117 + 4, 0);
                                                        GM_ST(uint8_t, ((uint8_t *)&t117 + 0), ((uint8_t)3ULL));
                                                        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t117 + 8), t116);
                                                        GM_ST(gm_tktr4rmbindgv, (((uint8_t *)t115) + 16), t117);
                                                    }
                                                    {
                                                        void * t118 = gm_l14;
                                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 1, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 2, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 4, 0);
                                                        GM_ST(uint64_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 16, 0);
                                                        GM_ST(uint64_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 24, 0);
                                                        GM_ST(uint64_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 32, 0);
                                                        ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out))[0] = 0;
                                                        GM_ST(void *, ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 8), t118);
                                                        goto gm_fdone;
                                                    }
                                                }
gm_lb11:;
                                                {
                                                    void * t119 = gm_l0;
                                                    gm_b16_8 t120;
                                                    GM_ST(void *, (uint8_t *)&t120, (void *)(gm_skiffh4urhl7e));
                                                    GM_ST(uintptr_t, (uint8_t *)&t120 + 8, (uintptr_t)(13));
                                                    gm_tob1snuii5bme t121;
                                                    gm_f_error_96ci5lu((void *)&t121, t119, t120);
                                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 4, 0);
                                                    ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out))[0] = 1;
                                                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 8), t121);
                                                    goto gm_fdone;
                                                }
                                            }
gm_lb10:;
                                        }
gm_lb9:;
                                    }
gm_lb8:;
                                    void * t122 = gm_l12;
                                    {
                                        {
                                            uint8_t t123 = GM_LD(uint8_t, ((((uint8_t *)t122) + 16) + 0));
                                            uint8_t t124 = (uint8_t)(t123 == ((uint8_t)4ULL));
                                            if (!t124) {
                                                goto gm_lb13;
                                            }
                                            void * t125 = GM_LD(void *, ((((uint8_t *)t122) + 16) + 8));
                                            gm_l16 = t125;
                                            {
                                                {
                                                    uint8_t t126 = gm_l11;
                                                    if (!t126) {
                                                        goto gm_lb15;
                                                    }
                                                    uint8_t t127 = gm_l2;
                                                    uint8_t t128 = (uint8_t)!t127;
                                                    if (!t128) {
                                                        goto gm_lb15;
                                                    }
                                                    void * t129 = gm_l16;
                                                    uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)t129) + 40));
                                                    if (!t130) {
                                                        goto gm_lb15;
                                                    }
                                                    {
                                                        void * t131 = gm_l0;
                                                        gm_b16_8 t132;
                                                        GM_ST(void *, (uint8_t *)&t132, (void *)(gm_skiffh4urhl7e));
                                                        GM_ST(uintptr_t, (uint8_t *)&t132 + 8, (uintptr_t)(13));
                                                        gm_tob1snuii5bme t133;
                                                        gm_f_error_96ci5lu((void *)&t133, t131, t132);
                                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 1, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 2, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 4, 0);
                                                        ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out))[0] = 1;
                                                        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 8), t133);
                                                        goto gm_fdone;
                                                    }
                                                }
gm_lb15:;
                                            }
gm_lb14:;
                                            void * t134 = gm_l16;
                                            gm_l6 = t134;
                                            goto gm_lb12;
                                        }
gm_lb13:;
                                        {
                                            uint8_t t135 = GM_LD(uint8_t, ((((uint8_t *)t122) + 16) + 0));
                                            uint8_t t136 = (uint8_t)(t135 == ((uint8_t)3ULL));
                                            if (!t136) {
                                                goto gm_lb16;
                                            }
                                            memmove(((uint8_t *)&F.l17), ((((uint8_t *)t122) + 16) + 8), 40);
                                            uint64_t t137 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 8));
                                            uint8_t t138 = (t137 > ((uint64_t)0ULL));
                                            if (!t138) {
                                                goto gm_lb16;
                                            }
                                            {
                                                {
                                                    uint8_t t139 = gm_l11;
                                                    if (!t139) {
                                                        goto gm_lb18;
                                                    }
                                                    uint8_t t140 = gm_l2;
                                                    uint8_t t141 = (uint8_t)!t140;
                                                    if (!t141) {
                                                        goto gm_lb18;
                                                    }
                                                    {
                                                        void * t142 = gm_l0;
                                                        gm_b16_8 t143;
                                                        GM_ST(void *, (uint8_t *)&t143, (void *)(gm_skiffh4urhl7e));
                                                        GM_ST(uintptr_t, (uint8_t *)&t143 + 8, (uintptr_t)(13));
                                                        gm_tob1snuii5bme t144;
                                                        gm_f_error_96ci5lu((void *)&t144, t142, t143);
                                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 1, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 2, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 4, 0);
                                                        ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out))[0] = 1;
                                                        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 8), t144);
                                                        goto gm_fdone;
                                                    }
                                                }
gm_lb18:;
                                            }
gm_lb17:;
                                            {
                                                {
                                                    void * t145 = GM_LD(void *, ((uint8_t *)&F.l17));
                                                    uint64_t t146 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 8));
                                                    uint64_t t147 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 8));
                                                    uint64_t r148; bool o149 = __builtin_sub_overflow(t147, ((uint64_t)1ULL), &r148);
                                                    if (o149) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 183, 48);
                                                    uint64_t t150 = r148;
                                                    if ((uintptr_t)t150 >= (uintptr_t)(t146)) gm_trap_index((uintptr_t)t150, t146, gm_src3nt8pf0qk5gn, 183, 36);
                                                    uint8_t t151 = GM_LD(uint8_t, ((((uint8_t *)t145) + (uintptr_t)t150 * 48) + 0));
                                                    uint8_t t152 = (uint8_t)(t151 == ((uint8_t)4ULL));
                                                    if (!t152) {
                                                        goto gm_lb20;
                                                    }
                                                    void * t153 = GM_LD(void *, ((((uint8_t *)t145) + (uintptr_t)t150 * 48) + 8));
                                                    gm_l18 = t153;
                                                    void * t154 = gm_l18;
                                                    gm_l6 = t154;
                                                    goto gm_lb19;
                                                }
gm_lb20:;
                                                {
                                                    void * t155 = gm_l0;
                                                    gm_b16_8 t156;
                                                    GM_ST(void *, (uint8_t *)&t156, (void *)(gm_skiffh4urhl7e));
                                                    GM_ST(uintptr_t, (uint8_t *)&t156 + 8, (uintptr_t)(13));
                                                    gm_tob1snuii5bme t157;
                                                    gm_f_error_96ci5lu((void *)&t157, t155, t156);
                                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 4, 0);
                                                    ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out))[0] = 1;
                                                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 8), t157);
                                                    goto gm_fdone;
                                                }
                                            }
gm_lb19:;
                                            goto gm_lb12;
                                        }
gm_lb16:;
                                        {
                                            void * t158 = gm_l0;
                                            gm_b16_8 t159;
                                            GM_ST(void *, (uint8_t *)&t159, (void *)(gm_skiffh4urhl7e));
                                            GM_ST(uintptr_t, (uint8_t *)&t159 + 8, (uintptr_t)(13));
                                            gm_tob1snuii5bme t160;
                                            gm_f_error_96ci5lu((void *)&t160, t158, t159);
                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 4, 0);
                                            ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out))[0] = 1;
                                            GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 8), t160);
                                            goto gm_fdone;
                                        }
gm_lb21:;
                                        gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 176, 13);
                                    }
gm_lb12:;
                                    goto gm_lb6;
                                }
gm_lb7:;
                                void * t161 = gm_f_new_table_hh6cki7((void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l19 = t161;
                                {
                                    {
                                        uint8_t t162 = gm_l11;
                                        if (!t162) {
                                            goto gm_lb23;
                                        }
                                        uint8_t t163 = gm_l2;
                                        if (!t163) {
                                            goto gm_lb23;
                                        }
                                        {
                                            gm_trqfvmjtt4sij t164;
                                            gm_f_list_0202sdh_3((void *)&t164, (void *)NULL, cx, NULL);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            F.l20 = t164;
                                        }
                                        {
                                            void * t165 = (void *)((uint8_t *)&F.l20);
                                            void * t166 = gm_l19;
                                            gm_tktr4rmbindgv t167;
                                            memset(&t167, 0, sizeof t167);
                                            GM_ST(uint8_t, ((uint8_t *)&t167 + 0), ((uint8_t)4ULL));
                                            GM_ST(void *, ((uint8_t *)&t167 + 8), t166);
                                            gm_tftb1odc739t5 t168 = gm_f_push_0pgacsj_3(t165, &t167);
                                            gm_l26 = t168;
                                        }
                                        uint8_t t169 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                                        if (!t169) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_src3nt8pf0qk5gn, 192, 17);
                                        {
                                            void * t170 = gm_l6;
                                            void * t171 = (void *)((uint8_t *)t170);
                                            gm_b16_8 t172 = gm_l10;
                                            gm_trqfvmjtt4sij t173 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l20));
                                            uint64_t t174 = gm_l3;
                                            gm_tktr4rmbindgv t175;
                                            GM_ST(uint8_t, (uint8_t *)&t175 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t175 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t175 + 4, 0);
                                            GM_ST(uint8_t, ((uint8_t *)&t175 + 0), ((uint8_t)3ULL));
                                            GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t175 + 8), t173);
                                            gm_trc3jdv2g5qre t176;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t176 + 0), t172);
                                            GM_ST(gm_tktr4rmbindgv, ((uint8_t *)&t176 + 16), t175);
                                            GM_ST(uint64_t, ((uint8_t *)&t176 + 64), t174);
                                            gm_tftb1odc739t5 t177 = gm_f_push_2pp1ri6(t171, &t176);
                                            gm_l27 = t177;
                                        }
                                        uint8_t t178 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                                        if (!t178) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 1, gm_src3nt8pf0qk5gn, 193, 17);
                                        goto gm_lb22;
                                    }
gm_lb23:;
                                    {
                                        void * t179 = gm_l6;
                                        void * t180 = (void *)((uint8_t *)t179);
                                        gm_b16_8 t181 = gm_l10;
                                        void * t182 = gm_l19;
                                        uint64_t t183 = gm_l3;
                                        gm_tktr4rmbindgv t184;
                                        memset(&t184, 0, sizeof t184);
                                        GM_ST(uint8_t, ((uint8_t *)&t184 + 0), ((uint8_t)4ULL));
                                        GM_ST(void *, ((uint8_t *)&t184 + 8), t182);
                                        gm_trc3jdv2g5qre t185;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t185 + 0), t181);
                                        GM_ST(gm_tktr4rmbindgv, ((uint8_t *)&t185 + 16), t184);
                                        GM_ST(uint64_t, ((uint8_t *)&t185 + 64), t183);
                                        gm_tftb1odc739t5 t186 = gm_f_push_2pp1ri6(t180, &t185);
                                        gm_l28 = t186;
                                    }
                                    uint8_t t187 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                                    if (!t187) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 1, gm_src3nt8pf0qk5gn, 195, 17);
                                }
gm_lb22:;
                                void * t188 = gm_l19;
                                gm_l6 = t188;
                            }
gm_lb6:;
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 165, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t189 = gm_l8;
                uint64_t r190; bool o191 = __builtin_add_overflow(t189, ((uint64_t)1ULL), &r190);
                uint64_t t192 = r190;
                gm_l8 = t192;
            }
        }
gm_lb1:;
    }
    {
        void * t193 = gm_l6;
        GM_ST(uint8_t, (((uint8_t *)t193) + 40), ((uint8_t)1));
        void * t194 = gm_l6;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out))[0] = 0;
        GM_ST(void *, ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 8), t194);
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
__attribute__((aligned(4))) void gm_f_value_oq952o2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_value_oq952o2 F;
    void * gm_l0; /* r */
    uint8_t gm_l1; /* c */
    gm_b16_8 gm_l2; /* rest */
    GM_STACK_CHECK(&F, 1241u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tqs5p4n2tq19v gm_l3;
    gm_tu615l4qlr8b2 gm_l4;
    uint8_t gm_l5;
    void * t22 = gm_l0;
    GM_ST(uint8_t, (((uint8_t *)t22) + 32), ((uint8_t)0));
    void * t23 = gm_l0;
    gm_tqs5p4n2tq19v t24 = gm_f_peek_bkdvl65(t23);
    gm_l3 = t24;
    uint8_t t25 = (uint8_t)((((uint8_t *)&gm_l3))[1] != 0);
    if (t25) {
        uint8_t t26 = GM_LD(uint8_t, ((uint8_t *)&gm_l3));
        gm_l1 = t26;
    } else {
        void * t27 = gm_l0;
        gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)gm_cal7njs7es4));
        gm_tob1snuii5bme t29;
        gm_f_error_96ci5lu((void *)&t29, t27, t28);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
        ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t29);
        goto gm_fdone;
    }
    {
        {
            {
                {
                    uint8_t t30 = gm_l1;
                    uint8_t t31 = ((t30 == ((uint8_t)34ULL)));
                    if (!t31) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t32 = gm_l1;
                    uint8_t t33 = ((t32 == ((uint8_t)39ULL)));
                    if (!t33) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                goto gm_lb2;
            }
gm_lb3:;
            {
                void * t34 = gm_l0;
                gm_f_parse_string_80niubc((void *)&gm_l4, (void *)NULL, cx, NULL, t34);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t35 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
            if (t35) {
            } else {
                gm_tob1snuii5bme t36 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&gm_l4) + 8));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t36);
                goto gm_fdone;
            }
            {
                gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
                gm_tktr4rmbindgv t38;
                GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t38 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t38 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t38 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t38 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t38 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)0ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t38 + 8), t37);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 0;
                GM_ST(gm_tktr4rmbindgv, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t38);
                goto gm_fdone;
            }
        }
gm_lb2:;
        {
            uint8_t t39 = gm_l1;
            uint8_t t40 = ((t39 == ((uint8_t)91ULL)));
            if (!t40) {
                goto gm_lb6;
            }
            void * t41 = gm_l0;
            gm_f_array_canpt3e((void *)&(*(gm_tefknldmnh2si *)gm_out), (void *)NULL, cx, NULL, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb6:;
        {
            uint8_t t42 = gm_l1;
            uint8_t t43 = ((t42 == ((uint8_t)123ULL)));
            if (!t43) {
                goto gm_lb7;
            }
            void * t44 = gm_l0;
            gm_f_inline_table_cvvpgel((void *)&(*(gm_tefknldmnh2si *)gm_out), (void *)NULL, cx, NULL, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb7:;
        {
            uint8_t t45 = gm_l1;
            uint8_t t46 = ((t45 == ((uint8_t)116ULL)));
            if (!t46) {
                goto gm_lb8;
            }
            void * t47 = gm_l0;
            void * t48 = GM_LD(void *, ((uint8_t *)t47));
            uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)t47) + 8));
            void * t50 = gm_l0;
            uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)t50) + 16));
            uint64_t t52 = (uintptr_t)t51;
            uint64_t t53 = (uintptr_t)t49;
            if (t52 > t53 || t53 > (uintptr_t)t49) gm_trap_slice(t52, t53, t49, gm_src3nt8pf0qk5gn, 258, 20);
            uint64_t t54 = ((uintptr_t)t49) - (uintptr_t)t51;
            gm_b16_8 t55;
            GM_ST(void *, (uint8_t *)&t55, (void *)(((uint8_t *)t48 + (uintptr_t)t51 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(t54));
            gm_b16_8 t56;
            GM_ST(void *, (uint8_t *)&t56, (void *)(gm_sehko5bb96s6p));
            GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(4));
            uint8_t t57 = gm_f_starts_with_6vvt01e(t55, t56);
            uint8_t t58 = (uint8_t)!t57;
            if (t58) {
                void * t59 = gm_l0;
                gm_b16_8 t60;
                GM_ST(void *, (uint8_t *)&t60, (void *)(gm_shlilv0hq3bqu));
                GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(32));
                gm_tob1snuii5bme t61;
                gm_f_error_96ci5lu((void *)&t61, t59, t60);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t61);
                goto gm_fdone;
            }
            void * t62 = gm_l0;
            void * t63 = gm_l0;
            uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)t63) + 16));
            uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)4ULL), &r65);
            if (o66) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 259, 13);
            uint64_t t67 = r65;
            GM_ST(uint64_t, (((uint8_t *)t62) + 16), t67);
            void * t68 = gm_l0;
            GM_ST(uint8_t, (((uint8_t *)t68) + 32), ((uint8_t)1));
            {
                gm_tktr4rmbindgv t69;
                memset(&t69, 0, sizeof t69);
                GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)2ULL));
                GM_ST(uint8_t, ((uint8_t *)&t69 + 8), ((uint8_t)1));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 0;
                GM_ST(gm_tktr4rmbindgv, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t69);
                goto gm_fdone;
            }
        }
gm_lb8:;
        {
            uint8_t t70 = gm_l1;
            uint8_t t71 = ((t70 == ((uint8_t)102ULL)));
            if (!t71) {
                goto gm_lb9;
            }
            void * t72 = gm_l0;
            void * t73 = GM_LD(void *, ((uint8_t *)t72));
            uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)t72) + 8));
            void * t75 = gm_l0;
            uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)t75) + 16));
            uint64_t t77 = (uintptr_t)t76;
            uint64_t t78 = (uintptr_t)t74;
            if (t77 > t78 || t78 > (uintptr_t)t74) gm_trap_slice(t77, t78, t74, gm_src3nt8pf0qk5gn, 264, 20);
            uint64_t t79 = ((uintptr_t)t74) - (uintptr_t)t76;
            gm_b16_8 t80;
            GM_ST(void *, (uint8_t *)&t80, (void *)(((uint8_t *)t73 + (uintptr_t)t76 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(t79));
            gm_b16_8 t81;
            GM_ST(void *, (uint8_t *)&t81, (void *)(gm_sogehh8t1m7tn));
            GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(5));
            uint8_t t82 = gm_f_starts_with_6vvt01e(t80, t81);
            uint8_t t83 = (uint8_t)!t82;
            if (t83) {
                void * t84 = gm_l0;
                gm_b16_8 t85;
                GM_ST(void *, (uint8_t *)&t85, (void *)(gm_sne7l93pr2tmb));
                GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(33));
                gm_tob1snuii5bme t86;
                gm_f_error_96ci5lu((void *)&t86, t84, t85);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t86);
                goto gm_fdone;
            }
            void * t87 = gm_l0;
            void * t88 = gm_l0;
            uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)t88) + 16));
            uint64_t r90; bool o91 = __builtin_add_overflow(t89, ((uint64_t)5ULL), &r90);
            if (o91) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 265, 13);
            uint64_t t92 = r90;
            GM_ST(uint64_t, (((uint8_t *)t87) + 16), t92);
            void * t93 = gm_l0;
            GM_ST(uint8_t, (((uint8_t *)t93) + 32), ((uint8_t)1));
            {
                gm_tktr4rmbindgv t94;
                memset(&t94, 0, sizeof t94);
                GM_ST(uint8_t, ((uint8_t *)&t94 + 0), ((uint8_t)2ULL));
                GM_ST(uint8_t, ((uint8_t *)&t94 + 8), ((uint8_t)0));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 0;
                GM_ST(gm_tktr4rmbindgv, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t94);
                goto gm_fdone;
            }
        }
gm_lb9:;
        {
            {
                {
                    uint8_t t95 = gm_l1;
                    uint8_t t96 = ((t95 == ((uint8_t)43ULL)));
                    if (!t96) {
                        goto gm_lb12;
                    }
                    goto gm_lb11;
                }
gm_lb12:;
                {
                    uint8_t t97 = gm_l1;
                    uint8_t t98 = ((t97 == ((uint8_t)45ULL)));
                    if (!t98) {
                        goto gm_lb13;
                    }
                    goto gm_lb11;
                }
gm_lb13:;
                {
                    uint8_t t99 = gm_l1;
                    uint8_t t100 = (t99 >= ((uint8_t)48ULL));
                    if (!t100) {
                        goto gm_lb14;
                    }
                    uint8_t t101 = (t99 <= ((uint8_t)57ULL));
                    if (!t101) {
                        goto gm_lb14;
                    }
                    goto gm_lb11;
                }
gm_lb14:;
                goto gm_lb10;
            }
gm_lb11:;
            void * t102 = gm_l0;
            gm_f_number_4l95pjb((void *)&(*(gm_tefknldmnh2si *)gm_out), t102);
            goto gm_fdone;
        }
gm_lb10:;
        {
            {
                {
                    uint8_t t103 = gm_l1;
                    uint8_t t104 = ((t103 == ((uint8_t)105ULL)));
                    if (!t104) {
                        goto gm_lb17;
                    }
                    goto gm_lb16;
                }
gm_lb17:;
                {
                    uint8_t t105 = gm_l1;
                    uint8_t t106 = ((t105 == ((uint8_t)110ULL)));
                    if (!t106) {
                        goto gm_lb18;
                    }
                    goto gm_lb16;
                }
gm_lb18:;
                goto gm_lb15;
            }
gm_lb16:;
            void * t107 = gm_l0;
            void * t108 = GM_LD(void *, ((uint8_t *)t107));
            uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)t107) + 8));
            void * t110 = gm_l0;
            uint64_t t111 = GM_LD(uint64_t, (((uint8_t *)t110) + 16));
            uint64_t t112 = (uintptr_t)t111;
            uint64_t t113 = (uintptr_t)t109;
            if (t112 > t113 || t113 > (uintptr_t)t109) gm_trap_slice(t112, t113, t109, gm_src3nt8pf0qk5gn, 271, 21);
            uint64_t t114 = ((uintptr_t)t109) - (uintptr_t)t111;
            gm_b16_8 t115;
            GM_ST(void *, (uint8_t *)&t115, (void *)(((uint8_t *)t108 + (uintptr_t)t111 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t115 + 8, (uintptr_t)(t114));
            gm_l2 = t115;
            gm_b16_8 t116 = gm_l2;
            gm_b16_8 t117;
            GM_ST(void *, (uint8_t *)&t117, (void *)(gm_sns75reqq32d2));
            GM_ST(uintptr_t, (uint8_t *)&t117 + 8, (uintptr_t)(3));
            uint8_t t118 = gm_f_starts_with_6vvt01e(t116, t117);
            if (t118) {
                gm_l5 = ((uint8_t)1);
            } else {
                gm_b16_8 t119 = gm_l2;
                gm_b16_8 t120;
                GM_ST(void *, (uint8_t *)&t120, (void *)(gm_sfa86erpod86s));
                GM_ST(uintptr_t, (uint8_t *)&t120 + 8, (uintptr_t)(3));
                uint8_t t121 = gm_f_starts_with_6vvt01e(t119, t120);
                gm_l5 = t121;
            }
            uint8_t t122 = gm_l5;
            if (t122) {
                void * t123 = gm_l0;
                void * t124 = gm_l0;
                uint64_t t125 = GM_LD(uint64_t, (((uint8_t *)t124) + 16));
                uint64_t r126; bool o127 = __builtin_add_overflow(t125, ((uint64_t)3ULL), &r126);
                if (o127) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 273, 17);
                uint64_t t128 = r126;
                GM_ST(uint64_t, (((uint8_t *)t123) + 16), t128);
                void * t129 = gm_l0;
                GM_ST(uint8_t, (((uint8_t *)t129) + 32), ((uint8_t)1));
                {
                    void * t130 = GM_LD(void *, ((uint8_t *)&gm_l2));
                    uint64_t t131 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                    uint64_t t132 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t133 = (uintptr_t)((uint64_t)3ULL);
                    if (t132 > t133 || t133 > (uintptr_t)t131) gm_trap_slice(t132, t133, t131, gm_src3nt8pf0qk5gn, 275, 36);
                    uint64_t t134 = ((uintptr_t)((uint64_t)3ULL)) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t135;
                    GM_ST(void *, (uint8_t *)&t135, (void *)(((uint8_t *)t130 + (uintptr_t)((uint64_t)0ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t135 + 8, (uintptr_t)(t134));
                    gm_tktr4rmbindgv t136;
                    GM_ST(uint8_t, (uint8_t *)&t136 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t136 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t136 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t136 + 24, 0);
                    GM_ST(uint64_t, (uint8_t *)&t136 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t136 + 40, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t136 + 0), ((uint8_t)5ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t136 + 8), t135);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 0;
                    GM_ST(gm_tktr4rmbindgv, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t136);
                    goto gm_fdone;
                }
            }
            {
                void * t137 = gm_l0;
                gm_b16_8 t138 = GM_LD(gm_b16_8, ((uint8_t *)gm_cal7njs7es4));
                gm_tob1snuii5bme t139;
                gm_f_error_96ci5lu((void *)&t139, t137, t138);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t139);
                goto gm_fdone;
            }
        }
gm_lb15:;
        {
            uint8_t t140 = gm_l1;
            uint8_t t141 = ((t140 == ((uint8_t)61ULL)));
            if (!t141) {
                goto gm_lb19;
            }
            {
                void * t142 = gm_l0;
                gm_b16_8 t143;
                GM_ST(void *, (uint8_t *)&t143, (void *)(gm_sv5o4caljvbmj));
                GM_ST(uintptr_t, (uint8_t *)&t143 + 8, (uintptr_t)(27));
                gm_tob1snuii5bme t144;
                gm_f_error_96ci5lu((void *)&t144, t142, t143);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t144);
                goto gm_fdone;
            }
        }
gm_lb19:;
        {
            void * t145 = gm_l0;
            gm_b16_8 t146 = GM_LD(gm_b16_8, ((uint8_t *)gm_cal7njs7es4));
            gm_tob1snuii5bme t147;
            gm_f_error_96ci5lu((void *)&t147, t145, t146);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
            ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
            GM_ST(gm_tob1snuii5bme, ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8), t147);
            goto gm_fdone;
        }
gm_lb20:;
        gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 253, 5);
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

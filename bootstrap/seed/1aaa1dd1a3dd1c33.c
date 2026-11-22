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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { int32_t f0; } gm_t19enj90vg8bd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; } gm_tb31k5l794t7p;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; uint64_t f2; } gm_tuglhjjmj2g58;
typedef struct gm_cs7e9ppsd384bj { uint32_t f0; gm_csguankko4lmhd f1; } gm_cs7e9ppsd384bj;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; } gm_t0am46iohlpol;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t19enj90vg8bd) == 4 && _Alignof(gm_t19enj90vg8bd) == 4 && offsetof(gm_t19enj90vg8bd, f0) == 0, "layout of `(i32,)`");
typedef struct { uint8_t f0; gm_tqs5p4n2tq19v f1; uint8_t f2; } gm_t1h8kp9hsu3ua;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuglhjjmj2g58 v0; uint16_t v1; } f1; } gm_t5v8n6qrn4rh4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; uint8_t v2; gm_t9qkh0rsooc8d v3; gm_t9qkh0rsooc8d v4; gm_tcc1rq4n1avb2 v5; uint32_t v6; uint32_t v7; } f1; } gm_t8588ma2cr9d7;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tb31k5l794t7p) == 16 && _Alignof(gm_tb31k5l794t7p) == 8 && offsetof(gm_tb31k5l794t7p, f0) == 0 && offsetof(gm_tb31k5l794t7p, f1) == 8, "layout of `Char`");
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tcrdb1inj22os;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_b16_8 f1; } gm_thervm8j23qet;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_b16_8 f2; } gm_thognv9odov03;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { gm_b16_8 f0; gm_tf5srh4mp4b3i f1; } gm_tj64ui6kruni6;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tk9n3vlila9qg;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; } gm_tq3m5qgvkpje8;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_b16_8 f2; gm_t88rcucg1b7uk f3; uint8_t p4[4]; gm_b16_8 f4; uint32_t f5; uint8_t p6[4]; gm_b16_8 f6; uint32_t f7; uint8_t pe[4]; } gm_tqh25f25b69k5;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t p3[3]; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tsoie1ppgdvcg;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
_Static_assert(sizeof(gm_tuglhjjmj2g58) == 16 && _Alignof(gm_tuglhjjmj2g58) == 8 && offsetof(gm_tuglhjjmj2g58, f0) == 0 && offsetof(gm_tuglhjjmj2g58, f1) == 4 && offsetof(gm_tuglhjjmj2g58, f2) == 8, "layout of `(signed: bool, bits: u32, storage: u64)`");
_Static_assert(sizeof(gm_cs7e9ppsd384bj) == 8 && _Alignof(gm_cs7e9ppsd384bj) == 4 && offsetof(gm_cs7e9ppsd384bj, f0) == 0 && offsetof(gm_cs7e9ppsd384bj, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0am46iohlpol) == 2 && _Alignof(gm_t0am46iohlpol) == 1 && offsetof(gm_t0am46iohlpol, f0) == 0 && offsetof(gm_t0am46iohlpol, f1) == 1, "layout of `(Command, Command)`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
_Static_assert(sizeof(gm_t1h8kp9hsu3ua) == 4 && _Alignof(gm_t1h8kp9hsu3ua) == 1 && offsetof(gm_t1h8kp9hsu3ua, f0) == 0 && offsetof(gm_t1h8kp9hsu3ua, f1) == 1 && offsetof(gm_t1h8kp9hsu3ua, f2) == 3, "layout of `Hints`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `(string, usize)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
typedef struct { uint64_t f0; gm_tsoie1ppgdvcg f1; } gm_t5im2v50m3hk6;
_Static_assert(sizeof(gm_t5v8n6qrn4rh4) == 24 && _Alignof(gm_t5v8n6qrn4rh4) == 8 && offsetof(gm_t5v8n6qrn4rh4, f0) == 0 && offsetof(gm_t5v8n6qrn4rh4, f1) == 8, "layout of `Repr`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t8588ma2cr9d7) == 12 && _Alignof(gm_t8588ma2cr9d7) == 4 && offsetof(gm_t8588ma2cr9d7, f0) == 0 && offsetof(gm_t8588ma2cr9d7, f1) == 4, "layout of `Builtin`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_thognv9odov03 v0; gm_tiilmfd16v3fc v1; } f1; } gm_tbf8u1b86cgmi;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
_Static_assert(sizeof(gm_tcrdb1inj22os) == 72 && _Alignof(gm_tcrdb1inj22os) == 8 && offsetof(gm_tcrdb1inj22os, f0) == 0 && offsetof(gm_tcrdb1inj22os, f1) == 16 && offsetof(gm_tcrdb1inj22os, f2) == 24 && offsetof(gm_tcrdb1inj22os, f3) == 40 && offsetof(gm_tcrdb1inj22os, f4) == 56, "layout of `(string, usize, string, string, string)`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; } gm_td7pkamr64l0c;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thervm8j23qet) == 24 && _Alignof(gm_thervm8j23qet) == 8 && offsetof(gm_thervm8j23qet, f0) == 0 && offsetof(gm_thervm8j23qet, f1) == 8, "layout of `Slot(string, ())`");
_Static_assert(sizeof(gm_thognv9odov03) == 40 && _Alignof(gm_thognv9odov03) == 8 && offsetof(gm_thognv9odov03, f0) == 0 && offsetof(gm_thognv9odov03, f1) == 8 && offsetof(gm_thognv9odov03, f2) == 24, "layout of `ForeignFn`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tj64ui6kruni6) == 32 && _Alignof(gm_tj64ui6kruni6) == 8 && offsetof(gm_tj64ui6kruni6, f0) == 0 && offsetof(gm_tj64ui6kruni6, f1) == 16, "layout of `(string, ?usize)`");
_Static_assert(sizeof(gm_tk9n3vlila9qg) == 40 && _Alignof(gm_tk9n3vlila9qg) == 8 && offsetof(gm_tk9n3vlila9qg, f0) == 0 && offsetof(gm_tk9n3vlila9qg, f1) == 16 && offsetof(gm_tk9n3vlila9qg, f2) == 32, "layout of `(lib: string, link_name: string, variadic: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { void * f0; uint32_t f1; uint8_t p2[4]; gm_b16_8 f2; gm_t9805qtonbsag f3; gm_t9805qtonbsag f4; gm_th2lqbo77pvsc f5; gm_th2lqbo77pvsc f6; gm_th2lqbo77pvsc f7; void * f8; gm_trqfvmjtt4sij f9; uint64_t f10; uint32_t f11; uint8_t p12[4]; gm_trqfvmjtt4sij f12; gm_b16_8 f13; uint8_t f14; uint8_t f15; uint8_t f16; uint8_t p17[5]; uint64_t f17; gm_tf5srh4mp4b3i f18; gm_b16_8 f19; gm_b16_8 f20; uint32_t f21; uint8_t f22; uint8_t p23[3]; gm_trqfvmjtt4sij f23; void * f24; } gm_to0hr49bb3bdb;
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_tq3m5qgvkpje8) == 40 && _Alignof(gm_tq3m5qgvkpje8) == 8 && offsetof(gm_tq3m5qgvkpje8, f0) == 0 && offsetof(gm_tq3m5qgvkpje8, f1) == 16 && offsetof(gm_tq3m5qgvkpje8, f2) == 24, "layout of `(string, usize, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tk9n3vlila9qg v0; } f1; } gm_tq9qng821udh4;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; } f1; } gm_tqgtd5gtgj65s;
_Static_assert(sizeof(gm_tqh25f25b69k5) == 96 && _Alignof(gm_tqh25f25b69k5) == 8 && offsetof(gm_tqh25f25b69k5, f0) == 0 && offsetof(gm_tqh25f25b69k5, f1) == 8 && offsetof(gm_tqh25f25b69k5, f2) == 24 && offsetof(gm_tqh25f25b69k5, f3) == 40 && offsetof(gm_tqh25f25b69k5, f4) == 48 && offsetof(gm_tqh25f25b69k5, f5) == 64 && offsetof(gm_tqh25f25b69k5, f6) == 72 && offsetof(gm_tqh25f25b69k5, f7) == 88, "layout of `Func`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tsoie1ppgdvcg) == 40 && _Alignof(gm_tsoie1ppgdvcg) == 8 && offsetof(gm_tsoie1ppgdvcg, f0) == 0 && offsetof(gm_tsoie1ppgdvcg, f1) == 16 && offsetof(gm_tsoie1ppgdvcg, f2) == 20 && offsetof(gm_tsoie1ppgdvcg, f3) == 24, "layout of `LocalDecl`");
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
typedef struct { gm_t4c7ipjj9s3fp f0; uint8_t f1; uint8_t pe[7]; } gm_tuutj662vpgtg;
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t5im2v50m3hk6) == 48 && _Alignof(gm_t5im2v50m3hk6) == 8 && offsetof(gm_t5im2v50m3hk6, f0) == 0 && offsetof(gm_t5im2v50m3hk6, f1) == 8, "layout of `(i: usize, x: LocalDecl)`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tbf8u1b86cgmi) == 48 && _Alignof(gm_tbf8u1b86cgmi) == 8 && offsetof(gm_tbf8u1b86cgmi, f0) == 0 && offsetof(gm_tbf8u1b86cgmi, f1) == 8, "layout of `ScopeKind`");
_Static_assert(sizeof(gm_td7pkamr64l0c) == 80 && _Alignof(gm_td7pkamr64l0c) == 8 && offsetof(gm_td7pkamr64l0c, f0) == 0 && offsetof(gm_td7pkamr64l0c, f1) == 40, "layout of `(q: List(u32), r: List(u32))`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
typedef struct { gm_tbf8u1b86cgmi f0; uint32_t f1; uint8_t p2[4]; gm_trqfvmjtt4sij f2; uint64_t f3; } gm_tehgl6lkqgvt0;
_Static_assert(sizeof(gm_to0hr49bb3bdb) == 360 && _Alignof(gm_to0hr49bb3bdb) == 8 && offsetof(gm_to0hr49bb3bdb, f0) == 0 && offsetof(gm_to0hr49bb3bdb, f1) == 8 && offsetof(gm_to0hr49bb3bdb, f2) == 16 && offsetof(gm_to0hr49bb3bdb, f3) == 32 && offsetof(gm_to0hr49bb3bdb, f4) == 40 && offsetof(gm_to0hr49bb3bdb, f5) == 48 && offsetof(gm_to0hr49bb3bdb, f6) == 72 && offsetof(gm_to0hr49bb3bdb, f7) == 96 && offsetof(gm_to0hr49bb3bdb, f8) == 120 && offsetof(gm_to0hr49bb3bdb, f9) == 128 && offsetof(gm_to0hr49bb3bdb, f10) == 168 && offsetof(gm_to0hr49bb3bdb, f11) == 176 && offsetof(gm_to0hr49bb3bdb, f12) == 184 && offsetof(gm_to0hr49bb3bdb, f13) == 224 && offsetof(gm_to0hr49bb3bdb, f14) == 240 && offsetof(gm_to0hr49bb3bdb, f15) == 241 && offsetof(gm_to0hr49bb3bdb, f16) == 242 && offsetof(gm_to0hr49bb3bdb, f17) == 248 && offsetof(gm_to0hr49bb3bdb, f18) == 256 && offsetof(gm_to0hr49bb3bdb, f19) == 272 && offsetof(gm_to0hr49bb3bdb, f20) == 288 && offsetof(gm_to0hr49bb3bdb, f21) == 304 && offsetof(gm_to0hr49bb3bdb, f22) == 308 && offsetof(gm_to0hr49bb3bdb, f23) == 312 && offsetof(gm_to0hr49bb3bdb, f24) == 352, "layout of `FnGen`");
_Static_assert(sizeof(gm_tq9qng821udh4) == 48 && _Alignof(gm_tq9qng821udh4) == 8 && offsetof(gm_tq9qng821udh4, f0) == 0 && offsetof(gm_tq9qng821udh4, f1) == 8, "layout of `FnKind`");
_Static_assert(sizeof(gm_tqgtd5gtgj65s) == 40 && _Alignof(gm_tqgtd5gtgj65s) == 8 && offsetof(gm_tqgtd5gtgj65s, f0) == 0 && offsetof(gm_tqgtd5gtgj65s, f1) == 8, "layout of `Unsup!()`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
_Static_assert(sizeof(gm_tuutj662vpgtg) == 64 && _Alignof(gm_tuutj662vpgtg) == 8 && offsetof(gm_tuutj662vpgtg, f0) == 0 && offsetof(gm_tuutj662vpgtg, f1) == 56, "layout of `?HashMap((u32, u32), bool, null, null)`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tehgl6lkqgvt0) == 104 && _Alignof(gm_tehgl6lkqgvt0) == 8 && offsetof(gm_tehgl6lkqgvt0, f0) == 0 && offsetof(gm_tehgl6lkqgvt0, f1) == 48 && offsetof(gm_tehgl6lkqgvt0, f2) == 56 && offsetof(gm_tehgl6lkqgvt0, f3) == 96, "layout of `Scope`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
struct gm_fr_add_member_name_j3i0eje {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_any_child_ffc8h1k {
    gm_frame h;
    gm_closure l1; /* pred */
    uint8_t l2; /* found */
    uint8_t ret;
};
struct gm_fr_block_scope_e3cgusu {
    gm_frame h;
    gm_tehgl6lkqgvt0 ret;
};
struct gm_fr_c_abi_wrapper_33vbmt4 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* params */
    gm_trqfvmjtt4sij l4; /* args */
    gm_trqfvmjtt4sij l14; /* with_out */
    gm_b0_1 ret;
};
struct gm_fr_call_depth_4m6ap7n {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_calls_probe_2ia3rhf {
    gm_frame h;
    void * l0; /* g */
    gm_tsth9qu1dv0g3 l2; /* callee */
    uint8_t l5; /* ok */
    uint8_t ret;
};
struct gm_fr_captured_locals_glsvloj {
    gm_frame h;
    gm_t4c7ipjj9s3fp l1; /* set */
    gm_t4c7ipjj9s3fp l2; /* set */
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_check_unwind_2hji4bl {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_collect_upvars_q0085da {
    gm_frame h;
    void * l0; /* prog */
    uint32_t l1; /* f */
    void * l3; /* out */
    gm_b0_1 ret;
};
struct gm_fr_collect_withs_fa29mk1 {
    gm_frame h;
    void * l1; /* out */
    gm_t710lsrq1m8gk l2; /* w */
    gm_b0_1 ret;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_conv_attr_m0b89pc {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_defer_body_avm7tau {
    gm_frame h;
    gm_td7pkamr64l0c l4; /* saved */
    gm_b0_1 ret;
};
struct gm_fr_defer_body_inner_v8ddaht {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_4q8glfd {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_93nt26c {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_gm6v230 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jjmeosp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_k21v01c {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lv6i4jh {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_meisvg6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ppl3r45 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_end_lines_99v0bhd {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_end_scope_45ar5qv {
    gm_frame h;
    gm_tehgl6lkqgvt0 l1; /* s */
    gm_thognv9odov03 l7; /* l */
    gm_b0_1 ret;
};
struct gm_fr_exit_c_name_i72r98r {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_expr_call_depth_dliqc7u {
    gm_frame h;
    void * l0; /* g */
    uint32_t l2; /* depth */
    gm_tsth9qu1dv0g3 l3; /* callee */
    uint32_t ret;
};
struct gm_fr_extend_07kdnvi {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_vogsd72 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_fn_5evrfa7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_7u87bne {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_count_g9jh9qr {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_fn_e5aba81 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_jjarlg6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_kssb0q2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_v0t9sik {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_07f8le4 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_32n178h {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4hn970h {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4un19t8 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5bcbbmm {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5erjuiq {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_822rutp {
    gm_frame h;
    gm_tud7oovfvnf43 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_99gc605 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_9pumrco {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cp0hre9 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_d6fj9tl {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dd6d4bt {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ddrb17v {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dsta2ql {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_eq87866 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fsfutor {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ghqt9ar {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_gt1sds7 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hi89jlk {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hs8tdgq {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kadoe0b {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ko1kpil {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l05am3t {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lb0inss {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_loh175m {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lsmcccn {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lufljqc {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_pu04c4a {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rmu2e6q {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rtv6jsp {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_u9etquj {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_frame_field_nj8m22n {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_frame_locals_mt4nb6o {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* out */
    gm_t4c7ipjj9s3fp l10; /* set */
    gm_tsoie1ppgdvcg l15; /* l */
    gm_b16_8 ret;
};
struct gm_fr_frame_name_stddmgg {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_frame_up_g8486eu {
    gm_frame h;
    gm_tommf4j9upomc ret;
};
struct gm_fr_fresh_5kokjmg {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_fresh_tmp_6tadl69 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_gen_body_j6ku8qk {
    gm_frame h;
    gm_trqfvmjtt4sij l10; /* params */
    gm_tqh25f25b69k5 l20; /* lf */
    gm_trqfvmjtt4sij l21; /* all */
    gm_to0hr49bb3bdb l29; /* fg */
    gm_tsoie1ppgdvcg l33; /* l */
    gm_tqgtd5gtgj65s ret;
};
struct gm_fr_gen_fn_2fqvb90 {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* frame */
    gm_trqfvmjtt4sij l8; /* local_names */
    gm_tsoie1ppgdvcg l11; /* l */
    gm_t4c7ipjj9s3fp l12; /* taken */
    gm_tsoie1ppgdvcg l16; /* l */
    gm_tqh25f25b69k5 l22; /* lf */
    gm_trqfvmjtt4sij l26; /* withs */
    gm_tqgtd5gtgj65s ret;
};
struct gm_fr_get_ag7vhtq {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_has_fiber_with_53c3jik {
    gm_frame h;
    gm_t710lsrq1m8gk l1; /* w */
    uint8_t ret;
};
struct gm_fr_hash_map_cuh4fs9 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_t9h7o8p {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hint_attrs_mktdhi1 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* s */
    gm_b16_8 ret;
};
struct gm_fr_indent_line_b3mckqa {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_is_borrowed_qi4apdt {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_unit_17uennd {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_unit_n83ttnb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_label_710oev4 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_ladder_36amdbn {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_ladder_target_84n3aep {
    gm_frame h;
    gm_tj64ui6kruni6 ret;
};
struct gm_fr_line_l6u87p9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_1p9d72n {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_2n883h6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_4plvds6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_7p5m9ll {
    gm_frame h;
    gm_t0uuhaljsf410 l1; /* args */
    gm_b0_1 ret;
};
struct gm_fr_linef_aka1a8l {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_bi02v43 {
    gm_frame h;
    gm_tfeab279c16rr l1; /* args */
    gm_b0_1 ret;
};
struct gm_fr_linef_cs9kbvs {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_cuactnf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_eils77b {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_g6msml8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_md5jtn8 {
    gm_frame h;
    gm_tq3m5qgvkpje8 l1; /* args */
    gm_b0_1 ret;
};
struct gm_fr_linef_mkfjg2u {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_njoe3l6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_nvqsj6t {
    gm_frame h;
    gm_tq3m5qgvkpje8 l1; /* args */
    gm_b0_1 ret;
};
struct gm_fr_linef_qqk2cu4 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_uu8i6ue {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_lir_framed_6i9516j {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_list_9ljroit {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ns53od0 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_rl2scuf {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ubba23m {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_local_addr_g9lkr6l {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_local_exit_4aogt00 {
    gm_frame h;
    gm_tehgl6lkqgvt0 l7; /* s */
    gm_thognv9odov03 l9; /* l */
    gm_tehgl6lkqgvt0 l17; /* s */
    gm_tehgl6lkqgvt0 l23; /* s */
    gm_thognv9odov03 l24; /* l */
    uint8_t ret;
};
struct gm_fr_local_lv_m2jtf0n {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_loop_scope_ee94ihm {
    gm_frame h;
    gm_tehgl6lkqgvt0 ret;
};
struct gm_fr_no_defers_hci755r {
    gm_frame h;
    gm_tehgl6lkqgvt0 l3; /* s */
    uint8_t ret;
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
struct gm_fr_param_index_rp1nn6k {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_plain_cty_q9lhlci {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_probe_free_4hilimu {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_7dfimu1 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_givp2as {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_t83rg2m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_vd1290p {
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
struct gm_fr_regrow_bg5jjth {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_egu240o {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_hkmvuvc {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_replacen1_7ksfmmn {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_restore_scopes_f62992p {
    gm_frame h;
    gm_td7pkamr64l0c l1; /* saved */
    gm_b0_1 ret;
};
struct gm_fr_set_aside_scopes_ho12if8 {
    gm_frame h;
    gm_td7pkamr64l0c l1; /* saved */
    gm_td7pkamr64l0c ret;
};
struct gm_fr_size_attrs_abk1l3m {
    gm_frame h;
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
struct gm_fr_src_line_d24sajl {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_start_exit_m57dnn5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_tmp_uninit_jr39s5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_tmp_uohsptq {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unique_members_mrojmjf {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_8ekedsl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unit_ty_vgceilu {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unsupported_531q51r {
    gm_frame h;
    gm_t08olq97rscn3 ret;
};
struct gm_fr_unwind_call_4k8ulav {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_unwind_ladder_a9ij1gj {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_with_scope_o670qbc {
    gm_frame h;
    gm_tehgl6lkqgvt0 ret;
};
struct gm_fr_write_3c9chj4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_ajj3a37 {
    gm_frame h;
    gm_tftksr4op114e l1; /* args */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_cdouvhd {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_dfbs2m5 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_f1cbljc {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_i2vvij8 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_ih4qokb {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_ihqa4kk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_ildv1bj {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_j67vjku {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_l0vqbl8 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_m7rqe8h {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_n0vcum5 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_s0f8vaq {
    gm_frame h;
    gm_tq3m5qgvkpje8 l1; /* args */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_s858s21 {
    gm_frame h;
    gm_tcrdb1inj22os l1; /* args */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_shh24f1 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_va1cfg2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_zero_tlk13aa {
    gm_frame h;
    gm_b16_8 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eqgnrg8h3j6k(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_add_member_name_j3i0eje(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_any_child_ffc8h1k(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_backtrace_name_mars429(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_block_scope_e3cgusu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_borrowed_params_b39va3r(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_by_ref_fi0o860(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_c_abi_wrapper_33vbmt4(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_c_string_n7h27b3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint32_t gm_f_call_depth_4m6ap7n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_calls_probe_2ia3rhf(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_captured_locals_glsvloj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_unwind_2hji4bl(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_collect_upvars_q0085da(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, void * a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_collect_withs_fa29mk1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_conv_attr_m0b89pc(uint8_t a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_cty_66ee0kf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_debug_view_gp38euq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_defer_body_avm7tau(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2, uint8_t a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_defer_body_inner_v8ddaht(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2, uint8_t a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_4q8glfd(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_93nt26c(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_gm6v230(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jjmeosp(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_k21v01c(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lv6i4jh(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_meisvg6(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ppl3r45(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_end_lines_99v0bhd(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_end_scope_45ar5qv(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint8_t a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_exit_c_name_i72r98r(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) uint32_t gm_f_expr_call_depth_dliqc7u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_07kdnvi(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_vogsd72(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_efqiqfc(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5evrfa7(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7u87bne(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint64_t gm_f_fn_count_g9jh9qr(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_e5aba81(void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_jjarlg6(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kssb0q2(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_name_6n34573(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_tag_okd90ep(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t9805qtonbsag a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_v0t9sik(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_foreign_decl_rld7m78(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_format_01lon97(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_07f8le4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_2eptqlp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tb31k5l794t7p a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_2kt92vg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_32n178h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_4hn970h(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_4un19t8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_5bcbbmm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tb31k5l794t7p a0);
inline __attribute__((aligned(4))) void gm_f_format_5erjuiq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_822rutp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_8ghesn9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_99gc605(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_9pumrco(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_cp0hre9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_d6fj9tl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_dd6d4bt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_ddrb17v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_dsta2ql(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_eq87866(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fq216g1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_fsfutor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ghqt9ar(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_gt1sds7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_he6jnp0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_hi89jlk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_hs8tdgq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_kadoe0b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ko1kpil(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_l05am3t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_lb0inss(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_lo2svpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_loh175m(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_lsmcccn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_lufljqc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_mccr87v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_ncqd055(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_pu04c4a(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_rmu2e6q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_rtv6jsp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_u9etquj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) uint64_t gm_f_frame_cost_oei64tv(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_frame_done_014nc4i(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_frame_field_nj8m22n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_frame_locals_mt4nb6o(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_frame_name_stddmgg(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_frame_up_g8486eu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_5kokjmg(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_fresh_tmp_6tadl69(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_func_in17r5n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_gamma_value_rgbfhcc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_gen_body_j6ku8qk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, void * a3, gm_t9805qtonbsag a4);
__attribute__((aligned(4))) void gm_f_gen_fn_2fqvb90(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t9805qtonbsag a2);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_ag7vhtq(void * a0, gm_tcc1rq4n1avb2 a1);
__attribute__((aligned(4))) uint8_t gm_f_has_fiber_with_53c3jik(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_has_long_list_75nlnqn(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_hash_map_cuh4fs9(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_t9h7o8p(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_hint_attrs_mktdhi1(void *env, gm_node *cx, gm_node *inst, gm_t1h8kp9hsu3ua a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_indent_line_b3mckqa(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_borrowed_qi4apdt(void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_is_plain_qr08i40(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_17uennd(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_jump_pvnkvme(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tf5srh4mp4b3i a2);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_label_710oev4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_ladder_36amdbn(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_ladder_target_84n3aep(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_lc_defer_list_8u47fth(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_line_directive_fjg94gc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_line_l6u87p9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_1p9d72n(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_2n883h6(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_4plvds6(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_7afcl2l(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_7p5m9ll(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_8uttu8h(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_9u95itl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_aka1a8l(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_ao39327(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_bi02v43(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_c5qou5f(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_cs9kbvs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t19enj90vg8bd a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_cuactnf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_eils77b(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_fbspl3h(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_g6msml8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_md5jtn8(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_mkfjg2u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_njoe3l6(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_nvqsj6t(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_qqk2cu4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_sf7abhb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t19enj90vg8bd a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_slpvrc7(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_uu8i6ue(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_lir_body_90044es(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_lir_check_bnbf23l(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
inline __attribute__((aligned(4))) uint8_t gm_f_lir_framed_6i9516j(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_lir_of_60vujj2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_list_9ljroit(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ns53od0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_rl2scuf(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ubba23m(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_local_addr_g9lkr6l(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_local_exit_4aogt00(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_local_lv_m2jtf0n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_long_body_qgvuu1c(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_loop_scope_ee94ihm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_mirror_lcjegsf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_needs_wrapper_ktlbb7i(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_no_defers_hci755r(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_outlines_2kvbvit(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_cty_e8tvhrm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_param_index_rp1nn6k(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_plain_cty_q9lhlci(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_plain_value_e380k1u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_probe_free_4hilimu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_7dfimu1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_givp2as(void * a0, gm_cs7e9ppsd384bj a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_3g1tdpr(void * a0, gm_tcc1rq4n1avb2 a1, uint8_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_5c6qgui(void * a0, gm_b16_8 a1, uint8_t a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_bg5jjth(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_egu240o(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hkmvuvc(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) gm_b16_8 gm_f_replacen1_7ksfmmn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_repr_53quqki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_restore_scopes_f62992p(void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_set_aside_scopes_ho12if8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_size_attrs_abk1l3m(uint32_t a0);
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_spec_name_jptsbbf(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_src_line_d24sajl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_start_exit_m57dnn5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uninit_jr39s5u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uohsptq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unique_members_mrojmjf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_unique_with_mffff7e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, uint8_t a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unit_8ekedsl(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_ty_vgceilu(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_unsupported_531q51r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unwind_call_4k8ulav(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_unwind_ladder_a9ij1gj(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_with_scope_o670qbc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_0ie0tub(void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2venbiq(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_3c9chj4(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_6dciq50(void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_70cb4d8(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ajj3a37(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_cdouvhd(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_dfbs2m5(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_f1cbljc(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_glpclq4(void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_hqo1u7h(void * a0, gm_t4ce0jd3tdro4 a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_i2vvij8(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ih4qokb(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ihqa4kk(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ildv1bj(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_isshs6q(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_j67vjku(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jog5i62(void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_l0vqbl8(void * a0, gm_t4ce0jd3tdro4 a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_m7rqe8h(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_n0vcum5(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_qb1f4ht(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_s0f8vaq(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_s858s21(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_sa6ppdq(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_shh24f1(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_va1cfg2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_vq1ifl4(void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_zero_tlk13aa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern uint8_t gm_c1id0n0l1pf[8] __attribute__((aligned(8)));
extern uint8_t gm_c71idar1l1k[1] __attribute__((aligned(1)));
extern uint8_t gm_cbemal9c6bf[8] __attribute__((aligned(8)));
extern uint8_t gm_cc9imq1spis[16] __attribute__((aligned(8)));
extern uint8_t gm_ccmgjp6th6r[16] __attribute__((aligned(8)));
extern uint8_t gm_cdmki9acqi9[4] __attribute__((aligned(4)));
extern uint8_t gm_cgsmfcq937e_4[16] __attribute__((aligned(8)));
extern uint8_t gm_csshki16t34[4] __attribute__((aligned(4)));
extern uint8_t gm_ctq6t8frtor[4] __attribute__((aligned(4)));
extern uint8_t gm_cvpsnk4jni3[4] __attribute__((aligned(4)));
extern const uint8_t gm_s01fuch54uo3k[11];
extern const uint8_t gm_s04qus0luoga5[5];
extern const uint8_t gm_s0a1ag8clb46q[13];
extern const uint8_t gm_s0lpuem1an6p4[7];
extern const uint8_t gm_s0m07bui94rjq[8];
extern const uint8_t gm_s0r850bpi2rnm[14];
extern const uint8_t gm_s0uoh0jkm28kv[24];
extern const uint8_t gm_s2254b9hkee3b[6];
extern const uint8_t gm_s25h5btu4ug6a[12];
extern const uint8_t gm_s2cuflhqkkuaq[2];
extern const uint8_t gm_s2gtin895e08a[18];
extern const uint8_t gm_s2hbi2g88pale[19];
extern const uint8_t gm_s2hi6cc9n75l5[7];
extern const uint8_t gm_s2kvk8bnjk52t[12];
extern const uint8_t gm_s2micd2hub1nf[26];
extern const uint8_t gm_s2ogeh8fp9da3[12];
extern const uint8_t gm_s302tqgcs0oai[49];
extern const uint8_t gm_s39jha51ulivf[27];
extern const uint8_t gm_s3cu59itn73k7[23];
extern const uint8_t gm_s4pru1na1k33s[3];
extern const uint8_t gm_s4ql65vkv7h9p[13];
extern const uint8_t gm_s5ie9tn1qnf6v[9];
extern const uint8_t gm_s5k0s6vffccsf[9];
extern const uint8_t gm_s65rai75evpi7[14];
extern const uint8_t gm_s69crkid1kjkv[22];
extern const uint8_t gm_s6btsj90mfdi5[35];
extern const uint8_t gm_s78l3ok4on4sg[36];
extern const uint8_t gm_s8aupotg4okcv[103];
extern const uint8_t gm_s8cfqegc6giqu[53];
extern const uint8_t gm_s8fhmkhb94g4l[8];
extern const uint8_t gm_s8hh4pp2rpnlj[43];
extern const uint8_t gm_s8ue7na9eegfa[7];
extern const uint8_t gm_s9nl6jkck8c25[8];
extern const uint8_t gm_s9nvmaqikg2bc[8];
extern const uint8_t gm_s9r83c0uo3ifr[8];
extern const uint8_t gm_sal52ei1j638k[17];
extern const uint8_t gm_sapbk38ibps8q[28];
extern const uint8_t gm_sb2k9e71jsraf[7];
extern const uint8_t gm_sb3o3ij0n2htb[5];
extern const uint8_t gm_sb8kl54a499bf[7];
extern const uint8_t gm_sbrtnmpf78kqq[10];
extern const uint8_t gm_sc6pb1tb5tnnv[34];
extern const uint8_t gm_sdi44r3kug6n6[14];
extern const uint8_t gm_sdikuksp636di[12];
extern const uint8_t gm_sdkba1tit6s52[17];
extern const uint8_t gm_sdsb9longvn22[40];
extern const uint8_t gm_sfhct1r6m1qqo[11];
extern const uint8_t gm_sg398rgmck67b[4];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sgdqi8a9n3f12[15];
extern const uint8_t gm_sgt05f0dtajpv[18];
extern const uint8_t gm_sh16qm7deqse5[7];
extern const uint8_t gm_sh1e27cf0k9if[6];
extern const uint8_t gm_sh40na9qbga3l[2];
extern const uint8_t gm_si731m8b0sr6s[4];
extern const uint8_t gm_sicgmpca63jln[2];
extern const uint8_t gm_sidk0bupbfuvu[11];
extern const uint8_t gm_siji3oo7ud53t[10];
extern const uint8_t gm_sj7gfkb5eknah[76];
extern const uint8_t gm_sjibpf3glbc54[9];
extern const uint8_t gm_sjs17a3d2ohel[35];
extern const uint8_t gm_sk04qg20jmac0[4];
extern const uint8_t gm_sk0qegfrnee00[4];
extern const uint8_t gm_skdpq4t6o94o2[25];
extern const uint8_t gm_skdv3lmatcupg[21];
extern const uint8_t gm_skmddjugniqo1[51];
extern const uint8_t gm_skr89ndurop26[113];
extern const uint8_t gm_slg6o1a142s3f[44];
extern const uint8_t gm_slnuijmj5p1vk[19];
extern const uint8_t gm_slv0l3penlcbn[59];
extern const uint8_t gm_sm3vri4hmq0fl[13];
extern const uint8_t gm_sm7odqe0d6lgu[23];
extern const uint8_t gm_smap40unho18s[19];
extern const uint8_t gm_smgdg1m3kad32[74];
extern const uint8_t gm_smhvmmegaur89[7];
extern const uint8_t gm_smr5ddngcjuao[19];
extern const uint8_t gm_sn2gg3b98t635[21];
extern const uint8_t gm_sni0seheggmp0[30];
extern const uint8_t gm_snt0968qgofbj[37];
extern const uint8_t gm_snurnbocs9786[49];
extern const uint8_t gm_so0h0lkl6bp4l[39];
extern const uint8_t gm_so2qoj4e0o0al[6];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_soa9mnl4ahjud[8];
extern const uint8_t gm_soah6d9unb7uo[32];
extern const uint8_t gm_sou1qqe3k350t[13];
extern const uint8_t gm_souosds7adra9[36];
extern const uint8_t gm_sovhd8e8hvflr[52];
extern const uint8_t gm_sp1g56u3ibrtp[23];
extern const uint8_t gm_spdft68ac8308[95];
extern const uint8_t gm_spdntfv7vkd2t[17];
extern const uint8_t gm_spdpobhso0dbc[10];
extern const uint8_t gm_spfojlg262423[13];
extern const uint8_t gm_sqas1r3k72n65[51];
extern const uint8_t gm_sqqaphhmqv39s[5];
extern const uint8_t gm_sqqlqus41v6vq[12];
extern const uint8_t gm_sqtc9fffut4s2[13];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcd5arorrf8jkg[];
extern const char gm_srcht195tfrfuqo[];
extern const char gm_srckfibkpt681aa[];
extern const char gm_srcmok80ho8hh01[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_ss03naco6s5l7[5];
extern const uint8_t gm_ssmeaumlrdi7h[7];
extern const uint8_t gm_st1k7cra2pjic[71];
extern const uint8_t gm_st61cilmhqbek[13];
extern const uint8_t gm_stb0ck7v8hutn[8];
extern const uint8_t gm_stf7lvoc7oe54[16];
extern const uint8_t gm_stocunr1upos5[9];
extern const uint8_t gm_sts5j6f5pkov0[5];
extern const uint8_t gm_stsk5ch7ehebo[5];
extern const uint8_t gm_sttopppkehb8k[20];
extern const uint8_t gm_su20i5iji4ao3[4];
extern const uint8_t gm_su43dt5mihp55[22];
extern const uint8_t gm_sua89kt0f2v9n[30];
extern const uint8_t gm_sug1rl2kq22u3[9];
extern const uint8_t gm_suuphrufuvh4m[12];
extern const uint8_t gm_sv394jlms97cd[14];
extern const uint8_t gm_svdapq3j66qso[60];
extern const uint8_t gm_svg1jvtfj7fgp[6];
extern const uint8_t gm_svghvlr5om8a6[29];
extern const uint8_t gm_svmt0iljnadus[4];
extern const gm_tmpl gm_tpl086m1ajg247c;
extern const gm_tmpl gm_tpl0cgce8migcnp;
extern const gm_tmpl gm_tpl0uott3s3afvp;
extern const gm_tmpl gm_tpl1f7c1o805fpf;
extern const gm_tmpl gm_tpl1vbgcviqausd;
extern const gm_tmpl gm_tpl3bdcsso3qmi0;
extern const gm_tmpl gm_tpl3ergg6mce23h;
extern const gm_tmpl gm_tpl6skdtj6qlkmd;
extern const gm_tmpl gm_tpl6vfpiq020ehi;
extern const gm_tmpl gm_tpl7eeir95ujdvn;
extern const gm_tmpl gm_tpl7jau285qbbrq;
extern const gm_tmpl gm_tpl7pm1mtuteh6j;
extern const gm_tmpl gm_tpl7t589bv3t4us;
extern const gm_tmpl gm_tpl7vtkjsp2pa8b;
extern const gm_tmpl gm_tpl8f1efhudc5d4;
extern const gm_tmpl gm_tpl8mdj3nc4lpjv;
extern const gm_tmpl gm_tpl8ptl55ago227;
extern const gm_tmpl gm_tpl92h94vggr1gu;
extern const gm_tmpl gm_tpl9rm8d4b25q2c;
extern const gm_tmpl gm_tpla1fscog446dv;
extern const gm_tmpl gm_tpla6sajpp07b9l;
extern const gm_tmpl gm_tplb9i7jfun8rvg;
extern const gm_tmpl gm_tplbnt69qpq91i8;
extern const gm_tmpl gm_tplbu8sthucqj1b;
extern const gm_tmpl gm_tplc65rc46hqohp;
extern const gm_tmpl gm_tple6jpca094i2j;
extern const gm_tmpl gm_tpleie0q8nvjqch;
extern const gm_tmpl gm_tpleoabj1u2rb54;
extern const gm_tmpl gm_tpleth0e0pd50mr;
extern const gm_tmpl gm_tplett0du8rhujl;
extern const gm_tmpl gm_tplf1r4lr6ofdql;
extern const gm_tmpl gm_tplf4oacobin5qk;
extern const gm_tmpl gm_tplfft5kohcphn8;
extern const gm_tmpl gm_tplfnm34ailh6k9;
extern const gm_tmpl gm_tplfqr816g9de0k;
extern const gm_tmpl gm_tplh7231rl9trm2;
extern const gm_tmpl gm_tplhkjcpttbsqt0;
extern const gm_tmpl gm_tplicuqpjvsak60;
extern const gm_tmpl gm_tplikisll89mgrm;
extern const gm_tmpl gm_tplilne6f8eq90i;
extern const gm_tmpl gm_tplj16n1o03849r;
extern const gm_tmpl gm_tplj2qf541fmi6f;
extern const gm_tmpl gm_tplk34ojqiaho84;
extern const gm_tmpl gm_tplkj8cf8psv8p6;
extern const gm_tmpl gm_tplkk98947i5b3i;
extern const gm_tmpl gm_tplkodpfg3ik1kh;
extern const gm_tmpl gm_tpllaqikqjbb69k;
extern const gm_tmpl gm_tplm9hj31tk9ip4;
extern const gm_tmpl gm_tplmjmservvuqtp;
extern const gm_tmpl gm_tplnv6l3ecgreid;
extern const gm_tmpl gm_tploc41q7b5gurn;
extern const gm_tmpl gm_tplptgaia3s19c9;
extern const gm_tmpl gm_tplptu6sgqf50o0;
extern const gm_tmpl gm_tplr7pcqj5621b9;
extern const gm_tmpl gm_tpls8octb03drbp;
extern const gm_tmpl gm_tplsf6r3t44km8l;
extern const gm_tmpl gm_tplsil8jb9ng0ao;
extern const gm_tmpl gm_tplsjs5e78h34ng;
extern const gm_tmpl gm_tplt0qsjg06adbb;
extern const gm_tmpl gm_tplt5jbb3iqgb9i;
extern const gm_tmpl gm_tpltev7cklsieaq;
extern const gm_tmpl gm_tplu7gq2cm8aanh;
extern const gm_tmpl gm_tplvflfleui9qjm;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eqgnrg8h3j6k(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_add_member_name_j3i0eje(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_add_member_name_j3i0eje F;
    void * gm_l0; /* ty */
    gm_b16_8 gm_l1; /* name */
    GM_STACK_CHECK(&F, 61u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l2;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 816);
    gm_b16_8 t4 = gm_l1;
    gm_tftb1odc739t5 t5 = gm_f_put_5c6qgui(t3, t4, ((uint8_t)1));
    gm_l2 = t5;
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srckfibkpt681aa, 450, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) void gm_f_block_scope_e3cgusu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_block_scope_e3cgusu F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_tbf8u1b86cgmi t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)0ULL));
    uint32_t t4 = gm_f_fresh_5kokjmg(t2);
    gm_trqfvmjtt4sij t5;
    gm_f_list_rl2scuf((void *)&t5, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tehgl6lkqgvt0 *)gm_out) + 52, 0);
    GM_ST(gm_tbf8u1b86cgmi, ((uint8_t *)&(*(gm_tehgl6lkqgvt0 *)gm_out) + 0), t3);
    GM_ST(uint32_t, ((uint8_t *)&(*(gm_tehgl6lkqgvt0 *)gm_out) + 48), t4);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_tehgl6lkqgvt0 *)gm_out) + 56), t5);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_tehgl6lkqgvt0 *)gm_out) + 96), ((uint64_t)0ULL));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_unwind_2hji4bl(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_check_unwind_2hji4bl F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 49u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)t2) + 240));
    if (t3) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t4 = gm_l0;
    gm_b16_8 t5;
    GM_ST(void *, (uint8_t *)&t5, (void *)(gm_smr5ddngcjuao));
    GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(18));
    gm_b0_1 t6 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t7 = gm_l0;
    gm_b0_1 t8 = gm_f_unwind_ladder_a9ij1gj((void *)NULL, cx, NULL, t7);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_conv_attr_m0b89pc(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_conv_attr_m0b89pc F;
    uint8_t gm_l0; /* win64 */
    GM_STACK_CHECK_SHALLOW(&F, 98u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l1;
    uint8_t t2 = gm_l0;
    if (t2) {
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skdpq4t6o94o2));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(24));
        gm_l1 = t3;
    } else {
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(0));
        gm_l1 = t4;
    }
    gm_b16_8 t5 = gm_l1;
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_defer_body_avm7tau(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2, uint8_t a3) {
    struct gm_fr_defer_body_avm7tau F;
    void * gm_l0; /* fg */
    gm_th2lqbo77pvsc gm_l1; /* guard */
    gm_thervm8j23qet gm_l2; /* d */
    uint8_t gm_l3; /* unwinding */
    GM_STACK_CHECK(&F, 82u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_f_set_aside_scopes_ho12if8((void *)&F.l4, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t3 = gm_l0;
        uint8_t t4 = gm_l3;
        gm_b0_1 t5 = gm_f_defer_body_inner_v8ddaht((void *)NULL, cx, NULL, t3, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l1)), &(*(gm_thervm8j23qet *)((uint8_t *)a2)), t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t6 = gm_l0;
        gm_f_restore_scopes_f62992p(t6, &(*(gm_td7pkamr64l0c *)((uint8_t *)&F.l4)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_4q8glfd(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_4q8glfd F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jjmeosp(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jjmeosp F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_k21v01c(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_k21v01c F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lv6i4jh(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lv6i4jh F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 104)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_meisvg6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_meisvg6 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ppl3r45(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ppl3r45 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_rcd2jln F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_07kdnvi(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_07kdnvi F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_hkmvuvc(t12, t13, t14);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5evrfa7(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_5evrfa7 F;
    void * gm_l0; /* x */
    GM_STACK_CHECK(&F, 26u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_calls_probe_2ia3rhf *)((&F.h)->up))->l0));
    void * t3 = gm_l0;
    uint8_t t4 = gm_f_calls_probe_2ia3rhf((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t5 = (uint8_t)!t4;
    if (t5) {
        GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_calls_probe_2ia3rhf *)((&F.h)->up))->l5), ((uint8_t)0));
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
inline __attribute__((aligned(4))) uint64_t gm_f_fn_count_g9jh9qr(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_count_g9jh9qr F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t3) + 344) + 8));
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_fn_e5aba81(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_e5aba81 F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 19u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint8_t t3 = gm_f_has_fiber_with_53c3jik(t2);
    F.ret = t3;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_jjarlg6(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_jjarlg6 F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 36u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_collect_upvars_q0085da *)((&F.h)->up))->l0));
    uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_collect_upvars_q0085da *)((&F.h)->up))->l1));
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_collect_upvars_q0085da *)((&F.h)->up))->l3));
    gm_b0_1 t6 = gm_f_collect_upvars_q0085da((void *)NULL, cx, NULL, t2, t3, t4, t5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kssb0q2(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_kssb0q2 F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_collect_withs_fa29mk1 *)((&F.h)->up))->l1));
    gm_b0_1 t4 = gm_f_collect_withs_fa29mk1((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_v0t9sik(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_v0t9sik F;
    void * gm_l0; /* x */
    GM_STACK_CHECK(&F, 45u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint32_t t2 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_expr_call_depth_dliqc7u *)((&F.h)->up))->l2));
    uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)gm_cdmki9acqi9));
    uint8_t t4 = (t2 <= t3);
    if (t4) {
        uint32_t t5 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_expr_call_depth_dliqc7u *)((&F.h)->up))->l2));
        void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_expr_call_depth_dliqc7u *)((&F.h)->up))->l0));
        void * t7 = gm_l0;
        uint32_t t8 = gm_f_expr_call_depth_dliqc7u((void *)NULL, cx, NULL, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t9 = (t5 > t8 ? t5 : t8);
        GM_ST(uint32_t, ((uint8_t *)&((struct gm_fr_expr_call_depth_dliqc7u *)((&F.h)->up))->l2), t9);
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
inline __attribute__((aligned(4))) void gm_f_format_07f8le4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_07f8le4 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9r83c0uo3ifr));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplvflfleui9qjm), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_32n178h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_32n178h F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4pru1na1k33s));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(2));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplk34ojqiaho84), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_4hn970h(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_4hn970h F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_stf7lvoc7oe54));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(15));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_01lon97((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplmjmservvuqtp), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_4un19t8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_4un19t8 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdkba1tit6s52));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(16));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpla6sajpp07b9l), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_822rutp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_822rutp F;
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_snurnbocs9786));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(48));
    gm_tud7oovfvnf43 t3 = GM_LD(gm_tud7oovfvnf43, ((uint8_t *)a0));
    gm_f_format_fq216g1((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplfnm34ailh6k9), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_99gc605(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_99gc605 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_smhvmmegaur89));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_01lon97((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplnv6l3ecgreid), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_9pumrco(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_9pumrco F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4ce0jd3tdro4 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sal52ei1j638k));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(16));
    gm_f_format_lo2svpj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplc65rc46hqohp), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_cp0hre9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_cp0hre9 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sgt05f0dtajpv));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(17));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplptu6sgqf50o0), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_d6fj9tl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_d6fj9tl F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sc6pb1tb5tnnv));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(33));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplsjs5e78h34ng), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_dd6d4bt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_dd6d4bt F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9nl6jkck8c25));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl7eeir95ujdvn), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ddrb17v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_ddrb17v F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4ce0jd3tdro4 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqqaphhmqv39s));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_f_format_lo2svpj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplf4oacobin5qk), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_dsta2ql(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_dsta2ql F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sb2k9e71jsraf));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplfqr816g9de0k), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ghqt9ar(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_ghqt9ar F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4ce0jd3tdro4 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0lpuem1an6p4));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_f_format_lo2svpj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplsil8jb9ng0ao), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_gt1sds7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_gt1sds7 F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4ce0jd3tdro4 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_so2qoj4e0o0al));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
    gm_f_format_lo2svpj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpllaqikqjbb69k), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_hi89jlk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_hi89jlk F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_souosds7adra9));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(35));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpltev7cklsieaq), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_hs8tdgq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_hs8tdgq F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8fhmkhb94g4l));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpleie0q8nvjqch), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_kadoe0b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_kadoe0b F;
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s302tqgcs0oai));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(48));
    gm_t0uuhaljsf410 t3 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
    gm_f_format_8ghesn9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl7vtkjsp2pa8b), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ko1kpil(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ko1kpil F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s01fuch54uo3k));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl7pm1mtuteh6j), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_pu04c4a(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_pu04c4a F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sni0seheggmp0));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(29));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl8mdj3nc4lpjv), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_rmu2e6q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_rmu2e6q F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sb3o3ij0n2htb));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpleoabj1u2rb54), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_rtv6jsp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_rtv6jsp F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4ce0jd3tdro4 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_svmt0iljnadus));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_lo2svpj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplj2qf541fmi6f), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_u9etquj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_u9etquj F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s0uoh0jkm28kv));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(23));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_he6jnp0((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl086m1ajg247c), t2);
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
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_ag7vhtq(void * a0, gm_tcc1rq4n1avb2 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_ag7vhtq F;
    void * gm_l0; /* m */
    gm_tcc1rq4n1avb2 gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 122u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t4 = gm_f_find_efqiqfc(t2, t3);
    gm_l3 = t4;
    uint8_t t5 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
    if (t5) {
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
        gm_l2 = t6;
    } else {
        gm_tqs5p4n2tq19v t7;
        memset(&t7, 0, sizeof t7);
        F.ret = t7;
        goto gm_fret;
    }
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, ((uint8_t *)t8));
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t8) + 8));
    uint64_t t11 = gm_l2;
    if ((uintptr_t)t11 >= (uintptr_t)(t10)) gm_trap_index((uintptr_t)t11, t10, gm_srcbsc23is03ttm, 139, 12);
    uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t9) + (uintptr_t)t11 * 16) + 12));
    gm_tqs5p4n2tq19v t13;
    GM_ST(uint8_t, ((uint8_t *)&t13), t12);
    ((uint8_t *)&t13)[1] = 1;
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_map_cuh4fs9(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_cuh4fs9 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_k21v01c();
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_borrowed_qi4apdt(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_borrowed_qi4apdt F;
    void * gm_l0; /* fg */
    uint32_t gm_l1; /* l */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l3;
    {
        {
            void * t4 = gm_l0;
            uint32_t t5 = gm_l1;
            gm_tf5srh4mp4b3i t6 = gm_f_param_index_rp1nn6k(t4, t5);
            gm_l3 = t6;
            uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
            if (!t7) {
                goto gm_lb2;
            }
            uint64_t t8 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
            gm_l2 = t8;
            void * t9 = gm_l0;
            void * t10 = GM_LD(void *, (((uint8_t *)t9) + 272));
            uint64_t t11 = GM_LD(uint64_t, ((((uint8_t *)t9) + 272) + 8));
            uint64_t t12 = gm_l2;
            if ((uintptr_t)t12 >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)t12, t11, gm_srcmok80ho8hh01, 531, 40);
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)t10) + (uintptr_t)t12 * 1));
            F.ret = t13;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    F.ret = ((uint8_t)0);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_17uennd(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_unit_17uennd F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 43u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    uint8_t t6 = gm_f_is_unit_n83ttnb(t4, GM_CONV(gm_csguankko4lmhd, t5));
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_unit_n83ttnb F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* elems */
    GM_STACK_CHECK_SHALLOW(&F, 125u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_t53at80pfdus3 gm_l3;
    {
        {
            void * t4 = gm_l0;
            gm_t88rcucg1b7uk t5 = gm_l1;
            gm_f_kind_jcnt4hm((void *)&gm_l3, t4, GM_CONV(gm_csguankko4lmhd, t5));
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)12ULL));
            if (!t7) {
                goto gm_lb2;
            }
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
            gm_l2 = t8;
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t10 = ((t9 == ((uint64_t)0ULL)));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_label_710oev4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_label_710oev4 F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* name */
    GM_STACK_CHECK(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l2;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 128);
    gm_b16_8 t4 = gm_l1;
    gm_tnpifv20irnph t5;
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t4);
    gm_tftb1odc739t5 t6 = gm_f_write_f1cbljc(t3, t5);
    gm_l2 = t6;
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t7) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srcmok80ho8hh01, 507, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_ladder_36amdbn(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_ladder_36amdbn F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* target */
    gm_tf5srh4mp4b3i gm_l2; /* scope */
    GM_STACK_CHECK(&F, 152u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tj64ui6kruni6 gm_l3;
    void * t2 = gm_l0;
    gm_f_ladder_target_84n3aep((void *)&gm_l3, (void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
    gm_l1 = t3;
    gm_tf5srh4mp4b3i t4 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&gm_l3) + 16));
    gm_l2 = t4;
    void * t5 = gm_l0;
    gm_b16_8 t6 = gm_l1;
    gm_tf5srh4mp4b3i t7 = gm_l2;
    gm_b0_1 t8 = gm_f_jump_pvnkvme((void *)NULL, cx, NULL, t5, t6, t7);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_1p9d72n(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_1p9d72n F;
    void * gm_l0; /* fg */
    gm_tiilmfd16v3fc gm_l1; /* args */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skr89ndurop26));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(112));
    gm_b0_1 t4 = gm_f_linef_fbspl3h((void *)NULL, cx, NULL, t2, &(*(gm_tiilmfd16v3fc *)((uint8_t *)a1)), ((void *)&gm_tpl3ergg6mce23h), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_2n883h6(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1) {
    struct gm_fr_linef_2n883h6 F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_st1k7cra2pjic));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(70));
    gm_b0_1 t5 = gm_f_linef_7afcl2l((void *)NULL, cx, NULL, t2, t3, ((void *)&gm_tpl92h94vggr1gu), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_4plvds6(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1) {
    struct gm_fr_linef_4plvds6 F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sou1qqe3k350t));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(12));
    gm_b0_1 t5 = gm_f_linef_7afcl2l((void *)NULL, cx, NULL, t2, t3, ((void *)&gm_tpl6skdtj6qlkmd), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_7p5m9ll(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_7p5m9ll F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sj7gfkb5eknah));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(75));
    gm_b0_1 t4 = gm_f_linef_8uttu8h((void *)NULL, cx, NULL, t2, &(*(gm_t0uuhaljsf410 *)((uint8_t *)a1)), ((void *)&gm_tplicuqpjvsak60), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_aka1a8l(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_linef_aka1a8l F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* args */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_skmddjugniqo1));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(50));
    gm_b0_1 t5 = gm_f_linef_9u95itl((void *)NULL, cx, NULL, t2, t3, ((void *)&gm_tplikisll89mgrm), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_bi02v43(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_bi02v43 F;
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
    gm_b0_1 t4 = gm_f_linef_ao39327((void *)NULL, cx, NULL, t2, &(*(gm_tfeab279c16rr *)((uint8_t *)a1)), ((void *)&gm_tpl3bdcsso3qmi0), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_cs9kbvs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t19enj90vg8bd a1) {
    struct gm_fr_linef_cs9kbvs F;
    void * gm_l0; /* fg */
    gm_t19enj90vg8bd gm_l1; /* args */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t19enj90vg8bd t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s78l3ok4on4sg));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(35));
    gm_b0_1 t5 = gm_f_linef_sf7abhb((void *)NULL, cx, NULL, t2, t3, ((void *)&gm_tpl0cgce8migcnp), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_cuactnf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1) {
    struct gm_fr_linef_cuactnf F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_slv0l3penlcbn));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(58));
    gm_b0_1 t5 = gm_f_linef_7afcl2l((void *)NULL, cx, NULL, t2, t3, ((void *)&gm_tplilne6f8eq90i), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_eils77b(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_eils77b F;
    void * gm_l0; /* fg */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssmeaumlrdi7h));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_b0_1 t4 = gm_f_linef_slpvrc7((void *)NULL, cx, NULL, t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tplett0du8rhujl), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_md5jtn8(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_md5jtn8 F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sttopppkehb8k));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(19));
    gm_b0_1 t4 = gm_f_linef_c5qou5f((void *)NULL, cx, NULL, t2, &(*(gm_tq3m5qgvkpje8 *)((uint8_t *)a1)), ((void *)&gm_tplj16n1o03849r), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_mkfjg2u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1) {
    struct gm_fr_linef_mkfjg2u F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_smgdg1m3kad32));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(73));
    gm_b0_1 t5 = gm_f_linef_7afcl2l((void *)NULL, cx, NULL, t2, t3, ((void *)&gm_tplb9i7jfun8rvg), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_nvqsj6t(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_nvqsj6t F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_svghvlr5om8a6));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(28));
    gm_b0_1 t4 = gm_f_linef_c5qou5f((void *)NULL, cx, NULL, t2, &(*(gm_tq3m5qgvkpje8 *)((uint8_t *)a1)), ((void *)&gm_tplkj8cf8psv8p6), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_qqk2cu4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1) {
    struct gm_fr_linef_qqk2cu4 F;
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
    gm_b0_1 t5 = gm_f_linef_7afcl2l((void *)NULL, cx, NULL, t2, t3, ((void *)&gm_tpla1fscog446dv), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_uu8i6ue(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_uu8i6ue F;
    void * gm_l0; /* fg */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5ie9tn1qnf6v));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
    gm_b0_1 t4 = gm_f_linef_slpvrc7((void *)NULL, cx, NULL, t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tple6jpca094i2j), t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_lir_framed_6i9516j(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2) {
    struct gm_fr_lir_framed_6i9516j F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* body */
    GM_STACK_CHECK(&F, 68u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l3;
    uint8_t gm_l4;
    uint8_t gm_l5;
    void * t2 = gm_l2;
    uint8_t t3 = gm_f_has_fiber_with_53c3jik(t2);
    if (t3) {
        gm_l5 = ((uint8_t)1);
    } else {
        void * t4 = gm_l0;
        uint32_t t5 = gm_l1;
        uint8_t t6 = gm_f_outlines_2kvbvit((void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t6;
    }
    uint8_t t7 = gm_l5;
    if (t7) {
        gm_l4 = ((uint8_t)1);
    } else {
        void * t8 = gm_l0;
        uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t8) + 1136) + 1));
        gm_l4 = t9;
    }
    uint8_t t10 = gm_l4;
    if (t10) {
        gm_l3 = ((uint8_t)1);
    } else {
        void * t11 = gm_l0;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t11) + 1136) + 2));
        gm_l3 = t12;
    }
    uint8_t t13 = gm_l3;
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
inline __attribute__((aligned(4))) void gm_f_list_9ljroit(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_9ljroit F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_meisvg6();
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
inline __attribute__((aligned(4))) void gm_f_list_ns53od0(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ns53od0 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_93nt26c();
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
inline __attribute__((aligned(4))) void gm_f_list_rl2scuf(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_rl2scuf F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_4q8glfd();
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
inline __attribute__((aligned(4))) void gm_f_list_ubba23m(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ubba23m F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_lv6i4jh();
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_plain_cty_q9lhlci(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_plain_cty_q9lhlci F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* m */
    GM_STACK_CHECK(&F, 157u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_th2lqbo77pvsc gm_l3;
    {
        {
            void * t4 = gm_l0;
            gm_t88rcucg1b7uk t5 = gm_l1;
            gm_f_mirror_lcjegsf((void *)&gm_l3, (void *)NULL, cx, NULL, t4, t5);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t6 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
            if (!t6) {
                goto gm_lb2;
            }
            gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
            gm_l2 = t7;
            gm_b16_8 t8 = gm_l2;
            F.ret = t8;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t9 = gm_l0;
    gm_t88rcucg1b7uk t10 = gm_l1;
    gm_b16_8 t11 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t9, t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t11;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_7dfimu1(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_7dfimu1 F;
    void * gm_l0; /* l */
    gm_th2lqbo77pvsc gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_jjmeosp();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_bg5jjth(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_givp2as(void * a0, gm_cs7e9ppsd384bj a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_givp2as F;
    void * gm_l0; /* l */
    gm_tplpbqf3duvn3 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tplpbqf3duvn3, a1);
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
        gm_b16_8 t9 = gm_f_empty_slice_meisvg6();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_egu240o(t8, ((uint64_t)1ULL), t10);
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
    gm_tplpbqf3duvn3 t18 = gm_l1;
    GM_ST(gm_tplpbqf3duvn3, (((uint8_t *)t16) + (uintptr_t)t17 * 8), t18);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_bg5jjth(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_bg5jjth F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_egu240o(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_egu240o F;
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
inline __attribute__((aligned(4))) void gm_f_restore_scopes_f62992p(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_restore_scopes_f62992p F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK_SHALLOW(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        memmove((((uint8_t *)t2) + 184), ((uint8_t *)a1), 40);
    }
    {
        void * t3 = gm_l0;
        memmove((((uint8_t *)t3) + 312), (((uint8_t *)a1) + 40), 40);
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_set_aside_scopes_ho12if8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_set_aside_scopes_ho12if8 F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 200u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        gm_trqfvmjtt4sij t4 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t2) + 184));
        gm_trqfvmjtt4sij t5 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t3) + 312));
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1 + 0), t4);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1 + 40), t5);
    }
    {
        void * t6 = gm_l0;
        gm_trqfvmjtt4sij t7;
        gm_f_list_ubba23m((void *)&t7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t6) + 184), t7);
    }
    {
        void * t8 = gm_l0;
        gm_trqfvmjtt4sij t9;
        gm_f_list_ns53od0((void *)&t9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t8) + 312), t9);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 80);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unique_members_mrojmjf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1) {
    struct gm_fr_unique_members_mrojmjf F;
    gm_b16_8 gm_l0; /* names */
    void * gm_l1; /* taken */
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t2 = gm_l0;
    void * t3 = gm_l1;
    gm_b16_8 t4 = gm_f_unique_with_mffff7e((void *)NULL, cx, NULL, t2, t3, ((uint8_t)0));
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_2age34u F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 180u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3 = gm_f_empty_slice_rcd2jln();
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    gm_b16_8 t5 = gm_f_empty_slice_5nn7ph0();
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
    gm_t6j6smj7qoctu t7;
    GM_ST(gm_b16_8, ((uint8_t *)&t7 + 0), t4);
    GM_ST(gm_b16_8, ((uint8_t *)&t7 + 16), t6);
    gm_t53at80pfdus3 t8;
    GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t8 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t8 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)12ULL));
    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t8 + 8), t7);
    gm_t88rcucg1b7uk t9 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t2, &t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unit_8ekedsl(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_8ekedsl F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    void * t4 = GM_LD(void *, ((uint8_t *)t3));
    void * t5 = (void *)(((uint8_t *)t4) + 8);
    gm_t88rcucg1b7uk t6 = gm_f_unit_ty_vgceilu((void *)NULL, cx, NULL, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t7 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t2, t6);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_ty_vgceilu(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_ty_vgceilu F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unwind_call_4k8ulav(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_unwind_call_4k8ulav F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* call */
    GM_STACK_CHECK(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)t2) + 240));
    if (t3) {
        void * t4 = gm_l0;
        gm_b16_8 t5 = gm_l1;
        gm_tnpifv20irnph t6;
        GM_ST(gm_b16_8, ((uint8_t *)&t6 + 0), t5);
        gm_b0_1 t7 = gm_f_linef_qqk2cu4((void *)NULL, cx, NULL, t4, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    } else {
        void * t8 = gm_l0;
        gm_b16_8 t9 = gm_l1;
        gm_tnpifv20irnph t10;
        GM_ST(gm_b16_8, ((uint8_t *)&t10 + 0), t9);
        gm_b0_1 t11 = gm_f_linef_g6msml8((void *)NULL, cx, NULL, t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t12 = gm_l0;
        gm_b0_1 t13 = gm_f_unwind_ladder_a9ij1gj((void *)NULL, cx, NULL, t12);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_3c9chj4(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_3c9chj4 F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8cfqegc6giqu));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(52));
    gm_tftb1odc739t5 t4 = gm_f_write_2venbiq(t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tplf1r4lr6ofdql), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ajj3a37(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_ajj3a37 F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slg6o1a142s3f));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(43));
    gm_tftb1odc739t5 t4 = gm_f_write_6dciq50(t2, &(*(gm_tftksr4op114e *)((uint8_t *)a1)), ((void *)&gm_tplh7231rl9trm2), t3);
    F.ret = t4;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_dfbs2m5(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_dfbs2m5 F;
    void * gm_l0; /* l */
    gm_t3877pk7lv748 gm_l1; /* args */
    GM_STACK_CHECK(&F, 62u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sm3vri4hmq0fl));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(12));
    gm_tftb1odc739t5 t4 = gm_f_write_glpclq4(t2, &(*(gm_t3877pk7lv748 *)((uint8_t *)a1)), ((void *)&gm_tpl1vbgcviqausd), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_f1cbljc(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_f1cbljc F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sh1e27cf0k9if));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(5));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tpl8ptl55ago227), t4);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ih4qokb(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_ih4qokb F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_snt0968qgofbj));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(36));
    gm_tftb1odc739t5 t4 = gm_f_write_qb1f4ht(t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tpl6vfpiq020ehi), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ihqa4kk(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_ihqa4kk F;
    void * gm_l0; /* l */
    gm_tommf4j9upomc gm_l1; /* args */
    GM_STACK_CHECK(&F, 62u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0a1ag8clb46q));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(12));
    gm_tftb1odc739t5 t4 = gm_f_write_sa6ppdq(t2, &(*(gm_tommf4j9upomc *)((uint8_t *)a1)), ((void *)&gm_tplfft5kohcphn8), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ildv1bj(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_ildv1bj F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_st61cilmhqbek));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(12));
    gm_tftb1odc739t5 t4 = gm_f_write_isshs6q(t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tplkk98947i5b3i), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_j67vjku(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_j67vjku F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sua89kt0f2v9n));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(29));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplbnt69qpq91i8), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_l0vqbl8(void * a0, gm_t4ce0jd3tdro4 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_l0vqbl8 F;
    void * gm_l0; /* l */
    gm_t4ce0jd3tdro4 gm_l1; /* args */
    GM_STACK_CHECK(&F, 54u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t4ce0jd3tdro4 t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sk0qegfrnee00));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(3));
    gm_tftb1odc739t5 t5 = gm_f_write_hqo1u7h(t2, t3, ((void *)&gm_tpls8octb03drbp), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_m7rqe8h(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_m7rqe8h F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqas1r3k72n65));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(50));
    gm_tftb1odc739t5 t4 = gm_f_write_2venbiq(t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tplt0qsjg06adbb), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_n0vcum5(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_n0vcum5 F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s8ue7na9eegfa));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(6));
    gm_tftb1odc739t5 t5 = gm_f_write_vq1ifl4(t2, t3, ((void *)&gm_tplkodpfg3ik1kh), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_s0f8vaq(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_s0f8vaq F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s69crkid1kjkv));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(21));
    gm_tftb1odc739t5 t4 = gm_f_write_70cb4d8(t2, &(*(gm_tq3m5qgvkpje8 *)((uint8_t *)a1)), ((void *)&gm_tploc41q7b5gurn), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_s858s21(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_s858s21 F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s6btsj90mfdi5));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(34));
    gm_tftb1odc739t5 t4 = gm_f_write_0ie0tub(t2, &(*(gm_tcrdb1inj22os *)((uint8_t *)a1)), ((void *)&gm_tpl0uott3s3afvp), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_shh24f1(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_shh24f1 F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_spfojlg262423));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(12));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tpl8f1efhudc5d4), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_va1cfg2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_va1cfg2 F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8aupotg4okcv));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(102));
    gm_tftb1odc739t5 t4 = gm_f_write_2venbiq(t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tplt5jbb3iqgb9i), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_block_scope_e3cgusu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_c_abi_wrapper_33vbmt4(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2) {
    struct gm_fr_c_abi_wrapper_33vbmt4 F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* def */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    uint32_t gm_l8; /* p */
    gm_t88rcucg1b7uk gm_l9; /* t */
    gm_b16_8 gm_l10; /* call */
    gm_b16_8 gm_l11; /* ret */
    gm_b16_8 gm_l12; /* body */
    gm_b16_8 gm_l13; /* c */
    gm_b16_8 gm_l15; /* out_call */
    gm_b16_8 gm_l16; /* v */
    gm_b16_8 gm_l17; /* m */
    gm_b16_8 gm_l18; /* v */
    gm_b16_8 gm_l19; /* m */
    gm_b16_8 gm_l20; /* sig */
    GM_STACK_CHECK(&F, 2703u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tn68el8d14vku gm_l21;
    gm_tabb710d9mq2e gm_l22;
    gm_tftb1odc739t5 gm_l23;
    gm_tabb710d9mq2e gm_l24;
    gm_tftb1odc739t5 gm_l25;
    gm_tabb710d9mq2e gm_l26;
    gm_tftb1odc739t5 gm_l27;
    gm_tftb1odc739t5 gm_l28;
    gm_tabb710d9mq2e gm_l29;
    gm_tabb710d9mq2e gm_l30;
    gm_tabb710d9mq2e gm_l31;
    gm_tftb1odc739t5 gm_l32;
    gm_tftb1odc739t5 gm_l33;
    gm_tabb710d9mq2e gm_l34;
    gm_tabb710d9mq2e gm_l35;
    gm_th2lqbo77pvsc gm_l36;
    gm_tabb710d9mq2e gm_l37;
    gm_tabb710d9mq2e gm_l38;
    gm_th2lqbo77pvsc gm_l39;
    gm_tabb710d9mq2e gm_l40;
    gm_tabb710d9mq2e gm_l41;
    gm_tabb710d9mq2e gm_l42;
    gm_tabb710d9mq2e gm_l43;
    gm_tftb1odc739t5 gm_l44;
    gm_tftb1odc739t5 gm_l45;
    {
        gm_trqfvmjtt4sij t11;
        gm_f_list_epvducg((void *)&t11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l3 = t11;
        gm_trqfvmjtt4sij t12;
        gm_f_list_epvducg((void *)&t12, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l4 = t12;
        void * t13 = gm_l2;
        gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t13) + 32));
        gm_l5 = t14;
        gm_l6 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t15 = gm_l6;
                uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint8_t t17 = (t15 < t16);
                if (!t17) {
                    goto gm_lb1;
                }
                {
                    uint64_t t18 = gm_l6;
                    void * t19 = GM_LD(void *, ((uint8_t *)&gm_l5));
                    uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint64_t t21 = gm_l6;
                    uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)t19) + (uintptr_t)t21 * 4));
                    gm_tn68el8d14vku t23;
                    GM_ST(uint32_t, (uint8_t *)&t23 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t23 + 0), t18);
                    GM_ST(uint32_t, ((uint8_t *)&t23 + 8), t22);
                    gm_l21 = t23;
                    {
                        {
                            uint64_t t24 = GM_LD(uint64_t, ((uint8_t *)&gm_l21));
                            gm_l7 = t24;
                            uint32_t t25 = GM_LD(uint32_t, (((uint8_t *)&gm_l21) + 8));
                            gm_l8 = t25;
                            void * t26 = gm_l2;
                            void * t27 = GM_LD(void *, (((uint8_t *)t26) + 48));
                            uint64_t t28 = GM_LD(uint64_t, ((((uint8_t *)t26) + 48) + 8));
                            uint32_t t29 = gm_l8;
                            uint64_t t30 = (uint64_t)t29;
                            if ((uintptr_t)t30 >= (uintptr_t)(t28)) gm_trap_index((uintptr_t)t30, t28, gm_srcmok80ho8hh01, 370, 14);
                            gm_t88rcucg1b7uk t31 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t27) + (uintptr_t)t30 * 40) + 16));
                            gm_l9 = t31;
                            {
                                void * t32 = (void *)((uint8_t *)&F.l3);
                                void * t33 = gm_l0;
                                void * t34 = (void *)(((uint8_t *)t33) + 8);
                                gm_t88rcucg1b7uk t35 = gm_l9;
                                gm_b16_8 t36 = gm_f_plain_cty_q9lhlci((void *)NULL, cx, NULL, t34, t35);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint64_t t37 = gm_l7;
                                gm_t3877pk7lv748 t38;
                                GM_ST(gm_b16_8, ((uint8_t *)&t38 + 0), t36);
                                GM_ST(uint64_t, ((uint8_t *)&t38 + 16), t37);
                                gm_f_format_eq87866((void *)&gm_l22, (void *)NULL, cx, NULL, &t38);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t39 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                                if (!t39) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 8, gm_srcmok80ho8hh01, 371, 21);
                                gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
                                gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
                                gm_tftb1odc739t5 t42 = gm_f_push_3ef746b(t32, t41);
                                gm_l23 = t42;
                            }
                            uint8_t t43 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                            if (!t43) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_srcmok80ho8hh01, 371, 9);
                            void * t44 = gm_l0;
                            void * t45 = (void *)(((uint8_t *)t44) + 8);
                            gm_t88rcucg1b7uk t46 = gm_l9;
                            uint8_t t47 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t45, t46);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            if (t47) {
                                void * t48 = (void *)((uint8_t *)&F.l4);
                                uint64_t t49 = gm_l7;
                                gm_t4ce0jd3tdro4 t50;
                                GM_ST(uint64_t, ((uint8_t *)&t50 + 0), t49);
                                gm_f_format_ddrb17v((void *)&gm_l24, (void *)NULL, cx, NULL, t50);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t51 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                                if (!t51) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 8, gm_srcmok80ho8hh01, 372, 39);
                                gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 8));
                                gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
                                gm_tftb1odc739t5 t54 = gm_f_push_3ef746b(t48, t53);
                                gm_l25 = t54;
                                uint8_t t55 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                                if (!t55) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_srcmok80ho8hh01, 372, 29);
                            } else {
                                void * t56 = (void *)((uint8_t *)&F.l4);
                                uint64_t t57 = gm_l7;
                                gm_t4ce0jd3tdro4 t58;
                                GM_ST(uint64_t, ((uint8_t *)&t58 + 0), t57);
                                gm_f_format_l05am3t((void *)&gm_l26, (void *)NULL, cx, NULL, t58);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t59 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                                if (!t59) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 8, gm_srcmok80ho8hh01, 372, 83);
                                gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l26) + 8));
                                gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
                                gm_tftb1odc739t5 t62 = gm_f_push_3ef746b(t56, t61);
                                gm_l27 = t62;
                                uint8_t t63 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                                if (!t63) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 1, gm_srcmok80ho8hh01, 372, 73);
                            }
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 369, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t64 = gm_l6;
                uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
                uint64_t t67 = r65;
                gm_l6 = t67;
            }
        }
gm_lb1:;
    }
    {
        uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
        uint8_t t69 = ((t68 == ((uint64_t)0ULL)));
        if (t69) {
            void * t70 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t71;
            GM_ST(void *, (uint8_t *)&t71, (void *)(gm_sts5j6f5pkov0));
            GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(4));
            gm_tftb1odc739t5 t72 = gm_f_push_3ef746b(t70, t71);
            gm_l28 = t72;
            uint8_t t73 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
            if (!t73) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 1, gm_srcmok80ho8hh01, 374, 32);
        }
        void * t74 = gm_l0;
        uint32_t t75 = gm_l1;
        gm_b16_8 t76 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t74, t75);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
        gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
        gm_b16_8 t79;
        GM_ST(void *, (uint8_t *)&t79, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(2));
        gm_f_join_sfknljb((void *)&gm_l29, (void *)NULL, cx, NULL, t78, t79);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t80 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
        if (!t80) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 8, gm_srcmok80ho8hh01, 375, 48);
        gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l29) + 8));
        gm_t6j6smj7qoctu t82;
        GM_ST(gm_b16_8, ((uint8_t *)&t82 + 0), t76);
        GM_ST(gm_b16_8, ((uint8_t *)&t82 + 16), t81);
        gm_f_format_99gc605((void *)&gm_l30, (void *)NULL, cx, NULL, &t82);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t83 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
        if (!t83) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 8, gm_srcmok80ho8hh01, 375, 13);
        gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 8));
        gm_l10 = t84;
        gm_b16_8 t85;
        GM_ST(void *, (uint8_t *)&t85, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(0));
        gm_l11 = t85;
        gm_b16_8 t86;
        GM_ST(void *, (uint8_t *)&t86, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(0));
        gm_l12 = t86;
        void * t87 = gm_l0;
        void * t88 = (void *)(((uint8_t *)t87) + 8);
        void * t89 = gm_l2;
        gm_t88rcucg1b7uk t90 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t89) + 64));
        uint8_t t91 = gm_f_is_unit_17uennd(t88, GM_CONV(gm_csguankko4lmhd, t90));
        if (t91) {
            gm_b16_8 t92;
            GM_ST(void *, (uint8_t *)&t92, (void *)(gm_sts5j6f5pkov0));
            GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(4));
            gm_l11 = t92;
            gm_b16_8 t93 = gm_l10;
            gm_tnpifv20irnph t94;
            GM_ST(gm_b16_8, ((uint8_t *)&t94 + 0), t93);
            gm_f_format_lsmcccn((void *)&gm_l31, (void *)NULL, cx, NULL, t94);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t95 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
            if (!t95) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 8, gm_srcmok80ho8hh01, 380, 16);
            gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l31) + 8));
            gm_b16_8 t97 = GM_LD(gm_b16_8, ((uint8_t *)&t96));
            gm_l12 = t97;
        } else {
            void * t98 = gm_l0;
            void * t99 = (void *)(((uint8_t *)t98) + 8);
            void * t100 = gm_l2;
            gm_t88rcucg1b7uk t101 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t100) + 64));
            uint8_t t102 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t99, t101);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (t102) {
                void * t103 = gm_l0;
                void * t104 = (void *)(((uint8_t *)t103) + 8);
                void * t105 = gm_l2;
                gm_t88rcucg1b7uk t106 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t105) + 64));
                gm_b16_8 t107 = gm_f_plain_cty_q9lhlci((void *)NULL, cx, NULL, t104, t106);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l11 = t107;
                void * t108 = gm_l0;
                void * t109 = (void *)(((uint8_t *)t108) + 8);
                void * t110 = gm_l2;
                gm_t88rcucg1b7uk t111 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t110) + 64));
                gm_b16_8 t112 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t109, t111);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l13 = t112;
                {
                    gm_trqfvmjtt4sij t113;
                    gm_f_list_epvducg((void *)&t113, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l14 = t113;
                }
                void * t114 = (void *)((uint8_t *)&F.l14);
                gm_b16_8 t115;
                GM_ST(void *, (uint8_t *)&t115, (void *)(gm_s2254b9hkee3b));
                GM_ST(uintptr_t, (uint8_t *)&t115 + 8, (uintptr_t)(5));
                gm_tftb1odc739t5 t116 = gm_f_push_3ef746b(t114, t115);
                gm_l32 = t116;
                uint8_t t117 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                if (!t117) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 1, gm_srcmok80ho8hh01, 385, 9);
                void * t118 = (void *)((uint8_t *)&F.l14);
                gm_b16_8 t119 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
                gm_b16_8 t120 = GM_LD(gm_b16_8, ((uint8_t *)&t119));
                gm_tftb1odc739t5 t121 = gm_f_extend_vogsd72(t118, t120);
                gm_l33 = t121;
                uint8_t t122 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                if (!t122) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 1, gm_srcmok80ho8hh01, 386, 9);
                {
                    void * t123 = gm_l0;
                    uint32_t t124 = gm_l1;
                    gm_b16_8 t125 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t123, t124);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
                    gm_b16_8 t127 = GM_LD(gm_b16_8, ((uint8_t *)&t126));
                    gm_b16_8 t128;
                    GM_ST(void *, (uint8_t *)&t128, (void *)(gm_sg5gdnrith2i6));
                    GM_ST(uintptr_t, (uint8_t *)&t128 + 8, (uintptr_t)(2));
                    gm_f_join_sfknljb((void *)&gm_l34, (void *)NULL, cx, NULL, t127, t128);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t129 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                    if (!t129) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 8, gm_srcmok80ho8hh01, 387, 56);
                    gm_b16_8 t130 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l34) + 8));
                    gm_t6j6smj7qoctu t131;
                    GM_ST(gm_b16_8, ((uint8_t *)&t131 + 0), t125);
                    GM_ST(gm_b16_8, ((uint8_t *)&t131 + 16), t130);
                    gm_f_format_99gc605((void *)&gm_l35, (void *)NULL, cx, NULL, &t131);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t132 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
                if (!t132) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 8, gm_srcmok80ho8hh01, 387, 21);
                gm_b16_8 t133 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l35) + 8));
                gm_l15 = t133;
                gm_b16_8 t134;
                GM_ST(void *, (uint8_t *)&t134, (void *)(gm_ss03naco6s5l7));
                GM_ST(uintptr_t, (uint8_t *)&t134 + 8, (uintptr_t)(4));
                gm_l16 = t134;
                {
                    {
                        {
                            void * t135 = gm_l0;
                            void * t136 = (void *)(((uint8_t *)t135) + 8);
                            void * t137 = gm_l2;
                            gm_t88rcucg1b7uk t138 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t137) + 64));
                            gm_f_mirror_lcjegsf((void *)&gm_l36, (void *)NULL, cx, NULL, t136, t138);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t139 = (uint8_t)((((uint8_t *)&gm_l36))[16] != 0);
                        if (!t139) {
                            goto gm_lb7;
                        }
                        gm_b16_8 t140 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l36));
                        gm_l17 = t140;
                        {
                            gm_b16_8 t141 = gm_l17;
                            gm_tnpifv20irnph t142;
                            GM_ST(gm_b16_8, ((uint8_t *)&t142 + 0), t141);
                            gm_f_format_cp0hre9((void *)&gm_l37, (void *)NULL, cx, NULL, t142);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t143 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
                        if (!t143) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 8, gm_srcmok80ho8hh01, 389, 44);
                        gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l37) + 8));
                        gm_b16_8 t145 = GM_LD(gm_b16_8, ((uint8_t *)&t144));
                        gm_l16 = t145;
                        goto gm_lb6;
                    }
gm_lb7:;
                }
gm_lb6:;
                {
                    gm_b16_8 t146 = gm_l13;
                    gm_b16_8 t147 = gm_l15;
                    gm_b16_8 t148 = gm_l16;
                    gm_tfeab279c16rr t149;
                    GM_ST(gm_b16_8, ((uint8_t *)&t149 + 0), t146);
                    GM_ST(gm_b16_8, ((uint8_t *)&t149 + 16), t147);
                    GM_ST(gm_b16_8, ((uint8_t *)&t149 + 32), t148);
                    gm_f_format_u9etquj((void *)&gm_l38, (void *)NULL, cx, NULL, &t149);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t150 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                if (!t150) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 8, gm_srcmok80ho8hh01, 390, 16);
                gm_b16_8 t151 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l38) + 8));
                gm_b16_8 t152 = GM_LD(gm_b16_8, ((uint8_t *)&t151));
                gm_l12 = t152;
            } else {
                void * t153 = gm_l0;
                void * t154 = (void *)(((uint8_t *)t153) + 8);
                void * t155 = gm_l2;
                gm_t88rcucg1b7uk t156 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t155) + 64));
                gm_b16_8 t157 = gm_f_plain_cty_q9lhlci((void *)NULL, cx, NULL, t154, t156);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l11 = t157;
                gm_b16_8 t158 = gm_l10;
                gm_l18 = t158;
                {
                    {
                        void * t159 = gm_l0;
                        void * t160 = (void *)(((uint8_t *)t159) + 8);
                        void * t161 = gm_l2;
                        gm_t88rcucg1b7uk t162 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t161) + 64));
                        gm_f_mirror_lcjegsf((void *)&gm_l39, (void *)NULL, cx, NULL, t160, t162);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t163 = (uint8_t)((((uint8_t *)&gm_l39))[16] != 0);
                        if (!t163) {
                            goto gm_lb9;
                        }
                        gm_b16_8 t164 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l39));
                        gm_l19 = t164;
                        void * t165 = gm_l0;
                        void * t166 = (void *)(((uint8_t *)t165) + 8);
                        void * t167 = gm_l2;
                        gm_t88rcucg1b7uk t168 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t167) + 64));
                        uint8_t t169 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t166, t168);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (t169) {
                            {
                                gm_b16_8 t170 = gm_l19;
                                gm_b16_8 t171 = gm_l10;
                                gm_t6j6smj7qoctu t172;
                                GM_ST(gm_b16_8, ((uint8_t *)&t172 + 0), t170);
                                GM_ST(gm_b16_8, ((uint8_t *)&t172 + 16), t171);
                                gm_f_format_4hn970h((void *)&gm_l40, (void *)NULL, cx, NULL, &t172);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            uint8_t t173 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
                            if (!t173) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l40) + 8, gm_srcmok80ho8hh01, 395, 43);
                            gm_b16_8 t174 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l40) + 8));
                            gm_l18 = t174;
                        }
                        goto gm_lb8;
                    }
gm_lb9:;
                }
gm_lb8:;
                gm_b16_8 t175 = gm_l18;
                gm_tnpifv20irnph t176;
                GM_ST(gm_b16_8, ((uint8_t *)&t176 + 0), t175);
                gm_f_format_ko1kpil((void *)&gm_l41, (void *)NULL, cx, NULL, t176);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t177 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                if (!t177) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 8, gm_srcmok80ho8hh01, 397, 16);
                gm_b16_8 t178 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l41) + 8));
                gm_b16_8 t179 = GM_LD(gm_b16_8, ((uint8_t *)&t178));
                gm_l12 = t179;
            }
        }
    }
    {
        void * t180 = gm_l2;
        uint8_t t181 = GM_LD(uint8_t, (((uint8_t *)t180) + 144));
        gm_b16_8 t182 = gm_f_conv_attr_m0b89pc(t181);
        gm_b16_8 t183 = gm_l11;
        void * t184 = gm_l0;
        uint32_t t185 = gm_l1;
        gm_b16_8 t186 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t184, t185);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t187 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
        gm_b16_8 t188 = GM_LD(gm_b16_8, ((uint8_t *)&t187));
        gm_b16_8 t189;
        GM_ST(void *, (uint8_t *)&t189, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t189 + 8, (uintptr_t)(2));
        gm_f_join_sfknljb((void *)&gm_l42, (void *)NULL, cx, NULL, t188, t189);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t190 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
        if (!t190) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l42) + 8, gm_srcmok80ho8hh01, 399, 116);
        gm_b16_8 t191 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l42) + 8));
        gm_t0uuhaljsf410 t192;
        GM_ST(gm_b16_8, ((uint8_t *)&t192 + 0), t182);
        GM_ST(gm_b16_8, ((uint8_t *)&t192 + 16), t183);
        GM_ST(gm_b16_8, ((uint8_t *)&t192 + 32), t186);
        GM_ST(gm_b16_8, ((uint8_t *)&t192 + 48), t191);
        gm_f_format_kadoe0b((void *)&gm_l43, (void *)NULL, cx, NULL, &t192);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t193 = (uint8_t)!((((uint8_t *)&gm_l43))[0] != 0);
        if (!t193) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l43) + 8, gm_srcmok80ho8hh01, 399, 12);
        gm_b16_8 t194 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l43) + 8));
        gm_l20 = t194;
        void * t195 = gm_l0;
        void * t196 = (void *)(((uint8_t *)t195) + 1944);
        gm_b16_8 t197 = gm_l20;
        gm_tnpifv20irnph t198;
        GM_ST(gm_b16_8, ((uint8_t *)&t198 + 0), t197);
        gm_tftb1odc739t5 t199 = gm_f_write_i2vvij8(t196, t198);
        gm_l44 = t199;
        uint8_t t200 = (uint8_t)!((((uint8_t *)&gm_l44))[0] != 0);
        if (!t200) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l44) + 1, gm_srcmok80ho8hh01, 400, 5);
        void * t201 = gm_l0;
        void * t202 = (void *)(((uint8_t *)t201) + 1984);
        gm_b16_8 t203 = gm_l20;
        gm_b16_8 t204 = gm_l12;
        gm_t6j6smj7qoctu t205;
        GM_ST(gm_b16_8, ((uint8_t *)&t205 + 0), t203);
        GM_ST(gm_b16_8, ((uint8_t *)&t205 + 16), t204);
        gm_tftb1odc739t5 t206 = gm_f_write_ildv1bj(t202, &t205);
        gm_l45 = t206;
        uint8_t t207 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
        if (!t207) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l45) + 1, gm_srcmok80ho8hh01, 401, 5);
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
__attribute__((aligned(4))) uint32_t gm_f_call_depth_4m6ap7n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_call_depth_4m6ap7n F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    uint64_t gm_l2; /* i */
    uint8_t gm_l3; /* known */
    uint32_t gm_l4; /* depth */
    void * gm_l5; /* def */
    void * gm_l6; /* b */
    GM_STACK_CHECK(&F, 326u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l7;
    uint8_t gm_l8;
    uint8_t gm_l9;
    uint32_t t10 = gm_l1;
    uint64_t t11 = (uint64_t)t10;
    gm_l2 = t11;
    {
        for (;;) {
            void * t12 = gm_l0;
            uint64_t t13 = GM_LD(uint64_t, ((((uint8_t *)t12) + 3040) + 8));
            uint64_t t14 = gm_l2;
            uint8_t t15 = (t13 <= t14);
            if (!t15) {
                goto gm_lb1;
            }
            {
                void * t16 = gm_l0;
                void * t17 = (void *)(((uint8_t *)t16) + 3040);
                gm_tftb1odc739t5 t18 = gm_f_push_t83rg2m(t17, ((uint8_t)0ULL));
                gm_l7 = t18;
                uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srcmok80ho8hh01, 306, 41);
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, (((uint8_t *)t20) + 3040));
    uint64_t t22 = GM_LD(uint64_t, ((((uint8_t *)t20) + 3040) + 8));
    uint64_t t23 = gm_l2;
    if ((uintptr_t)t23 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t23, t22, gm_srcmok80ho8hh01, 307, 14);
    uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)t21) + (uintptr_t)t23 * 1));
    gm_l3 = t24;
    uint8_t t25 = gm_l3;
    uint8_t t26 = ((t25 == ((uint8_t)1ULL)));
    if (t26) {
        uint8_t t27 = GM_LD(uint8_t, ((uint8_t *)gm_c71idar1l1k));
        uint32_t t28 = (uint32_t)t27;
        F.ret = t28;
        goto gm_fret;
    }
    uint8_t t29 = gm_l3;
    uint8_t t30 = (t29 > ((uint8_t)1ULL));
    if (t30) {
        uint8_t t31 = gm_l3;
        uint32_t t32 = (uint32_t)t31;
        uint32_t r33; bool o34 = __builtin_sub_overflow(t32, ((uint32_t)2ULL), &r33);
        if (o34) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 310, 27);
        uint32_t t35 = r33;
        F.ret = t35;
        goto gm_fret;
    }
    void * t36 = gm_l0;
    void * t37 = GM_LD(void *, (((uint8_t *)t36) + 3040));
    uint64_t t38 = GM_LD(uint64_t, ((((uint8_t *)t36) + 3040) + 8));
    uint64_t t39 = gm_l2;
    if ((uintptr_t)t39 >= (uintptr_t)(t38)) gm_trap_index((uintptr_t)t39, t38, gm_srcmok80ho8hh01, 311, 5);
    GM_ST(uint8_t, (((uint8_t *)t37) + (uintptr_t)t39 * 1), ((uint8_t)1ULL));
    uint8_t t40 = GM_LD(uint8_t, ((uint8_t *)gm_c71idar1l1k));
    uint32_t t41 = (uint32_t)t40;
    gm_l4 = t41;
    void * t42 = gm_l0;
    void * t43 = GM_LD(void *, ((uint8_t *)t42));
    uint32_t t44 = gm_l1;
    void * t45 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t43, t44);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t45;
    {
        {
            void * t46 = gm_l5;
            uint8_t t47 = (uint8_t)(GM_LD(void *, (((uint8_t *)t46) + 152)) != 0);
            if (!t47) {
                goto gm_lb5;
            }
            void * t48 = GM_LD(void *, (((uint8_t *)t46) + 152));
            gm_l6 = t48;
            void * t49 = gm_l5;
            {
                {
                    uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)t49) + 96) + 0));
                    uint8_t t51 = (uint8_t)(t50 == ((uint8_t)3ULL));
                    if (!t51) {
                        goto gm_lb7;
                    }
                    gm_l8 = ((uint8_t)0);
                    goto gm_lb6;
                }
gm_lb7:;
                {
                    gm_l8 = ((uint8_t)1);
                    goto gm_lb6;
                }
gm_lb8:;
                gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 314, 26);
            }
gm_lb6:;
            uint8_t t52 = gm_l8;
            if (!t52) {
                goto gm_lb5;
            }
            void * t53 = gm_l0;
            void * t54 = gm_l6;
            uint32_t t55 = gm_f_expr_call_depth_dliqc7u((void *)NULL, cx, NULL, t53, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t55;
            goto gm_lb4;
        }
gm_lb5:;
    }
gm_lb4:;
    void * t56 = gm_l0;
    void * t57 = GM_LD(void *, (((uint8_t *)t56) + 3040));
    uint64_t t58 = GM_LD(uint64_t, ((((uint8_t *)t56) + 3040) + 8));
    uint64_t t59 = gm_l2;
    if ((uintptr_t)t59 >= (uintptr_t)(t58)) gm_trap_index((uintptr_t)t59, t58, gm_srcmok80ho8hh01, 315, 5);
    uint32_t t60 = gm_l4;
    uint32_t t61 = GM_LD(uint32_t, ((uint8_t *)gm_cdmki9acqi9));
    uint8_t t62 = (t60 > t61);
    if (t62) {
        uint8_t t63 = GM_LD(uint8_t, ((uint8_t *)gm_c71idar1l1k));
        gm_l9 = t63;
    } else {
        uint32_t t64 = gm_l4;
        uint8_t t65 = (uint8_t)t64;
        uint8_t r66; bool o67 = __builtin_add_overflow(t65, ((uint8_t)2ULL), &r66);
        if (o67) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 315, 79);
        uint8_t t68 = r66;
        gm_l9 = t68;
    }
    uint8_t t69 = gm_l9;
    GM_ST(uint8_t, (((uint8_t *)t57) + (uintptr_t)t59 * 1), t69);
    uint32_t t70 = gm_l4;
    F.ret = t70;
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
inline __attribute__((aligned(4))) uint8_t gm_f_calls_probe_2ia3rhf(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_calls_probe_2ia3rhf F;
    void * gm_l1; /* e */
    uint32_t gm_l3; /* f */
    uint8_t gm_l4; /* x */
    GM_STACK_CHECK(&F, 101u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    void * t15 = gm_l1;
    {
        {
            uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t15) + 8) + 0));
            uint8_t t17 = (uint8_t)(t16 == ((uint8_t)34ULL));
            if (!t17) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l2), ((((uint8_t *)t15) + 8) + 8), 40);
            {
                {
                    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                    uint8_t t19 = (uint8_t)(t18 == ((uint8_t)0ULL));
                    if (!t19) {
                        goto gm_lb4;
                    }
                    uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&F.l2) + 8));
                    gm_l3 = t20;
                    {
                        {
                            void * t21 = F.l0;
                            void * t22 = GM_LD(void *, ((uint8_t *)t21));
                            uint32_t t23 = gm_l3;
                            void * t24 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t22, t23);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t25 = (uint8_t)((((((uint8_t *)t24) + 145) + 1))[1] != 0);
                            if (!t25) {
                                goto gm_lb6;
                            }
                            uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t24) + 145) + 1));
                            gm_l4 = t26;
                            uint8_t t27 = gm_l4;
                            if (!t27) {
                                goto gm_lb6;
                            }
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
gm_lb6:;
                    }
gm_lb5:;
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
gm_lb7:;
                gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 340, 29);
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
        {
            {
                {
                    uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t15) + 8) + 0));
                    uint8_t t29 = (uint8_t)(t28 == ((uint8_t)55ULL));
                    if (!t29) {
                        goto gm_lb10;
                    }
                    goto gm_lb9;
                }
gm_lb10:;
                {
                    uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t15) + 8) + 0));
                    uint8_t t31 = (uint8_t)(t30 == ((uint8_t)53ULL));
                    if (!t31) {
                        goto gm_lb11;
                    }
                    goto gm_lb9;
                }
gm_lb11:;
                {
                    uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)t15) + 8) + 0));
                    uint8_t t33 = (uint8_t)(t32 == ((uint8_t)59ULL));
                    if (!t33) {
                        goto gm_lb12;
                    }
                    goto gm_lb9;
                }
gm_lb12:;
                goto gm_lb8;
            }
gm_lb9:;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb8:;
        {
            goto gm_lb1;
        }
gm_lb13:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 339, 5);
    }
gm_lb1:;
    F.l5 = ((uint8_t)1);
    void * t34 = gm_l1;
    gm_closure t35 = ((gm_closure){(void *)gm_f_fn_5evrfa7, (void *)&F.h, cx});
    gm_f_for_each_child_fm49s73(t34, t35);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t36 = F.l5;
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
inline __attribute__((aligned(4))) void gm_f_captured_locals_glsvloj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_captured_locals_glsvloj F;
    void * gm_l0; /* g */
    uint64_t gm_l3; /* n */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    void * gm_l7; /* d */
    void * gm_l8; /* b */
    GM_STACK_CHECK(&F, 419u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            {
                void * t11 = gm_l0;
                uint8_t t12 = (uint8_t)(((((uint8_t *)t11) + 2896))[56] != 0);
                if (!t12) {
                    goto gm_lb2;
                }
                memmove(((uint8_t *)&F.l1), (((uint8_t *)t11) + 2896), 56);
            }
            {
                memmove(gm_out, ((uint8_t *)&F.l1), 56);
                goto gm_fdone;
            }
        }
gm_lb2:;
    }
gm_lb1:;
    {
        gm_t4c7ipjj9s3fp t13;
        gm_f_hash_map_t9h7o8p((void *)&t13, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t13;
    }
    void * t14 = gm_l0;
    void * t15 = GM_LD(void *, ((uint8_t *)t14));
    uint64_t t16 = gm_f_fn_count_g9jh9qr(t15);
    gm_l3 = t16;
    gm_l5 = ((uint64_t)0ULL);
    uint64_t t17 = gm_l3;
    gm_l4 = t17;
    {
        for (;;) {
            uint64_t t18 = gm_l5;
            uint64_t t19 = gm_l4;
            uint8_t t20 = (t18 < t19);
            if (!t20) {
                goto gm_lb3;
            }
            {
                {
                    {
                        uint64_t t21 = gm_l5;
                        gm_l6 = t21;
                        void * t22 = gm_l0;
                        void * t23 = GM_LD(void *, ((uint8_t *)t22));
                        uint64_t t24 = gm_l6;
                        uint32_t t25 = (uint32_t)t24;
                        void * t26 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t23, t25);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l7 = t26;
                        {
                            {
                                void * t27 = gm_l7;
                                uint8_t t28 = (uint8_t)(GM_LD(void *, (((uint8_t *)t27) + 152)) != 0);
                                if (!t28) {
                                    goto gm_lb9;
                                }
                                void * t29 = GM_LD(void *, (((uint8_t *)t27) + 152));
                                gm_l8 = t29;
                                void * t30 = gm_l0;
                                void * t31 = GM_LD(void *, ((uint8_t *)t30));
                                uint64_t t32 = gm_l6;
                                uint32_t t33 = (uint32_t)t32;
                                void * t34 = gm_l8;
                                void * t35 = (void *)((uint8_t *)&F.l2);
                                gm_b0_1 t36 = gm_f_collect_upvars_q0085da((void *)NULL, cx, NULL, t31, t33, t34, t35);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb8;
                            }
gm_lb9:;
                        }
gm_lb8:;
                        goto gm_lb6;
                    }
gm_lb7:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 833, 5);
                }
gm_lb6:;
            }
gm_lb5:;
            uint64_t t37 = gm_l5;
            uint64_t r38; bool o39 = __builtin_add_overflow(t37, ((uint64_t)1ULL), &r38);
            uint64_t t40 = r38;
            gm_l5 = t40;
        }
    }
gm_lb3:;
    {
        void * t41 = gm_l0;
        gm_t4c7ipjj9s3fp t42 = GM_LD(gm_t4c7ipjj9s3fp, ((uint8_t *)&F.l2));
        gm_tuutj662vpgtg t43;
        GM_ST(uint8_t, (uint8_t *)&t43 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&t43 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&t43 + 60, 0);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t43), t42);
        ((uint8_t *)&t43)[56] = 1;
        GM_ST(gm_tuutj662vpgtg, (((uint8_t *)t41) + 2896), t43);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l2), 56);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_check_unwind_2hji4bl(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_collect_upvars_q0085da(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, void * a3) {
    struct gm_fr_collect_upvars_q0085da F;
    void * gm_l2; /* e */
    uint32_t gm_l4; /* levels */
    uint32_t gm_l5; /* local */
    uint32_t gm_l6; /* owner */
    uint32_t gm_l7; /* <tmp> */
    uint32_t gm_l8; /* <tmp> */
    uint32_t gm_l9; /* p */
    GM_STACK_CHECK(&F, 165u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    gm_l2 = a2;
    F.l3 = a3;
    gm_tftb1odc739t5 gm_l10;
    {
        {
            void * t11 = gm_l2;
            uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t11) + 8) + 0));
            uint8_t t13 = (uint8_t)(t12 == ((uint8_t)16ULL));
            if (!t13) {
                goto gm_lb2;
            }
            uint32_t t14 = GM_LD(uint32_t, ((((uint8_t *)t11) + 8) + 8));
            gm_l4 = t14;
            uint32_t t15 = GM_LD(uint32_t, (((((uint8_t *)t11) + 8) + 8) + 4));
            gm_l5 = t15;
            uint32_t t16 = F.l1;
            gm_l6 = t16;
            gm_l8 = ((uint32_t)0ULL);
            uint32_t t17 = gm_l4;
            gm_l7 = t17;
            {
                for (;;) {
                    uint32_t t19 = gm_l8;
                    uint32_t t20 = gm_l7;
                    uint8_t t21 = (t19 < t20);
                    if (!t21) {
                        goto gm_lb3;
                    }
                    {
                        {
                            {
                                {
                                    {
                                        void * t22 = F.l0;
                                        uint32_t t23 = gm_l6;
                                        void * t24 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t22, t23);
                                        if (gm_unw.kind) {
                                            goto gm_ll18;
                                        }
                                        uint8_t t25 = (uint8_t)(((((uint8_t *)t24) + 88))[4] != 0);
                                        if (!t25) {
                                            goto gm_lb9;
                                        }
                                        uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)t24) + 88));
                                        gm_l9 = t26;
                                        uint32_t t27 = gm_l9;
                                        gm_l6 = t27;
                                        goto gm_lb8;
                                    }
gm_lb9:;
                                }
gm_lb8:;
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 858, 9);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint32_t t28 = gm_l8;
                    uint32_t r29; bool o30 = __builtin_add_overflow(t28, ((uint32_t)1ULL), &r29);
                    uint32_t t31 = r29;
                    gm_l8 = t31;
                }
            }
            goto gm_ls32;
gm_ll18:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb3; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb5; }
            }
            goto gm_lad;
gm_ls32:;
gm_lb3:;
            void * t33 = F.l3;
            uint32_t t34 = gm_l6;
            uint32_t t35 = gm_l5;
            gm_tcc1rq4n1avb2 t36;
            GM_ST(uint32_t, ((uint8_t *)&t36 + 0), t34);
            GM_ST(uint32_t, ((uint8_t *)&t36 + 4), t35);
            gm_tftb1odc739t5 t37 = gm_f_put_3g1tdpr(t33, t36, ((uint8_t)1));
            gm_l10 = t37;
            uint8_t t38 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
            if (!t38) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srcmok80ho8hh01, 861, 9);
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t39 = gm_l2;
    gm_closure t40 = ((gm_closure){(void *)gm_f_fn_jjarlg6, (void *)&F.h, cx});
    gm_f_for_each_child_fm49s73(t39, t40);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_collect_withs_fa29mk1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_collect_withs_fa29mk1 F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    gm_tftb1odc739t5 gm_l3;
    {
        {
            void * t4 = gm_l0;
            uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)59ULL));
            if (!t6) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l2), ((((uint8_t *)t4) + 8) + 8), 40);
            void * t7 = F.l1;
            uint32_t t8 = GM_LD(uint32_t, ((uint8_t *)&F.l2));
            void * t9 = gm_l0;
            gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t9));
            gm_tplpbqf3duvn3 t11;
            GM_ST(uint32_t, ((uint8_t *)&t11 + 0), t8);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t11 + 4), t10);
            gm_tftb1odc739t5 t12 = gm_f_push_givp2as(t7, GM_CONV(gm_cs7e9ppsd384bj, t11));
            gm_l3 = t12;
            uint8_t t13 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
            if (!t13) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srcmok80ho8hh01, 417, 29);
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t14 = gm_l0;
    gm_closure t15 = ((gm_closure){(void *)gm_f_fn_kssb0q2, (void *)&F.h, cx});
    gm_f_for_each_child_fm49s73(t14, t15);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_defer_body_avm7tau(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2, uint8_t a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_defer_body_inner_v8ddaht(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2, uint8_t a3) {
    struct gm_fr_defer_body_inner_v8ddaht F;
    void * gm_l0; /* fg */
    gm_th2lqbo77pvsc gm_l1; /* guard */
    gm_thervm8j23qet gm_l2; /* d */
    uint8_t gm_l3; /* unwinding */
    uint8_t gm_l4; /* saved_check */
    gm_b16_8 gm_l5; /* g */
    void * gm_l6; /* c */
    GM_STACK_CHECK(&F, 408u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l3 = a3;
    void * t6 = gm_l0;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)t6) + 240));
    gm_l4 = t7;
    void * t8 = gm_l0;
    GM_ST(uint8_t, (((uint8_t *)t8) + 240), ((uint8_t)1));
    {
        {
            uint8_t t9 = (uint8_t)((((uint8_t *)a1))[16] != 0);
            if (!t9) {
                goto gm_lb2;
            }
            gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)a1));
            gm_l5 = t10;
            void * t11 = gm_l0;
            gm_b16_8 t12 = gm_l5;
            gm_tnpifv20irnph t13;
            GM_ST(gm_b16_8, ((uint8_t *)&t13 + 0), t12);
            gm_b0_1 t14 = gm_f_linef_g6msml8((void *)NULL, cx, NULL, t11, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
        void * t15 = gm_l0;
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_sh40na9qbga3l));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(1));
        gm_b0_1 t17 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
gm_lb1:;
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)t19) + 168));
    uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
    if (o22) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 692, 5);
    uint64_t t23 = r21;
    GM_ST(uint64_t, (((uint8_t *)t18) + 168), t23);
    uint8_t t24 = gm_l3;
    if (t24) {
        void * t25 = gm_l0;
        gm_b16_8 t26;
        GM_ST(void *, (uint8_t *)&t26, (void *)(gm_sovhd8e8hvflr));
        GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(51));
        gm_b0_1 t27 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t25, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t28 = gm_l0;
            uint8_t t29 = (uint8_t)(GM_LD(void *, (((uint8_t *)t28) + 352)) != 0);
            if (!t29) {
                goto gm_lb4;
            }
            void * t30 = GM_LD(void *, (((uint8_t *)t28) + 352));
            gm_l6 = t30;
            void * t31 = gm_l6;
            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 8));
            gm_tqgtd5gtgj65s t33;
            gm_f_lc_defer_list_8u47fth((void *)&t33, (void *)NULL, cx, NULL, t31, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb3;
        }
gm_lb4:;
    }
gm_lb3:;
    uint8_t t34 = gm_l3;
    if (t34) {
        void * t35 = gm_l0;
        gm_b16_8 t36;
        GM_ST(void *, (uint8_t *)&t36, (void *)(gm_s2hbi2g88pale));
        GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(18));
        gm_b0_1 t37 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t35, t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    void * t38 = gm_l0;
    void * t39 = gm_l0;
    uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)t39) + 168));
    uint64_t r41; bool o42 = __builtin_sub_overflow(t40, ((uint64_t)1ULL), &r41);
    if (o42) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 698, 5);
    uint64_t t43 = r41;
    GM_ST(uint64_t, (((uint8_t *)t38) + 168), t43);
    void * t44 = gm_l0;
    gm_b16_8 t45;
    GM_ST(void *, (uint8_t *)&t45, (void *)(gm_s2cuflhqkkuaq));
    GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(1));
    gm_b0_1 t46 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t44, t45);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t47 = gm_l0;
    uint8_t t48 = gm_l4;
    GM_ST(uint8_t, (((uint8_t *)t47) + 240), t48);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_end_lines_99v0bhd(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_end_scope_45ar5qv(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint8_t a2) {
    struct gm_fr_end_scope_45ar5qv F;
    void * gm_l0; /* fg */
    uint8_t gm_l2; /* fallthrough */
    gm_b16_8 gm_l3; /* skip */
    uint64_t gm_l4; /* k */
    gm_thervm8j23qet gm_l5; /* d */
    gm_th2lqbo77pvsc gm_l6; /* guard */
    gm_tiilmfd16v3fc gm_l8; /* w */
    GM_STACK_CHECK(&F, 784u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l9;
    gm_tabb710d9mq2e gm_l10;
    gm_tabb710d9mq2e gm_l11;
    gm_tabb710d9mq2e gm_l12;
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)a1) + 96));
    uint8_t t10 = ((t9 == ((uint64_t)0ULL)));
    if (t10) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    gm_b16_8 t11;
    GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(0));
    gm_l3 = t11;
    uint8_t t12 = gm_l2;
    if (t12) {
        void * t13 = gm_l0;
        uint32_t t14 = gm_f_fresh_5kokjmg(t13);
        gm_t88rcucg1b7uk t15;
        GM_ST(uint32_t, ((uint8_t *)&t15 + 0), t14);
        gm_f_format_hs8tdgq((void *)&gm_l9, (void *)NULL, cx, NULL, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
        if (!t16) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srcmok80ho8hh01, 643, 16);
        gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
        gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
        gm_l3 = t18;
        void * t19 = gm_l0;
        gm_b16_8 t20 = gm_l3;
        gm_tnpifv20irnph t21;
        GM_ST(gm_b16_8, ((uint8_t *)&t21 + 0), t20);
        gm_b0_1 t22 = gm_f_linef_njoe3l6((void *)NULL, cx, NULL, t19, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)0ULL));
            if (!t24) {
                goto gm_lb2;
            }
            uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)a1) + 96));
            gm_l4 = t25;
            {
                for (;;) {
                    uint64_t t26 = gm_l4;
                    uint8_t t27 = (t26 >= ((uint64_t)1ULL));
                    if (!t27) {
                        goto gm_lb3;
                    }
                    {
                        {
                            void * t28 = gm_l0;
                            uint32_t t29 = GM_LD(uint32_t, (((uint8_t *)a1) + 48));
                            uint64_t t30 = gm_l4;
                            gm_tb31k5l794t7p t31;
                            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
                            GM_ST(uint32_t, ((uint8_t *)&t31 + 0), t29);
                            GM_ST(uint64_t, ((uint8_t *)&t31 + 8), t30);
                            gm_f_format_5bcbbmm((void *)&gm_l10, (void *)NULL, cx, NULL, t31);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                            if (!t32) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_srcmok80ho8hh01, 650, 26);
                            gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
                            gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
                            gm_b0_1 t35 = gm_f_label_710oev4((void *)NULL, cx, NULL, t28, t34);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        {
                            void * t36 = GM_LD(void *, (((uint8_t *)a1) + 56));
                            uint64_t t37 = GM_LD(uint64_t, ((((uint8_t *)a1) + 56) + 8));
                            uint64_t t38 = gm_l4;
                            uint64_t r39; bool o40 = __builtin_sub_overflow(t38, ((uint64_t)1ULL), &r39);
                            if (o40) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 651, 37);
                            uint64_t t41 = r39;
                            if ((uintptr_t)t41 >= (uintptr_t)(t37)) gm_trap_index((uintptr_t)t41, t37, gm_srcmok80ho8hh01, 651, 22);
                            memmove(((uint8_t *)&gm_l5), (((uint8_t *)t36) + (uintptr_t)t41 * 24), 24);
                        }
                        {
                            memset(&gm_l6, 0, sizeof gm_l6);
                        }
                        uint8_t t42 = GM_LD(uint8_t, ((uint8_t *)&gm_l5));
                        if (t42) {
                            gm_b16_8 t43;
                            GM_ST(void *, (uint8_t *)&t43, (void *)(gm_sm7odqe0d6lgu));
                            GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(22));
                            GM_ST(uint8_t, (uint8_t *)&gm_l6 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&gm_l6 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&gm_l6 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&gm_l6), t43);
                            ((uint8_t *)&gm_l6)[16] = 1;
                        }
                        {
                            void * t44 = gm_l0;
                            gm_b0_1 t45 = gm_f_defer_body_avm7tau((void *)NULL, cx, NULL, t44, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l6)), &(*(gm_thervm8j23qet *)((uint8_t *)&gm_l5)), ((uint8_t)1));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint64_t t46 = gm_l4;
                        uint64_t r47; bool o48 = __builtin_sub_overflow(t46, ((uint64_t)1ULL), &r47);
                        if (o48) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 655, 17);
                        uint64_t t49 = r47;
                        gm_l4 = t49;
                    }
gm_lb5:;
                }
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t51 = (uint8_t)(t50 == ((uint8_t)1ULL));
            if (!t51) {
                goto gm_lb6;
            }
            {
                memmove(((uint8_t *)&F.l7), (((uint8_t *)a1) + 8), 40);
            }
            {
                void * t52 = gm_l0;
                uint32_t t53 = GM_LD(uint32_t, (((uint8_t *)a1) + 48));
                gm_t88rcucg1b7uk t54;
                GM_ST(uint32_t, ((uint8_t *)&t54 + 0), t53);
                gm_f_format_dd6d4bt((void *)&gm_l11, (void *)NULL, cx, NULL, t54);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t55 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                if (!t55) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_srcmok80ho8hh01, 659, 22);
                gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
                gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
                gm_b0_1 t58 = gm_f_label_710oev4((void *)NULL, cx, NULL, t52, t57);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t59 = gm_l0;
            uint32_t t60 = GM_LD(uint32_t, ((uint8_t *)&F.l7));
            gm_t88rcucg1b7uk t61;
            GM_ST(uint32_t, ((uint8_t *)&t61 + 0), t60);
            gm_b0_1 t62 = gm_f_linef_aka1a8l((void *)NULL, cx, NULL, t59, t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t63 = gm_l0;
            gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 8));
            gm_tnpifv20irnph t65;
            GM_ST(gm_b16_8, ((uint8_t *)&t65 + 0), t64);
            gm_b0_1 t66 = gm_f_linef_2n883h6((void *)NULL, cx, NULL, t63, t65);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t67 = gm_l0;
            gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 24));
            gm_tnpifv20irnph t69;
            GM_ST(gm_b16_8, ((uint8_t *)&t69 + 0), t68);
            gm_b0_1 t70 = gm_f_linef_mkfjg2u((void *)NULL, cx, NULL, t67, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t71 = gm_l0;
            gm_b16_8 t72;
            GM_ST(void *, (uint8_t *)&t72, (void *)(gm_s2cuflhqkkuaq));
            GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(1));
            gm_b0_1 t73 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t71, t72);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb6:;
        {
            uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t75 = (uint8_t)(t74 == ((uint8_t)2ULL));
            if (!t75) {
                goto gm_lb7;
            }
            {
                memmove(((uint8_t *)&gm_l8), (((uint8_t *)a1) + 8), 24);
            }
            {
                void * t76 = gm_l0;
                uint32_t t77 = GM_LD(uint32_t, (((uint8_t *)a1) + 48));
                gm_t88rcucg1b7uk t78;
                GM_ST(uint32_t, ((uint8_t *)&t78 + 0), t77);
                gm_f_format_dd6d4bt((void *)&gm_l12, (void *)NULL, cx, NULL, t78);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t79 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                if (!t79) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_srcmok80ho8hh01, 666, 22);
                gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
                gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&t80));
                gm_b0_1 t82 = gm_f_label_710oev4((void *)NULL, cx, NULL, t76, t81);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t83 = gm_l0;
                uint32_t t84 = GM_LD(uint32_t, ((uint8_t *)&gm_l8));
                gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
                gm_tiilmfd16v3fc t86;
                GM_ST(uint32_t, (uint8_t *)&t86 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t86 + 0), t84);
                GM_ST(gm_b16_8, ((uint8_t *)&t86 + 8), t85);
                gm_b0_1 t87 = gm_f_linef_1p9d72n((void *)NULL, cx, NULL, t83, &t86);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto gm_lb1;
        }
gm_lb7:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 646, 5);
    }
gm_lb1:;
    void * t88 = gm_l0;
    gm_b0_1 t89 = gm_f_ladder_36amdbn((void *)NULL, cx, NULL, t88);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t90 = gm_l2;
    if (t90) {
        void * t91 = gm_l0;
        gm_b16_8 t92 = gm_l3;
        gm_b0_1 t93 = gm_f_label_710oev4((void *)NULL, cx, NULL, t91, t92);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_exit_c_name_i72r98r(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_exit_c_name_i72r98r F;
    gm_t9qkh0rsooc8d gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 155u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l1;
    {
        {
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t9 = (uint8_t)(t8 == ((uint8_t)0ULL));
            if (!t9) {
                goto gm_lb2;
            }
            gm_b16_8 t10;
            GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sbrtnmpf78kqq));
            GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(9));
            gm_l1 = t10;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t12 = (uint8_t)(t11 == ((uint8_t)1ULL));
            if (!t12) {
                goto gm_lb3;
            }
            gm_b16_8 t13;
            GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sug1rl2kq22u3));
            GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(8));
            gm_l1 = t13;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t15 = (uint8_t)(t14 == ((uint8_t)2ULL));
            if (!t15) {
                goto gm_lb4;
            }
            gm_b16_8 t16;
            GM_ST(void *, (uint8_t *)&t16, (void *)(gm_s2ogeh8fp9da3));
            GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(11));
            gm_l1 = t16;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)3ULL));
            if (!t18) {
                goto gm_lb5;
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s9nvmaqikg2bc));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(7));
            gm_l1 = t19;
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t21 = (uint8_t)(t20 == ((uint8_t)4ULL));
            if (!t21) {
                goto gm_lb6;
            }
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_spdpobhso0dbc));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(9));
            gm_l1 = t22;
            goto gm_lb1;
        }
gm_lb6:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 708, 45);
    }
gm_lb1:;
    gm_b16_8 t23 = gm_l1;
    F.ret = t23;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_expr_call_depth_dliqc7u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_expr_call_depth_dliqc7u F;
    void * gm_l1; /* e */
    uint32_t gm_l4; /* c */
    gm_t8588ma2cr9d7 gm_l5; /* op */
    GM_STACK_CHECK(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l6;
    F.l2 = ((uint32_t)0ULL);
    void * t22 = gm_l1;
    {
        {
            uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)34ULL));
            if (!t24) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l3), ((((uint8_t *)t22) + 8) + 8), 40);
            {
                {
                    uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                    uint8_t t26 = (uint8_t)(t25 == ((uint8_t)0ULL));
                    if (!t26) {
                        goto gm_lb4;
                    }
                    uint32_t t27 = GM_LD(uint32_t, (((uint8_t *)&F.l3) + 8));
                    gm_l4 = t27;
                    void * t28 = F.l0;
                    uint32_t t29 = gm_l4;
                    uint32_t t30 = gm_f_call_depth_4m6ap7n((void *)NULL, cx, NULL, t28, t29);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint32_t r31; bool o32 = __builtin_add_overflow(t30, ((uint32_t)1ULL), &r31);
                    uint32_t t33 = o32 ? (1 ? ((uint32_t)4294967295ULL) : ((uint32_t)0ULL)) : r31;
                    F.l2 = t33;
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)gm_c71idar1l1k));
                    uint32_t t35 = (uint32_t)t34;
                    F.ret = t35;
                    goto gm_fret;
                }
gm_lb5:;
                gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 323, 29);
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
        {
            {
                {
                    uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
                    uint8_t t37 = (uint8_t)(t36 == ((uint8_t)55ULL));
                    if (!t37) {
                        goto gm_lb8;
                    }
                    goto gm_lb7;
                }
gm_lb8:;
                {
                    uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
                    uint8_t t39 = (uint8_t)(t38 == ((uint8_t)56ULL));
                    if (!t39) {
                        goto gm_lb9;
                    }
                    goto gm_lb7;
                }
gm_lb9:;
                {
                    uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
                    uint8_t t41 = (uint8_t)(t40 == ((uint8_t)58ULL));
                    if (!t41) {
                        goto gm_lb10;
                    }
                    goto gm_lb7;
                }
gm_lb10:;
                {
                    uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
                    uint8_t t43 = (uint8_t)(t42 == ((uint8_t)53ULL));
                    if (!t43) {
                        goto gm_lb11;
                    }
                    goto gm_lb7;
                }
gm_lb11:;
                {
                    uint8_t t44 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
                    uint8_t t45 = (uint8_t)(t44 == ((uint8_t)54ULL));
                    if (!t45) {
                        goto gm_lb12;
                    }
                    goto gm_lb7;
                }
gm_lb12:;
                {
                    uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
                    uint8_t t47 = (uint8_t)(t46 == ((uint8_t)59ULL));
                    if (!t47) {
                        goto gm_lb13;
                    }
                    goto gm_lb7;
                }
gm_lb13:;
                goto gm_lb6;
            }
gm_lb7:;
            uint8_t t48 = GM_LD(uint8_t, ((uint8_t *)gm_c71idar1l1k));
            uint32_t t49 = (uint32_t)t48;
            F.ret = t49;
            goto gm_fret;
        }
gm_lb6:;
        {
            uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t51 = (uint8_t)(t50 == ((uint8_t)33ULL));
            if (!t51) {
                goto gm_lb14;
            }
            gm_t8588ma2cr9d7 t52 = GM_LD(gm_t8588ma2cr9d7, ((((uint8_t *)t22) + 8) + 8));
            gm_l5 = t52;
            {
                {
                    uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                    uint8_t t54 = (uint8_t)(t53 == ((uint8_t)74ULL));
                    if (!t54) {
                        goto gm_lb16;
                    }
                    gm_l6 = ((uint8_t)1);
                    goto gm_lb15;
                }
gm_lb16:;
                {
                    uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                    uint8_t t56 = (uint8_t)(t55 == ((uint8_t)80ULL));
                    if (!t56) {
                        goto gm_lb17;
                    }
                    gm_l6 = ((uint8_t)1);
                    goto gm_lb15;
                }
gm_lb17:;
                {
                    uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                    uint8_t t58 = (uint8_t)(t57 == ((uint8_t)73ULL));
                    if (!t58) {
                        goto gm_lb18;
                    }
                    gm_l6 = ((uint8_t)1);
                    goto gm_lb15;
                }
gm_lb18:;
                {
                    gm_l6 = ((uint8_t)0);
                    goto gm_lb15;
                }
gm_lb19:;
                gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 328, 31);
            }
gm_lb15:;
            uint8_t t59 = gm_l6;
            if (t59) {
                uint8_t t60 = GM_LD(uint8_t, ((uint8_t *)gm_c71idar1l1k));
                uint32_t t61 = (uint32_t)t60;
                F.ret = t61;
                goto gm_fret;
            }
            goto gm_lb1;
        }
gm_lb14:;
        {
            goto gm_lb1;
        }
gm_lb20:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 322, 5);
    }
gm_lb1:;
    void * t62 = gm_l1;
    gm_closure t63 = ((gm_closure){(void *)gm_f_fn_v0t9sik, (void *)&F.h, cx});
    gm_f_for_each_child_fm49s73(t62, t63);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint32_t t64 = F.l2;
    F.ret = t64;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5evrfa7(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) uint8_t gm_f_fn_e5aba81(void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_jjarlg6(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kssb0q2(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_v0t9sik(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_frame_field_nj8m22n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2) {
    struct gm_fr_frame_field_nj8m22n F;
    void * gm_l0; /* fg */
    uint32_t gm_l1; /* levels */
    gm_b16_8 gm_l2; /* field */
    gm_b16_8 gm_l3; /* p */
    uint32_t gm_l4; /* f */
    GM_STACK_CHECK(&F, 403u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l5;
    gm_tommf4j9upomc gm_l6;
    gm_tabb710d9mq2e gm_l7;
    uint32_t t2 = gm_l1;
    uint8_t t3 = ((t2 == ((uint32_t)0ULL)));
    if (t3) {
        gm_b16_8 t4 = gm_l2;
        gm_tnpifv20irnph t5;
        GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t4);
        gm_f_format_rmu2e6q((void *)&gm_l5, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
        if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 8, gm_srcmok80ho8hh01, 560, 29);
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        F.ret = t8;
        goto gm_fret;
    }
    {
        void * t9 = gm_l0;
        uint32_t t10 = gm_l1;
        gm_f_frame_up_g8486eu((void *)&gm_l6, (void *)NULL, cx, NULL, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
    gm_l3 = t11;
    uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 16));
    gm_l4 = t12;
    {
        void * t13 = gm_l0;
        void * t14 = GM_LD(void *, ((uint8_t *)t13));
        uint32_t t15 = gm_l4;
        gm_b16_8 t16 = gm_f_frame_name_stddmgg((void *)NULL, cx, NULL, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t17 = gm_l3;
        gm_b16_8 t18 = gm_l2;
        gm_tfeab279c16rr t19;
        GM_ST(gm_b16_8, ((uint8_t *)&t19 + 0), t16);
        GM_ST(gm_b16_8, ((uint8_t *)&t19 + 16), t17);
        GM_ST(gm_b16_8, ((uint8_t *)&t19 + 32), t18);
        gm_f_format_lufljqc((void *)&gm_l7, (void *)NULL, cx, NULL, &t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (!t20) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_srcmok80ho8hh01, 562, 12);
    gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
    gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
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
__attribute__((aligned(4))) gm_b16_8 gm_f_frame_locals_mt4nb6o(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_frame_locals_mt4nb6o F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    uint64_t gm_l2; /* at */
    gm_b16_8 gm_l3; /* known */
    void * gm_l4; /* def */
    uint8_t gm_l6; /* all */
    void * gm_l7; /* b */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint8_t gm_l11; /* filc */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    GM_STACK_CHECK(&F, 957u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l16;
    uint8_t gm_l17;
    uint8_t gm_l18;
    uint8_t gm_l19;
    gm_tftb1odc739t5 gm_l20;
    gm_t5im2v50m3hk6 gm_l21;
    uint8_t gm_l22;
    uint8_t gm_l23;
    uint8_t gm_l24;
    gm_tqs5p4n2tq19v gm_l25;
    gm_tftb1odc739t5 gm_l26;
    uint32_t t22 = gm_l1;
    uint64_t t23 = (uint64_t)t22;
    gm_l2 = t23;
    {
        for (;;) {
            void * t24 = gm_l0;
            uint64_t t25 = GM_LD(uint64_t, ((((uint8_t *)t24) + 2960) + 8));
            uint64_t t26 = gm_l2;
            uint8_t t27 = (t25 <= t26);
            if (!t27) {
                goto gm_lb1;
            }
            {
                void * t28 = gm_l0;
                void * t29 = (void *)(((uint8_t *)t28) + 2960);
                gm_th2lqbo77pvsc t30;
                memset(&t30, 0, sizeof t30);
                gm_tftb1odc739t5 t31 = gm_f_push_7dfimu1(t29, &t30);
                gm_l16 = t31;
                uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                if (!t32) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_srcmok80ho8hh01, 809, 42);
            }
gm_lb3:;
        }
    }
gm_lb1:;
    {
        {
            void * t33 = gm_l0;
            void * t34 = GM_LD(void *, (((uint8_t *)t33) + 2960));
            uint64_t t35 = GM_LD(uint64_t, ((((uint8_t *)t33) + 2960) + 8));
            uint64_t t36 = gm_l2;
            if ((uintptr_t)t36 >= (uintptr_t)(t35)) gm_trap_index((uintptr_t)t36, t35, gm_srcmok80ho8hh01, 810, 17);
            uint8_t t37 = (uint8_t)(((((uint8_t *)t34) + (uintptr_t)t36 * 24))[16] != 0);
            if (!t37) {
                goto gm_lb5;
            }
            gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)t34) + (uintptr_t)t36 * 24));
            gm_l3 = t38;
            gm_b16_8 t39 = gm_l3;
            F.ret = t39;
            goto gm_fret;
        }
gm_lb5:;
    }
gm_lb4:;
    void * t40 = gm_l0;
    void * t41 = GM_LD(void *, ((uint8_t *)t40));
    uint32_t t42 = gm_l1;
    void * t43 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t41, t42);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t43;
    {
        gm_trqfvmjtt4sij t44;
        gm_f_list_s83jn2q((void *)&t44, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l5 = t44;
    }
    void * t45 = gm_l0;
    uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)t45) + 1136) + 1));
    if (t46) {
        gm_l18 = ((uint8_t)1);
    } else {
        void * t47 = gm_l0;
        uint8_t t48 = GM_LD(uint8_t, ((((uint8_t *)t47) + 1136) + 2));
        gm_l18 = t48;
    }
    uint8_t t49 = gm_l18;
    if (t49) {
        gm_l17 = ((uint8_t)1);
    } else {
        void * t50 = gm_l0;
        uint32_t t51 = gm_l1;
        uint8_t t52 = gm_f_outlines_2kvbvit((void *)NULL, cx, NULL, t50, t51);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l17 = t52;
    }
    uint8_t t53 = gm_l17;
    gm_l6 = t53;
    {
        {
            void * t54 = gm_l4;
            uint8_t t55 = (uint8_t)(GM_LD(void *, (((uint8_t *)t54) + 152)) != 0);
            if (!t55) {
                goto gm_lb7;
            }
            void * t56 = GM_LD(void *, (((uint8_t *)t54) + 152));
            gm_l7 = t56;
            uint8_t t57 = gm_l6;
            if (t57) {
                gm_l19 = ((uint8_t)1);
            } else {
                void * t58 = gm_l7;
                uint8_t t59 = gm_f_has_fiber_with_53c3jik(t58);
                gm_l19 = t59;
            }
            uint8_t t60 = gm_l19;
            gm_l6 = t60;
            goto gm_lb6;
        }
gm_lb7:;
    }
gm_lb6:;
    uint8_t t61 = gm_l6;
    if (t61) {
        void * t62 = gm_l4;
        gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)t62) + 48));
        gm_l8 = t63;
        gm_l9 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t64 = gm_l9;
                uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                uint8_t t66 = (t64 < t65);
                if (!t66) {
                    goto gm_lb8;
                }
                {
                    void * t67 = GM_LD(void *, ((uint8_t *)&gm_l8));
                    uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                    uint64_t t69 = gm_l9;
                    {
                        {
                            void * t70 = (void *)((uint8_t *)&F.l5);
                            gm_tftb1odc739t5 t71 = gm_f_push_vd1290p(t70, ((uint8_t)1));
                            gm_l20 = t71;
                            uint8_t t72 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                            if (!t72) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_srcmok80ho8hh01, 816, 31);
                            goto gm_lb11;
                        }
gm_lb12:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 816, 9);
                    }
gm_lb11:;
                }
gm_lb10:;
                uint64_t t73 = gm_l9;
                uint64_t r74; bool o75 = __builtin_add_overflow(t73, ((uint64_t)1ULL), &r74);
                uint64_t t76 = r74;
                gm_l9 = t76;
            }
        }
gm_lb8:;
    } else {
        void * t77 = gm_l0;
        gm_t4c7ipjj9s3fp t78;
        gm_f_captured_locals_glsvloj((void *)&t78, (void *)NULL, cx, NULL, t77);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l10 = t78;
        void * t79 = gm_l0;
        uint8_t t80 = GM_LD(uint8_t, (((((uint8_t *)t79) + 8) + 8) + 56));
        gm_l11 = t80;
        void * t81 = gm_l4;
        gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)t81) + 48));
        gm_l12 = t82;
        gm_l13 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t83 = gm_l13;
                uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                uint8_t t85 = (t83 < t84);
                if (!t85) {
                    goto gm_lb13;
                }
                {
                    {
                        uint64_t t86 = gm_l13;
                        void * t87 = GM_LD(void *, ((uint8_t *)&gm_l12));
                        uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                        uint64_t t89 = gm_l13;
                        gm_tsoie1ppgdvcg t90 = GM_LD(gm_tsoie1ppgdvcg, (((uint8_t *)t87) + (uintptr_t)t89 * 40));
                        GM_ST(uint64_t, ((uint8_t *)&gm_l21 + 0), t86);
                        GM_ST(gm_tsoie1ppgdvcg, ((uint8_t *)&gm_l21 + 8), t90);
                    }
                    {
                        {
                            uint64_t t91 = GM_LD(uint64_t, ((uint8_t *)&gm_l21));
                            gm_l14 = t91;
                            memmove(((uint8_t *)&F.l15), (((uint8_t *)&gm_l21) + 8), 40);
                            void * t92 = (void *)((uint8_t *)&F.l5);
                            uint8_t t93 = gm_l11;
                            uint8_t t94 = (uint8_t)!t93;
                            if (t94) {
                                void * t95 = gm_l0;
                                void * t96 = (void *)(((uint8_t *)t95) + 8);
                                gm_t88rcucg1b7uk t97 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l15) + 16));
                                uint64_t t98 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t96, t97);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint64_t t99 = GM_LD(uint64_t, ((uint8_t *)gm_cbemal9c6bf));
                                uint8_t t100 = (t98 > t99);
                                gm_l23 = t100;
                            } else {
                                gm_l23 = ((uint8_t)0);
                            }
                            uint8_t t101 = gm_l23;
                            if (t101) {
                                gm_l22 = ((uint8_t)1);
                            } else {
                                void * t102 = (void *)((uint8_t *)&F.l10);
                                uint32_t t103 = gm_l1;
                                uint64_t t104 = gm_l14;
                                uint32_t t105 = (uint32_t)t104;
                                gm_tcc1rq4n1avb2 t106;
                                GM_ST(uint32_t, ((uint8_t *)&t106 + 0), t103);
                                GM_ST(uint32_t, ((uint8_t *)&t106 + 4), t105);
                                gm_tqs5p4n2tq19v t107 = gm_f_get_ag7vhtq(t102, t106);
                                gm_l25 = t107;
                                {
                                    {
                                        uint8_t t108 = (uint8_t)((((uint8_t *)&gm_l25))[1] != 0);
                                        if (t108) {
                                            goto gm_lb19;
                                        }
                                        gm_l24 = ((uint8_t)0);
                                        goto gm_lb18;
                                    }
gm_lb19:;
                                    {
                                        gm_l24 = ((uint8_t)1);
                                        goto gm_lb18;
                                    }
gm_lb20:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 821, 70);
                                }
gm_lb18:;
                                uint8_t t109 = gm_l24;
                                gm_l22 = t109;
                            }
                            uint8_t t110 = gm_l22;
                            gm_tftb1odc739t5 t111 = gm_f_push_vd1290p(t92, t110);
                            gm_l26 = t111;
                            uint8_t t112 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                            if (!t112) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_srcmok80ho8hh01, 821, 13);
                            goto gm_lb16;
                        }
gm_lb17:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 820, 9);
                    }
gm_lb16:;
                }
gm_lb15:;
                uint64_t t113 = gm_l13;
                uint64_t r114; bool o115 = __builtin_add_overflow(t113, ((uint64_t)1ULL), &r114);
                uint64_t t116 = r114;
                gm_l13 = t116;
            }
        }
gm_lb13:;
    }
    {
        void * t117 = gm_l0;
        void * t118 = GM_LD(void *, (((uint8_t *)t117) + 2960));
        uint64_t t119 = GM_LD(uint64_t, ((((uint8_t *)t117) + 2960) + 8));
        uint64_t t120 = gm_l2;
        if ((uintptr_t)t120 >= (uintptr_t)(t119)) gm_trap_index((uintptr_t)t120, t119, gm_srcmok80ho8hh01, 824, 5);
        gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
        gm_th2lqbo77pvsc t122;
        GM_ST(uint8_t, (uint8_t *)&t122 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t122 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t122 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t122), t121);
        ((uint8_t *)&t122)[16] = 1;
        GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)t118) + (uintptr_t)t120 * 24), t122);
    }
    gm_b16_8 t123 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
    F.ret = t123;
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
inline __attribute__((aligned(4))) void gm_f_frame_up_g8486eu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_frame_up_g8486eu F;
    void * gm_l0; /* fg */
    uint32_t gm_l1; /* levels */
    gm_b16_8 gm_l2; /* expr */
    uint32_t gm_l3; /* f */
    uint32_t gm_l4; /* <tmp> */
    uint32_t gm_l5; /* <tmp> */
    uint32_t gm_l6; /* p */
    GM_STACK_CHECK(&F, 247u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l7;
    gm_b16_8 t9;
    GM_ST(void *, (uint8_t *)&t9, (void *)(gm_sb8kl54a499bf));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(6));
    gm_l2 = t9;
    void * t10 = gm_l0;
    uint32_t t11 = GM_LD(uint32_t, (((uint8_t *)t10) + 8));
    gm_l3 = t11;
    gm_l5 = ((uint32_t)0ULL);
    uint32_t t12 = gm_l1;
    gm_l4 = t12;
    {
        for (;;) {
            uint32_t t13 = gm_l5;
            uint32_t t14 = gm_l4;
            uint8_t t15 = (t13 < t14);
            if (!t15) {
                goto gm_lb1;
            }
            {
                {
                    {
                        gm_b16_8 t16 = gm_l2;
                        gm_tnpifv20irnph t17;
                        GM_ST(gm_b16_8, ((uint8_t *)&t17 + 0), t16);
                        gm_f_format_lb0inss((void *)&gm_l7, (void *)NULL, cx, NULL, t17);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                        if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_srcmok80ho8hh01, 552, 16);
                        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
                        gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
                        gm_l2 = t20;
                        {
                            {
                                void * t21 = gm_l0;
                                void * t22 = GM_LD(void *, ((uint8_t *)t21));
                                void * t23 = GM_LD(void *, ((uint8_t *)t22));
                                uint32_t t24 = gm_l3;
                                void * t25 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t23, t24);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t26 = (uint8_t)(((((uint8_t *)t25) + 88))[4] != 0);
                                if (!t26) {
                                    goto gm_lb7;
                                }
                                uint32_t t27 = GM_LD(uint32_t, (((uint8_t *)t25) + 88));
                                gm_l6 = t27;
                                uint32_t t28 = gm_l6;
                                gm_l3 = t28;
                                goto gm_lb6;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 551, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint32_t t29 = gm_l5;
            uint32_t r30; bool o31 = __builtin_add_overflow(t29, ((uint32_t)1ULL), &r30);
            uint32_t t32 = r30;
            gm_l5 = t32;
        }
    }
gm_lb1:;
    gm_b16_8 t33 = gm_l2;
    uint32_t t34 = gm_l3;
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tommf4j9upomc *)gm_out) + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tommf4j9upomc *)gm_out) + 0), t33);
    GM_ST(uint32_t, ((uint8_t *)&(*(gm_tommf4j9upomc *)gm_out) + 16), t34);
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
extern __attribute__((aligned(4))) uint32_t gm_f_fresh_5kokjmg(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_fresh_tmp_6tadl69(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fresh_tmp_6tadl69 F;
    void * gm_l0; /* fg */
    uint64_t gm_l1; /* n */
    void * gm_l2; /* names */
    GM_STACK_CHECK(&F, 283u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tabb710d9mq2e gm_l3;
    gm_tftb1odc739t5 gm_l4;
    void * t5 = gm_l0;
    uint32_t t6 = gm_f_fresh_5kokjmg(t5);
    uint64_t t7 = (uint64_t)t6;
    gm_l1 = t7;
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, ((uint8_t *)t8));
    void * t10 = (void *)(((uint8_t *)t9) + 3120);
    gm_l2 = t10;
    {
        for (;;) {
            void * t11 = gm_l2;
            uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 8));
            uint64_t t13 = gm_l1;
            uint8_t t14 = (t12 < t13);
            if (!t14) {
                goto gm_lb1;
            }
            {
                void * t15 = gm_l2;
                void * t16 = gm_l2;
                uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t16) + 8));
                uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)1ULL), &r18);
                if (o19) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 474, 62);
                uint64_t t20 = r18;
                gm_t4ce0jd3tdro4 t21;
                GM_ST(uint64_t, ((uint8_t *)&t21 + 0), t20);
                gm_f_format_rtv6jsp((void *)&gm_l3, (void *)NULL, cx, NULL, t21);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t22 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
                if (!t22) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srcmok80ho8hh01, 474, 44);
                gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
                gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
                gm_tftb1odc739t5 t25 = gm_f_push_3ef746b(t15, t24);
                gm_l4 = t25;
                uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
                if (!t26) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srcmok80ho8hh01, 474, 33);
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t27 = gm_l2;
    void * t28 = GM_LD(void *, ((uint8_t *)t27));
    uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)t27) + 8));
    uint64_t t30 = gm_l1;
    uint64_t r31; bool o32 = __builtin_sub_overflow(t30, ((uint64_t)1ULL), &r31);
    if (o32) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 475, 24);
    uint64_t t33 = r31;
    if ((uintptr_t)t33 >= (uintptr_t)(t29)) gm_trap_index((uintptr_t)t33, t29, gm_srcmok80ho8hh01, 475, 12);
    gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)t28) + (uintptr_t)t33 * 16));
    F.ret = t34;
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
__attribute__((aligned(4))) void gm_f_gen_body_j6ku8qk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, void * a3, gm_t9805qtonbsag a4) {
    struct gm_fr_gen_body_j6ku8qk F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* def */
    void * gm_l3; /* body */
    gm_t9805qtonbsag gm_l4; /* spec */
    uint8_t gm_l5; /* plain */
    uint8_t gm_l6; /* out_ret */
    uint8_t gm_l7; /* void_ret */
    uint8_t gm_l8; /* blob_ret */
    gm_b16_8 gm_l9; /* ret */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* i */
    uint32_t gm_l14; /* p */
    gm_t88rcucg1b7uk gm_l15; /* t */
    gm_b16_8 gm_l16; /* c */
    gm_b16_8 gm_l17; /* borrowed */
    gm_b16_8 gm_l18; /* in_frame */
    uint8_t gm_l19; /* split_runs */
    uint8_t gm_l22; /* framed */
    gm_b16_8 gm_l23; /* cname */
    uint32_t gm_l24; /* sp */
    gm_b16_8 gm_l25; /* sig */
    uint64_t gm_l26; /* cost */
    gm_b16_8 gm_l27; /* name */
    gm_b16_8 gm_l28; /* tag */
    gm_b16_8 gm_l30; /* <tmp> */
    uint64_t gm_l31; /* <tmp> */
    uint64_t gm_l32; /* i */
    gm_b16_8 gm_l34; /* c */
    gm_b16_8 gm_l35; /* pop */
    gm_b16_8 gm_l36; /* name */
    gm_b16_8 gm_l37; /* n */
    int32_t gm_l38; /* clause_lit */
    gm_b16_8 gm_l39; /* <tmp> */
    uint64_t gm_l40; /* <tmp> */
    uint64_t gm_l41; /* i */
    uint32_t gm_l42; /* p */
    gm_t88rcucg1b7uk gm_l43; /* t */
    gm_b16_8 gm_l44; /* lv */
    gm_b16_8 gm_l45; /* an */
    gm_b16_8 gm_l46; /* a */
    gm_b16_8 gm_l47; /* lv */
    gm_b16_8 gm_l48; /* kill */
    gm_b16_8 gm_l49; /* r */
    gm_b16_8 gm_l50; /* r */
    gm_b16_8 gm_l51; /* ct */
    gm_b16_8 gm_l52; /* tail */
    gm_b16_8 gm_l53; /* text */
    uint64_t gm_l54; /* nl */
    gm_b16_8 gm_l55; /* header */
    uint64_t gm_l56; /* at */
    GM_STACK_CHECK(&F, 6828u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    uint8_t gm_l57;
    uint8_t gm_l58;
    gm_tftb1odc739t5 gm_l59;
    gm_tftb1odc739t5 gm_l60;
    gm_tftb1odc739t5 gm_l61;
    gm_tftb1odc739t5 gm_l62;
    gm_tn68el8d14vku gm_l63;
    gm_tabb710d9mq2e gm_l64;
    gm_tftb1odc739t5 gm_l65;
    gm_tftb1odc739t5 gm_l66;
    uint8_t gm_l67;
    uint8_t gm_l68;
    gm_tqgtd5gtgj65s gm_l69;
    gm_tftb1odc739t5 gm_l70;
    gm_tftb1odc739t5 gm_l71;
    gm_tabb710d9mq2e gm_l72;
    gm_tabb710d9mq2e gm_l73;
    gm_tftb1odc739t5 gm_l74;
    gm_tftb1odc739t5 gm_l75;
    gm_tftb1odc739t5 gm_l76;
    gm_t5im2v50m3hk6 gm_l77;
    gm_b16_8 gm_l78;
    gm_th2lqbo77pvsc gm_l79;
    gm_tn68el8d14vku gm_l80;
    gm_tabb710d9mq2e gm_l81;
    gm_tqgtd5gtgj65s gm_l82;
    gm_tabb710d9mq2e gm_l83;
    gm_tftb1odc739t5 gm_l84;
    gm_tftb1odc739t5 gm_l85;
    gm_tftb1odc739t5 gm_l86;
    gm_tabb710d9mq2e gm_l87;
    gm_tftb1odc739t5 gm_l88;
    gm_tf5srh4mp4b3i gm_l89;
    uint8_t gm_l90;
    gm_tftb1odc739t5 gm_l91;
    gm_tftb1odc739t5 gm_l92;
    gm_tftb1odc739t5 gm_l93;
    gm_b16_8 gm_l94;
    gm_tftb1odc739t5 gm_l95;
    gm_tftb1odc739t5 gm_l96;
    {
        void * t42 = gm_l0;
        uint32_t t43 = gm_l1;
        uint8_t t44 = gm_f_is_plain_qr08i40((void *)NULL, cx, NULL, t42, t43);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t44;
        void * t45 = gm_l0;
        void * t46 = (void *)(((uint8_t *)t45) + 8);
        void * t47 = gm_l2;
        gm_t88rcucg1b7uk t48 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t47) + 64));
        uint8_t t49 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t46, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t49;
        uint8_t t50 = gm_l5;
        if (t50) {
            void * t51 = gm_l0;
            void * t52 = (void *)(((uint8_t *)t51) + 8);
            void * t53 = gm_l2;
            gm_t88rcucg1b7uk t54 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t53) + 64));
            uint8_t t55 = gm_f_is_unit_17uennd(t52, GM_CONV(gm_csguankko4lmhd, t54));
            gm_l58 = t55;
        } else {
            gm_l58 = ((uint8_t)0);
        }
        uint8_t t56 = gm_l58;
        if (t56) {
            gm_l57 = ((uint8_t)1);
        } else {
            uint8_t t57 = gm_l6;
            gm_l57 = t57;
        }
        uint8_t t58 = gm_l57;
        gm_l7 = t58;
        uint8_t t59 = gm_l5;
        uint8_t t60 = (uint8_t)!t59;
        gm_l8 = t60;
        gm_b16_8 t61;
        GM_ST(void *, (uint8_t *)&t61, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(0));
        gm_l9 = t61;
        uint8_t t62 = gm_l7;
        if (t62) {
            gm_b16_8 t63;
            GM_ST(void *, (uint8_t *)&t63, (void *)(gm_sts5j6f5pkov0));
            GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(4));
            gm_l9 = t63;
        } else {
            uint8_t t64 = gm_l8;
            if (t64) {
                void * t65 = gm_l0;
                void * t66 = (void *)(((uint8_t *)t65) + 8);
                void * t67 = gm_l2;
                gm_t88rcucg1b7uk t68 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t67) + 64));
                gm_b16_8 t69 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t66, t68);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l9 = t69;
            } else {
                void * t70 = gm_l0;
                void * t71 = (void *)(((uint8_t *)t70) + 8);
                void * t72 = gm_l2;
                gm_t88rcucg1b7uk t73 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t72) + 64));
                gm_b16_8 t74 = gm_f_plain_cty_q9lhlci((void *)NULL, cx, NULL, t71, t73);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l9 = t74;
            }
        }
        gm_trqfvmjtt4sij t75;
        gm_f_list_epvducg((void *)&t75, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l10 = t75;
        uint8_t t76 = gm_l6;
        if (t76) {
            void * t77 = (void *)((uint8_t *)&F.l10);
            gm_b16_8 t78;
            GM_ST(void *, (uint8_t *)&t78, (void *)(gm_s4ql65vkv7h9p));
            GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(12));
            gm_tftb1odc739t5 t79 = gm_f_push_3ef746b(t77, t78);
            gm_l59 = t79;
            uint8_t t80 = (uint8_t)!((((uint8_t *)&gm_l59))[0] != 0);
            if (!t80) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l59) + 1, gm_srcmok80ho8hh01, 121, 18);
        }
        uint8_t t81 = gm_l5;
        uint8_t t82 = (uint8_t)!t81;
        if (t82) {
            void * t83 = (void *)((uint8_t *)&F.l10);
            gm_b16_8 t84;
            GM_ST(void *, (uint8_t *)&t84, (void *)(gm_siji3oo7ud53t));
            GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(9));
            gm_tftb1odc739t5 t85 = gm_f_push_3ef746b(t83, t84);
            gm_l60 = t85;
            uint8_t t86 = (uint8_t)!((((uint8_t *)&gm_l60))[0] != 0);
            if (!t86) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l60) + 1, gm_srcmok80ho8hh01, 123, 9);
            void * t87 = (void *)((uint8_t *)&F.l10);
            gm_b16_8 t88;
            GM_ST(void *, (uint8_t *)&t88, (void *)(gm_s2kvk8bnjk52t));
            GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(11));
            gm_tftb1odc739t5 t89 = gm_f_push_3ef746b(t87, t88);
            gm_l61 = t89;
            uint8_t t90 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
            if (!t90) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l61) + 1, gm_srcmok80ho8hh01, 124, 9);
            void * t91 = (void *)((uint8_t *)&F.l10);
            gm_b16_8 t92;
            GM_ST(void *, (uint8_t *)&t92, (void *)(gm_sdi44r3kug6n6));
            GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(13));
            gm_tftb1odc739t5 t93 = gm_f_push_3ef746b(t91, t92);
            gm_l62 = t93;
            uint8_t t94 = (uint8_t)!((((uint8_t *)&gm_l62))[0] != 0);
            if (!t94) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l62) + 1, gm_srcmok80ho8hh01, 125, 9);
        }
    }
    {
        void * t95 = gm_l2;
        gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)t95) + 32));
        gm_l11 = t96;
        gm_l12 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t97 = gm_l12;
                uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                uint8_t t99 = (t97 < t98);
                if (!t99) {
                    goto gm_lb1;
                }
                {
                    uint64_t t100 = gm_l12;
                    void * t101 = GM_LD(void *, ((uint8_t *)&gm_l11));
                    uint64_t t102 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                    uint64_t t103 = gm_l12;
                    uint32_t t104 = GM_LD(uint32_t, (((uint8_t *)t101) + (uintptr_t)t103 * 4));
                    gm_tn68el8d14vku t105;
                    GM_ST(uint32_t, (uint8_t *)&t105 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t105 + 0), t100);
                    GM_ST(uint32_t, ((uint8_t *)&t105 + 8), t104);
                    gm_l63 = t105;
                    {
                        {
                            uint64_t t106 = GM_LD(uint64_t, ((uint8_t *)&gm_l63));
                            gm_l13 = t106;
                            uint32_t t107 = GM_LD(uint32_t, (((uint8_t *)&gm_l63) + 8));
                            gm_l14 = t107;
                            void * t108 = gm_l2;
                            void * t109 = GM_LD(void *, (((uint8_t *)t108) + 48));
                            uint64_t t110 = GM_LD(uint64_t, ((((uint8_t *)t108) + 48) + 8));
                            uint32_t t111 = gm_l14;
                            uint64_t t112 = (uint64_t)t111;
                            if ((uintptr_t)t112 >= (uintptr_t)(t110)) gm_trap_index((uintptr_t)t112, t110, gm_srcmok80ho8hh01, 128, 14);
                            gm_t88rcucg1b7uk t113 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t109) + (uintptr_t)t112 * 40) + 16));
                            gm_l15 = t113;
                            gm_b16_8 t114;
                            GM_ST(void *, (uint8_t *)&t114, (void *)(gm_sr4qbrk6tlgaa));
                            GM_ST(uintptr_t, (uint8_t *)&t114 + 8, (uintptr_t)(0));
                            gm_l16 = t114;
                            {
                                {
                                    uint8_t t115 = gm_l5;
                                    if (!t115) {
                                        goto gm_lb7;
                                    }
                                    void * t116 = gm_l0;
                                    void * t117 = (void *)(((uint8_t *)t116) + 8);
                                    gm_t88rcucg1b7uk t118 = gm_l15;
                                    uint8_t t119 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t117, t118);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t120 = (uint8_t)!t119;
                                    if (!t120) {
                                        goto gm_lb7;
                                    }
                                    void * t121 = gm_l0;
                                    void * t122 = (void *)(((uint8_t *)t121) + 8);
                                    gm_t88rcucg1b7uk t123 = gm_l15;
                                    gm_b16_8 t124 = gm_f_plain_cty_q9lhlci((void *)NULL, cx, NULL, t122, t123);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l16 = t124;
                                    goto gm_lb6;
                                }
gm_lb7:;
                                void * t125 = gm_l0;
                                void * t126 = (void *)(((uint8_t *)t125) + 8);
                                gm_t88rcucg1b7uk t127 = gm_l15;
                                gm_b16_8 t128 = gm_f_param_cty_e8tvhrm((void *)NULL, cx, NULL, t126, t127);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l16 = t128;
                            }
gm_lb6:;
                            {
                                void * t129 = (void *)((uint8_t *)&F.l10);
                                gm_b16_8 t130 = gm_l16;
                                uint64_t t131 = gm_l13;
                                gm_t3877pk7lv748 t132;
                                GM_ST(gm_b16_8, ((uint8_t *)&t132 + 0), t130);
                                GM_ST(uint64_t, ((uint8_t *)&t132 + 16), t131);
                                gm_f_format_eq87866((void *)&gm_l64, (void *)NULL, cx, NULL, &t132);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t133 = (uint8_t)!((((uint8_t *)&gm_l64))[0] != 0);
                                if (!t133) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l64) + 8, gm_srcmok80ho8hh01, 131, 21);
                                gm_b16_8 t134 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l64) + 8));
                                gm_b16_8 t135 = GM_LD(gm_b16_8, ((uint8_t *)&t134));
                                gm_tftb1odc739t5 t136 = gm_f_push_3ef746b(t129, t135);
                                gm_l65 = t136;
                            }
                            uint8_t t137 = (uint8_t)!((((uint8_t *)&gm_l65))[0] != 0);
                            if (!t137) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l65) + 1, gm_srcmok80ho8hh01, 131, 9);
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 127, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t138 = gm_l12;
                uint64_t r139; bool o140 = __builtin_add_overflow(t138, ((uint64_t)1ULL), &r139);
                uint64_t t141 = r139;
                gm_l12 = t141;
            }
        }
gm_lb1:;
    }
    {
        uint64_t t142 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
        uint8_t t143 = ((t142 == ((uint64_t)0ULL)));
        if (t143) {
            void * t144 = (void *)((uint8_t *)&F.l10);
            gm_b16_8 t145;
            GM_ST(void *, (uint8_t *)&t145, (void *)(gm_sts5j6f5pkov0));
            GM_ST(uintptr_t, (uint8_t *)&t145 + 8, (uintptr_t)(4));
            gm_tftb1odc739t5 t146 = gm_f_push_3ef746b(t144, t145);
            gm_l66 = t146;
            uint8_t t147 = (uint8_t)!((((uint8_t *)&gm_l66))[0] != 0);
            if (!t147) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l66) + 1, gm_srcmok80ho8hh01, 133, 32);
        }
        {
            {
                {
                    {
                        uint8_t t148 = (uint8_t)((((uint8_t *)&gm_l4))[4] != 0);
                        if (t148) {
                            goto gm_lb11;
                        }
                        gm_l67 = ((uint8_t)1);
                        goto gm_lb10;
                    }
gm_lb11:;
                    {
                        gm_l67 = ((uint8_t)0);
                        goto gm_lb10;
                    }
gm_lb12:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 134, 8);
                }
gm_lb10:;
                uint8_t t149 = gm_l67;
                if (!t149) {
                    goto gm_lb9;
                }
                void * t150 = gm_l0;
                uint32_t t151 = gm_l1;
                uint8_t t152 = gm_f_needs_wrapper_ktlbb7i((void *)NULL, cx, NULL, t150, t151);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!t152) {
                    goto gm_lb9;
                }
                void * t153 = gm_l0;
                uint32_t t154 = gm_l1;
                void * t155 = gm_l2;
                gm_b0_1 t156 = gm_f_c_abi_wrapper_33vbmt4((void *)NULL, cx, NULL, t153, t154, t155);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_lb8;
            }
gm_lb9:;
        }
gm_lb8:;
        void * t157 = gm_l0;
        uint32_t t158 = gm_l1;
        gm_b16_8 t159 = gm_f_borrowed_params_b39va3r((void *)NULL, cx, NULL, t157, t158);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l17 = t159;
        void * t160 = gm_l0;
        uint32_t t161 = gm_l1;
        gm_b16_8 t162 = gm_f_frame_locals_mt4nb6o((void *)NULL, cx, NULL, t160, t161);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l18 = t162;
        void * t163 = gm_l0;
        uint8_t t164 = GM_LD(uint8_t, ((((uint8_t *)t163) + 1136) + 2));
        uint8_t t165 = (uint8_t)!t164;
        if (t165) {
            void * t166 = gm_l0;
            uint32_t t167 = gm_l1;
            uint8_t t168 = gm_f_long_body_qgvuu1c((void *)NULL, cx, NULL, t166, t167);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l68 = t168;
        } else {
            gm_l68 = ((uint8_t)0);
        }
        uint8_t t169 = gm_l68;
        gm_l19 = t169;
        void * t170 = gm_l0;
        uint32_t t171 = gm_l1;
        gm_tqh25f25b69k5 t172;
        gm_f_lir_of_60vujj2((void *)&t172, (void *)NULL, cx, NULL, t170, t171);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l20 = t172;
        void * t173 = gm_l0;
        void * t174 = gm_l2;
        void * t175 = (void *)((uint8_t *)&F.l20);
        gm_f_lir_check_bnbf23l((void *)&gm_l69, (void *)NULL, cx, NULL, t173, t174, t175);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t176 = (uint8_t)!((((uint8_t *)&gm_l69))[0] != 0);
        if (t176) {
        } else {
            gm_t08olq97rscn3 t177 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l69) + 8));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8), t177);
            goto gm_fdone;
        }
    }
    {
        gm_trqfvmjtt4sij t178;
        gm_f_list_s83jn2q((void *)&t178, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l21 = t178;
        void * t179 = (void *)((uint8_t *)&F.l21);
        gm_b16_8 t180 = gm_l18;
        gm_b16_8 t181 = GM_LD(gm_b16_8, ((uint8_t *)&t180));
        gm_tftb1odc739t5 t182 = gm_f_extend_07kdnvi(t179, t181);
        gm_l70 = t182;
        uint8_t t183 = (uint8_t)!((((uint8_t *)&gm_l70))[0] != 0);
        if (!t183) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l70) + 1, gm_srcmok80ho8hh01, 143, 5);
        void * t184 = gm_l0;
        uint32_t t185 = gm_l1;
        void * t186 = gm_l3;
        uint8_t t187 = gm_f_lir_framed_6i9516j((void *)NULL, cx, NULL, t184, t185, t186);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l22 = t187;
        {
            for (;;) {
                uint64_t t188 = GM_LD(uint64_t, (((uint8_t *)&F.l21) + 8));
                uint64_t t189 = GM_LD(uint64_t, ((((uint8_t *)&F.l20) + 8) + 8));
                uint8_t t190 = (t188 < t189);
                if (!t190) {
                    goto gm_lb13;
                }
                {
                    void * t191 = (void *)((uint8_t *)&F.l21);
                    uint8_t t192 = gm_l22;
                    gm_tftb1odc739t5 t193 = gm_f_push_vd1290p(t191, t192);
                    gm_l71 = t193;
                    uint8_t t194 = (uint8_t)!((((uint8_t *)&gm_l71))[0] != 0);
                    if (!t194) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l71) + 1, gm_srcmok80ho8hh01, 145, 43);
                }
gm_lb15:;
            }
        }
gm_lb13:;
        gm_b16_8 t195 = GM_LD(gm_b16_8, ((uint8_t *)&F.l21));
        gm_l18 = t195;
        gm_b16_8 t196;
        GM_ST(void *, (uint8_t *)&t196, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t196 + 8, (uintptr_t)(0));
        gm_l23 = t196;
        {
            {
                uint8_t t197 = (uint8_t)((((uint8_t *)&gm_l4))[4] != 0);
                if (!t197) {
                    goto gm_lb17;
                }
                uint32_t t198 = GM_LD(uint32_t, ((uint8_t *)&gm_l4));
                gm_l24 = t198;
                void * t199 = gm_l0;
                uint32_t t200 = gm_l1;
                uint32_t t201 = gm_l24;
                gm_b16_8 t202 = gm_f_spec_name_jptsbbf((void *)NULL, cx, NULL, t199, t200, t201);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l23 = t202;
                goto gm_lb16;
            }
gm_lb17:;
            void * t203 = gm_l0;
            uint32_t t204 = gm_l1;
            gm_b16_8 t205 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t203, t204);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l23 = t205;
        }
gm_lb16:;
        void * t206 = gm_l2;
        gm_t1h8kp9hsu3ua t207 = GM_LD(gm_t1h8kp9hsu3ua, (((uint8_t *)t206) + 145));
        gm_b16_8 t208 = gm_f_hint_attrs_mktdhi1((void *)NULL, cx, NULL, t207);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t209 = gm_l2;
        uint8_t t210 = GM_LD(uint8_t, (((uint8_t *)t209) + 144));
        gm_b16_8 t211 = gm_f_conv_attr_m0b89pc(t210);
        gm_b16_8 t212 = gm_l9;
        gm_b16_8 t213 = gm_l23;
        gm_b16_8 t214 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
        gm_b16_8 t215 = GM_LD(gm_b16_8, ((uint8_t *)&t214));
        gm_b16_8 t216;
        GM_ST(void *, (uint8_t *)&t216, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t216 + 8, (uintptr_t)(2));
        gm_f_join_sfknljb((void *)&gm_l72, (void *)NULL, cx, NULL, t215, t216);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t217 = (uint8_t)!((((uint8_t *)&gm_l72))[0] != 0);
        if (!t217) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l72) + 8, gm_srcmok80ho8hh01, 149, 132);
        gm_b16_8 t218 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l72) + 8));
        gm_tud7oovfvnf43 t219;
        GM_ST(gm_b16_8, ((uint8_t *)&t219 + 0), t208);
        GM_ST(gm_b16_8, ((uint8_t *)&t219 + 16), t211);
        GM_ST(gm_b16_8, ((uint8_t *)&t219 + 32), t212);
        GM_ST(gm_b16_8, ((uint8_t *)&t219 + 48), t213);
        GM_ST(gm_b16_8, ((uint8_t *)&t219 + 64), t218);
        gm_f_format_822rutp((void *)&gm_l73, (void *)NULL, cx, NULL, &t219);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t220 = (uint8_t)!((((uint8_t *)&gm_l73))[0] != 0);
        if (!t220) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l73) + 8, gm_srcmok80ho8hh01, 149, 12);
        gm_b16_8 t221 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l73) + 8));
        gm_l25 = t221;
        void * t222 = gm_l0;
        void * t223 = (void *)(((uint8_t *)t222) + 1944);
        gm_b16_8 t224 = gm_l25;
        gm_tnpifv20irnph t225;
        GM_ST(gm_b16_8, ((uint8_t *)&t225 + 0), t224);
        gm_tftb1odc739t5 t226 = gm_f_write_i2vvij8(t223, t225);
        gm_l74 = t226;
        uint8_t t227 = (uint8_t)!((((uint8_t *)&gm_l74))[0] != 0);
        if (!t227) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l74) + 1, gm_srcmok80ho8hh01, 150, 5);
        void * t228 = gm_l0;
        void * t229 = GM_LD(void *, ((uint8_t *)t228));
        void * t230 = GM_LD(void *, ((uint8_t *)t229));
        void * t231 = gm_l2;
        gm_b16_8 t232 = GM_LD(gm_b16_8, (((uint8_t *)t231) + 48));
        void * t233 = gm_l2;
        void * t234 = GM_LD(void *, (((uint8_t *)t233) + 152));
        uint64_t t235 = gm_f_frame_cost_oei64tv((void *)NULL, cx, NULL, t230, t232, t234);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l26 = t235;
        uint64_t t236 = gm_l26;
        uint64_t t237 = GM_LD(uint64_t, ((uint8_t *)gm_c1id0n0l1pf));
        uint8_t t238 = (t236 > t237);
        if (t238) {
            void * t239 = gm_l2;
            gm_b16_8 t240 = GM_LD(gm_b16_8, ((uint8_t *)t239));
            gm_b16_8 t241 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t240);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l27 = t241;
            {
                void * t242 = gm_l0;
                void * t243 = (void *)(((uint8_t *)t242) + 1984);
                gm_b16_8 t244 = gm_l25;
                gm_b16_8 t245 = gm_l27;
                uint64_t t246 = gm_l26;
                gm_tftksr4op114e t247;
                GM_ST(gm_b16_8, ((uint8_t *)&t247 + 0), t244);
                GM_ST(gm_b16_8, ((uint8_t *)&t247 + 16), t245);
                GM_ST(uint64_t, ((uint8_t *)&t247 + 32), t246);
                gm_tftb1odc739t5 t248 = gm_f_write_ajj3a37(t243, &t247);
                gm_l75 = t248;
            }
            uint8_t t249 = (uint8_t)!((((uint8_t *)&gm_l75))[0] != 0);
            if (!t249) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l75) + 1, gm_srcmok80ho8hh01, 156, 9);
            {
                memset(&(*(gm_tqgtd5gtgj65s *)gm_out), 0, sizeof (*(gm_tqgtd5gtgj65s *)gm_out));
                ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 0;
                GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8), GM_ZERO(gm_b0_1));
                goto gm_fdone;
            }
        }
        void * t250 = gm_l0;
        uint32_t t251 = gm_l1;
        gm_t9805qtonbsag t252 = gm_l4;
        gm_b16_8 t253 = gm_f_fn_tag_okd90ep((void *)NULL, cx, NULL, t250, t251, t252);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l28 = t253;
        void * t254 = gm_l0;
        uint32_t t255 = gm_l1;
        gm_b16_8 t256 = gm_l28;
        gm_t9805qtonbsag t257 = gm_l4;
        gm_t9805qtonbsag t258;
        memset(&t258, 0, sizeof t258);
        gm_th2lqbo77pvsc t259;
        memset(&t259, 0, sizeof t259);
        gm_th2lqbo77pvsc t260;
        memset(&t260, 0, sizeof t260);
        gm_th2lqbo77pvsc t261;
        memset(&t261, 0, sizeof t261);
        void * t262 = gm_l2;
        gm_trqfvmjtt4sij t263;
        gm_f_list_uvfu1k1((void *)&t263, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t264;
        gm_f_list_ubba23m((void *)&t264, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t265 = GM_LD(gm_b16_8, ((uint8_t *)gm_cgsmfcq937e_4));
        gm_tf5srh4mp4b3i t266;
        memset(&t266, 0, sizeof t266);
        gm_b16_8 t267 = gm_l17;
        gm_b16_8 t268 = gm_l18;
        uint8_t t269 = gm_l19;
        gm_trqfvmjtt4sij t270;
        gm_f_list_ns53od0((void *)&t270, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to0hr49bb3bdb t271;
        GM_ST(uint32_t, (uint8_t *)&t271 + 12, 0);
        GM_ST(uint32_t, (uint8_t *)&t271 + 180, 0);
        GM_ST(uint8_t, (uint8_t *)&t271 + 243, 0);
        GM_ST(uint32_t, (uint8_t *)&t271 + 244, 0);
        GM_ST(uint8_t, (uint8_t *)&t271 + 309, 0);
        GM_ST(uint16_t, (uint8_t *)&t271 + 310, 0);
        GM_ST(void *, ((uint8_t *)&t271 + 0), t254);
        GM_ST(uint32_t, ((uint8_t *)&t271 + 8), t255);
        GM_ST(gm_b16_8, ((uint8_t *)&t271 + 16), t256);
        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t271 + 32), t257);
        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t271 + 40), t258);
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t271 + 48), t259);
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t271 + 72), t260);
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t271 + 96), t261);
        GM_ST(void *, ((uint8_t *)&t271 + 120), t262);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t271 + 128), t263);
        GM_ST(uint64_t, ((uint8_t *)&t271 + 168), ((uint64_t)1ULL));
        GM_ST(uint32_t, ((uint8_t *)&t271 + 176), ((uint32_t)0ULL));
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t271 + 184), t264);
        GM_ST(gm_b16_8, ((uint8_t *)&t271 + 224), t265);
        GM_ST(uint8_t, ((uint8_t *)&t271 + 240), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t271 + 241), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t271 + 242), ((uint8_t)0));
        GM_ST(uint64_t, ((uint8_t *)&t271 + 248), ((uint64_t)0ULL));
        GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t271 + 256), t266);
        GM_ST(gm_b16_8, ((uint8_t *)&t271 + 272), t267);
        GM_ST(gm_b16_8, ((uint8_t *)&t271 + 288), t268);
        GM_ST(uint32_t, ((uint8_t *)&t271 + 304), ((uint32_t)0ULL));
        GM_ST(uint8_t, ((uint8_t *)&t271 + 308), t269);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t271 + 312), t270);
        GM_ST(void *, ((uint8_t *)&t271 + 352), ((void *)0));
        F.l29 = t271;
    }
    {
        void * t272 = (void *)(((uint8_t *)&F.l29) + 128);
        gm_b16_8 t273 = gm_l25;
        gm_tnpifv20irnph t274;
        GM_ST(gm_b16_8, ((uint8_t *)&t274 + 0), t273);
        gm_tftb1odc739t5 t275 = gm_f_write_n0vcum5(t272, t274);
        gm_l76 = t275;
        uint8_t t276 = (uint8_t)!((((uint8_t *)&gm_l76))[0] != 0);
        if (!t276) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l76) + 1, gm_srcmok80ho8hh01, 173, 5);
        void * t277 = (void *)((uint8_t *)&F.l29);
        void * t278 = gm_l2;
        gm_tl874t2atnlh3 t279 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t278) + 16));
        gm_b0_1 t280 = gm_f_src_line_d24sajl((void *)NULL, cx, NULL, t277, t279);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t281 = gm_l2;
        uint8_t t282 = GM_LD(uint8_t, (((uint8_t *)t281) + 145));
        if (t282) {
            void * t283 = (void *)((uint8_t *)&F.l29);
            gm_b16_8 t284;
            GM_ST(void *, (uint8_t *)&t284, (void *)(gm_s2gtin895e08a));
            GM_ST(uintptr_t, (uint8_t *)&t284 + 8, (uintptr_t)(17));
            gm_b0_1 t285 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t283, t284);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t286 = gm_l5;
        if (t286) {
            void * t287 = (void *)((uint8_t *)&F.l29);
            gm_b16_8 t288;
            GM_ST(void *, (uint8_t *)&t288, (void *)(gm_svdapq3j66qso));
            GM_ST(uintptr_t, (uint8_t *)&t288 + 8, (uintptr_t)(59));
            gm_b0_1 t289 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t287, t288);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        void * t290 = (void *)((uint8_t *)&F.l29);
        void * t291 = gm_l0;
        uint32_t t292 = gm_l1;
        gm_b16_8 t293 = gm_f_frame_name_stddmgg((void *)NULL, cx, NULL, t291, t292);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t294;
        GM_ST(gm_b16_8, ((uint8_t *)&t294 + 0), t293);
        gm_b0_1 t295 = gm_f_linef_4plvds6((void *)NULL, cx, NULL, t290, t294);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t296 = gm_l2;
        gm_b16_8 t297 = GM_LD(gm_b16_8, (((uint8_t *)t296) + 48));
        gm_l30 = t297;
        gm_l31 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t298 = gm_l31;
                uint64_t t299 = GM_LD(uint64_t, (((uint8_t *)&gm_l30) + 8));
                uint8_t t300 = (t298 < t299);
                if (!t300) {
                    goto gm_lb18;
                }
                {
                    {
                        uint64_t t301 = gm_l31;
                        void * t302 = GM_LD(void *, ((uint8_t *)&gm_l30));
                        uint64_t t303 = GM_LD(uint64_t, (((uint8_t *)&gm_l30) + 8));
                        uint64_t t304 = gm_l31;
                        gm_tsoie1ppgdvcg t305 = GM_LD(gm_tsoie1ppgdvcg, (((uint8_t *)t302) + (uintptr_t)t304 * 40));
                        GM_ST(uint64_t, ((uint8_t *)&gm_l77 + 0), t301);
                        GM_ST(gm_tsoie1ppgdvcg, ((uint8_t *)&gm_l77 + 8), t305);
                    }
                    {
                        {
                            uint64_t t306 = GM_LD(uint64_t, ((uint8_t *)&gm_l77));
                            gm_l32 = t306;
                            memmove(((uint8_t *)&F.l33), (((uint8_t *)&gm_l77) + 8), 40);
                            void * t307 = GM_LD(void *, (((uint8_t *)&F.l29) + 288));
                            uint64_t t308 = GM_LD(uint64_t, ((((uint8_t *)&F.l29) + 288) + 8));
                            uint64_t t309 = gm_l32;
                            if ((uintptr_t)t309 >= (uintptr_t)(t308)) gm_trap_index((uintptr_t)t309, t308, gm_srcmok80ho8hh01, 179, 16);
                            uint8_t t310 = GM_LD(uint8_t, (((uint8_t *)t307) + (uintptr_t)t309 * 1));
                            uint8_t t311 = (uint8_t)!t310;
                            if (t311) {
                                uint64_t t312 = GM_LD(uint64_t, (((uint8_t *)&F.l29) + 248));
                                void * t313 = GM_LD(void *, ((uint8_t *)&F.l29));
                                void * t314 = (void *)(((uint8_t *)t313) + 8);
                                gm_t88rcucg1b7uk t315 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l33) + 16));
                                uint64_t t316 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t314, t315);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint64_t r317; bool o318 = __builtin_add_overflow(t312, t316, &r317);
                                if (o318) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 180, 13);
                                uint64_t t319 = r317;
                                GM_ST(uint64_t, (((uint8_t *)&F.l29) + 248), t319);
                                void * t320 = GM_LD(void *, ((uint8_t *)&F.l29));
                                void * t321 = (void *)(((uint8_t *)t320) + 8);
                                gm_t88rcucg1b7uk t322 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l33) + 16));
                                gm_b16_8 t323 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t321, t322);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l34 = t323;
                                void * t324 = (void *)((uint8_t *)&F.l29);
                                gm_b16_8 t325 = gm_l34;
                                uint64_t t326 = gm_l32;
                                gm_b16_8 t327 = GM_LD(gm_b16_8, ((uint8_t *)&F.l33));
                                gm_tq3m5qgvkpje8 t328;
                                GM_ST(gm_b16_8, ((uint8_t *)&t328 + 0), t325);
                                GM_ST(uint64_t, ((uint8_t *)&t328 + 16), t326);
                                GM_ST(gm_b16_8, ((uint8_t *)&t328 + 24), t327);
                                gm_b0_1 t329 = gm_f_linef_md5jtn8((void *)NULL, cx, NULL, t324, &t328);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            goto gm_lb21;
                        }
gm_lb22:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 178, 5);
                    }
gm_lb21:;
                }
gm_lb20:;
                uint64_t t330 = gm_l31;
                uint64_t r331; bool o332 = __builtin_add_overflow(t330, ((uint64_t)1ULL), &r331);
                uint64_t t333 = r331;
                gm_l31 = t333;
            }
        }
gm_lb18:;
    }
    {
        {
            {
                void * t334 = GM_LD(void *, ((uint8_t *)&F.l29));
                uint8_t t335 = GM_LD(uint8_t, (((uint8_t *)t334) + 1136));
                if (!t335) {
                    goto gm_lb24;
                }
                void * t336 = GM_LD(void *, ((uint8_t *)&F.l29));
                void * t337 = gm_l2;
                uint8_t t338 = gm_f_probe_free_4hilimu((void *)NULL, cx, NULL, t336, t337);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t339 = (uint8_t)!t338;
                if (!t339) {
                    goto gm_lb24;
                }
                void * t340 = GM_LD(void *, ((uint8_t *)&F.l29));
                uint32_t t341 = gm_l1;
                uint32_t t342 = gm_f_call_depth_4m6ap7n((void *)NULL, cx, NULL, t340, t341);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint32_t t343 = GM_LD(uint32_t, ((uint8_t *)gm_cdmki9acqi9));
                uint8_t t344 = (t342 <= t343);
                if (t344) {
                    void * t345 = (void *)((uint8_t *)&F.l29);
                    gm_b16_8 t346;
                    GM_ST(void *, (uint8_t *)&t346, (void *)(gm_s8hh4pp2rpnlj));
                    GM_ST(uintptr_t, (uint8_t *)&t346 + 8, (uintptr_t)(42));
                    gm_b0_1 t347 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t345, t346);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                } else {
                    void * t348 = (void *)((uint8_t *)&F.l29);
                    gm_b16_8 t349;
                    GM_ST(void *, (uint8_t *)&t349, (void *)(gm_sjs17a3d2ohel));
                    GM_ST(uintptr_t, (uint8_t *)&t349 + 8, (uintptr_t)(34));
                    gm_b0_1 t350 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t348, t349);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint64_t t351 = GM_LD(uint64_t, ((((uint8_t *)&F.l29) + 128) + 8));
                gm_b16_8 t352;
                GM_ST(void *, (uint8_t *)&t352, (void *)(gm_spdntfv7vkd2t));
                GM_ST(uintptr_t, (uint8_t *)&t352 + 8, (uintptr_t)(16));
                gm_l78 = t352;
                uint64_t t353 = GM_LD(uint64_t, (((uint8_t *)&gm_l78) + 8));
                uint64_t r354; bool o355 = __builtin_sub_overflow(t351, t353, &r354);
                if (o355) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 192, 23);
                uint64_t t356 = r354;
                gm_tf5srh4mp4b3i t357;
                GM_ST(uint8_t, (uint8_t *)&t357 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t357 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t357 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t357), t356);
                ((uint8_t *)&t357)[8] = 1;
                GM_ST(gm_tf5srh4mp4b3i, (((uint8_t *)&F.l29) + 256), t357);
                goto gm_lb23;
            }
gm_lb24:;
        }
gm_lb23:;
        gm_b16_8 t358;
        GM_ST(void *, (uint8_t *)&t358, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t358 + 8, (uintptr_t)(0));
        gm_l35 = t358;
        void * t359 = GM_LD(void *, ((uint8_t *)&F.l29));
        uint8_t t360 = GM_LD(uint8_t, ((((uint8_t *)t359) + 1136) + 1));
        if (t360) {
            gm_b16_8 t361;
            GM_ST(void *, (uint8_t *)&t361, (void *)(gm_s04qus0luoga5));
            GM_ST(uintptr_t, (uint8_t *)&t361 + 8, (uintptr_t)(4));
            gm_l36 = t361;
            {
                {
                    void * t362 = GM_LD(void *, ((uint8_t *)&F.l29));
                    void * t363 = GM_LD(void *, ((uint8_t *)t362));
                    uint32_t t364 = gm_l1;
                    gm_f_backtrace_name_mars429((void *)&gm_l79, (void *)NULL, cx, NULL, t363, t364);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t365 = (uint8_t)((((uint8_t *)&gm_l79))[16] != 0);
                    if (!t365) {
                        goto gm_lb26;
                    }
                    gm_b16_8 t366 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l79));
                    gm_l37 = t366;
                    gm_b16_8 t367 = gm_l37;
                    gm_b16_8 t368 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t367);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l36 = t368;
                    goto gm_lb25;
                }
gm_lb26:;
            }
gm_lb25:;
            void * t369 = (void *)((uint8_t *)&F.l29);
            gm_b16_8 t370 = gm_l36;
            gm_tnpifv20irnph t371;
            GM_ST(gm_b16_8, ((uint8_t *)&t371 + 0), t370);
            gm_b0_1 t372 = gm_f_linef_cuactnf((void *)NULL, cx, NULL, t369, t371);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t373;
            GM_ST(void *, (uint8_t *)&t373, (void *)(gm_sn2gg3b98t635));
            GM_ST(uintptr_t, (uint8_t *)&t373 + 8, (uintptr_t)(20));
            gm_l35 = t373;
        }
        void * t374 = (void *)((uint8_t *)&F.l29);
        gm_b16_8 t375;
        GM_ST(void *, (uint8_t *)&t375, (void *)(gm_s2micd2hub1nf));
        GM_ST(uintptr_t, (uint8_t *)&t375 + 8, (uintptr_t)(25));
        gm_b0_1 t376 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t374, t375);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t377 = gm_l2;
        uint8_t t378 = GM_LD(uint8_t, ((((uint8_t *)t377) + 96) + 0));
        uint8_t t379 = (uint8_t)((t378 == ((uint8_t)2ULL)));
        gm_tq9qng821udh4 t380 = GM_LD(gm_tq9qng821udh4, (((uint8_t *)t377) + 96));
        gm_tq9qng821udh4 t381;
        memset(&t381, 0, sizeof t381);
        GM_ST(uint8_t, ((uint8_t *)&t381 + 0), ((uint8_t)2ULL));
        if (t379) {
            gm_l38 = ((int32_t)1LL);
        } else {
            gm_l38 = ((int32_t)0LL);
        }
    }
    {
        void * t382 = (void *)((uint8_t *)&F.l29);
        int32_t t383 = gm_l38;
        gm_t19enj90vg8bd t384;
        GM_ST(int32_t, ((uint8_t *)&t384 + 0), t383);
        gm_b0_1 t385 = gm_f_linef_cs9kbvs((void *)NULL, cx, NULL, t382, t384);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t386 = (void *)((uint8_t *)&F.l29);
        gm_b16_8 t387;
        GM_ST(void *, (uint8_t *)&t387, (void *)(gm_sdikuksp636di));
        GM_ST(uintptr_t, (uint8_t *)&t387 + 8, (uintptr_t)(11));
        gm_b0_1 t388 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t386, t387);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t389 = gm_l2;
        gm_b16_8 t390 = GM_LD(gm_b16_8, (((uint8_t *)t389) + 32));
        gm_l39 = t390;
        gm_l40 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t391 = gm_l40;
                uint64_t t392 = GM_LD(uint64_t, (((uint8_t *)&gm_l39) + 8));
                uint8_t t393 = (t391 < t392);
                if (!t393) {
                    goto gm_lb27;
                }
                {
                    uint64_t t394 = gm_l40;
                    void * t395 = GM_LD(void *, ((uint8_t *)&gm_l39));
                    uint64_t t396 = GM_LD(uint64_t, (((uint8_t *)&gm_l39) + 8));
                    uint64_t t397 = gm_l40;
                    uint32_t t398 = GM_LD(uint32_t, (((uint8_t *)t395) + (uintptr_t)t397 * 4));
                    gm_tn68el8d14vku t399;
                    GM_ST(uint32_t, (uint8_t *)&t399 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t399 + 0), t394);
                    GM_ST(uint32_t, ((uint8_t *)&t399 + 8), t398);
                    gm_l80 = t399;
                    {
                        {
                            uint64_t t400 = GM_LD(uint64_t, ((uint8_t *)&gm_l80));
                            gm_l41 = t400;
                            uint32_t t401 = GM_LD(uint32_t, (((uint8_t *)&gm_l80) + 8));
                            gm_l42 = t401;
                            void * t402 = gm_l2;
                            void * t403 = GM_LD(void *, (((uint8_t *)t402) + 48));
                            uint64_t t404 = GM_LD(uint64_t, ((((uint8_t *)t402) + 48) + 8));
                            uint32_t t405 = gm_l42;
                            uint64_t t406 = (uint64_t)t405;
                            if ((uintptr_t)t406 >= (uintptr_t)(t404)) gm_trap_index((uintptr_t)t406, t404, gm_srcmok80ho8hh01, 207, 14);
                            gm_t88rcucg1b7uk t407 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t403) + (uintptr_t)t406 * 40) + 16));
                            gm_l43 = t407;
                            void * t408 = GM_LD(void *, (((uint8_t *)&F.l29) + 272));
                            uint64_t t409 = GM_LD(uint64_t, ((((uint8_t *)&F.l29) + 272) + 8));
                            uint64_t t410 = gm_l41;
                            if ((uintptr_t)t410 >= (uintptr_t)(t409)) gm_trap_index((uintptr_t)t410, t409, gm_srcmok80ho8hh01, 208, 12);
                            uint8_t t411 = GM_LD(uint8_t, (((uint8_t *)t408) + (uintptr_t)t410 * 1));
                            if (t411) {
                                goto gm_lb29;
                            }
                            void * t412 = GM_LD(void *, ((uint8_t *)&F.l29));
                            void * t413 = (void *)(((uint8_t *)t412) + 8);
                            gm_t88rcucg1b7uk t414 = gm_l43;
                            uint8_t t415 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t413, t414);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            if (t415) {
                                void * t416 = (void *)((uint8_t *)&F.l29);
                                uint32_t t417 = gm_l42;
                                gm_b16_8 t418 = gm_f_local_lv_m2jtf0n((void *)NULL, cx, NULL, t416, t417);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l44 = t418;
                                void * t419 = (void *)((uint8_t *)&F.l29);
                                gm_b16_8 t420 = gm_l44;
                                uint64_t t421 = gm_l41;
                                gm_b16_8 t422 = gm_l44;
                                gm_tq3m5qgvkpje8 t423;
                                GM_ST(gm_b16_8, ((uint8_t *)&t423 + 0), t420);
                                GM_ST(uint64_t, ((uint8_t *)&t423 + 16), t421);
                                GM_ST(gm_b16_8, ((uint8_t *)&t423 + 24), t422);
                                gm_b0_1 t424 = gm_f_linef_nvqsj6t((void *)NULL, cx, NULL, t419, &t423);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb29;
                            }
                            {
                                uint64_t t425 = gm_l41;
                                gm_t4ce0jd3tdro4 t426;
                                GM_ST(uint64_t, ((uint8_t *)&t426 + 0), t425);
                                gm_f_format_l05am3t((void *)&gm_l81, (void *)NULL, cx, NULL, t426);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            uint8_t t427 = (uint8_t)!((((uint8_t *)&gm_l81))[0] != 0);
                            if (!t427) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l81) + 8, gm_srcmok80ho8hh01, 214, 15);
                            gm_b16_8 t428 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l81) + 8));
                            gm_l45 = t428;
                            gm_b16_8 t429 = gm_l45;
                            gm_b16_8 t430 = GM_LD(gm_b16_8, ((uint8_t *)&t429));
                            gm_l46 = t430;
                            uint8_t t431 = gm_l5;
                            if (t431) {
                                void * t432 = (void *)((uint8_t *)&F.l29);
                                gm_t88rcucg1b7uk t433 = gm_l43;
                                gm_b16_8 t434 = gm_l45;
                                gm_b16_8 t435 = GM_LD(gm_b16_8, ((uint8_t *)&t434));
                                gm_b16_8 t436 = gm_f_gamma_value_rgbfhcc((void *)NULL, cx, NULL, t432, t433, t435);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l46 = t436;
                            }
                            void * t437 = (void *)((uint8_t *)&F.l29);
                            uint32_t t438 = gm_l42;
                            gm_b16_8 t439 = gm_f_local_lv_m2jtf0n((void *)NULL, cx, NULL, t437, t438);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l47 = t439;
                            {
                                void * t440 = (void *)((uint8_t *)&F.l29);
                                gm_b16_8 t441 = gm_l47;
                                gm_b16_8 t442 = gm_l46;
                                gm_t6j6smj7qoctu t443;
                                GM_ST(gm_b16_8, ((uint8_t *)&t443 + 0), t441);
                                GM_ST(gm_b16_8, ((uint8_t *)&t443 + 16), t442);
                                gm_b0_1 t444 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t440, &t443);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            goto gm_lb30;
                        }
gm_lb31:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 206, 5);
                    }
gm_lb30:;
                }
gm_lb29:;
                uint64_t t445 = gm_l40;
                uint64_t r446; bool o447 = __builtin_add_overflow(t445, ((uint64_t)1ULL), &r446);
                uint64_t t448 = r446;
                gm_l40 = t448;
            }
        }
gm_lb27:;
    }
    {
        void * t449 = (void *)((uint8_t *)&F.l29);
        void * t450 = (void *)((uint8_t *)&F.l20);
        gm_f_lir_body_90044es((void *)&gm_l82, (void *)NULL, cx, NULL, t449, t450);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t451 = (uint8_t)!((((uint8_t *)&gm_l82))[0] != 0);
        if (t451) {
        } else {
            gm_t08olq97rscn3 t452 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l82) + 8));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8), t452);
            goto gm_fdone;
        }
        uint8_t t453 = GM_LD(uint8_t, (((uint8_t *)&F.l29) + 241));
        if (t453) {
            void * t454 = (void *)((uint8_t *)&F.l29);
            gm_b16_8 t455;
            GM_ST(void *, (uint8_t *)&t455, (void *)(gm_s0r850bpi2rnm));
            GM_ST(uintptr_t, (uint8_t *)&t455 + 8, (uintptr_t)(13));
            gm_b0_1 t456 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t454, t455);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t457 = (void *)((uint8_t *)&F.l29);
            gm_b16_8 t458;
            GM_ST(void *, (uint8_t *)&t458, (void *)(gm_s2hi6cc9n75l5));
            GM_ST(uintptr_t, (uint8_t *)&t458 + 8, (uintptr_t)(6));
            gm_b0_1 t459 = gm_f_label_710oev4((void *)NULL, cx, NULL, t457, t458);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t460 = (void *)((uint8_t *)&F.l29);
            gm_b16_8 t461;
            GM_ST(void *, (uint8_t *)&t461, (void *)(gm_spdft68ac8308));
            GM_ST(uintptr_t, (uint8_t *)&t461 + 8, (uintptr_t)(94));
            gm_b0_1 t462 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t460, t461);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t463 = (void *)((uint8_t *)&F.l29);
            gm_b16_8 t464;
            GM_ST(void *, (uint8_t *)&t464, (void *)(gm_sgdqi8a9n3f12));
            GM_ST(uintptr_t, (uint8_t *)&t464 + 8, (uintptr_t)(14));
            gm_b0_1 t465 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t463, t464);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        gm_b16_8 t466;
        GM_ST(void *, (uint8_t *)&t466, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t466 + 8, (uintptr_t)(0));
        gm_l48 = t466;
        void * t467 = GM_LD(void *, ((uint8_t *)&F.l29));
        uint8_t t468 = GM_LD(uint8_t, ((((uint8_t *)t467) + 1136) + 1));
        if (t468) {
            gm_b16_8 t469 = gm_l35;
            gm_tnpifv20irnph t470;
            GM_ST(gm_b16_8, ((uint8_t *)&t470 + 0), t469);
            gm_f_format_d6fj9tl((void *)&gm_l83, (void *)NULL, cx, NULL, t470);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t471 = (uint8_t)!((((uint8_t *)&gm_l83))[0] != 0);
            if (!t471) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l83) + 8, gm_srcmok80ho8hh01, 229, 33);
            gm_b16_8 t472 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l83) + 8));
            gm_b16_8 t473 = GM_LD(gm_b16_8, ((uint8_t *)&t472));
            gm_l48 = t473;
        }
        uint8_t t474 = gm_l6;
        if (t474) {
            void * t475 = (void *)(((uint8_t *)&F.l29) + 128);
            gm_b16_8 t476 = gm_l48;
            gm_b16_8 t477 = gm_l48;
            gm_t6j6smj7qoctu t478;
            GM_ST(gm_b16_8, ((uint8_t *)&t478 + 0), t476);
            GM_ST(gm_b16_8, ((uint8_t *)&t478 + 16), t477);
            gm_tftb1odc739t5 t479 = gm_f_write_va1cfg2(t475, &t478);
            gm_l84 = t479;
            uint8_t t480 = (uint8_t)!((((uint8_t *)&gm_l84))[0] != 0);
            if (!t480) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l84) + 1, gm_srcmok80ho8hh01, 232, 9);
        } else {
            uint8_t t481 = gm_l7;
            if (t481) {
                void * t482 = (void *)(((uint8_t *)&F.l29) + 128);
                gm_b16_8 t483 = gm_l48;
                gm_b16_8 t484 = gm_l48;
                gm_t6j6smj7qoctu t485;
                GM_ST(gm_b16_8, ((uint8_t *)&t485 + 0), t483);
                GM_ST(gm_b16_8, ((uint8_t *)&t485 + 16), t484);
                gm_tftb1odc739t5 t486 = gm_f_write_m7rqe8h(t482, &t485);
                gm_l85 = t486;
                uint8_t t487 = (uint8_t)!((((uint8_t *)&gm_l85))[0] != 0);
                if (!t487) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l85) + 1, gm_srcmok80ho8hh01, 234, 9);
            } else {
                uint64_t t488 = GM_LD(uint64_t, (((uint8_t *)&gm_l48) + 8));
                uint8_t t489 = ((t488 == ((uint64_t)0ULL)));
                if (t489) {
                    gm_b16_8 t490;
                    GM_ST(void *, (uint8_t *)&t490, (void *)(gm_svg1jvtfj7fgp));
                    GM_ST(uintptr_t, (uint8_t *)&t490 + 8, (uintptr_t)(5));
                    gm_l49 = t490;
                    uint8_t t491 = gm_l8;
                    uint8_t t492 = (uint8_t)!t491;
                    if (t492) {
                        void * t493 = (void *)((uint8_t *)&F.l29);
                        void * t494 = gm_l2;
                        gm_t88rcucg1b7uk t495 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t494) + 64));
                        gm_b16_8 t496;
                        GM_ST(void *, (uint8_t *)&t496, (void *)(gm_svg1jvtfj7fgp));
                        GM_ST(uintptr_t, (uint8_t *)&t496 + 8, (uintptr_t)(5));
                        gm_b16_8 t497 = gm_f_plain_value_e380k1u((void *)NULL, cx, NULL, t493, t495, t496);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l49 = t497;
                    }
                    void * t498 = (void *)(((uint8_t *)&F.l29) + 128);
                    gm_b16_8 t499 = gm_l49;
                    gm_b16_8 t500 = gm_l49;
                    gm_t6j6smj7qoctu t501;
                    GM_ST(gm_b16_8, ((uint8_t *)&t501 + 0), t499);
                    GM_ST(gm_b16_8, ((uint8_t *)&t501 + 16), t500);
                    gm_tftb1odc739t5 t502 = gm_f_write_3c9chj4(t498, &t501);
                    gm_l86 = t502;
                    uint8_t t503 = (uint8_t)!((((uint8_t *)&gm_l86))[0] != 0);
                    if (!t503) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l86) + 1, gm_srcmok80ho8hh01, 238, 9);
                } else {
                    gm_b16_8 t504;
                    GM_ST(void *, (uint8_t *)&t504, (void *)(gm_ss03naco6s5l7));
                    GM_ST(uintptr_t, (uint8_t *)&t504 + 8, (uintptr_t)(4));
                    gm_l50 = t504;
                    uint8_t t505 = gm_l8;
                    uint8_t t506 = (uint8_t)!t505;
                    if (t506) {
                        void * t507 = (void *)((uint8_t *)&F.l29);
                        void * t508 = gm_l2;
                        gm_t88rcucg1b7uk t509 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t508) + 64));
                        gm_b16_8 t510;
                        GM_ST(void *, (uint8_t *)&t510, (void *)(gm_ss03naco6s5l7));
                        GM_ST(uintptr_t, (uint8_t *)&t510 + 8, (uintptr_t)(4));
                        gm_b16_8 t511 = gm_f_plain_value_e380k1u((void *)NULL, cx, NULL, t507, t509, t510);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l50 = t511;
                    }
                    void * t512 = GM_LD(void *, ((uint8_t *)&F.l29));
                    void * t513 = (void *)(((uint8_t *)t512) + 8);
                    void * t514 = gm_l2;
                    gm_t88rcucg1b7uk t515 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t514) + 64));
                    gm_b16_8 t516 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t513, t515);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l51 = t516;
                    {
                        gm_b16_8 t517 = gm_l51;
                        gm_b16_8 t518 = gm_l48;
                        gm_b16_8 t519 = gm_l50;
                        gm_tfeab279c16rr t520;
                        GM_ST(gm_b16_8, ((uint8_t *)&t520 + 0), t517);
                        GM_ST(gm_b16_8, ((uint8_t *)&t520 + 16), t518);
                        GM_ST(gm_b16_8, ((uint8_t *)&t520 + 32), t519);
                        gm_f_format_hi89jlk((void *)&gm_l87, (void *)NULL, cx, NULL, &t520);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t521 = (uint8_t)!((((uint8_t *)&gm_l87))[0] != 0);
                    if (!t521) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l87) + 8, gm_srcmok80ho8hh01, 243, 17);
                    gm_b16_8 t522 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l87) + 8));
                    gm_l52 = t522;
                    {
                        void * t523 = (void *)(((uint8_t *)&F.l29) + 128);
                        gm_b16_8 t524 = gm_l52;
                        gm_b16_8 t525 = gm_l52;
                        gm_t6j6smj7qoctu t526;
                        GM_ST(gm_b16_8, ((uint8_t *)&t526 + 0), t524);
                        GM_ST(gm_b16_8, ((uint8_t *)&t526 + 16), t525);
                        gm_tftb1odc739t5 t527 = gm_f_write_ih4qokb(t523, &t526);
                        gm_l88 = t527;
                    }
                    uint8_t t528 = (uint8_t)!((((uint8_t *)&gm_l88))[0] != 0);
                    if (!t528) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l88) + 1, gm_srcmok80ho8hh01, 244, 9);
                }
            }
        }
    }
    {
        uint8_t t529 = gm_l6;
        uint8_t t530 = (uint8_t)!t529;
        if (t530) {
            uint64_t t531 = GM_LD(uint64_t, (((uint8_t *)&F.l29) + 248));
            void * t532 = GM_LD(void *, ((uint8_t *)&F.l29));
            void * t533 = (void *)(((uint8_t *)t532) + 8);
            void * t534 = gm_l2;
            gm_t88rcucg1b7uk t535 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t534) + 64));
            uint64_t t536 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t533, t535);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t r537; bool o538 = __builtin_mul_overflow(((uint64_t)2ULL), t536, &r537);
            if (o538) gm_trap("integer overflow in `*`", gm_srcmok80ho8hh01, 247, 39);
            uint64_t t539 = r537;
            uint64_t r540; bool o541 = __builtin_add_overflow(t531, t539, &r540);
            if (o541) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 247, 22);
            uint64_t t542 = r540;
            GM_ST(uint64_t, (((uint8_t *)&F.l29) + 248), t542);
        }
        void * t543 = (void *)((uint8_t *)&F.l29);
        gm_b0_1 t544 = gm_f_end_lines_99v0bhd((void *)NULL, cx, NULL, t543);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t545 = GM_LD(gm_b16_8, (((uint8_t *)&F.l29) + 128));
        gm_l53 = t545;
        gm_b16_8 t546 = gm_l53;
        gm_b16_8 t547 = GM_LD(gm_b16_8, ((uint8_t *)&t546));
        gm_b16_8 t548;
        GM_ST(void *, (uint8_t *)&t548, (void *)(gm_so514oj5nbhcu));
        GM_ST(uintptr_t, (uint8_t *)&t548 + 8, (uintptr_t)(1));
        gm_tf5srh4mp4b3i t549 = gm_f_find_nab29co(t547, t548);
        gm_l89 = t549;
        uint8_t t550 = (uint8_t)((((uint8_t *)&gm_l89))[8] != 0);
        if (t550) {
            uint64_t t551 = GM_LD(uint64_t, ((uint8_t *)&gm_l89));
            gm_l54 = t551;
        } else {
            uint64_t t552 = GM_LD(uint64_t, (((uint8_t *)&gm_l53) + 8));
            gm_l54 = t552;
        }
        void * t553 = GM_LD(void *, ((uint8_t *)&gm_l53));
        uint64_t t554 = GM_LD(uint64_t, (((uint8_t *)&gm_l53) + 8));
        uint64_t t555 = gm_l54;
        uint64_t t556 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t557 = (uintptr_t)t555;
        if (t556 > t557 || t557 > (uintptr_t)t554) gm_trap_slice(t556, t557, t554, gm_srcmok80ho8hh01, 253, 22);
        uint64_t t558 = ((uintptr_t)t555) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t559;
        GM_ST(void *, (uint8_t *)&t559, (void *)(((uint8_t *)t553 + (uintptr_t)((uint64_t)0ULL) * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t559 + 8, (uintptr_t)(t558));
        gm_b16_8 t560 = GM_LD(gm_b16_8, ((uint8_t *)&t559));
        gm_l55 = t560;
        {
            {
                void * t561 = GM_LD(void *, ((uint8_t *)&F.l29));
                uint8_t t562 = GM_LD(uint8_t, ((((uint8_t *)t561) + 1136) + 1));
                uint8_t t563 = (uint8_t)!t562;
                if (!t563) {
                    goto gm_lb33;
                }
                uint32_t t564 = GM_LD(uint32_t, (((uint8_t *)&F.l29) + 176));
                uint32_t t565 = GM_LD(uint32_t, ((uint8_t *)gm_csshki16t34));
                uint8_t t566 = (t564 <= t565);
                if (!t566) {
                    goto gm_lb33;
                }
                void * t567 = gm_l2;
                {
                    {
                        uint8_t t568 = (uint8_t)((((((uint8_t *)t567) + 145) + 1))[1] != 0);
                        if (t568) {
                            goto gm_lb35;
                        }
                        gm_l90 = ((uint8_t)1);
                        goto gm_lb34;
                    }
gm_lb35:;
                    {
                        gm_l90 = ((uint8_t)0);
                        goto gm_lb34;
                    }
gm_lb36:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 254, 65);
                }
gm_lb34:;
                uint8_t t569 = gm_l90;
                if (!t569) {
                    goto gm_lb33;
                }
                gm_b16_8 t570 = gm_l55;
                gm_b16_8 t571;
                GM_ST(void *, (uint8_t *)&t571, (void *)(gm_skdv3lmatcupg));
                GM_ST(uintptr_t, (uint8_t *)&t571 + 8, (uintptr_t)(20));
                gm_b16_8 t572;
                GM_ST(void *, (uint8_t *)&t572, (void *)(gm_sapbk38ibps8q));
                GM_ST(uintptr_t, (uint8_t *)&t572 + 8, (uintptr_t)(27));
                gm_b16_8 t573 = gm_f_replacen1_7ksfmmn((void *)NULL, cx, NULL, t570, t571, t572);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l55 = t573;
                goto gm_lb32;
            }
gm_lb33:;
        }
gm_lb32:;
        uint32_t t574 = GM_LD(uint32_t, (((uint8_t *)&F.l29) + 304));
        uint32_t t575 = GM_LD(uint32_t, ((uint8_t *)gm_cvpsnk4jni3));
        uint8_t t576 = (t574 > t575);
        if (t576) {
            gm_b16_8 t577 = gm_l55;
            gm_b16_8 t578;
            GM_ST(void *, (uint8_t *)&t578, (void *)(gm_skdv3lmatcupg));
            GM_ST(uintptr_t, (uint8_t *)&t578 + 8, (uintptr_t)(20));
            gm_b16_8 t579;
            GM_ST(void *, (uint8_t *)&t579, (void *)(gm_soah6d9unb7uo));
            GM_ST(uintptr_t, (uint8_t *)&t579 + 8, (uintptr_t)(31));
            gm_b16_8 t580 = gm_f_replacen1_7ksfmmn((void *)NULL, cx, NULL, t577, t578, t579);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l55 = t580;
        }
    }
    {
        {
            {
                uint32_t t581 = GM_LD(uint32_t, (((uint8_t *)&F.l29) + 304));
                uint32_t t582 = GM_LD(uint32_t, ((uint8_t *)gm_ctq6t8frtor));
                uint8_t t583 = (t581 > t582);
                if (!t583) {
                    goto gm_lb38;
                }
                gm_b16_8 t584 = gm_l55;
                gm_b16_8 t585;
                GM_ST(void *, (uint8_t *)&t585, (void *)(gm_soa9mnl4ahjud));
                GM_ST(uintptr_t, (uint8_t *)&t585 + 8, (uintptr_t)(7));
                uint8_t t586 = gm_f_starts_with_6vvt01e(t584, t585);
                if (!t586) {
                    goto gm_lb38;
                }
                gm_b16_8 t587 = gm_l55;
                gm_b16_8 t588;
                GM_ST(void *, (uint8_t *)&t588, (void *)(gm_s65rai75evpi7));
                GM_ST(uintptr_t, (uint8_t *)&t588 + 8, (uintptr_t)(13));
                uint8_t t589 = gm_f_contains_gkki4kq(t587, t588);
                uint8_t t590 = (uint8_t)!t589;
                if (!t590) {
                    goto gm_lb38;
                }
                gm_b16_8 t591 = gm_l55;
                gm_b16_8 t592;
                GM_ST(void *, (uint8_t *)&t592, (void *)(gm_soa9mnl4ahjud));
                GM_ST(uintptr_t, (uint8_t *)&t592 + 8, (uintptr_t)(7));
                gm_b16_8 t593;
                GM_ST(void *, (uint8_t *)&t593, (void *)(gm_smap40unho18s));
                GM_ST(uintptr_t, (uint8_t *)&t593 + 8, (uintptr_t)(18));
                gm_b16_8 t594 = gm_f_replacen1_7ksfmmn((void *)NULL, cx, NULL, t591, t592, t593);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l55 = t594;
                goto gm_lb37;
            }
gm_lb38:;
        }
gm_lb37:;
        void * t595 = gm_l0;
        void * t596 = (void *)(((uint8_t *)t595) + 1984);
        gm_b16_8 t597 = gm_l55;
        gm_tftb1odc739t5 t598 = gm_f_extend_0cs85gs(t596, t597);
        gm_l91 = t598;
        uint8_t t599 = (uint8_t)!((((uint8_t *)&gm_l91))[0] != 0);
        if (!t599) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l91) + 1, gm_srcmok80ho8hh01, 265, 5);
        {
            {
                uint8_t t600 = (uint8_t)(((((uint8_t *)&F.l29) + 256))[8] != 0);
                if (!t600) {
                    goto gm_lb40;
                }
                uint64_t t601 = GM_LD(uint64_t, (((uint8_t *)&F.l29) + 256));
                gm_l56 = t601;
                void * t602 = gm_l0;
                void * t603 = (void *)(((uint8_t *)t602) + 1984);
                void * t604 = GM_LD(void *, ((uint8_t *)&gm_l53));
                uint64_t t605 = GM_LD(uint64_t, (((uint8_t *)&gm_l53) + 8));
                uint64_t t606 = gm_l54;
                uint64_t t607 = gm_l56;
                uint64_t t608 = (uintptr_t)t606;
                uint64_t t609 = (uintptr_t)t607;
                if (t608 > t609 || t609 > (uintptr_t)t605) gm_trap_slice(t608, t609, t605, gm_srcmok80ho8hh01, 267, 25);
                uint64_t t610 = ((uintptr_t)t607) - (uintptr_t)t606;
                gm_b16_8 t611;
                GM_ST(void *, (uint8_t *)&t611, (void *)(((uint8_t *)t604 + (uintptr_t)t606 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t611 + 8, (uintptr_t)(t610));
                gm_b16_8 t612 = GM_LD(gm_b16_8, ((uint8_t *)&t611));
                gm_tftb1odc739t5 t613 = gm_f_extend_0cs85gs(t603, t612);
                gm_l92 = t613;
                uint8_t t614 = (uint8_t)!((((uint8_t *)&gm_l92))[0] != 0);
                if (!t614) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l92) + 1, gm_srcmok80ho8hh01, 267, 9);
                void * t615 = gm_l0;
                void * t616 = (void *)(((uint8_t *)t615) + 1984);
                uint64_t t617 = GM_LD(uint64_t, (((uint8_t *)&F.l29) + 248));
                gm_t4ce0jd3tdro4 t618;
                GM_ST(uint64_t, ((uint8_t *)&t618 + 0), t617);
                gm_tftb1odc739t5 t619 = gm_f_write_l0vqbl8(t616, t618);
                gm_l93 = t619;
                uint8_t t620 = (uint8_t)!((((uint8_t *)&gm_l93))[0] != 0);
                if (!t620) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l93) + 1, gm_srcmok80ho8hh01, 268, 9);
                void * t621 = gm_l0;
                void * t622 = (void *)(((uint8_t *)t621) + 1984);
                void * t623 = GM_LD(void *, ((uint8_t *)&gm_l53));
                uint64_t t624 = GM_LD(uint64_t, (((uint8_t *)&gm_l53) + 8));
                uint64_t t625 = gm_l56;
                gm_b16_8 t626;
                GM_ST(void *, (uint8_t *)&t626, (void *)(gm_sv394jlms97cd));
                GM_ST(uintptr_t, (uint8_t *)&t626 + 8, (uintptr_t)(13));
                gm_l94 = t626;
                uint64_t t627 = GM_LD(uint64_t, (((uint8_t *)&gm_l94) + 8));
                uint64_t r628; bool o629 = __builtin_add_overflow(t625, t627, &r628);
                if (o629) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 269, 30);
                uint64_t t630 = r628;
                uint64_t t631 = (uintptr_t)t630;
                uint64_t t632 = (uintptr_t)t624;
                if (t631 > t632 || t632 > (uintptr_t)t624) gm_trap_slice(t631, t632, t624, gm_srcmok80ho8hh01, 269, 25);
                uint64_t t633 = ((uintptr_t)t624) - (uintptr_t)t630;
                gm_b16_8 t634;
                GM_ST(void *, (uint8_t *)&t634, (void *)(((uint8_t *)t623 + (uintptr_t)t630 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t634 + 8, (uintptr_t)(t633));
                gm_b16_8 t635 = GM_LD(gm_b16_8, ((uint8_t *)&t634));
                gm_tftb1odc739t5 t636 = gm_f_extend_0cs85gs(t622, t635);
                gm_l95 = t636;
                uint8_t t637 = (uint8_t)!((((uint8_t *)&gm_l95))[0] != 0);
                if (!t637) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l95) + 1, gm_srcmok80ho8hh01, 269, 9);
                goto gm_lb39;
            }
gm_lb40:;
            void * t638 = gm_l0;
            void * t639 = (void *)(((uint8_t *)t638) + 1984);
            void * t640 = GM_LD(void *, ((uint8_t *)&gm_l53));
            uint64_t t641 = GM_LD(uint64_t, (((uint8_t *)&gm_l53) + 8));
            uint64_t t642 = gm_l54;
            uint64_t t643 = (uintptr_t)t642;
            uint64_t t644 = (uintptr_t)t641;
            if (t643 > t644 || t644 > (uintptr_t)t641) gm_trap_slice(t643, t644, t641, gm_srcmok80ho8hh01, 271, 25);
            uint64_t t645 = ((uintptr_t)t641) - (uintptr_t)t642;
            gm_b16_8 t646;
            GM_ST(void *, (uint8_t *)&t646, (void *)(((uint8_t *)t640 + (uintptr_t)t642 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t646 + 8, (uintptr_t)(t645));
            gm_b16_8 t647 = GM_LD(gm_b16_8, ((uint8_t *)&t646));
            gm_tftb1odc739t5 t648 = gm_f_extend_0cs85gs(t639, t647);
            gm_l96 = t648;
            uint8_t t649 = (uint8_t)!((((uint8_t *)&gm_l96))[0] != 0);
            if (!t649) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l96) + 1, gm_srcmok80ho8hh01, 271, 9);
        }
gm_lb39:;
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
__attribute__((aligned(4))) void gm_f_gen_fn_2fqvb90(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t9805qtonbsag a2) {
    struct gm_fr_gen_fn_2fqvb90 F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    gm_t9805qtonbsag gm_l2; /* spec */
    void * gm_l3; /* def */
    void * gm_l4; /* body */
    gm_b16_8 gm_l5; /* in_frame */
    gm_b16_8 gm_l7; /* names */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* i */
    gm_b16_8 gm_l17; /* c */
    gm_b16_8 gm_l18; /* n */
    gm_b16_8 gm_l19; /* alias */
    gm_b16_8 gm_l20; /* dims */
    gm_t6j6smj7qoctu gm_l21; /* view */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    uint64_t gm_l25; /* i */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    gm_tplpbqf3duvn3 gm_l29; /* w */
    GM_STACK_CHECK(&F, 2334u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l30;
    gm_tabb710d9mq2e gm_l31;
    gm_tftb1odc739t5 gm_l32;
    gm_tftb1odc739t5 gm_l33;
    gm_t5im2v50m3hk6 gm_l34;
    uint8_t gm_l35;
    gm_ttu09lij36f8c gm_l36;
    gm_tftb1odc739t5 gm_l37;
    gm_tftb1odc739t5 gm_l38;
    gm_tftb1odc739t5 gm_l39;
    gm_tftb1odc739t5 gm_l40;
    gm_tftb1odc739t5 gm_l41;
    gm_tftb1odc739t5 gm_l42;
    gm_tftb1odc739t5 gm_l43;
    {
        void * t32 = gm_l0;
        void * t33 = GM_LD(void *, ((uint8_t *)t32));
        uint32_t t34 = gm_l1;
        void * t35 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t33, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t35;
        void * t36 = gm_l3;
        {
            {
                uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t36) + 96) + 0));
                uint8_t t38 = (uint8_t)(t37 == ((uint8_t)3ULL));
                if (!t38) {
                    goto gm_lb2;
                }
                gm_l30 = ((uint8_t)1);
                goto gm_lb1;
            }
gm_lb2:;
            {
                gm_l30 = ((uint8_t)0);
                goto gm_lb1;
            }
gm_lb3:;
            gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 53, 8);
        }
gm_lb1:;
        uint8_t t39 = gm_l30;
        if (t39) {
            void * t40 = gm_l0;
            uint32_t t41 = gm_l1;
            gm_f_foreign_decl_rld7m78((void *)&(*(gm_tqgtd5gtgj65s *)gm_out), (void *)NULL, cx, NULL, t40, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        void * t42 = gm_l3;
        uint8_t t43 = (uint8_t)(GM_LD(void *, (((uint8_t *)t42) + 152)) != 0);
        if (t43) {
            void * t44 = GM_LD(void *, (((uint8_t *)t42) + 152));
            gm_l4 = t44;
        } else {
            void * t45 = gm_l3;
            gm_tl874t2atnlh3 t46 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t45) + 16));
            void * t47 = gm_l3;
            gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)t47));
            gm_tnpifv20irnph t49;
            GM_ST(gm_b16_8, ((uint8_t *)&t49 + 0), t48);
            gm_f_format_pu04c4a((void *)&gm_l31, (void *)NULL, cx, NULL, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t50 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
            if (!t50) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 8, gm_srcmok80ho8hh01, 54, 57);
            gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l31) + 8));
            gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
            gm_t08olq97rscn3 t53;
            gm_f_unsupported_531q51r((void *)&t53, (void *)NULL, cx, NULL, t46, t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8), t53);
            goto gm_fdone;
        }
        void * t54 = gm_l0;
        uint32_t t55 = gm_l1;
        uint8_t t56 = gm_f_frame_done_014nc4i((void *)NULL, cx, NULL, t54, t55);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (t56) {
            void * t57 = gm_l0;
            uint32_t t58 = gm_l1;
            void * t59 = gm_l3;
            void * t60 = gm_l4;
            gm_t9805qtonbsag t61 = gm_l2;
            gm_f_gen_body_j6ku8qk((void *)&(*(gm_tqgtd5gtgj65s *)gm_out), (void *)NULL, cx, NULL, t57, t58, t59, t60, t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        void * t62 = gm_l0;
        uint32_t t63 = gm_l1;
        gm_b16_8 t64 = gm_f_frame_locals_mt4nb6o((void *)NULL, cx, NULL, t62, t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t64;
        gm_trqfvmjtt4sij t65;
        gm_f_list_uvfu1k1((void *)&t65, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l6 = t65;
        void * t66 = (void *)((uint8_t *)&F.l6);
        void * t67 = gm_l0;
        uint32_t t68 = gm_l1;
        gm_b16_8 t69 = gm_f_frame_name_stddmgg((void *)NULL, cx, NULL, t67, t68);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t70;
        GM_ST(gm_b16_8, ((uint8_t *)&t70 + 0), t69);
        gm_tftb1odc739t5 t71 = gm_f_write_j67vjku(t66, t70);
        gm_l32 = t71;
    }
    {
        uint8_t t72 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
        if (!t72) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 1, gm_srcmok80ho8hh01, 59, 5);
        gm_b16_8 t73 = gm_f_empty_slice_ppl3r45();
        gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
        gm_l7 = t74;
        void * t75 = gm_l0;
        uint8_t t76 = GM_LD(uint8_t, ((((uint8_t *)t75) + 1136) + 2));
        if (t76) {
            {
                gm_trqfvmjtt4sij t77;
                gm_f_list_epvducg((void *)&t77, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l8 = t77;
            }
            void * t78 = gm_l3;
            gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)t78) + 48));
            gm_l9 = t79;
            gm_l10 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t80 = gm_l10;
                    uint64_t t81 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                    uint8_t t82 = (t80 < t81);
                    if (!t82) {
                        goto gm_lb4;
                    }
                    {
                        void * t83 = GM_LD(void *, ((uint8_t *)&gm_l9));
                        uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                        uint64_t t85 = gm_l10;
                        {
                            {
                                memmove(((uint8_t *)&F.l11), (((uint8_t *)t83) + (uintptr_t)t85 * 40), 40);
                                void * t86 = (void *)((uint8_t *)&F.l8);
                                gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
                                gm_tftb1odc739t5 t88 = gm_f_push_3ef746b(t86, t87);
                                gm_l33 = t88;
                                uint8_t t89 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                                if (!t89) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 1, gm_srcmok80ho8hh01, 64, 31);
                                goto gm_lb7;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 64, 9);
                        }
gm_lb7:;
                    }
gm_lb6:;
                    uint64_t t90 = gm_l10;
                    uint64_t r91; bool o92 = __builtin_add_overflow(t90, ((uint64_t)1ULL), &r91);
                    uint64_t t93 = r91;
                    gm_l10 = t93;
                }
            }
gm_lb4:;
            {
                gm_t4c7ipjj9s3fp t94;
                gm_f_hash_map_cuh4fs9((void *)&t94, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l12 = t94;
            }
            gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
            gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&t95));
            void * t97 = (void *)((uint8_t *)&F.l12);
            gm_b16_8 t98 = gm_f_unique_members_mrojmjf((void *)NULL, cx, NULL, t96, t97);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t99 = GM_LD(gm_b16_8, ((uint8_t *)&t98));
            gm_l7 = t99;
        }
        void * t100 = gm_l3;
        gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)t100) + 48));
        gm_l13 = t101;
        gm_l14 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t102 = gm_l14;
                uint64_t t103 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                uint8_t t104 = (t102 < t103);
                if (!t104) {
                    goto gm_lb9;
                }
                {
                    {
                        uint64_t t105 = gm_l14;
                        void * t106 = GM_LD(void *, ((uint8_t *)&gm_l13));
                        uint64_t t107 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                        uint64_t t108 = gm_l14;
                        gm_tsoie1ppgdvcg t109 = GM_LD(gm_tsoie1ppgdvcg, (((uint8_t *)t106) + (uintptr_t)t108 * 40));
                        GM_ST(uint64_t, ((uint8_t *)&gm_l34 + 0), t105);
                        GM_ST(gm_tsoie1ppgdvcg, ((uint8_t *)&gm_l34 + 8), t109);
                    }
                    {
                        {
                            uint64_t t110 = GM_LD(uint64_t, ((uint8_t *)&gm_l34));
                            gm_l15 = t110;
                            memmove(((uint8_t *)&F.l16), (((uint8_t *)&gm_l34) + 8), 40);
                            void * t111 = GM_LD(void *, ((uint8_t *)&gm_l5));
                            uint64_t t112 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                            uint64_t t113 = gm_l15;
                            if ((uintptr_t)t113 >= (uintptr_t)(t112)) gm_trap_index((uintptr_t)t113, t112, gm_srcmok80ho8hh01, 69, 16);
                            uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)t111) + (uintptr_t)t113 * 1));
                            uint8_t t115 = (uint8_t)!t114;
                            if (t115) {
                                goto gm_lb11;
                            }
                            void * t116 = gm_l0;
                            void * t117 = (void *)(((uint8_t *)t116) + 8);
                            gm_t88rcucg1b7uk t118 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l16) + 16));
                            gm_b16_8 t119 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t117, t118);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l17 = t119;
                            {
                                {
                                    uint64_t t120 = gm_l15;
                                    uint64_t t121 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                    uint8_t t122 = (t120 < t121);
                                    if (!t122) {
                                        goto gm_lb15;
                                    }
                                    void * t123 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                    uint64_t t124 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                    uint64_t t125 = gm_l15;
                                    {
                                        {
                                            uint8_t t126 = (uint8_t)(((((uint8_t *)t123) + (uintptr_t)t125 * 24))[16] != 0);
                                            if (t126) {
                                                goto gm_lb17;
                                            }
                                            gm_l35 = ((uint8_t)0);
                                            goto gm_lb16;
                                        }
gm_lb17:;
                                        {
                                            gm_l35 = ((uint8_t)1);
                                            goto gm_lb16;
                                        }
gm_lb18:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 71, 30);
                                    }
gm_lb16:;
                                    uint8_t t127 = gm_l35;
                                    if (!t127) {
                                        goto gm_lb15;
                                    }
                                    void * t128 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                    uint64_t t129 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                    uint64_t t130 = gm_l15;
                                    uint8_t t131 = (uint8_t)(((((uint8_t *)t128) + (uintptr_t)t130 * 24))[16] != 0);
                                    if (!t131) gm_trap("unwrap of null", gm_srcmok80ho8hh01, 72, 18);
                                    gm_b16_8 t132 = GM_LD(gm_b16_8, (((uint8_t *)t128) + (uintptr_t)t130 * 24));
                                    gm_l18 = t132;
                                    gm_b16_8 t133 = gm_l17;
                                    gm_l19 = t133;
                                    gm_b16_8 t134;
                                    GM_ST(void *, (uint8_t *)&t134, (void *)(gm_sr4qbrk6tlgaa));
                                    GM_ST(uintptr_t, (uint8_t *)&t134 + 8, (uintptr_t)(0));
                                    gm_l20 = t134;
                                    {
                                        {
                                            {
                                                void * t135 = gm_l0;
                                                void * t136 = (void *)(((uint8_t *)t135) + 8);
                                                gm_t88rcucg1b7uk t137 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l16) + 16));
                                                gm_f_debug_view_gp38euq((void *)&gm_l36, (void *)NULL, cx, NULL, t136, t137);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                            }
                                            uint8_t t138 = (uint8_t)((((uint8_t *)&gm_l36))[32] != 0);
                                            if (!t138) {
                                                goto gm_lb20;
                                            }
                                            {
                                                memmove(((uint8_t *)&gm_l21), ((uint8_t *)&gm_l36), 32);
                                            }
                                            gm_b16_8 t139 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l21));
                                            gm_l19 = t139;
                                            gm_b16_8 t140 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 16));
                                            gm_l20 = t140;
                                            goto gm_lb19;
                                        }
gm_lb20:;
                                    }
gm_lb19:;
                                    void * t141 = gm_l0;
                                    void * t142 = (void *)(((uint8_t *)t141) + 8);
                                    gm_b16_8 t143 = gm_l18;
                                    gm_b0_1 t144 = gm_f_add_member_name_j3i0eje((void *)NULL, cx, NULL, t142, t143);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    void * t145 = (void *)((uint8_t *)&F.l6);
                                    gm_b16_8 t146 = gm_l17;
                                    uint64_t t147 = gm_l15;
                                    gm_b16_8 t148 = gm_l19;
                                    gm_b16_8 t149 = gm_l18;
                                    gm_b16_8 t150 = gm_l20;
                                    gm_tcrdb1inj22os t151;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t151 + 0), t146);
                                    GM_ST(uint64_t, ((uint8_t *)&t151 + 16), t147);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t151 + 24), t148);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t151 + 40), t149);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t151 + 56), t150);
                                    gm_tftb1odc739t5 t152 = gm_f_write_s858s21(t145, &t151);
                                    gm_l37 = t152;
                                    uint8_t t153 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
                                    if (!t153) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 1, gm_srcmok80ho8hh01, 80, 13);
                                    goto gm_lb14;
                                }
gm_lb15:;
                                void * t154 = (void *)((uint8_t *)&F.l6);
                                gm_b16_8 t155 = gm_l17;
                                uint64_t t156 = gm_l15;
                                gm_b16_8 t157 = GM_LD(gm_b16_8, ((uint8_t *)&F.l16));
                                gm_tq3m5qgvkpje8 t158;
                                GM_ST(gm_b16_8, ((uint8_t *)&t158 + 0), t155);
                                GM_ST(uint64_t, ((uint8_t *)&t158 + 16), t156);
                                GM_ST(gm_b16_8, ((uint8_t *)&t158 + 24), t157);
                                gm_tftb1odc739t5 t159 = gm_f_write_s0f8vaq(t154, &t158);
                                gm_l38 = t159;
                                uint8_t t160 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                                if (!t160) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 1, gm_srcmok80ho8hh01, 82, 13);
                            }
gm_lb14:;
                            goto gm_lb12;
                        }
gm_lb13:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 68, 5);
                    }
gm_lb12:;
                }
gm_lb11:;
                uint64_t t161 = gm_l14;
                uint64_t r162; bool o163 = __builtin_add_overflow(t161, ((uint64_t)1ULL), &r162);
                uint64_t t164 = r162;
                gm_l14 = t164;
            }
        }
gm_lb9:;
    }
    {
        void * t165 = gm_l0;
        uint32_t t166 = gm_l1;
        void * t167 = gm_l4;
        uint8_t t168 = gm_f_lir_framed_6i9516j((void *)NULL, cx, NULL, t165, t166, t167);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (t168) {
            void * t169 = gm_l0;
            uint32_t t170 = gm_l1;
            gm_f_lir_of_60vujj2((void *)&F.l22, (void *)NULL, cx, NULL, t169, t170);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t171 = gm_l3;
            uint64_t t172 = GM_LD(uint64_t, ((((uint8_t *)t171) + 48) + 8));
            gm_l24 = t172;
            uint64_t t173 = GM_LD(uint64_t, ((((uint8_t *)&F.l22) + 8) + 8));
            gm_l23 = t173;
            {
                for (;;) {
                    uint64_t t174 = gm_l24;
                    uint64_t t175 = gm_l23;
                    uint8_t t176 = (t174 < t175);
                    if (!t176) {
                        goto gm_lb21;
                    }
                    {
                        {
                            {
                                uint64_t t177 = gm_l24;
                                gm_l25 = t177;
                                void * t178 = (void *)((uint8_t *)&F.l6);
                                void * t179 = gm_l0;
                                void * t180 = (void *)(((uint8_t *)t179) + 8);
                                void * t181 = GM_LD(void *, (((uint8_t *)&F.l22) + 8));
                                uint64_t t182 = GM_LD(uint64_t, ((((uint8_t *)&F.l22) + 8) + 8));
                                uint64_t t183 = gm_l25;
                                gm_t88rcucg1b7uk t184 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t181) + (uintptr_t)t183 * 32));
                                gm_b16_8 t185 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t180, t184);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint64_t t186 = gm_l25;
                                gm_t3877pk7lv748 t187;
                                GM_ST(gm_b16_8, ((uint8_t *)&t187 + 0), t185);
                                GM_ST(uint64_t, ((uint8_t *)&t187 + 16), t186);
                                gm_tftb1odc739t5 t188 = gm_f_write_dfbs2m5(t178, &t187);
                                gm_l39 = t188;
                                uint8_t t189 = (uint8_t)!((((uint8_t *)&gm_l39))[0] != 0);
                                if (!t189) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l39) + 1, gm_srcmok80ho8hh01, 89, 50);
                                goto gm_lb24;
                            }
gm_lb25:;
                            gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 89, 9);
                        }
gm_lb24:;
                    }
gm_lb23:;
                    uint64_t t190 = gm_l24;
                    uint64_t r191; bool o192 = __builtin_add_overflow(t190, ((uint64_t)1ULL), &r191);
                    uint64_t t193 = r191;
                    gm_l24 = t193;
                }
            }
gm_lb21:;
        }
        void * t194 = (void *)((uint8_t *)&F.l6);
        void * t195 = gm_l0;
        void * t196 = (void *)(((uint8_t *)t195) + 8);
        void * t197 = gm_l3;
        gm_t88rcucg1b7uk t198 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t197) + 64));
        gm_b16_8 t199 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t196, t198);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t200;
        GM_ST(gm_b16_8, ((uint8_t *)&t200 + 0), t199);
        gm_tftb1odc739t5 t201 = gm_f_write_shh24f1(t194, t200);
        gm_l40 = t201;
        uint8_t t202 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
        if (!t202) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l40) + 1, gm_srcmok80ho8hh01, 91, 5);
        gm_trqfvmjtt4sij t203;
        gm_f_list_9ljroit((void *)&t203, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l26 = t203;
        void * t204 = gm_l4;
        void * t205 = (void *)((uint8_t *)&F.l26);
        gm_b0_1 t206 = gm_f_collect_withs_fa29mk1((void *)NULL, cx, NULL, t204, t205);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t207 = GM_LD(gm_b16_8, ((uint8_t *)&F.l26));
        gm_l27 = t207;
        gm_l28 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t208 = gm_l28;
                uint64_t t209 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                uint8_t t210 = (t208 < t209);
                if (!t210) {
                    goto gm_lb26;
                }
                {
                    void * t211 = GM_LD(void *, ((uint8_t *)&gm_l27));
                    uint64_t t212 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                    uint64_t t213 = gm_l28;
                    {
                        {
                            gm_tplpbqf3duvn3 t214 = GM_LD(gm_tplpbqf3duvn3, (((uint8_t *)t211) + (uintptr_t)t213 * 8));
                            gm_l29 = t214;
                            void * t215 = (void *)((uint8_t *)&F.l6);
                            void * t216 = gm_l0;
                            void * t217 = (void *)(((uint8_t *)t216) + 8);
                            gm_t88rcucg1b7uk t218 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l29) + 4));
                            gm_b16_8 t219 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t217, t218);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint32_t t220 = GM_LD(uint32_t, ((uint8_t *)&gm_l29));
                            gm_tommf4j9upomc t221;
                            GM_ST(uint32_t, (uint8_t *)&t221 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t221 + 0), t219);
                            GM_ST(uint32_t, ((uint8_t *)&t221 + 16), t220);
                            gm_tftb1odc739t5 t222 = gm_f_write_ihqa4kk(t215, &t221);
                            gm_l41 = t222;
                            uint8_t t223 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                            if (!t223) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 1, gm_srcmok80ho8hh01, 95, 9);
                            goto gm_lb29;
                        }
gm_lb30:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 94, 5);
                    }
gm_lb29:;
                }
gm_lb28:;
                uint64_t t224 = gm_l28;
                uint64_t r225; bool o226 = __builtin_add_overflow(t224, ((uint64_t)1ULL), &r225);
                uint64_t t227 = r225;
                gm_l28 = t227;
            }
        }
gm_lb26:;
    }
    {
        void * t228 = (void *)((uint8_t *)&F.l6);
        gm_b16_8 t229;
        GM_ST(void *, (uint8_t *)&t229, (void *)(gm_sg398rgmck67b));
        GM_ST(uintptr_t, (uint8_t *)&t229 + 8, (uintptr_t)(3));
        gm_tftb1odc739t5 t230 = gm_f_extend_0cs85gs(t228, t229);
        gm_l42 = t230;
        uint8_t t231 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
        if (!t231) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l42) + 1, gm_srcmok80ho8hh01, 97, 5);
        void * t232 = gm_l0;
        void * t233 = (void *)(((uint8_t *)t232) + 1904);
        gm_b16_8 t234 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
        gm_b16_8 t235 = GM_LD(gm_b16_8, ((uint8_t *)&t234));
        gm_tftb1odc739t5 t236 = gm_f_extend_0cs85gs(t233, t235);
        gm_l43 = t236;
        uint8_t t237 = (uint8_t)!((((uint8_t *)&gm_l43))[0] != 0);
        if (!t237) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l43) + 1, gm_srcmok80ho8hh01, 98, 5);
        void * t238 = gm_l0;
        uint32_t t239 = gm_l1;
        void * t240 = gm_l3;
        void * t241 = gm_l4;
        gm_t9805qtonbsag t242 = gm_l2;
        gm_f_gen_body_j6ku8qk((void *)&(*(gm_tqgtd5gtgj65s *)gm_out), (void *)NULL, cx, NULL, t238, t239, t240, t241, t242);
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_fiber_with_53c3jik(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_fiber_with_53c3jik F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l2;
    {
        {
            void * t7 = gm_l0;
            uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
            uint8_t t9 = (uint8_t)(t8 == ((uint8_t)59ULL));
            if (!t9) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l1), ((((uint8_t *)t7) + 8) + 8), 40);
            {
                {
                    uint8_t t10 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l1) + 24)) != 0);
                    if (t10) {
                        goto gm_lb4;
                    }
                    gm_l2 = ((uint8_t)0);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    gm_l2 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb5:;
                gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 849, 12);
            }
gm_lb3:;
            uint8_t t11 = gm_l2;
            if (t11) {
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t12 = gm_l0;
    gm_closure t13 = ((gm_closure){GM_PTR_TAG(gm_f_fn_e5aba81), NULL, cx});
    uint8_t t14 = gm_f_any_child_ffc8h1k(t12, t13);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t14;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_hint_attrs_mktdhi1(void *env, gm_node *cx, gm_node *inst, gm_t1h8kp9hsu3ua a0) {
    struct gm_fr_hint_attrs_mktdhi1 F;
    gm_t1h8kp9hsu3ua gm_l0; /* h */
    uint8_t gm_l2; /* x */
    GM_STACK_CHECK(&F, 230u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l3;
    gm_tftb1odc739t5 gm_l4;
    gm_tftb1odc739t5 gm_l5;
    gm_tftb1odc739t5 gm_l6;
    gm_trqfvmjtt4sij t4;
    gm_f_list_uvfu1k1((void *)&t4, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t4;
    {
        {
            uint8_t t5 = (uint8_t)(((((uint8_t *)&gm_l0) + 1))[1] != 0);
            if (!t5) {
                goto gm_lb2;
            }
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 1));
            gm_l2 = t6;
            uint8_t t7 = gm_l2;
            if (t7) {
                void * t8 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t9;
                GM_ST(void *, (uint8_t *)&t9, (void *)(gm_so0h0lkl6bp4l));
                GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(38));
                gm_tftb1odc739t5 t10 = gm_f_extend_0cs85gs(t8, t9);
                gm_l3 = t10;
                uint8_t t11 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
                if (!t11) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srcmok80ho8hh01, 408, 16);
            } else {
                void * t12 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t13;
                GM_ST(void *, (uint8_t *)&t13, (void *)(gm_s39jha51ulivf));
                GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(26));
                gm_tftb1odc739t5 t14 = gm_f_extend_0cs85gs(t12, t13);
                gm_l4 = t14;
                uint8_t t15 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
                if (!t15) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srcmok80ho8hh01, 408, 77);
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 3));
    if (t16) {
        void * t17 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t18;
        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s3cu59itn73k7));
        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(22));
        gm_tftb1odc739t5 t19 = gm_f_extend_0cs85gs(t17, t18);
        gm_l5 = t19;
        uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
        if (!t20) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srcmok80ho8hh01, 410, 17);
    }
    uint8_t t21 = GM_LD(uint8_t, ((uint8_t *)&gm_l0));
    if (t21) {
        void * t22 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(gm_slnuijmj5p1vk));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(18));
        gm_tftb1odc739t5 t24 = gm_f_extend_0cs85gs(t22, t23);
        gm_l6 = t24;
        uint8_t t25 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
        if (!t25) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srcmok80ho8hh01, 411, 22);
    }
    gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_indent_line_b3mckqa(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_indent_line_b3mckqa F;
    void * gm_l0; /* fg */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK(&F, 203u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l2;
    gm_tftb1odc739t5 gm_l3;
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 168));
    uint64_t r7; bool o8 = __builtin_mul_overflow(t6, ((uint64_t)4ULL), &r7);
    if (o8) gm_trap("integer overflow in `*`", gm_srcmok80ho8hh01, 435, 10);
    uint64_t t9 = r7;
    gm_l1 = t9;
    {
        for (;;) {
            uint64_t t10 = gm_l1;
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)gm_cc9imq1spis) + 8));
            uint8_t t12 = (t10 > t11);
            if (!t12) {
                goto gm_lb1;
            }
            {
                void * t13 = gm_l0;
                void * t14 = (void *)(((uint8_t *)t13) + 128);
                gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)gm_cc9imq1spis));
                gm_tftb1odc739t5 t16 = gm_f_extend_0cs85gs(t14, t15);
                gm_l2 = t16;
                uint8_t t17 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
                if (!t17) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srcmok80ho8hh01, 437, 9);
                uint64_t t18 = gm_l1;
                uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)gm_cc9imq1spis) + 8));
                uint64_t r20; bool o21 = __builtin_sub_overflow(t18, t19, &r20);
                if (o21) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 438, 9);
                uint64_t t22 = r20;
                gm_l1 = t22;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t23 = gm_l0;
    void * t24 = (void *)(((uint8_t *)t23) + 128);
    void * t25 = GM_LD(void *, ((uint8_t *)gm_cc9imq1spis));
    uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)gm_cc9imq1spis) + 8));
    uint64_t t27 = gm_l1;
    uint64_t t28 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t29 = (uintptr_t)t27;
    if (t28 > t29 || t29 > (uintptr_t)t26) gm_trap_slice(t28, t29, t26, gm_srcmok80ho8hh01, 440, 19);
    uint64_t t30 = ((uintptr_t)t27) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t31;
    GM_ST(void *, (uint8_t *)&t31, (void *)(((uint8_t *)t25 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(t30));
    gm_tftb1odc739t5 t32 = gm_f_extend_0cs85gs(t24, t31);
    gm_l3 = t32;
    uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t33) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srcmok80ho8hh01, 440, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_is_borrowed_qi4apdt(void * a0, uint32_t a1);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_label_710oev4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_ladder_36amdbn(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_ladder_target_84n3aep(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_ladder_target_84n3aep F;
    void * gm_l0; /* fg */
    uint64_t gm_l1; /* i */
    void * gm_l2; /* s */
    gm_tf5srh4mp4b3i gm_l3; /* found */
    gm_tf5srh4mp4b3i gm_l4; /* none */
    GM_STACK_CHECK(&F, 560u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tabb710d9mq2e gm_l5;
    gm_tabb710d9mq2e gm_l6;
    void * t11 = gm_l0;
    uint64_t t12 = GM_LD(uint64_t, ((((uint8_t *)t11) + 184) + 8));
    gm_l1 = t12;
    {
        for (;;) {
            uint64_t t13 = gm_l1;
            uint8_t t14 = (t13 > ((uint64_t)0ULL));
            if (!t14) {
                goto gm_lb1;
            }
            {
                uint64_t t15 = gm_l1;
                uint64_t r16; bool o17 = __builtin_sub_overflow(t15, ((uint64_t)1ULL), &r16);
                if (o17) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 606, 9);
                uint64_t t18 = r16;
                gm_l1 = t18;
                void * t19 = gm_l0;
                void * t20 = GM_LD(void *, (((uint8_t *)t19) + 184));
                uint64_t t21 = GM_LD(uint64_t, ((((uint8_t *)t19) + 184) + 8));
                uint64_t t22 = gm_l1;
                if ((uintptr_t)t22 >= (uintptr_t)(t21)) gm_trap_index((uintptr_t)t22, t21, gm_srcmok80ho8hh01, 607, 15);
                void * t23 = (void *)(((uint8_t *)t20) + (uintptr_t)t22 * 104);
                gm_l2 = t23;
                uint64_t t24 = gm_l1;
                gm_tf5srh4mp4b3i t25;
                GM_ST(uint8_t, (uint8_t *)&t25 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t25 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t25 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t25), t24);
                ((uint8_t *)&t25)[8] = 1;
                gm_l3 = t25;
                void * t26 = gm_l2;
                {
                    {
                        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)t26) + 0));
                        uint8_t t28 = (uint8_t)(t27 == ((uint8_t)0ULL));
                        if (!t28) {
                            goto gm_lb5;
                        }
                        void * t29 = gm_l2;
                        uint64_t t30 = GM_LD(uint64_t, ((((uint8_t *)t29) + 56) + 8));
                        uint8_t t31 = ((t30 == ((uint64_t)0ULL)));
                        if (t31) {
                            goto gm_lb3;
                        }
                        void * t32 = gm_l2;
                        uint64_t t33 = GM_LD(uint64_t, ((((uint8_t *)t32) + 56) + 8));
                        void * t34 = gm_l2;
                        uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)t34) + 96));
                        uint8_t t36 = (t33 > t35);
                        if (t36) {
                            void * t37 = gm_l2;
                            void * t38 = gm_l2;
                            uint64_t t39 = GM_LD(uint64_t, ((((uint8_t *)t38) + 56) + 8));
                            GM_ST(uint64_t, (((uint8_t *)t37) + 96), t39);
                        }
                        {
                            void * t40 = gm_l2;
                            uint32_t t41 = GM_LD(uint32_t, (((uint8_t *)t40) + 48));
                            void * t42 = gm_l2;
                            uint64_t t43 = GM_LD(uint64_t, ((((uint8_t *)t42) + 56) + 8));
                            gm_tb31k5l794t7p t44;
                            GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
                            GM_ST(uint32_t, ((uint8_t *)&t44 + 0), t41);
                            GM_ST(uint64_t, ((uint8_t *)&t44 + 8), t43);
                            gm_f_format_5bcbbmm((void *)&gm_l5, (void *)NULL, cx, NULL, t44);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t45 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
                        if (!t45) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 8, gm_srcmok80ho8hh01, 613, 25);
                        {
                            gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
                            gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
                            gm_tf5srh4mp4b3i t48 = gm_l3;
                            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tj64ui6kruni6 *)gm_out) + 0), t47);
                            GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&(*(gm_tj64ui6kruni6 *)gm_out) + 16), t48);
                            goto gm_fdone;
                        }
                    }
gm_lb5:;
                    {
                        {
                            {
                                uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)t26) + 0));
                                uint8_t t50 = (uint8_t)(t49 == ((uint8_t)1ULL));
                                if (!t50) {
                                    goto gm_lb8;
                                }
                                goto gm_lb7;
                            }
gm_lb8:;
                            {
                                uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)t26) + 0));
                                uint8_t t52 = (uint8_t)(t51 == ((uint8_t)2ULL));
                                if (!t52) {
                                    goto gm_lb9;
                                }
                                goto gm_lb7;
                            }
gm_lb9:;
                            goto gm_lb6;
                        }
gm_lb7:;
                        void * t53 = gm_l2;
                        GM_ST(uint64_t, (((uint8_t *)t53) + 96), ((uint64_t)1ULL));
                        {
                            void * t54 = gm_l2;
                            uint32_t t55 = GM_LD(uint32_t, (((uint8_t *)t54) + 48));
                            gm_t88rcucg1b7uk t56;
                            GM_ST(uint32_t, ((uint8_t *)&t56 + 0), t55);
                            gm_f_format_dd6d4bt((void *)&gm_l6, (void *)NULL, cx, NULL, t56);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                        if (!t57) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_srcmok80ho8hh01, 617, 25);
                        {
                            gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
                            gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
                            gm_tf5srh4mp4b3i t60 = gm_l3;
                            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tj64ui6kruni6 *)gm_out) + 0), t59);
                            GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&(*(gm_tj64ui6kruni6 *)gm_out) + 16), t60);
                            goto gm_fdone;
                        }
                    }
gm_lb6:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 609, 9);
                }
gm_lb4:;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    gm_tf5srh4mp4b3i t61;
    memset(&t61, 0, sizeof t61);
    gm_l4 = t61;
    void * t62 = gm_l0;
    uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)t62) + 242));
    if (t63) {
        gm_b16_8 t64;
        GM_ST(void *, (uint8_t *)&t64, (void *)(gm_sjibpf3glbc54));
        GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(8));
        gm_tf5srh4mp4b3i t65 = gm_l4;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tj64ui6kruni6 *)gm_out) + 0), t64);
        GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&(*(gm_tj64ui6kruni6 *)gm_out) + 16), t65);
        goto gm_fdone;
    }
    void * t66 = gm_l0;
    GM_ST(uint8_t, (((uint8_t *)t66) + 241), ((uint8_t)1));
    gm_b16_8 t67;
    GM_ST(void *, (uint8_t *)&t67, (void *)(gm_s2hi6cc9n75l5));
    GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(6));
    gm_tf5srh4mp4b3i t68 = gm_l4;
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tj64ui6kruni6 *)gm_out) + 0), t67);
    GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&(*(gm_tj64ui6kruni6 *)gm_out) + 16), t68);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_line_l6u87p9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
extern __attribute__((aligned(4))) uint8_t gm_f_lir_framed_6i9516j(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_local_addr_g9lkr6l(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_local_addr_g9lkr6l F;
    void * gm_l0; /* fg */
    uint32_t gm_l1; /* l */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 312u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l3;
    gm_tabb710d9mq2e gm_l4;
    gm_tabb710d9mq2e gm_l5;
    {
        {
            void * t4 = gm_l0;
            uint32_t t5 = gm_l1;
            gm_tf5srh4mp4b3i t6 = gm_f_param_index_rp1nn6k(t4, t5);
            gm_l3 = t6;
            uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
            if (!t7) {
                goto gm_lb2;
            }
            uint64_t t8 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
            gm_l2 = t8;
            void * t9 = gm_l0;
            void * t10 = GM_LD(void *, (((uint8_t *)t9) + 272));
            uint64_t t11 = GM_LD(uint64_t, ((((uint8_t *)t9) + 272) + 8));
            uint64_t t12 = gm_l2;
            if ((uintptr_t)t12 >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)t12, t11, gm_srcmok80ho8hh01, 524, 12);
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)t10) + (uintptr_t)t12 * 1));
            if (t13) {
                uint64_t t14 = gm_l2;
                gm_t4ce0jd3tdro4 t15;
                GM_ST(uint64_t, ((uint8_t *)&t15 + 0), t14);
                gm_f_format_9pumrco((void *)&gm_l4, (void *)NULL, cx, NULL, t15);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
                if (!t16) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 8, gm_srcmok80ho8hh01, 524, 36);
                gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
                gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
                F.ret = t18;
                goto gm_fret;
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t19 = gm_l0;
    uint32_t t20 = gm_l1;
    gm_b16_8 t21 = gm_f_local_lv_m2jtf0n((void *)NULL, cx, NULL, t19, t20);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tnpifv20irnph t22;
    GM_ST(gm_b16_8, ((uint8_t *)&t22 + 0), t21);
    gm_f_format_4un19t8((void *)&gm_l5, (void *)NULL, cx, NULL, t22);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t23 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t23) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 8, gm_srcmok80ho8hh01, 526, 12);
    gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
    gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
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
__attribute__((aligned(4))) uint8_t gm_f_local_exit_4aogt00(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_local_exit_4aogt00 F;
    void * gm_l0; /* fg */
    gm_t9qkh0rsooc8d gm_l1; /* kind */
    gm_b16_8 gm_l2; /* label */
    gm_b16_8 gm_l3; /* is_err */
    gm_tf5srh4mp4b3i gm_l4; /* target */
    gm_tf5srh4mp4b3i gm_l5; /* found */
    uint64_t gm_l6; /* i */
    uint8_t gm_l8; /* hit */
    gm_tiilmfd16v3fc gm_l10; /* w */
    uint64_t gm_l11; /* start */
    uint64_t gm_l12; /* t */
    uint8_t gm_l13; /* err */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* j */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    gm_thervm8j23qet gm_l20; /* d */
    gm_b16_8 gm_l21; /* dest */
    uint64_t gm_l22; /* t */
    gm_tiilmfd16v3fc gm_l25; /* w */
    GM_STACK_CHECK(&F, 1074u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tabb710d9mq2e gm_l26;
    uint8_t gm_l27;
    gm_tabb710d9mq2e gm_l28;
    uint8_t gm_l29;
    uint8_t gm_l30;
    uint8_t gm_l31;
    gm_tf5srh4mp4b3i t30;
    memset(&t30, 0, sizeof t30);
    gm_l4 = t30;
    gm_t9qkh0rsooc8d t31 = gm_l1;
    uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t31) + 0));
    uint8_t t33 = (uint8_t)((t32 == ((uint8_t)0ULL)));
    gm_t9qkh0rsooc8d t34;
    GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)0ULL));
    if (t33) {
    } else {
        gm_t9qkh0rsooc8d t35 = gm_l1;
        gm_t9qkh0rsooc8d t36;
        GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)1ULL));
        gm_t9qkh0rsooc8d t37;
        GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)2ULL));
        gm_t0am46iohlpol t38;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t38 + 0), t36);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t38 + 1), t37);
        uint8_t t39 = 0;
        if (gm_eqgnrg8h3j6k(((uint8_t *)&t38) + 0, ((uint8_t *)&t35))) t39 = 1;
        if (gm_eqgnrg8h3j6k(((uint8_t *)&t38) + 1, ((uint8_t *)&t35))) t39 = 1;
        uint8_t t40 = (uint8_t)(t39);
        if (t40) {
            gm_tf5srh4mp4b3i t41;
            memset(&t41, 0, sizeof t41);
            gm_l5 = t41;
            void * t42 = gm_l0;
            uint64_t t43 = GM_LD(uint64_t, ((((uint8_t *)t42) + 184) + 8));
            gm_l6 = t43;
            {
                for (;;) {
                    uint64_t t44 = gm_l6;
                    uint8_t t45 = (t44 > ((uint64_t)0ULL));
                    if (!t45) {
                        goto gm_lb1;
                    }
                    {
                        uint64_t t46 = gm_l6;
                        uint64_t r47; bool o48 = __builtin_sub_overflow(t46, ((uint64_t)1ULL), &r47);
                        if (o48) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 738, 13);
                        uint64_t t49 = r47;
                        gm_l6 = t49;
                        void * t50 = gm_l0;
                        void * t51 = GM_LD(void *, (((uint8_t *)t50) + 184));
                        uint64_t t52 = GM_LD(uint64_t, ((((uint8_t *)t50) + 184) + 8));
                        uint64_t t53 = gm_l6;
                        if ((uintptr_t)t53 >= (uintptr_t)(t52)) gm_trap_index((uintptr_t)t53, t52, gm_srcmok80ho8hh01, 739, 18);
                        memmove(((uint8_t *)&F.l7), (((uint8_t *)t51) + (uintptr_t)t53 * 104), 104);
                        gm_l8 = ((uint8_t)0);
                        {
                            {
                                uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
                                uint8_t t55 = (uint8_t)(t54 == ((uint8_t)1ULL));
                                if (!t55) {
                                    goto gm_lb5;
                                }
                                {
                                    memmove(((uint8_t *)&F.l9), (((uint8_t *)&F.l7) + 8), 40);
                                }
                                {
                                    uint32_t t56 = GM_LD(uint32_t, ((uint8_t *)&F.l9));
                                    gm_t88rcucg1b7uk t57;
                                    GM_ST(uint32_t, ((uint8_t *)&t57 + 0), t56);
                                    gm_f_format_32n178h((void *)&gm_l26, (void *)NULL, cx, NULL, t57);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                uint8_t t58 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                                if (!t58) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 8, gm_srcmok80ho8hh01, 742, 37);
                                gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l26) + 8));
                                gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&t59));
                                gm_b16_8 t61 = gm_l2;
                                uint8_t t62 = (gm_eq0iqiubfm7e(((uint8_t *)&t60), ((uint8_t *)&t61)));
                                gm_l8 = t62;
                                goto gm_lb4;
                            }
gm_lb5:;
                            {
                                uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
                                uint8_t t64 = (uint8_t)(t63 == ((uint8_t)2ULL));
                                if (!t64) {
                                    goto gm_lb6;
                                }
                                memmove(((uint8_t *)&gm_l10), (((uint8_t *)&F.l7) + 8), 24);
                                gm_t9qkh0rsooc8d t65 = gm_l1;
                                uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&t65) + 0));
                                uint8_t t67 = (uint8_t)((t66 == ((uint8_t)1ULL)));
                                gm_t9qkh0rsooc8d t68;
                                GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)1ULL));
                                if (t67) {
                                    uint32_t t69 = GM_LD(uint32_t, ((uint8_t *)&gm_l10));
                                    gm_t88rcucg1b7uk t70;
                                    GM_ST(uint32_t, ((uint8_t *)&t70 + 0), t69);
                                    gm_f_format_32n178h((void *)&gm_l28, (void *)NULL, cx, NULL, t70);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t71 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                                    if (!t71) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 8, gm_srcmok80ho8hh01, 743, 56);
                                    gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l28) + 8));
                                    gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
                                    gm_b16_8 t74 = gm_l2;
                                    uint8_t t75 = (gm_eq0iqiubfm7e(((uint8_t *)&t73), ((uint8_t *)&t74)));
                                    gm_l27 = t75;
                                } else {
                                    gm_l27 = ((uint8_t)0);
                                }
                                uint8_t t76 = gm_l27;
                                gm_l8 = t76;
                                goto gm_lb4;
                            }
gm_lb6:;
                            {
                                uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
                                uint8_t t78 = (uint8_t)(t77 == ((uint8_t)0ULL));
                                if (!t78) {
                                    goto gm_lb7;
                                }
                                goto gm_lb4;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 741, 13);
                        }
gm_lb4:;
                        uint8_t t79 = gm_l8;
                        if (t79) {
                            uint64_t t80 = gm_l6;
                            gm_tf5srh4mp4b3i t81;
                            GM_ST(uint8_t, (uint8_t *)&t81 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t81 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t81 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t81), t80);
                            ((uint8_t *)&t81)[8] = 1;
                            gm_l5 = t81;
                            goto gm_lb1;
                        }
                    }
gm_lb3:;
                }
            }
gm_lb1:;
            {
                {
                    uint8_t t82 = (uint8_t)((((uint8_t *)&gm_l5))[8] != 0);
                    if (t82) {
                        goto gm_lb9;
                    }
                    gm_l29 = ((uint8_t)1);
                    goto gm_lb8;
                }
gm_lb9:;
                {
                    gm_l29 = ((uint8_t)0);
                    goto gm_lb8;
                }
gm_lb10:;
                gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 751, 12);
            }
gm_lb8:;
            uint8_t t83 = gm_l29;
            if (t83) {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_tf5srh4mp4b3i t84 = gm_l5;
            gm_l4 = t84;
        } else {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
    }
    gm_l11 = ((uint64_t)0ULL);
    {
        {
            uint8_t t85 = (uint8_t)((((uint8_t *)&gm_l4))[8] != 0);
            if (!t85) {
                goto gm_lb12;
            }
            uint64_t t86 = GM_LD(uint64_t, ((uint8_t *)&gm_l4));
            gm_l12 = t86;
            uint64_t t87 = gm_l12;
            uint64_t r88; bool o89 = __builtin_add_overflow(t87, ((uint64_t)1ULL), &r88);
            if (o89) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 757, 30);
            uint64_t t90 = r88;
            gm_l11 = t90;
            goto gm_lb11;
        }
gm_lb12:;
    }
gm_lb11:;
    gm_t9qkh0rsooc8d t91 = gm_l1;
    uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&t91) + 0));
    uint8_t t93 = (uint8_t)((t92 == ((uint8_t)0ULL)));
    gm_t9qkh0rsooc8d t94;
    GM_ST(uint8_t, ((uint8_t *)&t94 + 0), ((uint8_t)0ULL));
    if (t93) {
        gm_b16_8 t95 = gm_l3;
        gm_b16_8 t96;
        GM_ST(void *, (uint8_t *)&t96, (void *)(gm_sicgmpca63jln));
        GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(1));
        uint8_t t97 = (gm_eq0iqiubfm7e(((uint8_t *)&t95), ((uint8_t *)&t96)));
        uint8_t t98 = (uint8_t)!t97;
        gm_l30 = t98;
    } else {
        gm_l30 = ((uint8_t)0);
    }
    uint8_t t99 = gm_l30;
    gm_l13 = t99;
    uint64_t t100 = gm_l11;
    gm_l15 = t100;
    void * t101 = gm_l0;
    uint64_t t102 = GM_LD(uint64_t, ((((uint8_t *)t101) + 184) + 8));
    gm_l14 = t102;
    {
        for (;;) {
            uint64_t t103 = gm_l15;
            uint64_t t104 = gm_l14;
            uint8_t t105 = (t103 < t104);
            if (!t105) {
                goto gm_lb13;
            }
            {
                {
                    {
                        uint64_t t106 = gm_l15;
                        gm_l16 = t106;
                        {
                            void * t107 = gm_l0;
                            void * t108 = GM_LD(void *, (((uint8_t *)t107) + 184));
                            uint64_t t109 = GM_LD(uint64_t, ((((uint8_t *)t107) + 184) + 8));
                            uint64_t t110 = gm_l16;
                            if ((uintptr_t)t110 >= (uintptr_t)(t109)) gm_trap_index((uintptr_t)t110, t109, gm_srcmok80ho8hh01, 763, 14);
                            memmove(((uint8_t *)&F.l17), (((uint8_t *)t108) + (uintptr_t)t110 * 104), 104);
                        }
                        {
                            uint8_t t111 = GM_LD(uint8_t, (((uint8_t *)&F.l17) + 0));
                            uint8_t t112 = (uint8_t)((t111 == ((uint8_t)0ULL)));
                            gm_tbf8u1b86cgmi t113 = GM_LD(gm_tbf8u1b86cgmi, ((uint8_t *)&F.l17));
                            gm_tbf8u1b86cgmi t114;
                            memset(&t114, 0, sizeof t114);
                            GM_ST(uint8_t, ((uint8_t *)&t114 + 0), ((uint8_t)0ULL));
                            if (t112) {
                                gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 56));
                                gm_l18 = t115;
                                gm_l19 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t116 = gm_l19;
                                        uint64_t t117 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                                        uint8_t t118 = (t116 < t117);
                                        if (!t118) {
                                            goto gm_lb18;
                                        }
                                        {
                                            void * t119 = GM_LD(void *, ((uint8_t *)&gm_l18));
                                            uint64_t t120 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                                            uint64_t t121 = gm_l19;
                                            {
                                                {
                                                    memmove(((uint8_t *)&gm_l20), (((uint8_t *)t119) + (uintptr_t)t121 * 24), 24);
                                                    uint8_t t122 = GM_LD(uint8_t, ((uint8_t *)&gm_l20));
                                                    uint8_t t123 = (uint8_t)!t122;
                                                    if (t123) {
                                                        gm_l31 = ((uint8_t)1);
                                                    } else {
                                                        uint8_t t124 = gm_l13;
                                                        gm_l31 = t124;
                                                    }
                                                    uint8_t t125 = gm_l31;
                                                    if (t125) {
                                                        F.ret = ((uint8_t)0);
                                                        goto gm_fret;
                                                    }
                                                    goto gm_lb21;
                                                }
gm_lb22:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 765, 13);
                                            }
gm_lb21:;
                                        }
gm_lb20:;
                                        uint64_t t126 = gm_l19;
                                        uint64_t r127; bool o128 = __builtin_add_overflow(t126, ((uint64_t)1ULL), &r127);
                                        uint64_t t129 = r127;
                                        gm_l19 = t129;
                                    }
                                }
gm_lb18:;
                            }
                        }
                        goto gm_lb16;
                    }
gm_lb17:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 762, 5);
                }
gm_lb16:;
            }
gm_lb15:;
            uint64_t t130 = gm_l15;
            uint64_t r131; bool o132 = __builtin_add_overflow(t130, ((uint64_t)1ULL), &r131);
            uint64_t t133 = r131;
            gm_l15 = t133;
        }
    }
gm_lb13:;
    gm_b16_8 t134;
    GM_ST(void *, (uint8_t *)&t134, (void *)(gm_s0m07bui94rjq));
    GM_ST(uintptr_t, (uint8_t *)&t134 + 8, (uintptr_t)(7));
    gm_l21 = t134;
    {
        {
            uint8_t t135 = (uint8_t)((((uint8_t *)&gm_l4))[8] != 0);
            if (!t135) {
                goto gm_lb24;
            }
            uint64_t t136 = GM_LD(uint64_t, ((uint8_t *)&gm_l4));
            gm_l22 = t136;
            void * t137 = gm_l0;
            void * t138 = GM_LD(void *, (((uint8_t *)t137) + 184));
            uint64_t t139 = GM_LD(uint64_t, ((((uint8_t *)t137) + 184) + 8));
            uint64_t t140 = gm_l22;
            if ((uintptr_t)t140 >= (uintptr_t)(t139)) gm_trap_index((uintptr_t)t140, t139, gm_srcmok80ho8hh01, 772, 14);
            memmove(((uint8_t *)&F.l23), (((uint8_t *)t138) + (uintptr_t)t140 * 104), 104);
            {
                {
                    uint8_t t141 = GM_LD(uint8_t, (((uint8_t *)&F.l23) + 0));
                    uint8_t t142 = (uint8_t)(t141 == ((uint8_t)1ULL));
                    if (!t142) {
                        goto gm_lb26;
                    }
                    memmove(((uint8_t *)&F.l24), (((uint8_t *)&F.l23) + 8), 40);
                    gm_t9qkh0rsooc8d t143 = gm_l1;
                    uint8_t t144 = GM_LD(uint8_t, (((uint8_t *)&t143) + 0));
                    uint8_t t145 = (uint8_t)((t144 == ((uint8_t)1ULL)));
                    gm_t9qkh0rsooc8d t146;
                    GM_ST(uint8_t, ((uint8_t *)&t146 + 0), ((uint8_t)1ULL));
                    if (t145) {
                        gm_b16_8 t147 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 8));
                        gm_l21 = t147;
                    } else {
                        gm_b16_8 t148 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 24));
                        gm_l21 = t148;
                    }
                    goto gm_lb25;
                }
gm_lb26:;
                {
                    uint8_t t149 = GM_LD(uint8_t, (((uint8_t *)&F.l23) + 0));
                    uint8_t t150 = (uint8_t)(t149 == ((uint8_t)2ULL));
                    if (!t150) {
                        goto gm_lb27;
                    }
                    memmove(((uint8_t *)&gm_l25), (((uint8_t *)&F.l23) + 8), 24);
                    gm_b16_8 t151 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l25) + 8));
                    gm_l21 = t151;
                    goto gm_lb25;
                }
gm_lb27:;
                {
                    uint8_t t152 = GM_LD(uint8_t, (((uint8_t *)&F.l23) + 0));
                    uint8_t t153 = (uint8_t)(t152 == ((uint8_t)0ULL));
                    if (!t153) {
                        goto gm_lb28;
                    }
                    gm_trap("reached `@unreachable()`", gm_srcmok80ho8hh01, 776, 23);
                }
gm_lb28:;
                gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 773, 9);
            }
gm_lb25:;
            goto gm_lb23;
        }
gm_lb24:;
    }
gm_lb23:;
    void * t154 = gm_l0;
    gm_b16_8 t155 = gm_l21;
    gm_tf5srh4mp4b3i t156 = gm_l4;
    gm_b0_1 t157 = gm_f_jump_pvnkvme((void *)NULL, cx, NULL, t154, t155, t156);
    if (gm_unw.kind) {
        goto gm_lad;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_local_lv_m2jtf0n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_local_lv_m2jtf0n F;
    void * gm_l0; /* fg */
    uint32_t gm_l1; /* l */
    void * gm_l2; /* g */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 471u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l4;
    gm_tftb1odc739t5 gm_l5;
    gm_tabb710d9mq2e gm_l6;
    gm_tftb1odc739t5 gm_l7;
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, ((uint8_t *)t8));
    gm_l2 = t9;
    uint32_t t10 = gm_l1;
    uint64_t t11 = (uint64_t)t10;
    gm_l3 = t11;
    void * t12 = gm_l0;
    void * t13 = GM_LD(void *, (((uint8_t *)t12) + 288));
    uint64_t t14 = GM_LD(uint64_t, ((((uint8_t *)t12) + 288) + 8));
    uint64_t t15 = gm_l3;
    if ((uintptr_t)t15 >= (uintptr_t)(t14)) gm_trap_index((uintptr_t)t15, t14, gm_srcmok80ho8hh01, 539, 8);
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)t13) + (uintptr_t)t15 * 1));
    if (t16) {
        {
            for (;;) {
                void * t17 = gm_l2;
                uint64_t t18 = GM_LD(uint64_t, ((((uint8_t *)t17) + 3160) + 8));
                uint64_t t19 = gm_l3;
                uint8_t t20 = (t18 <= t19);
                if (!t20) {
                    goto gm_lb1;
                }
                {
                    void * t21 = gm_l2;
                    void * t22 = (void *)(((uint8_t *)t21) + 3160);
                    void * t23 = gm_l2;
                    uint64_t t24 = GM_LD(uint64_t, ((((uint8_t *)t23) + 3160) + 8));
                    gm_t4ce0jd3tdro4 t25;
                    GM_ST(uint64_t, ((uint8_t *)&t25 + 0), t24);
                    gm_f_format_gt1sds7((void *)&gm_l4, (void *)NULL, cx, NULL, t25);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
                    if (!t26) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 8, gm_srcmok80ho8hh01, 540, 65);
                    gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
                    gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
                    gm_tftb1odc739t5 t29 = gm_f_push_3ef746b(t22, t28);
                    gm_l5 = t29;
                    uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
                    if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srcmok80ho8hh01, 540, 46);
                }
gm_lb3:;
            }
        }
gm_lb1:;
        void * t31 = gm_l2;
        void * t32 = GM_LD(void *, (((uint8_t *)t31) + 3160));
        uint64_t t33 = GM_LD(uint64_t, ((((uint8_t *)t31) + 3160) + 8));
        uint64_t t34 = gm_l3;
        if ((uintptr_t)t34 >= (uintptr_t)(t33)) gm_trap_index((uintptr_t)t34, t33, gm_srcmok80ho8hh01, 541, 16);
        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)t32) + (uintptr_t)t34 * 16));
        F.ret = t35;
        goto gm_fret;
    }
    {
        for (;;) {
            void * t36 = gm_l2;
            uint64_t t37 = GM_LD(uint64_t, ((((uint8_t *)t36) + 3200) + 8));
            uint64_t t38 = gm_l3;
            uint8_t t39 = (t37 <= t38);
            if (!t39) {
                goto gm_lb4;
            }
            {
                void * t40 = gm_l2;
                void * t41 = (void *)(((uint8_t *)t40) + 3200);
                void * t42 = gm_l2;
                uint64_t t43 = GM_LD(uint64_t, ((((uint8_t *)t42) + 3200) + 8));
                gm_t4ce0jd3tdro4 t44;
                GM_ST(uint64_t, ((uint8_t *)&t44 + 0), t43);
                gm_f_format_ghqt9ar((void *)&gm_l6, (void *)NULL, cx, NULL, t44);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t45 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                if (!t45) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_srcmok80ho8hh01, 543, 61);
                gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
                gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
                gm_tftb1odc739t5 t48 = gm_f_push_3ef746b(t41, t47);
                gm_l7 = t48;
                uint8_t t49 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                if (!t49) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srcmok80ho8hh01, 543, 42);
            }
gm_lb6:;
        }
    }
gm_lb4:;
    void * t50 = gm_l2;
    void * t51 = GM_LD(void *, (((uint8_t *)t50) + 3200));
    uint64_t t52 = GM_LD(uint64_t, ((((uint8_t *)t50) + 3200) + 8));
    uint64_t t53 = gm_l3;
    if ((uintptr_t)t53 >= (uintptr_t)(t52)) gm_trap_index((uintptr_t)t53, t52, gm_srcmok80ho8hh01, 544, 12);
    gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)t51) + (uintptr_t)t53 * 16));
    F.ret = t54;
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
inline __attribute__((aligned(4))) void gm_f_loop_scope_ee94ihm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_loop_scope_ee94ihm F;
    void * gm_l0; /* fg */
    uint32_t gm_l1; /* label */
    gm_b16_8 gm_l2; /* brk */
    gm_b16_8 gm_l3; /* cont_ */
    GM_STACK_CHECK(&F, 220u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint32_t t2 = gm_l1;
    gm_b16_8 t3 = gm_l2;
    gm_b16_8 t4 = gm_l3;
    gm_thognv9odov03 t5;
    GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
    GM_ST(uint32_t, ((uint8_t *)&t5 + 0), t2);
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 8), t3);
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 24), t4);
    void * t6 = gm_l0;
    gm_tbf8u1b86cgmi t7;
    GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)1ULL));
    GM_ST(gm_thognv9odov03, ((uint8_t *)&t7 + 8), t5);
    uint32_t t8 = gm_f_fresh_5kokjmg(t6);
    gm_trqfvmjtt4sij t9;
    gm_f_list_rl2scuf((void *)&t9, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tehgl6lkqgvt0 *)gm_out) + 52, 0);
    GM_ST(gm_tbf8u1b86cgmi, ((uint8_t *)&(*(gm_tehgl6lkqgvt0 *)gm_out) + 0), t7);
    GM_ST(uint32_t, ((uint8_t *)&(*(gm_tehgl6lkqgvt0 *)gm_out) + 48), t8);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_tehgl6lkqgvt0 *)gm_out) + 56), t9);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_tehgl6lkqgvt0 *)gm_out) + 96), ((uint64_t)0ULL));
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
inline __attribute__((aligned(4))) uint8_t gm_f_no_defers_hci755r(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_no_defers_hci755r F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 126u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t9 = gm_l0;
    gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)t9) + 184));
    gm_l1 = t10;
    gm_l2 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t11 = gm_l2;
            uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t13 = (t11 < t12);
            if (!t13) {
                goto gm_lb1;
            }
            {
                void * t14 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t16 = gm_l2;
                {
                    {
                        memmove(((uint8_t *)&F.l3), (((uint8_t *)t14) + (uintptr_t)t16 * 104), 104);
                        {
                            {
                                {
                                    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                                    uint8_t t18 = (uint8_t)((t17 == ((uint8_t)0ULL)));
                                    if (!t18) {
                                        goto gm_lb7;
                                    }
                                }
                                uint64_t t19 = GM_LD(uint64_t, ((((uint8_t *)&F.l3) + 56) + 8));
                                uint8_t t20 = (t19 > ((uint64_t)0ULL));
                                if (!t20) {
                                    goto gm_lb7;
                                }
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 787, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t21 = gm_l2;
            uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
            uint64_t t24 = r22;
            gm_l2 = t24;
        }
    }
gm_lb1:;
    F.ret = ((uint8_t)1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_param_index_rp1nn6k(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_param_index_rp1nn6k F;
    void * gm_l0; /* fg */
    uint32_t gm_l1; /* l */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    uint32_t gm_l5; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 274u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tn68el8d14vku gm_l6;
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, (((uint8_t *)t7) + 120));
    gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)t8) + 32));
    gm_l2 = t9;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t10 = gm_l3;
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t12 = (t10 < t11);
            if (!t12) {
                goto gm_lb1;
            }
            {
                uint64_t t13 = gm_l3;
                void * t14 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t16 = gm_l3;
                uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)t14) + (uintptr_t)t16 * 4));
                gm_tn68el8d14vku t18;
                GM_ST(uint32_t, (uint8_t *)&t18 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t18 + 0), t13);
                GM_ST(uint32_t, ((uint8_t *)&t18 + 8), t17);
                gm_l6 = t18;
                {
                    {
                        uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&gm_l6));
                        gm_l4 = t19;
                        uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 8));
                        gm_l5 = t20;
                        uint32_t t21 = gm_l5;
                        uint32_t t22 = gm_l1;
                        uint8_t t23 = ((t21 == t22));
                        if (t23) {
                            uint64_t t24 = gm_l4;
                            gm_tf5srh4mp4b3i t25;
                            GM_ST(uint8_t, (uint8_t *)&t25 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t25 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t25 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t25), t24);
                            ((uint8_t *)&t25)[8] = 1;
                            F.ret = t25;
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 514, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t26 = gm_l3;
            uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
            uint64_t t29 = r27;
            gm_l3 = t29;
        }
    }
gm_lb1:;
    gm_tf5srh4mp4b3i t30;
    memset(&t30, 0, sizeof t30);
    F.ret = t30;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_probe_free_4hilimu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_probe_free_4hilimu F;
    void * gm_l0; /* g */
    void * gm_l1; /* def */
    uint8_t gm_l2; /* x */
    void * gm_l3; /* b */
    GM_STACK_CHECK(&F, 81u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t4 = gm_l0;
    uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 1136) + 1));
    if (t5) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    {
        {
            void * t6 = gm_l1;
            uint8_t t7 = (uint8_t)((((((uint8_t *)t6) + 145) + 1))[1] != 0);
            if (!t7) {
                goto gm_lb2;
            }
            uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t6) + 145) + 1));
            gm_l2 = t8;
            uint8_t t9 = gm_l2;
            if (!t9) {
                goto gm_lb2;
            }
            void * t10 = gm_l1;
            uint8_t t11 = (uint8_t)(GM_LD(void *, (((uint8_t *)t10) + 152)) != 0);
            if (t11) {
                void * t12 = GM_LD(void *, (((uint8_t *)t10) + 152));
                gm_l3 = t12;
            } else {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            void * t13 = gm_l0;
            void * t14 = gm_l3;
            uint8_t t15 = gm_f_calls_probe_2ia3rhf((void *)NULL, cx, NULL, t13, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t15;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    F.ret = ((uint8_t)0);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_replacen1_7ksfmmn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_replacen1_7ksfmmn F;
    gm_b16_8 gm_l0; /* text */
    gm_b16_8 gm_l1; /* from */
    gm_b16_8 gm_l2; /* to */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 442u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tf5srh4mp4b3i gm_l4;
    gm_tabb710d9mq2e gm_l5;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3 = gm_l1;
    gm_tf5srh4mp4b3i t4 = gm_f_find_nab29co(t2, t3);
    gm_l4 = t4;
    uint8_t t5 = (uint8_t)((((uint8_t *)&gm_l4))[8] != 0);
    if (t5) {
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&gm_l4));
        gm_l3 = t6;
    } else {
        gm_b16_8 t7 = gm_l0;
        F.ret = t7;
        goto gm_fret;
    }
    {
        void * t8 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t10 = gm_l3;
        uint64_t t11 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t12 = (uintptr_t)t10;
        if (t11 > t12 || t12 > (uintptr_t)t9) gm_trap_slice(t11, t12, t9, gm_srcmok80ho8hh01, 47, 33);
        uint64_t t13 = ((uintptr_t)t10) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13));
        gm_b16_8 t15 = gm_l2;
        void * t16 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t18 = gm_l3;
        uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint64_t r20; bool o21 = __builtin_add_overflow(t18, t19, &r20);
        if (o21) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 47, 54);
        uint64_t t22 = r20;
        uint64_t t23 = (uintptr_t)t22;
        uint64_t t24 = (uintptr_t)t17;
        if (t23 > t24 || t24 > (uintptr_t)t17) gm_trap_slice(t23, t24, t17, gm_srcmok80ho8hh01, 47, 49);
        uint64_t t25 = ((uintptr_t)t17) - (uintptr_t)t22;
        gm_b16_8 t26;
        GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t16 + (uintptr_t)t22 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25));
        gm_tfeab279c16rr t27;
        GM_ST(gm_b16_8, ((uint8_t *)&t27 + 0), t14);
        GM_ST(gm_b16_8, ((uint8_t *)&t27 + 16), t15);
        GM_ST(gm_b16_8, ((uint8_t *)&t27 + 32), t26);
        gm_f_format_dsta2ql((void *)&gm_l5, (void *)NULL, cx, NULL, &t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t28 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t28) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 8, gm_srcmok80ho8hh01, 47, 12);
    gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
    gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
    F.ret = t30;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_size_attrs_abk1l3m(uint32_t a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_src_line_d24sajl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_src_line_d24sajl F;
    void * gm_l0; /* fg */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* d */
    GM_STACK_CHECK(&F, 158u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_th2lqbo77pvsc gm_l3;
    gm_tftb1odc739t5 gm_l4;
    {
        {
            void * t4 = gm_l0;
            void * t5 = GM_LD(void *, ((uint8_t *)t4));
            gm_tl874t2atnlh3 t6 = gm_l1;
            gm_f_line_directive_fjg94gc((void *)&gm_l3, (void *)NULL, cx, NULL, t5, t6);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
            if (!t7) {
                goto gm_lb2;
            }
            gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
            gm_l2 = t8;
            void * t9 = gm_l0;
            void * t10 = (void *)(((uint8_t *)t9) + 128);
            gm_b16_8 t11 = gm_l2;
            gm_tnpifv20irnph t12;
            GM_ST(gm_b16_8, ((uint8_t *)&t12 + 0), t11);
            gm_tftb1odc739t5 t13 = gm_f_write_cdouvhd(t10, t12);
            gm_l4 = t13;
            uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
            if (!t14) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srcmok80ho8hh01, 425, 41);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_start_exit_m57dnn5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_start_exit_m57dnn5 F;
    void * gm_l0; /* fg */
    gm_t9qkh0rsooc8d gm_l1; /* kind */
    gm_b16_8 gm_l2; /* frame */
    gm_b16_8 gm_l3; /* label */
    gm_b16_8 gm_l4; /* is_err */
    uint8_t gm_l5; /* saved */
    GM_STACK_CHECK(&F, 314u);
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
            gm_b16_8 t4 = gm_l2;
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sb8kl54a499bf));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(6));
            uint8_t t6 = (gm_eq0iqiubfm7e(((uint8_t *)&t4), ((uint8_t *)&t5)));
            if (!t6) {
                goto gm_lb2;
            }
            void * t7 = gm_l0;
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)t7) + 242));
            uint8_t t9 = (uint8_t)!t8;
            if (!t9) {
                goto gm_lb2;
            }
            void * t10 = gm_l0;
            gm_t9qkh0rsooc8d t11 = gm_l1;
            gm_b16_8 t12 = gm_l3;
            gm_b16_8 t13 = gm_l4;
            uint8_t t14 = gm_f_local_exit_4aogt00((void *)NULL, cx, NULL, t10, t11, t12, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t14) {
                goto gm_lb2;
            }
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t15 = gm_l0;
    gm_t9qkh0rsooc8d t16 = gm_l1;
    gm_b16_8 t17 = gm_f_exit_c_name_i72r98r(t16);
    gm_b16_8 t18 = gm_l2;
    gm_b16_8 t19 = gm_l3;
    gm_b16_8 t20 = gm_l4;
    gm_t0uuhaljsf410 t21;
    GM_ST(gm_b16_8, ((uint8_t *)&t21 + 0), t17);
    GM_ST(gm_b16_8, ((uint8_t *)&t21 + 16), t18);
    GM_ST(gm_b16_8, ((uint8_t *)&t21 + 32), t19);
    GM_ST(gm_b16_8, ((uint8_t *)&t21 + 48), t20);
    gm_b0_1 t22 = gm_f_linef_7p5m9ll((void *)NULL, cx, NULL, t15, &t21);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t23 = gm_l0;
    uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)t23) + 240));
    gm_l5 = t24;
    void * t25 = gm_l0;
    GM_ST(uint8_t, (((uint8_t *)t25) + 240), ((uint8_t)0));
    void * t26 = gm_l0;
    gm_b0_1 t27 = gm_f_ladder_36amdbn((void *)NULL, cx, NULL, t26);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t28 = gm_l0;
    uint8_t t29 = gm_l5;
    GM_ST(uint8_t, (((uint8_t *)t28) + 240), t29);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uninit_jr39s5u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_tmp_uninit_jr39s5u F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* c */
    gm_b16_8 gm_l3; /* n */
    GM_STACK_CHECK(&F, 300u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 248));
    void * t5 = gm_l0;
    void * t6 = GM_LD(void *, ((uint8_t *)t5));
    void * t7 = (void *)(((uint8_t *)t6) + 8);
    gm_t88rcucg1b7uk t8 = gm_l1;
    uint64_t t9 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t7, t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t r10; bool o11 = __builtin_add_overflow(t4, t9, &r10);
    if (o11) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 489, 5);
    uint64_t t12 = r10;
    GM_ST(uint64_t, (((uint8_t *)t2) + 248), t12);
    void * t13 = gm_l0;
    void * t14 = GM_LD(void *, ((uint8_t *)t13));
    void * t15 = (void *)(((uint8_t *)t14) + 8);
    gm_t88rcucg1b7uk t16 = gm_l1;
    gm_b16_8 t17 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t15, t16);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t17;
    void * t18 = gm_l0;
    gm_b16_8 t19 = gm_f_fresh_tmp_6tadl69((void *)NULL, cx, NULL, t18);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t19;
    void * t20 = gm_l0;
    gm_b16_8 t21 = gm_l2;
    gm_b16_8 t22 = gm_l3;
    gm_t6j6smj7qoctu t23;
    GM_ST(gm_b16_8, ((uint8_t *)&t23 + 0), t21);
    GM_ST(gm_b16_8, ((uint8_t *)&t23 + 16), t22);
    gm_b0_1 t24 = gm_f_linef_eils77b((void *)NULL, cx, NULL, t20, &t23);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t25 = gm_l3;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uohsptq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_tmp_uohsptq F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* init */
    gm_b16_8 gm_l3; /* c */
    gm_b16_8 gm_l4; /* n */
    GM_STACK_CHECK(&F, 348u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 248));
    void * t5 = gm_l0;
    void * t6 = GM_LD(void *, ((uint8_t *)t5));
    void * t7 = (void *)(((uint8_t *)t6) + 8);
    gm_t88rcucg1b7uk t8 = gm_l1;
    uint64_t t9 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t7, t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t r10; bool o11 = __builtin_add_overflow(t4, t9, &r10);
    if (o11) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 480, 5);
    uint64_t t12 = r10;
    GM_ST(uint64_t, (((uint8_t *)t2) + 248), t12);
    void * t13 = gm_l0;
    void * t14 = GM_LD(void *, ((uint8_t *)t13));
    void * t15 = (void *)(((uint8_t *)t14) + 8);
    gm_t88rcucg1b7uk t16 = gm_l1;
    gm_b16_8 t17 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t15, t16);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t17;
    void * t18 = gm_l0;
    gm_b16_8 t19 = gm_f_fresh_tmp_6tadl69((void *)NULL, cx, NULL, t18);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t19;
    void * t20 = gm_l0;
    gm_b16_8 t21 = gm_l3;
    gm_b16_8 t22 = gm_l4;
    gm_b16_8 t23 = gm_l2;
    gm_tfeab279c16rr t24;
    GM_ST(gm_b16_8, ((uint8_t *)&t24 + 0), t21);
    GM_ST(gm_b16_8, ((uint8_t *)&t24 + 16), t22);
    GM_ST(gm_b16_8, ((uint8_t *)&t24 + 32), t23);
    gm_b0_1 t25 = gm_f_linef_bi02v43((void *)NULL, cx, NULL, t20, &t24);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t26 = gm_l4;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_unit_8ekedsl(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_unwind_call_4k8ulav(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unwind_ladder_a9ij1gj(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unwind_ladder_a9ij1gj F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 168));
    uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)1ULL), &r5);
    if (o6) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 588, 5);
    uint64_t t7 = r5;
    GM_ST(uint64_t, (((uint8_t *)t2) + 168), t7);
    void * t8 = gm_l0;
    gm_b0_1 t9 = gm_f_ladder_36amdbn((void *)NULL, cx, NULL, t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t10 = gm_l0;
    void * t11 = gm_l0;
    uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 168));
    uint64_t r13; bool o14 = __builtin_sub_overflow(t12, ((uint64_t)1ULL), &r13);
    if (o14) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 590, 5);
    uint64_t t15 = r13;
    GM_ST(uint64_t, (((uint8_t *)t10) + 168), t15);
    void * t16 = gm_l0;
    gm_b16_8 t17;
    GM_ST(void *, (uint8_t *)&t17, (void *)(gm_s2cuflhqkkuaq));
    GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(1));
    gm_b0_1 t18 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t16, t17);
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
inline __attribute__((aligned(4))) void gm_f_with_scope_o670qbc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2) {
    struct gm_fr_with_scope_o670qbc F;
    void * gm_l0; /* fg */
    uint32_t gm_l1; /* label */
    gm_b16_8 gm_l2; /* end */
    GM_STACK_CHECK(&F, 172u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint32_t t2 = gm_l1;
    gm_b16_8 t3 = gm_l2;
    gm_tiilmfd16v3fc t4;
    GM_ST(uint32_t, (uint8_t *)&t4 + 4, 0);
    GM_ST(uint32_t, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_b16_8, ((uint8_t *)&t4 + 8), t3);
    void * t5 = gm_l0;
    gm_tbf8u1b86cgmi t6;
    GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 32, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 40, 0);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)2ULL));
    GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t6 + 8), t4);
    uint32_t t7 = gm_f_fresh_5kokjmg(t5);
    gm_trqfvmjtt4sij t8;
    gm_f_list_rl2scuf((void *)&t8, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tehgl6lkqgvt0 *)gm_out) + 52, 0);
    GM_ST(gm_tbf8u1b86cgmi, ((uint8_t *)&(*(gm_tehgl6lkqgvt0 *)gm_out) + 0), t6);
    GM_ST(uint32_t, ((uint8_t *)&(*(gm_tehgl6lkqgvt0 *)gm_out) + 48), t7);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_tehgl6lkqgvt0 *)gm_out) + 56), t8);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_tehgl6lkqgvt0 *)gm_out) + 96), ((uint64_t)0ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_zero_tlk13aa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_zero_tlk13aa F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* cty */
    GM_STACK_CHECK(&F, 344u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l3;
    gm_t5v8n6qrn4rh4 gm_l4;
    gm_tabb710d9mq2e gm_l5;
    gm_tabb710d9mq2e gm_l6;
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, ((uint8_t *)t8));
    void * t10 = (void *)(((uint8_t *)t9) + 8);
    gm_t88rcucg1b7uk t11 = gm_l1;
    gm_b16_8 t12 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t10, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t12;
    {
        void * t13 = gm_l0;
        void * t14 = GM_LD(void *, ((uint8_t *)t13));
        void * t15 = (void *)(((uint8_t *)t14) + 8);
        gm_t88rcucg1b7uk t16 = gm_l1;
        gm_f_repr_53quqki((void *)&gm_l4, (void *)NULL, cx, NULL, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)0ULL));
            if (!t18) {
                goto gm_lb2;
            }
            gm_l3 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)1ULL));
            if (!t20) {
                goto gm_lb3;
            }
            gm_l3 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)2ULL));
            if (!t22) {
                goto gm_lb4;
            }
            gm_l3 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)3ULL));
            if (!t24) {
                goto gm_lb5;
            }
            gm_l3 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb5:;
        {
            gm_l3 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb6:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 500, 8);
    }
gm_lb1:;
    uint8_t t25 = gm_l3;
    if (t25) {
        gm_b16_8 t26 = gm_l2;
        gm_tnpifv20irnph t27;
        GM_ST(gm_b16_8, ((uint8_t *)&t27 + 0), t26);
        gm_f_format_07f8le4((void *)&gm_l5, (void *)NULL, cx, NULL, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t28 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
        if (!t28) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 8, gm_srcmok80ho8hh01, 500, 76);
        gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
        gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
        F.ret = t30;
        goto gm_fret;
    }
    {
        gm_b16_8 t31 = gm_l2;
        gm_tnpifv20irnph t32;
        GM_ST(gm_b16_8, ((uint8_t *)&t32 + 0), t31);
        gm_f_format_loh175m((void *)&gm_l6, (void *)NULL, cx, NULL, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (!t33) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_srcmok80ho8hh01, 501, 12);
    gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
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

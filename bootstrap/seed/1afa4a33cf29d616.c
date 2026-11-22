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
typedef struct gm_csfq64m1m1rt6m { int64_t f0; } gm_csfq64m1m1rt6m;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t f5; } gm_t4q2b5ibsrdik;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { int64_t a[4]; } gm_t767tucha9lsh;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { int64_t f0; } gm_tm98ap4112hee;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; int32_t v1; } f1; } gm_tsoiri44d0i6q;
_Static_assert(sizeof(gm_csfq64m1m1rt6m) == 8 && _Alignof(gm_csfq64m1m1rt6m) == 8 && offsetof(gm_csfq64m1m1rt6m, f0) == 0, "layout of `Dir`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; } gm_t0am46iohlpol;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_tm98ap4112hee f0; gm_b16_8 f1; } gm_t3olsle24rveu;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
_Static_assert(sizeof(gm_t4q2b5ibsrdik) == 6 && _Alignof(gm_t4q2b5ibsrdik) == 1 && offsetof(gm_t4q2b5ibsrdik, f0) == 0 && offsetof(gm_t4q2b5ibsrdik, f1) == 1 && offsetof(gm_t4q2b5ibsrdik, f2) == 2 && offsetof(gm_t4q2b5ibsrdik, f3) == 3 && offsetof(gm_t4q2b5ibsrdik, f4) == 4 && offsetof(gm_t4q2b5ibsrdik, f5) == 5, "layout of `OpenOptions`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; gm_t9qkh0rsooc8d f3; gm_t9qkh0rsooc8d f4; gm_t9qkh0rsooc8d f5; gm_t9qkh0rsooc8d f6; gm_t9qkh0rsooc8d f7; gm_t9qkh0rsooc8d f8; gm_t9qkh0rsooc8d f9; gm_t9qkh0rsooc8d f10; gm_t9qkh0rsooc8d f11; gm_t9qkh0rsooc8d f12; gm_t9qkh0rsooc8d f13; } gm_t4qfn59m757do;
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; int64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t5j14m8ru132p;
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_t617vr72bd7b3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tm98ap4112hee v1; } f1; } gm_t62ebbi2i5rkd;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_t4ce0jd3tdro4 f0; uint8_t f1; uint8_t pe[7]; } gm_t6ie8iajlnpt1;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
_Static_assert(sizeof(gm_t767tucha9lsh) == 32 && _Alignof(gm_t767tucha9lsh) == 8 && offsetof(gm_t767tucha9lsh, a) == 0, "layout of `[4]i64`");
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; uint64_t f1; } gm_te0mgnjdphs16;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; uint64_t v1; } f1; } gm_thf5kr0is9u9i;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_tsoiri44d0i6q v1; } f1; } gm_tlmjd3nndlf3r;
_Static_assert(sizeof(gm_tm98ap4112hee) == 8 && _Alignof(gm_tm98ap4112hee) == 8 && offsetof(gm_tm98ap4112hee, f0) == 0, "layout of `File`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_tm98ap4112hee f0; uint8_t f1; uint8_t pe[7]; } gm_tppo2hmut88dq;
typedef struct { gm_tsoiri44d0i6q f0; uint8_t f1; uint8_t pe[3]; } gm_tqnlpelep1582;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tsoiri44d0i6q) == 8 && _Alignof(gm_tsoiri44d0i6q) == 4 && offsetof(gm_tsoiri44d0i6q, f0) == 0 && offsetof(gm_tsoiri44d0i6q, f1) == 4, "layout of `ExitStatus`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; } f1; } gm_ttln0d0hd70en;
typedef struct { gm_b16_8 f0; uint16_t f1; uint8_t pe[6]; } gm_tvaas22dl64e0;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; uint16_t v1; } f1; } gm_tvcqs9u9iqpct;
_Static_assert(sizeof(gm_t0am46iohlpol) == 2 && _Alignof(gm_t0am46iohlpol) == 1 && offsetof(gm_t0am46iohlpol, f0) == 0 && offsetof(gm_t0am46iohlpol, f1) == 1, "layout of `(Command, Command)`");
typedef struct { gm_t617vr72bd7b3 f0; uint8_t f1; uint8_t pe[7]; } gm_t11t9d8s5diq9;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; uint8_t p3[5]; gm_th2lqbo77pvsc f3; gm_b16_8 f4; uint8_t f5; uint8_t f6; uint8_t p7[6]; gm_th2lqbo77pvsc f7; } gm_t32dckm4jfarl;
_Static_assert(sizeof(gm_t3olsle24rveu) == 24 && _Alignof(gm_t3olsle24rveu) == 8 && offsetof(gm_t3olsle24rveu, f0) == 0 && offsetof(gm_t3olsle24rveu, f1) == 8, "layout of `(child: Child, data: string)`");
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
_Static_assert(sizeof(gm_t4qfn59m757do) == 14 && _Alignof(gm_t4qfn59m757do) == 1 && offsetof(gm_t4qfn59m757do, f0) == 0 && offsetof(gm_t4qfn59m757do, f1) == 1 && offsetof(gm_t4qfn59m757do, f2) == 2 && offsetof(gm_t4qfn59m757do, f3) == 3 && offsetof(gm_t4qfn59m757do, f4) == 4 && offsetof(gm_t4qfn59m757do, f5) == 5 && offsetof(gm_t4qfn59m757do, f6) == 6 && offsetof(gm_t4qfn59m757do, f7) == 7 && offsetof(gm_t4qfn59m757do, f8) == 8 && offsetof(gm_t4qfn59m757do, f9) == 9 && offsetof(gm_t4qfn59m757do, f10) == 10 && offsetof(gm_t4qfn59m757do, f11) == 11 && offsetof(gm_t4qfn59m757do, f12) == 12 && offsetof(gm_t4qfn59m757do, f13) == 13, "layout of `(SysOp, SysOp, SysOp, SysOp, SysOp, SysOp, SysOp, SysOp, SysOp, SysOp, SysOp, SysOp, SysOp, SysOp)`");
_Static_assert(sizeof(gm_t5j14m8ru132p) == 32 && _Alignof(gm_t5j14m8ru132p) == 8 && offsetof(gm_t5j14m8ru132p, f0) == 0 && offsetof(gm_t5j14m8ru132p, f1) == 8 && offsetof(gm_t5j14m8ru132p, f2) == 16 && offsetof(gm_t5j14m8ru132p, f3) == 24, "layout of `FileInfo`");
_Static_assert(sizeof(gm_t617vr72bd7b3) == 16 && _Alignof(gm_t617vr72bd7b3) == 8 && offsetof(gm_t617vr72bd7b3, f0) == 0 && offsetof(gm_t617vr72bd7b3, f1) == 8, "layout of `DirEntry`");
_Static_assert(sizeof(gm_t62ebbi2i5rkd) == 16 && _Alignof(gm_t62ebbi2i5rkd) == 8 && offsetof(gm_t62ebbi2i5rkd, f0) == 0 && offsetof(gm_t62ebbi2i5rkd, f1) == 8, "layout of `IoError!File`");
_Static_assert(sizeof(gm_t6ie8iajlnpt1) == 16 && _Alignof(gm_t6ie8iajlnpt1) == 8 && offsetof(gm_t6ie8iajlnpt1, f0) == 0 && offsetof(gm_t6ie8iajlnpt1, f1) == 8, "layout of `?Duration`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
typedef struct { gm_thf5kr0is9u9i f0; gm_tppo2hmut88dq f1; int64_t f2; } gm_t7eae7gj5k2n2;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_trqfvmjtt4sij v1; } f1; } gm_t9922qjstblh1;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; int64_t v1; } f1; } gm_tarq9lp8tdk6f;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_b16_8 v1; } f1; } gm_td0ept12e7q8b;
_Static_assert(sizeof(gm_te0mgnjdphs16) == 16 && _Alignof(gm_te0mgnjdphs16) == 8 && offsetof(gm_te0mgnjdphs16, f0) == 0 && offsetof(gm_te0mgnjdphs16, f1) == 8, "layout of `Chunk`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_te0mgnjdphs16 f0; uint8_t f1; uint8_t pe[7]; } gm_thdrvp9q0u7b5;
_Static_assert(sizeof(gm_thf5kr0is9u9i) == 16 && _Alignof(gm_thf5kr0is9u9i) == 8 && offsetof(gm_thf5kr0is9u9i, f0) == 0 && offsetof(gm_thf5kr0is9u9i, f1) == 8, "layout of `IoError!usize`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t5j14m8ru132p v1; } f1; } gm_tih6h4avv5nrb;
_Static_assert(sizeof(gm_tlmjd3nndlf3r) == 12 && _Alignof(gm_tlmjd3nndlf3r) == 4 && offsetof(gm_tlmjd3nndlf3r, f0) == 0 && offsetof(gm_tlmjd3nndlf3r, f1) == 4, "layout of `IoError!ExitStatus`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t3olsle24rveu v1; gm_tvaas22dl64e0 v2; gm_tm98ap4112hee v3; gm_t3olsle24rveu v4; gm_t3olsle24rveu v5; gm_t3olsle24rveu v6; gm_t3olsle24rveu v7; gm_tm98ap4112hee v8; } f1; } gm_tnp3hpcm9uhoi;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_t62ebbi2i5rkd v1; } f1; } gm_toqti1ubjnfd1;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2lqu8jsj511d v0; gm_t617vr72bd7b3 v1; gm_t66j1im9ip128 v2; gm_t2lqu8jsj511d v3; uint64_t v4; gm_t617vr72bd7b3 v5; } f1; } gm_tpekhfs32op0f;
_Static_assert(sizeof(gm_tppo2hmut88dq) == 16 && _Alignof(gm_tppo2hmut88dq) == 8 && offsetof(gm_tppo2hmut88dq, f0) == 0 && offsetof(gm_tppo2hmut88dq, f1) == 8, "layout of `?Child`");
_Static_assert(sizeof(gm_tqnlpelep1582) == 12 && _Alignof(gm_tqnlpelep1582) == 4 && offsetof(gm_tqnlpelep1582, f0) == 0 && offsetof(gm_tqnlpelep1582, f1) == 8, "layout of `?ExitStatus`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_tqnlpelep1582 v1; } f1; } gm_tr6norheal11n;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ttln0d0hd70en) == 12 && _Alignof(gm_ttln0d0hd70en) == 4 && offsetof(gm_ttln0d0hd70en, f0) == 0 && offsetof(gm_ttln0d0hd70en, f1) == 4, "layout of `IoError!()`");
typedef struct { gm_t4c7ipjj9s3fp f0; gm_t4c7ipjj9s3fp f1; gm_t4c7ipjj9s3fp f2; gm_t4c7ipjj9s3fp f3; gm_t4c7ipjj9s3fp f4; int64_t f5; gm_tf5srh4mp4b3i f6; } gm_ttse10gjbhca2;
_Static_assert(sizeof(gm_tvaas22dl64e0) == 24 && _Alignof(gm_tvaas22dl64e0) == 8 && offsetof(gm_tvaas22dl64e0, f0) == 0 && offsetof(gm_tvaas22dl64e0, f1) == 16, "layout of `(string, u16)`");
_Static_assert(sizeof(gm_tvcqs9u9iqpct) == 12 && _Alignof(gm_tvcqs9u9iqpct) == 4 && offsetof(gm_tvcqs9u9iqpct, f0) == 0 && offsetof(gm_tvcqs9u9iqpct, f1) == 4, "layout of `IoError!u16`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_t7eae7gj5k2n2 v1; } f1; } gm_t0jbubompi7qd;
_Static_assert(sizeof(gm_t11t9d8s5diq9) == 24 && _Alignof(gm_t11t9d8s5diq9) == 8 && offsetof(gm_t11t9d8s5diq9, f0) == 0 && offsetof(gm_t11t9d8s5diq9, f1) == 16, "layout of `?DirEntry`");
_Static_assert(sizeof(gm_t32dckm4jfarl) == 80 && _Alignof(gm_t32dckm4jfarl) == 8 && offsetof(gm_t32dckm4jfarl, f0) == 0 && offsetof(gm_t32dckm4jfarl, f1) == 1 && offsetof(gm_t32dckm4jfarl, f2) == 2 && offsetof(gm_t32dckm4jfarl, f3) == 8 && offsetof(gm_t32dckm4jfarl, f4) == 32 && offsetof(gm_t32dckm4jfarl, f5) == 48 && offsetof(gm_t32dckm4jfarl, f6) == 49 && offsetof(gm_t32dckm4jfarl, f7) == 56, "layout of `SpawnOptions`");
_Static_assert(sizeof(gm_t7eae7gj5k2n2) == 40 && _Alignof(gm_t7eae7gj5k2n2) == 8 && offsetof(gm_t7eae7gj5k2n2, f0) == 0 && offsetof(gm_t7eae7gj5k2n2, f1) == 16 && offsetof(gm_t7eae7gj5k2n2, f2) == 32, "layout of `HostResult`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; gm_b16_8 v1; } f1; } gm_t934k1j7u70ct;
_Static_assert(sizeof(gm_t9922qjstblh1) == 48 && _Alignof(gm_t9922qjstblh1) == 8 && offsetof(gm_t9922qjstblh1, f0) == 0 && offsetof(gm_t9922qjstblh1, f1) == 8, "layout of `Flow!List(string)`");
_Static_assert(sizeof(gm_tarq9lp8tdk6f) == 32 && _Alignof(gm_tarq9lp8tdk6f) == 8 && offsetof(gm_tarq9lp8tdk6f, f0) == 0 && offsetof(gm_tarq9lp8tdk6f, f1) == 8, "layout of `Flow!i64`");
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
_Static_assert(sizeof(gm_td0ept12e7q8b) == 32 && _Alignof(gm_td0ept12e7q8b) == 8 && offsetof(gm_td0ept12e7q8b, f0) == 0 && offsetof(gm_td0ept12e7q8b, f1) == 8, "layout of `Flow!string`");
_Static_assert(sizeof(gm_thdrvp9q0u7b5) == 24 && _Alignof(gm_thdrvp9q0u7b5) == 8 && offsetof(gm_thdrvp9q0u7b5, f0) == 0 && offsetof(gm_thdrvp9q0u7b5, f1) == 16, "layout of `?Chunk`");
_Static_assert(sizeof(gm_tih6h4avv5nrb) == 40 && _Alignof(gm_tih6h4avv5nrb) == 8 && offsetof(gm_tih6h4avv5nrb, f0) == 0 && offsetof(gm_tih6h4avv5nrb, f1) == 8, "layout of `IoError!FileInfo`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t11t9d8s5diq9 v1; } f1; } gm_tiuq7k5hm0kak;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_thdrvp9q0u7b5 v1; } f1; } gm_tlbmih0s72in3;
_Static_assert(sizeof(gm_tnp3hpcm9uhoi) == 32 && _Alignof(gm_tnp3hpcm9uhoi) == 8 && offsetof(gm_tnp3hpcm9uhoi, f0) == 0 && offsetof(gm_tnp3hpcm9uhoi, f1) == 8, "layout of `HostOp`");
_Static_assert(sizeof(gm_toqti1ubjnfd1) == 32 && _Alignof(gm_toqti1ubjnfd1) == 8 && offsetof(gm_toqti1ubjnfd1, f0) == 0 && offsetof(gm_toqti1ubjnfd1, f1) == 8, "layout of `Flow!IoError!Socket`");
_Static_assert(sizeof(gm_tpekhfs32op0f) == 40 && _Alignof(gm_tpekhfs32op0f) == 8 && offsetof(gm_tpekhfs32op0f, f0) == 0 && offsetof(gm_tpekhfs32op0f, f1) == 8, "layout of `MemError`");
_Static_assert(sizeof(gm_tr6norheal11n) == 16 && _Alignof(gm_tr6norheal11n) == 4 && offsetof(gm_tr6norheal11n, f0) == 0 && offsetof(gm_tr6norheal11n, f1) == 4, "layout of `IoError!?ExitStatus`");
_Static_assert(sizeof(gm_ttse10gjbhca2) == 304 && _Alignof(gm_ttse10gjbhca2) == 8 && offsetof(gm_ttse10gjbhca2, f0) == 0 && offsetof(gm_ttse10gjbhca2, f1) == 56 && offsetof(gm_ttse10gjbhca2, f2) == 112 && offsetof(gm_ttse10gjbhca2, f3) == 168 && offsetof(gm_ttse10gjbhca2, f4) == 224 && offsetof(gm_ttse10gjbhca2, f5) == 280 && offsetof(gm_ttse10gjbhca2, f6) == 288, "layout of `SysState`");
_Static_assert(sizeof(gm_t0jbubompi7qd) == 48 && _Alignof(gm_t0jbubompi7qd) == 8 && offsetof(gm_t0jbubompi7qd, f0) == 0 && offsetof(gm_t0jbubompi7qd, f1) == 8, "layout of `Flow!HostResult`");
_Static_assert(sizeof(gm_t934k1j7u70ct) == 48 && _Alignof(gm_t934k1j7u70ct) == 8 && offsetof(gm_t934k1j7u70ct, f0) == 0 && offsetof(gm_t934k1j7u70ct, f1) == 8, "layout of `MemError![]u8`");
_Static_assert(sizeof(gm_tiuq7k5hm0kak) == 32 && _Alignof(gm_tiuq7k5hm0kak) == 8 && offsetof(gm_tiuq7k5hm0kak, f0) == 0 && offsetof(gm_tiuq7k5hm0kak, f1) == 8, "layout of `IoError!?DirEntry`");
_Static_assert(sizeof(gm_tlbmih0s72in3) == 32 && _Alignof(gm_tlbmih0s72in3) == 8 && offsetof(gm_tlbmih0s72in3, f0) == 0 && offsetof(gm_tlbmih0s72in3, f1) == 8, "layout of `IoError!?Chunk`");
struct gm_fr_aligned_q6sor4h {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_cpu_count_0o5882g {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_deinit_gvn47sj {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_16ab7e4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_2p7d2kv {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_57qa4h8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_6l50clp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hgrt2o4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_khi2i98 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fn_1lff729 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_2gkkusq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_aqcn8lk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_cijad0r {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ducmvdu {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_j9i1sr8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_mslt7ot {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_r8m0qjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_rg62rr4 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_tlii61s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_trt04qi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_vhnrvmh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_get_8o0rtd2 {
    gm_frame h;
    gm_tppo2hmut88dq ret;
};
struct gm_fr_get_f2mbmm8 {
    gm_frame h;
    gm_tppo2hmut88dq ret;
};
struct gm_fr_get_fqooi5f {
    gm_frame h;
    gm_tppo2hmut88dq ret;
};
struct gm_fr_get_gej4j8d {
    gm_frame h;
    gm_tppo2hmut88dq ret;
};
struct gm_fr_get_v07up09 {
    gm_frame h;
    gm_tppo2hmut88dq ret;
};
struct gm_fr_hash_map_3d5nsdr {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_3sndrtb {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_abi2bn7 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_irh77t1 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_ql49boo {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mem_trap_ecogko0 {
    gm_frame h;
    gm_tpekhfs32op0f l2; /* e */
    gm_t76123gfet73g ret;
};
struct gm_fr_proc_read_kc2l8gi {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_proc_status_7nq4d17 {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_read_u6r4vns {
    gm_frame h;
    gm_t934k1j7u70ct ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_socket_on_host_thread_u8c2vg5 {
    gm_frame h;
    gm_t7eae7gj5k2n2 l3; /* r */
    gm_toqti1ubjnfd1 ret;
};
struct gm_fr_sys_code_cjtlfo1 {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_sys_count_50c5lcp {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_sys_dest_rtrb14h {
    gm_frame h;
    gm_tpekhfs32op0f l4; /* e */
    gm_td0ept12e7q8b ret;
};
struct gm_fr_sys_fs_u0grkf6 {
    gm_frame h;
    gm_tpekhfs32op0f l28; /* err */
    gm_tarq9lp8tdk6f ret;
};
struct gm_fr_sys_has_0sego5l {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_sys_int_32e79ed {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_sys_kind_sjfdqbt {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_sys_net_aff7h5l {
    gm_frame h;
    gm_b16_8 l6; /* host */
    uint16_t l7; /* port */
    gm_t62ebbi2i5rkd l8; /* r */
    gm_b16_8 l12; /* host */
    uint16_t l13; /* port */
    gm_t62ebbi2i5rkd l14; /* r */
    gm_tm98ap4112hee l18; /* l */
    gm_tvcqs9u9iqpct l19; /* r */
    gm_tm98ap4112hee l22; /* l */
    gm_t62ebbi2i5rkd l23; /* r */
    gm_b16_8 l27; /* data */
    gm_tm98ap4112hee l28; /* s */
    gm_thf5kr0is9u9i l29; /* r */
    gm_b16_8 l30; /* buf */
    gm_tm98ap4112hee l31; /* s */
    gm_thf5kr0is9u9i l32; /* r */
    gm_tm98ap4112hee l33; /* s */
    gm_tm98ap4112hee l34; /* l */
    gm_t9qkh0rsooc8d l35; /* how */
    gm_tm98ap4112hee l36; /* s */
    gm_ttln0d0hd70en l37; /* r */
    gm_tm98ap4112hee l40; /* s */
    gm_t6ie8iajlnpt1 l41; /* recv */
    gm_t6ie8iajlnpt1 l42; /* send */
    gm_ttln0d0hd70en l43; /* r */
    gm_tm98ap4112hee l44; /* s */
    gm_b16_8 l45; /* buf */
    gm_thf5kr0is9u9i l46; /* r */
    gm_tarq9lp8tdk6f ret;
};
struct gm_fr_sys_net_code_ihgq920 {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_sys_net_count_46l7ueq {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_sys_next_handle_r35ek81 {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_sys_proc_jhdp277 {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* argv */
    gm_trqfvmjtt4sij l6; /* env */
    gm_t32dckm4jfarl l10; /* opts */
    gm_tarq9lp8tdk6f ret;
};
struct gm_fr_sys_read_b0gj9fg {
    gm_frame h;
    gm_b16_8 l3; /* buf */
    gm_thf5kr0is9u9i l4; /* r */
    gm_tarq9lp8tdk6f ret;
};
struct gm_fr_sys_ret_vpa2k8i {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_sys_run_2vqjo3u {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_sys_src_5svsr6i {
    gm_frame h;
    gm_tpekhfs32op0f l4; /* e */
    gm_td0ept12e7q8b ret;
};
struct gm_fr_sys_state_cvl1lvl {
    gm_frame h;
    gm_ttse10gjbhca2 ret;
};
struct gm_fr_sys_stdio_nsl9o9u {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_sys_strings_r85ragc {
    gm_frame h;
    gm_tpekhfs32op0f l5; /* e */
    gm_trqfvmjtt4sij l7; /* out */
    gm_t9922qjstblh1 ret;
};
struct gm_fr_sys_unit_nrsqciq {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_view_qalkbv1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_write_view_pebit6h {
    gm_frame h;
    gm_t934k1j7u70ct ret;
};
inline bool gm_eqtje7cegoml(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1);
inline __attribute__((aligned(4))) uint32_t gm_f_cpu_count_0o5882g(void);
__attribute__((aligned(4))) void gm_f_defaults_p077jgf(gm_closure a0);
inline __attribute__((aligned(4))) void gm_f_deinit_gvn47sj(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_16ab7e4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2p7d2kv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_57qa4h8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6l50clp(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hgrt2o4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_khi2i98(void);
__attribute__((aligned(4))) void gm_f_env_exit_prbvpjt(void *gm_out, void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_fault_u31b92l(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3, uint8_t a4);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_9m991tf(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_a1bt60v(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_hjd0ufa(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_hjk709p(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_pnb83dt(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1lff729(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2gkkusq(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_aqcn8lk(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cijad0r(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ducmvdu(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_j9i1sr8(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mslt7ot(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_r8m0qjr(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rg62rr4(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tlii61s(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_trt04qi(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vhnrvmh(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_get_8o0rtd2(void * a0, int64_t a1);
inline __attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_get_f2mbmm8(void * a0, int64_t a1);
inline __attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_get_fqooi5f(void * a0, int64_t a1);
inline __attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_get_gej4j8d(void * a0, int64_t a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_int_pg7p4gc(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_get_v07up09(void * a0, int64_t a1);
inline __attribute__((aligned(4))) void gm_f_hash_map_3d5nsdr(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_3sndrtb(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_abi2bn7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_irh77t1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_ql49boo(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_host_write_r26no87(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_mem_message_e0go901(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_mem_trap_ecogko0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_on_host_thread_kmegc07(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_perform_native_2ppar83(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, uint32_t a3, gm_b16_8 a4, gm_b16_8 a5);
__attribute__((aligned(4))) int64_t gm_f_proc_read_kc2l8gi(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a0, gm_b16_8 a1);
__attribute__((aligned(4))) int64_t gm_f_proc_status_7nq4d17(gm_tlmjd3nndlf3r a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_0c6jnkb(void * a0, int64_t a1, gm_csfq64m1m1rt6m a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_0tusk8g(void * a0, int64_t a1, gm_csfq64m1m1rt6m a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_endcgjj(void * a0, int64_t a1, gm_csfq64m1m1rt6m a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_ha6815g(void * a0, int64_t a1, gm_csfq64m1m1rt6m a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_qkbvor7(void * a0, int64_t a1, gm_csfq64m1m1rt6m a2);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_remove_20jm5n8(void * a0, int64_t a1);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_remove_3j2d0bb(void * a0, int64_t a1);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_remove_7bvuc21(void * a0, int64_t a1);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_remove_82qm8hc(void * a0, int64_t a1);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_remove_jvpr521(void * a0, int64_t a1);
__attribute__((aligned(4))) void gm_f_sleep_gqtie06(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2);
__attribute__((aligned(4))) gm_t2lqu8jsj511d gm_f_slice_parts_1se5hu4(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_socket_on_host_thread_u8c2vg5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
__attribute__((aligned(4))) int64_t gm_f_sys_code_cjtlfo1(gm_toe5fmr3vvf4d a0);
inline __attribute__((aligned(4))) int64_t gm_f_sys_count_50c5lcp(gm_thf5kr0is9u9i a0);
__attribute__((aligned(4))) void gm_f_sys_dest_rtrb14h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_sys_fs_u0grkf6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4);
inline __attribute__((aligned(4))) uint8_t gm_f_sys_has_0sego5l(uint32_t a0, uint32_t a1);
inline __attribute__((aligned(4))) int64_t gm_f_sys_int_32e79ed(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
__attribute__((aligned(4))) int64_t gm_f_sys_kind_sjfdqbt(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void gm_f_sys_net_aff7h5l(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4);
inline __attribute__((aligned(4))) int64_t gm_f_sys_net_code_ihgq920(gm_toe5fmr3vvf4d a0);
inline __attribute__((aligned(4))) int64_t gm_f_sys_net_count_46l7ueq(gm_thf5kr0is9u9i a0);
inline __attribute__((aligned(4))) int64_t gm_f_sys_next_handle_r35ek81(void * a0);
__attribute__((aligned(4))) void gm_f_sys_proc_jhdp277(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_sys_read_b0gj9fg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_sys_ret_vpa2k8i(void * a0, gm_b16_8 a1, int64_t a2);
__attribute__((aligned(4))) void gm_f_sys_run_2vqjo3u(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_sys_src_5svsr6i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_sys_state_cvl1lvl(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_sys_stdio_nsl9o9u(uint32_t a0);
__attribute__((aligned(4))) void gm_f_sys_strings_r85ragc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) int64_t gm_f_sys_unit_nrsqciq(gm_ttln0d0hd70en a0);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_write_view_pebit6h(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
extern uint8_t gm_c3qosqjl81k[8] __attribute__((aligned(8)));
extern uint8_t gm_c6p4chcutca[8] __attribute__((aligned(8)));
extern uint8_t gm_cbcngu2pqs7[8] __attribute__((aligned(8)));
extern uint8_t gm_cndfnv7mbeh[8] __attribute__((aligned(8)));
extern const uint8_t gm_s27n8348bef64[63];
extern const uint8_t gm_sd8o4m7695hpq[38];
extern const uint8_t gm_sqn7kgtu67hrg[42];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcn3jjc71a2bv3[];
extern const char gm_srcq7ohh8h77bn7[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_stfd80s46313k[38];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eqtje7cegoml(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_aligned_q6sor4h F;
    uint64_t gm_l0; /* addr */
    uint64_t gm_l1; /* align */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t2 = gm_l1;
    uint8_t t3 = (t2 <= ((uint64_t)1ULL));
    if (t3) {
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
    uint64_t t4 = gm_l1;
    uint64_t t5 = gm_l1;
    uint64_t r6; bool o7 = __builtin_sub_overflow(t5, ((uint64_t)1ULL), &r6);
    if (o7) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 197, 17);
    uint64_t t8 = r6;
    uint64_t t9 = (uint64_t)(t4 & t8);
    uint8_t t10 = ((t9 == ((uint64_t)0ULL)));
    if (t10) {
        uint64_t t11 = gm_l0;
        uint64_t t12 = gm_l1;
        uint64_t r13; bool o14 = __builtin_sub_overflow(t12, ((uint64_t)1ULL), &r13);
        if (o14) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 197, 50);
        uint64_t t15 = r13;
        uint64_t t16 = (uint64_t)(t11 & t15);
        uint8_t t17 = ((t16 == ((uint64_t)0ULL)));
        F.ret = t17;
        goto gm_fret;
    }
    uint64_t t18 = gm_l0;
    uint64_t t19 = gm_l1;
    if (t19 == 0) gm_trap("division by zero", gm_srcq7ohh8h77bn7, 198, 12);
    uint64_t t20 = (uint64_t)(t18 % t19);
    uint8_t t21 = ((t20 == ((uint64_t)0ULL)));
    F.ret = t21;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_cpu_count_0o5882g(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cpu_count_0o5882g F;
    GM_STACK_CHECK(&F, 12u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint32_t t2 = gm_cpu_count();
    F.ret = t2;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_deinit_gvn47sj(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_gvn47sj F;
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
    gm_b16_8 t21 = gm_f_empty_slice_5nn7ph0();
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_16ab7e4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_16ab7e4 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_57qa4h8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_57qa4h8 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6l50clp(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_6l50clp F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hgrt2o4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hgrt2o4 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_khi2i98(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_khi2i98 F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1lff729(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_1lff729 F;
    GM_STACK_CHECK(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l34));
    gm_node *pn3 = gm_find(cx, 163658498);
    if (!pn3) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 406, 50);
    gm_b0_1 t4;
    t4 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn3->clauses[7])(pn3->env, pn3->hctx, pn3, t2);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2gkkusq(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_2gkkusq F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l44));
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l45));
    gm_node *pn4 = gm_find(cx, 163658498);
    if (!pn4) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 436, 50);
    gm_thf5kr0is9u9i t5;
    t5 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn4->clauses[10])(pn4->env, pn4->hctx, pn4, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_thf5kr0is9u9i, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l46), t5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_aqcn8lk(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_aqcn8lk F;
    GM_STACK_CHECK(&F, 34u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l6));
    uint16_t t3 = GM_LD(uint16_t, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l7));
    gm_node *pn4 = gm_find(cx, 163658498);
    if (!pn4) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 341, 54);
    gm_t62ebbi2i5rkd t5;
    t5 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8, uint16_t))pn4->clauses[0])(pn4->env, pn4->hctx, pn4, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_t62ebbi2i5rkd, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l8), t5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cijad0r(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_cijad0r F;
    GM_STACK_CHECK(&F, 34u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l12));
    uint16_t t3 = GM_LD(uint16_t, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l13));
    gm_node *pn4 = gm_find(cx, 163658498);
    if (!pn4) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 352, 50);
    gm_t62ebbi2i5rkd t5;
    t5 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8, uint16_t))pn4->clauses[1])(pn4->env, pn4->hctx, pn4, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_t62ebbi2i5rkd, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l14), t5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ducmvdu(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_ducmvdu F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l31));
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l30));
    gm_node *pn4 = gm_find(cx, 163658498);
    if (!pn4) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 396, 54);
    gm_thf5kr0is9u9i t5;
    t5 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn4->clauses[5])(pn4->env, pn4->hctx, pn4, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_thf5kr0is9u9i, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l32), t5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_j9i1sr8(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_j9i1sr8 F;
    GM_STACK_CHECK(&F, 52u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l40));
    gm_t6ie8iajlnpt1 t3 = GM_LD(gm_t6ie8iajlnpt1, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l41));
    gm_t6ie8iajlnpt1 t4 = GM_LD(gm_t6ie8iajlnpt1, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l42));
    gm_node *pn5 = gm_find(cx, 163658498);
    if (!pn5) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 429, 50);
    gm_ttln0d0hd70en t6;
    t6 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_t6ie8iajlnpt1, gm_t6ie8iajlnpt1))pn5->clauses[9])(pn5->env, pn5->hctx, pn5, t2, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_ttln0d0hd70en, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l43), t6);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mslt7ot(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_mslt7ot F;
    GM_STACK_CHECK(&F, 20u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l18));
    gm_node *pn3 = gm_find(cx, 163658498);
    if (!pn3) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 361, 50);
    gm_tvcqs9u9iqpct t4;
    t4 = ((gm_tvcqs9u9iqpct (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn3->clauses[2])(pn3->env, pn3->hctx, pn3, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_tvcqs9u9iqpct, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l19), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_r8m0qjr(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_r8m0qjr F;
    GM_STACK_CHECK(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l33));
    gm_node *pn3 = gm_find(cx, 163658498);
    if (!pn3) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 402, 50);
    gm_b0_1 t4;
    t4 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn3->clauses[6])(pn3->env, pn3->hctx, pn3, t2);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rg62rr4(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_rg62rr4 F;
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l22));
    gm_node *pn3 = gm_find(cx, 163658498);
    if (!pn3) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 371, 54);
    gm_t62ebbi2i5rkd t4;
    t4 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn3->clauses[3])(pn3->env, pn3->hctx, pn3, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_t62ebbi2i5rkd, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l23), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tlii61s(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_tlii61s F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l28));
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l27));
    gm_node *pn4 = gm_find(cx, 163658498);
    if (!pn4) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 385, 54);
    gm_thf5kr0is9u9i t5;
    t5 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn4->clauses[4])(pn4->env, pn4->hctx, pn4, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_thf5kr0is9u9i, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l29), t5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_trt04qi(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_trt04qi F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_sys_read_b0gj9fg *)((&F.h)->up))->l3));
    gm_node *pn3 = gm_find(cx, 1018230276);
    if (!pn3) gm_trap("no handler installed for effect `In`", gm_srcn3jjc71a2bv3, 195, 41);
    gm_thf5kr0is9u9i t4;
    t4 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[0])(pn3->env, pn3->hctx, pn3, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_thf5kr0is9u9i, ((uint8_t *)&((struct gm_fr_sys_read_b0gj9fg *)((&F.h)->up))->l4), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vhnrvmh(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_vhnrvmh F;
    GM_STACK_CHECK(&F, 21u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l36));
    gm_t9qkh0rsooc8d t3 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l35));
    gm_node *pn4 = gm_find(cx, 163658498);
    if (!pn4) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 419, 50);
    gm_ttln0d0hd70en t5;
    t5 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_t9qkh0rsooc8d))pn4->clauses[8])(pn4->env, pn4->hctx, pn4, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_ttln0d0hd70en, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l37), t5);
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
inline __attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_get_8o0rtd2(void * a0, int64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_8o0rtd2 F;
    void * gm_l0; /* m */
    int64_t gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 185u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t4 = gm_f_find_9m991tf(t2, t3);
    gm_l3 = t4;
    uint8_t t5 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
    if (t5) {
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
        gm_l2 = t6;
    } else {
        gm_tppo2hmut88dq t7;
        memset(&t7, 0, sizeof t7);
        F.ret = t7;
        goto gm_fret;
    }
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, ((uint8_t *)t8));
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t8) + 8));
    uint64_t t11 = gm_l2;
    if ((uintptr_t)t11 >= (uintptr_t)(t10)) gm_trap_index((uintptr_t)t11, t10, gm_srcbsc23is03ttm, 139, 12);
    gm_tm98ap4112hee t12 = GM_LD(gm_tm98ap4112hee, ((((uint8_t *)t9) + (uintptr_t)t11 * 24) + 16));
    gm_tppo2hmut88dq t13;
    GM_ST(uint8_t, (uint8_t *)&t13 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t13 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t13 + 12, 0);
    GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t13), t12);
    ((uint8_t *)&t13)[8] = 1;
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_get_f2mbmm8(void * a0, int64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_f2mbmm8 F;
    void * gm_l0; /* m */
    int64_t gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 185u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t4 = gm_f_find_a1bt60v(t2, t3);
    gm_l3 = t4;
    uint8_t t5 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
    if (t5) {
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
        gm_l2 = t6;
    } else {
        gm_tppo2hmut88dq t7;
        memset(&t7, 0, sizeof t7);
        F.ret = t7;
        goto gm_fret;
    }
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, ((uint8_t *)t8));
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t8) + 8));
    uint64_t t11 = gm_l2;
    if ((uintptr_t)t11 >= (uintptr_t)(t10)) gm_trap_index((uintptr_t)t11, t10, gm_srcbsc23is03ttm, 139, 12);
    gm_tm98ap4112hee t12 = GM_LD(gm_tm98ap4112hee, ((((uint8_t *)t9) + (uintptr_t)t11 * 24) + 16));
    gm_tppo2hmut88dq t13;
    GM_ST(uint8_t, (uint8_t *)&t13 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t13 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t13 + 12, 0);
    GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t13), t12);
    ((uint8_t *)&t13)[8] = 1;
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_get_fqooi5f(void * a0, int64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_fqooi5f F;
    void * gm_l0; /* m */
    int64_t gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 185u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t4 = gm_f_find_pnb83dt(t2, t3);
    gm_l3 = t4;
    uint8_t t5 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
    if (t5) {
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
        gm_l2 = t6;
    } else {
        gm_tppo2hmut88dq t7;
        memset(&t7, 0, sizeof t7);
        F.ret = t7;
        goto gm_fret;
    }
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, ((uint8_t *)t8));
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t8) + 8));
    uint64_t t11 = gm_l2;
    if ((uintptr_t)t11 >= (uintptr_t)(t10)) gm_trap_index((uintptr_t)t11, t10, gm_srcbsc23is03ttm, 139, 12);
    gm_tm98ap4112hee t12 = GM_LD(gm_tm98ap4112hee, ((((uint8_t *)t9) + (uintptr_t)t11 * 24) + 16));
    gm_tppo2hmut88dq t13;
    GM_ST(uint8_t, (uint8_t *)&t13 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t13 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t13 + 12, 0);
    GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t13), t12);
    ((uint8_t *)&t13)[8] = 1;
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_get_gej4j8d(void * a0, int64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_gej4j8d F;
    void * gm_l0; /* m */
    int64_t gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 185u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t4 = gm_f_find_hjk709p(t2, t3);
    gm_l3 = t4;
    uint8_t t5 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
    if (t5) {
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
        gm_l2 = t6;
    } else {
        gm_tppo2hmut88dq t7;
        memset(&t7, 0, sizeof t7);
        F.ret = t7;
        goto gm_fret;
    }
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, ((uint8_t *)t8));
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t8) + 8));
    uint64_t t11 = gm_l2;
    if ((uintptr_t)t11 >= (uintptr_t)(t10)) gm_trap_index((uintptr_t)t11, t10, gm_srcbsc23is03ttm, 139, 12);
    gm_tm98ap4112hee t12 = GM_LD(gm_tm98ap4112hee, ((((uint8_t *)t9) + (uintptr_t)t11 * 24) + 16));
    gm_tppo2hmut88dq t13;
    GM_ST(uint8_t, (uint8_t *)&t13 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t13 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t13 + 12, 0);
    GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t13), t12);
    ((uint8_t *)&t13)[8] = 1;
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_get_v07up09(void * a0, int64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_v07up09 F;
    void * gm_l0; /* m */
    int64_t gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 185u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t4 = gm_f_find_hjd0ufa(t2, t3);
    gm_l3 = t4;
    uint8_t t5 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
    if (t5) {
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
        gm_l2 = t6;
    } else {
        gm_tppo2hmut88dq t7;
        memset(&t7, 0, sizeof t7);
        F.ret = t7;
        goto gm_fret;
    }
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, ((uint8_t *)t8));
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t8) + 8));
    uint64_t t11 = gm_l2;
    if ((uintptr_t)t11 >= (uintptr_t)(t10)) gm_trap_index((uintptr_t)t11, t10, gm_srcbsc23is03ttm, 139, 12);
    gm_tm98ap4112hee t12 = GM_LD(gm_tm98ap4112hee, ((((uint8_t *)t9) + (uintptr_t)t11 * 24) + 16));
    gm_tppo2hmut88dq t13;
    GM_ST(uint8_t, (uint8_t *)&t13 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t13 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t13 + 12, 0);
    GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t13), t12);
    ((uint8_t *)&t13)[8] = 1;
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_map_3d5nsdr(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_3d5nsdr F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_hgrt2o4();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_3sndrtb(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_3sndrtb F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_57qa4h8();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_abi2bn7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_abi2bn7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_6l50clp();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_irh77t1(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_irh77t1 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_16ab7e4();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_ql49boo(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_ql49boo F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_khi2i98();
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
inline __attribute__((aligned(4))) void gm_f_mem_trap_ecogko0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2) {
    struct gm_fr_mem_trap_ecogko0 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tl874t2atnlh3 t3 = gm_l1;
    gm_b16_8 t4 = gm_f_mem_message_e0go901((void *)NULL, cx, NULL, &(*(gm_tpekhfs32op0f *)((uint8_t *)a2)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)gm_out), (void *)NULL, cx, NULL, t2, t3, t4);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_read_u6r4vns F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint64_t gm_l3; /* align */
    void * gm_l4; /* p */
    GM_STACK_CHECK(&F, 260u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * gm_l5;
    {
        {
            uint64_t t6 = gm_l2;
            uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
            if (!t7) {
                goto gm_lb2;
            }
            uint64_t t8 = gm_l3;
            uint8_t t9 = (t8 <= ((uint64_t)1ULL));
            if (!t9) {
                goto gm_lb2;
            }
            gm_b16_8 t10 = gm_f_empty_slice_ijtp9lo();
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 40, 0);
            ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8), t10);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    uint64_t t11 = gm_l1;
    uint64_t t12 = gm_l3;
    uint8_t t13 = gm_f_aligned_q6sor4h(t11, t12);
    if (t13) {
        {
            {
                void * t14 = gm_l0;
                uint64_t t15 = gm_l1;
                uint64_t t16 = gm_l2;
                void * t17 = gm_f_view_qalkbv1(t14, t15, t16, ((uint8_t)0));
                gm_l5 = t17;
                uint8_t t18 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l5)) != 0);
                if (!t18) {
                    goto gm_lb4;
                }
                void * t19 = GM_LD(void *, ((uint8_t *)&gm_l5));
                gm_l4 = t19;
                void * t20 = gm_l4;
                uint64_t t21 = gm_l2;
                uint64_t t22 = gm_f_us_cuv01fp(t21);
                uint64_t t23 = ((uintptr_t)t22) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t24;
                GM_ST(void *, (uint8_t *)&t24, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(t23));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 40, 0);
                ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8), t24);
                goto gm_fdone;
            }
gm_lb4:;
        }
gm_lb3:;
    }
    {
        void * t25 = gm_l0;
        uint64_t t26 = gm_l1;
        uint64_t t27 = gm_l2;
        uint64_t t28 = gm_l3;
        gm_tpekhfs32op0f t29;
        gm_f_fault_u31b92l((void *)&t29, t25, t26, t27, t28, ((uint8_t)0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 1;
        GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8), t29);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) int64_t gm_f_sys_count_50c5lcp(gm_thf5kr0is9u9i a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_count_50c5lcp F;
    gm_thf5kr0is9u9i gm_l0; /* r */
    gm_toe5fmr3vvf4d gm_l1; /* e */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 114u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t gm_l3;
    {
        {
            uint8_t t5 = (uint8_t)!((((uint8_t *)&gm_l0))[0] != 0);
            if (t5) {
                goto gm_lb2;
            }
            gm_toe5fmr3vvf4d t6 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l0) + 8));
            gm_l1 = t6;
            gm_toe5fmr3vvf4d t7 = gm_l1;
            int64_t t8 = gm_f_sys_code_cjtlfo1(t7);
            gm_l3 = t8;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l0))[0] != 0);
            if (!t9) {
                goto gm_lb3;
            }
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            gm_l2 = t10;
            uint64_t t11 = gm_l2;
            int64_t t12 = (int64_t)t11;
            gm_l3 = t12;
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 78, 45);
    }
gm_lb1:;
    int64_t t13 = gm_l3;
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_sys_has_0sego5l(uint32_t a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_has_0sego5l F;
    uint32_t gm_l0; /* flags */
    uint32_t gm_l1; /* bit */
    GM_STACK_CHECK_SHALLOW(&F, 23u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t2 = gm_l0;
    uint32_t t3 = gm_l1;
    uint32_t t4 = (uint32_t)(t2 & t3);
    uint8_t t5 = (!(t4 == ((uint32_t)0ULL)));
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) int64_t gm_f_sys_int_32e79ed(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_int_32e79ed F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* b */
    __int128 gm_l3; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 180u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = a2;
    uint8_t gm_l4;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4 = gm_l2;
    unsigned __int128 t5 = gm_f_get_int_pg7p4gc(t2, GM_CONV(gm_csguankko4lmhd, t3), t4);
    __int128 t6;
    memset(&t6, 0, sizeof t6); memcpy(&t6, ((uint8_t *)&t5), sizeof t6 < 16 ? sizeof t6 : 16);
    gm_l3 = t6;
    __int128 t7 = gm_l3;
    uint8_t t8 = (t7 < ((__int128)(((unsigned __int128)18446744073709551615ULL << 64) | 9223372036854775808ULL)));
    if (t8) {
        gm_l4 = ((uint8_t)1);
    } else {
        __int128 t9 = gm_l3;
        uint8_t t10 = (t9 > ((__int128)(((unsigned __int128)0ULL << 64) | 9223372036854775807ULL)));
        gm_l4 = t10;
    }
    uint8_t t11 = gm_l4;
    if (t11) {
        F.ret = ((int64_t)-1LL);
        goto gm_fret;
    }
    __int128 t12 = gm_l3;
    int64_t t13 = (int64_t)t12;
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) int64_t gm_f_sys_net_code_ihgq920(gm_toe5fmr3vvf4d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_net_code_ihgq920 F;
    gm_toe5fmr3vvf4d gm_l0; /* e */
    GM_STACK_CHECK_SHALLOW(&F, 82u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t gm_l1;
    gm_toe5fmr3vvf4d t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)&t2) + 0));
    uint8_t t4 = (uint8_t)((t3 == ((uint8_t)3ULL)));
    gm_toe5fmr3vvf4d t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)3ULL));
    if (t4) {
        int64_t t6 = GM_LD(int64_t, ((uint8_t *)gm_cbcngu2pqs7));
        gm_l1 = t6;
    } else {
        gm_toe5fmr3vvf4d t7 = gm_l0;
        int64_t t8 = gm_f_sys_code_cjtlfo1(t7);
        gm_l1 = t8;
    }
    int64_t t9 = gm_l1;
    F.ret = t9;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) int64_t gm_f_sys_net_count_46l7ueq(gm_thf5kr0is9u9i a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_net_count_46l7ueq F;
    gm_thf5kr0is9u9i gm_l0; /* r */
    gm_toe5fmr3vvf4d gm_l1; /* e */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 114u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t gm_l3;
    {
        {
            uint8_t t5 = (uint8_t)!((((uint8_t *)&gm_l0))[0] != 0);
            if (t5) {
                goto gm_lb2;
            }
            gm_toe5fmr3vvf4d t6 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l0) + 8));
            gm_l1 = t6;
            gm_toe5fmr3vvf4d t7 = gm_l1;
            int64_t t8 = gm_f_sys_net_code_ihgq920(t7);
            gm_l3 = t8;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l0))[0] != 0);
            if (!t9) {
                goto gm_lb3;
            }
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            gm_l2 = t10;
            uint64_t t11 = gm_l2;
            int64_t t12 = (int64_t)t11;
            gm_l3 = t12;
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 84, 49);
    }
gm_lb1:;
    int64_t t13 = gm_l3;
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) int64_t gm_f_sys_next_handle_r35ek81(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_next_handle_r35ek81 F;
    void * gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    int64_t t4 = GM_LD(int64_t, (((uint8_t *)t3) + 280));
    int64_t r5; bool o6 = __builtin_add_overflow(t4, ((int64_t)1LL), &r5);
    if (o6) gm_trap("integer overflow in `+`", gm_srcn3jjc71a2bv3, 50, 5);
    int64_t t7 = r5;
    GM_ST(int64_t, (((uint8_t *)t2) + 280), t7);
    void * t8 = gm_l0;
    int64_t t9 = GM_LD(int64_t, (((uint8_t *)t8) + 280));
    int64_t r10; bool o11 = __builtin_add_overflow(t9, ((int64_t)2LL), &r10);
    if (o11) gm_trap("integer overflow in `+`", gm_srcn3jjc71a2bv3, 51, 12);
    int64_t t12 = r10;
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_sys_ret_vpa2k8i(void * a0, gm_b16_8 a1, int64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_ret_vpa2k8i F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* dst */
    int64_t gm_l2; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    gm_b16_8 t3 = gm_l1;
    int64_t t4 = gm_l2;
    uint64_t t5;
    memset(&t5, 0, sizeof t5); memcpy(&t5, ((uint8_t *)&t4), sizeof t5 < 8 ? sizeof t5 : 8);
    unsigned __int128 t6 = (unsigned __int128)t5;
    gm_f_put_uint_cddin99(t2, t3, t6);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_sys_stdio_nsl9o9u(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_stdio_nsl9o9u F;
    uint32_t gm_l0; /* bits */
    GM_STACK_CHECK_SHALLOW(&F, 21u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9qkh0rsooc8d gm_l1;
    {
        {
            uint32_t t6 = gm_l0;
            uint8_t t7 = ((t6 == ((uint32_t)1ULL)));
            if (!t7) {
                goto gm_lb2;
            }
            gm_t9qkh0rsooc8d t8;
            GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)1ULL));
            gm_l1 = t8;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint32_t t9 = gm_l0;
            uint8_t t10 = ((t9 == ((uint32_t)2ULL)));
            if (!t10) {
                goto gm_lb3;
            }
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)2ULL));
            gm_l1 = t11;
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            gm_l1 = t12;
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 106, 48);
    }
gm_lb1:;
    gm_t9qkh0rsooc8d t13 = gm_l1;
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) int64_t gm_f_sys_unit_nrsqciq(gm_ttln0d0hd70en a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_unit_nrsqciq F;
    gm_ttln0d0hd70en gm_l0; /* r */
    gm_toe5fmr3vvf4d gm_l1; /* e */
    GM_STACK_CHECK_SHALLOW(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t gm_l2;
    {
        {
            uint8_t t5 = (uint8_t)!((((uint8_t *)&gm_l0))[0] != 0);
            if (t5) {
                goto gm_lb2;
            }
            gm_toe5fmr3vvf4d t6 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l0) + 4));
            gm_l1 = t6;
            gm_toe5fmr3vvf4d t7 = gm_l1;
            int64_t t8 = gm_f_sys_code_cjtlfo1(t7);
            gm_l2 = t8;
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l2 = ((int64_t)0LL);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 90, 41);
    }
gm_lb1:;
    int64_t t9 = gm_l2;
    F.ret = t9;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_view_qalkbv1 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint8_t gm_l3; /* write */
    uint64_t gm_l4; /* p */
    void * gm_l5; /* pg */
    void * gm_l6; /* ss */
    uint64_t gm_l7; /* rel */
    uint64_t gm_l8; /* i */
    void * gm_l9; /* s */
    uint64_t gm_l10; /* off */
    GM_STACK_CHECK_SHALLOW(&F, 522u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l11;
    uint8_t gm_l12;
    uint8_t gm_l13;
    uint8_t gm_l14;
    uint64_t t2 = gm_l1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 40));
    uint64_t r5; bool o6 = __builtin_sub_overflow(t2, t4, &r5);
    uint64_t t7 = r5;
    uint64_t t8 = GM_LD(uint64_t, ((uint8_t *)gm_c6p4chcutca));
    unsigned __int128 n9 = (unsigned __int128)t8;
    if (n9 >= 64) gm_trap_shift((uint64_t)n9, "u64", gm_srcq7ohh8h77bn7, 206, 10);
    uint64_t t10 = (uint64_t)(t7 >> (int)n9);
    gm_l4 = t10;
    uint64_t t11 = gm_l4;
    void * t12 = gm_l0;
    uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t12) + 8));
    uint64_t t14 = (uint64_t)t13;
    uint8_t t15 = (t11 >= t14);
    if (t15) {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    void * t16 = gm_l0;
    void * t17 = GM_LD(void *, ((uint8_t *)t16));
    uint64_t t18 = gm_l4;
    uint64_t t19 = gm_f_us_cuv01fp(t18);
    void * t20 = (void *)(((uint8_t *)t17) + (uintptr_t)t19 * 32);
    gm_l5 = t20;
    void * t21 = gm_l5;
    uint8_t t22 = (uint8_t)(GM_LD(void *, ((uint8_t *)t21)) != 0);
    if (t22) {
        void * t23 = GM_LD(void *, ((uint8_t *)t21));
        gm_l6 = t23;
    } else {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    uint64_t t24 = gm_l1;
    void * t25 = gm_l5;
    uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)t25) + 8));
    uint64_t r27; bool o28 = __builtin_sub_overflow(t24, t26, &r27);
    if (o28) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 210, 12);
    uint64_t t29 = r27;
    gm_l7 = t29;
    uint64_t t30 = gm_l7;
    void * t31 = gm_l5;
    uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)t31) + 16));
    unsigned __int128 n33 = (unsigned __int128)t32;
    if (n33 >= 64) gm_trap_shift((uint64_t)n33, "u64", gm_srcq7ohh8h77bn7, 211, 10);
    uint64_t t34 = (uint64_t)(t30 >> (int)n33);
    gm_l8 = t34;
    void * t35 = gm_l6;
    uint64_t t36 = gm_l8;
    uint64_t t37 = gm_f_us_cuv01fp(t36);
    void * t38 = (void *)(((uint8_t *)t35) + (uintptr_t)t37 * 24);
    gm_l9 = t38;
    uint64_t t39 = gm_l7;
    uint64_t t40 = gm_l8;
    void * t41 = gm_l5;
    uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)t41) + 16));
    unsigned __int128 n43 = (unsigned __int128)t42;
    if (n43 >= 64) gm_trap_shift((uint64_t)n43, "u64", gm_srcq7ohh8h77bn7, 213, 19);
    uint64_t r44 = (uint64_t)((uint64_t)t40 << (int)n43);
    bool o45 = (uint64_t)(r44 >> (int)n43) != t40;
    if (o45) gm_trap("integer overflow in `<<`", gm_srcq7ohh8h77bn7, 213, 19);
    uint64_t t46 = r44;
    uint64_t r47; bool o48 = __builtin_sub_overflow(t39, t46, &r47);
    if (o48) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 213, 12);
    uint64_t t49 = r47;
    gm_l10 = t49;
    void * t50 = gm_l9;
    uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)t50) + 17));
    uint8_t t52 = (uint8_t)!t51;
    if (t52) {
        gm_l13 = ((uint8_t)1);
    } else {
        uint64_t t53 = gm_l2;
        void * t54 = gm_l9;
        uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)t54) + 8));
        uint8_t t56 = (t53 > t55);
        gm_l13 = t56;
    }
    uint8_t t57 = gm_l13;
    if (t57) {
        gm_l12 = ((uint8_t)1);
    } else {
        uint64_t t58 = gm_l10;
        void * t59 = gm_l9;
        uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)t59) + 8));
        uint64_t t61 = gm_l2;
        uint64_t r62; bool o63 = __builtin_sub_overflow(t60, t61, &r62);
        if (o63) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 214, 44);
        uint64_t t64 = r62;
        uint8_t t65 = (t58 > t64);
        gm_l12 = t65;
    }
    uint8_t t66 = gm_l12;
    if (t66) {
        gm_l11 = ((uint8_t)1);
    } else {
        uint8_t t67 = gm_l3;
        if (t67) {
            void * t68 = gm_l9;
            uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)t68) + 18));
            uint8_t t70 = (uint8_t)!t69;
            gm_l14 = t70;
        } else {
            gm_l14 = ((uint8_t)0);
        }
        uint8_t t71 = gm_l14;
        gm_l11 = t71;
    }
    uint8_t t72 = gm_l11;
    if (t72) {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    void * t73 = gm_l9;
    void * t74 = GM_LD(void *, ((uint8_t *)t73));
    uint64_t t75 = gm_l10;
    uint64_t t76 = gm_f_us_cuv01fp(t75);
    void * t77 = (void *)(((uint8_t *)t74) + (uintptr_t)t76 * 1);
    void * t78 = t77;
    F.ret = t78;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_write_view_pebit6h(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_view_pebit6h F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint64_t gm_l3; /* align */
    void * gm_l4; /* p */
    GM_STACK_CHECK(&F, 260u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * gm_l5;
    {
        {
            uint64_t t6 = gm_l2;
            uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
            if (!t7) {
                goto gm_lb2;
            }
            uint64_t t8 = gm_l3;
            uint8_t t9 = (t8 <= ((uint64_t)1ULL));
            if (!t9) {
                goto gm_lb2;
            }
            gm_b16_8 t10 = gm_f_empty_slice_ijtp9lo();
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 40, 0);
            ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8), t10);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    uint64_t t11 = gm_l1;
    uint64_t t12 = gm_l3;
    uint8_t t13 = gm_f_aligned_q6sor4h(t11, t12);
    if (t13) {
        {
            {
                void * t14 = gm_l0;
                uint64_t t15 = gm_l1;
                uint64_t t16 = gm_l2;
                void * t17 = gm_f_view_qalkbv1(t14, t15, t16, ((uint8_t)1));
                gm_l5 = t17;
                uint8_t t18 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l5)) != 0);
                if (!t18) {
                    goto gm_lb4;
                }
                void * t19 = GM_LD(void *, ((uint8_t *)&gm_l5));
                gm_l4 = t19;
                void * t20 = gm_l4;
                uint64_t t21 = gm_l2;
                uint64_t t22 = gm_f_us_cuv01fp(t21);
                uint64_t t23 = ((uintptr_t)t22) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t24;
                GM_ST(void *, (uint8_t *)&t24, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(t23));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 40, 0);
                ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8), t24);
                goto gm_fdone;
            }
gm_lb4:;
        }
gm_lb3:;
    }
    {
        void * t25 = gm_l0;
        uint64_t t26 = gm_l1;
        uint64_t t27 = gm_l2;
        uint64_t t28 = gm_l3;
        gm_tpekhfs32op0f t29;
        gm_f_fault_u31b92l((void *)&t29, t25, t26, t27, t28, ((uint8_t)1));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 1;
        GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8), t29);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1lff729(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2gkkusq(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_aqcn8lk(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cijad0r(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ducmvdu(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_j9i1sr8(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mslt7ot(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_r8m0qjr(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rg62rr4(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tlii61s(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_trt04qi(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vhnrvmh(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) int64_t gm_f_proc_read_kc2l8gi(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a0, gm_b16_8 a1) {
    struct gm_fr_proc_read_kc2l8gi F;
    gm_tm98ap4112hee gm_l0; /* c */
    gm_b16_8 gm_l1; /* buf */
    gm_toe5fmr3vvf4d gm_l2; /* err */
    gm_thdrvp9q0u7b5 gm_l3; /* got */
    gm_te0mgnjdphs16 gm_l4; /* chunk */
    GM_STACK_CHECK(&F, 270u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tlbmih0s72in3 gm_l5;
    int64_t gm_l6;
    gm_tm98ap4112hee t2 = gm_l0;
    gm_b16_8 t3 = gm_l1;
    gm_node *pn4 = gm_find(cx, 2119230);
    if (!pn4) gm_trap("no handler installed for effect `Proc`", gm_srcn3jjc71a2bv3, 458, 12);
    gm_tlbmih0s72in3 t5;
    ((void (*)(void *, void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn4->clauses[3])((void *)&t5, pn4->env, pn4->hctx, pn4, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t5;
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (t6) {
        memmove(((uint8_t *)&gm_l3), (((uint8_t *)&gm_l5) + 8), 24);
    } else {
        gm_toe5fmr3vvf4d t7 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l5) + 8));
        gm_l2 = t7;
        gm_toe5fmr3vvf4d t8 = gm_l2;
        int64_t t9 = gm_f_sys_code_cjtlfo1(t8);
        F.ret = t9;
        goto gm_fret;
    }
    uint8_t t10 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
    if (t10) {
        gm_te0mgnjdphs16 t11 = GM_LD(gm_te0mgnjdphs16, ((uint8_t *)&gm_l3));
        gm_l4 = t11;
    } else {
        F.ret = ((int64_t)0LL);
        goto gm_fret;
    }
    uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
    int64_t t13 = (int64_t)t12;
    gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l4));
    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
    uint8_t t16 = (uint8_t)((t15 == ((uint8_t)1ULL)));
    gm_t9qkh0rsooc8d t17;
    GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)1ULL));
    if (t16) {
        unsigned __int128 n18 = (unsigned __int128)((uint64_t)32ULL);
        if (n18 >= 64) gm_trap_shift((uint64_t)n18, "i64", gm_srcn3jjc71a2bv3, 460, 63);
        int64_t r19 = (int64_t)((uint64_t)((int64_t)1LL) << (int)n18);
        bool o20 = (int64_t)(r19 >> (int)n18) != ((int64_t)1LL);
        if (o20) gm_trap("integer overflow in `<<`", gm_srcn3jjc71a2bv3, 460, 63);
        int64_t t21 = r19;
        gm_l6 = t21;
    } else {
        gm_l6 = ((int64_t)0LL);
    }
    int64_t t22 = gm_l6;
    int64_t r23; bool o24 = __builtin_add_overflow(t13, t22, &r23);
    if (o24) gm_trap("integer overflow in `+`", gm_srcn3jjc71a2bv3, 460, 12);
    int64_t t25 = r23;
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
inline __attribute__((aligned(4))) int64_t gm_f_proc_status_7nq4d17(gm_tlmjd3nndlf3r a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_proc_status_7nq4d17 F;
    gm_tlmjd3nndlf3r gm_l0; /* r */
    gm_toe5fmr3vvf4d gm_l1; /* err */
    gm_tsoiri44d0i6q gm_l2; /* status */
    uint32_t gm_l3; /* code */
    int32_t gm_l4; /* sig */
    GM_STACK_CHECK_SHALLOW(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t gm_l5;
    uint8_t t5 = (uint8_t)!((((uint8_t *)&gm_l0))[0] != 0);
    if (t5) {
        gm_tsoiri44d0i6q t6 = GM_LD(gm_tsoiri44d0i6q, (((uint8_t *)&gm_l0) + 4));
        gm_l2 = t6;
    } else {
        gm_toe5fmr3vvf4d t7 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l0) + 4));
        gm_l1 = t7;
        gm_toe5fmr3vvf4d t8 = gm_l1;
        int64_t t9 = gm_f_sys_code_cjtlfo1(t8);
        F.ret = t9;
        goto gm_fret;
    }
    {
        {
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)0ULL));
            if (!t11) {
                goto gm_lb2;
            }
            uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
            gm_l3 = t12;
            uint32_t t13 = gm_l3;
            int64_t t14 = (int64_t)t13;
            gm_l5 = t14;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t16 = (uint8_t)(t15 == ((uint8_t)1ULL));
            if (!t16) {
                goto gm_lb3;
            }
            int32_t t17 = GM_LD(int32_t, (((uint8_t *)&gm_l2) + 4));
            gm_l4 = t17;
            int32_t t18 = gm_l4;
            int64_t t19 = (int64_t)t18;
            unsigned __int128 n20 = (unsigned __int128)((uint64_t)32ULL);
            if (n20 >= 64) gm_trap_shift((uint64_t)n20, "i64", gm_srcn3jjc71a2bv3, 468, 39);
            int64_t r21 = (int64_t)((uint64_t)((int64_t)1LL) << (int)n20);
            bool o22 = (int64_t)(r21 >> (int)n20) != ((int64_t)1LL);
            if (o22) gm_trap("integer overflow in `<<`", gm_srcn3jjc71a2bv3, 468, 39);
            int64_t t23 = r21;
            int64_t r24; bool o25 = __builtin_add_overflow(t19, t23, &r24);
            if (o25) gm_trap("integer overflow in `+`", gm_srcn3jjc71a2bv3, 468, 27);
            int64_t t26 = r24;
            gm_l5 = t26;
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 466, 12);
    }
gm_lb1:;
    int64_t t27 = gm_l5;
    F.ret = t27;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_socket_on_host_thread_u8c2vg5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2) {
    struct gm_fr_socket_on_host_thread_u8c2vg5 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tnp3hpcm9uhoi gm_l2; /* op */
    gm_t62ebbi2i5rkd gm_l4; /* result */
    gm_tm98ap4112hee gm_l5; /* s */
    gm_toe5fmr3vvf4d gm_l6; /* e */
    GM_STACK_CHECK(&F, 355u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&gm_l2, a2, sizeof gm_l2);
    gm_t0jbubompi7qd gm_l7;
    uint64_t gm_l8;
    {
        void * t4 = gm_l0;
        gm_tl874t2atnlh3 t5 = gm_l1;
        gm_tnp3hpcm9uhoi t6 = GM_LD(gm_tnp3hpcm9uhoi, ((uint8_t *)&gm_l2));
        gm_f_on_host_thread_kmegc07((void *)&gm_l7, (void *)NULL, cx, NULL, t4, t5, &t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (t7) {
    } else {
        gm_t76123gfet73g t8 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l7) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_toqti1ubjnfd1 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_toqti1ubjnfd1 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_toqti1ubjnfd1 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_toqti1ubjnfd1 *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_toqti1ubjnfd1 *)gm_out) + 8), t8);
        goto gm_fdone;
    }
    {
        memmove(((uint8_t *)&F.l3), (((uint8_t *)&gm_l7) + 8), 40);
    }
    gm_toe5fmr3vvf4d t9;
    GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)12ULL));
    gm_t62ebbi2i5rkd t10;
    GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
    ((uint8_t *)&t10)[0] = 1;
    GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t10 + 8), t9);
    gm_l4 = t10;
    {
        {
            uint8_t t11 = (uint8_t)(((((uint8_t *)&F.l3) + 16))[8] != 0);
            if (!t11) {
                goto gm_lb2;
            }
            gm_tm98ap4112hee t12 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&F.l3) + 16));
            gm_l5 = t12;
            gm_tm98ap4112hee t13 = gm_l5;
            gm_t62ebbi2i5rkd t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            ((uint8_t *)&t14)[0] = 0;
            GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t14 + 8), t13);
            gm_l4 = t14;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    uint8_t t15 = (uint8_t)!((((uint8_t *)&F.l3))[0] != 0);
    if (t15) {
        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
        gm_l8 = t16;
    } else {
        gm_toe5fmr3vvf4d t17 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&F.l3) + 8));
        gm_l6 = t17;
        gm_toe5fmr3vvf4d t18 = gm_l6;
        gm_t62ebbi2i5rkd t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
        ((uint8_t *)&t19)[0] = 1;
        GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t19 + 8), t18);
        gm_l4 = t19;
        gm_t62ebbi2i5rkd t20 = gm_l4;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_toqti1ubjnfd1 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_toqti1ubjnfd1 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_toqti1ubjnfd1 *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_toqti1ubjnfd1 *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_toqti1ubjnfd1 *)gm_out))[0] = 0;
        GM_ST(gm_t62ebbi2i5rkd, ((uint8_t *)&(*(gm_toqti1ubjnfd1 *)gm_out) + 8), t20);
        goto gm_fdone;
    }
    uint64_t t21 = gm_l8;
    {
        gm_t62ebbi2i5rkd t22 = gm_l4;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_toqti1ubjnfd1 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_toqti1ubjnfd1 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_toqti1ubjnfd1 *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_toqti1ubjnfd1 *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_toqti1ubjnfd1 *)gm_out))[0] = 0;
        GM_ST(gm_t62ebbi2i5rkd, ((uint8_t *)&(*(gm_toqti1ubjnfd1 *)gm_out) + 8), t22);
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
inline __attribute__((aligned(4))) int64_t gm_f_sys_code_cjtlfo1(gm_toe5fmr3vvf4d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_code_cjtlfo1 F;
    gm_toe5fmr3vvf4d gm_l0; /* e */
    int32_t gm_l1; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t gm_l2;
    {
        {
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)0ULL));
            if (!t18) {
                goto gm_lb2;
            }
            gm_l2 = ((int64_t)-1LL);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)1ULL));
            if (!t20) {
                goto gm_lb3;
            }
            gm_l2 = ((int64_t)-2LL);
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)2ULL));
            if (!t22) {
                goto gm_lb4;
            }
            gm_l2 = ((int64_t)-3LL);
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)3ULL));
            if (!t24) {
                goto gm_lb5;
            }
            gm_l2 = ((int64_t)-4LL);
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)4ULL));
            if (!t26) {
                goto gm_lb6;
            }
            gm_l2 = ((int64_t)-5LL);
            goto gm_lb1;
        }
gm_lb6:;
        {
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t28 = (uint8_t)(t27 == ((uint8_t)5ULL));
            if (!t28) {
                goto gm_lb7;
            }
            gm_l2 = ((int64_t)-6LL);
            goto gm_lb1;
        }
gm_lb7:;
        {
            uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t30 = (uint8_t)(t29 == ((uint8_t)6ULL));
            if (!t30) {
                goto gm_lb8;
            }
            gm_l2 = ((int64_t)-7LL);
            goto gm_lb1;
        }
gm_lb8:;
        {
            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t32 = (uint8_t)(t31 == ((uint8_t)7ULL));
            if (!t32) {
                goto gm_lb9;
            }
            gm_l2 = ((int64_t)-8LL);
            goto gm_lb1;
        }
gm_lb9:;
        {
            uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t34 = (uint8_t)(t33 == ((uint8_t)8ULL));
            if (!t34) {
                goto gm_lb10;
            }
            gm_l2 = ((int64_t)-9LL);
            goto gm_lb1;
        }
gm_lb10:;
        {
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t36 = (uint8_t)(t35 == ((uint8_t)9ULL));
            if (!t36) {
                goto gm_lb11;
            }
            gm_l2 = ((int64_t)-10LL);
            goto gm_lb1;
        }
gm_lb11:;
        {
            uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t38 = (uint8_t)(t37 == ((uint8_t)10ULL));
            if (!t38) {
                goto gm_lb12;
            }
            gm_l2 = ((int64_t)-11LL);
            goto gm_lb1;
        }
gm_lb12:;
        {
            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t40 = (uint8_t)(t39 == ((uint8_t)11ULL));
            if (!t40) {
                goto gm_lb13;
            }
            gm_l2 = ((int64_t)-12LL);
            goto gm_lb1;
        }
gm_lb13:;
        {
            uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t42 = (uint8_t)(t41 == ((uint8_t)12ULL));
            if (!t42) {
                goto gm_lb14;
            }
            gm_l2 = ((int64_t)-13LL);
            goto gm_lb1;
        }
gm_lb14:;
        {
            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t44 = (uint8_t)(t43 == ((uint8_t)13ULL));
            if (!t44) {
                goto gm_lb15;
            }
            int32_t t45 = GM_LD(int32_t, (((uint8_t *)&gm_l0) + 4));
            gm_l1 = t45;
            int32_t t46 = gm_l1;
            int64_t t47 = (int64_t)t46;
            int64_t r48; bool o49 = __builtin_sub_overflow(((int64_t)-1000LL), t47, &r48);
            if (o49) gm_trap("integer overflow in `-`", gm_srcn3jjc71a2bv3, 70, 18);
            int64_t t50 = r48;
            gm_l2 = t50;
            goto gm_lb1;
        }
gm_lb15:;
        gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 56, 38);
    }
gm_lb1:;
    int64_t t51 = gm_l2;
    F.ret = t51;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) int64_t gm_f_sys_count_50c5lcp(gm_thf5kr0is9u9i a0);
inline __attribute__((aligned(4))) void gm_f_sys_dest_rtrb14h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_sys_dest_rtrb14h F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* b */
    gm_t2lqu8jsj511d gm_l3; /* sp */
    gm_b16_8 gm_l5; /* v */
    GM_STACK_CHECK(&F, 265u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t934k1j7u70ct gm_l6;
    void * t2 = gm_l0;
    gm_b16_8 t3 = gm_l2;
    gm_t2lqu8jsj511d t4 = gm_f_slice_parts_1se5hu4(t2, t3);
    gm_l3 = t4;
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 184);
        uint64_t t7 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        gm_f_write_view_pebit6h((void *)&gm_l6, t6, t7, t8, ((uint64_t)1ULL));
    }
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (t9) {
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
        gm_l5 = t10;
    } else {
        {
            memmove(((uint8_t *)&F.l4), (((uint8_t *)&gm_l6) + 8), 40);
        }
        {
            void * t11 = gm_l0;
            gm_tl874t2atnlh3 t12 = gm_l1;
            gm_t76123gfet73g t13;
            gm_f_mem_trap_ecogko0((void *)&t13, (void *)NULL, cx, NULL, t11, t12, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l4)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 8), t13);
            goto gm_fdone;
        }
    }
    {
        gm_b16_8 t14 = gm_l5;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 8), t14);
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
__attribute__((aligned(4))) void gm_f_sys_fs_u0grkf6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_sys_fs_u0grkf6 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t9qkh0rsooc8d gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    gm_b16_8 gm_l4; /* vals */
    int64_t gm_l5; /* h */
    gm_b16_8 gm_l6; /* path */
    uint32_t gm_l7; /* flags */
    gm_t4q2b5ibsrdik gm_l8; /* opts */
    gm_toe5fmr3vvf4d gm_l9; /* err */
    gm_tm98ap4112hee gm_l10; /* f */
    int64_t gm_l11; /* handle */
    gm_tm98ap4112hee gm_l12; /* f */
    gm_b16_8 gm_l13; /* buf */
    gm_tm98ap4112hee gm_l14; /* f */
    gm_b16_8 gm_l15; /* data */
    gm_tm98ap4112hee gm_l16; /* f */
    int64_t gm_l17; /* off */
    int64_t gm_l18; /* whence */
    gm_tm98ap4112hee gm_l19; /* f */
    gm_t9qkh0rsooc8d gm_l20; /* from */
    gm_toe5fmr3vvf4d gm_l21; /* err */
    uint64_t gm_l22; /* pos */
    gm_b16_8 gm_l23; /* path */
    gm_toe5fmr3vvf4d gm_l24; /* err */
    gm_t5j14m8ru132p gm_l25; /* info */
    gm_t2lqu8jsj511d gm_l26; /* sp */
    uint64_t gm_l27; /* count */
    gm_b16_8 gm_l29; /* view */
    gm_t767tucha9lsh gm_l30; /* fields */
    uint64_t gm_l31; /* <tmp> */
    uint64_t gm_l32; /* <tmp> */
    uint64_t gm_l33; /* i */
    gm_b16_8 gm_l34; /* from */
    gm_b16_8 gm_l35; /* to */
    gm_b16_8 gm_l36; /* path */
    gm_toe5fmr3vvf4d gm_l37; /* err */
    gm_tm98ap4112hee gm_l38; /* d */
    int64_t gm_l39; /* handle */
    gm_tm98ap4112hee gm_l40; /* d */
    gm_b16_8 gm_l41; /* buf */
    gm_toe5fmr3vvf4d gm_l42; /* err */
    gm_t11t9d8s5diq9 gm_l43; /* next */
    gm_t617vr72bd7b3 gm_l44; /* entry */
    gm_tm98ap4112hee gm_l45; /* d */
    uint64_t gm_l46; /* size */
    gm_tm98ap4112hee gm_l47; /* f */
    gm_tm98ap4112hee gm_l48; /* f */
    gm_b16_8 gm_l49; /* buf */
    gm_b16_8 gm_l50; /* path */
    gm_b16_8 gm_l51; /* buf */
    gm_b16_8 gm_l52; /* path */
    gm_b16_8 gm_l53; /* buf */
    gm_b16_8 gm_l54; /* target */
    gm_b16_8 gm_l55; /* path */
    gm_b16_8 gm_l56; /* path */
    gm_b16_8 gm_l57; /* path */
    GM_STACK_CHECK(&F, 7025u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_td0ept12e7q8b gm_l58;
    gm_t62ebbi2i5rkd gm_l59;
    gm_tftb1odc739t5 gm_l60;
    gm_tppo2hmut88dq gm_l61;
    gm_td0ept12e7q8b gm_l62;
    gm_tppo2hmut88dq gm_l63;
    gm_t0jbubompi7qd gm_l64;
    gm_t7eae7gj5k2n2 gm_l65;
    gm_td0ept12e7q8b gm_l66;
    gm_tppo2hmut88dq gm_l67;
    gm_tppo2hmut88dq gm_l68;
    gm_thf5kr0is9u9i gm_l69;
    gm_td0ept12e7q8b gm_l70;
    gm_tih6h4avv5nrb gm_l71;
    gm_tih6h4avv5nrb gm_l72;
    gm_t934k1j7u70ct gm_l73;
    gm_td0ept12e7q8b gm_l74;
    gm_td0ept12e7q8b gm_l75;
    gm_td0ept12e7q8b gm_l76;
    gm_td0ept12e7q8b gm_l77;
    gm_td0ept12e7q8b gm_l78;
    gm_td0ept12e7q8b gm_l79;
    gm_t62ebbi2i5rkd gm_l80;
    gm_tftb1odc739t5 gm_l81;
    gm_tppo2hmut88dq gm_l82;
    gm_td0ept12e7q8b gm_l83;
    gm_tiuq7k5hm0kak gm_l84;
    gm_tppo2hmut88dq gm_l85;
    gm_tppo2hmut88dq gm_l86;
    gm_tppo2hmut88dq gm_l87;
    gm_td0ept12e7q8b gm_l88;
    gm_td0ept12e7q8b gm_l89;
    gm_td0ept12e7q8b gm_l90;
    gm_td0ept12e7q8b gm_l91;
    gm_td0ept12e7q8b gm_l92;
    gm_td0ept12e7q8b gm_l93;
    gm_td0ept12e7q8b gm_l94;
    gm_td0ept12e7q8b gm_l95;
    gm_td0ept12e7q8b gm_l96;
    {
        gm_l5 = ((int64_t)-1LL);
        {
            {
                uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint8_t t43 = (t42 > ((uint64_t)0ULL));
                if (!t43) {
                    goto gm_lb2;
                }
                gm_t9qkh0rsooc8d t44 = gm_l2;
                gm_t9qkh0rsooc8d t45;
                GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)5ULL));
                gm_t9qkh0rsooc8d t46;
                GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)10ULL));
                gm_t9qkh0rsooc8d t47;
                GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)18ULL));
                gm_t9qkh0rsooc8d t48;
                GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)11ULL));
                gm_t9qkh0rsooc8d t49;
                GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)12ULL));
                gm_t9qkh0rsooc8d t50;
                GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)13ULL));
                gm_t9qkh0rsooc8d t51;
                GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)14ULL));
                gm_t9qkh0rsooc8d t52;
                GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)15ULL));
                gm_t9qkh0rsooc8d t53;
                GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)21ULL));
                gm_t9qkh0rsooc8d t54;
                GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)22ULL));
                gm_t9qkh0rsooc8d t55;
                GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)23ULL));
                gm_t9qkh0rsooc8d t56;
                GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)24ULL));
                gm_t9qkh0rsooc8d t57;
                GM_ST(uint8_t, ((uint8_t *)&t57 + 0), ((uint8_t)25ULL));
                gm_t9qkh0rsooc8d t58;
                GM_ST(uint8_t, ((uint8_t *)&t58 + 0), ((uint8_t)26ULL));
                gm_t4qfn59m757do t59;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t59 + 0), t45);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t59 + 1), t46);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t59 + 2), t47);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t59 + 3), t48);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t59 + 4), t49);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t59 + 5), t50);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t59 + 6), t51);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t59 + 7), t52);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t59 + 8), t53);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t59 + 9), t54);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t59 + 10), t55);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t59 + 11), t56);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t59 + 12), t57);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t59 + 13), t58);
                uint8_t t60 = 0;
                if (gm_eqtje7cegoml(((uint8_t *)&t59) + 0, ((uint8_t *)&t44))) t60 = 1;
                if (gm_eqtje7cegoml(((uint8_t *)&t59) + 1, ((uint8_t *)&t44))) t60 = 1;
                if (gm_eqtje7cegoml(((uint8_t *)&t59) + 2, ((uint8_t *)&t44))) t60 = 1;
                if (gm_eqtje7cegoml(((uint8_t *)&t59) + 3, ((uint8_t *)&t44))) t60 = 1;
                if (gm_eqtje7cegoml(((uint8_t *)&t59) + 4, ((uint8_t *)&t44))) t60 = 1;
                if (gm_eqtje7cegoml(((uint8_t *)&t59) + 5, ((uint8_t *)&t44))) t60 = 1;
                if (gm_eqtje7cegoml(((uint8_t *)&t59) + 6, ((uint8_t *)&t44))) t60 = 1;
                if (gm_eqtje7cegoml(((uint8_t *)&t59) + 7, ((uint8_t *)&t44))) t60 = 1;
                if (gm_eqtje7cegoml(((uint8_t *)&t59) + 8, ((uint8_t *)&t44))) t60 = 1;
                if (gm_eqtje7cegoml(((uint8_t *)&t59) + 9, ((uint8_t *)&t44))) t60 = 1;
                if (gm_eqtje7cegoml(((uint8_t *)&t59) + 10, ((uint8_t *)&t44))) t60 = 1;
                if (gm_eqtje7cegoml(((uint8_t *)&t59) + 11, ((uint8_t *)&t44))) t60 = 1;
                if (gm_eqtje7cegoml(((uint8_t *)&t59) + 12, ((uint8_t *)&t44))) t60 = 1;
                if (gm_eqtje7cegoml(((uint8_t *)&t59) + 13, ((uint8_t *)&t44))) t60 = 1;
                uint8_t t61 = (uint8_t)(!t60);
                if (!t61) {
                    goto gm_lb2;
                }
                void * t62 = gm_l0;
                void * t63 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                void * t65 = GM_LD(void *, (((uint8_t *)t63) + (uintptr_t)((uint64_t)0ULL) * 8));
                gm_t88rcucg1b7uk t66 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t65) + 56));
                void * t67 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t68)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t68, gm_srcn3jjc71a2bv3, 203, 36);
                gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)t67) + (uintptr_t)((uint64_t)0ULL) * 16));
                gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
                int64_t t71 = gm_f_sys_int_32e79ed(t62, GM_CONV(gm_csguankko4lmhd, t66), t70);
                gm_l5 = t71;
                goto gm_lb1;
            }
gm_lb2:;
        }
gm_lb1:;
        {
            {
                {
                    uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t73 = (uint8_t)(t72 == ((uint8_t)5ULL));
                    if (!t73) {
                        goto gm_lb4;
                    }
                    {
                        void * t74 = gm_l0;
                        gm_tl874t2atnlh3 t75 = gm_l1;
                        void * t76 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t77 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t77)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t77, gm_srcn3jjc71a2bv3, 207, 41);
                        gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)t76) + (uintptr_t)((uint64_t)0ULL) * 16));
                        gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
                        gm_f_sys_src_5svsr6i((void *)&gm_l58, (void *)NULL, cx, NULL, t74, t75, t79);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t80 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
                    if (t80) {
                    } else {
                        gm_t76123gfet73g t81 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l58) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t81);
                        goto gm_fdone;
                    }
                    gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l58) + 8));
                    gm_l6 = t82;
                    void * t83 = gm_l0;
                    void * t84 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t85)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t85, gm_srcn3jjc71a2bv3, 208, 33);
                    gm_b16_8 t86 = GM_LD(gm_b16_8, (((uint8_t *)t84) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&t86));
                    unsigned __int128 t88 = gm_f_get_uint_bq8c4b7(t83, t87);
                    uint32_t t89 = (uint32_t)t88;
                    gm_l7 = t89;
                    uint32_t t90 = gm_l7;
                    uint8_t t91 = gm_f_sys_has_0sego5l(t90, ((uint32_t)1ULL));
                    uint32_t t92 = gm_l7;
                    uint8_t t93 = gm_f_sys_has_0sego5l(t92, ((uint32_t)2ULL));
                    uint32_t t94 = gm_l7;
                    uint8_t t95 = gm_f_sys_has_0sego5l(t94, ((uint32_t)4ULL));
                    uint32_t t96 = gm_l7;
                    uint8_t t97 = gm_f_sys_has_0sego5l(t96, ((uint32_t)8ULL));
                    uint32_t t98 = gm_l7;
                    uint8_t t99 = gm_f_sys_has_0sego5l(t98, ((uint32_t)16ULL));
                    uint32_t t100 = gm_l7;
                    uint8_t t101 = gm_f_sys_has_0sego5l(t100, ((uint32_t)32ULL));
                    gm_t4q2b5ibsrdik t102;
                    GM_ST(uint8_t, ((uint8_t *)&t102 + 0), t91);
                    GM_ST(uint8_t, ((uint8_t *)&t102 + 1), t93);
                    GM_ST(uint8_t, ((uint8_t *)&t102 + 2), t95);
                    GM_ST(uint8_t, ((uint8_t *)&t102 + 3), t97);
                    GM_ST(uint8_t, ((uint8_t *)&t102 + 4), t99);
                    GM_ST(uint8_t, ((uint8_t *)&t102 + 5), t101);
                    gm_l8 = t102;
                    gm_b16_8 t103 = gm_l6;
                    gm_t4q2b5ibsrdik t104 = gm_l8;
                    gm_node *pn105 = gm_find(cx, 2126215853);
                    if (!pn105) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 217, 18);
                    gm_t62ebbi2i5rkd t106;
                    t106 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_t4q2b5ibsrdik))pn105->clauses[0])(pn105->env, pn105->hctx, pn105, t103, t104);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l59 = t106;
                    uint8_t t107 = (uint8_t)!((((uint8_t *)&gm_l59))[0] != 0);
                    if (t107) {
                        gm_tm98ap4112hee t108 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&gm_l59) + 8));
                        gm_l10 = t108;
                    } else {
                        gm_toe5fmr3vvf4d t109 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l59) + 8));
                        gm_l9 = t109;
                        gm_toe5fmr3vvf4d t110 = gm_l9;
                        int64_t t111 = gm_f_sys_code_cjtlfo1(t110);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t111);
                        goto gm_fdone;
                    }
                    void * t112 = gm_l0;
                    void * t113 = (void *)(((uint8_t *)t112) + 1368);
                    int64_t t114 = gm_f_sys_next_handle_r35ek81(t113);
                    gm_l11 = t114;
                    void * t115 = gm_l0;
                    void * t116 = (void *)(((uint8_t *)t115) + 1368);
                    int64_t t117 = gm_l11;
                    gm_tm98ap4112hee t118 = gm_l10;
                    gm_tftb1odc739t5 t119 = gm_f_put_endcgjj(t116, t117, GM_CONV(gm_csfq64m1m1rt6m, t118));
                    gm_l60 = t119;
                    uint8_t t120 = (uint8_t)!((((uint8_t *)&gm_l60))[0] != 0);
                    if (!t120) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l60) + 1, gm_srcn3jjc71a2bv3, 219, 13);
                    {
                        int64_t t121 = gm_l11;
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t121);
                        goto gm_fdone;
                    }
                }
gm_lb4:;
            }
            {
                {
                    uint8_t t122 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t123 = (uint8_t)(t122 == ((uint8_t)6ULL));
                    if (!t123) {
                        goto gm_lb5;
                    }
                    void * t124 = gm_l0;
                    void * t125 = (void *)(((uint8_t *)t124) + 1368);
                    int64_t t126 = gm_l5;
                    gm_tppo2hmut88dq t127 = gm_f_remove_82qm8hc(t125, t126);
                    gm_l61 = t127;
                    uint8_t t128 = (uint8_t)((((uint8_t *)&gm_l61))[8] != 0);
                    if (t128) {
                        gm_tm98ap4112hee t129 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l61));
                        gm_l12 = t129;
                    } else {
                        int64_t t130 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t130);
                        goto gm_fdone;
                    }
                    gm_tm98ap4112hee t131 = gm_l12;
                    gm_node *pn132 = gm_find(cx, 2126215853);
                    if (!pn132) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 224, 13);
                    gm_b0_1 t133;
                    t133 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn132->clauses[1])(pn132->env, pn132->hctx, pn132, t131);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), ((int64_t)0LL));
                    goto gm_fdone;
                }
gm_lb5:;
                {
                    uint8_t t134 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t135 = (uint8_t)(t134 == ((uint8_t)7ULL));
                    if (!t135) {
                        goto gm_lb6;
                    }
                    {
                        void * t136 = gm_l0;
                        gm_tl874t2atnlh3 t137 = gm_l1;
                        void * t138 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t139 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t139)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t139, gm_srcn3jjc71a2bv3, 228, 41);
                        gm_b16_8 t140 = GM_LD(gm_b16_8, (((uint8_t *)t138) + (uintptr_t)((uint64_t)1ULL) * 16));
                        gm_b16_8 t141 = GM_LD(gm_b16_8, ((uint8_t *)&t140));
                        gm_f_sys_dest_rtrb14h((void *)&gm_l62, (void *)NULL, cx, NULL, t136, t137, t141);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t142 = (uint8_t)!((((uint8_t *)&gm_l62))[0] != 0);
                    if (t142) {
                    } else {
                        gm_t76123gfet73g t143 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l62) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t143);
                        goto gm_fdone;
                    }
                    gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l62) + 8));
                    gm_l13 = t144;
                    void * t145 = gm_l0;
                    void * t146 = (void *)(((uint8_t *)t145) + 1368);
                    int64_t t147 = gm_l5;
                    gm_tppo2hmut88dq t148 = gm_f_get_f2mbmm8(t146, t147);
                    gm_l63 = t148;
                    uint8_t t149 = (uint8_t)((((uint8_t *)&gm_l63))[8] != 0);
                    if (t149) {
                        gm_tm98ap4112hee t150 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l63));
                        gm_l14 = t150;
                    } else {
                        int64_t t151 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t151);
                        goto gm_fdone;
                    }
                    void * t152 = gm_l0;
                    uint64_t t153 = GM_LD(uint64_t, ((((uint8_t *)t152) + 1672) + 8));
                    uint8_t t154 = (t153 > ((uint64_t)1ULL));
                    if (t154) {
                        {
                            void * t155 = gm_l0;
                            gm_tl874t2atnlh3 t156 = gm_l1;
                            gm_tm98ap4112hee t157 = gm_l14;
                            gm_b16_8 t158 = gm_l13;
                            gm_t3olsle24rveu t159;
                            GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t159 + 0), t157);
                            GM_ST(gm_b16_8, ((uint8_t *)&t159 + 8), t158);
                            gm_tnp3hpcm9uhoi t160;
                            GM_ST(uint8_t, (uint8_t *)&t160 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t160 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t160 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t160 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_t3olsle24rveu, ((uint8_t *)&t160 + 8), t159);
                            gm_f_on_host_thread_kmegc07((void *)&gm_l64, (void *)NULL, cx, NULL, t155, t156, &t160);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t161 = (uint8_t)!((((uint8_t *)&gm_l64))[0] != 0);
                        if (t161) {
                        } else {
                            gm_t76123gfet73g t162 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l64) + 8));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t162);
                            goto gm_fdone;
                        }
                        {
                            memmove(((uint8_t *)&gm_l65), (((uint8_t *)&gm_l64) + 8), 40);
                        }
                        {
                            gm_thf5kr0is9u9i t163 = GM_LD(gm_thf5kr0is9u9i, ((uint8_t *)&gm_l65));
                            int64_t t164 = gm_f_sys_count_50c5lcp(t163);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                            GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t164);
                            goto gm_fdone;
                        }
                    }
                    {
                        gm_tm98ap4112hee t165 = gm_l14;
                        gm_b16_8 t166 = gm_l13;
                        gm_node *pn167 = gm_find(cx, 2126215853);
                        if (!pn167) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 231, 30);
                        gm_thf5kr0is9u9i t168;
                        t168 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn167->clauses[2])(pn167->env, pn167->hctx, pn167, t165, t166);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        int64_t t169 = gm_f_sys_count_50c5lcp(t168);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t169);
                        goto gm_fdone;
                    }
                }
gm_lb6:;
            }
            {
                {
                    uint8_t t170 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t171 = (uint8_t)(t170 == ((uint8_t)8ULL));
                    if (!t171) {
                        goto gm_lb7;
                    }
                    {
                        void * t172 = gm_l0;
                        gm_tl874t2atnlh3 t173 = gm_l1;
                        void * t174 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t175 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t175)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t175, gm_srcn3jjc71a2bv3, 234, 41);
                        gm_b16_8 t176 = GM_LD(gm_b16_8, (((uint8_t *)t174) + (uintptr_t)((uint64_t)1ULL) * 16));
                        gm_b16_8 t177 = GM_LD(gm_b16_8, ((uint8_t *)&t176));
                        gm_f_sys_src_5svsr6i((void *)&gm_l66, (void *)NULL, cx, NULL, t172, t173, t177);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t178 = (uint8_t)!((((uint8_t *)&gm_l66))[0] != 0);
                    if (t178) {
                    } else {
                        gm_t76123gfet73g t179 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l66) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t179);
                        goto gm_fdone;
                    }
                    gm_b16_8 t180 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l66) + 8));
                    gm_l15 = t180;
                    void * t181 = gm_l0;
                    void * t182 = (void *)(((uint8_t *)t181) + 1368);
                    int64_t t183 = gm_l5;
                    gm_tppo2hmut88dq t184 = gm_f_get_f2mbmm8(t182, t183);
                    gm_l67 = t184;
                    uint8_t t185 = (uint8_t)((((uint8_t *)&gm_l67))[8] != 0);
                    if (t185) {
                        gm_tm98ap4112hee t186 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l67));
                        gm_l16 = t186;
                    } else {
                        int64_t t187 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t187);
                        goto gm_fdone;
                    }
                    {
                        gm_tm98ap4112hee t188 = gm_l16;
                        gm_b16_8 t189 = gm_l15;
                        gm_node *pn190 = gm_find(cx, 2126215853);
                        if (!pn190) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 236, 30);
                        gm_thf5kr0is9u9i t191;
                        t191 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn190->clauses[3])(pn190->env, pn190->hctx, pn190, t188, t189);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        int64_t t192 = gm_f_sys_count_50c5lcp(t191);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t192);
                        goto gm_fdone;
                    }
                }
gm_lb7:;
                {
                    uint8_t t193 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t194 = (uint8_t)(t193 == ((uint8_t)9ULL));
                    if (!t194) {
                        goto gm_lb8;
                    }
                    void * t195 = gm_l0;
                    void * t196 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t197 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t197)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t197, gm_srcn3jjc71a2bv3, 239, 31);
                    void * t198 = GM_LD(void *, (((uint8_t *)t196) + (uintptr_t)((uint64_t)1ULL) * 8));
                    gm_t88rcucg1b7uk t199 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t198) + 56));
                    void * t200 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t201 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t201)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t201, gm_srcn3jjc71a2bv3, 239, 43);
                    gm_b16_8 t202 = GM_LD(gm_b16_8, (((uint8_t *)t200) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t203 = GM_LD(gm_b16_8, ((uint8_t *)&t202));
                    int64_t t204 = gm_f_sys_int_32e79ed(t195, GM_CONV(gm_csguankko4lmhd, t199), t203);
                    gm_l17 = t204;
                    void * t205 = gm_l0;
                    void * t206 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t207 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t207)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t207, gm_srcn3jjc71a2bv3, 240, 34);
                    void * t208 = GM_LD(void *, (((uint8_t *)t206) + (uintptr_t)((uint64_t)2ULL) * 8));
                    gm_t88rcucg1b7uk t209 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t208) + 56));
                    void * t210 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t211 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t211)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t211, gm_srcn3jjc71a2bv3, 240, 46);
                    gm_b16_8 t212 = GM_LD(gm_b16_8, (((uint8_t *)t210) + (uintptr_t)((uint64_t)2ULL) * 16));
                    gm_b16_8 t213 = GM_LD(gm_b16_8, ((uint8_t *)&t212));
                    int64_t t214 = gm_f_sys_int_32e79ed(t205, GM_CONV(gm_csguankko4lmhd, t209), t213);
                    gm_l18 = t214;
                    void * t215 = gm_l0;
                    void * t216 = (void *)(((uint8_t *)t215) + 1368);
                    int64_t t217 = gm_l5;
                    gm_tppo2hmut88dq t218 = gm_f_get_f2mbmm8(t216, t217);
                    gm_l68 = t218;
                    uint8_t t219 = (uint8_t)((((uint8_t *)&gm_l68))[8] != 0);
                    if (t219) {
                        gm_tm98ap4112hee t220 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l68));
                        gm_l19 = t220;
                    } else {
                        int64_t t221 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t221);
                        goto gm_fdone;
                    }
                    {
                        {
                            int64_t t222 = gm_l18;
                            uint8_t t223 = ((t222 == ((int64_t)0LL)));
                            if (!t223) {
                                goto gm_lb10;
                            }
                            int64_t t224 = gm_l17;
                            uint8_t t225 = (t224 < ((int64_t)0LL));
                            if (!t225) {
                                goto gm_lb10;
                            }
                            int64_t t226 = GM_LD(int64_t, ((uint8_t *)gm_cndfnv7mbeh));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                            GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t226);
                            goto gm_fdone;
                        }
gm_lb10:;
                    }
gm_lb9:;
                    {
                        {
                            int64_t t227 = gm_l18;
                            uint8_t t228 = ((t227 == ((int64_t)0LL)));
                            if (!t228) {
                                goto gm_lb12;
                            }
                            gm_t9qkh0rsooc8d t229;
                            GM_ST(uint8_t, ((uint8_t *)&t229 + 0), ((uint8_t)0ULL));
                            gm_l20 = t229;
                            goto gm_lb11;
                        }
gm_lb12:;
                        {
                            int64_t t230 = gm_l18;
                            uint8_t t231 = ((t230 == ((int64_t)1LL)));
                            if (!t231) {
                                goto gm_lb13;
                            }
                            gm_t9qkh0rsooc8d t232;
                            GM_ST(uint8_t, ((uint8_t *)&t232 + 0), ((uint8_t)1ULL));
                            gm_l20 = t232;
                            goto gm_lb11;
                        }
gm_lb13:;
                        {
                            int64_t t233 = gm_l18;
                            uint8_t t234 = ((t233 == ((int64_t)2LL)));
                            if (!t234) {
                                goto gm_lb14;
                            }
                            gm_t9qkh0rsooc8d t235;
                            GM_ST(uint8_t, ((uint8_t *)&t235 + 0), ((uint8_t)2ULL));
                            gm_l20 = t235;
                            goto gm_lb11;
                        }
gm_lb14:;
                        {
                            int64_t t236 = GM_LD(int64_t, ((uint8_t *)gm_cndfnv7mbeh));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                            GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t236);
                            goto gm_fdone;
                        }
gm_lb15:;
                        gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 243, 33);
                    }
gm_lb11:;
                    gm_tm98ap4112hee t237 = gm_l19;
                    int64_t t238 = gm_l17;
                    gm_t9qkh0rsooc8d t239 = gm_l20;
                    gm_node *pn240 = gm_find(cx, 2126215853);
                    if (!pn240) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 249, 20);
                    gm_thf5kr0is9u9i t241;
                    t241 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, int64_t, gm_t9qkh0rsooc8d))pn240->clauses[4])(pn240->env, pn240->hctx, pn240, t237, t238, t239);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l69 = t241;
                    uint8_t t242 = (uint8_t)!((((uint8_t *)&gm_l69))[0] != 0);
                    if (t242) {
                        uint64_t t243 = GM_LD(uint64_t, (((uint8_t *)&gm_l69) + 8));
                        gm_l22 = t243;
                    } else {
                        gm_toe5fmr3vvf4d t244 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l69) + 8));
                        gm_l21 = t244;
                        gm_toe5fmr3vvf4d t245 = gm_l21;
                        int64_t t246 = gm_f_sys_code_cjtlfo1(t245);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t246);
                        goto gm_fdone;
                    }
                    uint64_t t247 = gm_l22;
                    int64_t t248;
                    memset(&t248, 0, sizeof t248); memcpy(&t248, ((uint8_t *)&t247), sizeof t248 < 8 ? sizeof t248 : 8);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t248);
                    goto gm_fdone;
                }
gm_lb8:;
            }
            {
                {
                    {
                        {
                            uint8_t t249 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t250 = (uint8_t)(t249 == ((uint8_t)10ULL));
                            if (!t250) {
                                goto gm_lb18;
                            }
                            goto gm_lb17;
                        }
gm_lb18:;
                        {
                            uint8_t t251 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t252 = (uint8_t)(t251 == ((uint8_t)18ULL));
                            if (!t252) {
                                goto gm_lb19;
                            }
                            goto gm_lb17;
                        }
gm_lb19:;
                        goto gm_lb16;
                    }
gm_lb17:;
                    {
                        void * t253 = gm_l0;
                        gm_tl874t2atnlh3 t254 = gm_l1;
                        void * t255 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t256 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t256)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t256, gm_srcn3jjc71a2bv3, 253, 41);
                        gm_b16_8 t257 = GM_LD(gm_b16_8, (((uint8_t *)t255) + (uintptr_t)((uint64_t)0ULL) * 16));
                        gm_b16_8 t258 = GM_LD(gm_b16_8, ((uint8_t *)&t257));
                        gm_f_sys_src_5svsr6i((void *)&gm_l70, (void *)NULL, cx, NULL, t253, t254, t258);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t259 = (uint8_t)!((((uint8_t *)&gm_l70))[0] != 0);
                    if (t259) {
                    } else {
                        gm_t76123gfet73g t260 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l70) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t260);
                        goto gm_fdone;
                    }
                    gm_b16_8 t261 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l70) + 8));
                    gm_l23 = t261;
                    gm_t9qkh0rsooc8d t262 = gm_l2;
                    uint8_t t263 = GM_LD(uint8_t, (((uint8_t *)&t262) + 0));
                    uint8_t t264 = (uint8_t)((t263 == ((uint8_t)10ULL)));
                    gm_t9qkh0rsooc8d t265;
                    GM_ST(uint8_t, ((uint8_t *)&t265 + 0), ((uint8_t)10ULL));
                    if (t264) {
                        gm_b16_8 t266 = gm_l23;
                        gm_node *pn267 = gm_find(cx, 2126215853);
                        if (!pn267) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 254, 41);
                        gm_tih6h4avv5nrb t268;
                        ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn267->clauses[5])((void *)&t268, pn267->env, pn267->hctx, pn267, t266);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l71 = t268;
                    } else {
                        gm_b16_8 t269 = gm_l23;
                        gm_node *pn270 = gm_find(cx, 2126215853);
                        if (!pn270) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 254, 64);
                        gm_tih6h4avv5nrb t271;
                        ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn270->clauses[13])((void *)&t271, pn270->env, pn270->hctx, pn270, t269);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l71 = t271;
                    }
                    {
                        memmove(((uint8_t *)&gm_l72), ((uint8_t *)&gm_l71), 40);
                    }
                    uint8_t t272 = (uint8_t)!((((uint8_t *)&gm_l72))[0] != 0);
                    if (t272) {
                        memmove(((uint8_t *)&gm_l25), (((uint8_t *)&gm_l72) + 8), 32);
                    } else {
                        gm_toe5fmr3vvf4d t273 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l72) + 8));
                        gm_l24 = t273;
                        gm_toe5fmr3vvf4d t274 = gm_l24;
                        int64_t t275 = gm_f_sys_code_cjtlfo1(t274);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t275);
                        goto gm_fdone;
                    }
                    void * t276 = gm_l0;
                    void * t277 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t278 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t278)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t278, gm_srcn3jjc71a2bv3, 255, 33);
                    gm_b16_8 t279 = GM_LD(gm_b16_8, (((uint8_t *)t277) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t280 = GM_LD(gm_b16_8, ((uint8_t *)&t279));
                    gm_t2lqu8jsj511d t281 = gm_f_slice_parts_1se5hu4(t276, t280);
                    gm_l26 = t281;
                    uint64_t t282 = GM_LD(uint64_t, (((uint8_t *)&gm_l26) + 8));
                    uint64_t t283 = (t282 < ((uint64_t)4ULL) ? t282 : ((uint64_t)4ULL));
                    gm_l27 = t283;
                    {
                        void * t284 = gm_l0;
                        void * t285 = (void *)(((uint8_t *)t284) + 184);
                        uint64_t t286 = GM_LD(uint64_t, ((uint8_t *)&gm_l26));
                        uint64_t t287 = gm_l27;
                        uint64_t r288; bool o289 = __builtin_mul_overflow(t287, ((uint64_t)8ULL), &r288);
                        if (o289) gm_trap("integer overflow in `*`", gm_srcn3jjc71a2bv3, 257, 46);
                        uint64_t t290 = r288;
                        gm_f_write_view_pebit6h((void *)&gm_l73, t285, t286, t290, ((uint64_t)8ULL));
                    }
                    uint8_t t291 = (uint8_t)!((((uint8_t *)&gm_l73))[0] != 0);
                    if (t291) {
                        gm_b16_8 t292 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l73) + 8));
                        gm_l29 = t292;
                    } else {
                        {
                            memmove(((uint8_t *)&F.l28), (((uint8_t *)&gm_l73) + 8), 40);
                        }
                        {
                            void * t293 = gm_l0;
                            gm_tl874t2atnlh3 t294 = gm_l1;
                            gm_t76123gfet73g t295;
                            gm_f_mem_trap_ecogko0((void *)&t295, (void *)NULL, cx, NULL, t293, t294, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l28)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t295);
                            goto gm_fdone;
                        }
                    }
                    {
                        uint64_t t296 = GM_LD(uint64_t, ((uint8_t *)&gm_l25));
                        int64_t t297;
                        memset(&t297, 0, sizeof t297); memcpy(&t297, ((uint8_t *)&t296), sizeof t297 < 8 ? sizeof t297 : 8);
                        gm_t9qkh0rsooc8d t298 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l25) + 8));
                        int64_t t299 = gm_f_sys_kind_sjfdqbt(t298);
                        int64_t t300 = GM_LD(int64_t, (((uint8_t *)&gm_l25) + 16));
                        uint32_t t301 = GM_LD(uint32_t, (((uint8_t *)&gm_l25) + 24));
                        int64_t t302 = (int64_t)t301;
                        GM_ST(int64_t, ((uint8_t *)&gm_l30 + 0), t297);
                        GM_ST(int64_t, ((uint8_t *)&gm_l30 + 8), t299);
                        GM_ST(int64_t, ((uint8_t *)&gm_l30 + 16), t300);
                        GM_ST(int64_t, ((uint8_t *)&gm_l30 + 24), t302);
                    }
                    gm_l32 = ((uint64_t)0ULL);
                    uint64_t t303 = gm_l27;
                    uint64_t t304 = gm_f_us_cuv01fp(t303);
                    gm_l31 = t304;
                    {
                        for (;;) {
                            uint64_t t305 = gm_l32;
                            uint64_t t306 = gm_l31;
                            uint8_t t307 = (t305 < t306);
                            if (!t307) {
                                goto gm_lb20;
                            }
                            {
                                {
                                    {
                                        uint64_t t308 = gm_l32;
                                        gm_l33 = t308;
                                        void * t309 = gm_l0;
                                        void * t310 = GM_LD(void *, ((uint8_t *)&gm_l29));
                                        uint64_t t311 = GM_LD(uint64_t, (((uint8_t *)&gm_l29) + 8));
                                        uint64_t t312 = gm_l33;
                                        uint64_t r313; bool o314 = __builtin_mul_overflow(t312, ((uint64_t)8ULL), &r313);
                                        if (o314) gm_trap("integer overflow in `*`", gm_srcn3jjc71a2bv3, 259, 53);
                                        uint64_t t315 = r313;
                                        uint64_t t316 = gm_l33;
                                        uint64_t r317; bool o318 = __builtin_add_overflow(t316, ((uint64_t)1ULL), &r317);
                                        if (o318) gm_trap("integer overflow in `+`", gm_srcn3jjc71a2bv3, 259, 61);
                                        uint64_t t319 = r317;
                                        uint64_t r320; bool o321 = __builtin_mul_overflow(t319, ((uint64_t)8ULL), &r320);
                                        if (o321) gm_trap("integer overflow in `*`", gm_srcn3jjc71a2bv3, 259, 60);
                                        uint64_t t322 = r320;
                                        uint64_t t323 = (uintptr_t)t315;
                                        uint64_t t324 = (uintptr_t)t322;
                                        if (t323 > t324 || t324 > (uintptr_t)t311) gm_trap_slice(t323, t324, t311, gm_srcn3jjc71a2bv3, 259, 48);
                                        uint64_t t325 = ((uintptr_t)t322) - (uintptr_t)t315;
                                        gm_b16_8 t326;
                                        GM_ST(void *, (uint8_t *)&t326, (void *)(((uint8_t *)t310 + (uintptr_t)t315 * 1)));
                                        GM_ST(uintptr_t, (uint8_t *)&t326 + 8, (uintptr_t)(t325));
                                        uint64_t t327 = gm_l33;
                                        if ((uintptr_t)t327 >= (uintptr_t)(((uint64_t)4ULL))) gm_trap_index((uintptr_t)t327, ((uint64_t)4ULL), gm_srcn3jjc71a2bv3, 259, 79);
                                        int64_t t328 = GM_LD(int64_t, (((uint8_t *)&gm_l30) + (uintptr_t)t327 * 8));
                                        uint64_t t329;
                                        memset(&t329, 0, sizeof t329); memcpy(&t329, ((uint8_t *)&t328), sizeof t329 < 8 ? sizeof t329 : 8);
                                        unsigned __int128 t330 = (unsigned __int128)t329;
                                        gm_f_put_uint_cddin99(t309, t326, t330);
                                        goto gm_lb23;
                                    }
gm_lb24:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 259, 13);
                                }
gm_lb23:;
                            }
gm_lb22:;
                            uint64_t t331 = gm_l32;
                            uint64_t r332; bool o333 = __builtin_add_overflow(t331, ((uint64_t)1ULL), &r332);
                            uint64_t t334 = r332;
                            gm_l32 = t334;
                        }
                    }
gm_lb20:;
                    {
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), ((int64_t)0LL));
                        goto gm_fdone;
                    }
                }
gm_lb16:;
            }
            {
                {
                    uint8_t t335 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t336 = (uint8_t)(t335 == ((uint8_t)11ULL));
                    if (!t336) {
                        goto gm_lb25;
                    }
                    {
                        void * t337 = gm_l0;
                        gm_tl874t2atnlh3 t338 = gm_l1;
                        void * t339 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t340 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t340)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t340, gm_srcn3jjc71a2bv3, 262, 68);
                        gm_b16_8 t341 = GM_LD(gm_b16_8, (((uint8_t *)t339) + (uintptr_t)((uint64_t)0ULL) * 16));
                        gm_b16_8 t342 = GM_LD(gm_b16_8, ((uint8_t *)&t341));
                        gm_f_sys_src_5svsr6i((void *)&gm_l74, (void *)NULL, cx, NULL, t337, t338, t342);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t343 = (uint8_t)!((((uint8_t *)&gm_l74))[0] != 0);
                    if (t343) {
                    } else {
                        gm_t76123gfet73g t344 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l74) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t344);
                        goto gm_fdone;
                    }
                    {
                        gm_b16_8 t345 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l74) + 8));
                        gm_node *pn346 = gm_find(cx, 2126215853);
                        if (!pn346) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 262, 38);
                        gm_ttln0d0hd70en t347;
                        t347 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn346->clauses[6])(pn346->env, pn346->hctx, pn346, t345);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        int64_t t348 = gm_f_sys_unit_nrsqciq(t347);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t348);
                        goto gm_fdone;
                    }
                }
gm_lb25:;
                {
                    uint8_t t349 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t350 = (uint8_t)(t349 == ((uint8_t)12ULL));
                    if (!t350) {
                        goto gm_lb26;
                    }
                    {
                        void * t351 = gm_l0;
                        gm_tl874t2atnlh3 t352 = gm_l1;
                        void * t353 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t354 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t354)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t354, gm_srcn3jjc71a2bv3, 264, 41);
                        gm_b16_8 t355 = GM_LD(gm_b16_8, (((uint8_t *)t353) + (uintptr_t)((uint64_t)0ULL) * 16));
                        gm_b16_8 t356 = GM_LD(gm_b16_8, ((uint8_t *)&t355));
                        gm_f_sys_src_5svsr6i((void *)&gm_l75, (void *)NULL, cx, NULL, t351, t352, t356);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t357 = (uint8_t)!((((uint8_t *)&gm_l75))[0] != 0);
                    if (t357) {
                    } else {
                        gm_t76123gfet73g t358 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l75) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t358);
                        goto gm_fdone;
                    }
                    gm_b16_8 t359 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l75) + 8));
                    gm_l34 = t359;
                    {
                        void * t360 = gm_l0;
                        gm_tl874t2atnlh3 t361 = gm_l1;
                        void * t362 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t363 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t363)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t363, gm_srcn3jjc71a2bv3, 265, 39);
                        gm_b16_8 t364 = GM_LD(gm_b16_8, (((uint8_t *)t362) + (uintptr_t)((uint64_t)1ULL) * 16));
                        gm_b16_8 t365 = GM_LD(gm_b16_8, ((uint8_t *)&t364));
                        gm_f_sys_src_5svsr6i((void *)&gm_l76, (void *)NULL, cx, NULL, t360, t361, t365);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t366 = (uint8_t)!((((uint8_t *)&gm_l76))[0] != 0);
                    if (t366) {
                    } else {
                        gm_t76123gfet73g t367 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l76) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t367);
                        goto gm_fdone;
                    }
                    gm_b16_8 t368 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l76) + 8));
                    gm_l35 = t368;
                    {
                        gm_b16_8 t369 = gm_l34;
                        gm_b16_8 t370 = gm_l35;
                        gm_node *pn371 = gm_find(cx, 2126215853);
                        if (!pn371) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 266, 29);
                        gm_ttln0d0hd70en t372;
                        t372 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_b16_8))pn371->clauses[7])(pn371->env, pn371->hctx, pn371, t369, t370);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        int64_t t373 = gm_f_sys_unit_nrsqciq(t372);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t373);
                        goto gm_fdone;
                    }
                }
gm_lb26:;
                {
                    uint8_t t374 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t375 = (uint8_t)(t374 == ((uint8_t)13ULL));
                    if (!t375) {
                        goto gm_lb27;
                    }
                    {
                        void * t376 = gm_l0;
                        gm_tl874t2atnlh3 t377 = gm_l1;
                        void * t378 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t379 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t379)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t379, gm_srcn3jjc71a2bv3, 268, 71);
                        gm_b16_8 t380 = GM_LD(gm_b16_8, (((uint8_t *)t378) + (uintptr_t)((uint64_t)0ULL) * 16));
                        gm_b16_8 t381 = GM_LD(gm_b16_8, ((uint8_t *)&t380));
                        gm_f_sys_src_5svsr6i((void *)&gm_l77, (void *)NULL, cx, NULL, t376, t377, t381);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t382 = (uint8_t)!((((uint8_t *)&gm_l77))[0] != 0);
                    if (t382) {
                    } else {
                        gm_t76123gfet73g t383 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l77) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t383);
                        goto gm_fdone;
                    }
                    {
                        gm_b16_8 t384 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l77) + 8));
                        gm_node *pn385 = gm_find(cx, 2126215853);
                        if (!pn385) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 268, 39);
                        gm_ttln0d0hd70en t386;
                        t386 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn385->clauses[8])(pn385->env, pn385->hctx, pn385, t384);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        int64_t t387 = gm_f_sys_unit_nrsqciq(t386);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t387);
                        goto gm_fdone;
                    }
                }
gm_lb27:;
            }
            {
                {
                    uint8_t t388 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t389 = (uint8_t)(t388 == ((uint8_t)14ULL));
                    if (!t389) {
                        goto gm_lb28;
                    }
                    {
                        void * t390 = gm_l0;
                        gm_tl874t2atnlh3 t391 = gm_l1;
                        void * t392 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t393 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t393)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t393, gm_srcn3jjc71a2bv3, 269, 75);
                        gm_b16_8 t394 = GM_LD(gm_b16_8, (((uint8_t *)t392) + (uintptr_t)((uint64_t)0ULL) * 16));
                        gm_b16_8 t395 = GM_LD(gm_b16_8, ((uint8_t *)&t394));
                        gm_f_sys_src_5svsr6i((void *)&gm_l78, (void *)NULL, cx, NULL, t390, t391, t395);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t396 = (uint8_t)!((((uint8_t *)&gm_l78))[0] != 0);
                    if (t396) {
                    } else {
                        gm_t76123gfet73g t397 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l78) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t397);
                        goto gm_fdone;
                    }
                    {
                        gm_b16_8 t398 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l78) + 8));
                        gm_node *pn399 = gm_find(cx, 2126215853);
                        if (!pn399) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 269, 41);
                        gm_ttln0d0hd70en t400;
                        t400 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn399->clauses[9])(pn399->env, pn399->hctx, pn399, t398);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        int64_t t401 = gm_f_sys_unit_nrsqciq(t400);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t401);
                        goto gm_fdone;
                    }
                }
gm_lb28:;
                {
                    uint8_t t402 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t403 = (uint8_t)(t402 == ((uint8_t)15ULL));
                    if (!t403) {
                        goto gm_lb29;
                    }
                    {
                        void * t404 = gm_l0;
                        gm_tl874t2atnlh3 t405 = gm_l1;
                        void * t406 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t407 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t407)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t407, gm_srcn3jjc71a2bv3, 271, 41);
                        gm_b16_8 t408 = GM_LD(gm_b16_8, (((uint8_t *)t406) + (uintptr_t)((uint64_t)0ULL) * 16));
                        gm_b16_8 t409 = GM_LD(gm_b16_8, ((uint8_t *)&t408));
                        gm_f_sys_src_5svsr6i((void *)&gm_l79, (void *)NULL, cx, NULL, t404, t405, t409);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t410 = (uint8_t)!((((uint8_t *)&gm_l79))[0] != 0);
                    if (t410) {
                    } else {
                        gm_t76123gfet73g t411 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l79) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t411);
                        goto gm_fdone;
                    }
                    gm_b16_8 t412 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l79) + 8));
                    gm_l36 = t412;
                    gm_b16_8 t413 = gm_l36;
                    gm_node *pn414 = gm_find(cx, 2126215853);
                    if (!pn414) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 272, 18);
                    gm_t62ebbi2i5rkd t415;
                    t415 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8))pn414->clauses[10])(pn414->env, pn414->hctx, pn414, t413);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l80 = t415;
                    uint8_t t416 = (uint8_t)!((((uint8_t *)&gm_l80))[0] != 0);
                    if (t416) {
                        gm_tm98ap4112hee t417 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&gm_l80) + 8));
                        gm_l38 = t417;
                    } else {
                        gm_toe5fmr3vvf4d t418 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l80) + 8));
                        gm_l37 = t418;
                        gm_toe5fmr3vvf4d t419 = gm_l37;
                        int64_t t420 = gm_f_sys_code_cjtlfo1(t419);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t420);
                        goto gm_fdone;
                    }
                    void * t421 = gm_l0;
                    void * t422 = (void *)(((uint8_t *)t421) + 1368);
                    int64_t t423 = gm_f_sys_next_handle_r35ek81(t422);
                    gm_l39 = t423;
                    void * t424 = gm_l0;
                    void * t425 = (void *)((((uint8_t *)t424) + 1368) + 56);
                    int64_t t426 = gm_l39;
                    gm_tm98ap4112hee t427 = gm_l38;
                    gm_tftb1odc739t5 t428 = gm_f_put_0c6jnkb(t425, t426, GM_CONV(gm_csfq64m1m1rt6m, t427));
                    gm_l81 = t428;
                    uint8_t t429 = (uint8_t)!((((uint8_t *)&gm_l81))[0] != 0);
                    if (!t429) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l81) + 1, gm_srcn3jjc71a2bv3, 274, 13);
                    {
                        int64_t t430 = gm_l39;
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t430);
                        goto gm_fdone;
                    }
                }
gm_lb29:;
            }
            {
                {
                    uint8_t t431 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t432 = (uint8_t)(t431 == ((uint8_t)16ULL));
                    if (!t432) {
                        goto gm_lb30;
                    }
                    void * t433 = gm_l0;
                    void * t434 = (void *)((((uint8_t *)t433) + 1368) + 56);
                    int64_t t435 = gm_l5;
                    gm_tppo2hmut88dq t436 = gm_f_get_v07up09(t434, t435);
                    gm_l82 = t436;
                    uint8_t t437 = (uint8_t)((((uint8_t *)&gm_l82))[8] != 0);
                    if (t437) {
                        gm_tm98ap4112hee t438 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l82));
                        gm_l40 = t438;
                    } else {
                        int64_t t439 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t439);
                        goto gm_fdone;
                    }
                    {
                        void * t440 = gm_l0;
                        gm_tl874t2atnlh3 t441 = gm_l1;
                        void * t442 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t443 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t443)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t443, gm_srcn3jjc71a2bv3, 279, 41);
                        gm_b16_8 t444 = GM_LD(gm_b16_8, (((uint8_t *)t442) + (uintptr_t)((uint64_t)1ULL) * 16));
                        gm_b16_8 t445 = GM_LD(gm_b16_8, ((uint8_t *)&t444));
                        gm_f_sys_dest_rtrb14h((void *)&gm_l83, (void *)NULL, cx, NULL, t440, t441, t445);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t446 = (uint8_t)!((((uint8_t *)&gm_l83))[0] != 0);
                    if (t446) {
                    } else {
                        gm_t76123gfet73g t447 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l83) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t447);
                        goto gm_fdone;
                    }
                    gm_b16_8 t448 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l83) + 8));
                    gm_l41 = t448;
                    {
                        gm_tm98ap4112hee t449 = gm_l40;
                        gm_b16_8 t450 = gm_l41;
                        gm_node *pn451 = gm_find(cx, 2126215853);
                        if (!pn451) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 280, 21);
                        gm_tiuq7k5hm0kak t452;
                        ((void (*)(void *, void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn451->clauses[11])((void *)&t452, pn451->env, pn451->hctx, pn451, t449, t450);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l84 = t452;
                    }
                    uint8_t t453 = (uint8_t)!((((uint8_t *)&gm_l84))[0] != 0);
                    if (t453) {
                        memmove(((uint8_t *)&gm_l43), (((uint8_t *)&gm_l84) + 8), 24);
                    } else {
                        gm_toe5fmr3vvf4d t454 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l84) + 8));
                        gm_l42 = t454;
                        gm_toe5fmr3vvf4d t455 = gm_l42;
                        int64_t t456 = gm_f_sys_code_cjtlfo1(t455);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t456);
                        goto gm_fdone;
                    }
                    uint8_t t457 = (uint8_t)((((uint8_t *)&gm_l43))[16] != 0);
                    if (t457) {
                        gm_t617vr72bd7b3 t458 = GM_LD(gm_t617vr72bd7b3, ((uint8_t *)&gm_l43));
                        gm_l44 = t458;
                    } else {
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), ((int64_t)0LL));
                        goto gm_fdone;
                    }
                    {
                        uint64_t t459 = GM_LD(uint64_t, ((uint8_t *)&gm_l44));
                        int64_t t460 = (int64_t)t459;
                        gm_t9qkh0rsooc8d t461 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l44) + 8));
                        int64_t t462 = gm_f_sys_kind_sjfdqbt(t461);
                        unsigned __int128 n463 = (unsigned __int128)((uint64_t)32ULL);
                        if (n463 >= 64) gm_trap_shift((uint64_t)n463, "i64", gm_srcn3jjc71a2bv3, 282, 44);
                        int64_t r464 = (int64_t)((uint64_t)t462 << (int)n463);
                        bool o465 = (int64_t)(r464 >> (int)n463) != t462;
                        if (o465) gm_trap("integer overflow in `<<`", gm_srcn3jjc71a2bv3, 282, 44);
                        int64_t t466 = r464;
                        int64_t r467; bool o468 = __builtin_add_overflow(t460, t466, &r467);
                        if (o468) gm_trap("integer overflow in `+`", gm_srcn3jjc71a2bv3, 282, 20);
                        int64_t t469 = r467;
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t469);
                        goto gm_fdone;
                    }
                }
gm_lb30:;
                {
                    uint8_t t470 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t471 = (uint8_t)(t470 == ((uint8_t)17ULL));
                    if (!t471) {
                        goto gm_lb31;
                    }
                    void * t472 = gm_l0;
                    void * t473 = (void *)((((uint8_t *)t472) + 1368) + 56);
                    int64_t t474 = gm_l5;
                    gm_tppo2hmut88dq t475 = gm_f_remove_20jm5n8(t473, t474);
                    gm_l85 = t475;
                    uint8_t t476 = (uint8_t)((((uint8_t *)&gm_l85))[8] != 0);
                    if (t476) {
                        gm_tm98ap4112hee t477 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l85));
                        gm_l45 = t477;
                    } else {
                        int64_t t478 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t478);
                        goto gm_fdone;
                    }
                    gm_tm98ap4112hee t479 = gm_l45;
                    gm_node *pn480 = gm_find(cx, 2126215853);
                    if (!pn480) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 286, 13);
                    gm_b0_1 t481;
                    t481 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn480->clauses[12])(pn480->env, pn480->hctx, pn480, t479);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), ((int64_t)0LL));
                    goto gm_fdone;
                }
gm_lb31:;
            }
            {
                {
                    uint8_t t482 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t483 = (uint8_t)(t482 == ((uint8_t)19ULL));
                    if (!t483) {
                        goto gm_lb32;
                    }
                    void * t484 = gm_l0;
                    void * t485 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t486 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t486)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t486, gm_srcn3jjc71a2bv3, 290, 32);
                    gm_b16_8 t487 = GM_LD(gm_b16_8, (((uint8_t *)t485) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t488 = GM_LD(gm_b16_8, ((uint8_t *)&t487));
                    unsigned __int128 t489 = gm_f_get_uint_bq8c4b7(t484, t488);
                    uint64_t t490 = (uint64_t)t489;
                    gm_l46 = t490;
                    void * t491 = gm_l0;
                    void * t492 = (void *)(((uint8_t *)t491) + 1368);
                    int64_t t493 = gm_l5;
                    gm_tppo2hmut88dq t494 = gm_f_get_f2mbmm8(t492, t493);
                    gm_l86 = t494;
                    uint8_t t495 = (uint8_t)((((uint8_t *)&gm_l86))[8] != 0);
                    if (t495) {
                        gm_tm98ap4112hee t496 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l86));
                        gm_l47 = t496;
                    } else {
                        int64_t t497 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t497);
                        goto gm_fdone;
                    }
                    gm_tm98ap4112hee t498 = gm_l47;
                    uint64_t t499 = gm_l46;
                    gm_node *pn500 = gm_find(cx, 2126215853);
                    if (!pn500) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 292, 29);
                    gm_ttln0d0hd70en t501;
                    t501 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, uint64_t))pn500->clauses[14])(pn500->env, pn500->hctx, pn500, t498, t499);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    int64_t t502 = gm_f_sys_unit_nrsqciq(t501);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t502);
                    goto gm_fdone;
                }
gm_lb32:;
                {
                    uint8_t t503 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t504 = (uint8_t)(t503 == ((uint8_t)20ULL));
                    if (!t504) {
                        goto gm_lb33;
                    }
                    void * t505 = gm_l0;
                    void * t506 = (void *)(((uint8_t *)t505) + 1368);
                    int64_t t507 = gm_l5;
                    gm_tppo2hmut88dq t508 = gm_f_get_f2mbmm8(t506, t507);
                    gm_l87 = t508;
                    uint8_t t509 = (uint8_t)((((uint8_t *)&gm_l87))[8] != 0);
                    if (t509) {
                        gm_tm98ap4112hee t510 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l87));
                        gm_l48 = t510;
                    } else {
                        int64_t t511 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t511);
                        goto gm_fdone;
                    }
                    gm_tm98ap4112hee t512 = gm_l48;
                    gm_node *pn513 = gm_find(cx, 2126215853);
                    if (!pn513) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 296, 29);
                    gm_ttln0d0hd70en t514;
                    t514 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn513->clauses[15])(pn513->env, pn513->hctx, pn513, t512);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    int64_t t515 = gm_f_sys_unit_nrsqciq(t514);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t515);
                    goto gm_fdone;
                }
gm_lb33:;
                {
                    uint8_t t516 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t517 = (uint8_t)(t516 == ((uint8_t)21ULL));
                    if (!t517) {
                        goto gm_lb34;
                    }
                    {
                        void * t518 = gm_l0;
                        gm_tl874t2atnlh3 t519 = gm_l1;
                        void * t520 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t521 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t521)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t521, gm_srcn3jjc71a2bv3, 299, 41);
                        gm_b16_8 t522 = GM_LD(gm_b16_8, (((uint8_t *)t520) + (uintptr_t)((uint64_t)0ULL) * 16));
                        gm_b16_8 t523 = GM_LD(gm_b16_8, ((uint8_t *)&t522));
                        gm_f_sys_dest_rtrb14h((void *)&gm_l88, (void *)NULL, cx, NULL, t518, t519, t523);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t524 = (uint8_t)!((((uint8_t *)&gm_l88))[0] != 0);
                    if (t524) {
                    } else {
                        gm_t76123gfet73g t525 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l88) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t525);
                        goto gm_fdone;
                    }
                    gm_b16_8 t526 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l88) + 8));
                    gm_l49 = t526;
                    {
                        gm_b16_8 t527 = gm_l49;
                        gm_node *pn528 = gm_find(cx, 2126215853);
                        if (!pn528) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 300, 30);
                        gm_thf5kr0is9u9i t529;
                        t529 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_b16_8))pn528->clauses[16])(pn528->env, pn528->hctx, pn528, t527);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        int64_t t530 = gm_f_sys_count_50c5lcp(t529);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t530);
                        goto gm_fdone;
                    }
                }
gm_lb34:;
            }
            {
                {
                    uint8_t t531 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t532 = (uint8_t)(t531 == ((uint8_t)22ULL));
                    if (!t532) {
                        goto gm_lb35;
                    }
                    {
                        void * t533 = gm_l0;
                        gm_tl874t2atnlh3 t534 = gm_l1;
                        void * t535 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t536 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t536)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t536, gm_srcn3jjc71a2bv3, 303, 41);
                        gm_b16_8 t537 = GM_LD(gm_b16_8, (((uint8_t *)t535) + (uintptr_t)((uint64_t)0ULL) * 16));
                        gm_b16_8 t538 = GM_LD(gm_b16_8, ((uint8_t *)&t537));
                        gm_f_sys_src_5svsr6i((void *)&gm_l89, (void *)NULL, cx, NULL, t533, t534, t538);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t539 = (uint8_t)!((((uint8_t *)&gm_l89))[0] != 0);
                    if (t539) {
                    } else {
                        gm_t76123gfet73g t540 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l89) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t540);
                        goto gm_fdone;
                    }
                    gm_b16_8 t541 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l89) + 8));
                    gm_l50 = t541;
                    {
                        void * t542 = gm_l0;
                        gm_tl874t2atnlh3 t543 = gm_l1;
                        void * t544 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t545 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t545)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t545, gm_srcn3jjc71a2bv3, 304, 41);
                        gm_b16_8 t546 = GM_LD(gm_b16_8, (((uint8_t *)t544) + (uintptr_t)((uint64_t)1ULL) * 16));
                        gm_b16_8 t547 = GM_LD(gm_b16_8, ((uint8_t *)&t546));
                        gm_f_sys_dest_rtrb14h((void *)&gm_l90, (void *)NULL, cx, NULL, t542, t543, t547);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t548 = (uint8_t)!((((uint8_t *)&gm_l90))[0] != 0);
                    if (t548) {
                    } else {
                        gm_t76123gfet73g t549 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l90) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t549);
                        goto gm_fdone;
                    }
                    gm_b16_8 t550 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l90) + 8));
                    gm_l51 = t550;
                    {
                        gm_b16_8 t551 = gm_l50;
                        gm_b16_8 t552 = gm_l51;
                        gm_node *pn553 = gm_find(cx, 2126215853);
                        if (!pn553) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 305, 30);
                        gm_thf5kr0is9u9i t554;
                        t554 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_b16_8))pn553->clauses[17])(pn553->env, pn553->hctx, pn553, t551, t552);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        int64_t t555 = gm_f_sys_count_50c5lcp(t554);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t555);
                        goto gm_fdone;
                    }
                }
gm_lb35:;
                {
                    uint8_t t556 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t557 = (uint8_t)(t556 == ((uint8_t)23ULL));
                    if (!t557) {
                        goto gm_lb36;
                    }
                    {
                        void * t558 = gm_l0;
                        gm_tl874t2atnlh3 t559 = gm_l1;
                        void * t560 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t561 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t561)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t561, gm_srcn3jjc71a2bv3, 308, 41);
                        gm_b16_8 t562 = GM_LD(gm_b16_8, (((uint8_t *)t560) + (uintptr_t)((uint64_t)0ULL) * 16));
                        gm_b16_8 t563 = GM_LD(gm_b16_8, ((uint8_t *)&t562));
                        gm_f_sys_src_5svsr6i((void *)&gm_l91, (void *)NULL, cx, NULL, t558, t559, t563);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t564 = (uint8_t)!((((uint8_t *)&gm_l91))[0] != 0);
                    if (t564) {
                    } else {
                        gm_t76123gfet73g t565 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l91) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t565);
                        goto gm_fdone;
                    }
                    gm_b16_8 t566 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l91) + 8));
                    gm_l52 = t566;
                    {
                        void * t567 = gm_l0;
                        gm_tl874t2atnlh3 t568 = gm_l1;
                        void * t569 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t570 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t570)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t570, gm_srcn3jjc71a2bv3, 309, 41);
                        gm_b16_8 t571 = GM_LD(gm_b16_8, (((uint8_t *)t569) + (uintptr_t)((uint64_t)1ULL) * 16));
                        gm_b16_8 t572 = GM_LD(gm_b16_8, ((uint8_t *)&t571));
                        gm_f_sys_dest_rtrb14h((void *)&gm_l92, (void *)NULL, cx, NULL, t567, t568, t572);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t573 = (uint8_t)!((((uint8_t *)&gm_l92))[0] != 0);
                    if (t573) {
                    } else {
                        gm_t76123gfet73g t574 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l92) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t574);
                        goto gm_fdone;
                    }
                    gm_b16_8 t575 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l92) + 8));
                    gm_l53 = t575;
                    {
                        gm_b16_8 t576 = gm_l52;
                        gm_b16_8 t577 = gm_l53;
                        gm_node *pn578 = gm_find(cx, 2126215853);
                        if (!pn578) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 310, 30);
                        gm_thf5kr0is9u9i t579;
                        t579 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_b16_8))pn578->clauses[18])(pn578->env, pn578->hctx, pn578, t576, t577);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        int64_t t580 = gm_f_sys_count_50c5lcp(t579);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t580);
                        goto gm_fdone;
                    }
                }
gm_lb36:;
            }
            {
                {
                    uint8_t t581 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t582 = (uint8_t)(t581 == ((uint8_t)24ULL));
                    if (!t582) {
                        goto gm_lb37;
                    }
                    {
                        void * t583 = gm_l0;
                        gm_tl874t2atnlh3 t584 = gm_l1;
                        void * t585 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t586 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t586)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t586, gm_srcn3jjc71a2bv3, 313, 43);
                        gm_b16_8 t587 = GM_LD(gm_b16_8, (((uint8_t *)t585) + (uintptr_t)((uint64_t)0ULL) * 16));
                        gm_b16_8 t588 = GM_LD(gm_b16_8, ((uint8_t *)&t587));
                        gm_f_sys_src_5svsr6i((void *)&gm_l93, (void *)NULL, cx, NULL, t583, t584, t588);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t589 = (uint8_t)!((((uint8_t *)&gm_l93))[0] != 0);
                    if (t589) {
                    } else {
                        gm_t76123gfet73g t590 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l93) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t590);
                        goto gm_fdone;
                    }
                    gm_b16_8 t591 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l93) + 8));
                    gm_l54 = t591;
                    {
                        void * t592 = gm_l0;
                        gm_tl874t2atnlh3 t593 = gm_l1;
                        void * t594 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t595 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t595)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t595, gm_srcn3jjc71a2bv3, 314, 41);
                        gm_b16_8 t596 = GM_LD(gm_b16_8, (((uint8_t *)t594) + (uintptr_t)((uint64_t)1ULL) * 16));
                        gm_b16_8 t597 = GM_LD(gm_b16_8, ((uint8_t *)&t596));
                        gm_f_sys_src_5svsr6i((void *)&gm_l94, (void *)NULL, cx, NULL, t592, t593, t597);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t598 = (uint8_t)!((((uint8_t *)&gm_l94))[0] != 0);
                    if (t598) {
                    } else {
                        gm_t76123gfet73g t599 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l94) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t599);
                        goto gm_fdone;
                    }
                    gm_b16_8 t600 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l94) + 8));
                    gm_l55 = t600;
                    {
                        gm_b16_8 t601 = gm_l54;
                        gm_b16_8 t602 = gm_l55;
                        gm_node *pn603 = gm_find(cx, 2126215853);
                        if (!pn603) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 315, 29);
                        gm_ttln0d0hd70en t604;
                        t604 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_b16_8))pn603->clauses[19])(pn603->env, pn603->hctx, pn603, t601, t602);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        int64_t t605 = gm_f_sys_unit_nrsqciq(t604);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t605);
                        goto gm_fdone;
                    }
                }
gm_lb37:;
                {
                    uint8_t t606 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t607 = (uint8_t)(t606 == ((uint8_t)25ULL));
                    if (!t607) {
                        goto gm_lb38;
                    }
                    {
                        void * t608 = gm_l0;
                        gm_tl874t2atnlh3 t609 = gm_l1;
                        void * t610 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t611 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t611)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t611, gm_srcn3jjc71a2bv3, 318, 41);
                        gm_b16_8 t612 = GM_LD(gm_b16_8, (((uint8_t *)t610) + (uintptr_t)((uint64_t)0ULL) * 16));
                        gm_b16_8 t613 = GM_LD(gm_b16_8, ((uint8_t *)&t612));
                        gm_f_sys_src_5svsr6i((void *)&gm_l95, (void *)NULL, cx, NULL, t608, t609, t613);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t614 = (uint8_t)!((((uint8_t *)&gm_l95))[0] != 0);
                    if (t614) {
                    } else {
                        gm_t76123gfet73g t615 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l95) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t615);
                        goto gm_fdone;
                    }
                    gm_b16_8 t616 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l95) + 8));
                    gm_l56 = t616;
                    {
                        gm_b16_8 t617 = gm_l56;
                        void * t618 = gm_l0;
                        void * t619 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t620 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t620)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t620, gm_srcn3jjc71a2bv3, 319, 58);
                        gm_b16_8 t621 = GM_LD(gm_b16_8, (((uint8_t *)t619) + (uintptr_t)((uint64_t)1ULL) * 16));
                        gm_b16_8 t622 = GM_LD(gm_b16_8, ((uint8_t *)&t621));
                        unsigned __int128 t623 = gm_f_get_uint_bq8c4b7(t618, t622);
                        uint32_t t624 = (uint32_t)t623;
                        gm_node *pn625 = gm_find(cx, 2126215853);
                        if (!pn625) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 319, 29);
                        gm_ttln0d0hd70en t626;
                        t626 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8, uint32_t))pn625->clauses[20])(pn625->env, pn625->hctx, pn625, t617, t624);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        int64_t t627 = gm_f_sys_unit_nrsqciq(t626);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t627);
                        goto gm_fdone;
                    }
                }
gm_lb38:;
            }
            {
                {
                    uint8_t t628 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t629 = (uint8_t)(t628 == ((uint8_t)26ULL));
                    if (!t629) {
                        goto gm_lb39;
                    }
                    {
                        void * t630 = gm_l0;
                        gm_tl874t2atnlh3 t631 = gm_l1;
                        void * t632 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t633 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t633)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t633, gm_srcn3jjc71a2bv3, 322, 41);
                        gm_b16_8 t634 = GM_LD(gm_b16_8, (((uint8_t *)t632) + (uintptr_t)((uint64_t)0ULL) * 16));
                        gm_b16_8 t635 = GM_LD(gm_b16_8, ((uint8_t *)&t634));
                        gm_f_sys_src_5svsr6i((void *)&gm_l96, (void *)NULL, cx, NULL, t630, t631, t635);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t636 = (uint8_t)!((((uint8_t *)&gm_l96))[0] != 0);
                    if (t636) {
                    } else {
                        gm_t76123gfet73g t637 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l96) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t637);
                        goto gm_fdone;
                    }
                    gm_b16_8 t638 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l96) + 8));
                    gm_l57 = t638;
                    {
                        gm_b16_8 t639 = gm_l57;
                        void * t640 = gm_l0;
                        void * t641 = GM_LD(void *, ((uint8_t *)&gm_l3));
                        uint64_t t642 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t642)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t642, gm_srcn3jjc71a2bv3, 323, 62);
                        void * t643 = GM_LD(void *, (((uint8_t *)t641) + (uintptr_t)((uint64_t)1ULL) * 8));
                        gm_t88rcucg1b7uk t644 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t643) + 56));
                        void * t645 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t646 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t646)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t646, gm_srcn3jjc71a2bv3, 323, 74);
                        gm_b16_8 t647 = GM_LD(gm_b16_8, (((uint8_t *)t645) + (uintptr_t)((uint64_t)1ULL) * 16));
                        gm_b16_8 t648 = GM_LD(gm_b16_8, ((uint8_t *)&t647));
                        int64_t t649 = gm_f_sys_int_32e79ed(t640, GM_CONV(gm_csguankko4lmhd, t644), t648);
                        gm_node *pn650 = gm_find(cx, 2126215853);
                        if (!pn650) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 323, 29);
                        gm_ttln0d0hd70en t651;
                        t651 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8, int64_t))pn650->clauses[21])(pn650->env, pn650->hctx, pn650, t639, t649);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        int64_t t652 = gm_f_sys_unit_nrsqciq(t651);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t652);
                        goto gm_fdone;
                    }
                }
gm_lb39:;
                {
                    void * t653 = gm_l0;
                    gm_tl874t2atnlh3 t654 = gm_l1;
                    gm_b16_8 t655;
                    GM_ST(void *, (uint8_t *)&t655, (void *)(gm_sqn7kgtu67hrg));
                    GM_ST(uintptr_t, (uint8_t *)&t655 + 8, (uintptr_t)(41));
                    gm_t76123gfet73g t656;
                    gm_f_trap_qh84l9d((void *)&t656, (void *)NULL, cx, NULL, t653, t654, t655);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t656);
                    goto gm_fdone;
                }
gm_lb40:;
                gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 205, 5);
            }
        }
gm_lb3:;
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
extern __attribute__((aligned(4))) uint8_t gm_f_sys_has_0sego5l(uint32_t a0, uint32_t a1);
extern __attribute__((aligned(4))) int64_t gm_f_sys_int_32e79ed(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) int64_t gm_f_sys_kind_sjfdqbt(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_kind_sjfdqbt F;
    gm_t9qkh0rsooc8d gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 41u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t gm_l1;
    {
        {
            uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)0ULL));
            if (!t8) {
                goto gm_lb2;
            }
            gm_l1 = ((int64_t)0LL);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)1ULL));
            if (!t10) {
                goto gm_lb3;
            }
            gm_l1 = ((int64_t)1LL);
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t12 = (uint8_t)(t11 == ((uint8_t)2ULL));
            if (!t12) {
                goto gm_lb4;
            }
            gm_l1 = ((int64_t)2LL);
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)3ULL));
            if (!t14) {
                goto gm_lb5;
            }
            gm_l1 = ((int64_t)3LL);
            goto gm_lb1;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 96, 42);
    }
gm_lb1:;
    int64_t t15 = gm_l1;
    F.ret = t15;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_sys_net_aff7h5l(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_sys_net_aff7h5l F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t9qkh0rsooc8d gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    gm_b16_8 gm_l4; /* vals */
    int64_t gm_l5; /* h */
    gm_toe5fmr3vvf4d gm_l9; /* err */
    gm_tm98ap4112hee gm_l10; /* s */
    int64_t gm_l11; /* handle */
    gm_toe5fmr3vvf4d gm_l15; /* err */
    gm_tm98ap4112hee gm_l16; /* l */
    int64_t gm_l17; /* handle */
    gm_toe5fmr3vvf4d gm_l20; /* err */
    uint16_t gm_l21; /* port */
    gm_toe5fmr3vvf4d gm_l24; /* err */
    gm_tm98ap4112hee gm_l25; /* s */
    int64_t gm_l26; /* handle */
    uint64_t gm_l38; /* recv_ns */
    uint64_t gm_l39; /* send_ns */
    GM_STACK_CHECK(&F, 4000u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_td0ept12e7q8b gm_l47;
    gm_toqti1ubjnfd1 gm_l48;
    gm_tftb1odc739t5 gm_l49;
    gm_td0ept12e7q8b gm_l50;
    gm_tftb1odc739t5 gm_l51;
    gm_tppo2hmut88dq gm_l52;
    gm_tppo2hmut88dq gm_l53;
    gm_toqti1ubjnfd1 gm_l54;
    gm_tftb1odc739t5 gm_l55;
    gm_td0ept12e7q8b gm_l56;
    gm_tppo2hmut88dq gm_l57;
    gm_t0jbubompi7qd gm_l58;
    gm_t7eae7gj5k2n2 gm_l59;
    gm_td0ept12e7q8b gm_l60;
    gm_tppo2hmut88dq gm_l61;
    gm_t0jbubompi7qd gm_l62;
    gm_t7eae7gj5k2n2 gm_l63;
    gm_tppo2hmut88dq gm_l64;
    gm_tppo2hmut88dq gm_l65;
    int64_t gm_l66;
    gm_tppo2hmut88dq gm_l67;
    gm_tppo2hmut88dq gm_l68;
    gm_tppo2hmut88dq gm_l69;
    gm_td0ept12e7q8b gm_l70;
    {
        gm_l5 = ((int64_t)-1LL);
        gm_t9qkh0rsooc8d t22 = gm_l2;
        gm_t9qkh0rsooc8d t23;
        GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)27ULL));
        gm_t9qkh0rsooc8d t24;
        GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)28ULL));
        gm_t0am46iohlpol t25;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t25 + 0), t23);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t25 + 1), t24);
        uint8_t t26 = 0;
        if (gm_eqtje7cegoml(((uint8_t *)&t25) + 0, ((uint8_t *)&t22))) t26 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t25) + 1, ((uint8_t *)&t22))) t26 = 1;
        uint8_t t27 = (uint8_t)(!t26);
        if (t27) {
            void * t28 = gm_l0;
            void * t29 = GM_LD(void *, ((uint8_t *)&gm_l3));
            uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t30)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t30, gm_srcn3jjc71a2bv3, 332, 70);
            void * t31 = GM_LD(void *, (((uint8_t *)t29) + (uintptr_t)((uint64_t)0ULL) * 8));
            gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t31) + 56));
            void * t33 = GM_LD(void *, ((uint8_t *)&gm_l4));
            uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t34)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t34, gm_srcn3jjc71a2bv3, 332, 82);
            gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)t33) + (uintptr_t)((uint64_t)0ULL) * 16));
            gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
            int64_t t37 = gm_f_sys_int_32e79ed(t28, GM_CONV(gm_csguankko4lmhd, t32), t36);
            gm_l5 = t37;
        }
        {
            {
                {
                    uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t39 = (uint8_t)(t38 == ((uint8_t)27ULL));
                    if (!t39) {
                        goto gm_lb2;
                    }
                    {
                        void * t40 = gm_l0;
                        gm_tl874t2atnlh3 t41 = gm_l1;
                        void * t42 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t43)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t43, gm_srcn3jjc71a2bv3, 335, 41);
                        gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)t42) + (uintptr_t)((uint64_t)0ULL) * 16));
                        gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
                        gm_f_sys_src_5svsr6i((void *)&gm_l47, (void *)NULL, cx, NULL, t40, t41, t45);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t46 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
                    if (t46) {
                    } else {
                        gm_t76123gfet73g t47 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l47) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t47);
                        goto gm_fdone;
                    }
                    gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l47) + 8));
                    F.l6 = t48;
                    void * t49 = gm_l0;
                    void * t50 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t51)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t51, gm_srcn3jjc71a2bv3, 336, 32);
                    gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)t50) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
                    unsigned __int128 t54 = gm_f_get_uint_bq8c4b7(t49, t53);
                    uint16_t t55 = (uint16_t)t54;
                    F.l7 = t55;
                    gm_toe5fmr3vvf4d t56;
                    GM_ST(uint8_t, (uint8_t *)&t56 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t56 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t56 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)12ULL));
                    gm_t62ebbi2i5rkd t57;
                    GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t57 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
                    ((uint8_t *)&t57)[0] = 1;
                    GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t57 + 8), t56);
                    F.l8 = t57;
                    void * t58 = gm_l0;
                    uint64_t t59 = GM_LD(uint64_t, ((((uint8_t *)t58) + 1672) + 8));
                    uint8_t t60 = (t59 > ((uint64_t)1ULL));
                    if (t60) {
                        {
                            void * t61 = gm_l0;
                            gm_tl874t2atnlh3 t62 = gm_l1;
                            gm_b16_8 t63 = F.l6;
                            gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&t63));
                            uint16_t t65 = F.l7;
                            gm_tvaas22dl64e0 t66;
                            GM_ST(uint16_t, (uint8_t *)&t66 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t66 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t66 + 0), t64);
                            GM_ST(uint16_t, ((uint8_t *)&t66 + 16), t65);
                            gm_tnp3hpcm9uhoi t67;
                            GM_ST(uint8_t, (uint8_t *)&t67 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t67 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t67 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)2ULL));
                            GM_ST(gm_tvaas22dl64e0, ((uint8_t *)&t67 + 8), t66);
                            gm_f_socket_on_host_thread_u8c2vg5((void *)&gm_l48, (void *)NULL, cx, NULL, t61, t62, &t67);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t68 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                        if (t68) {
                        } else {
                            gm_t76123gfet73g t69 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l48) + 8));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t69);
                            goto gm_fdone;
                        }
                        gm_t62ebbi2i5rkd t70 = GM_LD(gm_t62ebbi2i5rkd, (((uint8_t *)&gm_l48) + 8));
                        F.l8 = t70;
                    } else {
                        gm_closure t71 = ((gm_closure){(void *)gm_f_fn_aqcn8lk, (void *)&F.h, cx});
                        gm_f_defaults_p077jgf(t71);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t72 = (uint8_t)!((((uint8_t *)&F.l8))[0] != 0);
                    if (t72) {
                        gm_tm98ap4112hee t73 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&F.l8) + 8));
                        gm_l10 = t73;
                    } else {
                        gm_toe5fmr3vvf4d t74 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&F.l8) + 8));
                        gm_l9 = t74;
                        gm_toe5fmr3vvf4d t75 = gm_l9;
                        int64_t t76 = gm_f_sys_code_cjtlfo1(t75);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t76);
                        goto gm_fdone;
                    }
                    void * t77 = gm_l0;
                    void * t78 = (void *)(((uint8_t *)t77) + 1368);
                    int64_t t79 = gm_f_sys_next_handle_r35ek81(t78);
                    gm_l11 = t79;
                    void * t80 = gm_l0;
                    void * t81 = (void *)((((uint8_t *)t80) + 1368) + 112);
                    int64_t t82 = gm_l11;
                    gm_tm98ap4112hee t83 = gm_l10;
                    gm_tftb1odc739t5 t84 = gm_f_put_qkbvor7(t81, t82, GM_CONV(gm_csfq64m1m1rt6m, t83));
                    gm_l49 = t84;
                    uint8_t t85 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
                    if (!t85) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l49) + 1, gm_srcn3jjc71a2bv3, 345, 13);
                    {
                        int64_t t86 = gm_l11;
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t86);
                        goto gm_fdone;
                    }
                }
gm_lb2:;
            }
            {
                {
                    uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t88 = (uint8_t)(t87 == ((uint8_t)28ULL));
                    if (!t88) {
                        goto gm_lb3;
                    }
                    {
                        void * t89 = gm_l0;
                        gm_tl874t2atnlh3 t90 = gm_l1;
                        void * t91 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t92 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t92)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t92, gm_srcn3jjc71a2bv3, 349, 41);
                        gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)t91) + (uintptr_t)((uint64_t)0ULL) * 16));
                        gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
                        gm_f_sys_src_5svsr6i((void *)&gm_l50, (void *)NULL, cx, NULL, t89, t90, t94);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t95 = (uint8_t)!((((uint8_t *)&gm_l50))[0] != 0);
                    if (t95) {
                    } else {
                        gm_t76123gfet73g t96 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l50) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t96);
                        goto gm_fdone;
                    }
                    gm_b16_8 t97 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l50) + 8));
                    F.l12 = t97;
                    void * t98 = gm_l0;
                    void * t99 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t100 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t100)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t100, gm_srcn3jjc71a2bv3, 350, 32);
                    gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)t99) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&t101));
                    unsigned __int128 t103 = gm_f_get_uint_bq8c4b7(t98, t102);
                    uint16_t t104 = (uint16_t)t103;
                    F.l13 = t104;
                    gm_toe5fmr3vvf4d t105;
                    GM_ST(uint8_t, (uint8_t *)&t105 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t105 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t105 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t105 + 0), ((uint8_t)12ULL));
                    gm_t62ebbi2i5rkd t106;
                    GM_ST(uint8_t, (uint8_t *)&t106 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t106 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t106 + 4, 0);
                    ((uint8_t *)&t106)[0] = 1;
                    GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t106 + 8), t105);
                    F.l14 = t106;
                    gm_closure t107 = ((gm_closure){(void *)gm_f_fn_cijad0r, (void *)&F.h, cx});
                    gm_f_defaults_p077jgf(t107);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t108 = (uint8_t)!((((uint8_t *)&F.l14))[0] != 0);
                    if (t108) {
                        gm_tm98ap4112hee t109 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&F.l14) + 8));
                        gm_l16 = t109;
                    } else {
                        gm_toe5fmr3vvf4d t110 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&F.l14) + 8));
                        gm_l15 = t110;
                        gm_toe5fmr3vvf4d t111 = gm_l15;
                        int64_t t112 = gm_f_sys_code_cjtlfo1(t111);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t112);
                        goto gm_fdone;
                    }
                    void * t113 = gm_l0;
                    void * t114 = (void *)(((uint8_t *)t113) + 1368);
                    int64_t t115 = gm_f_sys_next_handle_r35ek81(t114);
                    gm_l17 = t115;
                    void * t116 = gm_l0;
                    void * t117 = (void *)((((uint8_t *)t116) + 1368) + 168);
                    int64_t t118 = gm_l17;
                    gm_tm98ap4112hee t119 = gm_l16;
                    gm_tftb1odc739t5 t120 = gm_f_put_ha6815g(t117, t118, GM_CONV(gm_csfq64m1m1rt6m, t119));
                    gm_l51 = t120;
                    uint8_t t121 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
                    if (!t121) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l51) + 1, gm_srcn3jjc71a2bv3, 355, 13);
                    {
                        int64_t t122 = gm_l17;
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t122);
                        goto gm_fdone;
                    }
                }
gm_lb3:;
                {
                    uint8_t t123 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t124 = (uint8_t)(t123 == ((uint8_t)29ULL));
                    if (!t124) {
                        goto gm_lb4;
                    }
                    void * t125 = gm_l0;
                    void * t126 = (void *)((((uint8_t *)t125) + 1368) + 168);
                    int64_t t127 = gm_l5;
                    gm_tppo2hmut88dq t128 = gm_f_get_8o0rtd2(t126, t127);
                    gm_l52 = t128;
                    uint8_t t129 = (uint8_t)((((uint8_t *)&gm_l52))[8] != 0);
                    if (t129) {
                        gm_tm98ap4112hee t130 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l52));
                        F.l18 = t130;
                    } else {
                        int64_t t131 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t131);
                        goto gm_fdone;
                    }
                    gm_toe5fmr3vvf4d t132;
                    GM_ST(uint8_t, (uint8_t *)&t132 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t132 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t132 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t132 + 0), ((uint8_t)12ULL));
                    gm_tvcqs9u9iqpct t133;
                    GM_ST(uint8_t, (uint8_t *)&t133 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t133 + 2, 0);
                    ((uint8_t *)&t133)[0] = 1;
                    GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t133 + 4), t132);
                    F.l19 = t133;
                    gm_closure t134 = ((gm_closure){(void *)gm_f_fn_mslt7ot, (void *)&F.h, cx});
                    gm_f_defaults_p077jgf(t134);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t135 = (uint8_t)!((((uint8_t *)&F.l19))[0] != 0);
                    if (t135) {
                        uint16_t t136 = GM_LD(uint16_t, (((uint8_t *)&F.l19) + 4));
                        gm_l21 = t136;
                    } else {
                        gm_toe5fmr3vvf4d t137 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&F.l19) + 4));
                        gm_l20 = t137;
                        gm_toe5fmr3vvf4d t138 = gm_l20;
                        int64_t t139 = gm_f_sys_code_cjtlfo1(t138);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t139);
                        goto gm_fdone;
                    }
                    uint16_t t140 = gm_l21;
                    int64_t t141 = (int64_t)t140;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t141);
                    goto gm_fdone;
                }
gm_lb4:;
            }
            {
                {
                    uint8_t t142 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t143 = (uint8_t)(t142 == ((uint8_t)30ULL));
                    if (!t143) {
                        goto gm_lb5;
                    }
                    void * t144 = gm_l0;
                    void * t145 = (void *)((((uint8_t *)t144) + 1368) + 168);
                    int64_t t146 = gm_l5;
                    gm_tppo2hmut88dq t147 = gm_f_get_8o0rtd2(t145, t146);
                    gm_l53 = t147;
                    uint8_t t148 = (uint8_t)((((uint8_t *)&gm_l53))[8] != 0);
                    if (t148) {
                        gm_tm98ap4112hee t149 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l53));
                        F.l22 = t149;
                    } else {
                        int64_t t150 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t150);
                        goto gm_fdone;
                    }
                    gm_toe5fmr3vvf4d t151;
                    GM_ST(uint8_t, (uint8_t *)&t151 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t151 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t151 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t151 + 0), ((uint8_t)12ULL));
                    gm_t62ebbi2i5rkd t152;
                    GM_ST(uint8_t, (uint8_t *)&t152 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t152 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t152 + 4, 0);
                    ((uint8_t *)&t152)[0] = 1;
                    GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t152 + 8), t151);
                    F.l23 = t152;
                    void * t153 = gm_l0;
                    uint64_t t154 = GM_LD(uint64_t, ((((uint8_t *)t153) + 1672) + 8));
                    uint8_t t155 = (t154 > ((uint64_t)1ULL));
                    if (t155) {
                        {
                            void * t156 = gm_l0;
                            gm_tl874t2atnlh3 t157 = gm_l1;
                            gm_tm98ap4112hee t158 = F.l22;
                            gm_tnp3hpcm9uhoi t159;
                            GM_ST(uint8_t, (uint8_t *)&t159 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t159 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t159 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t159 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t159 + 24, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t159 + 0), ((uint8_t)3ULL));
                            GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t159 + 8), t158);
                            gm_f_socket_on_host_thread_u8c2vg5((void *)&gm_l54, (void *)NULL, cx, NULL, t156, t157, &t159);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t160 = (uint8_t)!((((uint8_t *)&gm_l54))[0] != 0);
                        if (t160) {
                        } else {
                            gm_t76123gfet73g t161 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l54) + 8));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t161);
                            goto gm_fdone;
                        }
                        gm_t62ebbi2i5rkd t162 = GM_LD(gm_t62ebbi2i5rkd, (((uint8_t *)&gm_l54) + 8));
                        F.l23 = t162;
                    } else {
                        gm_closure t163 = ((gm_closure){(void *)gm_f_fn_rg62rr4, (void *)&F.h, cx});
                        gm_f_defaults_p077jgf(t163);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t164 = (uint8_t)!((((uint8_t *)&F.l23))[0] != 0);
                    if (t164) {
                        gm_tm98ap4112hee t165 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&F.l23) + 8));
                        gm_l25 = t165;
                    } else {
                        gm_toe5fmr3vvf4d t166 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&F.l23) + 8));
                        gm_l24 = t166;
                        gm_toe5fmr3vvf4d t167 = gm_l24;
                        int64_t t168 = gm_f_sys_code_cjtlfo1(t167);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t168);
                        goto gm_fdone;
                    }
                    void * t169 = gm_l0;
                    void * t170 = (void *)(((uint8_t *)t169) + 1368);
                    int64_t t171 = gm_f_sys_next_handle_r35ek81(t170);
                    gm_l26 = t171;
                    void * t172 = gm_l0;
                    void * t173 = (void *)((((uint8_t *)t172) + 1368) + 112);
                    int64_t t174 = gm_l26;
                    gm_tm98ap4112hee t175 = gm_l25;
                    gm_tftb1odc739t5 t176 = gm_f_put_qkbvor7(t173, t174, GM_CONV(gm_csfq64m1m1rt6m, t175));
                    gm_l55 = t176;
                    uint8_t t177 = (uint8_t)!((((uint8_t *)&gm_l55))[0] != 0);
                    if (!t177) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l55) + 1, gm_srcn3jjc71a2bv3, 375, 13);
                    int64_t t178 = gm_l26;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t178);
                    goto gm_fdone;
                }
gm_lb5:;
                {
                    uint8_t t179 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t180 = (uint8_t)(t179 == ((uint8_t)31ULL));
                    if (!t180) {
                        goto gm_lb6;
                    }
                    {
                        void * t181 = gm_l0;
                        gm_tl874t2atnlh3 t182 = gm_l1;
                        void * t183 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t184 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t184)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t184, gm_srcn3jjc71a2bv3, 379, 41);
                        gm_b16_8 t185 = GM_LD(gm_b16_8, (((uint8_t *)t183) + (uintptr_t)((uint64_t)1ULL) * 16));
                        gm_b16_8 t186 = GM_LD(gm_b16_8, ((uint8_t *)&t185));
                        gm_f_sys_src_5svsr6i((void *)&gm_l56, (void *)NULL, cx, NULL, t181, t182, t186);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t187 = (uint8_t)!((((uint8_t *)&gm_l56))[0] != 0);
                    if (t187) {
                    } else {
                        gm_t76123gfet73g t188 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l56) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t188);
                        goto gm_fdone;
                    }
                    gm_b16_8 t189 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l56) + 8));
                    F.l27 = t189;
                    void * t190 = gm_l0;
                    void * t191 = (void *)((((uint8_t *)t190) + 1368) + 112);
                    int64_t t192 = gm_l5;
                    gm_tppo2hmut88dq t193 = gm_f_get_gej4j8d(t191, t192);
                    gm_l57 = t193;
                    uint8_t t194 = (uint8_t)((((uint8_t *)&gm_l57))[8] != 0);
                    if (t194) {
                        gm_tm98ap4112hee t195 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l57));
                        F.l28 = t195;
                    } else {
                        int64_t t196 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t196);
                        goto gm_fdone;
                    }
                    gm_toe5fmr3vvf4d t197;
                    GM_ST(uint8_t, (uint8_t *)&t197 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t197 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t197 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t197 + 0), ((uint8_t)12ULL));
                    gm_thf5kr0is9u9i t198;
                    GM_ST(uint8_t, (uint8_t *)&t198 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t198 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t198 + 4, 0);
                    ((uint8_t *)&t198)[0] = 1;
                    GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t198 + 8), t197);
                    F.l29 = t198;
                    void * t199 = gm_l0;
                    uint64_t t200 = GM_LD(uint64_t, ((((uint8_t *)t199) + 1672) + 8));
                    uint8_t t201 = (t200 > ((uint64_t)1ULL));
                    if (t201) {
                        {
                            void * t202 = gm_l0;
                            gm_tl874t2atnlh3 t203 = gm_l1;
                            gm_tm98ap4112hee t204 = F.l28;
                            gm_b16_8 t205 = F.l27;
                            gm_t3olsle24rveu t206;
                            GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t206 + 0), t204);
                            GM_ST(gm_b16_8, ((uint8_t *)&t206 + 8), t205);
                            gm_tnp3hpcm9uhoi t207;
                            GM_ST(uint8_t, (uint8_t *)&t207 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t207 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t207 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t207 + 0), ((uint8_t)4ULL));
                            GM_ST(gm_t3olsle24rveu, ((uint8_t *)&t207 + 8), t206);
                            gm_f_on_host_thread_kmegc07((void *)&gm_l58, (void *)NULL, cx, NULL, t202, t203, &t207);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t208 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
                        if (t208) {
                        } else {
                            gm_t76123gfet73g t209 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l58) + 8));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t209);
                            goto gm_fdone;
                        }
                        {
                            memmove(((uint8_t *)&gm_l59), (((uint8_t *)&gm_l58) + 8), 40);
                        }
                        gm_thf5kr0is9u9i t210 = GM_LD(gm_thf5kr0is9u9i, ((uint8_t *)&gm_l59));
                        F.l29 = t210;
                    } else {
                        gm_closure t211 = ((gm_closure){(void *)gm_f_fn_tlii61s, (void *)&F.h, cx});
                        gm_f_defaults_p077jgf(t211);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        gm_thf5kr0is9u9i t212 = F.l29;
                        int64_t t213 = gm_f_sys_net_count_46l7ueq(t212);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t213);
                        goto gm_fdone;
                    }
                }
gm_lb6:;
            }
            {
                {
                    uint8_t t214 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t215 = (uint8_t)(t214 == ((uint8_t)32ULL));
                    if (!t215) {
                        goto gm_lb7;
                    }
                    {
                        void * t216 = gm_l0;
                        gm_tl874t2atnlh3 t217 = gm_l1;
                        void * t218 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t219 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t219)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t219, gm_srcn3jjc71a2bv3, 390, 41);
                        gm_b16_8 t220 = GM_LD(gm_b16_8, (((uint8_t *)t218) + (uintptr_t)((uint64_t)1ULL) * 16));
                        gm_b16_8 t221 = GM_LD(gm_b16_8, ((uint8_t *)&t220));
                        gm_f_sys_dest_rtrb14h((void *)&gm_l60, (void *)NULL, cx, NULL, t216, t217, t221);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t222 = (uint8_t)!((((uint8_t *)&gm_l60))[0] != 0);
                    if (t222) {
                    } else {
                        gm_t76123gfet73g t223 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l60) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t223);
                        goto gm_fdone;
                    }
                    gm_b16_8 t224 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l60) + 8));
                    F.l30 = t224;
                    void * t225 = gm_l0;
                    void * t226 = (void *)((((uint8_t *)t225) + 1368) + 112);
                    int64_t t227 = gm_l5;
                    gm_tppo2hmut88dq t228 = gm_f_get_gej4j8d(t226, t227);
                    gm_l61 = t228;
                    uint8_t t229 = (uint8_t)((((uint8_t *)&gm_l61))[8] != 0);
                    if (t229) {
                        gm_tm98ap4112hee t230 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l61));
                        F.l31 = t230;
                    } else {
                        int64_t t231 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t231);
                        goto gm_fdone;
                    }
                    gm_toe5fmr3vvf4d t232;
                    GM_ST(uint8_t, (uint8_t *)&t232 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t232 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t232 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t232 + 0), ((uint8_t)12ULL));
                    gm_thf5kr0is9u9i t233;
                    GM_ST(uint8_t, (uint8_t *)&t233 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t233 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t233 + 4, 0);
                    ((uint8_t *)&t233)[0] = 1;
                    GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t233 + 8), t232);
                    F.l32 = t233;
                    void * t234 = gm_l0;
                    uint64_t t235 = GM_LD(uint64_t, ((((uint8_t *)t234) + 1672) + 8));
                    uint8_t t236 = (t235 > ((uint64_t)1ULL));
                    if (t236) {
                        {
                            void * t237 = gm_l0;
                            gm_tl874t2atnlh3 t238 = gm_l1;
                            gm_tm98ap4112hee t239 = F.l31;
                            gm_b16_8 t240 = F.l30;
                            gm_t3olsle24rveu t241;
                            GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t241 + 0), t239);
                            GM_ST(gm_b16_8, ((uint8_t *)&t241 + 8), t240);
                            gm_tnp3hpcm9uhoi t242;
                            GM_ST(uint8_t, (uint8_t *)&t242 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t242 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t242 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t242 + 0), ((uint8_t)5ULL));
                            GM_ST(gm_t3olsle24rveu, ((uint8_t *)&t242 + 8), t241);
                            gm_f_on_host_thread_kmegc07((void *)&gm_l62, (void *)NULL, cx, NULL, t237, t238, &t242);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t243 = (uint8_t)!((((uint8_t *)&gm_l62))[0] != 0);
                        if (t243) {
                        } else {
                            gm_t76123gfet73g t244 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l62) + 8));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t244);
                            goto gm_fdone;
                        }
                        {
                            memmove(((uint8_t *)&gm_l63), (((uint8_t *)&gm_l62) + 8), 40);
                        }
                        gm_thf5kr0is9u9i t245 = GM_LD(gm_thf5kr0is9u9i, ((uint8_t *)&gm_l63));
                        F.l32 = t245;
                    } else {
                        gm_closure t246 = ((gm_closure){(void *)gm_f_fn_ducmvdu, (void *)&F.h, cx});
                        gm_f_defaults_p077jgf(t246);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        gm_thf5kr0is9u9i t247 = F.l32;
                        int64_t t248 = gm_f_sys_net_count_46l7ueq(t247);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t248);
                        goto gm_fdone;
                    }
                }
gm_lb7:;
                {
                    uint8_t t249 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t250 = (uint8_t)(t249 == ((uint8_t)33ULL));
                    if (!t250) {
                        goto gm_lb8;
                    }
                    {
                        {
                            void * t251 = gm_l0;
                            void * t252 = (void *)((((uint8_t *)t251) + 1368) + 112);
                            int64_t t253 = gm_l5;
                            gm_tppo2hmut88dq t254 = gm_f_remove_7bvuc21(t252, t253);
                            gm_l64 = t254;
                            uint8_t t255 = (uint8_t)((((uint8_t *)&gm_l64))[8] != 0);
                            if (!t255) {
                                goto gm_lb10;
                            }
                            gm_tm98ap4112hee t256 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l64));
                            F.l33 = t256;
                            gm_closure t257 = ((gm_closure){(void *)gm_f_fn_r8m0qjr, (void *)&F.h, cx});
                            gm_f_defaults_p077jgf(t257);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                            GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), ((int64_t)0LL));
                            goto gm_fdone;
                        }
gm_lb10:;
                    }
gm_lb9:;
                    {
                        {
                            void * t258 = gm_l0;
                            void * t259 = (void *)((((uint8_t *)t258) + 1368) + 168);
                            int64_t t260 = gm_l5;
                            gm_tppo2hmut88dq t261 = gm_f_remove_3j2d0bb(t259, t260);
                            gm_l65 = t261;
                            uint8_t t262 = (uint8_t)((((uint8_t *)&gm_l65))[8] != 0);
                            if (!t262) {
                                goto gm_lb12;
                            }
                            gm_tm98ap4112hee t263 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l65));
                            F.l34 = t263;
                            gm_closure t264 = ((gm_closure){(void *)gm_f_fn_1lff729, (void *)&F.h, cx});
                            gm_f_defaults_p077jgf(t264);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                            GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), ((int64_t)0LL));
                            goto gm_fdone;
                        }
gm_lb12:;
                    }
gm_lb11:;
                    int64_t t265 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t265);
                    goto gm_fdone;
                }
gm_lb8:;
            }
            {
                {
                    uint8_t t266 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t267 = (uint8_t)(t266 == ((uint8_t)34ULL));
                    if (!t267) {
                        goto gm_lb13;
                    }
                    void * t268 = gm_l0;
                    void * t269 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t270 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t270)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t270, gm_srcn3jjc71a2bv3, 412, 50);
                    void * t271 = GM_LD(void *, (((uint8_t *)t269) + (uintptr_t)((uint64_t)1ULL) * 8));
                    gm_t88rcucg1b7uk t272 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t271) + 56));
                    void * t273 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t274 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t274)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t274, gm_srcn3jjc71a2bv3, 412, 62);
                    gm_b16_8 t275 = GM_LD(gm_b16_8, (((uint8_t *)t273) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t276 = GM_LD(gm_b16_8, ((uint8_t *)&t275));
                    int64_t t277 = gm_f_sys_int_32e79ed(t268, GM_CONV(gm_csguankko4lmhd, t272), t276);
                    gm_l66 = t277;
                    {
                        {
                            int64_t t278 = gm_l66;
                            uint8_t t279 = ((t278 == ((int64_t)0LL)));
                            if (!t279) {
                                goto gm_lb15;
                            }
                            gm_t9qkh0rsooc8d t280;
                            GM_ST(uint8_t, ((uint8_t *)&t280 + 0), ((uint8_t)0ULL));
                            F.l35 = t280;
                            goto gm_lb14;
                        }
gm_lb15:;
                        {
                            int64_t t281 = gm_l66;
                            uint8_t t282 = ((t281 == ((int64_t)1LL)));
                            if (!t282) {
                                goto gm_lb16;
                            }
                            gm_t9qkh0rsooc8d t283;
                            GM_ST(uint8_t, ((uint8_t *)&t283 + 0), ((uint8_t)1ULL));
                            F.l35 = t283;
                            goto gm_lb14;
                        }
gm_lb16:;
                        {
                            gm_t9qkh0rsooc8d t284;
                            GM_ST(uint8_t, ((uint8_t *)&t284 + 0), ((uint8_t)2ULL));
                            F.l35 = t284;
                            goto gm_lb14;
                        }
gm_lb17:;
                        gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 412, 33);
                    }
gm_lb14:;
                    void * t285 = gm_l0;
                    void * t286 = (void *)((((uint8_t *)t285) + 1368) + 112);
                    int64_t t287 = gm_l5;
                    gm_tppo2hmut88dq t288 = gm_f_get_gej4j8d(t286, t287);
                    gm_l67 = t288;
                    uint8_t t289 = (uint8_t)((((uint8_t *)&gm_l67))[8] != 0);
                    if (t289) {
                        gm_tm98ap4112hee t290 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l67));
                        F.l36 = t290;
                    } else {
                        int64_t t291 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t291);
                        goto gm_fdone;
                    }
                    gm_toe5fmr3vvf4d t292;
                    GM_ST(uint8_t, (uint8_t *)&t292 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t292 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t292 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t292 + 0), ((uint8_t)12ULL));
                    gm_ttln0d0hd70en t293;
                    GM_ST(uint8_t, (uint8_t *)&t293 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t293 + 2, 0);
                    ((uint8_t *)&t293)[0] = 1;
                    GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t293 + 4), t292);
                    F.l37 = t293;
                    gm_closure t294 = ((gm_closure){(void *)gm_f_fn_vhnrvmh, (void *)&F.h, cx});
                    gm_f_defaults_p077jgf(t294);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_ttln0d0hd70en t295 = F.l37;
                    int64_t t296 = gm_f_sys_unit_nrsqciq(t295);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t296);
                    goto gm_fdone;
                }
gm_lb13:;
                {
                    uint8_t t297 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t298 = (uint8_t)(t297 == ((uint8_t)35ULL));
                    if (!t298) {
                        goto gm_lb18;
                    }
                    void * t299 = gm_l0;
                    void * t300 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t301 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t301)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t301, gm_srcn3jjc71a2bv3, 423, 35);
                    gm_b16_8 t302 = GM_LD(gm_b16_8, (((uint8_t *)t300) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t303 = GM_LD(gm_b16_8, ((uint8_t *)&t302));
                    unsigned __int128 t304 = gm_f_get_uint_bq8c4b7(t299, t303);
                    uint64_t t305 = (uint64_t)t304;
                    gm_l38 = t305;
                    void * t306 = gm_l0;
                    void * t307 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t308 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t308)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t308, gm_srcn3jjc71a2bv3, 424, 35);
                    gm_b16_8 t309 = GM_LD(gm_b16_8, (((uint8_t *)t307) + (uintptr_t)((uint64_t)2ULL) * 16));
                    gm_b16_8 t310 = GM_LD(gm_b16_8, ((uint8_t *)&t309));
                    unsigned __int128 t311 = gm_f_get_uint_bq8c4b7(t306, t310);
                    uint64_t t312 = (uint64_t)t311;
                    gm_l39 = t312;
                    void * t313 = gm_l0;
                    void * t314 = (void *)((((uint8_t *)t313) + 1368) + 112);
                    int64_t t315 = gm_l5;
                    gm_tppo2hmut88dq t316 = gm_f_get_gej4j8d(t314, t315);
                    gm_l68 = t316;
                    uint8_t t317 = (uint8_t)((((uint8_t *)&gm_l68))[8] != 0);
                    if (t317) {
                        gm_tm98ap4112hee t318 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l68));
                        F.l40 = t318;
                    } else {
                        int64_t t319 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t319);
                        goto gm_fdone;
                    }
                    uint64_t t320 = gm_l38;
                    uint8_t t321 = (t320 > ((uint64_t)0ULL));
                    if (t321) {
                        uint64_t t322 = gm_l38;
                        gm_t4ce0jd3tdro4 t323;
                        GM_ST(uint64_t, ((uint8_t *)&t323 + 0), t322);
                        gm_t6ie8iajlnpt1 t324;
                        GM_ST(uint8_t, (uint8_t *)&t324 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t324 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t324 + 12, 0);
                        GM_ST(gm_t4ce0jd3tdro4, ((uint8_t *)&t324), t323);
                        ((uint8_t *)&t324)[8] = 1;
                        F.l41 = t324;
                    } else {
                        gm_t6ie8iajlnpt1 t325;
                        memset(&t325, 0, sizeof t325);
                        F.l41 = t325;
                    }
                    uint64_t t326 = gm_l39;
                    uint8_t t327 = (t326 > ((uint64_t)0ULL));
                    if (t327) {
                        uint64_t t328 = gm_l39;
                        gm_t4ce0jd3tdro4 t329;
                        GM_ST(uint64_t, ((uint8_t *)&t329 + 0), t328);
                        gm_t6ie8iajlnpt1 t330;
                        GM_ST(uint8_t, (uint8_t *)&t330 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t330 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t330 + 12, 0);
                        GM_ST(gm_t4ce0jd3tdro4, ((uint8_t *)&t330), t329);
                        ((uint8_t *)&t330)[8] = 1;
                        F.l42 = t330;
                    } else {
                        gm_t6ie8iajlnpt1 t331;
                        memset(&t331, 0, sizeof t331);
                        F.l42 = t331;
                    }
                    gm_toe5fmr3vvf4d t332;
                    GM_ST(uint8_t, (uint8_t *)&t332 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t332 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t332 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t332 + 0), ((uint8_t)12ULL));
                    gm_ttln0d0hd70en t333;
                    GM_ST(uint8_t, (uint8_t *)&t333 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t333 + 2, 0);
                    ((uint8_t *)&t333)[0] = 1;
                    GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t333 + 4), t332);
                    F.l43 = t333;
                    gm_closure t334 = ((gm_closure){(void *)gm_f_fn_j9i1sr8, (void *)&F.h, cx});
                    gm_f_defaults_p077jgf(t334);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_ttln0d0hd70en t335 = F.l43;
                    int64_t t336 = gm_f_sys_unit_nrsqciq(t335);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t336);
                    goto gm_fdone;
                }
gm_lb18:;
            }
            {
                {
                    uint8_t t337 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t338 = (uint8_t)(t337 == ((uint8_t)36ULL));
                    if (!t338) {
                        goto gm_lb19;
                    }
                    void * t339 = gm_l0;
                    void * t340 = (void *)((((uint8_t *)t339) + 1368) + 112);
                    int64_t t341 = gm_l5;
                    gm_tppo2hmut88dq t342 = gm_f_get_gej4j8d(t340, t341);
                    gm_l69 = t342;
                    uint8_t t343 = (uint8_t)((((uint8_t *)&gm_l69))[8] != 0);
                    if (t343) {
                        gm_tm98ap4112hee t344 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l69));
                        F.l44 = t344;
                    } else {
                        int64_t t345 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t345);
                        goto gm_fdone;
                    }
                    {
                        void * t346 = gm_l0;
                        gm_tl874t2atnlh3 t347 = gm_l1;
                        void * t348 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t349 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t349)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t349, gm_srcn3jjc71a2bv3, 434, 41);
                        gm_b16_8 t350 = GM_LD(gm_b16_8, (((uint8_t *)t348) + (uintptr_t)((uint64_t)1ULL) * 16));
                        gm_b16_8 t351 = GM_LD(gm_b16_8, ((uint8_t *)&t350));
                        gm_f_sys_dest_rtrb14h((void *)&gm_l70, (void *)NULL, cx, NULL, t346, t347, t351);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t352 = (uint8_t)!((((uint8_t *)&gm_l70))[0] != 0);
                    if (t352) {
                    } else {
                        gm_t76123gfet73g t353 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l70) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t353);
                        goto gm_fdone;
                    }
                    gm_b16_8 t354 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l70) + 8));
                    F.l45 = t354;
                    gm_toe5fmr3vvf4d t355;
                    GM_ST(uint8_t, (uint8_t *)&t355 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t355 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t355 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t355 + 0), ((uint8_t)12ULL));
                    gm_thf5kr0is9u9i t356;
                    GM_ST(uint8_t, (uint8_t *)&t356 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t356 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t356 + 4, 0);
                    ((uint8_t *)&t356)[0] = 1;
                    GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t356 + 8), t355);
                    F.l46 = t356;
                    gm_closure t357 = ((gm_closure){(void *)gm_f_fn_2gkkusq, (void *)&F.h, cx});
                    gm_f_defaults_p077jgf(t357);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    {
                        gm_thf5kr0is9u9i t358 = F.l46;
                        int64_t t359 = gm_f_sys_count_50c5lcp(t358);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t359);
                        goto gm_fdone;
                    }
                }
gm_lb19:;
                {
                    void * t360 = gm_l0;
                    gm_tl874t2atnlh3 t361 = gm_l1;
                    gm_b16_8 t362;
                    GM_ST(void *, (uint8_t *)&t362, (void *)(gm_sd8o4m7695hpq));
                    GM_ST(uintptr_t, (uint8_t *)&t362 + 8, (uintptr_t)(37));
                    gm_t76123gfet73g t363;
                    gm_f_trap_qh84l9d((void *)&t363, (void *)NULL, cx, NULL, t360, t361, t362);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t363);
                    goto gm_fdone;
                }
gm_lb20:;
                gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 333, 5);
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
extern __attribute__((aligned(4))) int64_t gm_f_sys_net_code_ihgq920(gm_toe5fmr3vvf4d a0);
extern __attribute__((aligned(4))) int64_t gm_f_sys_net_count_46l7ueq(gm_thf5kr0is9u9i a0);
extern __attribute__((aligned(4))) int64_t gm_f_sys_next_handle_r35ek81(void * a0);
__attribute__((aligned(4))) void gm_f_sys_proc_jhdp277(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_sys_proc_jhdp277 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t9qkh0rsooc8d gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    gm_b16_8 gm_l4; /* vals */
    gm_b16_8 gm_l7; /* dir */
    gm_b16_8 gm_l8; /* program */
    uint32_t gm_l9; /* flags */
    gm_toe5fmr3vvf4d gm_l11; /* err */
    gm_tm98ap4112hee gm_l12; /* c */
    int64_t gm_l13; /* handle */
    int64_t gm_l14; /* h */
    gm_b16_8 gm_l15; /* data */
    gm_tm98ap4112hee gm_l16; /* c */
    gm_tm98ap4112hee gm_l17; /* c */
    gm_b16_8 gm_l18; /* buf */
    gm_tm98ap4112hee gm_l19; /* c */
    gm_tm98ap4112hee gm_l20; /* c */
    int64_t gm_l21; /* r */
    gm_tm98ap4112hee gm_l22; /* c */
    gm_tr6norheal11n gm_l23; /* r */
    gm_toe5fmr3vvf4d gm_l24; /* err */
    gm_tqnlpelep1582 gm_l25; /* got */
    gm_tsoiri44d0i6q gm_l26; /* status */
    uint32_t gm_l27; /* code */
    int32_t gm_l28; /* sig */
    gm_tm98ap4112hee gm_l29; /* c */
    GM_STACK_CHECK(&F, 3291u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_t9922qjstblh1 gm_l30;
    gm_t9922qjstblh1 gm_l31;
    gm_td0ept12e7q8b gm_l32;
    gm_td0ept12e7q8b gm_l33;
    gm_th2lqbo77pvsc gm_l34;
    gm_th2lqbo77pvsc gm_l35;
    gm_t62ebbi2i5rkd gm_l36;
    gm_tftb1odc739t5 gm_l37;
    gm_td0ept12e7q8b gm_l38;
    gm_tppo2hmut88dq gm_l39;
    gm_t0jbubompi7qd gm_l40;
    gm_t7eae7gj5k2n2 gm_l41;
    gm_tppo2hmut88dq gm_l42;
    gm_td0ept12e7q8b gm_l43;
    gm_tppo2hmut88dq gm_l44;
    gm_t0jbubompi7qd gm_l45;
    gm_t7eae7gj5k2n2 gm_l46;
    gm_tppo2hmut88dq gm_l47;
    gm_t0jbubompi7qd gm_l48;
    gm_t7eae7gj5k2n2 gm_l49;
    gm_tppo2hmut88dq gm_l50;
    gm_tarq9lp8tdk6f gm_l51;
    gm_tppo2hmut88dq gm_l52;
    {
        gm_t9qkh0rsooc8d t14 = gm_l2;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
        uint8_t t16 = (uint8_t)((t15 == ((uint8_t)37ULL)));
        gm_t9qkh0rsooc8d t17;
        GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)37ULL));
        if (t16) {
            {
                {
                    void * t19 = gm_l0;
                    gm_tl874t2atnlh3 t20 = gm_l1;
                    void * t21 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t22, gm_srcn3jjc71a2bv3, 475, 41);
                    gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)t21) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
                    gm_f_sys_strings_r85ragc((void *)&gm_l30, (void *)NULL, cx, NULL, t19, t20, t24);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t25 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
                if (t25) {
                } else {
                    gm_t76123gfet73g t26 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l30) + 8));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t26);
                    goto gm_fdone;
                }
                {
                    memmove(((uint8_t *)&F.l5), (((uint8_t *)&gm_l30) + 8), 40);
                }
                {
                    void * t27 = gm_l0;
                    gm_tl874t2atnlh3 t28 = gm_l1;
                    void * t29 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t30)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t30, gm_srcn3jjc71a2bv3, 476, 40);
                    gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)t29) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
                    gm_f_sys_strings_r85ragc((void *)&gm_l31, (void *)NULL, cx, NULL, t27, t28, t32);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                if (t33) {
                } else {
                    gm_t76123gfet73g t34 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l31) + 8));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t34);
                    goto gm_fdone;
                }
                {
                    memmove(((uint8_t *)&F.l6), (((uint8_t *)&gm_l31) + 8), 40);
                }
                {
                    void * t35 = gm_l0;
                    gm_tl874t2atnlh3 t36 = gm_l1;
                    void * t37 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t38)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t38, gm_srcn3jjc71a2bv3, 477, 36);
                    gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)t37) + (uintptr_t)((uint64_t)2ULL) * 16));
                    gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
                    gm_f_sys_src_5svsr6i((void *)&gm_l32, (void *)NULL, cx, NULL, t35, t36, t40);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t41 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                if (t41) {
                } else {
                    gm_t76123gfet73g t42 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l32) + 8));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t42);
                    goto gm_fdone;
                }
                gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l32) + 8));
                gm_l7 = t43;
                {
                    void * t44 = gm_l0;
                    gm_tl874t2atnlh3 t45 = gm_l1;
                    void * t46 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)3ULL) >= (uintptr_t)(t47)) gm_trap_index((uintptr_t)((uint64_t)3ULL), t47, gm_srcn3jjc71a2bv3, 478, 40);
                    gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)t46) + (uintptr_t)((uint64_t)3ULL) * 16));
                    gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
                    gm_f_sys_src_5svsr6i((void *)&gm_l33, (void *)NULL, cx, NULL, t44, t45, t49);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t50 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                if (t50) {
                } else {
                    gm_t76123gfet73g t51 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l33) + 8));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t51);
                    goto gm_fdone;
                }
                gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l33) + 8));
                gm_l8 = t52;
                void * t53 = gm_l0;
                void * t54 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                if ((uintptr_t)((uint64_t)4ULL) >= (uintptr_t)(t55)) gm_trap_index((uintptr_t)((uint64_t)4ULL), t55, gm_srcn3jjc71a2bv3, 479, 29);
                gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)t54) + (uintptr_t)((uint64_t)4ULL) * 16));
                gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
                unsigned __int128 t58 = gm_f_get_uint_bq8c4b7(t53, t57);
                uint32_t t59 = (uint32_t)t58;
                gm_l9 = t59;
                uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
                uint8_t t61 = ((t60 == ((uint64_t)0ULL)));
                if (t61) {
                    int64_t t62 = GM_LD(int64_t, ((uint8_t *)gm_cndfnv7mbeh));
                    GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&F.ret + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&F.ret + 24, 0);
                    ((uint8_t *)&F.ret)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&F.ret + 8), t62);
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                    goto gm_ld18_2;
                }
                {
                    uint32_t t63 = gm_l9;
                    uint32_t t64 = (uint32_t)(t63 & ((uint32_t)3ULL));
                    gm_t9qkh0rsooc8d t65 = gm_f_sys_stdio_nsl9o9u(t64);
                    uint32_t t66 = gm_l9;
                    unsigned __int128 n67 = (unsigned __int128)((uint64_t)2ULL);
                    if (n67 >= 32) gm_trap_shift((uint64_t)n67, "u32", gm_srcn3jjc71a2bv3, 485, 32);
                    uint32_t t68 = (uint32_t)(t66 >> (int)n67);
                    uint32_t t69 = (uint32_t)(t68 & ((uint32_t)3ULL));
                    gm_t9qkh0rsooc8d t70 = gm_f_sys_stdio_nsl9o9u(t69);
                    uint32_t t71 = gm_l9;
                    unsigned __int128 n72 = (unsigned __int128)((uint64_t)4ULL);
                    if (n72 >= 32) gm_trap_shift((uint64_t)n72, "u32", gm_srcn3jjc71a2bv3, 486, 32);
                    uint32_t t73 = (uint32_t)(t71 >> (int)n72);
                    uint32_t t74 = (uint32_t)(t73 & ((uint32_t)3ULL));
                    gm_t9qkh0rsooc8d t75 = gm_f_sys_stdio_nsl9o9u(t74);
                    uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    uint8_t t77 = (t76 > ((uint64_t)0ULL));
                    if (t77) {
                        gm_b16_8 t78 = gm_l7;
                        GM_ST(uint8_t, (uint8_t *)&gm_l34 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&gm_l34 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&gm_l34 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&gm_l34), t78);
                        ((uint8_t *)&gm_l34)[16] = 1;
                    } else {
                        memset(&gm_l34, 0, sizeof gm_l34);
                    }
                    gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
                    gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
                    uint32_t t81 = gm_l9;
                    gm_th2lqbo77pvsc t82 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l34));
                    uint8_t t83 = gm_f_sys_has_0sego5l(t81, ((uint32_t)64ULL));
                    uint32_t t84 = gm_l9;
                    uint8_t t85 = gm_f_sys_has_0sego5l(t84, ((uint32_t)128ULL));
                    uint32_t t86 = gm_l9;
                    uint8_t t87 = gm_f_sys_has_0sego5l(t86, ((uint32_t)256ULL));
                    if (t87) {
                        gm_b16_8 t88 = gm_l8;
                        GM_ST(uint8_t, (uint8_t *)&gm_l35 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&gm_l35 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&gm_l35 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&gm_l35), t88);
                        ((uint8_t *)&gm_l35)[16] = 1;
                    } else {
                        memset(&gm_l35, 0, sizeof gm_l35);
                    }
                    gm_th2lqbo77pvsc t89 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l35));
                    GM_ST(uint8_t, (uint8_t *)&F.l10 + 3, 0);
                    GM_ST(uint32_t, (uint8_t *)&F.l10 + 4, 0);
                    GM_ST(uint16_t, (uint8_t *)&F.l10 + 50, 0);
                    GM_ST(uint32_t, (uint8_t *)&F.l10 + 52, 0);
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l10 + 0), t65);
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l10 + 1), t70);
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l10 + 2), t75);
                    GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&F.l10 + 8), t82);
                    GM_ST(gm_b16_8, ((uint8_t *)&F.l10 + 32), t80);
                    GM_ST(uint8_t, ((uint8_t *)&F.l10 + 48), t83);
                    GM_ST(uint8_t, ((uint8_t *)&F.l10 + 49), t85);
                    GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&F.l10 + 56), t89);
                }
                {
                    gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
                    gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&t90));
                    gm_node *pn92 = gm_find(cx, 2119230);
                    if (!pn92) gm_trap("no handler installed for effect `Proc`", gm_srcn3jjc71a2bv3, 493, 14);
                    gm_t62ebbi2i5rkd t93;
                    t93 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8, const void *))pn92->clauses[0])(pn92->env, pn92->hctx, pn92, t91, &(*(gm_t32dckm4jfarl *)((uint8_t *)&F.l10)));
                    if (gm_unw.kind) {
                        goto gm_ld18_2;
                    }
                    gm_l36 = t93;
                }
                uint8_t t94 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
                if (t94) {
                    gm_tm98ap4112hee t95 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&gm_l36) + 8));
                    gm_l12 = t95;
                } else {
                    gm_toe5fmr3vvf4d t96 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l36) + 8));
                    gm_l11 = t96;
                    gm_toe5fmr3vvf4d t97 = gm_l11;
                    int64_t t98 = gm_f_sys_code_cjtlfo1(t97);
                    GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&F.ret + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&F.ret + 24, 0);
                    ((uint8_t *)&F.ret)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&F.ret + 8), t98);
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                    goto gm_ld18_2;
                }
                void * t99 = gm_l0;
                void * t100 = (void *)(((uint8_t *)t99) + 1368);
                int64_t t101 = gm_f_sys_next_handle_r35ek81(t100);
                gm_l13 = t101;
                void * t102 = gm_l0;
                void * t103 = (void *)((((uint8_t *)t102) + 1368) + 224);
                int64_t t104 = gm_l13;
                gm_tm98ap4112hee t105 = gm_l12;
                gm_tftb1odc739t5 t106 = gm_f_put_0tusk8g(t103, t104, GM_CONV(gm_csfq64m1m1rt6m, t105));
                gm_l37 = t106;
                uint8_t t107 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
                if (!t107) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 1, gm_srcn3jjc71a2bv3, 495, 9);
                {
                    int64_t t108 = gm_l13;
                    GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&F.ret + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&F.ret + 24, 0);
                    ((uint8_t *)&F.ret)[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&F.ret + 8), t108);
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                    goto gm_ld18_2;
                }
            }
            {
                void * t109 = (void *)((uint8_t *)&F.l6);
                gm_f_deinit_gvn47sj(t109);
            }
            {
                void * t110 = (void *)((uint8_t *)&F.l5);
                gm_f_deinit_gvn47sj(t110);
            }
            goto gm_ls111;
gm_ld18_2:;
            {
                gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
                void * t112 = (void *)((uint8_t *)&F.l6);
                gm_f_deinit_gvn47sj(t112);
                gm_unw = gm_saved;
            }
gm_ld18_1:;
            {
                gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
                void * t113 = (void *)((uint8_t *)&F.l5);
                gm_f_deinit_gvn47sj(t113);
                gm_unw = gm_saved;
            }
            goto gm_lad;
gm_ls111:;
gm_lb1:;
        }
    }
    {
        void * t114 = gm_l0;
        void * t115 = GM_LD(void *, ((uint8_t *)&gm_l3));
        uint64_t t116 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t116)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t116, gm_srcn3jjc71a2bv3, 498, 21);
        void * t117 = GM_LD(void *, (((uint8_t *)t115) + (uintptr_t)((uint64_t)0ULL) * 8));
        gm_t88rcucg1b7uk t118 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t117) + 56));
        void * t119 = GM_LD(void *, ((uint8_t *)&gm_l4));
        uint64_t t120 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t120)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t120, gm_srcn3jjc71a2bv3, 498, 33);
        gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)t119) + (uintptr_t)((uint64_t)0ULL) * 16));
        gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&t121));
        int64_t t123 = gm_f_sys_int_32e79ed(t114, GM_CONV(gm_csguankko4lmhd, t118), t122);
        gm_l14 = t123;
        {
            {
                {
                    uint8_t t124 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t125 = (uint8_t)(t124 == ((uint8_t)38ULL));
                    if (!t125) {
                        goto gm_lb3;
                    }
                    {
                        void * t126 = gm_l0;
                        gm_tl874t2atnlh3 t127 = gm_l1;
                        void * t128 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t129 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t129)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t129, gm_srcn3jjc71a2bv3, 501, 41);
                        gm_b16_8 t130 = GM_LD(gm_b16_8, (((uint8_t *)t128) + (uintptr_t)((uint64_t)1ULL) * 16));
                        gm_b16_8 t131 = GM_LD(gm_b16_8, ((uint8_t *)&t130));
                        gm_f_sys_src_5svsr6i((void *)&gm_l38, (void *)NULL, cx, NULL, t126, t127, t131);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t132 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                    if (t132) {
                    } else {
                        gm_t76123gfet73g t133 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l38) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t133);
                        goto gm_fdone;
                    }
                    gm_b16_8 t134 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l38) + 8));
                    gm_l15 = t134;
                    void * t135 = gm_l0;
                    void * t136 = (void *)((((uint8_t *)t135) + 1368) + 224);
                    int64_t t137 = gm_l14;
                    gm_tppo2hmut88dq t138 = gm_f_get_fqooi5f(t136, t137);
                    gm_l39 = t138;
                    uint8_t t139 = (uint8_t)((((uint8_t *)&gm_l39))[8] != 0);
                    if (t139) {
                        gm_tm98ap4112hee t140 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l39));
                        gm_l16 = t140;
                    } else {
                        int64_t t141 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t141);
                        goto gm_fdone;
                    }
                    void * t142 = gm_l0;
                    uint64_t t143 = GM_LD(uint64_t, ((((uint8_t *)t142) + 1672) + 8));
                    uint8_t t144 = (t143 > ((uint64_t)1ULL));
                    if (t144) {
                        {
                            void * t145 = gm_l0;
                            gm_tl874t2atnlh3 t146 = gm_l1;
                            gm_tm98ap4112hee t147 = gm_l16;
                            gm_b16_8 t148 = gm_l15;
                            gm_t3olsle24rveu t149;
                            GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t149 + 0), t147);
                            GM_ST(gm_b16_8, ((uint8_t *)&t149 + 8), t148);
                            gm_tnp3hpcm9uhoi t150;
                            GM_ST(uint8_t, (uint8_t *)&t150 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t150 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t150 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t150 + 0), ((uint8_t)7ULL));
                            GM_ST(gm_t3olsle24rveu, ((uint8_t *)&t150 + 8), t149);
                            gm_f_on_host_thread_kmegc07((void *)&gm_l40, (void *)NULL, cx, NULL, t145, t146, &t150);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t151 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
                        if (t151) {
                        } else {
                            gm_t76123gfet73g t152 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l40) + 8));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t152);
                            goto gm_fdone;
                        }
                        {
                            memmove(((uint8_t *)&gm_l41), (((uint8_t *)&gm_l40) + 8), 40);
                        }
                        {
                            int64_t t153 = GM_LD(int64_t, (((uint8_t *)&gm_l41) + 32));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                            GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t153);
                            goto gm_fdone;
                        }
                    }
                    {
                        gm_tm98ap4112hee t154 = gm_l16;
                        gm_b16_8 t155 = gm_l15;
                        gm_node *pn156 = gm_find(cx, 2119230);
                        if (!pn156) gm_trap("no handler installed for effect `Proc`", gm_srcn3jjc71a2bv3, 504, 30);
                        gm_thf5kr0is9u9i t157;
                        t157 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn156->clauses[1])(pn156->env, pn156->hctx, pn156, t154, t155);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        int64_t t158 = gm_f_sys_count_50c5lcp(t157);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t158);
                        goto gm_fdone;
                    }
                }
gm_lb3:;
                {
                    uint8_t t159 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t160 = (uint8_t)(t159 == ((uint8_t)39ULL));
                    if (!t160) {
                        goto gm_lb4;
                    }
                    void * t161 = gm_l0;
                    void * t162 = (void *)((((uint8_t *)t161) + 1368) + 224);
                    int64_t t163 = gm_l14;
                    gm_tppo2hmut88dq t164 = gm_f_get_fqooi5f(t162, t163);
                    gm_l42 = t164;
                    uint8_t t165 = (uint8_t)((((uint8_t *)&gm_l42))[8] != 0);
                    if (t165) {
                        gm_tm98ap4112hee t166 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l42));
                        gm_l17 = t166;
                    } else {
                        int64_t t167 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t167);
                        goto gm_fdone;
                    }
                    gm_tm98ap4112hee t168 = gm_l17;
                    gm_node *pn169 = gm_find(cx, 2119230);
                    if (!pn169) gm_trap("no handler installed for effect `Proc`", gm_srcn3jjc71a2bv3, 508, 13);
                    gm_b0_1 t170;
                    t170 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn169->clauses[2])(pn169->env, pn169->hctx, pn169, t168);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), ((int64_t)0LL));
                    goto gm_fdone;
                }
gm_lb4:;
            }
            {
                {
                    uint8_t t171 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t172 = (uint8_t)(t171 == ((uint8_t)40ULL));
                    if (!t172) {
                        goto gm_lb5;
                    }
                    {
                        void * t173 = gm_l0;
                        gm_tl874t2atnlh3 t174 = gm_l1;
                        void * t175 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t176 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t176)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t176, gm_srcn3jjc71a2bv3, 512, 41);
                        gm_b16_8 t177 = GM_LD(gm_b16_8, (((uint8_t *)t175) + (uintptr_t)((uint64_t)1ULL) * 16));
                        gm_b16_8 t178 = GM_LD(gm_b16_8, ((uint8_t *)&t177));
                        gm_f_sys_dest_rtrb14h((void *)&gm_l43, (void *)NULL, cx, NULL, t173, t174, t178);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t179 = (uint8_t)!((((uint8_t *)&gm_l43))[0] != 0);
                    if (t179) {
                    } else {
                        gm_t76123gfet73g t180 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l43) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t180);
                        goto gm_fdone;
                    }
                    gm_b16_8 t181 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l43) + 8));
                    gm_l18 = t181;
                    void * t182 = gm_l0;
                    void * t183 = (void *)((((uint8_t *)t182) + 1368) + 224);
                    int64_t t184 = gm_l14;
                    gm_tppo2hmut88dq t185 = gm_f_get_fqooi5f(t183, t184);
                    gm_l44 = t185;
                    uint8_t t186 = (uint8_t)((((uint8_t *)&gm_l44))[8] != 0);
                    if (t186) {
                        gm_tm98ap4112hee t187 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l44));
                        gm_l19 = t187;
                    } else {
                        int64_t t188 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t188);
                        goto gm_fdone;
                    }
                    void * t189 = gm_l0;
                    uint64_t t190 = GM_LD(uint64_t, ((((uint8_t *)t189) + 1672) + 8));
                    uint8_t t191 = (t190 > ((uint64_t)1ULL));
                    if (t191) {
                        {
                            void * t192 = gm_l0;
                            gm_tl874t2atnlh3 t193 = gm_l1;
                            gm_tm98ap4112hee t194 = gm_l19;
                            gm_b16_8 t195 = gm_l18;
                            gm_t3olsle24rveu t196;
                            GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t196 + 0), t194);
                            GM_ST(gm_b16_8, ((uint8_t *)&t196 + 8), t195);
                            gm_tnp3hpcm9uhoi t197;
                            GM_ST(uint8_t, (uint8_t *)&t197 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t197 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t197 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t197 + 0), ((uint8_t)6ULL));
                            GM_ST(gm_t3olsle24rveu, ((uint8_t *)&t197 + 8), t196);
                            gm_f_on_host_thread_kmegc07((void *)&gm_l45, (void *)NULL, cx, NULL, t192, t193, &t197);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t198 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
                        if (t198) {
                        } else {
                            gm_t76123gfet73g t199 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l45) + 8));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t199);
                            goto gm_fdone;
                        }
                        {
                            memmove(((uint8_t *)&gm_l46), (((uint8_t *)&gm_l45) + 8), 40);
                        }
                        {
                            int64_t t200 = GM_LD(int64_t, (((uint8_t *)&gm_l46) + 32));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                            GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t200);
                            goto gm_fdone;
                        }
                    }
                    {
                        gm_tm98ap4112hee t201 = gm_l19;
                        gm_b16_8 t202 = gm_l18;
                        int64_t t203 = gm_f_proc_read_kc2l8gi((void *)NULL, cx, NULL, t201, t202);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t203);
                        goto gm_fdone;
                    }
                }
gm_lb5:;
                {
                    uint8_t t204 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t205 = (uint8_t)(t204 == ((uint8_t)41ULL));
                    if (!t205) {
                        goto gm_lb6;
                    }
                    void * t206 = gm_l0;
                    void * t207 = (void *)((((uint8_t *)t206) + 1368) + 224);
                    int64_t t208 = gm_l14;
                    gm_tppo2hmut88dq t209 = gm_f_get_fqooi5f(t207, t208);
                    gm_l47 = t209;
                    uint8_t t210 = (uint8_t)((((uint8_t *)&gm_l47))[8] != 0);
                    if (t210) {
                        gm_tm98ap4112hee t211 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l47));
                        gm_l20 = t211;
                    } else {
                        int64_t t212 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t212);
                        goto gm_fdone;
                    }
                    void * t213 = gm_l0;
                    uint64_t t214 = GM_LD(uint64_t, ((((uint8_t *)t213) + 1672) + 8));
                    uint8_t t215 = (t214 > ((uint64_t)1ULL));
                    if (t215) {
                        {
                            void * t216 = gm_l0;
                            gm_tl874t2atnlh3 t217 = gm_l1;
                            gm_tm98ap4112hee t218 = gm_l20;
                            gm_tnp3hpcm9uhoi t219;
                            GM_ST(uint8_t, (uint8_t *)&t219 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t219 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t219 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t219 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t219 + 24, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t219 + 0), ((uint8_t)8ULL));
                            GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t219 + 8), t218);
                            gm_f_on_host_thread_kmegc07((void *)&gm_l48, (void *)NULL, cx, NULL, t216, t217, &t219);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t220 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                        if (t220) {
                        } else {
                            gm_t76123gfet73g t221 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l48) + 8));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t221);
                            goto gm_fdone;
                        }
                        {
                            memmove(((uint8_t *)&gm_l49), (((uint8_t *)&gm_l48) + 8), 40);
                        }
                        int64_t t222 = GM_LD(int64_t, (((uint8_t *)&gm_l49) + 32));
                        gm_l21 = t222;
                    } else {
                        gm_tm98ap4112hee t223 = gm_l20;
                        gm_node *pn224 = gm_find(cx, 2119230);
                        if (!pn224) gm_trap("no handler installed for effect `Proc`", gm_srcn3jjc71a2bv3, 519, 121);
                        gm_tlmjd3nndlf3r t225;
                        t225 = ((gm_tlmjd3nndlf3r (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn224->clauses[4])(pn224->env, pn224->hctx, pn224, t223);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        int64_t t226 = gm_f_proc_status_7nq4d17(t225);
                        gm_l21 = t226;
                    }
                    void * t227 = gm_l0;
                    void * t228 = (void *)((((uint8_t *)t227) + 1368) + 224);
                    int64_t t229 = gm_l14;
                    gm_tppo2hmut88dq t230 = gm_f_remove_jvpr521(t228, t229);
                    int64_t t231 = gm_l21;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t231);
                    goto gm_fdone;
                }
gm_lb6:;
            }
            {
                {
                    uint8_t t232 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t233 = (uint8_t)(t232 == ((uint8_t)43ULL));
                    if (!t233) {
                        goto gm_lb7;
                    }
                    void * t234 = gm_l0;
                    void * t235 = (void *)((((uint8_t *)t234) + 1368) + 224);
                    int64_t t236 = gm_l14;
                    gm_tppo2hmut88dq t237 = gm_f_get_fqooi5f(t235, t236);
                    gm_l50 = t237;
                    uint8_t t238 = (uint8_t)((((uint8_t *)&gm_l50))[8] != 0);
                    if (t238) {
                        gm_tm98ap4112hee t239 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l50));
                        gm_l22 = t239;
                    } else {
                        int64_t t240 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t240);
                        goto gm_fdone;
                    }
                    gm_tm98ap4112hee t241 = gm_l22;
                    gm_node *pn242 = gm_find(cx, 2119230);
                    if (!pn242) gm_trap("no handler installed for effect `Proc`", gm_srcn3jjc71a2bv3, 525, 18);
                    gm_tr6norheal11n t243;
                    t243 = ((gm_tr6norheal11n (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn242->clauses[5])(pn242->env, pn242->hctx, pn242, t241);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l23 = t243;
                    uint8_t t244 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                    if (t244) {
                        gm_tqnlpelep1582 t245 = GM_LD(gm_tqnlpelep1582, (((uint8_t *)&gm_l23) + 4));
                        gm_l25 = t245;
                    } else {
                        gm_toe5fmr3vvf4d t246 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l23) + 4));
                        gm_l24 = t246;
                        gm_toe5fmr3vvf4d t247 = gm_l24;
                        int64_t t248 = gm_f_sys_code_cjtlfo1(t247);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t248);
                        goto gm_fdone;
                    }
                    uint8_t t249 = (uint8_t)((((uint8_t *)&gm_l25))[8] != 0);
                    if (t249) {
                        gm_tsoiri44d0i6q t250 = GM_LD(gm_tsoiri44d0i6q, ((uint8_t *)&gm_l25));
                        gm_l26 = t250;
                    } else {
                        unsigned __int128 n251 = (unsigned __int128)((uint64_t)33ULL);
                        if (n251 >= 64) gm_trap_shift((uint64_t)n251, "i64", gm_srcn3jjc71a2bv3, 527, 39);
                        int64_t r252 = (int64_t)((uint64_t)((int64_t)1LL) << (int)n251);
                        bool o253 = (int64_t)(r252 >> (int)n251) != ((int64_t)1LL);
                        if (o253) gm_trap("integer overflow in `<<`", gm_srcn3jjc71a2bv3, 527, 39);
                        int64_t t254 = r252;
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t254);
                        goto gm_fdone;
                    }
                    void * t255 = gm_l0;
                    void * t256 = (void *)((((uint8_t *)t255) + 1368) + 224);
                    int64_t t257 = gm_l14;
                    gm_tppo2hmut88dq t258 = gm_f_remove_jvpr521(t256, t257);
                    {
                        {
                            uint8_t t259 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                            uint8_t t260 = (uint8_t)(t259 == ((uint8_t)0ULL));
                            if (!t260) {
                                goto gm_lb9;
                            }
                            uint32_t t261 = GM_LD(uint32_t, (((uint8_t *)&gm_l26) + 4));
                            gm_l27 = t261;
                            uint32_t t262 = gm_l27;
                            int64_t t263 = (int64_t)t262;
                            GM_ST(uint8_t, (uint8_t *)&gm_l51 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&gm_l51 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&gm_l51 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&gm_l51 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&gm_l51 + 24, 0);
                            ((uint8_t *)&gm_l51)[0] = 0;
                            GM_ST(int64_t, ((uint8_t *)&gm_l51 + 8), t263);
                            goto gm_lb8;
                        }
gm_lb9:;
                        {
                            uint8_t t264 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                            uint8_t t265 = (uint8_t)(t264 == ((uint8_t)1ULL));
                            if (!t265) {
                                goto gm_lb10;
                            }
                            int32_t t266 = GM_LD(int32_t, (((uint8_t *)&gm_l26) + 4));
                            gm_l28 = t266;
                            int32_t t267 = gm_l28;
                            int64_t t268 = (int64_t)t267;
                            unsigned __int128 n269 = (unsigned __int128)((uint64_t)32ULL);
                            if (n269 >= 64) gm_trap_shift((uint64_t)n269, "i64", gm_srcn3jjc71a2bv3, 531, 47);
                            int64_t r270 = (int64_t)((uint64_t)((int64_t)1LL) << (int)n269);
                            bool o271 = (int64_t)(r270 >> (int)n269) != ((int64_t)1LL);
                            if (o271) gm_trap("integer overflow in `<<`", gm_srcn3jjc71a2bv3, 531, 47);
                            int64_t t272 = r270;
                            int64_t r273; bool o274 = __builtin_add_overflow(t268, t272, &r273);
                            if (o274) gm_trap("integer overflow in `+`", gm_srcn3jjc71a2bv3, 531, 35);
                            int64_t t275 = r273;
                            GM_ST(uint8_t, (uint8_t *)&gm_l51 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&gm_l51 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&gm_l51 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&gm_l51 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&gm_l51 + 24, 0);
                            ((uint8_t *)&gm_l51)[0] = 0;
                            GM_ST(int64_t, ((uint8_t *)&gm_l51 + 8), t275);
                            goto gm_lb8;
                        }
gm_lb10:;
                        gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 529, 20);
                    }
gm_lb8:;
                    memmove(gm_out, ((uint8_t *)&gm_l51), 32);
                    goto gm_fdone;
                }
gm_lb7:;
            }
            {
                {
                    uint8_t t276 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t277 = (uint8_t)(t276 == ((uint8_t)42ULL));
                    if (!t277) {
                        goto gm_lb11;
                    }
                    void * t278 = gm_l0;
                    void * t279 = (void *)((((uint8_t *)t278) + 1368) + 224);
                    int64_t t280 = gm_l14;
                    gm_tppo2hmut88dq t281 = gm_f_get_fqooi5f(t279, t280);
                    gm_l52 = t281;
                    uint8_t t282 = (uint8_t)((((uint8_t *)&gm_l52))[8] != 0);
                    if (t282) {
                        gm_tm98ap4112hee t283 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&gm_l52));
                        gm_l29 = t283;
                    } else {
                        int64_t t284 = GM_LD(int64_t, ((uint8_t *)gm_c3qosqjl81k));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t284);
                        goto gm_fdone;
                    }
                    gm_tm98ap4112hee t285 = gm_l29;
                    gm_node *pn286 = gm_find(cx, 2119230);
                    if (!pn286) gm_trap("no handler installed for effect `Proc`", gm_srcn3jjc71a2bv3, 536, 29);
                    gm_ttln0d0hd70en t287;
                    t287 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn286->clauses[6])(pn286->env, pn286->hctx, pn286, t285);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    int64_t t288 = gm_f_sys_unit_nrsqciq(t287);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                    GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t288);
                    goto gm_fdone;
                }
gm_lb11:;
                {
                    void * t289 = gm_l0;
                    gm_tl874t2atnlh3 t290 = gm_l1;
                    gm_b16_8 t291;
                    GM_ST(void *, (uint8_t *)&t291, (void *)(gm_stfd80s46313k));
                    GM_ST(uintptr_t, (uint8_t *)&t291 + 8, (uintptr_t)(37));
                    gm_t76123gfet73g t292;
                    gm_f_trap_qh84l9d((void *)&t292, (void *)NULL, cx, NULL, t289, t290, t291);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t292);
                    goto gm_fdone;
                }
gm_lb12:;
                gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 499, 5);
            }
        }
gm_lb2:;
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
inline __attribute__((aligned(4))) void gm_f_sys_read_b0gj9fg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_sys_read_b0gj9fg F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* vals */
    GM_STACK_CHECK(&F, 471u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_td0ept12e7q8b gm_l5;
    gm_t0jbubompi7qd gm_l6;
    gm_t7eae7gj5k2n2 gm_l7;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l1;
        void * t4 = GM_LD(void *, ((uint8_t *)&gm_l2));
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t5)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t5, gm_srcn3jjc71a2bv3, 192, 33);
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)t4) + (uintptr_t)((uint64_t)0ULL) * 16));
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
        gm_f_sys_dest_rtrb14h((void *)&gm_l5, (void *)NULL, cx, NULL, t2, t3, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (t8) {
    } else {
        gm_t76123gfet73g t9 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l5) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t9);
        goto gm_fdone;
    }
    gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
    F.l3 = t10;
    void * t11 = gm_l0;
    uint64_t t12 = GM_LD(uint64_t, ((((uint8_t *)t11) + 1672) + 8));
    uint8_t t13 = (t12 > ((uint64_t)1ULL));
    if (t13) {
        {
            void * t14 = gm_l0;
            gm_tl874t2atnlh3 t15 = gm_l1;
            gm_b16_8 t16 = F.l3;
            gm_tnp3hpcm9uhoi t17;
            GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t17 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t17 + 8), t16);
            gm_f_on_host_thread_kmegc07((void *)&gm_l6, (void *)NULL, cx, NULL, t14, t15, &t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
        if (t18) {
        } else {
            gm_t76123gfet73g t19 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l6) + 8));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t19);
            goto gm_fdone;
        }
        {
            memmove(((uint8_t *)&gm_l7), (((uint8_t *)&gm_l6) + 8), 40);
        }
        {
            gm_thf5kr0is9u9i t20 = GM_LD(gm_thf5kr0is9u9i, ((uint8_t *)&gm_l7));
            int64_t t21 = gm_f_sys_count_50c5lcp(t20);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t21);
            goto gm_fdone;
        }
    }
    gm_toe5fmr3vvf4d t22;
    GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)12ULL));
    gm_thf5kr0is9u9i t23;
    GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
    ((uint8_t *)&t23)[0] = 1;
    GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t23 + 8), t22);
    F.l4 = t23;
    gm_closure t24 = ((gm_closure){(void *)gm_f_fn_trt04qi, (void *)&F.h, cx});
    gm_f_defaults_p077jgf(t24);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        gm_thf5kr0is9u9i t25 = F.l4;
        int64_t t26 = gm_f_sys_count_50c5lcp(t25);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
        GM_ST(int64_t, ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8), t26);
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
extern __attribute__((aligned(4))) void gm_f_sys_ret_vpa2k8i(void * a0, gm_b16_8 a1, int64_t a2);
__attribute__((aligned(4))) void gm_f_sys_run_2vqjo3u(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4, gm_b16_8 a5) {
    struct gm_fr_sys_run_2vqjo3u F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t9qkh0rsooc8d gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    gm_b16_8 gm_l4; /* vals */
    gm_b16_8 gm_l5; /* dst */
    int64_t gm_l6; /* n */
    uint64_t gm_l7; /* now */
    uint64_t gm_l8; /* base */
    gm_b16_8 gm_l9; /* buf */
    gm_b16_8 gm_l10; /* data */
    GM_STACK_CHECK(&F, 1664u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_tarq9lp8tdk6f gm_l11;
    gm_t4ce0jd3tdro4 gm_l12;
    uint8_t gm_l13;
    gm_td0ept12e7q8b gm_l14;
    gm_td0ept12e7q8b gm_l15;
    uint32_t gm_l16;
    gm_tarq9lp8tdk6f gm_l17;
    gm_tarq9lp8tdk6f gm_l18;
    gm_tarq9lp8tdk6f gm_l19;
    {
        gm_l6 = ((int64_t)0LL);
        {
            {
                {
                    uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t63 = (uint8_t)(t62 == ((uint8_t)0ULL));
                    if (!t63) {
                        goto gm_lb2;
                    }
                    void * t64 = gm_l0;
                    gm_tl874t2atnlh3 t65 = gm_l1;
                    gm_b16_8 t66 = gm_l4;
                    gm_f_sys_read_b0gj9fg((void *)&gm_l11, (void *)NULL, cx, NULL, t64, t65, t66);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t67 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                    if (t67) {
                    } else {
                        gm_t76123gfet73g t68 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l11) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t68);
                        goto gm_fdone;
                    }
                    int64_t t69 = GM_LD(int64_t, (((uint8_t *)&gm_l11) + 8));
                    gm_l6 = t69;
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t71 = (uint8_t)(t70 == ((uint8_t)1ULL));
                    if (!t71) {
                        goto gm_lb3;
                    }
                    gm_node *pn72 = gm_find(cx, 1579066750);
                    if (!pn72) gm_trap("no handler installed for effect `Clock`", gm_srcn3jjc71a2bv3, 158, 20);
                    gm_t4ce0jd3tdro4 t73;
                    t73 = ((gm_t4ce0jd3tdro4 (*)(void *, gm_node *, gm_node *))pn72->clauses[0])(pn72->env, pn72->hctx, pn72);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l12 = t73;
                    uint64_t t74 = GM_LD(uint64_t, ((uint8_t *)&gm_l12));
                    gm_l7 = t74;
                    void * t75 = gm_l0;
                    {
                        {
                            uint8_t t76 = (uint8_t)((((((uint8_t *)t75) + 1368) + 288))[8] != 0);
                            if (t76) {
                                goto gm_lb5;
                            }
                            gm_l13 = ((uint8_t)1);
                            goto gm_lb4;
                        }
gm_lb5:;
                        {
                            gm_l13 = ((uint8_t)0);
                            goto gm_lb4;
                        }
gm_lb6:;
                        gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 159, 16);
                    }
gm_lb4:;
                    uint8_t t77 = gm_l13;
                    if (t77) {
                        void * t78 = gm_l0;
                        uint64_t t79 = gm_l7;
                        gm_tf5srh4mp4b3i t80;
                        GM_ST(uint8_t, (uint8_t *)&t80 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t80 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t80 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t80), t79);
                        ((uint8_t *)&t80)[8] = 1;
                        GM_ST(gm_tf5srh4mp4b3i, ((((uint8_t *)t78) + 1368) + 288), t80);
                    }
                    void * t81 = gm_l0;
                    uint8_t t82 = (uint8_t)((((((uint8_t *)t81) + 1368) + 288))[8] != 0);
                    if (t82) {
                        uint64_t t83 = GM_LD(uint64_t, ((((uint8_t *)t81) + 1368) + 288));
                        gm_l8 = t83;
                    } else {
                        gm_l8 = ((uint64_t)0ULL);
                    }
                    void * t84 = gm_l0;
                    gm_b16_8 t85 = gm_l5;
                    uint64_t t86 = gm_l7;
                    uint64_t t87 = gm_l8;
                    uint64_t r88; bool o89 = __builtin_sub_overflow(t86, t87, &r88);
                    uint64_t t90 = r88;
                    unsigned __int128 t91 = (unsigned __int128)t90;
                    gm_f_put_uint_cddin99(t84, t85, t91);
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
gm_lb3:;
                {
                    uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t93 = (uint8_t)(t92 == ((uint8_t)2ULL));
                    if (!t93) {
                        goto gm_lb7;
                    }
                    gm_node *pn94 = gm_find(cx, 1579066750);
                    if (!pn94) gm_trap("no handler installed for effect `Clock`", gm_srcn3jjc71a2bv3, 164, 29);
                    int64_t t95;
                    t95 = ((int64_t (*)(void *, gm_node *, gm_node *))pn94->clauses[1])(pn94->env, pn94->hctx, pn94);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l6 = t95;
                    goto gm_lb1;
                }
gm_lb7:;
                {
                    uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t97 = (uint8_t)(t96 == ((uint8_t)3ULL));
                    if (!t97) {
                        goto gm_lb8;
                    }
                    void * t98 = gm_l0;
                    gm_tl874t2atnlh3 t99 = gm_l1;
                    void * t100 = gm_l0;
                    void * t101 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t102 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t102)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t102, gm_srcn3jjc71a2bv3, 166, 45);
                    gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)t101) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&t103));
                    unsigned __int128 t105 = gm_f_get_uint_bq8c4b7(t100, t104);
                    uint64_t t106 = (uint64_t)t105;
                    gm_f_sleep_gqtie06((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t98, t99, t106);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb8:;
            }
            {
                {
                    uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t108 = (uint8_t)(t107 == ((uint8_t)4ULL));
                    if (!t108) {
                        goto gm_lb9;
                    }
                    {
                        void * t109 = gm_l0;
                        gm_tl874t2atnlh3 t110 = gm_l1;
                        void * t111 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t112 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t112)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t112, gm_srcn3jjc71a2bv3, 169, 41);
                        gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)t111) + (uintptr_t)((uint64_t)0ULL) * 16));
                        gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
                        gm_f_sys_dest_rtrb14h((void *)&gm_l14, (void *)NULL, cx, NULL, t109, t110, t114);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t115 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                    if (t115) {
                    } else {
                        gm_t76123gfet73g t116 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l14) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t116);
                        goto gm_fdone;
                    }
                    gm_b16_8 t117 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
                    gm_l9 = t117;
                    gm_b16_8 t118 = gm_l9;
                    gm_node *pn119 = gm_find(cx, 1087599154);
                    if (!pn119) gm_trap("no handler installed for effect `Rand`", gm_srcn3jjc71a2bv3, 170, 13);
                    gm_b0_1 t120;
                    t120 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_b16_8))pn119->clauses[0])(pn119->env, pn119->hctx, pn119, t118);
                    if (gm_unw.kind) {
                        goto gm_lad;
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
                }
gm_lb9:;
                {
                    uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t122 = (uint8_t)(t121 == ((uint8_t)54ULL));
                    if (!t122) {
                        goto gm_lb10;
                    }
                    {
                        void * t123 = gm_l0;
                        gm_tl874t2atnlh3 t124 = gm_l1;
                        void * t125 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t126 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t126)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t126, gm_srcn3jjc71a2bv3, 174, 41);
                        gm_b16_8 t127 = GM_LD(gm_b16_8, (((uint8_t *)t125) + (uintptr_t)((uint64_t)1ULL) * 16));
                        gm_b16_8 t128 = GM_LD(gm_b16_8, ((uint8_t *)&t127));
                        gm_f_sys_src_5svsr6i((void *)&gm_l15, (void *)NULL, cx, NULL, t123, t124, t128);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t129 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                    if (t129) {
                    } else {
                        gm_t76123gfet73g t130 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l15) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t130);
                        goto gm_fdone;
                    }
                    gm_b16_8 t131 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
                    gm_l10 = t131;
                    void * t132 = gm_l0;
                    void * t133 = gm_l0;
                    void * t134 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t135 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t135)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t135, gm_srcn3jjc71a2bv3, 175, 40);
                    void * t136 = GM_LD(void *, (((uint8_t *)t134) + (uintptr_t)((uint64_t)0ULL) * 8));
                    gm_t88rcucg1b7uk t137 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t136) + 56));
                    void * t138 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t139 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t139)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t139, gm_srcn3jjc71a2bv3, 175, 52);
                    gm_b16_8 t140 = GM_LD(gm_b16_8, (((uint8_t *)t138) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t141 = GM_LD(gm_b16_8, ((uint8_t *)&t140));
                    int64_t t142 = gm_f_sys_int_32e79ed(t133, GM_CONV(gm_csguankko4lmhd, t137), t141);
                    uint8_t t143 = ((t142 == ((int64_t)2LL)));
                    if (t143) {
                        gm_l16 = ((uint32_t)2ULL);
                    } else {
                        gm_l16 = ((uint32_t)1ULL);
                    }
                    uint32_t t144 = gm_l16;
                    gm_b16_8 t145 = gm_l10;
                    gm_b0_1 t146 = gm_f_host_write_r26no87((void *)NULL, cx, NULL, t132, t144, t145);
                    if (gm_unw.kind) {
                        goto gm_lad;
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
                }
gm_lb10:;
            }
            {
                {
                    uint8_t t147 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t148 = (uint8_t)(t147 == ((uint8_t)53ULL));
                    if (!t148) {
                        goto gm_lb11;
                    }
                    uint32_t t149 = gm_f_cpu_count_0o5882g();
                    int64_t t150 = (int64_t)t149;
                    gm_l6 = t150;
                    goto gm_lb1;
                }
gm_lb11:;
                {
                    {
                        {
                            uint8_t t151 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t152 = (uint8_t)(t151 == ((uint8_t)5ULL));
                            if (!t152) {
                                goto gm_lb14;
                            }
                            goto gm_lb13;
                        }
gm_lb14:;
                        {
                            uint8_t t153 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t154 = (uint8_t)(t153 == ((uint8_t)6ULL));
                            if (!t154) {
                                goto gm_lb15;
                            }
                            goto gm_lb13;
                        }
gm_lb15:;
                        {
                            uint8_t t155 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t156 = (uint8_t)(t155 == ((uint8_t)7ULL));
                            if (!t156) {
                                goto gm_lb16;
                            }
                            goto gm_lb13;
                        }
gm_lb16:;
                        {
                            uint8_t t157 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t158 = (uint8_t)(t157 == ((uint8_t)8ULL));
                            if (!t158) {
                                goto gm_lb17;
                            }
                            goto gm_lb13;
                        }
gm_lb17:;
                        {
                            uint8_t t159 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t160 = (uint8_t)(t159 == ((uint8_t)9ULL));
                            if (!t160) {
                                goto gm_lb18;
                            }
                            goto gm_lb13;
                        }
gm_lb18:;
                        {
                            uint8_t t161 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t162 = (uint8_t)(t161 == ((uint8_t)10ULL));
                            if (!t162) {
                                goto gm_lb19;
                            }
                            goto gm_lb13;
                        }
gm_lb19:;
                        {
                            uint8_t t163 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t164 = (uint8_t)(t163 == ((uint8_t)11ULL));
                            if (!t164) {
                                goto gm_lb20;
                            }
                            goto gm_lb13;
                        }
gm_lb20:;
                        {
                            uint8_t t165 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t166 = (uint8_t)(t165 == ((uint8_t)12ULL));
                            if (!t166) {
                                goto gm_lb21;
                            }
                            goto gm_lb13;
                        }
gm_lb21:;
                        {
                            uint8_t t167 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t168 = (uint8_t)(t167 == ((uint8_t)13ULL));
                            if (!t168) {
                                goto gm_lb22;
                            }
                            goto gm_lb13;
                        }
gm_lb22:;
                        {
                            uint8_t t169 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t170 = (uint8_t)(t169 == ((uint8_t)14ULL));
                            if (!t170) {
                                goto gm_lb23;
                            }
                            goto gm_lb13;
                        }
gm_lb23:;
                        {
                            uint8_t t171 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t172 = (uint8_t)(t171 == ((uint8_t)15ULL));
                            if (!t172) {
                                goto gm_lb24;
                            }
                            goto gm_lb13;
                        }
gm_lb24:;
                        {
                            uint8_t t173 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t174 = (uint8_t)(t173 == ((uint8_t)16ULL));
                            if (!t174) {
                                goto gm_lb25;
                            }
                            goto gm_lb13;
                        }
gm_lb25:;
                        {
                            uint8_t t175 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t176 = (uint8_t)(t175 == ((uint8_t)17ULL));
                            if (!t176) {
                                goto gm_lb26;
                            }
                            goto gm_lb13;
                        }
gm_lb26:;
                        {
                            uint8_t t177 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t178 = (uint8_t)(t177 == ((uint8_t)18ULL));
                            if (!t178) {
                                goto gm_lb27;
                            }
                            goto gm_lb13;
                        }
gm_lb27:;
                        {
                            uint8_t t179 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t180 = (uint8_t)(t179 == ((uint8_t)19ULL));
                            if (!t180) {
                                goto gm_lb28;
                            }
                            goto gm_lb13;
                        }
gm_lb28:;
                        {
                            uint8_t t181 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t182 = (uint8_t)(t181 == ((uint8_t)20ULL));
                            if (!t182) {
                                goto gm_lb29;
                            }
                            goto gm_lb13;
                        }
gm_lb29:;
                        {
                            uint8_t t183 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t184 = (uint8_t)(t183 == ((uint8_t)21ULL));
                            if (!t184) {
                                goto gm_lb30;
                            }
                            goto gm_lb13;
                        }
gm_lb30:;
                        {
                            uint8_t t185 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t186 = (uint8_t)(t185 == ((uint8_t)22ULL));
                            if (!t186) {
                                goto gm_lb31;
                            }
                            goto gm_lb13;
                        }
gm_lb31:;
                        {
                            uint8_t t187 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t188 = (uint8_t)(t187 == ((uint8_t)23ULL));
                            if (!t188) {
                                goto gm_lb32;
                            }
                            goto gm_lb13;
                        }
gm_lb32:;
                        {
                            uint8_t t189 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t190 = (uint8_t)(t189 == ((uint8_t)24ULL));
                            if (!t190) {
                                goto gm_lb33;
                            }
                            goto gm_lb13;
                        }
gm_lb33:;
                        {
                            uint8_t t191 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t192 = (uint8_t)(t191 == ((uint8_t)25ULL));
                            if (!t192) {
                                goto gm_lb34;
                            }
                            goto gm_lb13;
                        }
gm_lb34:;
                        {
                            uint8_t t193 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t194 = (uint8_t)(t193 == ((uint8_t)26ULL));
                            if (!t194) {
                                goto gm_lb35;
                            }
                            goto gm_lb13;
                        }
gm_lb35:;
                        goto gm_lb12;
                    }
gm_lb13:;
                    void * t195 = gm_l0;
                    gm_tl874t2atnlh3 t196 = gm_l1;
                    gm_t9qkh0rsooc8d t197 = gm_l2;
                    gm_b16_8 t198 = gm_l3;
                    gm_b16_8 t199 = gm_l4;
                    gm_f_sys_fs_u0grkf6((void *)&gm_l17, (void *)NULL, cx, NULL, t195, t196, t197, t198, t199);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t200 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                    if (t200) {
                    } else {
                        gm_t76123gfet73g t201 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l17) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t201);
                        goto gm_fdone;
                    }
                    int64_t t202 = GM_LD(int64_t, (((uint8_t *)&gm_l17) + 8));
                    gm_l6 = t202;
                    goto gm_lb1;
                }
gm_lb12:;
            }
            {
                {
                    {
                        {
                            uint8_t t203 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t204 = (uint8_t)(t203 == ((uint8_t)27ULL));
                            if (!t204) {
                                goto gm_lb38;
                            }
                            goto gm_lb37;
                        }
gm_lb38:;
                        {
                            uint8_t t205 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t206 = (uint8_t)(t205 == ((uint8_t)28ULL));
                            if (!t206) {
                                goto gm_lb39;
                            }
                            goto gm_lb37;
                        }
gm_lb39:;
                        {
                            uint8_t t207 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t208 = (uint8_t)(t207 == ((uint8_t)29ULL));
                            if (!t208) {
                                goto gm_lb40;
                            }
                            goto gm_lb37;
                        }
gm_lb40:;
                        {
                            uint8_t t209 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t210 = (uint8_t)(t209 == ((uint8_t)30ULL));
                            if (!t210) {
                                goto gm_lb41;
                            }
                            goto gm_lb37;
                        }
gm_lb41:;
                        {
                            uint8_t t211 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t212 = (uint8_t)(t211 == ((uint8_t)31ULL));
                            if (!t212) {
                                goto gm_lb42;
                            }
                            goto gm_lb37;
                        }
gm_lb42:;
                        {
                            uint8_t t213 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t214 = (uint8_t)(t213 == ((uint8_t)32ULL));
                            if (!t214) {
                                goto gm_lb43;
                            }
                            goto gm_lb37;
                        }
gm_lb43:;
                        {
                            uint8_t t215 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t216 = (uint8_t)(t215 == ((uint8_t)33ULL));
                            if (!t216) {
                                goto gm_lb44;
                            }
                            goto gm_lb37;
                        }
gm_lb44:;
                        {
                            uint8_t t217 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t218 = (uint8_t)(t217 == ((uint8_t)34ULL));
                            if (!t218) {
                                goto gm_lb45;
                            }
                            goto gm_lb37;
                        }
gm_lb45:;
                        {
                            uint8_t t219 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t220 = (uint8_t)(t219 == ((uint8_t)35ULL));
                            if (!t220) {
                                goto gm_lb46;
                            }
                            goto gm_lb37;
                        }
gm_lb46:;
                        {
                            uint8_t t221 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t222 = (uint8_t)(t221 == ((uint8_t)36ULL));
                            if (!t222) {
                                goto gm_lb47;
                            }
                            goto gm_lb37;
                        }
gm_lb47:;
                        goto gm_lb36;
                    }
gm_lb37:;
                    void * t223 = gm_l0;
                    gm_tl874t2atnlh3 t224 = gm_l1;
                    gm_t9qkh0rsooc8d t225 = gm_l2;
                    gm_b16_8 t226 = gm_l3;
                    gm_b16_8 t227 = gm_l4;
                    gm_f_sys_net_aff7h5l((void *)&gm_l18, (void *)NULL, cx, NULL, t223, t224, t225, t226, t227);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t228 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                    if (t228) {
                    } else {
                        gm_t76123gfet73g t229 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l18) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t229);
                        goto gm_fdone;
                    }
                    int64_t t230 = GM_LD(int64_t, (((uint8_t *)&gm_l18) + 8));
                    gm_l6 = t230;
                    goto gm_lb1;
                }
gm_lb36:;
                {
                    {
                        {
                            uint8_t t231 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t232 = (uint8_t)(t231 == ((uint8_t)37ULL));
                            if (!t232) {
                                goto gm_lb50;
                            }
                            goto gm_lb49;
                        }
gm_lb50:;
                        {
                            uint8_t t233 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t234 = (uint8_t)(t233 == ((uint8_t)38ULL));
                            if (!t234) {
                                goto gm_lb51;
                            }
                            goto gm_lb49;
                        }
gm_lb51:;
                        {
                            uint8_t t235 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t236 = (uint8_t)(t235 == ((uint8_t)39ULL));
                            if (!t236) {
                                goto gm_lb52;
                            }
                            goto gm_lb49;
                        }
gm_lb52:;
                        {
                            uint8_t t237 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t238 = (uint8_t)(t237 == ((uint8_t)40ULL));
                            if (!t238) {
                                goto gm_lb53;
                            }
                            goto gm_lb49;
                        }
gm_lb53:;
                        {
                            uint8_t t239 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t240 = (uint8_t)(t239 == ((uint8_t)41ULL));
                            if (!t240) {
                                goto gm_lb54;
                            }
                            goto gm_lb49;
                        }
gm_lb54:;
                        {
                            uint8_t t241 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t242 = (uint8_t)(t241 == ((uint8_t)43ULL));
                            if (!t242) {
                                goto gm_lb55;
                            }
                            goto gm_lb49;
                        }
gm_lb55:;
                        {
                            uint8_t t243 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t244 = (uint8_t)(t243 == ((uint8_t)42ULL));
                            if (!t244) {
                                goto gm_lb56;
                            }
                            goto gm_lb49;
                        }
gm_lb56:;
                        goto gm_lb48;
                    }
gm_lb49:;
                    void * t245 = gm_l0;
                    gm_tl874t2atnlh3 t246 = gm_l1;
                    gm_t9qkh0rsooc8d t247 = gm_l2;
                    gm_b16_8 t248 = gm_l3;
                    gm_b16_8 t249 = gm_l4;
                    gm_f_sys_proc_jhdp277((void *)&gm_l19, (void *)NULL, cx, NULL, t245, t246, t247, t248, t249);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t250 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                    if (t250) {
                    } else {
                        gm_t76123gfet73g t251 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l19) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t251);
                        goto gm_fdone;
                    }
                    int64_t t252 = GM_LD(int64_t, (((uint8_t *)&gm_l19) + 8));
                    gm_l6 = t252;
                    goto gm_lb1;
                }
gm_lb48:;
            }
            {
                {
                    uint8_t t253 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t254 = (uint8_t)(t253 == ((uint8_t)44ULL));
                    if (!t254) {
                        goto gm_lb57;
                    }
                    void * t255 = gm_l0;
                    gm_tl874t2atnlh3 t256 = gm_l1;
                    gm_t9qkh0rsooc8d t257;
                    GM_ST(uint8_t, ((uint8_t *)&t257 + 0), ((uint8_t)2ULL));
                    gm_b16_8 t258 = gm_f_empty_slice_2p7d2kv();
                    gm_b16_8 t259 = GM_LD(gm_b16_8, ((uint8_t *)&t258));
                    gm_b16_8 t260 = gm_l5;
                    gm_f_perform_native_2ppar83((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t255, t256, t257, ((uint32_t)0ULL), t259, t260);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb57:;
                {
                    uint8_t t261 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t262 = (uint8_t)(t261 == ((uint8_t)45ULL));
                    if (!t262) {
                        goto gm_lb58;
                    }
                    void * t263 = gm_l0;
                    gm_tl874t2atnlh3 t264 = gm_l1;
                    gm_b16_8 t265 = gm_l4;
                    gm_b16_8 t266 = gm_l5;
                    gm_t9qkh0rsooc8d t267;
                    GM_ST(uint8_t, ((uint8_t *)&t267 + 0), ((uint8_t)2ULL));
                    gm_f_perform_native_2ppar83((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t263, t264, t267, ((uint32_t)1ULL), t265, t266);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb58:;
                {
                    uint8_t t268 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t269 = (uint8_t)(t268 == ((uint8_t)46ULL));
                    if (!t269) {
                        goto gm_lb59;
                    }
                    void * t270 = gm_l0;
                    void * t271 = gm_l0;
                    void * t272 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t273 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t273)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t273, gm_srcn3jjc71a2bv3, 184, 52);
                    gm_b16_8 t274 = GM_LD(gm_b16_8, (((uint8_t *)t272) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t275 = GM_LD(gm_b16_8, ((uint8_t *)&t274));
                    unsigned __int128 t276 = gm_f_get_uint_bq8c4b7(t271, t275);
                    uint8_t t277 = (uint8_t)t276;
                    gm_f_env_exit_prbvpjt((void *)&(*(gm_tcfa6l7reoosn *)gm_out), t270, t277);
                    goto gm_fdone;
                }
gm_lb59:;
                {
                    void * t278 = gm_l0;
                    gm_tl874t2atnlh3 t279 = gm_l1;
                    gm_b16_8 t280;
                    GM_ST(void *, (uint8_t *)&t280, (void *)(gm_s27n8348bef64));
                    GM_ST(uintptr_t, (uint8_t *)&t280 + 8, (uintptr_t)(62));
                    gm_t76123gfet73g t281;
                    gm_f_trap_qh84l9d((void *)&t281, (void *)NULL, cx, NULL, t278, t279, t280);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t281);
                    goto gm_fdone;
                }
gm_lb60:;
                gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 155, 5);
            }
        }
gm_lb1:;
    }
    {
        void * t282 = gm_l0;
        gm_b16_8 t283 = gm_l5;
        int64_t t284 = gm_l6;
        gm_f_sys_ret_vpa2k8i(t282, t283, t284);
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
inline __attribute__((aligned(4))) void gm_f_sys_src_5svsr6i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_sys_src_5svsr6i F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* b */
    gm_t2lqu8jsj511d gm_l3; /* sp */
    gm_b16_8 gm_l5; /* v */
    GM_STACK_CHECK(&F, 281u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t934k1j7u70ct gm_l6;
    void * t2 = gm_l0;
    gm_b16_8 t3 = gm_l2;
    gm_t2lqu8jsj511d t4 = gm_f_slice_parts_1se5hu4(t2, t3);
    gm_l3 = t4;
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 184);
        uint64_t t7 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        gm_f_read_u6r4vns((void *)&gm_l6, t6, t7, t8, ((uint64_t)1ULL));
    }
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (t9) {
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
        gm_l5 = t10;
    } else {
        {
            memmove(((uint8_t *)&F.l4), (((uint8_t *)&gm_l6) + 8), 40);
        }
        {
            void * t11 = gm_l0;
            gm_tl874t2atnlh3 t12 = gm_l1;
            gm_t76123gfet73g t13;
            gm_f_mem_trap_ecogko0((void *)&t13, (void *)NULL, cx, NULL, t11, t12, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l4)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 8), t13);
            goto gm_fdone;
        }
    }
    {
        gm_b16_8 t14 = gm_l5;
        gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 8), t15);
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
inline __attribute__((aligned(4))) void gm_f_sys_state_cvl1lvl(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_sys_state_cvl1lvl F;
    GM_STACK_CHECK(&F, 296u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t4c7ipjj9s3fp t2;
    gm_f_hash_map_3sndrtb((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t3;
    gm_f_hash_map_irh77t1((void *)&t3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t4;
    gm_f_hash_map_abi2bn7((void *)&t4, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t5;
    gm_f_hash_map_ql49boo((void *)&t5, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t6;
    gm_f_hash_map_3d5nsdr((void *)&t6, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tf5srh4mp4b3i t7;
    memset(&t7, 0, sizeof t7);
    GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&(*(gm_ttse10gjbhca2 *)gm_out) + 0), t2);
    GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&(*(gm_ttse10gjbhca2 *)gm_out) + 56), t3);
    GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&(*(gm_ttse10gjbhca2 *)gm_out) + 112), t4);
    GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&(*(gm_ttse10gjbhca2 *)gm_out) + 168), t5);
    GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&(*(gm_ttse10gjbhca2 *)gm_out) + 224), t6);
    GM_ST(int64_t, ((uint8_t *)&(*(gm_ttse10gjbhca2 *)gm_out) + 280), ((int64_t)0LL));
    GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&(*(gm_ttse10gjbhca2 *)gm_out) + 288), t7);
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
extern __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_sys_stdio_nsl9o9u(uint32_t a0);
__attribute__((aligned(4))) void gm_f_sys_strings_r85ragc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_sys_strings_r85ragc F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* b */
    gm_t2lqu8jsj511d gm_l3; /* sp */
    uint64_t gm_l4; /* ps */
    gm_b16_8 gm_l6; /* items */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    gm_b16_8 gm_l11; /* s */
    GM_STACK_CHECK(&F, 756u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t934k1j7u70ct gm_l12;
    gm_td0ept12e7q8b gm_l13;
    gm_tftb1odc739t5 gm_l14;
    void * t7 = gm_l0;
    gm_b16_8 t8 = gm_l2;
    gm_t2lqu8jsj511d t9 = gm_f_slice_parts_1se5hu4(t7, t8);
    gm_l3 = t9;
    void * t10 = gm_l0;
    uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)t10) + 176));
    uint64_t t12 = gm_f_us_cuv01fp(t11);
    gm_l4 = t12;
    {
        void * t13 = gm_l0;
        void * t14 = (void *)(((uint8_t *)t13) + 184);
        uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        uint64_t t17 = gm_l4;
        uint64_t r18; bool o19 = __builtin_mul_overflow(((uint64_t)2ULL), t17, &r18);
        if (o19) gm_trap("integer overflow in `*`", gm_srcn3jjc71a2bv3, 143, 47);
        uint64_t t20 = r18;
        uint64_t t21 = (uint64_t)t20;
        uint64_t r22; bool o23 = __builtin_mul_overflow(t16, t21, &r22);
        uint64_t t24 = o23 ? (1 ? ((uint64_t)18446744073709551615ULL) : ((uint64_t)0ULL)) : r22;
        gm_f_read_u6r4vns((void *)&gm_l12, t14, t15, t24, ((uint64_t)1ULL));
    }
    uint8_t t25 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
    if (t25) {
        gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
        gm_l6 = t26;
    } else {
        {
            memmove(((uint8_t *)&F.l5), (((uint8_t *)&gm_l12) + 8), 40);
        }
        {
            void * t27 = gm_l0;
            gm_tl874t2atnlh3 t28 = gm_l1;
            gm_t76123gfet73g t29;
            gm_f_mem_trap_ecogko0((void *)&t29, (void *)NULL, cx, NULL, t27, t28, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l5)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t9922qjstblh1 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t9922qjstblh1 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t9922qjstblh1 *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t9922qjstblh1 *)gm_out) + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t9922qjstblh1 *)gm_out) + 40, 0);
            ((uint8_t *)&(*(gm_t9922qjstblh1 *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t9922qjstblh1 *)gm_out) + 8), t29);
            goto gm_fdone;
        }
    }
    {
        gm_trqfvmjtt4sij t30;
        gm_f_list_epvducg((void *)&t30, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l7 = t30;
    }
    gm_l9 = ((uint64_t)0ULL);
    uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint64_t t32 = gm_f_us_cuv01fp(t31);
    gm_l8 = t32;
    {
        for (;;) {
            uint64_t t33 = gm_l9;
            uint64_t t34 = gm_l8;
            uint8_t t35 = (t33 < t34);
            if (!t35) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t36 = gm_l9;
                        gm_l10 = t36;
                        void * t37 = gm_l0;
                        gm_tl874t2atnlh3 t38 = gm_l1;
                        void * t39 = GM_LD(void *, ((uint8_t *)&gm_l6));
                        uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                        uint64_t t41 = gm_l10;
                        uint64_t r42; bool o43 = __builtin_mul_overflow(t41, ((uint64_t)2ULL), &r42);
                        if (o43) gm_trap("integer overflow in `*`", gm_srcn3jjc71a2bv3, 146, 40);
                        uint64_t t44 = r42;
                        uint64_t t45 = gm_l4;
                        uint64_t r46; bool o47 = __builtin_mul_overflow(t44, t45, &r46);
                        if (o47) gm_trap("integer overflow in `*`", gm_srcn3jjc71a2bv3, 146, 40);
                        uint64_t t48 = r46;
                        uint64_t t49 = gm_l10;
                        uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
                        if (o51) gm_trap("integer overflow in `+`", gm_srcn3jjc71a2bv3, 146, 53);
                        uint64_t t52 = r50;
                        uint64_t r53; bool o54 = __builtin_mul_overflow(t52, ((uint64_t)2ULL), &r53);
                        if (o54) gm_trap("integer overflow in `*`", gm_srcn3jjc71a2bv3, 146, 52);
                        uint64_t t55 = r53;
                        uint64_t t56 = gm_l4;
                        uint64_t r57; bool o58 = __builtin_mul_overflow(t55, t56, &r57);
                        if (o58) gm_trap("integer overflow in `*`", gm_srcn3jjc71a2bv3, 146, 52);
                        uint64_t t59 = r57;
                        uint64_t t60 = (uintptr_t)t48;
                        uint64_t t61 = (uintptr_t)t59;
                        if (t60 > t61 || t61 > (uintptr_t)t40) gm_trap_slice(t60, t61, t40, gm_srcn3jjc71a2bv3, 146, 34);
                        uint64_t t62 = ((uintptr_t)t59) - (uintptr_t)t48;
                        gm_b16_8 t63;
                        GM_ST(void *, (uint8_t *)&t63, (void *)(((uint8_t *)t39 + (uintptr_t)t48 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(t62));
                        gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&t63));
                        gm_f_sys_src_5svsr6i((void *)&gm_l13, (void *)NULL, cx, NULL, t37, t38, t64);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t65 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                        if (t65) {
                        } else {
                            gm_t76123gfet73g t66 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l13) + 8));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t9922qjstblh1 *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t9922qjstblh1 *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t9922qjstblh1 *)gm_out) + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t9922qjstblh1 *)gm_out) + 32, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t9922qjstblh1 *)gm_out) + 40, 0);
                            ((uint8_t *)&(*(gm_t9922qjstblh1 *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t9922qjstblh1 *)gm_out) + 8), t66);
                            goto gm_fdone;
                        }
                        gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
                        gm_l11 = t67;
                        void * t68 = (void *)((uint8_t *)&F.l7);
                        gm_b16_8 t69 = gm_l11;
                        gm_tftb1odc739t5 t70 = gm_f_push_3ef746b(t68, t69);
                        gm_l14 = t70;
                        uint8_t t71 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                        if (!t71) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_srcn3jjc71a2bv3, 147, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 145, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t72 = gm_l9;
            uint64_t r73; bool o74 = __builtin_add_overflow(t72, ((uint64_t)1ULL), &r73);
            uint64_t t75 = r73;
            gm_l9 = t75;
        }
    }
gm_lb1:;
    {
        gm_trqfvmjtt4sij t76 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l7));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t9922qjstblh1 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t9922qjstblh1 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t9922qjstblh1 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t9922qjstblh1 *)gm_out))[0] = 0;
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_t9922qjstblh1 *)gm_out) + 8), t76);
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
extern __attribute__((aligned(4))) int64_t gm_f_sys_unit_nrsqciq(gm_ttln0d0hd70en a0);

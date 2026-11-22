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
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t f5; } gm_t4q2b5ibsrdik;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; int32_t v1; } f1; } gm_tsoiri44d0i6q;
typedef struct { uint64_t f0; uint8_t f1; uint8_t p2[7]; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; uint8_t f5; uint8_t f6; uint8_t pe[6]; } gm_t0rfe1ou14h21;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_b16_8 v1; } f1; } gm_t3ujhu6qph7de;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
_Static_assert(sizeof(gm_t4q2b5ibsrdik) == 6 && _Alignof(gm_t4q2b5ibsrdik) == 1 && offsetof(gm_t4q2b5ibsrdik, f0) == 0 && offsetof(gm_t4q2b5ibsrdik, f1) == 1 && offsetof(gm_t4q2b5ibsrdik, f2) == 2 && offsetof(gm_t4q2b5ibsrdik, f3) == 3 && offsetof(gm_t4q2b5ibsrdik, f4) == 4 && offsetof(gm_t4q2b5ibsrdik, f5) == 5, "layout of `OpenOptions`");
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; int64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t5j14m8ru132p;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_t9qkh0rsooc8d v1; } f1; } gm_tj5jn79kq9pnm;
typedef struct { gm_b16_8 a[5]; } gm_tjgkb0mi112kg;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint8_t f3; uint8_t pe[7]; } gm_ts1lka1v5g5ua;
_Static_assert(sizeof(gm_tsoiri44d0i6q) == 8 && _Alignof(gm_tsoiri44d0i6q) == 4 && offsetof(gm_tsoiri44d0i6q, f0) == 0 && offsetof(gm_tsoiri44d0i6q, f1) == 4, "layout of `ExitStatus`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t4ce0jd3tdro4 v0; } f1; } gm_ttiiv5vhiuvvr;
_Static_assert(sizeof(gm_t0rfe1ou14h21) == 64 && _Alignof(gm_t0rfe1ou14h21) == 8 && offsetof(gm_t0rfe1ou14h21, f0) == 0 && offsetof(gm_t0rfe1ou14h21, f1) == 8 && offsetof(gm_t0rfe1ou14h21, f2) == 16 && offsetof(gm_t0rfe1ou14h21, f3) == 24 && offsetof(gm_t0rfe1ou14h21, f4) == 40 && offsetof(gm_t0rfe1ou14h21, f5) == 56 && offsetof(gm_t0rfe1ou14h21, f6) == 57, "layout of `Target`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; uint8_t p3[5]; gm_th2lqbo77pvsc f3; gm_b16_8 f4; uint8_t f5; uint8_t f6; uint8_t p7[6]; gm_th2lqbo77pvsc f7; } gm_t32dckm4jfarl;
_Static_assert(sizeof(gm_t3ujhu6qph7de) == 24 && _Alignof(gm_t3ujhu6qph7de) == 8 && offsetof(gm_t3ujhu6qph7de, f0) == 0 && offsetof(gm_t3ujhu6qph7de, f1) == 8, "layout of `string!string`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_trqfvmjtt4sij v1; } f1; } gm_t470aqkia5212;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
_Static_assert(sizeof(gm_t5j14m8ru132p) == 32 && _Alignof(gm_t5j14m8ru132p) == 8 && offsetof(gm_t5j14m8ru132p, f0) == 0 && offsetof(gm_t5j14m8ru132p, f1) == 8 && offsetof(gm_t5j14m8ru132p, f2) == 16 && offsetof(gm_t5j14m8ru132p, f3) == 24, "layout of `FileInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t5j14m8ru132p v1; } f1; } gm_tih6h4avv5nrb;
_Static_assert(sizeof(gm_tj5jn79kq9pnm) == 12 && _Alignof(gm_tj5jn79kq9pnm) == 4 && offsetof(gm_tj5jn79kq9pnm, f0) == 0 && offsetof(gm_tj5jn79kq9pnm, f1) == 4, "layout of `ReadError`");
_Static_assert(sizeof(gm_tjgkb0mi112kg) == 80 && _Alignof(gm_tjgkb0mi112kg) == 8 && offsetof(gm_tjgkb0mi112kg, a) == 0, "layout of `[5]string`");
typedef struct { gm_tsoiri44d0i6q f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; uint8_t f3; uint8_t pe[7]; } gm_tjm4rm50v5mle;
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_ts1lka1v5g5ua v1; } f1; } gm_tm7gsm6lfro1o;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t0rfe1ou14h21 v1; } f1; } gm_tod6pa33o8u9u;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts1lka1v5g5ua) == 56 && _Alignof(gm_ts1lka1v5g5ua) == 8 && offsetof(gm_ts1lka1v5g5ua, f0) == 0 && offsetof(gm_ts1lka1v5g5ua, f1) == 16 && offsetof(gm_ts1lka1v5g5ua, f2) == 32 && offsetof(gm_ts1lka1v5g5ua, f3) == 48, "layout of `Toolchain`");
_Static_assert(sizeof(gm_ttiiv5vhiuvvr) == 16 && _Alignof(gm_ttiiv5vhiuvvr) == 8 && offsetof(gm_ttiiv5vhiuvvr, f0) == 0 && offsetof(gm_ttiiv5vhiuvvr, f1) == 8, "layout of `ParseError`");
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttiiv5vhiuvvr v0; uint64_t v1; } f1; } gm_tutnubt6u4hoq;
_Static_assert(sizeof(gm_t32dckm4jfarl) == 80 && _Alignof(gm_t32dckm4jfarl) == 8 && offsetof(gm_t32dckm4jfarl, f0) == 0 && offsetof(gm_t32dckm4jfarl, f1) == 1 && offsetof(gm_t32dckm4jfarl, f2) == 2 && offsetof(gm_t32dckm4jfarl, f3) == 8 && offsetof(gm_t32dckm4jfarl, f4) == 32 && offsetof(gm_t32dckm4jfarl, f5) == 48 && offsetof(gm_t32dckm4jfarl, f6) == 49 && offsetof(gm_t32dckm4jfarl, f7) == 56, "layout of `SpawnOptions`");
_Static_assert(sizeof(gm_t470aqkia5212) == 48 && _Alignof(gm_t470aqkia5212) == 8 && offsetof(gm_t470aqkia5212, f0) == 0 && offsetof(gm_t470aqkia5212, f1) == 8, "layout of `string!List(string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tj5jn79kq9pnm v0; gm_tjm4rm50v5mle v1; } f1; } gm_tat31u3lvqjk4;
_Static_assert(sizeof(gm_tih6h4avv5nrb) == 40 && _Alignof(gm_tih6h4avv5nrb) == 8 && offsetof(gm_tih6h4avv5nrb, f0) == 0 && offsetof(gm_tih6h4avv5nrb, f1) == 8, "layout of `IoError!FileInfo`");
_Static_assert(sizeof(gm_tjm4rm50v5mle) == 96 && _Alignof(gm_tjm4rm50v5mle) == 8 && offsetof(gm_tjm4rm50v5mle, f0) == 0 && offsetof(gm_tjm4rm50v5mle, f1) == 8 && offsetof(gm_tjm4rm50v5mle, f2) == 48 && offsetof(gm_tjm4rm50v5mle, f3) == 88, "layout of `Output`");
_Static_assert(sizeof(gm_tm7gsm6lfro1o) == 64 && _Alignof(gm_tm7gsm6lfro1o) == 8 && offsetof(gm_tm7gsm6lfro1o, f0) == 0 && offsetof(gm_tm7gsm6lfro1o, f1) == 8, "layout of `string!Toolchain`");
_Static_assert(sizeof(gm_tod6pa33o8u9u) == 72 && _Alignof(gm_tod6pa33o8u9u) == 8 && offsetof(gm_tod6pa33o8u9u, f0) == 0 && offsetof(gm_tod6pa33o8u9u, f1) == 8, "layout of `string!Target`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
_Static_assert(sizeof(gm_tutnubt6u4hoq) == 24 && _Alignof(gm_tutnubt6u4hoq) == 8 && offsetof(gm_tutnubt6u4hoq, f0) == 0 && offsetof(gm_tutnubt6u4hoq, f1) == 8, "layout of `ParseError!usize`");
_Static_assert(sizeof(gm_tat31u3lvqjk4) == 104 && _Alignof(gm_tat31u3lvqjk4) == 8 && offsetof(gm_tat31u3lvqjk4, f0) == 0 && offsetof(gm_tat31u3lvqjk4, f1) == 8, "layout of `RunError!Output`");
struct gm_fr_any_is_2366lre {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_any_starts_dv2l7a5 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_check_triple_uqaropr {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* parts */
    gm_t0rfe1ou14h21 l9; /* tg */
    gm_tl572d7579hns ret;
    gm_b0_1 w0;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_default_cc_qpj8rre {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_defined_kppjhtk {
    gm_frame h;
    gm_b16_8 l1; /* name */
    uint8_t ret;
    gm_b0_1 w0;
};
struct gm_fr_display_30kc1i9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vu26gaj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_env_var_mbpep74 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_extend_vogsd72 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_for_body_3400t3p {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_g9kn84c {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_i2koblr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_tdd1e60 {
    gm_frame h;
    gm_ttu09lij36f8c l3; /* first */
    gm_b0_1 ret;
};
struct gm_fr_for_safe_build_9nqaffp {
    gm_frame h;
    gm_ts1lka1v5g5ua l0; /* t */
    gm_th2lqbo77pvsc l1; /* found */
    gm_tm7gsm6lfro1o ret;
    gm_b0_1 w0;
};
struct gm_fr_for_target_439caa2 {
    gm_frame h;
    gm_tm7gsm6lfro1o ret;
};
struct gm_fr_for_triple_ok3tm31 {
    gm_frame h;
    gm_ts1lka1v5g5ua l7; /* t */
    gm_trqfvmjtt4sij l13; /* words */
    gm_ts1lka1v5g5ua l14; /* t */
    gm_tm7gsm6lfro1o ret;
};
struct gm_fr_format_0acc90p {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_13mdm96 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_2g98t2r {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_2lgoflc {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5htdr0o {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7dijd7n {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_8pjgcns {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_blcbfcc {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dl7scf9 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_g2mro4q {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_gshr6sq {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_mcmtfgd {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_n1doesn {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_o4fb8d3 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_oaqgi6o {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_q3io8lg {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ra2b58j {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_seuq78o {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_svnhg71 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_from_env_rukl5lk {
    gm_frame h;
    gm_tm7gsm6lfro1o ret;
};
struct gm_fr_from_vars_r2mjdrk {
    gm_frame h;
    gm_ts1lka1v5g5ua l3; /* t */
    gm_trqfvmjtt4sij l6; /* words */
    gm_trqfvmjtt4sij l9; /* words */
    gm_trqfvmjtt4sij l12; /* words */
    gm_tm7gsm6lfro1o ret;
};
struct gm_fr_has_c7mpl87 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_has_white_space_569v0vo {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_hash_map_p3t5tj8 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_host_from_env_qkl08nm {
    gm_frame h;
    gm_ts1lka1v5g5ua l2; /* t */
    gm_tm7gsm6lfro1o ret;
};
struct gm_fr_io_message_p7jbrea {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_is_default_24c3cui {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_file_tcuv9rv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_macros_hrin9id {
    gm_frame h;
    gm_tjgkb0mi112kg l1; /* tail */
    gm_trqfvmjtt4sij l2; /* argv */
    gm_tjm4rm50v5mle l4; /* out */
    gm_t3ujhu6qph7de ret;
};
struct gm_fr_num_1gcf1u8 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_optimizing_cc_ego0let {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_quote_word_3e35l5m {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_run_error_gs1dinp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_runner_for_79o4h4m {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* runner */
    gm_trqfvmjtt4sij l2; /* words */
    gm_t470aqkia5212 ret;
};
struct gm_fr_runner_from_env_bnildq8 {
    gm_frame h;
    gm_t470aqkia5212 ret;
};
struct gm_fr_split_words_jk1lams {
    gm_frame h;
    gm_t470aqkia5212 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_strip_wasm_trap_report_h977b8r {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_success_q3lb6vt {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_target_72cra6q {
    gm_frame h;
    gm_tod6pa33o8u9u ret;
};
struct gm_fr_target_for_omoprob {
    gm_frame h;
    gm_t0rfe1ou14h21 l4; /* tg */
    gm_tod6pa33o8u9u ret;
};
struct gm_fr_target_from_macros_hkgk1pc {
    gm_frame h;
    gm_t4c7ipjj9s3fp l1; /* defs */
    gm_tod6pa33o8u9u ret;
    gm_b0_1 w0;
};
struct gm_fr_trim_fdrfe6i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_value_3oqtvqn {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_with_manifest_flags_objbdaj {
    gm_frame h;
    gm_ts1lka1v5g5ua l0; /* t */
    gm_ts1lka1v5g5ua l3; /* r */
    gm_trqfvmjtt4sij l4; /* words */
    gm_trqfvmjtt4sij l5; /* words */
    gm_ts1lka1v5g5ua ret;
};
struct gm_fr_words_of_f1gud4i {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* l */
    gm_b16_8 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eqb2nno7n67r(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
inline bool gm_eqmpqhhfmb90(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) uint8_t gm_f_any_is_2366lre(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_any_starts_dv2l7a5(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_check_triple_uqaropr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_command_b2u33fl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_default_cc_qpj8rre(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint8_t gm_f_defined_kppjhtk(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_display_30kc1i9(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vu26gaj(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_env_var_mbpep74(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_vogsd72(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_file_name_em7v8lv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_3400t3p(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_g9kn84c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_i2koblr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_tdd1e60(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_for_safe_build_9nqaffp(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_for_target_439caa2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_for_triple_ok3tm31(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1, const void * a2, const void * a3, uint8_t a4, uint8_t a5);
inline __attribute__((aligned(4))) void gm_f_format_0acc90p(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_13mdm96(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_2g98t2r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_2lgoflc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_5htdr0o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_7dijd7n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_8pjgcns(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_blcbfcc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_dl7scf9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_g2mro4q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_gshr6sq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_mcmtfgd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_n1doesn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_n3u30k9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_o4fb8d3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_oaqgi6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_q3io8lg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ra2b58j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_seuq78o(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_svnhg71(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_from_env_rukl5lk(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_from_vars_r2mjdrk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1, const void * a2);
__attribute__((aligned(4))) void gm_f_get_e3joqcc(void *gm_out, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_c7mpl87(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_has_white_space_569v0vo(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_hash_map_p3t5tj8(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_host_from_env_qkl08nm(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint8_t a0);
__attribute__((aligned(4))) void gm_f_host_target_vsu78om(void *gm_out);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_io_message_p7jbrea(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0);
__attribute__((aligned(4))) uint8_t gm_f_is_default_24c3cui(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_tcuv9rv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_qveh5b2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_s30klli(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_sssrp43(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_macros_hrin9id(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_num_1gcf1u8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_optimizing_cc_ego0let(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_os_error_text_319eh6h(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0);
__attribute__((aligned(4))) void gm_f_output_mg6eie5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) void gm_f_parse_int_8aueeu2(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_program_on_path_fm43kjf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_7shfu9u(void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_quote_posix_56modn2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_quote_word_3e35l5m(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) gm_b16_8 gm_f_run_error_gs1dinp(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0);
__attribute__((aligned(4))) void gm_f_runner_for_79o4h4m(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_runner_from_env_bnildq8(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_sq2k3vc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_svmmoet(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_split_once_mqcu1ce(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_split_posix_e978ams(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_split_words_jk1lams(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_strip_prefix_b4949s8(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_strip_wasm_trap_report_h977b8r(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_success_q3lb6vt(gm_tsoiri44d0i6q a0);
__attribute__((aligned(4))) void gm_f_target_72cra6q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_target_for_omoprob(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_target_from_macros_hkgk1pc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_6sjv6rg(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_179crak(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_value_3oqtvqn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_with_manifest_flags_objbdaj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_words_of_f1gud4i(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
extern uint8_t gm_c2tkt0obrcs[16] __attribute__((aligned(8)));
extern uint8_t gm_c695aum3fk1[16] __attribute__((aligned(8)));
extern uint8_t gm_cc7b06pnl1q[1] __attribute__((aligned(1)));
extern uint8_t gm_cl03rhutns3[112] __attribute__((aligned(8)));
extern uint8_t gm_clslitrb510[16] __attribute__((aligned(8)));
extern uint8_t gm_cojk58d4vkf[16] __attribute__((aligned(8)));
extern void *const gm_hl0tthikq3q4[];
extern void *const gm_hl5iavnkj4ht[];
extern void *const gm_hlib8i5oaffv[];
extern void *const gm_hloaeaafij99[];
extern const uint8_t gm_s048hmhpmbi7c[8];
extern const uint8_t gm_s0thiorolmqbb[14];
extern const uint8_t gm_s1d20j48bjjln[12];
extern const uint8_t gm_s1s57v200fem0[6];
extern const uint8_t gm_s1ti14uv3rrcf[65];
extern const uint8_t gm_s24frr2u6q1at[9];
extern const uint8_t gm_s26219ruuua5s[19];
extern const uint8_t gm_s2cgbvfh1b7rp[6];
extern const uint8_t gm_s2ct6k0fls3pb[12];
extern const uint8_t gm_s2gnpj7k4l22n[2];
extern const uint8_t gm_s2kc17j0uoe9m[23];
extern const uint8_t gm_s2to760maglj8[13];
extern const uint8_t gm_s33a8rv0vs9rh[3];
extern const uint8_t gm_s39mljh669qgl[2];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s3pufl09hvbbf[6];
extern const uint8_t gm_s4109lou8dsdj[12];
extern const uint8_t gm_s5ejk09ngvect[3];
extern const uint8_t gm_s5gha6q48spt5[6];
extern const uint8_t gm_s5k56vqvt8t7d[12];
extern const uint8_t gm_s5lr3gdic3o7e[15];
extern const uint8_t gm_s6juej7e0fhbb[8];
extern const uint8_t gm_s7jb41578vpai[24];
extern const uint8_t gm_s86bd4orcs15b[7];
extern const uint8_t gm_s8tkvhpqkq5en[8];
extern const uint8_t gm_s9aa9jihjgf22[9];
extern const uint8_t gm_s9eqc3kak4tak[23];
extern const uint8_t gm_sa9o54tgja188[12];
extern const uint8_t gm_saalafjdd3tnp[9];
extern const uint8_t gm_sar7ovt8nasdo[8];
extern const uint8_t gm_sb4hpcnufos1f[12];
extern const uint8_t gm_sb8829ojmkbv6[6];
extern const uint8_t gm_sbc0kjtg5aiab[5];
extern const uint8_t gm_sbdp6ovojv1tg[10];
extern const uint8_t gm_sbjdqrlfpuoqj[4];
extern const uint8_t gm_sbk48vc8h32c1[9];
extern const uint8_t gm_sblsu018nl4o3[3];
extern const uint8_t gm_sbrifoc7ea21m[7];
extern const uint8_t gm_sc5p6b8d5c87a[13];
extern const uint8_t gm_sc6celu0e642t[6];
extern const uint8_t gm_sc829ikr3c206[10];
extern const uint8_t gm_scgvr0isof71e[9];
extern const uint8_t gm_sch7ep6ht4utr[58];
extern const uint8_t gm_scq91tgm9lph4[6];
extern const uint8_t gm_sdad15q200tmk[4];
extern const uint8_t gm_se006k0nbfdeg[8];
extern const uint8_t gm_se4kub9a3omhd[5];
extern const uint8_t gm_se6fhe7nkildm[18];
extern const uint8_t gm_sennl2fjkcbdb[4];
extern const uint8_t gm_sf6pbim706kre[4];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sffbik0ast0lg[53];
extern const uint8_t gm_sfrku7jk647q0[83];
extern const uint8_t gm_sg1t2674vj2eg[32];
extern const uint8_t gm_sg4edpe0eroav[8];
extern const uint8_t gm_sg90vqvamkjbo[11];
extern const uint8_t gm_sgboe8ukotqh2[51];
extern const uint8_t gm_sgfrcimthamdn[10];
extern const uint8_t gm_sgs7af5k634cn[11];
extern const uint8_t gm_sh5f7b7njpj3l[6];
extern const uint8_t gm_shjpff93a0f25[7];
extern const uint8_t gm_shki2pqrgmpd1[19];
extern const uint8_t gm_si56o26p8hqsl[8];
extern const uint8_t gm_sibidb2jab0ss[14];
extern const uint8_t gm_sidpdvffdra02[14];
extern const uint8_t gm_sj7cc74b29dv3[14];
extern const uint8_t gm_sjdv8icqpfhc5[6];
extern const uint8_t gm_sje4rbc88csdu[7];
extern const uint8_t gm_sjhqrnq2lvjcm[35];
extern const uint8_t gm_sk50arqpf74k6[6];
extern const uint8_t gm_sk754qkda90si[6];
extern const uint8_t gm_skbsro97j503c[7];
extern const uint8_t gm_skkog7g7r3edb[8];
extern const uint8_t gm_skn7gps778eo2[3];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_sl5rf5cfkcmdk[7];
extern const uint8_t gm_sm3fgo8acha61[13];
extern const uint8_t gm_smiidqh71uo72[10];
extern const uint8_t gm_smnkiec8dekcp[8];
extern const uint8_t gm_smuveuet82l27[11];
extern const uint8_t gm_sn2e6t65fstoj[19];
extern const uint8_t gm_sn3ltks10rrv7[8];
extern const uint8_t gm_snl0g0eaf7ift[8];
extern const uint8_t gm_snnagva49avpd[8];
extern const uint8_t gm_snqe7tsf40ume[138];
extern const uint8_t gm_snrk30ug2qmu2[8];
extern const uint8_t gm_sntolo76g17vv[7];
extern const uint8_t gm_so5q4maooc958[35];
extern const uint8_t gm_so9c50giqp3gn[6];
extern const uint8_t gm_sonht3t87vai7[9];
extern const uint8_t gm_soo4rtckloi4d[13];
extern const uint8_t gm_spervk999ikim[8];
extern const uint8_t gm_sps1dthki4jpu[50];
extern const uint8_t gm_sq8qkmb2jr755[7];
extern const uint8_t gm_sqg9do4ma3giu[6];
extern const uint8_t gm_sqro42saen22n[108];
extern const uint8_t gm_sr389r8t7d0mf[11];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_src7u0d314efpdm[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcmm27gepldtcm[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srct4i4cmesm57g[];
extern const uint8_t gm_srol0ktjpbtfv[58];
extern const uint8_t gm_ss3f6irsroj9l[37];
extern const uint8_t gm_ssevq7ii4bscf[10];
extern const uint8_t gm_sspppdgt1j7r3[29];
extern const uint8_t gm_sss60if2anvc8[5];
extern const uint8_t gm_st5oflrv0tmc6[21];
extern const uint8_t gm_stfp83525k3nr[7];
extern const uint8_t gm_su4retifs0sre[9];
extern const uint8_t gm_sup67cuqst8bs[8];
extern const uint8_t gm_suq81kgcl2c91[19];
extern const uint8_t gm_suu5u3potp0p3[7];
extern const uint8_t gm_suvioac7tr94t[10];
extern const uint8_t gm_svgn73l5s66lk[7];
extern const uint8_t gm_svl13be5ca8rt[5];
extern const uint8_t gm_svln5ui59rjil[6];
extern const gm_tmpl gm_tpl0o80ilivc5vg;
extern const gm_tmpl gm_tpl0rhldcdavmcg;
extern const gm_tmpl gm_tpl149k5asebpko;
extern const gm_tmpl gm_tpl18roma1gvhpk;
extern const gm_tmpl gm_tpl5m0ku1mi16i0;
extern const gm_tmpl gm_tpl7hvv6i3t3o0k;
extern const gm_tmpl gm_tplao1juqpqjfv9;
extern const gm_tmpl gm_tplau4cbc1piuvn;
extern const gm_tmpl gm_tplcsb5vnabjejk;
extern const gm_tmpl gm_tple4r0jrrru5m7;
extern const gm_tmpl gm_tple7qj0fr2tlub;
extern const gm_tmpl gm_tplg45fl0f0qtll;
extern const gm_tmpl gm_tplgemvjdfjljaa;
extern const gm_tmpl gm_tplkhnuvgfhr9v5;
extern const gm_tmpl gm_tplnnqg0hvtcqac;
extern const gm_tmpl gm_tplo12j44trajdm;
extern const gm_tmpl gm_tplqk3ejs6j0ne5;
extern const gm_tmpl gm_tplts0sr46c58jl;
extern const gm_tmpl gm_tpluguraohl9kn4;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eqb2nno7n67r(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint64_t, a) == GM_LD(uint64_t, b);
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline bool gm_eqmpqhhfmb90(const uint8_t *a, const uint8_t *b) {
    if (a[8] != b[8]) return false;
    return !a[8] || gm_eqb2nno7n67r(a, b);
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
inline __attribute__((aligned(4))) void gm_f_default_cc_qpj8rre(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_default_cc_qpj8rre F;
    GM_STACK_CHECK_SHALLOW(&F, 0u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_defined_kppjhtk(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_defined_kppjhtk F;
    gm_b16_8 gm_l0; /* macros */
    gm_b16_8 gm_l2; /* <tmp> */
    GM_STACK_CHECK(&F, 66u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    gm_b16_8 t3 = gm_l0;
    gm_l2 = t3;
    {
        gm_node wn4 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlib8i5oaffv, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t7 = gm_l2;
        gm_b0_1 t8 = gm_f_lines_9uns0k2_sssrp43((void *)NULL, (&wn4), NULL, t7);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_display_30kc1i9(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_display_30kc1i9 F;
    void * gm_l0; /* tc */
    GM_STACK_CHECK(&F, 137u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tabb710d9mq2e gm_l1;
    void * t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)t2));
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s3p0rbs9hc3lg));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(1));
    gm_f_join_sfknljb((void *)&gm_l1, (void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t5 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
    if (!t5) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l1) + 8, gm_src7u0d314efpdm, 178, 66);
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 8));
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vu26gaj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vu26gaj F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 40)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_i2koblr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_i2koblr F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* p */
    GM_STACK_CHECK(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l2;
    {
        {
            gm_b16_8 t4 = gm_l0;
            gm_l1 = t4;
            void * t5 = (void *)((uint8_t *)&((struct gm_fr_check_triple_uqaropr *)((&F.h)->up))->l2);
            gm_b16_8 t6 = gm_l1;
            gm_tftb1odc739t5 t7 = gm_f_push_3ef746b(t5, t6);
            gm_l2 = t7;
            uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
            if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srcmm27gepldtcm, 275, 39);
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 275, 5);
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_format_0acc90p(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_0acc90p F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sgboe8ukotqh2));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(50));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplau4cbc1piuvn), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_13mdm96(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_13mdm96 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5k56vqvt8t7d));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(11));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplts0sr46c58jl), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_2g98t2r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_2g98t2r F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sspppdgt1j7r3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(28));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplg45fl0f0qtll), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_2lgoflc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_2lgoflc F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s1s57v200fem0));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(5));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplkhnuvgfhr9v5), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_7dijd7n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7dijd7n F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s7jb41578vpai));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(23));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl18roma1gvhpk), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_8pjgcns(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_8pjgcns F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sch7ep6ht4utr));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(57));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplo12j44trajdm), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_blcbfcc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_blcbfcc F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sqro42saen22n));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(107));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl7hvv6i3t3o0k), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_dl7scf9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_dl7scf9 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_ss3f6irsroj9l));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(36));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl5m0ku1mi16i0), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_g2mro4q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_g2mro4q F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s86bd4orcs15b));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplnnqg0hvtcqac), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_gshr6sq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_gshr6sq F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s9eqc3kak4tak));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(22));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplqk3ejs6j0ne5), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_mcmtfgd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_mcmtfgd F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sg1t2674vj2eg));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(31));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl149k5asebpko), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_n1doesn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_n1doesn F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2kc17j0uoe9m));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(22));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tple4r0jrrru5m7), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_o4fb8d3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_o4fb8d3 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_suq81kgcl2c91));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(18));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplcsb5vnabjejk), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_oaqgi6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_oaqgi6o F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_srol0ktjpbtfv));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(57));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpluguraohl9kn4), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_q3io8lg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_q3io8lg F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sps1dthki4jpu));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(49));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl0o80ilivc5vg), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ra2b58j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ra2b58j F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sjhqrnq2lvjcm));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(34));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl0rhldcdavmcg), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_seuq78o(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_seuq78o F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4ce0jd3tdro4 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sffbik0ast0lg));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(52));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplao1juqpqjfv9), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_svnhg71(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_svnhg71 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2to760maglj8));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(12));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tple7qj0fr2tlub), t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_c7mpl87(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_has_c7mpl87 F;
    gm_b16_8 gm_l0; /* m */
    GM_STACK_CHECK(&F, 69u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    gm_th2lqbo77pvsc gm_l2;
    void * t5 = (void *)((uint8_t *)&((struct gm_fr_target_from_macros_hkgk1pc *)((&F.h)->up))->l1);
    gm_b16_8 t6 = gm_l0;
    gm_f_get_e3joqcc((void *)&gm_l2, t5, t6);
    {
        {
            uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l2))[16] != 0);
            if (t7) {
                goto gm_lb2;
            }
            gm_l1 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l1 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 406, 37);
    }
gm_lb1:;
    uint8_t t8 = gm_l1;
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_map_p3t5tj8(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_p3t5tj8 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_vu26gaj();
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_io_message_p7jbrea(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0) {
    struct gm_fr_io_message_p7jbrea F;
    gm_toe5fmr3vvf4d gm_l0; /* e */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_toe5fmr3vvf4d t2 = gm_l0;
    gm_b16_8 t3 = gm_f_os_error_text_319eh6h((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t3;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_tcuv9rv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_file_tcuv9rv F;
    gm_b16_8 gm_l0; /* p */
    gm_t5j14m8ru132p gm_l1; /* info */
    GM_STACK_CHECK(&F, 151u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tih6h4avv5nrb gm_l2;
    gm_b16_8 t2 = gm_l0;
    gm_node *pn3 = gm_find(cx, 2126215853);
    if (!pn3) gm_trap("no handler installed for effect `Fs`", gm_srcc2qutubarto1, 234, 13);
    gm_tih6h4avv5nrb t4;
    ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[5])((void *)&t4, pn3->env, pn3->hctx, pn3, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t4;
    uint8_t t5 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (t5) {
        memmove(((uint8_t *)&gm_l1), (((uint8_t *)&gm_l2) + 8), 32);
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    gm_t9qkh0rsooc8d t6 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 8));
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
    uint8_t t8 = (uint8_t)((t7 == ((uint8_t)0ULL)));
    gm_t9qkh0rsooc8d t9;
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)0ULL));
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
inline __attribute__((aligned(4))) void gm_f_optimizing_cc_ego0let(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_optimizing_cc_ego0let F;
    gm_b16_8 gm_l0; /* cc */
    GM_STACK_CHECK(&F, 73u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_th2lqbo77pvsc gm_l1;
    gm_b16_8 t2 = gm_l0;
    uint8_t t3 = gm_f_program_on_path_fm43kjf((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (t3) {
        gm_b16_8 t4 = gm_l0;
        GM_ST(uint8_t, (uint8_t *)&gm_l1 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&gm_l1 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&gm_l1 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&gm_l1), t4);
        ((uint8_t *)&gm_l1)[16] = 1;
    } else {
        memset(&gm_l1, 0, sizeof gm_l1);
    }
    memmove(gm_out, ((uint8_t *)&gm_l1), 24);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_quote_word_3e35l5m(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_quote_word_3e35l5m F;
    gm_b16_8 gm_l0; /* w */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3 = gm_f_quote_posix_56modn2((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t3;
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
inline __attribute__((aligned(4))) void gm_f_split_words_jk1lams(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_split_words_jk1lams F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_f_split_posix_e978ams((void *)&(*(gm_t470aqkia5212 *)gm_out), (void *)NULL, cx, NULL, t2);
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
inline __attribute__((aligned(4))) uint8_t gm_f_success_q3lb6vt(gm_tsoiri44d0i6q a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_success_q3lb6vt F;
    gm_tsoiri44d0i6q gm_l0; /* s */
    uint32_t gm_l1; /* code */
    GM_STACK_CHECK_SHALLOW(&F, 29u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l2;
    {
        {
            uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)0ULL));
            if (!t6) {
                goto gm_lb2;
            }
            uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
            gm_l1 = t7;
            uint32_t t8 = gm_l1;
            uint8_t t9 = ((t8 == ((uint32_t)0ULL)));
            gm_l2 = t9;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)1ULL));
            if (!t11) {
                goto gm_lb3;
            }
            gm_l2 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srct4i4cmesm57g, 65, 12);
    }
gm_lb1:;
    uint8_t t12 = gm_l2;
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_trim_fdrfe6i F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3 = gm_f_trim_start_179crak(t2);
    gm_b16_8 t4 = gm_f_trim_end_6sjv6rg(t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_any_is_2366lre(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_any_is_2366lre F;
    gm_b16_8 gm_l0; /* words */
    gm_b16_8 gm_l1; /* word */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* w */
    GM_STACK_CHECK_SHALLOW(&F, 196u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t7 = gm_l0;
    gm_l2 = t7;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t8 = gm_l3;
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t10 = (t8 < t9);
            if (!t10) {
                goto gm_lb1;
            }
            {
                void * t11 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t13 = gm_l3;
                {
                    {
                        gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t11) + (uintptr_t)t13 * 16));
                        gm_l4 = t14;
                        gm_b16_8 t15 = gm_l4;
                        gm_b16_8 t16 = gm_l1;
                        uint8_t t17 = (gm_eq0iqiubfm7e(((uint8_t *)&t15), ((uint8_t *)&t16)));
                        if (t17) {
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 354, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t18 = gm_l3;
            uint64_t r19; bool o20 = __builtin_add_overflow(t18, ((uint64_t)1ULL), &r19);
            uint64_t t21 = r19;
            gm_l3 = t21;
        }
    }
gm_lb1:;
    F.ret = ((uint8_t)0);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_any_starts_dv2l7a5(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_any_starts_dv2l7a5 F;
    gm_b16_8 gm_l0; /* words */
    gm_b16_8 gm_l1; /* prefix */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* w */
    GM_STACK_CHECK_SHALLOW(&F, 196u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t7 = gm_l0;
    gm_l2 = t7;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t8 = gm_l3;
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t10 = (t8 < t9);
            if (!t10) {
                goto gm_lb1;
            }
            {
                void * t11 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t13 = gm_l3;
                {
                    {
                        gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t11) + (uintptr_t)t13 * 16));
                        gm_l4 = t14;
                        gm_b16_8 t15 = gm_l4;
                        gm_b16_8 t16 = gm_l1;
                        uint8_t t17 = gm_f_starts_with_6vvt01e(t15, t16);
                        if (t17) {
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 347, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t18 = gm_l3;
            uint64_t r19; bool o20 = __builtin_add_overflow(t18, ((uint64_t)1ULL), &r19);
            uint64_t t21 = r19;
            gm_l3 = t21;
        }
    }
gm_lb1:;
    F.ret = ((uint8_t)0);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_check_triple_uqaropr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_check_triple_uqaropr F;
    gm_b16_8 gm_l0; /* triple */
    gm_b16_8 gm_l1; /* macros */
    gm_b16_8 gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* arch */
    gm_b16_8 gm_l5; /* rest */
    gm_b16_8 gm_l6; /* m1 */
    gm_b16_8 gm_l7; /* m2 */
    uint8_t gm_l8; /* big */
    gm_b16_8 gm_l10; /* os_name */
    gm_b16_8 gm_l11; /* os_macro */
    GM_STACK_CHECK(&F, 2939u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l12;
    uint8_t gm_l13;
    gm_tabb710d9mq2e gm_l14;
    gm_tod6pa33o8u9u gm_l15;
    gm_b16_8 gm_l16;
    gm_b16_8 gm_l17;
    gm_tabb710d9mq2e gm_l18;
    gm_t6j6smj7qoctu gm_l19;
    uint8_t gm_l20;
    gm_t6j6smj7qoctu gm_l21;
    gm_t6j6smj7qoctu gm_l22;
    uint8_t gm_l23;
    gm_t6j6smj7qoctu gm_l24;
    gm_t6j6smj7qoctu gm_l25;
    gm_tabb710d9mq2e gm_l26;
    {
        gm_trqfvmjtt4sij t38;
        gm_f_list_epvducg((void *)&t38, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t38;
        gm_b16_8 t39 = gm_l0;
        gm_l3 = t39;
        {
            gm_node wn40 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hloaeaafij99, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
            gm_b16_8 t43 = gm_l3;
            gm_b16_8 t44;
            GM_ST(void *, (uint8_t *)&t44, (void *)(gm_skolmnfc0ccbd));
            GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(1));
            gm_b0_1 t45 = gm_f_split_obtinjv_svmmoet((void *)NULL, (&wn40), NULL, t43, t44);
            if (gm_unw.kind) {
                goto gm_ll42;
            }
        }
        goto gm_ls46;
gm_ll42:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we41; }
        goto gm_lad;
gm_ls46:;
gm_lb1:;
we41:;
        void * t47 = GM_LD(void *, ((uint8_t *)&F.l2));
        uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t48)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t48, gm_srcmm27gepldtcm, 276, 13);
        gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)t47) + (uintptr_t)((uint64_t)0ULL) * 16));
        gm_l4 = t49;
        void * t50 = GM_LD(void *, ((uint8_t *)&F.l2));
        uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
        uint64_t t52 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t53 = (uintptr_t)t51;
        if (t52 > t53 || t53 > (uintptr_t)t51) gm_trap_slice(t52, t53, t51, gm_srcmm27gepldtcm, 277, 13);
        uint64_t t54 = ((uintptr_t)t51) - (uintptr_t)((uint64_t)1ULL);
        gm_b16_8 t55;
        GM_ST(void *, (uint8_t *)&t55, (void *)(((uint8_t *)t50 + (uintptr_t)((uint64_t)1ULL) * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(t54));
        gm_l5 = t55;
        gm_b16_8 t56;
        GM_ST(void *, (uint8_t *)&t56, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(0));
        gm_l6 = t56;
        gm_b16_8 t57;
        GM_ST(void *, (uint8_t *)&t57, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(0));
        gm_l7 = t57;
        gm_l8 = ((uint8_t)0);
        {
            {
                {
                    {
                        gm_b16_8 t58 = gm_l4;
                        gm_b16_8 t59;
                        GM_ST(void *, (uint8_t *)&t59, (void *)(gm_skbsro97j503c));
                        GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(6));
                        uint8_t t60 = (gm_eq0iqiubfm7e(((uint8_t *)&t58), ((uint8_t *)&t59)));
                        if (!t60) {
                            goto gm_lb5;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    {
                        gm_b16_8 t61 = gm_l4;
                        gm_b16_8 t62;
                        GM_ST(void *, (uint8_t *)&t62, (void *)(gm_sk50arqpf74k6));
                        GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(5));
                        uint8_t t63 = (gm_eq0iqiubfm7e(((uint8_t *)&t61), ((uint8_t *)&t62)));
                        if (!t63) {
                            goto gm_lb6;
                        }
                        goto gm_lb4;
                    }
gm_lb6:;
                    {
                        gm_b16_8 t64 = gm_l4;
                        gm_b16_8 t65;
                        GM_ST(void *, (uint8_t *)&t65, (void *)(gm_s8tkvhpqkq5en));
                        GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(7));
                        uint8_t t66 = (gm_eq0iqiubfm7e(((uint8_t *)&t64), ((uint8_t *)&t65)));
                        if (!t66) {
                            goto gm_lb7;
                        }
                        goto gm_lb4;
                    }
gm_lb7:;
                    goto gm_lb3;
                }
gm_lb4:;
                gm_b16_8 t67;
                GM_ST(void *, (uint8_t *)&t67, (void *)(gm_smuveuet82l27));
                GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(10));
                gm_l6 = t67;
                goto gm_lb2;
            }
gm_lb3:;
            {
                {
                    {
                        gm_b16_8 t68 = gm_l4;
                        gm_b16_8 t69;
                        GM_ST(void *, (uint8_t *)&t69, (void *)(gm_sg4edpe0eroav));
                        GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(7));
                        uint8_t t70 = (gm_eq0iqiubfm7e(((uint8_t *)&t68), ((uint8_t *)&t69)));
                        if (!t70) {
                            goto gm_lb10;
                        }
                        goto gm_lb9;
                    }
gm_lb10:;
                    {
                        gm_b16_8 t71 = gm_l4;
                        gm_b16_8 t72;
                        GM_ST(void *, (uint8_t *)&t72, (void *)(gm_s3pufl09hvbbf));
                        GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(5));
                        uint8_t t73 = (gm_eq0iqiubfm7e(((uint8_t *)&t71), ((uint8_t *)&t72)));
                        if (!t73) {
                            goto gm_lb11;
                        }
                        goto gm_lb9;
                    }
gm_lb11:;
                    {
                        gm_b16_8 t74 = gm_l4;
                        gm_b16_8 t75;
                        GM_ST(void *, (uint8_t *)&t75, (void *)(gm_suu5u3potp0p3));
                        GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(6));
                        uint8_t t76 = (gm_eq0iqiubfm7e(((uint8_t *)&t74), ((uint8_t *)&t75)));
                        if (!t76) {
                            goto gm_lb12;
                        }
                        goto gm_lb9;
                    }
gm_lb12:;
                    goto gm_lb8;
                }
gm_lb9:;
                gm_b16_8 t77;
                GM_ST(void *, (uint8_t *)&t77, (void *)(gm_s2ct6k0fls3pb));
                GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(11));
                gm_l6 = t77;
                goto gm_lb2;
            }
gm_lb8:;
            {
                {
                    {
                        gm_b16_8 t78 = gm_l4;
                        gm_b16_8 t79;
                        GM_ST(void *, (uint8_t *)&t79, (void *)(gm_sg90vqvamkjbo));
                        GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(10));
                        uint8_t t80 = (gm_eq0iqiubfm7e(((uint8_t *)&t78), ((uint8_t *)&t79)));
                        if (!t80) {
                            goto gm_lb15;
                        }
                        goto gm_lb14;
                    }
gm_lb15:;
                    {
                        gm_b16_8 t81 = gm_l4;
                        gm_b16_8 t82;
                        GM_ST(void *, (uint8_t *)&t82, (void *)(gm_s9aa9jihjgf22));
                        GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(8));
                        uint8_t t83 = (gm_eq0iqiubfm7e(((uint8_t *)&t81), ((uint8_t *)&t82)));
                        if (!t83) {
                            goto gm_lb16;
                        }
                        goto gm_lb14;
                    }
gm_lb16:;
                    goto gm_lb13;
                }
gm_lb14:;
                gm_b16_8 t84;
                GM_ST(void *, (uint8_t *)&t84, (void *)(gm_s2ct6k0fls3pb));
                GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(11));
                gm_l6 = t84;
                gm_l8 = ((uint8_t)1);
                goto gm_lb2;
            }
gm_lb13:;
            {
                gm_b16_8 t85 = gm_l4;
                gm_b16_8 t86;
                GM_ST(void *, (uint8_t *)&t86, (void *)(gm_se006k0nbfdeg));
                GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(7));
                uint8_t t87 = (gm_eq0iqiubfm7e(((uint8_t *)&t85), ((uint8_t *)&t86)));
                if (!t87) {
                    goto gm_lb17;
                }
                gm_b16_8 t88;
                GM_ST(void *, (uint8_t *)&t88, (void *)(gm_sup67cuqst8bs));
                GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(7));
                gm_l6 = t88;
                goto gm_lb2;
            }
gm_lb17:;
            {
                gm_b16_8 t89 = gm_l4;
                gm_b16_8 t90;
                GM_ST(void *, (uint8_t *)&t90, (void *)(gm_sb8829ojmkbv6));
                GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)(5));
                uint8_t t91 = (gm_eq0iqiubfm7e(((uint8_t *)&t89), ((uint8_t *)&t90)));
                if (!t91) {
                    goto gm_lb18;
                }
                gm_b16_8 t92;
                GM_ST(void *, (uint8_t *)&t92, (void *)(gm_sgfrcimthamdn));
                GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(9));
                gm_l6 = t92;
                gm_l8 = ((uint8_t)1);
                goto gm_lb2;
            }
gm_lb18:;
            {
                {
                    {
                        gm_b16_8 t93 = gm_l4;
                        gm_b16_8 t94;
                        GM_ST(void *, (uint8_t *)&t94, (void *)(gm_sc829ikr3c206));
                        GM_ST(uintptr_t, (uint8_t *)&t94 + 8, (uintptr_t)(9));
                        uint8_t t95 = (gm_eq0iqiubfm7e(((uint8_t *)&t93), ((uint8_t *)&t94)));
                        if (!t95) {
                            goto gm_lb21;
                        }
                        goto gm_lb20;
                    }
gm_lb21:;
                    {
                        gm_b16_8 t96 = gm_l4;
                        gm_b16_8 t97;
                        GM_ST(void *, (uint8_t *)&t97, (void *)(gm_so9c50giqp3gn));
                        GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(5));
                        uint8_t t98 = (gm_eq0iqiubfm7e(((uint8_t *)&t96), ((uint8_t *)&t97)));
                        if (!t98) {
                            goto gm_lb22;
                        }
                        goto gm_lb20;
                    }
gm_lb22:;
                    goto gm_lb19;
                }
gm_lb20:;
                gm_b16_8 t99;
                GM_ST(void *, (uint8_t *)&t99, (void *)(gm_s0thiorolmqbb));
                GM_ST(uintptr_t, (uint8_t *)&t99 + 8, (uintptr_t)(13));
                gm_l6 = t99;
                gm_b16_8 t100;
                GM_ST(void *, (uint8_t *)&t100, (void *)(gm_suvioac7tr94t));
                GM_ST(uintptr_t, (uint8_t *)&t100 + 8, (uintptr_t)(9));
                gm_l7 = t100;
                gm_l8 = ((uint8_t)1);
                goto gm_lb2;
            }
gm_lb19:;
            {
                {
                    {
                        gm_b16_8 t101 = gm_l4;
                        gm_b16_8 t102;
                        GM_ST(void *, (uint8_t *)&t102, (void *)(gm_sa9o54tgja188));
                        GM_ST(uintptr_t, (uint8_t *)&t102 + 8, (uintptr_t)(11));
                        uint8_t t103 = (gm_eq0iqiubfm7e(((uint8_t *)&t101), ((uint8_t *)&t102)));
                        if (!t103) {
                            goto gm_lb25;
                        }
                        goto gm_lb24;
                    }
gm_lb25:;
                    {
                        gm_b16_8 t104 = gm_l4;
                        gm_b16_8 t105;
                        GM_ST(void *, (uint8_t *)&t105, (void *)(gm_si56o26p8hqsl));
                        GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)(7));
                        uint8_t t106 = (gm_eq0iqiubfm7e(((uint8_t *)&t104), ((uint8_t *)&t105)));
                        if (!t106) {
                            goto gm_lb26;
                        }
                        goto gm_lb24;
                    }
gm_lb26:;
                    goto gm_lb23;
                }
gm_lb24:;
                gm_b16_8 t107;
                GM_ST(void *, (uint8_t *)&t107, (void *)(gm_s0thiorolmqbb));
                GM_ST(uintptr_t, (uint8_t *)&t107 + 8, (uintptr_t)(13));
                gm_l6 = t107;
                gm_b16_8 t108;
                GM_ST(void *, (uint8_t *)&t108, (void *)(gm_suvioac7tr94t));
                GM_ST(uintptr_t, (uint8_t *)&t108 + 8, (uintptr_t)(9));
                gm_l7 = t108;
                goto gm_lb2;
            }
gm_lb23:;
            {
                gm_b16_8 t109 = gm_l4;
                gm_b16_8 t110;
                GM_ST(void *, (uint8_t *)&t110, (void *)(gm_sq8qkmb2jr755));
                GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(6));
                uint8_t t111 = (gm_eq0iqiubfm7e(((uint8_t *)&t109), ((uint8_t *)&t110)));
                if (!t111) {
                    goto gm_lb27;
                }
                gm_b16_8 t112;
                GM_ST(void *, (uint8_t *)&t112, (void *)(gm_sonht3t87vai7));
                GM_ST(uintptr_t, (uint8_t *)&t112 + 8, (uintptr_t)(8));
                gm_l6 = t112;
                gm_b16_8 t113;
                GM_ST(void *, (uint8_t *)&t113, (void *)(gm_sgs7af5k634cn));
                GM_ST(uintptr_t, (uint8_t *)&t113 + 8, (uintptr_t)(10));
                gm_l7 = t113;
                gm_l8 = ((uint8_t)1);
                goto gm_lb2;
            }
gm_lb27:;
            {
                gm_b16_8 t114 = gm_l4;
                gm_b16_8 t115;
                GM_ST(void *, (uint8_t *)&t115, (void *)(gm_s24frr2u6q1at));
                GM_ST(uintptr_t, (uint8_t *)&t115 + 8, (uintptr_t)(8));
                uint8_t t116 = (gm_eq0iqiubfm7e(((uint8_t *)&t114), ((uint8_t *)&t115)));
                if (!t116) {
                    goto gm_lb28;
                }
                gm_b16_8 t117;
                GM_ST(void *, (uint8_t *)&t117, (void *)(gm_sonht3t87vai7));
                GM_ST(uintptr_t, (uint8_t *)&t117 + 8, (uintptr_t)(8));
                gm_l6 = t117;
                gm_b16_8 t118;
                GM_ST(void *, (uint8_t *)&t118, (void *)(gm_sgs7af5k634cn));
                GM_ST(uintptr_t, (uint8_t *)&t118 + 8, (uintptr_t)(10));
                gm_l7 = t118;
                goto gm_lb2;
            }
gm_lb28:;
            {
                gm_b16_8 t119 = gm_l4;
                gm_b16_8 t120;
                GM_ST(void *, (uint8_t *)&t120, (void *)(gm_snl0g0eaf7ift));
                GM_ST(uintptr_t, (uint8_t *)&t120 + 8, (uintptr_t)(7));
                uint8_t t121 = (gm_eq0iqiubfm7e(((uint8_t *)&t119), ((uint8_t *)&t120)));
                if (!t121) {
                    goto gm_lb29;
                }
                gm_b16_8 t122;
                GM_ST(void *, (uint8_t *)&t122, (void *)(gm_sbdp6ovojv1tg));
                GM_ST(uintptr_t, (uint8_t *)&t122 + 8, (uintptr_t)(9));
                gm_l6 = t122;
                gm_l8 = ((uint8_t)1);
                goto gm_lb2;
            }
gm_lb29:;
            {
                gm_b16_8 t123 = gm_l4;
                gm_b16_8 t124;
                GM_ST(void *, (uint8_t *)&t124, (void *)(gm_svgn73l5s66lk));
                GM_ST(uintptr_t, (uint8_t *)&t124 + 8, (uintptr_t)(6));
                uint8_t t125 = (gm_eq0iqiubfm7e(((uint8_t *)&t123), ((uint8_t *)&t124)));
                if (!t125) {
                    goto gm_lb30;
                }
                gm_b16_8 t126;
                GM_ST(void *, (uint8_t *)&t126, (void *)(gm_sr389r8t7d0mf));
                GM_ST(uintptr_t, (uint8_t *)&t126 + 8, (uintptr_t)(10));
                gm_l6 = t126;
                goto gm_lb2;
            }
gm_lb30:;
            {
                gm_b16_8 t127 = gm_l4;
                gm_b16_8 t128;
                GM_ST(void *, (uint8_t *)&t128, (void *)(gm_sb4hpcnufos1f));
                GM_ST(uintptr_t, (uint8_t *)&t128 + 8, (uintptr_t)(11));
                uint8_t t129 = (gm_eq0iqiubfm7e(((uint8_t *)&t127), ((uint8_t *)&t128)));
                if (!t129) {
                    goto gm_lb31;
                }
                gm_b16_8 t130;
                GM_ST(void *, (uint8_t *)&t130, (void *)(gm_sibidb2jab0ss));
                GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(13));
                gm_l6 = t130;
                goto gm_lb2;
            }
gm_lb31:;
            {
                {
                    gm_b16_8 t131 = gm_l0;
                    gm_b16_8 t132 = gm_l4;
                    gm_t6j6smj7qoctu t133;
                    GM_ST(gm_b16_8, ((uint8_t *)&t133 + 0), t131);
                    GM_ST(gm_b16_8, ((uint8_t *)&t133 + 16), t132);
                    gm_f_format_oaqgi6o((void *)&gm_l12, (void *)NULL, cx, NULL, &t133);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t134 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                if (!t134) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_srcmm27gepldtcm, 318, 22);
                {
                    gm_b16_8 t135 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
                    gm_b16_8 t136 = GM_LD(gm_b16_8, ((uint8_t *)&t135));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t136);
                    goto gm_fdone;
                }
            }
gm_lb32:;
            gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 282, 5);
        }
gm_lb2:;
    }
    {
        {
            {
                gm_b16_8 t137 = gm_l1;
                gm_b16_8 t138 = gm_l6;
                uint8_t t139 = gm_f_defined_kppjhtk(t137, t138);
                uint8_t t140 = (uint8_t)!t139;
                if (!t140) {
                    goto gm_lb34;
                }
                uint64_t t141 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint8_t t142 = ((t141 == ((uint64_t)0ULL)));
                if (t142) {
                    gm_l13 = ((uint8_t)1);
                } else {
                    gm_b16_8 t143 = gm_l1;
                    gm_b16_8 t144 = gm_l7;
                    uint8_t t145 = gm_f_defined_kppjhtk(t143, t144);
                    uint8_t t146 = (uint8_t)!t145;
                    gm_l13 = t146;
                }
                uint8_t t147 = gm_l13;
                if (!t147) {
                    goto gm_lb34;
                }
                {
                    gm_b16_8 t148 = gm_l0;
                    gm_b16_8 t149 = gm_l6;
                    gm_t6j6smj7qoctu t150;
                    GM_ST(gm_b16_8, ((uint8_t *)&t150 + 0), t148);
                    GM_ST(gm_b16_8, ((uint8_t *)&t150 + 16), t149);
                    gm_f_format_5htdr0o((void *)&gm_l14, (void *)NULL, cx, NULL, &t150);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t151 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                if (!t151) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_srcmm27gepldtcm, 321, 17);
                {
                    gm_b16_8 t152 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
                    gm_b16_8 t153 = GM_LD(gm_b16_8, ((uint8_t *)&t152));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t153);
                    goto gm_fdone;
                }
            }
gm_lb34:;
        }
gm_lb33:;
        gm_b16_8 t154 = gm_l1;
        gm_f_target_from_macros_hkgk1pc((void *)&gm_l15, (void *)NULL, cx, NULL, t154);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t155 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
        if (t155) {
        } else {
            gm_b16_8 t156 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t156);
            goto gm_fdone;
        }
        memmove(((uint8_t *)&F.l9), (((uint8_t *)&gm_l15) + 8), 64);
        uint8_t t157 = gm_l8;
        uint8_t t158 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 8));
        uint8_t t159 = ((t157 == t158));
        if (t159) {
            uint8_t t160 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 8));
            if (t160) {
                gm_b16_8 t161;
                GM_ST(void *, (uint8_t *)&t161, (void *)(gm_stfp83525k3nr));
                GM_ST(uintptr_t, (uint8_t *)&t161 + 8, (uintptr_t)(6));
                gm_l16 = t161;
            } else {
                gm_b16_8 t162;
                GM_ST(void *, (uint8_t *)&t162, (void *)(gm_sennl2fjkcbdb));
                GM_ST(uintptr_t, (uint8_t *)&t162 + 8, (uintptr_t)(3));
                gm_l16 = t162;
            }
            {
                gm_b16_8 t163 = gm_l16;
                gm_b16_8 t164 = gm_l0;
                uint8_t t165 = gm_l8;
                if (t165) {
                    gm_b16_8 t166;
                    GM_ST(void *, (uint8_t *)&t166, (void *)(gm_sennl2fjkcbdb));
                    GM_ST(uintptr_t, (uint8_t *)&t166 + 8, (uintptr_t)(3));
                    gm_l17 = t166;
                } else {
                    gm_b16_8 t167;
                    GM_ST(void *, (uint8_t *)&t167, (void *)(gm_stfp83525k3nr));
                    GM_ST(uintptr_t, (uint8_t *)&t167 + 8, (uintptr_t)(6));
                    gm_l17 = t167;
                }
                gm_b16_8 t168 = gm_l17;
                gm_tfeab279c16rr t169;
                GM_ST(gm_b16_8, ((uint8_t *)&t169 + 0), t163);
                GM_ST(gm_b16_8, ((uint8_t *)&t169 + 16), t164);
                GM_ST(gm_b16_8, ((uint8_t *)&t169 + 32), t168);
                gm_f_format_0acc90p((void *)&gm_l18, (void *)NULL, cx, NULL, &t169);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t170 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
            if (!t170) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_srcmm27gepldtcm, 325, 17);
            {
                gm_b16_8 t171 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
                gm_b16_8 t172 = GM_LD(gm_b16_8, ((uint8_t *)&t171));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t172);
                goto gm_fdone;
            }
        }
    }
    {
        gm_b16_8 t173;
        GM_ST(void *, (uint8_t *)&t173, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t173 + 8, (uintptr_t)(0));
        gm_l10 = t173;
        gm_b16_8 t174;
        GM_ST(void *, (uint8_t *)&t174, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t174 + 8, (uintptr_t)(0));
        gm_l11 = t174;
        gm_b16_8 t175 = gm_l5;
        gm_b16_8 t176 = GM_LD(gm_b16_8, ((uint8_t *)&t175));
        gm_b16_8 t177;
        GM_ST(void *, (uint8_t *)&t177, (void *)(gm_sk754qkda90si));
        GM_ST(uintptr_t, (uint8_t *)&t177 + 8, (uintptr_t)(5));
        uint8_t t178 = gm_f_any_starts_dv2l7a5(t176, t177);
        if (t178) {
            gm_b16_8 t179;
            GM_ST(void *, (uint8_t *)&t179, (void *)(gm_sk754qkda90si));
            GM_ST(uintptr_t, (uint8_t *)&t179 + 8, (uintptr_t)(5));
            gm_b16_8 t180;
            GM_ST(void *, (uint8_t *)&t180, (void *)(gm_ssevq7ii4bscf));
            GM_ST(uintptr_t, (uint8_t *)&t180 + 8, (uintptr_t)(9));
            GM_ST(gm_b16_8, ((uint8_t *)&gm_l19 + 0), t179);
            GM_ST(gm_b16_8, ((uint8_t *)&gm_l19 + 16), t180);
            gm_b16_8 t181 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l19));
            gm_l10 = t181;
            gm_b16_8 t182 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 16));
            gm_l11 = t182;
        } else {
            gm_b16_8 t183 = gm_l5;
            gm_b16_8 t184 = GM_LD(gm_b16_8, ((uint8_t *)&t183));
            gm_b16_8 t185;
            GM_ST(void *, (uint8_t *)&t185, (void *)(gm_sntolo76g17vv));
            GM_ST(uintptr_t, (uint8_t *)&t185 + 8, (uintptr_t)(6));
            uint8_t t186 = gm_f_any_starts_dv2l7a5(t184, t185);
            if (t186) {
                gm_l20 = ((uint8_t)1);
            } else {
                gm_b16_8 t187 = gm_l5;
                gm_b16_8 t188 = GM_LD(gm_b16_8, ((uint8_t *)&t187));
                gm_b16_8 t189;
                GM_ST(void *, (uint8_t *)&t189, (void *)(gm_sh5f7b7njpj3l));
                GM_ST(uintptr_t, (uint8_t *)&t189 + 8, (uintptr_t)(5));
                uint8_t t190 = gm_f_any_is_2366lre(t188, t189);
                gm_l20 = t190;
            }
            uint8_t t191 = gm_l20;
            if (t191) {
                gm_b16_8 t192;
                GM_ST(void *, (uint8_t *)&t192, (void *)(gm_sntolo76g17vv));
                GM_ST(uintptr_t, (uint8_t *)&t192 + 8, (uintptr_t)(6));
                gm_b16_8 t193;
                GM_ST(void *, (uint8_t *)&t193, (void *)(gm_smiidqh71uo72));
                GM_ST(uintptr_t, (uint8_t *)&t193 + 8, (uintptr_t)(9));
                GM_ST(gm_b16_8, ((uint8_t *)&gm_l21 + 0), t192);
                GM_ST(gm_b16_8, ((uint8_t *)&gm_l21 + 16), t193);
                gm_b16_8 t194 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l21));
                gm_l10 = t194;
                gm_b16_8 t195 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 16));
                gm_l11 = t195;
            } else {
                gm_b16_8 t196 = gm_l5;
                gm_b16_8 t197 = GM_LD(gm_b16_8, ((uint8_t *)&t196));
                gm_b16_8 t198;
                GM_ST(void *, (uint8_t *)&t198, (void *)(gm_snrk30ug2qmu2));
                GM_ST(uintptr_t, (uint8_t *)&t198 + 8, (uintptr_t)(7));
                uint8_t t199 = gm_f_any_starts_dv2l7a5(t197, t198);
                if (t199) {
                    gm_b16_8 t200;
                    GM_ST(void *, (uint8_t *)&t200, (void *)(gm_snrk30ug2qmu2));
                    GM_ST(uintptr_t, (uint8_t *)&t200 + 8, (uintptr_t)(7));
                    gm_b16_8 t201;
                    GM_ST(void *, (uint8_t *)&t201, (void *)(gm_s1d20j48bjjln));
                    GM_ST(uintptr_t, (uint8_t *)&t201 + 8, (uintptr_t)(11));
                    GM_ST(gm_b16_8, ((uint8_t *)&gm_l22 + 0), t200);
                    GM_ST(gm_b16_8, ((uint8_t *)&gm_l22 + 16), t201);
                    gm_b16_8 t202 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l22));
                    gm_l10 = t202;
                    gm_b16_8 t203 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 16));
                    gm_l11 = t203;
                } else {
                    gm_b16_8 t204 = gm_l5;
                    gm_b16_8 t205 = GM_LD(gm_b16_8, ((uint8_t *)&t204));
                    gm_b16_8 t206;
                    GM_ST(void *, (uint8_t *)&t206, (void *)(gm_skkog7g7r3edb));
                    GM_ST(uintptr_t, (uint8_t *)&t206 + 8, (uintptr_t)(7));
                    uint8_t t207 = gm_f_any_starts_dv2l7a5(t205, t206);
                    if (t207) {
                        gm_l23 = ((uint8_t)1);
                    } else {
                        gm_b16_8 t208 = gm_l5;
                        gm_b16_8 t209 = GM_LD(gm_b16_8, ((uint8_t *)&t208));
                        gm_b16_8 t210;
                        GM_ST(void *, (uint8_t *)&t210, (void *)(gm_s6juej7e0fhbb));
                        GM_ST(uintptr_t, (uint8_t *)&t210 + 8, (uintptr_t)(7));
                        uint8_t t211 = gm_f_any_is_2366lre(t209, t210);
                        gm_l23 = t211;
                    }
                    uint8_t t212 = gm_l23;
                    if (t212) {
                        gm_b16_8 t213;
                        GM_ST(void *, (uint8_t *)&t213, (void *)(gm_skkog7g7r3edb));
                        GM_ST(uintptr_t, (uint8_t *)&t213 + 8, (uintptr_t)(7));
                        gm_b16_8 t214;
                        GM_ST(void *, (uint8_t *)&t214, (void *)(gm_sl5rf5cfkcmdk));
                        GM_ST(uintptr_t, (uint8_t *)&t214 + 8, (uintptr_t)(6));
                        GM_ST(gm_b16_8, ((uint8_t *)&gm_l24 + 0), t213);
                        GM_ST(gm_b16_8, ((uint8_t *)&gm_l24 + 16), t214);
                        gm_b16_8 t215 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l24));
                        gm_l10 = t215;
                        gm_b16_8 t216 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 16));
                        gm_l11 = t216;
                    } else {
                        gm_b16_8 t217 = gm_l5;
                        gm_b16_8 t218 = GM_LD(gm_b16_8, ((uint8_t *)&t217));
                        gm_b16_8 t219;
                        GM_ST(void *, (uint8_t *)&t219, (void *)(gm_se4kub9a3omhd));
                        GM_ST(uintptr_t, (uint8_t *)&t219 + 8, (uintptr_t)(4));
                        uint8_t t220 = gm_f_any_starts_dv2l7a5(t218, t219);
                        if (t220) {
                            gm_b16_8 t221;
                            GM_ST(void *, (uint8_t *)&t221, (void *)(gm_sbc0kjtg5aiab));
                            GM_ST(uintptr_t, (uint8_t *)&t221 + 8, (uintptr_t)(4));
                            gm_b16_8 t222;
                            GM_ST(void *, (uint8_t *)&t222, (void *)(gm_scgvr0isof71e));
                            GM_ST(uintptr_t, (uint8_t *)&t222 + 8, (uintptr_t)(8));
                            GM_ST(gm_b16_8, ((uint8_t *)&gm_l25 + 0), t221);
                            GM_ST(gm_b16_8, ((uint8_t *)&gm_l25 + 16), t222);
                            gm_b16_8 t223 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l25));
                            gm_l10 = t223;
                            gm_b16_8 t224 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l25) + 16));
                            gm_l11 = t224;
                        }
                    }
                }
            }
        }
    }
    {
        {
            {
                uint64_t t225 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                uint8_t t226 = (t225 > ((uint64_t)0ULL));
                if (!t226) {
                    goto gm_lb36;
                }
                gm_b16_8 t227 = gm_l1;
                gm_b16_8 t228 = gm_l11;
                uint8_t t229 = gm_f_defined_kppjhtk(t227, t228);
                uint8_t t230 = (uint8_t)!t229;
                if (!t230) {
                    goto gm_lb36;
                }
                {
                    gm_b16_8 t231 = gm_l10;
                    gm_b16_8 t232 = gm_l0;
                    gm_t6j6smj7qoctu t233;
                    GM_ST(gm_b16_8, ((uint8_t *)&t233 + 0), t231);
                    GM_ST(gm_b16_8, ((uint8_t *)&t233 + 16), t232);
                    gm_f_format_dl7scf9((void *)&gm_l26, (void *)NULL, cx, NULL, &t233);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t234 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                if (!t234) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 8, gm_srcmm27gepldtcm, 341, 17);
                {
                    gm_b16_8 t235 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l26) + 8));
                    gm_b16_8 t236 = GM_LD(gm_b16_8, ((uint8_t *)&t235));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t236);
                    goto gm_fdone;
                }
            }
gm_lb36:;
        }
gm_lb35:;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), GM_ZERO(gm_b0_1));
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
extern __attribute__((aligned(4))) uint8_t gm_f_defined_kppjhtk(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_env_var_mbpep74(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_env_var_mbpep74 F;
    gm_b16_8 gm_l0; /* name */
    gm_b16_8 gm_l1; /* v */
    GM_STACK_CHECK(&F, 274u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_th2lqbo77pvsc gm_l2;
    gm_th2lqbo77pvsc gm_l3;
    gm_b16_8 gm_l4;
    {
        gm_b16_8 t2 = gm_l0;
        gm_node *pn3 = gm_find(cx, 621690230);
        if (!pn3) gm_trap("no handler installed for effect `Env`", gm_srcmm27gepldtcm, 37, 10);
        gm_th2lqbo77pvsc t4;
        if (pn3->kind == GM_NATIVE) {
            const uint8_t *ev; size_t evn;
            bool evok = gm_env_var(GM_LD(void *, &t2), GM_LD(uintptr_t, (uint8_t *)&t2 + 8), &ev, &evn);
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(ev));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(evn));
            gm_th2lqbo77pvsc t6;
            GM_ST(uint8_t, (uint8_t *)&t6 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t6 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t6 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t6), t5);
            ((uint8_t *)&t6)[16] = 1;
            gm_th2lqbo77pvsc t7;
            memset(&t7, 0, sizeof t7);
            t4 = evok ? t6 : t7;
        } else {
            ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[1])((void *)&t4, pn3->env, pn3->hctx, pn3, t2);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t4;
    }
    uint8_t t8 = (uint8_t)((((uint8_t *)&gm_l2))[16] != 0);
    if (t8) {
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
        gm_l1 = t9;
    } else {
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
    gm_b16_8 t10 = gm_l1;
    gm_b16_8 t11 = gm_f_trim_fdrfe6i(t10);
    gm_l4 = t11;
    uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
    uint8_t t13 = ((t12 == ((uint64_t)0ULL)));
    if (t13) {
        memset(&gm_l3, 0, sizeof gm_l3);
    } else {
        gm_b16_8 t14 = gm_l1;
        GM_ST(uint8_t, (uint8_t *)&gm_l3 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&gm_l3 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&gm_l3 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&gm_l3), t14);
        ((uint8_t *)&gm_l3)[16] = 1;
    }
    {
        memmove(gm_out, ((uint8_t *)&gm_l3), 24);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_3400t3p(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_3400t3p F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    gm_b16_8 gm_l2; /* rest */
    gm_b16_8 gm_l3; /* name */
    gm_b16_8 gm_l4; /* value */
    gm_t6j6smj7qoctu gm_l5; /* kv */
    GM_STACK_CHECK(&F, 408u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_th2lqbo77pvsc gm_l6;
    gm_ttu09lij36f8c gm_l7;
    gm_tftb1odc739t5 gm_l8;
    {
        {
            gm_b16_8 t6 = gm_l0;
            gm_l1 = t6;
            gm_b16_8 t7 = gm_l1;
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(gm_saalafjdd3tnp));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(8));
            gm_f_strip_prefix_b4949s8((void *)&gm_l6, t7, t8);
            uint8_t t9 = (uint8_t)((((uint8_t *)&gm_l6))[16] != 0);
            if (t9) {
                gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
                gm_l2 = t10;
            } else {
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            gm_b16_8 t11 = gm_l2;
            gm_l3 = t11;
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(0));
            gm_l4 = t12;
            {
                {
                    {
                        gm_b16_8 t13 = gm_l2;
                        gm_b16_8 t14;
                        GM_ST(void *, (uint8_t *)&t14, (void *)(gm_s3p0rbs9hc3lg));
                        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(1));
                        gm_f_split_once_mqcu1ce((void *)&gm_l7, t13, t14);
                    }
                    uint8_t t15 = (uint8_t)((((uint8_t *)&gm_l7))[32] != 0);
                    if (!t15) {
                        goto gm_lb4;
                    }
                    {
                        memmove(((uint8_t *)&gm_l5), ((uint8_t *)&gm_l7), 32);
                    }
                    gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
                    gm_l3 = t16;
                    gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 16));
                    gm_b16_8 t18 = gm_f_trim_fdrfe6i(t17);
                    gm_l4 = t18;
                    goto gm_lb3;
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t20 = (t19 > ((uint64_t)0ULL));
            if (t20) {
                void * t21 = (void *)((uint8_t *)&((struct gm_fr_target_from_macros_hkgk1pc *)((&F.h)->up))->l1);
                gm_b16_8 t22 = gm_l3;
                gm_b16_8 t23 = gm_l4;
                gm_tftb1odc739t5 t24 = gm_f_put_7shfu9u(t21, t22, t23);
                gm_l8 = t24;
                uint8_t t25 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                if (!t25) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srcmm27gepldtcm, 383, 27);
            }
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 375, 5);
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_g9kn84c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_g9kn84c F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* dir */
    gm_b16_8 gm_l2; /* f */
    GM_STACK_CHECK(&F, 172u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l3;
    {
        {
            gm_b16_8 t9 = gm_l0;
            gm_l1 = t9;
            gm_b16_8 t10 = gm_l1;
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(gm_s5gha6q48spt5));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(5));
            gm_b16_8 t12 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t10, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l2 = t12;
            {
                {
                    {
                        {
                            uint8_t t13 = (uint8_t)((((uint8_t *)&((struct gm_fr_for_safe_build_9nqaffp *)((&F.h)->up))->l1))[16] != 0);
                            if (t13) {
                                goto gm_lb6;
                            }
                            gm_l3 = ((uint8_t)1);
                            goto gm_lb5;
                        }
gm_lb6:;
                        {
                            gm_l3 = ((uint8_t)0);
                            goto gm_lb5;
                        }
gm_lb7:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 166, 16);
                    }
gm_lb5:;
                    uint8_t t14 = gm_l3;
                    if (!t14) {
                        goto gm_lb4;
                    }
                    gm_b16_8 t15 = gm_l2;
                    uint8_t t16 = gm_f_is_file_tcuv9rv((void *)NULL, cx, NULL, t15);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (!t16) {
                        goto gm_lb4;
                    }
                    gm_b16_8 t17 = gm_l2;
                    gm_th2lqbo77pvsc t18;
                    GM_ST(uint8_t, (uint8_t *)&t18 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t18 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t18 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t18), t17);
                    ((uint8_t *)&t18)[16] = 1;
                    GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&((struct gm_fr_for_safe_build_9nqaffp *)((&F.h)->up))->l1), t18);
                    goto gm_lb3;
                }
gm_lb4:;
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 164, 9);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_i2koblr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_tdd1e60(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_tdd1e60 F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    gm_b16_8 gm_l2; /* rest */
    gm_t6j6smj7qoctu gm_l4; /* f */
    gm_b16_8 gm_l5; /* word */
    GM_STACK_CHECK_SHALLOW(&F, 284u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_th2lqbo77pvsc gm_l6;
    {
        {
            gm_b16_8 t6 = gm_l0;
            gm_l1 = t6;
            {
                gm_b16_8 t7 = gm_l1;
                gm_b16_8 t8;
                GM_ST(void *, (uint8_t *)&t8, (void *)(gm_saalafjdd3tnp));
                GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(8));
                gm_f_strip_prefix_b4949s8((void *)&gm_l6, t7, t8);
            }
            uint8_t t9 = (uint8_t)((((uint8_t *)&gm_l6))[16] != 0);
            if (t9) {
                gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
                gm_l2 = t10;
            } else {
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            {
                gm_b16_8 t11 = gm_l2;
                gm_b16_8 t12;
                GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s3p0rbs9hc3lg));
                GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(1));
                gm_f_split_once_mqcu1ce((void *)&F.l3, t11, t12);
            }
            {
                {
                    uint8_t t13 = (uint8_t)((((uint8_t *)&F.l3))[32] != 0);
                    if (!t13) {
                        goto gm_lb4;
                    }
                    memmove(((uint8_t *)&gm_l4), ((uint8_t *)&F.l3), 32);
                    gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                    gm_l5 = t14;
                    goto gm_lb3;
                }
gm_lb4:;
                gm_b16_8 t15 = gm_l2;
                gm_l5 = t15;
            }
gm_lb3:;
            gm_b16_8 t16 = gm_l5;
            gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_defined_kppjhtk *)((&F.h)->up))->l1));
            uint8_t t18 = (gm_eq0iqiubfm7e(((uint8_t *)&t16), ((uint8_t *)&t17)));
            if (t18) {
                uint8_t t19 = 0;
                ((struct gm_fr_defined_kppjhtk *)((&F.h)->up))->ret = ((uint8_t)1);
                gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t19;
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 362, 5);
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
inline __attribute__((aligned(4))) void gm_f_for_safe_build_9nqaffp(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_for_safe_build_9nqaffp F;
    gm_b16_8 gm_l2; /* path */
    gm_b16_8 gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* filcc */
    GM_STACK_CHECK(&F, 650u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tm7gsm6lfro1o gm_l5;
    uint8_t gm_l6;
    gm_th2lqbo77pvsc gm_l7;
    gm_th2lqbo77pvsc gm_l8;
    uint8_t gm_l9;
    {
        gm_f_from_env_rukl5lk((void *)&gm_l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t13 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (t13) {
    } else {
        gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
        memset(&(*(gm_tm7gsm6lfro1o *)gm_out), 0, sizeof (*(gm_tm7gsm6lfro1o *)gm_out));
        ((uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out) + 8), t14);
        goto gm_fdone;
    }
    {
        memmove(((uint8_t *)&F.l0), (((uint8_t *)&gm_l5) + 8), 56);
    }
    {
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(gm_s5ejk09ngvect));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(2));
        gm_f_env_var_mbpep74((void *)&gm_l7, (void *)NULL, cx, NULL, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            uint8_t t16 = (uint8_t)((((uint8_t *)&gm_l7))[16] != 0);
            if (t16) {
                goto gm_lb2;
            }
            gm_l6 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l6 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 161, 8);
    }
gm_lb1:;
    uint8_t t17 = gm_l6;
    if (t17) {
        gm_ts1lka1v5g5ua t18 = GM_LD(gm_ts1lka1v5g5ua, ((uint8_t *)&F.l0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out))[0] = 0;
        GM_ST(gm_ts1lka1v5g5ua, ((uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out) + 8), t18);
        goto gm_fdone;
    }
    {
        gm_th2lqbo77pvsc t19;
        memset(&t19, 0, sizeof t19);
        F.l1 = t19;
    }
    {
        {
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sss60if2anvc8));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(4));
            gm_node *pn21 = gm_find(cx, 621690230);
            if (!pn21) gm_trap("no handler installed for effect `Env`", gm_srcmm27gepldtcm, 163, 16);
            gm_th2lqbo77pvsc t22;
            if (pn21->kind == GM_NATIVE) {
                const uint8_t *ev; size_t evn;
                bool evok = gm_env_var(GM_LD(void *, &t20), GM_LD(uintptr_t, (uint8_t *)&t20 + 8), &ev, &evn);
                gm_b16_8 t23;
                GM_ST(void *, (uint8_t *)&t23, (void *)(ev));
                GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(evn));
                gm_th2lqbo77pvsc t24;
                GM_ST(uint8_t, (uint8_t *)&t24 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t24 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t24 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t24), t23);
                ((uint8_t *)&t24)[16] = 1;
                gm_th2lqbo77pvsc t25;
                memset(&t25, 0, sizeof t25);
                t22 = evok ? t24 : t25;
            } else {
                ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn21->clauses[1])((void *)&t22, pn21->env, pn21->hctx, pn21, t20);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t22;
            uint8_t t26 = (uint8_t)((((uint8_t *)&gm_l8))[16] != 0);
            if (!t26) {
                goto gm_lb5;
            }
            gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l8));
            gm_l2 = t27;
            gm_b16_8 t28 = gm_l2;
            gm_l3 = t28;
            {
                gm_node wn29 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hl0tthikq3q4, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
                gm_b16_8 t32 = gm_l3;
                gm_b16_8 t33;
                GM_ST(void *, (uint8_t *)&t33, (void *)(gm_s39mljh669qgl));
                GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(1));
                gm_b0_1 t34 = gm_f_split_obtinjv_sq2k3vc((void *)NULL, (&wn29), NULL, t32, t33);
                if (gm_unw.kind) {
                    goto gm_ll31;
                }
            }
            goto gm_ls35;
gm_ll31:;
            if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we30; }
            goto gm_lad;
gm_ls35:;
gm_lb6:;
we30:;
            goto gm_lb4;
        }
gm_lb5:;
    }
gm_lb4:;
    {
        {
            {
                {
                    uint8_t t36 = (uint8_t)((((uint8_t *)&F.l1))[16] != 0);
                    if (t36) {
                        goto gm_lb10;
                    }
                    gm_l9 = ((uint8_t)1);
                    goto gm_lb9;
                }
gm_lb10:;
                {
                    gm_l9 = ((uint8_t)0);
                    goto gm_lb9;
                }
gm_lb11:;
                gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 169, 8);
            }
gm_lb9:;
            uint8_t t37 = gm_l9;
            if (!t37) {
                goto gm_lb8;
            }
            gm_b16_8 t38;
            GM_ST(void *, (uint8_t *)&t38, (void *)(gm_shki2pqrgmpd1));
            GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(18));
            uint8_t t39 = gm_f_is_file_tcuv9rv((void *)NULL, cx, NULL, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t39) {
                goto gm_lb8;
            }
            gm_b16_8 t40;
            GM_ST(void *, (uint8_t *)&t40, (void *)(gm_shki2pqrgmpd1));
            GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(18));
            gm_th2lqbo77pvsc t41;
            GM_ST(uint8_t, (uint8_t *)&t41 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t41 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t41 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t41), t40);
            ((uint8_t *)&t41)[16] = 1;
            F.l1 = t41;
            goto gm_lb7;
        }
gm_lb8:;
    }
gm_lb7:;
    uint8_t t42 = (uint8_t)((((uint8_t *)&F.l1))[16] != 0);
    if (t42) {
        gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
        gm_l4 = t43;
    } else {
        gm_b16_8 t44;
        GM_ST(void *, (uint8_t *)&t44, (void *)(gm_snqe7tsf40ume));
        GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(137));
        memset(&(*(gm_tm7gsm6lfro1o *)gm_out), 0, sizeof (*(gm_tm7gsm6lfro1o *)gm_out));
        ((uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out) + 8), t44);
        goto gm_fdone;
    }
    {
        gm_b16_8 t45 = gm_l4;
        gm_th2lqbo77pvsc t46;
        memset(&t46, 0, sizeof t46);
        gm_b16_8 t47 = gm_f_words_of_f1gud4i((void *)NULL, cx, NULL, t45, &t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, ((uint8_t *)&F.l0), t47);
    }
    {
        gm_ts1lka1v5g5ua t48 = GM_LD(gm_ts1lka1v5g5ua, ((uint8_t *)&F.l0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out))[0] = 0;
        GM_ST(gm_ts1lka1v5g5ua, ((uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out) + 8), t48);
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
__attribute__((aligned(4))) void gm_f_for_target_439caa2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_target_439caa2 F;
    gm_b16_8 gm_l0; /* triple */
    gm_th2lqbo77pvsc gm_l1; /* cc */
    gm_b16_8 gm_l2; /* sdk */
    uint64_t gm_l3; /* end */
    gm_b16_8 gm_l4; /* root */
    GM_STACK_CHECK(&F, 882u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l5;
    gm_th2lqbo77pvsc gm_l6;
    uint8_t gm_l7;
    gm_tabb710d9mq2e gm_l8;
    gm_tabb710d9mq2e gm_l9;
    gm_tabb710d9mq2e gm_l10;
    gm_tabb710d9mq2e gm_l11;
    {
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s5ejk09ngvect));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(2));
        gm_f_env_var_mbpep74((void *)&gm_l1, (void *)NULL, cx, NULL, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            {
                {
                    uint8_t t13 = (uint8_t)((((uint8_t *)&gm_l1))[16] != 0);
                    if (t13) {
                        goto gm_lb4;
                    }
                    gm_l5 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    gm_l5 = ((uint8_t)0);
                    goto gm_lb3;
                }
gm_lb5:;
                gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 82, 8);
            }
gm_lb3:;
            uint8_t t14 = gm_l5;
            if (!t14) {
                goto gm_lb2;
            }
            gm_b16_8 t15 = gm_l0;
            gm_b16_8 t16;
            GM_ST(void *, (uint8_t *)&t16, (void *)(gm_s4109lou8dsdj));
            GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(11));
            uint8_t t17 = gm_f_starts_with_6vvt01e(t15, t16);
            if (!t17) {
                goto gm_lb2;
            }
            {
                {
                    {
                        gm_b16_8 t18;
                        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sj7cc74b29dv3));
                        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(13));
                        gm_f_env_var_mbpep74((void *)&gm_l6, (void *)NULL, cx, NULL, t18);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t19 = (uint8_t)((((uint8_t *)&gm_l6))[16] != 0);
                    if (!t19) {
                        goto gm_lb7;
                    }
                    gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
                    gm_l2 = t20;
                    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                    gm_l3 = t21;
                    {
                        for (;;) {
                            uint64_t t22 = gm_l3;
                            uint8_t t23 = (t22 > ((uint64_t)0ULL));
                            if (!t23) {
                                goto gm_lb8;
                            }
                            void * t24 = GM_LD(void *, ((uint8_t *)&gm_l2));
                            uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                            uint64_t t26 = gm_l3;
                            uint64_t r27; bool o28 = __builtin_sub_overflow(t26, ((uint64_t)1ULL), &r27);
                            if (o28) gm_trap("integer overflow in `-`", gm_srcmm27gepldtcm, 85, 36);
                            uint64_t t29 = r27;
                            if ((uintptr_t)t29 >= (uintptr_t)(t25)) gm_trap_index((uintptr_t)t29, t25, gm_srcmm27gepldtcm, 85, 32);
                            uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)t24) + (uintptr_t)t29 * 1));
                            uint8_t t31 = ((t30 == ((uint8_t)47ULL)));
                            if (t31) {
                                gm_l7 = ((uint8_t)1);
                            } else {
                                void * t32 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                uint64_t t34 = gm_l3;
                                uint64_t r35; bool o36 = __builtin_sub_overflow(t34, ((uint64_t)1ULL), &r35);
                                if (o36) gm_trap("integer overflow in `-`", gm_srcmm27gepldtcm, 85, 59);
                                uint64_t t37 = r35;
                                if ((uintptr_t)t37 >= (uintptr_t)(t33)) gm_trap_index((uintptr_t)t37, t33, gm_srcmm27gepldtcm, 85, 55);
                                uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)t32) + (uintptr_t)t37 * 1));
                                uint8_t t39 = ((t38 == ((uint8_t)92ULL)));
                                gm_l7 = t39;
                            }
                            uint8_t t40 = gm_l7;
                            if (!t40) {
                                goto gm_lb8;
                            }
                            {
                                uint64_t t41 = gm_l3;
                                uint64_t r42; bool o43 = __builtin_sub_overflow(t41, ((uint64_t)1ULL), &r42);
                                if (o43) gm_trap("integer overflow in `-`", gm_srcmm27gepldtcm, 85, 79);
                                uint64_t t44 = r42;
                                gm_l3 = t44;
                            }
gm_lb10:;
                        }
                    }
gm_lb8:;
                    void * t45 = GM_LD(void *, ((uint8_t *)&gm_l2));
                    uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                    uint64_t t47 = gm_l3;
                    uint64_t t48 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t49 = (uintptr_t)t47;
                    if (t48 > t49 || t49 > (uintptr_t)t46) gm_trap_slice(t48, t49, t46, gm_srcmm27gepldtcm, 86, 21);
                    uint64_t t50 = ((uintptr_t)t47) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t51;
                    GM_ST(void *, (uint8_t *)&t51, (void *)(((uint8_t *)t45 + (uintptr_t)((uint64_t)0ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(t50));
                    gm_l4 = t51;
                    {
                        gm_b16_8 t52 = gm_l4;
                        gm_tnpifv20irnph t53;
                        GM_ST(gm_b16_8, ((uint8_t *)&t53 + 0), t52);
                        gm_f_format_svnhg71((void *)&gm_l8, (void *)NULL, cx, NULL, t53);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t54 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                    if (!t54) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_srcmm27gepldtcm, 87, 53);
                    {
                        gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
                        gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&t55));
                        gm_b16_8 t57 = gm_f_quote_word_3e35l5m((void *)NULL, cx, NULL, t56);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t58 = gm_l4;
                        gm_tnpifv20irnph t59;
                        GM_ST(gm_b16_8, ((uint8_t *)&t59 + 0), t58);
                        gm_f_format_mcmtfgd((void *)&gm_l9, (void *)NULL, cx, NULL, t59);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t60 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                        if (!t60) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srcmm27gepldtcm, 87, 104);
                        gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
                        gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&t61));
                        gm_b16_8 t63 = gm_f_quote_word_3e35l5m((void *)NULL, cx, NULL, t62);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t6j6smj7qoctu t64;
                        GM_ST(gm_b16_8, ((uint8_t *)&t64 + 0), t57);
                        GM_ST(gm_b16_8, ((uint8_t *)&t64 + 16), t63);
                        gm_f_format_2lgoflc((void *)&gm_l10, (void *)NULL, cx, NULL, &t64);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t65 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                    if (!t65) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_srcmm27gepldtcm, 87, 18);
                    {
                        gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
                        gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&t66));
                        GM_ST(uint8_t, (uint8_t *)&gm_l1 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&gm_l1 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&gm_l1 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&gm_l1), t67);
                        ((uint8_t *)&gm_l1)[16] = 1;
                    }
                    goto gm_lb6;
                }
gm_lb7:;
            }
gm_lb6:;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        gm_b16_8 t68 = gm_l0;
        gm_b16_8 t69;
        GM_ST(void *, (uint8_t *)&t69, (void *)(gm_shjpff93a0f25));
        GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(6));
        gm_th2lqbo77pvsc t70 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l1));
        gm_th2lqbo77pvsc t71;
        gm_f_env_var_mbpep74((void *)&t71, (void *)NULL, cx, NULL, t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t72;
        GM_ST(void *, (uint8_t *)&t72, (void *)(gm_snnagva49avpd));
        GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(7));
        gm_th2lqbo77pvsc t73;
        gm_f_env_var_mbpep74((void *)&t73, (void *)NULL, cx, NULL, t72);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t74 = gm_l0;
        gm_tnpifv20irnph t75;
        GM_ST(gm_b16_8, ((uint8_t *)&t75 + 0), t74);
        gm_f_format_g2mro4q((void *)&gm_l11, (void *)NULL, cx, NULL, t75);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t76 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
        if (!t76) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_srcmm27gepldtcm, 90, 95);
        gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
        gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
        uint8_t t79 = gm_f_program_on_path_fm43kjf((void *)NULL, cx, NULL, t78);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t80;
        GM_ST(void *, (uint8_t *)&t80, (void *)(gm_s2cgbvfh1b7rp));
        GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(5));
        uint8_t t81 = gm_f_program_on_path_fm43kjf((void *)NULL, cx, NULL, t80);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_for_triple_ok3tm31((void *)&(*(gm_tm7gsm6lfro1o *)gm_out), (void *)NULL, cx, NULL, t68, &t70, &t71, &t73, t79, t81);
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
__attribute__((aligned(4))) void gm_f_for_triple_ok3tm31(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1, const void * a2, const void * a3, uint8_t a4, uint8_t a5) {
    struct gm_fr_for_triple_ok3tm31 F;
    gm_b16_8 gm_l0; /* triple */
    gm_th2lqbo77pvsc gm_l1; /* cc */
    gm_th2lqbo77pvsc gm_l2; /* cflags */
    gm_th2lqbo77pvsc gm_l3; /* ldflags */
    uint8_t gm_l4; /* have_gcc */
    uint8_t gm_l5; /* have_clang */
    gm_b16_8 gm_l6; /* c */
    gm_b16_8 gm_l8; /* prog */
    uint8_t gm_l9; /* names_target */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_b16_8 gm_l12; /* w */
    gm_b16_8 gm_l15; /* gcc */
    GM_STACK_CHECK(&F, 1844u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    gm_l4 = a4;
    gm_l5 = a5;
    uint8_t gm_l16;
    gm_tabb710d9mq2e gm_l17;
    gm_tm7gsm6lfro1o gm_l18;
    gm_th2lqbo77pvsc gm_l19;
    uint8_t gm_l20;
    gm_tftb1odc739t5 gm_l21;
    gm_tabb710d9mq2e gm_l22;
    gm_tftb1odc739t5 gm_l23;
    uint8_t gm_l24;
    gm_tftb1odc739t5 gm_l25;
    gm_tftb1odc739t5 gm_l26;
    gm_tm7gsm6lfro1o gm_l27;
    gm_tabb710d9mq2e gm_l28;
    gm_tabb710d9mq2e gm_l29;
    gm_tabb710d9mq2e gm_l30;
    uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t12 = ((t11 == ((uint64_t)0ULL)));
    if (t12) {
        gm_l16 = ((uint8_t)1);
    } else {
        gm_b16_8 t13 = gm_l0;
        uint8_t t14 = gm_f_has_white_space_569v0vo(t13);
        gm_l16 = t14;
    }
    uint8_t t15 = gm_l16;
    if (t15) {
        {
            gm_b16_8 t16 = gm_l0;
            gm_tnpifv20irnph t17;
            GM_ST(gm_b16_8, ((uint8_t *)&t17 + 0), t16);
            gm_f_format_8pjgcns((void *)&gm_l17, (void *)NULL, cx, NULL, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
        if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_srcmm27gepldtcm, 106, 17);
        {
            gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
            gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
            memset(&(*(gm_tm7gsm6lfro1o *)gm_out), 0, sizeof (*(gm_tm7gsm6lfro1o *)gm_out));
            ((uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out) + 8), t20);
            goto gm_fdone;
        }
    }
    {
        {
            uint8_t t21 = (uint8_t)((((uint8_t *)&gm_l1))[16] != 0);
            if (!t21) {
                goto gm_lb2;
            }
            gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l1));
            gm_l6 = t22;
            {
                gm_b16_8 t23 = gm_l6;
                gm_th2lqbo77pvsc t24;
                GM_ST(uint8_t, (uint8_t *)&t24 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t24 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t24 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t24), t23);
                ((uint8_t *)&t24)[16] = 1;
                gm_th2lqbo77pvsc t25 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)a2));
                gm_th2lqbo77pvsc t26 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)a3));
                gm_f_from_vars_r2mjdrk((void *)&gm_l18, (void *)NULL, cx, NULL, &t24, &t25, &t26);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
            if (t27) {
            } else {
                gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
                memset(&(*(gm_tm7gsm6lfro1o *)gm_out), 0, sizeof (*(gm_tm7gsm6lfro1o *)gm_out));
                ((uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out))[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out) + 8), t28);
                goto gm_fdone;
            }
            {
                memmove(((uint8_t *)&F.l7), (((uint8_t *)&gm_l18) + 8), 56);
            }
            {
                void * t29 = GM_LD(void *, ((uint8_t *)&F.l7));
                uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t30)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t30, gm_srcmm27gepldtcm, 110, 32);
                gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)t29) + (uintptr_t)((uint64_t)0ULL) * 16));
                gm_f_file_name_em7v8lv((void *)&gm_l19, (void *)NULL, cx, NULL, t31);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t32 = (uint8_t)((((uint8_t *)&gm_l19))[16] != 0);
            if (t32) {
                gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l19));
                gm_l8 = t33;
            } else {
                gm_b16_8 t34;
                GM_ST(void *, (uint8_t *)&t34, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(0));
                gm_l8 = t34;
            }
            gm_l9 = ((uint8_t)0);
            gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
            gm_l10 = t35;
            gm_l11 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t36 = gm_l11;
                    uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                    uint8_t t38 = (t36 < t37);
                    if (!t38) {
                        goto gm_lb3;
                    }
                    {
                        void * t39 = GM_LD(void *, ((uint8_t *)&gm_l10));
                        uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                        uint64_t t41 = gm_l11;
                        {
                            {
                                gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)t39) + (uintptr_t)t41 * 16));
                                gm_l12 = t42;
                                gm_b16_8 t43 = gm_l12;
                                gm_b16_8 t44;
                                GM_ST(void *, (uint8_t *)&t44, (void *)(gm_smnkiec8dekcp));
                                GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(7));
                                uint8_t t45 = (gm_eq0iqiubfm7e(((uint8_t *)&t43), ((uint8_t *)&t44)));
                                if (t45) {
                                    gm_l20 = ((uint8_t)1);
                                } else {
                                    gm_b16_8 t46 = gm_l12;
                                    gm_b16_8 t47;
                                    GM_ST(void *, (uint8_t *)&t47, (void *)(gm_sbk48vc8h32c1));
                                    GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(8));
                                    uint8_t t48 = gm_f_starts_with_6vvt01e(t46, t47);
                                    gm_l20 = t48;
                                }
                                uint8_t t49 = gm_l20;
                                if (t49) {
                                    gm_l9 = ((uint8_t)1);
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 112, 9);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t50 = gm_l11;
                    uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                    uint64_t t53 = r51;
                    gm_l11 = t53;
                }
            }
gm_lb3:;
            uint8_t t54 = gm_l9;
            uint8_t t55 = (uint8_t)!t54;
            if (t55) {
                gm_trqfvmjtt4sij t56;
                gm_f_list_epvducg((void *)&t56, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l13 = t56;
                void * t57 = (void *)((uint8_t *)&F.l13);
                gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
                gm_tftb1odc739t5 t59 = gm_f_extend_vogsd72(t57, t58);
                gm_l21 = t59;
                uint8_t t60 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                if (!t60) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srcmm27gepldtcm, 117, 13);
                gm_b16_8 t61 = gm_l8;
                gm_b16_8 t62;
                GM_ST(void *, (uint8_t *)&t62, (void *)(gm_s2cgbvfh1b7rp));
                GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(5));
                uint8_t t63 = gm_f_contains_gkki4kq(t61, t62);
                if (t63) {
                    void * t64 = (void *)((uint8_t *)&F.l13);
                    gm_b16_8 t65 = gm_l0;
                    gm_tnpifv20irnph t66;
                    GM_ST(gm_b16_8, ((uint8_t *)&t66 + 0), t65);
                    gm_f_format_13mdm96((void *)&gm_l22, (void *)NULL, cx, NULL, t66);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t67 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                    if (!t67) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 8, gm_srcmm27gepldtcm, 119, 28);
                    gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
                    gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
                    gm_tftb1odc739t5 t70 = gm_f_push_3ef746b(t64, t69);
                    gm_l23 = t70;
                    uint8_t t71 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                    if (!t71) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_srcmm27gepldtcm, 119, 17);
                } else {
                    {
                        {
                            gm_b16_8 t72 = gm_l8;
                            gm_b16_8 t73;
                            GM_ST(void *, (uint8_t *)&t73, (void *)(gm_sf6pbim706kre));
                            GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(3));
                            uint8_t t74 = (gm_eq0iqiubfm7e(((uint8_t *)&t72), ((uint8_t *)&t73)));
                            if (!t74) {
                                goto gm_lb9;
                            }
                            uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
                            uint8_t t76 = (t75 > ((uint64_t)1ULL));
                            if (!t76) {
                                goto gm_lb9;
                            }
                            void * t77 = GM_LD(void *, ((uint8_t *)&F.l7));
                            uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
                            gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)t77) + (uintptr_t)((uint64_t)1ULL) * 16));
                            gm_b16_8 t80;
                            GM_ST(void *, (uint8_t *)&t80, (void *)(gm_sblsu018nl4o3));
                            GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(2));
                            uint8_t t81 = (gm_eq0iqiubfm7e(((uint8_t *)&t79), ((uint8_t *)&t80)));
                            if (t81) {
                                gm_l24 = ((uint8_t)1);
                            } else {
                                void * t82 = GM_LD(void *, ((uint8_t *)&F.l7));
                                uint64_t t83 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
                                gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)t82) + (uintptr_t)((uint64_t)1ULL) * 16));
                                gm_b16_8 t85;
                                GM_ST(void *, (uint8_t *)&t85, (void *)(gm_sbjdqrlfpuoqj));
                                GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(3));
                                uint8_t t86 = (gm_eq0iqiubfm7e(((uint8_t *)&t84), ((uint8_t *)&t85)));
                                gm_l24 = t86;
                            }
                            uint8_t t87 = gm_l24;
                            if (!t87) {
                                goto gm_lb9;
                            }
                            void * t88 = (void *)((uint8_t *)&F.l13);
                            gm_b16_8 t89;
                            GM_ST(void *, (uint8_t *)&t89, (void *)(gm_smnkiec8dekcp));
                            GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(7));
                            gm_tftb1odc739t5 t90 = gm_f_push_3ef746b(t88, t89);
                            gm_l25 = t90;
                            uint8_t t91 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                            if (!t91) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_srcmm27gepldtcm, 121, 17);
                            void * t92 = (void *)((uint8_t *)&F.l13);
                            gm_b16_8 t93 = gm_l0;
                            gm_tftb1odc739t5 t94 = gm_f_push_3ef746b(t92, t93);
                            gm_l26 = t94;
                            uint8_t t95 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                            if (!t95) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_srcmm27gepldtcm, 122, 17);
                            goto gm_lb8;
                        }
gm_lb9:;
                    }
gm_lb8:;
                }
                gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                gm_b16_8 t97 = GM_LD(gm_b16_8, ((uint8_t *)&t96));
                GM_ST(gm_b16_8, ((uint8_t *)&F.l7), t97);
            }
            {
                gm_ts1lka1v5g5ua t98 = GM_LD(gm_ts1lka1v5g5ua, ((uint8_t *)&F.l7));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out))[0] = 0;
                GM_ST(gm_ts1lka1v5g5ua, ((uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out) + 8), t98);
                goto gm_fdone;
            }
        }
gm_lb2:;
    }
gm_lb1:;
    {
        gm_th2lqbo77pvsc t99;
        memset(&t99, 0, sizeof t99);
        gm_th2lqbo77pvsc t100 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)a2));
        gm_th2lqbo77pvsc t101 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)a3));
        gm_f_from_vars_r2mjdrk((void *)&gm_l27, (void *)NULL, cx, NULL, &t99, &t100, &t101);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t102 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
    if (t102) {
    } else {
        gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l27) + 8));
        memset(&(*(gm_tm7gsm6lfro1o *)gm_out), 0, sizeof (*(gm_tm7gsm6lfro1o *)gm_out));
        ((uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out) + 8), t103);
        goto gm_fdone;
    }
    {
        memmove(((uint8_t *)&F.l14), (((uint8_t *)&gm_l27) + 8), 56);
    }
    {
        gm_b16_8 t104 = gm_l0;
        gm_tnpifv20irnph t105;
        GM_ST(gm_b16_8, ((uint8_t *)&t105 + 0), t104);
        gm_f_format_g2mro4q((void *)&gm_l28, (void *)NULL, cx, NULL, t105);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t106 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
    if (!t106) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 8, gm_srcmm27gepldtcm, 129, 12);
    gm_b16_8 t107 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l28) + 8));
    gm_l15 = t107;
    uint8_t t108 = gm_l4;
    if (t108) {
        gm_b16_8 t109 = gm_l15;
        gm_b16_8 t110 = GM_LD(gm_b16_8, ((uint8_t *)&t109));
        gm_th2lqbo77pvsc t111;
        memset(&t111, 0, sizeof t111);
        gm_b16_8 t112 = gm_f_words_of_f1gud4i((void *)NULL, cx, NULL, t110, &t111);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, ((uint8_t *)&F.l14), t112);
    } else {
        uint8_t t113 = gm_l5;
        if (t113) {
            gm_b16_8 t114;
            GM_ST(void *, (uint8_t *)&t114, (void *)(gm_s2cgbvfh1b7rp));
            GM_ST(uintptr_t, (uint8_t *)&t114 + 8, (uintptr_t)(5));
            gm_b16_8 t115 = gm_l0;
            gm_tnpifv20irnph t116;
            GM_ST(gm_b16_8, ((uint8_t *)&t116 + 0), t115);
            gm_f_format_13mdm96((void *)&gm_l29, (void *)NULL, cx, NULL, t116);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t117 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
            if (!t117) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 8, gm_srcmm27gepldtcm, 133, 34);
            gm_b16_8 t118 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l29) + 8));
            gm_b16_8 t119 = GM_LD(gm_b16_8, ((uint8_t *)&t118));
            gm_th2lqbo77pvsc t120;
            GM_ST(uint8_t, (uint8_t *)&t120 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t120 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t120 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t120), t119);
            ((uint8_t *)&t120)[16] = 1;
            gm_b16_8 t121 = gm_f_words_of_f1gud4i((void *)NULL, cx, NULL, t114, &t120);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, ((uint8_t *)&F.l14), t121);
        } else {
            {
                gm_b16_8 t122 = gm_l0;
                gm_b16_8 t123 = gm_l0;
                gm_t6j6smj7qoctu t124;
                GM_ST(gm_b16_8, ((uint8_t *)&t124 + 0), t122);
                GM_ST(gm_b16_8, ((uint8_t *)&t124 + 16), t123);
                gm_f_format_blcbfcc((void *)&gm_l30, (void *)NULL, cx, NULL, &t124);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t125 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
            if (!t125) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 8, gm_srcmm27gepldtcm, 135, 17);
            {
                gm_b16_8 t126 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 8));
                gm_b16_8 t127 = GM_LD(gm_b16_8, ((uint8_t *)&t126));
                memset(&(*(gm_tm7gsm6lfro1o *)gm_out), 0, sizeof (*(gm_tm7gsm6lfro1o *)gm_out));
                ((uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out))[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out) + 8), t127);
                goto gm_fdone;
            }
        }
    }
    {
        gm_ts1lka1v5g5ua t128 = GM_LD(gm_ts1lka1v5g5ua, ((uint8_t *)&F.l14));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out))[0] = 0;
        GM_ST(gm_ts1lka1v5g5ua, ((uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out) + 8), t128);
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
inline __attribute__((aligned(4))) void gm_f_from_env_rukl5lk(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_from_env_rukl5lk F;
    GM_STACK_CHECK(&F, 169u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_th2lqbo77pvsc gm_l0;
    gm_th2lqbo77pvsc gm_l1;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s5ejk09ngvect));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(2));
        gm_f_env_var_mbpep74((void *)&gm_l1, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t3 = (uint8_t)((((uint8_t *)&gm_l1))[16] != 0);
    if (t3) {
        memmove(((uint8_t *)&gm_l0), ((uint8_t *)&gm_l1), 24);
    } else {
        gm_f_default_cc_qpj8rre((void *)&gm_l0, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_shjpff93a0f25));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(6));
        gm_th2lqbo77pvsc t5 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l0));
        gm_th2lqbo77pvsc t6;
        gm_f_env_var_mbpep74((void *)&t6, (void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(gm_snnagva49avpd));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(7));
        gm_th2lqbo77pvsc t8;
        gm_f_env_var_mbpep74((void *)&t8, (void *)NULL, cx, NULL, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_from_vars_r2mjdrk((void *)&(*(gm_tm7gsm6lfro1o *)gm_out), (void *)NULL, cx, NULL, &t5, &t6, &t8);
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
inline __attribute__((aligned(4))) void gm_f_from_vars_r2mjdrk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1, const void * a2) {
    struct gm_fr_from_vars_r2mjdrk F;
    gm_th2lqbo77pvsc gm_l0; /* cc */
    gm_th2lqbo77pvsc gm_l1; /* cflags */
    gm_th2lqbo77pvsc gm_l2; /* ldflags */
    gm_b16_8 gm_l4; /* c */
    gm_b16_8 gm_l5; /* e */
    gm_b16_8 gm_l7; /* f */
    gm_b16_8 gm_l8; /* e */
    gm_b16_8 gm_l10; /* f */
    gm_b16_8 gm_l11; /* e */
    GM_STACK_CHECK(&F, 1010u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&gm_l0, a0, sizeof gm_l0);
    gm_t470aqkia5212 gm_l13;
    gm_tabb710d9mq2e gm_l14;
    gm_t470aqkia5212 gm_l15;
    gm_tabb710d9mq2e gm_l16;
    gm_t470aqkia5212 gm_l17;
    gm_tabb710d9mq2e gm_l18;
    {
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)gm_c695aum3fk1));
        gm_th2lqbo77pvsc t9;
        memset(&t9, 0, sizeof t9);
        gm_b16_8 t10 = gm_f_words_of_f1gud4i((void *)NULL, cx, NULL, t8, &t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t11 = gm_f_empty_slice_5nn7ph0();
        gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
        gm_b16_8 t13 = gm_f_empty_slice_5nn7ph0();
        gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
        GM_ST(uint8_t, (uint8_t *)&F.l3 + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&F.l3 + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&F.l3 + 52, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&F.l3 + 0), t10);
        GM_ST(gm_b16_8, ((uint8_t *)&F.l3 + 16), t12);
        GM_ST(gm_b16_8, ((uint8_t *)&F.l3 + 32), t14);
        GM_ST(uint8_t, ((uint8_t *)&F.l3 + 48), ((uint8_t)0));
    }
    {
        {
            uint8_t t15 = (uint8_t)((((uint8_t *)&gm_l0))[16] != 0);
            if (!t15) {
                goto gm_lb2;
            }
            gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
            gm_l4 = t16;
            gm_b16_8 t17 = gm_l4;
            gm_f_split_words_jk1lams((void *)&gm_l13, (void *)NULL, cx, NULL, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
            if (t18) {
                memmove(((uint8_t *)&F.l6), (((uint8_t *)&gm_l13) + 8), 40);
            } else {
                gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
                gm_l5 = t19;
                {
                    gm_b16_8 t20 = gm_l5;
                    gm_tnpifv20irnph t21;
                    GM_ST(gm_b16_8, ((uint8_t *)&t21 + 0), t20);
                    gm_f_format_o4fb8d3((void *)&gm_l14, (void *)NULL, cx, NULL, t21);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t22 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                if (!t22) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_srcmm27gepldtcm, 62, 57);
                {
                    gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
                    gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
                    memset(&(*(gm_tm7gsm6lfro1o *)gm_out), 0, sizeof (*(gm_tm7gsm6lfro1o *)gm_out));
                    ((uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out))[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out) + 8), t24);
                    goto gm_fdone;
                }
            }
            uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
            uint8_t t26 = (t25 > ((uint64_t)0ULL));
            if (t26) {
                gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
                gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
                GM_ST(gm_b16_8, ((uint8_t *)&F.l3), t28);
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            uint8_t t29 = (uint8_t)((((uint8_t *)a1))[16] != 0);
            if (!t29) {
                goto gm_lb4;
            }
            gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)a1));
            gm_l7 = t30;
            gm_b16_8 t31 = gm_l7;
            gm_f_split_words_jk1lams((void *)&gm_l15, (void *)NULL, cx, NULL, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
            if (t32) {
                memmove(((uint8_t *)&F.l9), (((uint8_t *)&gm_l15) + 8), 40);
            } else {
                gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
                gm_l8 = t33;
                {
                    gm_b16_8 t34 = gm_l8;
                    gm_tnpifv20irnph t35;
                    GM_ST(gm_b16_8, ((uint8_t *)&t35 + 0), t34);
                    gm_f_format_n1doesn((void *)&gm_l16, (void *)NULL, cx, NULL, t35);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                if (!t36) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_srcmm27gepldtcm, 66, 57);
                {
                    gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
                    gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
                    memset(&(*(gm_tm7gsm6lfro1o *)gm_out), 0, sizeof (*(gm_tm7gsm6lfro1o *)gm_out));
                    ((uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out))[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out) + 8), t38);
                    goto gm_fdone;
                }
            }
            gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
            gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
            GM_ST(gm_b16_8, (((uint8_t *)&F.l3) + 16), t40);
            goto gm_lb3;
        }
gm_lb4:;
    }
gm_lb3:;
    {
        {
            uint8_t t41 = (uint8_t)((((uint8_t *)a2))[16] != 0);
            if (!t41) {
                goto gm_lb6;
            }
            gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)a2));
            gm_l10 = t42;
            gm_b16_8 t43 = gm_l10;
            gm_f_split_words_jk1lams((void *)&gm_l17, (void *)NULL, cx, NULL, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t44 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
            if (t44) {
                memmove(((uint8_t *)&F.l12), (((uint8_t *)&gm_l17) + 8), 40);
            } else {
                gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
                gm_l11 = t45;
                {
                    gm_b16_8 t46 = gm_l11;
                    gm_tnpifv20irnph t47;
                    GM_ST(gm_b16_8, ((uint8_t *)&t47 + 0), t46);
                    gm_f_format_7dijd7n((void *)&gm_l18, (void *)NULL, cx, NULL, t47);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t48 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                if (!t48) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_srcmm27gepldtcm, 70, 57);
                {
                    gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
                    gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
                    memset(&(*(gm_tm7gsm6lfro1o *)gm_out), 0, sizeof (*(gm_tm7gsm6lfro1o *)gm_out));
                    ((uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out))[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out) + 8), t50);
                    goto gm_fdone;
                }
            }
            gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
            gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
            GM_ST(gm_b16_8, (((uint8_t *)&F.l3) + 32), t52);
            goto gm_lb5;
        }
gm_lb6:;
    }
gm_lb5:;
    {
        gm_ts1lka1v5g5ua t53 = GM_LD(gm_ts1lka1v5g5ua, ((uint8_t *)&F.l3));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out))[0] = 0;
        GM_ST(gm_ts1lka1v5g5ua, ((uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out) + 8), t53);
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
extern __attribute__((aligned(4))) uint8_t gm_f_has_c7mpl87(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_has_white_space_569v0vo(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_white_space_569v0vo F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint8_t gm_l3; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 126u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t7 = gm_l0;
    gm_l1 = t7;
    gm_l2 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t8 = gm_l2;
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t10 = (t8 < t9);
            if (!t10) {
                goto gm_lb1;
            }
            {
                void * t11 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t13 = gm_l2;
                {
                    {
                        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)t11) + (uintptr_t)t13 * 1));
                        gm_l3 = t14;
                        uint8_t t15 = gm_l3;
                        gm_t4q2b5ibsrdik t16;
                        GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)32ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t16 + 1), ((uint8_t)9ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t16 + 2), ((uint8_t)10ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t16 + 3), ((uint8_t)13ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t16 + 4), ((uint8_t)11ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t16 + 5), ((uint8_t)12ULL));
                        uint8_t t17 = 0;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t16) + 0, ((uint8_t *)&t15))) t17 = 1;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t16) + 1, ((uint8_t *)&t15))) t17 = 1;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t16) + 2, ((uint8_t *)&t15))) t17 = 1;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t16) + 3, ((uint8_t *)&t15))) t17 = 1;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t16) + 4, ((uint8_t *)&t15))) t17 = 1;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t16) + 5, ((uint8_t *)&t15))) t17 = 1;
                        uint8_t t18 = (uint8_t)(t17);
                        if (t18) {
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 149, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t19 = gm_l2;
            uint64_t r20; bool o21 = __builtin_add_overflow(t19, ((uint64_t)1ULL), &r20);
            uint64_t t22 = r20;
            gm_l2 = t22;
        }
    }
gm_lb1:;
    F.ret = ((uint8_t)0);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_host_from_env_qkl08nm(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint8_t a0) {
    struct gm_fr_host_from_env_qkl08nm F;
    uint8_t gm_l0; /* optimized */
    gm_b16_8 gm_l1; /* cc */
    GM_STACK_CHECK(&F, 378u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l3;
    uint8_t gm_l4;
    gm_th2lqbo77pvsc gm_l5;
    gm_th2lqbo77pvsc gm_l6;
    gm_tm7gsm6lfro1o gm_l7;
    uint8_t t5 = gm_l0;
    uint8_t t6 = (uint8_t)!t5;
    if (t6) {
        gm_l3 = ((uint8_t)1);
    } else {
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(gm_s5ejk09ngvect));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(2));
        gm_f_env_var_mbpep74((void *)&gm_l5, (void *)NULL, cx, NULL, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        {
            {
                uint8_t t8 = (uint8_t)((((uint8_t *)&gm_l5))[16] != 0);
                if (t8) {
                    goto gm_lb2;
                }
                gm_l4 = ((uint8_t)0);
                goto gm_lb1;
            }
gm_lb2:;
            {
                gm_l4 = ((uint8_t)1);
                goto gm_lb1;
            }
gm_lb3:;
            gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 51, 25);
        }
gm_lb1:;
        uint8_t t9 = gm_l4;
        gm_l3 = t9;
    }
    uint8_t t10 = gm_l3;
    if (t10) {
        gm_f_from_env_rukl5lk((void *)&(*(gm_tm7gsm6lfro1o *)gm_out), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
    }
    {
        gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)gm_c2tkt0obrcs));
        gm_f_optimizing_cc_ego0let((void *)&gm_l6, (void *)NULL, cx, NULL, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t12 = (uint8_t)((((uint8_t *)&gm_l6))[16] != 0);
    if (t12) {
        gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
        gm_l1 = t13;
    } else {
        gm_f_from_env_rukl5lk((void *)&(*(gm_tm7gsm6lfro1o *)gm_out), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
    }
    {
        gm_b16_8 t14 = gm_l1;
        gm_th2lqbo77pvsc t15;
        GM_ST(uint8_t, (uint8_t *)&t15 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t15 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t15 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t15), t14);
        ((uint8_t *)&t15)[16] = 1;
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_shjpff93a0f25));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(6));
        gm_th2lqbo77pvsc t17;
        gm_f_env_var_mbpep74((void *)&t17, (void *)NULL, cx, NULL, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t18;
        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_snnagva49avpd));
        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(7));
        gm_th2lqbo77pvsc t19;
        gm_f_env_var_mbpep74((void *)&t19, (void *)NULL, cx, NULL, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_from_vars_r2mjdrk((void *)&gm_l7, (void *)NULL, cx, NULL, &t15, &t17, &t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (t20) {
    } else {
        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
        memset(&(*(gm_tm7gsm6lfro1o *)gm_out), 0, sizeof (*(gm_tm7gsm6lfro1o *)gm_out));
        ((uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out) + 8), t21);
        goto gm_fdone;
    }
    {
        memmove(((uint8_t *)&F.l2), (((uint8_t *)&gm_l7) + 8), 56);
    }
    GM_ST(uint8_t, (((uint8_t *)&F.l2) + 48), ((uint8_t)1));
    {
        gm_ts1lka1v5g5ua t22 = GM_LD(gm_ts1lka1v5g5ua, ((uint8_t *)&F.l2));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out))[0] = 0;
        GM_ST(gm_ts1lka1v5g5ua, ((uint8_t *)&(*(gm_tm7gsm6lfro1o *)gm_out) + 8), t22);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_default_24c3cui(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_default_24c3cui F;
    void * gm_l0; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 135u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    uint8_t gm_l2;
    uint8_t gm_l3;
    uint8_t gm_l4;
    void * t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)t2) + 48));
    if (t3) {
        gm_l3 = ((uint8_t)1);
    } else {
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 8));
        uint8_t t6 = ((t5 == ((uint64_t)1ULL)));
        if (t6) {
            void * t7 = gm_l0;
            void * t8 = GM_LD(void *, ((uint8_t *)t7));
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t9)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t9, gm_srcmm27gepldtcm, 177, 80);
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)t8) + (uintptr_t)((uint64_t)0ULL) * 16));
            gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)gm_c695aum3fk1));
            uint8_t t12 = (gm_eq0iqiubfm7e(((uint8_t *)&t10), ((uint8_t *)&t11)));
            gm_l4 = t12;
        } else {
            gm_l4 = ((uint8_t)0);
        }
        uint8_t t13 = gm_l4;
        gm_l3 = t13;
    }
    uint8_t t14 = gm_l3;
    if (t14) {
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uint64_t, ((((uint8_t *)t15) + 16) + 8));
        uint8_t t17 = ((t16 == ((uint64_t)0ULL)));
        gm_l2 = t17;
    } else {
        gm_l2 = ((uint8_t)0);
    }
    uint8_t t18 = gm_l2;
    if (t18) {
        void * t19 = gm_l0;
        uint64_t t20 = GM_LD(uint64_t, ((((uint8_t *)t19) + 32) + 8));
        uint8_t t21 = ((t20 == ((uint64_t)0ULL)));
        gm_l1 = t21;
    } else {
        gm_l1 = ((uint8_t)0);
    }
    uint8_t t22 = gm_l1;
    F.ret = t22;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_macros_hrin9id(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_macros_hrin9id F;
    void * gm_l0; /* t */
    gm_tj5jn79kq9pnm gm_l3; /* e */
    GM_STACK_CHECK(&F, 892u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tat31u3lvqjk4 gm_l5;
    gm_tabb710d9mq2e gm_l6;
    gm_tabb710d9mq2e gm_l7;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sdad15q200tmk));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(3));
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skn7gps778eo2));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(2));
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s33a8rv0vs9rh));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(2));
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_s2gnpj7k4l22n));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(1));
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_skolmnfc0ccbd));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(1));
        gm_tjgkb0mi112kg t7;
        GM_ST(gm_b16_8, ((uint8_t *)&t7 + 0), t2);
        GM_ST(gm_b16_8, ((uint8_t *)&t7 + 16), t3);
        GM_ST(gm_b16_8, ((uint8_t *)&t7 + 32), t4);
        GM_ST(gm_b16_8, ((uint8_t *)&t7 + 48), t5);
        GM_ST(gm_b16_8, ((uint8_t *)&t7 + 64), t6);
        F.l1 = t7;
    }
    {
        void * t8 = gm_l0;
        gm_b16_8 t9 = gm_f_empty_slice_5nn7ph0();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        void * t11 = (void *)((uint8_t *)&F.l1);
        uint64_t t12 = ((uintptr_t)((uint64_t)5ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(((uint8_t *)t11 + (uintptr_t)((uint64_t)0ULL) * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(t12));
        gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
        gm_f_command_b2u33fl((void *)&F.l2, (void *)NULL, cx, NULL, t8, t10, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
        gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
        gm_t9qkh0rsooc8d t17 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)gm_cc7b06pnl1q));
        gm_t9qkh0rsooc8d t18 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)gm_cc7b06pnl1q));
        gm_th2lqbo77pvsc t19;
        memset(&t19, 0, sizeof t19);
        gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)gm_clslitrb510));
        gm_th2lqbo77pvsc t21;
        memset(&t21, 0, sizeof t21);
        gm_t9qkh0rsooc8d t22;
        GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)1ULL));
        gm_t32dckm4jfarl t23;
        GM_ST(uint8_t, (uint8_t *)&t23 + 3, 0);
        GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
        GM_ST(uint16_t, (uint8_t *)&t23 + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&t23 + 52, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t23 + 0), t22);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t23 + 1), t17);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t23 + 2), t18);
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t23 + 8), t19);
        GM_ST(gm_b16_8, ((uint8_t *)&t23 + 32), t20);
        GM_ST(uint8_t, ((uint8_t *)&t23 + 48), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t23 + 49), ((uint8_t)0));
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t23 + 56), t21);
        gm_f_output_mg6eie5((void *)&gm_l5, (void *)NULL, cx, NULL, t16, &t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (t24) {
        memmove(((uint8_t *)&F.l4), (((uint8_t *)&gm_l5) + 8), 96);
    } else {
        gm_tj5jn79kq9pnm t25 = GM_LD(gm_tj5jn79kq9pnm, (((uint8_t *)&gm_l5) + 8));
        gm_l3 = t25;
        {
            void * t26 = gm_l0;
            gm_b16_8 t27 = gm_f_display_30kc1i9((void *)NULL, cx, NULL, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tj5jn79kq9pnm t28 = gm_l3;
            gm_b16_8 t29 = gm_f_run_error_gs1dinp((void *)NULL, cx, NULL, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6j6smj7qoctu t30;
            GM_ST(gm_b16_8, ((uint8_t *)&t30 + 0), t27);
            GM_ST(gm_b16_8, ((uint8_t *)&t30 + 16), t29);
            gm_f_format_ra2b58j((void *)&gm_l6, (void *)NULL, cx, NULL, &t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t31 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
        if (!t31) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_srcmm27gepldtcm, 219, 17);
        {
            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
            gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8), t33);
            goto gm_fdone;
        }
    }
    gm_tsoiri44d0i6q t34 = GM_LD(gm_tsoiri44d0i6q, ((uint8_t *)&F.l4));
    uint8_t t35 = gm_f_success_q3lb6vt(t34);
    uint8_t t36 = (uint8_t)!t35;
    if (t36) {
        {
            void * t37 = gm_l0;
            gm_b16_8 t38 = gm_f_display_30kc1i9((void *)NULL, cx, NULL, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 48));
            gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
            gm_t6j6smj7qoctu t41;
            GM_ST(gm_b16_8, ((uint8_t *)&t41 + 0), t38);
            GM_ST(gm_b16_8, ((uint8_t *)&t41 + 16), t40);
            gm_f_format_q3io8lg((void *)&gm_l7, (void *)NULL, cx, NULL, &t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t42 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
        if (!t42) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_srcmm27gepldtcm, 222, 17);
        {
            gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
            gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8), t44);
            goto gm_fdone;
        }
    }
    {
        gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 8));
        gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8), t46);
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_num_1gcf1u8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_num_1gcf1u8 F;
    gm_b16_8 gm_l0; /* m */
    gm_b16_8 gm_l1; /* v */
    GM_STACK_CHECK(&F, 307u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_th2lqbo77pvsc gm_l2;
    gm_tf5srh4mp4b3i gm_l3;
    gm_tutnubt6u4hoq gm_l4;
    {
        void * t2 = (void *)((uint8_t *)&((struct gm_fr_target_from_macros_hkgk1pc *)((&F.h)->up))->l1);
        gm_b16_8 t3 = gm_l0;
        gm_f_get_e3joqcc((void *)&gm_l2, t2, t3);
    }
    uint8_t t4 = (uint8_t)((((uint8_t *)&gm_l2))[16] != 0);
    if (t4) {
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
        gm_l1 = t5;
    } else {
        gm_tf5srh4mp4b3i t6;
        memset(&t6, 0, sizeof t6);
        F.ret = t6;
        goto gm_fret;
    }
    gm_b16_8 t7 = gm_l1;
    gm_b16_8 t8;
    GM_ST(void *, (uint8_t *)&t8, (void *)(gm_sfdbfrv46lihd));
    GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(1));
    uint8_t t9 = gm_f_contains_gkki4kq(t7, t8);
    if (t9) {
        gm_tf5srh4mp4b3i t10;
        memset(&t10, 0, sizeof t10);
        F.ret = t10;
        goto gm_fret;
    }
    {
        gm_b16_8 t11 = gm_l1;
        gm_f_parse_int_8aueeu2((void *)&gm_l4, t11);
    }
    uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (t12) {
        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
        gm_tf5srh4mp4b3i t14;
        GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t14), t13);
        ((uint8_t *)&t14)[8] = 1;
        gm_l3 = t14;
    } else {
        gm_tf5srh4mp4b3i t15;
        memset(&t15, 0, sizeof t15);
        gm_l3 = t15;
    }
    gm_tf5srh4mp4b3i t16 = gm_l3;
    F.ret = t16;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_run_error_gs1dinp(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0) {
    struct gm_fr_run_error_gs1dinp F;
    gm_tj5jn79kq9pnm gm_l0; /* e */
    gm_toe5fmr3vvf4d gm_l1; /* io */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l2;
    {
        {
            uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)0ULL));
            if (!t6) {
                goto gm_lb2;
            }
            gm_toe5fmr3vvf4d t7 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l0) + 4));
            gm_l1 = t7;
            gm_toe5fmr3vvf4d t8 = gm_l1;
            gm_b16_8 t9 = gm_f_io_message_p7jbrea((void *)NULL, cx, NULL, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l2 = t9;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)1ULL));
            if (!t11) {
                goto gm_lb3;
            }
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sidpdvffdra02));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(13));
            gm_l2 = t12;
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 229, 67);
    }
gm_lb1:;
    gm_b16_8 t13 = gm_l2;
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
inline __attribute__((aligned(4))) void gm_f_runner_for_79o4h4m(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_runner_for_79o4h4m F;
    gm_b16_8 gm_l0; /* exe */
    GM_STACK_CHECK(&F, 288u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t470aqkia5212 gm_l3;
    gm_tftb1odc739t5 gm_l4;
    {
        gm_f_runner_from_env_bnildq8((void *)&gm_l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t4 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (t4) {
    } else {
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 40, 0);
        ((uint8_t *)&(*(gm_t470aqkia5212 *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 8), t5);
        goto gm_fdone;
    }
    {
        memmove(((uint8_t *)&F.l1), (((uint8_t *)&gm_l3) + 8), 40);
    }
    {
        {
            uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&F.l1) + 8));
            uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
            if (!t7) {
                goto gm_lb2;
            }
            gm_b16_8 t8 = gm_l0;
            gm_b16_8 t9;
            GM_ST(void *, (uint8_t *)&t9, (void *)(gm_sc6celu0e642t));
            GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(5));
            uint8_t t10 = gm_f_ends_with_biriu5l(t8, t9);
            if (!t10) {
                goto gm_lb2;
            }
            {
                gm_trqfvmjtt4sij t11;
                gm_f_list_epvducg((void *)&t11, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l2 = t11;
            }
            void * t12 = (void *)((uint8_t *)&F.l2);
            void * t13 = (void *)((uint8_t *)gm_cl03rhutns3);
            uint64_t t14 = ((uintptr_t)((uint64_t)7ULL)) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t13 + (uintptr_t)((uint64_t)0ULL) * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14));
            gm_tftb1odc739t5 t16 = gm_f_extend_vogsd72(t12, t15);
            gm_l4 = t16;
            uint8_t t17 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
            if (!t17) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srcmm27gepldtcm, 249, 9);
            {
                gm_trqfvmjtt4sij t18 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l2));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t470aqkia5212 *)gm_out))[0] = 0;
                GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 8), t18);
                goto gm_fdone;
            }
        }
gm_lb2:;
    }
gm_lb1:;
    {
        gm_trqfvmjtt4sij t19 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t470aqkia5212 *)gm_out))[0] = 0;
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 8), t19);
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
inline __attribute__((aligned(4))) void gm_f_runner_from_env_bnildq8(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_runner_from_env_bnildq8 F;
    gm_b16_8 gm_l0; /* v */
    gm_b16_8 gm_l1; /* e */
    GM_STACK_CHECK(&F, 467u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_th2lqbo77pvsc gm_l2;
    gm_trqfvmjtt4sij gm_l3;
    gm_t470aqkia5212 gm_l4;
    gm_tabb710d9mq2e gm_l5;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sc5p6b8d5c87a));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(12));
        gm_node *pn3 = gm_find(cx, 621690230);
        if (!pn3) gm_trap("no handler installed for effect `Env`", gm_srcmm27gepldtcm, 238, 10);
        gm_th2lqbo77pvsc t4;
        if (pn3->kind == GM_NATIVE) {
            const uint8_t *ev; size_t evn;
            bool evok = gm_env_var(GM_LD(void *, &t2), GM_LD(uintptr_t, (uint8_t *)&t2 + 8), &ev, &evn);
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(ev));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(evn));
            gm_th2lqbo77pvsc t6;
            GM_ST(uint8_t, (uint8_t *)&t6 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t6 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t6 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t6), t5);
            ((uint8_t *)&t6)[16] = 1;
            gm_th2lqbo77pvsc t7;
            memset(&t7, 0, sizeof t7);
            t4 = evok ? t6 : t7;
        } else {
            ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[1])((void *)&t4, pn3->env, pn3->hctx, pn3, t2);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t4;
    }
    uint8_t t8 = (uint8_t)((((uint8_t *)&gm_l2))[16] != 0);
    if (t8) {
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
        gm_l0 = t9;
    } else {
        gm_trqfvmjtt4sij t10;
        gm_f_list_epvducg((void *)&t10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t470aqkia5212 *)gm_out))[0] = 0;
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 8), t10);
        goto gm_fdone;
    }
    {
        gm_b16_8 t11 = gm_l0;
        gm_f_split_words_jk1lams((void *)&gm_l4, (void *)NULL, cx, NULL, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (t12) {
        memmove(((uint8_t *)&gm_l3), (((uint8_t *)&gm_l4) + 8), 40);
    } else {
        gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
        gm_l1 = t13;
        {
            gm_b16_8 t14 = gm_l1;
            gm_tnpifv20irnph t15;
            GM_ST(gm_b16_8, ((uint8_t *)&t15 + 0), t14);
            gm_f_format_2g98t2r((void *)&gm_l5, (void *)NULL, cx, NULL, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
        if (!t16) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 8, gm_srcmm27gepldtcm, 239, 51);
        {
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
            gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 40, 0);
            ((uint8_t *)&(*(gm_t470aqkia5212 *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 8), t18);
            goto gm_fdone;
        }
    }
    {
        gm_trqfvmjtt4sij t19 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&gm_l3));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t470aqkia5212 *)gm_out))[0] = 0;
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 8), t19);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_strip_wasm_trap_report_h977b8r(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_strip_wasm_trap_report_h977b8r F;
    gm_b16_8 gm_l0; /* stderr */
    uint64_t gm_l1; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 270u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tf5srh4mp4b3i gm_l2;
    uint8_t gm_l3;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)gm_cojk58d4vkf));
    gm_tf5srh4mp4b3i t4 = gm_f_find_nab29co(t2, t3);
    gm_l2 = t4;
    uint8_t t5 = (uint8_t)((((uint8_t *)&gm_l2))[8] != 0);
    if (t5) {
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
        gm_l1 = t6;
    } else {
        gm_b16_8 t7 = gm_l0;
        F.ret = t7;
        goto gm_fret;
    }
    uint64_t t8 = gm_l1;
    uint8_t t9 = ((t8 == ((uint64_t)0ULL)));
    if (t9) {
        gm_l3 = ((uint8_t)1);
    } else {
        void * t10 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t12 = gm_l1;
        uint64_t r13; bool o14 = __builtin_sub_overflow(t12, ((uint64_t)1ULL), &r13);
        if (o14) gm_trap("integer overflow in `-`", gm_srcmm27gepldtcm, 267, 25);
        uint64_t t15 = r13;
        if ((uintptr_t)t15 >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)t15, t11, gm_srcmm27gepldtcm, 267, 18);
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)t10) + (uintptr_t)t15 * 1));
        uint8_t t17 = ((t16 == ((uint8_t)10ULL)));
        gm_l3 = t17;
    }
    uint8_t t18 = gm_l3;
    if (t18) {
        void * t19 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t21 = gm_l1;
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = (uintptr_t)t21;
        if (t22 > t23 || t23 > (uintptr_t)t20) gm_trap_slice(t22, t23, t20, gm_srcmm27gepldtcm, 267, 49);
        uint64_t t24 = ((uintptr_t)t21) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t25;
        GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t19 + (uintptr_t)((uint64_t)0ULL) * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(t24));
        F.ret = t25;
        goto gm_fret;
    }
    gm_b16_8 t26 = gm_l0;
    F.ret = t26;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_target_72cra6q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_target_72cra6q F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* m */
    gm_b16_8 gm_l2; /* e */
    GM_STACK_CHECK(&F, 540u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t3ujhu6qph7de gm_l3;
    gm_t0rfe1ou14h21 gm_l4;
    gm_tod6pa33o8u9u gm_l5;
    gm_tabb710d9mq2e gm_l6;
    void * t2 = gm_l0;
    uint8_t t3 = gm_f_is_default_24c3cui(t2);
    if (t3) {
        gm_t0rfe1ou14h21 t4;
        gm_f_host_target_vsu78om((void *)&t4);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out))[0] = 0;
        GM_ST(gm_t0rfe1ou14h21, ((uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out) + 8), t4);
        goto gm_fdone;
    }
    {
        void * t5 = gm_l0;
        gm_f_macros_hrin9id((void *)&gm_l3, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (t6) {
    } else {
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
        memset(&(*(gm_tod6pa33o8u9u *)gm_out), 0, sizeof (*(gm_tod6pa33o8u9u *)gm_out));
        ((uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out) + 8), t7);
        goto gm_fdone;
    }
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
    gm_l1 = t8;
    {
        gm_b16_8 t9 = gm_l1;
        gm_f_target_from_macros_hkgk1pc((void *)&gm_l5, (void *)NULL, cx, NULL, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (t10) {
        memmove(((uint8_t *)&gm_l4), (((uint8_t *)&gm_l5) + 8), 64);
    } else {
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
        gm_l2 = t11;
        {
            void * t12 = gm_l0;
            gm_b16_8 t13 = gm_f_display_30kc1i9((void *)NULL, cx, NULL, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t14 = gm_l2;
            gm_t6j6smj7qoctu t15;
            GM_ST(gm_b16_8, ((uint8_t *)&t15 + 0), t13);
            GM_ST(gm_b16_8, ((uint8_t *)&t15 + 16), t14);
            gm_f_format_gshr6sq((void *)&gm_l6, (void *)NULL, cx, NULL, &t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
        if (!t16) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_srcmm27gepldtcm, 202, 54);
        {
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
            gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
            memset(&(*(gm_tod6pa33o8u9u *)gm_out), 0, sizeof (*(gm_tod6pa33o8u9u *)gm_out));
            ((uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out) + 8), t18);
            goto gm_fdone;
        }
    }
    {
        gm_t0rfe1ou14h21 t19 = GM_LD(gm_t0rfe1ou14h21, ((uint8_t *)&gm_l4));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out))[0] = 0;
        GM_ST(gm_t0rfe1ou14h21, ((uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out) + 8), t19);
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
inline __attribute__((aligned(4))) void gm_f_target_for_omoprob(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_target_for_omoprob F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* triple */
    gm_b16_8 gm_l2; /* m */
    gm_b16_8 gm_l3; /* e */
    gm_b16_8 gm_l5; /* e */
    GM_STACK_CHECK(&F, 637u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t3ujhu6qph7de gm_l6;
    gm_tod6pa33o8u9u gm_l7;
    gm_tabb710d9mq2e gm_l8;
    gm_tl572d7579hns gm_l9;
    gm_tabb710d9mq2e gm_l10;
    {
        void * t2 = gm_l0;
        gm_f_macros_hrin9id((void *)&gm_l6, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t3 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (t3) {
    } else {
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
        memset(&(*(gm_tod6pa33o8u9u *)gm_out), 0, sizeof (*(gm_tod6pa33o8u9u *)gm_out));
        ((uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out) + 8), t4);
        goto gm_fdone;
    }
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
    gm_l2 = t5;
    {
        gm_b16_8 t6 = gm_l2;
        gm_f_target_from_macros_hkgk1pc((void *)&gm_l7, (void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (t7) {
        memmove(((uint8_t *)&F.l4), (((uint8_t *)&gm_l7) + 8), 64);
    } else {
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
        gm_l3 = t8;
        {
            void * t9 = gm_l0;
            gm_b16_8 t10 = gm_f_display_30kc1i9((void *)NULL, cx, NULL, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t11 = gm_l3;
            gm_t6j6smj7qoctu t12;
            GM_ST(gm_b16_8, ((uint8_t *)&t12 + 0), t10);
            GM_ST(gm_b16_8, ((uint8_t *)&t12 + 16), t11);
            gm_f_format_gshr6sq((void *)&gm_l8, (void *)NULL, cx, NULL, &t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t13 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
        if (!t13) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_srcmm27gepldtcm, 209, 53);
        {
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
            gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
            memset(&(*(gm_tod6pa33o8u9u *)gm_out), 0, sizeof (*(gm_tod6pa33o8u9u *)gm_out));
            ((uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out) + 8), t15);
            goto gm_fdone;
        }
    }
    {
        gm_b16_8 t16 = gm_l1;
        gm_b16_8 t17 = gm_l2;
        gm_f_check_triple_uqaropr((void *)&gm_l9, (void *)NULL, cx, NULL, t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (t18) {
    } else {
        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
        gm_l5 = t19;
        {
            void * t20 = gm_l0;
            gm_b16_8 t21 = gm_f_display_30kc1i9((void *)NULL, cx, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t22 = gm_l5;
            gm_t6j6smj7qoctu t23;
            GM_ST(gm_b16_8, ((uint8_t *)&t23 + 0), t21);
            GM_ST(gm_b16_8, ((uint8_t *)&t23 + 16), t22);
            gm_f_format_gshr6sq((void *)&gm_l10, (void *)NULL, cx, NULL, &t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
        if (!t24) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_srcmm27gepldtcm, 210, 49);
        {
            gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
            gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
            memset(&(*(gm_tod6pa33o8u9u *)gm_out), 0, sizeof (*(gm_tod6pa33o8u9u *)gm_out));
            ((uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out) + 8), t26);
            goto gm_fdone;
        }
    }
    {
        gm_t0rfe1ou14h21 t27 = GM_LD(gm_t0rfe1ou14h21, ((uint8_t *)&F.l4));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out))[0] = 0;
        GM_ST(gm_t0rfe1ou14h21, ((uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out) + 8), t27);
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
__attribute__((aligned(4))) void gm_f_target_from_macros_hkgk1pc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_target_from_macros_hkgk1pc F;
    gm_b16_8 gm_l0; /* macros */
    gm_b16_8 gm_l2; /* <tmp> */
    gm_closure gm_l3; /* num */
    uint64_t gm_l4; /* ptr_bytes */
    gm_closure gm_l5; /* value */
    uint8_t gm_l6; /* little_endian */
    gm_b16_8 gm_l7; /* order */
    gm_b16_8 gm_l8; /* big */
    gm_closure gm_l9; /* has */
    gm_b16_8 gm_l10; /* arch */
    gm_b16_8 gm_l11; /* os_name */
    GM_STACK_CHECK(&F, 2298u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tf5srh4mp4b3i gm_l12;
    gm_tabb710d9mq2e gm_l13;
    uint8_t gm_l14;
    gm_th2lqbo77pvsc gm_l15;
    gm_th2lqbo77pvsc gm_l16;
    gm_th2lqbo77pvsc gm_l17;
    uint64_t gm_l18;
    {
        gm_t4c7ipjj9s3fp t14;
        gm_f_hash_map_p3t5tj8((void *)&t14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t14;
    }
    gm_b16_8 t15 = gm_l0;
    gm_l2 = t15;
    {
        gm_node wn16 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hl5iavnkj4ht, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t19 = gm_l2;
        gm_b0_1 t20 = gm_f_lines_9uns0k2_s30klli((void *)NULL, (&wn16), NULL, t19);
        if (gm_unw.kind) {
            goto gm_ll18;
        }
    }
    goto gm_ls21;
gm_ll18:;
    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we17; }
    goto gm_lad;
gm_ls21:;
gm_lb1:;
we17:;
    gm_closure t22 = ((gm_closure){(void *)gm_f_num_1gcf1u8, (void *)&F.h, cx});
    gm_l3 = t22;
    gm_closure t23 = gm_l3;
    gm_b16_8 t24;
    GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sn2e6t65fstoj));
    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(18));
    gm_closure t25 = t23;
    gm_node *ccx26 = t25.cx;
    void * t27 = t25.code;
    gm_tf5srh4mp4b3i t28;
    if (GM_PTR_TAGGED(t27)) {
        t28 = ((gm_tf5srh4mp4b3i (*)(gm_b16_8))GM_PTR_UNTAG(t27))(t24);
    } else {
        t28 = ((gm_tf5srh4mp4b3i (*)(void *, gm_node *, gm_node *, gm_b16_8))t27)(t25.env, ccx26, NULL, t24);
    }
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l12 = t28;
    uint8_t t29 = (uint8_t)((((uint8_t *)&gm_l12))[8] != 0);
    if (t29) {
        uint64_t t30 = GM_LD(uint64_t, ((uint8_t *)&gm_l12));
        gm_l4 = t30;
    } else {
        gm_b16_8 t31;
        GM_ST(void *, (uint8_t *)&t31, (void *)(gm_so5q4maooc958));
        GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(34));
        memset(&(*(gm_tod6pa33o8u9u *)gm_out), 0, sizeof (*(gm_tod6pa33o8u9u *)gm_out));
        ((uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out) + 8), t31);
        goto gm_fdone;
    }
    {
        {
            uint64_t t32 = gm_l4;
            uint8_t t33 = (!(t32 == ((uint64_t)4ULL)));
            if (!t33) {
                goto gm_lb3;
            }
            uint64_t t34 = gm_l4;
            uint8_t t35 = (!(t34 == ((uint64_t)8ULL)));
            if (!t35) {
                goto gm_lb3;
            }
            {
                uint64_t t36 = gm_l4;
                gm_t4ce0jd3tdro4 t37;
                GM_ST(uint64_t, ((uint8_t *)&t37 + 0), t36);
                gm_f_format_seuq78o((void *)&gm_l13, (void *)NULL, cx, NULL, t37);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t38 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
            if (!t38) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_srcmm27gepldtcm, 392, 17);
            {
                gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
                gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
                memset(&(*(gm_tod6pa33o8u9u *)gm_out), 0, sizeof (*(gm_tod6pa33o8u9u *)gm_out));
                ((uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out))[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out) + 8), t40);
                goto gm_fdone;
            }
        }
gm_lb3:;
    }
gm_lb2:;
    {
        void * t41 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t42;
        GM_ST(void *, (uint8_t *)&t42, (void *)(gm_se6fhe7nkildm));
        GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(17));
        gm_f_get_e3joqcc((void *)&gm_l15, t41, t42);
    }
    {
        {
            uint8_t t43 = (uint8_t)((((uint8_t *)&gm_l15))[16] != 0);
            if (t43) {
                goto gm_lb5;
            }
            gm_l14 = ((uint8_t)1);
            goto gm_lb4;
        }
gm_lb5:;
        {
            gm_l14 = ((uint8_t)0);
            goto gm_lb4;
        }
gm_lb6:;
        gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 394, 8);
    }
gm_lb4:;
    uint8_t t44 = gm_l14;
    if (t44) {
        gm_b16_8 t45;
        GM_ST(void *, (uint8_t *)&t45, (void *)(gm_sfrku7jk647q0));
        GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(82));
        memset(&(*(gm_tod6pa33o8u9u *)gm_out), 0, sizeof (*(gm_tod6pa33o8u9u *)gm_out));
        ((uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out) + 8), t45);
        goto gm_fdone;
    }
    gm_closure t46 = ((gm_closure){(void *)gm_f_value_3oqtvqn, (void *)&F.h, cx});
    gm_l5 = t46;
    gm_l6 = ((uint8_t)1);
    {
        {
            gm_closure t47 = gm_l5;
            gm_b16_8 t48;
            GM_ST(void *, (uint8_t *)&t48, (void *)(gm_s5lr3gdic3o7e));
            GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(14));
            gm_closure t49 = t47;
            gm_node *ccx50 = t49.cx;
            void * t51 = t49.code;
            gm_th2lqbo77pvsc t52;
            if (GM_PTR_TAGGED(t51)) {
                t52 = ((gm_th2lqbo77pvsc (*)(gm_b16_8))GM_PTR_UNTAG(t51))(t48);
            } else {
                ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))t51)((void *)&t52, t49.env, ccx50, NULL, t48);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l16 = t52;
            uint8_t t53 = (uint8_t)((((uint8_t *)&gm_l16))[16] != 0);
            if (!t53) {
                goto gm_lb8;
            }
            gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l16));
            gm_l7 = t54;
            {
                {
                    gm_closure t55 = gm_l5;
                    gm_b16_8 t56;
                    GM_ST(void *, (uint8_t *)&t56, (void *)(gm_st5oflrv0tmc6));
                    GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(20));
                    gm_closure t57 = t55;
                    gm_node *ccx58 = t57.cx;
                    void * t59 = t57.code;
                    gm_th2lqbo77pvsc t60;
                    if (GM_PTR_TAGGED(t59)) {
                        t60 = ((gm_th2lqbo77pvsc (*)(gm_b16_8))GM_PTR_UNTAG(t59))(t56);
                    } else {
                        ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))t59)((void *)&t60, t57.env, ccx58, NULL, t56);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l17 = t60;
                    uint8_t t61 = (uint8_t)((((uint8_t *)&gm_l17))[16] != 0);
                    if (!t61) {
                        goto gm_lb10;
                    }
                    gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l17));
                    gm_l8 = t62;
                    gm_b16_8 t63 = gm_l7;
                    gm_b16_8 t64 = gm_l8;
                    uint8_t t65 = (!gm_eq0iqiubfm7e(((uint8_t *)&t63), ((uint8_t *)&t64)));
                    gm_l6 = t65;
                    goto gm_lb9;
                }
gm_lb10:;
            }
gm_lb9:;
            goto gm_lb7;
        }
gm_lb8:;
    }
gm_lb7:;
    gm_closure t66 = ((gm_closure){(void *)gm_f_has_c7mpl87, (void *)&F.h, cx});
    gm_l9 = t66;
    gm_b16_8 t67;
    GM_ST(void *, (uint8_t *)&t67, (void *)(gm_svln5ui59rjil));
    GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(5));
    gm_l10 = t67;
    gm_closure t68 = gm_l9;
    gm_b16_8 t69;
    GM_ST(void *, (uint8_t *)&t69, (void *)(gm_smuveuet82l27));
    GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(10));
    gm_closure t70 = t68;
    gm_node *ccx71 = t70.cx;
    void * t72 = t70.code;
    uint8_t t73;
    if (GM_PTR_TAGGED(t72)) {
        t73 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t72))(t69);
    } else {
        t73 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t72)(t70.env, ccx71, NULL, t69);
    }
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (t73) {
        gm_b16_8 t74;
        GM_ST(void *, (uint8_t *)&t74, (void *)(gm_sje4rbc88csdu));
        GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(6));
        gm_l10 = t74;
    } else {
        gm_closure t75 = gm_l9;
        gm_b16_8 t76;
        GM_ST(void *, (uint8_t *)&t76, (void *)(gm_s2ct6k0fls3pb));
        GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(11));
        gm_closure t77 = t75;
        gm_node *ccx78 = t77.cx;
        void * t79 = t77.code;
        uint8_t t80;
        if (GM_PTR_TAGGED(t79)) {
            t80 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t79))(t76);
        } else {
            t80 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t79)(t77.env, ccx78, NULL, t76);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (t80) {
            gm_b16_8 t81;
            GM_ST(void *, (uint8_t *)&t81, (void *)(gm_spervk999ikim));
            GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(7));
            gm_l10 = t81;
        } else {
            {
                {
                    gm_closure t82 = gm_l9;
                    gm_b16_8 t83;
                    GM_ST(void *, (uint8_t *)&t83, (void *)(gm_sup67cuqst8bs));
                    GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(7));
                    gm_closure t84 = t82;
                    gm_node *ccx85 = t84.cx;
                    void * t86 = t84.code;
                    uint8_t t87;
                    if (GM_PTR_TAGGED(t86)) {
                        t87 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t86))(t83);
                    } else {
                        t87 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t86)(t84.env, ccx85, NULL, t83);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (!t87) {
                        goto gm_lb12;
                    }
                    gm_closure t88 = gm_l3;
                    gm_b16_8 t89;
                    GM_ST(void *, (uint8_t *)&t89, (void *)(gm_sm3fgo8acha61));
                    GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(12));
                    gm_closure t90 = t88;
                    gm_node *ccx91 = t90.cx;
                    void * t92 = t90.code;
                    gm_tf5srh4mp4b3i t93;
                    if (GM_PTR_TAGGED(t92)) {
                        t93 = ((gm_tf5srh4mp4b3i (*)(gm_b16_8))GM_PTR_UNTAG(t92))(t89);
                    } else {
                        t93 = ((gm_tf5srh4mp4b3i (*)(void *, gm_node *, gm_node *, gm_b16_8))t92)(t90.env, ccx91, NULL, t89);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tf5srh4mp4b3i t94;
                    GM_ST(uint8_t, (uint8_t *)&t94 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t94 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t94 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t94), ((uint64_t)64ULL));
                    ((uint8_t *)&t94)[8] = 1;
                    uint8_t t95 = (gm_eqmpqhhfmb90(((uint8_t *)&t93), ((uint8_t *)&t94)));
                    if (!t95) {
                        goto gm_lb12;
                    }
                    gm_b16_8 t96;
                    GM_ST(void *, (uint8_t *)&t96, (void *)(gm_sar7ovt8nasdo));
                    GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(7));
                    gm_l10 = t96;
                    goto gm_lb11;
                }
gm_lb12:;
                gm_closure t97 = gm_l9;
                gm_b16_8 t98;
                GM_ST(void *, (uint8_t *)&t98, (void *)(gm_sgfrcimthamdn));
                GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(9));
                gm_closure t99 = t97;
                gm_node *ccx100 = t99.cx;
                void * t101 = t99.code;
                uint8_t t102;
                if (GM_PTR_TAGGED(t101)) {
                    t102 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t101))(t98);
                } else {
                    t102 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t101)(t99.env, ccx100, NULL, t98);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (t102) {
                    gm_b16_8 t103;
                    GM_ST(void *, (uint8_t *)&t103, (void *)(gm_sqg9do4ma3giu));
                    GM_ST(uintptr_t, (uint8_t *)&t103 + 8, (uintptr_t)(5));
                    gm_l10 = t103;
                } else {
                    gm_closure t104 = gm_l9;
                    gm_b16_8 t105;
                    GM_ST(void *, (uint8_t *)&t105, (void *)(gm_sr389r8t7d0mf));
                    GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)(10));
                    gm_closure t106 = t104;
                    gm_node *ccx107 = t106.cx;
                    void * t108 = t106.code;
                    uint8_t t109;
                    if (GM_PTR_TAGGED(t108)) {
                        t109 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t108))(t105);
                    } else {
                        t109 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t108)(t106.env, ccx107, NULL, t105);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (t109) {
                        gm_b16_8 t110;
                        GM_ST(void *, (uint8_t *)&t110, (void *)(gm_sbrifoc7ea21m));
                        GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(6));
                        gm_l10 = t110;
                    }
                }
            }
gm_lb11:;
        }
    }
    gm_b16_8 t111;
    GM_ST(void *, (uint8_t *)&t111, (void *)(gm_soo4rtckloi4d));
    GM_ST(uintptr_t, (uint8_t *)&t111 + 8, (uintptr_t)(12));
    gm_l11 = t111;
    gm_closure t112 = gm_l9;
    gm_b16_8 t113;
    GM_ST(void *, (uint8_t *)&t113, (void *)(gm_ssevq7ii4bscf));
    GM_ST(uintptr_t, (uint8_t *)&t113 + 8, (uintptr_t)(9));
    gm_closure t114 = t112;
    gm_node *ccx115 = t114.cx;
    void * t116 = t114.code;
    uint8_t t117;
    if (GM_PTR_TAGGED(t116)) {
        t117 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t116))(t113);
    } else {
        t117 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t116)(t114.env, ccx115, NULL, t113);
    }
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (t117) {
        gm_b16_8 t118;
        GM_ST(void *, (uint8_t *)&t118, (void *)(gm_scq91tgm9lph4));
        GM_ST(uintptr_t, (uint8_t *)&t118 + 8, (uintptr_t)(5));
        gm_l11 = t118;
    } else {
        gm_closure t119 = gm_l9;
        gm_b16_8 t120;
        GM_ST(void *, (uint8_t *)&t120, (void *)(gm_smiidqh71uo72));
        GM_ST(uintptr_t, (uint8_t *)&t120 + 8, (uintptr_t)(9));
        gm_closure t121 = t119;
        gm_node *ccx122 = t121.cx;
        void * t123 = t121.code;
        uint8_t t124;
        if (GM_PTR_TAGGED(t123)) {
            t124 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t123))(t120);
        } else {
            t124 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t123)(t121.env, ccx122, NULL, t120);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (t124) {
            gm_b16_8 t125;
            GM_ST(void *, (uint8_t *)&t125, (void *)(gm_sjdv8icqpfhc5));
            GM_ST(uintptr_t, (uint8_t *)&t125 + 8, (uintptr_t)(5));
            gm_l11 = t125;
        } else {
            gm_closure t126 = gm_l9;
            gm_b16_8 t127;
            GM_ST(void *, (uint8_t *)&t127, (void *)(gm_sl5rf5cfkcmdk));
            GM_ST(uintptr_t, (uint8_t *)&t127 + 8, (uintptr_t)(6));
            gm_closure t128 = t126;
            gm_node *ccx129 = t128.cx;
            void * t130 = t128.code;
            uint8_t t131;
            if (GM_PTR_TAGGED(t130)) {
                t131 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t130))(t127);
            } else {
                t131 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t130)(t128.env, ccx129, NULL, t127);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (t131) {
                gm_b16_8 t132;
                GM_ST(void *, (uint8_t *)&t132, (void *)(gm_s048hmhpmbi7c));
                GM_ST(uintptr_t, (uint8_t *)&t132 + 8, (uintptr_t)(7));
                gm_l11 = t132;
            } else {
                gm_closure t133 = gm_l9;
                gm_b16_8 t134;
                GM_ST(void *, (uint8_t *)&t134, (void *)(gm_s1d20j48bjjln));
                GM_ST(uintptr_t, (uint8_t *)&t134 + 8, (uintptr_t)(11));
                gm_closure t135 = t133;
                gm_node *ccx136 = t135.cx;
                void * t137 = t135.code;
                uint8_t t138;
                if (GM_PTR_TAGGED(t137)) {
                    t138 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t137))(t134);
                } else {
                    t138 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t137)(t135.env, ccx136, NULL, t134);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (t138) {
                    gm_b16_8 t139;
                    GM_ST(void *, (uint8_t *)&t139, (void *)(gm_sn3ltks10rrv7));
                    GM_ST(uintptr_t, (uint8_t *)&t139 + 8, (uintptr_t)(7));
                    gm_l11 = t139;
                } else {
                    gm_closure t140 = gm_l9;
                    gm_b16_8 t141;
                    GM_ST(void *, (uint8_t *)&t141, (void *)(gm_scgvr0isof71e));
                    GM_ST(uintptr_t, (uint8_t *)&t141 + 8, (uintptr_t)(8));
                    gm_closure t142 = t140;
                    gm_node *ccx143 = t142.cx;
                    void * t144 = t142.code;
                    uint8_t t145;
                    if (GM_PTR_TAGGED(t144)) {
                        t145 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t144))(t141);
                    } else {
                        t145 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t144)(t142.env, ccx143, NULL, t141);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (t145) {
                        gm_b16_8 t146;
                        GM_ST(void *, (uint8_t *)&t146, (void *)(gm_svl13be5ca8rt));
                        GM_ST(uintptr_t, (uint8_t *)&t146 + 8, (uintptr_t)(4));
                        gm_l11 = t146;
                    }
                }
            }
        }
    }
    {
        uint64_t t147 = gm_l4;
        uint8_t t148 = gm_l6;
        gm_closure t149 = gm_l9;
        gm_b16_8 t150;
        GM_ST(void *, (uint8_t *)&t150, (void *)(gm_sgfrcimthamdn));
        GM_ST(uintptr_t, (uint8_t *)&t150 + 8, (uintptr_t)(9));
        gm_closure t151 = t149;
        gm_node *ccx152 = t151.cx;
        void * t153 = t151.code;
        uint8_t t154;
        if (GM_PTR_TAGGED(t153)) {
            t154 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t153))(t150);
        } else {
            t154 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t153)(t151.env, ccx152, NULL, t150);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (t154) {
            gm_l18 = ((uint64_t)8ULL);
        } else {
            gm_l18 = ((uint64_t)16ULL);
        }
        uint64_t t155 = gm_l18;
        gm_b16_8 t156 = gm_l11;
        gm_b16_8 t157 = gm_l10;
        gm_closure t158 = gm_l9;
        gm_b16_8 t159;
        GM_ST(void *, (uint8_t *)&t159, (void *)(gm_su4retifs0sre));
        GM_ST(uintptr_t, (uint8_t *)&t159 + 8, (uintptr_t)(8));
        gm_closure t160 = t158;
        gm_node *ccx161 = t160.cx;
        void * t162 = t160.code;
        uint8_t t163;
        if (GM_PTR_TAGGED(t162)) {
            t163 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t162))(t159);
        } else {
            t163 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t162)(t160.env, ccx161, NULL, t159);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_closure t164 = gm_l9;
        gm_b16_8 t165;
        GM_ST(void *, (uint8_t *)&t165, (void *)(gm_s26219ruuua5s));
        GM_ST(uintptr_t, (uint8_t *)&t165 + 8, (uintptr_t)(18));
        gm_closure t166 = t164;
        gm_node *ccx167 = t166.cx;
        void * t168 = t166.code;
        uint8_t t169;
        if (GM_PTR_TAGGED(t168)) {
            t169 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t168))(t165);
        } else {
            t169 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t168)(t166.env, ccx167, NULL, t165);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t0rfe1ou14h21 t170;
        GM_ST(uint8_t, (uint8_t *)&t170 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t170 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t170 + 12, 0);
        GM_ST(uint16_t, (uint8_t *)&t170 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&t170 + 60, 0);
        GM_ST(uint64_t, ((uint8_t *)&t170 + 0), t147);
        GM_ST(uint8_t, ((uint8_t *)&t170 + 8), t148);
        GM_ST(uint64_t, ((uint8_t *)&t170 + 16), t155);
        GM_ST(gm_b16_8, ((uint8_t *)&t170 + 24), t156);
        GM_ST(gm_b16_8, ((uint8_t *)&t170 + 40), t157);
        GM_ST(uint8_t, ((uint8_t *)&t170 + 56), t163);
        GM_ST(uint8_t, ((uint8_t *)&t170 + 57), t169);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out))[0] = 0;
        GM_ST(gm_t0rfe1ou14h21, ((uint8_t *)&(*(gm_tod6pa33o8u9u *)gm_out) + 8), t170);
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
inline __attribute__((aligned(4))) void gm_f_value_3oqtvqn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_value_3oqtvqn F;
    gm_b16_8 gm_l0; /* m */
    gm_b16_8 gm_l1; /* v */
    GM_STACK_CHECK(&F, 210u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_th2lqbo77pvsc gm_l2;
    gm_b16_8 gm_l3;
    gm_th2lqbo77pvsc gm_l4;
    {
        void * t2 = (void *)((uint8_t *)&((struct gm_fr_target_from_macros_hkgk1pc *)((&F.h)->up))->l1);
        gm_b16_8 t3 = gm_l0;
        gm_f_get_e3joqcc((void *)&gm_l2, t2, t3);
    }
    uint8_t t4 = (uint8_t)((((uint8_t *)&gm_l2))[16] != 0);
    if (t4) {
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
        gm_l1 = t5;
    } else {
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
    {
        void * t6 = (void *)((uint8_t *)&((struct gm_fr_target_from_macros_hkgk1pc *)((&F.h)->up))->l1);
        gm_b16_8 t7 = gm_l1;
        gm_f_get_e3joqcc((void *)&gm_l4, t6, t7);
    }
    uint8_t t8 = (uint8_t)((((uint8_t *)&gm_l4))[16] != 0);
    if (t8) {
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
        gm_l3 = t9;
    } else {
        gm_b16_8 t10 = gm_l1;
        gm_l3 = t10;
    }
    {
        gm_b16_8 t11 = gm_l3;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t11);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_with_manifest_flags_objbdaj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_with_manifest_flags_objbdaj F;
    gm_b16_8 gm_l1; /* cflags */
    gm_b16_8 gm_l2; /* ldflags */
    GM_STACK_CHECK(&F, 310u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l6;
    gm_tftb1odc739t5 gm_l7;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    {
        memmove(((uint8_t *)&F.l3), ((uint8_t *)&F.l0), 56);
    }
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t3 = (t2 > ((uint64_t)0ULL));
    if (t3) {
        gm_trqfvmjtt4sij t4;
        gm_f_list_epvducg((void *)&t4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l4 = t4;
        void * t5 = (void *)((uint8_t *)&F.l4);
        gm_b16_8 t6 = gm_l1;
        gm_tftb1odc739t5 t7 = gm_f_extend_vogsd72(t5, t6);
        gm_l6 = t7;
        uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
        if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srcmm27gepldtcm, 184, 9);
        void * t9 = (void *)((uint8_t *)&F.l4);
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 16));
        gm_tftb1odc739t5 t11 = gm_f_extend_vogsd72(t9, t10);
        gm_l7 = t11;
        uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
        if (!t12) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srcmm27gepldtcm, 185, 9);
        gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
        gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
        GM_ST(gm_b16_8, (((uint8_t *)&F.l3) + 16), t14);
    }
    uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint8_t t16 = (t15 > ((uint64_t)0ULL));
    if (t16) {
        gm_trqfvmjtt4sij t17;
        gm_f_list_epvducg((void *)&t17, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l5 = t17;
        void * t18 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t19 = gm_l2;
        gm_tftb1odc739t5 t20 = gm_f_extend_vogsd72(t18, t19);
        gm_l8 = t20;
        uint8_t t21 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
        if (!t21) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srcmm27gepldtcm, 190, 9);
        void * t22 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 32));
        gm_tftb1odc739t5 t24 = gm_f_extend_vogsd72(t22, t23);
        gm_l9 = t24;
        uint8_t t25 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
        if (!t25) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcmm27gepldtcm, 191, 9);
        gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
        gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
        GM_ST(gm_b16_8, (((uint8_t *)&F.l3) + 32), t27);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l3), 56);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_words_of_f1gud4i(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1) {
    struct gm_fr_words_of_f1gud4i F;
    gm_b16_8 gm_l0; /* a */
    gm_th2lqbo77pvsc gm_l1; /* b */
    gm_b16_8 gm_l3; /* w */
    GM_STACK_CHECK(&F, 235u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    gm_tftb1odc739t5 gm_l4;
    gm_tftb1odc739t5 gm_l5;
    gm_trqfvmjtt4sij t4;
    gm_f_list_epvducg((void *)&t4, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t4;
    void * t5 = (void *)((uint8_t *)&F.l2);
    gm_b16_8 t6 = gm_l0;
    gm_tftb1odc739t5 t7 = gm_f_push_3ef746b(t5, t6);
    gm_l4 = t7;
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srcmm27gepldtcm, 143, 5);
    {
        {
            uint8_t t9 = (uint8_t)((((uint8_t *)&gm_l1))[16] != 0);
            if (!t9) {
                goto gm_lb2;
            }
            gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l1));
            gm_l3 = t10;
            void * t11 = (void *)((uint8_t *)&F.l2);
            gm_b16_8 t12 = gm_l3;
            gm_tftb1odc739t5 t13 = gm_f_push_3ef746b(t11, t12);
            gm_l5 = t13;
            uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
            if (!t14) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srcmm27gepldtcm, 144, 17);
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
    F.ret = t16;
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

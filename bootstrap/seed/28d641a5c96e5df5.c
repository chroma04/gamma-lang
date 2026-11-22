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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; uint8_t v2; gm_t9qkh0rsooc8d v3; gm_t9qkh0rsooc8d v4; gm_tcc1rq4n1avb2 v5; uint32_t v6; uint32_t v7; } f1; } gm_t8588ma2cr9d7;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `(e: ^const Expr, l: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tf5srh4mp4b3i f1; uint8_t f2; uint8_t pe[7]; } gm_tjpqvfd72o62u;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `(string, usize)`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8588ma2cr9d7) == 12 && _Alignof(gm_t8588ma2cr9d7) == 4 && offsetof(gm_t8588ma2cr9d7, f0) == 0 && offsetof(gm_t8588ma2cr9d7, f1) == 4, "layout of `Builtin`");
typedef struct { gm_tjpqvfd72o62u f0; uint8_t f1; uint8_t pe[7]; } gm_t8cc1mtbrhlpt;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint64_t f0; gm_t0mglksrkfa5e f1; } gm_t8ta482bl6be0;
typedef struct { gm_t8588ma2cr9d7 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_t9q8lhljes74c;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { gm_tnnjeerolos0a f0; gm_tnnjeerolos0a f1; } gm_tcf2f71ra2nps;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { gm_tep2drsl9408b f0; uint8_t f1; uint8_t pe[7]; } gm_tdlpv0q69lov1;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `(addr: string, t: Ty)`");
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tjpqvfd72o62u) == 32 && _Alignof(gm_tjpqvfd72o62u) == 8 && offsetof(gm_tjpqvfd72o62u, f0) == 0 && offsetof(gm_tjpqvfd72o62u, f1) == 8 && offsetof(gm_tjpqvfd72o62u, f2) == 24, "layout of `SoaInfo`");
typedef struct { uint64_t f0; gm_tep2drsl9408b f1; } gm_tjvm1fbs3qbum;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
typedef struct { gm_th2lqbo77pvsc f0; void * f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tm7a6rksvs6n5;
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { gm_th2lqbo77pvsc f0; void * f1; } gm_tpouv9r7lrri2;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8cc1mtbrhlpt) == 40 && _Alignof(gm_t8cc1mtbrhlpt) == 8 && offsetof(gm_t8cc1mtbrhlpt, f0) == 0 && offsetof(gm_t8cc1mtbrhlpt, f1) == 32, "layout of `?SoaInfo`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8ta482bl6be0) == 48 && _Alignof(gm_t8ta482bl6be0) == 8 && offsetof(gm_t8ta482bl6be0, f0) == 0 && offsetof(gm_t8ta482bl6be0, f1) == 8, "layout of `(i: usize, x: FieldDef)`");
_Static_assert(sizeof(gm_t9q8lhljes74c) == 32 && _Alignof(gm_t9q8lhljes74c) == 8 && offsetof(gm_t9q8lhljes74c, f0) == 0 && offsetof(gm_t9q8lhljes74c, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tcf2f71ra2nps) == 16 && _Alignof(gm_tcf2f71ra2nps) == 4 && offsetof(gm_tcf2f71ra2nps, f0) == 0 && offsetof(gm_tcf2f71ra2nps, f1) == 8, "layout of `(ArrayKind, ArrayKind)`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdlpv0q69lov1) == 32 && _Alignof(gm_tdlpv0q69lov1) == 8 && offsetof(gm_tdlpv0q69lov1, f0) == 0 && offsetof(gm_tdlpv0q69lov1, f1) == 24, "layout of `?(variants: []VariantDef, tag: Ty)`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tjvm1fbs3qbum) == 32 && _Alignof(gm_tjvm1fbs3qbum) == 8 && offsetof(gm_tjvm1fbs3qbum, f0) == 0 && offsetof(gm_tjvm1fbs3qbum, f1) == 8, "layout of `(i: usize, x: SourceField)`");
_Static_assert(sizeof(gm_tm7a6rksvs6n5) == 48 && _Alignof(gm_tm7a6rksvs6n5) == 8 && offsetof(gm_tm7a6rksvs6n5, f0) == 0 && offsetof(gm_tm7a6rksvs6n5, f1) == 24 && offsetof(gm_tm7a6rksvs6n5, f2) == 32, "layout of `TuplePart`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tpouv9r7lrri2) == 32 && _Alignof(gm_tpouv9r7lrri2) == 8 && offsetof(gm_tpouv9r7lrri2, f0) == 0 && offsetof(gm_tpouv9r7lrri2, f1) == 24, "layout of `SpreadPart`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
typedef struct { uint64_t f0; gm_tckdobunaulee f1; } gm_t3l8idb2m6c1r;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_t3l8idb2m6c1r) == 80 && _Alignof(gm_t3l8idb2m6c1r) == 8 && offsetof(gm_t3l8idb2m6c1r, f0) == 0 && offsetof(gm_t3l8idb2m6c1r, f1) == 8, "layout of `(i: usize, x: VariantDef)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_t9q8lhljes74c v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; void * v55; gm_tandvbt63uk7n v56; gm_t710lsrq1m8gk v57; } f1; } gm_to0f5qn75cgg8;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_to0f5qn75cgg8) == 64 && _Alignof(gm_to0f5qn75cgg8) == 8 && offsetof(gm_to0f5qn75cgg8, f0) == 0 && offsetof(gm_to0f5qn75cgg8, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_to0f5qn75cgg8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tsf6i2agfnpva;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_tsf6i2agfnpva) == 88 && _Alignof(gm_tsf6i2agfnpva) == 8 && offsetof(gm_tsf6i2agfnpva, f0) == 0 && offsetof(gm_tsf6i2agfnpva, f1) == 8 && offsetof(gm_tsf6i2agfnpva, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
struct gm_fr_bind_temp_sa5rcbr {
    gm_frame h;
    void * ret;
};
struct gm_fr_clear_4qp22g2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_construct_struct_ve6tb88 {
    gm_frame h;
    gm_trqfvmjtt4sij l9; /* stmts */
    gm_trqfvmjtt4sij l10; /* slots */
    gm_trqfvmjtt4sij l11; /* explicit */
    gm_trqfvmjtt4sij l14; /* written */
    gm_t7te2tdpebbt4 l18; /* arg */
    gm_tc3oht4ksth9d l19; /* e */
    gm_tc3oht4ksth9d l21; /* e */
    gm_t0mglksrkfa5e l26; /* f */
    gm_tc3oht4ksth9d l29; /* e */
    gm_t0mglksrkfa5e l46; /* f */
    gm_trqfvmjtt4sij l57; /* values */
    void * ret;
};
struct gm_fr_construct_tuple_6ee5ejf {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* slots */
    gm_t7te2tdpebbt4 l13; /* a */
    gm_trqfvmjtt4sij l14; /* stmts */
    gm_tc3oht4ksth9d l16; /* e */
    gm_t7te2tdpebbt4 l24; /* arg */
    gm_trqfvmjtt4sij l25; /* parts */
    gm_tc3oht4ksth9d l26; /* e */
    gm_tc3oht4ksth9d l28; /* e */
    gm_tc3oht4ksth9d l30; /* e */
    gm_tm7a6rksvs6n5 l37; /* part */
    gm_trqfvmjtt4sij l41; /* values */
    void * ret;
};
struct gm_fr_construct_variant_o6is81c {
    gm_frame h;
    gm_tckdobunaulee l10; /* v */
    gm_tc3oht4ksth9d l18; /* x */
    void * ret;
};
struct gm_fr_default_value_lo723gi {
    gm_frame h;
    gm_tbci5ospk5nnn l11; /* v */
    gm_tngtuiuskn938 l13; /* r */
    void * ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8q3kspo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_b8btuen {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_dtfif5p {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ose1ba9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vj3ckdj_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_9huj44a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_expr_o6qt7e2 {
    gm_frame h;
    void * ret;
};
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_field_default_4ssmakm {
    gm_frame h;
    gm_tpautr4br7h3l l7; /* def */
    void * ret;
};
struct gm_fr_field_default_expr_tqfuiff {
    gm_frame h;
    void * ret;
};
struct gm_fr_field_value_19vleud {
    gm_frame h;
    void * ret;
};
struct gm_fr_float_value_cr4h9gi {
    gm_frame h;
    gm_tngtuiuskn938 l1; /* r */
    void * ret;
};
struct gm_fr_format_0o67o8r {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_12rbmhd {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_1e409ou {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3tnlp8q {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_6fdilsr {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_6nmb3uf {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_843cvhd {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_9i81nmj {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_9tg469g {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_a07tfib {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_adtncrl {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bbsjt91 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bm4rhr0 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_e1r9br2 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_j4le8o0 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ktbu9b9 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l002mid {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lc07prd {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_s1oodgm {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t8pf89n {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_from_int_ofjuoqh {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* i */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_plain_data_ej4mh2m {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_zero_5aept6u {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    uint8_t ret;
};
struct gm_fr_is_zero_8tcct9o {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5kthfl8 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7gsrufb {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ir0h3tc_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_pmp06kq {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_neg_0u9uqg6 {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_neg_agd5c5k {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_9a0n4ld {
    gm_frame h;
    gm_tm7a6rksvs6n5 l1; /* x */
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
struct gm_fr_push_lnebar3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_r2k6rjf {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ucgmlo2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_vd1290p {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_52486fh {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_61fb705_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_gp456bk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h08har5_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_hkmvuvc {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j7fb9h1 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_k4j2gj3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_show_8uijkbs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_fsnl6fa {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_spread_source_fields_5csral7 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_t0mglksrkfa5e l13; /* f */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_strip_wrappers_e5thpib {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_temp_ilbvj8q {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_template_9o92cvv {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_to_float_o1iop8t {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    double ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqb2ul45tidi(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void * gm_f_bind_temp_sa5rcbr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_struct_ty_l1kjtap(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_block_typed_e7svg7n(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_check_const_expr_2ot4q9v(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3);
inline __attribute__((aligned(4))) void gm_f_clear_4qp22g2(void * a0);
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_construct_struct_ve6tb88(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_construct_tuple_6ee5ejf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_construct_variant_o6is81c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2, const void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void gm_f_copy_pmkg67r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_default_const_k60bpu4(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) void * gm_f_default_value_lo723gi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_b8btuen(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dtfif5p(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ose1ba9(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) void * gm_f_field_default_4ssmakm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint64_t a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) void * gm_f_field_default_expr_tqfuiff(void * a0, uint32_t a1, uint64_t a2);
__attribute__((aligned(4))) void * gm_f_field_value_19vleud(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_float_value_cr4h9gi(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) void gm_f_format_0o67o8r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_12rbmhd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_1e409ou(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_3tnlp8q(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_6fdilsr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_6nmb3uf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_843cvhd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_9i81nmj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_9tg469g(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_a07tfib(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_adtncrl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_bbsjt91(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_bm4rhr0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_e1r9br2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_j4le8o0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ktbu9b9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_l002mid(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_lc07prd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_rkil4k4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_s1oodgm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_t8pf89n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_hidden_field_u7uao2s(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_o9r57ha(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_plain_data_ej4mh2m(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_is_plain_n60iihm(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_5aept6u(const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_8tcct9o(const void * a0);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_lift_temps_7c6gk6l(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5kthfl8(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7gsrufb(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_pmp06kq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) void gm_f_neg_0u9uqg6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_neg_agd5c5k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) uint32_t gm_f_new_local_003h9es(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, uint8_t a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_9a0n4ld(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_lnebar3(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_r2k6rjf(void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_52486fh(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_gp456bk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h08har5_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hkmvuvc(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j7fb9h1(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void * gm_f_soa_construct_8nun9tl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void gm_f_soa_info_pq4q43l(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_spread_parts_e62q2l2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_spread_source_fields_5csral7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2);
__attribute__((aligned(4))) gm_csguankko4lmhd gm_f_strip_wrappers_e5thpib(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_struct_fields_chqu3it(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint32_t gm_f_temp_ilbvj8q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1);
__attribute__((aligned(4))) double gm_f_to_f16_0pcu0pq(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) float gm_f_to_f32_8bnhsji(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) double gm_f_to_f64_4avhrht(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) double gm_f_to_float_o1iop8t(void *env, gm_node *cx, gm_node *inst, const void * a0, uint16_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_uninit_message_u2c3peu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_union_variants_a7khaqp(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern const uint8_t gm_s0uum3co6vapt[23];
extern const uint8_t gm_s1lj31vo6lvqe[26];
extern const uint8_t gm_s2haaj13v41fd[25];
extern const uint8_t gm_s323uftq8kr36[49];
extern const uint8_t gm_s454sq1vshj3e[77];
extern const uint8_t gm_s802sbfa6mtsg[27];
extern const uint8_t gm_s9pf9com5692e[39];
extern const uint8_t gm_sbapuchngtvak[28];
extern const uint8_t gm_sbdqlrmtee4eo[66];
extern const uint8_t gm_sc6uqn3rvm1p8[56];
extern const uint8_t gm_ser747td9jt8d[41];
extern const uint8_t gm_setofg57no9qt[34];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sfdj0c5vltira[31];
extern const uint8_t gm_sglgjrjci6t1d[25];
extern const uint8_t gm_shi1s7rq8m35t[40];
extern const uint8_t gm_shv1k5m05egbe[6];
extern const uint8_t gm_sknpneumdgqfs[38];
extern const uint8_t gm_slpi6dabivg2e[29];
extern const uint8_t gm_sml38fgda46cm[56];
extern const uint8_t gm_sna4vk4o059d0[10];
extern const uint8_t gm_so1m4qmhi2fvm[93];
extern const uint8_t gm_sr2ptnspelf8s[98];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src7nt72u848oha[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srrqh3pev0igg[9];
extern const uint8_t gm_sto8li44rv2ou[8];
extern const gm_tmpl gm_tpl1g1lh0kp24oa;
extern const gm_tmpl gm_tpl56vg2jjplgvg;
extern const gm_tmpl gm_tpl5hraomijgptv;
extern const gm_tmpl gm_tpl9vu8tqcv1d9s;
extern const gm_tmpl gm_tplacjeuo4at0jj;
extern const gm_tmpl gm_tpldp3bk13rlc44;
extern const gm_tmpl gm_tplful0p0r5pifs;
extern const gm_tmpl gm_tplg575kk57c4i8;
extern const gm_tmpl gm_tplh6c513o0038j;
extern const gm_tmpl gm_tplk7hjamhd5jcl;
extern const gm_tmpl gm_tplpf7ckn15kjth;
extern const gm_tmpl gm_tplq5eond7ctc71;
extern const gm_tmpl gm_tplqbtvqd3a4g25;
extern const gm_tmpl gm_tplr869c1s382eh;
extern const gm_tmpl gm_tplre5cfbnr7ltv;
extern const gm_tmpl gm_tplrivm1rlf1ps3;
extern const gm_tmpl gm_tpls3402qmr7otb;
extern const gm_tmpl gm_tplsnlr78cvfrao;
extern const gm_tmpl gm_tplsqibbtt8789o;
extern const gm_tmpl gm_tpltvrl7dp7h9m1;
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
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline bool gm_eqb2ul45tidi(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)1) return gm_eq9m6mvtmgde(a + 4, b + 4);
    return true;
}
inline __attribute__((aligned(4))) void gm_f_clear_4qp22g2(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_4qp22g2 F;
    void * gm_l0; /* l */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    void * t4 = GM_LD(void *, ((uint8_t *)t3));
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t3) + 8));
    uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
    if (t6 > t7 || t7 > (uintptr_t)t5) gm_trap_slice(t6, t7, t5, gm_srcrgc9bkquf6nr, 140, 15);
    uint64_t t8 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t9;
    GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t4 + (uintptr_t)((uint64_t)0ULL) * 8)));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8));
    GM_ST(gm_b16_8, ((uint8_t *)t2), t9);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_b8btuen(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_b8btuen F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dtfif5p(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_dtfif5p F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ose1ba9(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ose1ba9 F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_error_89glhjr F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 8));
    gm_tl874t2atnlh3 t5 = gm_l1;
    gm_b16_8 t6 = gm_l2;
    gm_b0_1 t7 = gm_f_report_eardf07((void *)NULL, cx, NULL, t4, t5, t6);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_error_9huj44a F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* msg */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    gm_tl874t2atnlh3 t4 = gm_l1;
    gm_b16_8 t5 = gm_l2;
    gm_b0_1 t6 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t3, t4, t5);
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
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_error_expr_o6qt7e2 F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 156u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = gm_f_empty_slice_g3tddrb_2();
    gm_tl874t2atnlh3 t6 = gm_l1;
    gm_to0f5qn75cgg8 t7;
    memset(&t7, 0, sizeof t7);
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)35ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t7 + 8), t5);
    void * t8 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t4, &t7, t6);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_error_ki8i3k6 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)26ULL));
    gm_t88rcucg1b7uk t4 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t2, &t3);
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
inline __attribute__((aligned(4))) void * gm_f_field_default_expr_tqfuiff(void * a0, uint32_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_field_default_expr_tqfuiff F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* id */
    uint64_t gm_l2; /* i */
    gm_b16_8 gm_l3; /* exprs */
    GM_STACK_CHECK_SHALLOW(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 1088));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 1088) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_srclv0dcbbfhves, 716, 14);
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + (uintptr_t)t6 * 184) + 96));
    gm_l3 = t7;
    uint64_t t8 = gm_l2;
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint8_t t10 = (t8 >= t9);
    if (t10) {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    void * t11 = GM_LD(void *, ((uint8_t *)&gm_l3));
    uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint64_t t13 = gm_l2;
    void * t14 = GM_LD(void *, (((uint8_t *)t11) + (uintptr_t)t13 * 8));
    F.ret = t14;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_format_0o67o8r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_0o67o8r F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s454sq1vshj3e));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(76));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpltvrl7dp7h9m1), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_12rbmhd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_12rbmhd F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sml38fgda46cm));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(55));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplacjeuo4at0jj), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_1e409ou(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_1e409ou F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sbdqlrmtee4eo));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(65));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplr869c1s382eh), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_3tnlp8q(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_3tnlp8q F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sfdj0c5vltira));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(30));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpls3402qmr7otb), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_6fdilsr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_6fdilsr F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s323uftq8kr36));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(48));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplre5cfbnr7ltv), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_6nmb3uf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_6nmb3uf F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_so1m4qmhi2fvm));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(92));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplk7hjamhd5jcl), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_843cvhd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_843cvhd F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9pf9com5692e));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(38));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplsnlr78cvfrao), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_9i81nmj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_9i81nmj F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_ser747td9jt8d));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(40));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl1g1lh0kp24oa), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_9tg469g(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_9tg469g F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sknpneumdgqfs));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(37));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpldp3bk13rlc44), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_a07tfib(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_a07tfib F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sc6uqn3rvm1p8));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(55));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplh6c513o0038j), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_adtncrl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_adtncrl F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slpi6dabivg2e));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(28));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplpf7ckn15kjth), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_bbsjt91(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_bbsjt91 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1lj31vo6lvqe));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(25));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl56vg2jjplgvg), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_bm4rhr0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_bm4rhr0 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2haaj13v41fd));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(24));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplqbtvqd3a4g25), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_e1r9br2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_e1r9br2 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shi1s7rq8m35t));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(39));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl5hraomijgptv), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_j4le8o0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_j4le8o0 F;
    gm_tf1bjg81u0glv gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s802sbfa6mtsg));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(26));
    gm_tf1bjg81u0glv t3 = GM_LD(gm_tf1bjg81u0glv, ((uint8_t *)a0));
    gm_f_format_rkil4k4((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplful0p0r5pifs), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ktbu9b9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ktbu9b9 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbapuchngtvak));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(27));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplq5eond7ctc71), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_s1oodgm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_s1oodgm F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sr2ptnspelf8s));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(97));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplsqibbtt8789o), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_t8pf89n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_t8pf89n F;
    gm_tf1bjg81u0glv gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_setofg57no9qt));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(33));
    gm_tf1bjg81u0glv t3 = GM_LD(gm_tf1bjg81u0glv, ((uint8_t *)a0));
    gm_f_format_rkil4k4((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplrivm1rlf1ps3), t2);
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
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_from_int_ofjuoqh F;
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tbci5ospk5nnn t2 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l0));
    gm_tbci5ospk5nnn t3;
    gm_f_one_08qg1op((void *)&t3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&(*(gm_tngtuiuskn938 *)gm_out) + 0), t2);
    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&(*(gm_tngtuiuskn938 *)gm_out) + 48), t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_param_u9t0efs F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 53u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 40));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 40) + 8));
    uint32_t t5 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 249, 57);
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)t3) + (uintptr_t)t6 * 1));
    uint8_t t8 = (uint8_t)(t7 & ((uint8_t)1ULL));
    uint8_t t9 = (!(t8 == ((uint8_t)0ULL)));
    F.ret = t9;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_error_8k27gd1 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 140u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_t53at80pfdus3 t4;
    gm_f_kind_jcnt4hm((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
    uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    uint8_t t6 = (uint8_t)((t5 == ((uint8_t)26ULL)));
    gm_t53at80pfdus3 t7;
    memset(&t7, 0, sizeof t7);
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)26ULL));
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_plain_data_ej4mh2m(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_is_plain_data_ej4mh2m F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 66u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    uint8_t t6 = gm_f_is_error_8k27gd1(t4, GM_CONV(gm_csguankko4lmhd, t5));
    if (t6) {
        gm_l2 = ((uint8_t)1);
    } else {
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, ((uint8_t *)t7));
        gm_t88rcucg1b7uk t9 = gm_l1;
        uint8_t t10 = gm_f_is_plain_n60iihm(t8, GM_CONV(gm_csguankko4lmhd, t9));
        gm_l2 = t10;
    }
    uint8_t t11 = gm_l2;
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_5aept6u(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_zero_5aept6u F;
    GM_STACK_CHECK_SHALLOW(&F, 3u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t2 = gm_f_is_zero_8tcct9o(&(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
    F.ret = t2;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_8tcct9o(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_zero_8tcct9o F;
    GM_STACK_CHECK_SHALLOW(&F, 11u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint64_t t2 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 8));
    uint8_t t3 = ((t2 == ((uint64_t)0ULL)));
    F.ret = t3;
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
inline __attribute__((aligned(4))) void gm_f_list_5kthfl8(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_5kthfl8 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_b8btuen();
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
inline __attribute__((aligned(4))) void gm_f_list_7gsrufb(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7gsrufb F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_dtfif5p();
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
inline __attribute__((aligned(4))) void gm_f_neg_0u9uqg6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_neg_0u9uqg6 F;
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tbci5ospk5nnn t2;
    gm_f_neg_agd5c5k((void *)&t2, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t3 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l0) + 48));
    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&(*(gm_tngtuiuskn938 *)gm_out) + 0), t2);
    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&(*(gm_tngtuiuskn938 *)gm_out) + 48), t3);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_9a0n4ld(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_9a0n4ld F;
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
        gm_b16_8 t9 = gm_f_empty_slice_b8btuen();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_52486fh(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 48), ((uint8_t *)a1), 48);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_lnebar3(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_lnebar3 F;
    void * gm_l0; /* l */
    gm_tep2drsl9408b gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_dtfif5p();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_gp456bk(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_52486fh(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_52486fh F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_gp456bk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_gp456bk F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_report_eardf07 F;
    void * gm_l0; /* ds */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 256u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    gm_tl874t2atnlh3 t3 = gm_l1;
    gm_b16_8 t4 = gm_l2;
    gm_t4gut2c9fj1kt t5;
    gm_f_error_s6hn871((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_push_o4ckdr1(t2, &t5);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_8uijkbs F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_b16_8 t5 = gm_f_show_kmjeh8s((void *)NULL, cx, NULL, t3, t4);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_fsnl6fa F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_b16_8 t5 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t3, t4);
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
inline __attribute__((aligned(4))) uint32_t gm_f_temp_ilbvj8q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_temp_ilbvj8q F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shv1k5m05egbe));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_tl874t2atnlh3 t5 = gm_l2;
    uint32_t t6 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t2, t3, t4, ((uint8_t)1), t5);
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
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_template_9o92cvv F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 176));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 176) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 276, 56);
    uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)t3) + (uintptr_t)t6 * 4));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) double gm_f_to_float_o1iop8t(void *env, gm_node *cx, gm_node *inst, const void * a0, uint16_t a1) {
    struct gm_fr_to_float_o1iop8t F;
    uint16_t gm_l1; /* bits */
    GM_STACK_CHECK(&F, 52u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_l1 = a1;
    uint16_t t2 = gm_l1;
    uint8_t t3 = ((t2 == ((uint16_t)16ULL)));
    if (t3) {
        double t4 = gm_f_to_f16_0pcu0pq((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t4;
        goto gm_fret;
    }
    uint16_t t5 = gm_l1;
    uint8_t t6 = ((t5 == ((uint16_t)32ULL)));
    if (t6) {
        float t7 = gm_f_to_f32_8bnhsji((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        double t8 = (double)t7;
        F.ret = t8;
        goto gm_fret;
    }
    double t9 = gm_f_to_f64_4avhrht((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l0)));
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
inline __attribute__((aligned(4))) void * gm_f_bind_temp_sa5rcbr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_bind_temp_sa5rcbr F;
    void * gm_l0; /* cx */
    void * gm_l1; /* v */
    void * gm_l2; /* stmts */
    uint32_t gm_l3; /* t */
    GM_STACK_CHECK(&F, 327u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l4;
    gm_tftb1odc739t5 gm_l5;
    void * t12 = gm_l1;
    {
        {
            uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)0ULL));
            if (!t14) {
                goto gm_lb2;
            }
            gm_l4 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
            uint8_t t16 = (uint8_t)(t15 == ((uint8_t)1ULL));
            if (!t16) {
                goto gm_lb3;
            }
            gm_l4 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)2ULL));
            if (!t18) {
                goto gm_lb4;
            }
            gm_l4 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)4ULL));
            if (!t20) {
                goto gm_lb5;
            }
            gm_l4 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)5ULL));
            if (!t22) {
                goto gm_lb6;
            }
            gm_l4 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb6:;
        {
            uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)7ULL));
            if (!t24) {
                goto gm_lb7;
            }
            gm_l4 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb7:;
        {
            uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)8ULL));
            if (!t26) {
                goto gm_lb8;
            }
            gm_l4 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb8:;
        {
            uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
            uint8_t t28 = (uint8_t)(t27 == ((uint8_t)14ULL));
            if (!t28) {
                goto gm_lb9;
            }
            gm_l4 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb9:;
        {
            gm_l4 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb10:;
        gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 379, 8);
    }
gm_lb1:;
    uint8_t t29 = gm_l4;
    if (t29) {
        void * t30 = gm_l1;
        F.ret = t30;
        goto gm_fret;
    }
    void * t31 = gm_l0;
    void * t32 = gm_l1;
    void * t33 = gm_l2;
    gm_b0_1 t34 = gm_f_lift_temps_7c6gk6l((void *)NULL, cx, NULL, t31, t32, t33);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t35 = gm_l0;
    void * t36 = gm_l1;
    gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t36));
    void * t38 = gm_l1;
    gm_tl874t2atnlh3 t39 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t38) + 72));
    uint32_t t40 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t35, t37, t39);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t40;
    {
        void * t41 = gm_l2;
        uint32_t t42 = gm_l3;
        void * t43 = gm_l1;
        gm_ttggq7ng4kp3p t44;
        GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t44 + 0), t42);
        GM_ST(void *, ((uint8_t *)&t44 + 8), t43);
        gm_trcmmd12cefb1 t45;
        GM_ST(uint8_t, (uint8_t *)&t45 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t45 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t45 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t45 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)0ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t45 + 8), t44);
        gm_tftb1odc739t5 t46 = gm_f_push_d6c76ud_2(t41, &t45);
        gm_l5 = t46;
    }
    uint8_t t47 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t47) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_src7nt72u848oha, 384, 5);
    {
        void * t48 = gm_l1;
        gm_t88rcucg1b7uk t49 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t48));
        uint32_t t50 = gm_l3;
        void * t51 = gm_l1;
        gm_tl874t2atnlh3 t52 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t51) + 72));
        gm_to0f5qn75cgg8 t53;
        memset(&t53, 0, sizeof t53);
        GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)15ULL));
        GM_ST(uint32_t, ((uint8_t *)&t53 + 8), t50);
        void * t54 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t49, &t53, t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t54;
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
__attribute__((aligned(4))) void * gm_f_construct_struct_ve6tb88(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_construct_struct_ve6tb88 F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* args */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint32_t gm_l4; /* id */
    gm_tjpqvfd72o62u gm_l5; /* info */
    gm_t6j6smj7qoctu gm_l6; /* h */
    gm_b16_8 gm_l7; /* fields */
    uint64_t gm_l8; /* n */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint8_t gm_l15; /* bad_args */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_t08olq97rscn3 gm_l20; /* fname */
    gm_tf5srh4mp4b3i gm_l22; /* found */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    uint64_t gm_l25; /* i */
    uint64_t gm_l27; /* i */
    void * gm_l28; /* v */
    void * gm_l30; /* v */
    gm_t88rcucg1b7uk gm_l31; /* vt */
    gm_b16_8 gm_l32; /* src */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    uint64_t gm_l35; /* w */
    void * gm_l36; /* x */
    void * gm_l37; /* base */
    gm_b16_8 gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    uint64_t gm_l40; /* k */
    gm_tep2drsl9408b gm_l41; /* sf */
    gm_tf5srh4mp4b3i gm_l42; /* found */
    gm_b16_8 gm_l43; /* <tmp> */
    uint64_t gm_l44; /* <tmp> */
    uint64_t gm_l45; /* i */
    uint64_t gm_l47; /* i */
    void * gm_l48; /* proj */
    uint8_t gm_l49; /* out_of_order */
    uint64_t gm_l50; /* <tmp> */
    uint64_t gm_l51; /* <tmp> */
    uint64_t gm_l52; /* k */
    gm_b16_8 gm_l53; /* <tmp> */
    uint64_t gm_l54; /* <tmp> */
    uint64_t gm_l55; /* w */
    void * gm_l56; /* v */
    uint64_t gm_l58; /* <tmp> */
    uint64_t gm_l59; /* <tmp> */
    uint64_t gm_l60; /* i */
    void * gm_l61; /* v */
    gm_t88rcucg1b7uk gm_l62; /* ft */
    void * gm_l63; /* d */
    void * gm_l64; /* agg */
    GM_STACK_CHECK(&F, 5475u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t53at80pfdus3 gm_l65;
    gm_t8cc1mtbrhlpt gm_l66;
    gm_ttu09lij36f8c gm_l67;
    gm_tabb710d9mq2e gm_l68;
    gm_tabb710d9mq2e gm_l69;
    gm_th2lqbo77pvsc gm_l70;
    gm_tftb1odc739t5 gm_l71;
    gm_tftb1odc739t5 gm_l72;
    gm_tabb710d9mq2e gm_l73;
    gm_t8ta482bl6be0 gm_l74;
    gm_tabb710d9mq2e gm_l75;
    gm_tabb710d9mq2e gm_l76;
    gm_tftb1odc739t5 gm_l77;
    gm_th2lqbo77pvsc gm_l78;
    gm_tjvm1fbs3qbum gm_l79;
    gm_t8ta482bl6be0 gm_l80;
    gm_tabb710d9mq2e gm_l81;
    gm_tabb710d9mq2e gm_l82;
    uint8_t gm_l83;
    gm_tabb710d9mq2e gm_l84;
    gm_tftb1odc739t5 gm_l85;
    uint8_t gm_l86;
    gm_tkn42u7na3t81 gm_l87;
    gm_tsf6i2agfnpva gm_l88;
    gm_tftb1odc739t5 gm_l89;
    void * gm_l90;
    gm_tftb1odc739t5 gm_l91;
    gm_tabb710d9mq2e gm_l92;
    gm_tftb1odc739t5 gm_l93;
    {
        {
            {
                {
                    void * t73 = gm_l0;
                    void * t74 = GM_LD(void *, ((uint8_t *)t73));
                    void * t75 = (void *)(((uint8_t *)t74) + 8);
                    gm_t88rcucg1b7uk t76 = gm_l1;
                    gm_f_kind_jcnt4hm((void *)&gm_l65, t75, GM_CONV(gm_csguankko4lmhd, t76));
                    uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&gm_l65) + 0));
                    uint8_t t78 = (uint8_t)(t77 == ((uint8_t)17ULL));
                    if (!t78) {
                        goto gm_lb2;
                    }
                    uint32_t t79 = GM_LD(uint32_t, (((uint8_t *)&gm_l65) + 8));
                    gm_l4 = t79;
                    {
                        {
                            {
                                void * t80 = gm_l0;
                                void * t81 = GM_LD(void *, ((uint8_t *)t80));
                                gm_t88rcucg1b7uk t82 = gm_l1;
                                gm_f_soa_info_pq4q43l((void *)&gm_l66, t81, GM_CONV(gm_csguankko4lmhd, t82));
                            }
                            uint8_t t83 = (uint8_t)((((uint8_t *)&gm_l66))[32] != 0);
                            if (!t83) {
                                goto gm_lb4;
                            }
                            {
                                memmove(((uint8_t *)&gm_l5), ((uint8_t *)&gm_l66), 32);
                            }
                            {
                                void * t84 = gm_l0;
                                gm_t88rcucg1b7uk t85 = gm_l1;
                                gm_b16_8 t86 = gm_l2;
                                gm_tl874t2atnlh3 t87 = gm_l3;
                                void * t88 = gm_f_soa_construct_8nun9tl((void *)NULL, cx, NULL, t84, t85, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l5)), t86, t87);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t88;
                                goto gm_fret;
                            }
                        }
gm_lb4:;
                    }
gm_lb3:;
                    {
                        {
                            {
                                void * t89 = gm_l0;
                                gm_t88rcucg1b7uk t90 = gm_l1;
                                gm_f_hidden_field_u7uao2s((void *)&gm_l67, t89, GM_CONV(gm_csguankko4lmhd, t90));
                            }
                            uint8_t t91 = (uint8_t)((((uint8_t *)&gm_l67))[32] != 0);
                            if (!t91) {
                                goto gm_lb6;
                            }
                            {
                                memmove(((uint8_t *)&gm_l6), ((uint8_t *)&gm_l67), 32);
                            }
                            {
                                void * t92 = gm_l0;
                                gm_tl874t2atnlh3 t93 = gm_l3;
                                void * t94 = gm_l0;
                                gm_t88rcucg1b7uk t95 = gm_l1;
                                gm_b16_8 t96 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t94, t95);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_b16_8 t97 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 16));
                                gm_b16_8 t98 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
                                gm_tfeab279c16rr t99;
                                GM_ST(gm_b16_8, ((uint8_t *)&t99 + 0), t96);
                                GM_ST(gm_b16_8, ((uint8_t *)&t99 + 16), t97);
                                GM_ST(gm_b16_8, ((uint8_t *)&t99 + 32), t98);
                                gm_f_format_0o67o8r((void *)&gm_l68, (void *)NULL, cx, NULL, &t99);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t100 = (uint8_t)!((((uint8_t *)&gm_l68))[0] != 0);
                                if (!t100) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l68) + 8, gm_src7nt72u848oha, 194, 24);
                                gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l68) + 8));
                                gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&t101));
                                gm_b0_1 t103 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t92, t93, t102);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            void * t104 = gm_l0;
                            void * t105 = GM_LD(void *, ((uint8_t *)t104));
                            gm_tl874t2atnlh3 t106 = gm_l3;
                            void * t107 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t105, t106);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t107;
                            goto gm_fret;
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint32_t t108 = gm_l4;
                    void * t109 = gm_l0;
                    void * t110 = GM_LD(void *, ((uint8_t *)t109));
                    gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)t110) + 1848));
                    uint8_t t112 = 0;
                    for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t111) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t111))) + i * 4, ((uint8_t *)&t108))) { t112 = 1; break; }
                    uint8_t t113 = (uint8_t)(t112);
                    if (t113) {
                        void * t114 = gm_l0;
                        gm_tl874t2atnlh3 t115 = gm_l3;
                        void * t116 = gm_l0;
                        gm_t88rcucg1b7uk t117 = gm_l1;
                        gm_b16_8 t118 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t116, t117);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tnpifv20irnph t119;
                        GM_ST(gm_b16_8, ((uint8_t *)&t119 + 0), t118);
                        gm_f_format_s1oodgm((void *)&gm_l69, (void *)NULL, cx, NULL, t119);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t120 = (uint8_t)!((((uint8_t *)&gm_l69))[0] != 0);
                        if (!t120) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l69) + 8, gm_src7nt72u848oha, 200, 24);
                        gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l69) + 8));
                        gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&t121));
                        gm_b0_1 t123 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t114, t115, t122);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t124 = gm_l0;
                        void * t125 = GM_LD(void *, ((uint8_t *)t124));
                        gm_tl874t2atnlh3 t126 = gm_l3;
                        void * t127 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t125, t126);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t127;
                        goto gm_fret;
                    }
                }
                {
                    void * t128 = gm_l0;
                    void * t129 = GM_LD(void *, ((uint8_t *)t128));
                    gm_t88rcucg1b7uk t130 = gm_l1;
                    gm_f_struct_fields_chqu3it((void *)&gm_l70, t129, GM_CONV(gm_csguankko4lmhd, t130));
                    uint8_t t131 = (uint8_t)((((uint8_t *)&gm_l70))[16] != 0);
                    if (t131) {
                        gm_b16_8 t132 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l70));
                        gm_l7 = t132;
                    } else {
                        gm_b16_8 t133 = gm_f_empty_slice_ose1ba9();
                        gm_l7 = t133;
                    }
                    uint64_t t134 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    gm_l8 = t134;
                    gm_trqfvmjtt4sij t135;
                    gm_f_list_ir0h3tc_2((void *)&t135, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l9 = t135;
                    gm_trqfvmjtt4sij t136;
                    gm_f_list_pmp06kq((void *)&t136, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l10 = t136;
                    gm_trqfvmjtt4sij t137;
                    gm_f_list_s83jn2q((void *)&t137, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l11 = t137;
                    gm_l13 = ((uint64_t)0ULL);
                    uint64_t t138 = gm_l8;
                    gm_l12 = t138;
                    {
                        for (;;) {
                            uint64_t t139 = gm_l13;
                            uint64_t t140 = gm_l12;
                            uint8_t t141 = (t139 < t140);
                            if (!t141) {
                                goto gm_lb7;
                            }
                            {
                                {
                                    {
                                        void * t142 = (void *)((uint8_t *)&F.l10);
                                        gm_tftb1odc739t5 t143 = gm_f_push_r2k6rjf(t142, ((void *)0));
                                        gm_l71 = t143;
                                        uint8_t t144 = (uint8_t)!((((uint8_t *)&gm_l71))[0] != 0);
                                        if (!t144) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l71) + 1, gm_src7nt72u848oha, 209, 9);
                                        void * t145 = (void *)((uint8_t *)&F.l11);
                                        gm_tftb1odc739t5 t146 = gm_f_push_vd1290p(t145, ((uint8_t)0));
                                        gm_l72 = t146;
                                        uint8_t t147 = (uint8_t)!((((uint8_t *)&gm_l72))[0] != 0);
                                        if (!t147) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l72) + 1, gm_src7nt72u848oha, 210, 9);
                                        goto gm_lb10;
                                    }
gm_lb11:;
                                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 208, 5);
                                }
gm_lb10:;
                            }
gm_lb9:;
                            uint64_t t148 = gm_l13;
                            uint64_t r149; bool o150 = __builtin_add_overflow(t148, ((uint64_t)1ULL), &r149);
                            uint64_t t151 = r149;
                            gm_l13 = t151;
                        }
                    }
gm_lb7:;
                    gm_trqfvmjtt4sij t152;
                    gm_f_list_5dhgg4i((void *)&t152, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l14 = t152;
                    gm_l15 = ((uint8_t)0);
                    gm_b16_8 t153 = gm_l2;
                    gm_l16 = t153;
                    gm_l17 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            {
                                uint64_t t154 = gm_l17;
                                uint64_t t155 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                                uint8_t t156 = (t154 < t155);
                                if (!t156) {
                                    goto gm_lb12;
                                }
                                {
                                    {
                                        void * t157 = GM_LD(void *, ((uint8_t *)&gm_l16));
                                        uint64_t t158 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                                        uint64_t t159 = gm_l17;
                                        {
                                            {
                                                {
                                                    {
                                                        memmove(((uint8_t *)&F.l18), (((uint8_t *)t157) + (uintptr_t)t159 * 144), 144);
                                                        {
                                                            {
                                                                {
                                                                    uint8_t t160 = GM_LD(uint8_t, (((uint8_t *)&F.l18) + 0));
                                                                    uint8_t t161 = (uint8_t)(t160 == ((uint8_t)0ULL));
                                                                    if (!t161) {
                                                                        goto gm_lb18;
                                                                    }
                                                                    memmove(((uint8_t *)&F.l19), (((uint8_t *)&F.l18) + 8), 88);
                                                                    uint8_t t162 = gm_l15;
                                                                    uint8_t t163 = (uint8_t)!t162;
                                                                    if (t163) {
                                                                        void * t164 = gm_l0;
                                                                        gm_tl874t2atnlh3 t165 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 72));
                                                                        void * t166 = gm_l0;
                                                                        gm_t88rcucg1b7uk t167 = gm_l1;
                                                                        gm_b16_8 t168 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t166, t167);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        void * t169 = gm_l0;
                                                                        gm_t88rcucg1b7uk t170 = gm_l1;
                                                                        gm_b16_8 t171 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t169, t170);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        gm_t6j6smj7qoctu t172;
                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t172 + 0), t168);
                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t172 + 16), t171);
                                                                        gm_f_format_1e409ou((void *)&gm_l73, (void *)NULL, cx, NULL, &t172);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        uint8_t t173 = (uint8_t)!((((uint8_t *)&gm_l73))[0] != 0);
                                                                        if (!t173) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l73) + 8, gm_src7nt72u848oha, 218, 38);
                                                                        gm_b16_8 t174 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l73) + 8));
                                                                        gm_b16_8 t175 = GM_LD(gm_b16_8, ((uint8_t *)&t174));
                                                                        gm_b0_1 t176 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t164, t165, t175);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                    }
                                                                    gm_l15 = ((uint8_t)1);
                                                                    goto gm_lb17;
                                                                }
gm_lb18:;
                                                                {
                                                                    uint8_t t177 = GM_LD(uint8_t, (((uint8_t *)&F.l18) + 0));
                                                                    uint8_t t178 = (uint8_t)(t177 == ((uint8_t)1ULL));
                                                                    if (!t178) {
                                                                        goto gm_lb19;
                                                                    }
                                                                    {
                                                                        memmove(((uint8_t *)&gm_l20), (((uint8_t *)&F.l18) + 8), 32);
                                                                    }
                                                                    {
                                                                        memmove(((uint8_t *)&F.l21), ((((uint8_t *)&F.l18) + 8) + 32), 88);
                                                                    }
                                                                    gm_tf5srh4mp4b3i t179;
                                                                    memset(&t179, 0, sizeof t179);
                                                                    gm_l22 = t179;
                                                                    gm_b16_8 t180 = gm_l7;
                                                                    gm_b16_8 t181 = GM_LD(gm_b16_8, ((uint8_t *)&t180));
                                                                    gm_l23 = t181;
                                                                    gm_l24 = ((uint64_t)0ULL);
                                                                    {
                                                                        for (;;) {
                                                                            uint64_t t182 = gm_l24;
                                                                            uint64_t t183 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                                                                            uint8_t t184 = (t182 < t183);
                                                                            if (!t184) {
                                                                                goto gm_lb20;
                                                                            }
                                                                            {
                                                                                {
                                                                                    uint64_t t185 = gm_l24;
                                                                                    void * t186 = GM_LD(void *, ((uint8_t *)&gm_l23));
                                                                                    uint64_t t187 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                                                                                    uint64_t t188 = gm_l24;
                                                                                    gm_t0mglksrkfa5e t189 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)t186) + (uintptr_t)t188 * 40));
                                                                                    GM_ST(uint64_t, ((uint8_t *)&gm_l74 + 0), t185);
                                                                                    GM_ST(gm_t0mglksrkfa5e, ((uint8_t *)&gm_l74 + 8), t189);
                                                                                }
                                                                                {
                                                                                    {
                                                                                        uint64_t t190 = GM_LD(uint64_t, ((uint8_t *)&gm_l74));
                                                                                        gm_l25 = t190;
                                                                                        memmove(((uint8_t *)&F.l26), (((uint8_t *)&gm_l74) + 8), 40);
                                                                                        gm_b16_8 t191 = GM_LD(gm_b16_8, ((uint8_t *)&F.l26));
                                                                                        gm_b16_8 t192 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l20));
                                                                                        uint8_t t193 = (gm_eq0iqiubfm7e(((uint8_t *)&t191), ((uint8_t *)&t192)));
                                                                                        if (t193) {
                                                                                            uint64_t t194 = gm_l25;
                                                                                            gm_tf5srh4mp4b3i t195;
                                                                                            GM_ST(uint8_t, (uint8_t *)&t195 + 9, 0);
                                                                                            GM_ST(uint16_t, (uint8_t *)&t195 + 10, 0);
                                                                                            GM_ST(uint32_t, (uint8_t *)&t195 + 12, 0);
                                                                                            GM_ST(uint64_t, ((uint8_t *)&t195), t194);
                                                                                            ((uint8_t *)&t195)[8] = 1;
                                                                                            gm_l22 = t195;
                                                                                            goto gm_lb20;
                                                                                        }
                                                                                        goto gm_lb23;
                                                                                    }
gm_lb24:;
                                                                                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 224, 17);
                                                                                }
gm_lb23:;
                                                                            }
gm_lb22:;
                                                                            uint64_t t196 = gm_l24;
                                                                            uint64_t r197; bool o198 = __builtin_add_overflow(t196, ((uint64_t)1ULL), &r197);
                                                                            uint64_t t199 = r197;
                                                                            gm_l24 = t199;
                                                                        }
                                                                    }
gm_lb20:;
                                                                    uint8_t t200 = (uint8_t)((((uint8_t *)&gm_l22))[8] != 0);
                                                                    if (t200) {
                                                                        uint64_t t201 = GM_LD(uint64_t, ((uint8_t *)&gm_l22));
                                                                        gm_l27 = t201;
                                                                    } else {
                                                                        {
                                                                            void * t202 = gm_l0;
                                                                            gm_tl874t2atnlh3 t203 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l20) + 16));
                                                                            void * t204 = gm_l0;
                                                                            gm_t88rcucg1b7uk t205 = gm_l1;
                                                                            gm_b16_8 t206 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t204, t205);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_lad;
                                                                            }
                                                                            gm_b16_8 t207 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l20));
                                                                            gm_t6j6smj7qoctu t208;
                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t208 + 0), t206);
                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t208 + 16), t207);
                                                                            gm_f_format_lc07prd((void *)&gm_l75, (void *)NULL, cx, NULL, &t208);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_lad;
                                                                            }
                                                                            uint8_t t209 = (uint8_t)!((((uint8_t *)&gm_l75))[0] != 0);
                                                                            if (!t209) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l75) + 8, gm_src7nt72u848oha, 231, 42);
                                                                            gm_b16_8 t210 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l75) + 8));
                                                                            gm_b16_8 t211 = GM_LD(gm_b16_8, ((uint8_t *)&t210));
                                                                            gm_b0_1 t212 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t202, t203, t211);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_lad;
                                                                            }
                                                                        }
                                                                        void * t213 = gm_l0;
                                                                        void * t214 = (void *)((uint8_t *)&F.l21);
                                                                        gm_tkn42u7na3t81 t215;
                                                                        memset(&t215, 0, sizeof t215);
                                                                        void * t216 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t213, t214, t215);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        goto gm_lb14;
                                                                    }
                                                                    void * t217 = GM_LD(void *, ((uint8_t *)&F.l11));
                                                                    uint64_t t218 = GM_LD(uint64_t, (((uint8_t *)&F.l11) + 8));
                                                                    uint64_t t219 = gm_l27;
                                                                    if ((uintptr_t)t219 >= (uintptr_t)(t218)) gm_trap_index((uintptr_t)t219, t218, gm_src7nt72u848oha, 235, 20);
                                                                    uint8_t t220 = GM_LD(uint8_t, (((uint8_t *)t217) + (uintptr_t)t219 * 1));
                                                                    if (t220) {
                                                                        void * t221 = gm_l0;
                                                                        gm_tl874t2atnlh3 t222 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l20) + 16));
                                                                        gm_b16_8 t223 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l20));
                                                                        gm_tnpifv20irnph t224;
                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t224 + 0), t223);
                                                                        gm_f_format_bbsjt91((void *)&gm_l76, (void *)NULL, cx, NULL, t224);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        uint8_t t225 = (uint8_t)!((((uint8_t *)&gm_l76))[0] != 0);
                                                                        if (!t225) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l76) + 8, gm_src7nt72u848oha, 235, 61);
                                                                        gm_b16_8 t226 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l76) + 8));
                                                                        gm_b16_8 t227 = GM_LD(gm_b16_8, ((uint8_t *)&t226));
                                                                        gm_b0_1 t228 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t221, t222, t227);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                    }
                                                                    void * t229 = GM_LD(void *, ((uint8_t *)&F.l11));
                                                                    uint64_t t230 = GM_LD(uint64_t, (((uint8_t *)&F.l11) + 8));
                                                                    uint64_t t231 = gm_l27;
                                                                    if ((uintptr_t)t231 >= (uintptr_t)(t230)) gm_trap_index((uintptr_t)t231, t230, gm_src7nt72u848oha, 236, 17);
                                                                    GM_ST(uint8_t, (((uint8_t *)t229) + (uintptr_t)t231 * 1), ((uint8_t)1));
                                                                    void * t232 = gm_l0;
                                                                    void * t233 = (void *)((uint8_t *)&F.l21);
                                                                    void * t234 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                                                    uint64_t t235 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                                                    uint64_t t236 = gm_l27;
                                                                    if ((uintptr_t)t236 >= (uintptr_t)(t235)) gm_trap_index((uintptr_t)t236, t235, gm_src7nt72u848oha, 237, 41);
                                                                    gm_t88rcucg1b7uk t237 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t234) + (uintptr_t)t236 * 40) + 16));
                                                                    void * t238 = gm_f_field_value_19vleud((void *)NULL, cx, NULL, t232, t233, t237);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    gm_l28 = t238;
                                                                    void * t239 = (void *)((uint8_t *)&F.l14);
                                                                    uint64_t t240 = gm_l27;
                                                                    gm_tftb1odc739t5 t241 = gm_f_push_ucgmlo2(t239, t240);
                                                                    gm_l77 = t241;
                                                                    uint8_t t242 = (uint8_t)!((((uint8_t *)&gm_l77))[0] != 0);
                                                                    if (!t242) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l77) + 1, gm_src7nt72u848oha, 238, 17);
                                                                    void * t243 = GM_LD(void *, ((uint8_t *)&F.l10));
                                                                    uint64_t t244 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                                                                    uint64_t t245 = gm_l27;
                                                                    if ((uintptr_t)t245 >= (uintptr_t)(t244)) gm_trap_index((uintptr_t)t245, t244, gm_src7nt72u848oha, 239, 17);
                                                                    void * t246 = gm_l28;
                                                                    GM_ST(void *, (((uint8_t *)t243) + (uintptr_t)t245 * 8), t246);
                                                                    goto gm_lb17;
                                                                }
gm_lb19:;
                                                            }
                                                            {
                                                                {
                                                                    uint8_t t247 = GM_LD(uint8_t, (((uint8_t *)&F.l18) + 0));
                                                                    uint8_t t248 = (uint8_t)(t247 == ((uint8_t)2ULL));
                                                                    if (!t248) {
                                                                        goto gm_lb25;
                                                                    }
                                                                    {
                                                                        memmove(((uint8_t *)&F.l29), (((uint8_t *)&F.l18) + 8), 88);
                                                                    }
                                                                    void * t249 = gm_l0;
                                                                    void * t250 = (void *)((uint8_t *)&F.l29);
                                                                    gm_tkn42u7na3t81 t251;
                                                                    memset(&t251, 0, sizeof t251);
                                                                    void * t252 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t249, t250, t251);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    gm_l30 = t252;
                                                                    void * t253 = gm_l0;
                                                                    void * t254 = (void *)(((uint8_t *)t253) + 96);
                                                                    void * t255 = gm_l0;
                                                                    void * t256 = GM_LD(void *, ((uint8_t *)t255));
                                                                    void * t257 = gm_l30;
                                                                    gm_t88rcucg1b7uk t258 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t257));
                                                                    gm_t88rcucg1b7uk t259 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t254, t256, t258);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    gm_l31 = t259;
                                                                    {
                                                                        void * t260 = gm_l0;
                                                                        gm_t88rcucg1b7uk t261 = gm_l31;
                                                                        void * t262 = (void *)((uint8_t *)&F.l29);
                                                                        gm_f_spread_source_fields_5csral7((void *)&gm_l78, (void *)NULL, cx, NULL, t260, t261, t262);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                    }
                                                                    uint8_t t263 = (uint8_t)((((uint8_t *)&gm_l78))[16] != 0);
                                                                    if (t263) {
                                                                        gm_b16_8 t264 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l78));
                                                                        gm_l32 = t264;
                                                                    } else {
                                                                        goto gm_lb14;
                                                                    }
                                                                    gm_b16_8 t265 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
                                                                    gm_l33 = t265;
                                                                    gm_l34 = ((uint64_t)0ULL);
                                                                    {
                                                                        for (;;) {
                                                                            uint64_t t266 = gm_l34;
                                                                            uint64_t t267 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                                                                            uint8_t t268 = (t266 < t267);
                                                                            if (!t268) {
                                                                                goto gm_lb26;
                                                                            }
                                                                            {
                                                                                void * t269 = GM_LD(void *, ((uint8_t *)&gm_l33));
                                                                                uint64_t t270 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                                                                                uint64_t t271 = gm_l34;
                                                                                {
                                                                                    {
                                                                                        uint64_t t272 = GM_LD(uint64_t, (((uint8_t *)t269) + (uintptr_t)t271 * 8));
                                                                                        gm_l35 = t272;
                                                                                        {
                                                                                            {
                                                                                                void * t273 = GM_LD(void *, ((uint8_t *)&F.l10));
                                                                                                uint64_t t274 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                                                                                                uint64_t t275 = gm_l35;
                                                                                                if ((uintptr_t)t275 >= (uintptr_t)(t274)) gm_trap_index((uintptr_t)t275, t274, gm_src7nt72u848oha, 247, 29);
                                                                                                uint8_t t276 = (uint8_t)(GM_LD(void *, (((uint8_t *)t273) + (uintptr_t)t275 * 8)) != 0);
                                                                                                if (!t276) {
                                                                                                    goto gm_lb32;
                                                                                                }
                                                                                                void * t277 = GM_LD(void *, (((uint8_t *)t273) + (uintptr_t)t275 * 8));
                                                                                                gm_l36 = t277;
                                                                                                void * t278 = GM_LD(void *, ((uint8_t *)&F.l10));
                                                                                                uint64_t t279 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                                                                                                uint64_t t280 = gm_l35;
                                                                                                if ((uintptr_t)t280 >= (uintptr_t)(t279)) gm_trap_index((uintptr_t)t280, t279, gm_src7nt72u848oha, 247, 46);
                                                                                                void * t281 = gm_l0;
                                                                                                void * t282 = gm_l36;
                                                                                                void * t283 = (void *)((uint8_t *)&F.l9);
                                                                                                void * t284 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t281, t282, t283);
                                                                                                if (gm_unw.kind) {
                                                                                                    goto gm_lad;
                                                                                                }
                                                                                                GM_ST(void *, (((uint8_t *)t278) + (uintptr_t)t280 * 8), t284);
                                                                                                goto gm_lb31;
                                                                                            }
gm_lb32:;
                                                                                        }
gm_lb31:;
                                                                                        goto gm_lb29;
                                                                                    }
gm_lb30:;
                                                                                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 246, 17);
                                                                                }
gm_lb29:;
                                                                            }
gm_lb28:;
                                                                            uint64_t t285 = gm_l34;
                                                                            uint64_t r286; bool o287 = __builtin_add_overflow(t285, ((uint64_t)1ULL), &r286);
                                                                            uint64_t t288 = r286;
                                                                            gm_l34 = t288;
                                                                        }
                                                                    }
gm_lb26:;
                                                                    void * t289 = (void *)((uint8_t *)&F.l14);
                                                                    gm_f_clear_4qp22g2(t289);
                                                                    void * t290 = gm_l0;
                                                                    void * t291 = gm_l30;
                                                                    void * t292 = (void *)((uint8_t *)&F.l9);
                                                                    void * t293 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t290, t291, t292);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    gm_l37 = t293;
                                                                    gm_b16_8 t294 = gm_l32;
                                                                    gm_b16_8 t295 = GM_LD(gm_b16_8, ((uint8_t *)&t294));
                                                                    gm_l38 = t295;
                                                                    gm_l39 = ((uint64_t)0ULL);
                                                                    {
                                                                        for (;;) {
                                                                            uint64_t t296 = gm_l39;
                                                                            uint64_t t297 = GM_LD(uint64_t, (((uint8_t *)&gm_l38) + 8));
                                                                            uint8_t t298 = (t296 < t297);
                                                                            if (!t298) {
                                                                                goto gm_lb33;
                                                                            }
                                                                            {
                                                                                {
                                                                                    uint64_t t299 = gm_l39;
                                                                                    void * t300 = GM_LD(void *, ((uint8_t *)&gm_l38));
                                                                                    uint64_t t301 = GM_LD(uint64_t, (((uint8_t *)&gm_l38) + 8));
                                                                                    uint64_t t302 = gm_l39;
                                                                                    gm_tep2drsl9408b t303 = GM_LD(gm_tep2drsl9408b, (((uint8_t *)t300) + (uintptr_t)t302 * 24));
                                                                                    GM_ST(uint64_t, ((uint8_t *)&gm_l79 + 0), t299);
                                                                                    GM_ST(gm_tep2drsl9408b, ((uint8_t *)&gm_l79 + 8), t303);
                                                                                }
                                                                                {
                                                                                    {
                                                                                        uint64_t t304 = GM_LD(uint64_t, ((uint8_t *)&gm_l79));
                                                                                        gm_l40 = t304;
                                                                                        {
                                                                                            memmove(((uint8_t *)&gm_l41), (((uint8_t *)&gm_l79) + 8), 24);
                                                                                        }
                                                                                        gm_tf5srh4mp4b3i t305;
                                                                                        memset(&t305, 0, sizeof t305);
                                                                                        gm_l42 = t305;
                                                                                        gm_b16_8 t306 = gm_l7;
                                                                                        gm_b16_8 t307 = GM_LD(gm_b16_8, ((uint8_t *)&t306));
                                                                                        gm_l43 = t307;
                                                                                        gm_l44 = ((uint64_t)0ULL);
                                                                                        {
                                                                                            for (;;) {
                                                                                                uint64_t t308 = gm_l44;
                                                                                                uint64_t t309 = GM_LD(uint64_t, (((uint8_t *)&gm_l43) + 8));
                                                                                                uint8_t t310 = (t308 < t309);
                                                                                                if (!t310) {
                                                                                                    goto gm_lb38;
                                                                                                }
                                                                                                {
                                                                                                    {
                                                                                                        uint64_t t311 = gm_l44;
                                                                                                        void * t312 = GM_LD(void *, ((uint8_t *)&gm_l43));
                                                                                                        uint64_t t313 = GM_LD(uint64_t, (((uint8_t *)&gm_l43) + 8));
                                                                                                        uint64_t t314 = gm_l44;
                                                                                                        gm_t0mglksrkfa5e t315 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)t312) + (uintptr_t)t314 * 40));
                                                                                                        GM_ST(uint64_t, ((uint8_t *)&gm_l80 + 0), t311);
                                                                                                        GM_ST(gm_t0mglksrkfa5e, ((uint8_t *)&gm_l80 + 8), t315);
                                                                                                    }
                                                                                                    {
                                                                                                        {
                                                                                                            uint64_t t316 = GM_LD(uint64_t, ((uint8_t *)&gm_l80));
                                                                                                            gm_l45 = t316;
                                                                                                            memmove(((uint8_t *)&F.l46), (((uint8_t *)&gm_l80) + 8), 40);
                                                                                                            gm_b16_8 t317 = GM_LD(gm_b16_8, ((uint8_t *)&F.l46));
                                                                                                            gm_b16_8 t318 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l41));
                                                                                                            uint8_t t319 = (gm_eq0iqiubfm7e(((uint8_t *)&t317), ((uint8_t *)&t318)));
                                                                                                            if (t319) {
                                                                                                                uint64_t t320 = gm_l45;
                                                                                                                gm_tf5srh4mp4b3i t321;
                                                                                                                GM_ST(uint8_t, (uint8_t *)&t321 + 9, 0);
                                                                                                                GM_ST(uint16_t, (uint8_t *)&t321 + 10, 0);
                                                                                                                GM_ST(uint32_t, (uint8_t *)&t321 + 12, 0);
                                                                                                                GM_ST(uint64_t, ((uint8_t *)&t321), t320);
                                                                                                                ((uint8_t *)&t321)[8] = 1;
                                                                                                                gm_l42 = t321;
                                                                                                                goto gm_lb38;
                                                                                                            }
                                                                                                            goto gm_lb41;
                                                                                                        }
gm_lb42:;
                                                                                                        gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 253, 21);
                                                                                                    }
gm_lb41:;
                                                                                                }
gm_lb40:;
                                                                                                uint64_t t322 = gm_l44;
                                                                                                uint64_t r323; bool o324 = __builtin_add_overflow(t322, ((uint64_t)1ULL), &r323);
                                                                                                uint64_t t325 = r323;
                                                                                                gm_l44 = t325;
                                                                                            }
                                                                                        }
gm_lb38:;
                                                                                        uint8_t t326 = (uint8_t)((((uint8_t *)&gm_l42))[8] != 0);
                                                                                        if (t326) {
                                                                                            uint64_t t327 = GM_LD(uint64_t, ((uint8_t *)&gm_l42));
                                                                                            gm_l47 = t327;
                                                                                        } else {
                                                                                            {
                                                                                                void * t328 = gm_l0;
                                                                                                gm_tl874t2atnlh3 t329 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l29) + 72));
                                                                                                void * t330 = gm_l0;
                                                                                                gm_t88rcucg1b7uk t331 = gm_l1;
                                                                                                gm_b16_8 t332 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t330, t331);
                                                                                                if (gm_unw.kind) {
                                                                                                    goto gm_lad;
                                                                                                }
                                                                                                gm_b16_8 t333 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l41));
                                                                                                gm_t6j6smj7qoctu t334;
                                                                                                GM_ST(gm_b16_8, ((uint8_t *)&t334 + 0), t332);
                                                                                                GM_ST(gm_b16_8, ((uint8_t *)&t334 + 16), t333);
                                                                                                gm_f_format_6fdilsr((void *)&gm_l81, (void *)NULL, cx, NULL, &t334);
                                                                                                if (gm_unw.kind) {
                                                                                                    goto gm_lad;
                                                                                                }
                                                                                                uint8_t t335 = (uint8_t)!((((uint8_t *)&gm_l81))[0] != 0);
                                                                                                if (!t335) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l81) + 8, gm_src7nt72u848oha, 260, 42);
                                                                                                gm_b16_8 t336 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l81) + 8));
                                                                                                gm_b16_8 t337 = GM_LD(gm_b16_8, ((uint8_t *)&t336));
                                                                                                gm_b0_1 t338 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t328, t329, t337);
                                                                                                if (gm_unw.kind) {
                                                                                                    goto gm_lad;
                                                                                                }
                                                                                            }
                                                                                            goto gm_lb35;
                                                                                        }
                                                                                        void * t339 = GM_LD(void *, ((uint8_t *)&F.l11));
                                                                                        uint64_t t340 = GM_LD(uint64_t, (((uint8_t *)&F.l11) + 8));
                                                                                        uint64_t t341 = gm_l47;
                                                                                        if ((uintptr_t)t341 >= (uintptr_t)(t340)) gm_trap_index((uintptr_t)t341, t340, gm_src7nt72u848oha, 263, 24);
                                                                                        uint8_t t342 = GM_LD(uint8_t, (((uint8_t *)t339) + (uintptr_t)t341 * 1));
                                                                                        if (t342) {
                                                                                            void * t343 = gm_l0;
                                                                                            gm_tl874t2atnlh3 t344 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l29) + 72));
                                                                                            gm_b16_8 t345 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l41));
                                                                                            gm_tnpifv20irnph t346;
                                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t346 + 0), t345);
                                                                                            gm_f_format_adtncrl((void *)&gm_l82, (void *)NULL, cx, NULL, t346);
                                                                                            if (gm_unw.kind) {
                                                                                                goto gm_lad;
                                                                                            }
                                                                                            uint8_t t347 = (uint8_t)!((((uint8_t *)&gm_l82))[0] != 0);
                                                                                            if (!t347) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l82) + 8, gm_src7nt72u848oha, 266, 42);
                                                                                            gm_b16_8 t348 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l82) + 8));
                                                                                            gm_b16_8 t349 = GM_LD(gm_b16_8, ((uint8_t *)&t348));
                                                                                            gm_b0_1 t350 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t343, t344, t349);
                                                                                            if (gm_unw.kind) {
                                                                                                goto gm_lad;
                                                                                            }
                                                                                            goto gm_lb35;
                                                                                        }
                                                                                        void * t351 = GM_LD(void *, ((uint8_t *)&F.l10));
                                                                                        uint64_t t352 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                                                                                        uint64_t t353 = gm_l47;
                                                                                        if ((uintptr_t)t353 >= (uintptr_t)(t352)) gm_trap_index((uintptr_t)t353, t352, gm_src7nt72u848oha, 269, 24);
                                                                                        {
                                                                                            {
                                                                                                uint8_t t354 = (uint8_t)(GM_LD(void *, (((uint8_t *)t351) + (uintptr_t)t353 * 8)) != 0);
                                                                                                if (t354) {
                                                                                                    goto gm_lb44;
                                                                                                }
                                                                                                gm_l83 = ((uint8_t)0);
                                                                                                goto gm_lb43;
                                                                                            }
gm_lb44:;
                                                                                            {
                                                                                                gm_l83 = ((uint8_t)1);
                                                                                                goto gm_lb43;
                                                                                            }
gm_lb45:;
                                                                                            gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 269, 24);
                                                                                        }
gm_lb43:;
                                                                                        uint8_t t355 = gm_l83;
                                                                                        if (t355) {
                                                                                            void * t356 = gm_l0;
                                                                                            gm_tl874t2atnlh3 t357 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l29) + 72));
                                                                                            gm_b16_8 t358 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l41));
                                                                                            gm_tnpifv20irnph t359;
                                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t359 + 0), t358);
                                                                                            gm_f_format_9tg469g((void *)&gm_l84, (void *)NULL, cx, NULL, t359);
                                                                                            if (gm_unw.kind) {
                                                                                                goto gm_lad;
                                                                                            }
                                                                                            uint8_t t360 = (uint8_t)!((((uint8_t *)&gm_l84))[0] != 0);
                                                                                            if (!t360) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l84) + 8, gm_src7nt72u848oha, 269, 70);
                                                                                            gm_b16_8 t361 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l84) + 8));
                                                                                            gm_b16_8 t362 = GM_LD(gm_b16_8, ((uint8_t *)&t361));
                                                                                            gm_b0_1 t363 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t356, t357, t362);
                                                                                            if (gm_unw.kind) {
                                                                                                goto gm_lad;
                                                                                            }
                                                                                        }
                                                                                        {
                                                                                            gm_t88rcucg1b7uk t364 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l41) + 16));
                                                                                            void * t365 = gm_l37;
                                                                                            uint64_t t366 = gm_l40;
                                                                                            uint32_t t367 = (uint32_t)t366;
                                                                                            gm_tbl8il92l97ut t368;
                                                                                            GM_ST(uint32_t, (uint8_t *)&t368 + 12, 0);
                                                                                            GM_ST(void *, ((uint8_t *)&t368 + 0), t365);
                                                                                            GM_ST(uint32_t, ((uint8_t *)&t368 + 8), t367);
                                                                                            gm_tl874t2atnlh3 t369 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l29) + 72));
                                                                                            gm_to0f5qn75cgg8 t370;
                                                                                            memset(&t370, 0, sizeof t370);
                                                                                            GM_ST(uint8_t, ((uint8_t *)&t370 + 0), ((uint8_t)19ULL));
                                                                                            GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t370 + 8), t368);
                                                                                            void * t371 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t364, &t370, t369);
                                                                                            if (gm_unw.kind) {
                                                                                                goto gm_lad;
                                                                                            }
                                                                                            gm_l48 = t371;
                                                                                        }
                                                                                        void * t372 = GM_LD(void *, ((uint8_t *)&F.l10));
                                                                                        uint64_t t373 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                                                                                        uint64_t t374 = gm_l47;
                                                                                        if ((uintptr_t)t374 >= (uintptr_t)(t373)) gm_trap_index((uintptr_t)t374, t373, gm_src7nt72u848oha, 271, 21);
                                                                                        void * t375 = gm_l0;
                                                                                        void * t376 = gm_l48;
                                                                                        void * t377 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                                                                        uint64_t t378 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                                                                        uint64_t t379 = gm_l47;
                                                                                        if ((uintptr_t)t379 >= (uintptr_t)(t378)) gm_trap_index((uintptr_t)t379, t378, gm_src7nt72u848oha, 271, 54);
                                                                                        gm_t88rcucg1b7uk t380 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t377) + (uintptr_t)t379 * 40) + 16));
                                                                                        void * t381 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t375, t376, t380);
                                                                                        if (gm_unw.kind) {
                                                                                            goto gm_lad;
                                                                                        }
                                                                                        GM_ST(void *, (((uint8_t *)t372) + (uintptr_t)t374 * 8), t381);
                                                                                        goto gm_lb36;
                                                                                    }
gm_lb37:;
                                                                                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 251, 17);
                                                                                }
gm_lb36:;
                                                                            }
gm_lb35:;
                                                                            uint64_t t382 = gm_l39;
                                                                            uint64_t r383; bool o384 = __builtin_add_overflow(t382, ((uint64_t)1ULL), &r383);
                                                                            uint64_t t385 = r383;
                                                                            gm_l39 = t385;
                                                                        }
                                                                    }
gm_lb33:;
                                                                    goto gm_lb17;
                                                                }
gm_lb25:;
                                                            }
                                                            {
                                                                gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 215, 9);
                                                            }
                                                        }
gm_lb17:;
                                                    }
                                                    {
                                                        goto gm_lb15;
                                                    }
                                                }
gm_lb16:;
                                            }
                                            {
                                                gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 214, 5);
                                            }
                                        }
gm_lb15:;
                                    }
                                }
gm_lb14:;
                            }
                            {
                                uint64_t t386 = gm_l17;
                                uint64_t r387; bool o388 = __builtin_add_overflow(t386, ((uint64_t)1ULL), &r387);
                                uint64_t t389 = r387;
                                gm_l17 = t389;
                            }
                        }
                    }
gm_lb12:;
                }
                {
                    gm_l49 = ((uint8_t)0);
                    gm_l51 = ((uint64_t)1ULL);
                    uint64_t t390 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 8));
                    uint64_t t391 = (t390 > ((uint64_t)1ULL) ? t390 : ((uint64_t)1ULL));
                    gm_l50 = t391;
                    {
                        for (;;) {
                            uint64_t t392 = gm_l51;
                            uint64_t t393 = gm_l50;
                            uint8_t t394 = (t392 < t393);
                            if (!t394) {
                                goto gm_lb46;
                            }
                            {
                                {
                                    {
                                        uint64_t t395 = gm_l51;
                                        gm_l52 = t395;
                                        void * t396 = GM_LD(void *, ((uint8_t *)&F.l14));
                                        uint64_t t397 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 8));
                                        uint64_t t398 = gm_l52;
                                        uint64_t r399; bool o400 = __builtin_sub_overflow(t398, ((uint64_t)1ULL), &r399);
                                        if (o400) gm_trap("integer overflow in `-`", gm_src7nt72u848oha, 280, 26);
                                        uint64_t t401 = r399;
                                        if ((uintptr_t)t401 >= (uintptr_t)(t397)) gm_trap_index((uintptr_t)t401, t397, gm_src7nt72u848oha, 280, 12);
                                        uint64_t t402 = GM_LD(uint64_t, (((uint8_t *)t396) + (uintptr_t)t401 * 8));
                                        void * t403 = GM_LD(void *, ((uint8_t *)&F.l14));
                                        uint64_t t404 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 8));
                                        uint64_t t405 = gm_l52;
                                        if ((uintptr_t)t405 >= (uintptr_t)(t404)) gm_trap_index((uintptr_t)t405, t404, gm_src7nt72u848oha, 280, 35);
                                        uint64_t t406 = GM_LD(uint64_t, (((uint8_t *)t403) + (uintptr_t)t405 * 8));
                                        uint8_t t407 = (t402 > t406);
                                        if (t407) {
                                            gm_l49 = ((uint8_t)1);
                                        }
                                        goto gm_lb49;
                                    }
gm_lb50:;
                                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 279, 5);
                                }
gm_lb49:;
                            }
gm_lb48:;
                            uint64_t t408 = gm_l51;
                            uint64_t r409; bool o410 = __builtin_add_overflow(t408, ((uint64_t)1ULL), &r409);
                            uint64_t t411 = r409;
                            gm_l51 = t411;
                        }
                    }
gm_lb46:;
                    uint8_t t412 = gm_l49;
                    if (t412) {
                        gm_b16_8 t413 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
                        gm_l53 = t413;
                        gm_l54 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t414 = gm_l54;
                                uint64_t t415 = GM_LD(uint64_t, (((uint8_t *)&gm_l53) + 8));
                                uint8_t t416 = (t414 < t415);
                                if (!t416) {
                                    goto gm_lb51;
                                }
                                {
                                    void * t417 = GM_LD(void *, ((uint8_t *)&gm_l53));
                                    uint64_t t418 = GM_LD(uint64_t, (((uint8_t *)&gm_l53) + 8));
                                    uint64_t t419 = gm_l54;
                                    {
                                        {
                                            uint64_t t420 = GM_LD(uint64_t, (((uint8_t *)t417) + (uintptr_t)t419 * 8));
                                            gm_l55 = t420;
                                            {
                                                {
                                                    void * t421 = GM_LD(void *, ((uint8_t *)&F.l10));
                                                    uint64_t t422 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                                                    uint64_t t423 = gm_l55;
                                                    if ((uintptr_t)t423 >= (uintptr_t)(t422)) gm_trap_index((uintptr_t)t423, t422, gm_src7nt72u848oha, 284, 21);
                                                    uint8_t t424 = (uint8_t)(GM_LD(void *, (((uint8_t *)t421) + (uintptr_t)t423 * 8)) != 0);
                                                    if (!t424) {
                                                        goto gm_lb57;
                                                    }
                                                    void * t425 = GM_LD(void *, (((uint8_t *)t421) + (uintptr_t)t423 * 8));
                                                    gm_l56 = t425;
                                                    void * t426 = GM_LD(void *, ((uint8_t *)&F.l10));
                                                    uint64_t t427 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                                                    uint64_t t428 = gm_l55;
                                                    if ((uintptr_t)t428 >= (uintptr_t)(t427)) gm_trap_index((uintptr_t)t428, t427, gm_src7nt72u848oha, 284, 38);
                                                    void * t429 = gm_l0;
                                                    void * t430 = gm_l56;
                                                    void * t431 = (void *)((uint8_t *)&F.l9);
                                                    void * t432 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t429, t430, t431);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    GM_ST(void *, (((uint8_t *)t426) + (uintptr_t)t428 * 8), t432);
                                                    goto gm_lb56;
                                                }
gm_lb57:;
                                            }
gm_lb56:;
                                            goto gm_lb54;
                                        }
gm_lb55:;
                                        gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 283, 9);
                                    }
gm_lb54:;
                                }
gm_lb53:;
                                uint64_t t433 = gm_l54;
                                uint64_t r434; bool o435 = __builtin_add_overflow(t433, ((uint64_t)1ULL), &r434);
                                uint64_t t436 = r434;
                                gm_l54 = t436;
                            }
                        }
gm_lb51:;
                    }
                }
                {
                    gm_trqfvmjtt4sij t437;
                    gm_f_list_vp1djv1_2((void *)&t437, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l57 = t437;
                    gm_l59 = ((uint64_t)0ULL);
                    uint64_t t438 = gm_l8;
                    gm_l58 = t438;
                    {
                        for (;;) {
                            uint64_t t439 = gm_l59;
                            uint64_t t440 = gm_l58;
                            uint8_t t441 = (t439 < t440);
                            if (!t441) {
                                goto gm_lb58;
                            }
                            {
                                {
                                    {
                                        uint64_t t442 = gm_l59;
                                        gm_l60 = t442;
                                        {
                                            {
                                                void * t443 = GM_LD(void *, ((uint8_t *)&F.l10));
                                                uint64_t t444 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                                                uint64_t t445 = gm_l60;
                                                if ((uintptr_t)t445 >= (uintptr_t)(t444)) gm_trap_index((uintptr_t)t445, t444, gm_src7nt72u848oha, 289, 17);
                                                uint8_t t446 = (uint8_t)(GM_LD(void *, (((uint8_t *)t443) + (uintptr_t)t445 * 8)) != 0);
                                                if (!t446) {
                                                    goto gm_lb64;
                                                }
                                                void * t447 = GM_LD(void *, (((uint8_t *)t443) + (uintptr_t)t445 * 8));
                                                gm_l61 = t447;
                                                void * t448 = (void *)((uint8_t *)&F.l57);
                                                void * t449 = gm_l61;
                                                gm_tftb1odc739t5 t450 = gm_f_push_d943vgp_2(t448, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t449)));
                                                gm_l85 = t450;
                                                uint8_t t451 = (uint8_t)!((((uint8_t *)&gm_l85))[0] != 0);
                                                if (!t451) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l85) + 1, gm_src7nt72u848oha, 290, 13);
                                                goto gm_lb60;
                                            }
gm_lb64:;
                                        }
gm_lb63:;
                                        {
                                            {
                                                void * t452 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                                uint64_t t453 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                                uint64_t t454 = gm_l60;
                                                gm_b16_8 t455 = GM_LD(gm_b16_8, (((uint8_t *)t452) + (uintptr_t)t454 * 40));
                                                gm_b16_8 t456;
                                                GM_ST(void *, (uint8_t *)&t456, (void *)(gm_sfdbfrv46lihd));
                                                GM_ST(uintptr_t, (uint8_t *)&t456 + 8, (uintptr_t)(1));
                                                uint8_t t457 = (gm_eq0iqiubfm7e(((uint8_t *)&t455), ((uint8_t *)&t456)));
                                                if (!t457) {
                                                    goto gm_lb66;
                                                }
                                                void * t458 = gm_l0;
                                                void * t459 = GM_LD(void *, ((uint8_t *)t458));
                                                gm_t88rcucg1b7uk t460 = gm_l1;
                                                gm_tkn42u7na3t81 t461 = gm_f_bits_struct_ty_l1kjtap(t459, GM_CONV(gm_csguankko4lmhd, t460));
                                                gm_l87 = t461;
                                                {
                                                    {
                                                        uint8_t t462 = (uint8_t)((((uint8_t *)&gm_l87))[4] != 0);
                                                        if (t462) {
                                                            goto gm_lb68;
                                                        }
                                                        gm_l86 = ((uint8_t)0);
                                                        goto gm_lb67;
                                                    }
gm_lb68:;
                                                    {
                                                        gm_l86 = ((uint8_t)1);
                                                        goto gm_lb67;
                                                    }
gm_lb69:;
                                                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 294, 38);
                                                }
gm_lb67:;
                                                uint8_t t463 = gm_l86;
                                                if (!t463) {
                                                    goto gm_lb66;
                                                }
                                                void * t464 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                                uint64_t t465 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                                uint64_t t466 = gm_l60;
                                                gm_t88rcucg1b7uk t467 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t464) + (uintptr_t)t466 * 40) + 16));
                                                gm_l62 = t467;
                                                {
                                                    void * t468 = (void *)((uint8_t *)&F.l57);
                                                    void * t469 = gm_l0;
                                                    void * t470 = GM_LD(void *, ((uint8_t *)t469));
                                                    void * t471 = (void *)(((uint8_t *)t470) + 8);
                                                    gm_t88rcucg1b7uk t472 = gm_l62;
                                                    gm_t53at80pfdus3 t473;
                                                    gm_f_kind_jcnt4hm((void *)&t473, t471, GM_CONV(gm_csguankko4lmhd, t472));
                                                    uint8_t t474 = GM_LD(uint8_t, (((uint8_t *)&t473) + 0));
                                                    uint8_t t475 = (uint8_t)((t474 == ((uint8_t)4ULL)));
                                                    gm_t53at80pfdus3 t476;
                                                    memset(&t476, 0, sizeof t476);
                                                    GM_ST(uint8_t, ((uint8_t *)&t476 + 0), ((uint8_t)4ULL));
                                                    if (t475) {
                                                        gm_t88rcucg1b7uk t477 = gm_l62;
                                                        gm_tl874t2atnlh3 t478 = gm_l3;
                                                        gm_to0f5qn75cgg8 t479;
                                                        memset(&t479, 0, sizeof t479);
                                                        GM_ST(uint8_t, ((uint8_t *)&t479 + 0), ((uint8_t)4ULL));
                                                        GM_ST(uint8_t, ((uint8_t *)&t479 + 8), ((uint8_t)0));
                                                        void * t480 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t477, &t479, t478);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        memmove(((uint8_t *)&gm_l88), ((uint8_t *)t480), 88);
                                                    } else {
                                                        gm_t88rcucg1b7uk t481 = gm_l62;
                                                        gm_tbci5ospk5nnn t482;
                                                        gm_f_zero_s25l990((void *)&t482, (void *)NULL, cx, NULL);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_tl874t2atnlh3 t483 = gm_l3;
                                                        gm_to0f5qn75cgg8 t484;
                                                        GM_ST(uint8_t, (uint8_t *)&t484 + 1, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t484 + 2, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&t484 + 4, 0);
                                                        GM_ST(uint64_t, (uint8_t *)&t484 + 56, 0);
                                                        GM_ST(uint8_t, ((uint8_t *)&t484 + 0), ((uint8_t)0ULL));
                                                        GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t484 + 8), t482);
                                                        void * t485 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t481, &t484, t483);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        memmove(((uint8_t *)&gm_l88), ((uint8_t *)t485), 88);
                                                    }
                                                    gm_tftb1odc739t5 t486 = gm_f_push_d943vgp_2(t468, &(*(gm_tsf6i2agfnpva *)((uint8_t *)&gm_l88)));
                                                    gm_l89 = t486;
                                                }
                                                uint8_t t487 = (uint8_t)!((((uint8_t *)&gm_l89))[0] != 0);
                                                if (!t487) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l89) + 1, gm_src7nt72u848oha, 296, 13);
                                                goto gm_lb60;
                                            }
gm_lb66:;
                                        }
gm_lb65:;
                                        {
                                            {
                                                void * t488 = gm_l0;
                                                uint32_t t489 = gm_l4;
                                                uint64_t t490 = gm_l60;
                                                void * t491 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                                uint64_t t492 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                                uint64_t t493 = gm_l60;
                                                gm_t88rcucg1b7uk t494 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t491) + (uintptr_t)t493 * 40) + 16));
                                                void * t495 = gm_f_field_default_4ssmakm((void *)NULL, cx, NULL, t488, t489, t490, t494);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_l90 = t495;
                                                uint8_t t496 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l90)) != 0);
                                                if (!t496) {
                                                    goto gm_lb71;
                                                }
                                                void * t497 = GM_LD(void *, ((uint8_t *)&gm_l90));
                                                gm_l63 = t497;
                                                void * t498 = (void *)((uint8_t *)&F.l57);
                                                void * t499 = gm_l63;
                                                gm_tftb1odc739t5 t500 = gm_f_push_d943vgp_2(t498, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t499)));
                                                gm_l91 = t500;
                                                uint8_t t501 = (uint8_t)!((((uint8_t *)&gm_l91))[0] != 0);
                                                if (!t501) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l91) + 1, gm_src7nt72u848oha, 300, 13);
                                                goto gm_lb70;
                                            }
gm_lb71:;
                                            uint8_t t502 = gm_l15;
                                            uint8_t t503 = (uint8_t)!t502;
                                            if (t503) {
                                                void * t504 = gm_l0;
                                                gm_tl874t2atnlh3 t505 = gm_l3;
                                                void * t506 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                                uint64_t t507 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                                uint64_t t508 = gm_l60;
                                                gm_b16_8 t509 = GM_LD(gm_b16_8, (((uint8_t *)t506) + (uintptr_t)t508 * 40));
                                                gm_tnpifv20irnph t510;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t510 + 0), t509);
                                                gm_f_format_843cvhd((void *)&gm_l92, (void *)NULL, cx, NULL, t510);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t511 = (uint8_t)!((((uint8_t *)&gm_l92))[0] != 0);
                                                if (!t511) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l92) + 8, gm_src7nt72u848oha, 302, 46);
                                                gm_b16_8 t512 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l92) + 8));
                                                gm_b16_8 t513 = GM_LD(gm_b16_8, ((uint8_t *)&t512));
                                                gm_b0_1 t514 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t504, t505, t513);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                            }
                                            void * t515 = (void *)((uint8_t *)&F.l57);
                                            void * t516 = gm_l0;
                                            void * t517 = GM_LD(void *, ((uint8_t *)t516));
                                            gm_tl874t2atnlh3 t518 = gm_l3;
                                            void * t519 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t517, t518);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_tftb1odc739t5 t520 = gm_f_push_d943vgp_2(t515, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t519)));
                                            gm_l93 = t520;
                                            uint8_t t521 = (uint8_t)!((((uint8_t *)&gm_l93))[0] != 0);
                                            if (!t521) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l93) + 1, gm_src7nt72u848oha, 303, 13);
                                        }
gm_lb70:;
                                        goto gm_lb61;
                                    }
gm_lb62:;
                                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 288, 5);
                                }
gm_lb61:;
                            }
gm_lb60:;
                            uint64_t t522 = gm_l59;
                            uint64_t r523; bool o524 = __builtin_add_overflow(t522, ((uint64_t)1ULL), &r523);
                            uint64_t t525 = r523;
                            gm_l59 = t525;
                        }
                    }
gm_lb58:;
                }
                {
                    gm_t88rcucg1b7uk t526 = gm_l1;
                    gm_b16_8 t527 = GM_LD(gm_b16_8, ((uint8_t *)&F.l57));
                    gm_tl874t2atnlh3 t528 = gm_l3;
                    gm_to0f5qn75cgg8 t529;
                    memset(&t529, 0, sizeof t529);
                    GM_ST(uint8_t, ((uint8_t *)&t529 + 0), ((uint8_t)35ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t529 + 8), t527);
                    void * t530 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t526, &t529, t528);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l64 = t530;
                    uint64_t t531 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                    uint8_t t532 = ((t531 == ((uint64_t)0ULL)));
                    if (t532) {
                        void * t533 = gm_l64;
                        F.ret = t533;
                        goto gm_fret;
                    }
                    gm_t88rcucg1b7uk t534 = gm_l1;
                    gm_b16_8 t535 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
                    void * t536 = gm_l64;
                    gm_tl874t2atnlh3 t537 = gm_l3;
                    void * t538 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t534, t535, t536, t537);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t538;
                    goto gm_fret;
                }
            }
gm_lb2:;
        }
        {
            void * t539 = gm_l0;
            void * t540 = GM_LD(void *, ((uint8_t *)t539));
            gm_tl874t2atnlh3 t541 = gm_l3;
            void * t542 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t540, t541);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t542;
            goto gm_fret;
        }
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
__attribute__((aligned(4))) void * gm_f_construct_tuple_6ee5ejf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_construct_tuple_6ee5ejf F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* elems */
    gm_b16_8 gm_l3; /* names */
    gm_b16_8 gm_l4; /* args */
    gm_tl874t2atnlh3 gm_l5; /* span */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* next */
    uint8_t gm_l10; /* spreading */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint8_t gm_l15; /* spread_failed */
    void * gm_l17; /* v */
    gm_t88rcucg1b7uk gm_l18; /* vt */
    gm_tnnjeerolos0a gm_l19; /* kind */
    gm_th2lqbo77pvsc gm_l20; /* what */
    gm_b16_8 gm_l21; /* w */
    gm_b16_8 gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    gm_t08olq97rscn3 gm_l27; /* n */
    uint64_t gm_l29; /* i */
    gm_b16_8 gm_l31; /* ps */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    gm_tpouv9r7lrri2 gm_l34; /* p */
    gm_b16_8 gm_l35; /* <tmp> */
    uint64_t gm_l36; /* <tmp> */
    uint64_t gm_l38; /* i */
    gm_b16_8 gm_l39; /* n */
    void * gm_l40; /* v */
    uint64_t gm_l42; /* <tmp> */
    uint64_t gm_l43; /* <tmp> */
    uint64_t gm_l44; /* i */
    void * gm_l45; /* v */
    void * gm_l46; /* agg */
    GM_STACK_CHECK(&F, 3705u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_tftb1odc739t5 gm_l47;
    uint8_t gm_l48;
    gm_t53at80pfdus3 gm_l49;
    gm_th2lqbo77pvsc gm_l50;
    gm_tabb710d9mq2e gm_l51;
    gm_tabb710d9mq2e gm_l52;
    gm_tftb1odc739t5 gm_l53;
    gm_tf5srh4mp4b3i gm_l54;
    gm_tabb710d9mq2e gm_l55;
    gm_tftb1odc739t5 gm_l56;
    gm_th2lqbo77pvsc gm_l57;
    gm_tftb1odc739t5 gm_l58;
    uint64_t gm_l59;
    gm_tf5srh4mp4b3i gm_l60;
    gm_tabb710d9mq2e gm_l61;
    gm_tabb710d9mq2e gm_l62;
    uint8_t gm_l63;
    gm_tabb710d9mq2e gm_l64;
    void * gm_l65;
    gm_tftb1odc739t5 gm_l66;
    gm_tabb710d9mq2e gm_l67;
    gm_tftb1odc739t5 gm_l68;
    {
        gm_trqfvmjtt4sij t54;
        gm_f_list_pmp06kq((void *)&t54, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l6 = t54;
        gm_b16_8 t55 = gm_l2;
        gm_l7 = t55;
        gm_l8 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t56 = gm_l8;
                uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint8_t t58 = (t56 < t57);
                if (!t58) {
                    goto gm_lb1;
                }
                {
                    void * t59 = GM_LD(void *, ((uint8_t *)&gm_l7));
                    uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    uint64_t t61 = gm_l8;
                    {
                        {
                            void * t62 = (void *)((uint8_t *)&F.l6);
                            gm_tftb1odc739t5 t63 = gm_f_push_r2k6rjf(t62, ((void *)0));
                            gm_l47 = t63;
                            uint8_t t64 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
                            if (!t64) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l47) + 1, gm_src7nt72u848oha, 84, 22);
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 84, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t65 = gm_l8;
                uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)1ULL), &r66);
                uint64_t t68 = r66;
                gm_l8 = t68;
            }
        }
gm_lb1:;
        gm_l9 = ((uint64_t)0ULL);
        gm_l10 = ((uint8_t)0);
        gm_b16_8 t69 = gm_l4;
        gm_l11 = t69;
        gm_l12 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t70 = gm_l12;
                uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                uint8_t t72 = (t70 < t71);
                if (!t72) {
                    goto gm_lb6;
                }
                {
                    void * t73 = GM_LD(void *, ((uint8_t *)&gm_l11));
                    uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                    uint64_t t75 = gm_l12;
                    {
                        {
                            memmove(((uint8_t *)&F.l13), (((uint8_t *)t73) + (uintptr_t)t75 * 144), 144);
                            {
                                {
                                    uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&F.l13) + 0));
                                    uint8_t t77 = (uint8_t)(t76 == ((uint8_t)2ULL));
                                    if (!t77) {
                                        goto gm_lb12;
                                    }
                                    gm_l48 = ((uint8_t)1);
                                    goto gm_lb11;
                                }
gm_lb12:;
                                {
                                    gm_l48 = ((uint8_t)0);
                                    goto gm_lb11;
                                }
gm_lb13:;
                                gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 89, 12);
                            }
gm_lb11:;
                            uint8_t t78 = gm_l48;
                            if (t78) {
                                gm_l10 = ((uint8_t)1);
                            }
                            goto gm_lb9;
                        }
gm_lb10:;
                        gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 88, 5);
                    }
gm_lb9:;
                }
gm_lb8:;
                uint64_t t79 = gm_l12;
                uint64_t r80; bool o81 = __builtin_add_overflow(t79, ((uint64_t)1ULL), &r80);
                uint64_t t82 = r80;
                gm_l12 = t82;
            }
        }
gm_lb6:;
    }
    {
        gm_trqfvmjtt4sij t83;
        gm_f_list_ir0h3tc_2((void *)&t83, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l14 = t83;
        gm_l15 = ((uint8_t)0);
        {
            {
                uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint8_t t85 = ((t84 == ((uint64_t)1ULL)));
                if (!t85) {
                    goto gm_lb15;
                }
                uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint8_t t87 = (!(t86 == ((uint64_t)1ULL)));
                if (!t87) {
                    goto gm_lb15;
                }
                {
                    void * t88 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t89)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t89, gm_src7nt72u848oha, 96, 67);
                    uint8_t t90 = GM_LD(uint8_t, ((((uint8_t *)t88) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
                    uint8_t t91 = (uint8_t)(t90 == ((uint8_t)0ULL));
                    if (!t91) {
                        goto gm_lb15;
                    }
                    memmove(((uint8_t *)&F.l16), ((((uint8_t *)t88) + (uintptr_t)((uint64_t)0ULL) * 144) + 8), 88);
                }
                void * t92 = gm_l0;
                void * t93 = (void *)((uint8_t *)&F.l16);
                gm_tkn42u7na3t81 t94;
                memset(&t94, 0, sizeof t94);
                void * t95 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t92, t93, t94);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l17 = t95;
                void * t96 = gm_l0;
                void * t97 = (void *)(((uint8_t *)t96) + 96);
                void * t98 = gm_l0;
                void * t99 = GM_LD(void *, ((uint8_t *)t98));
                void * t100 = gm_l17;
                gm_t88rcucg1b7uk t101 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t100));
                gm_t88rcucg1b7uk t102 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t97, t99, t101);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l18 = t102;
                {
                    void * t103 = gm_l0;
                    void * t104 = GM_LD(void *, ((uint8_t *)t103));
                    void * t105 = (void *)(((uint8_t *)t104) + 8);
                    gm_t88rcucg1b7uk t106 = gm_l18;
                    gm_f_kind_jcnt4hm((void *)&gm_l49, t105, GM_CONV(gm_csguankko4lmhd, t106));
                }
                {
                    {
                        uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&gm_l49) + 0));
                        uint8_t t108 = (uint8_t)(t107 == ((uint8_t)12ULL));
                        if (!t108) {
                            goto gm_lb17;
                        }
                        gm_b16_8 t109;
                        GM_ST(void *, (uint8_t *)&t109, (void *)(gm_sto8li44rv2ou));
                        GM_ST(uintptr_t, (uint8_t *)&t109 + 8, (uintptr_t)(7));
                        GM_ST(uint8_t, (uint8_t *)&gm_l20 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&gm_l20 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&gm_l20 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&gm_l20), t109);
                        ((uint8_t *)&gm_l20)[16] = 1;
                        goto gm_lb16;
                    }
gm_lb17:;
                    {
                        uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&gm_l49) + 0));
                        uint8_t t111 = (uint8_t)(t110 == ((uint8_t)11ULL));
                        if (!t111) {
                            goto gm_lb18;
                        }
                        gm_tnnjeerolos0a t112 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&gm_l49) + 8) + 12));
                        gm_l19 = t112;
                        gm_tnnjeerolos0a t113 = gm_l19;
                        gm_tnnjeerolos0a t114;
                        GM_ST(uint8_t, (uint8_t *)&t114 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t114 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t114 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t114 + 0), ((uint8_t)0ULL));
                        gm_tnnjeerolos0a t115;
                        GM_ST(uint8_t, (uint8_t *)&t115 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t115 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t115 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t115 + 0), ((uint8_t)2ULL));
                        gm_tcf2f71ra2nps t116;
                        GM_ST(gm_tnnjeerolos0a, ((uint8_t *)&t116 + 0), t114);
                        GM_ST(gm_tnnjeerolos0a, ((uint8_t *)&t116 + 8), t115);
                        uint8_t t117 = 0;
                        if (gm_eqb2ul45tidi(((uint8_t *)&t116) + 0, ((uint8_t *)&t113))) t117 = 1;
                        if (gm_eqb2ul45tidi(((uint8_t *)&t116) + 8, ((uint8_t *)&t113))) t117 = 1;
                        uint8_t t118 = (uint8_t)(t117);
                        if (t118) {
                            gm_b16_8 t119;
                            GM_ST(void *, (uint8_t *)&t119, (void *)(gm_srrqh3pev0igg));
                            GM_ST(uintptr_t, (uint8_t *)&t119 + 8, (uintptr_t)(8));
                            GM_ST(uint8_t, (uint8_t *)&gm_l50 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&gm_l50 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&gm_l50 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&gm_l50), t119);
                            ((uint8_t *)&gm_l50)[16] = 1;
                        } else {
                            memset(&gm_l50, 0, sizeof gm_l50);
                        }
                        memmove(((uint8_t *)&gm_l20), ((uint8_t *)&gm_l50), 24);
                        goto gm_lb16;
                    }
gm_lb18:;
                    {
                        memset(&gm_l20, 0, sizeof gm_l20);
                        goto gm_lb16;
                    }
gm_lb19:;
                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 99, 25);
                }
gm_lb16:;
                {
                    {
                        uint8_t t120 = (uint8_t)((((uint8_t *)&gm_l20))[16] != 0);
                        if (!t120) {
                            goto gm_lb21;
                        }
                        gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l20));
                        gm_l21 = t121;
                        {
                            void * t122 = gm_l0;
                            gm_tl874t2atnlh3 t123 = gm_l5;
                            void * t124 = gm_l0;
                            gm_t88rcucg1b7uk t125 = gm_l1;
                            gm_b16_8 t126 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t124, t125);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_b16_8 t127 = gm_l21;
                            void * t128 = gm_l0;
                            gm_t88rcucg1b7uk t129 = gm_l1;
                            gm_b16_8 t130 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t128, t129);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tfeab279c16rr t131;
                            GM_ST(gm_b16_8, ((uint8_t *)&t131 + 0), t126);
                            GM_ST(gm_b16_8, ((uint8_t *)&t131 + 16), t127);
                            GM_ST(gm_b16_8, ((uint8_t *)&t131 + 32), t130);
                            gm_f_format_6nmb3uf((void *)&gm_l51, (void *)NULL, cx, NULL, &t131);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t132 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
                            if (!t132) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l51) + 8, gm_src7nt72u848oha, 105, 28);
                            gm_b16_8 t133 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l51) + 8));
                            gm_b16_8 t134 = GM_LD(gm_b16_8, ((uint8_t *)&t133));
                            gm_b0_1 t135 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t122, t123, t134);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        void * t136 = gm_l0;
                        void * t137 = GM_LD(void *, ((uint8_t *)t136));
                        gm_tl874t2atnlh3 t138 = gm_l5;
                        void * t139 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t137, t138);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t139;
                        goto gm_fret;
                    }
gm_lb21:;
                }
gm_lb20:;
                uint64_t t140 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint8_t t141 = (t140 > ((uint64_t)0ULL));
                if (t141) {
                    void * t142 = GM_LD(void *, ((uint8_t *)&F.l6));
                    uint64_t t143 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t143)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t143, gm_src7nt72u848oha, 109, 13);
                    void * t144 = gm_l0;
                    void * t145 = gm_l17;
                    void * t146 = GM_LD(void *, ((uint8_t *)&gm_l2));
                    uint64_t t147 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                    gm_t88rcucg1b7uk t148 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t146) + (uintptr_t)((uint64_t)0ULL) * 4));
                    void * t149 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t144, t145, t148);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(void *, (((uint8_t *)t142) + (uintptr_t)((uint64_t)0ULL) * 8), t149);
                    gm_l9 = ((uint64_t)1ULL);
                }
                goto gm_lb14;
            }
gm_lb15:;
        }
gm_lb14:;
    }
    {
        void * t150 = GM_LD(void *, ((uint8_t *)&gm_l4));
        uint64_t t151 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
        uint64_t t152 = gm_l9;
        uint64_t t153 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
        uint64_t t154 = (t152 < t153 ? t152 : t153);
        uint64_t t155 = (uintptr_t)t154;
        uint64_t t156 = (uintptr_t)t151;
        if (t155 > t156 || t156 > (uintptr_t)t151) gm_trap_slice(t155, t156, t151, gm_src7nt72u848oha, 113, 16);
        uint64_t t157 = ((uintptr_t)t151) - (uintptr_t)t154;
        gm_b16_8 t158;
        GM_ST(void *, (uint8_t *)&t158, (void *)(((uint8_t *)t150 + (uintptr_t)t154 * 144)));
        GM_ST(uintptr_t, (uint8_t *)&t158 + 8, (uintptr_t)(t157));
        gm_l22 = t158;
        gm_l23 = ((uint64_t)0ULL);
        {
            for (;;) {
                {
                    uint64_t t159 = gm_l23;
                    uint64_t t160 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                    uint8_t t161 = (t159 < t160);
                    if (!t161) {
                        goto gm_lb22;
                    }
                    {
                        {
                            void * t162 = GM_LD(void *, ((uint8_t *)&gm_l22));
                            uint64_t t163 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                            uint64_t t164 = gm_l23;
                            {
                                {
                                    {
                                        {
                                            memmove(((uint8_t *)&F.l24), (((uint8_t *)t162) + (uintptr_t)t164 * 144), 144);
                                            gm_trqfvmjtt4sij t165;
                                            gm_f_list_5kthfl8((void *)&t165, (void *)NULL, cx, NULL);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            F.l25 = t165;
                                            {
                                                {
                                                    uint8_t t166 = GM_LD(uint8_t, (((uint8_t *)&F.l24) + 0));
                                                    uint8_t t167 = (uint8_t)(t166 == ((uint8_t)0ULL));
                                                    if (!t167) {
                                                        goto gm_lb28;
                                                    }
                                                    {
                                                        memmove(((uint8_t *)&F.l26), (((uint8_t *)&F.l24) + 8), 88);
                                                    }
                                                    uint64_t t168 = gm_l9;
                                                    uint64_t t169 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                                    uint8_t t170 = (t168 >= t169);
                                                    if (t170) {
                                                        void * t171 = gm_l0;
                                                        gm_tl874t2atnlh3 t172 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l26) + 72));
                                                        void * t173 = gm_l0;
                                                        gm_t88rcucg1b7uk t174 = gm_l1;
                                                        gm_b16_8 t175 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t173, t174);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_tnpifv20irnph t176;
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t176 + 0), t175);
                                                        gm_f_format_bm4rhr0((void *)&gm_l52, (void *)NULL, cx, NULL, t176);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        uint8_t t177 = (uint8_t)!((((uint8_t *)&gm_l52))[0] != 0);
                                                        if (!t177) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l52) + 8, gm_src7nt72u848oha, 118, 38);
                                                        gm_b16_8 t178 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l52) + 8));
                                                        gm_b16_8 t179 = GM_LD(gm_b16_8, ((uint8_t *)&t178));
                                                        gm_b0_1 t180 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t171, t172, t179);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        goto gm_lb24;
                                                    }
                                                    {
                                                        void * t181 = (void *)((uint8_t *)&F.l25);
                                                        gm_th2lqbo77pvsc t182;
                                                        memset(&t182, 0, sizeof t182);
                                                        void * t183 = gm_l0;
                                                        void * t184 = (void *)((uint8_t *)&F.l26);
                                                        void * t185 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                                        uint64_t t186 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                                        uint64_t t187 = gm_l9;
                                                        gm_t88rcucg1b7uk t188 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t185) + (uintptr_t)t187 * 4));
                                                        void * t189 = gm_f_field_value_19vleud((void *)NULL, cx, NULL, t183, t184, t188);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_tl874t2atnlh3 t190 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l26) + 72));
                                                        gm_tm7a6rksvs6n5 t191;
                                                        GM_ST(uint32_t, (uint8_t *)&t191 + 44, 0);
                                                        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t191 + 0), t182);
                                                        GM_ST(void *, ((uint8_t *)&t191 + 24), t189);
                                                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t191 + 32), t190);
                                                        gm_tftb1odc739t5 t192 = gm_f_push_9a0n4ld(t181, &t191);
                                                        gm_l53 = t192;
                                                    }
                                                    uint8_t t193 = (uint8_t)!((((uint8_t *)&gm_l53))[0] != 0);
                                                    if (!t193) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l53) + 1, gm_src7nt72u848oha, 121, 17);
                                                    goto gm_lb27;
                                                }
gm_lb28:;
                                                {
                                                    uint8_t t194 = GM_LD(uint8_t, (((uint8_t *)&F.l24) + 0));
                                                    uint8_t t195 = (uint8_t)(t194 == ((uint8_t)1ULL));
                                                    if (!t195) {
                                                        goto gm_lb29;
                                                    }
                                                    {
                                                        memmove(((uint8_t *)&gm_l27), (((uint8_t *)&F.l24) + 8), 32);
                                                    }
                                                    {
                                                        memmove(((uint8_t *)&F.l28), ((((uint8_t *)&F.l24) + 8) + 32), 88);
                                                    }
                                                    gm_b16_8 t196 = gm_l3;
                                                    gm_b16_8 t197 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l27));
                                                    gm_tf5srh4mp4b3i t198 = gm_f_index_of_o9r57ha(t196, t197);
                                                    gm_l54 = t198;
                                                    uint8_t t199 = (uint8_t)((((uint8_t *)&gm_l54))[8] != 0);
                                                    if (t199) {
                                                        uint64_t t200 = GM_LD(uint64_t, ((uint8_t *)&gm_l54));
                                                        gm_l29 = t200;
                                                    } else {
                                                        {
                                                            void * t201 = gm_l0;
                                                            gm_tl874t2atnlh3 t202 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l27) + 16));
                                                            void * t203 = gm_l0;
                                                            gm_t88rcucg1b7uk t204 = gm_l1;
                                                            gm_b16_8 t205 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t203, t204);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            gm_b16_8 t206 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l27));
                                                            gm_t6j6smj7qoctu t207;
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t207 + 0), t205);
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t207 + 16), t206);
                                                            gm_f_format_3tnlp8q((void *)&gm_l55, (void *)NULL, cx, NULL, &t207);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            uint8_t t208 = (uint8_t)!((((uint8_t *)&gm_l55))[0] != 0);
                                                            if (!t208) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l55) + 8, gm_src7nt72u848oha, 125, 38);
                                                            gm_b16_8 t209 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l55) + 8));
                                                            gm_b16_8 t210 = GM_LD(gm_b16_8, ((uint8_t *)&t209));
                                                            gm_b0_1 t211 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t201, t202, t210);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                        }
                                                        goto gm_lb24;
                                                    }
                                                    {
                                                        void * t212 = (void *)((uint8_t *)&F.l25);
                                                        gm_b16_8 t213 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l27));
                                                        gm_th2lqbo77pvsc t214;
                                                        GM_ST(uint8_t, (uint8_t *)&t214 + 17, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t214 + 18, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&t214 + 20, 0);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t214), t213);
                                                        ((uint8_t *)&t214)[16] = 1;
                                                        void * t215 = gm_l0;
                                                        void * t216 = (void *)((uint8_t *)&F.l28);
                                                        void * t217 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                                        uint64_t t218 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                                        uint64_t t219 = gm_l29;
                                                        if ((uintptr_t)t219 >= (uintptr_t)(t218)) gm_trap_index((uintptr_t)t219, t218, gm_src7nt72u848oha, 128, 78);
                                                        gm_t88rcucg1b7uk t220 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t217) + (uintptr_t)t219 * 4));
                                                        void * t221 = gm_f_field_value_19vleud((void *)NULL, cx, NULL, t215, t216, t220);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_tl874t2atnlh3 t222 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l27) + 16));
                                                        gm_tm7a6rksvs6n5 t223;
                                                        GM_ST(uint32_t, (uint8_t *)&t223 + 44, 0);
                                                        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t223 + 0), t214);
                                                        GM_ST(void *, ((uint8_t *)&t223 + 24), t221);
                                                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t223 + 32), t222);
                                                        gm_tftb1odc739t5 t224 = gm_f_push_9a0n4ld(t212, &t223);
                                                        gm_l56 = t224;
                                                    }
                                                    uint8_t t225 = (uint8_t)!((((uint8_t *)&gm_l56))[0] != 0);
                                                    if (!t225) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l56) + 1, gm_src7nt72u848oha, 128, 17);
                                                    goto gm_lb27;
                                                }
gm_lb29:;
                                                {
                                                    uint8_t t226 = GM_LD(uint8_t, (((uint8_t *)&F.l24) + 0));
                                                    uint8_t t227 = (uint8_t)(t226 == ((uint8_t)2ULL));
                                                    if (!t227) {
                                                        goto gm_lb30;
                                                    }
                                                    {
                                                        memmove(((uint8_t *)&F.l30), (((uint8_t *)&F.l24) + 8), 88);
                                                    }
                                                    {
                                                        void * t228 = gm_l0;
                                                        void * t229 = (void *)((uint8_t *)&F.l30);
                                                        void * t230 = (void *)((uint8_t *)&F.l14);
                                                        gm_f_spread_parts_e62q2l2((void *)&gm_l57, (void *)NULL, cx, NULL, t228, t229, t230);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                    }
                                                    uint8_t t231 = (uint8_t)((((uint8_t *)&gm_l57))[16] != 0);
                                                    if (t231) {
                                                        gm_b16_8 t232 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l57));
                                                        gm_l31 = t232;
                                                    } else {
                                                        gm_l15 = ((uint8_t)1);
                                                        goto gm_lb24;
                                                    }
                                                    gm_b16_8 t233 = gm_l31;
                                                    gm_l32 = t233;
                                                    gm_l33 = ((uint64_t)0ULL);
                                                    {
                                                        for (;;) {
                                                            uint64_t t234 = gm_l33;
                                                            uint64_t t235 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                                                            uint8_t t236 = (t234 < t235);
                                                            if (!t236) {
                                                                goto gm_lb31;
                                                            }
                                                            {
                                                                void * t237 = GM_LD(void *, ((uint8_t *)&gm_l32));
                                                                uint64_t t238 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                                                                uint64_t t239 = gm_l33;
                                                                {
                                                                    {
                                                                        {
                                                                            memmove(((uint8_t *)&gm_l34), (((uint8_t *)t237) + (uintptr_t)t239 * 32), 32);
                                                                        }
                                                                        {
                                                                            void * t240 = (void *)((uint8_t *)&F.l25);
                                                                            void * t241 = GM_LD(void *, (((uint8_t *)&gm_l34) + 24));
                                                                            gm_tl874t2atnlh3 t242 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l30) + 72));
                                                                            gm_th2lqbo77pvsc t243 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l34));
                                                                            gm_tm7a6rksvs6n5 t244;
                                                                            GM_ST(uint32_t, (uint8_t *)&t244 + 44, 0);
                                                                            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t244 + 0), t243);
                                                                            GM_ST(void *, ((uint8_t *)&t244 + 24), t241);
                                                                            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t244 + 32), t242);
                                                                            gm_tftb1odc739t5 t245 = gm_f_push_9a0n4ld(t240, &t244);
                                                                            gm_l58 = t245;
                                                                        }
                                                                        uint8_t t246 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
                                                                        if (!t246) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l58) + 1, gm_src7nt72u848oha, 135, 31);
                                                                        goto gm_lb34;
                                                                    }
gm_lb35:;
                                                                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 135, 17);
                                                                }
gm_lb34:;
                                                            }
gm_lb33:;
                                                            uint64_t t247 = gm_l33;
                                                            uint64_t r248; bool o249 = __builtin_add_overflow(t247, ((uint64_t)1ULL), &r248);
                                                            uint64_t t250 = r248;
                                                            gm_l33 = t250;
                                                        }
                                                    }
gm_lb31:;
                                                    goto gm_lb27;
                                                }
gm_lb30:;
                                                gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 115, 9);
                                            }
gm_lb27:;
                                        }
                                        {
                                            gm_b16_8 t251 = GM_LD(gm_b16_8, ((uint8_t *)&F.l25));
                                            gm_l35 = t251;
                                            gm_l36 = ((uint64_t)0ULL);
                                            {
                                                for (;;) {
                                                    uint64_t t252 = gm_l36;
                                                    uint64_t t253 = GM_LD(uint64_t, (((uint8_t *)&gm_l35) + 8));
                                                    uint8_t t254 = (t252 < t253);
                                                    if (!t254) {
                                                        goto gm_lb36;
                                                    }
                                                    {
                                                        void * t255 = GM_LD(void *, ((uint8_t *)&gm_l35));
                                                        uint64_t t256 = GM_LD(uint64_t, (((uint8_t *)&gm_l35) + 8));
                                                        uint64_t t257 = gm_l36;
                                                        {
                                                            {
                                                                memmove(((uint8_t *)&F.l37), (((uint8_t *)t255) + (uintptr_t)t257 * 48), 48);
                                                                gm_l38 = ((uint64_t)0ULL);
                                                                {
                                                                    {
                                                                        uint8_t t258 = (uint8_t)((((uint8_t *)&F.l37))[16] != 0);
                                                                        if (!t258) {
                                                                            goto gm_lb42;
                                                                        }
                                                                        gm_b16_8 t259 = GM_LD(gm_b16_8, ((uint8_t *)&F.l37));
                                                                        gm_l39 = t259;
                                                                        gm_b16_8 t260 = gm_l3;
                                                                        gm_b16_8 t261 = gm_l39;
                                                                        gm_tf5srh4mp4b3i t262 = gm_f_index_of_o9r57ha(t260, t261);
                                                                        gm_l60 = t262;
                                                                        uint8_t t263 = (uint8_t)((((uint8_t *)&gm_l60))[8] != 0);
                                                                        if (t263) {
                                                                            uint64_t t264 = GM_LD(uint64_t, ((uint8_t *)&gm_l60));
                                                                            gm_l59 = t264;
                                                                        } else {
                                                                            {
                                                                                void * t265 = gm_l0;
                                                                                gm_tl874t2atnlh3 t266 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l37) + 32));
                                                                                void * t267 = gm_l0;
                                                                                gm_t88rcucg1b7uk t268 = gm_l1;
                                                                                gm_b16_8 t269 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t267, t268);
                                                                                if (gm_unw.kind) {
                                                                                    goto gm_lad;
                                                                                }
                                                                                gm_b16_8 t270 = gm_l39;
                                                                                gm_t6j6smj7qoctu t271;
                                                                                GM_ST(gm_b16_8, ((uint8_t *)&t271 + 0), t269);
                                                                                GM_ST(gm_b16_8, ((uint8_t *)&t271 + 16), t270);
                                                                                gm_f_format_3tnlp8q((void *)&gm_l61, (void *)NULL, cx, NULL, &t271);
                                                                                if (gm_unw.kind) {
                                                                                    goto gm_lad;
                                                                                }
                                                                                uint8_t t272 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
                                                                                if (!t272) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l61) + 8, gm_src7nt72u848oha, 142, 39);
                                                                                gm_b16_8 t273 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l61) + 8));
                                                                                gm_b16_8 t274 = GM_LD(gm_b16_8, ((uint8_t *)&t273));
                                                                                gm_b0_1 t275 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t265, t266, t274);
                                                                                if (gm_unw.kind) {
                                                                                    goto gm_lad;
                                                                                }
                                                                            }
                                                                            goto gm_lb38;
                                                                        }
                                                                        uint64_t t276 = gm_l59;
                                                                        gm_l38 = t276;
                                                                        goto gm_lb41;
                                                                    }
gm_lb42:;
                                                                    uint64_t t277 = gm_l9;
                                                                    uint64_t t278 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                                                    uint8_t t279 = (t277 >= t278);
                                                                    if (t279) {
                                                                        void * t280 = gm_l0;
                                                                        gm_tl874t2atnlh3 t281 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l37) + 32));
                                                                        void * t282 = gm_l0;
                                                                        gm_t88rcucg1b7uk t283 = gm_l1;
                                                                        gm_b16_8 t284 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t282, t283);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        gm_tnpifv20irnph t285;
                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t285 + 0), t284);
                                                                        gm_f_format_bm4rhr0((void *)&gm_l62, (void *)NULL, cx, NULL, t285);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        uint8_t t286 = (uint8_t)!((((uint8_t *)&gm_l62))[0] != 0);
                                                                        if (!t286) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l62) + 8, gm_src7nt72u848oha, 147, 39);
                                                                        gm_b16_8 t287 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l62) + 8));
                                                                        gm_b16_8 t288 = GM_LD(gm_b16_8, ((uint8_t *)&t287));
                                                                        gm_b0_1 t289 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t280, t281, t288);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        goto gm_lb36;
                                                                    }
                                                                    uint64_t t290 = gm_l9;
                                                                    gm_l38 = t290;
                                                                    uint64_t t291 = gm_l9;
                                                                    uint64_t r292; bool o293 = __builtin_add_overflow(t291, ((uint64_t)1ULL), &r292);
                                                                    if (o293) gm_trap("integer overflow in `+`", gm_src7nt72u848oha, 151, 17);
                                                                    uint64_t t294 = r292;
                                                                    gm_l9 = t294;
                                                                }
gm_lb41:;
                                                                void * t295 = GM_LD(void *, ((uint8_t *)&F.l6));
                                                                uint64_t t296 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
                                                                uint64_t t297 = gm_l38;
                                                                if ((uintptr_t)t297 >= (uintptr_t)(t296)) gm_trap_index((uintptr_t)t297, t296, gm_src7nt72u848oha, 153, 16);
                                                                {
                                                                    {
                                                                        uint8_t t298 = (uint8_t)(GM_LD(void *, (((uint8_t *)t295) + (uintptr_t)t297 * 8)) != 0);
                                                                        if (t298) {
                                                                            goto gm_lb44;
                                                                        }
                                                                        gm_l63 = ((uint8_t)0);
                                                                        goto gm_lb43;
                                                                    }
gm_lb44:;
                                                                    {
                                                                        gm_l63 = ((uint8_t)1);
                                                                        goto gm_lb43;
                                                                    }
gm_lb45:;
                                                                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 153, 16);
                                                                }
gm_lb43:;
                                                                uint8_t t299 = gm_l63;
                                                                if (t299) {
                                                                    void * t300 = gm_l0;
                                                                    gm_tl874t2atnlh3 t301 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l37) + 32));
                                                                    uint64_t t302 = gm_l38;
                                                                    void * t303 = gm_l0;
                                                                    gm_t88rcucg1b7uk t304 = gm_l1;
                                                                    gm_b16_8 t305 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t303, t304);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    gm_tf1bjg81u0glv t306;
                                                                    GM_ST(uint64_t, ((uint8_t *)&t306 + 0), t302);
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t306 + 8), t305);
                                                                    gm_f_format_t8pf89n((void *)&gm_l64, (void *)NULL, cx, NULL, &t306);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    uint8_t t307 = (uint8_t)!((((uint8_t *)&gm_l64))[0] != 0);
                                                                    if (!t307) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l64) + 8, gm_src7nt72u848oha, 153, 63);
                                                                    gm_b16_8 t308 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l64) + 8));
                                                                    gm_b16_8 t309 = GM_LD(gm_b16_8, ((uint8_t *)&t308));
                                                                    gm_b0_1 t310 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t300, t301, t309);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                }
                                                                void * t311 = gm_l0;
                                                                void * t312 = GM_LD(void *, (((uint8_t *)&F.l37) + 24));
                                                                void * t313 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                                                uint64_t t314 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                                                uint64_t t315 = gm_l38;
                                                                if ((uintptr_t)t315 >= (uintptr_t)(t314)) gm_trap_index((uintptr_t)t315, t314, gm_src7nt72u848oha, 154, 40);
                                                                gm_t88rcucg1b7uk t316 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t313) + (uintptr_t)t315 * 4));
                                                                void * t317 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t311, t312, t316);
                                                                if (gm_unw.kind) {
                                                                    goto gm_lad;
                                                                }
                                                                gm_l40 = t317;
                                                                void * t318 = GM_LD(void *, ((uint8_t *)&F.l6));
                                                                uint64_t t319 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
                                                                uint64_t t320 = gm_l38;
                                                                if ((uintptr_t)t320 >= (uintptr_t)(t319)) gm_trap_index((uintptr_t)t320, t319, gm_src7nt72u848oha, 155, 13);
                                                                uint8_t t321 = gm_l10;
                                                                if (t321) {
                                                                    void * t322 = gm_l0;
                                                                    void * t323 = gm_l40;
                                                                    void * t324 = (void *)((uint8_t *)&F.l14);
                                                                    void * t325 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t322, t323, t324);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    gm_l65 = t325;
                                                                } else {
                                                                    void * t326 = gm_l40;
                                                                    gm_l65 = t326;
                                                                }
                                                                void * t327 = gm_l65;
                                                                GM_ST(void *, (((uint8_t *)t318) + (uintptr_t)t320 * 8), t327);
                                                                goto gm_lb39;
                                                            }
gm_lb40:;
                                                            gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 138, 9);
                                                        }
gm_lb39:;
                                                    }
gm_lb38:;
                                                    uint64_t t328 = gm_l36;
                                                    uint64_t r329; bool o330 = __builtin_add_overflow(t328, ((uint64_t)1ULL), &r329);
                                                    uint64_t t331 = r329;
                                                    gm_l36 = t331;
                                                }
                                            }
gm_lb36:;
                                        }
                                        {
                                            goto gm_lb25;
                                        }
                                    }
gm_lb26:;
                                }
                                {
                                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 113, 5);
                                }
                            }
gm_lb25:;
                        }
                    }
gm_lb24:;
                }
                {
                    uint64_t t332 = gm_l23;
                    uint64_t r333; bool o334 = __builtin_add_overflow(t332, ((uint64_t)1ULL), &r333);
                    uint64_t t335 = r333;
                    gm_l23 = t335;
                }
            }
        }
gm_lb22:;
    }
    {
        gm_trqfvmjtt4sij t336;
        gm_f_list_vp1djv1_2((void *)&t336, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l41 = t336;
        gm_l43 = ((uint64_t)0ULL);
        uint64_t t337 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        gm_l42 = t337;
        {
            for (;;) {
                uint64_t t338 = gm_l43;
                uint64_t t339 = gm_l42;
                uint8_t t340 = (t338 < t339);
                if (!t340) {
                    goto gm_lb46;
                }
                {
                    {
                        {
                            uint64_t t341 = gm_l43;
                            gm_l44 = t341;
                            {
                                {
                                    void * t342 = GM_LD(void *, ((uint8_t *)&F.l6));
                                    uint64_t t343 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
                                    uint64_t t344 = gm_l44;
                                    if ((uintptr_t)t344 >= (uintptr_t)(t343)) gm_trap_index((uintptr_t)t344, t343, gm_src7nt72u848oha, 160, 17);
                                    uint8_t t345 = (uint8_t)(GM_LD(void *, (((uint8_t *)t342) + (uintptr_t)t344 * 8)) != 0);
                                    if (!t345) {
                                        goto gm_lb52;
                                    }
                                    void * t346 = GM_LD(void *, (((uint8_t *)t342) + (uintptr_t)t344 * 8));
                                    gm_l45 = t346;
                                    void * t347 = (void *)((uint8_t *)&F.l41);
                                    void * t348 = gm_l45;
                                    gm_tftb1odc739t5 t349 = gm_f_push_d943vgp_2(t347, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t348)));
                                    gm_l66 = t349;
                                    uint8_t t350 = (uint8_t)!((((uint8_t *)&gm_l66))[0] != 0);
                                    if (!t350) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l66) + 1, gm_src7nt72u848oha, 161, 13);
                                    goto gm_lb51;
                                }
gm_lb52:;
                                uint8_t t351 = gm_l15;
                                uint8_t t352 = (uint8_t)!t351;
                                if (t352) {
                                    void * t353 = gm_l0;
                                    gm_tl874t2atnlh3 t354 = gm_l5;
                                    uint64_t t355 = gm_l44;
                                    void * t356 = gm_l0;
                                    gm_t88rcucg1b7uk t357 = gm_l1;
                                    gm_b16_8 t358 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t356, t357);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tf1bjg81u0glv t359;
                                    GM_ST(uint64_t, ((uint8_t *)&t359 + 0), t355);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t359 + 8), t358);
                                    gm_f_format_j4le8o0((void *)&gm_l67, (void *)NULL, cx, NULL, &t359);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t360 = (uint8_t)!((((uint8_t *)&gm_l67))[0] != 0);
                                    if (!t360) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l67) + 8, gm_src7nt72u848oha, 163, 51);
                                    gm_b16_8 t361 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l67) + 8));
                                    gm_b16_8 t362 = GM_LD(gm_b16_8, ((uint8_t *)&t361));
                                    gm_b0_1 t363 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t353, t354, t362);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                void * t364 = (void *)((uint8_t *)&F.l41);
                                void * t365 = gm_l0;
                                void * t366 = GM_LD(void *, ((uint8_t *)t365));
                                gm_tl874t2atnlh3 t367 = gm_l5;
                                void * t368 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t366, t367);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tftb1odc739t5 t369 = gm_f_push_d943vgp_2(t364, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t368)));
                                gm_l68 = t369;
                                uint8_t t370 = (uint8_t)!((((uint8_t *)&gm_l68))[0] != 0);
                                if (!t370) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l68) + 1, gm_src7nt72u848oha, 164, 13);
                            }
gm_lb51:;
                            goto gm_lb49;
                        }
gm_lb50:;
                        gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 159, 5);
                    }
gm_lb49:;
                }
gm_lb48:;
                uint64_t t371 = gm_l43;
                uint64_t r372; bool o373 = __builtin_add_overflow(t371, ((uint64_t)1ULL), &r372);
                uint64_t t374 = r372;
                gm_l43 = t374;
            }
        }
gm_lb46:;
    }
    {
        gm_t88rcucg1b7uk t375 = gm_l1;
        gm_b16_8 t376 = GM_LD(gm_b16_8, ((uint8_t *)&F.l41));
        gm_tl874t2atnlh3 t377 = gm_l5;
        gm_to0f5qn75cgg8 t378;
        memset(&t378, 0, sizeof t378);
        GM_ST(uint8_t, ((uint8_t *)&t378 + 0), ((uint8_t)35ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t378 + 8), t376);
        void * t379 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t375, &t378, t377);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l46 = t379;
        uint64_t t380 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 8));
        uint8_t t381 = ((t380 == ((uint64_t)0ULL)));
        if (t381) {
            void * t382 = gm_l46;
            F.ret = t382;
            goto gm_fret;
        }
        gm_t88rcucg1b7uk t383 = gm_l1;
        gm_b16_8 t384 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
        void * t385 = gm_l46;
        gm_tl874t2atnlh3 t386 = gm_l5;
        void * t387 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t383, t384, t385, t386);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t387;
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
__attribute__((aligned(4))) void * gm_f_construct_variant_o6is81c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2, const void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_construct_variant_o6is81c F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* u */
    gm_t08olq97rscn3 gm_l2; /* name */
    gm_th2lqbo77pvsc gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_tep2drsl9408b gm_l5; /* vs */
    gm_tf5srh4mp4b3i gm_l6; /* idx */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint64_t gm_l11; /* i */
    gm_tkn42u7na3t81 gm_l12; /* payload_ty */
    void * gm_l13; /* payload */
    gm_t88rcucg1b7uk gm_l14; /* pt */
    gm_b16_8 gm_l15; /* a */
    gm_b16_8 gm_l16; /* elems */
    gm_b16_8 gm_l17; /* names */
    GM_STACK_CHECK(&F, 2151u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&gm_l3, a3, sizeof gm_l3);
    gm_l4 = a4;
    gm_tdlpv0q69lov1 gm_l19;
    gm_tabb710d9mq2e gm_l20;
    gm_t3l8idb2m6c1r gm_l21;
    gm_tabb710d9mq2e gm_l22;
    gm_tabb710d9mq2e gm_l23;
    gm_t53at80pfdus3 gm_l24;
    uint8_t gm_l25;
    uint8_t gm_l26;
    uint8_t gm_l27;
    uint8_t gm_l28;
    uint8_t gm_l29;
    gm_tabb710d9mq2e gm_l30;
    uint8_t gm_l31;
    gm_tabb710d9mq2e gm_l32;
    {
        void * t22 = gm_l0;
        void * t23 = GM_LD(void *, ((uint8_t *)t22));
        gm_t88rcucg1b7uk t24 = gm_l1;
        gm_f_union_variants_a7khaqp((void *)&gm_l19, t23, GM_CONV(gm_csguankko4lmhd, t24));
        uint8_t t25 = (uint8_t)((((uint8_t *)&gm_l19))[24] != 0);
        if (t25) {
            memmove(((uint8_t *)&gm_l5), ((uint8_t *)&gm_l19), 24);
        } else {
            {
                void * t26 = gm_l0;
                gm_tl874t2atnlh3 t27 = gm_l4;
                gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                void * t29 = gm_l0;
                gm_t88rcucg1b7uk t30 = gm_l1;
                gm_b16_8 t31 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t29, t30);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t6j6smj7qoctu t32;
                GM_ST(gm_b16_8, ((uint8_t *)&t32 + 0), t28);
                GM_ST(gm_b16_8, ((uint8_t *)&t32 + 16), t31);
                gm_f_format_12rbmhd((void *)&gm_l20, (void *)NULL, cx, NULL, &t32);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                if (!t33) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 8, gm_src7nt72u848oha, 20, 24);
                gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
                gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
                gm_b0_1 t36 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t26, t27, t35);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t37 = gm_l0;
            void * t38 = GM_LD(void *, ((uint8_t *)t37));
            gm_tl874t2atnlh3 t39 = gm_l4;
            void * t40 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t38, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t40;
            goto gm_fret;
        }
        gm_tf5srh4mp4b3i t41;
        memset(&t41, 0, sizeof t41);
        gm_l6 = t41;
        gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
        gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
        gm_l7 = t43;
        gm_l8 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t44 = gm_l8;
                uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint8_t t46 = (t44 < t45);
                if (!t46) {
                    goto gm_lb1;
                }
                {
                    {
                        uint64_t t47 = gm_l8;
                        void * t48 = GM_LD(void *, ((uint8_t *)&gm_l7));
                        uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                        uint64_t t50 = gm_l8;
                        gm_tckdobunaulee t51 = GM_LD(gm_tckdobunaulee, (((uint8_t *)t48) + (uintptr_t)t50 * 72));
                        GM_ST(uint64_t, ((uint8_t *)&gm_l21 + 0), t47);
                        GM_ST(gm_tckdobunaulee, ((uint8_t *)&gm_l21 + 8), t51);
                    }
                    {
                        {
                            uint64_t t52 = GM_LD(uint64_t, ((uint8_t *)&gm_l21));
                            gm_l9 = t52;
                            memmove(((uint8_t *)&F.l10), (((uint8_t *)&gm_l21) + 8), 72);
                            gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                            gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                            uint8_t t55 = (gm_eq0iqiubfm7e(((uint8_t *)&t53), ((uint8_t *)&t54)));
                            if (t55) {
                                uint64_t t56 = gm_l9;
                                gm_tf5srh4mp4b3i t57;
                                GM_ST(uint8_t, (uint8_t *)&t57 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&t57 + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&t57 + 12, 0);
                                GM_ST(uint64_t, ((uint8_t *)&t57), t56);
                                ((uint8_t *)&t57)[8] = 1;
                                gm_l6 = t57;
                                goto gm_lb1;
                            }
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 24, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t58 = gm_l8;
                uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
                uint64_t t61 = r59;
                gm_l8 = t61;
            }
        }
gm_lb1:;
    }
    {
        uint8_t t62 = (uint8_t)((((uint8_t *)&gm_l6))[8] != 0);
        if (t62) {
            uint64_t t63 = GM_LD(uint64_t, ((uint8_t *)&gm_l6));
            gm_l11 = t63;
        } else {
            {
                void * t64 = gm_l0;
                gm_tl874t2atnlh3 t65 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
                void * t66 = gm_l0;
                gm_t88rcucg1b7uk t67 = gm_l1;
                gm_b16_8 t68 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t66, t67);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                gm_t6j6smj7qoctu t70;
                GM_ST(gm_b16_8, ((uint8_t *)&t70 + 0), t68);
                GM_ST(gm_b16_8, ((uint8_t *)&t70 + 16), t69);
                gm_f_format_l002mid((void *)&gm_l22, (void *)NULL, cx, NULL, &t70);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t71 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                if (!t71) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 8, gm_src7nt72u848oha, 31, 29);
                gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
                gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
                gm_b0_1 t74 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t64, t65, t73);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t75 = gm_l0;
            void * t76 = GM_LD(void *, ((uint8_t *)t75));
            gm_tl874t2atnlh3 t77 = gm_l4;
            void * t78 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t76, t77);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t78;
            goto gm_fret;
        }
        void * t79 = GM_LD(void *, ((uint8_t *)&gm_l5));
        uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
        uint64_t t81 = gm_l11;
        if ((uintptr_t)t81 >= (uintptr_t)(t80)) gm_trap_index((uintptr_t)t81, t80, gm_src7nt72u848oha, 34, 19);
        gm_tkn42u7na3t81 t82 = GM_LD(gm_tkn42u7na3t81, ((((uint8_t *)t79) + (uintptr_t)t81 * 72) + 16));
        gm_l12 = t82;
        gm_l13 = ((void *)0);
        {
            {
                uint8_t t83 = (uint8_t)((((uint8_t *)&gm_l12))[4] != 0);
                if (!t83) {
                    goto gm_lb7;
                }
                gm_t88rcucg1b7uk t84 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l12));
                gm_l14 = t84;
                uint8_t t85 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
                if (t85) {
                    gm_b16_8 t86 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
                    gm_l15 = t86;
                } else {
                    {
                        void * t87 = gm_l0;
                        gm_tl874t2atnlh3 t88 = gm_l4;
                        gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                        gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                        gm_t6j6smj7qoctu t91;
                        GM_ST(gm_b16_8, ((uint8_t *)&t91 + 0), t89);
                        GM_ST(gm_b16_8, ((uint8_t *)&t91 + 16), t90);
                        gm_f_format_9i81nmj((void *)&gm_l23, (void *)NULL, cx, NULL, &t91);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t92 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                        if (!t92) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 8, gm_src7nt72u848oha, 38, 28);
                        gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 8));
                        gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
                        gm_b0_1 t95 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t87, t88, t94);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    void * t96 = gm_l0;
                    void * t97 = GM_LD(void *, ((uint8_t *)t96));
                    gm_tl874t2atnlh3 t98 = gm_l4;
                    void * t99 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t97, t98);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t99;
                    goto gm_fret;
                }
                {
                    {
                        void * t100 = gm_l0;
                        void * t101 = GM_LD(void *, ((uint8_t *)t100));
                        void * t102 = (void *)(((uint8_t *)t101) + 8);
                        gm_t88rcucg1b7uk t103 = gm_l14;
                        gm_f_kind_jcnt4hm((void *)&gm_l24, t102, GM_CONV(gm_csguankko4lmhd, t103));
                        uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                        uint8_t t105 = (uint8_t)(t104 == ((uint8_t)12ULL));
                        if (!t105) {
                            goto gm_lb9;
                        }
                        gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 8));
                        gm_l16 = t106;
                        gm_b16_8 t107 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l24) + 8) + 16));
                        gm_l17 = t107;
                        uint64_t t108 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                        uint8_t t109 = (!(t108 == ((uint64_t)1ULL)));
                        if (t109) {
                            gm_l26 = ((uint8_t)1);
                        } else {
                            uint64_t t110 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                            uint8_t t111 = (!(t110 == ((uint64_t)1ULL)));
                            gm_l26 = t111;
                        }
                        uint8_t t112 = gm_l26;
                        if (t112) {
                            gm_l25 = ((uint8_t)1);
                        } else {
                            void * t113 = GM_LD(void *, ((uint8_t *)&gm_l15));
                            uint64_t t114 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t114)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t114, gm_src7nt72u848oha, 43, 56);
                            {
                                {
                                    uint8_t t115 = GM_LD(uint8_t, ((((uint8_t *)t113) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
                                    uint8_t t116 = (uint8_t)(t115 == ((uint8_t)1ULL));
                                    if (!t116) {
                                        goto gm_lb11;
                                    }
                                    gm_l27 = ((uint8_t)1);
                                    goto gm_lb10;
                                }
gm_lb11:;
                                {
                                    gm_l27 = ((uint8_t)0);
                                    goto gm_lb10;
                                }
gm_lb12:;
                                gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 43, 56);
                            }
gm_lb10:;
                            uint8_t t117 = gm_l27;
                            gm_l25 = t117;
                        }
                        uint8_t t118 = gm_l25;
                        if (t118) {
                            void * t119 = gm_l0;
                            gm_t88rcucg1b7uk t120 = gm_l14;
                            gm_b16_8 t121 = gm_l16;
                            gm_b16_8 t122 = gm_l17;
                            gm_b16_8 t123 = gm_l15;
                            gm_tl874t2atnlh3 t124 = gm_l4;
                            void * t125 = gm_f_construct_tuple_6ee5ejf((void *)NULL, cx, NULL, t119, t120, t121, t122, t123, t124);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l13 = t125;
                            gm_t88rcucg1b7uk t126 = gm_l1;
                            uint64_t t127 = gm_l11;
                            uint32_t t128 = (uint32_t)t127;
                            void * t129 = gm_l13;
                            gm_ttggq7ng4kp3p t130;
                            GM_ST(uint32_t, (uint8_t *)&t130 + 4, 0);
                            GM_ST(uint32_t, ((uint8_t *)&t130 + 0), t128);
                            GM_ST(void *, ((uint8_t *)&t130 + 8), t129);
                            gm_tl874t2atnlh3 t131 = gm_l4;
                            gm_to0f5qn75cgg8 t132;
                            memset(&t132, 0, sizeof t132);
                            GM_ST(uint8_t, ((uint8_t *)&t132 + 0), ((uint8_t)38ULL));
                            GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t132 + 8), t130);
                            void * t133 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t126, &t132, t131);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t133;
                            goto gm_fret;
                        }
                        goto gm_lb8;
                    }
gm_lb9:;
                }
gm_lb8:;
                uint64_t t134 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                uint8_t t135 = (!(t134 == ((uint64_t)1ULL)));
                if (t135) {
                    gm_l28 = ((uint8_t)1);
                } else {
                    void * t136 = GM_LD(void *, ((uint8_t *)&gm_l15));
                    uint64_t t137 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t137)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t137, gm_src7nt72u848oha, 48, 30);
                    {
                        {
                            uint8_t t138 = GM_LD(uint8_t, ((((uint8_t *)t136) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
                            uint8_t t139 = (uint8_t)(t138 == ((uint8_t)0ULL));
                            if (!t139) {
                                goto gm_lb14;
                            }
                            gm_l29 = ((uint8_t)0);
                            goto gm_lb13;
                        }
gm_lb14:;
                        {
                            gm_l29 = ((uint8_t)1);
                            goto gm_lb13;
                        }
gm_lb15:;
                        gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 48, 30);
                    }
gm_lb13:;
                    uint8_t t140 = gm_l29;
                    gm_l28 = t140;
                }
                uint8_t t141 = gm_l28;
                if (t141) {
                    void * t142 = gm_l0;
                    gm_tl874t2atnlh3 t143 = gm_l4;
                    void * t144 = gm_l0;
                    gm_t88rcucg1b7uk t145 = gm_l14;
                    gm_b16_8 t146 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t144, t145);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t147;
                    GM_ST(gm_b16_8, ((uint8_t *)&t147 + 0), t146);
                    gm_f_format_e1r9br2((void *)&gm_l30, (void *)NULL, cx, NULL, t147);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t148 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
                    if (!t148) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 8, gm_src7nt72u848oha, 49, 28);
                    gm_b16_8 t149 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 8));
                    gm_b16_8 t150 = GM_LD(gm_b16_8, ((uint8_t *)&t149));
                    gm_b0_1 t151 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t142, t143, t150);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t152 = gm_l0;
                    void * t153 = GM_LD(void *, ((uint8_t *)t152));
                    gm_tl874t2atnlh3 t154 = gm_l4;
                    void * t155 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t153, t154);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t155;
                    goto gm_fret;
                }
                {
                    {
                        void * t156 = GM_LD(void *, ((uint8_t *)&gm_l15));
                        uint64_t t157 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t157)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t157, gm_src7nt72u848oha, 52, 27);
                        uint8_t t158 = GM_LD(uint8_t, ((((uint8_t *)t156) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
                        uint8_t t159 = (uint8_t)(t158 == ((uint8_t)0ULL));
                        if (!t159) {
                            goto gm_lb17;
                        }
                        memmove(((uint8_t *)&F.l18), ((((uint8_t *)t156) + (uintptr_t)((uint64_t)0ULL) * 144) + 8), 88);
                        void * t160 = gm_l0;
                        void * t161 = (void *)((uint8_t *)&F.l18);
                        gm_t88rcucg1b7uk t162 = gm_l14;
                        void * t163 = gm_f_field_value_19vleud((void *)NULL, cx, NULL, t160, t161, t162);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l13 = t163;
                        goto gm_lb16;
                    }
gm_lb17:;
                    void * t164 = gm_l0;
                    void * t165 = GM_LD(void *, ((uint8_t *)t164));
                    gm_tl874t2atnlh3 t166 = gm_l4;
                    void * t167 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t165, t166);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t167;
                    goto gm_fret;
                }
gm_lb16:;
                goto gm_lb6;
            }
gm_lb7:;
            {
                {
                    uint8_t t168 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
                    if (t168) {
                        goto gm_lb19;
                    }
                    gm_l31 = ((uint8_t)0);
                    goto gm_lb18;
                }
gm_lb19:;
                {
                    gm_l31 = ((uint8_t)1);
                    goto gm_lb18;
                }
gm_lb20:;
                gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 54, 15);
            }
gm_lb18:;
            uint8_t t169 = gm_l31;
            if (t169) {
                void * t170 = gm_l0;
                gm_tl874t2atnlh3 t171 = gm_l4;
                gm_b16_8 t172 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                gm_tnpifv20irnph t173;
                GM_ST(gm_b16_8, ((uint8_t *)&t173 + 0), t172);
                gm_f_format_ktbu9b9((void *)&gm_l32, (void *)NULL, cx, NULL, t173);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t174 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                if (!t174) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 8, gm_src7nt72u848oha, 55, 24);
                gm_b16_8 t175 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l32) + 8));
                gm_b16_8 t176 = GM_LD(gm_b16_8, ((uint8_t *)&t175));
                gm_b0_1 t177 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t170, t171, t176);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t178 = gm_l0;
                void * t179 = GM_LD(void *, ((uint8_t *)t178));
                gm_tl874t2atnlh3 t180 = gm_l4;
                void * t181 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t179, t180);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t181;
                goto gm_fret;
            }
        }
gm_lb6:;
    }
    {
        gm_t88rcucg1b7uk t182 = gm_l1;
        uint64_t t183 = gm_l11;
        uint32_t t184 = (uint32_t)t183;
        void * t185 = gm_l13;
        gm_ttggq7ng4kp3p t186;
        GM_ST(uint32_t, (uint8_t *)&t186 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t186 + 0), t184);
        GM_ST(void *, ((uint8_t *)&t186 + 8), t185);
        gm_tl874t2atnlh3 t187 = gm_l4;
        gm_to0f5qn75cgg8 t188;
        memset(&t188, 0, sizeof t188);
        GM_ST(uint8_t, ((uint8_t *)&t188 + 0), ((uint8_t)38ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t188 + 8), t186);
        void * t189 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t182, &t188, t187);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t189;
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
__attribute__((aligned(4))) void * gm_f_default_value_lo723gi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_default_value_lo723gi F;
    void * gm_l0; /* cx */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* d */
    gm_t88rcucg1b7uk gm_l3; /* ty */
    void * gm_l4; /* tys */
    void * gm_l5; /* e */
    void * gm_l6; /* inner */
    uint8_t gm_l7; /* negative */
    void * gm_l8; /* x */
    uint8_t gm_l9; /* numeric */
    void * gm_l10; /* lit */
    void * gm_l12; /* lit */
    uint8_t gm_l14; /* b */
    GM_STACK_CHECK(&F, 1799u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l15;
    uint8_t gm_l16;
    uint8_t gm_l17;
    gm_t53at80pfdus3 gm_l18;
    uint8_t gm_l19;
    uint8_t gm_l20;
    gm_t53at80pfdus3 gm_l21;
    uint8_t gm_l22;
    uint8_t gm_l23;
    gm_t53at80pfdus3 gm_l24;
    {
        void * t36 = gm_l0;
        void * t37 = GM_LD(void *, ((uint8_t *)t36));
        void * t38 = (void *)(((uint8_t *)t37) + 8);
        gm_l4 = t38;
        void * t39 = gm_l2;
        gm_l5 = t39;
        {
            for (;;) {
                void * t40 = gm_l5;
                uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
                uint8_t t42 = (uint8_t)(t41 == ((uint8_t)9ULL));
                if (!t42) {
                    goto gm_lb1;
                }
                void * t43 = GM_LD(void *, ((((uint8_t *)t40) + 8) + 8));
                gm_l6 = t43;
                {
                    void * t44 = gm_l6;
                    gm_l5 = t44;
                }
gm_lb3:;
            }
        }
gm_lb1:;
        gm_l7 = ((uint8_t)0);
        {
            {
                void * t45 = gm_l5;
                uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)t45) + 8) + 0));
                uint8_t t47 = (uint8_t)(t46 == ((uint8_t)12ULL));
                if (!t47) {
                    goto gm_lb5;
                }
                uint8_t t48 = GM_LD(uint8_t, (((((uint8_t *)t45) + 8) + 8) + 0));
                uint8_t t49 = (uint8_t)(t48 == ((uint8_t)0ULL));
                if (!t49) {
                    goto gm_lb5;
                }
                void * t50 = GM_LD(void *, (((((uint8_t *)t45) + 8) + 8) + 8));
                gm_l8 = t50;
                void * t51 = gm_l8;
                {
                    {
                        uint8_t t52 = GM_LD(uint8_t, ((((uint8_t *)t51) + 8) + 0));
                        uint8_t t53 = (uint8_t)(t52 == ((uint8_t)0ULL));
                        if (!t53) {
                            goto gm_lb7;
                        }
                        gm_l15 = ((uint8_t)1);
                        goto gm_lb6;
                    }
gm_lb7:;
                    {
                        uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)t51) + 8) + 0));
                        uint8_t t55 = (uint8_t)(t54 == ((uint8_t)1ULL));
                        if (!t55) {
                            goto gm_lb8;
                        }
                        gm_l15 = ((uint8_t)1);
                        goto gm_lb6;
                    }
gm_lb8:;
                    {
                        gm_l15 = ((uint8_t)0);
                        goto gm_lb6;
                    }
gm_lb9:;
                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 338, 38);
                }
gm_lb6:;
                uint8_t t56 = gm_l15;
                if (!t56) {
                    goto gm_lb5;
                }
                gm_l7 = ((uint8_t)1);
                void * t57 = gm_l8;
                gm_l5 = t57;
                goto gm_lb4;
            }
gm_lb5:;
        }
gm_lb4:;
        void * t58 = gm_l4;
        gm_t88rcucg1b7uk t59 = gm_l3;
        uint8_t t60 = gm_f_is_integer_8d4f9ek(t58, GM_CONV(gm_csguankko4lmhd, t59));
        if (t60) {
            gm_l16 = ((uint8_t)1);
        } else {
            void * t61 = gm_l4;
            gm_t88rcucg1b7uk t62 = gm_l3;
            gm_f_kind_jcnt4hm((void *)&gm_l18, t61, GM_CONV(gm_csguankko4lmhd, t62));
            {
                {
                    uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&gm_l18) + 0));
                    uint8_t t64 = (uint8_t)(t63 == ((uint8_t)3ULL));
                    if (!t64) {
                        goto gm_lb11;
                    }
                    gm_l17 = ((uint8_t)1);
                    goto gm_lb10;
                }
gm_lb11:;
                {
                    gm_l17 = ((uint8_t)0);
                    goto gm_lb10;
                }
gm_lb12:;
                gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 342, 38);
            }
gm_lb10:;
            uint8_t t65 = gm_l17;
            gm_l16 = t65;
        }
    }
    {
        uint8_t t66 = gm_l16;
        gm_l9 = t66;
        void * t67 = gm_l5;
        {
            {
                uint8_t t68 = GM_LD(uint8_t, ((((uint8_t *)t67) + 8) + 0));
                uint8_t t69 = (uint8_t)(t68 == ((uint8_t)0ULL));
                if (!t69) {
                    goto gm_lb14;
                }
                void * t70 = GM_LD(void *, ((((uint8_t *)t67) + 8) + 8));
                gm_l10 = t70;
                {
                    {
                        uint8_t t71 = gm_l9;
                        if (!t71) {
                            goto gm_lb16;
                        }
                        void * t72 = gm_l10;
                        {
                            {
                                uint8_t t73 = (uint8_t)(((((uint8_t *)t72) + 48))[8] != 0);
                                if (t73) {
                                    goto gm_lb18;
                                }
                                gm_l19 = ((uint8_t)1);
                                goto gm_lb17;
                            }
gm_lb18:;
                            {
                                gm_l19 = ((uint8_t)0);
                                goto gm_lb17;
                            }
gm_lb19:;
                            gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 344, 37);
                        }
gm_lb17:;
                        uint8_t t74 = gm_l19;
                        if (!t74) {
                            goto gm_lb16;
                        }
                        {
                            void * t75 = gm_l10;
                            memmove(((uint8_t *)&F.l11), ((uint8_t *)t75), 48);
                        }
                        uint8_t t76 = gm_l7;
                        if (t76) {
                            gm_tbci5ospk5nnn t77 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l11));
                            gm_f_neg_agd5c5k((void *)&F.l11, (void *)NULL, cx, NULL, &t77);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        void * t78 = gm_l4;
                        gm_t88rcucg1b7uk t79 = gm_l3;
                        uint8_t t80 = gm_f_is_integer_8d4f9ek(t78, GM_CONV(gm_csguankko4lmhd, t79));
                        if (t80) {
                            gm_t88rcucg1b7uk t81 = gm_l3;
                            gm_tbci5ospk5nnn t82 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l11));
                            void * t83 = gm_l2;
                            gm_tl874t2atnlh3 t84 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t83) + 72));
                            gm_to0f5qn75cgg8 t85;
                            GM_ST(uint8_t, (uint8_t *)&t85 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t85 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t85 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t85 + 56, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t85 + 0), ((uint8_t)0ULL));
                            GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t85 + 8), t82);
                            void * t86 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t81, &t85, t84);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t86;
                            goto gm_fret;
                        }
                        {
                            void * t87 = gm_l0;
                            gm_tngtuiuskn938 t88;
                            gm_f_from_int_ofjuoqh((void *)&t88, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l11)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_t88rcucg1b7uk t89 = gm_l3;
                            void * t90 = gm_l2;
                            gm_tl874t2atnlh3 t91 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t90) + 72));
                            void * t92 = gm_f_float_value_cr4h9gi((void *)NULL, cx, NULL, t87, &t88, t89, t91);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t92;
                            goto gm_fret;
                        }
                    }
gm_lb16:;
                }
gm_lb15:;
                goto gm_lb13;
            }
gm_lb14:;
            {
                uint8_t t93 = GM_LD(uint8_t, ((((uint8_t *)t67) + 8) + 0));
                uint8_t t94 = (uint8_t)(t93 == ((uint8_t)1ULL));
                if (!t94) {
                    goto gm_lb20;
                }
                void * t95 = GM_LD(void *, ((((uint8_t *)t67) + 8) + 8));
                gm_l12 = t95;
                {
                    {
                        {
                            void * t96 = gm_l4;
                            gm_t88rcucg1b7uk t97 = gm_l3;
                            gm_f_kind_jcnt4hm((void *)&gm_l21, t96, GM_CONV(gm_csguankko4lmhd, t97));
                        }
                        {
                            {
                                uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
                                uint8_t t99 = (uint8_t)(t98 == ((uint8_t)3ULL));
                                if (!t99) {
                                    goto gm_lb24;
                                }
                                gm_l20 = ((uint8_t)1);
                                goto gm_lb23;
                            }
gm_lb24:;
                            {
                                gm_l20 = ((uint8_t)0);
                                goto gm_lb23;
                            }
gm_lb25:;
                            gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 350, 27);
                        }
gm_lb23:;
                        uint8_t t100 = gm_l20;
                        if (!t100) {
                            goto gm_lb22;
                        }
                        void * t101 = gm_l12;
                        {
                            {
                                uint8_t t102 = (uint8_t)(((((uint8_t *)t101) + 96))[1] != 0);
                                if (t102) {
                                    goto gm_lb27;
                                }
                                gm_l22 = ((uint8_t)1);
                                goto gm_lb26;
                            }
gm_lb27:;
                            {
                                gm_l22 = ((uint8_t)0);
                                goto gm_lb26;
                            }
gm_lb28:;
                            gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 350, 57);
                        }
gm_lb26:;
                        uint8_t t103 = gm_l22;
                        if (!t103) {
                            goto gm_lb22;
                        }
                        void * t104 = gm_l12;
                        uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)t104) + 98));
                        uint8_t t106 = (uint8_t)!t105;
                        if (!t106) {
                            goto gm_lb22;
                        }
                        {
                            void * t107 = gm_l12;
                            memmove(((uint8_t *)&F.l13), ((uint8_t *)t107), 96);
                        }
                        uint8_t t108 = gm_l7;
                        if (t108) {
                            {
                                uint8_t t109 = gm_f_is_zero_5aept6u(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l13)));
                                if (t109) {
                                    void * t110 = gm_l0;
                                    void * t111 = GM_LD(void *, ((uint8_t *)t110));
                                    uint64_t t112 = gm_l1;
                                    void * t113 = gm_l2;
                                    gm_t88rcucg1b7uk t114 = gm_l3;
                                    uint8_t t115 = gm_f_default_const_k60bpu4((void *)NULL, cx, NULL, t111, t112, t113, t114);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t116 = (uint8_t)!t115;
                                    if (t116) {
                                        void * t117 = gm_l0;
                                        void * t118 = GM_LD(void *, ((uint8_t *)t117));
                                        void * t119 = gm_l2;
                                        gm_tl874t2atnlh3 t120 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t119) + 72));
                                        void * t121 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t118, t120);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.ret = t121;
                                        goto gm_fret;
                                    }
                                    {
                                        gm_t88rcucg1b7uk t122 = gm_l3;
                                        gm_b16_8 t123;
                                        GM_ST(void *, (uint8_t *)&t123, (void *)(gm_sna4vk4o059d0));
                                        GM_ST(uintptr_t, (uint8_t *)&t123 + 8, (uintptr_t)(9));
                                        void * t124 = gm_l2;
                                        gm_tl874t2atnlh3 t125 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t124) + 72));
                                        gm_t08olq97rscn3 t126;
                                        GM_ST(uint32_t, (uint8_t *)&t126 + 28, 0);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t126 + 0), t123);
                                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t126 + 16), t125);
                                        void * t127 = gm_l2;
                                        gm_tl874t2atnlh3 t128 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t127) + 72));
                                        gm_to0f5qn75cgg8 t129;
                                        GM_ST(uint8_t, (uint8_t *)&t129 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t129 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t129 + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t129 + 40, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t129 + 48, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t129 + 56, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t129 + 0), ((uint8_t)14ULL));
                                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t129 + 8), t126);
                                        void * t130 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t122, &t129, t128);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.ret = t130;
                                        goto gm_fret;
                                    }
                                }
                            }
                            {
                                gm_tngtuiuskn938 t131 = GM_LD(gm_tngtuiuskn938, ((uint8_t *)&F.l13));
                                gm_f_neg_0u9uqg6((void *)&F.l13, (void *)NULL, cx, NULL, &t131);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                        }
                        {
                            void * t132 = gm_l0;
                            gm_t88rcucg1b7uk t133 = gm_l3;
                            void * t134 = gm_l2;
                            gm_tl874t2atnlh3 t135 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t134) + 72));
                            void * t136 = gm_f_float_value_cr4h9gi((void *)NULL, cx, NULL, t132, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l13)), t133, t135);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t136;
                            goto gm_fret;
                        }
                    }
gm_lb22:;
                }
gm_lb21:;
                goto gm_lb13;
            }
gm_lb20:;
            {
                uint8_t t137 = GM_LD(uint8_t, ((((uint8_t *)t67) + 8) + 0));
                uint8_t t138 = (uint8_t)(t137 == ((uint8_t)3ULL));
                if (!t138) {
                    goto gm_lb29;
                }
                uint8_t t139 = GM_LD(uint8_t, ((((uint8_t *)t67) + 8) + 8));
                gm_l14 = t139;
                {
                    void * t140 = gm_l4;
                    gm_t88rcucg1b7uk t141 = gm_l3;
                    gm_t53at80pfdus3 t142;
                    gm_f_kind_jcnt4hm((void *)&t142, t140, GM_CONV(gm_csguankko4lmhd, t141));
                    uint8_t t143 = GM_LD(uint8_t, (((uint8_t *)&t142) + 0));
                    uint8_t t144 = (uint8_t)((t143 == ((uint8_t)4ULL)));
                    gm_t53at80pfdus3 t145;
                    memset(&t145, 0, sizeof t145);
                    GM_ST(uint8_t, ((uint8_t *)&t145 + 0), ((uint8_t)4ULL));
                    if (t144) {
                        gm_t88rcucg1b7uk t146 = gm_l3;
                        uint8_t t147 = gm_l14;
                        void * t148 = gm_l2;
                        gm_tl874t2atnlh3 t149 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t148) + 72));
                        gm_to0f5qn75cgg8 t150;
                        memset(&t150, 0, sizeof t150);
                        GM_ST(uint8_t, ((uint8_t *)&t150 + 0), ((uint8_t)4ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t150 + 8), t147);
                        void * t151 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t146, &t150, t149);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t151;
                        goto gm_fret;
                    }
                }
                goto gm_lb13;
            }
gm_lb29:;
            {
                uint8_t t152 = GM_LD(uint8_t, ((((uint8_t *)t67) + 8) + 0));
                uint8_t t153 = (uint8_t)(t152 == ((uint8_t)4ULL));
                if (!t153) {
                    goto gm_lb30;
                }
                void * t154 = gm_l4;
                gm_t88rcucg1b7uk t155 = gm_l3;
                gm_f_kind_jcnt4hm((void *)&gm_l24, t154, GM_CONV(gm_csguankko4lmhd, t155));
                {
                    {
                        uint8_t t156 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                        uint8_t t157 = (uint8_t)(t156 == ((uint8_t)13ULL));
                        if (!t157) {
                            goto gm_lb32;
                        }
                        gm_l23 = ((uint8_t)1);
                        goto gm_lb31;
                    }
gm_lb32:;
                    {
                        gm_l23 = ((uint8_t)0);
                        goto gm_lb31;
                    }
gm_lb33:;
                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 362, 21);
                }
gm_lb31:;
                uint8_t t158 = gm_l23;
                if (t158) {
                    gm_t88rcucg1b7uk t159 = gm_l3;
                    void * t160 = gm_l2;
                    gm_tl874t2atnlh3 t161 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t160) + 72));
                    gm_to0f5qn75cgg8 t162;
                    memset(&t162, 0, sizeof t162);
                    GM_ST(uint8_t, ((uint8_t *)&t162 + 0), ((uint8_t)7ULL));
                    void * t163 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t159, &t162, t161);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t163;
                    goto gm_fret;
                }
                goto gm_lb13;
            }
gm_lb30:;
            {
                goto gm_lb13;
            }
gm_lb34:;
            gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 343, 5);
        }
gm_lb13:;
    }
    {
        void * t164 = gm_l0;
        void * t165 = GM_LD(void *, ((uint8_t *)t164));
        uint64_t t166 = gm_l1;
        void * t167 = gm_l2;
        gm_t88rcucg1b7uk t168 = gm_l3;
        uint8_t t169 = gm_f_default_const_k60bpu4((void *)NULL, cx, NULL, t165, t166, t167, t168);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t170 = (uint8_t)!t169;
        if (t170) {
            void * t171 = gm_l0;
            void * t172 = GM_LD(void *, ((uint8_t *)t171));
            void * t173 = gm_l2;
            gm_tl874t2atnlh3 t174 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t173) + 72));
            void * t175 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t172, t174);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t175;
            goto gm_fret;
        }
        gm_t88rcucg1b7uk t176 = gm_l3;
        gm_b16_8 t177;
        GM_ST(void *, (uint8_t *)&t177, (void *)(gm_sna4vk4o059d0));
        GM_ST(uintptr_t, (uint8_t *)&t177 + 8, (uintptr_t)(9));
        void * t178 = gm_l2;
        gm_tl874t2atnlh3 t179 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t178) + 72));
        gm_t08olq97rscn3 t180;
        GM_ST(uint32_t, (uint8_t *)&t180 + 28, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t180 + 0), t177);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t180 + 16), t179);
        void * t181 = gm_l2;
        gm_tl874t2atnlh3 t182 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t181) + 72));
        gm_to0f5qn75cgg8 t183;
        GM_ST(uint8_t, (uint8_t *)&t183 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t183 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t183 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t183 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t183 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t183 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t183 + 0), ((uint8_t)14ULL));
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t183 + 8), t180);
        void * t184 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t176, &t183, t182);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t184;
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
__attribute__((aligned(4))) void * gm_f_field_default_4ssmakm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint64_t a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_field_default_4ssmakm F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* id */
    uint64_t gm_l2; /* i */
    gm_t88rcucg1b7uk gm_l3; /* ty */
    gm_b16_8 gm_l4; /* defaults */
    void * gm_l5; /* d */
    uint32_t gm_l6; /* tid */
    void * gm_l8; /* v */
    GM_STACK_CHECK(&F, 660u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l9;
    void * gm_l10;
    void * gm_l11;
    void * t6 = gm_l0;
    void * t7 = GM_LD(void *, ((uint8_t *)t6));
    void * t8 = GM_LD(void *, (((uint8_t *)t7) + 1088));
    uint64_t t9 = GM_LD(uint64_t, ((((uint8_t *)t7) + 1088) + 8));
    uint32_t t10 = gm_l1;
    uint64_t t11 = (uint64_t)t10;
    if ((uintptr_t)t11 >= (uintptr_t)(t9)) gm_trap_index((uintptr_t)t11, t9, gm_src7nt72u848oha, 315, 17);
    gm_b16_8 t12 = GM_LD(gm_b16_8, ((((uint8_t *)t8) + (uintptr_t)t11 * 184) + 80));
    gm_l4 = t12;
    uint64_t t13 = gm_l2;
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
    uint8_t t15 = (t13 >= t14);
    if (t15) {
        gm_l9 = ((uint8_t)1);
    } else {
        void * t16 = GM_LD(void *, ((uint8_t *)&gm_l4));
        uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
        uint64_t t18 = gm_l2;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)t16) + (uintptr_t)t18 * 1));
        uint8_t t20 = (uint8_t)!t19;
        gm_l9 = t20;
    }
    uint8_t t21 = gm_l9;
    if (t21) {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    void * t22 = gm_l0;
    void * t23 = GM_LD(void *, ((uint8_t *)t22));
    uint32_t t24 = gm_l1;
    uint64_t t25 = gm_l2;
    void * t26 = gm_f_field_default_expr_tqfuiff(t23, t24, t25);
    gm_l10 = t26;
    uint8_t t27 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l10)) != 0);
    if (t27) {
        void * t28 = GM_LD(void *, ((uint8_t *)&gm_l10));
        gm_l5 = t28;
    } else {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    void * t29 = gm_l0;
    void * t30 = GM_LD(void *, ((uint8_t *)t29));
    void * t31 = (void *)(((uint8_t *)t30) + 8);
    uint32_t t32 = gm_l1;
    uint32_t t33 = gm_f_template_9o92cvv(t31, t32);
    gm_l6 = t33;
    uint32_t t34 = gm_l6;
    uint32_t t35 = gm_l1;
    uint8_t t36 = (!(t34 == t35));
    if (t36) {
        {
            {
                void * t37 = gm_l0;
                void * t38 = GM_LD(void *, ((uint8_t *)t37));
                void * t39 = GM_LD(void *, (((uint8_t *)t38) + 1088));
                uint64_t t40 = GM_LD(uint64_t, ((((uint8_t *)t38) + 1088) + 8));
                uint32_t t41 = gm_l6;
                uint64_t t42 = (uint64_t)t41;
                if ((uintptr_t)t42 >= (uintptr_t)(t40)) gm_trap_index((uintptr_t)t42, t40, gm_src7nt72u848oha, 322, 28);
                uint8_t t43 = GM_LD(uint8_t, (((((uint8_t *)t39) + (uintptr_t)t42 * 184) + 24) + 0));
                uint8_t t44 = (uint8_t)(t43 == ((uint8_t)0ULL));
                if (!t44) {
                    goto gm_lb2;
                }
                memmove(((uint8_t *)&F.l7), (((((uint8_t *)t39) + (uintptr_t)t42 * 184) + 24) + 8), 48);
                {
                    {
                        uint64_t t45 = gm_l2;
                        uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
                        uint8_t t47 = (t45 < t46);
                        if (!t47) {
                            goto gm_lb4;
                        }
                        void * t48 = gm_l0;
                        void * t49 = GM_LD(void *, ((uint8_t *)t48));
                        void * t50 = (void *)(((uint8_t *)t49) + 8);
                        void * t51 = GM_LD(void *, ((uint8_t *)&F.l7));
                        uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
                        uint64_t t53 = gm_l2;
                        gm_t88rcucg1b7uk t54 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t51) + (uintptr_t)t53 * 40) + 16));
                        uint8_t t55 = gm_f_has_param_u9t0efs(t50, GM_CONV(gm_csguankko4lmhd, t54));
                        if (!t55) {
                            goto gm_lb4;
                        }
                        void * t56 = gm_l0;
                        void * t57 = GM_LD(void *, ((uint8_t *)t56));
                        void * t58 = gm_l0;
                        void * t59 = GM_LD(void *, ((uint8_t *)t58));
                        void * t60 = GM_LD(void *, (((uint8_t *)t59) + 1088));
                        uint64_t t61 = GM_LD(uint64_t, ((((uint8_t *)t59) + 1088) + 8));
                        uint32_t t62 = gm_l1;
                        uint64_t t63 = (uint64_t)t62;
                        if ((uintptr_t)t63 >= (uintptr_t)(t61)) gm_trap_index((uintptr_t)t63, t61, gm_src7nt72u848oha, 324, 45);
                        uint64_t t64 = GM_LD(uint64_t, ((((uint8_t *)t60) + (uintptr_t)t63 * 184) + 16));
                        void * t65 = gm_l5;
                        void * t66 = GM_LD(void *, ((uint8_t *)&F.l7));
                        uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
                        uint64_t t68 = gm_l2;
                        gm_t88rcucg1b7uk t69 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t66) + (uintptr_t)t68 * 40) + 16));
                        gm_tkn42u7na3t81 t70;
                        GM_ST(uint8_t, (uint8_t *)&t70 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t70 + 6, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t70), t69);
                        ((uint8_t *)&t70)[4] = 1;
                        void * t71 = gm_f_check_const_expr_2ot4q9v((void *)NULL, cx, NULL, t57, t64, t65, t70);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l8 = t71;
                        uint8_t t72 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l8)) != 0);
                        if (t72) {
                            void * t73 = GM_LD(void *, ((uint8_t *)&gm_l8));
                            gm_l11 = t73;
                        } else {
                            void * t74 = gm_l0;
                            void * t75 = GM_LD(void *, ((uint8_t *)t74));
                            void * t76 = gm_l5;
                            gm_tl874t2atnlh3 t77 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t76) + 72));
                            void * t78 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t75, t77);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l11 = t78;
                        }
                        void * t79 = gm_l11;
                        F.ret = t79;
                        goto gm_fret;
                    }
gm_lb4:;
                }
gm_lb3:;
                goto gm_lb1;
            }
gm_lb2:;
        }
gm_lb1:;
    }
    void * t80 = gm_l0;
    void * t81 = gm_l0;
    void * t82 = GM_LD(void *, ((uint8_t *)t81));
    void * t83 = GM_LD(void *, (((uint8_t *)t82) + 1088));
    uint64_t t84 = GM_LD(uint64_t, ((((uint8_t *)t82) + 1088) + 8));
    uint32_t t85 = gm_l1;
    uint64_t t86 = (uint64_t)t85;
    if ((uintptr_t)t86 >= (uintptr_t)(t84)) gm_trap_index((uintptr_t)t86, t84, gm_src7nt72u848oha, 329, 29);
    uint64_t t87 = GM_LD(uint64_t, ((((uint8_t *)t83) + (uintptr_t)t86 * 184) + 16));
    void * t88 = gm_l5;
    gm_t88rcucg1b7uk t89 = gm_l3;
    void * t90 = gm_f_default_value_lo723gi((void *)NULL, cx, NULL, t80, t87, t88, t89);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t90;
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
inline __attribute__((aligned(4))) void * gm_f_field_value_19vleud(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_field_value_19vleud F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    void * gm_l3; /* v */
    GM_STACK_CHECK(&F, 420u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l1;
        uint8_t t3 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        uint8_t t4 = (uint8_t)((t3 == ((uint8_t)5ULL)));
        gm_t2gtbh39ptekd t5 = GM_LD(gm_t2gtbh39ptekd, (((uint8_t *)t2) + 8));
        gm_t2gtbh39ptekd t6;
        memset(&t6, 0, sizeof t6);
        GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)5ULL));
        if (t4) {
            void * t7 = gm_l0;
            gm_t88rcucg1b7uk t8 = gm_l2;
            uint8_t t9 = gm_f_is_plain_data_ej4mh2m((void *)NULL, cx, NULL, t7, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t10 = (uint8_t)!t9;
            if (t10) {
                void * t11 = gm_l0;
                void * t12 = gm_l1;
                gm_tl874t2atnlh3 t13 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t12) + 72));
                void * t14 = gm_l0;
                void * t15 = GM_LD(void *, ((uint8_t *)t14));
                gm_t88rcucg1b7uk t16 = gm_l2;
                gm_b16_8 t17 = gm_f_uninit_message_u2c3peu((void *)NULL, cx, NULL, t15, t16);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b0_1 t18 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t11, t13, t17);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_t88rcucg1b7uk t19 = gm_l2;
            void * t20 = gm_l1;
            gm_tl874t2atnlh3 t21 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t20) + 72));
            gm_to0f5qn75cgg8 t22;
            memset(&t22, 0, sizeof t22);
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)8ULL));
            void * t23 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t19, &t22, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t23;
            goto gm_fret;
        }
    }
    void * t24 = gm_l0;
    void * t25 = gm_l1;
    gm_t88rcucg1b7uk t26 = gm_l2;
    gm_tkn42u7na3t81 t27;
    GM_ST(uint8_t, (uint8_t *)&t27 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t27 + 6, 0);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t27), t26);
    ((uint8_t *)&t27)[4] = 1;
    void * t28 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t24, t25, t27);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t28;
    void * t29 = gm_l0;
    void * t30 = gm_l3;
    gm_t88rcucg1b7uk t31 = gm_l2;
    void * t32 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t29, t30, t31);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t32;
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
inline __attribute__((aligned(4))) void * gm_f_float_value_cr4h9gi(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_float_value_cr4h9gi F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint16_t gm_l4; /* bits */
    double gm_l5; /* x */
    GM_STACK_CHECK(&F, 280u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t53at80pfdus3 gm_l6;
    {
        {
            {
                void * t4 = gm_l0;
                void * t5 = GM_LD(void *, ((uint8_t *)t4));
                void * t6 = (void *)(((uint8_t *)t5) + 8);
                gm_t88rcucg1b7uk t7 = gm_l2;
                gm_f_kind_jcnt4hm((void *)&gm_l6, t6, GM_CONV(gm_csguankko4lmhd, t7));
            }
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            uint8_t t9 = (uint8_t)(t8 == ((uint8_t)3ULL));
            if (!t9) {
                goto gm_lb2;
            }
            uint16_t t10 = GM_LD(uint16_t, (((uint8_t *)&gm_l6) + 8));
            gm_l4 = t10;
            {
                uint16_t t11 = gm_l4;
                double t12 = gm_f_to_float_o1iop8t((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l1)), t11);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l5 = t12;
            }
            {
                gm_t88rcucg1b7uk t13 = gm_l2;
                double t14 = gm_l5;
                gm_tl874t2atnlh3 t15 = gm_l3;
                gm_to0f5qn75cgg8 t16;
                memset(&t16, 0, sizeof t16);
                GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)1ULL));
                GM_ST(double, ((uint8_t *)&t16 + 8), t14);
                void * t17 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t13, &t16, t15);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t17;
                goto gm_fret;
            }
        }
gm_lb2:;
        void * t18 = gm_l0;
        void * t19 = GM_LD(void *, ((uint8_t *)t18));
        gm_tl874t2atnlh3 t20 = gm_l3;
        void * t21 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t21;
        goto gm_fret;
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
__attribute__((aligned(4))) void gm_f_spread_source_fields_5csral7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2) {
    struct gm_fr_spread_source_fields_5csral7 F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* vt */
    void * gm_l2; /* e */
    gm_b16_8 gm_l4; /* elems */
    gm_b16_8 gm_l5; /* names */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    gm_b16_8 gm_l9; /* n */
    gm_b16_8 gm_l10; /* fs */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    GM_STACK_CHECK(&F, 915u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l14;
    gm_tf1bjg81u0glv gm_l15;
    gm_tftb1odc739t5 gm_l16;
    gm_th2lqbo77pvsc gm_l17;
    gm_tabb710d9mq2e gm_l18;
    gm_tftb1odc739t5 gm_l19;
    {
        gm_trqfvmjtt4sij t14;
        gm_f_list_7gsrufb((void *)&t14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l3 = t14;
    }
    {
        {
            {
                void * t15 = gm_l0;
                void * t16 = GM_LD(void *, ((uint8_t *)t15));
                void * t17 = (void *)(((uint8_t *)t16) + 8);
                gm_t88rcucg1b7uk t18 = gm_l1;
                gm_f_kind_jcnt4hm((void *)&gm_l14, t17, GM_CONV(gm_csguankko4lmhd, t18));
            }
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)12ULL));
            if (!t20) {
                goto gm_lb2;
            }
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
            gm_l4 = t21;
            gm_b16_8 t22 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l14) + 8) + 16));
            gm_l5 = t22;
            uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t24 = (t23 > ((uint64_t)0ULL));
            if (!t24) {
                goto gm_lb2;
            }
            gm_b16_8 t25 = gm_l5;
            gm_l6 = t25;
            gm_l7 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t26 = gm_l7;
                    uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    uint8_t t28 = (t26 < t27);
                    if (!t28) {
                        goto gm_lb3;
                    }
                    {
                        uint64_t t29 = gm_l7;
                        void * t30 = GM_LD(void *, ((uint8_t *)&gm_l6));
                        uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                        uint64_t t32 = gm_l7;
                        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)t30) + (uintptr_t)t32 * 16));
                        GM_ST(uint64_t, ((uint8_t *)&gm_l15 + 0), t29);
                        GM_ST(gm_b16_8, ((uint8_t *)&gm_l15 + 8), t33);
                        {
                            {
                                uint64_t t34 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
                                gm_l8 = t34;
                                gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
                                gm_l9 = t35;
                                void * t36 = (void *)((uint8_t *)&F.l3);
                                gm_b16_8 t37 = gm_l9;
                                void * t38 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                uint64_t t40 = gm_l8;
                                if ((uintptr_t)t40 >= (uintptr_t)(t39)) gm_trap_index((uintptr_t)t40, t39, gm_src7nt72u848oha, 65, 74);
                                gm_t88rcucg1b7uk t41 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t38) + (uintptr_t)t40 * 4));
                                gm_tep2drsl9408b t42;
                                GM_ST(uint32_t, (uint8_t *)&t42 + 20, 0);
                                GM_ST(gm_b16_8, ((uint8_t *)&t42 + 0), t37);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t42 + 16), t41);
                                gm_tftb1odc739t5 t43 = gm_f_push_lnebar3(t36, &t42);
                                gm_l16 = t43;
                                uint8_t t44 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                                if (!t44) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_src7nt72u848oha, 65, 40);
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 65, 9);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t45 = gm_l7;
                    uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
                    uint64_t t48 = r46;
                    gm_l7 = t48;
                }
            }
gm_lb3:;
            {
                gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t49);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                goto gm_fdone;
            }
        }
gm_lb2:;
    }
gm_lb1:;
    {
        void * t50 = gm_l0;
        void * t51 = GM_LD(void *, ((uint8_t *)t50));
        gm_t88rcucg1b7uk t52 = gm_l1;
        gm_f_struct_fields_chqu3it((void *)&gm_l17, t51, GM_CONV(gm_csguankko4lmhd, t52));
    }
    uint8_t t53 = (uint8_t)((((uint8_t *)&gm_l17))[16] != 0);
    if (t53) {
        gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l17));
        gm_l10 = t54;
    } else {
        void * t55 = gm_l0;
        void * t56 = GM_LD(void *, ((uint8_t *)t55));
        void * t57 = (void *)(((uint8_t *)t56) + 8);
        gm_t88rcucg1b7uk t58 = gm_l1;
        uint8_t t59 = gm_f_is_error_8k27gd1(t57, GM_CONV(gm_csguankko4lmhd, t58));
        uint8_t t60 = (uint8_t)!t59;
        if (t60) {
            void * t61 = gm_l0;
            void * t62 = gm_l2;
            gm_tl874t2atnlh3 t63 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t62) + 72));
            void * t64 = gm_l0;
            gm_t88rcucg1b7uk t65 = gm_l1;
            gm_b16_8 t66 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t64, t65);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t67;
            GM_ST(gm_b16_8, ((uint8_t *)&t67 + 0), t66);
            gm_f_format_a07tfib((void *)&gm_l18, (void *)NULL, cx, NULL, t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t68 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
            if (!t68) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_src7nt72u848oha, 69, 57);
            gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
            gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
            gm_b0_1 t71 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t61, t63, t70);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
    gm_b16_8 t72 = gm_l10;
    gm_l11 = t72;
    gm_l12 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t73 = gm_l12;
            uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
            uint8_t t75 = (t73 < t74);
            if (!t75) {
                goto gm_lb8;
            }
            {
                void * t76 = GM_LD(void *, ((uint8_t *)&gm_l11));
                uint64_t t77 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                uint64_t t78 = gm_l12;
                {
                    {
                        {
                            memmove(((uint8_t *)&F.l13), (((uint8_t *)t76) + (uintptr_t)t78 * 40), 40);
                        }
                        {
                            void * t79 = (void *)((uint8_t *)&F.l3);
                            gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                            gm_t88rcucg1b7uk t81 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l13) + 16));
                            gm_tep2drsl9408b t82;
                            GM_ST(uint32_t, (uint8_t *)&t82 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t82 + 0), t80);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t82 + 16), t81);
                            gm_tftb1odc739t5 t83 = gm_f_push_lnebar3(t79, &t82);
                            gm_l19 = t83;
                        }
                        uint8_t t84 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                        if (!t84) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_src7nt72u848oha, 72, 19);
                        goto gm_lb11;
                    }
gm_lb12:;
                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 72, 5);
                }
gm_lb11:;
            }
gm_lb10:;
            uint64_t t85 = gm_l12;
            uint64_t r86; bool o87 = __builtin_add_overflow(t85, ((uint64_t)1ULL), &r86);
            uint64_t t88 = r86;
            gm_l12 = t88;
        }
    }
gm_lb8:;
    {
        gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t89);
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
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_strip_wrappers_e5thpib(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_strip_wrappers_e5thpib F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t0 */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_t88rcucg1b7uk gm_l3; /* i */
    gm_t88rcucg1b7uk gm_l4; /* ok */
    GM_STACK_CHECK_SHALLOW(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_t53at80pfdus3 gm_l5;
    gm_t88rcucg1b7uk t9 = gm_l1;
    gm_l2 = t9;
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb1;
            }
            {
                void * t10 = gm_l0;
                void * t11 = GM_LD(void *, ((uint8_t *)t10));
                void * t12 = (void *)(((uint8_t *)t11) + 8);
                gm_t88rcucg1b7uk t13 = gm_l2;
                gm_f_kind_jcnt4hm((void *)&gm_l5, t12, GM_CONV(gm_csguankko4lmhd, t13));
                {
                    {
                        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        uint8_t t15 = (uint8_t)(t14 == ((uint8_t)13ULL));
                        if (!t15) {
                            goto gm_lb5;
                        }
                        gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
                        gm_l3 = t16;
                        gm_t88rcucg1b7uk t17 = gm_l3;
                        gm_l2 = t17;
                        goto gm_lb4;
                    }
gm_lb5:;
                    {
                        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        uint8_t t19 = (uint8_t)(t18 == ((uint8_t)14ULL));
                        if (!t19) {
                            goto gm_lb6;
                        }
                        gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l5) + 8) + 4));
                        gm_l4 = t20;
                        gm_t88rcucg1b7uk t21 = gm_l4;
                        gm_l2 = t21;
                        goto gm_lb4;
                    }
gm_lb6:;
                    {
                        gm_t88rcucg1b7uk t22 = gm_l2;
                        F.ret = t22;
                        goto gm_fret;
                    }
gm_lb7:;
                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 9, 9);
                }
gm_lb4:;
            }
gm_lb3:;
        }
    }
gm_lb1:;
gm_fret:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
gm_fprop:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
}

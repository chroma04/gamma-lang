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
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint32_t f3; uint32_t f4; uint8_t p5[4]; void * f5; } gm_t04llc5uen0qo;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint32_t f3; uint64_t f4; uint32_t f5; uint32_t f6; void * f7; } gm_t1lvkimu9u0hv;
typedef struct { uint32_t f0; uint32_t f1; uint64_t f2; } gm_t4h1llua034m2;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint8_t f3; uint8_t p4[3]; void * f4; } gm_t6cngq8lhim84;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint32_t f3; uint32_t f4; uint8_t f5; uint8_t f6; uint8_t p7[2]; uint64_t f7; void * f8; } gm_td7fbab3thbb2;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint8_t p3[4]; uint64_t f3; uint32_t f4; uint32_t f5; void * f6; } gm_tdi0t3ilv4peg;
typedef struct { uint32_t f0; uint32_t f1; uint64_t f2; uint32_t f3; uint32_t f4; uint32_t f5; uint8_t p6[4]; void * f6; } gm_tdqpbd1b4j0lu;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t p3[1]; uint32_t f3; uint32_t f4; uint32_t f5; uint32_t f6; uint8_t p7[4]; void * f7; } gm_te4o1hd8337m3;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint8_t p3[4]; uint64_t f3; uint32_t f4; uint32_t f5; uint32_t f6; uint8_t p7[4]; void * f7; } gm_thbsr19fadfjo;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t p3[1]; uint32_t f3; uint32_t f4; uint32_t f5; void * f6; } gm_ti4ei62u4i1d9;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint32_t f3; uint32_t f4; uint32_t f5; void * f6; } gm_tn3qmqcq7vsk9;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; } gm_ttovhfee8mf28;
_Static_assert(sizeof(gm_t04llc5uen0qo) == 32 && _Alignof(gm_t04llc5uen0qo) == 8 && offsetof(gm_t04llc5uen0qo, f0) == 0 && offsetof(gm_t04llc5uen0qo, f1) == 4 && offsetof(gm_t04llc5uen0qo, f2) == 8 && offsetof(gm_t04llc5uen0qo, f3) == 12 && offsetof(gm_t04llc5uen0qo, f4) == 16 && offsetof(gm_t04llc5uen0qo, f5) == 24, "layout of `(base: u32, off: u32, size: u32, align: u32, v: u32, n: ^Node)`");
_Static_assert(sizeof(gm_t1lvkimu9u0hv) == 40 && _Alignof(gm_t1lvkimu9u0hv) == 8 && offsetof(gm_t1lvkimu9u0hv, f0) == 0 && offsetof(gm_t1lvkimu9u0hv, f1) == 4 && offsetof(gm_t1lvkimu9u0hv, f2) == 8 && offsetof(gm_t1lvkimu9u0hv, f3) == 12 && offsetof(gm_t1lvkimu9u0hv, f4) == 16 && offsetof(gm_t1lvkimu9u0hv, f5) == 24 && offsetof(gm_t1lvkimu9u0hv, f6) == 28 && offsetof(gm_t1lvkimu9u0hv, f7) == 32, "layout of `(r: u32, p: u32, len: u32, i: u32, esz: u64, size: u32, align: u32, n: ^Node)`");
_Static_assert(sizeof(gm_t4h1llua034m2) == 16 && _Alignof(gm_t4h1llua034m2) == 8 && offsetof(gm_t4h1llua034m2, f0) == 0 && offsetof(gm_t4h1llua034m2, f1) == 4 && offsetof(gm_t4h1llua034m2, f2) == 8, "layout of `(p: u32, i: u32, esz: u64)`");
_Static_assert(sizeof(gm_t6cngq8lhim84) == 24 && _Alignof(gm_t6cngq8lhim84) == 8 && offsetof(gm_t6cngq8lhim84, f0) == 0 && offsetof(gm_t6cngq8lhim84, f1) == 4 && offsetof(gm_t6cngq8lhim84, f2) == 8 && offsetof(gm_t6cngq8lhim84, f3) == 12 && offsetof(gm_t6cngq8lhim84, f4) == 16, "layout of `(lo: u32, hi: u32, len: u32, inclusive: bool, n: ^Node)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; uint8_t v2; gm_t9qkh0rsooc8d v3; gm_t9qkh0rsooc8d v4; gm_tcc1rq4n1avb2 v5; uint32_t v6; uint32_t v7; } f1; } gm_t8588ma2cr9d7;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; uint16_t f2; uint32_t f3; uint32_t f4; uint32_t f5; } gm_tb1f35qilrmun;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_td7fbab3thbb2) == 40 && _Alignof(gm_td7fbab3thbb2) == 8 && offsetof(gm_td7fbab3thbb2, f0) == 0 && offsetof(gm_td7fbab3thbb2, f1) == 4 && offsetof(gm_td7fbab3thbb2, f2) == 8 && offsetof(gm_td7fbab3thbb2, f3) == 12 && offsetof(gm_td7fbab3thbb2, f4) == 16 && offsetof(gm_td7fbab3thbb2, f5) == 20 && offsetof(gm_td7fbab3thbb2, f6) == 21 && offsetof(gm_td7fbab3thbb2, f7) == 24 && offsetof(gm_td7fbab3thbb2, f8) == 32, "layout of `(at: u32, p: u32, len: u32, lo: u32, hi: u32, has_len: bool, inclusive: bool, esz: u64, n: ^Node)`");
_Static_assert(sizeof(gm_tdi0t3ilv4peg) == 40 && _Alignof(gm_tdi0t3ilv4peg) == 8 && offsetof(gm_tdi0t3ilv4peg, f0) == 0 && offsetof(gm_tdi0t3ilv4peg, f1) == 4 && offsetof(gm_tdi0t3ilv4peg, f2) == 8 && offsetof(gm_tdi0t3ilv4peg, f3) == 16 && offsetof(gm_tdi0t3ilv4peg, f4) == 24 && offsetof(gm_tdi0t3ilv4peg, f5) == 28 && offsetof(gm_tdi0t3ilv4peg, f6) == 32, "layout of `(r: u32, p: u32, i: u32, esz: u64, size: u32, align: u32, n: ^Node)`");
_Static_assert(sizeof(gm_tdqpbd1b4j0lu) == 40 && _Alignof(gm_tdqpbd1b4j0lu) == 8 && offsetof(gm_tdqpbd1b4j0lu, f0) == 0 && offsetof(gm_tdqpbd1b4j0lu, f1) == 4 && offsetof(gm_tdqpbd1b4j0lu, f2) == 8 && offsetof(gm_tdqpbd1b4j0lu, f3) == 16 && offsetof(gm_tdqpbd1b4j0lu, f4) == 20 && offsetof(gm_tdqpbd1b4j0lu, f5) == 24 && offsetof(gm_tdqpbd1b4j0lu, f6) == 32, "layout of `(p: u32, i: u32, esz: u64, size: u32, align: u32, v: u32, n: ^Node)`");
_Static_assert(sizeof(gm_te4o1hd8337m3) == 32 && _Alignof(gm_te4o1hd8337m3) == 8 && offsetof(gm_te4o1hd8337m3, f0) == 0 && offsetof(gm_te4o1hd8337m3, f1) == 1 && offsetof(gm_te4o1hd8337m3, f2) == 2 && offsetof(gm_te4o1hd8337m3, f3) == 4 && offsetof(gm_te4o1hd8337m3, f4) == 8 && offsetof(gm_te4o1hd8337m3, f5) == 12 && offsetof(gm_te4o1hd8337m3, f6) == 16 && offsetof(gm_te4o1hd8337m3, f7) == 24, "layout of `(op: u8, signed: bool, wrap: bool, bits: u32, r: u32, a: u32, b: u32, n: ^Node)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
_Static_assert(sizeof(gm_thbsr19fadfjo) == 48 && _Alignof(gm_thbsr19fadfjo) == 8 && offsetof(gm_thbsr19fadfjo, f0) == 0 && offsetof(gm_thbsr19fadfjo, f1) == 4 && offsetof(gm_thbsr19fadfjo, f2) == 8 && offsetof(gm_thbsr19fadfjo, f3) == 16 && offsetof(gm_thbsr19fadfjo, f4) == 24 && offsetof(gm_thbsr19fadfjo, f5) == 28 && offsetof(gm_thbsr19fadfjo, f6) == 32 && offsetof(gm_thbsr19fadfjo, f7) == 40, "layout of `(p: u32, len: u32, i: u32, esz: u64, size: u32, align: u32, v: u32, n: ^Node)`");
typedef struct { gm_ttovhfee8mf28 f0; gm_ttovhfee8mf28 f1; uint8_t f2; uint8_t p3[3]; uint32_t f3; uint32_t f4; uint32_t f5; void * f6; } gm_thji9nptko1bl;
_Static_assert(sizeof(gm_ti4ei62u4i1d9) == 24 && _Alignof(gm_ti4ei62u4i1d9) == 8 && offsetof(gm_ti4ei62u4i1d9, f0) == 0 && offsetof(gm_ti4ei62u4i1d9, f1) == 1 && offsetof(gm_ti4ei62u4i1d9, f2) == 2 && offsetof(gm_ti4ei62u4i1d9, f3) == 4 && offsetof(gm_ti4ei62u4i1d9, f4) == 8 && offsetof(gm_ti4ei62u4i1d9, f5) == 12 && offsetof(gm_ti4ei62u4i1d9, f6) == 16, "layout of `(op: u8, signed: bool, wrap: bool, r: u32, a: u32, b: u32, n: ^Node)`");
typedef struct { gm_ttovhfee8mf28 f0; gm_ttovhfee8mf28 f1; uint32_t f2; uint32_t f3; uint32_t f4; uint8_t p5[4]; void * f5; } gm_tiag95i83438g;
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_tn3qmqcq7vsk9) == 32 && _Alignof(gm_tn3qmqcq7vsk9) == 8 && offsetof(gm_tn3qmqcq7vsk9, f0) == 0 && offsetof(gm_tn3qmqcq7vsk9, f1) == 4 && offsetof(gm_tn3qmqcq7vsk9, f2) == 8 && offsetof(gm_tn3qmqcq7vsk9, f3) == 12 && offsetof(gm_tn3qmqcq7vsk9, f4) == 16 && offsetof(gm_tn3qmqcq7vsk9, f5) == 20 && offsetof(gm_tn3qmqcq7vsk9, f6) == 24, "layout of `(r: u32, levels: u32, local: u32, rel: u32, size: u32, align: u32, n: ^Node)`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_ttovhfee8mf28) == 8 && _Alignof(gm_ttovhfee8mf28) == 4 && offsetof(gm_ttovhfee8mf28, f0) == 0 && offsetof(gm_ttovhfee8mf28, f1) == 4, "layout of `(signed: bool, bits: u32)`");
typedef struct { gm_tmn7n0n9raou5 f0; uint8_t f1; uint8_t p2[3]; uint32_t f2; uint32_t f3; uint32_t f4; } gm_tufm7evi61pnh;
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[2]; gm_ttovhfee8mf28 f1; gm_ttovhfee8mf28 f2; uint32_t f3; uint32_t f4; uint32_t f5; void * f6; } gm_t06kd2nmcgb3u;
_Static_assert(sizeof(gm_t8588ma2cr9d7) == 12 && _Alignof(gm_t8588ma2cr9d7) == 4 && offsetof(gm_t8588ma2cr9d7, f0) == 0 && offsetof(gm_t8588ma2cr9d7, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_t8588ma2cr9d7 f0; gm_ttovhfee8mf28 f1; gm_ttovhfee8mf28 f2; uint32_t f3; uint32_t f4; } gm_taqa4ib9msu5h;
_Static_assert(sizeof(gm_tb1f35qilrmun) == 16 && _Alignof(gm_tb1f35qilrmun) == 4 && offsetof(gm_tb1f35qilrmun, f0) == 0 && offsetof(gm_tb1f35qilrmun, f1) == 1 && offsetof(gm_tb1f35qilrmun, f2) == 2 && offsetof(gm_tb1f35qilrmun, f3) == 4 && offsetof(gm_tb1f35qilrmun, f4) == 8 && offsetof(gm_tb1f35qilrmun, f5) == 12, "layout of `Bc`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t f1; uint8_t p2[1]; uint32_t f2; uint32_t f3; uint32_t f4; } gm_tfd1kqjeh4les;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thji9nptko1bl) == 40 && _Alignof(gm_thji9nptko1bl) == 8 && offsetof(gm_thji9nptko1bl, f0) == 0 && offsetof(gm_thji9nptko1bl, f1) == 8 && offsetof(gm_thji9nptko1bl, f2) == 16 && offsetof(gm_thji9nptko1bl, f3) == 20 && offsetof(gm_thji9nptko1bl, f4) == 24 && offsetof(gm_thji9nptko1bl, f5) == 28 && offsetof(gm_thji9nptko1bl, f6) == 32, "layout of `(t: IntW, shift: IntW, wrap: bool, r: u32, a: u32, b: u32, n: ^Node)`");
_Static_assert(sizeof(gm_tiag95i83438g) == 40 && _Alignof(gm_tiag95i83438g) == 8 && offsetof(gm_tiag95i83438g, f0) == 0 && offsetof(gm_tiag95i83438g, f1) == 8 && offsetof(gm_tiag95i83438g, f2) == 16 && offsetof(gm_tiag95i83438g, f3) == 20 && offsetof(gm_tiag95i83438g, f4) == 24 && offsetof(gm_tiag95i83438g, f5) == 32, "layout of `(t: IntW, shift: IntW, r: u32, a: u32, b: u32, n: ^Node)`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[2]; gm_tmn7n0n9raou5 f1; uint32_t f2; uint32_t f3; uint32_t f4; } gm_tnkql1rjn1vbj;
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { uint8_t f0; uint8_t f1; uint8_t p2[2]; uint32_t f2; uint32_t f3; uint32_t f4; uint32_t f5; uint8_t p6[4]; void * f6; uint64_t f7; gm_b16_8 f8; uint32_t f9; uint8_t p10[4]; uint64_t f10; void * f11; uint32_t f12; uint8_t p13[4]; gm_th2lqbo77pvsc f13; uint32_t f14; uint8_t p15[4]; void * f15; } gm_tu1mv36lldn2v;
_Static_assert(sizeof(gm_tufm7evi61pnh) == 24 && _Alignof(gm_tufm7evi61pnh) == 4 && offsetof(gm_tufm7evi61pnh, f0) == 0 && offsetof(gm_tufm7evi61pnh, f1) == 8 && offsetof(gm_tufm7evi61pnh, f2) == 12 && offsetof(gm_tufm7evi61pnh, f3) == 16 && offsetof(gm_tufm7evi61pnh, f4) == 20, "layout of `(how: WordCmp, negated: bool, r: u32, a: u32, b: u32)`");
_Static_assert(sizeof(gm_t06kd2nmcgb3u) == 40 && _Alignof(gm_t06kd2nmcgb3u) == 8 && offsetof(gm_t06kd2nmcgb3u, f0) == 0 && offsetof(gm_t06kd2nmcgb3u, f1) == 4 && offsetof(gm_t06kd2nmcgb3u, f2) == 12 && offsetof(gm_t06kd2nmcgb3u, f3) == 20 && offsetof(gm_t06kd2nmcgb3u, f4) == 24 && offsetof(gm_t06kd2nmcgb3u, f5) == 28 && offsetof(gm_t06kd2nmcgb3u, f6) == 32, "layout of `(op: BinOp, t: IntW, shift: IntW, r: u32, a: u32, b: u32, n: ^Node)`");
_Static_assert(sizeof(gm_taqa4ib9msu5h) == 36 && _Alignof(gm_taqa4ib9msu5h) == 4 && offsetof(gm_taqa4ib9msu5h, f0) == 0 && offsetof(gm_taqa4ib9msu5h, f1) == 12 && offsetof(gm_taqa4ib9msu5h, f2) == 20 && offsetof(gm_taqa4ib9msu5h, f3) == 28 && offsetof(gm_taqa4ib9msu5h, f4) == 32, "layout of `(op: Builtin, from: IntW, to: IntW, r: u32, a: u32)`");
_Static_assert(sizeof(gm_tfd1kqjeh4les) == 16 && _Alignof(gm_tfd1kqjeh4les) == 4 && offsetof(gm_tfd1kqjeh4les, f0) == 0 && offsetof(gm_tfd1kqjeh4les, f1) == 2 && offsetof(gm_tfd1kqjeh4les, f2) == 4 && offsetof(gm_tfd1kqjeh4les, f3) == 8 && offsetof(gm_tfd1kqjeh4les, f4) == 12, "layout of `(op: BinOp, wide: bool, r: u32, a: u32, b: u32)`");
_Static_assert(sizeof(gm_tnkql1rjn1vbj) == 24 && _Alignof(gm_tnkql1rjn1vbj) == 4 && offsetof(gm_tnkql1rjn1vbj, f0) == 0 && offsetof(gm_tnkql1rjn1vbj, f1) == 4 && offsetof(gm_tnkql1rjn1vbj, f2) == 12 && offsetof(gm_tnkql1rjn1vbj, f3) == 16 && offsetof(gm_tnkql1rjn1vbj, f4) == 20, "layout of `(op: BinOp, how: WordCmp, r: u32, a: u32, b: u32)`");
_Static_assert(sizeof(gm_tu1mv36lldn2v) == 128 && _Alignof(gm_tu1mv36lldn2v) == 8 && offsetof(gm_tu1mv36lldn2v, f0) == 0 && offsetof(gm_tu1mv36lldn2v, f1) == 1 && offsetof(gm_tu1mv36lldn2v, f2) == 4 && offsetof(gm_tu1mv36lldn2v, f3) == 8 && offsetof(gm_tu1mv36lldn2v, f4) == 12 && offsetof(gm_tu1mv36lldn2v, f5) == 16 && offsetof(gm_tu1mv36lldn2v, f6) == 24 && offsetof(gm_tu1mv36lldn2v, f7) == 32 && offsetof(gm_tu1mv36lldn2v, f8) == 40 && offsetof(gm_tu1mv36lldn2v, f9) == 56 && offsetof(gm_tu1mv36lldn2v, f10) == 64 && offsetof(gm_tu1mv36lldn2v, f11) == 72 && offsetof(gm_tu1mv36lldn2v, f12) == 80 && offsetof(gm_tu1mv36lldn2v, f13) == 88 && offsetof(gm_tu1mv36lldn2v, f14) == 112 && offsetof(gm_tu1mv36lldn2v, f15) == 120, "layout of `CallSite`");
struct gm_fr_bc3_m9eakek {
    gm_frame h;
    gm_tb1f35qilrmun ret;
};
struct gm_fr_bc_align_6s4vol1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_bc_at_91tc4br {
    gm_frame h;
    void * ret;
};
struct gm_fr_bc_cmp_hcolbhq {
    gm_frame h;
    gm_tb1f35qilrmun ret;
};
struct gm_fr_bc_free_v10vouf {
    gm_frame h;
    gm_tb1f35qilrmun ret;
};
struct gm_fr_bc_of_ga25123 {
    gm_frame h;
    gm_taqa4ib9msu5h l20; /* x */
    gm_t1lvkimu9u0hv l55; /* x */
    gm_thbsr19fadfjo l56; /* x */
    gm_tdi0t3ilv4peg l57; /* x */
    gm_tdqpbd1b4j0lu l58; /* x */
    gm_tiag95i83438g l60; /* x */
    gm_thji9nptko1bl l61; /* x */
    gm_t06kd2nmcgb3u l62; /* x */
    gm_td7fbab3thbb2 l63; /* x */
    gm_tb1f35qilrmun ret;
};
struct gm_fr_bc_pc_vubo137 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_empty_slice_thu1fog {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fits16_1bnmjva {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fits32_a5qt6pu {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_in_memory_t2cr4s3 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_ld1_adb1ani {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_ld2_gce8h7a {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_ld4_lj8nmpt {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_ld8_ecs41ho {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_ldn_94iknnm {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_log2_acbegp6 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_pick_kkl1a7m {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_reads_upvars_gvd5s94 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_st1_aqrj474 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_st2_lkdmkcq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_st4_38u38jn {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_st8_6aops52 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_stn_cbrtno6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_view_qalkbv1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_vm_assemble_k5irtcr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_vm_view_fo2var1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_word_size_mu6ni08 {
    gm_frame h;
    uint8_t ret;
};
inline __attribute__((aligned(4))) gm_tb1f35qilrmun gm_f_bc3_m9eakek(gm_t9qkh0rsooc8d a0, uint32_t a1, uint32_t a2, uint32_t a3);
inline __attribute__((aligned(4))) uint8_t gm_f_bc_align_6s4vol1(uint32_t a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_bc_at_91tc4br(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tb1f35qilrmun gm_f_bc_cmp_hcolbhq(gm_ts6oqppjh0ba3 a0, gm_t9qkh0rsooc8d a1, gm_t9qkh0rsooc8d a2, uint8_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint16_t a7);
inline __attribute__((aligned(4))) gm_tb1f35qilrmun gm_f_bc_free_v10vouf(void);
__attribute__((aligned(4))) gm_tb1f35qilrmun gm_f_bc_of_ga25123(void * a0, uint8_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_bc_pc_vubo137(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_thu1fog(void);
inline __attribute__((aligned(4))) uint8_t gm_f_fits16_1bnmjva(uint64_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_fits32_a5qt6pu(uint64_t a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_free_5tbvv4o(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_in_memory_t2cr4s3(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_ld1_adb1ani(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_ld2_gce8h7a(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_ld4_lj8nmpt(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_ld8_ecs41ho(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_ldn_94iknnm(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_log2_acbegp6(uint32_t a0);
__attribute__((aligned(4))) void gm_f_make_fill_0sgj1dq(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, gm_tb1f35qilrmun a1, const void * a2);
__attribute__((aligned(4))) void gm_f_make_fill_bqbf5rp(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, uint32_t a1, const void * a2);
__attribute__((aligned(4))) void gm_f_make_fill_kjs404n(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, const void * a1, const void * a2);
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_pick_kkl1a7m(uint8_t a0, gm_t9qkh0rsooc8d a1, gm_t9qkh0rsooc8d a2, gm_t9qkh0rsooc8d a3);
__attribute__((aligned(4))) uint8_t gm_f_reads_upvars_gvd5s94(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_st1_aqrj474(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_st2_lkdmkcq(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_st4_38u38jn(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_st8_6aops52(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_stn_cbrtno6(void * a0, uint8_t a1, uint64_t a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_vm_assemble_k5irtcr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_vm_view_fo2var1(void * a0, uint64_t a1, uint64_t a2, uint64_t a3, uint8_t a4);
inline __attribute__((aligned(4))) uint8_t gm_f_word_size_mu6ni08(uint64_t a0);
extern uint8_t gm_c6p4chcutca[8] __attribute__((aligned(8)));
extern uint8_t gm_cl7gtp51edk[4] __attribute__((aligned(4)));
extern uint8_t gm_k6be0mlmbp5[40] __attribute__((aligned(8)));
extern uint8_t gm_kcvb629onmb[40] __attribute__((aligned(8)));
extern uint8_t gm_knhk77btln5[40] __attribute__((aligned(8)));
extern uint8_t gm_kv3bmftl0ar[40] __attribute__((aligned(8)));
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src6unagr666v56[];
extern const char gm_srcq7ohh8h77bn7[];
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) gm_tb1f35qilrmun gm_f_bc3_m9eakek(gm_t9qkh0rsooc8d a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bc3_m9eakek F;
    gm_t9qkh0rsooc8d gm_l0; /* op */
    uint32_t gm_l1; /* a */
    uint32_t gm_l2; /* b */
    uint32_t gm_l3; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 74u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t9qkh0rsooc8d t2 = gm_l0;
    uint32_t t3 = gm_l1;
    uint32_t t4 = gm_l2;
    uint32_t t5 = gm_l3;
    gm_tb1f35qilrmun t6;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t6 + 0), t2);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 1), ((uint8_t)0ULL));
    GM_ST(uint16_t, ((uint8_t *)&t6 + 2), ((uint16_t)0ULL));
    GM_ST(uint32_t, ((uint8_t *)&t6 + 4), t3);
    GM_ST(uint32_t, ((uint8_t *)&t6 + 8), t4);
    GM_ST(uint32_t, ((uint8_t *)&t6 + 12), t5);
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_bc_align_6s4vol1(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bc_align_6s4vol1 F;
    uint32_t gm_l0; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 37u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    uint8_t gm_l2;
    uint32_t t2 = gm_l0;
    uint8_t t3 = (t2 > ((uint32_t)0ULL));
    if (t3) {
        uint32_t t4 = gm_l0;
        uint8_t t5 = (t4 <= ((uint32_t)32768ULL));
        gm_l2 = t5;
    } else {
        gm_l2 = ((uint8_t)0);
    }
    uint8_t t6 = gm_l2;
    if (t6) {
        uint32_t t7 = gm_l0;
        uint32_t t8 = gm_l0;
        uint32_t r9; bool o10 = __builtin_sub_overflow(t8, ((uint32_t)1ULL), &r9);
        if (o10) gm_trap("integer overflow in `-`", gm_src6unagr666v56, 338, 66);
        uint32_t t11 = r9;
        uint32_t t12 = (uint32_t)(t7 & t11);
        uint8_t t13 = ((t12 == ((uint32_t)0ULL)));
        gm_l1 = t13;
    } else {
        gm_l1 = ((uint8_t)0);
    }
    uint8_t t14 = gm_l1;
    F.ret = t14;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_bc_at_91tc4br(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bc_at_91tc4br F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* k */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint64_t t3 = gm_l1;
    void * t4 = (void *)(((uint8_t *)t2) + (uintptr_t)t3 * 16);
    void * t5 = t4;
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tb1f35qilrmun gm_f_bc_free_v10vouf(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bc_free_v10vouf F;
    GM_STACK_CHECK_SHALLOW(&F, 49u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t9qkh0rsooc8d t2;
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)0ULL));
    gm_tb1f35qilrmun t3;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t3 + 0), t2);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 1), ((uint8_t)0ULL));
    GM_ST(uint16_t, ((uint8_t *)&t3 + 2), ((uint16_t)0ULL));
    GM_ST(uint32_t, ((uint8_t *)&t3 + 4), ((uint32_t)0ULL));
    GM_ST(uint32_t, ((uint8_t *)&t3 + 8), ((uint32_t)0ULL));
    GM_ST(uint32_t, ((uint8_t *)&t3 + 12), ((uint32_t)0ULL));
    F.ret = t3;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_bc_pc_vubo137(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bc_pc_vubo137 F;
    void * gm_l0; /* start */
    void * gm_l1; /* p */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = (uintptr_t)t4;
    uint64_t r6; bool o7 = __builtin_sub_overflow(t3, t5, &r6);
    if (o7) gm_trap("integer overflow in `-`", gm_src6unagr666v56, 595, 50);
    uint64_t t8 = r6;
    if (((uint64_t)16ULL) == 0) gm_trap("division by zero", gm_src6unagr666v56, 595, 49);
    uint64_t t9 = (uint64_t)(t8 / ((uint64_t)16ULL));
    F.ret = t9;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_thu1fog(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_thu1fog F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_fits16_1bnmjva(uint64_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fits16_1bnmjva F;
    uint64_t gm_l0; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 19u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t2 = gm_l0;
    uint8_t t3 = (t2 <= ((uint64_t)65535ULL));
    F.ret = t3;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_fits32_a5qt6pu(uint64_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fits32_a5qt6pu F;
    uint64_t gm_l0; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 19u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t2 = gm_l0;
    uint8_t t3 = (t2 <= ((uint64_t)4294967295ULL));
    F.ret = t3;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_ld1_adb1ani(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ld1_adb1ani F;
    void * gm_l0; /* p */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)t2) + (uintptr_t)((uint64_t)0ULL) * 1));
    uint64_t t4 = (uint64_t)t3;
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_ld2_gce8h7a(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ld2_gce8h7a F;
    void * gm_l0; /* p */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)t2) + (uintptr_t)((uint64_t)0ULL) * 1));
    uint64_t t4 = (uint64_t)t3;
    void * t5 = gm_l0;
    uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)t5) + (uintptr_t)((uint64_t)1ULL) * 1));
    uint64_t t7 = (uint64_t)t6;
    unsigned __int128 n8 = (unsigned __int128)((uint64_t)8ULL);
    if (n8 >= 64) gm_trap_shift((uint64_t)n8, "u64", gm_src6unagr666v56, 261, 50);
    uint64_t r9 = (uint64_t)((uint64_t)t7 << (int)n8);
    bool o10 = (uint64_t)(r9 >> (int)n8) != t7;
    if (o10) gm_trap("integer overflow in `<<`", gm_src6unagr666v56, 261, 50);
    uint64_t t11 = r9;
    uint64_t t12 = (uint64_t)(t4 | t11);
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_ld4_lj8nmpt(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ld4_lj8nmpt F;
    void * gm_l0; /* p */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)t2) + (uintptr_t)((uint64_t)0ULL) * 1));
    uint64_t t4 = (uint64_t)t3;
    void * t5 = gm_l0;
    uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)t5) + (uintptr_t)((uint64_t)1ULL) * 1));
    uint64_t t7 = (uint64_t)t6;
    unsigned __int128 n8 = (unsigned __int128)((uint64_t)8ULL);
    if (n8 >= 64) gm_trap_shift((uint64_t)n8, "u64", gm_src6unagr666v56, 264, 50);
    uint64_t r9 = (uint64_t)((uint64_t)t7 << (int)n8);
    bool o10 = (uint64_t)(r9 >> (int)n8) != t7;
    if (o10) gm_trap("integer overflow in `<<`", gm_src6unagr666v56, 264, 50);
    uint64_t t11 = r9;
    uint64_t t12 = (uint64_t)(t4 | t11);
    void * t13 = gm_l0;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)t13) + (uintptr_t)((uint64_t)2ULL) * 1));
    uint64_t t15 = (uint64_t)t14;
    unsigned __int128 n16 = (unsigned __int128)((uint64_t)16ULL);
    if (n16 >= 64) gm_trap_shift((uint64_t)n16, "u64", gm_src6unagr666v56, 264, 69);
    uint64_t r17 = (uint64_t)((uint64_t)t15 << (int)n16);
    bool o18 = (uint64_t)(r17 >> (int)n16) != t15;
    if (o18) gm_trap("integer overflow in `<<`", gm_src6unagr666v56, 264, 69);
    uint64_t t19 = r17;
    uint64_t t20 = (uint64_t)(t12 | t19);
    void * t21 = gm_l0;
    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)t21) + (uintptr_t)((uint64_t)3ULL) * 1));
    uint64_t t23 = (uint64_t)t22;
    unsigned __int128 n24 = (unsigned __int128)((uint64_t)24ULL);
    if (n24 >= 64) gm_trap_shift((uint64_t)n24, "u64", gm_src6unagr666v56, 264, 89);
    uint64_t r25 = (uint64_t)((uint64_t)t23 << (int)n24);
    bool o26 = (uint64_t)(r25 >> (int)n24) != t23;
    if (o26) gm_trap("integer overflow in `<<`", gm_src6unagr666v56, 264, 89);
    uint64_t t27 = r25;
    uint64_t t28 = (uint64_t)(t20 | t27);
    F.ret = t28;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_ld8_ecs41ho(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ld8_ecs41ho F;
    void * gm_l0; /* p */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)t2) + (uintptr_t)((uint64_t)0ULL) * 1));
    uint64_t t4 = (uint64_t)t3;
    void * t5 = gm_l0;
    uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)t5) + (uintptr_t)((uint64_t)1ULL) * 1));
    uint64_t t7 = (uint64_t)t6;
    unsigned __int128 n8 = (unsigned __int128)((uint64_t)8ULL);
    if (n8 >= 64) gm_trap_shift((uint64_t)n8, "u64", gm_src6unagr666v56, 267, 50);
    uint64_t r9 = (uint64_t)((uint64_t)t7 << (int)n8);
    bool o10 = (uint64_t)(r9 >> (int)n8) != t7;
    if (o10) gm_trap("integer overflow in `<<`", gm_src6unagr666v56, 267, 50);
    uint64_t t11 = r9;
    uint64_t t12 = (uint64_t)(t4 | t11);
    void * t13 = gm_l0;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)t13) + (uintptr_t)((uint64_t)2ULL) * 1));
    uint64_t t15 = (uint64_t)t14;
    unsigned __int128 n16 = (unsigned __int128)((uint64_t)16ULL);
    if (n16 >= 64) gm_trap_shift((uint64_t)n16, "u64", gm_src6unagr666v56, 267, 69);
    uint64_t r17 = (uint64_t)((uint64_t)t15 << (int)n16);
    bool o18 = (uint64_t)(r17 >> (int)n16) != t15;
    if (o18) gm_trap("integer overflow in `<<`", gm_src6unagr666v56, 267, 69);
    uint64_t t19 = r17;
    uint64_t t20 = (uint64_t)(t12 | t19);
    void * t21 = gm_l0;
    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)t21) + (uintptr_t)((uint64_t)3ULL) * 1));
    uint64_t t23 = (uint64_t)t22;
    unsigned __int128 n24 = (unsigned __int128)((uint64_t)24ULL);
    if (n24 >= 64) gm_trap_shift((uint64_t)n24, "u64", gm_src6unagr666v56, 267, 89);
    uint64_t r25 = (uint64_t)((uint64_t)t23 << (int)n24);
    bool o26 = (uint64_t)(r25 >> (int)n24) != t23;
    if (o26) gm_trap("integer overflow in `<<`", gm_src6unagr666v56, 267, 89);
    uint64_t t27 = r25;
    uint64_t t28 = (uint64_t)(t20 | t27);
    void * t29 = gm_l0;
    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)t29) + (uintptr_t)((uint64_t)4ULL) * 1));
    uint64_t t31 = (uint64_t)t30;
    unsigned __int128 n32 = (unsigned __int128)((uint64_t)32ULL);
    if (n32 >= 64) gm_trap_shift((uint64_t)n32, "u64", gm_src6unagr666v56, 267, 109);
    uint64_t r33 = (uint64_t)((uint64_t)t31 << (int)n32);
    bool o34 = (uint64_t)(r33 >> (int)n32) != t31;
    if (o34) gm_trap("integer overflow in `<<`", gm_src6unagr666v56, 267, 109);
    uint64_t t35 = r33;
    uint64_t t36 = (uint64_t)(t28 | t35);
    void * t37 = gm_l0;
    uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)t37) + (uintptr_t)((uint64_t)5ULL) * 1));
    uint64_t t39 = (uint64_t)t38;
    unsigned __int128 n40 = (unsigned __int128)((uint64_t)40ULL);
    if (n40 >= 64) gm_trap_shift((uint64_t)n40, "u64", gm_src6unagr666v56, 267, 129);
    uint64_t r41 = (uint64_t)((uint64_t)t39 << (int)n40);
    bool o42 = (uint64_t)(r41 >> (int)n40) != t39;
    if (o42) gm_trap("integer overflow in `<<`", gm_src6unagr666v56, 267, 129);
    uint64_t t43 = r41;
    uint64_t t44 = (uint64_t)(t36 | t43);
    void * t45 = gm_l0;
    uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)t45) + (uintptr_t)((uint64_t)6ULL) * 1));
    uint64_t t47 = (uint64_t)t46;
    unsigned __int128 n48 = (unsigned __int128)((uint64_t)48ULL);
    if (n48 >= 64) gm_trap_shift((uint64_t)n48, "u64", gm_src6unagr666v56, 267, 149);
    uint64_t r49 = (uint64_t)((uint64_t)t47 << (int)n48);
    bool o50 = (uint64_t)(r49 >> (int)n48) != t47;
    if (o50) gm_trap("integer overflow in `<<`", gm_src6unagr666v56, 267, 149);
    uint64_t t51 = r49;
    uint64_t t52 = (uint64_t)(t44 | t51);
    void * t53 = gm_l0;
    uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)t53) + (uintptr_t)((uint64_t)7ULL) * 1));
    uint64_t t55 = (uint64_t)t54;
    unsigned __int128 n56 = (unsigned __int128)((uint64_t)56ULL);
    if (n56 >= 64) gm_trap_shift((uint64_t)n56, "u64", gm_src6unagr666v56, 267, 169);
    uint64_t r57 = (uint64_t)((uint64_t)t55 << (int)n56);
    bool o58 = (uint64_t)(r57 >> (int)n56) != t55;
    if (o58) gm_trap("integer overflow in `<<`", gm_src6unagr666v56, 267, 169);
    uint64_t t59 = r57;
    uint64_t t60 = (uint64_t)(t52 | t59);
    F.ret = t60;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_ldn_94iknnm(void * a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ldn_94iknnm F;
    void * gm_l0; /* p */
    uint8_t gm_l1; /* size */
    GM_STACK_CHECK_SHALLOW(&F, 143u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t gm_l2;
    uint64_t gm_l3;
    uint64_t gm_l4;
    uint8_t t2 = gm_l1;
    uint8_t t3 = ((t2 == ((uint8_t)8ULL)));
    if (t3) {
        void * t4 = gm_l0;
        uint64_t t5 = gm_f_ld8_ecs41ho(t4);
        gm_l2 = t5;
    } else {
        uint8_t t6 = gm_l1;
        uint8_t t7 = ((t6 == ((uint8_t)4ULL)));
        if (t7) {
            void * t8 = gm_l0;
            uint64_t t9 = gm_f_ld4_lj8nmpt(t8);
            gm_l3 = t9;
        } else {
            uint8_t t10 = gm_l1;
            uint8_t t11 = ((t10 == ((uint8_t)1ULL)));
            if (t11) {
                void * t12 = gm_l0;
                uint64_t t13 = gm_f_ld1_adb1ani(t12);
                gm_l4 = t13;
            } else {
                void * t14 = gm_l0;
                uint64_t t15 = gm_f_ld2_gce8h7a(t14);
                gm_l4 = t15;
            }
            uint64_t t16 = gm_l4;
            gm_l3 = t16;
        }
        uint64_t t17 = gm_l3;
        gm_l2 = t17;
    }
    uint64_t t18 = gm_l2;
    F.ret = t18;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_log2_acbegp6(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_log2_acbegp6 F;
    uint32_t gm_l0; /* v */
    uint8_t gm_l1; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = ((uint8_t)0ULL);
    {
        for (;;) {
            uint8_t t5 = gm_l1;
            uint32_t t6 = (uint32_t)t5;
            unsigned __int128 n7 = (unsigned __int128)t6;
            if (n7 >= 32) gm_trap_shift((uint64_t)n7, "u32", gm_src6unagr666v56, 319, 12);
            uint32_t r8 = (uint32_t)((uint32_t)((uint32_t)1ULL) << (int)n7);
            bool o9 = (uint32_t)(r8 >> (int)n7) != ((uint32_t)1ULL);
            if (o9) gm_trap("integer overflow in `<<`", gm_src6unagr666v56, 319, 12);
            uint32_t t10 = r8;
            uint32_t t11 = gm_l0;
            uint8_t t12 = (t10 < t11);
            if (!t12) {
                goto gm_lb1;
            }
            {
                uint8_t t13 = gm_l1;
                uint8_t r14; bool o15 = __builtin_add_overflow(t13, ((uint8_t)1ULL), &r14);
                if (o15) gm_trap("integer overflow in `+`", gm_src6unagr666v56, 319, 36);
                uint8_t t16 = r14;
                gm_l1 = t16;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    uint8_t t17 = gm_l1;
    F.ret = t17;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_pick_kkl1a7m(uint8_t a0, gm_t9qkh0rsooc8d a1, gm_t9qkh0rsooc8d a2, gm_t9qkh0rsooc8d a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pick_kkl1a7m F;
    uint8_t gm_l0; /* op */
    gm_t9qkh0rsooc8d gm_l1; /* add */
    gm_t9qkh0rsooc8d gm_l2; /* sub */
    gm_t9qkh0rsooc8d gm_l3; /* mul */
    GM_STACK_CHECK_SHALLOW(&F, 17u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t9qkh0rsooc8d gm_l4;
    gm_t9qkh0rsooc8d gm_l5;
    uint8_t t2 = gm_l0;
    uint8_t t3 = ((t2 == ((uint8_t)0ULL)));
    if (t3) {
        gm_t9qkh0rsooc8d t4 = gm_l1;
        gm_l4 = t4;
    } else {
        uint8_t t5 = gm_l0;
        uint8_t t6 = ((t5 == ((uint8_t)1ULL)));
        if (t6) {
            gm_t9qkh0rsooc8d t7 = gm_l2;
            gm_l5 = t7;
        } else {
            gm_t9qkh0rsooc8d t8 = gm_l3;
            gm_l5 = t8;
        }
        gm_t9qkh0rsooc8d t9 = gm_l5;
        gm_l4 = t9;
    }
    gm_t9qkh0rsooc8d t10 = gm_l4;
    F.ret = t10;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_st1_aqrj474(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_st1_aqrj474 F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* v */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint64_t t3 = gm_l1;
    uint8_t t4 = (uint8_t)t3;
    GM_ST(uint8_t, (((uint8_t *)t2) + (uintptr_t)((uint64_t)0ULL) * 1), t4);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_st2_lkdmkcq(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_st2_lkdmkcq F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* v */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint64_t t3 = gm_l1;
    uint8_t t4 = (uint8_t)t3;
    GM_ST(uint8_t, (((uint8_t *)t2) + (uintptr_t)((uint64_t)0ULL) * 1), t4);
    void * t5 = gm_l0;
    uint64_t t6 = gm_l1;
    unsigned __int128 n7 = (unsigned __int128)((uint64_t)8ULL);
    if (n7 >= 64) gm_trap_shift((uint64_t)n7, "u64", gm_src6unagr666v56, 275, 13);
    uint64_t t8 = (uint64_t)(t6 >> (int)n7);
    uint8_t t9 = (uint8_t)t8;
    GM_ST(uint8_t, (((uint8_t *)t5) + (uintptr_t)((uint64_t)1ULL) * 1), t9);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_st4_38u38jn(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_st4_38u38jn F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* v */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint64_t t3 = gm_l1;
    uint8_t t4 = (uint8_t)t3;
    GM_ST(uint8_t, (((uint8_t *)t2) + (uintptr_t)((uint64_t)0ULL) * 1), t4);
    void * t5 = gm_l0;
    uint64_t t6 = gm_l1;
    unsigned __int128 n7 = (unsigned __int128)((uint64_t)8ULL);
    if (n7 >= 64) gm_trap_shift((uint64_t)n7, "u64", gm_src6unagr666v56, 281, 13);
    uint64_t t8 = (uint64_t)(t6 >> (int)n7);
    uint8_t t9 = (uint8_t)t8;
    GM_ST(uint8_t, (((uint8_t *)t5) + (uintptr_t)((uint64_t)1ULL) * 1), t9);
    void * t10 = gm_l0;
    uint64_t t11 = gm_l1;
    unsigned __int128 n12 = (unsigned __int128)((uint64_t)16ULL);
    if (n12 >= 64) gm_trap_shift((uint64_t)n12, "u64", gm_src6unagr666v56, 282, 13);
    uint64_t t13 = (uint64_t)(t11 >> (int)n12);
    uint8_t t14 = (uint8_t)t13;
    GM_ST(uint8_t, (((uint8_t *)t10) + (uintptr_t)((uint64_t)2ULL) * 1), t14);
    void * t15 = gm_l0;
    uint64_t t16 = gm_l1;
    unsigned __int128 n17 = (unsigned __int128)((uint64_t)24ULL);
    if (n17 >= 64) gm_trap_shift((uint64_t)n17, "u64", gm_src6unagr666v56, 283, 13);
    uint64_t t18 = (uint64_t)(t16 >> (int)n17);
    uint8_t t19 = (uint8_t)t18;
    GM_ST(uint8_t, (((uint8_t *)t15) + (uintptr_t)((uint64_t)3ULL) * 1), t19);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_st8_6aops52(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_st8_6aops52 F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* v */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint64_t t3 = gm_l1;
    uint8_t t4 = (uint8_t)t3;
    GM_ST(uint8_t, (((uint8_t *)t2) + (uintptr_t)((uint64_t)0ULL) * 1), t4);
    void * t5 = gm_l0;
    uint64_t t6 = gm_l1;
    unsigned __int128 n7 = (unsigned __int128)((uint64_t)8ULL);
    if (n7 >= 64) gm_trap_shift((uint64_t)n7, "u64", gm_src6unagr666v56, 289, 13);
    uint64_t t8 = (uint64_t)(t6 >> (int)n7);
    uint8_t t9 = (uint8_t)t8;
    GM_ST(uint8_t, (((uint8_t *)t5) + (uintptr_t)((uint64_t)1ULL) * 1), t9);
    void * t10 = gm_l0;
    uint64_t t11 = gm_l1;
    unsigned __int128 n12 = (unsigned __int128)((uint64_t)16ULL);
    if (n12 >= 64) gm_trap_shift((uint64_t)n12, "u64", gm_src6unagr666v56, 290, 13);
    uint64_t t13 = (uint64_t)(t11 >> (int)n12);
    uint8_t t14 = (uint8_t)t13;
    GM_ST(uint8_t, (((uint8_t *)t10) + (uintptr_t)((uint64_t)2ULL) * 1), t14);
    void * t15 = gm_l0;
    uint64_t t16 = gm_l1;
    unsigned __int128 n17 = (unsigned __int128)((uint64_t)24ULL);
    if (n17 >= 64) gm_trap_shift((uint64_t)n17, "u64", gm_src6unagr666v56, 291, 13);
    uint64_t t18 = (uint64_t)(t16 >> (int)n17);
    uint8_t t19 = (uint8_t)t18;
    GM_ST(uint8_t, (((uint8_t *)t15) + (uintptr_t)((uint64_t)3ULL) * 1), t19);
    void * t20 = gm_l0;
    uint64_t t21 = gm_l1;
    unsigned __int128 n22 = (unsigned __int128)((uint64_t)32ULL);
    if (n22 >= 64) gm_trap_shift((uint64_t)n22, "u64", gm_src6unagr666v56, 292, 13);
    uint64_t t23 = (uint64_t)(t21 >> (int)n22);
    uint8_t t24 = (uint8_t)t23;
    GM_ST(uint8_t, (((uint8_t *)t20) + (uintptr_t)((uint64_t)4ULL) * 1), t24);
    void * t25 = gm_l0;
    uint64_t t26 = gm_l1;
    unsigned __int128 n27 = (unsigned __int128)((uint64_t)40ULL);
    if (n27 >= 64) gm_trap_shift((uint64_t)n27, "u64", gm_src6unagr666v56, 293, 13);
    uint64_t t28 = (uint64_t)(t26 >> (int)n27);
    uint8_t t29 = (uint8_t)t28;
    GM_ST(uint8_t, (((uint8_t *)t25) + (uintptr_t)((uint64_t)5ULL) * 1), t29);
    void * t30 = gm_l0;
    uint64_t t31 = gm_l1;
    unsigned __int128 n32 = (unsigned __int128)((uint64_t)48ULL);
    if (n32 >= 64) gm_trap_shift((uint64_t)n32, "u64", gm_src6unagr666v56, 294, 13);
    uint64_t t33 = (uint64_t)(t31 >> (int)n32);
    uint8_t t34 = (uint8_t)t33;
    GM_ST(uint8_t, (((uint8_t *)t30) + (uintptr_t)((uint64_t)6ULL) * 1), t34);
    void * t35 = gm_l0;
    uint64_t t36 = gm_l1;
    unsigned __int128 n37 = (unsigned __int128)((uint64_t)56ULL);
    if (n37 >= 64) gm_trap_shift((uint64_t)n37, "u64", gm_src6unagr666v56, 295, 13);
    uint64_t t38 = (uint64_t)(t36 >> (int)n37);
    uint8_t t39 = (uint8_t)t38;
    GM_ST(uint8_t, (((uint8_t *)t35) + (uintptr_t)((uint64_t)7ULL) * 1), t39);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_stn_cbrtno6(void * a0, uint8_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_stn_cbrtno6 F;
    void * gm_l0; /* p */
    uint8_t gm_l1; /* size */
    uint64_t gm_l2; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t t2 = gm_l1;
    uint8_t t3 = ((t2 == ((uint8_t)8ULL)));
    if (t3) {
        void * t4 = gm_l0;
        uint64_t t5 = gm_l2;
        gm_f_st8_6aops52(t4, t5);
    } else {
        uint8_t t6 = gm_l1;
        uint8_t t7 = ((t6 == ((uint8_t)4ULL)));
        if (t7) {
            void * t8 = gm_l0;
            uint64_t t9 = gm_l2;
            gm_f_st4_38u38jn(t8, t9);
        } else {
            uint8_t t10 = gm_l1;
            uint8_t t11 = ((t10 == ((uint8_t)1ULL)));
            if (t11) {
                void * t12 = gm_l0;
                uint64_t t13 = gm_l2;
                gm_f_st1_aqrj474(t12, t13);
            } else {
                void * t14 = gm_l0;
                uint64_t t15 = gm_l2;
                gm_f_st2_lkdmkcq(t14, t15);
            }
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_vm_view_fo2var1(void * a0, uint64_t a1, uint64_t a2, uint64_t a3, uint8_t a4) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_vm_view_fo2var1 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* size */
    uint64_t gm_l3; /* align */
    uint8_t gm_l4; /* write */
    GM_STACK_CHECK_SHALLOW(&F, 123u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    uint64_t t2 = gm_l1;
    uint64_t t3 = gm_l3;
    uint64_t r4; bool o5 = __builtin_sub_overflow(t3, ((uint64_t)1ULL), &r4);
    if (o5) gm_trap("integer overflow in `-`", gm_src6unagr666v56, 312, 16);
    uint64_t t6 = r4;
    uint64_t t7 = (uint64_t)(t2 & t6);
    uint8_t t8 = (!(t7 == ((uint64_t)0ULL)));
    if (t8) {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    void * t9 = gm_l0;
    void * t10 = (void *)(((uint8_t *)t9) + 184);
    uint64_t t11 = gm_l1;
    uint64_t t12 = gm_l2;
    uint8_t t13 = gm_l4;
    void * t14 = gm_f_view_qalkbv1(t10, t11, t12, t13);
    F.ret = t14;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_word_size_mu6ni08(uint64_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_word_size_mu6ni08 F;
    uint64_t gm_l0; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 52u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    uint8_t gm_l2;
    uint8_t gm_l3;
    uint64_t t2 = gm_l0;
    uint8_t t3 = ((t2 == ((uint64_t)1ULL)));
    if (t3) {
        gm_l3 = ((uint8_t)1);
    } else {
        uint64_t t4 = gm_l0;
        uint8_t t5 = ((t4 == ((uint64_t)2ULL)));
        gm_l3 = t5;
    }
    uint8_t t6 = gm_l3;
    if (t6) {
        gm_l2 = ((uint8_t)1);
    } else {
        uint64_t t7 = gm_l0;
        uint8_t t8 = ((t7 == ((uint64_t)4ULL)));
        gm_l2 = t8;
    }
    uint8_t t9 = gm_l2;
    if (t9) {
        gm_l1 = ((uint8_t)1);
    } else {
        uint64_t t10 = gm_l0;
        uint8_t t11 = ((t10 == ((uint64_t)8ULL)));
        gm_l1 = t11;
    }
    uint8_t t12 = gm_l1;
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_tb1f35qilrmun gm_f_bc3_m9eakek(gm_t9qkh0rsooc8d a0, uint32_t a1, uint32_t a2, uint32_t a3);
extern __attribute__((aligned(4))) uint8_t gm_f_bc_align_6s4vol1(uint32_t a0);
extern __attribute__((aligned(4))) void * gm_f_bc_at_91tc4br(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_tb1f35qilrmun gm_f_bc_cmp_hcolbhq(gm_ts6oqppjh0ba3 a0, gm_t9qkh0rsooc8d a1, gm_t9qkh0rsooc8d a2, uint8_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint16_t a7) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bc_cmp_hcolbhq F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    gm_t9qkh0rsooc8d gm_l1; /* lt */
    gm_t9qkh0rsooc8d gm_l2; /* le */
    uint8_t gm_l3; /* jump */
    uint32_t gm_l4; /* a */
    uint32_t gm_l5; /* b */
    uint32_t gm_l6; /* r */
    uint16_t gm_l7; /* w */
    uint8_t gm_l8; /* swap */
    gm_t9qkh0rsooc8d gm_l9; /* which */
    uint32_t gm_l10; /* x */
    uint32_t gm_l11; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 172u);
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
    uint8_t gm_l12;
    uint8_t gm_l13;
    gm_ts6oqppjh0ba3 t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)&t2) + 0));
    uint8_t t4 = (uint8_t)((t3 == ((uint8_t)12ULL)));
    gm_ts6oqppjh0ba3 t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)12ULL));
    if (t4) {
        gm_l12 = ((uint8_t)1);
    } else {
        gm_ts6oqppjh0ba3 t6 = gm_l0;
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        uint8_t t8 = (uint8_t)((t7 == ((uint8_t)13ULL)));
        gm_ts6oqppjh0ba3 t9;
        GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)13ULL));
        gm_l12 = t8;
    }
    uint8_t t10 = gm_l12;
    gm_l8 = t10;
    gm_ts6oqppjh0ba3 t11 = gm_l0;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t11) + 0));
    uint8_t t13 = (uint8_t)((t12 == ((uint8_t)10ULL)));
    gm_ts6oqppjh0ba3 t14;
    GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)10ULL));
    if (t13) {
        gm_l13 = ((uint8_t)1);
    } else {
        gm_ts6oqppjh0ba3 t15 = gm_l0;
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t15) + 0));
        uint8_t t17 = (uint8_t)((t16 == ((uint8_t)12ULL)));
        gm_ts6oqppjh0ba3 t18;
        GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)12ULL));
        gm_l13 = t17;
    }
    uint8_t t19 = gm_l13;
    if (t19) {
        gm_t9qkh0rsooc8d t20 = gm_l1;
        gm_l9 = t20;
    } else {
        gm_t9qkh0rsooc8d t21 = gm_l2;
        gm_l9 = t21;
    }
    uint8_t t22 = gm_l8;
    if (t22) {
        uint32_t t23 = gm_l5;
        gm_l10 = t23;
    } else {
        uint32_t t24 = gm_l4;
        gm_l10 = t24;
    }
    uint8_t t25 = gm_l8;
    if (t25) {
        uint32_t t26 = gm_l4;
        gm_l11 = t26;
    } else {
        uint32_t t27 = gm_l5;
        gm_l11 = t27;
    }
    uint8_t t28 = gm_l3;
    if (t28) {
        gm_t9qkh0rsooc8d t29 = gm_l9;
        uint16_t t30 = gm_l7;
        uint32_t t31 = gm_l10;
        uint32_t t32 = gm_l11;
        uint32_t t33 = gm_l6;
        gm_tb1f35qilrmun t34;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t34 + 0), t29);
        GM_ST(uint8_t, ((uint8_t *)&t34 + 1), ((uint8_t)0ULL));
        GM_ST(uint16_t, ((uint8_t *)&t34 + 2), t30);
        GM_ST(uint32_t, ((uint8_t *)&t34 + 4), t31);
        GM_ST(uint32_t, ((uint8_t *)&t34 + 8), t32);
        GM_ST(uint32_t, ((uint8_t *)&t34 + 12), t33);
        F.ret = t34;
        goto gm_fret;
    }
    gm_t9qkh0rsooc8d t35 = gm_l9;
    uint16_t t36 = gm_l7;
    uint32_t t37 = gm_l6;
    uint32_t t38 = gm_l10;
    uint32_t t39 = gm_l11;
    gm_tb1f35qilrmun t40;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t40 + 0), t35);
    GM_ST(uint8_t, ((uint8_t *)&t40 + 1), ((uint8_t)0ULL));
    GM_ST(uint16_t, ((uint8_t *)&t40 + 2), t36);
    GM_ST(uint32_t, ((uint8_t *)&t40 + 4), t37);
    GM_ST(uint32_t, ((uint8_t *)&t40 + 8), t38);
    GM_ST(uint32_t, ((uint8_t *)&t40 + 12), t39);
    F.ret = t40;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_tb1f35qilrmun gm_f_bc_free_v10vouf(void);
__attribute__((aligned(4))) gm_tb1f35qilrmun gm_f_bc_of_ga25123(void * a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bc_of_ga25123 F;
    void * gm_l0; /* i */
    uint8_t gm_l1; /* words */
    uint32_t gm_l2; /* r */
    uint64_t gm_l3; /* v */
    uint32_t gm_l4; /* r */
    uint32_t gm_l5; /* x */
    gm_ti4ei62u4i1d9 gm_l6; /* x */
    gm_t9qkh0rsooc8d gm_l7; /* op */
    gm_te4o1hd8337m3 gm_l8; /* x */
    gm_t9qkh0rsooc8d gm_l9; /* op */
    gm_tfd1kqjeh4les gm_l10; /* x */
    gm_t9qkh0rsooc8d gm_l11; /* op */
    uint8_t gm_l12; /* op */
    uint32_t gm_l13; /* r */
    uint32_t gm_l14; /* a */
    uint32_t gm_l15; /* b */
    uint32_t gm_l16; /* r */
    uint64_t gm_l17; /* k */
    uint32_t gm_l18; /* r */
    uint32_t gm_l19; /* x */
    gm_tnkql1rjn1vbj gm_l21; /* x */
    uint32_t gm_l22; /* bits */
    gm_tnkql1rjn1vbj gm_l23; /* x */
    uint32_t gm_l24; /* bits */
    gm_tufm7evi61pnh gm_l25; /* x */
    gm_tufm7evi61pnh gm_l26; /* x */
    uint32_t gm_l27; /* to */
    uint32_t gm_l28; /* r */
    uint32_t gm_l29; /* to */
    uint32_t gm_l30; /* r */
    uint32_t gm_l31; /* to */
    uint32_t gm_l32; /* to */
    uint32_t gm_l33; /* r */
    uint32_t gm_l34; /* at */
    uint32_t gm_l35; /* size */
    uint32_t gm_l36; /* r */
    uint32_t gm_l37; /* at */
    uint32_t gm_l38; /* size */
    uint32_t gm_l39; /* at */
    uint32_t gm_l40; /* size */
    uint32_t gm_l41; /* v */
    uint32_t gm_l42; /* at */
    uint32_t gm_l43; /* size */
    uint32_t gm_l44; /* r */
    uint32_t gm_l45; /* r */
    uint32_t gm_l46; /* at */
    uint32_t gm_l47; /* d */
    uint32_t gm_l48; /* size */
    uint32_t gm_l49; /* at */
    uint32_t gm_l50; /* d */
    uint32_t gm_l51; /* size */
    uint32_t gm_l52; /* v */
    gm_t04llc5uen0qo gm_l53; /* x */
    gm_t04llc5uen0qo gm_l54; /* x */
    gm_t4h1llua034m2 gm_l59; /* x */
    uint8_t gm_l64; /* flags */
    uint32_t gm_l65; /* ri */
    uint32_t gm_l66; /* len */
    gm_t6cngq8lhim84 gm_l67; /* x */
    uint32_t gm_l68; /* r */
    uint64_t gm_l69; /* align */
    gm_t04llc5uen0qo gm_l70; /* x */
    gm_t04llc5uen0qo gm_l71; /* x */
    uint32_t gm_l72; /* d */
    uint32_t gm_l73; /* src */
    uint32_t gm_l74; /* size */
    uint32_t gm_l75; /* at */
    uint32_t gm_l76; /* size */
    gm_tn3qmqcq7vsk9 gm_l77; /* x */
    gm_tn3qmqcq7vsk9 gm_l78; /* x */
    uint32_t gm_l79; /* r */
    uint32_t gm_l80; /* at */
    uint32_t gm_l81; /* size */
    GM_STACK_CHECK_SHALLOW(&F, 4230u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t9qkh0rsooc8d gm_l82;
    gm_t9qkh0rsooc8d gm_l83;
    gm_t9qkh0rsooc8d gm_l84;
    gm_t9qkh0rsooc8d gm_l85;
    gm_t9qkh0rsooc8d gm_l86;
    gm_t9qkh0rsooc8d gm_l87;
    uint8_t gm_l88;
    gm_t9qkh0rsooc8d gm_l89;
    gm_t9qkh0rsooc8d gm_l90;
    gm_t9qkh0rsooc8d gm_l91;
    uint8_t gm_l92;
    gm_t9qkh0rsooc8d gm_l93;
    gm_t9qkh0rsooc8d gm_l94;
    gm_t9qkh0rsooc8d gm_l95;
    uint8_t gm_l96;
    gm_t9qkh0rsooc8d gm_l97;
    gm_t9qkh0rsooc8d gm_l98;
    gm_t9qkh0rsooc8d gm_l99;
    uint8_t gm_l100;
    gm_t9qkh0rsooc8d gm_l101;
    gm_t9qkh0rsooc8d gm_l102;
    gm_t9qkh0rsooc8d gm_l103;
    uint8_t gm_l104;
    uint8_t gm_l105;
    uint8_t gm_l106;
    uint8_t gm_l107;
    gm_t9qkh0rsooc8d gm_l108;
    gm_t9qkh0rsooc8d gm_l109;
    gm_t9qkh0rsooc8d gm_l110;
    uint8_t gm_l111;
    uint8_t gm_l112;
    gm_t9qkh0rsooc8d gm_l113;
    gm_t9qkh0rsooc8d gm_l114;
    gm_t9qkh0rsooc8d gm_l115;
    uint8_t gm_l116;
    uint8_t gm_l117;
    uint8_t gm_l118;
    uint8_t gm_l119;
    uint8_t gm_l120;
    uint8_t gm_l121;
    uint8_t gm_l122;
    uint8_t gm_l123;
    uint8_t gm_l124;
    uint8_t gm_l125;
    uint8_t gm_l126;
    uint8_t gm_l127;
    uint8_t gm_l128;
    uint8_t gm_l129;
    gm_t9qkh0rsooc8d gm_l130;
    gm_t9qkh0rsooc8d gm_l131;
    gm_t9qkh0rsooc8d gm_l132;
    gm_t9qkh0rsooc8d gm_l133;
    uint8_t gm_l134;
    uint8_t gm_l135;
    uint8_t gm_l136;
    uint8_t gm_l137;
    uint8_t gm_l138;
    uint8_t gm_l139;
    uint8_t gm_l140;
    uint8_t gm_l141;
    uint8_t gm_l142;
    uint32_t gm_l143;
    uint8_t gm_l144;
    uint8_t gm_l145;
    uint8_t gm_l146;
    uint8_t gm_l147;
    uint8_t gm_l148;
    uint8_t gm_l149;
    uint8_t gm_l150;
    uint8_t gm_l151;
    {
        void * t100 = gm_l0;
        {
            {
                {
                    uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t102 = (uint8_t)(t101 == ((uint8_t)0ULL));
                    if (!t102) {
                        goto gm_lb2;
                    }
                    uint32_t t103 = GM_LD(uint32_t, (((uint8_t *)t100) + 8));
                    gm_l2 = t103;
                    uint64_t t104 = GM_LD(uint64_t, ((((uint8_t *)t100) + 8) + 8));
                    gm_l3 = t104;
                    uint32_t t105 = gm_l2;
                    uint64_t t106 = gm_l3;
                    uint64_t t107 = (uint64_t)(t106 & ((uint64_t)4294967295ULL));
                    gm_t9qkh0rsooc8d t108;
                    GM_ST(uint8_t, ((uint8_t *)&t108 + 0), ((uint8_t)3ULL));
                    uint32_t t109 = (uint32_t)t107;
                    uint64_t t110 = gm_l3;
                    unsigned __int128 n111 = (unsigned __int128)((uint64_t)32ULL);
                    if (n111 >= 64) gm_trap_shift((uint64_t)n111, "u64", gm_src6unagr666v56, 356, 74);
                    uint64_t t112 = (uint64_t)(t110 >> (int)n111);
                    uint32_t t113 = (uint32_t)t112;
                    gm_tb1f35qilrmun t114 = gm_f_bc3_m9eakek(t108, t105, t109, t113);
                    F.ret = t114;
                    goto gm_fret;
                }
gm_lb2:;
                {
                    uint8_t t115 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t116 = (uint8_t)(t115 == ((uint8_t)66ULL));
                    if (!t116) {
                        goto gm_lb3;
                    }
                    uint32_t t117 = GM_LD(uint32_t, (((uint8_t *)t100) + 8));
                    gm_l4 = t117;
                    uint32_t t118 = GM_LD(uint32_t, ((((uint8_t *)t100) + 8) + 4));
                    gm_l5 = t118;
                    uint32_t t119 = gm_l4;
                    uint32_t t120 = gm_l5;
                    gm_t9qkh0rsooc8d t121;
                    GM_ST(uint8_t, ((uint8_t *)&t121 + 0), ((uint8_t)2ULL));
                    gm_tb1f35qilrmun t122 = gm_f_bc3_m9eakek(t121, t119, t120, ((uint32_t)0ULL));
                    F.ret = t122;
                    goto gm_fret;
                }
gm_lb3:;
                {
                    uint8_t t123 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t124 = (uint8_t)(t123 == ((uint8_t)21ULL));
                    if (!t124) {
                        goto gm_lb4;
                    }
                    memmove(((uint8_t *)&gm_l6), (((uint8_t *)t100) + 8), 24);
                    uint8_t t125 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 2));
                    if (t125) {
                        uint8_t t126 = GM_LD(uint8_t, ((uint8_t *)&gm_l6));
                        gm_t9qkh0rsooc8d t127;
                        GM_ST(uint8_t, ((uint8_t *)&t127 + 0), ((uint8_t)10ULL));
                        gm_t9qkh0rsooc8d t128;
                        GM_ST(uint8_t, ((uint8_t *)&t128 + 0), ((uint8_t)11ULL));
                        gm_t9qkh0rsooc8d t129;
                        GM_ST(uint8_t, ((uint8_t *)&t129 + 0), ((uint8_t)12ULL));
                        gm_t9qkh0rsooc8d t130 = gm_f_pick_kkl1a7m(t126, t127, t128, t129);
                        gm_l7 = t130;
                    } else {
                        uint8_t t131 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 1));
                        if (t131) {
                            uint8_t t132 = GM_LD(uint8_t, ((uint8_t *)&gm_l6));
                            gm_t9qkh0rsooc8d t133;
                            GM_ST(uint8_t, ((uint8_t *)&t133 + 0), ((uint8_t)4ULL));
                            gm_t9qkh0rsooc8d t134;
                            GM_ST(uint8_t, ((uint8_t *)&t134 + 0), ((uint8_t)5ULL));
                            gm_t9qkh0rsooc8d t135;
                            GM_ST(uint8_t, ((uint8_t *)&t135 + 0), ((uint8_t)6ULL));
                            gm_t9qkh0rsooc8d t136 = gm_f_pick_kkl1a7m(t132, t133, t134, t135);
                            gm_l82 = t136;
                        } else {
                            uint8_t t137 = GM_LD(uint8_t, ((uint8_t *)&gm_l6));
                            gm_t9qkh0rsooc8d t138;
                            GM_ST(uint8_t, ((uint8_t *)&t138 + 0), ((uint8_t)7ULL));
                            gm_t9qkh0rsooc8d t139;
                            GM_ST(uint8_t, ((uint8_t *)&t139 + 0), ((uint8_t)8ULL));
                            gm_t9qkh0rsooc8d t140;
                            GM_ST(uint8_t, ((uint8_t *)&t140 + 0), ((uint8_t)9ULL));
                            gm_t9qkh0rsooc8d t141 = gm_f_pick_kkl1a7m(t137, t138, t139, t140);
                            gm_l82 = t141;
                        }
                        gm_t9qkh0rsooc8d t142 = gm_l82;
                        gm_l7 = t142;
                    }
                    gm_t9qkh0rsooc8d t143 = gm_l7;
                    uint32_t t144 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
                    uint32_t t145 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 8));
                    uint32_t t146 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 12));
                    gm_tb1f35qilrmun t147 = gm_f_bc3_m9eakek(t143, t144, t145, t146);
                    F.ret = t147;
                    goto gm_fret;
                }
gm_lb4:;
            }
            {
                {
                    uint8_t t148 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t149 = (uint8_t)(t148 == ((uint8_t)22ULL));
                    if (!t149) {
                        goto gm_lb5;
                    }
                    memmove(((uint8_t *)&gm_l8), (((uint8_t *)t100) + 8), 32);
                    uint8_t t150 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 2));
                    if (t150) {
                        uint8_t t151 = GM_LD(uint8_t, ((uint8_t *)&gm_l8));
                        gm_t9qkh0rsooc8d t152;
                        GM_ST(uint8_t, ((uint8_t *)&t152 + 0), ((uint8_t)19ULL));
                        gm_t9qkh0rsooc8d t153;
                        GM_ST(uint8_t, ((uint8_t *)&t153 + 0), ((uint8_t)20ULL));
                        gm_t9qkh0rsooc8d t154;
                        GM_ST(uint8_t, ((uint8_t *)&t154 + 0), ((uint8_t)21ULL));
                        gm_t9qkh0rsooc8d t155 = gm_f_pick_kkl1a7m(t151, t152, t153, t154);
                        gm_l9 = t155;
                    } else {
                        uint8_t t156 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 1));
                        if (t156) {
                            uint8_t t157 = GM_LD(uint8_t, ((uint8_t *)&gm_l8));
                            gm_t9qkh0rsooc8d t158;
                            GM_ST(uint8_t, ((uint8_t *)&t158 + 0), ((uint8_t)13ULL));
                            gm_t9qkh0rsooc8d t159;
                            GM_ST(uint8_t, ((uint8_t *)&t159 + 0), ((uint8_t)14ULL));
                            gm_t9qkh0rsooc8d t160;
                            GM_ST(uint8_t, ((uint8_t *)&t160 + 0), ((uint8_t)15ULL));
                            gm_t9qkh0rsooc8d t161 = gm_f_pick_kkl1a7m(t157, t158, t159, t160);
                            gm_l83 = t161;
                        } else {
                            uint8_t t162 = GM_LD(uint8_t, ((uint8_t *)&gm_l8));
                            gm_t9qkh0rsooc8d t163;
                            GM_ST(uint8_t, ((uint8_t *)&t163 + 0), ((uint8_t)16ULL));
                            gm_t9qkh0rsooc8d t164;
                            GM_ST(uint8_t, ((uint8_t *)&t164 + 0), ((uint8_t)17ULL));
                            gm_t9qkh0rsooc8d t165;
                            GM_ST(uint8_t, ((uint8_t *)&t165 + 0), ((uint8_t)18ULL));
                            gm_t9qkh0rsooc8d t166 = gm_f_pick_kkl1a7m(t162, t163, t164, t165);
                            gm_l83 = t166;
                        }
                        gm_t9qkh0rsooc8d t167 = gm_l83;
                        gm_l9 = t167;
                    }
                    gm_t9qkh0rsooc8d t168 = gm_l9;
                    uint32_t t169 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 4));
                    uint16_t t170 = (uint16_t)t169;
                    uint32_t t171 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 8));
                    uint32_t t172 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 12));
                    uint32_t t173 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 16));
                    gm_tb1f35qilrmun t174;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t174 + 0), t168);
                    GM_ST(uint8_t, ((uint8_t *)&t174 + 1), ((uint8_t)0ULL));
                    GM_ST(uint16_t, ((uint8_t *)&t174 + 2), t170);
                    GM_ST(uint32_t, ((uint8_t *)&t174 + 4), t171);
                    GM_ST(uint32_t, ((uint8_t *)&t174 + 8), t172);
                    GM_ST(uint32_t, ((uint8_t *)&t174 + 12), t173);
                    F.ret = t174;
                    goto gm_fret;
                }
gm_lb5:;
                {
                    uint8_t t175 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t176 = (uint8_t)(t175 == ((uint8_t)32ULL));
                    if (!t176) {
                        goto gm_lb6;
                    }
                    gm_tfd1kqjeh4les t177 = GM_LD(gm_tfd1kqjeh4les, (((uint8_t *)t100) + 8));
                    gm_l10 = t177;
                    uint8_t t178 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 2));
                    uint8_t t179 = (uint8_t)!t178;
                    if (t179) {
                        gm_tb1f35qilrmun t180 = gm_f_bc_free_v10vouf();
                        F.ret = t180;
                        goto gm_fret;
                    }
                    {
                        {
                            uint8_t t181 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                            uint8_t t182 = (uint8_t)(t181 == ((uint8_t)0ULL));
                            if (!t182) {
                                goto gm_lb8;
                            }
                            gm_t9qkh0rsooc8d t183;
                            GM_ST(uint8_t, ((uint8_t *)&t183 + 0), ((uint8_t)22ULL));
                            gm_l11 = t183;
                            goto gm_lb7;
                        }
gm_lb8:;
                        {
                            uint8_t t184 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                            uint8_t t185 = (uint8_t)(t184 == ((uint8_t)1ULL));
                            if (!t185) {
                                goto gm_lb9;
                            }
                            gm_t9qkh0rsooc8d t186;
                            GM_ST(uint8_t, ((uint8_t *)&t186 + 0), ((uint8_t)23ULL));
                            gm_l11 = t186;
                            goto gm_lb7;
                        }
gm_lb9:;
                        {
                            uint8_t t187 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                            uint8_t t188 = (uint8_t)(t187 == ((uint8_t)2ULL));
                            if (!t188) {
                                goto gm_lb10;
                            }
                            gm_t9qkh0rsooc8d t189;
                            GM_ST(uint8_t, ((uint8_t *)&t189 + 0), ((uint8_t)24ULL));
                            gm_l11 = t189;
                            goto gm_lb7;
                        }
gm_lb10:;
                        {
                            gm_t9qkh0rsooc8d t190;
                            GM_ST(uint8_t, ((uint8_t *)&t190 + 0), ((uint8_t)25ULL));
                            gm_l11 = t190;
                            goto gm_lb7;
                        }
gm_lb11:;
                        gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 368, 24);
                    }
gm_lb7:;
                    gm_t9qkh0rsooc8d t191 = gm_l11;
                    uint32_t t192 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 4));
                    uint32_t t193 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 8));
                    uint32_t t194 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 12));
                    gm_tb1f35qilrmun t195 = gm_f_bc3_m9eakek(t191, t192, t193, t194);
                    F.ret = t195;
                    goto gm_fret;
                }
gm_lb6:;
            }
            {
                {
                    uint8_t t196 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t197 = (uint8_t)(t196 == ((uint8_t)8ULL));
                    if (!t197) {
                        goto gm_lb12;
                    }
                    uint8_t t198 = GM_LD(uint8_t, (((uint8_t *)t100) + 8));
                    gm_l12 = t198;
                    uint32_t t199 = GM_LD(uint32_t, ((((uint8_t *)t100) + 8) + 4));
                    gm_l13 = t199;
                    uint32_t t200 = GM_LD(uint32_t, ((((uint8_t *)t100) + 8) + 8));
                    gm_l14 = t200;
                    uint32_t t201 = GM_LD(uint32_t, ((((uint8_t *)t100) + 8) + 12));
                    gm_l15 = t201;
                    uint8_t t202 = gm_l12;
                    uint8_t t203 = ((t202 == ((uint8_t)0ULL)));
                    if (t203) {
                        gm_t9qkh0rsooc8d t204;
                        GM_ST(uint8_t, ((uint8_t *)&t204 + 0), ((uint8_t)26ULL));
                        gm_l84 = t204;
                    } else {
                        uint8_t t205 = gm_l12;
                        uint8_t t206 = ((t205 == ((uint8_t)1ULL)));
                        if (t206) {
                            gm_t9qkh0rsooc8d t207;
                            GM_ST(uint8_t, ((uint8_t *)&t207 + 0), ((uint8_t)27ULL));
                            gm_l85 = t207;
                        } else {
                            gm_t9qkh0rsooc8d t208;
                            GM_ST(uint8_t, ((uint8_t *)&t208 + 0), ((uint8_t)28ULL));
                            gm_l85 = t208;
                        }
                        gm_t9qkh0rsooc8d t209 = gm_l85;
                        gm_l84 = t209;
                    }
                    gm_t9qkh0rsooc8d t210 = gm_l84;
                    uint32_t t211 = gm_l13;
                    uint32_t t212 = gm_l14;
                    uint32_t t213 = gm_l15;
                    gm_tb1f35qilrmun t214 = gm_f_bc3_m9eakek(t210, t211, t212, t213);
                    F.ret = t214;
                    goto gm_fret;
                }
gm_lb12:;
                {
                    uint8_t t215 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t216 = (uint8_t)(t215 == ((uint8_t)19ULL));
                    if (!t216) {
                        goto gm_lb13;
                    }
                    uint32_t t217 = GM_LD(uint32_t, (((uint8_t *)t100) + 8));
                    gm_l16 = t217;
                    uint64_t t218 = GM_LD(uint64_t, ((((uint8_t *)t100) + 8) + 8));
                    gm_l17 = t218;
                    uint32_t t219 = gm_l16;
                    uint64_t t220 = gm_l17;
                    uint64_t t221 = (uint64_t)(t220 & ((uint64_t)4294967295ULL));
                    gm_t9qkh0rsooc8d t222;
                    GM_ST(uint8_t, ((uint8_t *)&t222 + 0), ((uint8_t)29ULL));
                    uint32_t t223 = (uint32_t)t221;
                    uint64_t t224 = gm_l17;
                    unsigned __int128 n225 = (unsigned __int128)((uint64_t)32ULL);
                    if (n225 >= 64) gm_trap_shift((uint64_t)n225, "u64", gm_src6unagr666v56, 377, 76);
                    uint64_t t226 = (uint64_t)(t224 >> (int)n225);
                    uint32_t t227 = (uint32_t)t226;
                    gm_tb1f35qilrmun t228 = gm_f_bc3_m9eakek(t222, t219, t223, t227);
                    F.ret = t228;
                    goto gm_fret;
                }
gm_lb13:;
                {
                    uint8_t t229 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t230 = (uint8_t)(t229 == ((uint8_t)28ULL));
                    if (!t230) {
                        goto gm_lb14;
                    }
                    uint32_t t231 = GM_LD(uint32_t, (((uint8_t *)t100) + 8));
                    gm_l18 = t231;
                    uint32_t t232 = GM_LD(uint32_t, ((((uint8_t *)t100) + 8) + 4));
                    gm_l19 = t232;
                    uint32_t t233 = gm_l18;
                    uint32_t t234 = gm_l19;
                    gm_t9qkh0rsooc8d t235;
                    GM_ST(uint8_t, ((uint8_t *)&t235 + 0), ((uint8_t)30ULL));
                    gm_tb1f35qilrmun t236 = gm_f_bc3_m9eakek(t235, t233, t234, ((uint32_t)0ULL));
                    F.ret = t236;
                    goto gm_fret;
                }
gm_lb14:;
                {
                    uint8_t t237 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t238 = (uint8_t)(t237 == ((uint8_t)30ULL));
                    if (!t238) {
                        goto gm_lb15;
                    }
                    memmove(((uint8_t *)&F.l20), (((uint8_t *)t100) + 8), 36);
                    gm_t8588ma2cr9d7 t239 = GM_LD(gm_t8588ma2cr9d7, ((uint8_t *)&F.l20));
                    uint8_t t240 = GM_LD(uint8_t, (((uint8_t *)&t239) + 0));
                    uint8_t t241 = (uint8_t)((t240 == ((uint8_t)6ULL)));
                    gm_t8588ma2cr9d7 t242;
                    GM_ST(uint8_t, (uint8_t *)&t242 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t242 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t242 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t242 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t242 + 0), ((uint8_t)6ULL));
                    if (t241) {
                        gm_tb1f35qilrmun t243 = gm_f_bc_free_v10vouf();
                        F.ret = t243;
                        goto gm_fret;
                    }
                    uint8_t t244 = GM_LD(uint8_t, (((uint8_t *)&F.l20) + 12));
                    if (t244) {
                        uint32_t t245 = GM_LD(uint32_t, ((((uint8_t *)&F.l20) + 12) + 4));
                        gm_t9qkh0rsooc8d t246;
                        GM_ST(uint8_t, ((uint8_t *)&t246 + 0), ((uint8_t)32ULL));
                        uint8_t t247 = (uint8_t)t245;
                        uint32_t t248 = GM_LD(uint32_t, ((((uint8_t *)&F.l20) + 20) + 4));
                        uint16_t t249 = (uint16_t)t248;
                        uint32_t t250 = GM_LD(uint32_t, (((uint8_t *)&F.l20) + 28));
                        uint32_t t251 = GM_LD(uint32_t, (((uint8_t *)&F.l20) + 32));
                        gm_tb1f35qilrmun t252;
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t252 + 0), t246);
                        GM_ST(uint8_t, ((uint8_t *)&t252 + 1), t247);
                        GM_ST(uint16_t, ((uint8_t *)&t252 + 2), t249);
                        GM_ST(uint32_t, ((uint8_t *)&t252 + 4), t250);
                        GM_ST(uint32_t, ((uint8_t *)&t252 + 8), t251);
                        GM_ST(uint32_t, ((uint8_t *)&t252 + 12), ((uint32_t)0ULL));
                        F.ret = t252;
                        goto gm_fret;
                    }
                    uint32_t t253 = GM_LD(uint32_t, ((((uint8_t *)&F.l20) + 20) + 4));
                    gm_t9qkh0rsooc8d t254;
                    GM_ST(uint8_t, ((uint8_t *)&t254 + 0), ((uint8_t)31ULL));
                    uint16_t t255 = (uint16_t)t253;
                    uint32_t t256 = GM_LD(uint32_t, (((uint8_t *)&F.l20) + 28));
                    uint32_t t257 = GM_LD(uint32_t, (((uint8_t *)&F.l20) + 32));
                    gm_tb1f35qilrmun t258;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t258 + 0), t254);
                    GM_ST(uint8_t, ((uint8_t *)&t258 + 1), ((uint8_t)0ULL));
                    GM_ST(uint16_t, ((uint8_t *)&t258 + 2), t255);
                    GM_ST(uint32_t, ((uint8_t *)&t258 + 4), t256);
                    GM_ST(uint32_t, ((uint8_t *)&t258 + 8), t257);
                    GM_ST(uint32_t, ((uint8_t *)&t258 + 12), ((uint32_t)0ULL));
                    F.ret = t258;
                    goto gm_fret;
                }
gm_lb15:;
            }
            {
                {
                    uint8_t t259 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t260 = (uint8_t)(t259 == ((uint8_t)26ULL));
                    if (!t260) {
                        goto gm_lb16;
                    }
                    memmove(((uint8_t *)&gm_l21), (((uint8_t *)t100) + 8), 24);
                    {
                        {
                            uint8_t t261 = GM_LD(uint8_t, ((((uint8_t *)&gm_l21) + 4) + 0));
                            uint8_t t262 = (uint8_t)(t261 == ((uint8_t)0ULL));
                            if (!t262) {
                                goto gm_lb18;
                            }
                            gm_ts6oqppjh0ba3 t263 = GM_LD(gm_ts6oqppjh0ba3, ((uint8_t *)&gm_l21));
                            uint32_t t264 = GM_LD(uint32_t, (((uint8_t *)&gm_l21) + 16));
                            uint32_t t265 = GM_LD(uint32_t, (((uint8_t *)&gm_l21) + 20));
                            uint32_t t266 = GM_LD(uint32_t, (((uint8_t *)&gm_l21) + 12));
                            gm_t9qkh0rsooc8d t267;
                            GM_ST(uint8_t, ((uint8_t *)&t267 + 0), ((uint8_t)33ULL));
                            gm_t9qkh0rsooc8d t268;
                            GM_ST(uint8_t, ((uint8_t *)&t268 + 0), ((uint8_t)34ULL));
                            gm_tb1f35qilrmun t269 = gm_f_bc_cmp_hcolbhq(t263, t267, t268, ((uint8_t)0), t264, t265, t266, ((uint16_t)0ULL));
                            F.ret = t269;
                            goto gm_fret;
                        }
gm_lb18:;
                        {
                            uint8_t t270 = GM_LD(uint8_t, ((((uint8_t *)&gm_l21) + 4) + 0));
                            uint8_t t271 = (uint8_t)(t270 == ((uint8_t)1ULL));
                            if (!t271) {
                                goto gm_lb19;
                            }
                            uint32_t t272 = GM_LD(uint32_t, ((((uint8_t *)&gm_l21) + 4) + 4));
                            gm_l22 = t272;
                            uint32_t t273 = gm_l22;
                            uint8_t t274 = ((t273 == ((uint32_t)64ULL)));
                            if (t274) {
                                gm_ts6oqppjh0ba3 t275 = GM_LD(gm_ts6oqppjh0ba3, ((uint8_t *)&gm_l21));
                                uint32_t t276 = GM_LD(uint32_t, (((uint8_t *)&gm_l21) + 16));
                                uint32_t t277 = GM_LD(uint32_t, (((uint8_t *)&gm_l21) + 20));
                                uint32_t t278 = GM_LD(uint32_t, (((uint8_t *)&gm_l21) + 12));
                                gm_t9qkh0rsooc8d t279;
                                GM_ST(uint8_t, ((uint8_t *)&t279 + 0), ((uint8_t)35ULL));
                                gm_t9qkh0rsooc8d t280;
                                GM_ST(uint8_t, ((uint8_t *)&t280 + 0), ((uint8_t)36ULL));
                                gm_tb1f35qilrmun t281 = gm_f_bc_cmp_hcolbhq(t275, t279, t280, ((uint8_t)0), t276, t277, t278, ((uint16_t)0ULL));
                                F.ret = t281;
                                goto gm_fret;
                            }
                            gm_ts6oqppjh0ba3 t282 = GM_LD(gm_ts6oqppjh0ba3, ((uint8_t *)&gm_l21));
                            uint32_t t283 = GM_LD(uint32_t, (((uint8_t *)&gm_l21) + 16));
                            uint32_t t284 = GM_LD(uint32_t, (((uint8_t *)&gm_l21) + 20));
                            uint32_t t285 = GM_LD(uint32_t, (((uint8_t *)&gm_l21) + 12));
                            uint32_t t286 = gm_l22;
                            gm_t9qkh0rsooc8d t287;
                            GM_ST(uint8_t, ((uint8_t *)&t287 + 0), ((uint8_t)37ULL));
                            gm_t9qkh0rsooc8d t288;
                            GM_ST(uint8_t, ((uint8_t *)&t288 + 0), ((uint8_t)38ULL));
                            uint16_t t289 = (uint16_t)t286;
                            gm_tb1f35qilrmun t290 = gm_f_bc_cmp_hcolbhq(t282, t287, t288, ((uint8_t)0), t283, t284, t285, t289);
                            F.ret = t290;
                            goto gm_fret;
                        }
gm_lb19:;
                        {
                            uint8_t t291 = GM_LD(uint8_t, ((((uint8_t *)&gm_l21) + 4) + 0));
                            uint8_t t292 = (uint8_t)(t291 == ((uint8_t)3ULL));
                            if (!t292) {
                                goto gm_lb20;
                            }
                            gm_ts6oqppjh0ba3 t293 = GM_LD(gm_ts6oqppjh0ba3, ((uint8_t *)&gm_l21));
                            uint32_t t294 = GM_LD(uint32_t, (((uint8_t *)&gm_l21) + 16));
                            uint32_t t295 = GM_LD(uint32_t, (((uint8_t *)&gm_l21) + 20));
                            uint32_t t296 = GM_LD(uint32_t, (((uint8_t *)&gm_l21) + 12));
                            gm_t9qkh0rsooc8d t297;
                            GM_ST(uint8_t, ((uint8_t *)&t297 + 0), ((uint8_t)39ULL));
                            gm_t9qkh0rsooc8d t298;
                            GM_ST(uint8_t, ((uint8_t *)&t298 + 0), ((uint8_t)40ULL));
                            gm_tb1f35qilrmun t299 = gm_f_bc_cmp_hcolbhq(t293, t297, t298, ((uint8_t)0), t294, t295, t296, ((uint16_t)0ULL));
                            F.ret = t299;
                            goto gm_fret;
                        }
gm_lb20:;
                        {
                            gm_tb1f35qilrmun t300 = gm_f_bc_free_v10vouf();
                            F.ret = t300;
                            goto gm_fret;
                        }
gm_lb21:;
                        gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 384, 20);
                    }
gm_lb17:;
                }
gm_lb16:;
                {
                    uint8_t t301 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t302 = (uint8_t)(t301 == ((uint8_t)24ULL));
                    if (!t302) {
                        goto gm_lb22;
                    }
                    memmove(((uint8_t *)&gm_l23), (((uint8_t *)t100) + 8), 24);
                    {
                        {
                            uint8_t t303 = GM_LD(uint8_t, ((((uint8_t *)&gm_l23) + 4) + 0));
                            uint8_t t304 = (uint8_t)(t303 == ((uint8_t)0ULL));
                            if (!t304) {
                                goto gm_lb24;
                            }
                            gm_ts6oqppjh0ba3 t305 = GM_LD(gm_ts6oqppjh0ba3, ((uint8_t *)&gm_l23));
                            uint32_t t306 = GM_LD(uint32_t, (((uint8_t *)&gm_l23) + 12));
                            uint32_t t307 = GM_LD(uint32_t, (((uint8_t *)&gm_l23) + 16));
                            uint32_t t308 = GM_LD(uint32_t, (((uint8_t *)&gm_l23) + 20));
                            gm_t9qkh0rsooc8d t309;
                            GM_ST(uint8_t, ((uint8_t *)&t309 + 0), ((uint8_t)43ULL));
                            gm_t9qkh0rsooc8d t310;
                            GM_ST(uint8_t, ((uint8_t *)&t310 + 0), ((uint8_t)44ULL));
                            gm_tb1f35qilrmun t311 = gm_f_bc_cmp_hcolbhq(t305, t309, t310, ((uint8_t)1), t306, t307, t308, ((uint16_t)0ULL));
                            F.ret = t311;
                            goto gm_fret;
                        }
gm_lb24:;
                        {
                            uint8_t t312 = GM_LD(uint8_t, ((((uint8_t *)&gm_l23) + 4) + 0));
                            uint8_t t313 = (uint8_t)(t312 == ((uint8_t)1ULL));
                            if (!t313) {
                                goto gm_lb25;
                            }
                            uint32_t t314 = GM_LD(uint32_t, ((((uint8_t *)&gm_l23) + 4) + 4));
                            gm_l24 = t314;
                            uint32_t t315 = gm_l24;
                            uint8_t t316 = ((t315 == ((uint32_t)64ULL)));
                            if (t316) {
                                gm_ts6oqppjh0ba3 t317 = GM_LD(gm_ts6oqppjh0ba3, ((uint8_t *)&gm_l23));
                                uint32_t t318 = GM_LD(uint32_t, (((uint8_t *)&gm_l23) + 12));
                                uint32_t t319 = GM_LD(uint32_t, (((uint8_t *)&gm_l23) + 16));
                                uint32_t t320 = GM_LD(uint32_t, (((uint8_t *)&gm_l23) + 20));
                                gm_t9qkh0rsooc8d t321;
                                GM_ST(uint8_t, ((uint8_t *)&t321 + 0), ((uint8_t)45ULL));
                                gm_t9qkh0rsooc8d t322;
                                GM_ST(uint8_t, ((uint8_t *)&t322 + 0), ((uint8_t)46ULL));
                                gm_tb1f35qilrmun t323 = gm_f_bc_cmp_hcolbhq(t317, t321, t322, ((uint8_t)1), t318, t319, t320, ((uint16_t)0ULL));
                                F.ret = t323;
                                goto gm_fret;
                            }
                            gm_ts6oqppjh0ba3 t324 = GM_LD(gm_ts6oqppjh0ba3, ((uint8_t *)&gm_l23));
                            uint32_t t325 = GM_LD(uint32_t, (((uint8_t *)&gm_l23) + 12));
                            uint32_t t326 = GM_LD(uint32_t, (((uint8_t *)&gm_l23) + 16));
                            uint32_t t327 = GM_LD(uint32_t, (((uint8_t *)&gm_l23) + 20));
                            uint32_t t328 = gm_l24;
                            gm_t9qkh0rsooc8d t329;
                            GM_ST(uint8_t, ((uint8_t *)&t329 + 0), ((uint8_t)47ULL));
                            gm_t9qkh0rsooc8d t330;
                            GM_ST(uint8_t, ((uint8_t *)&t330 + 0), ((uint8_t)48ULL));
                            uint16_t t331 = (uint16_t)t328;
                            gm_tb1f35qilrmun t332 = gm_f_bc_cmp_hcolbhq(t324, t329, t330, ((uint8_t)1), t325, t326, t327, t331);
                            F.ret = t332;
                            goto gm_fret;
                        }
gm_lb25:;
                        {
                            uint8_t t333 = GM_LD(uint8_t, ((((uint8_t *)&gm_l23) + 4) + 0));
                            uint8_t t334 = (uint8_t)(t333 == ((uint8_t)3ULL));
                            if (!t334) {
                                goto gm_lb26;
                            }
                            gm_ts6oqppjh0ba3 t335 = GM_LD(gm_ts6oqppjh0ba3, ((uint8_t *)&gm_l23));
                            uint32_t t336 = GM_LD(uint32_t, (((uint8_t *)&gm_l23) + 12));
                            uint32_t t337 = GM_LD(uint32_t, (((uint8_t *)&gm_l23) + 16));
                            uint32_t t338 = GM_LD(uint32_t, (((uint8_t *)&gm_l23) + 20));
                            gm_t9qkh0rsooc8d t339;
                            GM_ST(uint8_t, ((uint8_t *)&t339 + 0), ((uint8_t)49ULL));
                            gm_t9qkh0rsooc8d t340;
                            GM_ST(uint8_t, ((uint8_t *)&t340 + 0), ((uint8_t)50ULL));
                            gm_tb1f35qilrmun t341 = gm_f_bc_cmp_hcolbhq(t335, t339, t340, ((uint8_t)1), t336, t337, t338, ((uint16_t)0ULL));
                            F.ret = t341;
                            goto gm_fret;
                        }
gm_lb26:;
                        {
                            gm_tb1f35qilrmun t342 = gm_f_bc_free_v10vouf();
                            F.ret = t342;
                            goto gm_fret;
                        }
gm_lb27:;
                        gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 393, 21);
                    }
gm_lb23:;
                }
gm_lb22:;
            }
            {
                {
                    uint8_t t343 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t344 = (uint8_t)(t343 == ((uint8_t)27ULL));
                    if (!t344) {
                        goto gm_lb28;
                    }
                    memmove(((uint8_t *)&gm_l25), (((uint8_t *)t100) + 8), 24);
                    {
                        {
                            {
                                {
                                    uint8_t t345 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
                                    uint8_t t346 = (uint8_t)(t345 == ((uint8_t)0ULL));
                                    if (!t346) {
                                        goto gm_lb32;
                                    }
                                    goto gm_lb31;
                                }
gm_lb32:;
                                {
                                    uint8_t t347 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
                                    uint8_t t348 = (uint8_t)(t347 == ((uint8_t)1ULL));
                                    if (!t348) {
                                        goto gm_lb33;
                                    }
                                    goto gm_lb31;
                                }
gm_lb33:;
                                goto gm_lb30;
                            }
gm_lb31:;
                            uint8_t t349 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 8));
                            if (t349) {
                                gm_t9qkh0rsooc8d t350;
                                GM_ST(uint8_t, ((uint8_t *)&t350 + 0), ((uint8_t)42ULL));
                                gm_l86 = t350;
                            } else {
                                gm_t9qkh0rsooc8d t351;
                                GM_ST(uint8_t, ((uint8_t *)&t351 + 0), ((uint8_t)41ULL));
                                gm_l86 = t351;
                            }
                            gm_t9qkh0rsooc8d t352 = gm_l86;
                            uint32_t t353 = GM_LD(uint32_t, (((uint8_t *)&gm_l25) + 12));
                            uint32_t t354 = GM_LD(uint32_t, (((uint8_t *)&gm_l25) + 16));
                            uint32_t t355 = GM_LD(uint32_t, (((uint8_t *)&gm_l25) + 20));
                            gm_tb1f35qilrmun t356 = gm_f_bc3_m9eakek(t352, t353, t354, t355);
                            F.ret = t356;
                            goto gm_fret;
                        }
gm_lb30:;
                        {
                            gm_tb1f35qilrmun t357 = gm_f_bc_free_v10vouf();
                            F.ret = t357;
                            goto gm_fret;
                        }
gm_lb34:;
                        gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 402, 20);
                    }
gm_lb29:;
                }
gm_lb28:;
                {
                    uint8_t t358 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t359 = (uint8_t)(t358 == ((uint8_t)25ULL));
                    if (!t359) {
                        goto gm_lb35;
                    }
                    memmove(((uint8_t *)&gm_l26), (((uint8_t *)t100) + 8), 24);
                    {
                        {
                            {
                                {
                                    uint8_t t360 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                                    uint8_t t361 = (uint8_t)(t360 == ((uint8_t)0ULL));
                                    if (!t361) {
                                        goto gm_lb39;
                                    }
                                    goto gm_lb38;
                                }
gm_lb39:;
                                {
                                    uint8_t t362 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                                    uint8_t t363 = (uint8_t)(t362 == ((uint8_t)1ULL));
                                    if (!t363) {
                                        goto gm_lb40;
                                    }
                                    goto gm_lb38;
                                }
gm_lb40:;
                                goto gm_lb37;
                            }
gm_lb38:;
                            uint8_t t364 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 8));
                            if (t364) {
                                gm_t9qkh0rsooc8d t365;
                                GM_ST(uint8_t, ((uint8_t *)&t365 + 0), ((uint8_t)51ULL));
                                gm_l87 = t365;
                            } else {
                                gm_t9qkh0rsooc8d t366;
                                GM_ST(uint8_t, ((uint8_t *)&t366 + 0), ((uint8_t)52ULL));
                                gm_l87 = t366;
                            }
                            gm_t9qkh0rsooc8d t367 = gm_l87;
                            uint32_t t368 = GM_LD(uint32_t, (((uint8_t *)&gm_l26) + 12));
                            uint32_t t369 = GM_LD(uint32_t, (((uint8_t *)&gm_l26) + 16));
                            uint32_t t370 = GM_LD(uint32_t, (((uint8_t *)&gm_l26) + 20));
                            gm_tb1f35qilrmun t371 = gm_f_bc3_m9eakek(t367, t368, t369, t370);
                            F.ret = t371;
                            goto gm_fret;
                        }
gm_lb37:;
                        {
                            gm_tb1f35qilrmun t372 = gm_f_bc_free_v10vouf();
                            F.ret = t372;
                            goto gm_fret;
                        }
gm_lb41:;
                        gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 407, 20);
                    }
gm_lb36:;
                }
gm_lb35:;
            }
            {
                {
                    uint8_t t373 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t374 = (uint8_t)(t373 == ((uint8_t)33ULL));
                    if (!t374) {
                        goto gm_lb42;
                    }
                    uint32_t t375 = GM_LD(uint32_t, (((uint8_t *)t100) + 8));
                    gm_l27 = t375;
                    uint32_t t376 = gm_l27;
                    gm_t9qkh0rsooc8d t377;
                    GM_ST(uint8_t, ((uint8_t *)&t377 + 0), ((uint8_t)53ULL));
                    gm_tb1f35qilrmun t378 = gm_f_bc3_m9eakek(t377, t376, ((uint32_t)0ULL), ((uint32_t)0ULL));
                    F.ret = t378;
                    goto gm_fret;
                }
gm_lb42:;
                {
                    uint8_t t379 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t380 = (uint8_t)(t379 == ((uint8_t)34ULL));
                    if (!t380) {
                        goto gm_lb43;
                    }
                    uint32_t t381 = GM_LD(uint32_t, (((uint8_t *)t100) + 8));
                    gm_l28 = t381;
                    uint32_t t382 = GM_LD(uint32_t, ((((uint8_t *)t100) + 8) + 4));
                    gm_l29 = t382;
                    uint32_t t383 = gm_l28;
                    uint32_t t384 = gm_l29;
                    gm_t9qkh0rsooc8d t385;
                    GM_ST(uint8_t, ((uint8_t *)&t385 + 0), ((uint8_t)54ULL));
                    gm_tb1f35qilrmun t386 = gm_f_bc3_m9eakek(t385, t383, ((uint32_t)0ULL), t384);
                    F.ret = t386;
                    goto gm_fret;
                }
gm_lb43:;
                {
                    uint8_t t387 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t388 = (uint8_t)(t387 == ((uint8_t)35ULL));
                    if (!t388) {
                        goto gm_lb44;
                    }
                    uint32_t t389 = GM_LD(uint32_t, (((uint8_t *)t100) + 8));
                    gm_l30 = t389;
                    uint32_t t390 = GM_LD(uint32_t, ((((uint8_t *)t100) + 8) + 4));
                    gm_l31 = t390;
                    uint32_t t391 = gm_l30;
                    uint32_t t392 = gm_l31;
                    gm_t9qkh0rsooc8d t393;
                    GM_ST(uint8_t, ((uint8_t *)&t393 + 0), ((uint8_t)55ULL));
                    gm_tb1f35qilrmun t394 = gm_f_bc3_m9eakek(t393, t391, ((uint32_t)0ULL), t392);
                    F.ret = t394;
                    goto gm_fret;
                }
gm_lb44:;
                {
                    uint8_t t395 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t396 = (uint8_t)(t395 == ((uint8_t)73ULL));
                    if (!t396) {
                        goto gm_lb45;
                    }
                    uint32_t t397 = GM_LD(uint32_t, (((uint8_t *)t100) + 8));
                    gm_l32 = t397;
                    uint32_t t398 = gm_l32;
                    gm_t9qkh0rsooc8d t399;
                    GM_ST(uint8_t, ((uint8_t *)&t399 + 0), ((uint8_t)56ULL));
                    gm_tb1f35qilrmun t400 = gm_f_bc3_m9eakek(t399, t398, ((uint32_t)0ULL), ((uint32_t)0ULL));
                    F.ret = t400;
                    goto gm_fret;
                }
gm_lb45:;
                {
                    uint8_t t401 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t402 = (uint8_t)(t401 == ((uint8_t)3ULL));
                    if (!t402) {
                        goto gm_lb46;
                    }
                    uint32_t t403 = GM_LD(uint32_t, (((uint8_t *)t100) + 8));
                    gm_l33 = t403;
                    uint32_t t404 = GM_LD(uint32_t, ((((uint8_t *)t100) + 8) + 4));
                    gm_l34 = t404;
                    uint32_t t405 = GM_LD(uint32_t, ((((uint8_t *)t100) + 8) + 8));
                    gm_l35 = t405;
                    uint8_t t406 = gm_l1;
                    uint8_t t407 = (uint8_t)!t406;
                    if (t407) {
                        gm_l88 = ((uint8_t)1);
                    } else {
                        uint32_t t408 = gm_l35;
                        uint64_t t409 = (uint64_t)t408;
                        uint8_t t410 = gm_f_word_size_mu6ni08(t409);
                        uint8_t t411 = (uint8_t)!t410;
                        gm_l88 = t411;
                    }
                    uint8_t t412 = gm_l88;
                    if (t412) {
                        gm_tb1f35qilrmun t413 = gm_f_bc_free_v10vouf();
                        F.ret = t413;
                        goto gm_fret;
                    }
                    uint32_t t414 = gm_l35;
                    uint8_t t415 = ((t414 == ((uint32_t)8ULL)));
                    if (t415) {
                        gm_t9qkh0rsooc8d t416;
                        GM_ST(uint8_t, ((uint8_t *)&t416 + 0), ((uint8_t)60ULL));
                        gm_l89 = t416;
                    } else {
                        uint32_t t417 = gm_l35;
                        uint8_t t418 = ((t417 == ((uint32_t)4ULL)));
                        if (t418) {
                            gm_t9qkh0rsooc8d t419;
                            GM_ST(uint8_t, ((uint8_t *)&t419 + 0), ((uint8_t)59ULL));
                            gm_l90 = t419;
                        } else {
                            uint32_t t420 = gm_l35;
                            uint8_t t421 = ((t420 == ((uint32_t)1ULL)));
                            if (t421) {
                                gm_t9qkh0rsooc8d t422;
                                GM_ST(uint8_t, ((uint8_t *)&t422 + 0), ((uint8_t)57ULL));
                                gm_l91 = t422;
                            } else {
                                gm_t9qkh0rsooc8d t423;
                                GM_ST(uint8_t, ((uint8_t *)&t423 + 0), ((uint8_t)58ULL));
                                gm_l91 = t423;
                            }
                            gm_t9qkh0rsooc8d t424 = gm_l91;
                            gm_l90 = t424;
                        }
                        gm_t9qkh0rsooc8d t425 = gm_l90;
                        gm_l89 = t425;
                    }
                    gm_t9qkh0rsooc8d t426 = gm_l89;
                    uint32_t t427 = gm_l33;
                    uint32_t t428 = gm_l34;
                    gm_tb1f35qilrmun t429 = gm_f_bc3_m9eakek(t426, t427, t428, ((uint32_t)0ULL));
                    F.ret = t429;
                    goto gm_fret;
                }
gm_lb46:;
            }
            {
                {
                    uint8_t t430 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t431 = (uint8_t)(t430 == ((uint8_t)60ULL));
                    if (!t431) {
                        goto gm_lb47;
                    }
                    uint32_t t432 = GM_LD(uint32_t, (((uint8_t *)t100) + 8));
                    gm_l36 = t432;
                    uint32_t t433 = GM_LD(uint32_t, ((((uint8_t *)t100) + 8) + 4));
                    gm_l37 = t433;
                    uint32_t t434 = GM_LD(uint32_t, ((((uint8_t *)t100) + 8) + 8));
                    gm_l38 = t434;
                    uint8_t t435 = gm_l1;
                    uint8_t t436 = (uint8_t)!t435;
                    if (t436) {
                        gm_l92 = ((uint8_t)1);
                    } else {
                        uint32_t t437 = gm_l38;
                        uint64_t t438 = (uint64_t)t437;
                        uint8_t t439 = gm_f_word_size_mu6ni08(t438);
                        uint8_t t440 = (uint8_t)!t439;
                        gm_l92 = t440;
                    }
                    uint8_t t441 = gm_l92;
                    if (t441) {
                        gm_tb1f35qilrmun t442 = gm_f_bc_free_v10vouf();
                        F.ret = t442;
                        goto gm_fret;
                    }
                    uint32_t t443 = gm_l38;
                    uint8_t t444 = ((t443 == ((uint32_t)8ULL)));
                    if (t444) {
                        gm_t9qkh0rsooc8d t445;
                        GM_ST(uint8_t, ((uint8_t *)&t445 + 0), ((uint8_t)60ULL));
                        gm_l93 = t445;
                    } else {
                        uint32_t t446 = gm_l38;
                        uint8_t t447 = ((t446 == ((uint32_t)4ULL)));
                        if (t447) {
                            gm_t9qkh0rsooc8d t448;
                            GM_ST(uint8_t, ((uint8_t *)&t448 + 0), ((uint8_t)59ULL));
                            gm_l94 = t448;
                        } else {
                            uint32_t t449 = gm_l38;
                            uint8_t t450 = ((t449 == ((uint32_t)1ULL)));
                            if (t450) {
                                gm_t9qkh0rsooc8d t451;
                                GM_ST(uint8_t, ((uint8_t *)&t451 + 0), ((uint8_t)57ULL));
                                gm_l95 = t451;
                            } else {
                                gm_t9qkh0rsooc8d t452;
                                GM_ST(uint8_t, ((uint8_t *)&t452 + 0), ((uint8_t)58ULL));
                                gm_l95 = t452;
                            }
                            gm_t9qkh0rsooc8d t453 = gm_l95;
                            gm_l94 = t453;
                        }
                        gm_t9qkh0rsooc8d t454 = gm_l94;
                        gm_l93 = t454;
                    }
                    gm_t9qkh0rsooc8d t455 = gm_l93;
                    uint32_t t456 = gm_l36;
                    uint32_t t457 = gm_l37;
                    gm_tb1f35qilrmun t458 = gm_f_bc3_m9eakek(t455, t456, t457, ((uint32_t)0ULL));
                    F.ret = t458;
                    goto gm_fret;
                }
gm_lb47:;
                {
                    uint8_t t459 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t460 = (uint8_t)(t459 == ((uint8_t)4ULL));
                    if (!t460) {
                        goto gm_lb48;
                    }
                    uint32_t t461 = GM_LD(uint32_t, (((uint8_t *)t100) + 8));
                    gm_l39 = t461;
                    uint32_t t462 = GM_LD(uint32_t, ((((uint8_t *)t100) + 8) + 4));
                    gm_l40 = t462;
                    uint32_t t463 = GM_LD(uint32_t, ((((uint8_t *)t100) + 8) + 8));
                    gm_l41 = t463;
                    uint8_t t464 = gm_l1;
                    uint8_t t465 = (uint8_t)!t464;
                    if (t465) {
                        gm_l96 = ((uint8_t)1);
                    } else {
                        uint32_t t466 = gm_l40;
                        uint64_t t467 = (uint64_t)t466;
                        uint8_t t468 = gm_f_word_size_mu6ni08(t467);
                        uint8_t t469 = (uint8_t)!t468;
                        gm_l96 = t469;
                    }
                    uint8_t t470 = gm_l96;
                    if (t470) {
                        gm_tb1f35qilrmun t471 = gm_f_bc_free_v10vouf();
                        F.ret = t471;
                        goto gm_fret;
                    }
                    uint32_t t472 = gm_l40;
                    uint8_t t473 = ((t472 == ((uint32_t)8ULL)));
                    if (t473) {
                        gm_t9qkh0rsooc8d t474;
                        GM_ST(uint8_t, ((uint8_t *)&t474 + 0), ((uint8_t)64ULL));
                        gm_l97 = t474;
                    } else {
                        uint32_t t475 = gm_l40;
                        uint8_t t476 = ((t475 == ((uint32_t)4ULL)));
                        if (t476) {
                            gm_t9qkh0rsooc8d t477;
                            GM_ST(uint8_t, ((uint8_t *)&t477 + 0), ((uint8_t)63ULL));
                            gm_l98 = t477;
                        } else {
                            uint32_t t478 = gm_l40;
                            uint8_t t479 = ((t478 == ((uint32_t)1ULL)));
                            if (t479) {
                                gm_t9qkh0rsooc8d t480;
                                GM_ST(uint8_t, ((uint8_t *)&t480 + 0), ((uint8_t)61ULL));
                                gm_l99 = t480;
                            } else {
                                gm_t9qkh0rsooc8d t481;
                                GM_ST(uint8_t, ((uint8_t *)&t481 + 0), ((uint8_t)62ULL));
                                gm_l99 = t481;
                            }
                            gm_t9qkh0rsooc8d t482 = gm_l99;
                            gm_l98 = t482;
                        }
                        gm_t9qkh0rsooc8d t483 = gm_l98;
                        gm_l97 = t483;
                    }
                    gm_t9qkh0rsooc8d t484 = gm_l97;
                    uint32_t t485 = gm_l39;
                    uint32_t t486 = gm_l41;
                    gm_tb1f35qilrmun t487 = gm_f_bc3_m9eakek(t484, t485, t486, ((uint32_t)0ULL));
                    F.ret = t487;
                    goto gm_fret;
                }
gm_lb48:;
            }
            {
                {
                    uint8_t t488 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t489 = (uint8_t)(t488 == ((uint8_t)61ULL));
                    if (!t489) {
                        goto gm_lb49;
                    }
                    uint32_t t490 = GM_LD(uint32_t, (((uint8_t *)t100) + 8));
                    gm_l42 = t490;
                    uint32_t t491 = GM_LD(uint32_t, ((((uint8_t *)t100) + 8) + 4));
                    gm_l43 = t491;
                    uint32_t t492 = GM_LD(uint32_t, ((((uint8_t *)t100) + 8) + 8));
                    gm_l44 = t492;
                    uint8_t t493 = gm_l1;
                    uint8_t t494 = (uint8_t)!t493;
                    if (t494) {
                        gm_l100 = ((uint8_t)1);
                    } else {
                        uint32_t t495 = gm_l43;
                        uint64_t t496 = (uint64_t)t495;
                        uint8_t t497 = gm_f_word_size_mu6ni08(t496);
                        uint8_t t498 = (uint8_t)!t497;
                        gm_l100 = t498;
                    }
                    uint8_t t499 = gm_l100;
                    if (t499) {
                        gm_tb1f35qilrmun t500 = gm_f_bc_free_v10vouf();
                        F.ret = t500;
                        goto gm_fret;
                    }
                    uint32_t t501 = gm_l43;
                    uint8_t t502 = ((t501 == ((uint32_t)8ULL)));
                    if (t502) {
                        gm_t9qkh0rsooc8d t503;
                        GM_ST(uint8_t, ((uint8_t *)&t503 + 0), ((uint8_t)64ULL));
                        gm_l101 = t503;
                    } else {
                        uint32_t t504 = gm_l43;
                        uint8_t t505 = ((t504 == ((uint32_t)4ULL)));
                        if (t505) {
                            gm_t9qkh0rsooc8d t506;
                            GM_ST(uint8_t, ((uint8_t *)&t506 + 0), ((uint8_t)63ULL));
                            gm_l102 = t506;
                        } else {
                            uint32_t t507 = gm_l43;
                            uint8_t t508 = ((t507 == ((uint32_t)1ULL)));
                            if (t508) {
                                gm_t9qkh0rsooc8d t509;
                                GM_ST(uint8_t, ((uint8_t *)&t509 + 0), ((uint8_t)61ULL));
                                gm_l103 = t509;
                            } else {
                                gm_t9qkh0rsooc8d t510;
                                GM_ST(uint8_t, ((uint8_t *)&t510 + 0), ((uint8_t)62ULL));
                                gm_l103 = t510;
                            }
                            gm_t9qkh0rsooc8d t511 = gm_l103;
                            gm_l102 = t511;
                        }
                        gm_t9qkh0rsooc8d t512 = gm_l102;
                        gm_l101 = t512;
                    }
                    gm_t9qkh0rsooc8d t513 = gm_l101;
                    uint32_t t514 = gm_l42;
                    uint32_t t515 = gm_l44;
                    gm_tb1f35qilrmun t516 = gm_f_bc3_m9eakek(t513, t514, t515, ((uint32_t)0ULL));
                    F.ret = t516;
                    goto gm_fret;
                }
gm_lb49:;
                {
                    uint8_t t517 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t518 = (uint8_t)(t517 == ((uint8_t)6ULL));
                    if (!t518) {
                        goto gm_lb50;
                    }
                    uint32_t t519 = GM_LD(uint32_t, (((uint8_t *)t100) + 8));
                    gm_l45 = t519;
                    uint32_t t520 = GM_LD(uint32_t, ((((uint8_t *)t100) + 8) + 4));
                    gm_l46 = t520;
                    uint32_t t521 = GM_LD(uint32_t, ((((uint8_t *)t100) + 8) + 8));
                    gm_l47 = t521;
                    uint32_t t522 = GM_LD(uint32_t, ((((uint8_t *)t100) + 8) + 12));
                    gm_l48 = t522;
                    uint8_t t523 = gm_l1;
                    uint8_t t524 = (uint8_t)!t523;
                    if (t524) {
                        gm_l104 = ((uint8_t)1);
                    } else {
                        uint32_t t525 = gm_l48;
                        uint64_t t526 = (uint64_t)t525;
                        uint8_t t527 = gm_f_word_size_mu6ni08(t526);
                        uint8_t t528 = (uint8_t)!t527;
                        gm_l104 = t528;
                    }
                    uint8_t t529 = gm_l104;
                    if (t529) {
                        gm_tb1f35qilrmun t530 = gm_f_bc_free_v10vouf();
                        F.ret = t530;
                        goto gm_fret;
                    }
                    uint32_t t531 = gm_l48;
                    gm_t9qkh0rsooc8d t532;
                    GM_ST(uint8_t, ((uint8_t *)&t532 + 0), ((uint8_t)65ULL));
                    uint8_t t533 = (uint8_t)t531;
                    uint32_t t534 = gm_l45;
                    uint32_t t535 = gm_l46;
                    uint32_t t536 = gm_l47;
                    gm_tb1f35qilrmun t537;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t537 + 0), t532);
                    GM_ST(uint8_t, ((uint8_t *)&t537 + 1), t533);
                    GM_ST(uint16_t, ((uint8_t *)&t537 + 2), ((uint16_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t537 + 4), t534);
                    GM_ST(uint32_t, ((uint8_t *)&t537 + 8), t535);
                    GM_ST(uint32_t, ((uint8_t *)&t537 + 12), t536);
                    F.ret = t537;
                    goto gm_fret;
                }
gm_lb50:;
            }
            {
                {
                    uint8_t t538 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t539 = (uint8_t)(t538 == ((uint8_t)7ULL));
                    if (!t539) {
                        goto gm_lb51;
                    }
                    uint32_t t540 = GM_LD(uint32_t, (((uint8_t *)t100) + 8));
                    gm_l49 = t540;
                    uint32_t t541 = GM_LD(uint32_t, ((((uint8_t *)t100) + 8) + 4));
                    gm_l50 = t541;
                    uint32_t t542 = GM_LD(uint32_t, ((((uint8_t *)t100) + 8) + 8));
                    gm_l51 = t542;
                    uint32_t t543 = GM_LD(uint32_t, ((((uint8_t *)t100) + 8) + 12));
                    gm_l52 = t543;
                    uint8_t t544 = gm_l1;
                    uint8_t t545 = (uint8_t)!t544;
                    if (t545) {
                        gm_l105 = ((uint8_t)1);
                    } else {
                        uint32_t t546 = gm_l51;
                        uint64_t t547 = (uint64_t)t546;
                        uint8_t t548 = gm_f_word_size_mu6ni08(t547);
                        uint8_t t549 = (uint8_t)!t548;
                        gm_l105 = t549;
                    }
                    uint8_t t550 = gm_l105;
                    if (t550) {
                        gm_tb1f35qilrmun t551 = gm_f_bc_free_v10vouf();
                        F.ret = t551;
                        goto gm_fret;
                    }
                    uint32_t t552 = gm_l51;
                    gm_t9qkh0rsooc8d t553;
                    GM_ST(uint8_t, ((uint8_t *)&t553 + 0), ((uint8_t)66ULL));
                    uint8_t t554 = (uint8_t)t552;
                    uint32_t t555 = gm_l49;
                    uint32_t t556 = gm_l50;
                    uint32_t t557 = gm_l52;
                    gm_tb1f35qilrmun t558;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t558 + 0), t553);
                    GM_ST(uint8_t, ((uint8_t *)&t558 + 1), t554);
                    GM_ST(uint16_t, ((uint8_t *)&t558 + 2), ((uint16_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t558 + 4), t555);
                    GM_ST(uint32_t, ((uint8_t *)&t558 + 8), t556);
                    GM_ST(uint32_t, ((uint8_t *)&t558 + 12), t557);
                    F.ret = t558;
                    goto gm_fret;
                }
gm_lb51:;
                {
                    uint8_t t559 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t560 = (uint8_t)(t559 == ((uint8_t)12ULL));
                    if (!t560) {
                        goto gm_lb52;
                    }
                    memmove(((uint8_t *)&gm_l53), (((uint8_t *)t100) + 8), 32);
                    uint8_t t561 = gm_l1;
                    uint8_t t562 = (uint8_t)!t561;
                    if (t562) {
                        gm_l107 = ((uint8_t)1);
                    } else {
                        uint32_t t563 = GM_LD(uint32_t, (((uint8_t *)&gm_l53) + 12));
                        uint64_t t564 = (uint64_t)t563;
                        uint8_t t565 = gm_f_word_size_mu6ni08(t564);
                        uint8_t t566 = (uint8_t)!t565;
                        gm_l107 = t566;
                    }
                    uint8_t t567 = gm_l107;
                    if (t567) {
                        gm_l106 = ((uint8_t)1);
                    } else {
                        uint32_t t568 = GM_LD(uint32_t, (((uint8_t *)&gm_l53) + 16));
                        uint8_t t569 = gm_f_bc_align_6s4vol1(t568);
                        uint8_t t570 = (uint8_t)!t569;
                        gm_l106 = t570;
                    }
                    uint8_t t571 = gm_l106;
                    if (t571) {
                        gm_tb1f35qilrmun t572 = gm_f_bc_free_v10vouf();
                        F.ret = t572;
                        goto gm_fret;
                    }
                    uint32_t t573 = GM_LD(uint32_t, (((uint8_t *)&gm_l53) + 12));
                    uint8_t t574 = ((t573 == ((uint32_t)8ULL)));
                    if (t574) {
                        gm_t9qkh0rsooc8d t575;
                        GM_ST(uint8_t, ((uint8_t *)&t575 + 0), ((uint8_t)70ULL));
                        gm_l108 = t575;
                    } else {
                        uint32_t t576 = GM_LD(uint32_t, (((uint8_t *)&gm_l53) + 12));
                        uint8_t t577 = ((t576 == ((uint32_t)4ULL)));
                        if (t577) {
                            gm_t9qkh0rsooc8d t578;
                            GM_ST(uint8_t, ((uint8_t *)&t578 + 0), ((uint8_t)69ULL));
                            gm_l109 = t578;
                        } else {
                            uint32_t t579 = GM_LD(uint32_t, (((uint8_t *)&gm_l53) + 12));
                            uint8_t t580 = ((t579 == ((uint32_t)1ULL)));
                            if (t580) {
                                gm_t9qkh0rsooc8d t581;
                                GM_ST(uint8_t, ((uint8_t *)&t581 + 0), ((uint8_t)67ULL));
                                gm_l110 = t581;
                            } else {
                                gm_t9qkh0rsooc8d t582;
                                GM_ST(uint8_t, ((uint8_t *)&t582 + 0), ((uint8_t)68ULL));
                                gm_l110 = t582;
                            }
                            gm_t9qkh0rsooc8d t583 = gm_l110;
                            gm_l109 = t583;
                        }
                        gm_t9qkh0rsooc8d t584 = gm_l109;
                        gm_l108 = t584;
                    }
                    gm_t9qkh0rsooc8d t585 = gm_l108;
                    uint32_t t586 = GM_LD(uint32_t, (((uint8_t *)&gm_l53) + 16));
                    uint16_t t587 = (uint16_t)t586;
                    uint32_t t588 = GM_LD(uint32_t, ((uint8_t *)&gm_l53));
                    uint32_t t589 = GM_LD(uint32_t, (((uint8_t *)&gm_l53) + 4));
                    uint32_t t590 = GM_LD(uint32_t, (((uint8_t *)&gm_l53) + 8));
                    gm_tb1f35qilrmun t591;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t591 + 0), t585);
                    GM_ST(uint8_t, ((uint8_t *)&t591 + 1), ((uint8_t)0ULL));
                    GM_ST(uint16_t, ((uint8_t *)&t591 + 2), t587);
                    GM_ST(uint32_t, ((uint8_t *)&t591 + 4), t588);
                    GM_ST(uint32_t, ((uint8_t *)&t591 + 8), t589);
                    GM_ST(uint32_t, ((uint8_t *)&t591 + 12), t590);
                    F.ret = t591;
                    goto gm_fret;
                }
gm_lb52:;
            }
            {
                {
                    uint8_t t592 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t593 = (uint8_t)(t592 == ((uint8_t)13ULL));
                    if (!t593) {
                        goto gm_lb53;
                    }
                    memmove(((uint8_t *)&gm_l54), (((uint8_t *)t100) + 8), 32);
                    uint8_t t594 = gm_l1;
                    uint8_t t595 = (uint8_t)!t594;
                    if (t595) {
                        gm_l112 = ((uint8_t)1);
                    } else {
                        uint32_t t596 = GM_LD(uint32_t, (((uint8_t *)&gm_l54) + 8));
                        uint64_t t597 = (uint64_t)t596;
                        uint8_t t598 = gm_f_word_size_mu6ni08(t597);
                        uint8_t t599 = (uint8_t)!t598;
                        gm_l112 = t599;
                    }
                    uint8_t t600 = gm_l112;
                    if (t600) {
                        gm_l111 = ((uint8_t)1);
                    } else {
                        uint32_t t601 = GM_LD(uint32_t, (((uint8_t *)&gm_l54) + 12));
                        uint8_t t602 = gm_f_bc_align_6s4vol1(t601);
                        uint8_t t603 = (uint8_t)!t602;
                        gm_l111 = t603;
                    }
                    uint8_t t604 = gm_l111;
                    if (t604) {
                        gm_tb1f35qilrmun t605 = gm_f_bc_free_v10vouf();
                        F.ret = t605;
                        goto gm_fret;
                    }
                    uint32_t t606 = GM_LD(uint32_t, (((uint8_t *)&gm_l54) + 8));
                    uint8_t t607 = ((t606 == ((uint32_t)8ULL)));
                    if (t607) {
                        gm_t9qkh0rsooc8d t608;
                        GM_ST(uint8_t, ((uint8_t *)&t608 + 0), ((uint8_t)74ULL));
                        gm_l113 = t608;
                    } else {
                        uint32_t t609 = GM_LD(uint32_t, (((uint8_t *)&gm_l54) + 8));
                        uint8_t t610 = ((t609 == ((uint32_t)4ULL)));
                        if (t610) {
                            gm_t9qkh0rsooc8d t611;
                            GM_ST(uint8_t, ((uint8_t *)&t611 + 0), ((uint8_t)73ULL));
                            gm_l114 = t611;
                        } else {
                            uint32_t t612 = GM_LD(uint32_t, (((uint8_t *)&gm_l54) + 8));
                            uint8_t t613 = ((t612 == ((uint32_t)1ULL)));
                            if (t613) {
                                gm_t9qkh0rsooc8d t614;
                                GM_ST(uint8_t, ((uint8_t *)&t614 + 0), ((uint8_t)71ULL));
                                gm_l115 = t614;
                            } else {
                                gm_t9qkh0rsooc8d t615;
                                GM_ST(uint8_t, ((uint8_t *)&t615 + 0), ((uint8_t)72ULL));
                                gm_l115 = t615;
                            }
                            gm_t9qkh0rsooc8d t616 = gm_l115;
                            gm_l114 = t616;
                        }
                        gm_t9qkh0rsooc8d t617 = gm_l114;
                        gm_l113 = t617;
                    }
                    gm_t9qkh0rsooc8d t618 = gm_l113;
                    uint32_t t619 = GM_LD(uint32_t, (((uint8_t *)&gm_l54) + 12));
                    uint16_t t620 = (uint16_t)t619;
                    uint32_t t621 = GM_LD(uint32_t, ((uint8_t *)&gm_l54));
                    uint32_t t622 = GM_LD(uint32_t, (((uint8_t *)&gm_l54) + 16));
                    uint32_t t623 = GM_LD(uint32_t, (((uint8_t *)&gm_l54) + 4));
                    gm_tb1f35qilrmun t624;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t624 + 0), t618);
                    GM_ST(uint8_t, ((uint8_t *)&t624 + 1), ((uint8_t)0ULL));
                    GM_ST(uint16_t, ((uint8_t *)&t624 + 2), t620);
                    GM_ST(uint32_t, ((uint8_t *)&t624 + 4), t621);
                    GM_ST(uint32_t, ((uint8_t *)&t624 + 8), t622);
                    GM_ST(uint32_t, ((uint8_t *)&t624 + 12), t623);
                    F.ret = t624;
                    goto gm_fret;
                }
gm_lb53:;
                {
                    uint8_t t625 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t626 = (uint8_t)(t625 == ((uint8_t)14ULL));
                    if (!t626) {
                        goto gm_lb54;
                    }
                    memmove(((uint8_t *)&F.l55), (((uint8_t *)t100) + 8), 40);
                    uint8_t t627 = gm_l1;
                    uint8_t t628 = (uint8_t)!t627;
                    if (t628) {
                        gm_l119 = ((uint8_t)1);
                    } else {
                        uint32_t t629 = GM_LD(uint32_t, (((uint8_t *)&F.l55) + 24));
                        uint64_t t630 = (uint64_t)t629;
                        uint8_t t631 = gm_f_word_size_mu6ni08(t630);
                        uint8_t t632 = (uint8_t)!t631;
                        gm_l119 = t632;
                    }
                    uint8_t t633 = gm_l119;
                    if (t633) {
                        gm_l118 = ((uint8_t)1);
                    } else {
                        uint32_t t634 = GM_LD(uint32_t, (((uint8_t *)&F.l55) + 28));
                        uint32_t t635 = GM_LD(uint32_t, (((uint8_t *)&F.l55) + 24));
                        uint8_t t636 = (!(t634 == t635));
                        gm_l118 = t636;
                    }
                    uint8_t t637 = gm_l118;
                    if (t637) {
                        gm_l117 = ((uint8_t)1);
                    } else {
                        uint32_t t638 = GM_LD(uint32_t, (((uint8_t *)&F.l55) + 8));
                        uint32_t t639 = GM_LD(uint32_t, (((uint8_t *)&F.l55) + 4));
                        uint32_t r640; bool o641 = __builtin_add_overflow(t639, ((uint32_t)1ULL), &r640);
                        if (o641) gm_trap("integer overflow in `+`", gm_src6unagr666v56, 448, 97);
                        uint32_t t642 = r640;
                        uint8_t t643 = (!(t638 == t642));
                        gm_l117 = t643;
                    }
                    uint8_t t644 = gm_l117;
                    if (t644) {
                        gm_l116 = ((uint8_t)1);
                    } else {
                        uint64_t t645 = GM_LD(uint64_t, (((uint8_t *)&F.l55) + 16));
                        uint8_t t646 = gm_f_fits16_1bnmjva(t645);
                        uint8_t t647 = (uint8_t)!t646;
                        gm_l116 = t647;
                    }
                    uint8_t t648 = gm_l116;
                    if (t648) {
                        gm_tb1f35qilrmun t649 = gm_f_bc_free_v10vouf();
                        F.ret = t649;
                        goto gm_fret;
                    }
                    uint32_t t650 = GM_LD(uint32_t, (((uint8_t *)&F.l55) + 24));
                    gm_t9qkh0rsooc8d t651;
                    GM_ST(uint8_t, ((uint8_t *)&t651 + 0), ((uint8_t)75ULL));
                    uint8_t t652 = (uint8_t)t650;
                    uint64_t t653 = GM_LD(uint64_t, (((uint8_t *)&F.l55) + 16));
                    uint16_t t654 = (uint16_t)t653;
                    uint32_t t655 = GM_LD(uint32_t, ((uint8_t *)&F.l55));
                    uint32_t t656 = GM_LD(uint32_t, (((uint8_t *)&F.l55) + 4));
                    uint32_t t657 = GM_LD(uint32_t, (((uint8_t *)&F.l55) + 12));
                    gm_tb1f35qilrmun t658;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t658 + 0), t651);
                    GM_ST(uint8_t, ((uint8_t *)&t658 + 1), t652);
                    GM_ST(uint16_t, ((uint8_t *)&t658 + 2), t654);
                    GM_ST(uint32_t, ((uint8_t *)&t658 + 4), t655);
                    GM_ST(uint32_t, ((uint8_t *)&t658 + 8), t656);
                    GM_ST(uint32_t, ((uint8_t *)&t658 + 12), t657);
                    F.ret = t658;
                    goto gm_fret;
                }
gm_lb54:;
            }
            {
                {
                    uint8_t t659 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t660 = (uint8_t)(t659 == ((uint8_t)15ULL));
                    if (!t660) {
                        goto gm_lb55;
                    }
                    memmove(((uint8_t *)&F.l56), (((uint8_t *)t100) + 8), 48);
                    uint8_t t661 = gm_l1;
                    uint8_t t662 = (uint8_t)!t661;
                    if (t662) {
                        gm_l123 = ((uint8_t)1);
                    } else {
                        uint32_t t663 = GM_LD(uint32_t, (((uint8_t *)&F.l56) + 24));
                        uint64_t t664 = (uint64_t)t663;
                        uint8_t t665 = gm_f_word_size_mu6ni08(t664);
                        uint8_t t666 = (uint8_t)!t665;
                        gm_l123 = t666;
                    }
                    uint8_t t667 = gm_l123;
                    if (t667) {
                        gm_l122 = ((uint8_t)1);
                    } else {
                        uint32_t t668 = GM_LD(uint32_t, (((uint8_t *)&F.l56) + 28));
                        uint32_t t669 = GM_LD(uint32_t, (((uint8_t *)&F.l56) + 24));
                        uint8_t t670 = (!(t668 == t669));
                        gm_l122 = t670;
                    }
                    uint8_t t671 = gm_l122;
                    if (t671) {
                        gm_l121 = ((uint8_t)1);
                    } else {
                        uint32_t t672 = GM_LD(uint32_t, (((uint8_t *)&F.l56) + 4));
                        uint32_t t673 = GM_LD(uint32_t, ((uint8_t *)&F.l56));
                        uint32_t r674; bool o675 = __builtin_add_overflow(t673, ((uint32_t)1ULL), &r674);
                        if (o675) gm_trap("integer overflow in `+`", gm_src6unagr666v56, 452, 97);
                        uint32_t t676 = r674;
                        uint8_t t677 = (!(t672 == t676));
                        gm_l121 = t677;
                    }
                    uint8_t t678 = gm_l121;
                    if (t678) {
                        gm_l120 = ((uint8_t)1);
                    } else {
                        uint64_t t679 = GM_LD(uint64_t, (((uint8_t *)&F.l56) + 16));
                        uint8_t t680 = gm_f_fits16_1bnmjva(t679);
                        uint8_t t681 = (uint8_t)!t680;
                        gm_l120 = t681;
                    }
                    uint8_t t682 = gm_l120;
                    if (t682) {
                        gm_tb1f35qilrmun t683 = gm_f_bc_free_v10vouf();
                        F.ret = t683;
                        goto gm_fret;
                    }
                    uint32_t t684 = GM_LD(uint32_t, (((uint8_t *)&F.l56) + 24));
                    gm_t9qkh0rsooc8d t685;
                    GM_ST(uint8_t, ((uint8_t *)&t685 + 0), ((uint8_t)76ULL));
                    uint8_t t686 = (uint8_t)t684;
                    uint64_t t687 = GM_LD(uint64_t, (((uint8_t *)&F.l56) + 16));
                    uint16_t t688 = (uint16_t)t687;
                    uint32_t t689 = GM_LD(uint32_t, ((uint8_t *)&F.l56));
                    uint32_t t690 = GM_LD(uint32_t, (((uint8_t *)&F.l56) + 32));
                    uint32_t t691 = GM_LD(uint32_t, (((uint8_t *)&F.l56) + 8));
                    gm_tb1f35qilrmun t692;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t692 + 0), t685);
                    GM_ST(uint8_t, ((uint8_t *)&t692 + 1), t686);
                    GM_ST(uint16_t, ((uint8_t *)&t692 + 2), t688);
                    GM_ST(uint32_t, ((uint8_t *)&t692 + 4), t689);
                    GM_ST(uint32_t, ((uint8_t *)&t692 + 8), t690);
                    GM_ST(uint32_t, ((uint8_t *)&t692 + 12), t691);
                    F.ret = t692;
                    goto gm_fret;
                }
gm_lb55:;
                {
                    uint8_t t693 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t694 = (uint8_t)(t693 == ((uint8_t)16ULL));
                    if (!t694) {
                        goto gm_lb56;
                    }
                    memmove(((uint8_t *)&F.l57), (((uint8_t *)t100) + 8), 40);
                    uint8_t t695 = gm_l1;
                    uint8_t t696 = (uint8_t)!t695;
                    if (t696) {
                        gm_l126 = ((uint8_t)1);
                    } else {
                        uint32_t t697 = GM_LD(uint32_t, (((uint8_t *)&F.l57) + 24));
                        uint64_t t698 = (uint64_t)t697;
                        uint8_t t699 = gm_f_word_size_mu6ni08(t698);
                        uint8_t t700 = (uint8_t)!t699;
                        gm_l126 = t700;
                    }
                    uint8_t t701 = gm_l126;
                    if (t701) {
                        gm_l125 = ((uint8_t)1);
                    } else {
                        uint32_t t702 = GM_LD(uint32_t, (((uint8_t *)&F.l57) + 28));
                        uint32_t t703 = GM_LD(uint32_t, (((uint8_t *)&F.l57) + 24));
                        uint8_t t704 = (!(t702 == t703));
                        gm_l125 = t704;
                    }
                    uint8_t t705 = gm_l125;
                    if (t705) {
                        gm_l124 = ((uint8_t)1);
                    } else {
                        uint64_t t706 = GM_LD(uint64_t, (((uint8_t *)&F.l57) + 16));
                        uint8_t t707 = gm_f_fits16_1bnmjva(t706);
                        uint8_t t708 = (uint8_t)!t707;
                        gm_l124 = t708;
                    }
                    uint8_t t709 = gm_l124;
                    if (t709) {
                        gm_tb1f35qilrmun t710 = gm_f_bc_free_v10vouf();
                        F.ret = t710;
                        goto gm_fret;
                    }
                    uint32_t t711 = GM_LD(uint32_t, (((uint8_t *)&F.l57) + 24));
                    gm_t9qkh0rsooc8d t712;
                    GM_ST(uint8_t, ((uint8_t *)&t712 + 0), ((uint8_t)77ULL));
                    uint8_t t713 = (uint8_t)t711;
                    uint64_t t714 = GM_LD(uint64_t, (((uint8_t *)&F.l57) + 16));
                    uint16_t t715 = (uint16_t)t714;
                    uint32_t t716 = GM_LD(uint32_t, ((uint8_t *)&F.l57));
                    uint32_t t717 = GM_LD(uint32_t, (((uint8_t *)&F.l57) + 4));
                    uint32_t t718 = GM_LD(uint32_t, (((uint8_t *)&F.l57) + 8));
                    gm_tb1f35qilrmun t719;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t719 + 0), t712);
                    GM_ST(uint8_t, ((uint8_t *)&t719 + 1), t713);
                    GM_ST(uint16_t, ((uint8_t *)&t719 + 2), t715);
                    GM_ST(uint32_t, ((uint8_t *)&t719 + 4), t716);
                    GM_ST(uint32_t, ((uint8_t *)&t719 + 8), t717);
                    GM_ST(uint32_t, ((uint8_t *)&t719 + 12), t718);
                    F.ret = t719;
                    goto gm_fret;
                }
gm_lb56:;
            }
            {
                {
                    uint8_t t720 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t721 = (uint8_t)(t720 == ((uint8_t)17ULL));
                    if (!t721) {
                        goto gm_lb57;
                    }
                    memmove(((uint8_t *)&F.l58), (((uint8_t *)t100) + 8), 40);
                    uint8_t t722 = gm_l1;
                    uint8_t t723 = (uint8_t)!t722;
                    if (t723) {
                        gm_l129 = ((uint8_t)1);
                    } else {
                        uint32_t t724 = GM_LD(uint32_t, (((uint8_t *)&F.l58) + 16));
                        uint64_t t725 = (uint64_t)t724;
                        uint8_t t726 = gm_f_word_size_mu6ni08(t725);
                        uint8_t t727 = (uint8_t)!t726;
                        gm_l129 = t727;
                    }
                    uint8_t t728 = gm_l129;
                    if (t728) {
                        gm_l128 = ((uint8_t)1);
                    } else {
                        uint32_t t729 = GM_LD(uint32_t, (((uint8_t *)&F.l58) + 20));
                        uint32_t t730 = GM_LD(uint32_t, (((uint8_t *)&F.l58) + 16));
                        uint8_t t731 = (!(t729 == t730));
                        gm_l128 = t731;
                    }
                    uint8_t t732 = gm_l128;
                    if (t732) {
                        gm_l127 = ((uint8_t)1);
                    } else {
                        uint64_t t733 = GM_LD(uint64_t, (((uint8_t *)&F.l58) + 8));
                        uint8_t t734 = gm_f_fits16_1bnmjva(t733);
                        uint8_t t735 = (uint8_t)!t734;
                        gm_l127 = t735;
                    }
                    uint8_t t736 = gm_l127;
                    if (t736) {
                        gm_tb1f35qilrmun t737 = gm_f_bc_free_v10vouf();
                        F.ret = t737;
                        goto gm_fret;
                    }
                    uint32_t t738 = GM_LD(uint32_t, (((uint8_t *)&F.l58) + 16));
                    gm_t9qkh0rsooc8d t739;
                    GM_ST(uint8_t, ((uint8_t *)&t739 + 0), ((uint8_t)78ULL));
                    uint8_t t740 = (uint8_t)t738;
                    uint64_t t741 = GM_LD(uint64_t, (((uint8_t *)&F.l58) + 8));
                    uint16_t t742 = (uint16_t)t741;
                    uint32_t t743 = GM_LD(uint32_t, ((uint8_t *)&F.l58));
                    uint32_t t744 = GM_LD(uint32_t, (((uint8_t *)&F.l58) + 24));
                    uint32_t t745 = GM_LD(uint32_t, (((uint8_t *)&F.l58) + 4));
                    gm_tb1f35qilrmun t746;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t746 + 0), t739);
                    GM_ST(uint8_t, ((uint8_t *)&t746 + 1), t740);
                    GM_ST(uint16_t, ((uint8_t *)&t746 + 2), t742);
                    GM_ST(uint32_t, ((uint8_t *)&t746 + 4), t743);
                    GM_ST(uint32_t, ((uint8_t *)&t746 + 8), t744);
                    GM_ST(uint32_t, ((uint8_t *)&t746 + 12), t745);
                    F.ret = t746;
                    goto gm_fret;
                }
gm_lb57:;
                {
                    uint8_t t747 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t748 = (uint8_t)(t747 == ((uint8_t)18ULL));
                    if (!t748) {
                        goto gm_lb58;
                    }
                    gm_t4h1llua034m2 t749 = GM_LD(gm_t4h1llua034m2, (((uint8_t *)t100) + 8));
                    gm_l59 = t749;
                    uint64_t t750 = GM_LD(uint64_t, (((uint8_t *)&gm_l59) + 8));
                    uint8_t t751 = gm_f_fits32_a5qt6pu(t750);
                    uint8_t t752 = (uint8_t)!t751;
                    if (t752) {
                        gm_tb1f35qilrmun t753 = gm_f_bc_free_v10vouf();
                        F.ret = t753;
                        goto gm_fret;
                    }
                    uint32_t t754 = GM_LD(uint32_t, ((uint8_t *)&gm_l59));
                    uint32_t t755 = GM_LD(uint32_t, (((uint8_t *)&gm_l59) + 4));
                    uint64_t t756 = GM_LD(uint64_t, (((uint8_t *)&gm_l59) + 8));
                    gm_t9qkh0rsooc8d t757;
                    GM_ST(uint8_t, ((uint8_t *)&t757 + 0), ((uint8_t)79ULL));
                    uint32_t t758 = (uint32_t)t756;
                    gm_tb1f35qilrmun t759 = gm_f_bc3_m9eakek(t757, t754, t755, t758);
                    F.ret = t759;
                    goto gm_fret;
                }
gm_lb58:;
                {
                    uint8_t t760 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t761 = (uint8_t)(t760 == ((uint8_t)9ULL));
                    if (!t761) {
                        goto gm_lb59;
                    }
                    memmove(((uint8_t *)&F.l60), (((uint8_t *)t100) + 8), 40);
                    uint8_t t762 = GM_LD(uint8_t, ((uint8_t *)&F.l60));
                    if (t762) {
                        gm_t9qkh0rsooc8d t763;
                        GM_ST(uint8_t, ((uint8_t *)&t763 + 0), ((uint8_t)81ULL));
                        gm_l130 = t763;
                    } else {
                        gm_t9qkh0rsooc8d t764;
                        GM_ST(uint8_t, ((uint8_t *)&t764 + 0), ((uint8_t)80ULL));
                        gm_l130 = t764;
                    }
                    gm_t9qkh0rsooc8d t765 = gm_l130;
                    uint32_t t766 = GM_LD(uint32_t, (((uint8_t *)&F.l60) + 4));
                    uint16_t t767 = (uint16_t)t766;
                    uint32_t t768 = GM_LD(uint32_t, (((uint8_t *)&F.l60) + 16));
                    uint32_t t769 = GM_LD(uint32_t, (((uint8_t *)&F.l60) + 20));
                    uint32_t t770 = GM_LD(uint32_t, (((uint8_t *)&F.l60) + 24));
                    gm_tb1f35qilrmun t771;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t771 + 0), t765);
                    GM_ST(uint8_t, ((uint8_t *)&t771 + 1), ((uint8_t)0ULL));
                    GM_ST(uint16_t, ((uint8_t *)&t771 + 2), t767);
                    GM_ST(uint32_t, ((uint8_t *)&t771 + 4), t768);
                    GM_ST(uint32_t, ((uint8_t *)&t771 + 8), t769);
                    GM_ST(uint32_t, ((uint8_t *)&t771 + 12), t770);
                    F.ret = t771;
                    goto gm_fret;
                }
gm_lb59:;
            }
            {
                {
                    uint8_t t772 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t773 = (uint8_t)(t772 == ((uint8_t)10ULL));
                    if (!t773) {
                        goto gm_lb60;
                    }
                    memmove(((uint8_t *)&F.l61), (((uint8_t *)t100) + 8), 40);
                    {
                        {
                            uint8_t t774 = GM_LD(uint8_t, (((uint8_t *)&F.l61) + 16));
                            uint8_t t775 = (uint8_t)!t774;
                            if (!t775) {
                                goto gm_lb62;
                            }
                            uint8_t t776 = GM_LD(uint8_t, ((uint8_t *)&F.l61));
                            if (!t776) {
                                goto gm_lb62;
                            }
                            gm_tb1f35qilrmun t777 = gm_f_bc_free_v10vouf();
                            F.ret = t777;
                            goto gm_fret;
                        }
gm_lb62:;
                    }
gm_lb61:;
                    uint8_t t778 = GM_LD(uint8_t, (((uint8_t *)&F.l61) + 16));
                    if (t778) {
                        gm_t9qkh0rsooc8d t779;
                        GM_ST(uint8_t, ((uint8_t *)&t779 + 0), ((uint8_t)82ULL));
                        gm_l131 = t779;
                    } else {
                        gm_t9qkh0rsooc8d t780;
                        GM_ST(uint8_t, ((uint8_t *)&t780 + 0), ((uint8_t)83ULL));
                        gm_l131 = t780;
                    }
                    gm_t9qkh0rsooc8d t781 = gm_l131;
                    uint32_t t782 = GM_LD(uint32_t, (((uint8_t *)&F.l61) + 4));
                    uint16_t t783 = (uint16_t)t782;
                    uint32_t t784 = GM_LD(uint32_t, (((uint8_t *)&F.l61) + 20));
                    uint32_t t785 = GM_LD(uint32_t, (((uint8_t *)&F.l61) + 24));
                    uint32_t t786 = GM_LD(uint32_t, (((uint8_t *)&F.l61) + 28));
                    gm_tb1f35qilrmun t787;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t787 + 0), t781);
                    GM_ST(uint8_t, ((uint8_t *)&t787 + 1), ((uint8_t)0ULL));
                    GM_ST(uint16_t, ((uint8_t *)&t787 + 2), t783);
                    GM_ST(uint32_t, ((uint8_t *)&t787 + 4), t784);
                    GM_ST(uint32_t, ((uint8_t *)&t787 + 8), t785);
                    GM_ST(uint32_t, ((uint8_t *)&t787 + 12), t786);
                    F.ret = t787;
                    goto gm_fret;
                }
gm_lb60:;
                {
                    uint8_t t788 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t789 = (uint8_t)(t788 == ((uint8_t)20ULL));
                    if (!t789) {
                        goto gm_lb63;
                    }
                    memmove(((uint8_t *)&F.l62), (((uint8_t *)t100) + 8), 40);
                    gm_ts6oqppjh0ba3 t790 = GM_LD(gm_ts6oqppjh0ba3, ((uint8_t *)&F.l62));
                    uint8_t t791 = GM_LD(uint8_t, (((uint8_t *)&t790) + 0));
                    uint8_t t792 = (uint8_t)((t791 == ((uint8_t)4ULL)));
                    gm_ts6oqppjh0ba3 t793;
                    GM_ST(uint8_t, (uint8_t *)&t793 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t793 + 0), ((uint8_t)4ULL));
                    if (t792) {
                        uint8_t t794 = GM_LD(uint8_t, (((uint8_t *)&F.l62) + 4));
                        if (t794) {
                            gm_t9qkh0rsooc8d t795;
                            GM_ST(uint8_t, ((uint8_t *)&t795 + 0), ((uint8_t)86ULL));
                            gm_l132 = t795;
                        } else {
                            gm_t9qkh0rsooc8d t796;
                            GM_ST(uint8_t, ((uint8_t *)&t796 + 0), ((uint8_t)84ULL));
                            gm_l132 = t796;
                        }
                        gm_t9qkh0rsooc8d t797 = gm_l132;
                        uint32_t t798 = GM_LD(uint32_t, ((((uint8_t *)&F.l62) + 4) + 4));
                        uint16_t t799 = (uint16_t)t798;
                        uint32_t t800 = GM_LD(uint32_t, (((uint8_t *)&F.l62) + 20));
                        uint32_t t801 = GM_LD(uint32_t, (((uint8_t *)&F.l62) + 24));
                        uint32_t t802 = GM_LD(uint32_t, (((uint8_t *)&F.l62) + 28));
                        gm_tb1f35qilrmun t803;
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t803 + 0), t797);
                        GM_ST(uint8_t, ((uint8_t *)&t803 + 1), ((uint8_t)0ULL));
                        GM_ST(uint16_t, ((uint8_t *)&t803 + 2), t799);
                        GM_ST(uint32_t, ((uint8_t *)&t803 + 4), t800);
                        GM_ST(uint32_t, ((uint8_t *)&t803 + 8), t801);
                        GM_ST(uint32_t, ((uint8_t *)&t803 + 12), t802);
                        F.ret = t803;
                        goto gm_fret;
                    }
                    gm_ts6oqppjh0ba3 t804 = GM_LD(gm_ts6oqppjh0ba3, ((uint8_t *)&F.l62));
                    uint8_t t805 = GM_LD(uint8_t, (((uint8_t *)&t804) + 0));
                    uint8_t t806 = (uint8_t)((t805 == ((uint8_t)5ULL)));
                    gm_ts6oqppjh0ba3 t807;
                    GM_ST(uint8_t, (uint8_t *)&t807 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t807 + 0), ((uint8_t)5ULL));
                    if (t806) {
                        uint8_t t808 = GM_LD(uint8_t, (((uint8_t *)&F.l62) + 4));
                        if (t808) {
                            gm_t9qkh0rsooc8d t809;
                            GM_ST(uint8_t, ((uint8_t *)&t809 + 0), ((uint8_t)87ULL));
                            gm_l133 = t809;
                        } else {
                            gm_t9qkh0rsooc8d t810;
                            GM_ST(uint8_t, ((uint8_t *)&t810 + 0), ((uint8_t)85ULL));
                            gm_l133 = t810;
                        }
                        gm_t9qkh0rsooc8d t811 = gm_l133;
                        uint32_t t812 = GM_LD(uint32_t, ((((uint8_t *)&F.l62) + 4) + 4));
                        uint16_t t813 = (uint16_t)t812;
                        uint32_t t814 = GM_LD(uint32_t, (((uint8_t *)&F.l62) + 20));
                        uint32_t t815 = GM_LD(uint32_t, (((uint8_t *)&F.l62) + 24));
                        uint32_t t816 = GM_LD(uint32_t, (((uint8_t *)&F.l62) + 28));
                        gm_tb1f35qilrmun t817;
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t817 + 0), t811);
                        GM_ST(uint8_t, ((uint8_t *)&t817 + 1), ((uint8_t)0ULL));
                        GM_ST(uint16_t, ((uint8_t *)&t817 + 2), t813);
                        GM_ST(uint32_t, ((uint8_t *)&t817 + 4), t814);
                        GM_ST(uint32_t, ((uint8_t *)&t817 + 8), t815);
                        GM_ST(uint32_t, ((uint8_t *)&t817 + 12), t816);
                        F.ret = t817;
                        goto gm_fret;
                    }
                    gm_tb1f35qilrmun t818 = gm_f_bc_free_v10vouf();
                    F.ret = t818;
                    goto gm_fret;
                }
gm_lb63:;
            }
            {
                {
                    uint8_t t819 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t820 = (uint8_t)(t819 == ((uint8_t)64ULL));
                    if (!t820) {
                        goto gm_lb64;
                    }
                    memmove(((uint8_t *)&F.l63), (((uint8_t *)t100) + 8), 40);
                    uint8_t t821 = gm_l1;
                    uint8_t t822 = (uint8_t)!t821;
                    if (t822) {
                        gm_l137 = ((uint8_t)1);
                    } else {
                        uint32_t t823 = GM_LD(uint32_t, (((uint8_t *)&F.l63) + 12));
                        uint64_t t824 = (uint64_t)t823;
                        uint8_t t825 = gm_f_fits16_1bnmjva(t824);
                        uint8_t t826 = (uint8_t)!t825;
                        gm_l137 = t826;
                    }
                    uint8_t t827 = gm_l137;
                    if (t827) {
                        gm_l136 = ((uint8_t)1);
                    } else {
                        uint32_t t828 = GM_LD(uint32_t, (((uint8_t *)&F.l63) + 16));
                        uint64_t t829 = (uint64_t)t828;
                        uint8_t t830 = gm_f_fits16_1bnmjva(t829);
                        uint8_t t831 = (uint8_t)!t830;
                        gm_l136 = t831;
                    }
                    uint8_t t832 = gm_l136;
                    if (t832) {
                        gm_l135 = ((uint8_t)1);
                    } else {
                        uint64_t t833 = GM_LD(uint64_t, (((uint8_t *)&F.l63) + 24));
                        uint8_t t834 = gm_f_fits16_1bnmjva(t833);
                        uint8_t t835 = (uint8_t)!t834;
                        gm_l135 = t835;
                    }
                    uint8_t t836 = gm_l135;
                    if (t836) {
                        gm_l134 = ((uint8_t)1);
                    } else {
                        uint8_t t837 = GM_LD(uint8_t, (((uint8_t *)&F.l63) + 20));
                        if (t837) {
                            uint32_t t838 = GM_LD(uint32_t, (((uint8_t *)&F.l63) + 8));
                            uint32_t t839 = GM_LD(uint32_t, (((uint8_t *)&F.l63) + 4));
                            uint32_t r840; bool o841 = __builtin_add_overflow(t839, ((uint32_t)1ULL), &r840);
                            if (o841) gm_trap("integer overflow in `+`", gm_src6unagr666v56, 478, 128);
                            uint32_t t842 = r840;
                            uint8_t t843 = (!(t838 == t842));
                            gm_l138 = t843;
                        } else {
                            gm_l138 = ((uint8_t)0);
                        }
                        uint8_t t844 = gm_l138;
                        gm_l134 = t844;
                    }
                    uint8_t t845 = gm_l134;
                    if (t845) {
                        gm_tb1f35qilrmun t846 = gm_f_bc_free_v10vouf();
                        F.ret = t846;
                        goto gm_fret;
                    }
                    uint8_t t847 = GM_LD(uint8_t, (((uint8_t *)&F.l63) + 20));
                    if (t847) {
                        gm_l139 = ((uint8_t)1ULL);
                    } else {
                        gm_l139 = ((uint8_t)0ULL);
                    }
                    uint8_t t848 = gm_l139;
                    uint8_t t849 = GM_LD(uint8_t, (((uint8_t *)&F.l63) + 21));
                    if (t849) {
                        gm_l140 = ((uint8_t)2ULL);
                    } else {
                        gm_l140 = ((uint8_t)0ULL);
                    }
                    uint8_t t850 = gm_l140;
                    uint8_t t851 = (uint8_t)(t848 | t850);
                    gm_l64 = t851;
                    uint8_t t852 = gm_l64;
                    uint64_t t853 = GM_LD(uint64_t, (((uint8_t *)&F.l63) + 24));
                    gm_t9qkh0rsooc8d t854;
                    GM_ST(uint8_t, ((uint8_t *)&t854 + 0), ((uint8_t)91ULL));
                    uint16_t t855 = (uint16_t)t853;
                    uint32_t t856 = GM_LD(uint32_t, ((uint8_t *)&F.l63));
                    uint32_t t857 = GM_LD(uint32_t, (((uint8_t *)&F.l63) + 4));
                    uint32_t t858 = GM_LD(uint32_t, (((uint8_t *)&F.l63) + 12));
                    uint32_t t859 = GM_LD(uint32_t, (((uint8_t *)&F.l63) + 16));
                    unsigned __int128 n860 = (unsigned __int128)((uint64_t)16ULL);
                    if (n860 >= 32) gm_trap_shift((uint64_t)n860, "u32", gm_src6unagr666v56, 480, 98);
                    uint32_t r861 = (uint32_t)((uint32_t)t859 << (int)n860);
                    bool o862 = (uint32_t)(r861 >> (int)n860) != t859;
                    if (o862) gm_trap("integer overflow in `<<`", gm_src6unagr666v56, 480, 98);
                    uint32_t t863 = r861;
                    uint32_t t864 = (uint32_t)(t858 | t863);
                    gm_tb1f35qilrmun t865;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t865 + 0), t854);
                    GM_ST(uint8_t, ((uint8_t *)&t865 + 1), t852);
                    GM_ST(uint16_t, ((uint8_t *)&t865 + 2), t855);
                    GM_ST(uint32_t, ((uint8_t *)&t865 + 4), t856);
                    GM_ST(uint32_t, ((uint8_t *)&t865 + 8), t857);
                    GM_ST(uint32_t, ((uint8_t *)&t865 + 12), t864);
                    F.ret = t865;
                    goto gm_fret;
                }
gm_lb64:;
            }
            {
                {
                    uint8_t t866 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t867 = (uint8_t)(t866 == ((uint8_t)23ULL));
                    if (!t867) {
                        goto gm_lb65;
                    }
                    uint32_t t868 = GM_LD(uint32_t, (((uint8_t *)t100) + 8));
                    gm_l65 = t868;
                    uint32_t t869 = GM_LD(uint32_t, ((((uint8_t *)t100) + 8) + 4));
                    gm_l66 = t869;
                    uint32_t t870 = gm_l65;
                    uint32_t t871 = gm_l66;
                    gm_t9qkh0rsooc8d t872;
                    GM_ST(uint8_t, ((uint8_t *)&t872 + 0), ((uint8_t)93ULL));
                    gm_tb1f35qilrmun t873 = gm_f_bc3_m9eakek(t872, t870, t871, ((uint32_t)0ULL));
                    F.ret = t873;
                    goto gm_fret;
                }
gm_lb65:;
                {
                    uint8_t t874 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t875 = (uint8_t)(t874 == ((uint8_t)49ULL));
                    if (!t875) {
                        goto gm_lb66;
                    }
                    memmove(((uint8_t *)&gm_l67), (((uint8_t *)t100) + 8), 24);
                    uint32_t t876 = GM_LD(uint32_t, (((uint8_t *)&gm_l67) + 8));
                    uint32_t t877 = GM_LD(uint32_t, ((uint8_t *)gm_cl7gtp51edk));
                    uint8_t t878 = (!(t876 == t877));
                    gm_t9qkh0rsooc8d t879;
                    GM_ST(uint8_t, ((uint8_t *)&t879 + 0), ((uint8_t)94ULL));
                    if (t878) {
                        gm_l141 = ((uint8_t)1ULL);
                    } else {
                        gm_l141 = ((uint8_t)0ULL);
                    }
                    uint8_t t880 = gm_l141;
                    uint8_t t881 = GM_LD(uint8_t, (((uint8_t *)&gm_l67) + 12));
                    if (t881) {
                        gm_l142 = ((uint8_t)2ULL);
                    } else {
                        gm_l142 = ((uint8_t)0ULL);
                    }
                    uint8_t t882 = gm_l142;
                    uint8_t t883 = (uint8_t)(t880 | t882);
                    uint32_t t884 = GM_LD(uint32_t, ((uint8_t *)&gm_l67));
                    uint32_t t885 = GM_LD(uint32_t, (((uint8_t *)&gm_l67) + 4));
                    uint32_t t886 = GM_LD(uint32_t, (((uint8_t *)&gm_l67) + 8));
                    uint32_t t887 = GM_LD(uint32_t, ((uint8_t *)gm_cl7gtp51edk));
                    uint8_t t888 = ((t886 == t887));
                    if (t888) {
                        gm_l143 = ((uint32_t)0ULL);
                    } else {
                        uint32_t t889 = GM_LD(uint32_t, (((uint8_t *)&gm_l67) + 8));
                        gm_l143 = t889;
                    }
                    uint32_t t890 = gm_l143;
                    gm_tb1f35qilrmun t891;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t891 + 0), t879);
                    GM_ST(uint8_t, ((uint8_t *)&t891 + 1), t883);
                    GM_ST(uint16_t, ((uint8_t *)&t891 + 2), ((uint16_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t891 + 4), t884);
                    GM_ST(uint32_t, ((uint8_t *)&t891 + 8), t885);
                    GM_ST(uint32_t, ((uint8_t *)&t891 + 12), t890);
                    F.ret = t891;
                    goto gm_fret;
                }
gm_lb66:;
                {
                    uint8_t t892 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t893 = (uint8_t)(t892 == ((uint8_t)38ULL));
                    if (!t893) {
                        goto gm_lb67;
                    }
                    uint32_t t894 = GM_LD(uint32_t, (((uint8_t *)t100) + 8));
                    gm_l68 = t894;
                    uint64_t t895 = GM_LD(uint64_t, ((((uint8_t *)t100) + 8) + 8));
                    gm_l69 = t895;
                    uint64_t t896 = gm_l69;
                    uint8_t t897 = ((t896 == ((uint64_t)0ULL)));
                    if (t897) {
                        gm_l145 = ((uint8_t)1);
                    } else {
                        uint64_t t898 = gm_l69;
                        uint64_t t899 = gm_l69;
                        uint64_t r900; bool o901 = __builtin_sub_overflow(t899, ((uint64_t)1ULL), &r900);
                        if (o901) gm_trap("integer overflow in `-`", gm_src6unagr666v56, 485, 39);
                        uint64_t t902 = r900;
                        uint64_t t903 = (uint64_t)(t898 & t902);
                        uint8_t t904 = (!(t903 == ((uint64_t)0ULL)));
                        gm_l145 = t904;
                    }
                    uint8_t t905 = gm_l145;
                    if (t905) {
                        gm_l144 = ((uint8_t)1);
                    } else {
                        uint64_t t906 = gm_l69;
                        uint8_t t907 = gm_f_fits32_a5qt6pu(t906);
                        uint8_t t908 = (uint8_t)!t907;
                        gm_l144 = t908;
                    }
                    uint8_t t909 = gm_l144;
                    if (t909) {
                        gm_tb1f35qilrmun t910 = gm_f_bc_free_v10vouf();
                        F.ret = t910;
                        goto gm_fret;
                    }
                    uint32_t t911 = gm_l68;
                    uint64_t t912 = gm_l69;
                    uint64_t r913; bool o914 = __builtin_sub_overflow(t912, ((uint64_t)1ULL), &r913);
                    if (o914) gm_trap("integer overflow in `-`", gm_src6unagr666v56, 486, 39);
                    uint64_t t915 = r913;
                    gm_t9qkh0rsooc8d t916;
                    GM_ST(uint8_t, ((uint8_t *)&t916 + 0), ((uint8_t)92ULL));
                    uint32_t t917 = (uint32_t)t915;
                    gm_tb1f35qilrmun t918 = gm_f_bc3_m9eakek(t916, t911, t917, ((uint32_t)0ULL));
                    F.ret = t918;
                    goto gm_fret;
                }
gm_lb67:;
            }
            {
                {
                    uint8_t t919 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t920 = (uint8_t)(t919 == ((uint8_t)36ULL));
                    if (!t920) {
                        goto gm_lb68;
                    }
                    memmove(((uint8_t *)&gm_l70), (((uint8_t *)t100) + 8), 32);
                    uint32_t t921 = GM_LD(uint32_t, (((uint8_t *)&gm_l70) + 12));
                    uint64_t t922 = (uint64_t)t921;
                    uint8_t t923 = gm_f_fits16_1bnmjva(t922);
                    uint8_t t924 = (uint8_t)!t923;
                    if (t924) {
                        gm_l146 = ((uint8_t)1);
                    } else {
                        uint32_t t925 = GM_LD(uint32_t, (((uint8_t *)&gm_l70) + 16));
                        uint8_t t926 = gm_f_bc_align_6s4vol1(t925);
                        uint8_t t927 = (uint8_t)!t926;
                        gm_l146 = t927;
                    }
                    uint8_t t928 = gm_l146;
                    if (t928) {
                        gm_tb1f35qilrmun t929 = gm_f_bc_free_v10vouf();
                        F.ret = t929;
                        goto gm_fret;
                    }
                    uint32_t t930 = GM_LD(uint32_t, (((uint8_t *)&gm_l70) + 16));
                    gm_t9qkh0rsooc8d t931;
                    GM_ST(uint8_t, ((uint8_t *)&t931 + 0), ((uint8_t)95ULL));
                    uint8_t t932 = gm_f_log2_acbegp6(t930);
                    uint32_t t933 = GM_LD(uint32_t, (((uint8_t *)&gm_l70) + 12));
                    uint16_t t934 = (uint16_t)t933;
                    uint32_t t935 = GM_LD(uint32_t, ((uint8_t *)&gm_l70));
                    uint32_t t936 = GM_LD(uint32_t, (((uint8_t *)&gm_l70) + 4));
                    uint32_t t937 = GM_LD(uint32_t, (((uint8_t *)&gm_l70) + 8));
                    gm_tb1f35qilrmun t938;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t938 + 0), t931);
                    GM_ST(uint8_t, ((uint8_t *)&t938 + 1), t932);
                    GM_ST(uint16_t, ((uint8_t *)&t938 + 2), t934);
                    GM_ST(uint32_t, ((uint8_t *)&t938 + 4), t935);
                    GM_ST(uint32_t, ((uint8_t *)&t938 + 8), t936);
                    GM_ST(uint32_t, ((uint8_t *)&t938 + 12), t937);
                    F.ret = t938;
                    goto gm_fret;
                }
gm_lb68:;
                {
                    uint8_t t939 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t940 = (uint8_t)(t939 == ((uint8_t)37ULL));
                    if (!t940) {
                        goto gm_lb69;
                    }
                    memmove(((uint8_t *)&gm_l71), (((uint8_t *)t100) + 8), 32);
                    uint32_t t941 = GM_LD(uint32_t, (((uint8_t *)&gm_l71) + 12));
                    uint64_t t942 = (uint64_t)t941;
                    uint8_t t943 = gm_f_fits16_1bnmjva(t942);
                    uint8_t t944 = (uint8_t)!t943;
                    if (t944) {
                        gm_l147 = ((uint8_t)1);
                    } else {
                        uint32_t t945 = GM_LD(uint32_t, (((uint8_t *)&gm_l71) + 16));
                        uint8_t t946 = gm_f_bc_align_6s4vol1(t945);
                        uint8_t t947 = (uint8_t)!t946;
                        gm_l147 = t947;
                    }
                    uint8_t t948 = gm_l147;
                    if (t948) {
                        gm_tb1f35qilrmun t949 = gm_f_bc_free_v10vouf();
                        F.ret = t949;
                        goto gm_fret;
                    }
                    uint32_t t950 = GM_LD(uint32_t, (((uint8_t *)&gm_l71) + 16));
                    gm_t9qkh0rsooc8d t951;
                    GM_ST(uint8_t, ((uint8_t *)&t951 + 0), ((uint8_t)96ULL));
                    uint8_t t952 = gm_f_log2_acbegp6(t950);
                    uint32_t t953 = GM_LD(uint32_t, (((uint8_t *)&gm_l71) + 12));
                    uint16_t t954 = (uint16_t)t953;
                    uint32_t t955 = GM_LD(uint32_t, ((uint8_t *)&gm_l71));
                    uint32_t t956 = GM_LD(uint32_t, (((uint8_t *)&gm_l71) + 8));
                    uint32_t t957 = GM_LD(uint32_t, (((uint8_t *)&gm_l71) + 4));
                    gm_tb1f35qilrmun t958;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t958 + 0), t951);
                    GM_ST(uint8_t, ((uint8_t *)&t958 + 1), t952);
                    GM_ST(uint16_t, ((uint8_t *)&t958 + 2), t954);
                    GM_ST(uint32_t, ((uint8_t *)&t958 + 4), t955);
                    GM_ST(uint32_t, ((uint8_t *)&t958 + 8), t956);
                    GM_ST(uint32_t, ((uint8_t *)&t958 + 12), t957);
                    F.ret = t958;
                    goto gm_fret;
                }
gm_lb69:;
                {
                    uint8_t t959 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t960 = (uint8_t)(t959 == ((uint8_t)59ULL));
                    if (!t960) {
                        goto gm_lb70;
                    }
                    uint32_t t961 = GM_LD(uint32_t, (((uint8_t *)t100) + 8));
                    gm_l72 = t961;
                    uint32_t t962 = GM_LD(uint32_t, ((((uint8_t *)t100) + 8) + 4));
                    gm_l73 = t962;
                    uint32_t t963 = GM_LD(uint32_t, ((((uint8_t *)t100) + 8) + 8));
                    gm_l74 = t963;
                    uint32_t t964 = gm_l72;
                    uint32_t t965 = gm_l73;
                    uint32_t t966 = gm_l74;
                    gm_t9qkh0rsooc8d t967;
                    GM_ST(uint8_t, ((uint8_t *)&t967 + 0), ((uint8_t)97ULL));
                    gm_tb1f35qilrmun t968 = gm_f_bc3_m9eakek(t967, t964, t965, t966);
                    F.ret = t968;
                    goto gm_fret;
                }
gm_lb70:;
            }
            {
                {
                    uint8_t t969 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t970 = (uint8_t)(t969 == ((uint8_t)63ULL));
                    if (!t970) {
                        goto gm_lb71;
                    }
                    uint32_t t971 = GM_LD(uint32_t, (((uint8_t *)t100) + 8));
                    gm_l75 = t971;
                    uint32_t t972 = GM_LD(uint32_t, ((((uint8_t *)t100) + 8) + 4));
                    gm_l76 = t972;
                    uint32_t t973 = gm_l75;
                    uint32_t t974 = gm_l76;
                    gm_t9qkh0rsooc8d t975;
                    GM_ST(uint8_t, ((uint8_t *)&t975 + 0), ((uint8_t)98ULL));
                    gm_tb1f35qilrmun t976 = gm_f_bc3_m9eakek(t975, t973, t974, ((uint32_t)0ULL));
                    F.ret = t976;
                    goto gm_fret;
                }
gm_lb71:;
                {
                    {
                        {
                            uint8_t t977 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                            uint8_t t978 = (uint8_t)(t977 == ((uint8_t)55ULL));
                            if (!t978) {
                                goto gm_lb74;
                            }
                            goto gm_lb73;
                        }
gm_lb74:;
                        {
                            uint8_t t979 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                            uint8_t t980 = (uint8_t)(t979 == ((uint8_t)58ULL));
                            if (!t980) {
                                goto gm_lb75;
                            }
                            goto gm_lb73;
                        }
gm_lb75:;
                        {
                            uint8_t t981 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                            uint8_t t982 = (uint8_t)(t981 == ((uint8_t)56ULL));
                            if (!t982) {
                                goto gm_lb76;
                            }
                            goto gm_lb73;
                        }
gm_lb76:;
                        goto gm_lb72;
                    }
gm_lb73:;
                    uint32_t t983 = GM_LD(uint32_t, ((uint8_t *)gm_cl7gtp51edk));
                    gm_t9qkh0rsooc8d t984;
                    GM_ST(uint8_t, ((uint8_t *)&t984 + 0), ((uint8_t)99ULL));
                    gm_tb1f35qilrmun t985 = gm_f_bc3_m9eakek(t984, t983, ((uint32_t)0ULL), ((uint32_t)0ULL));
                    F.ret = t985;
                    goto gm_fret;
                }
gm_lb72:;
                {
                    uint8_t t986 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t987 = (uint8_t)(t986 == ((uint8_t)5ULL));
                    if (!t987) {
                        goto gm_lb77;
                    }
                    memmove(((uint8_t *)&gm_l77), (((uint8_t *)t100) + 8), 32);
                    uint8_t t988 = gm_l1;
                    uint8_t t989 = (uint8_t)!t988;
                    if (t989) {
                        gm_l149 = ((uint8_t)1);
                    } else {
                        uint32_t t990 = GM_LD(uint32_t, (((uint8_t *)&gm_l77) + 4));
                        uint8_t t991 = (!(t990 == ((uint32_t)1ULL)));
                        gm_l149 = t991;
                    }
                    uint8_t t992 = gm_l149;
                    if (t992) {
                        gm_l148 = ((uint8_t)1);
                    } else {
                        uint32_t t993 = GM_LD(uint32_t, (((uint8_t *)&gm_l77) + 16));
                        uint64_t t994 = (uint64_t)t993;
                        uint8_t t995 = gm_f_word_size_mu6ni08(t994);
                        uint8_t t996 = (uint8_t)!t995;
                        gm_l148 = t996;
                    }
                    uint8_t t997 = gm_l148;
                    if (t997) {
                        gm_tb1f35qilrmun t998 = gm_f_bc_free_v10vouf();
                        F.ret = t998;
                        goto gm_fret;
                    }
                    uint32_t t999 = GM_LD(uint32_t, (((uint8_t *)&gm_l77) + 16));
                    gm_t9qkh0rsooc8d t1000;
                    GM_ST(uint8_t, ((uint8_t *)&t1000 + 0), ((uint8_t)88ULL));
                    uint8_t t1001 = (uint8_t)t999;
                    uint32_t t1002 = GM_LD(uint32_t, ((uint8_t *)&gm_l77));
                    uint32_t t1003 = GM_LD(uint32_t, (((uint8_t *)&gm_l77) + 8));
                    uint32_t t1004 = GM_LD(uint32_t, (((uint8_t *)&gm_l77) + 12));
                    gm_tb1f35qilrmun t1005;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t1005 + 0), t1000);
                    GM_ST(uint8_t, ((uint8_t *)&t1005 + 1), t1001);
                    GM_ST(uint16_t, ((uint8_t *)&t1005 + 2), ((uint16_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t1005 + 4), t1002);
                    GM_ST(uint32_t, ((uint8_t *)&t1005 + 8), t1003);
                    GM_ST(uint32_t, ((uint8_t *)&t1005 + 12), t1004);
                    F.ret = t1005;
                    goto gm_fret;
                }
gm_lb77:;
                {
                    uint8_t t1006 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t1007 = (uint8_t)(t1006 == ((uint8_t)11ULL));
                    if (!t1007) {
                        goto gm_lb78;
                    }
                    memmove(((uint8_t *)&gm_l78), (((uint8_t *)t100) + 8), 32);
                    uint8_t t1008 = gm_l1;
                    uint8_t t1009 = (uint8_t)!t1008;
                    if (t1009) {
                        gm_l151 = ((uint8_t)1);
                    } else {
                        uint32_t t1010 = GM_LD(uint32_t, ((uint8_t *)&gm_l78));
                        uint8_t t1011 = (!(t1010 == ((uint32_t)1ULL)));
                        gm_l151 = t1011;
                    }
                    uint8_t t1012 = gm_l151;
                    if (t1012) {
                        gm_l150 = ((uint8_t)1);
                    } else {
                        uint32_t t1013 = GM_LD(uint32_t, (((uint8_t *)&gm_l78) + 12));
                        uint64_t t1014 = (uint64_t)t1013;
                        uint8_t t1015 = gm_f_word_size_mu6ni08(t1014);
                        uint8_t t1016 = (uint8_t)!t1015;
                        gm_l150 = t1016;
                    }
                    uint8_t t1017 = gm_l150;
                    if (t1017) {
                        gm_tb1f35qilrmun t1018 = gm_f_bc_free_v10vouf();
                        F.ret = t1018;
                        goto gm_fret;
                    }
                    uint32_t t1019 = GM_LD(uint32_t, (((uint8_t *)&gm_l78) + 12));
                    gm_t9qkh0rsooc8d t1020;
                    GM_ST(uint8_t, ((uint8_t *)&t1020 + 0), ((uint8_t)89ULL));
                    uint8_t t1021 = (uint8_t)t1019;
                    uint32_t t1022 = GM_LD(uint32_t, (((uint8_t *)&gm_l78) + 4));
                    uint32_t t1023 = GM_LD(uint32_t, (((uint8_t *)&gm_l78) + 20));
                    uint32_t t1024 = GM_LD(uint32_t, (((uint8_t *)&gm_l78) + 8));
                    gm_tb1f35qilrmun t1025;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t1025 + 0), t1020);
                    GM_ST(uint8_t, ((uint8_t *)&t1025 + 1), t1021);
                    GM_ST(uint16_t, ((uint8_t *)&t1025 + 2), ((uint16_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t1025 + 4), t1022);
                    GM_ST(uint32_t, ((uint8_t *)&t1025 + 8), t1023);
                    GM_ST(uint32_t, ((uint8_t *)&t1025 + 12), t1024);
                    F.ret = t1025;
                    goto gm_fret;
                }
gm_lb78:;
            }
            {
                {
                    uint8_t t1026 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t1027 = (uint8_t)(t1026 == ((uint8_t)70ULL));
                    if (!t1027) {
                        goto gm_lb79;
                    }
                    uint32_t t1028 = GM_LD(uint32_t, (((uint8_t *)t100) + 8));
                    gm_l79 = t1028;
                    uint32_t t1029 = gm_l79;
                    gm_t9qkh0rsooc8d t1030;
                    GM_ST(uint8_t, ((uint8_t *)&t1030 + 0), ((uint8_t)100ULL));
                    gm_tb1f35qilrmun t1031 = gm_f_bc3_m9eakek(t1030, t1029, ((uint32_t)0ULL), ((uint32_t)0ULL));
                    F.ret = t1031;
                    goto gm_fret;
                }
gm_lb79:;
                {
                    uint8_t t1032 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t1033 = (uint8_t)(t1032 == ((uint8_t)72ULL));
                    if (!t1033) {
                        goto gm_lb80;
                    }
                    gm_t9qkh0rsooc8d t1034;
                    GM_ST(uint8_t, ((uint8_t *)&t1034 + 0), ((uint8_t)101ULL));
                    gm_tb1f35qilrmun t1035 = gm_f_bc3_m9eakek(t1034, ((uint32_t)0ULL), ((uint32_t)0ULL), ((uint32_t)0ULL));
                    F.ret = t1035;
                    goto gm_fret;
                }
gm_lb80:;
                {
                    uint8_t t1036 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                    uint8_t t1037 = (uint8_t)(t1036 == ((uint8_t)71ULL));
                    if (!t1037) {
                        goto gm_lb81;
                    }
                    uint32_t t1038 = GM_LD(uint32_t, (((uint8_t *)t100) + 8));
                    gm_l80 = t1038;
                    uint32_t t1039 = GM_LD(uint32_t, ((((uint8_t *)t100) + 8) + 4));
                    gm_l81 = t1039;
                    uint32_t t1040 = gm_l80;
                    uint32_t t1041 = gm_l81;
                    gm_t9qkh0rsooc8d t1042;
                    GM_ST(uint8_t, ((uint8_t *)&t1042 + 0), ((uint8_t)102ULL));
                    gm_tb1f35qilrmun t1043 = gm_f_bc3_m9eakek(t1042, t1040, t1041, ((uint32_t)0ULL));
                    F.ret = t1043;
                    goto gm_fret;
                }
gm_lb81:;
                {
                    {
                        {
                            uint8_t t1044 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                            uint8_t t1045 = (uint8_t)(t1044 == ((uint8_t)1ULL));
                            if (!t1045) {
                                goto gm_lb84;
                            }
                            goto gm_lb83;
                        }
gm_lb84:;
                        {
                            uint8_t t1046 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                            uint8_t t1047 = (uint8_t)(t1046 == ((uint8_t)2ULL));
                            if (!t1047) {
                                goto gm_lb85;
                            }
                            goto gm_lb83;
                        }
gm_lb85:;
                        {
                            uint8_t t1048 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                            uint8_t t1049 = (uint8_t)(t1048 == ((uint8_t)65ULL));
                            if (!t1049) {
                                goto gm_lb86;
                            }
                            goto gm_lb83;
                        }
gm_lb86:;
                        {
                            uint8_t t1050 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                            uint8_t t1051 = (uint8_t)(t1050 == ((uint8_t)67ULL));
                            if (!t1051) {
                                goto gm_lb87;
                            }
                            goto gm_lb83;
                        }
gm_lb87:;
                        {
                            uint8_t t1052 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                            uint8_t t1053 = (uint8_t)(t1052 == ((uint8_t)68ULL));
                            if (!t1053) {
                                goto gm_lb88;
                            }
                            goto gm_lb83;
                        }
gm_lb88:;
                        {
                            uint8_t t1054 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                            uint8_t t1055 = (uint8_t)(t1054 == ((uint8_t)74ULL));
                            if (!t1055) {
                                goto gm_lb89;
                            }
                            goto gm_lb83;
                        }
gm_lb89:;
                        {
                            uint8_t t1056 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                            uint8_t t1057 = (uint8_t)(t1056 == ((uint8_t)75ULL));
                            if (!t1057) {
                                goto gm_lb90;
                            }
                            goto gm_lb83;
                        }
gm_lb90:;
                        {
                            uint8_t t1058 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                            uint8_t t1059 = (uint8_t)(t1058 == ((uint8_t)76ULL));
                            if (!t1059) {
                                goto gm_lb91;
                            }
                            goto gm_lb83;
                        }
gm_lb91:;
                        {
                            uint8_t t1060 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                            uint8_t t1061 = (uint8_t)(t1060 == ((uint8_t)77ULL));
                            if (!t1061) {
                                goto gm_lb92;
                            }
                            goto gm_lb83;
                        }
gm_lb92:;
                        {
                            uint8_t t1062 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                            uint8_t t1063 = (uint8_t)(t1062 == ((uint8_t)78ULL));
                            if (!t1063) {
                                goto gm_lb93;
                            }
                            goto gm_lb83;
                        }
gm_lb93:;
                        {
                            uint8_t t1064 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                            uint8_t t1065 = (uint8_t)(t1064 == ((uint8_t)40ULL));
                            if (!t1065) {
                                goto gm_lb94;
                            }
                            goto gm_lb83;
                        }
gm_lb94:;
                        {
                            uint8_t t1066 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                            uint8_t t1067 = (uint8_t)(t1066 == ((uint8_t)41ULL));
                            if (!t1067) {
                                goto gm_lb95;
                            }
                            goto gm_lb83;
                        }
gm_lb95:;
                        {
                            uint8_t t1068 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                            uint8_t t1069 = (uint8_t)(t1068 == ((uint8_t)42ULL));
                            if (!t1069) {
                                goto gm_lb96;
                            }
                            goto gm_lb83;
                        }
gm_lb96:;
                        {
                            uint8_t t1070 = GM_LD(uint8_t, (((uint8_t *)t100) + 0));
                            uint8_t t1071 = (uint8_t)(t1070 == ((uint8_t)43ULL));
                            if (!t1071) {
                                goto gm_lb97;
                            }
                            goto gm_lb83;
                        }
gm_lb97:;
                        goto gm_lb82;
                    }
gm_lb83:;
                    gm_t9qkh0rsooc8d t1072;
                    GM_ST(uint8_t, ((uint8_t *)&t1072 + 0), ((uint8_t)1ULL));
                    gm_tb1f35qilrmun t1073;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t1073 + 0), t1072);
                    GM_ST(uint8_t, ((uint8_t *)&t1073 + 1), ((uint8_t)0ULL));
                    GM_ST(uint16_t, ((uint8_t *)&t1073 + 2), ((uint16_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t1073 + 4), ((uint32_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t1073 + 8), ((uint32_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t1073 + 12), ((uint32_t)0ULL));
                    F.ret = t1073;
                    goto gm_fret;
                }
gm_lb82:;
            }
            {
                {
                    gm_tb1f35qilrmun t1074 = gm_f_bc_free_v10vouf();
                    F.ret = t1074;
                    goto gm_fret;
                }
gm_lb98:;
                gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 355, 5);
            }
        }
gm_lb1:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint64_t gm_f_bc_pc_vubo137(void * a0, void * a1);
extern __attribute__((aligned(4))) uint8_t gm_f_fits16_1bnmjva(uint64_t a0);
extern __attribute__((aligned(4))) uint8_t gm_f_fits32_a5qt6pu(uint64_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_in_memory_t2cr4s3(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_in_memory_t2cr4s3 F;
    void * gm_l0; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 26u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    void * t9 = gm_l0;
    {
        {
            {
                {
                    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)t9) + 0));
                    uint8_t t11 = (uint8_t)(t10 == ((uint8_t)1ULL));
                    if (!t11) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)t9) + 0));
                    uint8_t t13 = (uint8_t)(t12 == ((uint8_t)2ULL));
                    if (!t13) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)t9) + 0));
                    uint8_t t15 = (uint8_t)(t14 == ((uint8_t)65ULL));
                    if (!t15) {
                        goto gm_lb6;
                    }
                    goto gm_lb3;
                }
gm_lb6:;
                goto gm_lb2;
            }
gm_lb3:;
            gm_l1 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l1 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb7:;
        gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 532, 43);
    }
gm_lb1:;
    uint8_t t16 = gm_l1;
    F.ret = t16;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint64_t gm_f_ld1_adb1ani(void * a0);
extern __attribute__((aligned(4))) uint64_t gm_f_ld2_gce8h7a(void * a0);
extern __attribute__((aligned(4))) uint64_t gm_f_ld4_lj8nmpt(void * a0);
extern __attribute__((aligned(4))) uint64_t gm_f_ld8_ecs41ho(void * a0);
extern __attribute__((aligned(4))) uint64_t gm_f_ldn_94iknnm(void * a0, uint8_t a1);
extern __attribute__((aligned(4))) uint8_t gm_f_log2_acbegp6(uint32_t a0);
extern __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_pick_kkl1a7m(uint8_t a0, gm_t9qkh0rsooc8d a1, gm_t9qkh0rsooc8d a2, gm_t9qkh0rsooc8d a3);
inline __attribute__((aligned(4))) uint8_t gm_f_reads_upvars_gvd5s94(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_reads_upvars_gvd5s94 F;
    void * gm_l0; /* code */
    uint64_t gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* k */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    void * gm_l6; /* s */
    GM_STACK_CHECK(&F, 263u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = ((uint64_t)0ULL);
    void * t21 = gm_l0;
    uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)t21) + 8));
    gm_l1 = t22;
    {
        for (;;) {
            uint64_t t23 = gm_l2;
            uint64_t t24 = gm_l1;
            uint8_t t25 = (t23 < t24);
            if (!t25) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t26 = gm_l2;
                        gm_l3 = t26;
                        void * t27 = gm_l0;
                        void * t28 = GM_LD(void *, ((uint8_t *)t27));
                        uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)t27) + 8));
                        uint64_t t30 = gm_l3;
                        if ((uintptr_t)t30 >= (uintptr_t)(t29)) gm_trap_index((uintptr_t)t30, t29, gm_src6unagr666v56, 520, 15);
                        {
                            {
                                {
                                    {
                                        uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t28) + (uintptr_t)t30 * 56) + 0));
                                        uint8_t t32 = (uint8_t)(t31 == ((uint8_t)5ULL));
                                        if (!t32) {
                                            goto gm_lb9;
                                        }
                                        goto gm_lb8;
                                    }
gm_lb9:;
                                    {
                                        uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t28) + (uintptr_t)t30 * 56) + 0));
                                        uint8_t t34 = (uint8_t)(t33 == ((uint8_t)11ULL));
                                        if (!t34) {
                                            goto gm_lb10;
                                        }
                                        goto gm_lb8;
                                    }
gm_lb10:;
                                    {
                                        uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t28) + (uintptr_t)t30 * 56) + 0));
                                        uint8_t t36 = (uint8_t)(t35 == ((uint8_t)46ULL));
                                        if (!t36) {
                                            goto gm_lb11;
                                        }
                                        goto gm_lb8;
                                    }
gm_lb11:;
                                    {
                                        uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t28) + (uintptr_t)t30 * 56) + 0));
                                        uint8_t t38 = (uint8_t)(t37 == ((uint8_t)47ULL));
                                        if (!t38) {
                                            goto gm_lb12;
                                        }
                                        goto gm_lb8;
                                    }
gm_lb12:;
                                    {
                                        uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t28) + (uintptr_t)t30 * 56) + 0));
                                        uint8_t t40 = (uint8_t)(t39 == ((uint8_t)48ULL));
                                        if (!t40) {
                                            goto gm_lb13;
                                        }
                                        goto gm_lb8;
                                    }
gm_lb13:;
                                    goto gm_lb7;
                                }
gm_lb8:;
                                F.ret = ((uint8_t)1);
                                goto gm_fret;
                            }
gm_lb7:;
                            {
                                goto gm_lb6;
                            }
gm_lb14:;
                            gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 520, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 519, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t41 = gm_l2;
            uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
            uint64_t t44 = r42;
            gm_l2 = t44;
        }
    }
gm_lb1:;
    void * t45 = gm_l0;
    gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)t45) + 144));
    gm_l4 = t46;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t47 = gm_l5;
            uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t49 = (t47 < t48);
            if (!t49) {
                goto gm_lb15;
            }
            {
                void * t50 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t52 = gm_l5;
                {
                    {
                        void * t53 = GM_LD(void *, (((uint8_t *)t50) + (uintptr_t)t52 * 8));
                        gm_l6 = t53;
                        void * t54 = gm_l6;
                        uint8_t t55 = gm_f_reads_upvars_gvd5s94(t54);
                        if (t55) {
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto gm_lb18;
                    }
gm_lb19:;
                    gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 525, 5);
                }
gm_lb18:;
            }
gm_lb17:;
            uint64_t t56 = gm_l5;
            uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
            uint64_t t59 = r57;
            gm_l5 = t59;
        }
    }
gm_lb15:;
    F.ret = ((uint8_t)0);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_st1_aqrj474(void * a0, uint64_t a1);
extern __attribute__((aligned(4))) void gm_f_st2_lkdmkcq(void * a0, uint64_t a1);
extern __attribute__((aligned(4))) void gm_f_st4_38u38jn(void * a0, uint64_t a1);
extern __attribute__((aligned(4))) void gm_f_st8_6aops52(void * a0, uint64_t a1);
extern __attribute__((aligned(4))) void gm_f_stn_cbrtno6(void * a0, uint8_t a1, uint64_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_vm_assemble_k5irtcr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_vm_assemble_k5irtcr F;
    void * gm_l0; /* m */
    void * gm_l1; /* info */
    void * gm_l2; /* code */
    gm_b16_8 gm_l3; /* out */
    uint8_t gm_l4; /* frameless */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* k */
    uint32_t gm_l8; /* nsites */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* k */
    gm_b16_8 gm_l12; /* hidden */
    uint64_t gm_l13; /* n */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* l */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* k */
    uint32_t gm_l20; /* r */
    uint32_t gm_l21; /* local */
    GM_STACK_CHECK(&F, 1612u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l22;
    uint8_t gm_l23;
    uint8_t gm_l24;
    uint8_t gm_l25;
    uint8_t gm_l26;
    uint8_t gm_l27;
    gm_tabb710d9mq2e gm_l28;
    uint8_t gm_l29;
    gm_tabb710d9mq2e gm_l30;
    {
        void * t40 = gm_l2;
        uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)t40) + 8));
        gm_tb1f35qilrmun t42 = gm_f_bc_free_v10vouf();
        gm_f_make_fill_0sgj1dq((void *)&gm_l22, (void *)NULL, cx, NULL, t41, t42, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_knhk77btln5)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t43 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
        if (!t43) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 8, gm_src6unagr666v56, 542, 12);
        gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
        gm_l3 = t44;
        gm_l4 = ((uint8_t)1);
        gm_l6 = ((uint64_t)0ULL);
        void * t45 = gm_l2;
        uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)t45) + 8));
        gm_l5 = t46;
        {
            for (;;) {
                uint64_t t47 = gm_l6;
                uint64_t t48 = gm_l5;
                uint8_t t49 = (t47 < t48);
                if (!t49) {
                    goto gm_lb1;
                }
                {
                    {
                        {
                            uint64_t t50 = gm_l6;
                            gm_l7 = t50;
                            void * t51 = GM_LD(void *, ((uint8_t *)&gm_l3));
                            uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                            uint64_t t53 = gm_l7;
                            if ((uintptr_t)t53 >= (uintptr_t)(t52)) gm_trap_index((uintptr_t)t53, t52, gm_src6unagr666v56, 545, 9);
                            void * t54 = gm_l2;
                            void * t55 = GM_LD(void *, ((uint8_t *)t54));
                            uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)t54) + 8));
                            uint64_t t57 = gm_l7;
                            if ((uintptr_t)t57 >= (uintptr_t)(t56)) gm_trap_index((uintptr_t)t57, t56, gm_src6unagr666v56, 545, 25);
                            void * t58 = (void *)(((uint8_t *)t55) + (uintptr_t)t57 * 56);
                            void * t59 = gm_l0;
                            uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)t59) + 90));
                            gm_tb1f35qilrmun t61 = gm_f_bc_of_ga25123(t58, t60);
                            GM_ST(gm_tb1f35qilrmun, (((uint8_t *)t51) + (uintptr_t)t53 * 16), t61);
                            {
                                {
                                    void * t62 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                    uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                    uint64_t t64 = gm_l7;
                                    if ((uintptr_t)t64 >= (uintptr_t)(t63)) gm_trap_index((uintptr_t)t64, t63, gm_src6unagr666v56, 546, 12);
                                    gm_t9qkh0rsooc8d t65 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t62) + (uintptr_t)t64 * 16));
                                    uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&t65) + 0));
                                    uint8_t t67 = (uint8_t)((t66 == ((uint8_t)1ULL)));
                                    if (!t67) {
                                        goto gm_lb7;
                                    }
                                    void * t68 = gm_l2;
                                    void * t69 = GM_LD(void *, ((uint8_t *)t68));
                                    uint64_t t70 = GM_LD(uint64_t, (((uint8_t *)t68) + 8));
                                    uint64_t t71 = gm_l7;
                                    if ((uintptr_t)t71 >= (uintptr_t)(t70)) gm_trap_index((uintptr_t)t71, t70, gm_src6unagr666v56, 546, 52);
                                    void * t72 = (void *)(((uint8_t *)t69) + (uintptr_t)t71 * 56);
                                    gm_t9qkh0rsooc8d t73;
                                    GM_ST(uint8_t, ((uint8_t *)&t73 + 0), ((uint8_t)1ULL));
                                    uint8_t t74 = gm_f_in_memory_t2cr4s3(t72);
                                    uint8_t t75 = (uint8_t)!t74;
                                    if (!t75) {
                                        goto gm_lb7;
                                    }
                                    gm_l4 = ((uint8_t)0);
                                    goto gm_lb6;
                                }
gm_lb7:;
                            }
gm_lb6:;
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 544, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t76 = gm_l6;
                uint64_t r77; bool o78 = __builtin_add_overflow(t76, ((uint64_t)1ULL), &r77);
                uint64_t t79 = r77;
                gm_l6 = t79;
            }
        }
gm_lb1:;
    }
    {
        gm_l8 = ((uint32_t)0ULL);
        gm_l10 = ((uint64_t)0ULL);
        void * t80 = gm_l2;
        uint64_t t81 = GM_LD(uint64_t, (((uint8_t *)t80) + 8));
        gm_l9 = t81;
        {
            for (;;) {
                uint64_t t82 = gm_l10;
                uint64_t t83 = gm_l9;
                uint8_t t84 = (t82 < t83);
                if (!t84) {
                    goto gm_lb8;
                }
                {
                    {
                        {
                            uint64_t t85 = gm_l10;
                            gm_l11 = t85;
                            void * t86 = gm_l2;
                            void * t87 = GM_LD(void *, ((uint8_t *)t86));
                            uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)t86) + 8));
                            uint64_t t89 = gm_l11;
                            if ((uintptr_t)t89 >= (uintptr_t)(t88)) gm_trap_index((uintptr_t)t89, t88, gm_src6unagr666v56, 551, 12);
                            {
                                {
                                    uint8_t t90 = GM_LD(uint8_t, ((((uint8_t *)t87) + (uintptr_t)t89 * 56) + 0));
                                    uint8_t t91 = (uint8_t)(t90 == ((uint8_t)55ULL));
                                    if (!t91) {
                                        goto gm_lb14;
                                    }
                                    gm_l25 = ((uint8_t)1);
                                    goto gm_lb13;
                                }
gm_lb14:;
                                {
                                    gm_l25 = ((uint8_t)0);
                                    goto gm_lb13;
                                }
gm_lb15:;
                                gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 551, 12);
                            }
gm_lb13:;
                            uint8_t t92 = gm_l25;
                            if (t92) {
                                gm_l24 = ((uint8_t)1);
                            } else {
                                void * t93 = gm_l2;
                                void * t94 = GM_LD(void *, ((uint8_t *)t93));
                                uint64_t t95 = GM_LD(uint64_t, (((uint8_t *)t93) + 8));
                                uint64_t t96 = gm_l11;
                                if ((uintptr_t)t96 >= (uintptr_t)(t95)) gm_trap_index((uintptr_t)t96, t95, gm_src6unagr666v56, 551, 39);
                                {
                                    {
                                        uint8_t t97 = GM_LD(uint8_t, ((((uint8_t *)t94) + (uintptr_t)t96 * 56) + 0));
                                        uint8_t t98 = (uint8_t)(t97 == ((uint8_t)58ULL));
                                        if (!t98) {
                                            goto gm_lb17;
                                        }
                                        gm_l26 = ((uint8_t)1);
                                        goto gm_lb16;
                                    }
gm_lb17:;
                                    {
                                        gm_l26 = ((uint8_t)0);
                                        goto gm_lb16;
                                    }
gm_lb18:;
                                    gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 551, 39);
                                }
gm_lb16:;
                                uint8_t t99 = gm_l26;
                                gm_l24 = t99;
                            }
                            uint8_t t100 = gm_l24;
                            if (t100) {
                                gm_l23 = ((uint8_t)1);
                            } else {
                                void * t101 = gm_l2;
                                void * t102 = GM_LD(void *, ((uint8_t *)t101));
                                uint64_t t103 = GM_LD(uint64_t, (((uint8_t *)t101) + 8));
                                uint64_t t104 = gm_l11;
                                if ((uintptr_t)t104 >= (uintptr_t)(t103)) gm_trap_index((uintptr_t)t104, t103, gm_src6unagr666v56, 551, 69);
                                {
                                    {
                                        uint8_t t105 = GM_LD(uint8_t, ((((uint8_t *)t102) + (uintptr_t)t104 * 56) + 0));
                                        uint8_t t106 = (uint8_t)(t105 == ((uint8_t)56ULL));
                                        if (!t106) {
                                            goto gm_lb20;
                                        }
                                        gm_l27 = ((uint8_t)1);
                                        goto gm_lb19;
                                    }
gm_lb20:;
                                    {
                                        gm_l27 = ((uint8_t)0);
                                        goto gm_lb19;
                                    }
gm_lb21:;
                                    gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 551, 69);
                                }
gm_lb19:;
                                uint8_t t107 = gm_l27;
                                gm_l23 = t107;
                            }
                            uint8_t t108 = gm_l23;
                            if (t108) {
                                void * t109 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                uint64_t t110 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                uint64_t t111 = gm_l11;
                                if ((uintptr_t)t111 >= (uintptr_t)(t110)) gm_trap_index((uintptr_t)t111, t110, gm_src6unagr666v56, 552, 13);
                                uint32_t t112 = gm_l8;
                                GM_ST(uint32_t, ((((uint8_t *)t109) + (uintptr_t)t111 * 16) + 4), t112);
                                uint32_t t113 = gm_l8;
                                uint32_t r114; bool o115 = __builtin_add_overflow(t113, ((uint32_t)1ULL), &r114);
                                if (o115) gm_trap("integer overflow in `+`", gm_src6unagr666v56, 553, 13);
                                uint32_t t116 = r114;
                                gm_l8 = t116;
                            }
                            goto gm_lb11;
                        }
gm_lb12:;
                        gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 550, 5);
                    }
gm_lb11:;
                }
gm_lb10:;
                uint64_t t117 = gm_l10;
                uint64_t r118; bool o119 = __builtin_add_overflow(t117, ((uint64_t)1ULL), &r118);
                uint64_t t120 = r118;
                gm_l10 = t120;
            }
        }
gm_lb8:;
    }
    {
        void * t121 = gm_l2;
        uint32_t t122 = gm_l8;
        uint64_t t123 = (uint64_t)t122;
        uint32_t t124 = GM_LD(uint32_t, ((uint8_t *)gm_cl7gtp51edk));
        gm_b16_8 t125 = gm_f_empty_slice_thu1fog();
        gm_th2lqbo77pvsc t126;
        memset(&t126, 0, sizeof t126);
        gm_tu1mv36lldn2v t127;
        GM_ST(uint16_t, (uint8_t *)&t127 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t127 + 20, 0);
        GM_ST(uint32_t, (uint8_t *)&t127 + 60, 0);
        GM_ST(uint32_t, (uint8_t *)&t127 + 84, 0);
        GM_ST(uint32_t, (uint8_t *)&t127 + 116, 0);
        GM_ST(uint8_t, ((uint8_t *)&t127 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, ((uint8_t *)&t127 + 1), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t127 + 4), ((uint32_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t127 + 8), t124);
        GM_ST(uint32_t, ((uint8_t *)&t127 + 12), ((uint32_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t127 + 16), ((uint32_t)0ULL));
        GM_ST(void *, ((uint8_t *)&t127 + 24), ((void *)0));
        GM_ST(uint64_t, ((uint8_t *)&t127 + 32), ((uint64_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t127 + 40), t125);
        GM_ST(uint32_t, ((uint8_t *)&t127 + 56), ((uint32_t)0ULL));
        GM_ST(uint64_t, ((uint8_t *)&t127 + 64), ((uint64_t)0ULL));
        GM_ST(void *, ((uint8_t *)&t127 + 72), ((void *)0));
        GM_ST(uint32_t, ((uint8_t *)&t127 + 80), ((uint32_t)0ULL));
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t127 + 88), t126);
        GM_ST(uint32_t, ((uint8_t *)&t127 + 112), ((uint32_t)0ULL));
        GM_ST(void *, ((uint8_t *)&t127 + 120), ((void *)0));
        gm_f_make_fill_kjs404n((void *)&gm_l28, (void *)NULL, cx, NULL, t123, &t127, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kcvb629onmb)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t128 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
        if (!t128) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 8, gm_src6unagr666v56, 556, 18);
        gm_b16_8 t129 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l28) + 8));
        GM_ST(gm_b16_8, (((uint8_t *)t121) + 72), t129);
        void * t130 = gm_l2;
        gm_b16_8 t131 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t130) + 16), t131);
        void * t132 = gm_l2;
        uint8_t t133 = GM_LD(uint8_t, (((uint8_t *)t132) + 160));
        if (t133) {
            gm_l29 = ((uint8_t)1);
        } else {
            void * t134 = gm_l2;
            uint64_t t135 = GM_LD(uint64_t, ((((uint8_t *)t134) + 144) + 8));
            uint8_t t136 = (t135 > ((uint64_t)0ULL));
            gm_l29 = t136;
        }
        uint8_t t137 = gm_l29;
        if (t137) {
            gm_l4 = ((uint8_t)0);
        }
        void * t138 = gm_l2;
        uint8_t t139 = gm_l4;
        GM_ST(uint8_t, (((uint8_t *)t138) + 32), t139);
        void * t140 = gm_l2;
        uint8_t t141 = gm_f_reads_upvars_gvd5s94(t140);
        if (t141) {
            void * t142 = gm_l2;
            void * t143 = gm_l2;
            uint64_t t144 = GM_LD(uint64_t, (((uint8_t *)t143) + 104));
            if (((uint64_t)8ULL) == 0) gm_trap("division by zero", gm_src6unagr666v56, 562, 25);
            uint64_t t145 = (uint64_t)(t144 / ((uint64_t)8ULL));
            uint32_t t146 = (uint32_t)t145;
            GM_ST(uint32_t, (((uint8_t *)t142) + 88), t146);
            void * t147 = gm_l2;
            void * t148 = gm_l2;
            uint64_t t149 = GM_LD(uint64_t, (((uint8_t *)t148) + 104));
            uint64_t r150; bool o151 = __builtin_add_overflow(t149, ((uint64_t)8ULL), &r150);
            if (o151) gm_trap("integer overflow in `+`", gm_src6unagr666v56, 563, 9);
            uint64_t t152 = r150;
            GM_ST(uint64_t, (((uint8_t *)t147) + 104), t152);
        }
        uint8_t t153 = gm_l4;
        uint8_t t154 = (uint8_t)!t153;
        if (t154) {
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
        void * t155 = gm_l1;
        uint64_t t156 = GM_LD(uint64_t, ((((uint8_t *)t155) + 120) + 8));
        uint32_t t157 = GM_LD(uint32_t, ((uint8_t *)gm_cl7gtp51edk));
        gm_f_make_fill_bqbf5rp((void *)&gm_l30, (void *)NULL, cx, NULL, t156, t157, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kv3bmftl0ar)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t158 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
        if (!t158) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 8, gm_src6unagr666v56, 566, 15);
        gm_b16_8 t159 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 8));
        gm_l12 = t159;
    }
    {
        gm_l13 = ((uint64_t)0ULL);
        gm_l15 = ((uint64_t)0ULL);
        void * t160 = gm_l1;
        uint64_t t161 = GM_LD(uint64_t, ((((uint8_t *)t160) + 120) + 8));
        gm_l14 = t161;
        {
            for (;;) {
                uint64_t t162 = gm_l15;
                uint64_t t163 = gm_l14;
                uint8_t t164 = (t162 < t163);
                if (!t164) {
                    goto gm_lb22;
                }
                {
                    {
                        {
                            uint64_t t165 = gm_l15;
                            gm_l16 = t165;
                            void * t166 = gm_l1;
                            void * t167 = GM_LD(void *, (((uint8_t *)t166) + 120));
                            uint64_t t168 = GM_LD(uint64_t, ((((uint8_t *)t166) + 120) + 8));
                            uint64_t t169 = gm_l16;
                            if ((uintptr_t)t169 >= (uintptr_t)(t168)) gm_trap_index((uintptr_t)t169, t168, gm_src6unagr666v56, 569, 12);
                            uint8_t t170 = GM_LD(uint8_t, (((uint8_t *)t167) + (uintptr_t)t169 * 1));
                            if (t170) {
                                void * t171 = GM_LD(void *, ((uint8_t *)&gm_l12));
                                uint64_t t172 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                uint64_t t173 = gm_l16;
                                if ((uintptr_t)t173 >= (uintptr_t)(t172)) gm_trap_index((uintptr_t)t173, t172, gm_src6unagr666v56, 570, 13);
                                void * t174 = gm_l2;
                                uint64_t t175 = GM_LD(uint64_t, (((uint8_t *)t174) + 104));
                                if (((uint64_t)8ULL) == 0) gm_trap("division by zero", gm_src6unagr666v56, 570, 26);
                                uint64_t t176 = (uint64_t)(t175 / ((uint64_t)8ULL));
                                uint64_t t177 = gm_l13;
                                uint64_t r178; bool o179 = __builtin_add_overflow(t176, t177, &r178);
                                if (o179) gm_trap("integer overflow in `+`", gm_src6unagr666v56, 570, 26);
                                uint64_t t180 = r178;
                                uint32_t t181 = (uint32_t)t180;
                                GM_ST(uint32_t, (((uint8_t *)t171) + (uintptr_t)t173 * 4), t181);
                                uint64_t t182 = gm_l13;
                                uint64_t r183; bool o184 = __builtin_add_overflow(t182, ((uint64_t)1ULL), &r183);
                                if (o184) gm_trap("integer overflow in `+`", gm_src6unagr666v56, 571, 13);
                                uint64_t t185 = r183;
                                gm_l13 = t185;
                            }
                            goto gm_lb25;
                        }
gm_lb26:;
                        gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 568, 5);
                    }
gm_lb25:;
                }
gm_lb24:;
                uint64_t t186 = gm_l15;
                uint64_t r187; bool o188 = __builtin_add_overflow(t186, ((uint64_t)1ULL), &r187);
                uint64_t t189 = r187;
                gm_l15 = t189;
            }
        }
gm_lb22:;
        uint64_t t190 = gm_l13;
        uint8_t t191 = ((t190 == ((uint64_t)0ULL)));
        if (t191) {
            gm_b16_8 t192 = gm_l12;
            gm_b16_8 t193 = GM_LD(gm_b16_8, ((uint8_t *)&t192));
            gm_b0_1 t194 = gm_f_free_5tbvv4o((void *)NULL, cx, NULL, t193, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k6be0mlmbp5)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
        void * t195 = gm_l2;
        gm_b16_8 t196 = gm_l12;
        GM_ST(gm_b16_8, (((uint8_t *)t195) + 48), t196);
        void * t197 = gm_l2;
        void * t198 = gm_l2;
        uint64_t t199 = GM_LD(uint64_t, (((uint8_t *)t198) + 104));
        uint64_t t200 = gm_l13;
        uint64_t r201; bool o202 = __builtin_mul_overflow(((uint64_t)8ULL), t200, &r201);
        if (o202) gm_trap("integer overflow in `*`", gm_src6unagr666v56, 579, 18);
        uint64_t t203 = r201;
        uint64_t r204; bool o205 = __builtin_add_overflow(t199, t203, &r204);
        if (o205) gm_trap("integer overflow in `+`", gm_src6unagr666v56, 579, 5);
        uint64_t t206 = r204;
        GM_ST(uint64_t, (((uint8_t *)t197) + 104), t206);
        gm_l18 = ((uint64_t)0ULL);
    }
    {
        void * t207 = gm_l2;
        uint64_t t208 = GM_LD(uint64_t, (((uint8_t *)t207) + 8));
        gm_l17 = t208;
        {
            for (;;) {
                uint64_t t209 = gm_l18;
                uint64_t t210 = gm_l17;
                uint8_t t211 = (t209 < t210);
                if (!t211) {
                    goto gm_lb27;
                }
                {
                    {
                        {
                            uint64_t t212 = gm_l18;
                            gm_l19 = t212;
                            void * t213 = gm_l2;
                            void * t214 = GM_LD(void *, ((uint8_t *)t213));
                            uint64_t t215 = GM_LD(uint64_t, (((uint8_t *)t213) + 8));
                            uint64_t t216 = gm_l19;
                            if ((uintptr_t)t216 >= (uintptr_t)(t215)) gm_trap_index((uintptr_t)t216, t215, gm_src6unagr666v56, 581, 15);
                            {
                                {
                                    uint8_t t217 = GM_LD(uint8_t, ((((uint8_t *)t214) + (uintptr_t)t216 * 56) + 0));
                                    uint8_t t218 = (uint8_t)(t217 == ((uint8_t)65ULL));
                                    if (!t218) {
                                        goto gm_lb33;
                                    }
                                    uint32_t t219 = GM_LD(uint32_t, ((((uint8_t *)t214) + (uintptr_t)t216 * 56) + 8));
                                    gm_l20 = t219;
                                    uint32_t t220 = GM_LD(uint32_t, (((((uint8_t *)t214) + (uintptr_t)t216 * 56) + 8) + 4));
                                    gm_l21 = t220;
                                    void * t221 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                    uint64_t t222 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                    uint64_t t223 = gm_l19;
                                    if ((uintptr_t)t223 >= (uintptr_t)(t222)) gm_trap_index((uintptr_t)t223, t222, gm_src6unagr666v56, 582, 42);
                                    uint32_t t224 = gm_l20;
                                    uint32_t t225 = gm_l21;
                                    void * t226 = GM_LD(void *, ((uint8_t *)&gm_l12));
                                    uint64_t t227 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                    uint32_t t228 = gm_l21;
                                    uint64_t t229 = (uint64_t)t228;
                                    if ((uintptr_t)t229 >= (uintptr_t)(t227)) gm_trap_index((uintptr_t)t229, t227, gm_src6unagr666v56, 582, 78);
                                    uint32_t t230 = GM_LD(uint32_t, (((uint8_t *)t226) + (uintptr_t)t229 * 4));
                                    gm_t9qkh0rsooc8d t231;
                                    GM_ST(uint8_t, ((uint8_t *)&t231 + 0), ((uint8_t)90ULL));
                                    gm_tb1f35qilrmun t232 = gm_f_bc3_m9eakek(t231, t224, t225, t230);
                                    GM_ST(gm_tb1f35qilrmun, (((uint8_t *)t221) + (uintptr_t)t223 * 16), t232);
                                    goto gm_lb32;
                                }
gm_lb33:;
                                {
                                    {
                                        {
                                            uint8_t t233 = GM_LD(uint8_t, ((((uint8_t *)t214) + (uintptr_t)t216 * 56) + 0));
                                            uint8_t t234 = (uint8_t)(t233 == ((uint8_t)1ULL));
                                            if (!t234) {
                                                goto gm_lb36;
                                            }
                                            goto gm_lb35;
                                        }
gm_lb36:;
                                        {
                                            uint8_t t235 = GM_LD(uint8_t, ((((uint8_t *)t214) + (uintptr_t)t216 * 56) + 0));
                                            uint8_t t236 = (uint8_t)(t235 == ((uint8_t)2ULL));
                                            if (!t236) {
                                                goto gm_lb37;
                                            }
                                            goto gm_lb35;
                                        }
gm_lb37:;
                                        goto gm_lb34;
                                    }
gm_lb35:;
                                    void * t237 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                    uint64_t t238 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                    uint64_t t239 = gm_l19;
                                    if ((uintptr_t)t239 >= (uintptr_t)(t238)) gm_trap_index((uintptr_t)t239, t238, gm_src6unagr666v56, 583, 42);
                                    gm_tb1f35qilrmun t240 = gm_f_bc_free_v10vouf();
                                    GM_ST(gm_tb1f35qilrmun, (((uint8_t *)t237) + (uintptr_t)t239 * 16), t240);
                                    goto gm_lb32;
                                }
gm_lb34:;
                                {
                                    goto gm_lb32;
                                }
gm_lb38:;
                                gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 581, 9);
                            }
gm_lb32:;
                            goto gm_lb30;
                        }
gm_lb31:;
                        gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 580, 5);
                    }
gm_lb30:;
                }
gm_lb29:;
                uint64_t t241 = gm_l18;
                uint64_t r242; bool o243 = __builtin_add_overflow(t241, ((uint64_t)1ULL), &r242);
                uint64_t t244 = r242;
                gm_l18 = t244;
            }
        }
gm_lb27:;
    }
    {
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
extern __attribute__((aligned(4))) void * gm_f_vm_view_fo2var1(void * a0, uint64_t a1, uint64_t a2, uint64_t a3, uint8_t a4);
extern __attribute__((aligned(4))) uint8_t gm_f_word_size_mu6ni08(uint64_t a0);

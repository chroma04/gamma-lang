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
typedef struct gm_cs9ufftihtbpo2 { uint32_t f0; uint32_t f1; } gm_cs9ufftihtbpo2;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { int32_t f0; } gm_t19enj90vg8bd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint32_t a[1]; } gm_tdvq40fmif5j0;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint64_t f0; uint64_t f1; uint32_t f2; uint8_t p3[4]; uint64_t f3; } gm_tnen6863q2sjs;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; uint64_t f2; } gm_tuglhjjmj2g58;
_Static_assert(sizeof(gm_cs9ufftihtbpo2) == 8 && _Alignof(gm_cs9ufftihtbpo2) == 4 && offsetof(gm_cs9ufftihtbpo2, f0) == 0 && offsetof(gm_cs9ufftihtbpo2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { uint64_t f0; uint8_t f1; uint8_t p2[7]; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; uint8_t f5; uint8_t f6; uint8_t pe[6]; } gm_t0rfe1ou14h21;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t19enj90vg8bd) == 4 && _Alignof(gm_t19enj90vg8bd) == 4 && offsetof(gm_t19enj90vg8bd, f0) == 0, "layout of `(i32,)`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; } gm_t5hdnnftkup6m;
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; int64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t5j14m8ru132p;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuglhjjmj2g58 v0; uint16_t v1; } f1; } gm_t5v8n6qrn4rh4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { gm_tcc1rq4n1avb2 f0; uint8_t f1; uint8_t pe[3]; } gm_t736djc66po0l;
typedef struct { uint8_t f0; gm_t9qkh0rsooc8d f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t p5[3]; uint32_t f5; gm_t9805qtonbsag f6; gm_t9qkh0rsooc8d f7; uint8_t pe[3]; } gm_t7g88i52rrtm5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
typedef struct { gm_b16_8 a[3]; } gm_t9bmh80edou9c;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tcrdb1inj22os;
typedef struct { gm_b16_8 a[4]; } gm_tdmi4fk9nhqa7;
_Static_assert(sizeof(gm_tdvq40fmif5j0) == 4 && _Alignof(gm_tdvq40fmif5j0) == 4 && offsetof(gm_tdvq40fmif5j0, a) == 0, "layout of `[1]u32`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint32_t f0; uint32_t f1; gm_t9805qtonbsag f2; } gm_tf9f5m02f56gm;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t p3[5]; gm_b16_8 f3; } gm_tftmsj5vtqvej;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_t9805qtonbsag f0; uint32_t f1; } gm_tgqgdb97ka5kt;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; } gm_ti71ll5knnvij;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; } gm_tif3elqk01bdl;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; gm_b16_8 f8; gm_b16_8 f9; gm_b16_8 f10; } gm_tk5njge13tk1d;
typedef struct { gm_t9805qtonbsag f0; gm_t9805qtonbsag f1; gm_t9805qtonbsag f2; gm_t9805qtonbsag f3; } gm_tk8ebsa8coi4p;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; gm_b16_8 f3; } gm_tmr5oep1578tb;
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
_Static_assert(sizeof(gm_tnen6863q2sjs) == 32 && _Alignof(gm_tnen6863q2sjs) == 8 && offsetof(gm_tnen6863q2sjs, f0) == 0 && offsetof(gm_tnen6863q2sjs, f1) == 8 && offsetof(gm_tnen6863q2sjs, f2) == 16 && offsetof(gm_tnen6863q2sjs, f3) == 24, "layout of `(usize, usize, u32, usize)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { uint64_t f0; gm_t9805qtonbsag f1; } gm_tp0pp2cmr6tvd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; } gm_tq3m5qgvkpje8;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_b16_8 f2; gm_t88rcucg1b7uk f3; uint8_t p4[4]; gm_b16_8 f4; uint32_t f5; uint8_t p6[4]; gm_b16_8 f6; uint32_t f7; uint8_t pe[4]; } gm_tqh25f25b69k5;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
_Static_assert(sizeof(gm_tuglhjjmj2g58) == 16 && _Alignof(gm_tuglhjjmj2g58) == 8 && offsetof(gm_tuglhjjmj2g58, f0) == 0 && offsetof(gm_tuglhjjmj2g58, f1) == 4 && offsetof(gm_tuglhjjmj2g58, f2) == 8, "layout of `(signed: bool, bits: u32, storage: u64)`");
typedef struct { gm_t9805qtonbsag a[3]; } gm_tv199o0mcsqd6;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0rfe1ou14h21) == 64 && _Alignof(gm_t0rfe1ou14h21) == 8 && offsetof(gm_t0rfe1ou14h21, f0) == 0 && offsetof(gm_t0rfe1ou14h21, f1) == 8 && offsetof(gm_t0rfe1ou14h21, f2) == 16 && offsetof(gm_t0rfe1ou14h21, f3) == 24 && offsetof(gm_t0rfe1ou14h21, f4) == 40 && offsetof(gm_t0rfe1ou14h21, f5) == 56 && offsetof(gm_t0rfe1ou14h21, f6) == 57, "layout of `Target`");
typedef struct { gm_b16_8 f0; gm_tk5njge13tk1d f1; gm_b16_8 f2; uint8_t f3; uint8_t f4; uint8_t pe[6]; } gm_t0u6dopi4f21f;
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { uint64_t f0; gm_t6j6smj7qoctu f1; } gm_t1l3r16b8478v;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `(string, usize)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
typedef struct { gm_t0rfe1ou14h21 f0; void * f1; } gm_t4grvdhp6dkf1;
_Static_assert(sizeof(gm_t5hdnnftkup6m) == 24 && _Alignof(gm_t5hdnnftkup6m) == 8 && offsetof(gm_t5hdnnftkup6m, f0) == 0 && offsetof(gm_t5hdnnftkup6m, f1) == 16 && offsetof(gm_t5hdnnftkup6m, f2) == 20, "layout of `(string, u32, u32)`");
_Static_assert(sizeof(gm_t5j14m8ru132p) == 32 && _Alignof(gm_t5j14m8ru132p) == 8 && offsetof(gm_t5j14m8ru132p, f0) == 0 && offsetof(gm_t5j14m8ru132p, f1) == 8 && offsetof(gm_t5j14m8ru132p, f2) == 16 && offsetof(gm_t5j14m8ru132p, f3) == 24, "layout of `FileInfo`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_b16_8 v1; } f1; } gm_t5o0tpv5jka2l;
_Static_assert(sizeof(gm_t5v8n6qrn4rh4) == 24 && _Alignof(gm_t5v8n6qrn4rh4) == 8 && offsetof(gm_t5v8n6qrn4rh4, f0) == 0 && offsetof(gm_t5v8n6qrn4rh4, f1) == 8, "layout of `Repr`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t736djc66po0l) == 12 && _Alignof(gm_t736djc66po0l) == 4 && offsetof(gm_t736djc66po0l, f0) == 0 && offsetof(gm_t736djc66po0l, f1) == 8, "layout of `?(u32, u32)`");
_Static_assert(sizeof(gm_t7g88i52rrtm5) == 24 && _Alignof(gm_t7g88i52rrtm5) == 4 && offsetof(gm_t7g88i52rrtm5, f0) == 0 && offsetof(gm_t7g88i52rrtm5, f1) == 1 && offsetof(gm_t7g88i52rrtm5, f2) == 2 && offsetof(gm_t7g88i52rrtm5, f3) == 3 && offsetof(gm_t7g88i52rrtm5, f4) == 4 && offsetof(gm_t7g88i52rrtm5, f5) == 8 && offsetof(gm_t7g88i52rrtm5, f6) == 12 && offsetof(gm_t7g88i52rrtm5, f7) == 20, "layout of `FmtSpec`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_t9bmh80edou9c) == 48 && _Alignof(gm_t9bmh80edou9c) == 8 && offsetof(gm_t9bmh80edou9c, a) == 0, "layout of `[3]string`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_t6j6smj7qoctu v1; } f1; } gm_t9t4u07j77scv;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { gm_t4c7ipjj9s3fp f0; gm_trqfvmjtt4sij f1; uint8_t f2; uint8_t p3[7]; gm_trqfvmjtt4sij f3; gm_t4c7ipjj9s3fp f4; gm_t4c7ipjj9s3fp f5; } gm_tbpdf5ej15iof;
typedef struct { uint64_t f0; gm_tgqgdb97ka5kt f1; uint8_t pe[4]; } gm_tc42mm7h7sccr;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
_Static_assert(sizeof(gm_tcrdb1inj22os) == 72 && _Alignof(gm_tcrdb1inj22os) == 8 && offsetof(gm_tcrdb1inj22os, f0) == 0 && offsetof(gm_tcrdb1inj22os, f1) == 16 && offsetof(gm_tcrdb1inj22os, f2) == 24 && offsetof(gm_tcrdb1inj22os, f3) == 40 && offsetof(gm_tcrdb1inj22os, f4) == 56, "layout of `(string, usize, string, string, string)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tdmi4fk9nhqa7) == 64 && _Alignof(gm_tdmi4fk9nhqa7) == 8 && offsetof(gm_tdmi4fk9nhqa7, a) == 0, "layout of `[4]string`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_tqh25f25b69k5 f1; } gm_tdrnv2ejgmfii;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tf9f5m02f56gm) == 16 && _Alignof(gm_tf9f5m02f56gm) == 4 && offsetof(gm_tf9f5m02f56gm, f0) == 0 && offsetof(gm_tf9f5m02f56gm, f1) == 4 && offsetof(gm_tf9f5m02f56gm, f2) == 8, "layout of `SpecKey`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_tftmsj5vtqvej) == 24 && _Alignof(gm_tftmsj5vtqvej) == 8 && offsetof(gm_tftmsj5vtqvej, f0) == 0 && offsetof(gm_tftmsj5vtqvej, f1) == 1 && offsetof(gm_tftmsj5vtqvej, f2) == 2 && offsetof(gm_tftmsj5vtqvej, f3) == 8, "layout of `Options`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_tgqgdb97ka5kt) == 12 && _Alignof(gm_tgqgdb97ka5kt) == 4 && offsetof(gm_tgqgdb97ka5kt, f0) == 0 && offsetof(gm_tgqgdb97ka5kt, f1) == 8, "layout of `Owner`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_th2lqbo77pvsc f0; uint8_t f1; uint8_t pe[7]; } gm_ti5ramhglnvdt;
_Static_assert(sizeof(gm_ti71ll5knnvij) == 72 && _Alignof(gm_ti71ll5knnvij) == 8 && offsetof(gm_ti71ll5knnvij, f0) == 0 && offsetof(gm_ti71ll5knnvij, f1) == 16 && offsetof(gm_ti71ll5knnvij, f2) == 32 && offsetof(gm_ti71ll5knnvij, f3) == 40 && offsetof(gm_ti71ll5knnvij, f4) == 56, "layout of `(string, []u8, usize, []u8, string)`");
_Static_assert(sizeof(gm_tif3elqk01bdl) == 96 && _Alignof(gm_tif3elqk01bdl) == 8 && offsetof(gm_tif3elqk01bdl, f0) == 0 && offsetof(gm_tif3elqk01bdl, f1) == 16 && offsetof(gm_tif3elqk01bdl, f2) == 32 && offsetof(gm_tif3elqk01bdl, f3) == 48 && offsetof(gm_tif3elqk01bdl, f4) == 64 && offsetof(gm_tif3elqk01bdl, f5) == 80, "layout of `(string, string, string, string, string, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t5j14m8ru132p v1; } f1; } gm_tih6h4avv5nrb;
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tk5njge13tk1d) == 176 && _Alignof(gm_tk5njge13tk1d) == 8 && offsetof(gm_tk5njge13tk1d, f0) == 0 && offsetof(gm_tk5njge13tk1d, f1) == 16 && offsetof(gm_tk5njge13tk1d, f2) == 32 && offsetof(gm_tk5njge13tk1d, f3) == 48 && offsetof(gm_tk5njge13tk1d, f4) == 64 && offsetof(gm_tk5njge13tk1d, f5) == 80 && offsetof(gm_tk5njge13tk1d, f6) == 96 && offsetof(gm_tk5njge13tk1d, f7) == 112 && offsetof(gm_tk5njge13tk1d, f8) == 128 && offsetof(gm_tk5njge13tk1d, f9) == 144 && offsetof(gm_tk5njge13tk1d, f10) == 160, "layout of `Parts`");
_Static_assert(sizeof(gm_tk8ebsa8coi4p) == 32 && _Alignof(gm_tk8ebsa8coi4p) == 4 && offsetof(gm_tk8ebsa8coi4p, f0) == 0 && offsetof(gm_tk8ebsa8coi4p, f1) == 8 && offsetof(gm_tk8ebsa8coi4p, f2) == 16 && offsetof(gm_tk8ebsa8coi4p, f3) == 24, "layout of `Ambient`");
_Static_assert(sizeof(gm_tmr5oep1578tb) == 56 && _Alignof(gm_tmr5oep1578tb) == 8 && offsetof(gm_tmr5oep1578tb, f0) == 0 && offsetof(gm_tmr5oep1578tb, f1) == 16 && offsetof(gm_tmr5oep1578tb, f2) == 32 && offsetof(gm_tmr5oep1578tb, f3) == 40, "layout of `(string, []u8, usize, []u8)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_tp0pp2cmr6tvd) == 16 && _Alignof(gm_tp0pp2cmr6tvd) == 8 && offsetof(gm_tp0pp2cmr6tvd, f0) == 0 && offsetof(gm_tp0pp2cmr6tvd, f1) == 8, "layout of `(i: usize, x: ?u32)`");
_Static_assert(sizeof(gm_tq3m5qgvkpje8) == 40 && _Alignof(gm_tq3m5qgvkpje8) == 8 && offsetof(gm_tq3m5qgvkpje8, f0) == 0 && offsetof(gm_tq3m5qgvkpje8, f1) == 16 && offsetof(gm_tq3m5qgvkpje8, f2) == 24, "layout of `(string, usize, string)`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; } f1; } gm_tqgtd5gtgj65s;
_Static_assert(sizeof(gm_tqh25f25b69k5) == 96 && _Alignof(gm_tqh25f25b69k5) == 8 && offsetof(gm_tqh25f25b69k5, f0) == 0 && offsetof(gm_tqh25f25b69k5, f1) == 8 && offsetof(gm_tqh25f25b69k5, f2) == 24 && offsetof(gm_tqh25f25b69k5, f3) == 40 && offsetof(gm_tqh25f25b69k5, f4) == 48 && offsetof(gm_tqh25f25b69k5, f5) == 64 && offsetof(gm_tqh25f25b69k5, f6) == 72 && offsetof(gm_tqh25f25b69k5, f7) == 88, "layout of `Func`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t4c7ipjj9s3fp f0; } gm_ttiitul0263ld;
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
typedef struct { gm_t4c7ipjj9s3fp f0; uint8_t f1; uint8_t pe[7]; } gm_tuutj662vpgtg;
_Static_assert(sizeof(gm_tv199o0mcsqd6) == 24 && _Alignof(gm_tv199o0mcsqd6) == 4 && offsetof(gm_tv199o0mcsqd6, a) == 0, "layout of `[3]?u32`");
_Static_assert(sizeof(gm_t0u6dopi4f21f) == 216 && _Alignof(gm_t0u6dopi4f21f) == 8 && offsetof(gm_t0u6dopi4f21f, f0) == 0 && offsetof(gm_t0u6dopi4f21f, f1) == 16 && offsetof(gm_t0u6dopi4f21f, f2) == 192 && offsetof(gm_t0u6dopi4f21f, f3) == 208 && offsetof(gm_t0u6dopi4f21f, f4) == 209, "layout of `Generated`");
_Static_assert(sizeof(gm_t1l3r16b8478v) == 40 && _Alignof(gm_t1l3r16b8478v) == 8 && offsetof(gm_t1l3r16b8478v, f0) == 0 && offsetof(gm_t1l3r16b8478v, f1) == 8, "layout of `(i: usize, x: CSource)`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4grvdhp6dkf1) == 72 && _Alignof(gm_t4grvdhp6dkf1) == 8 && offsetof(gm_t4grvdhp6dkf1, f0) == 0 && offsetof(gm_t4grvdhp6dkf1, f1) == 64, "layout of `Layouts`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { uint64_t f0; gm_tc42mm7h7sccr f1; } gm_t5c6io15et1sc;
_Static_assert(sizeof(gm_t5o0tpv5jka2l) == 40 && _Alignof(gm_t5o0tpv5jka2l) == 8 && offsetof(gm_t5o0tpv5jka2l, f0) == 0 && offsetof(gm_t5o0tpv5jka2l, f1) == 8, "layout of `Unsup!string`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t9t4u07j77scv) == 40 && _Alignof(gm_t9t4u07j77scv) == 8 && offsetof(gm_t9t4u07j77scv, f0) == 0 && offsetof(gm_t9t4u07j77scv, f1) == 8, "layout of `Unsup!ConstsText`");
_Static_assert(sizeof(gm_tbpdf5ej15iof) == 256 && _Alignof(gm_tbpdf5ej15iof) == 8 && offsetof(gm_tbpdf5ej15iof, f0) == 0 && offsetof(gm_tbpdf5ej15iof, f1) == 56 && offsetof(gm_tbpdf5ej15iof, f2) == 96 && offsetof(gm_tbpdf5ej15iof, f3) == 104 && offsetof(gm_tbpdf5ej15iof, f4) == 144 && offsetof(gm_tbpdf5ej15iof, f5) == 200, "layout of `Views`");
_Static_assert(sizeof(gm_tc42mm7h7sccr) == 24 && _Alignof(gm_tc42mm7h7sccr) == 8 && offsetof(gm_tc42mm7h7sccr, f0) == 0 && offsetof(gm_tc42mm7h7sccr, f1) == 8, "layout of `OwnerMark`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdrnv2ejgmfii) == 104 && _Alignof(gm_tdrnv2ejgmfii) == 8 && offsetof(gm_tdrnv2ejgmfii, f0) == 0 && offsetof(gm_tdrnv2ejgmfii, f1) == 8, "layout of `(f: u32, func: Func)`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_t4c7ipjj9s3fp f2; gm_t4c7ipjj9s3fp f3; gm_t4c7ipjj9s3fp f4; gm_ttiitul0263ld f5; } gm_te68172gglous;
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_t0u6dopi4f21f v1; } f1; } gm_tetdkgoreqmn1;
_Static_assert(sizeof(gm_ti5ramhglnvdt) == 32 && _Alignof(gm_ti5ramhglnvdt) == 8 && offsetof(gm_ti5ramhglnvdt, f0) == 0 && offsetof(gm_ti5ramhglnvdt, f1) == 24, "layout of `??string`");
_Static_assert(sizeof(gm_tih6h4avv5nrb) == 40 && _Alignof(gm_tih6h4avv5nrb) == 8 && offsetof(gm_tih6h4avv5nrb, f0) == 0 && offsetof(gm_tih6h4avv5nrb, f1) == 8, "layout of `IoError!FileInfo`");
typedef struct { gm_tdrnv2ejgmfii f0; uint8_t f1; uint8_t pe[7]; } gm_tpo9d7ujpll8m;
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_tqgtd5gtgj65s) == 40 && _Alignof(gm_tqgtd5gtgj65s) == 8 && offsetof(gm_tqgtd5gtgj65s, f0) == 0 && offsetof(gm_tqgtd5gtgj65s, f1) == 8, "layout of `Unsup!()`");
typedef struct { void * f0; gm_t4grvdhp6dkf1 f1; gm_trqfvmjtt4sij f2; gm_t4c7ipjj9s3fp f3; gm_t4c7ipjj9s3fp f4; gm_ttiitul0263ld f5; gm_trqfvmjtt4sij f6; gm_trqfvmjtt4sij f7; gm_trqfvmjtt4sij f8; gm_t4c7ipjj9s3fp f9; gm_trqfvmjtt4sij f10; gm_t4c7ipjj9s3fp f11; gm_trqfvmjtt4sij f12; gm_t4c7ipjj9s3fp f13; gm_t4c7ipjj9s3fp f14; gm_trqfvmjtt4sij f15; gm_t4c7ipjj9s3fp f16; uint8_t f17; uint8_t p18[7]; gm_t4c7ipjj9s3fp f18; gm_tbpdf5ej15iof f19; } gm_trg1s7erfs461;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_ttiitul0263ld) == 56 && _Alignof(gm_ttiitul0263ld) == 8 && offsetof(gm_ttiitul0263ld, f0) == 0, "layout of `Namer`");
_Static_assert(sizeof(gm_tuutj662vpgtg) == 64 && _Alignof(gm_tuutj662vpgtg) == 8 && offsetof(gm_tuutj662vpgtg, f0) == 0 && offsetof(gm_tuutj662vpgtg, f1) == 56, "layout of `?HashMap((u32, u32), bool, null, null)`");
typedef struct { void * f0; gm_trg1s7erfs461 f1; gm_tftmsj5vtqvej f2; gm_t4c7ipjj9s3fp f3; gm_t4c7ipjj9s3fp f4; gm_ttiitul0263ld f5; gm_trqfvmjtt4sij f6; gm_trqfvmjtt4sij f7; gm_trqfvmjtt4sij f8; gm_t4c7ipjj9s3fp f9; gm_t4c7ipjj9s3fp f10; gm_t4c7ipjj9s3fp f11; gm_trqfvmjtt4sij f12; gm_t4c7ipjj9s3fp f13; gm_trqfvmjtt4sij f14; gm_t4c7ipjj9s3fp f15; gm_trqfvmjtt4sij f16; gm_t4c7ipjj9s3fp f17; gm_trqfvmjtt4sij f18; gm_trqfvmjtt4sij f19; gm_trqfvmjtt4sij f20; gm_te68172gglous f21; gm_trqfvmjtt4sij f22; gm_trqfvmjtt4sij f23; gm_trqfvmjtt4sij f24; gm_t4c7ipjj9s3fp f25; gm_tqetrgo0q24b5 f26; gm_trqfvmjtt4sij f27; gm_trqfvmjtt4sij f28; gm_t4c7ipjj9s3fp f29; gm_t4c7ipjj9s3fp f30; gm_trqfvmjtt4sij f31; gm_t4c7ipjj9s3fp f32; gm_t4c7ipjj9s3fp f33; gm_tuutj662vpgtg f34; gm_trqfvmjtt4sij f35; gm_trqfvmjtt4sij f36; gm_trqfvmjtt4sij f37; gm_trqfvmjtt4sij f38; gm_trqfvmjtt4sij f39; gm_trqfvmjtt4sij f40; gm_trqfvmjtt4sij f41; gm_trqfvmjtt4sij f42; gm_t4c7ipjj9s3fp f43; gm_t4c7ipjj9s3fp f44; gm_trqfvmjtt4sij f45; gm_t4c7ipjj9s3fp f46; gm_trqfvmjtt4sij f47; gm_t4c7ipjj9s3fp f48; gm_b16_8 f49; void * f50; gm_trqfvmjtt4sij f51; gm_tpo9d7ujpll8m f52; } gm_t1itmv09v9de5;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t5c6io15et1sc) == 32 && _Alignof(gm_t5c6io15et1sc) == 8 && offsetof(gm_t5c6io15et1sc, f0) == 0 && offsetof(gm_t5c6io15et1sc, f1) == 8, "layout of `(i: usize, x: OwnerMark)`");
_Static_assert(sizeof(gm_te68172gglous) == 304 && _Alignof(gm_te68172gglous) == 8 && offsetof(gm_te68172gglous, f0) == 0 && offsetof(gm_te68172gglous, f1) == 40 && offsetof(gm_te68172gglous, f2) == 80 && offsetof(gm_te68172gglous, f3) == 136 && offsetof(gm_te68172gglous, f4) == 192 && offsetof(gm_te68172gglous, f5) == 248, "layout of `Helpers`");
_Static_assert(sizeof(gm_tetdkgoreqmn1) == 224 && _Alignof(gm_tetdkgoreqmn1) == 8 && offsetof(gm_tetdkgoreqmn1, f0) == 0 && offsetof(gm_tetdkgoreqmn1, f1) == 8, "layout of `Unsup!Generated`");
_Static_assert(sizeof(gm_tpo9d7ujpll8m) == 112 && _Alignof(gm_tpo9d7ujpll8m) == 8 && offsetof(gm_tpo9d7ujpll8m, f0) == 0 && offsetof(gm_tpo9d7ujpll8m, f1) == 104, "layout of `?(f: u32, func: Func)`");
_Static_assert(sizeof(gm_trg1s7erfs461) == 1128 && _Alignof(gm_trg1s7erfs461) == 8 && offsetof(gm_trg1s7erfs461, f0) == 0 && offsetof(gm_trg1s7erfs461, f1) == 8 && offsetof(gm_trg1s7erfs461, f2) == 80 && offsetof(gm_trg1s7erfs461, f3) == 120 && offsetof(gm_trg1s7erfs461, f4) == 176 && offsetof(gm_trg1s7erfs461, f5) == 232 && offsetof(gm_trg1s7erfs461, f6) == 288 && offsetof(gm_trg1s7erfs461, f7) == 328 && offsetof(gm_trg1s7erfs461, f8) == 368 && offsetof(gm_trg1s7erfs461, f9) == 408 && offsetof(gm_trg1s7erfs461, f10) == 464 && offsetof(gm_trg1s7erfs461, f11) == 504 && offsetof(gm_trg1s7erfs461, f12) == 560 && offsetof(gm_trg1s7erfs461, f13) == 600 && offsetof(gm_trg1s7erfs461, f14) == 656 && offsetof(gm_trg1s7erfs461, f15) == 712 && offsetof(gm_trg1s7erfs461, f16) == 752 && offsetof(gm_trg1s7erfs461, f17) == 808 && offsetof(gm_trg1s7erfs461, f18) == 816 && offsetof(gm_trg1s7erfs461, f19) == 872, "layout of `CTypes`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
_Static_assert(sizeof(gm_t1itmv09v9de5) == 3760 && _Alignof(gm_t1itmv09v9de5) == 8 && offsetof(gm_t1itmv09v9de5, f0) == 0 && offsetof(gm_t1itmv09v9de5, f1) == 8 && offsetof(gm_t1itmv09v9de5, f2) == 1136 && offsetof(gm_t1itmv09v9de5, f3) == 1160 && offsetof(gm_t1itmv09v9de5, f4) == 1216 && offsetof(gm_t1itmv09v9de5, f5) == 1272 && offsetof(gm_t1itmv09v9de5, f6) == 1328 && offsetof(gm_t1itmv09v9de5, f7) == 1368 && offsetof(gm_t1itmv09v9de5, f8) == 1408 && offsetof(gm_t1itmv09v9de5, f9) == 1448 && offsetof(gm_t1itmv09v9de5, f10) == 1504 && offsetof(gm_t1itmv09v9de5, f11) == 1560 && offsetof(gm_t1itmv09v9de5, f12) == 1616 && offsetof(gm_t1itmv09v9de5, f13) == 1656 && offsetof(gm_t1itmv09v9de5, f14) == 1712 && offsetof(gm_t1itmv09v9de5, f15) == 1752 && offsetof(gm_t1itmv09v9de5, f16) == 1808 && offsetof(gm_t1itmv09v9de5, f17) == 1848 && offsetof(gm_t1itmv09v9de5, f18) == 1904 && offsetof(gm_t1itmv09v9de5, f19) == 1944 && offsetof(gm_t1itmv09v9de5, f20) == 1984 && offsetof(gm_t1itmv09v9de5, f21) == 2024 && offsetof(gm_t1itmv09v9de5, f22) == 2328 && offsetof(gm_t1itmv09v9de5, f23) == 2368 && offsetof(gm_t1itmv09v9de5, f24) == 2408 && offsetof(gm_t1itmv09v9de5, f25) == 2448 && offsetof(gm_t1itmv09v9de5, f26) == 2504 && offsetof(gm_t1itmv09v9de5, f27) == 2552 && offsetof(gm_t1itmv09v9de5, f28) == 2592 && offsetof(gm_t1itmv09v9de5, f29) == 2632 && offsetof(gm_t1itmv09v9de5, f30) == 2688 && offsetof(gm_t1itmv09v9de5, f31) == 2744 && offsetof(gm_t1itmv09v9de5, f32) == 2784 && offsetof(gm_t1itmv09v9de5, f33) == 2840 && offsetof(gm_t1itmv09v9de5, f34) == 2896 && offsetof(gm_t1itmv09v9de5, f35) == 2960 && offsetof(gm_t1itmv09v9de5, f36) == 3000 && offsetof(gm_t1itmv09v9de5, f37) == 3040 && offsetof(gm_t1itmv09v9de5, f38) == 3080 && offsetof(gm_t1itmv09v9de5, f39) == 3120 && offsetof(gm_t1itmv09v9de5, f40) == 3160 && offsetof(gm_t1itmv09v9de5, f41) == 3200 && offsetof(gm_t1itmv09v9de5, f42) == 3240 && offsetof(gm_t1itmv09v9de5, f43) == 3280 && offsetof(gm_t1itmv09v9de5, f44) == 3336 && offsetof(gm_t1itmv09v9de5, f45) == 3392 && offsetof(gm_t1itmv09v9de5, f46) == 3432 && offsetof(gm_t1itmv09v9de5, f47) == 3488 && offsetof(gm_t1itmv09v9de5, f48) == 3528 && offsetof(gm_t1itmv09v9de5, f49) == 3584 && offsetof(gm_t1itmv09v9de5, f50) == 3600 && offsetof(gm_t1itmv09v9de5, f51) == 3608 && offsetof(gm_t1itmv09v9de5, f52) == 3648, "layout of `Gen`");
struct gm_fr_addr_name_g5lrlt3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_adt_mpqv8qe {
    gm_frame h;
    void * ret;
};
struct gm_fr_any_child_ffc8h1k {
    gm_frame h;
    gm_closure l1; /* pred */
    uint8_t l2; /* found */
    uint8_t ret;
};
struct gm_fr_c_abi_type_nu2c2tm {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_callee_name_4k9m9s6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_clause_may_exit_7lc6flj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_clause_name_uk8dps6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_code_has_n3tn5b6 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_content_name_83mn5uf {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_conv_attr_m0b89pc {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_decl_table_l7u1k0g {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* names */
    gm_trqfvmjtt4sij l9; /* ptys */
    gm_trqfvmjtt4sij l14; /* params */
    gm_t9bmh80edou9c l15; /* <temporary array> */
    gm_trqfvmjtt4sij l17; /* args */
    gm_tdmi4fk9nhqa7 l18; /* <temporary array> */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_descriptors_fnnm3h1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_drain_leoibma {
    gm_frame h;
    gm_tqgtd5gtgj65s ret;
};
struct gm_fr_effect_id_5588nqa {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_empty_slice_08mdrht {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_13d6rhn {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_2ntv1ru {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_360e8h0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_465e48m {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_7hq6cu2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_al38tsc {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_asvj6q6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_e8istsh {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ednoq9d {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ekr3omu {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fh7svmj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_gm6v230 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_h06rie2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jagk2mc {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jckt05t {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jjmeosp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_kge4ibg {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_n44tb4r {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_nf78hfm {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_nqh6398 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ppl3r45 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qsq36pn {
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
struct gm_fr_empty_slice_vu26gaj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_expr_may_exit_qfnn2ei {
    gm_frame h;
    void * l0; /* g */
    gm_t9805qtonbsag l2; /* spec_ */
    gm_tsth9qu1dv0g3 l8; /* callee */
    uint8_t ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_msip1bg {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_vogsd72 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_fn_6gkpg0v {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_7u87bne {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_93mqgd8 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_foreign_decl_rld7m78 {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* params */
    gm_tqgtd5gtgj65s ret;
};
struct gm_fr_format_2g08abe {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3g143bv {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5029eea {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_54mn8jj {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_6g8g0f7 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_6gtuelb {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_6vickkk {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7fi363q {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_8n626pi {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_8nhas6j {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_8v2pi8t {
    gm_frame h;
    gm_tmr5oep1578tb l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_99gc605 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_avhd7c9 {
    gm_frame h;
    gm_tftksr4op114e l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_b6qg4rq {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_be3fee1 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bi036vc {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dcns5hm {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_de12mga {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dgfvefu {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_djjuahs {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dsta2ql {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_eagqd9c {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_eg7p593 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_en16lfl {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_eq87866 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fpa95lg {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fsfutor {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_h6b2ofo {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_k64sjdb {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kj13ck1 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l05am3t {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lrgko01 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_o1q76rb {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_or32na5 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t3l2mea {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_uaq30dv {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_v6efj7n {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vj1olp7 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vmlgaaq {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vp0clr1 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_frame_done_014nc4i {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_gen_entry_hj4odev {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* s */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_gen_exports_koai3gr {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_trqfvmjtt4sij l10; /* params */
    gm_trqfvmjtt4sij l11; /* args */
    gm_trqfvmjtt4sij l12; /* stores */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_gen_main_0gsgsok {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* s */
    gm_trqfvmjtt4sij l4; /* calls */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_gen_start_ip4boq0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_generate_6no5tn1 {
    gm_frame h;
    gm_tetdkgoreqmn1 ret;
};
struct gm_fr_generate_entries_br1bd7f {
    gm_frame h;
    gm_trg1s7erfs461 l5; /* <tmp> */
    gm_t4c7ipjj9s3fp l7; /* <tmp> */
    gm_t4c7ipjj9s3fp l8; /* <tmp> */
    gm_ttiitul0263ld l9; /* <tmp> */
    gm_trqfvmjtt4sij l10; /* <tmp> */
    gm_trqfvmjtt4sij l11; /* <tmp> */
    gm_trqfvmjtt4sij l12; /* <tmp> */
    gm_t4c7ipjj9s3fp l13; /* <tmp> */
    gm_t4c7ipjj9s3fp l14; /* <tmp> */
    gm_t4c7ipjj9s3fp l15; /* <tmp> */
    gm_trqfvmjtt4sij l16; /* <tmp> */
    gm_t4c7ipjj9s3fp l17; /* <tmp> */
    gm_trqfvmjtt4sij l18; /* <tmp> */
    gm_t4c7ipjj9s3fp l19; /* <tmp> */
    gm_trqfvmjtt4sij l20; /* <tmp> */
    gm_t4c7ipjj9s3fp l21; /* <tmp> */
    gm_trqfvmjtt4sij l22; /* <tmp> */
    gm_trqfvmjtt4sij l23; /* <tmp> */
    gm_trqfvmjtt4sij l24; /* <tmp> */
    gm_te68172gglous l25; /* <tmp> */
    gm_trqfvmjtt4sij l26; /* <tmp> */
    gm_trqfvmjtt4sij l27; /* <tmp> */
    gm_trqfvmjtt4sij l28; /* <tmp> */
    gm_t4c7ipjj9s3fp l29; /* <tmp> */
    gm_trqfvmjtt4sij l30; /* <tmp> */
    gm_trqfvmjtt4sij l31; /* <tmp> */
    gm_t4c7ipjj9s3fp l32; /* <tmp> */
    gm_t4c7ipjj9s3fp l33; /* <tmp> */
    gm_trqfvmjtt4sij l34; /* <tmp> */
    gm_t4c7ipjj9s3fp l35; /* <tmp> */
    gm_t4c7ipjj9s3fp l36; /* <tmp> */
    gm_trqfvmjtt4sij l37; /* <tmp> */
    gm_trqfvmjtt4sij l38; /* <tmp> */
    gm_trqfvmjtt4sij l39; /* <tmp> */
    gm_trqfvmjtt4sij l40; /* <tmp> */
    gm_trqfvmjtt4sij l41; /* <tmp> */
    gm_trqfvmjtt4sij l42; /* <tmp> */
    gm_trqfvmjtt4sij l43; /* <tmp> */
    gm_trqfvmjtt4sij l44; /* <tmp> */
    gm_t4c7ipjj9s3fp l45; /* <tmp> */
    gm_t4c7ipjj9s3fp l46; /* <tmp> */
    gm_trqfvmjtt4sij l47; /* <tmp> */
    gm_t4c7ipjj9s3fp l48; /* <tmp> */
    gm_trqfvmjtt4sij l49; /* <tmp> */
    gm_t4c7ipjj9s3fp l50; /* <tmp> */
    gm_trqfvmjtt4sij l51; /* <tmp> */
    gm_t1itmv09v9de5 l52; /* g */
    gm_trqfvmjtt4sij l65; /* head */
    gm_trqfvmjtt4sij l68; /* data */
    gm_trqfvmjtt4sij l74; /* out */
    uint64_t l75; /* data_lo */
    uint64_t l76; /* data_hi */
    gm_tk5njge13tk1d l96; /* parts */
    gm_tetdkgoreqmn1 ret;
};
struct gm_fr_generate_object_rgfvem4 {
    gm_frame h;
    gm_tetdkgoreqmn1 ret;
};
struct gm_fr_generate_tests_462i0us {
    gm_frame h;
    gm_tetdkgoreqmn1 ret;
};
struct gm_fr_get_ag7vhtq {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_get_dk5gh28 {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_get_ehtj21s {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_get_espvf96 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_get_m1knqqk {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_get_pftnkei {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_handler_decl_m66mgr8 {
    gm_frame h;
    void * ret;
};
struct gm_fr_handler_lit_b02boff {
    gm_frame h;
    void * ret;
};
struct gm_fr_handler_table_2kdtlit {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* names */
    gm_b16_8 ret;
};
struct gm_fr_hash_map_1c18lnh {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_1s9eugt {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_70470jk {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_76d05hh {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_em2q005 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_fupfd5g {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_g5h3sdu {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_lljdp3h {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_m04clhi {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_p3t5tj8 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_rs5nlfs {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_t9h7o8p {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_tl7apid {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_is_integer_eo6jkc5 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_never_in5ftma {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_plain_qr08i40 {
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
struct gm_fr_is_void_vs2outo {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_ghvav22 {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_layout_of_e6hfj1r {
    gm_frame h;
    gm_tbccvikg6b96e ret;
};
struct gm_fr_line_directive_fjg94gc {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_list_0c2v515 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_0vc6l5i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_1t0a5et {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_938p55h {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_a5k8eig {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_av8bg03 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_hme58st {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_k5efes8 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_lj5vg1g {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nof7vfj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_rc6kc0u {
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
struct gm_fr_mark_owner_uhhhf9f {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_may_unwind_irl2ktm {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_member_names_17el54k {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
    gm_b0_1 w0;
};
struct gm_fr_mirror_text_cr5muc9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_needs_wrapper_ktlbb7i {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_new_namer_5q88o9g {
    gm_frame h;
    gm_ttiitul0263ld ret;
};
struct gm_fr_offsets_eh6vfle {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_owned_d5psnvc {
    gm_frame h;
    gm_t4c7ipjj9s3fp l1; /* by_file */
    gm_trqfvmjtt4sij l2; /* paths */
    gm_trqfvmjtt4sij l3; /* texts */
    gm_trqfvmjtt4sij l12; /* out */
    gm_b16_8 ret;
};
struct gm_fr_owner_of_cirm596 {
    gm_frame h;
    gm_tgqgdb97ka5kt ret;
};
struct gm_fr_param_cty_e8tvhrm {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_pop_168lbb8 {
    gm_frame h;
    gm_t736djc66po0l ret;
};
struct gm_fr_pop_570g79n {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_97b8r3k {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_cn89s92 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_im9d4ro {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_jttnn67 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_k3l9nic {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_lm9fgpm {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_sgikgd8 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_vd1290p {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_queue_cd51r8c {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_queue_spec_khngsmv {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_2onqfca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_97at0vn {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h5ak80l {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_hkmvuvc {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j4iu2lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_m5jo08n {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_p6p5ugs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_vefhvf2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_show_5qbi623 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_show_ref_aqt1t7c {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_sort_skeree7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_spec_bitccmf {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_spec_depth_em7r6vt {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_spec_handler_table_8h552cp {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* names */
    gm_b16_8 ret;
};
struct gm_fr_spec_key_eljlge7 {
    gm_frame h;
    gm_tf9f5m02f56gm ret;
};
struct gm_fr_spec_slot_jild38d {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_src_loc_lrhtj7i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_src_name_0r539f2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_static_bytes_u0uvco3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_takes_fn_n239ee5 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_template_table_h2eglbh {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* pieces */
    gm_trqfvmjtt4sij l6; /* index */
    gm_trqfvmjtt4sij l7; /* specs */
    gm_b16_8 ret;
};
struct gm_fr_unsupported_531q51r {
    gm_frame h;
    gm_t08olq97rscn3 ret;
};
struct gm_fr_view_defs_u461okc {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_with_helpers_edhoqtc {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* out */
    gm_b16_8 ret;
};
struct gm_fr_within_sys_2lc2ugq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_write_08b77ko {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_13301jc {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_2uh64vr {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_3dkm7v9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_4uefpre {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_6rjcu4v {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_8o29aab {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_9falfvm {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_b3knhd6 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_c5hlb1u {
    gm_frame h;
    gm_tfeab279c16rr l1; /* args */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_d85j2rp {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_gb1ne57 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_gi12t4o {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_gk1hd5k {
    gm_frame h;
    gm_ti71ll5knnvij l1; /* args */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_hclpi1b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_j5p5p7n {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_jqf697l {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_kdsk4q9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_lu60214 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_n0bkcka {
    gm_frame h;
    gm_tcrdb1inj22os l1; /* args */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_oj4s9u6 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_okup0j2 {
    gm_frame h;
    gm_tud7oovfvnf43 l1; /* args */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_pfrpr5k {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_qph5tm9 {
    gm_frame h;
    gm_tif3elqk01bdl l1; /* args */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_qqu2rl9 {
    gm_frame h;
    gm_tfeab279c16rr l1; /* args */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_sf9mjak {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_ticpdcl {
    gm_frame h;
    gm_tud7oovfvnf43 l1; /* args */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_u3br5ci {
    gm_frame h;
    gm_tud7oovfvnf43 l1; /* args */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_u8u1amh {
    gm_frame h;
    gm_tfeab279c16rr l1; /* args */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_ug67q96 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_usci3o4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_uuia5cm {
    gm_frame h;
    gm_tq3m5qgvkpje8 l1; /* args */
    gm_tftb1odc739t5 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
inline bool gm_eqkricceou4u(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b16_8 gm_f_absolute_6pqhsaj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_addr_name_g5lrlt3(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void * gm_f_adt_mpqv8qe(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_any_child_ffc8h1k(void * a0, gm_closure a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_blob_typedefs_4nfn4l8(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_by_ref_fi0o860(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_c_abi_type_nu2c2tm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_c_string_n7h27b3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_callee_name_4k9m9s6(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_clause_may_exit_7lc6flj(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t9805qtonbsag a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_clause_name_uk8dps6(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t9805qtonbsag a2);
__attribute__((aligned(4))) uint8_t gm_f_code_has_n3tn5b6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_content_name_83mn5uf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_conv_attr_m0b89pc(uint8_t a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_cty_66ee0kf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_ctypes_j6au0st(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_decl_name_fode8m9(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_decl_table_l7u1k0g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_desc_0cag2kq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_descriptors_fnnm3h1(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_drain_leoibma(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint32_t gm_f_effect_id_5588nqa(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_effect_ref_6sjtehp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_08mdrht(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_13d6rhn(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2ntv1ru(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_360e8h0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_465e48m(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7hq6cu2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_al38tsc(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_asvj6q6(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_e8istsh(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ednoq9d(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ekr3omu(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_gm6v230(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_h06rie2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jagk2mc(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jckt05t(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jjmeosp(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_kge4ibg(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n44tb4r(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_nf78hfm(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_nqh6398(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ppl3r45(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qsq36pn(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vu26gaj(void);
__attribute__((aligned(4))) uint8_t gm_f_expr_may_exit_qfnn2ei(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9805qtonbsag a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_msip1bg(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_vogsd72(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_7io0v3c(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_csk9ups(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_efqiqfc(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_hb6lt4h(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_ovslcai(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_ve3qs5b(void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_6gkpg0v(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7u87bne(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_93mqgd8(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_key_b6f5f24(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_name_6n34573(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_foreign_decl_rld7m78(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_format_01lon97(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_2bh0ost(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_2g08abe(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_2kt92vg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_3g143bv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_5029eea(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_54mn8jj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_6g8g0f7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_6gtuelb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_6vickkk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_74pd3in(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_7fi363q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t19enj90vg8bd a0);
inline __attribute__((aligned(4))) void gm_f_format_8n626pi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_8nhas6j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_8v2pi8t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_99gc605(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_avhd7c9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_b6qg4rq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_be3fee1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_bi036vc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_dcns5hm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_de12mga(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t19enj90vg8bd a0);
inline __attribute__((aligned(4))) void gm_f_format_dgfvefu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_djjuahs(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_dq6oc41(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_dsta2ql(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_eagqd9c(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_eg7p593(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_en16lfl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_eq87866(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_fpa95lg(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_fsfutor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_h12an8k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_h6b2ofo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jqrpfot(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_k64sjdb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_kj13ck1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_l05am3t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_lo2svpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_lrgko01(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_mccr87v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_motlhb5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_ncqd055(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_o1q76rb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_og1p27h(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_or32na5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_rc5a5cm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_rqlkk21(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_s2bvqvf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_sc59up9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t19enj90vg8bd a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_t22qvc1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_t3l2mea(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_uaq30dv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_v6efj7n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_vj1olp7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_vmlgaaq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_vp0clr1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_frame_done_014nc4i(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void * gm_f_func_in17r5n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_gdb_script_section_320gurp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_gen_consts_6ibghrc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_gen_entry_hj4odev(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_gen_exports_koai3gr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_gen_fn_2fqvb90(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t9805qtonbsag a2);
__attribute__((aligned(4))) void gm_f_gen_main_0gsgsok(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_gen_start_ip4boq0(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_gen_trap_hook_lu55viv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_generate_6no5tn1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2);
__attribute__((aligned(4))) void gm_f_generate_entries_br1bd7f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2, const void * a3);
inline __attribute__((aligned(4))) void gm_f_generate_object_rgfvem4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_generate_tests_462i0us(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_get_9k2rdij(void *gm_out, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_ag7vhtq(void * a0, gm_tcc1rq4n1avb2 a1);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_dk5gh28(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_get_e3joqcc(void *gm_out, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_ehtj21s(void * a0, gm_tcc1rq4n1avb2 a1);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_get_gnmase9(void *gm_out, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_m1knqqk(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_pftnkei(void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_guard_o7k9a0e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_handler_decl_m66mgr8(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void * gm_f_handler_lit_b02boff(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_handler_name_6km8n3f(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_handler_table_2kdtlit(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint64_t gm_f_hash64_apkb7bo(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_hash_map_1c18lnh(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_1s9eugt(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_70470jk(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_76d05hh(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_em2q005(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_fupfd5g(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_g5h3sdu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_lljdp3h(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_m04clhi(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_p3t5tj8(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_rs5nlfs(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_t9h7o8p(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_tl7apid(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_hash_text_4aekv10(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_insert_v6fq3iv(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_introsort_r6p6ckl(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) uint8_t gm_f_is_ctl_oclllo3(void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) uint8_t gm_f_is_default_spec_cdd8jng(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_eo6jkc5(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_never_in5ftma(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_plain_qr08i40(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_17uennd(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_void_vs2outo(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_keys_qc5bjlr_s31qjug(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_cs9ufftihtbpo2 gm_f_line_col_9vn24mu(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_line_directive_fjg94gc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) void gm_f_list_0c2v515(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_0vc6l5i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_1t0a5et(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_938p55h(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_a5k8eig(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_av8bg03(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_hme58st(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_k5efes8(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_lj5vg1g(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_rc6kc0u(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_mark_owner_uhhhf9f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tgqgdb97ka5kt a1);
__attribute__((aligned(4))) uint8_t gm_f_may_unwind_irl2ktm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_member_names_17el54k(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_mirror_lcjegsf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_mirror_text_cr5muc9(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_needs_wrapper_ktlbb7i(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_new_helpers_4q783cn(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_new_namer_5q88o9g(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_owned_d5psnvc(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tgqgdb97ka5kt gm_f_owner_of_cirm596(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_cty_e8tvhrm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t736djc66po0l gm_f_pop_168lbb8(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_97b8r3k(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_cn89s92(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_jttnn67(void * a0, gm_tf9f5m02f56gm a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_lm9fgpm(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_sgikgd8(void * a0, gm_tcc1rq4n1avb2 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_074448v(void * a0, gm_b16_8 a1, uint32_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_3g1tdpr(void * a0, gm_tcc1rq4n1avb2 a1, uint8_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_7shfu9u(void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_bii31he(void * a0, gm_tl874t2atnlh3 a1, uint32_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_cmvik6u(void * a0, gm_tcc1rq4n1avb2 a1, uint8_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_dm7u1sg(void * a0, uint32_t a1, const void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_po09vud(void * a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_qoojoqv(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_v6g5pup(void * a0, uint32_t a1, uint8_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_queue_cd51r8c(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_queue_spec_khngsmv(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_97at0vn(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h5ak80l(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hkmvuvc(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_m5jo08n(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_p6p5ugs(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_vefhvf2(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_repr_53quqki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_ref_aqt1t7c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_sort_skeree7(gm_b16_8 a0);
__attribute__((aligned(4))) uint32_t gm_f_spec_bitccmf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tf9f5m02f56gm a1);
inline __attribute__((aligned(4))) uint32_t gm_f_spec_depth_em7r6vt(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_spec_handler_table_8h552cp(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) gm_tf9f5m02f56gm gm_f_spec_key_eljlge7(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_spec_literal_lmkpbc1(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_spec_name_jptsbbf(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) uint32_t gm_f_spec_slot_jild38d(gm_t9805qtonbsag a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_spec_text_iq7r8vn(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_src_loc_lrhtj7i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_src_name_0r539f2(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_static_bytes_u0uvco3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_takes_fn_n239ee5(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_template_table_h2eglbh(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_unsupported_531q51r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_view_defs_u461okc(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_with_helpers_edhoqtc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_within_sys_2lc2ugq(void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_04pf35h(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_08b77ko(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_0ie0tub(void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_0mj6rce(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_13301jc(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2uh64vr(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2venbiq(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_3dkm7v9(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_4uefpre(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_5qef5td(void * a0, gm_t4ce0jd3tdro4 a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_5um95qj(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_6rjcu4v(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_70cb4d8(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8o29aab(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8p1ions(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_9falfvm(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_av01663(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_b3knhd6(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_bigf9mn(void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_buk3227(void * a0, gm_t2lqu8jsj511d a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_c5hlb1u(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_d85j2rp(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_e55tuqo(void * a0, gm_t88rcucg1b7uk a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_gb1ne57(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_gdgk8he(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_gi12t4o(void * a0, gm_t19enj90vg8bd a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_gk1hd5k(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_glpclq4(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_hclpi1b(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_hqo1u7h(void * a0, gm_t4ce0jd3tdro4 a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_j5p5p7n(void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jqf697l(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_k2pnevr(void * a0, gm_t19enj90vg8bd a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_kdsk4q9(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_lu60214(void * a0, gm_t2lqu8jsj511d a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_n0bkcka(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_oepvt6a(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_oj4s9u6(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_okup0j2(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_pfrpr5k(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_qb1f4ht(void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_qgchepk(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_qph5tm9(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_qqu2rl9(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_s3v41br(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_sf9mjak(void * a0, gm_t4ce0jd3tdro4 a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ticpdcl(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u3br5ci(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u8u1amh(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ug67q96(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_usci3o4(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_uuia5cm(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_vm7g5p8(void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_vq1ifl4(void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b);
extern uint8_t gm_c9e8ns5bad4[16] __attribute__((aligned(8)));
extern uint8_t gm_ccmgjp6th6r[16] __attribute__((aligned(8)));
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern uint8_t gm_cgsmfcq937e_2[16] __attribute__((aligned(8)));
extern uint8_t gm_cto913mafo2[8] __attribute__((aligned(8)));
extern uint8_t gm_ctvsbqjhdl3[12] __attribute__((aligned(4)));
extern void *const gm_hlak2jvvbqg4[];
extern const uint8_t gm_s04qus0luoga5[5];
extern const uint8_t gm_s0fgdcd1ggor2[18];
extern const uint8_t gm_s107olobe2ou0[28];
extern const uint8_t gm_s10iv34fu7qe7[3];
extern const uint8_t gm_s1jaan454qth6[19];
extern const uint8_t gm_s1l1fmn0hvo9d[68];
extern const uint8_t gm_s205esok2u8pk[4];
extern const uint8_t gm_s23s60tf3fq5h[34];
extern const uint8_t gm_s2gnpj7k4l22n[2];
extern const uint8_t gm_s2kvk8bnjk52t[12];
extern const uint8_t gm_s2nn3moc8tfri[17];
extern const uint8_t gm_s2oqp92hnbubt[11];
extern const uint8_t gm_s34o1ojfv2p3d[17];
extern const uint8_t gm_s3dov523r8ujn[53];
extern const uint8_t gm_s3nnnhoq632g4[14];
extern const uint8_t gm_s4672be3dhb96[11];
extern const uint8_t gm_s4o541hcfllua[9];
extern const uint8_t gm_s4ql65vkv7h9p[13];
extern const uint8_t gm_s514u4evks75f[21];
extern const uint8_t gm_s5l9jdmrtaean[11];
extern const uint8_t gm_s5p0i898s3u4j[11];
extern const uint8_t gm_s5vn66tnlipdf[19];
extern const uint8_t gm_s64fvfprf3827[63];
extern const uint8_t gm_s65aicqb626jk[54];
extern const uint8_t gm_s698sbcv3nmjk[69];
extern const uint8_t gm_s6aq8jknpl0n5[27];
extern const uint8_t gm_s6unleea2pm63[35];
extern const uint8_t gm_s6vghnf0vdf2t[51];
extern const uint8_t gm_s70ks96e05b2g[39];
extern const uint8_t gm_s71434ub9jp42[11];
extern const uint8_t gm_s7fdjhp9ga5oo[8];
extern const uint8_t gm_s852r4rtnj5dj[54];
extern const uint8_t gm_s8rsp9val5ful[5];
extern const uint8_t gm_sa10lthaugqih[45];
extern const uint8_t gm_sa56bd0p8nuib[143];
extern const uint8_t gm_sa8q5t8i6ht30[6];
extern const uint8_t gm_sa9ktp2t0q9rs[34];
extern const uint8_t gm_sa9pve8r9407o[35];
extern const uint8_t gm_sar77rocuptb5[31];
extern const uint8_t gm_sarl8beoguiit[20];
extern const uint8_t gm_sasi3vvkcdvo0[40];
extern const uint8_t gm_savg92sdg6694[35];
extern const uint8_t gm_sb2k9e71jsraf[7];
extern const uint8_t gm_sbgqae2tm3q08[39];
extern const uint8_t gm_scj0sm2kpivk4[9];
extern const uint8_t gm_scj1kuqp0242l[6];
extern const uint8_t gm_sck2rq9li012q[23];
extern const uint8_t gm_scrjehcehrfpv[15];
extern const uint8_t gm_sd96cluvcn3lj[33];
extern const uint8_t gm_sdbocdqbukr6k[77];
extern const uint8_t gm_sdi44r3kug6n6[14];
extern const uint8_t gm_sdnjmvrkc1r7r[12];
extern const uint8_t gm_sdqssko83l66b[28];
extern const uint8_t gm_sdsb9longvn22[40];
extern const uint8_t gm_sdvpeftn9n4aa[7];
extern const uint8_t gm_sekmbu83qkg9c[44];
extern const uint8_t gm_seosa3iobfc09[35];
extern const uint8_t gm_sfa00o5gkcj29[5];
extern const uint8_t gm_sfn5aos9cps1h[13];
extern const uint8_t gm_sg2r3pcbg3kr2[32];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sgli4lbivdt1d[38];
extern const uint8_t gm_sgpgu8iag22ja[15];
extern const uint8_t gm_sh16qm7deqse5[7];
extern const uint8_t gm_sh5lbh5j6oqpf[36];
extern const uint8_t gm_shlk3g0tk7m4f[31];
extern const uint8_t gm_sihr7qmh227og[26];
extern const uint8_t gm_siji3oo7ud53t[10];
extern const uint8_t gm_sikpqtja8pqpv[3];
extern const uint8_t gm_sje4rbc88csdu[7];
extern const uint8_t gm_sjgqjs06244vq[21];
extern const uint8_t gm_sjtp8ip8t28uh[7];
extern const uint8_t gm_sk04qg20jmac0[4];
extern const uint8_t gm_sk0aaujkmprtl[35];
extern const uint8_t gm_sk0qegfrnee00[4];
extern const uint8_t gm_skdpq4t6o94o2[25];
extern const uint8_t gm_sknjgunq9356n[30];
extern const uint8_t gm_sl5h9isb3bijl[5];
extern const uint8_t gm_slbs27elv52b5[30];
extern const uint8_t gm_slku641rnhls8[7];
extern const uint8_t gm_slu6nnvsgea2t[41];
extern const uint8_t gm_sm7qdm4gk14td[7];
extern const uint8_t gm_smbbaf3n1gml2[8];
extern const uint8_t gm_smdv045psk1u8[25];
extern const uint8_t gm_smhvmmegaur89[7];
extern const uint8_t gm_sml9migrmgip7[103];
extern const uint8_t gm_sn275ucdkgsk6[82];
extern const uint8_t gm_snokcvhtidt97[6];
extern const uint8_t gm_sobumja6poqii[13];
extern const uint8_t gm_sog1crmvcid87[59];
extern const uint8_t gm_soo4rtckloi4d[13];
extern const uint8_t gm_soqt3jnmovq5l[4];
extern const uint8_t gm_sp5lvq92b4in0[11];
extern const uint8_t gm_sp9ailodohkkj[40];
extern const uint8_t gm_spbdk1kpko0k2[141];
extern const uint8_t gm_spervk999ikim[8];
extern const uint8_t gm_spf3a0kmdpgm5[6];
extern const uint8_t gm_sppi971hm2k0k[15];
extern const uint8_t gm_sq77ose3aa457[24];
extern const uint8_t gm_sq9pusqm0kq2f[7];
extern const uint8_t gm_sqg9do4ma3giu[6];
extern const uint8_t gm_sqldj8dr0j7rf[12];
extern const uint8_t gm_sqtc9fffut4s2[13];
extern const uint8_t gm_sr3lr7c9psgf2[12];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcd5arorrf8jkg[];
extern const char gm_srcht195tfrfuqo[];
extern const char gm_srckfibkpt681aa[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcuj9anpi7p1mq[];
extern const char gm_srcuokv9ad9oqqb[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_sreie3avjjb9l[64];
extern const uint8_t gm_sri1d6adh4kkk[11];
extern const uint8_t gm_srjb0lv60gms5[62];
extern const uint8_t gm_srnhu66btobke[9];
extern const uint8_t gm_ssao50s66pj11[8];
extern const uint8_t gm_stqjev4m440fr[46];
extern const uint8_t gm_strca8t49bpqu[2];
extern const uint8_t gm_sts5j6f5pkov0[5];
extern const uint8_t gm_stuj0mv5nl55t[4];
extern const uint8_t gm_su20i5iji4ao3[4];
extern const uint8_t gm_sucip17pohakb[22];
extern const uint8_t gm_sucsarpa3psmp[55];
extern const uint8_t gm_suk4smocgp7ip[54];
extern const uint8_t gm_suqchf5vh2hqp[5];
extern const uint8_t gm_suvh4o70k4cg3[28];
extern const uint8_t gm_sv7tbc2gvmpa1[100];
extern const uint8_t gm_svgsom6hbqtcn[40];
extern const uint8_t gm_svl13be5ca8rt[5];
extern const gm_tmpl gm_tpl0c7sat2t7j6v;
extern const gm_tmpl gm_tpl10tenob4hk5k;
extern const gm_tmpl gm_tpl20a4acojo21c;
extern const gm_tmpl gm_tpl3kt8q9c3ih5v;
extern const gm_tmpl gm_tpl44b660pgg1c8;
extern const gm_tmpl gm_tpl4dkmnviaa508;
extern const gm_tmpl gm_tpl4tk9leh8nue4;
extern const gm_tmpl gm_tpl59s4m8f706c9;
extern const gm_tmpl gm_tpl5n5ddjmjlhjb;
extern const gm_tmpl gm_tpl5utcgga3g5km;
extern const gm_tmpl gm_tpl6pohii1e8pbg;
extern const gm_tmpl gm_tpl70tkged35ahl;
extern const gm_tmpl gm_tpl77gpruiccsl2;
extern const gm_tmpl gm_tpl7t589bv3t4us;
extern const gm_tmpl gm_tpl86pqqgq5ch0o;
extern const gm_tmpl gm_tpl87um69vt0amn;
extern const gm_tmpl gm_tpl9900stiqclaj;
extern const gm_tmpl gm_tpla3k9rgcg5o3q;
extern const gm_tmpl gm_tplaos6tmpdtrie;
extern const gm_tmpl gm_tplb4rog3tmrj9n;
extern const gm_tmpl gm_tplb899rpghqa93;
extern const gm_tmpl gm_tplbk128im3qlnl;
extern const gm_tmpl gm_tplcoet0p57pg4s;
extern const gm_tmpl gm_tplebqthjijvqt0;
extern const gm_tmpl gm_tplei8v65ors6jc;
extern const gm_tmpl gm_tplemb9urr8jmq6;
extern const gm_tmpl gm_tples1m1m9iv6b0;
extern const gm_tmpl gm_tplevafblg3gmq9;
extern const gm_tmpl gm_tplf3en286s8fa5;
extern const gm_tmpl gm_tplfpi7koctqa7b;
extern const gm_tmpl gm_tplfqr816g9de0k;
extern const gm_tmpl gm_tplfsmasr3tgalo;
extern const gm_tmpl gm_tplh2sse9mocm92;
extern const gm_tmpl gm_tplhkjcpttbsqt0;
extern const gm_tmpl gm_tplhu7i2tvvfh15;
extern const gm_tmpl gm_tpli0v9k7mac3tj;
extern const gm_tmpl gm_tplj92mk62u70db;
extern const gm_tmpl gm_tpljkde9cibn2er;
extern const gm_tmpl gm_tpljueetjft1ril;
extern const gm_tmpl gm_tplk1botso018is;
extern const gm_tmpl gm_tplk27ieg1b14ol;
extern const gm_tmpl gm_tplk4i636uhvi9m;
extern const gm_tmpl gm_tplkd4irlmplk3e;
extern const gm_tmpl gm_tpll6jntfkrhtt4;
extern const gm_tmpl gm_tpllikrj1hgkvih;
extern const gm_tmpl gm_tpllm484u6fuiq3;
extern const gm_tmpl gm_tplncn0njcasr6v;
extern const gm_tmpl gm_tplnqad0r40bfej;
extern const gm_tmpl gm_tplnv6l3ecgreid;
extern const gm_tmpl gm_tplo12k9ajl5nes;
extern const gm_tmpl gm_tplq0mvos8lisnf;
extern const gm_tmpl gm_tplqgt00aqumuje;
extern const gm_tmpl gm_tplqt9e8rihgogv;
extern const gm_tmpl gm_tplr56tn8lpufgs;
extern const gm_tmpl gm_tplr5guiqmg1mn9;
extern const gm_tmpl gm_tplrcqo0t095bsv;
extern const gm_tmpl gm_tplrkvt0j8tqlqg;
extern const gm_tmpl gm_tplrodjuj3fgmsa;
extern const gm_tmpl gm_tplrp4haaemeqja;
extern const gm_tmpl gm_tpls8octb03drbp;
extern const gm_tmpl gm_tplsf6r3t44km8l;
extern const gm_tmpl gm_tpltgqv8dvi81j3;
extern const gm_tmpl gm_tplthb0oue219bq;
extern const gm_tmpl gm_tplu7gq2cm8aanh;
extern const gm_tmpl gm_tplvttcl46jpcs3;
extern const gm_tmpl gm_tplvvtt1jk959m0;
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
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    if (!gm_eq467d5cm1d5(a + 8, b + 8)) return false;
    return true;
}
inline bool gm_eqkricceou4u(const uint8_t *a, const uint8_t *b) {
    if (a[4] != b[4]) return false;
    return !a[4] || gm_eq467d5cm1d5(a, b);
}
inline __attribute__((aligned(4))) void * gm_f_adt_mpqv8qe(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_adt_mpqv8qe F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 1088));
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t3) + 1088) + 8));
    uint32_t t6 = gm_l1;
    uint64_t t7 = (uint64_t)t6;
    if ((uintptr_t)t7 >= (uintptr_t)(t5)) gm_trap_index((uintptr_t)t7, t5, gm_srcuj9anpi7p1mq, 135, 63);
    void * t8 = (void *)(((uint8_t *)t4) + (uintptr_t)t7 * 184);
    F.ret = t8;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_callee_name_4k9m9s6(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_callee_name_4k9m9s6 F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint32_t t3 = gm_l1;
    gm_b16_8 t4 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_content_name_83mn5uf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_content_name_83mn5uf F;
    gm_b16_8 gm_l0; /* prefix */
    gm_b16_8 gm_l1; /* content */
    GM_STACK_CHECK(&F, 201u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l2;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        gm_b16_8 t4 = gm_f_hash_text_4aekv10((void *)NULL, cx, NULL, t3, ((uint64_t)12ULL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6j6smj7qoctu t5;
        GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t2);
        GM_ST(gm_b16_8, ((uint8_t *)&t5 + 16), t4);
        gm_f_format_8n626pi((void *)&gm_l2, (void *)NULL, cx, NULL, &t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 8, gm_srcht195tfrfuqo, 86, 82);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_descriptors_fnnm3h1(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_descriptors_fnnm3h1 F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 217u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tabb710d9mq2e gm_l1;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 288));
        void * t4 = gm_l0;
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)t4) + 328));
        void * t6 = gm_l0;
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)t6) + 368));
        gm_tfeab279c16rr t8;
        GM_ST(gm_b16_8, ((uint8_t *)&t8 + 0), t3);
        GM_ST(gm_b16_8, ((uint8_t *)&t8 + 16), t5);
        GM_ST(gm_b16_8, ((uint8_t *)&t8 + 32), t7);
        gm_f_format_o1q76rb((void *)&gm_l1, (void *)NULL, cx, NULL, &t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
    if (!t9) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l1) + 8, gm_srckfibkpt681aa, 627, 63);
    gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 8));
    gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&t10));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_08mdrht(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_08mdrht F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_13d6rhn(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_13d6rhn F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2ntv1ru(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_2ntv1ru F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_360e8h0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_360e8h0 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 20)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_465e48m(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_465e48m F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7hq6cu2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_7hq6cu2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_al38tsc(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_al38tsc F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_asvj6q6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_asvj6q6 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_e8istsh(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_e8istsh F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 2)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ednoq9d(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ednoq9d F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ekr3omu(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ekr3omu F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fh7svmj F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_h06rie2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_h06rie2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jagk2mc(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jagk2mc F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jckt05t(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jckt05t F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_kge4ibg(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_kge4ibg F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n44tb4r(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_n44tb4r F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_nf78hfm(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_nf78hfm F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_nqh6398(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_nqh6398 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qsq36pn(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qsq36pn F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_msip1bg(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_msip1bg F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_97at0vn(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 32);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 32)));
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
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_file_o15fqku F;
    void * gm_l0; /* sm */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_srcv1nri2247d2r, 82, 75);
    void * t7 = (void *)(((uint8_t *)t3) + (uintptr_t)t6 * 80);
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_fn_6gkpg0v(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_6gkpg0v F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 67u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)t2));
    void * t4 = gm_l1;
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)t4));
    uint8_t t6 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t3), ((uint8_t *)&t5));
    F.ret = t6;
    goto gm_fret;
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_93mqgd8(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_93mqgd8 F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 35u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_expr_may_exit_qfnn2ei *)((&F.h)->up))->l0));
    void * t3 = gm_l0;
    gm_t9805qtonbsag t4 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)&((struct gm_fr_expr_may_exit_qfnn2ei *)((&F.h)->up))->l2));
    uint8_t t5 = gm_f_expr_may_exit_qfnn2ei((void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) void gm_f_format_2g08abe(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_2g08abe F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sri1d6adh4kkk));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplq0mvos8lisnf), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_3g143bv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_3g143bv F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sgpgu8iag22ja));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(14));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplnqad0r40bfej), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_5029eea(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_5029eea F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sl5h9isb3bijl));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(4));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_74pd3in((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplei8v65ors6jc), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_54mn8jj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_54mn8jj F;
    gm_t3877pk7lv748 gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s71434ub9jp42));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(10));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_2kt92vg((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpljkde9cibn2er), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_6g8g0f7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_6g8g0f7 F;
    gm_t5hdnnftkup6m gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s5p0i898s3u4j));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(10));
    gm_t5hdnnftkup6m t3 = GM_LD(gm_t5hdnnftkup6m, ((uint8_t *)a0));
    gm_f_format_s2bvqvf((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplk27ieg1b14ol), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_6gtuelb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_6gtuelb F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sihr7qmh227og));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(25));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplb899rpghqa93), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_6vickkk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0) {
    struct gm_fr_format_6vickkk F;
    gm_t2lqu8jsj511d gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t2lqu8jsj511d t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4672be3dhb96));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
    gm_f_format_2bh0ost((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplrodjuj3fgmsa), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_7fi363q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t19enj90vg8bd a0) {
    struct gm_fr_format_7fi363q F;
    gm_t19enj90vg8bd gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t19enj90vg8bd t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdqssko83l66b));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(27));
    gm_f_format_sc59up9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplfpi7koctqa7b), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_8n626pi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_8n626pi F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sl5h9isb3bijl));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(4));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplei8v65ors6jc), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_8nhas6j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_8nhas6j F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s4o541hcfllua));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(8));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_jqrpfot((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplrp4haaemeqja), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_8v2pi8t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_8v2pi8t F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sbgqae2tm3q08));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(38));
    gm_tmr5oep1578tb t3 = GM_LD(gm_tmr5oep1578tb, ((uint8_t *)a0));
    gm_f_format_motlhb5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl20a4acojo21c), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_avhd7c9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_avhd7c9 F;
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_suvh4o70k4cg3));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(27));
    gm_tftksr4op114e t3 = GM_LD(gm_tftksr4op114e, ((uint8_t *)a0));
    gm_f_format_h12an8k((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplr5guiqmg1mn9), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_b6qg4rq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_b6qg4rq F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sa9pve8r9407o));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(34));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpllikrj1hgkvih), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_be3fee1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_be3fee1 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sri1d6adh4kkk));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplq0mvos8lisnf), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_bi036vc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_bi036vc F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sfa00o5gkcj29));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl86pqqgq5ch0o), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_dcns5hm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_dcns5hm F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sb2k9e71jsraf));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_og1p27h((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplfqr816g9de0k), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_de12mga(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t19enj90vg8bd a0) {
    struct gm_fr_format_de12mga F;
    gm_t19enj90vg8bd gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t19enj90vg8bd t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_spbdk1kpko0k2));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(140));
    gm_f_format_sc59up9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl9900stiqclaj), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_dgfvefu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_dgfvefu F;
    gm_t3877pk7lv748 gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s5l9jdmrtaean));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(10));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_2kt92vg((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl4tk9leh8nue4), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_djjuahs(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_djjuahs F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sk0qegfrnee00));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpls8octb03drbp), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_eagqd9c(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_eagqd9c F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s107olobe2ou0));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(27));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplbk128im3qlnl), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_eg7p593(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_eg7p593 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sgli4lbivdt1d));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(37));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl70tkged35ahl), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_en16lfl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_en16lfl F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sl5h9isb3bijl));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(4));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_t22qvc1((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplei8v65ors6jc), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_fpa95lg(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_fpa95lg F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4ce0jd3tdro4 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s7fdjhp9ga5oo));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_lo2svpj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplk1botso018is), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_h6b2ofo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_h6b2ofo F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_stuj0mv5nl55t));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplf3en286s8fa5), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_k64sjdb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_k64sjdb F;
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_srnhu66btobke));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(8));
    gm_t0uuhaljsf410 t3 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
    gm_f_format_rqlkk21((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplvttcl46jpcs3), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_kj13ck1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_kj13ck1 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sdvpeftn9n4aa));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplkd4irlmplk3e), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_lrgko01(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_lrgko01 F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4ce0jd3tdro4 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssao50s66pj11));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_lo2svpj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplrkvt0j8tqlqg), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_o1q76rb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_o1q76rb F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sb2k9e71jsraf));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_jqrpfot((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplfqr816g9de0k), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_or32na5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_or32na5 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sjtp8ip8t28uh));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_01lon97((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplqt9e8rihgogv), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_t3l2mea(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_t3l2mea F;
    gm_tommf4j9upomc gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_spf3a0kmdpgm5));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(5));
    gm_tommf4j9upomc t3 = GM_LD(gm_tommf4j9upomc, ((uint8_t *)a0));
    gm_f_format_dq6oc41((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplthb0oue219bq), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_uaq30dv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_uaq30dv F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sk04qg20jmac0));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplhkjcpttbsqt0), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_v6efj7n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_v6efj7n F;
    gm_tiilmfd16v3fc gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sqldj8dr0j7rf));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(11));
    gm_tiilmfd16v3fc t3 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)a0));
    gm_f_format_rc5a5cm((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpljueetjft1ril), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_vj1olp7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_vj1olp7 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sa56bd0p8nuib));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(142));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpla3k9rgcg5o3q), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_vmlgaaq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_vmlgaaq F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sa8q5t8i6ht30));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(5));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_01lon97((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl4dkmnviaa508), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_vp0clr1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_vp0clr1 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sfn5aos9cps1h));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(12));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl5utcgga3g5km), t2);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_gen_start_ip4boq0(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_gen_start_ip4boq0 F;
    void * gm_l0; /* g */
    GM_STACK_CHECK(&F, 118u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int32_t gm_l1;
    gm_tabb710d9mq2e gm_l2;
    void * t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, ((((uint8_t *)t2) + 1136) + 1));
    if (t3) {
        gm_l1 = ((int32_t)1LL);
    } else {
        gm_l1 = ((int32_t)0LL);
    }
    int32_t t4 = gm_l1;
    gm_t19enj90vg8bd t5;
    GM_ST(int32_t, ((uint8_t *)&t5 + 0), t4);
    gm_f_format_de12mga((void *)&gm_l2, (void *)NULL, cx, NULL, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 8, gm_srcuokv9ad9oqqb, 9, 12);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
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
inline __attribute__((aligned(4))) void gm_f_generate_6no5tn1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2) {
    struct gm_fr_generate_6no5tn1 F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* entry */
    gm_tftmsj5vtqvej gm_l2; /* opts */
    gm_tdvq40fmif5j0 gm_l3; /* entries */
    GM_STACK_CHECK(&F, 128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&gm_l2, a2, sizeof gm_l2);
    uint32_t t2 = gm_l1;
    gm_tdvq40fmif5j0 t3;
    GM_ST(uint32_t, ((uint8_t *)&t3 + 0), t2);
    gm_l3 = t3;
    {
        void * t4 = gm_l0;
        void * t5 = (void *)((uint8_t *)&gm_l3);
        uint64_t t6 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 4)));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        gm_tftmsj5vtqvej t9 = GM_LD(gm_tftmsj5vtqvej, ((uint8_t *)&gm_l2));
        gm_f_generate_entries_br1bd7f((void *)&(*(gm_tetdkgoreqmn1 *)gm_out), (void *)NULL, cx, NULL, t4, t8, ((uint8_t)0), &t9);
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
inline __attribute__((aligned(4))) void gm_f_generate_object_rgfvem4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_generate_object_rgfvem4 F;
    void * gm_l0; /* p */
    gm_tftmsj5vtqvej gm_l1; /* opts */
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    void * t2 = gm_l0;
    gm_b16_8 t3 = gm_f_empty_slice_d914i5u();
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    gm_tftmsj5vtqvej t5 = GM_LD(gm_tftmsj5vtqvej, ((uint8_t *)&gm_l1));
    gm_f_generate_entries_br1bd7f((void *)&(*(gm_tetdkgoreqmn1 *)gm_out), (void *)NULL, cx, NULL, t2, t4, ((uint8_t)0), &t5);
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
inline __attribute__((aligned(4))) void gm_f_generate_tests_462i0us(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2) {
    struct gm_fr_generate_tests_462i0us F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* entries */
    gm_tftmsj5vtqvej gm_l2; /* opts */
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&gm_l2, a2, sizeof gm_l2);
    void * t2 = gm_l0;
    gm_b16_8 t3 = gm_l1;
    gm_tftmsj5vtqvej t4 = GM_LD(gm_tftmsj5vtqvej, ((uint8_t *)&gm_l2));
    gm_f_generate_entries_br1bd7f((void *)&(*(gm_tetdkgoreqmn1 *)gm_out), (void *)NULL, cx, NULL, t2, t3, ((uint8_t)1), &t4);
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
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_dk5gh28(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_dk5gh28 F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 118u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t4 = gm_f_find_hb6lt4h(t2, t3);
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
    uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t9) + (uintptr_t)t11 * 12) + 8));
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
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_ehtj21s(void * a0, gm_tcc1rq4n1avb2 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_ehtj21s F;
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
    gm_tf5srh4mp4b3i t4 = gm_f_find_csk9ups(t2, t3);
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_espvf96 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 193u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t4 = gm_f_find_ovslcai(t2, t3);
    gm_l3 = t4;
    uint8_t t5 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
    if (t5) {
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
        gm_l2 = t6;
    } else {
        gm_tf5srh4mp4b3i t7;
        memset(&t7, 0, sizeof t7);
        F.ret = t7;
        goto gm_fret;
    }
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, ((uint8_t *)t8));
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t8) + 8));
    uint64_t t11 = gm_l2;
    if ((uintptr_t)t11 >= (uintptr_t)(t10)) gm_trap_index((uintptr_t)t11, t10, gm_srcbsc23is03ttm, 139, 12);
    uint64_t t12 = GM_LD(uint64_t, ((((uint8_t *)t9) + (uintptr_t)t11 * 32) + 24));
    gm_tf5srh4mp4b3i t13;
    GM_ST(uint8_t, (uint8_t *)&t13 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t13 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t13 + 12, 0);
    GM_ST(uint64_t, ((uint8_t *)&t13), t12);
    ((uint8_t *)&t13)[8] = 1;
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_m1knqqk(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_m1knqqk F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 157u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t4 = gm_f_find_ve3qs5b(t2, t3);
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
    uint32_t t12 = GM_LD(uint32_t, ((((uint8_t *)t9) + (uintptr_t)t11 * 32) + 24));
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_pftnkei(void * a0, gm_tl874t2atnlh3 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_pftnkei F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t4 = gm_f_find_7io0v3c(t2, t3);
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
inline __attribute__((aligned(4))) void * gm_f_handler_decl_m66mgr8(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_handler_decl_m66mgr8 F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* d */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 544));
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t3) + 544) + 8));
    uint32_t t6 = gm_l1;
    uint64_t t7 = (uint64_t)t6;
    if ((uintptr_t)t7 >= (uintptr_t)(t5)) gm_trap_index((uintptr_t)t7, t5, gm_srcuj9anpi7p1mq, 132, 83);
    void * t8 = (void *)(((uint8_t *)t4) + (uintptr_t)t7 * 64);
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_handler_lit_b02boff(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_handler_lit_b02boff F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* h */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 504));
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t3) + 504) + 8));
    uint32_t t6 = gm_l1;
    uint64_t t7 = (uint64_t)t6;
    if ((uintptr_t)t7 >= (uintptr_t)(t5)) gm_trap_index((uintptr_t)t7, t5, gm_srcuj9anpi7p1mq, 129, 76);
    void * t8 = (void *)(((uint8_t *)t4) + (uintptr_t)t7 * 40);
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_map_1c18lnh(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_1c18lnh F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_13d6rhn();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_1s9eugt(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_1s9eugt F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_08mdrht();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_70470jk(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_70470jk F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_n44tb4r();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_76d05hh(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_76d05hh F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_ekr3omu();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_em2q005(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_em2q005 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_nf78hfm();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_fupfd5g(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_fupfd5g F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_360e8h0();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_g5h3sdu(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_g5h3sdu F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_h06rie2();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_lljdp3h(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_lljdp3h F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_7hq6cu2();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_m04clhi(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_m04clhi F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_jckt05t();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_rs5nlfs(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_rs5nlfs F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_jagk2mc();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_tl7apid(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_tl7apid F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_qsq36pn();
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_eo6jkc5(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_integer_eo6jkc5 F;
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
    uint8_t t6 = gm_f_is_integer_8d4f9ek(t4, GM_CONV(gm_csguankko4lmhd, t5));
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_never_in5ftma(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_never_in5ftma F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 156u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    gm_t53at80pfdus3 t6;
    gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
    uint8_t t8 = (uint8_t)((t7 == ((uint8_t)5ULL)));
    gm_t53at80pfdus3 t9;
    memset(&t9, 0, sizeof t9);
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)5ULL));
    F.ret = t8;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_void_vs2outo(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_void_vs2outo F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 42u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint8_t gm_l2;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint8_t t4 = gm_f_is_unit_17uennd(t2, GM_CONV(gm_csguankko4lmhd, t3));
    if (t4) {
        gm_l2 = ((uint8_t)1);
    } else {
        void * t5 = gm_l0;
        gm_t88rcucg1b7uk t6 = gm_l1;
        uint8_t t7 = gm_f_is_never_in5ftma(t5, GM_CONV(gm_csguankko4lmhd, t6));
        gm_l2 = t7;
    }
    uint8_t t8 = gm_l2;
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_ghvav22 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
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
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_layout_of_e6hfj1r F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 64));
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_layout_a5ovnrm((void *)&(*(gm_tbccvikg6b96e *)gm_out), (void *)NULL, cx, NULL, t3, t4);
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
inline __attribute__((aligned(4))) void gm_f_list_0c2v515(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0c2v515 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_ppl3r45();
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
inline __attribute__((aligned(4))) void gm_f_list_0vc6l5i(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0vc6l5i F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_e8istsh();
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
inline __attribute__((aligned(4))) void gm_f_list_1t0a5et(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_1t0a5et F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_al38tsc();
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
inline __attribute__((aligned(4))) void gm_f_list_938p55h(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_938p55h F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_jjmeosp();
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
inline __attribute__((aligned(4))) void gm_f_list_a5k8eig(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_a5k8eig F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_2ntv1ru();
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
inline __attribute__((aligned(4))) void gm_f_list_av8bg03(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_av8bg03 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_nqh6398();
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
inline __attribute__((aligned(4))) void gm_f_list_hme58st(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_hme58st F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_ednoq9d();
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
inline __attribute__((aligned(4))) void gm_f_list_k5efes8(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_k5efes8 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_kge4ibg();
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
inline __attribute__((aligned(4))) void gm_f_list_lj5vg1g(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_lj5vg1g F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_465e48m();
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
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_nof7vfj F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_rcd2jln();
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
inline __attribute__((aligned(4))) void gm_f_list_rc6kc0u(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_rc6kc0u F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_asvj6q6();
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_member_names_17el54k(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_member_names_17el54k F;
    void * gm_l0; /* ty */
    void * gm_l2; /* <tmp> */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_trqfvmjtt4sij t3;
    gm_f_list_epvducg((void *)&t3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t3;
    void * t4 = gm_l0;
    void * t5 = (void *)(((uint8_t *)t4) + 816);
    gm_l2 = t5;
    {
        gm_node wn6 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlak2jvvbqg4, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        void * t9 = gm_l2;
        gm_b0_1 t10 = gm_f_keys_qc5bjlr_s31qjug((void *)NULL, (&wn6), NULL, t9);
        if (gm_unw.kind) {
            goto gm_ll8;
        }
    }
    goto gm_ls11;
gm_ll8:;
    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we7; }
    goto gm_lad;
gm_ls11:;
gm_lb1:;
we7:;
    gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    gm_f_sort_skeree7(t12);
    gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_mirror_text_cr5muc9(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mirror_text_cr5muc9 F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 712));
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_new_namer_5q88o9g(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_new_namer_5q88o9g F;
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t4c7ipjj9s3fp t2;
    gm_f_hash_map_m04clhi((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&(*(gm_ttiitul0263ld *)gm_out) + 0), t2);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_offsets_eh6vfle F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tbccvikg6b96e gm_l2;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_layout_of_e6hfj1r((void *)&gm_l2, (void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 16));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t736djc66po0l gm_f_pop_168lbb8(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_168lbb8 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_tcc1rq4n1avb2 gm_l2; /* x */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    gm_l1 = t3;
    uint64_t t4 = gm_l1;
    uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
    if (t5) {
        gm_t736djc66po0l t6;
        memset(&t6, 0, sizeof t6);
        F.ret = t6;
        goto gm_fret;
    }
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
    uint64_t t10 = gm_l1;
    uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)1ULL), &r11);
    if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
    uint64_t t13 = r11;
    if ((uintptr_t)t13 >= (uintptr_t)(t9)) gm_trap_index((uintptr_t)t13, t9, gm_srcrgc9bkquf6nr, 133, 10);
    gm_tcc1rq4n1avb2 t14 = GM_LD(gm_tcc1rq4n1avb2, (((uint8_t *)t8) + (uintptr_t)t13 * 8));
    gm_l2 = t14;
    void * t15 = gm_l0;
    void * t16 = gm_l0;
    void * t17 = GM_LD(void *, ((uint8_t *)t16));
    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)t16) + 8));
    uint64_t t19 = gm_l1;
    uint64_t r20; bool o21 = __builtin_sub_overflow(t19, ((uint64_t)1ULL), &r20);
    if (o21) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
    uint64_t t22 = r20;
    uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t24 = (uintptr_t)t22;
    if (t23 > t24 || t24 > (uintptr_t)t18) gm_trap_slice(t23, t24, t18, gm_srcrgc9bkquf6nr, 134, 15);
    uint64_t t25 = ((uintptr_t)t22) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t17 + (uintptr_t)((uint64_t)0ULL) * 8)));
    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25));
    GM_ST(gm_b16_8, ((uint8_t *)t15), t26);
    gm_tcc1rq4n1avb2 t27 = gm_l2;
    gm_t736djc66po0l t28;
    GM_ST(uint8_t, (uint8_t *)&t28 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t28 + 10, 0);
    GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t28), t27);
    ((uint8_t *)&t28)[8] = 1;
    F.ret = t28;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_570g79n F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    uint32_t gm_l2; /* x */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    gm_l1 = t3;
    uint64_t t4 = gm_l1;
    uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
    if (t5) {
        gm_t9805qtonbsag t6;
        memset(&t6, 0, sizeof t6);
        F.ret = t6;
        goto gm_fret;
    }
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
    uint64_t t10 = gm_l1;
    uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)1ULL), &r11);
    if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
    uint64_t t13 = r11;
    if ((uintptr_t)t13 >= (uintptr_t)(t9)) gm_trap_index((uintptr_t)t13, t9, gm_srcrgc9bkquf6nr, 133, 10);
    uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)t8) + (uintptr_t)t13 * 4));
    gm_l2 = t14;
    void * t15 = gm_l0;
    void * t16 = gm_l0;
    void * t17 = GM_LD(void *, ((uint8_t *)t16));
    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)t16) + 8));
    uint64_t t19 = gm_l1;
    uint64_t r20; bool o21 = __builtin_sub_overflow(t19, ((uint64_t)1ULL), &r20);
    if (o21) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
    uint64_t t22 = r20;
    uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t24 = (uintptr_t)t22;
    if (t23 > t24 || t24 > (uintptr_t)t18) gm_trap_slice(t23, t24, t18, gm_srcrgc9bkquf6nr, 134, 15);
    uint64_t t25 = ((uintptr_t)t22) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t17 + (uintptr_t)((uint64_t)0ULL) * 4)));
    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25));
    GM_ST(gm_b16_8, ((uint8_t *)t15), t26);
    uint32_t t27 = gm_l2;
    gm_t9805qtonbsag t28;
    GM_ST(uint8_t, (uint8_t *)&t28 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t28 + 6, 0);
    GM_ST(uint32_t, ((uint8_t *)&t28), t27);
    ((uint8_t *)&t28)[4] = 1;
    F.ret = t28;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_97b8r3k(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_97b8r3k F;
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
        gm_b16_8 t9 = gm_f_empty_slice_465e48m();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_vefhvf2(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 40), ((uint8_t *)&F.l1), 40);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 40)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_cn89s92(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_cn89s92 F;
    void * gm_l0; /* l */
    gm_tc42mm7h7sccr gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_ednoq9d();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_m5jo08n(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_jttnn67(void * a0, gm_tf9f5m02f56gm a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_jttnn67 F;
    void * gm_l0; /* l */
    gm_tf9f5m02f56gm gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_2ntv1ru();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_h5ak80l(t8, ((uint64_t)1ULL), t10);
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
    gm_tf9f5m02f56gm t18 = gm_l1;
    GM_ST(gm_tf9f5m02f56gm, (((uint8_t *)t16) + (uintptr_t)t17 * 16), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_k3l9nic F;
    void * gm_l0; /* l */
    gm_t88rcucg1b7uk gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
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
        gm_b16_8 t9 = gm_f_empty_slice_rcd2jln();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_j4iu2lk(t8, ((uint64_t)1ULL), t10);
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
    gm_t88rcucg1b7uk t18 = gm_l1;
    GM_ST(gm_t88rcucg1b7uk, (((uint8_t *)t16) + (uintptr_t)t17 * 4), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_lm9fgpm(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_lm9fgpm F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_nqh6398();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_97at0vn(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 32), ((uint8_t *)a1), 32);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_sgikgd8(void * a0, gm_tcc1rq4n1avb2 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_sgikgd8 F;
    void * gm_l0; /* l */
    gm_tcc1rq4n1avb2 gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_kge4ibg();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_p6p5ugs(t8, ((uint64_t)1ULL), t10);
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
    gm_tcc1rq4n1avb2 t18 = gm_l1;
    GM_ST(gm_tcc1rq4n1avb2, (((uint8_t *)t16) + (uintptr_t)t17 * 8), t18);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_97at0vn(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_97at0vn F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h5ak80l(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_h5ak80l F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)16ULL), ((uint64_t)4ULL));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_j4iu2lk F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_m5jo08n(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_m5jo08n F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_p6p5ugs(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_p6p5ugs F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_vefhvf2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_vefhvf2 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)40ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_5qbi623 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    gm_b16_8 t6 = gm_f_show_kmjeh8s((void *)NULL, cx, NULL, t4, t5);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_ref_aqt1t7c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_show_ref_aqt1t7c F;
    void * gm_l0; /* t */
    void * gm_l1; /* e */
    gm_tmm67r2c94d5j gm_l3; /* p */
    GM_STACK_CHECK(&F, 200u);
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
    void * t7 = gm_l1;
    gm_b0_1 t8 = gm_f_effect_ref_6sjtehp((void *)NULL, cx, NULL, t6, t7);
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
inline __attribute__((aligned(4))) void gm_f_sort_skeree7(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_skeree7 F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_closure t3 = ((gm_closure){GM_PTR_TAG(gm_f_fn_6gkpg0v), NULL, cx});
    gm_f_introsort_r6p6ckl(t2, t3);
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
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint32_t gm_f_spec_depth_em7r6vt(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_spec_depth_em7r6vt F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* s */
    gm_tf9f5m02f56gm gm_l2; /* key */
    uint32_t gm_l3; /* o */
    GM_STACK_CHECK(&F, 93u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t4 = gm_l0;
    uint32_t t5 = gm_l1;
    gm_tf9f5m02f56gm t6 = gm_f_spec_key_eljlge7(t4, t5);
    gm_l2 = t6;
    {
        {
            uint8_t t7 = (uint8_t)(((((uint8_t *)&gm_l2) + 8))[4] != 0);
            if (!t7) {
                goto gm_lb2;
            }
            uint32_t t8 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 8));
            gm_l3 = t8;
            void * t9 = gm_l0;
            uint32_t t10 = gm_l3;
            uint32_t t11 = gm_f_spec_depth_em7r6vt(t9, t10);
            uint32_t r12; bool o13 = __builtin_add_overflow(((uint32_t)1ULL), t11, &r12);
            if (o13) gm_trap("integer overflow in `+`", gm_srcd5arorrf8jkg, 363, 32);
            uint32_t t14 = r12;
            F.ret = t14;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    F.ret = ((uint32_t)1ULL);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tf9f5m02f56gm gm_f_spec_key_eljlge7(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_spec_key_eljlge7 F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 2592));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 2592) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_srcd5arorrf8jkg, 358, 53);
    gm_tf9f5m02f56gm t7 = GM_LD(gm_tf9f5m02f56gm, (((uint8_t *)t3) + (uintptr_t)t6 * 16));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_spec_slot_jild38d(gm_t9805qtonbsag a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_spec_slot_jild38d F;
    gm_t9805qtonbsag gm_l0; /* outer */
    uint32_t gm_l1; /* o */
    GM_STACK_CHECK_SHALLOW(&F, 33u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            uint8_t t4 = (uint8_t)((((uint8_t *)&gm_l0))[4] != 0);
            if (!t4) {
                goto gm_lb2;
            }
            uint32_t t5 = GM_LD(uint32_t, ((uint8_t *)&gm_l0));
            gm_l1 = t5;
            uint32_t t6 = gm_l1;
            uint32_t r7; bool o8 = __builtin_add_overflow(t6, ((uint32_t)1ULL), &r7);
            if (o8) gm_trap("integer overflow in `+`", gm_srcd5arorrf8jkg, 369, 28);
            uint32_t t9 = r7;
            F.ret = t9;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    F.ret = ((uint32_t)0ULL);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_src_name_0r539f2(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_src_name_0r539f2 F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* file */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_slku641rnhls8));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    void * t3 = gm_l0;
    void * t4 = GM_LD(void *, ((uint8_t *)t3));
    void * t5 = GM_LD(void *, ((uint8_t *)t4));
    void * t6 = GM_LD(void *, ((uint8_t *)t5));
    void * t7 = GM_LD(void *, ((uint8_t *)t6));
    uint32_t t8 = gm_l1;
    void * t9 = gm_f_file_o15fqku(t7, t8);
    gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)t9));
    gm_b16_8 t11 = gm_f_content_name_83mn5uf((void *)NULL, cx, NULL, t2, t10);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_view_defs_u461okc(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_view_defs_u461okc F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 872) + 104));
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_08b77ko(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_08b77ko F;
    void * gm_l0; /* l */
    gm_t3877pk7lv748 gm_l1; /* args */
    GM_STACK_CHECK(&F, 62u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1l1fmn0hvo9d));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(67));
    gm_tftb1odc739t5 t4 = gm_f_write_oepvt6a(t2, &(*(gm_t3877pk7lv748 *)((uint8_t *)a1)), ((void *)&gm_tplemb9urr8jmq6), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_13301jc(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_13301jc F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3dov523r8ujn));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(52));
    gm_tftb1odc739t5 t4 = gm_f_write_2venbiq(t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tplo12k9ajl5nes), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2uh64vr(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_2uh64vr F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sppi971hm2k0k));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(14));
    gm_tftb1odc739t5 t4 = gm_f_write_av01663(t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tplebqthjijvqt0), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_3dkm7v9(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_3dkm7v9 F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_stqjev4m440fr));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(45));
    gm_tftb1odc739t5 t4 = gm_f_write_av01663(t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tplh2sse9mocm92), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_4uefpre(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_4uefpre F;
    void * gm_l0; /* l */
    gm_t3877pk7lv748 gm_l1; /* args */
    GM_STACK_CHECK(&F, 62u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2oqp92hnbubt));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
    gm_tftb1odc739t5 t4 = gm_f_write_04pf35h(t2, &(*(gm_t3877pk7lv748 *)((uint8_t *)a1)), ((void *)&gm_tpltgqv8dvi81j3), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_6rjcu4v(void * a0, gm_t4ce0jd3tdro4 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_6rjcu4v F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s70ks96e05b2g));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(38));
    gm_tftb1odc739t5 t5 = gm_f_write_5qef5td(t2, t3, ((void *)&gm_tpl77gpruiccsl2), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8o29aab(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_8o29aab F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0fgdcd1ggor2));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(17));
    gm_tftb1odc739t5 t4 = gm_f_write_2venbiq(t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tplr56tn8lpufgs), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_9falfvm(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_9falfvm F;
    void * gm_l0; /* l */
    gm_t3877pk7lv748 gm_l1; /* args */
    GM_STACK_CHECK(&F, 62u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s23s60tf3fq5h));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(33));
    gm_tftb1odc739t5 t4 = gm_f_write_glpclq4(t2, &(*(gm_t3877pk7lv748 *)((uint8_t *)a1)), ((void *)&gm_tpl44b660pgg1c8), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_b3knhd6(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_b3knhd6 F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_savg92sdg6694));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(34));
    gm_tftb1odc739t5 t4 = gm_f_write_av01663(t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tplfsmasr3tgalo), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_c5hlb1u(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_c5hlb1u F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sk0aaujkmprtl));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(34));
    gm_tftb1odc739t5 t4 = gm_f_write_s3v41br(t2, &(*(gm_tfeab279c16rr *)((uint8_t *)a1)), ((void *)&gm_tplevafblg3gmq9), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_d85j2rp(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_d85j2rp F;
    void * gm_l0; /* l */
    gm_tf1bjg81u0glv gm_l1; /* args */
    GM_STACK_CHECK(&F, 62u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s64fvfprf3827));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(62));
    gm_tftb1odc739t5 t4 = gm_f_write_bigf9mn(t2, &(*(gm_tf1bjg81u0glv *)((uint8_t *)a1)), ((void *)&gm_tpl10tenob4hk5k), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_gb1ne57(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_gb1ne57 F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sa10lthaugqih));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(44));
    gm_tftb1odc739t5 t4 = gm_f_write_av01663(t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tplb4rog3tmrj9n), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_gi12t4o(void * a0, gm_t19enj90vg8bd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_gi12t4o F;
    void * gm_l0; /* l */
    gm_t19enj90vg8bd gm_l1; /* args */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t19enj90vg8bd t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sarl8beoguiit));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(19));
    gm_tftb1odc739t5 t5 = gm_f_write_k2pnevr(t2, t3, ((void *)&gm_tpllm484u6fuiq3), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_gk1hd5k(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_gk1hd5k F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sn275ucdkgsk6));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(81));
    gm_tftb1odc739t5 t4 = gm_f_write_vm7g5p8(t2, &(*(gm_ti71ll5knnvij *)((uint8_t *)a1)), ((void *)&gm_tpl87um69vt0amn), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_hclpi1b(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_hclpi1b F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sknjgunq9356n));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(29));
    gm_tftb1odc739t5 t4 = gm_f_write_2venbiq(t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tplqgt00aqumuje), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_j5p5p7n(void * a0, gm_t88rcucg1b7uk a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_j5p5p7n F;
    void * gm_l0; /* l */
    gm_t88rcucg1b7uk gm_l1; /* args */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s6vghnf0vdf2t));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(50));
    gm_tftb1odc739t5 t5 = gm_f_write_e55tuqo(t2, t3, ((void *)&gm_tpl0c7sat2t7j6v), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jqf697l(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_jqf697l F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sog1crmvcid87));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(58));
    gm_tftb1odc739t5 t5 = gm_f_write_vq1ifl4(t2, t3, ((void *)&gm_tplj92mk62u70db), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_kdsk4q9(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_kdsk4q9 F;
    void * gm_l0; /* l */
    gm_t3877pk7lv748 gm_l1; /* args */
    GM_STACK_CHECK(&F, 62u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s852r4rtnj5dj));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(53));
    gm_tftb1odc739t5 t4 = gm_f_write_oepvt6a(t2, &(*(gm_t3877pk7lv748 *)((uint8_t *)a1)), ((void *)&gm_tpl59s4m8f706c9), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_lu60214(void * a0, gm_t2lqu8jsj511d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_lu60214 F;
    void * gm_l0; /* l */
    gm_t2lqu8jsj511d gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t2lqu8jsj511d t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s65aicqb626jk));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(53));
    gm_tftb1odc739t5 t5 = gm_f_write_buk3227(t2, t3, ((void *)&gm_tplaos6tmpdtrie), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_n0bkcka(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_n0bkcka F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_suk4smocgp7ip));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(53));
    gm_tftb1odc739t5 t4 = gm_f_write_0ie0tub(t2, &(*(gm_tcrdb1inj22os *)((uint8_t *)a1)), ((void *)&gm_tplcoet0p57pg4s), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_oj4s9u6(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_oj4s9u6 F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sp9ailodohkkj));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(39));
    gm_tftb1odc739t5 t4 = gm_f_write_av01663(t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tplrcqo0t095bsv), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_okup0j2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_okup0j2 F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sh5lbh5j6oqpf));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(35));
    gm_tftb1odc739t5 t4 = gm_f_write_gdgk8he(t2, &(*(gm_tud7oovfvnf43 *)((uint8_t *)a1)), ((void *)&gm_tpl6pohii1e8pbg), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_pfrpr5k(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_pfrpr5k F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_savg92sdg6694));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(34));
    gm_tftb1odc739t5 t4 = gm_f_write_qb1f4ht(t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tplfsmasr3tgalo), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_qph5tm9(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_qph5tm9 F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sa9ktp2t0q9rs));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(33));
    gm_tftb1odc739t5 t4 = gm_f_write_8p1ions(t2, &(*(gm_tif3elqk01bdl *)((uint8_t *)a1)), ((void *)&gm_tples1m1m9iv6b0), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_qqu2rl9(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_qqu2rl9 F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1jaan454qth6));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(18));
    gm_tftb1odc739t5 t4 = gm_f_write_5um95qj(t2, &(*(gm_tfeab279c16rr *)((uint8_t *)a1)), ((void *)&gm_tpl3kt8q9c3ih5v), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_sf9mjak(void * a0, gm_t4ce0jd3tdro4 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_sf9mjak F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sd96cluvcn3lj));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(32));
    gm_tftb1odc739t5 t5 = gm_f_write_hqo1u7h(t2, t3, ((void *)&gm_tplvvtt1jk959m0), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ticpdcl(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_ticpdcl F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sucip17pohakb));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(21));
    gm_tftb1odc739t5 t4 = gm_f_write_gdgk8he(t2, &(*(gm_tud7oovfvnf43 *)((uint8_t *)a1)), ((void *)&gm_tpli0v9k7mac3tj), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u3br5ci(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_u3br5ci F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slu6nnvsgea2t));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(40));
    gm_tftb1odc739t5 t4 = gm_f_write_0mj6rce(t2, &(*(gm_tud7oovfvnf43 *)((uint8_t *)a1)), ((void *)&gm_tplk4i636uhvi9m), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u8u1amh(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_u8u1amh F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjgqjs06244vq));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(20));
    gm_tftb1odc739t5 t4 = gm_f_write_s3v41br(t2, &(*(gm_tfeab279c16rr *)((uint8_t *)a1)), ((void *)&gm_tplncn0njcasr6v), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ug67q96(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_ug67q96 F;
    void * gm_l0; /* l */
    gm_tnen6863q2sjs gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sml9migrmgip7));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(102));
    gm_tftb1odc739t5 t4 = gm_f_write_qgchepk(t2, &(*(gm_tnen6863q2sjs *)((uint8_t *)a1)), ((void *)&gm_tpl5n5ddjmjlhjb), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_usci3o4(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_usci3o4 F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sg2r3pcbg3kr2));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(31));
    gm_tftb1odc739t5 t4 = gm_f_write_2venbiq(t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tpll6jntfkrhtt4), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_uuia5cm(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_uuia5cm F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s6unleea2pm63));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(34));
    gm_tftb1odc739t5 t4 = gm_f_write_70cb4d8(t2, &(*(gm_tq3m5qgvkpje8 *)((uint8_t *)a1)), ((void *)&gm_tplhu7i2tvvfh15), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t na = GM_LD(uintptr_t, a + 8), nb = GM_LD(uintptr_t, b + 8);
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    uintptr_t m = na < nb ? na : nb;
    int c = m == 0 ? 0 : memcmp(pa, pb, m);
    return c != 0 ? c < 0 : na < nb;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_addr_name_g5lrlt3(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_addr_name_g5lrlt3 F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK(&F, 186u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l2;
    void * t2 = gm_l0;
    uint32_t t3 = gm_l1;
    uint8_t t4 = gm_f_needs_wrapper_ktlbb7i((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (t4) {
        void * t5 = gm_l0;
        uint32_t t6 = gm_l1;
        gm_b16_8 t7 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t8;
        GM_ST(gm_b16_8, ((uint8_t *)&t8 + 0), t7);
        gm_f_format_bi036vc((void *)&gm_l2, (void *)NULL, cx, NULL, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
        if (!t9) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 8, gm_srcd5arorrf8jkg, 926, 36);
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
        gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&t10));
        F.ret = t11;
        goto gm_fret;
    }
    void * t12 = gm_l0;
    uint32_t t13 = gm_l1;
    gm_b16_8 t14 = gm_f_callee_name_4k9m9s6((void *)NULL, cx, NULL, t12, t13);
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
inline __attribute__((aligned(4))) void gm_f_c_abi_type_nu2c2tm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_c_abi_type_nu2c2tm F;
    void * gm_l0; /* g */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_tl874t2atnlh3 gm_l2; /* span */
    gm_b16_8 gm_l3; /* m */
    GM_STACK_CHECK(&F, 374u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t5v8n6qrn4rh4 gm_l4;
    gm_th2lqbo77pvsc gm_l5;
    gm_tabb710d9mq2e gm_l6;
    void * t12 = gm_l0;
    void * t13 = (void *)(((uint8_t *)t12) + 8);
    gm_t88rcucg1b7uk t14 = gm_l1;
    gm_f_repr_53quqki((void *)&gm_l4, (void *)NULL, cx, NULL, t13, t14);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            uint8_t t16 = (uint8_t)(t15 == ((uint8_t)2ULL));
            if (!t16) {
                goto gm_lb2;
            }
            gm_b16_8 t17;
            GM_ST(void *, (uint8_t *)&t17, (void *)(gm_snokcvhtidt97));
            GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(5));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t17);
            goto gm_fdone;
        }
gm_lb2:;
        {
            {
                {
                    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                    uint8_t t19 = (uint8_t)(t18 == ((uint8_t)0ULL));
                    if (!t19) {
                        goto gm_lb5;
                    }
                    goto gm_lb4;
                }
gm_lb5:;
                {
                    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                    uint8_t t21 = (uint8_t)(t20 == ((uint8_t)1ULL));
                    if (!t21) {
                        goto gm_lb6;
                    }
                    goto gm_lb4;
                }
gm_lb6:;
                {
                    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                    uint8_t t23 = (uint8_t)(t22 == ((uint8_t)3ULL));
                    if (!t23) {
                        goto gm_lb7;
                    }
                    goto gm_lb4;
                }
gm_lb7:;
                goto gm_lb3;
            }
gm_lb4:;
            void * t24 = gm_l0;
            void * t25 = (void *)(((uint8_t *)t24) + 8);
            gm_t88rcucg1b7uk t26 = gm_l1;
            gm_b16_8 t27 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t25, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t27);
            goto gm_fdone;
        }
gm_lb3:;
        {
            {
                {
                    {
                        void * t28 = gm_l0;
                        void * t29 = (void *)(((uint8_t *)t28) + 8);
                        gm_t88rcucg1b7uk t30 = gm_l1;
                        gm_f_mirror_lcjegsf((void *)&gm_l5, (void *)NULL, cx, NULL, t29, t30);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t31 = (uint8_t)((((uint8_t *)&gm_l5))[16] != 0);
                    if (!t31) {
                        goto gm_lb10;
                    }
                    gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
                    gm_l3 = t32;
                    {
                        gm_b16_8 t33 = gm_l3;
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t33);
                        goto gm_fdone;
                    }
                }
gm_lb10:;
            }
gm_lb9:;
            {
                gm_tl874t2atnlh3 t34 = gm_l2;
                void * t35 = gm_l0;
                void * t36 = (void *)(((uint8_t *)t35) + 8);
                gm_t88rcucg1b7uk t37 = gm_l1;
                gm_b16_8 t38 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t36, t37);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t39;
                GM_ST(gm_b16_8, ((uint8_t *)&t39 + 0), t38);
                gm_f_format_b6qg4rq((void *)&gm_l6, (void *)NULL, cx, NULL, t39);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t40 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                if (!t40) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_srcd5arorrf8jkg, 957, 39);
                gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
                gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
                gm_t08olq97rscn3 t43;
                gm_f_unsupported_531q51r((void *)&t43, (void *)NULL, cx, NULL, t34, t42);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t43);
                goto gm_fdone;
            }
        }
gm_lb8:;
        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 952, 5);
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_callee_name_4k9m9s6(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_clause_may_exit_7lc6flj(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t9805qtonbsag a2) {
    struct gm_fr_clause_may_exit_7lc6flj F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* c */
    gm_t9805qtonbsag gm_l2; /* outer */
    gm_tcc1rq4n1avb2 gm_l3; /* key */
    uint8_t gm_l4; /* m */
    uint8_t gm_l5; /* r */
    void * gm_l6; /* b */
    GM_STACK_CHECK(&F, 220u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tqs5p4n2tq19v gm_l7;
    gm_tftb1odc739t5 gm_l8;
    uint8_t gm_l9;
    gm_tftb1odc739t5 gm_l10;
    uint32_t t6 = gm_l1;
    gm_t9805qtonbsag t7 = gm_l2;
    uint32_t t8 = gm_f_spec_slot_jild38d(t7);
    gm_tcc1rq4n1avb2 t9;
    GM_ST(uint32_t, ((uint8_t *)&t9 + 0), t6);
    GM_ST(uint32_t, ((uint8_t *)&t9 + 4), t8);
    gm_l3 = t9;
    {
        {
            void * t10 = gm_l0;
            void * t11 = (void *)(((uint8_t *)t10) + 2840);
            gm_tcc1rq4n1avb2 t12 = gm_l3;
            gm_tqs5p4n2tq19v t13 = gm_f_get_ehtj21s(t11, t12);
            gm_l7 = t13;
            uint8_t t14 = (uint8_t)((((uint8_t *)&gm_l7))[1] != 0);
            if (!t14) {
                goto gm_lb2;
            }
            uint8_t t15 = GM_LD(uint8_t, ((uint8_t *)&gm_l7));
            gm_l4 = t15;
            uint8_t t16 = gm_l4;
            uint8_t t17 = (!(t16 == ((uint8_t)2ULL)));
            F.ret = t17;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t18 = gm_l0;
    void * t19 = (void *)(((uint8_t *)t18) + 2840);
    gm_tcc1rq4n1avb2 t20 = gm_l3;
    gm_tftb1odc739t5 t21 = gm_f_put_cmvik6u(t19, t20, ((uint8_t)1ULL));
    gm_l8 = t21;
    uint8_t t22 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (!t22) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srcd5arorrf8jkg, 819, 5);
    gm_l5 = ((uint8_t)1);
    {
        {
            void * t23 = gm_l0;
            void * t24 = GM_LD(void *, ((uint8_t *)t23));
            uint32_t t25 = gm_l1;
            void * t26 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t27 = (uint8_t)(GM_LD(void *, (((uint8_t *)t26) + 152)) != 0);
            if (!t27) {
                goto gm_lb4;
            }
            void * t28 = GM_LD(void *, (((uint8_t *)t26) + 152));
            gm_l6 = t28;
            void * t29 = gm_l0;
            void * t30 = gm_l6;
            gm_t9805qtonbsag t31 = gm_l2;
            uint8_t t32 = gm_f_expr_may_exit_qfnn2ei((void *)NULL, cx, NULL, t29, t30, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t32;
            goto gm_lb3;
        }
gm_lb4:;
    }
gm_lb3:;
    void * t33 = gm_l0;
    void * t34 = (void *)(((uint8_t *)t33) + 2840);
    gm_tcc1rq4n1avb2 t35 = gm_l3;
    uint8_t t36 = gm_l5;
    if (t36) {
        gm_l9 = ((uint8_t)3ULL);
    } else {
        gm_l9 = ((uint8_t)2ULL);
    }
    uint8_t t37 = gm_l9;
    gm_tftb1odc739t5 t38 = gm_f_put_cmvik6u(t34, t35, t37);
    gm_l10 = t38;
    uint8_t t39 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t39) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srcd5arorrf8jkg, 822, 5);
    uint8_t t40 = gm_l5;
    F.ret = t40;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_clause_name_uk8dps6(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t9805qtonbsag a2) {
    struct gm_fr_clause_name_uk8dps6 F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* c */
    gm_t9805qtonbsag gm_l2; /* outer */
    uint32_t gm_l3; /* o */
    GM_STACK_CHECK(&F, 133u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            uint8_t t4 = (uint8_t)((((uint8_t *)&gm_l2))[4] != 0);
            if (!t4) {
                goto gm_lb2;
            }
            uint32_t t5 = GM_LD(uint32_t, ((uint8_t *)&gm_l2));
            gm_l3 = t5;
            void * t6 = gm_l0;
            uint32_t t7 = gm_l1;
            uint32_t t8 = gm_l3;
            gm_b16_8 t9 = gm_f_queue_spec_khngsmv((void *)NULL, cx, NULL, t6, t7, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t9;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t10 = gm_l0;
    uint32_t t11 = gm_l1;
    gm_b0_1 t12 = gm_f_queue_cd51r8c((void *)NULL, cx, NULL, t10, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t13 = gm_l0;
    uint32_t t14 = gm_l1;
    gm_b16_8 t15 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t13, t14);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t15;
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
inline __attribute__((aligned(4))) uint8_t gm_f_code_has_n3tn5b6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_code_has_n3tn5b6 F;
    gm_b16_8 gm_l0; /* needle */
    GM_STACK_CHECK_SHALLOW(&F, 214u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_generate_entries_br1bd7f *)((&F.h)->up))->l74));
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&((struct gm_fr_generate_entries_br1bd7f *)((&F.h)->up))->l74) + 8));
    uint64_t t4 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_generate_entries_br1bd7f *)((&F.h)->up))->l75));
    uint64_t t5 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t6 = (uintptr_t)t4;
    if (t5 > t6 || t6 > (uintptr_t)t3) gm_trap_slice(t5, t6, t3, gm_srcd5arorrf8jkg, 284, 60);
    uint64_t t7 = ((uintptr_t)t4) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t8;
    GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t2 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7));
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
    gm_b16_8 t10 = gm_l0;
    uint8_t t11 = gm_f_contains_gkki4kq(t9, t10);
    if (t11) {
        gm_l1 = ((uint8_t)1);
    } else {
        void * t12 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_generate_entries_br1bd7f *)((&F.h)->up))->l74));
        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&((struct gm_fr_generate_entries_br1bd7f *)((&F.h)->up))->l74) + 8));
        uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_generate_entries_br1bd7f *)((&F.h)->up))->l76));
        uint64_t t15 = (uintptr_t)t14;
        uint64_t t16 = (uintptr_t)t13;
        if (t15 > t16 || t16 > (uintptr_t)t13) gm_trap_slice(t15, t16, t13, gm_srcd5arorrf8jkg, 284, 107);
        uint64_t t17 = ((uintptr_t)t13) - (uintptr_t)t14;
        gm_b16_8 t18;
        GM_ST(void *, (uint8_t *)&t18, (void *)(((uint8_t *)t12 + (uintptr_t)t14 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(t17));
        gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
        gm_b16_8 t20 = gm_l0;
        uint8_t t21 = gm_f_contains_gkki4kq(t19, t20);
        gm_l1 = t21;
    }
    uint8_t t22 = gm_l1;
    F.ret = t22;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b16_8 gm_f_conv_attr_m0b89pc(uint8_t a0);
__attribute__((aligned(4))) void gm_f_decl_table_l7u1k0g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_decl_table_l7u1k0g F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* d */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* k */
    uint32_t gm_l6; /* c */
    void * gm_l7; /* def */
    gm_b16_8 gm_l8; /* ret */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint32_t gm_l12; /* p */
    gm_b16_8 gm_l13; /* self_ty */
    gm_b16_8 gm_l16; /* self_arg */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* i */
    gm_b16_8 gm_l22; /* name */
    gm_b16_8 gm_l23; /* ret_kw */
    GM_STACK_CHECK(&F, 2809u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l24;
    gm_tqs5p4n2tq19v gm_l25;
    gm_tftb1odc739t5 gm_l26;
    gm_tn68el8d14vku gm_l27;
    gm_tftb1odc739t5 gm_l28;
    gm_tftb1odc739t5 gm_l29;
    gm_tabb710d9mq2e gm_l30;
    gm_tftb1odc739t5 gm_l31;
    gm_tabb710d9mq2e gm_l32;
    gm_tftb1odc739t5 gm_l33;
    gm_tabb710d9mq2e gm_l34;
    gm_tftb1odc739t5 gm_l35;
    gm_b16_8 gm_l36;
    gm_b16_8 gm_l37;
    gm_tabb710d9mq2e gm_l38;
    gm_tftb1odc739t5 gm_l39;
    gm_tftb1odc739t5 gm_l40;
    gm_tabb710d9mq2e gm_l41;
    gm_tftb1odc739t5 gm_l42;
    gm_tabb710d9mq2e gm_l43;
    gm_tabb710d9mq2e gm_l44;
    gm_tftb1odc739t5 gm_l45;
    gm_tabb710d9mq2e gm_l46;
    gm_tftb1odc739t5 gm_l47;
    gm_tabb710d9mq2e gm_l48;
    gm_tftb1odc739t5 gm_l49;
    {
        void * t20 = gm_l0;
        void * t21 = (void *)(((uint8_t *)t20) + 3336);
        uint32_t t22 = gm_l1;
        gm_tqs5p4n2tq19v t23 = gm_f_get_dk5gh28(t21, t22);
        gm_l25 = t23;
        {
            {
                uint8_t t24 = (uint8_t)((((uint8_t *)&gm_l25))[1] != 0);
                if (t24) {
                    goto gm_lb2;
                }
                gm_l24 = ((uint8_t)1);
                goto gm_lb1;
            }
gm_lb2:;
            {
                gm_l24 = ((uint8_t)0);
                goto gm_lb1;
            }
gm_lb3:;
            gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 604, 8);
        }
gm_lb1:;
        uint8_t t25 = gm_l24;
        if (t25) {
            {
                void * t26 = gm_l0;
                void * t27 = (void *)(((uint8_t *)t26) + 3336);
                uint32_t t28 = gm_l1;
                gm_tftb1odc739t5 t29 = gm_f_put_v6g5pup(t27, t28, ((uint8_t)1));
                gm_l26 = t29;
                uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_srcd5arorrf8jkg, 605, 9);
                gm_trqfvmjtt4sij t31;
                gm_f_list_epvducg((void *)&t31, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l2 = t31;
                void * t32 = gm_l0;
                void * t33 = GM_LD(void *, ((uint8_t *)t32));
                uint32_t t34 = gm_l1;
                void * t35 = gm_f_handler_decl_m66mgr8(t33, t34);
                gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)t35) + 48));
                gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
                gm_l3 = t37;
                gm_l4 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        {
                            uint64_t t38 = gm_l4;
                            uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                            uint8_t t40 = (t38 < t39);
                            if (!t40) {
                                goto gm_lb4;
                            }
                            {
                                {
                                    uint64_t t41 = gm_l4;
                                    void * t42 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                    uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                    uint64_t t44 = gm_l4;
                                    uint32_t t45 = GM_LD(uint32_t, (((uint8_t *)t42) + (uintptr_t)t44 * 4));
                                    gm_tn68el8d14vku t46;
                                    GM_ST(uint32_t, (uint8_t *)&t46 + 12, 0);
                                    GM_ST(uint64_t, ((uint8_t *)&t46 + 0), t41);
                                    GM_ST(uint32_t, ((uint8_t *)&t46 + 8), t45);
                                    gm_l27 = t46;
                                    {
                                        {
                                            {
                                                {
                                                    uint64_t t47 = GM_LD(uint64_t, ((uint8_t *)&gm_l27));
                                                    gm_l5 = t47;
                                                    uint32_t t48 = GM_LD(uint32_t, (((uint8_t *)&gm_l27) + 8));
                                                    gm_l6 = t48;
                                                    void * t49 = gm_l0;
                                                    uint32_t t50 = gm_l6;
                                                    gm_b0_1 t51 = gm_f_queue_cd51r8c((void *)NULL, cx, NULL, t49, t50);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    void * t52 = gm_l0;
                                                    void * t53 = GM_LD(void *, ((uint8_t *)t52));
                                                    uint32_t t54 = gm_l6;
                                                    void * t55 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t53, t54);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_l7 = t55;
                                                    void * t56 = gm_l0;
                                                    void * t57 = (void *)(((uint8_t *)t56) + 8);
                                                    void * t58 = gm_l7;
                                                    gm_t88rcucg1b7uk t59 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t58) + 64));
                                                    gm_b16_8 t60 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t57, t59);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_l8 = t60;
                                                    gm_trqfvmjtt4sij t61;
                                                    gm_f_list_nof7vfj((void *)&t61, (void *)NULL, cx, NULL);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    F.l9 = t61;
                                                    void * t62 = gm_l7;
                                                    gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)t62) + 32));
                                                    gm_l10 = t63;
                                                    gm_l11 = ((uint64_t)0ULL);
                                                    {
                                                        for (;;) {
                                                            uint64_t t64 = gm_l11;
                                                            uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                                            uint8_t t66 = (t64 < t65);
                                                            if (!t66) {
                                                                goto gm_lb9;
                                                            }
                                                            {
                                                                void * t67 = GM_LD(void *, ((uint8_t *)&gm_l10));
                                                                uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                                                uint64_t t69 = gm_l11;
                                                                {
                                                                    {
                                                                        uint32_t t70 = GM_LD(uint32_t, (((uint8_t *)t67) + (uintptr_t)t69 * 4));
                                                                        gm_l12 = t70;
                                                                        void * t71 = (void *)((uint8_t *)&F.l9);
                                                                        void * t72 = gm_l7;
                                                                        void * t73 = GM_LD(void *, (((uint8_t *)t72) + 48));
                                                                        uint64_t t74 = GM_LD(uint64_t, ((((uint8_t *)t72) + 48) + 8));
                                                                        uint32_t t75 = gm_l12;
                                                                        uint64_t t76 = (uint64_t)t75;
                                                                        if ((uintptr_t)t76 >= (uintptr_t)(t74)) gm_trap_index((uintptr_t)t76, t74, gm_srcd5arorrf8jkg, 612, 45);
                                                                        gm_t88rcucg1b7uk t77 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t73) + (uintptr_t)t76 * 40) + 16));
                                                                        gm_tftb1odc739t5 t78 = gm_f_push_k3l9nic(t71, GM_CONV(gm_csguankko4lmhd, t77));
                                                                        gm_l28 = t78;
                                                                        uint8_t t79 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                                                                        if (!t79) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 1, gm_srcd5arorrf8jkg, 612, 35);
                                                                        goto gm_lb12;
                                                                    }
gm_lb13:;
                                                                    gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 612, 13);
                                                                }
gm_lb12:;
                                                            }
gm_lb11:;
                                                            uint64_t t80 = gm_l11;
                                                            uint64_t r81; bool o82 = __builtin_add_overflow(t80, ((uint64_t)1ULL), &r81);
                                                            uint64_t t83 = r81;
                                                            gm_l11 = t83;
                                                        }
                                                    }
gm_lb9:;
                                                    void * t84 = gm_l0;
                                                    void * t85 = (void *)(((uint8_t *)t84) + 8);
                                                    void * t86 = GM_LD(void *, ((uint8_t *)&F.l9));
                                                    uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                                                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t87)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t87, gm_srcd5arorrf8jkg, 613, 33);
                                                    gm_t88rcucg1b7uk t88 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t86) + (uintptr_t)((uint64_t)0ULL) * 4));
                                                    gm_b16_8 t89 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t85, t88);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_l13 = t89;
                                                    gm_trqfvmjtt4sij t90;
                                                    gm_f_list_epvducg((void *)&t90, (void *)NULL, cx, NULL);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    F.l14 = t90;
                                                }
                                                {
                                                    void * t91 = (void *)((uint8_t *)&F.l14);
                                                    gm_b16_8 t92;
                                                    GM_ST(void *, (uint8_t *)&t92, (void *)(gm_siji3oo7ud53t));
                                                    GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(9));
                                                    gm_b16_8 t93;
                                                    GM_ST(void *, (uint8_t *)&t93, (void *)(gm_s2kvk8bnjk52t));
                                                    GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(11));
                                                    gm_b16_8 t94;
                                                    GM_ST(void *, (uint8_t *)&t94, (void *)(gm_sdi44r3kug6n6));
                                                    GM_ST(uintptr_t, (uint8_t *)&t94 + 8, (uintptr_t)(13));
                                                    gm_t9bmh80edou9c t95;
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t95 + 0), t92);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t95 + 16), t93);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t95 + 32), t94);
                                                    F.l15 = t95;
                                                    void * t96 = (void *)((uint8_t *)&F.l15);
                                                    uint64_t t97 = ((uintptr_t)((uint64_t)3ULL)) - (uintptr_t)((uint64_t)0ULL);
                                                    gm_b16_8 t98;
                                                    GM_ST(void *, (uint8_t *)&t98, (void *)(((uint8_t *)t96 + (uintptr_t)((uint64_t)0ULL) * 16)));
                                                    GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(t97));
                                                    gm_tftb1odc739t5 t99 = gm_f_extend_vogsd72(t91, t98);
                                                    gm_l29 = t99;
                                                    uint8_t t100 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                                                    if (!t100) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_srcd5arorrf8jkg, 615, 13);
                                                    void * t101 = gm_l0;
                                                    void * t102 = (void *)(((uint8_t *)t101) + 8);
                                                    void * t103 = GM_LD(void *, ((uint8_t *)&F.l9));
                                                    uint64_t t104 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                                                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t104)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t104, gm_srcd5arorrf8jkg, 617, 47);
                                                    gm_t88rcucg1b7uk t105 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t103) + (uintptr_t)((uint64_t)0ULL) * 4));
                                                    uint8_t t106 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t102, t105);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    if (t106) {
                                                        gm_b16_8 t107;
                                                        GM_ST(void *, (uint8_t *)&t107, (void *)(gm_s205esok2u8pk));
                                                        GM_ST(uintptr_t, (uint8_t *)&t107 + 8, (uintptr_t)(3));
                                                        gm_l16 = t107;
                                                    } else {
                                                        gm_b16_8 t108 = gm_l13;
                                                        gm_tnpifv20irnph t109;
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t109 + 0), t108);
                                                        gm_f_format_3g143bv((void *)&gm_l30, (void *)NULL, cx, NULL, t109);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        uint8_t t110 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
                                                        if (!t110) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 8, gm_srcd5arorrf8jkg, 617, 79);
                                                        gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 8));
                                                        gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&t111));
                                                        gm_l16 = t112;
                                                    }
                                                    gm_trqfvmjtt4sij t113;
                                                    gm_f_list_epvducg((void *)&t113, (void *)NULL, cx, NULL);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    F.l17 = t113;
                                                    void * t114 = (void *)((uint8_t *)&F.l17);
                                                    gm_b16_8 t115;
                                                    GM_ST(void *, (uint8_t *)&t115, (void *)(gm_s04qus0luoga5));
                                                    GM_ST(uintptr_t, (uint8_t *)&t115 + 8, (uintptr_t)(4));
                                                    gm_b16_8 t116;
                                                    GM_ST(void *, (uint8_t *)&t116, (void *)(gm_s10iv34fu7qe7));
                                                    GM_ST(uintptr_t, (uint8_t *)&t116 + 8, (uintptr_t)(2));
                                                    gm_b16_8 t117;
                                                    GM_ST(void *, (uint8_t *)&t117, (void *)(gm_suqchf5vh2hqp));
                                                    GM_ST(uintptr_t, (uint8_t *)&t117 + 8, (uintptr_t)(4));
                                                    gm_b16_8 t118 = gm_l16;
                                                    gm_tdmi4fk9nhqa7 t119;
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t119 + 0), t115);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t119 + 16), t116);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t119 + 32), t117);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t119 + 48), t118);
                                                    F.l18 = t119;
                                                    void * t120 = (void *)((uint8_t *)&F.l18);
                                                    uint64_t t121 = ((uintptr_t)((uint64_t)4ULL)) - (uintptr_t)((uint64_t)0ULL);
                                                    gm_b16_8 t122;
                                                    GM_ST(void *, (uint8_t *)&t122, (void *)(((uint8_t *)t120 + (uintptr_t)((uint64_t)0ULL) * 16)));
                                                    GM_ST(uintptr_t, (uint8_t *)&t122 + 8, (uintptr_t)(t121));
                                                    gm_tftb1odc739t5 t123 = gm_f_extend_vogsd72(t114, t122);
                                                    gm_l31 = t123;
                                                    uint8_t t124 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                                                    if (!t124) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 1, gm_srcd5arorrf8jkg, 619, 13);
                                                    gm_l20 = ((uint64_t)1ULL);
                                                    uint64_t t125 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                                                    gm_l19 = t125;
                                                    {
                                                        for (;;) {
                                                            uint64_t t126 = gm_l20;
                                                            uint64_t t127 = gm_l19;
                                                            uint8_t t128 = (t126 < t127);
                                                            if (!t128) {
                                                                goto gm_lb14;
                                                            }
                                                            {
                                                                {
                                                                    {
                                                                        uint64_t t129 = gm_l20;
                                                                        gm_l21 = t129;
                                                                        {
                                                                            void * t130 = (void *)((uint8_t *)&F.l14);
                                                                            void * t131 = gm_l0;
                                                                            void * t132 = (void *)(((uint8_t *)t131) + 8);
                                                                            void * t133 = GM_LD(void *, ((uint8_t *)&F.l9));
                                                                            uint64_t t134 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                                                                            uint64_t t135 = gm_l21;
                                                                            if ((uintptr_t)t135 >= (uintptr_t)(t134)) gm_trap_index((uintptr_t)t135, t134, gm_srcd5arorrf8jkg, 621, 65);
                                                                            gm_t88rcucg1b7uk t136 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t133) + (uintptr_t)t135 * 4));
                                                                            gm_b16_8 t137 = gm_f_param_cty_e8tvhrm((void *)NULL, cx, NULL, t132, t136);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_lad;
                                                                            }
                                                                            uint64_t t138 = gm_l21;
                                                                            gm_t3877pk7lv748 t139;
                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t139 + 0), t137);
                                                                            GM_ST(uint64_t, ((uint8_t *)&t139 + 16), t138);
                                                                            gm_f_format_eq87866((void *)&gm_l32, (void *)NULL, cx, NULL, &t139);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_lad;
                                                                            }
                                                                            uint8_t t140 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                                                                            if (!t140) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 8, gm_srcd5arorrf8jkg, 621, 29);
                                                                            gm_b16_8 t141 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l32) + 8));
                                                                            gm_b16_8 t142 = GM_LD(gm_b16_8, ((uint8_t *)&t141));
                                                                            gm_tftb1odc739t5 t143 = gm_f_push_3ef746b(t130, t142);
                                                                            gm_l33 = t143;
                                                                        }
                                                                        uint8_t t144 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                                                                        if (!t144) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 1, gm_srcd5arorrf8jkg, 621, 17);
                                                                        {
                                                                            void * t145 = (void *)((uint8_t *)&F.l17);
                                                                            uint64_t t146 = gm_l21;
                                                                            gm_t4ce0jd3tdro4 t147;
                                                                            GM_ST(uint64_t, ((uint8_t *)&t147 + 0), t146);
                                                                            gm_f_format_l05am3t((void *)&gm_l34, (void *)NULL, cx, NULL, t147);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_lad;
                                                                            }
                                                                            uint8_t t148 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                                                                            if (!t148) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 8, gm_srcd5arorrf8jkg, 622, 27);
                                                                            gm_b16_8 t149 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l34) + 8));
                                                                            gm_b16_8 t150 = GM_LD(gm_b16_8, ((uint8_t *)&t149));
                                                                            gm_tftb1odc739t5 t151 = gm_f_push_3ef746b(t145, t150);
                                                                            gm_l35 = t151;
                                                                        }
                                                                        uint8_t t152 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
                                                                        if (!t152) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 1, gm_srcd5arorrf8jkg, 622, 17);
                                                                        goto gm_lb17;
                                                                    }
gm_lb18:;
                                                                    gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 620, 13);
                                                                }
gm_lb17:;
                                                            }
gm_lb16:;
                                                            uint64_t t153 = gm_l20;
                                                            uint64_t r154; bool o155 = __builtin_add_overflow(t153, ((uint64_t)1ULL), &r154);
                                                            uint64_t t156 = r154;
                                                            gm_l20 = t156;
                                                        }
                                                    }
gm_lb14:;
                                                }
                                                {
                                                    void * t157 = gm_l0;
                                                    uint32_t t158 = gm_l1;
                                                    gm_b16_8 t159 = gm_f_decl_name_fode8m9((void *)NULL, cx, NULL, t157, t158);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_l36 = t159;
                                                    void * t160 = GM_LD(void *, ((uint8_t *)&gm_l36));
                                                    uint64_t t161 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 8));
                                                    gm_b16_8 t162;
                                                    GM_ST(void *, (uint8_t *)&t162, (void *)(gm_scj1kuqp0242l));
                                                    GM_ST(uintptr_t, (uint8_t *)&t162 + 8, (uintptr_t)(5));
                                                    gm_l37 = t162;
                                                    uint64_t t163 = GM_LD(uint64_t, (((uint8_t *)&gm_l37) + 8));
                                                    uint64_t t164 = (uintptr_t)t163;
                                                    uint64_t t165 = (uintptr_t)t161;
                                                    if (t164 > t165 || t165 > (uintptr_t)t161) gm_trap_slice(t164, t165, t161, gm_srcd5arorrf8jkg, 624, 46);
                                                    uint64_t t166 = ((uintptr_t)t161) - (uintptr_t)t163;
                                                    gm_b16_8 t167;
                                                    GM_ST(void *, (uint8_t *)&t167, (void *)(((uint8_t *)t160 + (uintptr_t)t163 * 1)));
                                                    GM_ST(uintptr_t, (uint8_t *)&t167 + 8, (uintptr_t)(t166));
                                                    uint64_t t168 = gm_l5;
                                                    gm_t3877pk7lv748 t169;
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t169 + 0), t167);
                                                    GM_ST(uint64_t, ((uint8_t *)&t169 + 16), t168);
                                                    gm_f_format_dgfvefu((void *)&gm_l38, (void *)NULL, cx, NULL, &t169);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    uint8_t t170 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                                                    if (!t170) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 8, gm_srcd5arorrf8jkg, 624, 21);
                                                    gm_b16_8 t171 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l38) + 8));
                                                    gm_l22 = t171;
                                                    gm_b16_8 t172;
                                                    GM_ST(void *, (uint8_t *)&t172, (void *)(gm_smbbaf3n1gml2));
                                                    GM_ST(uintptr_t, (uint8_t *)&t172 + 8, (uintptr_t)(7));
                                                    gm_l23 = t172;
                                                    void * t173 = gm_l0;
                                                    void * t174 = (void *)(((uint8_t *)t173) + 8);
                                                    void * t175 = gm_l7;
                                                    gm_t88rcucg1b7uk t176 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t175) + 64));
                                                    uint8_t t177 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t174, t176);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    if (t177) {
                                                        void * t178 = (void *)((uint8_t *)&F.l14);
                                                        gm_b16_8 t179;
                                                        GM_ST(void *, (uint8_t *)&t179, (void *)(gm_s4ql65vkv7h9p));
                                                        GM_ST(uintptr_t, (uint8_t *)&t179 + 8, (uintptr_t)(12));
                                                        gm_tftb1odc739t5 t180 = gm_f_insert_v6fq3iv(t178, ((uint64_t)0ULL), t179);
                                                        gm_l39 = t180;
                                                        uint8_t t181 = (uint8_t)!((((uint8_t *)&gm_l39))[0] != 0);
                                                        if (!t181) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l39) + 1, gm_srcd5arorrf8jkg, 628, 17);
                                                        void * t182 = (void *)((uint8_t *)&F.l17);
                                                        gm_b16_8 t183;
                                                        GM_ST(void *, (uint8_t *)&t183, (void *)(gm_sq9pusqm0kq2f));
                                                        GM_ST(uintptr_t, (uint8_t *)&t183 + 8, (uintptr_t)(6));
                                                        gm_tftb1odc739t5 t184 = gm_f_insert_v6fq3iv(t182, ((uint64_t)0ULL), t183);
                                                        gm_l40 = t184;
                                                        uint8_t t185 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
                                                        if (!t185) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l40) + 1, gm_srcd5arorrf8jkg, 629, 17);
                                                        gm_b16_8 t186;
                                                        GM_ST(void *, (uint8_t *)&t186, (void *)(gm_sts5j6f5pkov0));
                                                        GM_ST(uintptr_t, (uint8_t *)&t186 + 8, (uintptr_t)(4));
                                                        gm_l8 = t186;
                                                        gm_b16_8 t187;
                                                        GM_ST(void *, (uint8_t *)&t187, (void *)(gm_sr4qbrk6tlgaa));
                                                        GM_ST(uintptr_t, (uint8_t *)&t187 + 8, (uintptr_t)(0));
                                                        gm_l23 = t187;
                                                    }
                                                    void * t188 = gm_l0;
                                                    void * t189 = (void *)(((uint8_t *)t188) + 1944);
                                                    gm_b16_8 t190 = gm_l8;
                                                    gm_b16_8 t191 = gm_l22;
                                                    gm_b16_8 t192 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
                                                    gm_b16_8 t193 = GM_LD(gm_b16_8, ((uint8_t *)&t192));
                                                    gm_b16_8 t194;
                                                    GM_ST(void *, (uint8_t *)&t194, (void *)(gm_sg5gdnrith2i6));
                                                    GM_ST(uintptr_t, (uint8_t *)&t194 + 8, (uintptr_t)(2));
                                                    gm_f_join_sfknljb((void *)&gm_l41, (void *)NULL, cx, NULL, t193, t194);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    uint8_t t195 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                                                    if (!t195) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 8, gm_srcd5arorrf8jkg, 633, 62);
                                                    gm_b16_8 t196 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l41) + 8));
                                                    gm_tfeab279c16rr t197;
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t197 + 0), t190);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t197 + 16), t191);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t197 + 32), t196);
                                                    gm_tftb1odc739t5 t198 = gm_f_write_qqu2rl9(t189, &t197);
                                                    gm_l42 = t198;
                                                    uint8_t t199 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
                                                    if (!t199) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l42) + 1, gm_srcd5arorrf8jkg, 633, 13);
                                                    void * t200 = gm_l0;
                                                    void * t201 = (void *)(((uint8_t *)t200) + 1984);
                                                    gm_b16_8 t202 = gm_l8;
                                                    gm_b16_8 t203 = gm_l22;
                                                    gm_b16_8 t204 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
                                                    gm_b16_8 t205 = GM_LD(gm_b16_8, ((uint8_t *)&t204));
                                                    gm_b16_8 t206;
                                                    GM_ST(void *, (uint8_t *)&t206, (void *)(gm_sg5gdnrith2i6));
                                                    GM_ST(uintptr_t, (uint8_t *)&t206 + 8, (uintptr_t)(2));
                                                    gm_f_join_sfknljb((void *)&gm_l43, (void *)NULL, cx, NULL, t205, t206);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    uint8_t t207 = (uint8_t)!((((uint8_t *)&gm_l43))[0] != 0);
                                                    if (!t207) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l43) + 8, gm_srcd5arorrf8jkg, 634, 77);
                                                    gm_b16_8 t208 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l43) + 8));
                                                    gm_b16_8 t209 = gm_l23;
                                                    void * t210 = gm_l0;
                                                    uint32_t t211 = gm_l6;
                                                    gm_b16_8 t212 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t210, t211);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_b16_8 t213 = GM_LD(gm_b16_8, ((uint8_t *)&F.l17));
                                                    gm_b16_8 t214 = GM_LD(gm_b16_8, ((uint8_t *)&t213));
                                                    gm_b16_8 t215;
                                                    GM_ST(void *, (uint8_t *)&t215, (void *)(gm_sg5gdnrith2i6));
                                                    GM_ST(uintptr_t, (uint8_t *)&t215 + 8, (uintptr_t)(2));
                                                    gm_f_join_sfknljb((void *)&gm_l44, (void *)NULL, cx, NULL, t214, t215);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    uint8_t t216 = (uint8_t)!((((uint8_t *)&gm_l44))[0] != 0);
                                                    if (!t216) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l44) + 8, gm_srcd5arorrf8jkg, 634, 130);
                                                    gm_b16_8 t217 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l44) + 8));
                                                    gm_tif3elqk01bdl t218;
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t218 + 0), t202);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t218 + 16), t203);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t218 + 32), t208);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t218 + 48), t209);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t218 + 64), t212);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t218 + 80), t217);
                                                    gm_tftb1odc739t5 t219 = gm_f_write_qph5tm9(t201, &t218);
                                                    gm_l45 = t219;
                                                }
                                                {
                                                    uint8_t t220 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
                                                    if (!t220) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l45) + 1, gm_srcd5arorrf8jkg, 634, 13);
                                                    void * t221 = (void *)((uint8_t *)&F.l2);
                                                    gm_b16_8 t222 = gm_l22;
                                                    gm_tnpifv20irnph t223;
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t223 + 0), t222);
                                                    gm_f_format_2g08abe((void *)&gm_l46, (void *)NULL, cx, NULL, t223);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    uint8_t t224 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
                                                    if (!t224) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l46) + 8, gm_srcd5arorrf8jkg, 635, 24);
                                                    gm_b16_8 t225 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l46) + 8));
                                                    gm_b16_8 t226 = GM_LD(gm_b16_8, ((uint8_t *)&t225));
                                                    gm_tftb1odc739t5 t227 = gm_f_push_3ef746b(t221, t226);
                                                    gm_l47 = t227;
                                                    uint8_t t228 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
                                                    if (!t228) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l47) + 1, gm_srcd5arorrf8jkg, 635, 13);
                                                    goto gm_lb7;
                                                }
                                            }
gm_lb8:;
                                        }
                                        {
                                            gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 607, 9);
                                        }
                                    }
gm_lb7:;
                                }
                            }
gm_lb6:;
                        }
                        {
                            uint64_t t229 = gm_l4;
                            uint64_t r230; bool o231 = __builtin_add_overflow(t229, ((uint64_t)1ULL), &r230);
                            uint64_t t232 = r230;
                            gm_l4 = t232;
                        }
                    }
                }
gm_lb4:;
            }
            {
                void * t233 = gm_l0;
                void * t234 = (void *)(((uint8_t *)t233) + 3080);
                void * t235 = gm_l0;
                uint32_t t236 = gm_l1;
                gm_b16_8 t237 = gm_f_decl_name_fode8m9((void *)NULL, cx, NULL, t235, t236);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t238 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
                gm_b16_8 t239 = GM_LD(gm_b16_8, ((uint8_t *)&t238));
                gm_b16_8 t240;
                GM_ST(void *, (uint8_t *)&t240, (void *)(gm_sg5gdnrith2i6));
                GM_ST(uintptr_t, (uint8_t *)&t240 + 8, (uintptr_t)(2));
                gm_f_join_sfknljb((void *)&gm_l48, (void *)NULL, cx, NULL, t239, t240);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t241 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                if (!t241) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l48) + 8, gm_srcd5arorrf8jkg, 637, 87);
                gm_b16_8 t242 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l48) + 8));
                gm_t6j6smj7qoctu t243;
                GM_ST(gm_b16_8, ((uint8_t *)&t243 + 0), t237);
                GM_ST(gm_b16_8, ((uint8_t *)&t243 + 16), t242);
                gm_tftb1odc739t5 t244 = gm_f_write_b3knhd6(t234, &t243);
                gm_l49 = t244;
                uint8_t t245 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
                if (!t245) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l49) + 1, gm_srcd5arorrf8jkg, 637, 9);
            }
        }
    }
    {
        void * t246 = gm_l0;
        uint32_t t247 = gm_l1;
        gm_b16_8 t248 = gm_f_decl_name_fode8m9((void *)NULL, cx, NULL, t246, t247);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t248);
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
inline __attribute__((aligned(4))) void gm_f_drain_leoibma(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_drain_leoibma F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    gm_tcc1rq4n1avb2 gm_l2; /* c */
    GM_STACK_CHECK(&F, 380u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9805qtonbsag gm_l3;
    gm_tqgtd5gtgj65s gm_l4;
    gm_t736djc66po0l gm_l5;
    gm_tqgtd5gtgj65s gm_l6;
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb1;
            }
            {
                {
                    {
                        void * t9 = gm_l0;
                        void * t10 = (void *)(((uint8_t *)t9) + 2328);
                        gm_t9805qtonbsag t11 = gm_f_pop_570g79n(t10);
                        gm_l3 = t11;
                        uint8_t t12 = (uint8_t)((((uint8_t *)&gm_l3))[4] != 0);
                        if (!t12) {
                            goto gm_lb5;
                        }
                        uint32_t t13 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
                        gm_l1 = t13;
                        void * t14 = gm_l0;
                        void * t15 = gm_l0;
                        uint32_t t16 = gm_l1;
                        gm_tgqgdb97ka5kt t17 = gm_f_owner_of_cirm596((void *)NULL, cx, NULL, t15, t16);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b0_1 t18 = gm_f_mark_owner_uhhhf9f((void *)NULL, cx, NULL, t14, t17);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t19 = gm_l0;
                        uint32_t t20 = gm_l1;
                        gm_t9805qtonbsag t21;
                        memset(&t21, 0, sizeof t21);
                        gm_f_gen_fn_2fqvb90((void *)&gm_l4, (void *)NULL, cx, NULL, t19, t20, t21);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t22 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
                        if (t22) {
                        } else {
                            gm_t08olq97rscn3 t23 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l4) + 8));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8), t23);
                            goto gm_fdone;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    {
                        {
                            void * t24 = gm_l0;
                            void * t25 = (void *)(((uint8_t *)t24) + 2744);
                            gm_t736djc66po0l t26 = gm_f_pop_168lbb8(t25);
                            gm_l5 = t26;
                            uint8_t t27 = (uint8_t)((((uint8_t *)&gm_l5))[8] != 0);
                            if (!t27) {
                                goto gm_lb7;
                            }
                            gm_tcc1rq4n1avb2 t28 = GM_LD(gm_tcc1rq4n1avb2, ((uint8_t *)&gm_l5));
                            gm_l2 = t28;
                            void * t29 = gm_l0;
                            void * t30 = gm_l0;
                            uint32_t t31 = GM_LD(uint32_t, ((uint8_t *)&gm_l2));
                            gm_tgqgdb97ka5kt t32 = gm_f_owner_of_cirm596((void *)NULL, cx, NULL, t30, t31);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_b0_1 t33 = gm_f_mark_owner_uhhhf9f((void *)NULL, cx, NULL, t29, t32);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t34 = gm_l0;
                            uint32_t t35 = GM_LD(uint32_t, ((uint8_t *)&gm_l2));
                            uint32_t t36 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
                            gm_t9805qtonbsag t37;
                            GM_ST(uint8_t, (uint8_t *)&t37 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t37 + 6, 0);
                            GM_ST(uint32_t, ((uint8_t *)&t37), t36);
                            ((uint8_t *)&t37)[4] = 1;
                            gm_f_gen_fn_2fqvb90((void *)&gm_l6, (void *)NULL, cx, NULL, t34, t35, t37);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t38 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                            if (t38) {
                            } else {
                                gm_t08olq97rscn3 t39 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l6) + 8));
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 1;
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8), t39);
                                goto gm_fdone;
                            }
                            goto gm_lb6;
                        }
gm_lb7:;
                        void * t40 = gm_l0;
                        gm_tgqgdb97ka5kt t41 = GM_LD(gm_tgqgdb97ka5kt, ((uint8_t *)gm_ctvsbqjhdl3));
                        gm_b0_1 t42 = gm_f_mark_owner_uhhhf9f((void *)NULL, cx, NULL, t40, t41);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        memset(&(*(gm_tqgtd5gtgj65s *)gm_out), 0, sizeof (*(gm_tqgtd5gtgj65s *)gm_out));
                        ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 0;
                        GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8), GM_ZERO(gm_b0_1));
                        goto gm_fdone;
                    }
gm_lb6:;
                }
gm_lb4:;
            }
gm_lb3:;
        }
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
inline __attribute__((aligned(4))) uint32_t gm_f_effect_id_5588nqa(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_effect_id_5588nqa F;
    void * gm_l0; /* g */
    void * gm_l1; /* e */
    gm_b16_8 gm_l2; /* key */
    uint32_t gm_l3; /* id */
    uint32_t gm_l4; /* n */
    GM_STACK_CHECK(&F, 342u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t9805qtonbsag gm_l5;
    uint8_t gm_l6;
    gm_th2lqbo77pvsc gm_l7;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    void * t10 = gm_l0;
    void * t11 = GM_LD(void *, ((uint8_t *)t10));
    void * t12 = GM_LD(void *, ((uint8_t *)t11));
    void * t13 = (void *)(((uint8_t *)t12) + 8);
    void * t14 = gm_l1;
    gm_b16_8 t15 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t13, t14);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t15;
    {
        {
            void * t16 = gm_l0;
            void * t17 = (void *)(((uint8_t *)t16) + 1160);
            gm_b16_8 t18 = gm_l2;
            gm_t9805qtonbsag t19 = gm_f_get_m1knqqk(t17, t18);
            gm_l5 = t19;
            uint8_t t20 = (uint8_t)((((uint8_t *)&gm_l5))[4] != 0);
            if (!t20) {
                goto gm_lb2;
            }
            uint32_t t21 = GM_LD(uint32_t, ((uint8_t *)&gm_l5));
            gm_l3 = t21;
            uint32_t t22 = gm_l3;
            F.ret = t22;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_b16_8 t23 = gm_l2;
    uint64_t t24 = gm_f_hash64_apkb7bo(t23);
    uint64_t t25 = (uint64_t)(t24 & ((uint64_t)2147483647ULL));
    uint32_t t26 = (uint32_t)t25;
    gm_l4 = t26;
    {
        for (;;) {
            void * t27 = gm_l0;
            void * t28 = (void *)(((uint8_t *)t27) + 1656);
            uint32_t t29 = gm_l4;
            gm_f_get_gnmase9((void *)&gm_l7, t28, t29);
            {
                {
                    uint8_t t30 = (uint8_t)((((uint8_t *)&gm_l7))[16] != 0);
                    if (t30) {
                        goto gm_lb7;
                    }
                    gm_l6 = ((uint8_t)0);
                    goto gm_lb6;
                }
gm_lb7:;
                {
                    gm_l6 = ((uint8_t)1);
                    goto gm_lb6;
                }
gm_lb8:;
                gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 497, 11);
            }
gm_lb6:;
            uint8_t t31 = gm_l6;
            if (!t31) {
                goto gm_lb3;
            }
            {
                uint32_t t32 = gm_l4;
                uint32_t r33; bool o34 = __builtin_add_overflow(t32, ((uint32_t)1ULL), &r33);
                if (o34) gm_trap("integer overflow in `+`", gm_srcd5arorrf8jkg, 497, 52);
                uint32_t t35 = r33;
                uint32_t t36 = (uint32_t)(t35 & ((uint32_t)2147483647ULL));
                gm_l4 = t36;
            }
gm_lb5:;
        }
    }
gm_lb3:;
    void * t37 = gm_l0;
    void * t38 = (void *)(((uint8_t *)t37) + 1160);
    gm_b16_8 t39 = gm_l2;
    uint32_t t40 = gm_l4;
    gm_tftb1odc739t5 t41 = gm_f_put_074448v(t38, t39, t40);
    gm_l8 = t41;
    uint8_t t42 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (!t42) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srcd5arorrf8jkg, 498, 5);
    void * t43 = gm_l0;
    void * t44 = (void *)(((uint8_t *)t43) + 1656);
    uint32_t t45 = gm_l4;
    gm_b16_8 t46 = gm_l2;
    gm_tftb1odc739t5 t47 = gm_f_put_po09vud(t44, t45, t46);
    gm_l9 = t47;
    uint8_t t48 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (!t48) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcd5arorrf8jkg, 499, 5);
    uint32_t t49 = gm_l4;
    F.ret = t49;
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
__attribute__((aligned(4))) uint8_t gm_f_expr_may_exit_qfnn2ei(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9805qtonbsag a2) {
    struct gm_fr_expr_may_exit_qfnn2ei F;
    void * gm_l1; /* e */
    gm_teo9d4mncjfdu gm_l3; /* b */
    uint32_t gm_l4; /* levels */
    uint32_t gm_l5; /* levels */
    uint32_t gm_l6; /* levels */
    gm_t083kd7b1kkag gm_l7; /* t */
    uint8_t gm_l9; /* may */
    uint32_t gm_l10; /* f */
    gm_tiilmfd16v3fc gm_l11; /* eff */
    uint32_t gm_l12; /* op */
    uint32_t gm_l13; /* s */
    gm_tf9f5m02f56gm gm_l14; /* key */
    gm_b16_8 gm_l15; /* clauses */
    GM_STACK_CHECK(&F, 470u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    F.l2 = a2;
    void * t28 = gm_l1;
    {
        {
            uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t28) + 8) + 0));
            uint8_t t30 = (uint8_t)(t29 == ((uint8_t)49ULL));
            if (!t30) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l3), ((((uint8_t *)t28) + 8) + 8), 24);
            {
                {
                    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t32 = (uint8_t)(t31 == ((uint8_t)1ULL));
                    if (!t32) {
                        goto gm_lb4;
                    }
                    F.ret = ((uint8_t)1);
                    goto gm_fret;
                }
gm_lb4:;
                {
                    uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t34 = (uint8_t)(t33 == ((uint8_t)0ULL));
                    if (!t34) {
                        goto gm_lb5;
                    }
                    uint32_t t35 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
                    gm_l4 = t35;
                    uint32_t t36 = gm_l4;
                    uint8_t t37 = (t36 > ((uint32_t)0ULL));
                    if (t37) {
                        F.ret = ((uint8_t)1);
                        goto gm_fret;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 830, 22);
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)t28) + 8) + 0));
            uint8_t t39 = (uint8_t)(t38 == ((uint8_t)50ULL));
            if (!t39) {
                goto gm_lb6;
            }
            uint8_t t40 = GM_LD(uint8_t, (((((uint8_t *)t28) + 8) + 8) + 0));
            uint8_t t41 = (uint8_t)(t40 == ((uint8_t)0ULL));
            if (!t41) {
                goto gm_lb6;
            }
            uint32_t t42 = GM_LD(uint32_t, (((((uint8_t *)t28) + 8) + 8) + 4));
            gm_l5 = t42;
            uint32_t t43 = gm_l5;
            uint8_t t44 = (t43 > ((uint32_t)0ULL));
            if (t44) {
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto gm_lb1;
        }
gm_lb6:;
        {
            uint8_t t45 = GM_LD(uint8_t, ((((uint8_t *)t28) + 8) + 0));
            uint8_t t46 = (uint8_t)(t45 == ((uint8_t)50ULL));
            if (!t46) {
                goto gm_lb7;
            }
            uint8_t t47 = GM_LD(uint8_t, (((((uint8_t *)t28) + 8) + 8) + 0));
            uint8_t t48 = (uint8_t)(t47 == ((uint8_t)1ULL));
            if (!t48) {
                goto gm_lb7;
            }
            goto gm_lb1;
        }
gm_lb7:;
        {
            uint8_t t49 = GM_LD(uint8_t, ((((uint8_t *)t28) + 8) + 0));
            uint8_t t50 = (uint8_t)(t49 == ((uint8_t)51ULL));
            if (!t50) {
                goto gm_lb8;
            }
            uint32_t t51 = GM_LD(uint32_t, ((((uint8_t *)t28) + 8) + 8));
            gm_l6 = t51;
            uint32_t t52 = gm_l6;
            uint8_t t53 = (t52 > ((uint32_t)0ULL));
            if (t53) {
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto gm_lb1;
        }
gm_lb8:;
        {
            uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)t28) + 8) + 0));
            uint8_t t55 = (uint8_t)(t54 == ((uint8_t)42ULL));
            if (!t55) {
                goto gm_lb9;
            }
            memmove(((uint8_t *)&gm_l7), ((((uint8_t *)t28) + 8) + 8), 24);
            uint32_t t56 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 16));
            uint8_t t57 = (t56 > ((uint32_t)0ULL));
            if (t57) {
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto gm_lb1;
        }
gm_lb9:;
        {
            {
                {
                    uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)t28) + 8) + 0));
                    uint8_t t59 = (uint8_t)(t58 == ((uint8_t)59ULL));
                    if (!t59) {
                        goto gm_lb12;
                    }
                    goto gm_lb11;
                }
gm_lb12:;
                {
                    uint8_t t60 = GM_LD(uint8_t, ((((uint8_t *)t28) + 8) + 0));
                    uint8_t t61 = (uint8_t)(t60 == ((uint8_t)53ULL));
                    if (!t61) {
                        goto gm_lb13;
                    }
                    goto gm_lb11;
                }
gm_lb13:;
                {
                    uint8_t t62 = GM_LD(uint8_t, ((((uint8_t *)t28) + 8) + 0));
                    uint8_t t63 = (uint8_t)(t62 == ((uint8_t)54ULL));
                    if (!t63) {
                        goto gm_lb14;
                    }
                    goto gm_lb11;
                }
gm_lb14:;
                goto gm_lb10;
            }
gm_lb11:;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb10:;
        {
            uint8_t t64 = GM_LD(uint8_t, ((((uint8_t *)t28) + 8) + 0));
            uint8_t t65 = (uint8_t)(t64 == ((uint8_t)33ULL));
            if (!t65) {
                goto gm_lb15;
            }
            uint8_t t66 = GM_LD(uint8_t, (((((uint8_t *)t28) + 8) + 8) + 0));
            uint8_t t67 = (uint8_t)(t66 == ((uint8_t)74ULL));
            if (!t67) {
                goto gm_lb15;
            }
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb15:;
        {
            uint8_t t68 = GM_LD(uint8_t, ((((uint8_t *)t28) + 8) + 0));
            uint8_t t69 = (uint8_t)(t68 == ((uint8_t)55ULL));
            if (!t69) {
                goto gm_lb16;
            }
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb16:;
        {
            uint8_t t70 = GM_LD(uint8_t, ((((uint8_t *)t28) + 8) + 0));
            uint8_t t71 = (uint8_t)(t70 == ((uint8_t)34ULL));
            if (!t71) {
                goto gm_lb17;
            }
            memmove(((uint8_t *)&F.l8), ((((uint8_t *)t28) + 8) + 8), 40);
            gm_l9 = ((uint8_t)0);
            {
                {
                    uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + 0));
                    uint8_t t73 = (uint8_t)(t72 == ((uint8_t)0ULL));
                    if (!t73) {
                        goto gm_lb19;
                    }
                    uint32_t t74 = GM_LD(uint32_t, (((uint8_t *)&F.l8) + 8));
                    gm_l10 = t74;
                    void * t75 = F.l0;
                    uint32_t t76 = gm_l10;
                    uint8_t t77 = gm_f_may_unwind_irl2ktm((void *)NULL, cx, NULL, t75, t76);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l9 = t77;
                    goto gm_lb18;
                }
gm_lb19:;
                {
                    uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + 0));
                    uint8_t t79 = (uint8_t)(t78 == ((uint8_t)2ULL));
                    if (!t79) {
                        goto gm_lb20;
                    }
                    gm_l9 = ((uint8_t)1);
                    goto gm_lb18;
                }
gm_lb20:;
                {
                    uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + 0));
                    uint8_t t81 = (uint8_t)(t80 == ((uint8_t)1ULL));
                    if (!t81) {
                        goto gm_lb21;
                    }
                    memmove(((uint8_t *)&gm_l11), (((uint8_t *)&F.l8) + 8), 24);
                    uint32_t t82 = GM_LD(uint32_t, ((((uint8_t *)&F.l8) + 8) + 24));
                    gm_l12 = t82;
                    gm_l9 = ((uint8_t)1);
                    {
                        {
                            uint8_t t83 = (uint8_t)((((uint8_t *)&F.l2))[4] != 0);
                            if (!t83) {
                                goto gm_lb23;
                            }
                            uint32_t t84 = GM_LD(uint32_t, ((uint8_t *)&F.l2));
                            gm_l13 = t84;
                            void * t85 = F.l0;
                            void * t86 = GM_LD(void *, ((uint8_t *)t85));
                            uint32_t t87 = GM_LD(uint32_t, ((uint8_t *)&gm_l11));
                            uint32_t t88 = gm_l12;
                            uint8_t t89 = gm_f_is_ctl_oclllo3(t86, t87, t88);
                            uint8_t t90 = (uint8_t)!t89;
                            if (t90) {
                                void * t91 = F.l0;
                                uint32_t t92 = gm_l13;
                                gm_tf9f5m02f56gm t93 = gm_f_spec_key_eljlge7(t91, t92);
                                gm_l14 = t93;
                                void * t94 = F.l0;
                                void * t95 = GM_LD(void *, ((uint8_t *)t94));
                                uint32_t t96 = GM_LD(uint32_t, ((uint8_t *)&gm_l14));
                                void * t97 = gm_f_handler_lit_b02boff(t95, t96);
                                gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)t97) + 24));
                                gm_l15 = t98;
                                {
                                    {
                                        uint32_t t99 = gm_l12;
                                        uint64_t t100 = (uint64_t)t99;
                                        uint64_t t101 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                        uint8_t t102 = (t100 < t101);
                                        if (!t102) {
                                            goto gm_lb25;
                                        }
                                        void * t103 = F.l0;
                                        void * t104 = (void *)((uint8_t *)&gm_l11);
                                        uint32_t t105 = gm_f_effect_id_5588nqa((void *)NULL, cx, NULL, t103, t104);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint32_t t106 = GM_LD(uint32_t, (((uint8_t *)&gm_l14) + 4));
                                        uint8_t t107 = ((t105 == t106));
                                        if (!t107) {
                                            goto gm_lb25;
                                        }
                                        void * t108 = F.l0;
                                        void * t109 = GM_LD(void *, ((uint8_t *)&gm_l15));
                                        uint64_t t110 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                        uint32_t t111 = gm_l12;
                                        uint64_t t112 = (uint64_t)t111;
                                        if ((uintptr_t)t112 >= (uintptr_t)(t110)) gm_trap_index((uintptr_t)t112, t110, gm_srcd5arorrf8jkg, 856, 57);
                                        uint32_t t113 = GM_LD(uint32_t, (((uint8_t *)t109) + (uintptr_t)t112 * 4));
                                        gm_t9805qtonbsag t114 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l14) + 8));
                                        uint8_t t115 = gm_f_clause_may_exit_7lc6flj((void *)NULL, cx, NULL, t108, t113, t114);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l9 = t115;
                                        goto gm_lb24;
                                    }
gm_lb25:;
                                }
gm_lb24:;
                            }
                            goto gm_lb22;
                        }
gm_lb23:;
                    }
gm_lb22:;
                    goto gm_lb18;
                }
gm_lb21:;
                gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 846, 13);
            }
gm_lb18:;
            uint8_t t116 = gm_l9;
            if (t116) {
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto gm_lb1;
        }
gm_lb17:;
        {
            goto gm_lb1;
        }
gm_lb26:;
        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 829, 5);
    }
gm_lb1:;
    void * t117 = gm_l1;
    gm_closure t118 = ((gm_closure){(void *)gm_f_fn_93mqgd8, (void *)&F.h, cx});
    uint8_t t119 = gm_f_any_child_ffc8h1k(t117, t118);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t119;
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
extern __attribute__((aligned(4))) uint8_t gm_f_fn_93mqgd8(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_foreign_decl_rld7m78(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_foreign_decl_rld7m78 F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* def */
    gm_b16_8 gm_l3; /* lib */
    gm_b16_8 gm_l4; /* link_name */
    uint8_t gm_l5; /* variadic */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint32_t gm_l9; /* p */
    gm_b16_8 gm_l10; /* ret */
    GM_STACK_CHECK(&F, 1234u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l11;
    gm_t5o0tpv5jka2l gm_l12;
    gm_tftb1odc739t5 gm_l13;
    gm_tftb1odc739t5 gm_l14;
    gm_tftb1odc739t5 gm_l15;
    gm_b16_8 gm_l16;
    gm_t5o0tpv5jka2l gm_l17;
    gm_tabb710d9mq2e gm_l18;
    gm_tftb1odc739t5 gm_l19;
    void * t11 = gm_l0;
    void * t12 = GM_LD(void *, ((uint8_t *)t11));
    uint32_t t13 = gm_l1;
    void * t14 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t12, t13);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t14;
    {
        {
            void * t15 = gm_l2;
            uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t15) + 96) + 0));
            uint8_t t17 = (uint8_t)(t16 == ((uint8_t)3ULL));
            if (!t17) {
                goto gm_lb2;
            }
            gm_b16_8 t18 = GM_LD(gm_b16_8, ((((uint8_t *)t15) + 96) + 8));
            gm_l3 = t18;
            gm_b16_8 t19 = GM_LD(gm_b16_8, (((((uint8_t *)t15) + 96) + 8) + 16));
            gm_l4 = t19;
            uint8_t t20 = GM_LD(uint8_t, (((((uint8_t *)t15) + 96) + 8) + 32));
            gm_l5 = t20;
            {
                {
                    gm_b16_8 t21 = gm_l3;
                    gm_b16_8 t22;
                    GM_ST(void *, (uint8_t *)&t22, (void *)(gm_s2gnpj7k4l22n));
                    GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(1));
                    gm_b16_8 t23;
                    GM_ST(void *, (uint8_t *)&t23, (void *)(gm_strca8t49bpqu));
                    GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(1));
                    gm_t6j6smj7qoctu t24;
                    GM_ST(gm_b16_8, ((uint8_t *)&t24 + 0), t22);
                    GM_ST(gm_b16_8, ((uint8_t *)&t24 + 16), t23);
                    uint8_t t25 = 0;
                    if (gm_eq0iqiubfm7e(((uint8_t *)&t24) + 0, ((uint8_t *)&t21))) t25 = 1;
                    if (gm_eq0iqiubfm7e(((uint8_t *)&t24) + 16, ((uint8_t *)&t21))) t25 = 1;
                    uint8_t t26 = (uint8_t)(!t25);
                    if (!t26) {
                        goto gm_lb4;
                    }
                    gm_b16_8 t27 = gm_l3;
                    void * t28 = gm_l0;
                    gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)t28) + 3240));
                    uint8_t t30 = 0;
                    for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t29) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t29))) + i * 16, ((uint8_t *)&t27))) { t30 = 1; break; }
                    uint8_t t31 = (uint8_t)(!t30);
                    if (!t31) {
                        goto gm_lb4;
                    }
                    void * t32 = gm_l0;
                    void * t33 = (void *)(((uint8_t *)t32) + 3240);
                    gm_b16_8 t34 = gm_l3;
                    gm_tftb1odc739t5 t35 = gm_f_push_3ef746b(t33, t34);
                    gm_l11 = t35;
                    uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                    if (!t36) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srcd5arorrf8jkg, 935, 60);
                    goto gm_lb3;
                }
gm_lb4:;
            }
gm_lb3:;
            {
                gm_trqfvmjtt4sij t37;
                gm_f_list_epvducg((void *)&t37, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l6 = t37;
            }
            void * t38 = gm_l2;
            gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)t38) + 32));
            gm_l7 = t39;
            gm_l8 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t40 = gm_l8;
                    uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    uint8_t t42 = (t40 < t41);
                    if (!t42) {
                        goto gm_lb5;
                    }
                    {
                        void * t43 = GM_LD(void *, ((uint8_t *)&gm_l7));
                        uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                        uint64_t t45 = gm_l8;
                        {
                            {
                                uint32_t t46 = GM_LD(uint32_t, (((uint8_t *)t43) + (uintptr_t)t45 * 4));
                                gm_l9 = t46;
                                void * t47 = (void *)((uint8_t *)&F.l6);
                                void * t48 = gm_l0;
                                void * t49 = gm_l2;
                                void * t50 = GM_LD(void *, (((uint8_t *)t49) + 48));
                                uint64_t t51 = GM_LD(uint64_t, ((((uint8_t *)t49) + 48) + 8));
                                uint32_t t52 = gm_l9;
                                uint64_t t53 = (uint64_t)t52;
                                if ((uintptr_t)t53 >= (uintptr_t)(t51)) gm_trap_index((uintptr_t)t53, t51, gm_srcd5arorrf8jkg, 937, 56);
                                gm_t88rcucg1b7uk t54 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t50) + (uintptr_t)t53 * 40) + 16));
                                void * t55 = gm_l2;
                                gm_tl874t2atnlh3 t56 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t55) + 16));
                                gm_f_c_abi_type_nu2c2tm((void *)&gm_l12, (void *)NULL, cx, NULL, t48, t54, t56);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                                if (t57) {
                                } else {
                                    gm_t08olq97rscn3 t58 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l12) + 8));
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 4, 0);
                                    ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 1;
                                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8), t58);
                                    goto gm_fdone;
                                }
                                gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
                                gm_tftb1odc739t5 t60 = gm_f_push_3ef746b(t47, t59);
                                gm_l13 = t60;
                                uint8_t t61 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                                if (!t61) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srcd5arorrf8jkg, 937, 27);
                                goto gm_lb8;
                            }
gm_lb9:;
                            gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 937, 5);
                        }
gm_lb8:;
                    }
gm_lb7:;
                    uint64_t t62 = gm_l8;
                    uint64_t r63; bool o64 = __builtin_add_overflow(t62, ((uint64_t)1ULL), &r63);
                    uint64_t t65 = r63;
                    gm_l8 = t65;
                }
            }
gm_lb5:;
            uint8_t t66 = gm_l5;
            if (t66) {
                void * t67 = (void *)((uint8_t *)&F.l6);
                gm_b16_8 t68;
                GM_ST(void *, (uint8_t *)&t68, (void *)(gm_soqt3jnmovq5l));
                GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(3));
                gm_tftb1odc739t5 t69 = gm_f_push_3ef746b(t67, t68);
                gm_l14 = t69;
                uint8_t t70 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                if (!t70) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_srcd5arorrf8jkg, 938, 19);
            }
            uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
            uint8_t t72 = ((t71 == ((uint64_t)0ULL)));
            if (t72) {
                void * t73 = (void *)((uint8_t *)&F.l6);
                gm_b16_8 t74;
                GM_ST(void *, (uint8_t *)&t74, (void *)(gm_sts5j6f5pkov0));
                GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(4));
                gm_tftb1odc739t5 t75 = gm_f_push_3ef746b(t73, t74);
                gm_l15 = t75;
                uint8_t t76 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                if (!t76) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srcd5arorrf8jkg, 939, 32);
            }
            void * t77 = gm_l0;
            void * t78 = (void *)(((uint8_t *)t77) + 8);
            void * t79 = gm_l2;
            gm_t88rcucg1b7uk t80 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t79) + 64));
            uint8_t t81 = gm_f_is_never_in5ftma(t78, GM_CONV(gm_csguankko4lmhd, t80));
            if (t81) {
                gm_b16_8 t82;
                GM_ST(void *, (uint8_t *)&t82, (void *)(gm_shlk3g0tk7m4f));
                GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(30));
                gm_l10 = t82;
            } else {
                void * t83 = gm_l0;
                void * t84 = (void *)(((uint8_t *)t83) + 8);
                void * t85 = gm_l2;
                gm_t88rcucg1b7uk t86 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t85) + 64));
                uint8_t t87 = gm_f_is_unit_17uennd(t84, GM_CONV(gm_csguankko4lmhd, t86));
                if (t87) {
                    gm_b16_8 t88;
                    GM_ST(void *, (uint8_t *)&t88, (void *)(gm_sts5j6f5pkov0));
                    GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(4));
                    gm_l16 = t88;
                } else {
                    void * t89 = gm_l0;
                    void * t90 = gm_l2;
                    gm_t88rcucg1b7uk t91 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t90) + 64));
                    void * t92 = gm_l2;
                    gm_tl874t2atnlh3 t93 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t92) + 16));
                    gm_f_c_abi_type_nu2c2tm((void *)&gm_l17, (void *)NULL, cx, NULL, t89, t91, t93);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t94 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                    if (t94) {
                    } else {
                        gm_t08olq97rscn3 t95 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l17) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8), t95);
                        goto gm_fdone;
                    }
                    gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
                    gm_l16 = t96;
                }
                gm_b16_8 t97 = gm_l16;
                gm_l10 = t97;
            }
            {
                void * t98 = gm_l0;
                void * t99 = (void *)(((uint8_t *)t98) + 1944);
                void * t100 = gm_l2;
                uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)t100) + 144));
                gm_b16_8 t102 = gm_f_conv_attr_m0b89pc(t101);
                gm_b16_8 t103 = gm_l10;
                void * t104 = gm_l0;
                uint32_t t105 = gm_l1;
                gm_b16_8 t106 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t104, t105);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t107 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
                gm_b16_8 t108 = GM_LD(gm_b16_8, ((uint8_t *)&t107));
                gm_b16_8 t109;
                GM_ST(void *, (uint8_t *)&t109, (void *)(gm_sg5gdnrith2i6));
                GM_ST(uintptr_t, (uint8_t *)&t109 + 8, (uintptr_t)(2));
                gm_f_join_sfknljb((void *)&gm_l18, (void *)NULL, cx, NULL, t108, t109);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t110 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                if (!t110) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_srcd5arorrf8jkg, 947, 106);
                gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
                gm_b16_8 t112 = gm_l4;
                gm_b16_8 t113 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t112);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tud7oovfvnf43 t114;
                GM_ST(gm_b16_8, ((uint8_t *)&t114 + 0), t102);
                GM_ST(gm_b16_8, ((uint8_t *)&t114 + 16), t103);
                GM_ST(gm_b16_8, ((uint8_t *)&t114 + 32), t106);
                GM_ST(gm_b16_8, ((uint8_t *)&t114 + 48), t111);
                GM_ST(gm_b16_8, ((uint8_t *)&t114 + 64), t113);
                gm_tftb1odc739t5 t115 = gm_f_write_u3br5ci(t99, &t114);
                gm_l19 = t115;
            }
            uint8_t t116 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
            if (!t116) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_srcd5arorrf8jkg, 947, 5);
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("reached `@unreachable()`", gm_srcd5arorrf8jkg, 934, 57);
    }
gm_lb1:;
    memset(&(*(gm_tqgtd5gtgj65s *)gm_out), 0, sizeof (*(gm_tqgtd5gtgj65s *)gm_out));
    ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8), GM_ZERO(gm_b0_1));
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
inline __attribute__((aligned(4))) uint8_t gm_f_frame_done_014nc4i(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_frame_done_014nc4i F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    uint64_t gm_l2; /* i */
    uint8_t gm_l3; /* done */
    GM_STACK_CHECK(&F, 147u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l4;
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    gm_l2 = t6;
    {
        for (;;) {
            void * t7 = gm_l0;
            uint64_t t8 = GM_LD(uint64_t, ((((uint8_t *)t7) + 2408) + 8));
            uint64_t t9 = gm_l2;
            uint8_t t10 = (t8 <= t9);
            if (!t10) {
                goto gm_lb1;
            }
            {
                void * t11 = gm_l0;
                void * t12 = (void *)(((uint8_t *)t11) + 2408);
                gm_tftb1odc739t5 t13 = gm_f_push_vd1290p(t12, ((uint8_t)0));
                gm_l4 = t13;
                uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
                if (!t14) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srcd5arorrf8jkg, 485, 42);
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t15 = gm_l0;
    void * t16 = GM_LD(void *, (((uint8_t *)t15) + 2408));
    uint64_t t17 = GM_LD(uint64_t, ((((uint8_t *)t15) + 2408) + 8));
    uint64_t t18 = gm_l2;
    if ((uintptr_t)t18 >= (uintptr_t)(t17)) gm_trap_index((uintptr_t)t18, t17, gm_srcd5arorrf8jkg, 486, 13);
    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)t16) + (uintptr_t)t18 * 1));
    gm_l3 = t19;
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, (((uint8_t *)t20) + 2408));
    uint64_t t22 = GM_LD(uint64_t, ((((uint8_t *)t20) + 2408) + 8));
    uint64_t t23 = gm_l2;
    if ((uintptr_t)t23 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t23, t22, gm_srcd5arorrf8jkg, 487, 5);
    GM_ST(uint8_t, (((uint8_t *)t21) + (uintptr_t)t23 * 1), ((uint8_t)1));
    uint8_t t24 = gm_l3;
    F.ret = t24;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_gen_entry_hj4odev(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint64_t a2) {
    struct gm_fr_gen_entry_hj4odev F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* entry */
    uint64_t gm_l2; /* n */
    gm_t88rcucg1b7uk gm_l3; /* ret */
    gm_b16_8 gm_l4; /* rc */
    gm_b16_8 gm_l6; /* hidden */
    gm_t88rcucg1b7uk gm_l7; /* err */
    gm_t88rcucg1b7uk gm_l8; /* ok */
    gm_b16_8 gm_l9; /* offs */
    gm_b16_8 gm_l10; /* d */
    uint32_t gm_l11; /* id */
    gm_b16_8 gm_l12; /* name */
    GM_STACK_CHECK(&F, 1712u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l13;
    gm_tftb1odc739t5 gm_l14;
    gm_tftb1odc739t5 gm_l15;
    gm_tftb1odc739t5 gm_l16;
    gm_tftb1odc739t5 gm_l17;
    gm_tftb1odc739t5 gm_l18;
    gm_tftb1odc739t5 gm_l19;
    gm_t53at80pfdus3 gm_l20;
    gm_tftb1odc739t5 gm_l21;
    gm_tftb1odc739t5 gm_l22;
    gm_t53at80pfdus3 gm_l23;
    uint8_t gm_l24;
    gm_tftb1odc739t5 gm_l25;
    gm_tftb1odc739t5 gm_l26;
    gm_tftb1odc739t5 gm_l27;
    gm_tftb1odc739t5 gm_l28;
    gm_tftb1odc739t5 gm_l29;
    gm_tftb1odc739t5 gm_l30;
    gm_tftb1odc739t5 gm_l31;
    gm_tftb1odc739t5 gm_l32;
    gm_tftb1odc739t5 gm_l33;
    {
        void * t10 = gm_l0;
        void * t11 = GM_LD(void *, ((uint8_t *)t10));
        uint32_t t12 = gm_l1;
        void * t13 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t13) + 64));
        gm_l3 = t14;
        void * t15 = gm_l0;
        void * t16 = (void *)(((uint8_t *)t15) + 8);
        gm_t88rcucg1b7uk t17 = gm_l3;
        gm_b16_8 t18 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t18;
        gm_trqfvmjtt4sij t19;
        gm_f_list_uvfu1k1((void *)&t19, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l5 = t19;
        void * t20 = (void *)((uint8_t *)&F.l5);
        uint64_t t21 = gm_l2;
        gm_t4ce0jd3tdro4 t22;
        GM_ST(uint64_t, ((uint8_t *)&t22 + 0), t21);
        gm_tftb1odc739t5 t23 = gm_f_write_6rjcu4v(t20, t22);
        gm_l13 = t23;
        uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
        if (!t24) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srcd5arorrf8jkg, 758, 5);
        void * t25 = gm_l0;
        void * t26 = (void *)(((uint8_t *)t25) + 8);
        gm_t88rcucg1b7uk t27 = gm_l3;
        uint8_t t28 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t26, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (t28) {
            void * t29 = gm_l0;
            uint32_t t30 = gm_l1;
            uint8_t t31 = gm_f_is_plain_qr08i40((void *)NULL, cx, NULL, t29, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (t31) {
                gm_b16_8 t32;
                GM_ST(void *, (uint8_t *)&t32, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(0));
                gm_l6 = t32;
            } else {
                gm_b16_8 t33;
                GM_ST(void *, (uint8_t *)&t33, (void *)(gm_s34o1ojfv2p3d));
                GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(16));
                gm_l6 = t33;
            }
            void * t34 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t35 = gm_l4;
            void * t36 = gm_l0;
            uint32_t t37 = gm_l1;
            gm_b16_8 t38 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t36, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t39 = gm_l6;
            gm_tfeab279c16rr t40;
            GM_ST(gm_b16_8, ((uint8_t *)&t40 + 0), t35);
            GM_ST(gm_b16_8, ((uint8_t *)&t40 + 16), t38);
            GM_ST(gm_b16_8, ((uint8_t *)&t40 + 32), t39);
            gm_tftb1odc739t5 t41 = gm_f_write_u8u1amh(t34, &t40);
            gm_l14 = t41;
            uint8_t t42 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
            if (!t42) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_srcd5arorrf8jkg, 761, 9);
        } else {
            void * t43 = gm_l0;
            uint32_t t44 = gm_l1;
            uint8_t t45 = gm_f_is_plain_qr08i40((void *)NULL, cx, NULL, t43, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (t45) {
                void * t46 = gm_l0;
                void * t47 = (void *)(((uint8_t *)t46) + 8);
                gm_t88rcucg1b7uk t48 = gm_l3;
                uint8_t t49 = gm_f_is_void_vs2outo(t47, GM_CONV(gm_csguankko4lmhd, t48));
                if (t49) {
                    void * t50 = (void *)((uint8_t *)&F.l5);
                    void * t51 = gm_l0;
                    uint32_t t52 = gm_l1;
                    gm_b16_8 t53 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t51, t52);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t54 = gm_l4;
                    gm_b16_8 t55 = gm_l4;
                    gm_tfeab279c16rr t56;
                    GM_ST(gm_b16_8, ((uint8_t *)&t56 + 0), t53);
                    GM_ST(gm_b16_8, ((uint8_t *)&t56 + 16), t54);
                    GM_ST(gm_b16_8, ((uint8_t *)&t56 + 32), t55);
                    gm_tftb1odc739t5 t57 = gm_f_write_c5hlb1u(t50, &t56);
                    gm_l15 = t57;
                    uint8_t t58 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                    if (!t58) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srcd5arorrf8jkg, 764, 13);
                } else {
                    void * t59 = (void *)((uint8_t *)&F.l5);
                    gm_b16_8 t60 = gm_l4;
                    void * t61 = gm_l0;
                    uint32_t t62 = gm_l1;
                    gm_b16_8 t63 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t61, t62);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t6j6smj7qoctu t64;
                    GM_ST(gm_b16_8, ((uint8_t *)&t64 + 0), t60);
                    GM_ST(gm_b16_8, ((uint8_t *)&t64 + 16), t63);
                    gm_tftb1odc739t5 t65 = gm_f_write_8o29aab(t59, &t64);
                    gm_l16 = t65;
                    uint8_t t66 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                    if (!t66) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_srcd5arorrf8jkg, 766, 13);
                }
            } else {
                void * t67 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t68 = gm_l4;
                void * t69 = gm_l0;
                uint32_t t70 = gm_l1;
                gm_b16_8 t71 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t69, t70);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t6j6smj7qoctu t72;
                GM_ST(gm_b16_8, ((uint8_t *)&t72 + 0), t68);
                GM_ST(gm_b16_8, ((uint8_t *)&t72 + 16), t71);
                gm_tftb1odc739t5 t73 = gm_f_write_usci3o4(t67, &t72);
                gm_l17 = t73;
                uint8_t t74 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                if (!t74) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srcd5arorrf8jkg, 769, 9);
            }
        }
    }
    {
        void * t75 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t76;
        GM_ST(void *, (uint8_t *)&t76, (void *)(gm_scrjehcehrfpv));
        GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(14));
        gm_tftb1odc739t5 t77 = gm_f_extend_0cs85gs(t75, t76);
        gm_l18 = t77;
        uint8_t t78 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
        if (!t78) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_srcd5arorrf8jkg, 771, 5);
        void * t79 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t80;
        GM_ST(void *, (uint8_t *)&t80, (void *)(gm_s698sbcv3nmjk));
        GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(68));
        gm_tftb1odc739t5 t81 = gm_f_extend_0cs85gs(t79, t80);
        gm_l19 = t81;
        uint8_t t82 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
        if (!t82) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_srcd5arorrf8jkg, 772, 5);
        void * t83 = gm_l0;
        void * t84 = (void *)(((uint8_t *)t83) + 8);
        gm_t88rcucg1b7uk t85 = gm_l3;
        gm_f_kind_ghvav22((void *)&gm_l20, t84, GM_CONV(gm_csguankko4lmhd, t85));
        {
            {
                uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&gm_l20) + 0));
                uint8_t t87 = (uint8_t)(t86 == ((uint8_t)14ULL));
                if (!t87) {
                    goto gm_lb2;
                }
                gm_t88rcucg1b7uk t88 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l20) + 8));
                gm_l7 = t88;
                gm_t88rcucg1b7uk t89 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l20) + 8) + 4));
                gm_l8 = t89;
                void * t90 = gm_l0;
                void * t91 = (void *)(((uint8_t *)t90) + 8);
                gm_t88rcucg1b7uk t92 = gm_l3;
                gm_b16_8 t93 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t91, t92);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l9 = t93;
                void * t94 = gm_l0;
                void * t95 = (void *)(((uint8_t *)t94) + 8);
                gm_t88rcucg1b7uk t96 = gm_l7;
                gm_b16_8 t97 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t95, t96);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l10 = t97;
                void * t98 = (void *)((uint8_t *)&F.l5);
                void * t99 = GM_LD(void *, ((uint8_t *)&gm_l9));
                uint64_t t100 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t100)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t100, gm_srcd5arorrf8jkg, 777, 58);
                uint64_t t101 = GM_LD(uint64_t, (((uint8_t *)t99) + (uintptr_t)((uint64_t)0ULL) * 8));
                gm_t4ce0jd3tdro4 t102;
                GM_ST(uint64_t, ((uint8_t *)&t102 + 0), t101);
                gm_tftb1odc739t5 t103 = gm_f_write_sf9mjak(t98, t102);
                gm_l21 = t103;
                uint8_t t104 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                if (!t104) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srcd5arorrf8jkg, 777, 13);
                void * t105 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t106;
                GM_ST(void *, (uint8_t *)&t106, (void *)(gm_sreie3avjjb9l));
                GM_ST(uintptr_t, (uint8_t *)&t106 + 8, (uintptr_t)(63));
                gm_tftb1odc739t5 t107 = gm_f_extend_0cs85gs(t105, t106);
                gm_l22 = t107;
                uint8_t t108 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                if (!t108) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 1, gm_srcd5arorrf8jkg, 778, 13);
                {
                    {
                        {
                            void * t109 = gm_l0;
                            void * t110 = (void *)(((uint8_t *)t109) + 8);
                            gm_t88rcucg1b7uk t111 = gm_l7;
                            gm_f_kind_ghvav22((void *)&gm_l23, t110, GM_CONV(gm_csguankko4lmhd, t111));
                        }
                        uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&gm_l23) + 0));
                        uint8_t t113 = (uint8_t)(t112 == ((uint8_t)17ULL));
                        if (!t113) {
                            goto gm_lb4;
                        }
                        uint32_t t114 = GM_LD(uint32_t, (((uint8_t *)&gm_l23) + 8));
                        gm_l11 = t114;
                        void * t115 = gm_l0;
                        void * t116 = GM_LD(void *, ((uint8_t *)t115));
                        uint32_t t117 = gm_l11;
                        void * t118 = gm_f_adt_mpqv8qe(t116, t117);
                        {
                            {
                                uint8_t t119 = GM_LD(uint8_t, ((((uint8_t *)t118) + 24) + 0));
                                uint8_t t120 = (uint8_t)(t119 == ((uint8_t)1ULL));
                                if (!t120) {
                                    goto gm_lb6;
                                }
                                gm_l24 = ((uint8_t)1);
                                goto gm_lb5;
                            }
gm_lb6:;
                            {
                                gm_l24 = ((uint8_t)0);
                                goto gm_lb5;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 780, 50);
                        }
gm_lb5:;
                        uint8_t t121 = gm_l24;
                        if (!t121) {
                            goto gm_lb4;
                        }
                        void * t122 = gm_l0;
                        void * t123 = (void *)(((uint8_t *)t122) + 8);
                        gm_t88rcucg1b7uk t124 = gm_l7;
                        gm_b16_8 t125 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t123, t124);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l12 = t125;
                        {
                            void * t126 = (void *)((uint8_t *)&F.l5);
                            gm_b16_8 t127 = gm_l12;
                            gm_b16_8 t128 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t127);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint64_t t129 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                            gm_t3877pk7lv748 t130;
                            GM_ST(gm_b16_8, ((uint8_t *)&t130 + 0), t128);
                            GM_ST(uint64_t, ((uint8_t *)&t130 + 16), t129);
                            gm_tftb1odc739t5 t131 = gm_f_write_9falfvm(t126, &t130);
                            gm_l25 = t131;
                        }
                        uint8_t t132 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                        if (!t132) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_srcd5arorrf8jkg, 782, 17);
                        goto gm_lb3;
                    }
gm_lb4:;
                }
gm_lb3:;
                void * t133 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t134 = gm_l10;
                void * t135 = GM_LD(void *, ((uint8_t *)&gm_l9));
                uint64_t t136 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t136)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t136, gm_srcd5arorrf8jkg, 784, 82);
                uint64_t t137 = GM_LD(uint64_t, (((uint8_t *)t135) + (uintptr_t)((uint64_t)1ULL) * 8));
                gm_t3877pk7lv748 t138;
                GM_ST(gm_b16_8, ((uint8_t *)&t138 + 0), t134);
                GM_ST(uint64_t, ((uint8_t *)&t138 + 16), t137);
                gm_tftb1odc739t5 t139 = gm_f_write_kdsk4q9(t133, &t138);
                gm_l26 = t139;
                uint8_t t140 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                if (!t140) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_srcd5arorrf8jkg, 784, 13);
                void * t141 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t142;
                GM_ST(void *, (uint8_t *)&t142, (void *)(gm_sucsarpa3psmp));
                GM_ST(uintptr_t, (uint8_t *)&t142 + 8, (uintptr_t)(54));
                gm_tftb1odc739t5 t143 = gm_f_extend_0cs85gs(t141, t142);
                gm_l27 = t143;
                uint8_t t144 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                if (!t144) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 1, gm_srcd5arorrf8jkg, 785, 13);
                void * t145 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t146;
                GM_ST(void *, (uint8_t *)&t146, (void *)(gm_srjb0lv60gms5));
                GM_ST(uintptr_t, (uint8_t *)&t146 + 8, (uintptr_t)(61));
                gm_tftb1odc739t5 t147 = gm_f_extend_0cs85gs(t145, t146);
                gm_l28 = t147;
                uint8_t t148 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                if (!t148) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 1, gm_srcd5arorrf8jkg, 786, 13);
                void * t149 = gm_l0;
                void * t150 = (void *)(((uint8_t *)t149) + 8);
                gm_t88rcucg1b7uk t151 = gm_l8;
                uint8_t t152 = gm_f_is_integer_eo6jkc5(t150, GM_CONV(gm_csguankko4lmhd, t151));
                if (t152) {
                    void * t153 = (void *)((uint8_t *)&F.l5);
                    void * t154 = gm_l0;
                    void * t155 = (void *)(((uint8_t *)t154) + 8);
                    gm_t88rcucg1b7uk t156 = gm_l8;
                    gm_b16_8 t157 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t155, t156);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t158 = GM_LD(void *, ((uint8_t *)&gm_l9));
                    uint64_t t159 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t159)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t159, gm_srcd5arorrf8jkg, 788, 112);
                    uint64_t t160 = GM_LD(uint64_t, (((uint8_t *)t158) + (uintptr_t)((uint64_t)1ULL) * 8));
                    gm_t3877pk7lv748 t161;
                    GM_ST(gm_b16_8, ((uint8_t *)&t161 + 0), t157);
                    GM_ST(uint64_t, ((uint8_t *)&t161 + 16), t160);
                    gm_tftb1odc739t5 t162 = gm_f_write_08b77ko(t153, &t161);
                    gm_l29 = t162;
                    uint8_t t163 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                    if (!t163) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_srcd5arorrf8jkg, 788, 17);
                } else {
                    void * t164 = (void *)((uint8_t *)&F.l5);
                    gm_b16_8 t165;
                    GM_ST(void *, (uint8_t *)&t165, (void *)(gm_sar77rocuptb5));
                    GM_ST(uintptr_t, (uint8_t *)&t165 + 8, (uintptr_t)(30));
                    gm_tftb1odc739t5 t166 = gm_f_extend_0cs85gs(t164, t165);
                    gm_l30 = t166;
                    uint8_t t167 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
                    if (!t167) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 1, gm_srcd5arorrf8jkg, 790, 17);
                }
                goto gm_lb1;
            }
gm_lb2:;
            {
                void * t168 = gm_l0;
                void * t169 = (void *)(((uint8_t *)t168) + 8);
                gm_t88rcucg1b7uk t170 = gm_l3;
                uint8_t t171 = gm_f_is_integer_eo6jkc5(t169, GM_CONV(gm_csguankko4lmhd, t170));
                if (t171) {
                    void * t172 = (void *)((uint8_t *)&F.l5);
                    gm_b16_8 t173;
                    GM_ST(void *, (uint8_t *)&t173, (void *)(gm_sasi3vvkcdvo0));
                    GM_ST(uintptr_t, (uint8_t *)&t173 + 8, (uintptr_t)(39));
                    gm_tftb1odc739t5 t174 = gm_f_extend_0cs85gs(t172, t173);
                    gm_l31 = t174;
                    uint8_t t175 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                    if (!t175) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 1, gm_srcd5arorrf8jkg, 795, 17);
                } else {
                    void * t176 = (void *)((uint8_t *)&F.l5);
                    gm_b16_8 t177;
                    GM_ST(void *, (uint8_t *)&t177, (void *)(gm_sekmbu83qkg9c));
                    GM_ST(uintptr_t, (uint8_t *)&t177 + 8, (uintptr_t)(43));
                    gm_tftb1odc739t5 t178 = gm_f_extend_0cs85gs(t176, t177);
                    gm_l32 = t178;
                    uint8_t t179 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                    if (!t179) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 1, gm_srcd5arorrf8jkg, 797, 17);
                }
                goto gm_lb1;
            }
gm_lb8:;
            gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 773, 5);
        }
gm_lb1:;
    }
    {
        void * t180 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t181;
        GM_ST(void *, (uint8_t *)&t181, (void *)(gm_sikpqtja8pqpv));
        GM_ST(uintptr_t, (uint8_t *)&t181 + 8, (uintptr_t)(2));
        gm_tftb1odc739t5 t182 = gm_f_extend_0cs85gs(t180, t181);
        gm_l33 = t182;
        uint8_t t183 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
        if (!t183) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 1, gm_srcd5arorrf8jkg, 801, 5);
        gm_b16_8 t184 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
        gm_b16_8 t185 = GM_LD(gm_b16_8, ((uint8_t *)&t184));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t185);
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
__attribute__((aligned(4))) void gm_f_gen_exports_koai3gr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_gen_exports_koai3gr F;
    void * gm_l0; /* g */
    uint8_t gm_l2; /* wasi */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* k */
    gm_b16_8 gm_l6; /* sym */
    uint32_t gm_l7; /* f */
    void * gm_l8; /* def */
    gm_b16_8 gm_l9; /* cname */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* i */
    uint32_t gm_l16; /* p */
    gm_t88rcucg1b7uk gm_l17; /* t */
    gm_b16_8 gm_l18; /* pty */
    gm_b16_8 gm_l19; /* a */
    gm_b16_8 gm_l20; /* call */
    gm_b16_8 gm_l21; /* param_text */
    gm_b16_8 gm_l22; /* start */
    gm_b16_8 gm_l23; /* ret */
    gm_b16_8 gm_l24; /* value */
    gm_b16_8 gm_l25; /* body */
    gm_b16_8 gm_l26; /* result */
    gm_b16_8 gm_l27; /* cc */
    GM_STACK_CHECK(&F, 4014u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tf1bjg81u0glv gm_l28;
    gm_tabb710d9mq2e gm_l29;
    gm_tn68el8d14vku gm_l30;
    gm_t5o0tpv5jka2l gm_l31;
    gm_tabb710d9mq2e gm_l32;
    gm_tftb1odc739t5 gm_l33;
    gm_tabb710d9mq2e gm_l34;
    gm_tabb710d9mq2e gm_l35;
    gm_tftb1odc739t5 gm_l36;
    gm_tftb1odc739t5 gm_l37;
    gm_b16_8 gm_l38;
    uint8_t gm_l39;
    gm_th2lqbo77pvsc gm_l40;
    gm_tabb710d9mq2e gm_l41;
    gm_tftb1odc739t5 gm_l42;
    gm_tabb710d9mq2e gm_l43;
    gm_tabb710d9mq2e gm_l44;
    gm_tabb710d9mq2e gm_l45;
    gm_t5o0tpv5jka2l gm_l46;
    gm_b16_8 gm_l47;
    uint8_t gm_l48;
    gm_th2lqbo77pvsc gm_l49;
    gm_tabb710d9mq2e gm_l50;
    gm_tabb710d9mq2e gm_l51;
    gm_tabb710d9mq2e gm_l52;
    gm_tabb710d9mq2e gm_l53;
    gm_tftb1odc739t5 gm_l54;
    gm_tabb710d9mq2e gm_l55;
    gm_tftb1odc739t5 gm_l56;
    gm_tabb710d9mq2e gm_l57;
    gm_tabb710d9mq2e gm_l58;
    gm_tftb1odc739t5 gm_l59;
    gm_tftb1odc739t5 gm_l60;
    {
        gm_trqfvmjtt4sij t18;
        gm_f_list_uvfu1k1((void *)&t18, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t18;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, ((uint8_t *)t19));
        gm_b16_8 t21 = GM_LD(gm_b16_8, ((((uint8_t *)t20) + 80) + 24));
        gm_b16_8 t22;
        GM_ST(void *, (uint8_t *)&t22, (void *)(gm_svl13be5ca8rt));
        GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(4));
        uint8_t t23 = (gm_eq0iqiubfm7e(((uint8_t *)&t21), ((uint8_t *)&t22)));
        gm_l2 = t23;
        void * t24 = gm_l0;
        void * t25 = GM_LD(void *, ((uint8_t *)t24));
        void * t26 = GM_LD(void *, ((uint8_t *)t25));
        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)t26) + 880));
        gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
        gm_l3 = t28;
        gm_l4 = ((uint64_t)0ULL);
        {
            for (;;) {
                {
                    uint64_t t29 = gm_l4;
                    uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    uint8_t t31 = (t29 < t30);
                    if (!t31) {
                        goto gm_lb1;
                    }
                    {
                        {
                            uint64_t t32 = gm_l4;
                            void * t33 = GM_LD(void *, ((uint8_t *)&gm_l3));
                            uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                            uint64_t t35 = gm_l4;
                            gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)t33) + (uintptr_t)t35 * 16));
                            GM_ST(uint64_t, ((uint8_t *)&gm_l28 + 0), t32);
                            GM_ST(gm_b16_8, ((uint8_t *)&gm_l28 + 8), t36);
                            {
                                {
                                    {
                                        {
                                            uint64_t t37 = GM_LD(uint64_t, ((uint8_t *)&gm_l28));
                                            gm_l5 = t37;
                                            gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l28) + 8));
                                            gm_l6 = t38;
                                            void * t39 = gm_l0;
                                            void * t40 = GM_LD(void *, ((uint8_t *)t39));
                                            void * t41 = GM_LD(void *, ((uint8_t *)t40));
                                            void * t42 = GM_LD(void *, (((uint8_t *)t41) + 920));
                                            uint64_t t43 = GM_LD(uint64_t, ((((uint8_t *)t41) + 920) + 8));
                                            uint64_t t44 = gm_l5;
                                            if ((uintptr_t)t44 >= (uintptr_t)(t43)) gm_trap_index((uintptr_t)t44, t43, gm_srcd5arorrf8jkg, 650, 14);
                                            uint32_t t45 = GM_LD(uint32_t, (((uint8_t *)t42) + (uintptr_t)t44 * 4));
                                            gm_l7 = t45;
                                            void * t46 = gm_l0;
                                            void * t47 = GM_LD(void *, ((uint8_t *)t46));
                                            uint32_t t48 = gm_l7;
                                            void * t49 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t47, t48);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_l8 = t49;
                                            uint64_t t50 = gm_l5;
                                            gm_t4ce0jd3tdro4 t51;
                                            GM_ST(uint64_t, ((uint8_t *)&t51 + 0), t50);
                                            gm_f_format_fpa95lg((void *)&gm_l29, (void *)NULL, cx, NULL, t51);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            uint8_t t52 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                                            if (!t52) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 8, gm_srcd5arorrf8jkg, 654, 18);
                                            gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l29) + 8));
                                            gm_l9 = t53;
                                            gm_trqfvmjtt4sij t54;
                                            gm_f_list_epvducg((void *)&t54, (void *)NULL, cx, NULL);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            F.l10 = t54;
                                            gm_trqfvmjtt4sij t55;
                                            gm_f_list_epvducg((void *)&t55, (void *)NULL, cx, NULL);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            F.l11 = t55;
                                            gm_trqfvmjtt4sij t56;
                                            gm_f_list_uvfu1k1((void *)&t56, (void *)NULL, cx, NULL);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            F.l12 = t56;
                                            void * t57 = gm_l8;
                                            gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)t57) + 32));
                                            gm_l13 = t58;
                                            gm_l14 = ((uint64_t)0ULL);
                                            {
                                                for (;;) {
                                                    uint64_t t59 = gm_l14;
                                                    uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                                                    uint8_t t61 = (t59 < t60);
                                                    if (!t61) {
                                                        goto gm_lb6;
                                                    }
                                                    {
                                                        uint64_t t62 = gm_l14;
                                                        void * t63 = GM_LD(void *, ((uint8_t *)&gm_l13));
                                                        uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                                                        uint64_t t65 = gm_l14;
                                                        uint32_t t66 = GM_LD(uint32_t, (((uint8_t *)t63) + (uintptr_t)t65 * 4));
                                                        gm_tn68el8d14vku t67;
                                                        GM_ST(uint32_t, (uint8_t *)&t67 + 12, 0);
                                                        GM_ST(uint64_t, ((uint8_t *)&t67 + 0), t62);
                                                        GM_ST(uint32_t, ((uint8_t *)&t67 + 8), t66);
                                                        gm_l30 = t67;
                                                        {
                                                            {
                                                                uint64_t t68 = GM_LD(uint64_t, ((uint8_t *)&gm_l30));
                                                                gm_l15 = t68;
                                                                uint32_t t69 = GM_LD(uint32_t, (((uint8_t *)&gm_l30) + 8));
                                                                gm_l16 = t69;
                                                                void * t70 = gm_l8;
                                                                void * t71 = GM_LD(void *, (((uint8_t *)t70) + 48));
                                                                uint64_t t72 = GM_LD(uint64_t, ((((uint8_t *)t70) + 48) + 8));
                                                                uint32_t t73 = gm_l16;
                                                                uint64_t t74 = (uint64_t)t73;
                                                                if ((uintptr_t)t74 >= (uintptr_t)(t72)) gm_trap_index((uintptr_t)t74, t72, gm_srcd5arorrf8jkg, 659, 18);
                                                                gm_t88rcucg1b7uk t75 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t71) + (uintptr_t)t74 * 40) + 16));
                                                                gm_l17 = t75;
                                                                {
                                                                    void * t76 = gm_l0;
                                                                    gm_t88rcucg1b7uk t77 = gm_l17;
                                                                    void * t78 = gm_l8;
                                                                    gm_tl874t2atnlh3 t79 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t78) + 16));
                                                                    gm_f_c_abi_type_nu2c2tm((void *)&gm_l31, (void *)NULL, cx, NULL, t76, t77, t79);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                }
                                                                uint8_t t80 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                                                                if (t80) {
                                                                } else {
                                                                    gm_t08olq97rscn3 t81 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l31) + 8));
                                                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                                                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                                                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                                                                    ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
                                                                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t81);
                                                                    goto gm_fdone;
                                                                }
                                                                gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l31) + 8));
                                                                gm_l18 = t82;
                                                                {
                                                                    void * t83 = (void *)((uint8_t *)&F.l10);
                                                                    gm_b16_8 t84 = gm_l18;
                                                                    uint64_t t85 = gm_l15;
                                                                    gm_t3877pk7lv748 t86;
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t86 + 0), t84);
                                                                    GM_ST(uint64_t, ((uint8_t *)&t86 + 16), t85);
                                                                    gm_f_format_eq87866((void *)&gm_l32, (void *)NULL, cx, NULL, &t86);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    uint8_t t87 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                                                                    if (!t87) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 8, gm_srcd5arorrf8jkg, 661, 25);
                                                                    gm_b16_8 t88 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l32) + 8));
                                                                    gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&t88));
                                                                    gm_tftb1odc739t5 t90 = gm_f_push_3ef746b(t83, t89);
                                                                    gm_l33 = t90;
                                                                }
                                                                uint8_t t91 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                                                                if (!t91) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 1, gm_srcd5arorrf8jkg, 661, 13);
                                                                {
                                                                    uint64_t t92 = gm_l15;
                                                                    gm_t4ce0jd3tdro4 t93;
                                                                    GM_ST(uint64_t, ((uint8_t *)&t93 + 0), t92);
                                                                    gm_f_format_l05am3t((void *)&gm_l34, (void *)NULL, cx, NULL, t93);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                }
                                                                uint8_t t94 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                                                                if (!t94) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 8, gm_srcd5arorrf8jkg, 662, 18);
                                                                gm_b16_8 t95 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l34) + 8));
                                                                gm_l19 = t95;
                                                                uint8_t t96 = gm_l2;
                                                                if (t96) {
                                                                    {
                                                                        uint64_t t97 = gm_l5;
                                                                        uint64_t t98 = gm_l15;
                                                                        gm_t2lqu8jsj511d t99;
                                                                        GM_ST(uint64_t, ((uint8_t *)&t99 + 0), t97);
                                                                        GM_ST(uint64_t, ((uint8_t *)&t99 + 8), t98);
                                                                        gm_f_format_6vickkk((void *)&gm_l35, (void *)NULL, cx, NULL, t99);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                    }
                                                                    uint8_t t100 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
                                                                    if (!t100) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 8, gm_srcd5arorrf8jkg, 664, 21);
                                                                    gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l35) + 8));
                                                                    gm_l19 = t101;
                                                                    {
                                                                        void * t102 = (void *)((uint8_t *)&F.l1);
                                                                        gm_b16_8 t103 = gm_l18;
                                                                        gm_b16_8 t104 = gm_l19;
                                                                        gm_t6j6smj7qoctu t105;
                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t105 + 0), t103);
                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t105 + 16), t104);
                                                                        gm_tftb1odc739t5 t106 = gm_f_write_2uh64vr(t102, &t105);
                                                                        gm_l36 = t106;
                                                                    }
                                                                    uint8_t t107 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
                                                                    if (!t107) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 1, gm_srcd5arorrf8jkg, 665, 17);
                                                                    {
                                                                        void * t108 = (void *)((uint8_t *)&F.l12);
                                                                        gm_b16_8 t109 = gm_l19;
                                                                        uint64_t t110 = gm_l15;
                                                                        gm_t3877pk7lv748 t111;
                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t111 + 0), t109);
                                                                        GM_ST(uint64_t, ((uint8_t *)&t111 + 16), t110);
                                                                        gm_tftb1odc739t5 t112 = gm_f_write_4uefpre(t108, &t111);
                                                                        gm_l37 = t112;
                                                                    }
                                                                    uint8_t t113 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
                                                                    if (!t113) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 1, gm_srcd5arorrf8jkg, 666, 17);
                                                                }
                                                                {
                                                                    void * t114 = (void *)((uint8_t *)&F.l11);
                                                                    void * t115 = gm_l0;
                                                                    void * t116 = (void *)(((uint8_t *)t115) + 8);
                                                                    gm_t88rcucg1b7uk t117 = gm_l17;
                                                                    gm_f_mirror_lcjegsf((void *)&gm_l40, (void *)NULL, cx, NULL, t116, t117);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    {
                                                                        {
                                                                            uint8_t t118 = (uint8_t)((((uint8_t *)&gm_l40))[16] != 0);
                                                                            if (t118) {
                                                                                goto gm_lb12;
                                                                            }
                                                                            gm_l39 = ((uint8_t)0);
                                                                            goto gm_lb11;
                                                                        }
gm_lb12:;
                                                                        {
                                                                            gm_l39 = ((uint8_t)1);
                                                                            goto gm_lb11;
                                                                        }
gm_lb13:;
                                                                        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 669, 26);
                                                                    }
gm_lb11:;
                                                                    uint8_t t119 = gm_l39;
                                                                    if (t119) {
                                                                        gm_b16_8 t120 = gm_l19;
                                                                        gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&t120));
                                                                        gm_l38 = t121;
                                                                    } else {
                                                                        {
                                                                            void * t122 = gm_l0;
                                                                            void * t123 = (void *)(((uint8_t *)t122) + 8);
                                                                            gm_t88rcucg1b7uk t124 = gm_l17;
                                                                            gm_b16_8 t125 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t123, t124);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_lad;
                                                                            }
                                                                            gm_b16_8 t126 = gm_l19;
                                                                            gm_t6j6smj7qoctu t127;
                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t127 + 0), t125);
                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t127 + 16), t126);
                                                                            gm_f_format_or32na5((void *)&gm_l41, (void *)NULL, cx, NULL, &t127);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_lad;
                                                                            }
                                                                        }
                                                                        uint8_t t128 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                                                                        if (!t128) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 8, gm_srcd5arorrf8jkg, 669, 66);
                                                                        gm_b16_8 t129 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l41) + 8));
                                                                        gm_b16_8 t130 = GM_LD(gm_b16_8, ((uint8_t *)&t129));
                                                                        gm_l38 = t130;
                                                                    }
                                                                    gm_b16_8 t131 = gm_l38;
                                                                    gm_tftb1odc739t5 t132 = gm_f_push_3ef746b(t114, t131);
                                                                    gm_l42 = t132;
                                                                }
                                                                uint8_t t133 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
                                                                if (!t133) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l42) + 1, gm_srcd5arorrf8jkg, 669, 13);
                                                                goto gm_lb9;
                                                            }
gm_lb10:;
                                                            gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 658, 9);
                                                        }
gm_lb9:;
                                                    }
gm_lb8:;
                                                    uint64_t t134 = gm_l14;
                                                    uint64_t r135; bool o136 = __builtin_add_overflow(t134, ((uint64_t)1ULL), &r135);
                                                    uint64_t t137 = r135;
                                                    gm_l14 = t137;
                                                }
                                            }
gm_lb6:;
                                        }
                                        {
                                            void * t138 = gm_l0;
                                            uint32_t t139 = gm_l7;
                                            gm_b16_8 t140 = gm_f_addr_name_g5lrlt3((void *)NULL, cx, NULL, t138, t139);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_b16_8 t141 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
                                            gm_b16_8 t142 = GM_LD(gm_b16_8, ((uint8_t *)&t141));
                                            gm_b16_8 t143;
                                            GM_ST(void *, (uint8_t *)&t143, (void *)(gm_sg5gdnrith2i6));
                                            GM_ST(uintptr_t, (uint8_t *)&t143 + 8, (uintptr_t)(2));
                                            gm_f_join_sfknljb((void *)&gm_l43, (void *)NULL, cx, NULL, t142, t143);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            uint8_t t144 = (uint8_t)!((((uint8_t *)&gm_l43))[0] != 0);
                                            if (!t144) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l43) + 8, gm_srcd5arorrf8jkg, 671, 54);
                                            gm_b16_8 t145 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l43) + 8));
                                            gm_t6j6smj7qoctu t146;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t146 + 0), t140);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t146 + 16), t145);
                                            gm_f_format_99gc605((void *)&gm_l44, (void *)NULL, cx, NULL, &t146);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            uint8_t t147 = (uint8_t)!((((uint8_t *)&gm_l44))[0] != 0);
                                            if (!t147) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l44) + 8, gm_srcd5arorrf8jkg, 671, 17);
                                            gm_b16_8 t148 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l44) + 8));
                                            gm_l20 = t148;
                                            uint64_t t149 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                                            uint8_t t150 = ((t149 == ((uint64_t)0ULL)));
                                            if (t150) {
                                                gm_b16_8 t151;
                                                GM_ST(void *, (uint8_t *)&t151, (void *)(gm_sts5j6f5pkov0));
                                                GM_ST(uintptr_t, (uint8_t *)&t151 + 8, (uintptr_t)(4));
                                                gm_l21 = t151;
                                            } else {
                                                gm_b16_8 t152 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                                                gm_b16_8 t153 = GM_LD(gm_b16_8, ((uint8_t *)&t152));
                                                gm_b16_8 t154;
                                                GM_ST(void *, (uint8_t *)&t154, (void *)(gm_sg5gdnrith2i6));
                                                GM_ST(uintptr_t, (uint8_t *)&t154 + 8, (uintptr_t)(2));
                                                gm_f_join_sfknljb((void *)&gm_l45, (void *)NULL, cx, NULL, t153, t154);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t155 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
                                                if (!t155) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l45) + 8, gm_srcd5arorrf8jkg, 672, 73);
                                                gm_b16_8 t156 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l45) + 8));
                                                gm_b16_8 t157 = GM_LD(gm_b16_8, ((uint8_t *)&t156));
                                                gm_l21 = t157;
                                            }
                                            void * t158 = gm_l0;
                                            void * t159 = GM_LD(void *, ((uint8_t *)t158));
                                            gm_b16_8 t160 = GM_LD(gm_b16_8, ((((uint8_t *)t159) + 80) + 24));
                                            gm_b16_8 t161;
                                            GM_ST(void *, (uint8_t *)&t161, (void *)(gm_soo4rtckloi4d));
                                            GM_ST(uintptr_t, (uint8_t *)&t161 + 8, (uintptr_t)(12));
                                            gm_b16_8 t162;
                                            GM_ST(void *, (uint8_t *)&t162, (void *)(gm_svl13be5ca8rt));
                                            GM_ST(uintptr_t, (uint8_t *)&t162 + 8, (uintptr_t)(4));
                                            gm_t6j6smj7qoctu t163;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t163 + 0), t161);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t163 + 16), t162);
                                            uint8_t t164 = 0;
                                            if (gm_eq0iqiubfm7e(((uint8_t *)&t163) + 0, ((uint8_t *)&t160))) t164 = 1;
                                            if (gm_eq0iqiubfm7e(((uint8_t *)&t163) + 16, ((uint8_t *)&t160))) t164 = 1;
                                            uint8_t t165 = (uint8_t)(t164);
                                            if (t165) {
                                                gm_b16_8 t166;
                                                GM_ST(void *, (uint8_t *)&t166, (void *)(gm_sobumja6poqii));
                                                GM_ST(uintptr_t, (uint8_t *)&t166 + 8, (uintptr_t)(12));
                                                gm_l22 = t166;
                                            } else {
                                                gm_b16_8 t167;
                                                GM_ST(void *, (uint8_t *)&t167, (void *)(gm_sr4qbrk6tlgaa));
                                                GM_ST(uintptr_t, (uint8_t *)&t167 + 8, (uintptr_t)(0));
                                                gm_l22 = t167;
                                            }
                                            gm_b16_8 t168;
                                            GM_ST(void *, (uint8_t *)&t168, (void *)(gm_sts5j6f5pkov0));
                                            GM_ST(uintptr_t, (uint8_t *)&t168 + 8, (uintptr_t)(4));
                                            gm_l23 = t168;
                                            gm_b16_8 t169;
                                            GM_ST(void *, (uint8_t *)&t169, (void *)(gm_sr4qbrk6tlgaa));
                                            GM_ST(uintptr_t, (uint8_t *)&t169 + 8, (uintptr_t)(0));
                                            gm_l24 = t169;
                                            void * t170 = gm_l0;
                                            void * t171 = (void *)(((uint8_t *)t170) + 8);
                                            void * t172 = gm_l8;
                                            gm_t88rcucg1b7uk t173 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t172) + 64));
                                            uint8_t t174 = gm_f_is_unit_17uennd(t171, GM_CONV(gm_csguankko4lmhd, t173));
                                            uint8_t t175 = (uint8_t)!t174;
                                            if (t175) {
                                                {
                                                    void * t176 = gm_l0;
                                                    void * t177 = gm_l8;
                                                    gm_t88rcucg1b7uk t178 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t177) + 64));
                                                    void * t179 = gm_l8;
                                                    gm_tl874t2atnlh3 t180 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t179) + 16));
                                                    gm_f_c_abi_type_nu2c2tm((void *)&gm_l46, (void *)NULL, cx, NULL, t176, t178, t180);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                }
                                                uint8_t t181 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
                                                if (t181) {
                                                } else {
                                                    gm_t08olq97rscn3 t182 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l46) + 8));
                                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                                                    ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
                                                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t182);
                                                    goto gm_fdone;
                                                }
                                                gm_b16_8 t183 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l46) + 8));
                                                gm_l23 = t183;
                                                {
                                                    void * t184 = gm_l0;
                                                    void * t185 = (void *)(((uint8_t *)t184) + 8);
                                                    void * t186 = gm_l8;
                                                    gm_t88rcucg1b7uk t187 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t186) + 64));
                                                    gm_f_mirror_lcjegsf((void *)&gm_l49, (void *)NULL, cx, NULL, t185, t187);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                }
                                                {
                                                    {
                                                        uint8_t t188 = (uint8_t)((((uint8_t *)&gm_l49))[16] != 0);
                                                        if (t188) {
                                                            goto gm_lb15;
                                                        }
                                                        gm_l48 = ((uint8_t)0);
                                                        goto gm_lb14;
                                                    }
gm_lb15:;
                                                    {
                                                        gm_l48 = ((uint8_t)1);
                                                        goto gm_lb14;
                                                    }
gm_lb16:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 680, 24);
                                                }
gm_lb14:;
                                                uint8_t t189 = gm_l48;
                                                if (t189) {
                                                    gm_b16_8 t190 = gm_l20;
                                                    gm_b16_8 t191 = GM_LD(gm_b16_8, ((uint8_t *)&t190));
                                                    gm_l47 = t191;
                                                } else {
                                                    {
                                                        gm_b16_8 t192 = gm_l23;
                                                        gm_b16_8 t193 = gm_l20;
                                                        gm_t6j6smj7qoctu t194;
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t194 + 0), t192);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t194 + 16), t193);
                                                        gm_f_format_or32na5((void *)&gm_l50, (void *)NULL, cx, NULL, &t194);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                    }
                                                    uint8_t t195 = (uint8_t)!((((uint8_t *)&gm_l50))[0] != 0);
                                                    if (!t195) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l50) + 8, gm_srcd5arorrf8jkg, 680, 73);
                                                    gm_b16_8 t196 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l50) + 8));
                                                    gm_b16_8 t197 = GM_LD(gm_b16_8, ((uint8_t *)&t196));
                                                    gm_l47 = t197;
                                                }
                                                gm_b16_8 t198 = gm_l47;
                                                gm_l24 = t198;
                                            }
                                        }
                                        {
                                            uint64_t t199 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                                            uint8_t t200 = ((t199 == ((uint64_t)0ULL)));
                                            if (t200) {
                                                {
                                                    gm_b16_8 t201 = gm_l22;
                                                    gm_b16_8 t202 = gm_l20;
                                                    gm_t6j6smj7qoctu t203;
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t203 + 0), t201);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t203 + 16), t202);
                                                    gm_f_format_vmlgaaq((void *)&gm_l51, (void *)NULL, cx, NULL, &t203);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                }
                                                uint8_t t204 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
                                                if (!t204) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l51) + 8, gm_srcd5arorrf8jkg, 682, 44);
                                                gm_b16_8 t205 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l51) + 8));
                                                gm_b16_8 t206 = GM_LD(gm_b16_8, ((uint8_t *)&t205));
                                                gm_l25 = t206;
                                            } else {
                                                {
                                                    gm_b16_8 t207 = gm_l22;
                                                    gm_b16_8 t208 = gm_l24;
                                                    gm_t6j6smj7qoctu t209;
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t209 + 0), t207);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t209 + 16), t208);
                                                    gm_f_format_vp0clr1((void *)&gm_l52, (void *)NULL, cx, NULL, &t209);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                }
                                                uint8_t t210 = (uint8_t)!((((uint8_t *)&gm_l52))[0] != 0);
                                                if (!t210) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l52) + 8, gm_srcd5arorrf8jkg, 682, 87);
                                                gm_b16_8 t211 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l52) + 8));
                                                gm_b16_8 t212 = GM_LD(gm_b16_8, ((uint8_t *)&t211));
                                                gm_l25 = t212;
                                            }
                                            uint8_t t213 = gm_l2;
                                            if (t213) {
                                                uint64_t t214 = gm_l5;
                                                gm_t4ce0jd3tdro4 t215;
                                                GM_ST(uint64_t, ((uint8_t *)&t215 + 0), t214);
                                                gm_f_format_lrgko01((void *)&gm_l53, (void *)NULL, cx, NULL, t215);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t216 = (uint8_t)!((((uint8_t *)&gm_l53))[0] != 0);
                                                if (!t216) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l53) + 8, gm_srcd5arorrf8jkg, 684, 23);
                                                gm_b16_8 t217 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l53) + 8));
                                                gm_l26 = t217;
                                                uint64_t t218 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                                                uint8_t t219 = ((t218 == ((uint64_t)0ULL)));
                                                if (t219) {
                                                    {
                                                        void * t220 = (void *)((uint8_t *)&F.l1);
                                                        uint64_t t221 = gm_l5;
                                                        gm_b16_8 t222 = gm_l20;
                                                        gm_tf1bjg81u0glv t223;
                                                        GM_ST(uint64_t, ((uint8_t *)&t223 + 0), t221);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t223 + 8), t222);
                                                        gm_tftb1odc739t5 t224 = gm_f_write_d85j2rp(t220, &t223);
                                                        gm_l54 = t224;
                                                    }
                                                    uint8_t t225 = (uint8_t)!((((uint8_t *)&gm_l54))[0] != 0);
                                                    if (!t225) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l54) + 1, gm_srcd5arorrf8jkg, 686, 17);
                                                    {
                                                        gm_b16_8 t226 = gm_l22;
                                                        gm_b16_8 t227 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
                                                        uint64_t t228 = gm_l5;
                                                        gm_tftksr4op114e t229;
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t229 + 0), t226);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t229 + 16), t227);
                                                        GM_ST(uint64_t, ((uint8_t *)&t229 + 32), t228);
                                                        gm_f_format_avhd7c9((void *)&gm_l55, (void *)NULL, cx, NULL, &t229);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                    }
                                                    uint8_t t230 = (uint8_t)!((((uint8_t *)&gm_l55))[0] != 0);
                                                    if (!t230) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l55) + 8, gm_srcd5arorrf8jkg, 687, 24);
                                                    gm_b16_8 t231 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l55) + 8));
                                                    gm_b16_8 t232 = GM_LD(gm_b16_8, ((uint8_t *)&t231));
                                                    gm_l25 = t232;
                                                } else {
                                                    {
                                                        void * t233 = (void *)((uint8_t *)&F.l1);
                                                        gm_b16_8 t234 = gm_l23;
                                                        gm_b16_8 t235 = gm_l26;
                                                        uint64_t t236 = gm_l5;
                                                        gm_b16_8 t237 = gm_l26;
                                                        gm_b16_8 t238 = gm_l24;
                                                        gm_ti71ll5knnvij t239;
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t239 + 0), t234);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t239 + 16), t235);
                                                        GM_ST(uint64_t, ((uint8_t *)&t239 + 32), t236);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t239 + 40), t237);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t239 + 56), t238);
                                                        gm_tftb1odc739t5 t240 = gm_f_write_gk1hd5k(t233, &t239);
                                                        gm_l56 = t240;
                                                    }
                                                    uint8_t t241 = (uint8_t)!((((uint8_t *)&gm_l56))[0] != 0);
                                                    if (!t241) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l56) + 1, gm_srcd5arorrf8jkg, 689, 17);
                                                    {
                                                        gm_b16_8 t242 = gm_l22;
                                                        gm_b16_8 t243 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
                                                        uint64_t t244 = gm_l5;
                                                        gm_b16_8 t245 = gm_l26;
                                                        gm_tmr5oep1578tb t246;
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t246 + 0), t242);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t246 + 16), t243);
                                                        GM_ST(uint64_t, ((uint8_t *)&t246 + 32), t244);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t246 + 40), t245);
                                                        gm_f_format_8v2pi8t((void *)&gm_l57, (void *)NULL, cx, NULL, &t246);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                    }
                                                    uint8_t t247 = (uint8_t)!((((uint8_t *)&gm_l57))[0] != 0);
                                                    if (!t247) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l57) + 8, gm_srcd5arorrf8jkg, 690, 24);
                                                    gm_b16_8 t248 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l57) + 8));
                                                    gm_b16_8 t249 = GM_LD(gm_b16_8, ((uint8_t *)&t248));
                                                    gm_l25 = t249;
                                                }
                                            }
                                        }
                                        {
                                            void * t250 = gm_l8;
                                            uint8_t t251 = GM_LD(uint8_t, (((uint8_t *)t250) + 144));
                                            gm_b16_8 t252 = gm_f_conv_attr_m0b89pc(t251);
                                            gm_l27 = t252;
                                            uint8_t t253 = gm_l2;
                                            if (t253) {
                                                {
                                                    gm_b16_8 t254 = gm_l6;
                                                    gm_b16_8 t255 = gm_l27;
                                                    gm_t6j6smj7qoctu t256;
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t256 + 0), t254);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t256 + 16), t255);
                                                    gm_f_format_eg7p593((void *)&gm_l58, (void *)NULL, cx, NULL, &t256);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                }
                                                uint8_t t257 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
                                                if (!t257) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l58) + 8, gm_srcd5arorrf8jkg, 695, 24);
                                                gm_b16_8 t258 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l58) + 8));
                                                gm_b16_8 t259 = GM_LD(gm_b16_8, ((uint8_t *)&t258));
                                                gm_l27 = t259;
                                            }
                                            void * t260 = (void *)((uint8_t *)&F.l1);
                                            gm_b16_8 t261 = gm_l27;
                                            gm_b16_8 t262 = gm_l23;
                                            gm_b16_8 t263 = gm_l9;
                                            gm_b16_8 t264 = gm_l21;
                                            gm_b16_8 t265 = gm_l6;
                                            gm_tud7oovfvnf43 t266;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t266 + 0), t261);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t266 + 16), t262);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t266 + 32), t263);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t266 + 48), t264);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t266 + 64), t265);
                                            gm_tftb1odc739t5 t267 = gm_f_write_okup0j2(t260, &t266);
                                            gm_l59 = t267;
                                            uint8_t t268 = (uint8_t)!((((uint8_t *)&gm_l59))[0] != 0);
                                            if (!t268) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l59) + 1, gm_srcd5arorrf8jkg, 696, 9);
                                            void * t269 = (void *)((uint8_t *)&F.l1);
                                            gm_b16_8 t270 = gm_l27;
                                            gm_b16_8 t271 = gm_l23;
                                            gm_b16_8 t272 = gm_l9;
                                            gm_b16_8 t273 = gm_l21;
                                            gm_b16_8 t274 = gm_l25;
                                            gm_tud7oovfvnf43 t275;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t275 + 0), t270);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t275 + 16), t271);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t275 + 32), t272);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t275 + 48), t273);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t275 + 64), t274);
                                            gm_tftb1odc739t5 t276 = gm_f_write_ticpdcl(t269, &t275);
                                            gm_l60 = t276;
                                            uint8_t t277 = (uint8_t)!((((uint8_t *)&gm_l60))[0] != 0);
                                            if (!t277) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l60) + 1, gm_srcd5arorrf8jkg, 697, 9);
                                            goto gm_lb4;
                                        }
                                    }
gm_lb5:;
                                }
                                {
                                    gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 649, 5);
                                }
                            }
gm_lb4:;
                        }
                    }
gm_lb3:;
                }
                {
                    uint64_t t278 = gm_l4;
                    uint64_t r279; bool o280 = __builtin_add_overflow(t278, ((uint64_t)1ULL), &r279);
                    uint64_t t281 = r279;
                    gm_l4 = t281;
                }
            }
        }
gm_lb1:;
    }
    {
        gm_b16_8 t282 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
        gm_b16_8 t283 = GM_LD(gm_b16_8, ((uint8_t *)&t282));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t283);
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
__attribute__((aligned(4))) void gm_f_gen_main_0gsgsok(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2) {
    struct gm_fr_gen_main_0gsgsok F;
    void * gm_l0; /* g */
    gm_b16_8 gm_l1; /* entries */
    uint8_t gm_l2; /* select */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    uint32_t gm_l8; /* entry */
    gm_tk8ebsa8coi4p gm_l9; /* a */
    gm_tv199o0mcsqd6 gm_l10; /* amb */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* k */
    gm_t9805qtonbsag gm_l14; /* e */
    uint32_t gm_l15; /* eff */
    gm_tiilmfd16v3fc gm_l16; /* r */
    uint32_t gm_l17; /* id */
    GM_STACK_CHECK(&F, 1487u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tn68el8d14vku gm_l18;
    gm_t5o0tpv5jka2l gm_l19;
    gm_tftb1odc739t5 gm_l20;
    gm_tftb1odc739t5 gm_l21;
    gm_tftb1odc739t5 gm_l22;
    gm_tftb1odc739t5 gm_l23;
    gm_tftb1odc739t5 gm_l24;
    gm_tftb1odc739t5 gm_l25;
    gm_tftb1odc739t5 gm_l26;
    int32_t gm_l27;
    gm_tftb1odc739t5 gm_l28;
    gm_tftb1odc739t5 gm_l29;
    gm_tftb1odc739t5 gm_l30;
    gm_tftb1odc739t5 gm_l31;
    gm_tp0pp2cmr6tvd gm_l32;
    gm_tftb1odc739t5 gm_l33;
    gm_tftb1odc739t5 gm_l34;
    gm_tftb1odc739t5 gm_l35;
    gm_tftb1odc739t5 gm_l36;
    {
        gm_trqfvmjtt4sij t16;
        gm_f_list_uvfu1k1((void *)&t16, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l3 = t16;
    }
    {
        gm_trqfvmjtt4sij t17;
        gm_f_list_uvfu1k1((void *)&t17, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l4 = t17;
    }
    gm_b16_8 t18 = gm_l1;
    gm_l5 = t18;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t19 = gm_l6;
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t21 = (t19 < t20);
            if (!t21) {
                goto gm_lb1;
            }
            {
                uint64_t t22 = gm_l6;
                void * t23 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t25 = gm_l6;
                uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)t23) + (uintptr_t)t25 * 4));
                gm_tn68el8d14vku t27;
                GM_ST(uint32_t, (uint8_t *)&t27 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t27 + 0), t22);
                GM_ST(uint32_t, ((uint8_t *)&t27 + 8), t26);
                gm_l18 = t27;
                {
                    {
                        uint64_t t28 = GM_LD(uint64_t, ((uint8_t *)&gm_l18));
                        gm_l7 = t28;
                        uint32_t t29 = GM_LD(uint32_t, (((uint8_t *)&gm_l18) + 8));
                        gm_l8 = t29;
                        void * t30 = (void *)((uint8_t *)&F.l3);
                        void * t31 = gm_l0;
                        uint32_t t32 = gm_l8;
                        uint64_t t33 = gm_l7;
                        gm_f_gen_entry_hj4odev((void *)&gm_l19, (void *)NULL, cx, NULL, t31, t32, t33);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t34 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                        if (t34) {
                        } else {
                            gm_t08olq97rscn3 t35 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l19) + 8));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t35);
                            goto gm_fdone;
                        }
                        gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 8));
                        gm_tftb1odc739t5 t37 = gm_f_extend_0cs85gs(t30, t36);
                        gm_l20 = t37;
                        uint8_t t38 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                        if (!t38) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_srcd5arorrf8jkg, 712, 9);
                        void * t39 = (void *)((uint8_t *)&F.l4);
                        uint64_t t40 = gm_l7;
                        uint64_t t41 = gm_l7;
                        gm_t2lqu8jsj511d t42;
                        GM_ST(uint64_t, ((uint8_t *)&t42 + 0), t40);
                        GM_ST(uint64_t, ((uint8_t *)&t42 + 8), t41);
                        gm_tftb1odc739t5 t43 = gm_f_write_lu60214(t39, t42);
                        gm_l21 = t43;
                        uint8_t t44 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                        if (!t44) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srcd5arorrf8jkg, 713, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 711, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t45 = gm_l6;
            uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
            uint64_t t48 = r46;
            gm_l6 = t48;
        }
    }
gm_lb1:;
    void * t49 = (void *)((uint8_t *)&F.l3);
    gm_b16_8 t50;
    GM_ST(void *, (uint8_t *)&t50, (void *)(gm_seosa3iobfc09));
    GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(34));
    gm_tftb1odc739t5 t51 = gm_f_extend_0cs85gs(t49, t50);
    gm_l22 = t51;
    uint8_t t52 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
    if (!t52) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 1, gm_srcd5arorrf8jkg, 715, 5);
    uint8_t t53 = gm_l2;
    if (t53) {
        void * t54 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t55;
        GM_ST(void *, (uint8_t *)&t55, (void *)(gm_sv7tbc2gvmpa1));
        GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(99));
        gm_tftb1odc739t5 t56 = gm_f_extend_0cs85gs(t54, t55);
        gm_l23 = t56;
        uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
        if (!t57) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_srcd5arorrf8jkg, 717, 9);
    } else {
        void * t58 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t59;
        GM_ST(void *, (uint8_t *)&t59, (void *)(gm_slbs27elv52b5));
        GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(29));
        gm_tftb1odc739t5 t60 = gm_f_extend_0cs85gs(t58, t59);
        gm_l24 = t60;
        uint8_t t61 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
        if (!t61) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_srcd5arorrf8jkg, 719, 9);
    }
    void * t62 = (void *)((uint8_t *)&F.l3);
    gm_b16_8 t63;
    GM_ST(void *, (uint8_t *)&t63, (void *)(gm_sq77ose3aa457));
    GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(23));
    gm_tftb1odc739t5 t64 = gm_f_extend_0cs85gs(t62, t63);
    gm_l25 = t64;
    uint8_t t65 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
    if (!t65) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_srcd5arorrf8jkg, 721, 5);
    {
        {
            void * t66 = gm_l0;
            void * t67 = GM_LD(void *, ((uint8_t *)t66));
            gm_b16_8 t68 = GM_LD(gm_b16_8, ((((uint8_t *)t67) + 80) + 24));
            gm_b16_8 t69;
            GM_ST(void *, (uint8_t *)&t69, (void *)(gm_svl13be5ca8rt));
            GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(4));
            uint8_t t70 = (gm_eq0iqiubfm7e(((uint8_t *)&t68), ((uint8_t *)&t69)));
            if (!t70) {
                goto gm_lb7;
            }
            void * t71 = gm_l0;
            void * t72 = GM_LD(void *, ((uint8_t *)t71));
            void * t73 = GM_LD(void *, ((uint8_t *)t72));
            uint64_t t74 = GM_LD(uint64_t, ((((uint8_t *)t73) + 880) + 8));
            uint8_t t75 = (t74 > ((uint64_t)0ULL));
            if (!t75) {
                goto gm_lb7;
            }
            void * t76 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t77;
            GM_ST(void *, (uint8_t *)&t77, (void *)(gm_s2nn3moc8tfri));
            GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(16));
            gm_tftb1odc739t5 t78 = gm_f_extend_0cs85gs(t76, t77);
            gm_l26 = t78;
            uint8_t t79 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
            if (!t79) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_srcd5arorrf8jkg, 724, 9);
            goto gm_lb6;
        }
gm_lb7:;
        void * t80 = (void *)((uint8_t *)&F.l3);
        void * t81 = gm_l0;
        uint8_t t82 = GM_LD(uint8_t, ((((uint8_t *)t81) + 1136) + 1));
        if (t82) {
            gm_l27 = ((int32_t)1LL);
        } else {
            gm_l27 = ((int32_t)0LL);
        }
        int32_t t83 = gm_l27;
        gm_t19enj90vg8bd t84;
        GM_ST(int32_t, ((uint8_t *)&t84 + 0), t83);
        gm_tftb1odc739t5 t85 = gm_f_write_gi12t4o(t80, t84);
        gm_l28 = t85;
        uint8_t t86 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
        if (!t86) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 1, gm_srcd5arorrf8jkg, 726, 9);
        void * t87 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t88;
        GM_ST(void *, (uint8_t *)&t88, (void *)(gm_sck2rq9li012q));
        GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(22));
        gm_tftb1odc739t5 t89 = gm_f_extend_0cs85gs(t87, t88);
        gm_l29 = t89;
        uint8_t t90 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
        if (!t90) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_srcd5arorrf8jkg, 727, 9);
    }
gm_lb6:;
    void * t91 = (void *)((uint8_t *)&F.l3);
    gm_b16_8 t92;
    GM_ST(void *, (uint8_t *)&t92, (void *)(gm_smdv045psk1u8));
    GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(24));
    gm_tftb1odc739t5 t93 = gm_f_extend_0cs85gs(t91, t92);
    gm_l30 = t93;
    uint8_t t94 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
    if (!t94) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 1, gm_srcd5arorrf8jkg, 729, 5);
    {
        {
            uint8_t t95 = gm_l2;
            if (!t95) {
                goto gm_lb9;
            }
            void * t96 = gm_l0;
            void * t97 = GM_LD(void *, ((uint8_t *)t96));
            void * t98 = GM_LD(void *, ((uint8_t *)t97));
            uint32_t t99 = GM_LD(uint32_t, (((uint8_t *)t98) + 2280));
            uint8_t t100 = (t99 > ((uint32_t)0ULL));
            if (!t100) {
                goto gm_lb9;
            }
            void * t101 = (void *)((uint8_t *)&F.l3);
            void * t102 = gm_l0;
            void * t103 = GM_LD(void *, ((uint8_t *)t102));
            void * t104 = GM_LD(void *, ((uint8_t *)t103));
            uint32_t t105 = GM_LD(uint32_t, (((uint8_t *)t104) + 2280));
            gm_t88rcucg1b7uk t106;
            GM_ST(uint32_t, ((uint8_t *)&t106 + 0), t105);
            gm_tftb1odc739t5 t107 = gm_f_write_j5p5p7n(t101, t106);
            gm_l31 = t107;
            uint8_t t108 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
            if (!t108) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 1, gm_srcd5arorrf8jkg, 733, 9);
            goto gm_lb8;
        }
gm_lb9:;
    }
gm_lb8:;
    {
        void * t109 = gm_l0;
        void * t110 = GM_LD(void *, ((uint8_t *)t109));
        memmove(((uint8_t *)&gm_l9), (((uint8_t *)t110) + 48), 32);
    }
    {
        gm_t9805qtonbsag t111 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)&gm_l9));
        gm_t9805qtonbsag t112 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l9) + 8));
        gm_t9805qtonbsag t113 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l9) + 16));
        gm_tv199o0mcsqd6 t114;
        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t114 + 0), t111);
        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t114 + 8), t112);
        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t114 + 16), t113);
        gm_l10 = t114;
    }
    void * t115 = (void *)((uint8_t *)&gm_l10);
    uint64_t t116 = ((uintptr_t)((uint64_t)3ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t117;
    GM_ST(void *, (uint8_t *)&t117, (void *)(((uint8_t *)t115 + (uintptr_t)((uint64_t)0ULL) * 8)));
    GM_ST(uintptr_t, (uint8_t *)&t117 + 8, (uintptr_t)(t116));
    gm_b16_8 t118 = GM_LD(gm_b16_8, ((uint8_t *)&t117));
    gm_l11 = t118;
    gm_l12 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t119 = gm_l12;
            uint64_t t120 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
            uint8_t t121 = (t119 < t120);
            if (!t121) {
                goto gm_lb10;
            }
            {
                uint64_t t122 = gm_l12;
                void * t123 = GM_LD(void *, ((uint8_t *)&gm_l11));
                uint64_t t124 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                uint64_t t125 = gm_l12;
                gm_t9805qtonbsag t126 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)t123) + (uintptr_t)t125 * 8));
                gm_tp0pp2cmr6tvd t127;
                GM_ST(uint64_t, ((uint8_t *)&t127 + 0), t122);
                GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t127 + 8), t126);
                gm_l32 = t127;
                {
                    {
                        uint64_t t128 = GM_LD(uint64_t, ((uint8_t *)&gm_l32));
                        gm_l13 = t128;
                        gm_t9805qtonbsag t129 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l32) + 8));
                        gm_l14 = t129;
                        uint8_t t130 = (uint8_t)((((uint8_t *)&gm_l14))[4] != 0);
                        if (t130) {
                            uint32_t t131 = GM_LD(uint32_t, ((uint8_t *)&gm_l14));
                            gm_l15 = t131;
                        } else {
                            goto gm_lb12;
                        }
                        {
                            uint32_t t132 = gm_l15;
                            gm_b16_8 t133 = gm_f_empty_slice_fh7svmj();
                            gm_b16_8 t134 = GM_LD(gm_b16_8, ((uint8_t *)&t133));
                            gm_tiilmfd16v3fc t135;
                            GM_ST(uint32_t, (uint8_t *)&t135 + 4, 0);
                            GM_ST(uint32_t, ((uint8_t *)&t135 + 0), t132);
                            GM_ST(gm_b16_8, ((uint8_t *)&t135 + 8), t134);
                            gm_l16 = t135;
                        }
                        void * t136 = gm_l0;
                        void * t137 = (void *)((uint8_t *)&gm_l16);
                        uint32_t t138 = gm_f_effect_id_5588nqa((void *)NULL, cx, NULL, t136, t137);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l17 = t138;
                        {
                            void * t139 = (void *)((uint8_t *)&F.l3);
                            uint64_t t140 = gm_l13;
                            uint64_t t141 = gm_l13;
                            uint32_t t142 = gm_l17;
                            uint64_t t143 = gm_l13;
                            gm_tnen6863q2sjs t144;
                            GM_ST(uint32_t, (uint8_t *)&t144 + 20, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t144 + 0), t140);
                            GM_ST(uint64_t, ((uint8_t *)&t144 + 8), t141);
                            GM_ST(uint32_t, ((uint8_t *)&t144 + 16), t142);
                            GM_ST(uint64_t, ((uint8_t *)&t144 + 24), t143);
                            gm_tftb1odc739t5 t145 = gm_f_write_ug67q96(t139, &t144);
                            gm_l33 = t145;
                        }
                        uint8_t t146 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                        if (!t146) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 1, gm_srcd5arorrf8jkg, 741, 9);
                        goto gm_lb13;
                    }
gm_lb14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 737, 5);
                }
gm_lb13:;
            }
gm_lb12:;
            uint64_t t147 = gm_l12;
            uint64_t r148; bool o149 = __builtin_add_overflow(t147, ((uint64_t)1ULL), &r148);
            uint64_t t150 = r148;
            gm_l12 = t150;
        }
    }
gm_lb10:;
    uint8_t t151 = gm_l2;
    if (t151) {
        void * t152 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t153 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
        gm_tnpifv20irnph t154;
        GM_ST(gm_b16_8, ((uint8_t *)&t154 + 0), t153);
        gm_tftb1odc739t5 t155 = gm_f_write_jqf697l(t152, t154);
        gm_l34 = t155;
        uint8_t t156 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
        if (!t156) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 1, gm_srcd5arorrf8jkg, 744, 9);
    } else {
        void * t157 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t158;
        GM_ST(void *, (uint8_t *)&t158, (void *)(gm_svgsom6hbqtcn));
        GM_ST(uintptr_t, (uint8_t *)&t158 + 8, (uintptr_t)(39));
        gm_tftb1odc739t5 t159 = gm_f_extend_0cs85gs(t157, t158);
        gm_l35 = t159;
        uint8_t t160 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
        if (!t160) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 1, gm_srcd5arorrf8jkg, 746, 9);
    }
    void * t161 = (void *)((uint8_t *)&F.l3);
    gm_b16_8 t162;
    GM_ST(void *, (uint8_t *)&t162, (void *)(gm_sikpqtja8pqpv));
    GM_ST(uintptr_t, (uint8_t *)&t162 + 8, (uintptr_t)(2));
    gm_tftb1odc739t5 t163 = gm_f_extend_0cs85gs(t161, t162);
    gm_l36 = t163;
    uint8_t t164 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
    if (!t164) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 1, gm_srcd5arorrf8jkg, 748, 5);
    {
        gm_b16_8 t165 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
        gm_b16_8 t166 = GM_LD(gm_b16_8, ((uint8_t *)&t165));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t166);
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
extern __attribute__((aligned(4))) void gm_f_generate_6no5tn1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2);
__attribute__((aligned(4))) void gm_f_generate_entries_br1bd7f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2, const void * a3) {
    struct gm_fr_generate_entries_br1bd7f F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* entries */
    uint8_t gm_l2; /* select */
    gm_tftmsj5vtqvej gm_l3; /* opts */
    void * gm_l4; /* <tmp> */
    gm_tftmsj5vtqvej gm_l6; /* <tmp> */
    gm_b16_8 gm_l53; /* <tmp> */
    uint64_t gm_l54; /* <tmp> */
    uint32_t gm_l55; /* entry */
    gm_b16_8 gm_l56; /* <tmp> */
    uint64_t gm_l57; /* <tmp> */
    uint32_t gm_l58; /* f */
    uint32_t gm_l59; /* hook */
    gm_t6j6smj7qoctu gm_l60; /* consts */
    gm_b16_8 gm_l61; /* exports */
    uint8_t gm_l62; /* freestanding */
    gm_b16_8 gm_l63; /* main */
    gm_b16_8 gm_l64; /* checks */
    gm_b16_8 gm_l66; /* names */
    gm_b16_8 gm_l67; /* gdb */
    gm_b16_8 gm_l69; /* <tmp> */
    uint64_t gm_l70; /* <tmp> */
    uint32_t gm_l71; /* id */
    gm_b16_8 gm_l72; /* frames */
    gm_b16_8 gm_l73; /* protos */
    gm_closure gm_l77; /* code_has */
    uint8_t gm_l78; /* fibers */
    gm_b16_8 gm_l79; /* run_main */
    gm_b16_8 gm_l80; /* main_text */
    gm_b16_8 gm_l81; /* reset */
    gm_b16_8 gm_l82; /* split_support */
    gm_b16_8 gm_l83; /* split_types */
    gm_b16_8 gm_l84; /* split_frames */
    gm_b16_8 gm_l85; /* <tmp> */
    gm_b16_8 gm_l86; /* <tmp> */
    gm_b16_8 gm_l87; /* <tmp> */
    gm_b16_8 gm_l88; /* <tmp> */
    gm_b16_8 gm_l89; /* <tmp> */
    gm_b16_8 gm_l90; /* <tmp> */
    gm_b16_8 gm_l91; /* <tmp> */
    gm_b16_8 gm_l92; /* <tmp> */
    gm_b16_8 gm_l93; /* <tmp> */
    gm_b16_8 gm_l94; /* <tmp> */
    gm_b16_8 gm_l95; /* <tmp> */
    GM_STACK_CHECK(&F, 13682u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tqgtd5gtgj65s gm_l97;
    gm_t9t4u07j77scv gm_l98;
    gm_t5o0tpv5jka2l gm_l99;
    gm_t5o0tpv5jka2l gm_l100;
    gm_tabb710d9mq2e gm_l101;
    gm_tabb710d9mq2e gm_l102;
    gm_t5o0tpv5jka2l gm_l103;
    int32_t gm_l104;
    gm_tabb710d9mq2e gm_l105;
    gm_tftb1odc739t5 gm_l106;
    gm_tftb1odc739t5 gm_l107;
    gm_tftb1odc739t5 gm_l108;
    gm_tftb1odc739t5 gm_l109;
    gm_tftb1odc739t5 gm_l110;
    gm_tftb1odc739t5 gm_l111;
    gm_tftb1odc739t5 gm_l112;
    gm_tabb710d9mq2e gm_l113;
    gm_tftb1odc739t5 gm_l114;
    gm_tftb1odc739t5 gm_l115;
    gm_tftb1odc739t5 gm_l116;
    gm_tftb1odc739t5 gm_l117;
    gm_tftb1odc739t5 gm_l118;
    gm_tftb1odc739t5 gm_l119;
    gm_tftb1odc739t5 gm_l120;
    gm_tftb1odc739t5 gm_l121;
    gm_tftb1odc739t5 gm_l122;
    gm_tftb1odc739t5 gm_l123;
    gm_tftb1odc739t5 gm_l124;
    gm_tftb1odc739t5 gm_l125;
    gm_tabb710d9mq2e gm_l126;
    gm_tabb710d9mq2e gm_l127;
    gm_b16_8 gm_l128;
    gm_tabb710d9mq2e gm_l129;
    gm_tftb1odc739t5 gm_l130;
    gm_tftb1odc739t5 gm_l131;
    gm_tabb710d9mq2e gm_l132;
    gm_tabb710d9mq2e gm_l133;
    gm_tabb710d9mq2e gm_l134;
    gm_tabb710d9mq2e gm_l135;
    gm_tabb710d9mq2e gm_l136;
    gm_tabb710d9mq2e gm_l137;
    gm_tabb710d9mq2e gm_l138;
    uint8_t gm_l139;
    uint8_t gm_l140;
    {
        void * t32 = gm_l0;
        gm_l4 = t32;
        void * t33 = gm_l0;
        void * t34 = GM_LD(void *, ((uint8_t *)t33));
        void * t35 = gm_l0;
        gm_t0rfe1ou14h21 t36 = GM_LD(gm_t0rfe1ou14h21, (((uint8_t *)t35) + 80));
        gm_f_ctypes_j6au0st((void *)&F.l5, (void *)NULL, cx, NULL, t34, &t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        memmove(((uint8_t *)&gm_l6), ((uint8_t *)a3), 24);
        gm_f_hash_map_rs5nlfs((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_p3t5tj8((void *)&F.l8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_new_namer_5q88o9g((void *)&F.l9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_0c2v515((void *)&F.l10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_0c2v515((void *)&F.l11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_0c2v515((void *)&F.l12, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_g5h3sdu((void *)&F.l13, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_tl7apid((void *)&F.l14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_tl7apid((void *)&F.l15, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_0c2v515((void *)&F.l16, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_tl7apid((void *)&F.l17, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_hme58st((void *)&F.l18, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_p3t5tj8((void *)&F.l19, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_uvfu1k1((void *)&F.l20, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_p3t5tj8((void *)&F.l21, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_uvfu1k1((void *)&F.l22, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_uvfu1k1((void *)&F.l23, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_uvfu1k1((void *)&F.l24, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_new_helpers_4q783cn((void *)&F.l25, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_go2qg0b((void *)&F.l26, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_s83jn2q((void *)&F.l27, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_s83jn2q((void *)&F.l28, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_76d05hh((void *)&F.l29, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_rc6kc0u((void *)&F.l30, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_a5k8eig((void *)&F.l31, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_fupfd5g((void *)&F.l32, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_t9h7o8p((void *)&F.l33, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_k5efes8((void *)&F.l34, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_t9h7o8p((void *)&F.l35, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_em2q005((void *)&F.l36, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_uvfu1k1((void *)&F.l37, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_epvducg((void *)&F.l38, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_epvducg((void *)&F.l39, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_epvducg((void *)&F.l40, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_938p55h((void *)&F.l41, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_0vc6l5i((void *)&F.l42, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_uvfu1k1((void *)&F.l43, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_epvducg((void *)&F.l44, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_70470jk((void *)&F.l45, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_70470jk((void *)&F.l46, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_go2qg0b((void *)&F.l47, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_1s9eugt((void *)&F.l48, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_1t0a5et((void *)&F.l49, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_1c18lnh((void *)&F.l50, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_s83jn2q((void *)&F.l51, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t37 = gm_l4;
        gm_tqetrgo0q24b5 t38;
        memset(&t38, 0, sizeof t38);
        gm_tuutj662vpgtg t39;
        memset(&t39, 0, sizeof t39);
        gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)gm_cgsmfcq937e_2));
        gm_tpo9d7ujpll8m t41;
        memset(&t41, 0, sizeof t41);
        gm_trg1s7erfs461 t42 = GM_LD(gm_trg1s7erfs461, ((uint8_t *)&F.l5));
        gm_tftmsj5vtqvej t43 = GM_LD(gm_tftmsj5vtqvej, ((uint8_t *)&gm_l6));
        gm_t4c7ipjj9s3fp t44 = GM_LD(gm_t4c7ipjj9s3fp, ((uint8_t *)&F.l7));
        gm_t4c7ipjj9s3fp t45 = GM_LD(gm_t4c7ipjj9s3fp, ((uint8_t *)&F.l8));
        gm_ttiitul0263ld t46 = GM_LD(gm_ttiitul0263ld, ((uint8_t *)&F.l9));
        gm_trqfvmjtt4sij t47 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l10));
        gm_trqfvmjtt4sij t48 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l11));
        gm_trqfvmjtt4sij t49 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l12));
        gm_t4c7ipjj9s3fp t50 = GM_LD(gm_t4c7ipjj9s3fp, ((uint8_t *)&F.l13));
        gm_t4c7ipjj9s3fp t51 = GM_LD(gm_t4c7ipjj9s3fp, ((uint8_t *)&F.l14));
        gm_t4c7ipjj9s3fp t52 = GM_LD(gm_t4c7ipjj9s3fp, ((uint8_t *)&F.l15));
        gm_trqfvmjtt4sij t53 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l16));
        gm_t4c7ipjj9s3fp t54 = GM_LD(gm_t4c7ipjj9s3fp, ((uint8_t *)&F.l17));
        gm_trqfvmjtt4sij t55 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l18));
        gm_t4c7ipjj9s3fp t56 = GM_LD(gm_t4c7ipjj9s3fp, ((uint8_t *)&F.l19));
        gm_trqfvmjtt4sij t57 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l20));
        gm_t4c7ipjj9s3fp t58 = GM_LD(gm_t4c7ipjj9s3fp, ((uint8_t *)&F.l21));
        gm_trqfvmjtt4sij t59 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l22));
        gm_trqfvmjtt4sij t60 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l23));
        gm_trqfvmjtt4sij t61 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l24));
        gm_te68172gglous t62 = GM_LD(gm_te68172gglous, ((uint8_t *)&F.l25));
        gm_trqfvmjtt4sij t63 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l26));
        gm_trqfvmjtt4sij t64 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l27));
        gm_trqfvmjtt4sij t65 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l28));
        gm_t4c7ipjj9s3fp t66 = GM_LD(gm_t4c7ipjj9s3fp, ((uint8_t *)&F.l29));
        gm_trqfvmjtt4sij t67 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l30));
        gm_trqfvmjtt4sij t68 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l31));
        gm_t4c7ipjj9s3fp t69 = GM_LD(gm_t4c7ipjj9s3fp, ((uint8_t *)&F.l32));
        gm_t4c7ipjj9s3fp t70 = GM_LD(gm_t4c7ipjj9s3fp, ((uint8_t *)&F.l33));
        gm_trqfvmjtt4sij t71 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l34));
        gm_t4c7ipjj9s3fp t72 = GM_LD(gm_t4c7ipjj9s3fp, ((uint8_t *)&F.l35));
        gm_t4c7ipjj9s3fp t73 = GM_LD(gm_t4c7ipjj9s3fp, ((uint8_t *)&F.l36));
        gm_trqfvmjtt4sij t74 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l41));
        gm_trqfvmjtt4sij t75 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l42));
        gm_trqfvmjtt4sij t76 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l43));
        gm_trqfvmjtt4sij t77 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l37));
        gm_trqfvmjtt4sij t78 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l38));
        gm_trqfvmjtt4sij t79 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l39));
        gm_trqfvmjtt4sij t80 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l40));
        gm_trqfvmjtt4sij t81 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l44));
        gm_t4c7ipjj9s3fp t82 = GM_LD(gm_t4c7ipjj9s3fp, ((uint8_t *)&F.l45));
        gm_t4c7ipjj9s3fp t83 = GM_LD(gm_t4c7ipjj9s3fp, ((uint8_t *)&F.l46));
        gm_trqfvmjtt4sij t84 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l47));
        gm_t4c7ipjj9s3fp t85 = GM_LD(gm_t4c7ipjj9s3fp, ((uint8_t *)&F.l48));
        gm_trqfvmjtt4sij t86 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l49));
        gm_t4c7ipjj9s3fp t87 = GM_LD(gm_t4c7ipjj9s3fp, ((uint8_t *)&F.l50));
        gm_trqfvmjtt4sij t88 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l51));
        gm_t1itmv09v9de5 t89;
        GM_ST(void *, ((uint8_t *)&t89 + 0), t37);
        GM_ST(gm_trg1s7erfs461, ((uint8_t *)&t89 + 8), t42);
        GM_ST(gm_tftmsj5vtqvej, ((uint8_t *)&t89 + 1136), t43);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t89 + 1160), t44);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t89 + 1216), t45);
        GM_ST(gm_ttiitul0263ld, ((uint8_t *)&t89 + 1272), t46);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t89 + 1328), t47);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t89 + 1368), t48);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t89 + 1408), t49);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t89 + 1448), t50);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t89 + 1504), t51);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t89 + 1560), t52);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t89 + 1616), t53);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t89 + 1656), t54);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t89 + 1712), t55);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t89 + 1752), t56);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t89 + 1808), t57);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t89 + 1848), t58);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t89 + 1904), t59);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t89 + 1944), t60);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t89 + 1984), t61);
        GM_ST(gm_te68172gglous, ((uint8_t *)&t89 + 2024), t62);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t89 + 2328), t63);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t89 + 2368), t64);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t89 + 2408), t65);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t89 + 2448), t66);
        GM_ST(gm_tqetrgo0q24b5, ((uint8_t *)&t89 + 2504), t38);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t89 + 2552), t67);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t89 + 2592), t68);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t89 + 2632), t69);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t89 + 2688), t70);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t89 + 2744), t71);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t89 + 2784), t72);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t89 + 2840), t73);
        GM_ST(gm_tuutj662vpgtg, ((uint8_t *)&t89 + 2896), t39);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t89 + 2960), t74);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t89 + 3000), t75);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t89 + 3040), t76);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t89 + 3080), t77);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t89 + 3120), t78);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t89 + 3160), t79);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t89 + 3200), t80);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t89 + 3240), t81);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t89 + 3280), t82);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t89 + 3336), t83);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t89 + 3392), t84);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t89 + 3432), t85);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t89 + 3488), t86);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t89 + 3528), t87);
        GM_ST(gm_b16_8, ((uint8_t *)&t89 + 3584), t40);
        GM_ST(void *, ((uint8_t *)&t89 + 3600), ((void *)0));
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t89 + 3608), t88);
        GM_ST(gm_tpo9d7ujpll8m, ((uint8_t *)&t89 + 3648), t41);
        F.l52 = t89;
    }
    {
        uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)a3) + 2));
        GM_ST(uint8_t, ((((uint8_t *)&F.l52) + 8) + 808), t90);
        gm_b16_8 t91 = gm_l1;
        gm_l53 = t91;
        gm_l54 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t93 = gm_l54;
                uint64_t t94 = GM_LD(uint64_t, (((uint8_t *)&gm_l53) + 8));
                uint8_t t95 = (t93 < t94);
                if (!t95) {
                    goto gm_lb1;
                }
                {
                    void * t96 = GM_LD(void *, ((uint8_t *)&gm_l53));
                    uint64_t t97 = GM_LD(uint64_t, (((uint8_t *)&gm_l53) + 8));
                    uint64_t t98 = gm_l54;
                    {
                        {
                            uint32_t t99 = GM_LD(uint32_t, (((uint8_t *)t96) + (uintptr_t)t98 * 4));
                            gm_l55 = t99;
                            void * t100 = (void *)((uint8_t *)&F.l52);
                            uint32_t t101 = gm_l55;
                            gm_b0_1 t102 = gm_f_queue_cd51r8c((void *)NULL, cx, NULL, t100, t101);
                            if (gm_unw.kind) {
                                goto gm_ll92;
                            }
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 236, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t103 = gm_l54;
                uint64_t r104; bool o105 = __builtin_add_overflow(t103, ((uint64_t)1ULL), &r104);
                uint64_t t106 = r104;
                gm_l54 = t106;
            }
        }
        goto gm_ls107;
gm_ll92:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb1; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb3; }
        }
        goto gm_lad;
gm_ls107:;
gm_lb1:;
        void * t108 = gm_l0;
        void * t109 = GM_LD(void *, ((uint8_t *)t108));
        gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)t109) + 920));
        gm_l56 = t110;
        gm_l57 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t112 = gm_l57;
                uint64_t t113 = GM_LD(uint64_t, (((uint8_t *)&gm_l56) + 8));
                uint8_t t114 = (t112 < t113);
                if (!t114) {
                    goto gm_lb6;
                }
                {
                    void * t115 = GM_LD(void *, ((uint8_t *)&gm_l56));
                    uint64_t t116 = GM_LD(uint64_t, (((uint8_t *)&gm_l56) + 8));
                    uint64_t t117 = gm_l57;
                    {
                        {
                            uint32_t t118 = GM_LD(uint32_t, (((uint8_t *)t115) + (uintptr_t)t117 * 4));
                            gm_l58 = t118;
                            void * t119 = (void *)((uint8_t *)&F.l52);
                            uint32_t t120 = gm_l58;
                            gm_b0_1 t121 = gm_f_queue_cd51r8c((void *)NULL, cx, NULL, t119, t120);
                            if (gm_unw.kind) {
                                goto gm_ll111;
                            }
                            goto gm_lb9;
                        }
gm_lb10:;
                        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 237, 5);
                    }
gm_lb9:;
                }
gm_lb8:;
                uint64_t t122 = gm_l57;
                uint64_t r123; bool o124 = __builtin_add_overflow(t122, ((uint64_t)1ULL), &r123);
                uint64_t t125 = r123;
                gm_l57 = t125;
            }
        }
        goto gm_ls126;
gm_ll111:;
        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb6; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb8; }
        }
        goto gm_lad;
gm_ls126:;
gm_lb6:;
        {
            {
                void * t127 = gm_l0;
                void * t128 = GM_LD(void *, ((uint8_t *)t127));
                uint8_t t129 = (uint8_t)(((((uint8_t *)t128) + 960))[4] != 0);
                if (!t129) {
                    goto gm_lb12;
                }
                uint32_t t130 = GM_LD(uint32_t, (((uint8_t *)t128) + 960));
                gm_l59 = t130;
                void * t131 = (void *)((uint8_t *)&F.l52);
                uint32_t t132 = gm_l59;
                gm_b0_1 t133 = gm_f_queue_cd51r8c((void *)NULL, cx, NULL, t131, t132);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_lb11;
            }
gm_lb12:;
        }
gm_lb11:;
    }
    {
        void * t134 = (void *)((uint8_t *)&F.l52);
        gm_f_drain_leoibma((void *)&gm_l97, (void *)NULL, cx, NULL, t134);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t135 = (uint8_t)!((((uint8_t *)&gm_l97))[0] != 0);
        if (t135) {
        } else {
            gm_t08olq97rscn3 t136 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l97) + 8));
            memset(&(*(gm_tetdkgoreqmn1 *)gm_out), 0, sizeof (*(gm_tetdkgoreqmn1 *)gm_out));
            ((uint8_t *)&(*(gm_tetdkgoreqmn1 *)gm_out))[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_tetdkgoreqmn1 *)gm_out) + 8), t136);
            goto gm_fdone;
        }
        void * t137 = (void *)((uint8_t *)&F.l52);
        gm_f_gen_consts_6ibghrc((void *)&gm_l98, (void *)NULL, cx, NULL, t137);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t138 = (uint8_t)!((((uint8_t *)&gm_l98))[0] != 0);
        if (t138) {
        } else {
            gm_t08olq97rscn3 t139 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l98) + 8));
            memset(&(*(gm_tetdkgoreqmn1 *)gm_out), 0, sizeof (*(gm_tetdkgoreqmn1 *)gm_out));
            ((uint8_t *)&(*(gm_tetdkgoreqmn1 *)gm_out))[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_tetdkgoreqmn1 *)gm_out) + 8), t139);
            goto gm_fdone;
        }
        memmove(((uint8_t *)&gm_l60), (((uint8_t *)&gm_l98) + 8), 32);
        void * t140 = (void *)((uint8_t *)&F.l52);
        gm_f_gen_exports_koai3gr((void *)&gm_l99, (void *)NULL, cx, NULL, t140);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t141 = (uint8_t)!((((uint8_t *)&gm_l99))[0] != 0);
        if (t141) {
        } else {
            gm_t08olq97rscn3 t142 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l99) + 8));
            memset(&(*(gm_tetdkgoreqmn1 *)gm_out), 0, sizeof (*(gm_tetdkgoreqmn1 *)gm_out));
            ((uint8_t *)&(*(gm_tetdkgoreqmn1 *)gm_out))[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_tetdkgoreqmn1 *)gm_out) + 8), t142);
            goto gm_fdone;
        }
        gm_b16_8 t143 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l99) + 8));
        gm_l61 = t143;
        void * t144 = gm_l0;
        gm_b16_8 t145 = GM_LD(gm_b16_8, ((((uint8_t *)t144) + 80) + 24));
        gm_b16_8 t146;
        GM_ST(void *, (uint8_t *)&t146, (void *)(gm_soo4rtckloi4d));
        GM_ST(uintptr_t, (uint8_t *)&t146 + 8, (uintptr_t)(12));
        uint8_t t147 = (gm_eq0iqiubfm7e(((uint8_t *)&t145), ((uint8_t *)&t146)));
        gm_l62 = t147;
        uint8_t t148 = gm_l62;
        if (t148) {
            {
                void * t149 = (void *)((uint8_t *)&F.l52);
                gm_b16_8 t150 = gm_f_gen_start_ip4boq0((void *)NULL, cx, NULL, t149);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t151 = gm_l61;
                void * t152 = (void *)((uint8_t *)&F.l52);
                gm_f_gen_trap_hook_lu55viv((void *)&gm_l100, (void *)NULL, cx, NULL, t152);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t153 = (uint8_t)!((((uint8_t *)&gm_l100))[0] != 0);
                if (t153) {
                } else {
                    gm_t08olq97rscn3 t154 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l100) + 8));
                    memset(&(*(gm_tetdkgoreqmn1 *)gm_out), 0, sizeof (*(gm_tetdkgoreqmn1 *)gm_out));
                    ((uint8_t *)&(*(gm_tetdkgoreqmn1 *)gm_out))[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_tetdkgoreqmn1 *)gm_out) + 8), t154);
                    goto gm_fdone;
                }
                gm_b16_8 t155 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l100) + 8));
                gm_tfeab279c16rr t156;
                GM_ST(gm_b16_8, ((uint8_t *)&t156 + 0), t150);
                GM_ST(gm_b16_8, ((uint8_t *)&t156 + 16), t151);
                GM_ST(gm_b16_8, ((uint8_t *)&t156 + 32), t155);
                gm_f_format_dsta2ql((void *)&gm_l101, (void *)NULL, cx, NULL, &t156);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t157 = (uint8_t)!((((uint8_t *)&gm_l101))[0] != 0);
            if (!t157) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l101) + 8, gm_srcd5arorrf8jkg, 244, 33);
            gm_b16_8 t158 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l101) + 8));
            gm_b16_8 t159 = GM_LD(gm_b16_8, ((uint8_t *)&t158));
            gm_l61 = t159;
        }
        {
            {
                void * t160 = gm_l0;
                gm_b16_8 t161 = GM_LD(gm_b16_8, ((((uint8_t *)t160) + 80) + 24));
                gm_b16_8 t162;
                GM_ST(void *, (uint8_t *)&t162, (void *)(gm_svl13be5ca8rt));
                GM_ST(uintptr_t, (uint8_t *)&t162 + 8, (uintptr_t)(4));
                uint8_t t163 = (gm_eq0iqiubfm7e(((uint8_t *)&t161), ((uint8_t *)&t162)));
                if (!t163) {
                    goto gm_lb14;
                }
                void * t164 = gm_l0;
                void * t165 = GM_LD(void *, ((uint8_t *)t164));
                uint64_t t166 = GM_LD(uint64_t, ((((uint8_t *)t165) + 880) + 8));
                uint8_t t167 = (t166 > ((uint64_t)0ULL));
                if (!t167) {
                    goto gm_lb14;
                }
                {
                    void * t168 = (void *)((uint8_t *)&F.l52);
                    gm_b16_8 t169 = gm_f_gen_start_ip4boq0((void *)NULL, cx, NULL, t168);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t170 = gm_l61;
                    gm_t6j6smj7qoctu t171;
                    GM_ST(gm_b16_8, ((uint8_t *)&t171 + 0), t169);
                    GM_ST(gm_b16_8, ((uint8_t *)&t171 + 16), t170);
                    gm_f_format_8n626pi((void *)&gm_l102, (void *)NULL, cx, NULL, &t171);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t172 = (uint8_t)!((((uint8_t *)&gm_l102))[0] != 0);
                if (!t172) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l102) + 8, gm_srcd5arorrf8jkg, 245, 72);
                gm_b16_8 t173 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l102) + 8));
                gm_b16_8 t174 = GM_LD(gm_b16_8, ((uint8_t *)&t173));
                gm_l61 = t174;
                goto gm_lb13;
            }
gm_lb14:;
        }
gm_lb13:;
    }
    {
        uint8_t t175 = gm_l62;
        if (t175) {
            gm_b16_8 t176;
            GM_ST(void *, (uint8_t *)&t176, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t176 + 8, (uintptr_t)(0));
            gm_l63 = t176;
        } else {
            void * t177 = (void *)((uint8_t *)&F.l52);
            gm_b16_8 t178 = gm_l1;
            uint8_t t179 = gm_l2;
            gm_f_gen_main_0gsgsok((void *)&gm_l103, (void *)NULL, cx, NULL, t177, t178, t179);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t180 = (uint8_t)!((((uint8_t *)&gm_l103))[0] != 0);
            if (t180) {
            } else {
                gm_t08olq97rscn3 t181 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l103) + 8));
                memset(&(*(gm_tetdkgoreqmn1 *)gm_out), 0, sizeof (*(gm_tetdkgoreqmn1 *)gm_out));
                ((uint8_t *)&(*(gm_tetdkgoreqmn1 *)gm_out))[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_tetdkgoreqmn1 *)gm_out) + 8), t181);
                goto gm_fdone;
            }
            gm_b16_8 t182 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l103) + 8));
            gm_l63 = t182;
        }
        uint8_t t183 = GM_LD(uint8_t, ((uint8_t *)a3));
        if (t183) {
            gm_l104 = ((int32_t)1LL);
        } else {
            gm_l104 = ((int32_t)0LL);
        }
        int32_t t184 = gm_l104;
        gm_t19enj90vg8bd t185;
        GM_ST(int32_t, ((uint8_t *)&t185 + 0), t184);
        gm_f_format_7fi363q((void *)&gm_l105, (void *)NULL, cx, NULL, t185);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t186 = (uint8_t)!((((uint8_t *)&gm_l105))[0] != 0);
        if (!t186) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l105) + 8, gm_srcd5arorrf8jkg, 247, 15);
        gm_b16_8 t187 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l105) + 8));
        gm_l64 = t187;
        gm_trqfvmjtt4sij t188;
        gm_f_list_uvfu1k1((void *)&t188, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l65 = t188;
        void * t189 = (void *)((uint8_t *)&F.l65);
        gm_b16_8 t190 = GM_LD(gm_b16_8, ((uint8_t *)gm_c9e8ns5bad4));
        gm_tftb1odc739t5 t191 = gm_f_extend_0cs85gs(t189, t190);
        gm_l106 = t191;
        uint8_t t192 = (uint8_t)!((((uint8_t *)&gm_l106))[0] != 0);
        if (!t192) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l106) + 1, gm_srcd5arorrf8jkg, 249, 5);
        void * t193 = (void *)((uint8_t *)&F.l65);
        void * t194 = (void *)(((uint8_t *)&F.l52) + 8);
        gm_b16_8 t195 = gm_f_blob_typedefs_4nfn4l8((void *)NULL, cx, NULL, t194);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t196 = gm_f_extend_0cs85gs(t193, t195);
        gm_l107 = t196;
        uint8_t t197 = (uint8_t)!((((uint8_t *)&gm_l107))[0] != 0);
        if (!t197) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l107) + 1, gm_srcd5arorrf8jkg, 250, 5);
        void * t198 = (void *)(((uint8_t *)&F.l52) + 8);
        gm_b16_8 t199 = gm_f_member_names_17el54k((void *)NULL, cx, NULL, t198);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l66 = t199;
        void * t200 = (void *)((uint8_t *)&F.l65);
        gm_b16_8 t201 = gm_l66;
        gm_b16_8 t202 = GM_LD(gm_b16_8, ((uint8_t *)&t201));
        void * t203 = (void *)(((uint8_t *)&F.l52) + 8);
        gm_b16_8 t204 = gm_f_mirror_text_cr5muc9(t203);
        gm_b16_8 t205 = gm_f_guard_o7k9a0e((void *)NULL, cx, NULL, t202, t204);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t206 = gm_f_extend_0cs85gs(t200, t205);
        gm_l108 = t206;
        uint8_t t207 = (uint8_t)!((((uint8_t *)&gm_l108))[0] != 0);
        if (!t207) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l108) + 1, gm_srcd5arorrf8jkg, 254, 5);
        void * t208 = (void *)((uint8_t *)&F.l65);
        gm_b16_8 t209 = gm_l66;
        gm_b16_8 t210 = GM_LD(gm_b16_8, ((uint8_t *)&t209));
        void * t211 = (void *)(((uint8_t *)&F.l52) + 8);
        gm_b16_8 t212 = gm_f_view_defs_u461okc(t211);
        gm_b16_8 t213 = gm_f_guard_o7k9a0e((void *)NULL, cx, NULL, t210, t212);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t214 = gm_f_extend_0cs85gs(t208, t213);
        gm_l109 = t214;
    }
    {
        uint8_t t215 = (uint8_t)!((((uint8_t *)&gm_l109))[0] != 0);
        if (!t215) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l109) + 1, gm_srcd5arorrf8jkg, 255, 5);
        {
            {
                uint8_t t216 = GM_LD(uint8_t, (((uint8_t *)a3) + 2));
                if (!t216) {
                    goto gm_lb16;
                }
                uint64_t t217 = GM_LD(uint64_t, ((((uint8_t *)a3) + 8) + 8));
                uint8_t t218 = (t217 > ((uint64_t)0ULL));
                if (!t218) {
                    goto gm_lb16;
                }
                gm_b16_8 t219 = GM_LD(gm_b16_8, (((uint8_t *)a3) + 8));
                gm_b16_8 t220 = gm_f_gdb_script_section_320gurp((void *)NULL, cx, NULL, t219);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l67 = t220;
                goto gm_lb15;
            }
gm_lb16:;
            gm_b16_8 t221;
            GM_ST(void *, (uint8_t *)&t221, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t221 + 8, (uintptr_t)(0));
            gm_l67 = t221;
        }
gm_lb15:;
        gm_trqfvmjtt4sij t222;
        gm_f_list_uvfu1k1((void *)&t222, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l68 = t222;
        void * t223 = (void *)((uint8_t *)&F.l68);
        void * t224 = (void *)(((uint8_t *)&F.l52) + 8);
        gm_b16_8 t225 = gm_f_descriptors_fnnm3h1((void *)NULL, cx, NULL, t224);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t226 = gm_f_extend_0cs85gs(t223, t225);
        gm_l110 = t226;
        uint8_t t227 = (uint8_t)!((((uint8_t *)&gm_l110))[0] != 0);
        if (!t227) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l110) + 1, gm_srcd5arorrf8jkg, 259, 5);
        gm_b16_8 t228 = GM_LD(gm_b16_8, (((uint8_t *)&F.l52) + 3392));
        gm_l69 = t228;
        gm_l70 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t230 = gm_l70;
                uint64_t t231 = GM_LD(uint64_t, (((uint8_t *)&gm_l69) + 8));
                uint8_t t232 = (t230 < t231);
                if (!t232) {
                    goto gm_lb17;
                }
                {
                    void * t233 = GM_LD(void *, ((uint8_t *)&gm_l69));
                    uint64_t t234 = GM_LD(uint64_t, (((uint8_t *)&gm_l69) + 8));
                    uint64_t t235 = gm_l70;
                    {
                        {
                            uint32_t t236 = GM_LD(uint32_t, (((uint8_t *)t233) + (uintptr_t)t235 * 4));
                            gm_l71 = t236;
                            void * t237 = (void *)((uint8_t *)&F.l68);
                            void * t238 = (void *)((uint8_t *)&F.l52);
                            uint32_t t239 = gm_l71;
                            gm_b16_8 t240 = gm_f_src_name_0r539f2((void *)NULL, cx, NULL, t238, t239);
                            if (gm_unw.kind) {
                                goto gm_ll229;
                            }
                            void * t241 = GM_LD(void *, ((uint8_t *)&F.l52));
                            void * t242 = GM_LD(void *, ((uint8_t *)t241));
                            void * t243 = GM_LD(void *, ((uint8_t *)t242));
                            void * t244 = GM_LD(void *, ((uint8_t *)t243));
                            uint32_t t245 = gm_l71;
                            void * t246 = gm_f_file_o15fqku(t244, t245);
                            gm_b16_8 t247 = GM_LD(gm_b16_8, ((uint8_t *)t246));
                            gm_b16_8 t248 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t247);
                            if (gm_unw.kind) {
                                goto gm_ll229;
                            }
                            gm_t6j6smj7qoctu t249;
                            GM_ST(gm_b16_8, ((uint8_t *)&t249 + 0), t240);
                            GM_ST(gm_b16_8, ((uint8_t *)&t249 + 16), t248);
                            gm_tftb1odc739t5 t250 = gm_f_write_hclpi1b(t237, &t249);
                            gm_l111 = t250;
                            uint8_t t251 = (uint8_t)!((((uint8_t *)&gm_l111))[0] != 0);
                            if (!t251) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l111) + 1, gm_srcd5arorrf8jkg, 261, 9);
                            goto gm_lb20;
                        }
gm_lb21:;
                        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 260, 5);
                    }
gm_lb20:;
                }
gm_lb19:;
                uint64_t t252 = gm_l70;
                uint64_t r253; bool o254 = __builtin_add_overflow(t252, ((uint64_t)1ULL), &r253);
                uint64_t t255 = r253;
                gm_l70 = t255;
            }
        }
        goto gm_ls256;
gm_ll229:;
        if (gm_unw.frame == &F.h && gm_unw.label == 2) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb17; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb19; }
        }
        goto gm_lad;
gm_ls256:;
gm_lb17:;
    }
    {
        void * t257 = (void *)((uint8_t *)&F.l68);
        gm_b16_8 t258 = GM_LD(gm_b16_8, (((uint8_t *)&F.l52) + 1808));
        gm_b16_8 t259 = GM_LD(gm_b16_8, ((uint8_t *)&t258));
        gm_tftb1odc739t5 t260 = gm_f_extend_0cs85gs(t257, t259);
        gm_l112 = t260;
        uint8_t t261 = (uint8_t)!((((uint8_t *)&gm_l112))[0] != 0);
        if (!t261) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l112) + 1, gm_srcd5arorrf8jkg, 263, 5);
        gm_b16_8 t262 = gm_l66;
        gm_b16_8 t263 = GM_LD(gm_b16_8, ((uint8_t *)&t262));
        gm_b16_8 t264 = GM_LD(gm_b16_8, (((uint8_t *)&F.l52) + 1904));
        gm_b16_8 t265 = GM_LD(gm_b16_8, ((uint8_t *)&t264));
        gm_b16_8 t266 = gm_f_guard_o7k9a0e((void *)NULL, cx, NULL, t263, t265);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l72 = t266;
        gm_b16_8 t267 = GM_LD(gm_b16_8, (((uint8_t *)&F.l52) + 1944));
        gm_b16_8 t268 = GM_LD(gm_b16_8, (((uint8_t *)&F.l52) + 2024));
        gm_t6j6smj7qoctu t269;
        GM_ST(gm_b16_8, ((uint8_t *)&t269 + 0), t267);
        GM_ST(gm_b16_8, ((uint8_t *)&t269 + 16), t268);
        gm_f_format_en16lfl((void *)&gm_l113, (void *)NULL, cx, NULL, &t269);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t270 = (uint8_t)!((((uint8_t *)&gm_l113))[0] != 0);
        if (!t270) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l113) + 8, gm_srcd5arorrf8jkg, 265, 15);
        gm_b16_8 t271 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l113) + 8));
        gm_l73 = t271;
        gm_trqfvmjtt4sij t272;
        gm_f_list_uvfu1k1((void *)&t272, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l74 = t272;
        void * t273 = (void *)((uint8_t *)&F.l74);
        gm_b16_8 t274;
        GM_ST(void *, (uint8_t *)&t274, (void *)(gm_s6aq8jknpl0n5));
        GM_ST(uintptr_t, (uint8_t *)&t274 + 8, (uintptr_t)(26));
        gm_tftb1odc739t5 t275 = gm_f_extend_0cs85gs(t273, t274);
        gm_l114 = t275;
        uint8_t t276 = (uint8_t)!((((uint8_t *)&gm_l114))[0] != 0);
        if (!t276) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l114) + 1, gm_srcd5arorrf8jkg, 268, 5);
        void * t277 = (void *)((uint8_t *)&F.l74);
        gm_b16_8 t278 = gm_l64;
        gm_b16_8 t279 = GM_LD(gm_b16_8, ((uint8_t *)&t278));
        gm_tftb1odc739t5 t280 = gm_f_extend_0cs85gs(t277, t279);
        gm_l115 = t280;
        uint8_t t281 = (uint8_t)!((((uint8_t *)&gm_l115))[0] != 0);
        if (!t281) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l115) + 1, gm_srcd5arorrf8jkg, 269, 5);
        void * t282 = (void *)((uint8_t *)&F.l74);
        gm_b16_8 t283 = GM_LD(gm_b16_8, ((uint8_t *)&F.l65));
        gm_b16_8 t284 = GM_LD(gm_b16_8, ((uint8_t *)&t283));
        gm_tftb1odc739t5 t285 = gm_f_extend_0cs85gs(t282, t284);
        gm_l116 = t285;
        uint8_t t286 = (uint8_t)!((((uint8_t *)&gm_l116))[0] != 0);
        if (!t286) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l116) + 1, gm_srcd5arorrf8jkg, 270, 5);
        void * t287 = (void *)((uint8_t *)&F.l74);
        gm_b16_8 t288 = gm_l67;
        gm_tftb1odc739t5 t289 = gm_f_extend_0cs85gs(t287, t288);
        gm_l117 = t289;
        uint8_t t290 = (uint8_t)!((((uint8_t *)&gm_l117))[0] != 0);
        if (!t290) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l117) + 1, gm_srcd5arorrf8jkg, 271, 5);
        uint64_t t291 = GM_LD(uint64_t, (((uint8_t *)&F.l74) + 8));
        F.l75 = t291;
        void * t292 = (void *)((uint8_t *)&F.l74);
        gm_b16_8 t293 = GM_LD(gm_b16_8, ((uint8_t *)&F.l68));
        gm_b16_8 t294 = GM_LD(gm_b16_8, ((uint8_t *)&t293));
        gm_tftb1odc739t5 t295 = gm_f_extend_0cs85gs(t292, t294);
        gm_l118 = t295;
        uint8_t t296 = (uint8_t)!((((uint8_t *)&gm_l118))[0] != 0);
        if (!t296) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l118) + 1, gm_srcd5arorrf8jkg, 273, 5);
        uint64_t t297 = GM_LD(uint64_t, (((uint8_t *)&F.l74) + 8));
        F.l76 = t297;
        void * t298 = (void *)((uint8_t *)&F.l74);
        gm_b16_8 t299 = gm_l72;
        gm_tftb1odc739t5 t300 = gm_f_extend_0cs85gs(t298, t299);
        gm_l119 = t300;
    }
    {
        uint8_t t301 = (uint8_t)!((((uint8_t *)&gm_l119))[0] != 0);
        if (!t301) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l119) + 1, gm_srcd5arorrf8jkg, 275, 5);
        void * t302 = (void *)((uint8_t *)&F.l74);
        gm_b16_8 t303 = gm_l73;
        gm_b16_8 t304 = GM_LD(gm_b16_8, ((uint8_t *)&t303));
        gm_tftb1odc739t5 t305 = gm_f_extend_0cs85gs(t302, t304);
        gm_l120 = t305;
        uint8_t t306 = (uint8_t)!((((uint8_t *)&gm_l120))[0] != 0);
        if (!t306) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l120) + 1, gm_srcd5arorrf8jkg, 276, 5);
        void * t307 = (void *)((uint8_t *)&F.l74);
        gm_b16_8 t308 = GM_LD(gm_b16_8, (((uint8_t *)&F.l52) + 3080));
        gm_b16_8 t309 = GM_LD(gm_b16_8, ((uint8_t *)&t308));
        gm_tftb1odc739t5 t310 = gm_f_extend_0cs85gs(t307, t309);
        gm_l121 = t310;
        uint8_t t311 = (uint8_t)!((((uint8_t *)&gm_l121))[0] != 0);
        if (!t311) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l121) + 1, gm_srcd5arorrf8jkg, 277, 5);
        void * t312 = (void *)((uint8_t *)&F.l74);
        gm_b16_8 t313 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l60));
        gm_tftb1odc739t5 t314 = gm_f_extend_0cs85gs(t312, t313);
        gm_l122 = t314;
        uint8_t t315 = (uint8_t)!((((uint8_t *)&gm_l122))[0] != 0);
        if (!t315) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l122) + 1, gm_srcd5arorrf8jkg, 278, 5);
        void * t316 = (void *)((uint8_t *)&F.l74);
        gm_b16_8 t317 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l60) + 16));
        gm_tftb1odc739t5 t318 = gm_f_extend_0cs85gs(t316, t317);
        gm_l123 = t318;
        uint8_t t319 = (uint8_t)!((((uint8_t *)&gm_l123))[0] != 0);
        if (!t319) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l123) + 1, gm_srcd5arorrf8jkg, 279, 5);
        void * t320 = (void *)((uint8_t *)&F.l74);
        gm_b16_8 t321 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l52) + 2024) + 40));
        gm_b16_8 t322 = GM_LD(gm_b16_8, ((uint8_t *)&t321));
        gm_tftb1odc739t5 t323 = gm_f_extend_0cs85gs(t320, t322);
        gm_l124 = t323;
        uint8_t t324 = (uint8_t)!((((uint8_t *)&gm_l124))[0] != 0);
        if (!t324) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l124) + 1, gm_srcd5arorrf8jkg, 280, 5);
        void * t325 = (void *)((uint8_t *)&F.l74);
        gm_b16_8 t326 = GM_LD(gm_b16_8, (((uint8_t *)&F.l52) + 1984));
        gm_b16_8 t327 = GM_LD(gm_b16_8, ((uint8_t *)&t326));
        gm_tftb1odc739t5 t328 = gm_f_extend_0cs85gs(t325, t327);
        gm_l125 = t328;
        uint8_t t329 = (uint8_t)!((((uint8_t *)&gm_l125))[0] != 0);
        if (!t329) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l125) + 1, gm_srcd5arorrf8jkg, 281, 5);
        gm_closure t330 = ((gm_closure){(void *)gm_f_code_has_n3tn5b6, (void *)&F.h, cx});
        gm_l77 = t330;
        {
            {
                void * t331 = gm_l0;
                uint8_t t332 = GM_LD(uint8_t, ((((uint8_t *)t331) + 80) + 57));
                uint8_t t333 = (uint8_t)!t332;
                if (!t333) {
                    goto gm_lb23;
                }
                gm_closure t334 = gm_l77;
                gm_b16_8 t335;
                GM_ST(void *, (uint8_t *)&t335, (void *)(gm_scj0sm2kpivk4));
                GM_ST(uintptr_t, (uint8_t *)&t335 + 8, (uintptr_t)(8));
                gm_closure t336 = t334;
                gm_node *ccx337 = t336.cx;
                void * t338 = t336.code;
                uint8_t t339;
                if (GM_PTR_TAGGED(t338)) {
                    t339 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t338))(t335);
                } else {
                    t339 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t338)(t336.env, ccx337, NULL, t335);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!t339) {
                    goto gm_lb23;
                }
                {
                    gm_tl874t2atnlh3 t340 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                    gm_b16_8 t341;
                    GM_ST(void *, (uint8_t *)&t341, (void *)(gm_sdbocdqbukr6k));
                    GM_ST(uintptr_t, (uint8_t *)&t341 + 8, (uintptr_t)(76));
                    gm_t08olq97rscn3 t342;
                    gm_f_unsupported_531q51r((void *)&t342, (void *)NULL, cx, NULL, t340, t341);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    memset(&(*(gm_tetdkgoreqmn1 *)gm_out), 0, sizeof (*(gm_tetdkgoreqmn1 *)gm_out));
                    ((uint8_t *)&(*(gm_tetdkgoreqmn1 *)gm_out))[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_tetdkgoreqmn1 *)gm_out) + 8), t342);
                    goto gm_fdone;
                }
            }
gm_lb23:;
        }
gm_lb22:;
        {
            {
                uint8_t t343 = gm_l62;
                if (!t343) {
                    goto gm_lb25;
                }
                gm_closure t344 = gm_l77;
                gm_b16_8 t345;
                GM_ST(void *, (uint8_t *)&t345, (void *)(gm_sdnjmvrkc1r7r));
                GM_ST(uintptr_t, (uint8_t *)&t345 + 8, (uintptr_t)(11));
                gm_closure t346 = t344;
                gm_node *ccx347 = t346.cx;
                void * t348 = t346.code;
                uint8_t t349;
                if (GM_PTR_TAGGED(t348)) {
                    t349 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t348))(t345);
                } else {
                    t349 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t348)(t346.env, ccx347, NULL, t345);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!t349) {
                    goto gm_lb25;
                }
                {
                    void * t350 = gm_l0;
                    gm_b16_8 t351 = GM_LD(gm_b16_8, ((((uint8_t *)t350) + 80) + 40));
                    gm_b16_8 t352;
                    GM_ST(void *, (uint8_t *)&t352, (void *)(gm_sje4rbc88csdu));
                    GM_ST(uintptr_t, (uint8_t *)&t352 + 8, (uintptr_t)(6));
                    gm_b16_8 t353;
                    GM_ST(void *, (uint8_t *)&t353, (void *)(gm_spervk999ikim));
                    GM_ST(uintptr_t, (uint8_t *)&t353 + 8, (uintptr_t)(7));
                    gm_b16_8 t354;
                    GM_ST(void *, (uint8_t *)&t354, (void *)(gm_sqg9do4ma3giu));
                    GM_ST(uintptr_t, (uint8_t *)&t354 + 8, (uintptr_t)(5));
                    gm_tfeab279c16rr t355;
                    GM_ST(gm_b16_8, ((uint8_t *)&t355 + 0), t352);
                    GM_ST(gm_b16_8, ((uint8_t *)&t355 + 16), t353);
                    GM_ST(gm_b16_8, ((uint8_t *)&t355 + 32), t354);
                    uint8_t t356 = 0;
                    if (gm_eq0iqiubfm7e(((uint8_t *)&t355) + 0, ((uint8_t *)&t351))) t356 = 1;
                    if (gm_eq0iqiubfm7e(((uint8_t *)&t355) + 16, ((uint8_t *)&t351))) t356 = 1;
                    if (gm_eq0iqiubfm7e(((uint8_t *)&t355) + 32, ((uint8_t *)&t351))) t356 = 1;
                    uint8_t t357 = (uint8_t)(!t356);
                    if (!t357) {
                        goto gm_lb25;
                    }
                }
                {
                    void * t358 = gm_l0;
                    gm_b16_8 t359 = GM_LD(gm_b16_8, ((((uint8_t *)t358) + 80) + 40));
                    gm_tnpifv20irnph t360;
                    GM_ST(gm_b16_8, ((uint8_t *)&t360 + 0), t359);
                    gm_f_format_vj1olp7((void *)&gm_l126, (void *)NULL, cx, NULL, t360);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t361 = (uint8_t)!((((uint8_t *)&gm_l126))[0] != 0);
                if (!t361) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l126) + 8, gm_srcd5arorrf8jkg, 289, 32);
                {
                    gm_b16_8 t362 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l126) + 8));
                    gm_b16_8 t363 = GM_LD(gm_b16_8, ((uint8_t *)&t362));
                    gm_tl874t2atnlh3 t364 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                    gm_t08olq97rscn3 t365;
                    GM_ST(uint32_t, (uint8_t *)&t365 + 28, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t365 + 0), t363);
                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t365 + 16), t364);
                    memset(&(*(gm_tetdkgoreqmn1 *)gm_out), 0, sizeof (*(gm_tetdkgoreqmn1 *)gm_out));
                    ((uint8_t *)&(*(gm_tetdkgoreqmn1 *)gm_out))[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_tetdkgoreqmn1 *)gm_out) + 8), t365);
                    goto gm_fdone;
                }
            }
gm_lb25:;
        }
gm_lb24:;
    }
    {
        gm_closure t366 = gm_l77;
        gm_b16_8 t367;
        GM_ST(void *, (uint8_t *)&t367, (void *)(gm_sdnjmvrkc1r7r));
        GM_ST(uintptr_t, (uint8_t *)&t367 + 8, (uintptr_t)(11));
        gm_closure t368 = t366;
        gm_node *ccx369 = t368.cx;
        void * t370 = t368.code;
        uint8_t t371;
        if (GM_PTR_TAGGED(t370)) {
            t371 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t370))(t367);
        } else {
            t371 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t370)(t368.env, ccx369, NULL, t367);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l78 = t371;
        uint8_t t372 = gm_l78;
        if (t372) {
            gm_b16_8 t373;
            GM_ST(void *, (uint8_t *)&t373, (void *)(gm_s5vn66tnlipdf));
            GM_ST(uintptr_t, (uint8_t *)&t373 + 8, (uintptr_t)(18));
            gm_l79 = t373;
        } else {
            gm_b16_8 t374;
            GM_ST(void *, (uint8_t *)&t374, (void *)(gm_sr3lr7c9psgf2));
            GM_ST(uintptr_t, (uint8_t *)&t374 + 8, (uintptr_t)(11));
            gm_l79 = t374;
        }
        uint64_t t375 = GM_LD(uint64_t, (((uint8_t *)&gm_l63) + 8));
        uint8_t t376 = ((t375 == ((uint64_t)0ULL)));
        if (t376) {
            gm_b16_8 t377;
            GM_ST(void *, (uint8_t *)&t377, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t377 + 8, (uintptr_t)(0));
            gm_l80 = t377;
        } else {
            {
                gm_b16_8 t378 = gm_l79;
                gm_b16_8 t379 = gm_l63;
                gm_t6j6smj7qoctu t380;
                GM_ST(gm_b16_8, ((uint8_t *)&t380 + 0), t378);
                GM_ST(gm_b16_8, ((uint8_t *)&t380 + 16), t379);
                gm_f_format_6gtuelb((void *)&gm_l127, (void *)NULL, cx, NULL, &t380);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t381 = (uint8_t)!((((uint8_t *)&gm_l127))[0] != 0);
            if (!t381) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l127) + 8, gm_srcd5arorrf8jkg, 294, 56);
            gm_b16_8 t382 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l127) + 8));
            gm_b16_8 t383 = GM_LD(gm_b16_8, ((uint8_t *)&t382));
            gm_l80 = t383;
        }
        {
            {
                void * t384 = gm_l0;
                gm_b16_8 t385 = GM_LD(gm_b16_8, ((((uint8_t *)t384) + 80) + 24));
                gm_b16_8 t386;
                GM_ST(void *, (uint8_t *)&t386, (void *)(gm_svl13be5ca8rt));
                GM_ST(uintptr_t, (uint8_t *)&t386 + 8, (uintptr_t)(4));
                uint8_t t387 = (gm_eq0iqiubfm7e(((uint8_t *)&t385), ((uint8_t *)&t386)));
                if (!t387) {
                    goto gm_lb27;
                }
                uint64_t t388 = GM_LD(uint64_t, (((uint8_t *)&gm_l61) + 8));
                uint8_t t389 = (t388 > ((uint64_t)0ULL));
                if (!t389) {
                    goto gm_lb27;
                }
                uint8_t t390 = gm_l78;
                if (t390) {
                    gm_b16_8 t391;
                    GM_ST(void *, (uint8_t *)&t391, (void *)(gm_s514u4evks75f));
                    GM_ST(uintptr_t, (uint8_t *)&t391 + 8, (uintptr_t)(20));
                    gm_l128 = t391;
                } else {
                    gm_b16_8 t392;
                    GM_ST(void *, (uint8_t *)&t392, (void *)(gm_s3nnnhoq632g4));
                    GM_ST(uintptr_t, (uint8_t *)&t392 + 8, (uintptr_t)(13));
                    gm_l128 = t392;
                }
                {
                    gm_b16_8 t393 = gm_l128;
                    gm_b16_8 t394 = gm_l61;
                    gm_t6j6smj7qoctu t395;
                    GM_ST(gm_b16_8, ((uint8_t *)&t395 + 0), t393);
                    GM_ST(gm_b16_8, ((uint8_t *)&t395 + 16), t394);
                    gm_f_format_eagqd9c((void *)&gm_l129, (void *)NULL, cx, NULL, &t395);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t396 = (uint8_t)!((((uint8_t *)&gm_l129))[0] != 0);
                if (!t396) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l129) + 8, gm_srcd5arorrf8jkg, 296, 19);
                gm_b16_8 t397 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l129) + 8));
                gm_b16_8 t398 = GM_LD(gm_b16_8, ((uint8_t *)&t397));
                gm_l61 = t398;
                goto gm_lb26;
            }
gm_lb27:;
        }
gm_lb26:;
        void * t399 = (void *)((uint8_t *)&F.l74);
        gm_b16_8 t400 = gm_l61;
        gm_tftb1odc739t5 t401 = gm_f_extend_0cs85gs(t399, t400);
        gm_l130 = t401;
        uint8_t t402 = (uint8_t)!((((uint8_t *)&gm_l130))[0] != 0);
        if (!t402) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l130) + 1, gm_srcd5arorrf8jkg, 298, 5);
        void * t403 = (void *)((uint8_t *)&F.l74);
        gm_b16_8 t404 = gm_l80;
        gm_tftb1odc739t5 t405 = gm_f_extend_0cs85gs(t403, t404);
        gm_l131 = t405;
        uint8_t t406 = (uint8_t)!((((uint8_t *)&gm_l131))[0] != 0);
        if (!t406) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l131) + 1, gm_srcd5arorrf8jkg, 299, 5);
    }
    {
        gm_b16_8 t407;
        GM_ST(void *, (uint8_t *)&t407, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t407 + 8, (uintptr_t)(0));
        gm_l81 = t407;
        uint8_t t408 = GM_LD(uint8_t, (((uint8_t *)a3) + 2));
        if (t408) {
            gm_b16_8 t409 = GM_LD(gm_b16_8, ((uint8_t *)gm_ccmgjp6th6r));
            gm_tnpifv20irnph t410;
            GM_ST(gm_b16_8, ((uint8_t *)&t410 + 0), t409);
            gm_f_format_uaq30dv((void *)&gm_l132, (void *)NULL, cx, NULL, t410);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t411 = (uint8_t)!((((uint8_t *)&gm_l132))[0] != 0);
            if (!t411) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l132) + 8, gm_srcd5arorrf8jkg, 301, 34);
            gm_b16_8 t412 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l132) + 8));
            gm_b16_8 t413 = GM_LD(gm_b16_8, ((uint8_t *)&t412));
            gm_l81 = t413;
        }
        gm_b16_8 t414 = GM_LD(gm_b16_8, ((uint8_t *)gm_c9e8ns5bad4));
        gm_l82 = t414;
        void * t415 = (void *)(((uint8_t *)&F.l52) + 8);
        gm_b16_8 t416 = gm_f_blob_typedefs_4nfn4l8((void *)NULL, cx, NULL, t415);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t417 = (void *)(((uint8_t *)&F.l52) + 8);
        gm_b16_8 t418 = gm_f_mirror_text_cr5muc9(t417);
        void * t419 = (void *)(((uint8_t *)&F.l52) + 8);
        gm_b16_8 t420 = gm_f_view_defs_u461okc(t419);
        gm_tfeab279c16rr t421;
        GM_ST(gm_b16_8, ((uint8_t *)&t421 + 0), t416);
        GM_ST(gm_b16_8, ((uint8_t *)&t421 + 16), t418);
        GM_ST(gm_b16_8, ((uint8_t *)&t421 + 32), t420);
        gm_f_format_dsta2ql((void *)&gm_l133, (void *)NULL, cx, NULL, &t421);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t422 = (uint8_t)!((((uint8_t *)&gm_l133))[0] != 0);
        if (!t422) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l133) + 8, gm_srcd5arorrf8jkg, 305, 27);
        gm_b16_8 t423 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l133) + 8));
        gm_b16_8 t424 = GM_LD(gm_b16_8, ((uint8_t *)&t423));
        gm_l83 = t424;
        gm_b16_8 t425 = GM_LD(gm_b16_8, (((uint8_t *)&F.l52) + 1904));
        gm_b16_8 t426 = GM_LD(gm_b16_8, ((uint8_t *)&t425));
        gm_l84 = t426;
        gm_b16_8 t427 = GM_LD(gm_b16_8, ((uint8_t *)&F.l65));
        gm_b16_8 t428 = gm_l72;
        gm_t6j6smj7qoctu t429;
        GM_ST(gm_b16_8, ((uint8_t *)&t429 + 0), t427);
        GM_ST(gm_b16_8, ((uint8_t *)&t429 + 16), t428);
        gm_f_format_5029eea((void *)&gm_l134, (void *)NULL, cx, NULL, &t429);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t430 = (uint8_t)!((((uint8_t *)&gm_l134))[0] != 0);
        if (!t430) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l134) + 8, gm_srcd5arorrf8jkg, 308, 15);
        gm_b16_8 t431 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l134) + 8));
        gm_b16_8 t432 = GM_LD(gm_b16_8, ((uint8_t *)&t431));
        gm_l85 = t432;
        gm_b16_8 t433 = gm_l73;
        gm_b16_8 t434 = GM_LD(gm_b16_8, ((uint8_t *)&t433));
        gm_l86 = t434;
        gm_b16_8 t435 = GM_LD(gm_b16_8, ((uint8_t *)&F.l68));
        gm_b16_8 t436 = GM_LD(gm_b16_8, (((uint8_t *)&F.l52) + 3080));
        gm_b16_8 t437 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l60));
        gm_tfeab279c16rr t438;
        GM_ST(gm_b16_8, ((uint8_t *)&t438 + 0), t435);
        GM_ST(gm_b16_8, ((uint8_t *)&t438 + 16), t436);
        GM_ST(gm_b16_8, ((uint8_t *)&t438 + 32), t437);
        gm_f_format_dcns5hm((void *)&gm_l135, (void *)NULL, cx, NULL, &t438);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t439 = (uint8_t)!((((uint8_t *)&gm_l135))[0] != 0);
        if (!t439) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l135) + 8, gm_srcd5arorrf8jkg, 310, 15);
        gm_b16_8 t440 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l135) + 8));
        gm_b16_8 t441 = GM_LD(gm_b16_8, ((uint8_t *)&t440));
        gm_l87 = t441;
        gm_b16_8 t442 = gm_l64;
        gm_b16_8 t443 = gm_l67;
        gm_t6j6smj7qoctu t444;
        GM_ST(gm_b16_8, ((uint8_t *)&t444 + 0), t442);
        GM_ST(gm_b16_8, ((uint8_t *)&t444 + 16), t443);
        gm_f_format_5029eea((void *)&gm_l136, (void *)NULL, cx, NULL, &t444);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t445 = (uint8_t)!((((uint8_t *)&gm_l136))[0] != 0);
        if (!t445) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l136) + 8, gm_srcd5arorrf8jkg, 311, 15);
        gm_b16_8 t446 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l136) + 8));
        gm_b16_8 t447 = GM_LD(gm_b16_8, ((uint8_t *)&t446));
        gm_l88 = t447;
    }
    {
        gm_b16_8 t448 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l52) + 2024) + 40));
        gm_b16_8 t449 = GM_LD(gm_b16_8, (((uint8_t *)&F.l52) + 1984));
        gm_t6j6smj7qoctu t450;
        GM_ST(gm_b16_8, ((uint8_t *)&t450 + 0), t448);
        GM_ST(gm_b16_8, ((uint8_t *)&t450 + 16), t449);
        gm_f_format_en16lfl((void *)&gm_l137, (void *)NULL, cx, NULL, &t450);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t451 = (uint8_t)!((((uint8_t *)&gm_l137))[0] != 0);
        if (!t451) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l137) + 8, gm_srcd5arorrf8jkg, 312, 16);
        gm_b16_8 t452 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l137) + 8));
        gm_b16_8 t453 = GM_LD(gm_b16_8, ((uint8_t *)&t452));
        gm_l89 = t453;
        void * t454 = (void *)((uint8_t *)&F.l52);
        gm_b16_8 t455 = gm_f_owned_d5psnvc((void *)NULL, cx, NULL, t454);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t456 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l52) + 2024) + 40));
        gm_b16_8 t457 = GM_LD(gm_b16_8, ((uint8_t *)&t456));
        gm_b16_8 t458 = gm_f_with_helpers_edhoqtc((void *)NULL, cx, NULL, t455, t457);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l90 = t458;
        gm_b16_8 t459 = gm_l82;
        gm_l91 = t459;
        gm_b16_8 t460 = gm_l83;
        gm_l92 = t460;
        gm_b16_8 t461 = gm_l84;
        gm_l93 = t461;
        gm_b16_8 t462 = gm_l66;
        gm_b16_8 t463 = GM_LD(gm_b16_8, ((uint8_t *)&t462));
        gm_l94 = t463;
        gm_b16_8 t464 = gm_l81;
        gm_b16_8 t465 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l60) + 16));
        gm_b16_8 t466 = gm_l61;
        gm_b16_8 t467 = gm_l80;
        gm_t0uuhaljsf410 t468;
        GM_ST(gm_b16_8, ((uint8_t *)&t468 + 0), t464);
        GM_ST(gm_b16_8, ((uint8_t *)&t468 + 16), t465);
        GM_ST(gm_b16_8, ((uint8_t *)&t468 + 32), t466);
        GM_ST(gm_b16_8, ((uint8_t *)&t468 + 48), t467);
        gm_f_format_k64sjdb((void *)&gm_l138, (void *)NULL, cx, NULL, &t468);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t469 = (uint8_t)!((((uint8_t *)&gm_l138))[0] != 0);
        if (!t469) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l138) + 8, gm_srcd5arorrf8jkg, 320, 15);
        gm_b16_8 t470 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l138) + 8));
        gm_b16_8 t471 = GM_LD(gm_b16_8, ((uint8_t *)&t470));
        gm_l95 = t471;
        gm_b16_8 t472 = gm_l85;
        gm_b16_8 t473 = gm_l86;
        gm_b16_8 t474 = gm_l87;
        gm_b16_8 t475 = gm_l88;
        gm_b16_8 t476 = gm_l89;
        gm_b16_8 t477 = gm_l95;
        gm_b16_8 t478 = gm_l90;
        gm_b16_8 t479 = gm_l91;
        gm_b16_8 t480 = gm_l92;
        gm_b16_8 t481 = gm_l93;
        gm_b16_8 t482 = gm_l94;
        GM_ST(gm_b16_8, ((uint8_t *)&F.l96 + 0), t472);
        GM_ST(gm_b16_8, ((uint8_t *)&F.l96 + 16), t473);
        GM_ST(gm_b16_8, ((uint8_t *)&F.l96 + 32), t474);
        GM_ST(gm_b16_8, ((uint8_t *)&F.l96 + 48), t475);
        GM_ST(gm_b16_8, ((uint8_t *)&F.l96 + 64), t476);
        GM_ST(gm_b16_8, ((uint8_t *)&F.l96 + 80), t477);
        GM_ST(gm_b16_8, ((uint8_t *)&F.l96 + 96), t478);
        GM_ST(gm_b16_8, ((uint8_t *)&F.l96 + 112), t479);
        GM_ST(gm_b16_8, ((uint8_t *)&F.l96 + 128), t480);
        GM_ST(gm_b16_8, ((uint8_t *)&F.l96 + 144), t481);
        GM_ST(gm_b16_8, ((uint8_t *)&F.l96 + 160), t482);
        gm_b16_8 t483 = GM_LD(gm_b16_8, ((uint8_t *)&F.l74));
        gm_b16_8 t484 = GM_LD(gm_b16_8, ((uint8_t *)&t483));
        gm_b16_8 t485 = GM_LD(gm_b16_8, (((uint8_t *)&F.l52) + 3240));
        gm_b16_8 t486 = GM_LD(gm_b16_8, ((uint8_t *)&t485));
        uint8_t t487 = gm_l62;
        uint8_t t488 = gm_l62;
        gm_tk5njge13tk1d t489 = GM_LD(gm_tk5njge13tk1d, ((uint8_t *)&F.l96));
        if (t488) {
            void * t490 = gm_l0;
            void * t491 = GM_LD(void *, ((uint8_t *)t490));
            {
                {
                    uint8_t t492 = (uint8_t)(((((uint8_t *)t491) + 960))[4] != 0);
                    if (t492) {
                        goto gm_lb29;
                    }
                    gm_l140 = ((uint8_t)0);
                    goto gm_lb28;
                }
gm_lb29:;
                {
                    gm_l140 = ((uint8_t)1);
                    goto gm_lb28;
                }
gm_lb30:;
                gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 322, 129);
            }
gm_lb28:;
            uint8_t t493 = gm_l140;
            gm_l139 = t493;
        } else {
            gm_l139 = ((uint8_t)0);
        }
        uint8_t t494 = gm_l139;
        gm_t0u6dopi4f21f t495;
        GM_ST(uint16_t, (uint8_t *)&t495 + 210, 0);
        GM_ST(uint32_t, (uint8_t *)&t495 + 212, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t495 + 0), t484);
        GM_ST(gm_tk5njge13tk1d, ((uint8_t *)&t495 + 16), t489);
        GM_ST(gm_b16_8, ((uint8_t *)&t495 + 192), t486);
        GM_ST(uint8_t, ((uint8_t *)&t495 + 208), t487);
        GM_ST(uint8_t, ((uint8_t *)&t495 + 209), t494);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tetdkgoreqmn1 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tetdkgoreqmn1 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tetdkgoreqmn1 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tetdkgoreqmn1 *)gm_out))[0] = 0;
        GM_ST(gm_t0u6dopi4f21f, ((uint8_t *)&(*(gm_tetdkgoreqmn1 *)gm_out) + 8), t495);
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
extern __attribute__((aligned(4))) void gm_f_generate_object_rgfvem4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
extern __attribute__((aligned(4))) void gm_f_generate_tests_462i0us(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_handler_table_2kdtlit(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_handler_table_2kdtlit F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* h */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint32_t gm_l5; /* c */
    GM_STACK_CHECK(&F, 593u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l6;
    gm_tqs5p4n2tq19v gm_l7;
    gm_tftb1odc739t5 gm_l8;
    gm_tabb710d9mq2e gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_tabb710d9mq2e gm_l11;
    gm_tftb1odc739t5 gm_l12;
    void * t10 = gm_l0;
    void * t11 = (void *)(((uint8_t *)t10) + 3280);
    uint32_t t12 = gm_l1;
    gm_tqs5p4n2tq19v t13 = gm_f_get_dk5gh28(t11, t12);
    gm_l7 = t13;
    {
        {
            uint8_t t14 = (uint8_t)((((uint8_t *)&gm_l7))[1] != 0);
            if (t14) {
                goto gm_lb2;
            }
            gm_l6 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l6 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 589, 8);
    }
gm_lb1:;
    uint8_t t15 = gm_l6;
    if (t15) {
        void * t16 = gm_l0;
        void * t17 = (void *)(((uint8_t *)t16) + 3280);
        uint32_t t18 = gm_l1;
        gm_tftb1odc739t5 t19 = gm_f_put_v6g5pup(t17, t18, ((uint8_t)1));
        gm_l8 = t19;
        uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
        if (!t20) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srcd5arorrf8jkg, 590, 9);
        {
            gm_trqfvmjtt4sij t21;
            gm_f_list_epvducg((void *)&t21, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l2 = t21;
        }
        void * t22 = gm_l0;
        void * t23 = GM_LD(void *, ((uint8_t *)t22));
        uint32_t t24 = gm_l1;
        void * t25 = gm_f_handler_lit_b02boff(t23, t24);
        gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)t25) + 24));
        gm_l3 = t26;
        gm_l4 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t27 = gm_l4;
                uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint8_t t29 = (t27 < t28);
                if (!t29) {
                    goto gm_lb4;
                }
                {
                    void * t30 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    uint64_t t32 = gm_l4;
                    {
                        {
                            uint32_t t33 = GM_LD(uint32_t, (((uint8_t *)t30) + (uintptr_t)t32 * 4));
                            gm_l5 = t33;
                            void * t34 = gm_l0;
                            uint32_t t35 = gm_l5;
                            gm_b0_1 t36 = gm_f_queue_cd51r8c((void *)NULL, cx, NULL, t34, t35);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t37 = (void *)((uint8_t *)&F.l2);
                            void * t38 = gm_l0;
                            uint32_t t39 = gm_l5;
                            gm_b16_8 t40 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t38, t39);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tnpifv20irnph t41;
                            GM_ST(gm_b16_8, ((uint8_t *)&t41 + 0), t40);
                            gm_f_format_be3fee1((void *)&gm_l9, (void *)NULL, cx, NULL, t41);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t42 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                            if (!t42) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srcd5arorrf8jkg, 594, 24);
                            gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
                            gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
                            gm_tftb1odc739t5 t45 = gm_f_push_3ef746b(t37, t44);
                            gm_l10 = t45;
                            uint8_t t46 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                            if (!t46) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srcd5arorrf8jkg, 594, 13);
                            goto gm_lb7;
                        }
gm_lb8:;
                        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 592, 9);
                    }
gm_lb7:;
                }
gm_lb6:;
                uint64_t t47 = gm_l4;
                uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                uint64_t t50 = r48;
                gm_l4 = t50;
            }
        }
gm_lb4:;
        {
            void * t51 = gm_l0;
            void * t52 = (void *)(((uint8_t *)t51) + 3080);
            void * t53 = gm_l0;
            uint32_t t54 = gm_l1;
            gm_b16_8 t55 = gm_f_handler_name_6km8n3f((void *)NULL, cx, NULL, t53, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
            gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
            gm_b16_8 t58;
            GM_ST(void *, (uint8_t *)&t58, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(2));
            gm_f_join_sfknljb((void *)&gm_l11, (void *)NULL, cx, NULL, t57, t58);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t59 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
            if (!t59) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_srcd5arorrf8jkg, 596, 90);
            gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
            gm_t6j6smj7qoctu t61;
            GM_ST(gm_b16_8, ((uint8_t *)&t61 + 0), t55);
            GM_ST(gm_b16_8, ((uint8_t *)&t61 + 16), t60);
            gm_tftb1odc739t5 t62 = gm_f_write_b3knhd6(t52, &t61);
            gm_l12 = t62;
        }
        uint8_t t63 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
        if (!t63) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srcd5arorrf8jkg, 596, 9);
    }
    void * t64 = gm_l0;
    uint32_t t65 = gm_l1;
    gm_b16_8 t66 = gm_f_handler_name_6km8n3f((void *)NULL, cx, NULL, t64, t65);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t66;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_plain_qr08i40(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_is_plain_qr08i40 F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* def */
    GM_STACK_CHECK(&F, 106u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l3;
    uint8_t gm_l4;
    uint8_t gm_l5;
    void * t12 = gm_l0;
    void * t13 = GM_LD(void *, ((uint8_t *)t12));
    uint32_t t14 = gm_l1;
    void * t15 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t13, t14);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t15;
    void * t16 = gm_l2;
    {
        {
            uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t16) + 96) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)3ULL));
            if (!t18) {
                goto gm_lb2;
            }
            gm_l3 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            {
                {
                    uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t16) + 96) + 0));
                    uint8_t t20 = (uint8_t)(t19 == ((uint8_t)0ULL));
                    if (!t20) {
                        goto gm_lb5;
                    }
                    goto gm_lb4;
                }
gm_lb5:;
                {
                    uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t16) + 96) + 0));
                    uint8_t t22 = (uint8_t)(t21 == ((uint8_t)1ULL));
                    if (!t22) {
                        goto gm_lb6;
                    }
                    goto gm_lb4;
                }
gm_lb6:;
                goto gm_lb3;
            }
gm_lb4:;
            void * t23 = gm_l2;
            {
                {
                    uint8_t t24 = (uint8_t)(((((uint8_t *)t23) + 88))[4] != 0);
                    if (t24) {
                        goto gm_lb8;
                    }
                    gm_l5 = ((uint8_t)1);
                    goto gm_lb7;
                }
gm_lb8:;
                {
                    gm_l5 = ((uint8_t)0);
                    goto gm_lb7;
                }
gm_lb9:;
                gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 901, 29);
            }
gm_lb7:;
            uint8_t t25 = gm_l5;
            if (t25) {
                void * t26 = gm_l0;
                void * t27 = gm_l2;
                void * t28 = (void *)(((uint8_t *)t27) + 72);
                uint8_t t29 = gm_f_within_sys_2lc2ugq(t26, t28);
                gm_l4 = t29;
            } else {
                gm_l4 = ((uint8_t)0);
            }
            uint8_t t30 = gm_l4;
            gm_l3 = t30;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t16) + 96) + 0));
            uint8_t t32 = (uint8_t)(t31 == ((uint8_t)2ULL));
            if (!t32) {
                goto gm_lb10;
            }
            gm_l3 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb10:;
        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 899, 12);
    }
gm_lb1:;
    uint8_t t33 = gm_l3;
    F.ret = t33;
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
inline __attribute__((aligned(4))) void gm_f_line_directive_fjg94gc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_line_directive_fjg94gc F;
    void * gm_l0; /* g */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* f */
    gm_th2lqbo77pvsc gm_l3; /* path */
    gm_th2lqbo77pvsc gm_l4; /* cached */
    gm_b16_8 gm_l5; /* abs */
    uint8_t gm_l6; /* stat_failed */
    gm_b16_8 gm_l7; /* p */
    gm_tcc1rq4n1avb2 gm_l8; /* lc */
    GM_STACK_CHECK(&F, 593u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l9;
    gm_ti5ramhglnvdt gm_l10;
    gm_tih6h4avv5nrb gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_tabb710d9mq2e gm_l13;
    void * t7 = gm_l0;
    uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t7) + 1136) + 2));
    uint8_t t9 = (uint8_t)!t8;
    if (t9) {
        gm_l9 = ((uint8_t)1);
    } else {
        gm_tl874t2atnlh3 t10 = gm_l1;
        gm_tl874t2atnlh3 t11 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
        uint8_t t12 = (gm_eqff85ac10o3(((uint8_t *)&t10), ((uint8_t *)&t11)));
        gm_l9 = t12;
    }
    uint8_t t13 = gm_l9;
    if (t13) {
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
    void * t14 = gm_l0;
    void * t15 = GM_LD(void *, ((uint8_t *)t14));
    void * t16 = GM_LD(void *, ((uint8_t *)t15));
    void * t17 = GM_LD(void *, ((uint8_t *)t16));
    void * t18 = GM_LD(void *, ((uint8_t *)t17));
    uint32_t t19 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    void * t20 = gm_f_file_o15fqku(t18, t19);
    gm_l2 = t20;
    {
        memset(&gm_l3, 0, sizeof gm_l3);
    }
    {
        {
            {
                void * t21 = gm_l0;
                void * t22 = (void *)(((uint8_t *)t21) + 3432);
                uint32_t t23 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
                gm_f_get_9k2rdij((void *)&gm_l10, t22, t23);
            }
            uint8_t t24 = (uint8_t)((((uint8_t *)&gm_l10))[24] != 0);
            if (!t24) {
                goto gm_lb2;
            }
            {
                memmove(((uint8_t *)&gm_l4), ((uint8_t *)&gm_l10), 24);
            }
            {
                memmove(((uint8_t *)&gm_l3), ((uint8_t *)&gm_l4), 24);
            }
            goto gm_lb1;
        }
gm_lb2:;
        void * t25 = gm_l2;
        gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)t25));
        gm_b16_8 t27 = gm_f_absolute_6pqhsaj((void *)NULL, cx, NULL, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t27;
        {
            gm_b16_8 t28 = gm_l5;
            gm_node *pn29 = gm_find(cx, 2126215853);
            if (!pn29) gm_trap("no handler installed for effect `Fs`", gm_srcd5arorrf8jkg, 562, 24);
            gm_tih6h4avv5nrb t30;
            ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn29->clauses[5])((void *)&t30, pn29->env, pn29->hctx, pn29, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t30;
        }
        {
            {
                uint8_t t31 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                if (t31) {
                    goto gm_lb4;
                }
                gm_l6 = ((uint8_t)1);
                goto gm_lb3;
            }
gm_lb4:;
            {
                gm_l6 = ((uint8_t)0);
                goto gm_lb3;
            }
gm_lb5:;
            gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 562, 24);
        }
gm_lb3:;
        uint8_t t32 = gm_l6;
        uint8_t t33 = (uint8_t)!t32;
        if (t33) {
            gm_b16_8 t34 = gm_l5;
            gm_b16_8 t35 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&gm_l3 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&gm_l3 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&gm_l3 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&gm_l3), t35);
            ((uint8_t *)&gm_l3)[16] = 1;
        }
        {
            void * t36 = gm_l0;
            void * t37 = (void *)(((uint8_t *)t36) + 3432);
            uint32_t t38 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
            gm_tftb1odc739t5 t39 = gm_f_put_dm7u1sg(t37, t38, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)));
            gm_l12 = t39;
        }
        uint8_t t40 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
        if (!t40) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srcd5arorrf8jkg, 564, 9);
    }
gm_lb1:;
    uint8_t t41 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
    if (t41) {
        gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
        gm_l7 = t42;
    } else {
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
    void * t43 = gm_l2;
    uint32_t t44 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    gm_tcc1rq4n1avb2 t45 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t43, t44));
    gm_l8 = t45;
    {
        uint32_t t46 = GM_LD(uint32_t, ((uint8_t *)&gm_l8));
        gm_b16_8 t47 = gm_l7;
        gm_tiilmfd16v3fc t48;
        GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t48 + 0), t46);
        GM_ST(gm_b16_8, ((uint8_t *)&t48 + 8), t47);
        gm_f_format_v6efj7n((void *)&gm_l13, (void *)NULL, cx, NULL, &t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t49 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
    if (!t49) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_srcd5arorrf8jkg, 568, 12);
    {
        gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
        gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&t50));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t51);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_mark_owner_uhhhf9f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tgqgdb97ka5kt a1) {
    struct gm_fr_mark_owner_uhhhf9f F;
    void * gm_l0; /* g */
    gm_tgqgdb97ka5kt gm_l1; /* owner */
    uint64_t gm_l2; /* n */
    gm_tgqgdb97ka5kt gm_l3; /* last */
    GM_STACK_CHECK(&F, 216u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l4;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t4) + 1712) + 8));
    gm_l2 = t5;
    uint64_t t6 = gm_l2;
    uint8_t t7 = (t6 > ((uint64_t)0ULL));
    if (t7) {
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 1712));
        uint64_t t10 = GM_LD(uint64_t, ((((uint8_t *)t8) + 1712) + 8));
        uint64_t t11 = gm_l2;
        uint64_t r12; bool o13 = __builtin_sub_overflow(t11, ((uint64_t)1ULL), &r12);
        if (o13) gm_trap("integer overflow in `-`", gm_srcd5arorrf8jkg, 435, 37);
        uint64_t t14 = r12;
        if ((uintptr_t)t14 >= (uintptr_t)(t10)) gm_trap_index((uintptr_t)t14, t10, gm_srcd5arorrf8jkg, 435, 17);
        gm_tgqgdb97ka5kt t15 = GM_LD(gm_tgqgdb97ka5kt, ((((uint8_t *)t9) + (uintptr_t)t14 * 24) + 8));
        gm_l3 = t15;
        {
            {
                gm_t9805qtonbsag t16 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)&gm_l3));
                gm_t9805qtonbsag t17 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)&gm_l1));
                uint8_t t18 = (gm_eqkricceou4u(((uint8_t *)&t16), ((uint8_t *)&t17)));
                if (!t18) {
                    goto gm_lb2;
                }
                uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 8));
                uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
                uint8_t t21 = ((t19 == t20));
                if (!t21) {
                    goto gm_lb2;
                }
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
gm_lb2:;
        }
gm_lb1:;
    }
    void * t22 = gm_l0;
    void * t23 = (void *)(((uint8_t *)t22) + 1712);
    void * t24 = gm_l0;
    uint64_t t25 = GM_LD(uint64_t, ((((uint8_t *)t24) + 1984) + 8));
    gm_tgqgdb97ka5kt t26 = gm_l1;
    gm_tc42mm7h7sccr t27;
    GM_ST(uint32_t, (uint8_t *)&t27 + 20, 0);
    GM_ST(uint64_t, ((uint8_t *)&t27 + 0), t25);
    GM_ST(gm_tgqgdb97ka5kt, ((uint8_t *)&t27 + 8), t26);
    gm_tftb1odc739t5 t28 = gm_f_push_cn89s92(t23, &t27);
    gm_l4 = t28;
    uint8_t t29 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t29) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srcd5arorrf8jkg, 438, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_may_unwind_irl2ktm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_may_unwind_irl2ktm F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* def */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint32_t gm_l5; /* p */
    GM_STACK_CHECK(&F, 321u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l6;
    gm_t53at80pfdus3 gm_l7;
    void * t17 = gm_l0;
    void * t18 = GM_LD(void *, ((uint8_t *)t17));
    uint32_t t19 = gm_l1;
    void * t20 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t18, t19);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t20;
    void * t21 = gm_l2;
    {
        {
            uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t21) + 96) + 0));
            uint8_t t23 = (uint8_t)(t22 == ((uint8_t)3ULL));
            if (!t23) {
                goto gm_lb2;
            }
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb2:;
        {
            {
                {
                    uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t21) + 96) + 0));
                    uint8_t t25 = (uint8_t)(t24 == ((uint8_t)2ULL));
                    if (!t25) {
                        goto gm_lb5;
                    }
                    goto gm_lb4;
                }
gm_lb5:;
                {
                    uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t21) + 96) + 0));
                    uint8_t t27 = (uint8_t)(t26 == ((uint8_t)1ULL));
                    if (!t27) {
                        goto gm_lb6;
                    }
                    goto gm_lb4;
                }
gm_lb6:;
                goto gm_lb3;
            }
gm_lb4:;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb3:;
        {
            uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t21) + 96) + 0));
            uint8_t t29 = (uint8_t)(t28 == ((uint8_t)0ULL));
            if (!t29) {
                goto gm_lb7;
            }
            void * t30 = gm_l0;
            void * t31 = gm_l2;
            void * t32 = (void *)(((uint8_t *)t31) + 72);
            uint8_t t33 = gm_f_within_sys_2lc2ugq(t30, t32);
            uint8_t t34 = (uint8_t)!t33;
            if (t34) {
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            void * t35 = gm_l2;
            gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)t35) + 32));
            gm_l3 = t36;
            gm_l4 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t37 = gm_l4;
                    uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    uint8_t t39 = (t37 < t38);
                    if (!t39) {
                        goto gm_lb8;
                    }
                    {
                        void * t40 = GM_LD(void *, ((uint8_t *)&gm_l3));
                        uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                        uint64_t t42 = gm_l4;
                        {
                            {
                                uint32_t t43 = GM_LD(uint32_t, (((uint8_t *)t40) + (uintptr_t)t42 * 4));
                                gm_l5 = t43;
                                void * t44 = gm_l0;
                                void * t45 = (void *)(((uint8_t *)t44) + 8);
                                void * t46 = gm_l2;
                                void * t47 = GM_LD(void *, (((uint8_t *)t46) + 48));
                                uint64_t t48 = GM_LD(uint64_t, ((((uint8_t *)t46) + 48) + 8));
                                uint32_t t49 = gm_l5;
                                uint64_t t50 = (uint64_t)t49;
                                if ((uintptr_t)t50 >= (uintptr_t)(t48)) gm_trap_index((uintptr_t)t50, t48, gm_srcd5arorrf8jkg, 880, 30);
                                gm_t88rcucg1b7uk t51 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t47) + (uintptr_t)t50 * 40) + 16));
                                gm_f_kind_ghvav22((void *)&gm_l7, t45, GM_CONV(gm_csguankko4lmhd, t51));
                                {
                                    {
                                        uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                                        uint8_t t53 = (uint8_t)(t52 == ((uint8_t)15ULL));
                                        if (!t53) {
                                            goto gm_lb14;
                                        }
                                        gm_l6 = ((uint8_t)1);
                                        goto gm_lb13;
                                    }
gm_lb14:;
                                    {
                                        gm_l6 = ((uint8_t)0);
                                        goto gm_lb13;
                                    }
gm_lb15:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 880, 20);
                                }
gm_lb13:;
                                uint8_t t54 = gm_l6;
                                if (t54) {
                                    F.ret = ((uint8_t)1);
                                    goto gm_fret;
                                }
                                goto gm_lb11;
                            }
gm_lb12:;
                            gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 879, 13);
                        }
gm_lb11:;
                    }
gm_lb10:;
                    uint64_t t55 = gm_l4;
                    uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                    uint64_t t58 = r56;
                    gm_l4 = t58;
                }
            }
gm_lb8:;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb7:;
        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 874, 5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_needs_wrapper_ktlbb7i(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_needs_wrapper_ktlbb7i F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* def */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint32_t gm_l5; /* p */
    GM_STACK_CHECK(&F, 277u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l6;
    uint8_t gm_l7;
    void * t10 = gm_l0;
    void * t11 = GM_LD(void *, ((uint8_t *)t10));
    uint32_t t12 = gm_l1;
    void * t13 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t11, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t13;
    void * t14 = gm_l2;
    {
        {
            uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t14) + 96) + 0));
            uint8_t t16 = (uint8_t)(t15 == ((uint8_t)3ULL));
            if (!t16) {
                goto gm_lb2;
            }
            gm_l7 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l7 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 916, 8);
    }
gm_lb1:;
    uint8_t t17 = gm_l7;
    if (t17) {
        gm_l6 = ((uint8_t)1);
    } else {
        void * t18 = gm_l0;
        uint32_t t19 = gm_l1;
        uint8_t t20 = gm_f_is_plain_qr08i40((void *)NULL, cx, NULL, t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t21 = (uint8_t)!t20;
        gm_l6 = t21;
    }
    uint8_t t22 = gm_l6;
    if (t22) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t23 = gm_l2;
    gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)t23) + 32));
    gm_l3 = t24;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t25 = gm_l4;
            uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t27 = (t25 < t26);
            if (!t27) {
                goto gm_lb4;
            }
            {
                void * t28 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t30 = gm_l4;
                {
                    {
                        uint32_t t31 = GM_LD(uint32_t, (((uint8_t *)t28) + (uintptr_t)t30 * 4));
                        gm_l5 = t31;
                        void * t32 = gm_l0;
                        void * t33 = (void *)(((uint8_t *)t32) + 8);
                        void * t34 = gm_l2;
                        void * t35 = GM_LD(void *, (((uint8_t *)t34) + 48));
                        uint64_t t36 = GM_LD(uint64_t, ((((uint8_t *)t34) + 48) + 8));
                        uint32_t t37 = gm_l5;
                        uint64_t t38 = (uint64_t)t37;
                        if ((uintptr_t)t38 >= (uintptr_t)(t36)) gm_trap_index((uintptr_t)t38, t36, gm_srcd5arorrf8jkg, 918, 24);
                        gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t35) + (uintptr_t)t38 * 40) + 16));
                        uint8_t t40 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t33, t39);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (t40) {
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto gm_lb7;
                    }
gm_lb8:;
                    gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 917, 5);
                }
gm_lb7:;
            }
gm_lb6:;
            uint64_t t41 = gm_l4;
            uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
            uint64_t t44 = r42;
            gm_l4 = t44;
        }
    }
gm_lb4:;
    void * t45 = gm_l0;
    void * t46 = (void *)(((uint8_t *)t45) + 8);
    void * t47 = gm_l2;
    gm_t88rcucg1b7uk t48 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t47) + 64));
    uint8_t t49 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t46, t48);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t49;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_owned_d5psnvc(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_owned_d5psnvc F;
    void * gm_l0; /* g */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* k */
    gm_tc42mm7h7sccr gm_l7; /* m */
    uint64_t gm_l8; /* end */
    gm_b16_8 gm_l9; /* path */
    uint32_t gm_l10; /* file */
    uint64_t gm_l11; /* i */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* i */
    gm_b16_8 gm_l16; /* p */
    GM_STACK_CHECK(&F, 1504u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t5c6io15et1sc gm_l17;
    gm_tabb710d9mq2e gm_l18;
    uint8_t gm_l19;
    gm_tf5srh4mp4b3i gm_l20;
    gm_tftb1odc739t5 gm_l21;
    gm_tftb1odc739t5 gm_l22;
    gm_tftb1odc739t5 gm_l23;
    gm_tf5srh4mp4b3i gm_l24;
    gm_tftb1odc739t5 gm_l25;
    gm_tf1bjg81u0glv gm_l26;
    gm_tftb1odc739t5 gm_l27;
    {
        gm_t4c7ipjj9s3fp t17;
        gm_f_hash_map_lljdp3h((void *)&t17, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t17;
    }
    {
        gm_trqfvmjtt4sij t18;
        gm_f_list_epvducg((void *)&t18, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t18;
    }
    {
        gm_trqfvmjtt4sij t19;
        gm_f_list_lj5vg1g((void *)&t19, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l3 = t19;
    }
    void * t20 = gm_l0;
    gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)t20) + 1712));
    gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
    gm_l4 = t22;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t23 = gm_l5;
            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t25 = (t23 < t24);
            if (!t25) {
                goto gm_lb1;
            }
            {
                {
                    uint64_t t26 = gm_l5;
                    void * t27 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint64_t t29 = gm_l5;
                    gm_tc42mm7h7sccr t30 = GM_LD(gm_tc42mm7h7sccr, (((uint8_t *)t27) + (uintptr_t)t29 * 24));
                    GM_ST(uint64_t, ((uint8_t *)&gm_l17 + 0), t26);
                    GM_ST(gm_tc42mm7h7sccr, ((uint8_t *)&gm_l17 + 8), t30);
                }
                {
                    {
                        uint64_t t31 = GM_LD(uint64_t, ((uint8_t *)&gm_l17));
                        gm_l6 = t31;
                        memmove(((uint8_t *)&gm_l7), (((uint8_t *)&gm_l17) + 8), 24);
                        uint64_t t32 = gm_l6;
                        uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                        if (o34) gm_trap("integer overflow in `+`", gm_srcd5arorrf8jkg, 448, 19);
                        uint64_t t35 = r33;
                        void * t36 = gm_l0;
                        uint64_t t37 = GM_LD(uint64_t, ((((uint8_t *)t36) + 1712) + 8));
                        uint8_t t38 = (t35 < t37);
                        if (t38) {
                            void * t39 = gm_l0;
                            void * t40 = GM_LD(void *, (((uint8_t *)t39) + 1712));
                            uint64_t t41 = GM_LD(uint64_t, ((((uint8_t *)t39) + 1712) + 8));
                            uint64_t t42 = gm_l6;
                            uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
                            if (o44) gm_trap("integer overflow in `+`", gm_srcd5arorrf8jkg, 448, 73);
                            uint64_t t45 = r43;
                            if ((uintptr_t)t45 >= (uintptr_t)(t41)) gm_trap_index((uintptr_t)t45, t41, gm_srcd5arorrf8jkg, 448, 53);
                            uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)t40) + (uintptr_t)t45 * 24));
                            gm_l8 = t46;
                        } else {
                            void * t47 = gm_l0;
                            uint64_t t48 = GM_LD(uint64_t, ((((uint8_t *)t47) + 1984) + 8));
                            gm_l8 = t48;
                        }
                        uint64_t t49 = gm_l8;
                        uint64_t t50 = GM_LD(uint64_t, ((uint8_t *)&gm_l7));
                        uint8_t t51 = (t49 <= t50);
                        if (t51) {
                            goto gm_lb3;
                        }
                        gm_b16_8 t52;
                        GM_ST(void *, (uint8_t *)&t52, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(0));
                        gm_l9 = t52;
                        {
                            {
                                uint8_t t53 = (uint8_t)(((((uint8_t *)&gm_l7) + 8))[4] != 0);
                                if (!t53) {
                                    goto gm_lb7;
                                }
                                uint32_t t54 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 8));
                                gm_l10 = t54;
                                void * t55 = gm_l0;
                                void * t56 = GM_LD(void *, ((uint8_t *)t55));
                                void * t57 = GM_LD(void *, ((uint8_t *)t56));
                                void * t58 = GM_LD(void *, ((uint8_t *)t57));
                                void * t59 = GM_LD(void *, ((uint8_t *)t58));
                                uint32_t t60 = gm_l10;
                                void * t61 = gm_f_file_o15fqku(t59, t60);
                                gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)t61));
                                gm_l9 = t62;
                                uint32_t t63 = GM_LD(uint32_t, ((((uint8_t *)&gm_l7) + 8) + 8));
                                uint8_t t64 = (t63 > ((uint32_t)0ULL));
                                if (t64) {
                                    {
                                        gm_b16_8 t65 = gm_l9;
                                        uint32_t t66 = GM_LD(uint32_t, ((((uint8_t *)&gm_l7) + 8) + 8));
                                        gm_tommf4j9upomc t67;
                                        GM_ST(uint32_t, (uint8_t *)&t67 + 20, 0);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t67 + 0), t65);
                                        GM_ST(uint32_t, ((uint8_t *)&t67 + 16), t66);
                                        gm_f_format_t3l2mea((void *)&gm_l18, (void *)NULL, cx, NULL, &t67);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    uint8_t t68 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                                    if (!t68) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_srcd5arorrf8jkg, 453, 43);
                                    gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
                                    gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
                                    gm_l9 = t70;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        void * t71 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t72 = gm_l9;
                        gm_tf5srh4mp4b3i t73 = gm_f_get_espvf96(t71, t72);
                        gm_l20 = t73;
                        {
                            {
                                uint8_t t74 = (uint8_t)((((uint8_t *)&gm_l20))[8] != 0);
                                if (t74) {
                                    goto gm_lb9;
                                }
                                gm_l19 = ((uint8_t)1);
                                goto gm_lb8;
                            }
gm_lb9:;
                            {
                                gm_l19 = ((uint8_t)0);
                                goto gm_lb8;
                            }
gm_lb10:;
                            gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 455, 12);
                        }
gm_lb8:;
                        uint8_t t75 = gm_l19;
                        if (t75) {
                            void * t76 = (void *)((uint8_t *)&F.l1);
                            gm_b16_8 t77 = gm_l9;
                            uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                            gm_tftb1odc739t5 t79 = gm_f_put_qoojoqv(t76, t77, t78);
                            gm_l21 = t79;
                            uint8_t t80 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                            if (!t80) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srcd5arorrf8jkg, 456, 13);
                            void * t81 = (void *)((uint8_t *)&F.l2);
                            gm_b16_8 t82 = gm_l9;
                            gm_tftb1odc739t5 t83 = gm_f_push_3ef746b(t81, t82);
                            gm_l22 = t83;
                            uint8_t t84 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                            if (!t84) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 1, gm_srcd5arorrf8jkg, 457, 13);
                            void * t85 = (void *)((uint8_t *)&F.l3);
                            gm_trqfvmjtt4sij t86;
                            gm_f_list_uvfu1k1((void *)&t86, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tftb1odc739t5 t87 = gm_f_push_97b8r3k(t85, &t86);
                            gm_l23 = t87;
                            uint8_t t88 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                            if (!t88) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_srcd5arorrf8jkg, 458, 13);
                        }
                        void * t89 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t90 = gm_l9;
                        gm_tf5srh4mp4b3i t91 = gm_f_get_espvf96(t89, t90);
                        gm_l24 = t91;
                        uint8_t t92 = (uint8_t)((((uint8_t *)&gm_l24))[8] != 0);
                        if (t92) {
                            uint64_t t93 = GM_LD(uint64_t, ((uint8_t *)&gm_l24));
                            gm_l11 = t93;
                        } else {
                            gm_trap("reached `@unreachable()`", gm_srcd5arorrf8jkg, 460, 37);
                        }
                        void * t94 = GM_LD(void *, ((uint8_t *)&F.l3));
                        uint64_t t95 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                        uint64_t t96 = gm_l11;
                        if ((uintptr_t)t96 >= (uintptr_t)(t95)) gm_trap_index((uintptr_t)t96, t95, gm_srcd5arorrf8jkg, 461, 9);
                        void * t97 = (void *)(((uint8_t *)t94) + (uintptr_t)t96 * 40);
                        void * t98 = gm_l0;
                        void * t99 = GM_LD(void *, (((uint8_t *)t98) + 1984));
                        uint64_t t100 = GM_LD(uint64_t, ((((uint8_t *)t98) + 1984) + 8));
                        uint64_t t101 = GM_LD(uint64_t, ((uint8_t *)&gm_l7));
                        uint64_t t102 = gm_l8;
                        uint64_t t103 = (uintptr_t)t101;
                        uint64_t t104 = (uintptr_t)t102;
                        if (t103 > t104 || t104 > (uintptr_t)t100) gm_trap_slice(t103, t104, t100, gm_srcd5arorrf8jkg, 461, 31);
                        uint64_t t105 = ((uintptr_t)t102) - (uintptr_t)t101;
                        gm_b16_8 t106;
                        GM_ST(void *, (uint8_t *)&t106, (void *)(((uint8_t *)t99 + (uintptr_t)t101 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t106 + 8, (uintptr_t)(t105));
                        gm_b16_8 t107 = GM_LD(gm_b16_8, ((uint8_t *)&t106));
                        gm_tftb1odc739t5 t108 = gm_f_extend_0cs85gs(t97, t107);
                        gm_l25 = t108;
                        uint8_t t109 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                        if (!t109) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_srcd5arorrf8jkg, 461, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 447, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t110 = gm_l5;
            uint64_t r111; bool o112 = __builtin_add_overflow(t110, ((uint64_t)1ULL), &r111);
            uint64_t t113 = r111;
            gm_l5 = t113;
        }
    }
gm_lb1:;
    {
        gm_trqfvmjtt4sij t114;
        gm_f_list_av8bg03((void *)&t114, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l12 = t114;
    }
    gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    gm_b16_8 t116 = GM_LD(gm_b16_8, ((uint8_t *)&t115));
    gm_l13 = t116;
    gm_l14 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t117 = gm_l14;
            uint64_t t118 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
            uint8_t t119 = (t117 < t118);
            if (!t119) {
                goto gm_lb11;
            }
            {
                uint64_t t120 = gm_l14;
                void * t121 = GM_LD(void *, ((uint8_t *)&gm_l13));
                uint64_t t122 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                uint64_t t123 = gm_l14;
                gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)t121) + (uintptr_t)t123 * 16));
                GM_ST(uint64_t, ((uint8_t *)&gm_l26 + 0), t120);
                GM_ST(gm_b16_8, ((uint8_t *)&gm_l26 + 8), t124);
                {
                    {
                        uint64_t t125 = GM_LD(uint64_t, ((uint8_t *)&gm_l26));
                        gm_l15 = t125;
                        gm_b16_8 t126 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l26) + 8));
                        gm_l16 = t126;
                        void * t127 = (void *)((uint8_t *)&F.l12);
                        gm_b16_8 t128 = gm_l16;
                        void * t129 = GM_LD(void *, ((uint8_t *)&F.l3));
                        uint64_t t130 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                        uint64_t t131 = gm_l15;
                        if ((uintptr_t)t131 >= (uintptr_t)(t130)) gm_trap_index((uintptr_t)t131, t130, gm_srcd5arorrf8jkg, 464, 73);
                        gm_b16_8 t132 = GM_LD(gm_b16_8, (((uint8_t *)t129) + (uintptr_t)t131 * 40));
                        gm_b16_8 t133 = GM_LD(gm_b16_8, ((uint8_t *)&t132));
                        gm_t6j6smj7qoctu t134;
                        GM_ST(gm_b16_8, ((uint8_t *)&t134 + 0), t128);
                        GM_ST(gm_b16_8, ((uint8_t *)&t134 + 16), t133);
                        gm_tftb1odc739t5 t135 = gm_f_push_lm9fgpm(t127, &t134);
                        gm_l27 = t135;
                        uint8_t t136 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                        if (!t136) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 1, gm_srcd5arorrf8jkg, 464, 42);
                        goto gm_lb14;
                    }
gm_lb15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 464, 5);
                }
gm_lb14:;
            }
gm_lb13:;
            uint64_t t137 = gm_l14;
            uint64_t r138; bool o139 = __builtin_add_overflow(t137, ((uint64_t)1ULL), &r138);
            uint64_t t140 = r138;
            gm_l14 = t140;
        }
    }
gm_lb11:;
    gm_b16_8 t141 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
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
inline __attribute__((aligned(4))) gm_tgqgdb97ka5kt gm_f_owner_of_cirm596(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_owner_of_cirm596 F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    uint32_t gm_l2; /* root */
    uint32_t gm_l3; /* p */
    gm_tl874t2atnlh3 gm_l4; /* span */
    uint32_t gm_l5; /* group */
    GM_STACK_CHECK(&F, 321u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l6;
    uint32_t t8 = gm_l1;
    gm_l2 = t8;
    {
        for (;;) {
            void * t9 = gm_l0;
            void * t10 = GM_LD(void *, ((uint8_t *)t9));
            uint32_t t11 = gm_l2;
            void * t12 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t10, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t13 = (uint8_t)(((((uint8_t *)t12) + 88))[4] != 0);
            if (!t13) {
                goto gm_lb1;
            }
            uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)t12) + 88));
            gm_l3 = t14;
            {
                uint32_t t15 = gm_l3;
                gm_l2 = t15;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t16 = gm_l0;
    void * t17 = GM_LD(void *, ((uint8_t *)t16));
    uint32_t t18 = gm_l2;
    void * t19 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t17, t18);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tl874t2atnlh3 t20 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t19) + 16));
    gm_l4 = t20;
    gm_tl874t2atnlh3 t21 = gm_l4;
    gm_tl874t2atnlh3 t22 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
    uint8_t t23 = (gm_eqff85ac10o3(((uint8_t *)&t21), ((uint8_t *)&t22)));
    if (t23) {
        gm_tgqgdb97ka5kt t24 = GM_LD(gm_tgqgdb97ka5kt, ((uint8_t *)gm_ctvsbqjhdl3));
        F.ret = t24;
        goto gm_fret;
    }
    gm_l5 = ((uint32_t)0ULL);
    void * t25 = gm_l0;
    void * t26 = GM_LD(void *, ((uint8_t *)t25));
    void * t27 = GM_LD(void *, ((uint8_t *)t26));
    void * t28 = GM_LD(void *, (((uint8_t *)t27) + 344));
    uint64_t t29 = GM_LD(uint64_t, ((((uint8_t *)t27) + 344) + 8));
    uint32_t t30 = gm_l2;
    uint64_t t31 = (uint64_t)t30;
    if ((uintptr_t)t31 >= (uintptr_t)(t29)) gm_trap_index((uintptr_t)t31, t29, gm_srcd5arorrf8jkg, 427, 8);
    {
        {
            uint8_t t32 = (uint8_t)((((((uint8_t *)t28) + (uintptr_t)t31 * 136) + 96))[24] != 0);
            if (t32) {
                goto gm_lb5;
            }
            gm_l6 = ((uint8_t)0);
            goto gm_lb4;
        }
gm_lb5:;
        {
            gm_l6 = ((uint8_t)1);
            goto gm_lb4;
        }
gm_lb6:;
        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 427, 8);
    }
gm_lb4:;
    uint8_t t33 = gm_l6;
    if (t33) {
        void * t34 = gm_l0;
        uint32_t t35 = gm_l2;
        gm_b16_8 t36 = gm_f_fn_key_b6f5f24((void *)NULL, cx, NULL, t34, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t37 = gm_f_hash64_apkb7bo(t36);
        uint64_t t38 = GM_LD(uint64_t, ((uint8_t *)gm_cto913mafo2));
        if (t38 == 0) gm_trap("division by zero", gm_srcd5arorrf8jkg, 427, 76);
        uint64_t t39 = (uint64_t)(t37 % t38);
        uint32_t t40 = (uint32_t)t39;
        uint32_t r41; bool o42 = __builtin_add_overflow(((uint32_t)1ULL), t40, &r41);
        if (o42) gm_trap("integer overflow in `+`", gm_srcd5arorrf8jkg, 427, 71);
        uint32_t t43 = r41;
        gm_l5 = t43;
    }
    uint32_t t44 = GM_LD(uint32_t, ((uint8_t *)&gm_l4));
    gm_t9805qtonbsag t45;
    GM_ST(uint8_t, (uint8_t *)&t45 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t45 + 6, 0);
    GM_ST(uint32_t, ((uint8_t *)&t45), t44);
    ((uint8_t *)&t45)[4] = 1;
    uint32_t t46 = gm_l5;
    gm_tgqgdb97ka5kt t47;
    GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t47 + 0), t45);
    GM_ST(uint32_t, ((uint8_t *)&t47 + 8), t46);
    F.ret = t47;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_queue_cd51r8c(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_queue_cd51r8c F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 169u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l3;
    gm_tftb1odc739t5 gm_l4;
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    gm_l2 = t6;
    {
        for (;;) {
            void * t7 = gm_l0;
            uint64_t t8 = GM_LD(uint64_t, ((((uint8_t *)t7) + 2368) + 8));
            uint64_t t9 = gm_l2;
            uint8_t t10 = (t8 <= t9);
            if (!t10) {
                goto gm_lb1;
            }
            {
                void * t11 = gm_l0;
                void * t12 = (void *)(((uint8_t *)t11) + 2368);
                gm_tftb1odc739t5 t13 = gm_f_push_vd1290p(t12, ((uint8_t)0));
                gm_l3 = t13;
                uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
                if (!t14) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srcd5arorrf8jkg, 332, 37);
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t15 = gm_l0;
    void * t16 = GM_LD(void *, (((uint8_t *)t15) + 2368));
    uint64_t t17 = GM_LD(uint64_t, ((((uint8_t *)t15) + 2368) + 8));
    uint64_t t18 = gm_l2;
    if ((uintptr_t)t18 >= (uintptr_t)(t17)) gm_trap_index((uintptr_t)t18, t17, gm_srcd5arorrf8jkg, 333, 12);
    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)t16) + (uintptr_t)t18 * 1));
    uint8_t t20 = (uint8_t)!t19;
    if (t20) {
        void * t21 = gm_l0;
        void * t22 = GM_LD(void *, (((uint8_t *)t21) + 2368));
        uint64_t t23 = GM_LD(uint64_t, ((((uint8_t *)t21) + 2368) + 8));
        uint64_t t24 = gm_l2;
        if ((uintptr_t)t24 >= (uintptr_t)(t23)) gm_trap_index((uintptr_t)t24, t23, gm_srcd5arorrf8jkg, 334, 9);
        GM_ST(uint8_t, (((uint8_t *)t22) + (uintptr_t)t24 * 1), ((uint8_t)1));
        void * t25 = gm_l0;
        void * t26 = (void *)(((uint8_t *)t25) + 2328);
        uint32_t t27 = gm_l1;
        gm_tftb1odc739t5 t28 = gm_f_push_im9d4ro(t26, t27);
        gm_l4 = t28;
        uint8_t t29 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
        if (!t29) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srcd5arorrf8jkg, 335, 9);
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_queue_spec_khngsmv(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2) {
    struct gm_fr_queue_spec_khngsmv F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    uint32_t gm_l2; /* s */
    GM_STACK_CHECK(&F, 193u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l3;
    gm_tqs5p4n2tq19v gm_l4;
    gm_tftb1odc739t5 gm_l5;
    gm_tftb1odc739t5 gm_l6;
    void * t5 = gm_l0;
    void * t6 = (void *)(((uint8_t *)t5) + 2688);
    uint32_t t7 = gm_l1;
    uint32_t t8 = gm_l2;
    gm_tcc1rq4n1avb2 t9;
    GM_ST(uint32_t, ((uint8_t *)&t9 + 0), t7);
    GM_ST(uint32_t, ((uint8_t *)&t9 + 4), t8);
    gm_tqs5p4n2tq19v t10 = gm_f_get_ag7vhtq(t6, t9);
    gm_l4 = t10;
    {
        {
            uint8_t t11 = (uint8_t)((((uint8_t *)&gm_l4))[1] != 0);
            if (t11) {
                goto gm_lb2;
            }
            gm_l3 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l3 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 341, 8);
    }
gm_lb1:;
    uint8_t t12 = gm_l3;
    if (t12) {
        void * t13 = gm_l0;
        void * t14 = (void *)(((uint8_t *)t13) + 2688);
        uint32_t t15 = gm_l1;
        uint32_t t16 = gm_l2;
        gm_tcc1rq4n1avb2 t17;
        GM_ST(uint32_t, ((uint8_t *)&t17 + 0), t15);
        GM_ST(uint32_t, ((uint8_t *)&t17 + 4), t16);
        gm_tftb1odc739t5 t18 = gm_f_put_3g1tdpr(t14, t17, ((uint8_t)1));
        gm_l5 = t18;
        uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
        if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srcd5arorrf8jkg, 342, 9);
        void * t20 = gm_l0;
        void * t21 = (void *)(((uint8_t *)t20) + 2744);
        uint32_t t22 = gm_l1;
        uint32_t t23 = gm_l2;
        gm_tcc1rq4n1avb2 t24;
        GM_ST(uint32_t, ((uint8_t *)&t24 + 0), t22);
        GM_ST(uint32_t, ((uint8_t *)&t24 + 4), t23);
        gm_tftb1odc739t5 t25 = gm_f_push_sgikgd8(t21, t24);
        gm_l6 = t25;
        uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
        if (!t26) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srcd5arorrf8jkg, 343, 9);
    }
    void * t27 = gm_l0;
    uint32_t t28 = gm_l1;
    uint32_t t29 = gm_l2;
    gm_b16_8 t30 = gm_f_spec_name_jptsbbf((void *)NULL, cx, NULL, t27, t28, t29);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) uint32_t gm_f_spec_bitccmf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tf9f5m02f56gm a1) {
    struct gm_fr_spec_bitccmf F;
    void * gm_l0; /* g */
    gm_tf9f5m02f56gm gm_l1; /* key */
    gm_tl874t2atnlh3 gm_l2; /* k */
    uint32_t gm_l3; /* s */
    uint32_t gm_l4; /* s */
    GM_STACK_CHECK(&F, 235u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t9805qtonbsag gm_l5;
    gm_tftb1odc739t5 gm_l6;
    gm_tftb1odc739t5 gm_l7;
    uint32_t t4 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    gm_t9805qtonbsag t6 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l1) + 8));
    uint32_t t7 = gm_f_spec_slot_jild38d(t6);
    gm_tl874t2atnlh3 t8;
    GM_ST(uint32_t, ((uint8_t *)&t8 + 0), t4);
    GM_ST(uint32_t, ((uint8_t *)&t8 + 4), t5);
    GM_ST(uint32_t, ((uint8_t *)&t8 + 8), t7);
    gm_l2 = t8;
    {
        {
            void * t9 = gm_l0;
            void * t10 = (void *)(((uint8_t *)t9) + 2632);
            gm_tl874t2atnlh3 t11 = gm_l2;
            gm_t9805qtonbsag t12 = gm_f_get_pftnkei(t10, t11);
            gm_l5 = t12;
            uint8_t t13 = (uint8_t)((((uint8_t *)&gm_l5))[4] != 0);
            if (!t13) {
                goto gm_lb2;
            }
            uint32_t t14 = GM_LD(uint32_t, ((uint8_t *)&gm_l5));
            gm_l3 = t14;
            uint32_t t15 = gm_l3;
            F.ret = t15;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t16 = gm_l0;
    uint64_t t17 = GM_LD(uint64_t, ((((uint8_t *)t16) + 2592) + 8));
    uint32_t t18 = (uint32_t)t17;
    gm_l4 = t18;
    void * t19 = gm_l0;
    void * t20 = (void *)(((uint8_t *)t19) + 2592);
    gm_tf9f5m02f56gm t21 = gm_l1;
    gm_tftb1odc739t5 t22 = gm_f_push_jttnn67(t20, t21);
    gm_l6 = t22;
    uint8_t t23 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (!t23) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srcd5arorrf8jkg, 353, 5);
    void * t24 = gm_l0;
    void * t25 = (void *)(((uint8_t *)t24) + 2632);
    gm_tl874t2atnlh3 t26 = gm_l2;
    uint32_t t27 = gm_l4;
    gm_tftb1odc739t5 t28 = gm_f_put_bii31he(t25, t26, t27);
    gm_l7 = t28;
    uint8_t t29 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (!t29) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srcd5arorrf8jkg, 354, 5);
    uint32_t t30 = gm_l4;
    F.ret = t30;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint32_t gm_f_spec_depth_em7r6vt(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_spec_handler_table_8h552cp(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2) {
    struct gm_fr_spec_handler_table_8h552cp F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* h */
    uint32_t gm_l2; /* s */
    gm_b16_8 gm_l3; /* name */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint32_t gm_l7; /* c */
    GM_STACK_CHECK(&F, 766u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l8;
    uint8_t gm_l9;
    gm_tqs5p4n2tq19v gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tabb710d9mq2e gm_l12;
    gm_tftb1odc739t5 gm_l13;
    gm_tabb710d9mq2e gm_l14;
    gm_tftb1odc739t5 gm_l15;
    {
        void * t10 = gm_l0;
        uint32_t t11 = gm_l1;
        gm_b16_8 t12 = gm_f_handler_name_6km8n3f((void *)NULL, cx, NULL, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t13 = gm_l0;
        uint32_t t14 = gm_l2;
        gm_b16_8 t15 = gm_f_spec_text_iq7r8vn((void *)NULL, cx, NULL, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t16 = gm_f_hash_text_4aekv10((void *)NULL, cx, NULL, t15, ((uint64_t)6ULL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6j6smj7qoctu t17;
        GM_ST(gm_b16_8, ((uint8_t *)&t17 + 0), t12);
        GM_ST(gm_b16_8, ((uint8_t *)&t17 + 16), t16);
        gm_f_format_kj13ck1((void *)&gm_l8, (void *)NULL, cx, NULL, &t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_srcd5arorrf8jkg, 575, 13);
    gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
    gm_l3 = t19;
    void * t20 = gm_l0;
    void * t21 = (void *)(((uint8_t *)t20) + 2784);
    uint32_t t22 = gm_l1;
    uint32_t t23 = gm_l2;
    gm_tcc1rq4n1avb2 t24;
    GM_ST(uint32_t, ((uint8_t *)&t24 + 0), t22);
    GM_ST(uint32_t, ((uint8_t *)&t24 + 4), t23);
    gm_tqs5p4n2tq19v t25 = gm_f_get_ag7vhtq(t21, t24);
    gm_l10 = t25;
    {
        {
            uint8_t t26 = (uint8_t)((((uint8_t *)&gm_l10))[1] != 0);
            if (t26) {
                goto gm_lb2;
            }
            gm_l9 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l9 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 576, 8);
    }
gm_lb1:;
    uint8_t t27 = gm_l9;
    if (t27) {
        void * t28 = gm_l0;
        void * t29 = (void *)(((uint8_t *)t28) + 2784);
        uint32_t t30 = gm_l1;
        uint32_t t31 = gm_l2;
        gm_tcc1rq4n1avb2 t32;
        GM_ST(uint32_t, ((uint8_t *)&t32 + 0), t30);
        GM_ST(uint32_t, ((uint8_t *)&t32 + 4), t31);
        gm_tftb1odc739t5 t33 = gm_f_put_3g1tdpr(t29, t32, ((uint8_t)1));
        gm_l11 = t33;
        uint8_t t34 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
        if (!t34) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srcd5arorrf8jkg, 577, 9);
        {
            gm_trqfvmjtt4sij t35;
            gm_f_list_epvducg((void *)&t35, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l4 = t35;
        }
        void * t36 = gm_l0;
        void * t37 = GM_LD(void *, ((uint8_t *)t36));
        uint32_t t38 = gm_l1;
        void * t39 = gm_f_handler_lit_b02boff(t37, t38);
        gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)t39) + 24));
        gm_l5 = t40;
        gm_l6 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t41 = gm_l6;
                uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint8_t t43 = (t41 < t42);
                if (!t43) {
                    goto gm_lb4;
                }
                {
                    void * t44 = GM_LD(void *, ((uint8_t *)&gm_l5));
                    uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint64_t t46 = gm_l6;
                    {
                        {
                            uint32_t t47 = GM_LD(uint32_t, (((uint8_t *)t44) + (uintptr_t)t46 * 4));
                            gm_l7 = t47;
                            void * t48 = (void *)((uint8_t *)&F.l4);
                            void * t49 = gm_l0;
                            uint32_t t50 = gm_l7;
                            uint32_t t51 = gm_l2;
                            gm_b16_8 t52 = gm_f_queue_spec_khngsmv((void *)NULL, cx, NULL, t49, t50, t51);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tnpifv20irnph t53;
                            GM_ST(gm_b16_8, ((uint8_t *)&t53 + 0), t52);
                            gm_f_format_be3fee1((void *)&gm_l12, (void *)NULL, cx, NULL, t53);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t54 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                            if (!t54) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_srcd5arorrf8jkg, 580, 24);
                            gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
                            gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&t55));
                            gm_tftb1odc739t5 t57 = gm_f_push_3ef746b(t48, t56);
                            gm_l13 = t57;
                            uint8_t t58 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                            if (!t58) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srcd5arorrf8jkg, 580, 13);
                            goto gm_lb7;
                        }
gm_lb8:;
                        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 579, 9);
                    }
gm_lb7:;
                }
gm_lb6:;
                uint64_t t59 = gm_l6;
                uint64_t r60; bool o61 = __builtin_add_overflow(t59, ((uint64_t)1ULL), &r60);
                uint64_t t62 = r60;
                gm_l6 = t62;
            }
        }
gm_lb4:;
        {
            void * t63 = gm_l0;
            void * t64 = (void *)(((uint8_t *)t63) + 3080);
            gm_b16_8 t65 = gm_l3;
            gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
            gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&t66));
            gm_b16_8 t68;
            GM_ST(void *, (uint8_t *)&t68, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(2));
            gm_f_join_sfknljb((void *)&gm_l14, (void *)NULL, cx, NULL, t67, t68);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t69 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
            if (!t69) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_srcd5arorrf8jkg, 582, 77);
            gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
            gm_t6j6smj7qoctu t71;
            GM_ST(gm_b16_8, ((uint8_t *)&t71 + 0), t65);
            GM_ST(gm_b16_8, ((uint8_t *)&t71 + 16), t70);
            gm_tftb1odc739t5 t72 = gm_f_write_pfrpr5k(t64, &t71);
            gm_l15 = t72;
        }
        uint8_t t73 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
        if (!t73) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srcd5arorrf8jkg, 582, 9);
    }
    gm_b16_8 t74 = gm_l3;
    gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
    F.ret = t75;
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
extern __attribute__((aligned(4))) gm_tf9f5m02f56gm gm_f_spec_key_eljlge7(void * a0, uint32_t a1);
extern __attribute__((aligned(4))) uint32_t gm_f_spec_slot_jild38d(gm_t9805qtonbsag a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_src_loc_lrhtj7i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_src_loc_lrhtj7i F;
    void * gm_l0; /* g */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* f */
    gm_tcc1rq4n1avb2 gm_l3; /* lc */
    GM_STACK_CHECK(&F, 353u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l4;
    gm_tabb710d9mq2e gm_l5;
    gm_tl874t2atnlh3 t2 = gm_l1;
    gm_tl874t2atnlh3 t3 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
    uint8_t t4 = (gm_eqff85ac10o3(((uint8_t *)&t2), ((uint8_t *)&t3)));
    if (t4) {
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sp5lvq92b4in0));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(10));
        F.ret = t5;
        goto gm_fret;
    }
    void * t6 = gm_l0;
    void * t7 = GM_LD(void *, ((uint8_t *)t6));
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    void * t9 = GM_LD(void *, ((uint8_t *)t8));
    void * t10 = GM_LD(void *, ((uint8_t *)t9));
    uint32_t t11 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    void * t12 = gm_f_file_o15fqku(t10, t11);
    gm_l2 = t12;
    void * t13 = gm_l2;
    uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    gm_tcc1rq4n1avb2 t15 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t13, t14));
    gm_l3 = t15;
    uint32_t t16 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    void * t17 = gm_l0;
    gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)t17) + 3392));
    uint8_t t19 = 0;
    for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t18) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t18))) + i * 4, ((uint8_t *)&t16))) { t19 = 1; break; }
    uint8_t t20 = (uint8_t)(!t19);
    if (t20) {
        void * t21 = gm_l0;
        void * t22 = (void *)(((uint8_t *)t21) + 3392);
        uint32_t t23 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
        gm_tftb1odc739t5 t24 = gm_f_push_im9d4ro(t22, t23);
        gm_l4 = t24;
        uint8_t t25 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
        if (!t25) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srcd5arorrf8jkg, 544, 45);
    }
    {
        void * t26 = gm_l0;
        uint32_t t27 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
        gm_b16_8 t28 = gm_f_src_name_0r539f2((void *)NULL, cx, NULL, t26, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t29 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
        uint32_t t30 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
        gm_t5hdnnftkup6m t31;
        GM_ST(gm_b16_8, ((uint8_t *)&t31 + 0), t28);
        GM_ST(uint32_t, ((uint8_t *)&t31 + 16), t29);
        GM_ST(uint32_t, ((uint8_t *)&t31 + 20), t30);
        gm_f_format_6g8g0f7((void *)&gm_l5, (void *)NULL, cx, NULL, &t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t32) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 8, gm_srcd5arorrf8jkg, 545, 12);
    gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
    gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_src_name_0r539f2(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_static_bytes_u0uvco3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_static_bytes_u0uvco3 F;
    void * gm_l0; /* g */
    gm_b16_8 gm_l1; /* bytes */
    gm_b16_8 gm_l2; /* n */
    gm_b16_8 gm_l3; /* n */
    GM_STACK_CHECK(&F, 419u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_th2lqbo77pvsc gm_l4;
    gm_tftb1odc739t5 gm_l5;
    gm_tftb1odc739t5 gm_l6;
    {
        {
            void * t4 = gm_l0;
            void * t5 = (void *)(((uint8_t *)t4) + 1752);
            gm_b16_8 t6 = gm_l1;
            gm_f_get_e3joqcc((void *)&gm_l4, t5, t6);
            uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l4))[16] != 0);
            if (!t7) {
                goto gm_lb2;
            }
            gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
            gm_l2 = t8;
            gm_b16_8 t9 = gm_l2;
            F.ret = t9;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(gm_s8rsp9val5ful));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(4));
    gm_b16_8 t11 = gm_l1;
    gm_b16_8 t12 = gm_f_content_name_83mn5uf((void *)NULL, cx, NULL, t10, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t12;
    void * t13 = gm_l0;
    void * t14 = (void *)(((uint8_t *)t13) + 1752);
    gm_b16_8 t15 = gm_l1;
    gm_b16_8 t16 = gm_l3;
    gm_tftb1odc739t5 t17 = gm_f_put_7shfu9u(t14, t15, t16);
    gm_l5 = t17;
    uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srcd5arorrf8jkg, 507, 5);
    void * t19 = gm_l0;
    void * t20 = (void *)(((uint8_t *)t19) + 1808);
    gm_b16_8 t21 = gm_l3;
    uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
    if (o24) gm_trap("integer overflow in `+`", gm_srcd5arorrf8jkg, 508, 67);
    uint64_t t25 = r23;
    gm_b16_8 t26 = gm_l1;
    gm_b16_8 t27 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t26);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tq3m5qgvkpje8 t28;
    GM_ST(gm_b16_8, ((uint8_t *)&t28 + 0), t21);
    GM_ST(uint64_t, ((uint8_t *)&t28 + 16), t25);
    GM_ST(gm_b16_8, ((uint8_t *)&t28 + 24), t27);
    gm_tftb1odc739t5 t29 = gm_f_write_uuia5cm(t20, &t28);
    gm_l6 = t29;
    uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srcd5arorrf8jkg, 508, 5);
    gm_b16_8 t31 = gm_l3;
    F.ret = t31;
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
inline __attribute__((aligned(4))) uint8_t gm_f_takes_fn_n239ee5(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_takes_fn_n239ee5 F;
    void * gm_l0; /* g */
    gm_b16_8 gm_l1; /* params */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_t88rcucg1b7uk gm_l4; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 211u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l5;
    gm_t53at80pfdus3 gm_l6;
    gm_b16_8 t10 = gm_l1;
    gm_l2 = t10;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t11 = gm_l3;
            uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t13 = (t11 < t12);
            if (!t13) {
                goto gm_lb1;
            }
            {
                void * t14 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t16 = gm_l3;
                {
                    {
                        gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t14) + (uintptr_t)t16 * 4));
                        gm_l4 = t17;
                        void * t18 = gm_l0;
                        void * t19 = (void *)(((uint8_t *)t18) + 8);
                        gm_t88rcucg1b7uk t20 = gm_l4;
                        gm_f_kind_ghvav22((void *)&gm_l6, t19, GM_CONV(gm_csguankko4lmhd, t20));
                        {
                            {
                                uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                                uint8_t t22 = (uint8_t)(t21 == ((uint8_t)15ULL));
                                if (!t22) {
                                    goto gm_lb7;
                                }
                                gm_l5 = ((uint8_t)1);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                gm_l5 = ((uint8_t)0);
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 966, 12);
                        }
gm_lb6:;
                        uint8_t t23 = gm_l5;
                        if (t23) {
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 965, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t24 = gm_l3;
            uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
            uint64_t t27 = r25;
            gm_l3 = t27;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_template_table_h2eglbh(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_template_table_h2eglbh F;
    void * gm_l0; /* g */
    void * gm_l1; /* t */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l5; /* p */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    uint64_t gm_l11; /* n */
    gm_b16_8 gm_l12; /* text */
    gm_b16_8 gm_l13; /* name */
    gm_b16_8 gm_l14; /* name */
    GM_STACK_CHECK(&F, 2276u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l15;
    gm_tftb1odc739t5 gm_l16;
    gm_tabb710d9mq2e gm_l17;
    gm_tftb1odc739t5 gm_l18;
    gm_b16_8 gm_l19;
    gm_tabb710d9mq2e gm_l20;
    gm_tftb1odc739t5 gm_l21;
    gm_tabb710d9mq2e gm_l22;
    gm_tabb710d9mq2e gm_l23;
    gm_tabb710d9mq2e gm_l24;
    gm_tabb710d9mq2e gm_l25;
    gm_th2lqbo77pvsc gm_l26;
    gm_tftb1odc739t5 gm_l27;
    gm_tabb710d9mq2e gm_l28;
    gm_tftb1odc739t5 gm_l29;
    gm_tabb710d9mq2e gm_l30;
    gm_tftb1odc739t5 gm_l31;
    gm_tabb710d9mq2e gm_l32;
    gm_tftb1odc739t5 gm_l33;
    gm_tftb1odc739t5 gm_l34;
    gm_tftb1odc739t5 gm_l35;
    {
        gm_trqfvmjtt4sij t14;
        gm_f_list_epvducg((void *)&t14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t14;
    }
    void * t15 = gm_l1;
    gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)t15));
    gm_l3 = t16;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t17 = gm_l4;
            uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t19 = (t17 < t18);
            if (!t19) {
                goto gm_lb1;
            }
            {
                void * t20 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t22 = gm_l4;
                {
                    {
                        gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)t20) + (uintptr_t)t22 * 16));
                        gm_l5 = t23;
                        {
                            void * t24 = (void *)((uint8_t *)&F.l2);
                            void * t25 = gm_l0;
                            gm_b16_8 t26 = gm_l5;
                            gm_b16_8 t27 = gm_f_static_bytes_u0uvco3((void *)NULL, cx, NULL, t25, t26);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                            gm_t3877pk7lv748 t29;
                            GM_ST(gm_b16_8, ((uint8_t *)&t29 + 0), t27);
                            GM_ST(uint64_t, ((uint8_t *)&t29 + 16), t28);
                            gm_f_format_54mn8jj((void *)&gm_l15, (void *)NULL, cx, NULL, &t29);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                            if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_srcd5arorrf8jkg, 516, 37);
                            gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
                            gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
                            gm_tftb1odc739t5 t33 = gm_f_push_3ef746b(t24, t32);
                            gm_l16 = t33;
                        }
                        uint8_t t34 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                        if (!t34) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_srcd5arorrf8jkg, 516, 25);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 516, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t35 = gm_l4;
            uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
            uint64_t t38 = r36;
            gm_l4 = t38;
        }
    }
gm_lb1:;
    {
        gm_trqfvmjtt4sij t39;
        gm_f_list_epvducg((void *)&t39, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l6 = t39;
    }
    {
        gm_trqfvmjtt4sij t40;
        gm_f_list_epvducg((void *)&t40, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l7 = t40;
    }
    gm_l9 = ((uint64_t)0ULL);
    void * t41 = gm_l1;
    uint64_t t42 = GM_LD(uint64_t, ((((uint8_t *)t41) + 16) + 8));
    gm_l8 = t42;
    {
        for (;;) {
            uint64_t t43 = gm_l9;
            uint64_t t44 = gm_l8;
            uint8_t t45 = (t43 < t44);
            if (!t45) {
                goto gm_lb6;
            }
            {
                {
                    {
                        uint64_t t46 = gm_l9;
                        gm_l10 = t46;
                        void * t47 = (void *)((uint8_t *)&F.l6);
                        void * t48 = gm_l1;
                        void * t49 = GM_LD(void *, (((uint8_t *)t48) + 32));
                        uint64_t t50 = GM_LD(uint64_t, ((((uint8_t *)t48) + 32) + 8));
                        uint64_t t51 = gm_l10;
                        if ((uintptr_t)t51 >= (uintptr_t)(t50)) gm_trap_index((uintptr_t)t51, t50, gm_srcd5arorrf8jkg, 520, 38);
                        uint32_t t52 = GM_LD(uint32_t, (((uint8_t *)t49) + (uintptr_t)t51 * 4));
                        gm_t88rcucg1b7uk t53;
                        GM_ST(uint32_t, ((uint8_t *)&t53 + 0), t52);
                        gm_f_format_djjuahs((void *)&gm_l17, (void *)NULL, cx, NULL, t53);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t54 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                        if (!t54) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_srcd5arorrf8jkg, 520, 20);
                        gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
                        gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&t55));
                        gm_tftb1odc739t5 t57 = gm_f_push_3ef746b(t47, t56);
                        gm_l18 = t57;
                        uint8_t t58 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                        if (!t58) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_srcd5arorrf8jkg, 520, 9);
                        void * t59 = (void *)((uint8_t *)&F.l7);
                        void * t60 = gm_l1;
                        void * t61 = GM_LD(void *, (((uint8_t *)t60) + 16));
                        uint64_t t62 = GM_LD(uint64_t, ((((uint8_t *)t60) + 16) + 8));
                        uint64_t t63 = gm_l10;
                        if ((uintptr_t)t63 >= (uintptr_t)(t62)) gm_trap_index((uintptr_t)t63, t62, gm_srcd5arorrf8jkg, 521, 44);
                        void * t64 = (void *)(((uint8_t *)t61) + (uintptr_t)t63 * 24);
                        uint8_t t65 = gm_f_is_default_spec_cdd8jng(t64);
                        if (t65) {
                            gm_b16_8 t66;
                            GM_ST(void *, (uint8_t *)&t66, (void *)(gm_s04qus0luoga5));
                            GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(4));
                            gm_l19 = t66;
                        } else {
                            {
                                void * t67 = gm_l1;
                                void * t68 = GM_LD(void *, (((uint8_t *)t67) + 16));
                                uint64_t t69 = GM_LD(uint64_t, ((((uint8_t *)t67) + 16) + 8));
                                uint64_t t70 = gm_l10;
                                if ((uintptr_t)t70 >= (uintptr_t)(t69)) gm_trap_index((uintptr_t)t70, t69, gm_srcd5arorrf8jkg, 521, 105);
                                gm_b16_8 t71 = gm_f_spec_literal_lmkpbc1((void *)NULL, cx, NULL, &(*(gm_t7g88i52rrtm5 *)(((uint8_t *)t68) + (uintptr_t)t70 * 24)));
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tnpifv20irnph t72;
                                GM_ST(gm_b16_8, ((uint8_t *)&t72 + 0), t71);
                                gm_f_format_h6b2ofo((void *)&gm_l20, (void *)NULL, cx, NULL, t72);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            uint8_t t73 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                            if (!t73) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 8, gm_srcd5arorrf8jkg, 521, 74);
                            gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
                            gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
                            gm_l19 = t75;
                        }
                        gm_b16_8 t76 = gm_l19;
                        gm_tftb1odc739t5 t77 = gm_f_push_3ef746b(t59, t76);
                        gm_l21 = t77;
                        uint8_t t78 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                        if (!t78) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srcd5arorrf8jkg, 521, 9);
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 519, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t79 = gm_l9;
            uint64_t r80; bool o81 = __builtin_add_overflow(t79, ((uint64_t)1ULL), &r80);
            uint64_t t82 = r80;
            gm_l9 = t82;
        }
    }
gm_lb6:;
    void * t83 = gm_l1;
    uint64_t t84 = GM_LD(uint64_t, ((((uint8_t *)t83) + 16) + 8));
    gm_l11 = t84;
    {
        gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
        gm_b16_8 t86 = GM_LD(gm_b16_8, ((uint8_t *)&t85));
        gm_b16_8 t87;
        GM_ST(void *, (uint8_t *)&t87, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)(2));
        gm_f_join_sfknljb((void *)&gm_l22, (void *)NULL, cx, NULL, t86, t87);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t88 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
    if (!t88) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 8, gm_srcd5arorrf8jkg, 524, 36);
    {
        gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
        gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
        gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&t90));
        gm_b16_8 t92;
        GM_ST(void *, (uint8_t *)&t92, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(2));
        gm_f_join_sfknljb((void *)&gm_l23, (void *)NULL, cx, NULL, t91, t92);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t93 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
        if (!t93) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 8, gm_srcd5arorrf8jkg, 524, 67);
        gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 8));
        gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
        gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&t95));
        gm_b16_8 t97;
        GM_ST(void *, (uint8_t *)&t97, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(2));
        gm_f_join_sfknljb((void *)&gm_l24, (void *)NULL, cx, NULL, t96, t97);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t98 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
        if (!t98) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 8, gm_srcd5arorrf8jkg, 524, 97);
        gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 8));
        gm_tfeab279c16rr t100;
        GM_ST(gm_b16_8, ((uint8_t *)&t100 + 0), t89);
        GM_ST(gm_b16_8, ((uint8_t *)&t100 + 16), t94);
        GM_ST(gm_b16_8, ((uint8_t *)&t100 + 32), t99);
        gm_f_format_8nhas6j((void *)&gm_l25, (void *)NULL, cx, NULL, &t100);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t101 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
    if (!t101) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 8, gm_srcd5arorrf8jkg, 524, 13);
    gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l25) + 8));
    gm_l12 = t102;
    {
        {
            void * t103 = gm_l0;
            void * t104 = (void *)(((uint8_t *)t103) + 1848);
            gm_b16_8 t105 = gm_l12;
            gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)&t105));
            gm_f_get_e3joqcc((void *)&gm_l26, t104, t106);
            uint8_t t107 = (uint8_t)((((uint8_t *)&gm_l26))[16] != 0);
            if (!t107) {
                goto gm_lb12;
            }
            gm_b16_8 t108 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l26));
            gm_l13 = t108;
            gm_b16_8 t109 = gm_l13;
            F.ret = t109;
            goto gm_fret;
        }
gm_lb12:;
    }
gm_lb11:;
    gm_b16_8 t110;
    GM_ST(void *, (uint8_t *)&t110, (void *)(gm_sm7qdm4gk14td));
    GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(6));
    gm_b16_8 t111 = gm_l12;
    gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&t111));
    gm_b16_8 t113 = gm_f_content_name_83mn5uf((void *)NULL, cx, NULL, t110, t112);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l14 = t113;
    void * t114 = gm_l0;
    void * t115 = (void *)(((uint8_t *)t114) + 1848);
    gm_b16_8 t116 = gm_l12;
    gm_b16_8 t117 = GM_LD(gm_b16_8, ((uint8_t *)&t116));
    gm_b16_8 t118 = gm_l14;
    gm_tftb1odc739t5 t119 = gm_f_put_7shfu9u(t115, t117, t118);
    gm_l27 = t119;
    uint8_t t120 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
    if (!t120) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 1, gm_srcd5arorrf8jkg, 527, 5);
    {
        void * t121 = gm_l0;
        void * t122 = (void *)(((uint8_t *)t121) + 1808);
        gm_b16_8 t123 = gm_l14;
        gm_b16_8 t124 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
        gm_b16_8 t125 = GM_LD(gm_b16_8, ((uint8_t *)&t124));
        gm_b16_8 t126;
        GM_ST(void *, (uint8_t *)&t126, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t126 + 8, (uintptr_t)(2));
        gm_f_join_sfknljb((void *)&gm_l28, (void *)NULL, cx, NULL, t125, t126);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t127 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
        if (!t127) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 8, gm_srcd5arorrf8jkg, 528, 80);
        gm_b16_8 t128 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l28) + 8));
        gm_t6j6smj7qoctu t129;
        GM_ST(gm_b16_8, ((uint8_t *)&t129 + 0), t123);
        GM_ST(gm_b16_8, ((uint8_t *)&t129 + 16), t128);
        gm_tftb1odc739t5 t130 = gm_f_write_gb1ne57(t122, &t129);
        gm_l29 = t130;
    }
    uint8_t t131 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
    if (!t131) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_srcd5arorrf8jkg, 528, 5);
    uint64_t t132 = gm_l11;
    uint8_t t133 = (t132 > ((uint64_t)0ULL));
    if (t133) {
        {
            void * t134 = gm_l0;
            void * t135 = (void *)(((uint8_t *)t134) + 1808);
            gm_b16_8 t136 = gm_l14;
            gm_b16_8 t137 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
            gm_b16_8 t138 = GM_LD(gm_b16_8, ((uint8_t *)&t137));
            gm_b16_8 t139;
            GM_ST(void *, (uint8_t *)&t139, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t139 + 8, (uintptr_t)(2));
            gm_f_join_sfknljb((void *)&gm_l30, (void *)NULL, cx, NULL, t138, t139);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t140 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
            if (!t140) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 8, gm_srcd5arorrf8jkg, 530, 79);
            gm_b16_8 t141 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 8));
            gm_t6j6smj7qoctu t142;
            GM_ST(gm_b16_8, ((uint8_t *)&t142 + 0), t136);
            GM_ST(gm_b16_8, ((uint8_t *)&t142 + 16), t141);
            gm_tftb1odc739t5 t143 = gm_f_write_oj4s9u6(t135, &t142);
            gm_l31 = t143;
        }
        uint8_t t144 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
        if (!t144) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 1, gm_srcd5arorrf8jkg, 530, 9);
        {
            void * t145 = gm_l0;
            void * t146 = (void *)(((uint8_t *)t145) + 1808);
            gm_b16_8 t147 = gm_l14;
            gm_b16_8 t148 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
            gm_b16_8 t149 = GM_LD(gm_b16_8, ((uint8_t *)&t148));
            gm_b16_8 t150;
            GM_ST(void *, (uint8_t *)&t150, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t150 + 8, (uintptr_t)(2));
            gm_f_join_sfknljb((void *)&gm_l32, (void *)NULL, cx, NULL, t149, t150);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t151 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
            if (!t151) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 8, gm_srcd5arorrf8jkg, 531, 85);
            gm_b16_8 t152 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l32) + 8));
            gm_t6j6smj7qoctu t153;
            GM_ST(gm_b16_8, ((uint8_t *)&t153 + 0), t147);
            GM_ST(gm_b16_8, ((uint8_t *)&t153 + 16), t152);
            gm_tftb1odc739t5 t154 = gm_f_write_3dkm7v9(t146, &t153);
            gm_l33 = t154;
        }
        uint8_t t155 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
        if (!t155) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 1, gm_srcd5arorrf8jkg, 531, 9);
        {
            void * t156 = gm_l0;
            void * t157 = (void *)(((uint8_t *)t156) + 1808);
            gm_b16_8 t158 = gm_l14;
            uint64_t t159 = gm_l11;
            gm_b16_8 t160 = gm_l14;
            gm_b16_8 t161 = gm_l14;
            gm_b16_8 t162 = gm_l14;
            gm_tcrdb1inj22os t163;
            GM_ST(gm_b16_8, ((uint8_t *)&t163 + 0), t158);
            GM_ST(uint64_t, ((uint8_t *)&t163 + 16), t159);
            GM_ST(gm_b16_8, ((uint8_t *)&t163 + 24), t160);
            GM_ST(gm_b16_8, ((uint8_t *)&t163 + 40), t161);
            GM_ST(gm_b16_8, ((uint8_t *)&t163 + 56), t162);
            gm_tftb1odc739t5 t164 = gm_f_write_n0bkcka(t157, &t163);
            gm_l34 = t164;
        }
        uint8_t t165 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
        if (!t165) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 1, gm_srcd5arorrf8jkg, 532, 9);
    } else {
        void * t166 = gm_l0;
        void * t167 = (void *)(((uint8_t *)t166) + 1808);
        gm_b16_8 t168 = gm_l14;
        gm_b16_8 t169 = gm_l14;
        gm_t6j6smj7qoctu t170;
        GM_ST(gm_b16_8, ((uint8_t *)&t170 + 0), t168);
        GM_ST(gm_b16_8, ((uint8_t *)&t170 + 16), t169);
        gm_tftb1odc739t5 t171 = gm_f_write_13301jc(t167, &t170);
        gm_l35 = t171;
        uint8_t t172 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
        if (!t172) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 1, gm_srcd5arorrf8jkg, 534, 9);
    }
    gm_b16_8 t173 = gm_l14;
    F.ret = t173;
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
extern __attribute__((aligned(4))) void gm_f_unsupported_531q51r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_with_helpers_edhoqtc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_with_helpers_edhoqtc F;
    gm_b16_8 gm_l0; /* owned */
    gm_b16_8 gm_l1; /* helpers */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    gm_t6j6smj7qoctu gm_l5; /* o */
    GM_STACK_CHECK(&F, 685u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t1l3r16b8478v gm_l7;
    gm_tabb710d9mq2e gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_b16_8 t7 = gm_l0;
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    gm_l2 = t8;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t9 = gm_l3;
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                {
                    uint64_t t12 = gm_l3;
                    void * t13 = GM_LD(void *, ((uint8_t *)&gm_l2));
                    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                    uint64_t t15 = gm_l3;
                    gm_t6j6smj7qoctu t16 = GM_LD(gm_t6j6smj7qoctu, (((uint8_t *)t13) + (uintptr_t)t15 * 32));
                    GM_ST(uint64_t, ((uint8_t *)&gm_l7 + 0), t12);
                    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&gm_l7 + 8), t16);
                }
                {
                    {
                        uint64_t t17 = GM_LD(uint64_t, ((uint8_t *)&gm_l7));
                        gm_l4 = t17;
                        memmove(((uint8_t *)&gm_l5), (((uint8_t *)&gm_l7) + 8), 32);
                        uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                        uint8_t t19 = ((t18 == ((uint64_t)0ULL)));
                        if (t19) {
                            {
                                void * t20 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                uint64_t t22 = gm_l4;
                                if ((uintptr_t)t22 >= (uintptr_t)(t21)) gm_trap_index((uintptr_t)t22, t21, gm_srcd5arorrf8jkg, 472, 13);
                                gm_b16_8 t23 = gm_l1;
                                gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 16));
                                gm_t6j6smj7qoctu t25;
                                GM_ST(gm_b16_8, ((uint8_t *)&t25 + 0), t23);
                                GM_ST(gm_b16_8, ((uint8_t *)&t25 + 16), t24);
                                gm_f_format_8n626pi((void *)&gm_l8, (void *)NULL, cx, NULL, &t25);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                                if (!t26) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_srcd5arorrf8jkg, 472, 29);
                                gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
                                gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
                                GM_ST(gm_b16_8, ((((uint8_t *)t20) + (uintptr_t)t22 * 32) + 16), t28);
                            }
                            gm_b16_8 t29 = gm_l0;
                            gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
                            F.ret = t30;
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 470, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t31 = gm_l3;
            uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
            uint64_t t34 = r32;
            gm_l3 = t34;
        }
    }
gm_lb1:;
    {
        gm_trqfvmjtt4sij t35;
        gm_f_list_av8bg03((void *)&t35, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l6 = t35;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l6);
        gm_b16_8 t37;
        GM_ST(void *, (uint8_t *)&t37, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(0));
        gm_b16_8 t38 = gm_l1;
        gm_t6j6smj7qoctu t39;
        GM_ST(gm_b16_8, ((uint8_t *)&t39 + 0), t37);
        GM_ST(gm_b16_8, ((uint8_t *)&t39 + 16), t38);
        gm_tftb1odc739t5 t40 = gm_f_push_lm9fgpm(t36, &t39);
        gm_l9 = t40;
    }
    uint8_t t41 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (!t41) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcd5arorrf8jkg, 477, 5);
    void * t42 = (void *)((uint8_t *)&F.l6);
    gm_b16_8 t43 = gm_l0;
    gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
    gm_tftb1odc739t5 t45 = gm_f_extend_msip1bg(t42, t44);
    gm_l10 = t45;
    uint8_t t46 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t46) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srcd5arorrf8jkg, 478, 5);
    gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
    gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&t47));
    F.ret = t48;
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
inline __attribute__((aligned(4))) uint8_t gm_f_within_sys_2lc2ugq(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_within_sys_2lc2ugq F;
    void * gm_l0; /* g */
    void * gm_l1; /* set */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tiilmfd16v3fc gm_l4; /* e */
    GM_STACK_CHECK_SHALLOW(&F, 184u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t7 = gm_l1;
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)t7));
    gm_l2 = t8;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t9 = gm_l3;
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                void * t12 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t14 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&gm_l4), (((uint8_t *)t12) + (uintptr_t)t14 * 24), 24);
                        void * t15 = gm_l0;
                        void * t16 = GM_LD(void *, ((uint8_t *)t15));
                        gm_t9805qtonbsag t17 = GM_LD(gm_t9805qtonbsag, ((((uint8_t *)t16) + 48) + 24));
                        uint32_t t18 = GM_LD(uint32_t, ((uint8_t *)&gm_l4));
                        gm_t9805qtonbsag t19;
                        GM_ST(uint8_t, (uint8_t *)&t19 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t19 + 6, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t19), t18);
                        ((uint8_t *)&t19)[4] = 1;
                        uint8_t t20 = (!gm_eqkricceou4u(((uint8_t *)&t17), ((uint8_t *)&t19)));
                        if (t20) {
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 889, 5);
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
    F.ret = ((uint8_t)1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}

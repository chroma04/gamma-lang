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
typedef struct gm_cs4j964akt50um { uint64_t f0; _Bool f1; _Bool f2; _Bool f3; } gm_cs4j964akt50um;
typedef struct gm_cskjv6t053tkje { uint32_t f0; uint64_t f1; } gm_cskjv6t053tkje;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; uint8_t f1; uint8_t f2; uint8_t f3; uint8_t pe[5]; } gm_t81j4cfpg2pf9;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; } gm_tb31k5l794t7p;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
_Static_assert(sizeof(gm_cs4j964akt50um) == 16 && _Alignof(gm_cs4j964akt50um) == 8 && offsetof(gm_cs4j964akt50um, f0) == 0 && offsetof(gm_cs4j964akt50um, f1) == 8 && offsetof(gm_cs4j964akt50um, f2) == 9 && offsetof(gm_cs4j964akt50um, f3) == 10, "layout of `Open`");
_Static_assert(sizeof(gm_cskjv6t053tkje) == 16 && _Alignof(gm_cskjv6t053tkje) == 8 && offsetof(gm_cskjv6t053tkje, f0) == 0 && offsetof(gm_cskjv6t053tkje, f1) == 8, "layout of `Char`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t81j4cfpg2pf9) == 16 && _Alignof(gm_t81j4cfpg2pf9) == 8 && offsetof(gm_t81j4cfpg2pf9, f0) == 0 && offsetof(gm_t81j4cfpg2pf9, f1) == 8 && offsetof(gm_t81j4cfpg2pf9, f2) == 9 && offsetof(gm_t81j4cfpg2pf9, f3) == 10, "layout of `Open`");
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; uint8_t f3; uint8_t f4; uint8_t f5; uint8_t f6; uint8_t f7; uint8_t pe[3]; } gm_t88jm1dgsmkjs;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; gm_t9qkh0rsooc8d f3; gm_t9qkh0rsooc8d f4; gm_t9qkh0rsooc8d f5; gm_t9qkh0rsooc8d f6; gm_t9qkh0rsooc8d f7; } gm_t8doqk5beq9nl;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_tb31k5l794t7p) == 16 && _Alignof(gm_tb31k5l794t7p) == 8 && offsetof(gm_tb31k5l794t7p, f0) == 0 && offsetof(gm_tb31k5l794t7p, f1) == 8, "layout of `Char`");
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; } gm_tk22flr9mh3ql;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { gm_t81j4cfpg2pf9 f0; uint8_t f1; uint8_t pe[7]; } gm_toql92ug1137o;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t9qkh0rsooc8d v1; void * v2; void * v3; gm_tibupt4dmcqai v4; gm_b16_8 v5; gm_b16_8 v6; gm_t9qkh0rsooc8d v7; } f1; } gm_t3fcbuehh3bs2;
_Static_assert(sizeof(gm_t88jm1dgsmkjs) == 40 && _Alignof(gm_t88jm1dgsmkjs) == 8 && offsetof(gm_t88jm1dgsmkjs, f0) == 0 && offsetof(gm_t88jm1dgsmkjs, f1) == 8 && offsetof(gm_t88jm1dgsmkjs, f2) == 16 && offsetof(gm_t88jm1dgsmkjs, f3) == 32 && offsetof(gm_t88jm1dgsmkjs, f4) == 33 && offsetof(gm_t88jm1dgsmkjs, f5) == 34 && offsetof(gm_t88jm1dgsmkjs, f6) == 35 && offsetof(gm_t88jm1dgsmkjs, f7) == 36, "layout of `Info`");
_Static_assert(sizeof(gm_t8doqk5beq9nl) == 8 && _Alignof(gm_t8doqk5beq9nl) == 1 && offsetof(gm_t8doqk5beq9nl, f0) == 0 && offsetof(gm_t8doqk5beq9nl, f1) == 1 && offsetof(gm_t8doqk5beq9nl, f2) == 2 && offsetof(gm_t8doqk5beq9nl, f3) == 3 && offsetof(gm_t8doqk5beq9nl, f4) == 4 && offsetof(gm_t8doqk5beq9nl, f5) == 5 && offsetof(gm_t8doqk5beq9nl, f6) == 6 && offsetof(gm_t8doqk5beq9nl, f7) == 7, "layout of `(Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tk22flr9mh3ql) == 3 && _Alignof(gm_tk22flr9mh3ql) == 1 && offsetof(gm_tk22flr9mh3ql, f0) == 0 && offsetof(gm_tk22flr9mh3ql, f1) == 1 && offsetof(gm_tk22flr9mh3ql, f2) == 2, "layout of `(Keyword, Keyword, Keyword)`");
_Static_assert(sizeof(gm_toql92ug1137o) == 24 && _Alignof(gm_toql92ug1137o) == 8 && offsetof(gm_toql92ug1137o, f0) == 0 && offsetof(gm_toql92ug1137o, f1) == 16, "layout of `?Open`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_t3fcbuehh3bs2) == 32 && _Alignof(gm_t3fcbuehh3bs2) == 8 && offsetof(gm_t3fcbuehh3bs2, f0) == 0 && offsetof(gm_t3fcbuehh3bs2, f1) == 8, "layout of `TokenKind`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tm4pc0emhrv1g;
_Static_assert(sizeof(gm_tm4pc0emhrv1g) == 48 && _Alignof(gm_tm4pc0emhrv1g) == 8 && offsetof(gm_tm4pc0emhrv1g, f0) == 0 && offsetof(gm_tm4pc0emhrv1g, f1) == 32, "layout of `Token`");
struct gm_fr_adjacent_close_065uhgl {
    gm_frame h;
    gm_tm4pc0emhrv1g l3; /* a */
    gm_tm4pc0emhrv1g l4; /* b */
    uint8_t ret;
};
struct gm_fr_classify_e96658j {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* infos */
    gm_trqfvmjtt4sij l5; /* stack */
    gm_trqfvmjtt4sij l7; /* pending_matches */
    gm_tm4pc0emhrv1g l11; /* t */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_deinit_00kfno3 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_deinit_d5p9g61 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ginsuqm {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rqd6vee {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_is_label_colon_rkt3ka8 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_punct_hnuckjb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_at_lviicpm {
    gm_frame h;
    gm_t3fcbuehh3bs2 ret;
};
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_hcibotv {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_hngjb9i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_pop_j4rpvm5 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_pop_u3dhp9e {
    gm_frame h;
    gm_toql92ug1137o ret;
};
struct gm_fr_push_0uvdq16 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_n6hbl90 {
    gm_frame h;
    gm_t88jm1dgsmkjs l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ucgmlo2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3ifo8oj {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_k4j2gj3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_osbhhdi {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_reset_colon_m7bd948 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_starts_type_after_bracke_9p8imef {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_starts_with_non_white_o0cd8c4 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_tight_paren_keyword_rj4a6k9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_tight_paren_prev_p7h76um {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_type_follows_2br4pnf {
    gm_frame h;
    uint8_t ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq2ceic3f8rp(const uint8_t *a, const uint8_t *b);
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) uint8_t gm_f_adjacent_close_065uhgl(gm_b16_8 a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) uint8_t gm_f_all_white_1b9moim(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_classify_e96658j(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_cskjv6t053tkje gm_f_decode_jrk6b03(gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_deinit_00kfno3(void * a0);
inline __attribute__((aligned(4))) void gm_f_deinit_d5p9g61(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ginsuqm(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rqd6vee(void);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_hdeg351(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) uint8_t gm_f_is_label_colon_rkt3ka8(gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_punct_hnuckjb(const void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) uint8_t gm_f_is_punct_hnuckjb_c(gm_t3fcbuehh3bs2 a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) uint8_t gm_f_is_white_3njipsh(uint32_t a0);
inline __attribute__((aligned(4))) void gm_f_kind_at_lviicpm(void *gm_out, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) gm_t3fcbuehh3bs2 gm_f_kind_at_lviicpm_c(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_last_line_hk6q5l4(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_hcibotv(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_hngjb9i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tf5srh4mp4b3i gm_f_pop_j4rpvm5(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_u3dhp9e(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_punct_text_ov4r40u(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0uvdq16(void * a0, gm_cs4j964akt50um a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_n6hbl90(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3ifo8oj(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_osbhhdi(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) void gm_f_reset_colon_m7bd948(void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_starts_type_after_bracke_9p8imef(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_starts_type_after_bracke_9p8imef_c(gm_t3fcbuehh3bs2 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_non_white_o0cd8c4(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_tight_paren_keyword_rj4a6k9(gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) uint8_t gm_f_tight_paren_prev_p7h76um(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_tight_paren_prev_p7h76um_c(gm_t3fcbuehh3bs2 a0);
__attribute__((aligned(4))) uint8_t gm_f_type_follows_2br4pnf(gm_b16_8 a0, uint64_t a1);
extern const uint8_t gm_sm7emfdvcb7c2[3];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_src8kcterbt0p7b[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_stugmrojdno85[6];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq2ceic3f8rp(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
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
inline __attribute__((aligned(4))) void gm_f_deinit_00kfno3(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_00kfno3 F;
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
    gm_b16_8 t21 = gm_f_empty_slice_ginsuqm();
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
inline __attribute__((aligned(4))) void gm_f_deinit_d5p9g61(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_d5p9g61 F;
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
        uint64_t r15; bool o16 = __builtin_mul_overflow(((uint64_t)8ULL), t14, &r15);
        if (o16) gm_trap("integer overflow in `*`", gm_srcrgc9bkquf6nr, 146, 64);
        uint64_t t17 = r15;
        void * t18 = t7;
        gm_b0_1 t19;
        ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t18))(t9, t12, t17, ((uint64_t)8ULL));
    }
    void * t20 = gm_l0;
    gm_b16_8 t21 = gm_f_empty_slice_73v4ttb();
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ginsuqm(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ginsuqm F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rqd6vee(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_rqd6vee F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_punct_hnuckjb(const void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_punct_hnuckjb F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    gm_t9qkh0rsooc8d gm_l1; /* p */
    gm_t9qkh0rsooc8d gm_l2; /* q */
    GM_STACK_CHECK_SHALLOW(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    uint8_t gm_l3;
    {
        {
            uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)8ULL));
            if (!t6) {
                goto gm_lb2;
            }
            gm_t9qkh0rsooc8d t7 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
            gm_l2 = t7;
            gm_t9qkh0rsooc8d t8 = gm_l2;
            gm_t9qkh0rsooc8d t9 = gm_l1;
            uint8_t t10 = (gm_eqv3tj1pu43g(((uint8_t *)&t8), ((uint8_t *)&t9)));
            gm_l3 = t10;
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l3 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_src8kcterbt0p7b, 52, 51);
    }
gm_lb1:;
    uint8_t t11 = gm_l3;
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_kind_at_lviicpm(void *gm_out, gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_at_lviicpm F;
    gm_b16_8 gm_l0; /* toks */
    uint64_t gm_l1; /* j */
    GM_STACK_CHECK_SHALLOW(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t2 = gm_l1;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t4 = (t2 < t3);
    if (t4) {
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t7 = gm_l1;
        memmove(gm_out, (((uint8_t *)t5) + (uintptr_t)t7 * 48), 32);
        goto gm_fdone;
    }
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t3fcbuehh3bs2 *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_t3fcbuehh3bs2 *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_t3fcbuehh3bs2 *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t3fcbuehh3bs2 *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t3fcbuehh3bs2 *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t3fcbuehh3bs2 *)gm_out) + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&(*(gm_t3fcbuehh3bs2 *)gm_out) + 0), ((uint8_t)10ULL));
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
inline __attribute__((aligned(4))) void gm_f_list_hcibotv(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_hcibotv F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_rqd6vee();
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
inline __attribute__((aligned(4))) void gm_f_list_hngjb9i(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_hngjb9i F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_ginsuqm();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tf5srh4mp4b3i gm_f_pop_j4rpvm5(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_j4rpvm5 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    uint64_t gm_l2; /* x */
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
        gm_tf5srh4mp4b3i t6;
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
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t8) + (uintptr_t)t13 * 8));
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
    uint64_t t27 = gm_l2;
    gm_tf5srh4mp4b3i t28;
    GM_ST(uint8_t, (uint8_t *)&t28 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t28 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t28 + 12, 0);
    GM_ST(uint64_t, ((uint8_t *)&t28), t27);
    ((uint8_t *)&t28)[8] = 1;
    F.ret = t28;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_u3dhp9e(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_u3dhp9e F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_t81j4cfpg2pf9 gm_l2; /* x */
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
        memset(&(*(gm_toql92ug1137o *)gm_out), 0, sizeof (*(gm_toql92ug1137o *)gm_out));
        goto gm_fdone;
    }
    void * t6 = gm_l0;
    void * t7 = GM_LD(void *, ((uint8_t *)t6));
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t6) + 8));
    uint64_t t9 = gm_l1;
    uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
    if (o11) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
    uint64_t t12 = r10;
    if ((uintptr_t)t12 >= (uintptr_t)(t8)) gm_trap_index((uintptr_t)t12, t8, gm_srcrgc9bkquf6nr, 133, 10);
    gm_t81j4cfpg2pf9 t13 = GM_LD(gm_t81j4cfpg2pf9, (((uint8_t *)t7) + (uintptr_t)t12 * 16));
    gm_l2 = t13;
    void * t14 = gm_l0;
    void * t15 = gm_l0;
    void * t16 = GM_LD(void *, ((uint8_t *)t15));
    uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t15) + 8));
    uint64_t t18 = gm_l1;
    uint64_t r19; bool o20 = __builtin_sub_overflow(t18, ((uint64_t)1ULL), &r19);
    if (o20) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
    uint64_t t21 = r19;
    uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t23 = (uintptr_t)t21;
    if (t22 > t23 || t23 > (uintptr_t)t17) gm_trap_slice(t22, t23, t17, gm_srcrgc9bkquf6nr, 134, 15);
    uint64_t t24 = ((uintptr_t)t21) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t25;
    GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t16 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(t24));
    GM_ST(gm_b16_8, ((uint8_t *)t14), t25);
    gm_t81j4cfpg2pf9 t26 = gm_l2;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_toql92ug1137o *)gm_out) + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_toql92ug1137o *)gm_out) + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_toql92ug1137o *)gm_out) + 20, 0);
    GM_ST(gm_t81j4cfpg2pf9, ((uint8_t *)&(*(gm_toql92ug1137o *)gm_out)), t26);
    ((uint8_t *)&(*(gm_toql92ug1137o *)gm_out))[16] = 1;
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0uvdq16(void * a0, gm_cs4j964akt50um a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_0uvdq16 F;
    void * gm_l0; /* l */
    gm_t81j4cfpg2pf9 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t81j4cfpg2pf9, a1);
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
        gm_b16_8 t9 = gm_f_empty_slice_ginsuqm();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_3ifo8oj(t8, ((uint64_t)1ULL), t10);
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
    gm_t81j4cfpg2pf9 t18 = gm_l1;
    GM_ST(gm_t81j4cfpg2pf9, (((uint8_t *)t16) + (uintptr_t)t17 * 16), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_n6hbl90(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_n6hbl90 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_rqd6vee();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_osbhhdi(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 40), ((uint8_t *)a1), 40);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3ifo8oj(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_3ifo8oj F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_osbhhdi(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_osbhhdi F;
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
inline __attribute__((aligned(4))) void gm_f_reset_colon_m7bd948(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_reset_colon_m7bd948 F;
    void * gm_l0; /* stack */
    void * gm_l1; /* top_colon */
    GM_STACK_CHECK_SHALLOW(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    uint8_t t4 = (t3 > ((uint64_t)0ULL));
    if (t4) {
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, ((uint8_t *)t5));
        uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t5) + 8));
        void * t8 = gm_l0;
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t8) + 8));
        uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_src8kcterbt0p7b, 96, 21);
        uint64_t t12 = r10;
        if ((uintptr_t)t12 >= (uintptr_t)(t7)) gm_trap_index((uintptr_t)t12, t7, gm_src8kcterbt0p7b, 96, 9);
        GM_ST(uint8_t, ((((uint8_t *)t6) + (uintptr_t)t12 * 16) + 10), ((uint8_t)0));
    } else {
        void * t13 = gm_l1;
        GM_ST(uint8_t, ((uint8_t *)t13), ((uint8_t)0));
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_non_white_o0cd8c4(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_starts_with_non_white_o0cd8c4 F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 81u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tb31k5l794t7p gm_l1;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t3 = ((t2 == ((uint64_t)0ULL)));
    if (t3) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    gm_b16_8 t4 = gm_l0;
    gm_tb31k5l794t7p t5 = GM_CONV(gm_tb31k5l794t7p, gm_f_decode_jrk6b03(t4, ((uint64_t)0ULL)));
    gm_l1 = t5;
    uint32_t t6 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    uint8_t t7 = gm_f_is_white_3njipsh(t6);
    uint8_t t8 = (uint8_t)!t7;
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_tight_paren_prev_p7h76um(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_tight_paren_prev_p7h76um F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    gm_t9qkh0rsooc8d gm_l1; /* kw */
    GM_STACK_CHECK_SHALLOW(&F, 42u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t gm_l2;
    {
        {
            uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)2ULL));
            if (!t6) {
                goto gm_lb2;
            }
            gm_t9qkh0rsooc8d t7 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
            gm_l1 = t7;
            gm_t9qkh0rsooc8d t8 = gm_l1;
            uint8_t t9 = gm_f_tight_paren_keyword_rj4a6k9(t8);
            gm_l2 = t9;
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l2 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_src8kcterbt0p7b, 46, 49);
    }
gm_lb1:;
    uint8_t t10 = gm_l2;
    F.ret = t10;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_adjacent_close_065uhgl(gm_b16_8 a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_adjacent_close_065uhgl F;
    gm_b16_8 gm_l0; /* src */
    gm_b16_8 gm_l1; /* toks */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 208u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l5;
    uint64_t t2 = gm_l2;
    uint64_t r3; bool o4 = __builtin_add_overflow(t2, ((uint64_t)1ULL), &r3);
    if (o4) gm_trap("integer overflow in `+`", gm_src8kcterbt0p7b, 264, 8);
    uint64_t t5 = r3;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t7 = (t5 >= t6);
    if (t7) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    {
        void * t8 = GM_LD(void *, ((uint8_t *)&gm_l1));
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint64_t t10 = gm_l2;
        if ((uintptr_t)t10 >= (uintptr_t)(t9)) gm_trap_index((uintptr_t)t10, t9, gm_src8kcterbt0p7b, 265, 10);
        memmove(((uint8_t *)&F.l3), (((uint8_t *)t8) + (uintptr_t)t10 * 48), 48);
    }
    {
        void * t11 = GM_LD(void *, ((uint8_t *)&gm_l1));
        uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint64_t t13 = gm_l2;
        uint64_t r14; bool o15 = __builtin_add_overflow(t13, ((uint64_t)1ULL), &r14);
        if (o15) gm_trap("integer overflow in `+`", gm_src8kcterbt0p7b, 266, 15);
        uint64_t t16 = r14;
        if ((uintptr_t)t16 >= (uintptr_t)(t12)) gm_trap_index((uintptr_t)t16, t12, gm_src8kcterbt0p7b, 266, 10);
        memmove(((uint8_t *)&F.l4), (((uint8_t *)t11) + (uintptr_t)t16 * 48), 48);
    }
    {
        gm_t9qkh0rsooc8d t17;
        GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)66ULL));
        uint8_t t18 = gm_f_is_punct_hnuckjb(&(*(gm_t3fcbuehh3bs2 *)((uint8_t *)&F.l4)), t17);
        if (t18) {
            void * t19 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint32_t t21 = GM_LD(uint32_t, ((((uint8_t *)&F.l3) + 32) + 8));
            uint64_t t22 = (uint64_t)t21;
            uint32_t t23 = GM_LD(uint32_t, ((((uint8_t *)&F.l4) + 32) + 4));
            uint64_t t24 = (uint64_t)t23;
            uint64_t t25 = (uintptr_t)t22;
            uint64_t t26 = (uintptr_t)t24;
            if (t25 > t26 || t26 > (uintptr_t)t20) gm_trap_slice(t25, t26, t20, gm_src8kcterbt0p7b, 267, 52);
            uint64_t t27 = ((uintptr_t)t24) - (uintptr_t)t22;
            gm_b16_8 t28;
            GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t19 + (uintptr_t)t22 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
            uint8_t t29 = gm_f_all_white_1b9moim(t28);
            gm_l5 = t29;
        } else {
            gm_l5 = ((uint8_t)0);
        }
    }
    uint8_t t30 = gm_l5;
    F.ret = t30;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_classify_e96658j(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_classify_e96658j F;
    gm_b16_8 gm_l0; /* src */
    gm_b16_8 gm_l1; /* toks */
    uint8_t gm_l3; /* ends */
    gm_tf5srh4mp4b3i gm_l4; /* prev */
    uint8_t gm_l6; /* top_colon */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    uint64_t gm_l12; /* lo */
    uint64_t gm_l13; /* hi */
    uint8_t gm_l14; /* tb */
    uint8_t gm_l15; /* ta */
    uint8_t gm_l16; /* open */
    uint8_t gm_l17; /* close */
    uint8_t gm_l18; /* match_body */
    gm_b16_8 gm_l19; /* txt */
    uint64_t gm_l20; /* p */
    gm_t3fcbuehh3bs2 gm_l21; /* prev_kind */
    gm_b16_8 gm_l22; /* s */
    gm_t9qkh0rsooc8d gm_l23; /* k */
    gm_t9qkh0rsooc8d gm_l24; /* p */
    uint8_t gm_l25; /* attr */
    gm_toql92ug1137o gm_l26; /* popped */
    gm_t81j4cfpg2pf9 gm_l27; /* o */
    uint8_t gm_l28; /* starts_line */
    uint8_t gm_l29; /* seen */
    gm_b16_8 gm_l30; /* word */
    GM_STACK_CHECK(&F, 2021u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l31;
    uint8_t gm_l32;
    uint8_t gm_l33;
    uint8_t gm_l34;
    uint8_t gm_l35;
    gm_tftb1odc739t5 gm_l36;
    gm_tftb1odc739t5 gm_l37;
    gm_tftb1odc739t5 gm_l38;
    uint8_t gm_l39;
    uint8_t gm_l40;
    uint8_t gm_l41;
    uint8_t gm_l42;
    uint8_t gm_l43;
    uint8_t gm_l44;
    gm_tf5srh4mp4b3i gm_l45;
    gm_tftb1odc739t5 gm_l46;
    {
        {
            gm_trqfvmjtt4sij t89;
            gm_f_list_hcibotv((void *)&t89, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l2 = t89;
            gm_l3 = ((uint8_t)0);
            gm_tf5srh4mp4b3i t90;
            memset(&t90, 0, sizeof t90);
            gm_l4 = t90;
            gm_trqfvmjtt4sij t91;
            gm_f_list_hngjb9i((void *)&t91, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l5 = t91;
            gm_l6 = ((uint8_t)0);
            gm_trqfvmjtt4sij t92;
            gm_f_list_5dhgg4i((void *)&t92, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld88_1;
            }
            F.l7 = t92;
            gm_l9 = ((uint64_t)0ULL);
            uint64_t t93 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            gm_l8 = t93;
            {
                for (;;) {
                    {
                        uint64_t t94 = gm_l9;
                        uint64_t t95 = gm_l8;
                        uint8_t t96 = (t94 < t95);
                        if (!t96) {
                            goto gm_lb2;
                        }
                        {
                            {
                                {
                                    {
                                        {
                                            uint64_t t97 = gm_l9;
                                            gm_l10 = t97;
                                            void * t98 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                            uint64_t t99 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                            uint64_t t100 = gm_l10;
                                            memmove(((uint8_t *)&F.l11), (((uint8_t *)t98) + (uintptr_t)t100 * 48), 48);
                                            uint32_t t101 = GM_LD(uint32_t, ((((uint8_t *)&F.l11) + 32) + 4));
                                            uint64_t t102 = (uint64_t)t101;
                                            gm_l12 = t102;
                                            uint32_t t103 = GM_LD(uint32_t, ((((uint8_t *)&F.l11) + 32) + 8));
                                            uint64_t t104 = (uint64_t)t103;
                                            gm_l13 = t104;
                                            gm_l14 = ((uint8_t)0);
                                            gm_l15 = ((uint8_t)0);
                                            gm_l16 = ((uint8_t)0);
                                            gm_l17 = ((uint8_t)0);
                                            gm_l18 = ((uint8_t)0);
                                            gm_b16_8 t105;
                                            GM_ST(void *, (uint8_t *)&t105, (void *)(gm_sr4qbrk6tlgaa));
                                            GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)(0));
                                            gm_l19 = t105;
                                            {
                                                {
                                                    uint8_t t106 = (uint8_t)((((uint8_t *)&gm_l4))[8] != 0);
                                                    if (!t106) {
                                                        goto gm_lb8;
                                                    }
                                                    uint64_t t107 = GM_LD(uint64_t, ((uint8_t *)&gm_l4));
                                                    gm_l20 = t107;
                                                    void * t108 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                                    uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                                    uint64_t t110 = gm_l20;
                                                    if ((uintptr_t)t110 >= (uintptr_t)(t109)) gm_trap_index((uintptr_t)t110, t109, gm_src8kcterbt0p7b, 131, 37);
                                                    memmove(((uint8_t *)&gm_l21), (((uint8_t *)t108) + (uintptr_t)t110 * 48), 32);
                                                    goto gm_lb7;
                                                }
gm_lb8:;
                                                GM_ST(uint8_t, (uint8_t *)&gm_l21 + 1, 0);
                                                GM_ST(uint16_t, (uint8_t *)&gm_l21 + 2, 0);
                                                GM_ST(uint32_t, (uint8_t *)&gm_l21 + 4, 0);
                                                GM_ST(uint64_t, (uint8_t *)&gm_l21 + 8, 0);
                                                GM_ST(uint64_t, (uint8_t *)&gm_l21 + 16, 0);
                                                GM_ST(uint64_t, (uint8_t *)&gm_l21 + 24, 0);
                                                GM_ST(uint8_t, ((uint8_t *)&gm_l21 + 0), ((uint8_t)9ULL));
                                            }
gm_lb7:;
                                            {
                                                {
                                                    {
                                                        uint8_t t111 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
                                                        uint8_t t112 = (uint8_t)(t111 == ((uint8_t)9ULL));
                                                        if (!t112) {
                                                            goto gm_lb10;
                                                        }
                                                        gm_l3 = ((uint8_t)0);
                                                        gm_tf5srh4mp4b3i t113;
                                                        memset(&t113, 0, sizeof t113);
                                                        gm_l4 = t113;
                                                        void * t114 = (void *)((uint8_t *)&F.l5);
                                                        void * t115 = (void *)((uint8_t *)&gm_l6);
                                                        gm_f_reset_colon_m7bd948(t114, t115);
                                                        goto gm_lb4;
                                                    }
gm_lb10:;
                                                    {
                                                        uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
                                                        uint8_t t117 = (uint8_t)(t116 == ((uint8_t)10ULL));
                                                        if (!t117) {
                                                            goto gm_lb11;
                                                        }
                                                        goto gm_lb2;
                                                    }
gm_lb11:;
                                                    {
                                                        uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
                                                        uint8_t t119 = (uint8_t)(t118 == ((uint8_t)0ULL));
                                                        if (!t119) {
                                                            goto gm_lb12;
                                                        }
                                                        gm_b16_8 t120 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 8));
                                                        gm_l22 = t120;
                                                        gm_b16_8 t121 = gm_l22;
                                                        gm_b16_8 t122;
                                                        GM_ST(void *, (uint8_t *)&t122, (void *)(gm_stugmrojdno85));
                                                        GM_ST(uintptr_t, (uint8_t *)&t122 + 8, (uintptr_t)(5));
                                                        uint8_t t123 = (!gm_eq0iqiubfm7e(((uint8_t *)&t121), ((uint8_t *)&t122)));
                                                        gm_l3 = t123;
                                                        goto gm_lb9;
                                                    }
gm_lb12:;
                                                    {
                                                        {
                                                            {
                                                                uint8_t t124 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
                                                                uint8_t t125 = (uint8_t)(t124 == ((uint8_t)1ULL));
                                                                if (!t125) {
                                                                    goto gm_lb15;
                                                                }
                                                                goto gm_lb14;
                                                            }
gm_lb15:;
                                                            {
                                                                uint8_t t126 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
                                                                uint8_t t127 = (uint8_t)(t126 == ((uint8_t)3ULL));
                                                                if (!t127) {
                                                                    goto gm_lb16;
                                                                }
                                                                goto gm_lb14;
                                                            }
gm_lb16:;
                                                            {
                                                                uint8_t t128 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
                                                                uint8_t t129 = (uint8_t)(t128 == ((uint8_t)4ULL));
                                                                if (!t129) {
                                                                    goto gm_lb17;
                                                                }
                                                                goto gm_lb14;
                                                            }
gm_lb17:;
                                                            {
                                                                uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
                                                                uint8_t t131 = (uint8_t)(t130 == ((uint8_t)5ULL));
                                                                if (!t131) {
                                                                    goto gm_lb18;
                                                                }
                                                                goto gm_lb14;
                                                            }
gm_lb18:;
                                                            {
                                                                uint8_t t132 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
                                                                uint8_t t133 = (uint8_t)(t132 == ((uint8_t)6ULL));
                                                                if (!t133) {
                                                                    goto gm_lb19;
                                                                }
                                                                goto gm_lb14;
                                                            }
gm_lb19:;
                                                            goto gm_lb13;
                                                        }
gm_lb14:;
                                                        gm_l3 = ((uint8_t)1);
                                                        goto gm_lb9;
                                                    }
gm_lb13:;
                                                    {
                                                        uint8_t t134 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
                                                        uint8_t t135 = (uint8_t)(t134 == ((uint8_t)7ULL));
                                                        if (!t135) {
                                                            goto gm_lb20;
                                                        }
                                                        gm_l3 = ((uint8_t)0);
                                                        goto gm_lb9;
                                                    }
gm_lb20:;
                                                    {
                                                        uint8_t t136 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
                                                        uint8_t t137 = (uint8_t)(t136 == ((uint8_t)2ULL));
                                                        if (!t137) {
                                                            goto gm_lb21;
                                                        }
                                                        gm_t9qkh0rsooc8d t138 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l11) + 8));
                                                        gm_l23 = t138;
                                                        gm_t9qkh0rsooc8d t139 = gm_l23;
                                                        uint8_t t140 = GM_LD(uint8_t, (((uint8_t *)&t139) + 0));
                                                        uint8_t t141 = (uint8_t)((t140 == ((uint8_t)17ULL)));
                                                        gm_t9qkh0rsooc8d t142;
                                                        GM_ST(uint8_t, ((uint8_t *)&t142 + 0), ((uint8_t)17ULL));
                                                        if (t141) {
                                                            void * t143 = (void *)((uint8_t *)&F.l7);
                                                            uint64_t t144 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
                                                            gm_tftb1odc739t5 t145 = gm_f_push_ucgmlo2(t143, t144);
                                                            gm_l31 = t145;
                                                            uint8_t t146 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                                                            if (!t146) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 1, gm_src8kcterbt0p7b, 144, 34);
                                                        }
                                                        gm_t9qkh0rsooc8d t147 = gm_l23;
                                                        gm_t9qkh0rsooc8d t148;
                                                        GM_ST(uint8_t, ((uint8_t *)&t148 + 0), ((uint8_t)25ULL));
                                                        gm_t9qkh0rsooc8d t149;
                                                        GM_ST(uint8_t, ((uint8_t *)&t149 + 0), ((uint8_t)10ULL));
                                                        gm_t9qkh0rsooc8d t150;
                                                        GM_ST(uint8_t, ((uint8_t *)&t150 + 0), ((uint8_t)19ULL));
                                                        gm_tk22flr9mh3ql t151;
                                                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t151 + 0), t148);
                                                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t151 + 1), t149);
                                                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t151 + 2), t150);
                                                        uint8_t t152 = 0;
                                                        if (gm_eq2ceic3f8rp(((uint8_t *)&t151) + 0, ((uint8_t *)&t147))) t152 = 1;
                                                        if (gm_eq2ceic3f8rp(((uint8_t *)&t151) + 1, ((uint8_t *)&t147))) t152 = 1;
                                                        if (gm_eq2ceic3f8rp(((uint8_t *)&t151) + 2, ((uint8_t *)&t147))) t152 = 1;
                                                        uint8_t t153 = (uint8_t)(t152);
                                                        gm_l3 = t153;
                                                        goto gm_lb9;
                                                    }
gm_lb21:;
                                                }
                                                {
                                                    {
                                                        {
                                                            uint8_t t154 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
                                                            uint8_t t155 = (uint8_t)(t154 == ((uint8_t)8ULL));
                                                            if (!t155) {
                                                                goto gm_lb22;
                                                            }
                                                            gm_t9qkh0rsooc8d t156 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l11) + 8));
                                                            gm_l24 = t156;
                                                            {
                                                                {
                                                                    {
                                                                        uint8_t t157 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                                                                        uint8_t t158 = (uint8_t)(t157 == ((uint8_t)61ULL));
                                                                        if (!t158) {
                                                                            goto gm_lb24;
                                                                        }
                                                                        uint8_t t159 = gm_l3;
                                                                        if (t159) {
                                                                            gm_l34 = ((uint8_t)1);
                                                                        } else {
                                                                            uint8_t t160 = gm_f_tight_paren_prev_p7h76um(&(*(gm_t3fcbuehh3bs2 *)((uint8_t *)&gm_l21)));
                                                                            gm_l34 = t160;
                                                                        }
                                                                        uint8_t t161 = gm_l34;
                                                                        if (t161) {
                                                                            gm_l33 = ((uint8_t)1);
                                                                        } else {
                                                                            {
                                                                                {
                                                                                    uint8_t t162 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
                                                                                    uint8_t t163 = (uint8_t)(t162 == ((uint8_t)7ULL));
                                                                                    if (!t163) {
                                                                                        goto gm_lb26;
                                                                                    }
                                                                                    gm_l35 = ((uint8_t)1);
                                                                                    goto gm_lb25;
                                                                                }
gm_lb26:;
                                                                                {
                                                                                    uint8_t t164 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
                                                                                    uint8_t t165 = (uint8_t)(t164 == ((uint8_t)6ULL));
                                                                                    if (!t165) {
                                                                                        goto gm_lb27;
                                                                                    }
                                                                                    gm_l35 = ((uint8_t)1);
                                                                                    goto gm_lb25;
                                                                                }
gm_lb27:;
                                                                                {
                                                                                    gm_l35 = ((uint8_t)0);
                                                                                    goto gm_lb25;
                                                                                }
gm_lb28:;
                                                                                gm_trap("no match arm matched the scrutinee", gm_src8kcterbt0p7b, 149, 65);
                                                                            }
gm_lb25:;
                                                                            uint8_t t166 = gm_l35;
                                                                            gm_l33 = t166;
                                                                        }
                                                                        uint8_t t167 = gm_l33;
                                                                        if (t167) {
                                                                            gm_l32 = ((uint8_t)1);
                                                                        } else {
                                                                            gm_t9qkh0rsooc8d t168;
                                                                            GM_ST(uint8_t, ((uint8_t *)&t168 + 0), ((uint8_t)46ULL));
                                                                            uint8_t t169 = gm_f_is_punct_hnuckjb(&(*(gm_t3fcbuehh3bs2 *)((uint8_t *)&gm_l21)), t168);
                                                                            gm_l32 = t169;
                                                                        }
                                                                        uint8_t t170 = gm_l32;
                                                                        gm_l14 = t170;
                                                                        gm_l15 = ((uint8_t)1);
                                                                        gm_l16 = ((uint8_t)1);
                                                                        {
                                                                            {
                                                                                uint8_t t171 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
                                                                                uint8_t t172 = (uint8_t)(t171 == ((uint8_t)7ULL));
                                                                                if (!t172) {
                                                                                    goto gm_lb30;
                                                                                }
                                                                                gm_l25 = ((uint8_t)1);
                                                                                goto gm_lb29;
                                                                            }
gm_lb30:;
                                                                            {
                                                                                gm_l25 = ((uint8_t)0);
                                                                                goto gm_lb29;
                                                                            }
gm_lb31:;
                                                                            gm_trap("no match arm matched the scrutinee", gm_src8kcterbt0p7b, 152, 29);
                                                                        }
gm_lb29:;
                                                                        void * t173 = (void *)((uint8_t *)&F.l5);
                                                                        uint64_t t174 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                                                                        uint8_t t175 = gm_l25;
                                                                        gm_t81j4cfpg2pf9 t176;
                                                                        GM_ST(uint8_t, (uint8_t *)&t176 + 11, 0);
                                                                        GM_ST(uint32_t, (uint8_t *)&t176 + 12, 0);
                                                                        GM_ST(uint64_t, ((uint8_t *)&t176 + 0), t174);
                                                                        GM_ST(uint8_t, ((uint8_t *)&t176 + 8), ((uint8_t)0));
                                                                        GM_ST(uint8_t, ((uint8_t *)&t176 + 9), t175);
                                                                        GM_ST(uint8_t, ((uint8_t *)&t176 + 10), ((uint8_t)0));
                                                                        gm_tftb1odc739t5 t177 = gm_f_push_0uvdq16(t173, GM_CONV(gm_cs4j964akt50um, t176));
                                                                        gm_l36 = t177;
                                                                        uint8_t t178 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
                                                                        if (!t178) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 1, gm_src8kcterbt0p7b, 153, 21);
                                                                        gm_l3 = ((uint8_t)0);
                                                                        goto gm_lb23;
                                                                    }
gm_lb24:;
                                                                    {
                                                                        uint8_t t179 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                                                                        uint8_t t180 = (uint8_t)(t179 == ((uint8_t)63ULL));
                                                                        if (!t180) {
                                                                            goto gm_lb32;
                                                                        }
                                                                        uint8_t t181 = gm_l3;
                                                                        gm_l14 = t181;
                                                                        gm_l15 = ((uint8_t)1);
                                                                        gm_l16 = ((uint8_t)1);
                                                                        void * t182 = (void *)((uint8_t *)&F.l5);
                                                                        uint64_t t183 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                                                                        uint8_t t184 = gm_l3;
                                                                        uint8_t t185 = (uint8_t)!t184;
                                                                        gm_t81j4cfpg2pf9 t186;
                                                                        GM_ST(uint8_t, (uint8_t *)&t186 + 11, 0);
                                                                        GM_ST(uint32_t, (uint8_t *)&t186 + 12, 0);
                                                                        GM_ST(uint64_t, ((uint8_t *)&t186 + 0), t183);
                                                                        GM_ST(uint8_t, ((uint8_t *)&t186 + 8), t185);
                                                                        GM_ST(uint8_t, ((uint8_t *)&t186 + 9), ((uint8_t)0));
                                                                        GM_ST(uint8_t, ((uint8_t *)&t186 + 10), ((uint8_t)0));
                                                                        gm_tftb1odc739t5 t187 = gm_f_push_0uvdq16(t182, GM_CONV(gm_cs4j964akt50um, t186));
                                                                        gm_l37 = t187;
                                                                        uint8_t t188 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
                                                                        if (!t188) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 1, gm_src8kcterbt0p7b, 160, 21);
                                                                        gm_l3 = ((uint8_t)0);
                                                                        goto gm_lb23;
                                                                    }
gm_lb32:;
                                                                }
                                                                {
                                                                    {
                                                                        uint8_t t189 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                                                                        uint8_t t190 = (uint8_t)(t189 == ((uint8_t)65ULL));
                                                                        if (!t190) {
                                                                            goto gm_lb33;
                                                                        }
                                                                        {
                                                                            {
                                                                                uint64_t t191 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
                                                                                uint8_t t192 = (t191 > ((uint64_t)0ULL));
                                                                                if (!t192) {
                                                                                    goto gm_lb35;
                                                                                }
                                                                                void * t193 = GM_LD(void *, ((uint8_t *)&F.l7));
                                                                                uint64_t t194 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
                                                                                uint64_t t195 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
                                                                                uint64_t r196; bool o197 = __builtin_sub_overflow(t195, ((uint64_t)1ULL), &r196);
                                                                                if (o197) gm_trap("integer overflow in `-`", gm_src8kcterbt0p7b, 164, 80);
                                                                                uint64_t t198 = r196;
                                                                                if ((uintptr_t)t198 >= (uintptr_t)(t194)) gm_trap_index((uintptr_t)t198, t194, gm_src8kcterbt0p7b, 164, 58);
                                                                                uint64_t t199 = GM_LD(uint64_t, (((uint8_t *)t193) + (uintptr_t)t198 * 8));
                                                                                uint64_t t200 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
                                                                                uint8_t t201 = ((t199 == t200));
                                                                                if (!t201) {
                                                                                    goto gm_lb35;
                                                                                }
                                                                                void * t202 = (void *)((uint8_t *)&F.l7);
                                                                                gm_tf5srh4mp4b3i t203 = gm_f_pop_j4rpvm5(t202);
                                                                                gm_l18 = ((uint8_t)1);
                                                                                goto gm_lb34;
                                                                            }
gm_lb35:;
                                                                        }
gm_lb34:;
                                                                        gm_b16_8 t204 = gm_l0;
                                                                        gm_b16_8 t205 = gm_l1;
                                                                        uint64_t t206 = gm_l10;
                                                                        uint8_t t207 = gm_f_adjacent_close_065uhgl(t204, t205, t206);
                                                                        gm_l15 = t207;
                                                                        gm_l16 = ((uint8_t)1);
                                                                        void * t208 = (void *)((uint8_t *)&F.l5);
                                                                        uint64_t t209 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                                                                        gm_t81j4cfpg2pf9 t210;
                                                                        GM_ST(uint8_t, (uint8_t *)&t210 + 11, 0);
                                                                        GM_ST(uint32_t, (uint8_t *)&t210 + 12, 0);
                                                                        GM_ST(uint64_t, ((uint8_t *)&t210 + 0), t209);
                                                                        GM_ST(uint8_t, ((uint8_t *)&t210 + 8), ((uint8_t)0));
                                                                        GM_ST(uint8_t, ((uint8_t *)&t210 + 9), ((uint8_t)0));
                                                                        GM_ST(uint8_t, ((uint8_t *)&t210 + 10), ((uint8_t)0));
                                                                        gm_tftb1odc739t5 t211 = gm_f_push_0uvdq16(t208, GM_CONV(gm_cs4j964akt50um, t210));
                                                                        gm_l38 = t211;
                                                                        uint8_t t212 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                                                                        if (!t212) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 1, gm_src8kcterbt0p7b, 170, 21);
                                                                        gm_l3 = ((uint8_t)0);
                                                                        goto gm_lb23;
                                                                    }
gm_lb33:;
                                                                    {
                                                                        {
                                                                            {
                                                                                uint8_t t213 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                                                                                uint8_t t214 = (uint8_t)(t213 == ((uint8_t)62ULL));
                                                                                if (!t214) {
                                                                                    goto gm_lb38;
                                                                                }
                                                                                goto gm_lb37;
                                                                            }
gm_lb38:;
                                                                            {
                                                                                uint8_t t215 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                                                                                uint8_t t216 = (uint8_t)(t215 == ((uint8_t)64ULL));
                                                                                if (!t216) {
                                                                                    goto gm_lb39;
                                                                                }
                                                                                goto gm_lb37;
                                                                            }
gm_lb39:;
                                                                            {
                                                                                uint8_t t217 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                                                                                uint8_t t218 = (uint8_t)(t217 == ((uint8_t)66ULL));
                                                                                if (!t218) {
                                                                                    goto gm_lb40;
                                                                                }
                                                                                goto gm_lb37;
                                                                            }
gm_lb40:;
                                                                            goto gm_lb36;
                                                                        }
gm_lb37:;
                                                                        gm_l17 = ((uint8_t)1);
                                                                        gm_l14 = ((uint8_t)1);
                                                                        void * t219 = (void *)((uint8_t *)&F.l5);
                                                                        gm_f_pop_u3dhp9e((void *)&gm_l26, t219);
                                                                        gm_l3 = ((uint8_t)1);
                                                                        {
                                                                            {
                                                                                uint8_t t220 = (uint8_t)((((uint8_t *)&gm_l26))[16] != 0);
                                                                                if (!t220) {
                                                                                    goto gm_lb42;
                                                                                }
                                                                                gm_t81j4cfpg2pf9 t221 = GM_LD(gm_t81j4cfpg2pf9, ((uint8_t *)&gm_l26));
                                                                                gm_l27 = t221;
                                                                                uint8_t t222 = GM_LD(uint8_t, (((uint8_t *)&gm_l27) + 9));
                                                                                uint8_t t223 = (uint8_t)!t222;
                                                                                gm_l3 = t223;
                                                                                {
                                                                                    {
                                                                                        gm_t9qkh0rsooc8d t224 = gm_l24;
                                                                                        uint8_t t225 = GM_LD(uint8_t, (((uint8_t *)&t224) + 0));
                                                                                        uint8_t t226 = (uint8_t)((t225 == ((uint8_t)64ULL)));
                                                                                        if (!t226) {
                                                                                            goto gm_lb44;
                                                                                        }
                                                                                        uint8_t t227 = GM_LD(uint8_t, (((uint8_t *)&gm_l27) + 8));
                                                                                        if (!t227) {
                                                                                            goto gm_lb44;
                                                                                        }
                                                                                        gm_b16_8 t228 = gm_l1;
                                                                                        uint64_t t229 = gm_l10;
                                                                                        uint64_t r230; bool o231 = __builtin_add_overflow(t229, ((uint64_t)1ULL), &r230);
                                                                                        if (o231) gm_trap("integer overflow in `+`", gm_src8kcterbt0p7b, 180, 100);
                                                                                        uint64_t t232 = r230;
                                                                                        gm_t9qkh0rsooc8d t233;
                                                                                        GM_ST(uint8_t, ((uint8_t *)&t233 + 0), ((uint8_t)64ULL));
                                                                                        gm_t3fcbuehh3bs2 t234;
                                                                                        gm_f_kind_at_lviicpm((void *)&t234, t228, t232);
                                                                                        uint8_t t235 = gm_f_starts_type_after_bracke_9p8imef(&t234);
                                                                                        if (!t235) {
                                                                                            goto gm_lb44;
                                                                                        }
                                                                                        gm_l15 = ((uint8_t)1);
                                                                                        gm_l3 = ((uint8_t)0);
                                                                                        goto gm_lb43;
                                                                                    }
gm_lb44:;
                                                                                }
gm_lb43:;
                                                                                gm_t9qkh0rsooc8d t236 = gm_l24;
                                                                                uint8_t t237 = GM_LD(uint8_t, (((uint8_t *)&t236) + 0));
                                                                                uint8_t t238 = (uint8_t)((t237 == ((uint8_t)66ULL)));
                                                                                gm_t9qkh0rsooc8d t239;
                                                                                GM_ST(uint8_t, ((uint8_t *)&t239 + 0), ((uint8_t)66ULL));
                                                                                if (t238) {
                                                                                    uint64_t t240 = GM_LD(uint64_t, ((uint8_t *)&gm_l27));
                                                                                    uint64_t r241; bool o242 = __builtin_add_overflow(t240, ((uint64_t)1ULL), &r241);
                                                                                    if (o242) gm_trap("integer overflow in `+`", gm_src8kcterbt0p7b, 185, 34);
                                                                                    uint64_t t243 = r241;
                                                                                    uint64_t t244 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                                                                                    uint8_t t245 = ((t243 == t244));
                                                                                    if (t245) {
                                                                                        void * t246 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                                                                        uint64_t t247 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                                                                        void * t248 = GM_LD(void *, ((uint8_t *)&F.l2));
                                                                                        uint64_t t249 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                                                                                        uint64_t t250 = GM_LD(uint64_t, ((uint8_t *)&gm_l27));
                                                                                        if ((uintptr_t)t250 >= (uintptr_t)(t249)) gm_trap_index((uintptr_t)t250, t249, gm_src8kcterbt0p7b, 185, 82);
                                                                                        uint64_t t251 = GM_LD(uint64_t, ((((uint8_t *)t248) + (uintptr_t)t250 * 40) + 8));
                                                                                        uint64_t t252 = gm_l12;
                                                                                        uint64_t t253 = (uintptr_t)t251;
                                                                                        uint64_t t254 = (uintptr_t)t252;
                                                                                        if (t253 > t254 || t254 > (uintptr_t)t247) gm_trap_slice(t253, t254, t247, gm_src8kcterbt0p7b, 185, 78);
                                                                                        uint64_t t255 = ((uintptr_t)t252) - (uintptr_t)t251;
                                                                                        gm_b16_8 t256;
                                                                                        GM_ST(void *, (uint8_t *)&t256, (void *)(((uint8_t *)t246 + (uintptr_t)t251 * 1)));
                                                                                        GM_ST(uintptr_t, (uint8_t *)&t256 + 8, (uintptr_t)(t255));
                                                                                        uint8_t t257 = gm_f_all_white_1b9moim(t256);
                                                                                        gm_l39 = t257;
                                                                                    } else {
                                                                                        gm_l39 = ((uint8_t)0);
                                                                                    }
                                                                                    uint8_t t258 = gm_l39;
                                                                                    gm_l14 = t258;
                                                                                }
                                                                                goto gm_lb41;
                                                                            }
gm_lb42:;
                                                                        }
gm_lb41:;
                                                                        goto gm_lb23;
                                                                    }
gm_lb36:;
                                                                }
                                                                {
                                                                    {
                                                                        uint8_t t259 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                                                                        uint8_t t260 = (uint8_t)(t259 == ((uint8_t)57ULL));
                                                                        if (!t260) {
                                                                            goto gm_lb45;
                                                                        }
                                                                        gm_l14 = ((uint8_t)1);
                                                                        gm_l3 = ((uint8_t)0);
                                                                        void * t261 = (void *)((uint8_t *)&F.l5);
                                                                        void * t262 = (void *)((uint8_t *)&gm_l6);
                                                                        gm_f_reset_colon_m7bd948(t261, t262);
                                                                        goto gm_lb23;
                                                                    }
gm_lb45:;
                                                                    {
                                                                        {
                                                                            {
                                                                                uint8_t t263 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                                                                                uint8_t t264 = (uint8_t)(t263 == ((uint8_t)58ULL));
                                                                                if (!t264) {
                                                                                    goto gm_lb48;
                                                                                }
                                                                                goto gm_lb47;
                                                                            }
gm_lb48:;
                                                                            {
                                                                                uint8_t t265 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                                                                                uint8_t t266 = (uint8_t)(t265 == ((uint8_t)50ULL));
                                                                                if (!t266) {
                                                                                    goto gm_lb49;
                                                                                }
                                                                                goto gm_lb47;
                                                                            }
gm_lb49:;
                                                                            {
                                                                                uint8_t t267 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                                                                                uint8_t t268 = (uint8_t)(t267 == ((uint8_t)51ULL));
                                                                                if (!t268) {
                                                                                    goto gm_lb50;
                                                                                }
                                                                                goto gm_lb47;
                                                                            }
gm_lb50:;
                                                                            goto gm_lb46;
                                                                        }
gm_lb47:;
                                                                        uint8_t t269 = gm_l3;
                                                                        gm_l14 = t269;
                                                                        gm_l15 = ((uint8_t)1);
                                                                        {
                                                                            {
                                                                                uint8_t t270 = (uint8_t)((((uint8_t *)&gm_l4))[8] != 0);
                                                                                if (t270) {
                                                                                    goto gm_lb52;
                                                                                }
                                                                                gm_l41 = ((uint8_t)1);
                                                                                goto gm_lb51;
                                                                            }
gm_lb52:;
                                                                            {
                                                                                gm_l41 = ((uint8_t)0);
                                                                                goto gm_lb51;
                                                                            }
gm_lb53:;
                                                                            gm_trap("no match arm matched the scrutinee", gm_src8kcterbt0p7b, 200, 36);
                                                                        }
gm_lb51:;
                                                                        uint8_t t271 = gm_l41;
                                                                        if (t271) {
                                                                            gm_l40 = ((uint8_t)1);
                                                                        } else {
                                                                            void * t272 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                                                            uint64_t t273 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                                                            uint64_t t274 = gm_l12;
                                                                            uint64_t t275 = (uintptr_t)((uint64_t)0ULL);
                                                                            uint64_t t276 = (uintptr_t)t274;
                                                                            if (t275 > t276 || t276 > (uintptr_t)t273) gm_trap_slice(t275, t276, t273, gm_src8kcterbt0p7b, 200, 72);
                                                                            uint64_t t277 = ((uintptr_t)t274) - (uintptr_t)((uint64_t)0ULL);
                                                                            gm_b16_8 t278;
                                                                            GM_ST(void *, (uint8_t *)&t278, (void *)(((uint8_t *)t272 + (uintptr_t)((uint64_t)0ULL) * 1)));
                                                                            GM_ST(uintptr_t, (uint8_t *)&t278 + 8, (uintptr_t)(t277));
                                                                            gm_b16_8 t279 = gm_f_last_line_hk6q5l4(t278);
                                                                            uint8_t t280 = gm_f_all_white_1b9moim(t279);
                                                                            gm_l40 = t280;
                                                                        }
                                                                        uint8_t t281 = gm_l40;
                                                                        gm_l28 = t281;
                                                                        {
                                                                            {
                                                                                gm_t9qkh0rsooc8d t282 = gm_l24;
                                                                                uint8_t t283 = GM_LD(uint8_t, (((uint8_t *)&t282) + 0));
                                                                                uint8_t t284 = (uint8_t)((t283 == ((uint8_t)58ULL)));
                                                                                if (!t284) {
                                                                                    goto gm_lb55;
                                                                                }
                                                                                uint8_t t285 = gm_l28;
                                                                                if (!t285) {
                                                                                    goto gm_lb55;
                                                                                }
                                                                                void * t286 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                                                                uint64_t t287 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                                                                uint64_t t288 = gm_l13;
                                                                                uint64_t t289 = (uintptr_t)t288;
                                                                                uint64_t t290 = (uintptr_t)t287;
                                                                                if (t289 > t290 || t290 > (uintptr_t)t287) gm_trap_slice(t289, t290, t287, gm_src8kcterbt0p7b, 201, 80);
                                                                                uint64_t t291 = ((uintptr_t)t287) - (uintptr_t)t288;
                                                                                gm_b16_8 t292;
                                                                                GM_ST(void *, (uint8_t *)&t292, (void *)(((uint8_t *)t286 + (uintptr_t)t288 * 1)));
                                                                                GM_ST(uintptr_t, (uint8_t *)&t292 + 8, (uintptr_t)(t291));
                                                                                gm_t9qkh0rsooc8d t293;
                                                                                GM_ST(uint8_t, ((uint8_t *)&t293 + 0), ((uint8_t)58ULL));
                                                                                uint8_t t294 = gm_f_starts_with_non_white_o0cd8c4(t292);
                                                                                uint8_t t295 = (uint8_t)!t294;
                                                                                if (!t295) {
                                                                                    goto gm_lb55;
                                                                                }
                                                                                gm_l15 = ((uint8_t)0);
                                                                                goto gm_lb54;
                                                                            }
gm_lb55:;
                                                                        }
gm_lb54:;
                                                                        {
                                                                            {
                                                                                gm_t9qkh0rsooc8d t296 = gm_l24;
                                                                                uint8_t t297 = GM_LD(uint8_t, (((uint8_t *)&t296) + 0));
                                                                                uint8_t t298 = (uint8_t)((t297 == ((uint8_t)58ULL)));
                                                                                if (!t298) {
                                                                                    goto gm_lb57;
                                                                                }
                                                                                gm_t9qkh0rsooc8d t299;
                                                                                GM_ST(uint8_t, ((uint8_t *)&t299 + 0), ((uint8_t)58ULL));
                                                                                {
                                                                                    {
                                                                                        uint8_t t300 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
                                                                                        uint8_t t301 = (uint8_t)(t300 == ((uint8_t)3ULL));
                                                                                        if (!t301) {
                                                                                            goto gm_lb59;
                                                                                        }
                                                                                        gm_l42 = ((uint8_t)1);
                                                                                        goto gm_lb58;
                                                                                    }
gm_lb59:;
                                                                                    {
                                                                                        gm_l42 = ((uint8_t)0);
                                                                                        goto gm_lb58;
                                                                                    }
gm_lb60:;
                                                                                    gm_trap("no match arm matched the scrutinee", gm_src8kcterbt0p7b, 205, 38);
                                                                                }
gm_lb58:;
                                                                                uint8_t t302 = gm_l42;
                                                                                if (!t302) {
                                                                                    goto gm_lb57;
                                                                                }
                                                                                uint64_t t303 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                                                                                uint8_t t304 = (t303 > ((uint64_t)0ULL));
                                                                                if (t304) {
                                                                                    void * t305 = GM_LD(void *, ((uint8_t *)&F.l2));
                                                                                    uint64_t t306 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                                                                                    uint64_t t307 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                                                                                    uint64_t r308; bool o309 = __builtin_sub_overflow(t307, ((uint64_t)1ULL), &r308);
                                                                                    if (o309) gm_trap("integer overflow in `-`", gm_src8kcterbt0p7b, 206, 66);
                                                                                    uint64_t t310 = r308;
                                                                                    if ((uintptr_t)t310 >= (uintptr_t)(t306)) gm_trap_index((uintptr_t)t310, t306, gm_src8kcterbt0p7b, 206, 54);
                                                                                    uint64_t t311 = GM_LD(uint64_t, ((((uint8_t *)t305) + (uintptr_t)t310 * 40) + 8));
                                                                                    uint64_t t312 = gm_l12;
                                                                                    uint8_t t313 = ((t311 == t312));
                                                                                    gm_l43 = t313;
                                                                                } else {
                                                                                    gm_l43 = ((uint8_t)0);
                                                                                }
                                                                                uint8_t t314 = gm_l43;
                                                                                gm_l14 = t314;
                                                                                void * t315 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                                                                uint64_t t316 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                                                                uint64_t t317 = gm_l10;
                                                                                uint64_t r318; bool o319 = __builtin_add_overflow(t317, ((uint64_t)1ULL), &r318);
                                                                                if (o319) gm_trap("integer overflow in `+`", gm_src8kcterbt0p7b, 207, 41);
                                                                                uint64_t t320 = r318;
                                                                                if ((uintptr_t)t320 >= (uintptr_t)(t316)) gm_trap_index((uintptr_t)t320, t316, gm_src8kcterbt0p7b, 207, 36);
                                                                                uint32_t t321 = GM_LD(uint32_t, (((((uint8_t *)t315) + (uintptr_t)t320 * 48) + 32) + 4));
                                                                                uint64_t t322 = (uint64_t)t321;
                                                                                uint64_t t323 = gm_l13;
                                                                                uint8_t t324 = ((t322 == t323));
                                                                                gm_l15 = t324;
                                                                                goto gm_lb56;
                                                                            }
gm_lb57:;
                                                                        }
gm_lb56:;
                                                                        gm_l3 = ((uint8_t)0);
                                                                        goto gm_lb23;
                                                                    }
gm_lb46:;
                                                                }
                                                                {
                                                                    {
                                                                        uint8_t t325 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                                                                        uint8_t t326 = (uint8_t)(t325 == ((uint8_t)56ULL));
                                                                        if (!t326) {
                                                                            goto gm_lb61;
                                                                        }
                                                                        gm_b16_8 t327 = gm_l1;
                                                                        uint64_t t328 = gm_l10;
                                                                        uint8_t t329 = gm_f_is_label_colon_rkt3ka8(t327, t328);
                                                                        if (t329) {
                                                                            gm_l14 = ((uint8_t)1);
                                                                            gm_l3 = ((uint8_t)0);
                                                                        } else {
                                                                            uint64_t t330 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
                                                                            uint8_t t331 = (t330 > ((uint64_t)0ULL));
                                                                            if (t331) {
                                                                                void * t332 = GM_LD(void *, ((uint8_t *)&F.l5));
                                                                                uint64_t t333 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
                                                                                uint64_t t334 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
                                                                                uint64_t r335; bool o336 = __builtin_sub_overflow(t334, ((uint64_t)1ULL), &r335);
                                                                                if (o336) gm_trap("integer overflow in `-`", gm_src8kcterbt0p7b, 217, 70);
                                                                                uint64_t t337 = r335;
                                                                                if ((uintptr_t)t337 >= (uintptr_t)(t333)) gm_trap_index((uintptr_t)t337, t333, gm_src8kcterbt0p7b, 217, 58);
                                                                                uint8_t t338 = GM_LD(uint8_t, ((((uint8_t *)t332) + (uintptr_t)t337 * 16) + 10));
                                                                                gm_l29 = t338;
                                                                            } else {
                                                                                uint8_t t339 = gm_l6;
                                                                                gm_l29 = t339;
                                                                            }
                                                                            uint8_t t340 = gm_l29;
                                                                            uint8_t t341 = (uint8_t)!t340;
                                                                            gm_l14 = t341;
                                                                            gm_l15 = ((uint8_t)0);
                                                                            uint64_t t342 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
                                                                            uint8_t t343 = (t342 > ((uint64_t)0ULL));
                                                                            if (t343) {
                                                                                void * t344 = GM_LD(void *, ((uint8_t *)&F.l5));
                                                                                uint64_t t345 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
                                                                                uint64_t t346 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
                                                                                uint64_t r347; bool o348 = __builtin_sub_overflow(t346, ((uint64_t)1ULL), &r347);
                                                                                if (o348) gm_trap("integer overflow in `-`", gm_src8kcterbt0p7b, 220, 62);
                                                                                uint64_t t349 = r347;
                                                                                if ((uintptr_t)t349 >= (uintptr_t)(t345)) gm_trap_index((uintptr_t)t349, t345, gm_src8kcterbt0p7b, 220, 50);
                                                                                GM_ST(uint8_t, ((((uint8_t *)t344) + (uintptr_t)t349 * 16) + 10), ((uint8_t)1));
                                                                            } else {
                                                                                gm_l6 = ((uint8_t)1);
                                                                            }
                                                                            gm_l3 = ((uint8_t)0);
                                                                        }
                                                                        goto gm_lb23;
                                                                    }
gm_lb61:;
                                                                    {
                                                                        {
                                                                            {
                                                                                uint8_t t350 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                                                                                uint8_t t351 = (uint8_t)(t350 == ((uint8_t)45ULL));
                                                                                if (!t351) {
                                                                                    goto gm_lb64;
                                                                                }
                                                                                goto gm_lb63;
                                                                            }
gm_lb64:;
                                                                            {
                                                                                uint8_t t352 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                                                                                uint8_t t353 = (uint8_t)(t352 == ((uint8_t)47ULL));
                                                                                if (!t353) {
                                                                                    goto gm_lb65;
                                                                                }
                                                                                goto gm_lb63;
                                                                            }
gm_lb65:;
                                                                            goto gm_lb62;
                                                                        }
gm_lb63:;
                                                                        uint8_t t354 = gm_l3;
                                                                        gm_l14 = t354;
                                                                        uint8_t t355 = gm_l3;
                                                                        uint8_t t356 = (uint8_t)!t355;
                                                                        gm_l15 = t356;
                                                                        goto gm_lb23;
                                                                    }
gm_lb62:;
                                                                    {
                                                                        uint8_t t357 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                                                                        uint8_t t358 = (uint8_t)(t357 == ((uint8_t)46ULL));
                                                                        if (!t358) {
                                                                            goto gm_lb66;
                                                                        }
                                                                        uint8_t t359 = gm_l3;
                                                                        uint8_t t360 = (uint8_t)!t359;
                                                                        if (t360) {
                                                                            gm_l15 = ((uint8_t)1);
                                                                        } else {
                                                                            gm_b16_8 t361 = gm_l1;
                                                                            uint64_t t362 = gm_l10;
                                                                            uint64_t r363; bool o364 = __builtin_add_overflow(t362, ((uint64_t)1ULL), &r363);
                                                                            if (o364) gm_trap("integer overflow in `+`", gm_src8kcterbt0p7b, 232, 50);
                                                                            uint64_t t365 = r363;
                                                                            uint8_t t366 = gm_f_type_follows_2br4pnf(t361, t365);
                                                                            if (t366) {
                                                                                gm_l14 = ((uint8_t)1);
                                                                                gm_l15 = ((uint8_t)1);
                                                                                gm_l3 = ((uint8_t)0);
                                                                            } else {
                                                                                gm_l14 = ((uint8_t)1);
                                                                            }
                                                                        }
                                                                        goto gm_lb23;
                                                                    }
gm_lb66:;
                                                                }
                                                                {
                                                                    {
                                                                        {
                                                                            {
                                                                                uint8_t t367 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                                                                                uint8_t t368 = (uint8_t)(t367 == ((uint8_t)1ULL));
                                                                                if (!t368) {
                                                                                    goto gm_lb69;
                                                                                }
                                                                                goto gm_lb68;
                                                                            }
gm_lb69:;
                                                                            {
                                                                                uint8_t t369 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                                                                                uint8_t t370 = (uint8_t)(t369 == ((uint8_t)7ULL));
                                                                                if (!t370) {
                                                                                    goto gm_lb70;
                                                                                }
                                                                                goto gm_lb68;
                                                                            }
gm_lb70:;
                                                                            {
                                                                                uint8_t t371 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                                                                                uint8_t t372 = (uint8_t)(t371 == ((uint8_t)9ULL));
                                                                                if (!t372) {
                                                                                    goto gm_lb71;
                                                                                }
                                                                                goto gm_lb68;
                                                                            }
gm_lb71:;
                                                                            goto gm_lb67;
                                                                        }
gm_lb68:;
                                                                        uint8_t t373 = gm_l3;
                                                                        uint8_t t374 = (uint8_t)!t373;
                                                                        gm_l15 = t374;
                                                                        gm_l3 = ((uint8_t)0);
                                                                        goto gm_lb23;
                                                                    }
gm_lb67:;
                                                                    {
                                                                        {
                                                                            {
                                                                                uint8_t t375 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                                                                                uint8_t t376 = (uint8_t)(t375 == ((uint8_t)48ULL));
                                                                                if (!t376) {
                                                                                    goto gm_lb74;
                                                                                }
                                                                                goto gm_lb73;
                                                                            }
gm_lb74:;
                                                                            {
                                                                                uint8_t t377 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                                                                                uint8_t t378 = (uint8_t)(t377 == ((uint8_t)49ULL));
                                                                                if (!t378) {
                                                                                    goto gm_lb75;
                                                                                }
                                                                                goto gm_lb73;
                                                                            }
gm_lb75:;
                                                                            goto gm_lb72;
                                                                        }
gm_lb73:;
                                                                        gm_l3 = ((uint8_t)0);
                                                                        goto gm_lb23;
                                                                    }
gm_lb72:;
                                                                    {
                                                                        uint8_t t379 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                                                                        uint8_t t380 = (uint8_t)(t379 == ((uint8_t)53ULL));
                                                                        if (!t380) {
                                                                            goto gm_lb76;
                                                                        }
                                                                        gm_l3 = ((uint8_t)1);
                                                                        goto gm_lb23;
                                                                    }
gm_lb76:;
                                                                    {
                                                                        {
                                                                            {
                                                                                uint8_t t381 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                                                                                uint8_t t382 = (uint8_t)(t381 == ((uint8_t)67ULL));
                                                                                if (!t382) {
                                                                                    goto gm_lb79;
                                                                                }
                                                                                goto gm_lb78;
                                                                            }
gm_lb79:;
                                                                            {
                                                                                uint8_t t383 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                                                                                uint8_t t384 = (uint8_t)(t383 == ((uint8_t)68ULL));
                                                                                if (!t384) {
                                                                                    goto gm_lb80;
                                                                                }
                                                                                goto gm_lb78;
                                                                            }
gm_lb80:;
                                                                            goto gm_lb77;
                                                                        }
gm_lb78:;
                                                                        void * t385 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                                                        uint64_t t386 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                                                        uint64_t t387 = gm_l12;
                                                                        uint64_t t388 = gm_l13;
                                                                        uint64_t t389 = (uintptr_t)t387;
                                                                        uint64_t t390 = (uintptr_t)t388;
                                                                        if (t389 > t390 || t390 > (uintptr_t)t386) gm_trap_slice(t389, t390, t386, gm_src8kcterbt0p7b, 248, 29);
                                                                        uint64_t t391 = ((uintptr_t)t388) - (uintptr_t)t387;
                                                                        gm_b16_8 t392;
                                                                        GM_ST(void *, (uint8_t *)&t392, (void *)(((uint8_t *)t385 + (uintptr_t)t387 * 1)));
                                                                        GM_ST(uintptr_t, (uint8_t *)&t392 + 8, (uintptr_t)(t391));
                                                                        gm_l30 = t392;
                                                                        {
                                                                            {
                                                                                gm_b16_8 t393 = gm_l30;
                                                                                gm_b16_8 t394;
                                                                                GM_ST(void *, (uint8_t *)&t394, (void *)(gm_sm7emfdvcb7c2));
                                                                                GM_ST(uintptr_t, (uint8_t *)&t394 + 8, (uintptr_t)(2));
                                                                                uint8_t t395 = gm_f_contains_gkki4kq(t393, t394);
                                                                                uint8_t t396 = (uint8_t)!t395;
                                                                                if (!t396) {
                                                                                    goto gm_lb82;
                                                                                }
                                                                                gm_b16_8 t397 = gm_l30;
                                                                                gm_tf5srh4mp4b3i t398 = gm_f_index_of_hdeg351(t397, ((uint8_t)10ULL));
                                                                                gm_l45 = t398;
                                                                                {
                                                                                    {
                                                                                        uint8_t t399 = (uint8_t)((((uint8_t *)&gm_l45))[8] != 0);
                                                                                        if (t399) {
                                                                                            goto gm_lb84;
                                                                                        }
                                                                                        gm_l44 = ((uint8_t)1);
                                                                                        goto gm_lb83;
                                                                                    }
gm_lb84:;
                                                                                    {
                                                                                        gm_l44 = ((uint8_t)0);
                                                                                        goto gm_lb83;
                                                                                    }
gm_lb85:;
                                                                                    gm_trap("no match arm matched the scrutinee", gm_src8kcterbt0p7b, 249, 57);
                                                                                }
gm_lb83:;
                                                                                uint8_t t400 = gm_l44;
                                                                                if (!t400) {
                                                                                    goto gm_lb82;
                                                                                }
                                                                                gm_t9qkh0rsooc8d t401 = gm_l24;
                                                                                gm_b16_8 t402 = gm_f_punct_text_ov4r40u(t401);
                                                                                gm_l19 = t402;
                                                                                goto gm_lb81;
                                                                            }
gm_lb82:;
                                                                        }
gm_lb81:;
                                                                        gm_l3 = ((uint8_t)0);
                                                                        goto gm_lb23;
                                                                    }
gm_lb77:;
                                                                }
                                                                {
                                                                    {
                                                                        gm_l3 = ((uint8_t)0);
                                                                        goto gm_lb23;
                                                                    }
gm_lb86:;
                                                                    gm_trap("no match arm matched the scrutinee", gm_src8kcterbt0p7b, 147, 26);
                                                                }
                                                            }
gm_lb23:;
                                                        }
                                                        {
                                                            goto gm_lb9;
                                                        }
                                                    }
gm_lb22:;
                                                }
                                                {
                                                    gm_trap("no match arm matched the scrutinee", gm_src8kcterbt0p7b, 132, 9);
                                                }
                                            }
gm_lb9:;
                                        }
                                        {
                                            uint64_t t403 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                                            uint8_t t404 = ((t403 == ((uint64_t)0ULL)));
                                            if (t404) {
                                                void * t405 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                                uint64_t t406 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                                uint64_t t407 = gm_l12;
                                                uint64_t t408 = gm_l13;
                                                uint64_t t409 = (uintptr_t)t407;
                                                uint64_t t410 = (uintptr_t)t408;
                                                if (t409 > t410 || t410 > (uintptr_t)t406) gm_trap_slice(t409, t410, t406, gm_src8kcterbt0p7b, 255, 33);
                                                uint64_t t411 = ((uintptr_t)t408) - (uintptr_t)t407;
                                                gm_b16_8 t412;
                                                GM_ST(void *, (uint8_t *)&t412, (void *)(((uint8_t *)t405 + (uintptr_t)t407 * 1)));
                                                GM_ST(uintptr_t, (uint8_t *)&t412 + 8, (uintptr_t)(t411));
                                                gm_l19 = t412;
                                            }
                                            void * t413 = (void *)((uint8_t *)&F.l2);
                                            uint64_t t414 = gm_l12;
                                            uint64_t t415 = gm_l13;
                                            gm_b16_8 t416 = gm_l19;
                                            uint8_t t417 = gm_l14;
                                            uint8_t t418 = gm_l15;
                                            uint8_t t419 = gm_l16;
                                            uint8_t t420 = gm_l17;
                                            uint8_t t421 = gm_l18;
                                            gm_t88jm1dgsmkjs t422;
                                            GM_ST(uint8_t, (uint8_t *)&t422 + 37, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t422 + 38, 0);
                                            GM_ST(uint64_t, ((uint8_t *)&t422 + 0), t414);
                                            GM_ST(uint64_t, ((uint8_t *)&t422 + 8), t415);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t422 + 16), t416);
                                            GM_ST(uint8_t, ((uint8_t *)&t422 + 32), t417);
                                            GM_ST(uint8_t, ((uint8_t *)&t422 + 33), t418);
                                            GM_ST(uint8_t, ((uint8_t *)&t422 + 34), t419);
                                            GM_ST(uint8_t, ((uint8_t *)&t422 + 35), t420);
                                            GM_ST(uint8_t, ((uint8_t *)&t422 + 36), t421);
                                            gm_tftb1odc739t5 t423 = gm_f_push_n6hbl90(t413, &t422);
                                            gm_l46 = t423;
                                            uint8_t t424 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
                                            if (!t424) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l46) + 1, gm_src8kcterbt0p7b, 256, 9);
                                            uint64_t t425 = gm_l10;
                                            gm_tf5srh4mp4b3i t426;
                                            GM_ST(uint8_t, (uint8_t *)&t426 + 9, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t426 + 10, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t426 + 12, 0);
                                            GM_ST(uint64_t, ((uint8_t *)&t426), t425);
                                            ((uint8_t *)&t426)[8] = 1;
                                            gm_l4 = t426;
                                            goto gm_lb5;
                                        }
                                    }
gm_lb6:;
                                }
                                {
                                    gm_trap("no match arm matched the scrutinee", gm_src8kcterbt0p7b, 121, 5);
                                }
                            }
gm_lb5:;
                        }
gm_lb4:;
                    }
                    {
                        uint64_t t427 = gm_l9;
                        uint64_t r428; bool o429 = __builtin_add_overflow(t427, ((uint64_t)1ULL), &r428);
                        uint64_t t430 = r428;
                        gm_l9 = t430;
                    }
                }
            }
gm_lb2:;
        }
        {
            memmove(&F.ret, ((uint8_t *)&F.l2), 40);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
            goto gm_ld88_2;
        }
    }
    {
        void * t431 = (void *)((uint8_t *)&F.l7);
        gm_f_deinit_d5p9g61(t431);
    }
    {
        void * t432 = (void *)((uint8_t *)&F.l5);
        gm_f_deinit_00kfno3(t432);
    }
    goto gm_ls433;
gm_ld88_2:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t434 = (void *)((uint8_t *)&F.l7);
        gm_f_deinit_d5p9g61(t434);
        gm_unw = gm_saved;
    }
gm_ld88_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t435 = (void *)((uint8_t *)&F.l5);
        gm_f_deinit_00kfno3(t435);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls433:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_label_colon_rkt3ka8(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_label_colon_rkt3ka8 F;
    gm_b16_8 gm_l0; /* toks */
    uint64_t gm_l1; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    gm_t3fcbuehh3bs2 gm_l3;
    gm_b16_8 t9 = gm_l0;
    uint64_t t10 = gm_l1;
    uint64_t r11; bool o12 = __builtin_add_overflow(t10, ((uint64_t)1ULL), &r11);
    if (o12) gm_trap("integer overflow in `+`", gm_src8kcterbt0p7b, 103, 84);
    uint64_t t13 = r11;
    gm_f_kind_at_lviicpm((void *)&gm_l3, t9, t13);
    {
        {
            {
                {
                    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t15 = (uint8_t)(t14 == ((uint8_t)2ULL));
                    if (!t15) {
                        goto gm_lb4;
                    }
                    uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)&gm_l3) + 8) + 0));
                    uint8_t t17 = (uint8_t)(t16 == ((uint8_t)31ULL));
                    if (!t17) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t19 = (uint8_t)(t18 == ((uint8_t)2ULL));
                    if (!t19) {
                        goto gm_lb5;
                    }
                    uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)&gm_l3) + 8) + 0));
                    uint8_t t21 = (uint8_t)(t20 == ((uint8_t)12ULL));
                    if (!t21) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t23 = (uint8_t)(t22 == ((uint8_t)2ULL));
                    if (!t23) {
                        goto gm_lb6;
                    }
                    uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)&gm_l3) + 8) + 0));
                    uint8_t t25 = (uint8_t)(t24 == ((uint8_t)30ULL));
                    if (!t25) {
                        goto gm_lb6;
                    }
                    goto gm_lb3;
                }
gm_lb6:;
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
gm_lb7:;
        gm_trap("no match arm matched the scrutinee", gm_src8kcterbt0p7b, 103, 64);
    }
gm_lb1:;
    uint8_t t26 = gm_l2;
    F.ret = t26;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_is_punct_hnuckjb(const void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) uint8_t gm_f_is_punct_hnuckjb_c(gm_t3fcbuehh3bs2 a0, gm_t9qkh0rsooc8d a1) { return gm_f_is_punct_hnuckjb(&a0, a1); }
extern __attribute__((aligned(4))) void gm_f_kind_at_lviicpm(void *gm_out, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) gm_t3fcbuehh3bs2 gm_f_kind_at_lviicpm_c(gm_b16_8 a0, uint64_t a1) { gm_t3fcbuehh3bs2 gm_r; gm_f_kind_at_lviicpm(&gm_r, a0, a1); return gm_r; }
extern __attribute__((aligned(4))) void gm_f_reset_colon_m7bd948(void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_type_after_bracke_9p8imef(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_starts_type_after_bracke_9p8imef F;
    gm_t3fcbuehh3bs2 gm_l0; /* next */
    GM_STACK_CHECK_SHALLOW(&F, 58u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t gm_l1;
    {
        {
            {
                {
                    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
                    uint8_t t14 = (uint8_t)(t13 == ((uint8_t)0ULL));
                    if (!t14) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
                    uint8_t t16 = (uint8_t)(t15 == ((uint8_t)1ULL));
                    if (!t16) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
                    uint8_t t18 = (uint8_t)(t17 == ((uint8_t)6ULL));
                    if (!t18) {
                        goto gm_lb6;
                    }
                    goto gm_lb3;
                }
gm_lb6:;
                {
                    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
                    uint8_t t20 = (uint8_t)(t19 == ((uint8_t)2ULL));
                    if (!t20) {
                        goto gm_lb7;
                    }
                    uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
                    uint8_t t22 = (uint8_t)(t21 == ((uint8_t)11ULL));
                    if (!t22) {
                        goto gm_lb7;
                    }
                    goto gm_lb3;
                }
gm_lb7:;
                {
                    uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
                    uint8_t t24 = (uint8_t)(t23 == ((uint8_t)8ULL));
                    if (!t24) {
                        goto gm_lb8;
                    }
                    uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
                    uint8_t t26 = (uint8_t)(t25 == ((uint8_t)45ULL));
                    if (!t26) {
                        goto gm_lb8;
                    }
                    goto gm_lb3;
                }
gm_lb8:;
                {
                    uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
                    uint8_t t28 = (uint8_t)(t27 == ((uint8_t)8ULL));
                    if (!t28) {
                        goto gm_lb9;
                    }
                    uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
                    uint8_t t30 = (uint8_t)(t29 == ((uint8_t)47ULL));
                    if (!t30) {
                        goto gm_lb9;
                    }
                    goto gm_lb3;
                }
gm_lb9:;
                {
                    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
                    uint8_t t32 = (uint8_t)(t31 == ((uint8_t)8ULL));
                    if (!t32) {
                        goto gm_lb10;
                    }
                    uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
                    uint8_t t34 = (uint8_t)(t33 == ((uint8_t)63ULL));
                    if (!t34) {
                        goto gm_lb10;
                    }
                    goto gm_lb3;
                }
gm_lb10:;
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
gm_lb11:;
        gm_trap("no match arm matched the scrutinee", gm_src8kcterbt0p7b, 78, 61);
    }
gm_lb1:;
    uint8_t t35 = gm_l1;
    F.ret = t35;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_starts_type_after_bracke_9p8imef_c(gm_t3fcbuehh3bs2 a0) { return gm_f_starts_type_after_bracke_9p8imef(&a0); }
inline __attribute__((aligned(4))) uint8_t gm_f_tight_paren_keyword_rj4a6k9(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_tight_paren_keyword_rj4a6k9 F;
    gm_t9qkh0rsooc8d gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 22u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9qkh0rsooc8d t2 = gm_l0;
    gm_t9qkh0rsooc8d t3;
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)11ULL));
    gm_t9qkh0rsooc8d t4;
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)24ULL));
    gm_t9qkh0rsooc8d t5;
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)27ULL));
    gm_t9qkh0rsooc8d t6;
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)6ULL));
    gm_t9qkh0rsooc8d t7;
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)4ULL));
    gm_t9qkh0rsooc8d t8;
    GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)9ULL));
    gm_t9qkh0rsooc8d t9;
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)14ULL));
    gm_t9qkh0rsooc8d t10;
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)21ULL));
    gm_t8doqk5beq9nl t11;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t11 + 0), t3);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t11 + 1), t4);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t11 + 2), t5);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t11 + 3), t6);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t11 + 4), t7);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t11 + 5), t8);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t11 + 6), t9);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t11 + 7), t10);
    uint8_t t12 = 0;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t11) + 0, ((uint8_t *)&t2))) t12 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t11) + 1, ((uint8_t *)&t2))) t12 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t11) + 2, ((uint8_t *)&t2))) t12 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t11) + 3, ((uint8_t *)&t2))) t12 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t11) + 4, ((uint8_t *)&t2))) t12 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t11) + 5, ((uint8_t *)&t2))) t12 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t11) + 6, ((uint8_t *)&t2))) t12 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t11) + 7, ((uint8_t *)&t2))) t12 = 1;
    uint8_t t13 = (uint8_t)(t12);
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_tight_paren_prev_p7h76um(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_tight_paren_prev_p7h76um_c(gm_t3fcbuehh3bs2 a0) { return gm_f_tight_paren_prev_p7h76um(&a0); }
__attribute__((aligned(4))) uint8_t gm_f_type_follows_2br4pnf(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_type_follows_2br4pnf F;
    gm_b16_8 gm_l0; /* toks */
    uint64_t gm_l1; /* start */
    uint64_t gm_l2; /* j */
    gm_t3fcbuehh3bs2 gm_l3; /* k1 */
    gm_t3fcbuehh3bs2 gm_l4; /* k2 */
    GM_STACK_CHECK_SHALLOW(&F, 335u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t3fcbuehh3bs2 gm_l5;
    uint8_t gm_l6;
    uint64_t t23 = gm_l1;
    gm_l2 = t23;
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb1;
            }
            {
                gm_b16_8 t24 = gm_l0;
                uint64_t t25 = gm_l2;
                gm_f_kind_at_lviicpm((void *)&gm_l5, t24, t25);
                {
                    {
                        {
                            {
                                uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                uint8_t t27 = (uint8_t)(t26 == ((uint8_t)0ULL));
                                if (!t27) {
                                    goto gm_lb7;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                uint8_t t29 = (uint8_t)(t28 == ((uint8_t)1ULL));
                                if (!t29) {
                                    goto gm_lb8;
                                }
                                goto gm_lb6;
                            }
gm_lb8:;
                            goto gm_lb5;
                        }
gm_lb6:;
                        F.ret = ((uint8_t)1);
                        goto gm_fret;
                    }
gm_lb5:;
                    {
                        {
                            {
                                uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                uint8_t t31 = (uint8_t)(t30 == ((uint8_t)8ULL));
                                if (!t31) {
                                    goto gm_lb11;
                                }
                                uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)&gm_l5) + 8) + 0));
                                uint8_t t33 = (uint8_t)(t32 == ((uint8_t)45ULL));
                                if (!t33) {
                                    goto gm_lb11;
                                }
                                goto gm_lb10;
                            }
gm_lb11:;
                            {
                                uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                uint8_t t35 = (uint8_t)(t34 == ((uint8_t)8ULL));
                                if (!t35) {
                                    goto gm_lb12;
                                }
                                uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)&gm_l5) + 8) + 0));
                                uint8_t t37 = (uint8_t)(t36 == ((uint8_t)47ULL));
                                if (!t37) {
                                    goto gm_lb12;
                                }
                                goto gm_lb10;
                            }
gm_lb12:;
                            goto gm_lb9;
                        }
gm_lb10:;
                        uint64_t t38 = gm_l2;
                        uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
                        if (o40) gm_trap("integer overflow in `+`", gm_src8kcterbt0p7b, 63, 52);
                        uint64_t t41 = r39;
                        gm_l2 = t41;
                        goto gm_lb4;
                    }
gm_lb9:;
                    {
                        uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        uint8_t t43 = (uint8_t)(t42 == ((uint8_t)8ULL));
                        if (!t43) {
                            goto gm_lb13;
                        }
                        uint8_t t44 = GM_LD(uint8_t, ((((uint8_t *)&gm_l5) + 8) + 0));
                        uint8_t t45 = (uint8_t)(t44 == ((uint8_t)63ULL));
                        if (!t45) {
                            goto gm_lb13;
                        }
                        {
                            gm_b16_8 t46 = gm_l0;
                            uint64_t t47 = gm_l2;
                            uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                            if (o49) gm_trap("integer overflow in `+`", gm_src8kcterbt0p7b, 65, 37);
                            uint64_t t50 = r48;
                            gm_f_kind_at_lviicpm((void *)&gm_l3, t46, t50);
                        }
                        {
                            gm_b16_8 t51 = gm_l0;
                            uint64_t t52 = gm_l2;
                            uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)2ULL), &r53);
                            if (o54) gm_trap("integer overflow in `+`", gm_src8kcterbt0p7b, 66, 37);
                            uint64_t t55 = r53;
                            gm_f_kind_at_lviicpm((void *)&gm_l4, t51, t55);
                        }
                        {
                            gm_t9qkh0rsooc8d t56;
                            GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)64ULL));
                            uint8_t t57 = gm_f_is_punct_hnuckjb(&(*(gm_t3fcbuehh3bs2 *)((uint8_t *)&gm_l3)), t56);
                            if (t57) {
                                F.ret = ((uint8_t)1);
                                goto gm_fret;
                            }
                        }
                        {
                            {
                                {
                                    gm_t9qkh0rsooc8d t58;
                                    GM_ST(uint8_t, ((uint8_t *)&t58 + 0), ((uint8_t)47ULL));
                                    uint8_t t59 = gm_f_is_punct_hnuckjb(&(*(gm_t3fcbuehh3bs2 *)((uint8_t *)&gm_l3)), t58);
                                    if (!t59) {
                                        goto gm_lb15;
                                    }
                                }
                                {
                                    gm_t9qkh0rsooc8d t60;
                                    GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)64ULL));
                                    uint8_t t61 = gm_f_is_punct_hnuckjb(&(*(gm_t3fcbuehh3bs2 *)((uint8_t *)&gm_l4)), t60);
                                    if (!t61) {
                                        goto gm_lb15;
                                    }
                                }
                                F.ret = ((uint8_t)1);
                                goto gm_fret;
                            }
gm_lb15:;
                        }
gm_lb14:;
                        {
                            {
                                {
                                    {
                                        uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                                        uint8_t t63 = (uint8_t)(t62 == ((uint8_t)3ULL));
                                        if (!t63) {
                                            goto gm_lb19;
                                        }
                                        gm_l6 = ((uint8_t)1);
                                        goto gm_lb18;
                                    }
gm_lb19:;
                                    {
                                        gm_l6 = ((uint8_t)0);
                                        goto gm_lb18;
                                    }
gm_lb20:;
                                    gm_trap("no match arm matched the scrutinee", gm_src8kcterbt0p7b, 69, 20);
                                }
gm_lb18:;
                                uint8_t t64 = gm_l6;
                                if (!t64) {
                                    goto gm_lb17;
                                }
                                {
                                    gm_t9qkh0rsooc8d t65;
                                    GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)64ULL));
                                    uint8_t t66 = gm_f_is_punct_hnuckjb(&(*(gm_t3fcbuehh3bs2 *)((uint8_t *)&gm_l4)), t65);
                                    if (!t66) {
                                        goto gm_lb17;
                                    }
                                }
                                {
                                    gm_b16_8 t67 = gm_l0;
                                    uint64_t t68 = gm_l2;
                                    uint64_t r69; bool o70 = __builtin_add_overflow(t68, ((uint64_t)3ULL), &r69);
                                    if (o70) gm_trap("integer overflow in `+`", gm_src8kcterbt0p7b, 69, 111);
                                    uint64_t t71 = r69;
                                    gm_t3fcbuehh3bs2 t72;
                                    gm_f_kind_at_lviicpm((void *)&t72, t67, t71);
                                    uint8_t t73 = gm_f_starts_type_after_bracke_9p8imef(&t72);
                                    F.ret = t73;
                                    goto gm_fret;
                                }
                            }
gm_lb17:;
                        }
gm_lb16:;
                        F.ret = ((uint8_t)0);
                        goto gm_fret;
                    }
gm_lb13:;
                    {
                        F.ret = ((uint8_t)0);
                        goto gm_fret;
                    }
gm_lb21:;
                    gm_trap("no match arm matched the scrutinee", gm_src8kcterbt0p7b, 61, 9);
                }
gm_lb4:;
            }
gm_lb3:;
        }
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}

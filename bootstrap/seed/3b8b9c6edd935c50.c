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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { void * f0; uint32_t f1; uint8_t p2[4]; void * f2; } gm_tfim8ting9n8a;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { int64_t f0; } gm_tm98ap4112hee;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t a[4]; } gm_tpvg3mjn3dp06;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; int32_t v1; } f1; } gm_tsoiri44d0i6q;
_Static_assert(sizeof(gm_csfq64m1m1rt6m) == 8 && _Alignof(gm_csfq64m1m1rt6m) == 8 && offsetof(gm_csfq64m1m1rt6m, f0) == 0, "layout of `Dir`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_tm98ap4112hee f0; gm_b16_8 f1; } gm_t3olsle24rveu;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_tbl8il92l97ut f0; uint8_t f1; uint8_t pe[7]; } gm_t5lf9i3n1r3co;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tm98ap4112hee v1; } f1; } gm_t62ebbi2i5rkd;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `(e: ^const Expr, l: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
_Static_assert(sizeof(gm_tfim8ting9n8a) == 24 && _Alignof(gm_tfim8ting9n8a) == 8 && offsetof(gm_tfim8ting9n8a, f0) == 0 && offsetof(gm_tfim8ting9n8a, f1) == 8 && offsetof(gm_tfim8ting9n8a, f2) == 16, "layout of `(fr: ^Frame, cx: u32, code: ^Code)`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; uint64_t v1; } f1; } gm_thf5kr0is9u9i;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { uint64_t f0; gm_tf5srh4mp4b3i f1; } gm_tk4qs2jduhfn8;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_tsoiri44d0i6q v1; } f1; } gm_tlmjd3nndlf3r;
_Static_assert(sizeof(gm_tm98ap4112hee) == 8 && _Alignof(gm_tm98ap4112hee) == 8 && offsetof(gm_tm98ap4112hee, f0) == 0, "layout of `File`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint8_t p3[4]; gm_b16_8 f3; gm_tl874t2atnlh3 f4; uint8_t pe[4]; } gm_tnsjse4su5jue;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_b16_8 f2; uint32_t f3; gm_tl874t2atnlh3 f4; } gm_tonn1ba3fjh5g;
typedef struct { gm_b16_8 a[1]; } gm_tpmdr2hmdhsqr;
typedef struct { gm_tm98ap4112hee f0; uint8_t f1; uint8_t pe[7]; } gm_tppo2hmut88dq;
_Static_assert(sizeof(gm_tpvg3mjn3dp06) == 4 && _Alignof(gm_tpvg3mjn3dp06) == 1 && offsetof(gm_tpvg3mjn3dp06, a) == 0, "layout of `[4]u8`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tsoiri44d0i6q) == 8 && _Alignof(gm_tsoiri44d0i6q) == 4 && offsetof(gm_tsoiri44d0i6q, f0) == 0 && offsetof(gm_tsoiri44d0i6q, f1) == 4, "layout of `ExitStatus`");
typedef struct { gm_b16_8 f0; uint16_t f1; uint8_t pe[6]; } gm_tvaas22dl64e0;
typedef struct { gm_t76123gfet73g f0; uint8_t f1; uint8_t pe[7]; } gm_t07lf02ct4q61;
_Static_assert(sizeof(gm_t3olsle24rveu) == 24 && _Alignof(gm_t3olsle24rveu) == 8 && offsetof(gm_t3olsle24rveu, f0) == 0 && offsetof(gm_t3olsle24rveu, f1) == 8, "layout of `(child: Child, data: string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t5lf9i3n1r3co) == 24 && _Alignof(gm_t5lf9i3n1r3co) == 8 && offsetof(gm_t5lf9i3n1r3co, f0) == 0 && offsetof(gm_t5lf9i3n1r3co, f1) == 16, "layout of `?(e: ^const Expr, l: u32)`");
_Static_assert(sizeof(gm_t62ebbi2i5rkd) == 16 && _Alignof(gm_t62ebbi2i5rkd) == 8 && offsetof(gm_t62ebbi2i5rkd, f0) == 0 && offsetof(gm_t62ebbi2i5rkd, f1) == 8, "layout of `IoError!File`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
typedef struct { gm_thf5kr0is9u9i f0; gm_tppo2hmut88dq f1; int64_t f2; } gm_t7eae7gj5k2n2;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tfim8ting9n8a v0; gm_tonn1ba3fjh5g v1; gm_t79rbdv4rafe5 v2; } f1; } gm_t8dl9beiqk70v;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_tk4qs2jduhfn8 v1; uint64_t v2; void * v3; } f1; } gm_tdkjirf1g83ud;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_thf5kr0is9u9i) == 16 && _Alignof(gm_thf5kr0is9u9i) == 8 && offsetof(gm_thf5kr0is9u9i, f0) == 0 && offsetof(gm_thf5kr0is9u9i, f1) == 8, "layout of `IoError!usize`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tk4qs2jduhfn8) == 24 && _Alignof(gm_tk4qs2jduhfn8) == 8 && offsetof(gm_tk4qs2jduhfn8, f0) == 0 && offsetof(gm_tk4qs2jduhfn8, f1) == 8, "layout of `(seen: u64, deadline: ?u64)`");
_Static_assert(sizeof(gm_tlmjd3nndlf3r) == 12 && _Alignof(gm_tlmjd3nndlf3r) == 4 && offsetof(gm_tlmjd3nndlf3r, f0) == 0 && offsetof(gm_tlmjd3nndlf3r, f1) == 4, "layout of `IoError!ExitStatus`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t3olsle24rveu v1; gm_tvaas22dl64e0 v2; gm_tm98ap4112hee v3; gm_t3olsle24rveu v4; gm_t3olsle24rveu v5; gm_t3olsle24rveu v6; gm_t3olsle24rveu v7; gm_tm98ap4112hee v8; } f1; } gm_tnp3hpcm9uhoi;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tnsjse4su5jue) == 48 && _Alignof(gm_tnsjse4su5jue) == 8 && offsetof(gm_tnsjse4su5jue, f0) == 0 && offsetof(gm_tnsjse4su5jue, f1) == 4 && offsetof(gm_tnsjse4su5jue, f2) == 8 && offsetof(gm_tnsjse4su5jue, f3) == 16 && offsetof(gm_tnsjse4su5jue, f4) == 32, "layout of `(fiber: u32, node: u32, op: u32, args: []const []u8, span: Span)`");
typedef struct { gm_trqfvmjtt4sij f0; uint32_t f1; uint32_t f2; uint64_t f3; void * f4; gm_tqs5p4n2tq19v f5; uint8_t pe[6]; } gm_tobnqifq2n0g5;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; gm_trqfvmjtt4sij f2; } gm_tolm6p7jp1hb3;
_Static_assert(sizeof(gm_tonn1ba3fjh5g) == 56 && _Alignof(gm_tonn1ba3fjh5g) == 8 && offsetof(gm_tonn1ba3fjh5g, f0) == 0 && offsetof(gm_tonn1ba3fjh5g, f1) == 16 && offsetof(gm_tonn1ba3fjh5g, f2) == 24 && offsetof(gm_tonn1ba3fjh5g, f3) == 40 && offsetof(gm_tonn1ba3fjh5g, f4) == 44, "layout of `(f: []u8, fty: Ty, arg: []u8, cx: u32, span: Span)`");
_Static_assert(sizeof(gm_tpmdr2hmdhsqr) == 16 && _Alignof(gm_tpmdr2hmdhsqr) == 8 && offsetof(gm_tpmdr2hmdhsqr, a) == 0, "layout of `[1]string`");
_Static_assert(sizeof(gm_tppo2hmut88dq) == 16 && _Alignof(gm_tppo2hmut88dq) == 8 && offsetof(gm_tppo2hmut88dq, f0) == 0 && offsetof(gm_tppo2hmut88dq, f1) == 8, "layout of `?Child`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tvaas22dl64e0) == 24 && _Alignof(gm_tvaas22dl64e0) == 8 && offsetof(gm_tvaas22dl64e0, f0) == 0 && offsetof(gm_tvaas22dl64e0, f1) == 16, "layout of `(string, u16)`");
_Static_assert(sizeof(gm_t07lf02ct4q61) == 32 && _Alignof(gm_t07lf02ct4q61) == 8 && offsetof(gm_t07lf02ct4q61, f0) == 0 && offsetof(gm_t07lf02ct4q61, f1) == 24, "layout of `?Flow`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_t7eae7gj5k2n2 v1; } f1; } gm_t0jbubompi7qd;
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { gm_tnp3hpcm9uhoi f0; gm_t7eae7gj5k2n2 f1; uint32_t f2; uint8_t pe[4]; } gm_t6or663cm29rr;
_Static_assert(sizeof(gm_t7eae7gj5k2n2) == 40 && _Alignof(gm_t7eae7gj5k2n2) == 8 && offsetof(gm_t7eae7gj5k2n2, f0) == 0 && offsetof(gm_t7eae7gj5k2n2, f1) == 16 && offsetof(gm_t7eae7gj5k2n2, f2) == 32, "layout of `HostResult`");
_Static_assert(sizeof(gm_t8dl9beiqk70v) == 64 && _Alignof(gm_t8dl9beiqk70v) == 8 && offsetof(gm_t8dl9beiqk70v, f0) == 0 && offsetof(gm_t8dl9beiqk70v, f1) == 8, "layout of `Start`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_tdkjirf1g83ud f1; uint8_t f2; uint8_t pe[7]; } gm_t9jnd88rq7ul8;
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdkjirf1g83ud) == 32 && _Alignof(gm_tdkjirf1g83ud) == 8 && offsetof(gm_tdkjirf1g83ud, f0) == 0 && offsetof(gm_tdkjirf1g83ud, f1) == 8, "layout of `ThreadState`");
_Static_assert(sizeof(gm_tnp3hpcm9uhoi) == 32 && _Alignof(gm_tnp3hpcm9uhoi) == 8 && offsetof(gm_tnp3hpcm9uhoi, f0) == 0 && offsetof(gm_tnp3hpcm9uhoi, f1) == 8, "layout of `HostOp`");
_Static_assert(sizeof(gm_tobnqifq2n0g5) == 72 && _Alignof(gm_tobnqifq2n0g5) == 8 && offsetof(gm_tobnqifq2n0g5, f0) == 0 && offsetof(gm_tobnqifq2n0g5, f1) == 40 && offsetof(gm_tobnqifq2n0g5, f2) == 44 && offsetof(gm_tobnqifq2n0g5, f3) == 48 && offsetof(gm_tobnqifq2n0g5, f4) == 56 && offsetof(gm_tobnqifq2n0g5, f5) == 64, "layout of `Threads`");
_Static_assert(sizeof(gm_tolm6p7jp1hb3) == 72 && _Alignof(gm_tolm6p7jp1hb3) == 8 && offsetof(gm_tolm6p7jp1hb3, f0) == 0 && offsetof(gm_tolm6p7jp1hb3, f1) == 16 && offsetof(gm_tolm6p7jp1hb3, f2) == 32, "layout of `EvalTrap`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t07lf02ct4q61 f1; } gm_tslj4ghflekdk;
_Static_assert(sizeof(gm_t0jbubompi7qd) == 48 && _Alignof(gm_t0jbubompi7qd) == 8 && offsetof(gm_t0jbubompi7qd, f0) == 0 && offsetof(gm_t0jbubompi7qd, f1) == 8, "layout of `Flow!HostResult`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t6or663cm29rr) == 80 && _Alignof(gm_t6or663cm29rr) == 8 && offsetof(gm_t6or663cm29rr, f0) == 0 && offsetof(gm_t6or663cm29rr, f1) == 32 && offsetof(gm_t6or663cm29rr, f2) == 72, "layout of `HostCall`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tnsjse4su5jue v1; gm_tslj4ghflekdk v2; } f1; } gm_t9hl37efuvhj8;
_Static_assert(sizeof(gm_t9jnd88rq7ul8) == 48 && _Alignof(gm_t9jnd88rq7ul8) == 8 && offsetof(gm_t9jnd88rq7ul8, f0) == 0 && offsetof(gm_t9jnd88rq7ul8, f1) == 8 && offsetof(gm_t9jnd88rq7ul8, f2) == 40, "layout of `ThreadRec`");
_Static_assert(sizeof(gm_tslj4ghflekdk) == 40 && _Alignof(gm_tslj4ghflekdk) == 8 && offsetof(gm_tslj4ghflekdk, f0) == 0 && offsetof(gm_tslj4ghflekdk, f1) == 8, "layout of `(fiber: u32, flow: ?Flow)`");
_Static_assert(sizeof(gm_t9hl37efuvhj8) == 56 && _Alignof(gm_t9hl37efuvhj8) == 8 && offsetof(gm_t9hl37efuvhj8, f0) == 0 && offsetof(gm_t9hl37efuvhj8, f1) == 8, "layout of `Msg`");
struct gm_fr_check_fatal_dgfalcm {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_connected_a7ps01v {
    gm_frame h;
    gm_t7eae7gj5k2n2 ret;
};
struct gm_fr_copy_from_f7j6p05 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_5j6fqpv {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8othtri_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_env_exit_prbvpjt {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_fill_abmf5g5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_04kmcqn {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_uh8vg1k {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_func_ene90ss {
    gm_frame h;
    void * ret;
};
struct gm_fr_get_u64_tgpb2vh {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_host_main_55puiu2 {
    gm_frame h;
    void * l0; /* j */
    gm_b0_1 ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_join_r8j2dp1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_list_j8jr9cr_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_jh4e1ji {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_new_coro_06nq12a {
    gm_frame h;
    gm_t8dl9beiqk70v l1; /* start */
    uint32_t ret;
};
struct gm_fr_on_host_thread_kmegc07 {
    gm_frame h;
    gm_t7eae7gj5k2n2 l7; /* result */
    gm_t0jbubompi7qd ret;
};
struct gm_fr_ptr_of_b77b4c9 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_push_bn7ud1g {
    gm_frame h;
    gm_t9jnd88rq7ul8 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_rd_word_8flcq6o {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_regrow_l875bgq {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_runnable_o1q905g {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_sleep_gqtie06 {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_switch_thread_m7aoqko {
    gm_frame h;
    gm_t9jnd88rq7ul8 l8; /* t */
    gm_t9jnd88rq7ul8 l18; /* t */
    gm_t9jnd88rq7ul8 l25; /* t */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_sys_count_50c5lcp {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_thread_main_n6r943c {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_thread_op_pfli7b1 {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_threads_kh5dldj {
    gm_frame h;
    gm_trqfvmjtt4sij l0; /* l */
    gm_tobnqifq2n0g5 ret;
};
struct gm_fr_tick_6bsfu3n {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_tmp_8k6u9c9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
__attribute__((aligned(4))) uint32_t gm_f_add_coro_k13bkpe(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint64_t a2, uint32_t a3);
__attribute__((aligned(4))) void gm_f_call_function_u7ovth6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint32_t a2, gm_b16_8 a3, void * a4, uint32_t a5, const void * a6, uint8_t a7, gm_b16_8 a8);
__attribute__((aligned(4))) void gm_f_check_fatal_dgfalcm(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tcfa6l7reoosn gm_f_check_fatal_dgfalcm_c(void * a0);
__attribute__((aligned(4))) void gm_f_connected_a7ps01v(void *gm_out, gm_t62ebbi2i5rkd a0);
__attribute__((aligned(4))) gm_t7eae7gj5k2n2 gm_f_connected_a7ps01v_c(gm_t62ebbi2i5rkd a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_defaults_p077jgf(gm_closure a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_destroy_o4n4nl0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5j6fqpv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8othtri_2(void);
__attribute__((aligned(4))) void gm_f_env_exit_prbvpjt(void *gm_out, void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tcfa6l7reoosn gm_f_env_exit_prbvpjt_c(void * a0, uint8_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_04kmcqn(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_fn_from_addr_7a6bpqd(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_format_n3u30k9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_uh8vg1k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_free_rkuh2rm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) void * gm_f_func_ene90ss(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void * gm_f_func_in17r5n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_int_pg7p4gc(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_handoff_mtsh8r2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2);
inline __attribute__((aligned(4))) void gm_f_host_main_55puiu2(void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_join_r8j2dp1(gm_csfq64m1m1rt6m a0);
inline __attribute__((aligned(4))) void gm_f_list_j8jr9cr_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_jh4e1ji(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_load_at_6u81jcc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2, uint64_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_make_2ip78bj(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, const void * a1);
inline __attribute__((aligned(4))) uint32_t gm_f_new_coro_06nq12a(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint32_t a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_g0l0iur(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_jd11mvt(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_on_host_thread_kmegc07(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
__attribute__((aligned(4))) int64_t gm_f_proc_read_kc2l8gi(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a0, gm_b16_8 a1);
__attribute__((aligned(4))) int64_t gm_f_proc_status_7nq4d17(gm_tlmjd3nndlf3r a0);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bn7ud1g(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_l875bgq(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) uint8_t gm_f_runnable_o1q905g(void * a0, void * a1, gm_tf5srh4mp4b3i a2);
__attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_sleep_gqtie06(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2);
__attribute__((aligned(4))) gm_t62ebbi2i5rkd gm_f_spawn_2tmnfpi(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_switch_thread_m7aoqko(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) int64_t gm_f_sys_code_cjtlfo1(gm_toe5fmr3vvf4d a0);
inline __attribute__((aligned(4))) int64_t gm_f_sys_count_50c5lcp(gm_thf5kr0is9u9i a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_thread_main_n6r943c(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_thread_op_pfli7b1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_threads_kh5dldj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_tick_6bsfu3n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b16_8 gm_f_tmp_next_dalgo1f(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
extern uint8_t gm_cusfosftpfn[4] __attribute__((aligned(4)));
extern uint8_t gm_k3501um71nb[40] __attribute__((aligned(8)));
extern uint8_t gm_k6kfar4qbkb[40] __attribute__((aligned(8)));
extern uint8_t gm_k6qm3ruhsta[40] __attribute__((aligned(8)));
extern uint8_t gm_kf51o7bo59i[40] __attribute__((aligned(8)));
extern uint8_t gm_kh1g3s3qgm7[40] __attribute__((aligned(8)));
extern uint8_t gm_kmp1n1vvhms[40] __attribute__((aligned(8)));
extern const uint8_t gm_s632oftf9nssu[43];
extern const uint8_t gm_sb5pfucassrk8[34];
extern const uint8_t gm_sfjs7cccbf3qf[56];
extern const uint8_t gm_sqofr58g480ri[39];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srchntnl6m6g3am[];
extern const char gm_srcn3jjc71a2bv3[];
extern const char gm_srcnqtpfma6qe4a[];
extern const char gm_srcp0l171g2m7ho[];
extern const char gm_srcpit8mdn0781q[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_svu0pqir618v4[39];
extern const gm_tmpl gm_tplcn6c0fsph4j2;
extern const gm_type gm_tyo86ks9qkvt;
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_copy_from_f7j6p05 F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* src */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t4 = ((t2 == t3));
        if (t4) {
        } else {
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sqofr58g480ri));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(38));
            { gm_buf mb = {0}; size_t ml = GM_LD(uintptr_t, ((uint8_t *)&t5) + 8); const uint8_t *mp = GM_LD(void *, ((uint8_t *)&t5)); if (ml == 16 && memcmp(mp, "assertion failed", 16) == 0) gm_buf_push(&mb, mp, ml); else { gm_buf_push(&mb, "assertion failed: ", 18); gm_buf_push(&mb, mp, ml); } gm_trap_bytes(mb.data, mb.len, gm_srcc24pnihhgkug, 24, 5); }
        }
    }
    gm_b16_8 t6 = gm_l0;
    gm_b16_8 t7 = gm_l1;
    if (GM_LD(uintptr_t, ((uint8_t *)&t6) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t6)), GM_LD(void *, ((uint8_t *)&t7)), GM_LD(uintptr_t, ((uint8_t *)&t6) + 8) * 1);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5j6fqpv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_5j6fqpv F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8othtri_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8othtri_2 F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fill_abmf5g5 F;
    gm_b16_8 gm_l0; /* s */
    uint8_t gm_l1; /* x */
    void * gm_l2; /* p */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t7 = GM_LD(void *, ((uint8_t *)&gm_l0));
    gm_l2 = t7;
    gm_l4 = ((uint64_t)0ULL);
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_l3 = t8;
    {
        for (;;) {
            uint64_t t9 = gm_l4;
            uint64_t t10 = gm_l3;
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t12 = gm_l4;
                        gm_l5 = t12;
                        void * t13 = gm_l2;
                        uint64_t t14 = gm_l5;
                        uint8_t t15 = gm_l1;
                        GM_ST(uint8_t, (((uint8_t *)t13) + (uintptr_t)t14 * 1), t15);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc24pnihhgkug, 18, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t16 = gm_l4;
            uint64_t r17; bool o18 = __builtin_add_overflow(t16, ((uint64_t)1ULL), &r17);
            uint64_t t19 = r17;
            gm_l4 = t19;
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
inline __attribute__((aligned(4))) void gm_f_format_uh8vg1k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_uh8vg1k F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4ce0jd3tdro4 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sfjs7cccbf3qf));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(55));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplcn6c0fsph4j2), t3);
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
inline __attribute__((aligned(4))) void * gm_f_func_ene90ss(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_func_ene90ss F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    uint32_t t4 = gm_l1;
    void * t5 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t3, t4);
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
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_u64_tgpb2vh F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t gm_l2;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t3 = (t2 <= ((uint64_t)8ULL));
    if (t3) {
        void * t4 = gm_l0;
        gm_b16_8 t5 = gm_l1;
        uint64_t t6 = gm_f_rd_word_8flcq6o(t4, t5);
        gm_l2 = t6;
    } else {
        void * t7 = gm_l0;
        gm_b16_8 t8 = gm_l1;
        unsigned __int128 t9 = gm_f_get_uint_bq8c4b7(t7, t8);
        uint64_t t10 = (uint64_t)t9;
        gm_l2 = t10;
    }
    uint64_t t11 = gm_l2;
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_host_main_55puiu2(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_host_main_55puiu2 F;
    GM_STACK_CHECK(&F, 41u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_closure t2 = ((gm_closure){(void *)gm_f_fn_04kmcqn, (void *)&F.h, cx});
    gm_f_defaults_p077jgf(t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t3 = F.l0;
    void * t4 = (void *)(((uint8_t *)t3) + 72);
    gm_t9qkh0rsooc8d t5;
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)2ULL));
    __atomic_store_n(((uint32_t *)t4), ((uint32_t)1ULL), gm_ord[GM_LD(uint8_t, ((uint8_t *)&t5))]);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2) {
    struct gm_fr_int_ci6airg F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* signed */
    uint16_t gm_l2; /* bits */
    GM_STACK_CHECK(&F, 94u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    uint8_t t3 = gm_l1;
    uint16_t t4 = gm_l2;
    gm_tjcj4j2gbh6c8 t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), t3);
    GM_ST(uint16_t, ((uint8_t *)&t5 + 2), t4);
    gm_t53at80pfdus3 t6;
    memset(&t6, 0, sizeof t6);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tjcj4j2gbh6c8, ((uint8_t *)&t6 + 8), t5);
    gm_t88rcucg1b7uk t7 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t2, &t6);
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
inline __attribute__((aligned(4))) void gm_f_join_r8j2dp1(gm_csfq64m1m1rt6m a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_join_r8j2dp1 F;
    gm_tm98ap4112hee gm_l0; /* t */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_tm98ap4112hee, a0);
    int64_t t2 = GM_LD(int64_t, ((uint8_t *)&gm_l0));
    int64_t t3 = gm_thread_join(t2);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_list_j8jr9cr_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_j8jr9cr_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_8othtri_2();
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
inline __attribute__((aligned(4))) void gm_f_list_jh4e1ji(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_jh4e1ji F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_5j6fqpv();
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
inline __attribute__((aligned(4))) uint32_t gm_f_new_coro_06nq12a(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint32_t a2) {
    struct gm_fr_new_coro_06nq12a F;
    void * gm_l0; /* m */
    uint32_t gm_l2; /* thread */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    void * t2 = gm_l0;
    unsigned __int128 n3 = (unsigned __int128)((uint64_t)20ULL);
    if (n3 >= 64) gm_trap_shift((uint64_t)n3, "usize", gm_srcp0l171g2m7ho, 368, 96);
    uint64_t r4 = (uint64_t)((uint64_t)((uint64_t)8ULL) << (int)n3);
    bool o5 = (uint64_t)(r4 >> (int)n3) != ((uint64_t)8ULL);
    if (o5) gm_trap("integer overflow in `<<`", gm_srcp0l171g2m7ho, 368, 96);
    uint64_t t6 = r4;
    uint32_t t7 = gm_l2;
    uint32_t t8 = gm_f_add_coro_k13bkpe((void *)NULL, cx, NULL, t2, &(*(gm_t8dl9beiqk70v *)((uint8_t *)&F.l1)), t6, t7);
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
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_of_b77b4c9 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)&gm_l1));
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 176));
    uint64_t t7 = gm_f_us_cuv01fp(t6);
    uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t9 = (uintptr_t)t7;
    if (t8 > t9 || t9 > (uintptr_t)t4) gm_trap_slice(t8, t9, t4, gm_srcnqtpfma6qe4a, 104, 68);
    uint64_t t10 = ((uintptr_t)t7) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t11;
    GM_ST(void *, (uint8_t *)&t11, (void *)(((uint8_t *)t3 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(t10));
    uint64_t t12 = gm_f_get_u64_tgpb2vh(t2, t11);
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bn7ud1g(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_bn7ud1g F;
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
        gm_b16_8 t9 = gm_f_empty_slice_5j6fqpv();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_l875bgq(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_rd_word_8flcq6o F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)t2) + 89));
    if (t3) {
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t5 = ((t4 == ((uint64_t)8ULL)));
        if (t5) {
            void * t6 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t7)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t7, gm_srcnqtpfma6qe4a, 316, 36);
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)t6) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint64_t t9 = (uint64_t)t8;
            void * t10 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t11, gm_srcnqtpfma6qe4a, 316, 49);
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)t10) + (uintptr_t)((uint64_t)1ULL) * 1));
            uint64_t t13 = (uint64_t)t12;
            unsigned __int128 n14 = (unsigned __int128)((uint64_t)8ULL);
            if (n14 >= 64) gm_trap_shift((uint64_t)n14, "u64", gm_srcnqtpfma6qe4a, 316, 45);
            uint64_t r15 = (uint64_t)((uint64_t)t13 << (int)n14);
            bool o16 = (uint64_t)(r15 >> (int)n14) != t13;
            if (o16) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 45);
            uint64_t t17 = r15;
            uint64_t t18 = (uint64_t)(t9 | t17);
            void * t19 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t20)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t20, gm_srcnqtpfma6qe4a, 316, 68);
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)t19) + (uintptr_t)((uint64_t)2ULL) * 1));
            uint64_t t22 = (uint64_t)t21;
            unsigned __int128 n23 = (unsigned __int128)((uint64_t)16ULL);
            if (n23 >= 64) gm_trap_shift((uint64_t)n23, "u64", gm_srcnqtpfma6qe4a, 316, 64);
            uint64_t r24 = (uint64_t)((uint64_t)t22 << (int)n23);
            bool o25 = (uint64_t)(r24 >> (int)n23) != t22;
            if (o25) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 64);
            uint64_t t26 = r24;
            uint64_t t27 = (uint64_t)(t18 | t26);
            void * t28 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)3ULL) >= (uintptr_t)(t29)) gm_trap_index((uintptr_t)((uint64_t)3ULL), t29, gm_srcnqtpfma6qe4a, 316, 88);
            uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)t28) + (uintptr_t)((uint64_t)3ULL) * 1));
            uint64_t t31 = (uint64_t)t30;
            unsigned __int128 n32 = (unsigned __int128)((uint64_t)24ULL);
            if (n32 >= 64) gm_trap_shift((uint64_t)n32, "u64", gm_srcnqtpfma6qe4a, 316, 84);
            uint64_t r33 = (uint64_t)((uint64_t)t31 << (int)n32);
            bool o34 = (uint64_t)(r33 >> (int)n32) != t31;
            if (o34) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 84);
            uint64_t t35 = r33;
            uint64_t t36 = (uint64_t)(t27 | t35);
            void * t37 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)4ULL) >= (uintptr_t)(t38)) gm_trap_index((uintptr_t)((uint64_t)4ULL), t38, gm_srcnqtpfma6qe4a, 316, 108);
            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)t37) + (uintptr_t)((uint64_t)4ULL) * 1));
            uint64_t t40 = (uint64_t)t39;
            unsigned __int128 n41 = (unsigned __int128)((uint64_t)32ULL);
            if (n41 >= 64) gm_trap_shift((uint64_t)n41, "u64", gm_srcnqtpfma6qe4a, 316, 104);
            uint64_t r42 = (uint64_t)((uint64_t)t40 << (int)n41);
            bool o43 = (uint64_t)(r42 >> (int)n41) != t40;
            if (o43) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 104);
            uint64_t t44 = r42;
            uint64_t t45 = (uint64_t)(t36 | t44);
            void * t46 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)5ULL) >= (uintptr_t)(t47)) gm_trap_index((uintptr_t)((uint64_t)5ULL), t47, gm_srcnqtpfma6qe4a, 316, 128);
            uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)t46) + (uintptr_t)((uint64_t)5ULL) * 1));
            uint64_t t49 = (uint64_t)t48;
            unsigned __int128 n50 = (unsigned __int128)((uint64_t)40ULL);
            if (n50 >= 64) gm_trap_shift((uint64_t)n50, "u64", gm_srcnqtpfma6qe4a, 316, 124);
            uint64_t r51 = (uint64_t)((uint64_t)t49 << (int)n50);
            bool o52 = (uint64_t)(r51 >> (int)n50) != t49;
            if (o52) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 124);
            uint64_t t53 = r51;
            uint64_t t54 = (uint64_t)(t45 | t53);
            void * t55 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)6ULL) >= (uintptr_t)(t56)) gm_trap_index((uintptr_t)((uint64_t)6ULL), t56, gm_srcnqtpfma6qe4a, 316, 148);
            uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)t55) + (uintptr_t)((uint64_t)6ULL) * 1));
            uint64_t t58 = (uint64_t)t57;
            unsigned __int128 n59 = (unsigned __int128)((uint64_t)48ULL);
            if (n59 >= 64) gm_trap_shift((uint64_t)n59, "u64", gm_srcnqtpfma6qe4a, 316, 144);
            uint64_t r60 = (uint64_t)((uint64_t)t58 << (int)n59);
            bool o61 = (uint64_t)(r60 >> (int)n59) != t58;
            if (o61) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 144);
            uint64_t t62 = r60;
            uint64_t t63 = (uint64_t)(t54 | t62);
            void * t64 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)7ULL) >= (uintptr_t)(t65)) gm_trap_index((uintptr_t)((uint64_t)7ULL), t65, gm_srcnqtpfma6qe4a, 316, 168);
            uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)t64) + (uintptr_t)((uint64_t)7ULL) * 1));
            uint64_t t67 = (uint64_t)t66;
            unsigned __int128 n68 = (unsigned __int128)((uint64_t)56ULL);
            if (n68 >= 64) gm_trap_shift((uint64_t)n68, "u64", gm_srcnqtpfma6qe4a, 316, 164);
            uint64_t r69 = (uint64_t)((uint64_t)t67 << (int)n68);
            bool o70 = (uint64_t)(r69 >> (int)n68) != t67;
            if (o70) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 164);
            uint64_t t71 = r69;
            uint64_t t72 = (uint64_t)(t63 | t71);
            F.ret = t72;
            goto gm_fret;
        }
        uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t74 = ((t73 == ((uint64_t)4ULL)));
        if (t74) {
            void * t75 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t76)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t76, gm_srcnqtpfma6qe4a, 317, 36);
            uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)t75) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint64_t t78 = (uint64_t)t77;
            void * t79 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t80)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t80, gm_srcnqtpfma6qe4a, 317, 49);
            uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)t79) + (uintptr_t)((uint64_t)1ULL) * 1));
            uint64_t t82 = (uint64_t)t81;
            unsigned __int128 n83 = (unsigned __int128)((uint64_t)8ULL);
            if (n83 >= 64) gm_trap_shift((uint64_t)n83, "u64", gm_srcnqtpfma6qe4a, 317, 45);
            uint64_t r84 = (uint64_t)((uint64_t)t82 << (int)n83);
            bool o85 = (uint64_t)(r84 >> (int)n83) != t82;
            if (o85) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 45);
            uint64_t t86 = r84;
            uint64_t t87 = (uint64_t)(t78 | t86);
            void * t88 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t89)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t89, gm_srcnqtpfma6qe4a, 317, 68);
            uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)t88) + (uintptr_t)((uint64_t)2ULL) * 1));
            uint64_t t91 = (uint64_t)t90;
            unsigned __int128 n92 = (unsigned __int128)((uint64_t)16ULL);
            if (n92 >= 64) gm_trap_shift((uint64_t)n92, "u64", gm_srcnqtpfma6qe4a, 317, 64);
            uint64_t r93 = (uint64_t)((uint64_t)t91 << (int)n92);
            bool o94 = (uint64_t)(r93 >> (int)n92) != t91;
            if (o94) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 64);
            uint64_t t95 = r93;
            uint64_t t96 = (uint64_t)(t87 | t95);
            void * t97 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)3ULL) >= (uintptr_t)(t98)) gm_trap_index((uintptr_t)((uint64_t)3ULL), t98, gm_srcnqtpfma6qe4a, 317, 88);
            uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)t97) + (uintptr_t)((uint64_t)3ULL) * 1));
            uint64_t t100 = (uint64_t)t99;
            unsigned __int128 n101 = (unsigned __int128)((uint64_t)24ULL);
            if (n101 >= 64) gm_trap_shift((uint64_t)n101, "u64", gm_srcnqtpfma6qe4a, 317, 84);
            uint64_t r102 = (uint64_t)((uint64_t)t100 << (int)n101);
            bool o103 = (uint64_t)(r102 >> (int)n101) != t100;
            if (o103) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 84);
            uint64_t t104 = r102;
            uint64_t t105 = (uint64_t)(t96 | t104);
            F.ret = t105;
            goto gm_fret;
        }
        uint64_t t106 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t107 = ((t106 == ((uint64_t)1ULL)));
        if (t107) {
            void * t108 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t109)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t109, gm_srcnqtpfma6qe4a, 318, 36);
            uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)t108) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint64_t t111 = (uint64_t)t110;
            F.ret = t111;
            goto gm_fret;
        }
        uint64_t t112 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t113 = ((t112 == ((uint64_t)2ULL)));
        if (t113) {
            void * t114 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t115)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t115, gm_srcnqtpfma6qe4a, 319, 36);
            uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)t114) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint64_t t117 = (uint64_t)t116;
            void * t118 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t119 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t119)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t119, gm_srcnqtpfma6qe4a, 319, 49);
            uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)t118) + (uintptr_t)((uint64_t)1ULL) * 1));
            uint64_t t121 = (uint64_t)t120;
            unsigned __int128 n122 = (unsigned __int128)((uint64_t)8ULL);
            if (n122 >= 64) gm_trap_shift((uint64_t)n122, "u64", gm_srcnqtpfma6qe4a, 319, 45);
            uint64_t r123 = (uint64_t)((uint64_t)t121 << (int)n122);
            bool o124 = (uint64_t)(r123 >> (int)n122) != t121;
            if (o124) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 319, 45);
            uint64_t t125 = r123;
            uint64_t t126 = (uint64_t)(t117 | t125);
            F.ret = t126;
            goto gm_fret;
        }
    }
    void * t127 = gm_l0;
    gm_b16_8 t128 = gm_l1;
    unsigned __int128 t129 = gm_f_get_uint_bq8c4b7(t127, t128);
    uint64_t t130 = (uint64_t)t129;
    F.ret = t130;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_l875bgq(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_l875bgq F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_tmp_8k6u9c9 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* n */
    void * gm_l2; /* sc */
    gm_b16_8 gm_l3; /* chunk */
    uint64_t gm_l4; /* base */
    uint64_t gm_l5; /* top */
    uint64_t gm_l6; /* size */
    gm_b16_8 gm_l7; /* b */
    GM_STACK_CHECK(&F, 524u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l8;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 360);
    gm_l2 = t3;
    void * t4 = gm_l2;
    void * t5 = GM_LD(void *, ((uint8_t *)t4));
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t4) + 8));
    void * t7 = gm_l2;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 40));
    if ((uintptr_t)t8 >= (uintptr_t)(t6)) gm_trap_index((uintptr_t)t8, t6, gm_srcpit8mdn0781q, 387, 14);
    gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)t5) + (uintptr_t)t8 * 16));
    gm_l3 = t9;
    void * t10 = GM_LD(void *, ((uint8_t *)&gm_l3));
    uint64_t t11 = (uintptr_t)t10;
    gm_l4 = t11;
    uint64_t t12 = gm_l4;
    void * t13 = gm_l2;
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 48));
    uint64_t r15; bool o16 = __builtin_add_overflow(t12, t14, &r15);
    if (o16) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 391, 14);
    uint64_t t17 = r15;
    uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)7ULL), &r18);
    if (o19) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 391, 14);
    uint64_t t20 = r18;
    uint64_t t21 = gm_f_us_cuv01fp(((uint64_t)7ULL));
    uint64_t t22 = (uint64_t)~t21;
    uint64_t t23 = (uint64_t)(t20 & t22);
    uint64_t t24 = gm_l4;
    uint64_t r25; bool o26 = __builtin_sub_overflow(t23, t24, &r25);
    if (o26) gm_trap("integer overflow in `-`", gm_srcpit8mdn0781q, 391, 12);
    uint64_t t27 = r25;
    gm_l5 = t27;
    uint64_t t28 = gm_l1;
    uint64_t t29 = gm_f_us_cuv01fp(t28);
    gm_l6 = t29;
    uint64_t t30 = gm_l1;
    uint8_t t31 = ((t30 == ((uint64_t)0ULL)));
    if (t31) {
        gm_l8 = ((uint8_t)1);
    } else {
        uint64_t t32 = gm_l5;
        uint64_t t33 = gm_l6;
        uint64_t r34; bool o35 = __builtin_add_overflow(t32, t33, &r34);
        if (o35) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 393, 18);
        uint64_t t36 = r34;
        uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        uint8_t t38 = (t36 > t37);
        gm_l8 = t38;
    }
    uint8_t t39 = gm_l8;
    if (t39) {
        void * t40 = gm_l0;
        uint64_t t41 = gm_l1;
        gm_b16_8 t42 = gm_f_tmp_next_dalgo1f((void *)NULL, cx, NULL, t40, t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t42;
        goto gm_fret;
    }
    void * t43 = GM_LD(void *, ((uint8_t *)&gm_l3));
    uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint64_t t45 = gm_l5;
    uint64_t t46 = gm_l5;
    uint64_t t47 = gm_l6;
    uint64_t r48; bool o49 = __builtin_add_overflow(t46, t47, &r48);
    if (o49) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 394, 21);
    uint64_t t50 = r48;
    uint64_t t51 = (uintptr_t)t45;
    uint64_t t52 = (uintptr_t)t50;
    if (t51 > t52 || t52 > (uintptr_t)t44) gm_trap_slice(t51, t52, t44, gm_srcpit8mdn0781q, 394, 10);
    uint64_t t53 = ((uintptr_t)t50) - (uintptr_t)t45;
    gm_b16_8 t54;
    GM_ST(void *, (uint8_t *)&t54, (void *)(((uint8_t *)t43 + (uintptr_t)t45 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(t53));
    gm_l7 = t54;
    void * t55 = gm_l2;
    uint64_t t56 = gm_l5;
    uint64_t t57 = gm_l6;
    uint64_t r58; bool o59 = __builtin_add_overflow(t56, t57, &r58);
    if (o59) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 395, 14);
    uint64_t t60 = r58;
    GM_ST(uint64_t, (((uint8_t *)t55) + 48), t60);
    gm_b16_8 t61 = gm_l7;
    gm_f_fill_abmf5g5(t61, ((uint8_t)0ULL));
    gm_b16_8 t62 = gm_l7;
    F.ret = t62;
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
inline __attribute__((aligned(4))) void gm_f_check_fatal_dgfalcm(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_check_fatal_dgfalcm F;
    void * gm_l0; /* m */
    void * gm_l1; /* t */
    uint8_t gm_l2; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 101u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t6 = gm_l0;
            uint8_t t7 = (uint8_t)(GM_LD(void *, ((((uint8_t *)t6) + 1672) + 56)) != 0);
            if (!t7) {
                goto gm_lb2;
            }
            void * t8 = GM_LD(void *, ((((uint8_t *)t6) + 1672) + 56));
            gm_l1 = t8;
            {
                void * t9 = gm_l1;
                gm_t76123gfet73g t10;
                GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t10 + 16, 0);
                GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)4ULL));
                GM_ST(void *, ((uint8_t *)&t10 + 8), t9);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t10);
                goto gm_fdone;
            }
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            void * t11 = gm_l0;
            uint8_t t12 = (uint8_t)((((((uint8_t *)t11) + 1672) + 64))[1] != 0);
            if (!t12) {
                goto gm_lb4;
            }
            uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t11) + 1672) + 64));
            gm_l2 = t13;
            {
                uint8_t t14 = gm_l2;
                gm_t76123gfet73g t15;
                GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t15 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t15 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t15 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t15 + 16, 0);
                GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)3ULL));
                GM_ST(uint8_t, ((uint8_t *)&t15 + 8), t14);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t15);
                goto gm_fdone;
            }
        }
gm_lb4:;
    }
gm_lb3:;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), GM_ZERO(gm_b0_1));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tcfa6l7reoosn gm_f_check_fatal_dgfalcm_c(void * a0) { gm_tcfa6l7reoosn gm_r; gm_f_check_fatal_dgfalcm(&gm_r, a0); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_connected_a7ps01v(void *gm_out, gm_t62ebbi2i5rkd a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_connected_a7ps01v F;
    gm_t62ebbi2i5rkd gm_l0; /* r */
    gm_toe5fmr3vvf4d gm_l1; /* e */
    gm_tm98ap4112hee gm_l2; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t2 = (uint8_t)!((((uint8_t *)&gm_l0))[0] != 0);
    if (t2) {
        gm_tm98ap4112hee t3 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&gm_l0) + 8));
        gm_l2 = t3;
    } else {
        gm_toe5fmr3vvf4d t4 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l0) + 8));
        gm_l1 = t4;
        gm_toe5fmr3vvf4d t5 = gm_l1;
        gm_thf5kr0is9u9i t6;
        GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
        ((uint8_t *)&t6)[0] = 1;
        GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t6 + 8), t5);
        gm_tppo2hmut88dq t7;
        memset(&t7, 0, sizeof t7);
        GM_ST(gm_thf5kr0is9u9i, ((uint8_t *)&(*(gm_t7eae7gj5k2n2 *)gm_out) + 0), t6);
        GM_ST(gm_tppo2hmut88dq, ((uint8_t *)&(*(gm_t7eae7gj5k2n2 *)gm_out) + 16), t7);
        GM_ST(int64_t, ((uint8_t *)&(*(gm_t7eae7gj5k2n2 *)gm_out) + 32), ((int64_t)0LL));
        goto gm_fdone;
    }
    gm_thf5kr0is9u9i t8;
    GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
    ((uint8_t *)&t8)[0] = 0;
    GM_ST(uint64_t, ((uint8_t *)&t8 + 8), ((uint64_t)0ULL));
    gm_tm98ap4112hee t9 = gm_l2;
    gm_tppo2hmut88dq t10;
    GM_ST(uint8_t, (uint8_t *)&t10 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t10 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t10 + 12, 0);
    GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t10), t9);
    ((uint8_t *)&t10)[8] = 1;
    GM_ST(gm_thf5kr0is9u9i, ((uint8_t *)&(*(gm_t7eae7gj5k2n2 *)gm_out) + 0), t8);
    GM_ST(gm_tppo2hmut88dq, ((uint8_t *)&(*(gm_t7eae7gj5k2n2 *)gm_out) + 16), t10);
    GM_ST(int64_t, ((uint8_t *)&(*(gm_t7eae7gj5k2n2 *)gm_out) + 32), ((int64_t)0LL));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t7eae7gj5k2n2 gm_f_connected_a7ps01v_c(gm_t62ebbi2i5rkd a0) { gm_t7eae7gj5k2n2 gm_r; gm_f_connected_a7ps01v(&gm_r, a0); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_env_exit_prbvpjt(void *gm_out, void * a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_env_exit_prbvpjt F;
    void * gm_l0; /* m */
    uint8_t gm_l1; /* code */
    GM_STACK_CHECK_SHALLOW(&F, 69u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    {
        {
            void * t7 = gm_l0;
            uint32_t t8 = GM_LD(uint32_t, ((((uint8_t *)t7) + 1672) + 40));
            uint8_t t9 = (!(t8 == ((uint32_t)0ULL)));
            if (!t9) {
                goto gm_lb2;
            }
            void * t10 = gm_l0;
            {
                {
                    uint8_t t11 = (uint8_t)((((((uint8_t *)t10) + 1672) + 64))[1] != 0);
                    if (t11) {
                        goto gm_lb4;
                    }
                    gm_l2 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    gm_l2 = ((uint8_t)0);
                    goto gm_lb3;
                }
gm_lb5:;
                gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 141, 35);
            }
gm_lb3:;
            uint8_t t12 = gm_l2;
            if (!t12) {
                goto gm_lb2;
            }
            void * t13 = gm_l0;
            uint8_t t14 = gm_l1;
            gm_tqs5p4n2tq19v t15;
            GM_ST(uint8_t, ((uint8_t *)&t15), t14);
            ((uint8_t *)&t15)[1] = 1;
            GM_ST(gm_tqs5p4n2tq19v, ((((uint8_t *)t13) + 1672) + 64), t15);
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        uint8_t t16 = gm_l1;
        gm_t76123gfet73g t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t17 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t17 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t17 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t17 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)3ULL));
        GM_ST(uint8_t, ((uint8_t *)&t17 + 8), t16);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t17);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tcfa6l7reoosn gm_f_env_exit_prbvpjt_c(void * a0, uint8_t a1) { gm_tcfa6l7reoosn gm_r; gm_f_env_exit_prbvpjt(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_04kmcqn(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_04kmcqn F;
    gm_b16_8 gm_l0; /* buf */
    gm_tm98ap4112hee gm_l1; /* f */
    gm_b16_8 gm_l2; /* buf */
    gm_tm98ap4112hee gm_l3; /* s */
    gm_b16_8 gm_l4; /* data */
    gm_tm98ap4112hee gm_l5; /* s */
    gm_b16_8 gm_l6; /* buf */
    gm_b16_8 gm_l7; /* host */
    uint16_t gm_l8; /* port */
    gm_tm98ap4112hee gm_l9; /* l */
    gm_tm98ap4112hee gm_l10; /* c */
    gm_b16_8 gm_l11; /* buf */
    gm_tm98ap4112hee gm_l12; /* c */
    gm_b16_8 gm_l13; /* data */
    gm_tm98ap4112hee gm_l14; /* c */
    GM_STACK_CHECK(&F, 892u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t7eae7gj5k2n2 gm_l15;
    void * t12 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_host_main_55puiu2 *)((&F.h)->up))->l0));
    void * t13 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_host_main_55puiu2 *)((&F.h)->up))->l0));
    {
        {
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)t13) + 0));
            uint8_t t15 = (uint8_t)(t14 == ((uint8_t)0ULL));
            if (!t15) {
                goto gm_lb2;
            }
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)t13) + 8));
            gm_l0 = t16;
            gm_b16_8 t17 = gm_l0;
            gm_node *pn18 = gm_find(cx, 1018230276);
            if (!pn18) gm_trap("no handler installed for effect `In`", gm_srchntnl6m6g3am, 267, 46);
            gm_thf5kr0is9u9i t19;
            t19 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_b16_8))pn18->clauses[0])(pn18->env, pn18->hctx, pn18, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tppo2hmut88dq t20;
            memset(&t20, 0, sizeof t20);
            GM_ST(gm_thf5kr0is9u9i, ((uint8_t *)&gm_l15 + 0), t19);
            GM_ST(gm_tppo2hmut88dq, ((uint8_t *)&gm_l15 + 16), t20);
            GM_ST(int64_t, ((uint8_t *)&gm_l15 + 32), ((int64_t)0LL));
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)t13) + 0));
            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)1ULL));
            if (!t22) {
                goto gm_lb3;
            }
            gm_tm98ap4112hee t23 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)t13) + 8));
            gm_l1 = t23;
            gm_b16_8 t24 = GM_LD(gm_b16_8, ((((uint8_t *)t13) + 8) + 8));
            gm_l2 = t24;
            gm_tm98ap4112hee t25 = gm_l1;
            gm_b16_8 t26 = gm_l2;
            gm_node *pn27 = gm_find(cx, 2126215853);
            if (!pn27) gm_trap("no handler installed for effect `Fs`", gm_srchntnl6m6g3am, 268, 48);
            gm_thf5kr0is9u9i t28;
            t28 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn27->clauses[2])(pn27->env, pn27->hctx, pn27, t25, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tppo2hmut88dq t29;
            memset(&t29, 0, sizeof t29);
            GM_ST(gm_thf5kr0is9u9i, ((uint8_t *)&gm_l15 + 0), t28);
            GM_ST(gm_tppo2hmut88dq, ((uint8_t *)&gm_l15 + 16), t29);
            GM_ST(int64_t, ((uint8_t *)&gm_l15 + 32), ((int64_t)0LL));
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)t13) + 0));
            uint8_t t31 = (uint8_t)(t30 == ((uint8_t)4ULL));
            if (!t31) {
                goto gm_lb4;
            }
            gm_tm98ap4112hee t32 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)t13) + 8));
            gm_l3 = t32;
            gm_b16_8 t33 = GM_LD(gm_b16_8, ((((uint8_t *)t13) + 8) + 8));
            gm_l4 = t33;
            gm_tm98ap4112hee t34 = gm_l3;
            gm_b16_8 t35 = gm_l4;
            gm_node *pn36 = gm_find(cx, 163658498);
            if (!pn36) gm_trap("no handler installed for effect `Net`", gm_srchntnl6m6g3am, 269, 49);
            gm_thf5kr0is9u9i t37;
            t37 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn36->clauses[4])(pn36->env, pn36->hctx, pn36, t34, t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tppo2hmut88dq t38;
            memset(&t38, 0, sizeof t38);
            GM_ST(gm_thf5kr0is9u9i, ((uint8_t *)&gm_l15 + 0), t37);
            GM_ST(gm_tppo2hmut88dq, ((uint8_t *)&gm_l15 + 16), t38);
            GM_ST(int64_t, ((uint8_t *)&gm_l15 + 32), ((int64_t)0LL));
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)t13) + 0));
            uint8_t t40 = (uint8_t)(t39 == ((uint8_t)5ULL));
            if (!t40) {
                goto gm_lb5;
            }
            gm_tm98ap4112hee t41 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)t13) + 8));
            gm_l5 = t41;
            gm_b16_8 t42 = GM_LD(gm_b16_8, ((((uint8_t *)t13) + 8) + 8));
            gm_l6 = t42;
            gm_tm98ap4112hee t43 = gm_l5;
            gm_b16_8 t44 = gm_l6;
            gm_node *pn45 = gm_find(cx, 163658498);
            if (!pn45) gm_trap("no handler installed for effect `Net`", gm_srchntnl6m6g3am, 270, 48);
            gm_thf5kr0is9u9i t46;
            t46 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn45->clauses[5])(pn45->env, pn45->hctx, pn45, t43, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tppo2hmut88dq t47;
            memset(&t47, 0, sizeof t47);
            GM_ST(gm_thf5kr0is9u9i, ((uint8_t *)&gm_l15 + 0), t46);
            GM_ST(gm_tppo2hmut88dq, ((uint8_t *)&gm_l15 + 16), t47);
            GM_ST(int64_t, ((uint8_t *)&gm_l15 + 32), ((int64_t)0LL));
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)t13) + 0));
            uint8_t t49 = (uint8_t)(t48 == ((uint8_t)2ULL));
            if (!t49) {
                goto gm_lb6;
            }
            gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)t13) + 8));
            gm_l7 = t50;
            uint16_t t51 = GM_LD(uint16_t, ((((uint8_t *)t13) + 8) + 16));
            gm_l8 = t51;
            gm_b16_8 t52 = gm_l7;
            uint16_t t53 = gm_l8;
            gm_node *pn54 = gm_find(cx, 163658498);
            if (!pn54) gm_trap("no handler installed for effect `Net`", gm_srchntnl6m6g3am, 271, 47);
            gm_t62ebbi2i5rkd t55;
            t55 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8, uint16_t))pn54->clauses[0])(pn54->env, pn54->hctx, pn54, t52, t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_connected_a7ps01v((void *)&gm_l15, t55);
            goto gm_lb1;
        }
gm_lb6:;
        {
            uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)t13) + 0));
            uint8_t t57 = (uint8_t)(t56 == ((uint8_t)3ULL));
            if (!t57) {
                goto gm_lb7;
            }
            gm_tm98ap4112hee t58 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)t13) + 8));
            gm_l9 = t58;
            gm_tm98ap4112hee t59 = gm_l9;
            gm_node *pn60 = gm_find(cx, 163658498);
            if (!pn60) gm_trap("no handler installed for effect `Net`", gm_srchntnl6m6g3am, 272, 37);
            gm_t62ebbi2i5rkd t61;
            t61 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn60->clauses[3])(pn60->env, pn60->hctx, pn60, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_connected_a7ps01v((void *)&gm_l15, t61);
            goto gm_lb1;
        }
gm_lb7:;
        {
            uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)t13) + 0));
            uint8_t t63 = (uint8_t)(t62 == ((uint8_t)6ULL));
            if (!t63) {
                goto gm_lb8;
            }
            gm_tm98ap4112hee t64 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)t13) + 8));
            gm_l10 = t64;
            gm_b16_8 t65 = GM_LD(gm_b16_8, ((((uint8_t *)t13) + 8) + 8));
            gm_l11 = t65;
            gm_thf5kr0is9u9i t66;
            GM_ST(uint8_t, (uint8_t *)&t66 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t66 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t66 + 4, 0);
            ((uint8_t *)&t66)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t66 + 8), ((uint64_t)0ULL));
            gm_tppo2hmut88dq t67;
            memset(&t67, 0, sizeof t67);
            gm_tm98ap4112hee t68 = gm_l10;
            gm_b16_8 t69 = gm_l11;
            int64_t t70 = gm_f_proc_read_kc2l8gi((void *)NULL, cx, NULL, t68, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_thf5kr0is9u9i, ((uint8_t *)&gm_l15 + 0), t66);
            GM_ST(gm_tppo2hmut88dq, ((uint8_t *)&gm_l15 + 16), t67);
            GM_ST(int64_t, ((uint8_t *)&gm_l15 + 32), t70);
            goto gm_lb1;
        }
gm_lb8:;
        {
            uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)t13) + 0));
            uint8_t t72 = (uint8_t)(t71 == ((uint8_t)7ULL));
            if (!t72) {
                goto gm_lb9;
            }
            gm_tm98ap4112hee t73 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)t13) + 8));
            gm_l12 = t73;
            gm_b16_8 t74 = GM_LD(gm_b16_8, ((((uint8_t *)t13) + 8) + 8));
            gm_l13 = t74;
            gm_thf5kr0is9u9i t75;
            GM_ST(uint8_t, (uint8_t *)&t75 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t75 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t75 + 4, 0);
            ((uint8_t *)&t75)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t75 + 8), ((uint64_t)0ULL));
            gm_tppo2hmut88dq t76;
            memset(&t76, 0, sizeof t76);
            gm_tm98ap4112hee t77 = gm_l12;
            gm_b16_8 t78 = gm_l13;
            gm_node *pn79 = gm_find(cx, 2119230);
            if (!pn79) gm_trap("no handler installed for effect `Proc`", gm_srchntnl6m6g3am, 274, 73);
            gm_thf5kr0is9u9i t80;
            t80 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn79->clauses[1])(pn79->env, pn79->hctx, pn79, t77, t78);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            int64_t t81 = gm_f_sys_count_50c5lcp(t80);
            GM_ST(gm_thf5kr0is9u9i, ((uint8_t *)&gm_l15 + 0), t75);
            GM_ST(gm_tppo2hmut88dq, ((uint8_t *)&gm_l15 + 16), t76);
            GM_ST(int64_t, ((uint8_t *)&gm_l15 + 32), t81);
            goto gm_lb1;
        }
gm_lb9:;
        {
            uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)t13) + 0));
            uint8_t t83 = (uint8_t)(t82 == ((uint8_t)8ULL));
            if (!t83) {
                goto gm_lb10;
            }
            gm_tm98ap4112hee t84 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)t13) + 8));
            gm_l14 = t84;
            gm_thf5kr0is9u9i t85;
            GM_ST(uint8_t, (uint8_t *)&t85 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t85 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t85 + 4, 0);
            ((uint8_t *)&t85)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t85 + 8), ((uint64_t)0ULL));
            gm_tppo2hmut88dq t86;
            memset(&t86, 0, sizeof t86);
            gm_tm98ap4112hee t87 = gm_l14;
            gm_node *pn88 = gm_find(cx, 2119230);
            if (!pn88) gm_trap("no handler installed for effect `Proc`", gm_srchntnl6m6g3am, 275, 68);
            gm_tlmjd3nndlf3r t89;
            t89 = ((gm_tlmjd3nndlf3r (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn88->clauses[4])(pn88->env, pn88->hctx, pn88, t87);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            int64_t t90 = gm_f_proc_status_7nq4d17(t89);
            GM_ST(gm_thf5kr0is9u9i, ((uint8_t *)&gm_l15 + 0), t85);
            GM_ST(gm_tppo2hmut88dq, ((uint8_t *)&gm_l15 + 16), t86);
            GM_ST(int64_t, ((uint8_t *)&gm_l15 + 32), t90);
            goto gm_lb1;
        }
gm_lb10:;
        gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 266, 20);
    }
gm_lb1:;
    memmove((((uint8_t *)t12) + 32), ((uint8_t *)&gm_l15), 40);
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
extern __attribute__((aligned(4))) void gm_f_host_main_55puiu2(void * a0);
inline __attribute__((aligned(4))) void gm_f_on_host_thread_kmegc07(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2) {
    struct gm_fr_on_host_thread_kmegc07 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tnp3hpcm9uhoi gm_l2; /* op */
    void * gm_l3; /* j */
    gm_toe5fmr3vvf4d gm_l4; /* e */
    gm_tm98ap4112hee gm_l5; /* t */
    gm_tcfa6l7reoosn gm_l6; /* r */
    GM_STACK_CHECK(&F, 679u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&gm_l2, a2, sizeof gm_l2);
    gm_tq7s98ihonkju gm_l8;
    gm_t62ebbi2i5rkd gm_l9;
    {
        gm_thf5kr0is9u9i t2;
        GM_ST(uint8_t, (uint8_t *)&t2 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t2 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t2 + 4, 0);
        ((uint8_t *)&t2)[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&t2 + 8), ((uint64_t)0ULL));
        gm_tppo2hmut88dq t3;
        memset(&t3, 0, sizeof t3);
        gm_t7eae7gj5k2n2 t4;
        GM_ST(gm_thf5kr0is9u9i, ((uint8_t *)&t4 + 0), t2);
        GM_ST(gm_tppo2hmut88dq, ((uint8_t *)&t4 + 16), t3);
        GM_ST(int64_t, ((uint8_t *)&t4 + 32), ((int64_t)0LL));
        gm_tnp3hpcm9uhoi t5 = GM_LD(gm_tnp3hpcm9uhoi, ((uint8_t *)&gm_l2));
        gm_t6or663cm29rr t6;
        GM_ST(uint32_t, (uint8_t *)&t6 + 76, 0);
        GM_ST(gm_tnp3hpcm9uhoi, ((uint8_t *)&t6 + 0), t5);
        GM_ST(gm_t7eae7gj5k2n2, ((uint8_t *)&t6 + 32), t4);
        GM_ST(uint32_t, ((uint8_t *)&t6 + 72), ((uint32_t)0ULL));
        gm_tq7s98ihonkju t7 = gm_f_new_g0l0iur((void *)NULL, cx, NULL, &t6, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k6qm3ruhsta)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t7;
    }
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_srchntnl6m6g3am, 289, 10);
    void * t9 = GM_LD(void *, (((uint8_t *)&gm_l8) + 8));
    gm_l3 = t9;
    void * t10 = gm_l3;
    gm_t62ebbi2i5rkd t11 = gm_f_spawn_2tmnfpi(((void *)gm_f_host_main_55puiu2), t10);
    gm_l9 = t11;
    uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (t12) {
        gm_tm98ap4112hee t13 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&gm_l9) + 8));
        gm_l5 = t13;
    } else {
        gm_toe5fmr3vvf4d t14 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l9) + 8));
        gm_l4 = t14;
        {
            void * t15 = gm_l3;
            gm_b0_1 t16 = gm_f_destroy_o4n4nl0((void *)NULL, cx, NULL, t15, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kf51o7bo59i)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_toe5fmr3vvf4d t17 = gm_l4;
            gm_thf5kr0is9u9i t18;
            GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
            ((uint8_t *)&t18)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t18 + 8), t17);
            gm_tppo2hmut88dq t19;
            memset(&t19, 0, sizeof t19);
            gm_t7eae7gj5k2n2 t20;
            GM_ST(gm_thf5kr0is9u9i, ((uint8_t *)&t20 + 0), t18);
            GM_ST(gm_tppo2hmut88dq, ((uint8_t *)&t20 + 16), t19);
            GM_ST(int64_t, ((uint8_t *)&t20 + 32), ((int64_t)0LL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t0jbubompi7qd *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t0jbubompi7qd *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t0jbubompi7qd *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t0jbubompi7qd *)gm_out))[0] = 0;
            GM_ST(gm_t7eae7gj5k2n2, ((uint8_t *)&(*(gm_t0jbubompi7qd *)gm_out) + 8), t20);
            goto gm_fdone;
        }
    }
    {
        void * t21 = gm_l0;
        void * t22 = GM_LD(void *, (((uint8_t *)t21) + 1672));
        uint64_t t23 = GM_LD(uint64_t, ((((uint8_t *)t21) + 1672) + 8));
        void * t24 = gm_l0;
        uint32_t t25 = GM_LD(uint32_t, ((((uint8_t *)t24) + 1672) + 40));
        uint64_t t26 = (uint64_t)t25;
        if ((uintptr_t)t26 >= (uintptr_t)(t23)) gm_trap_index((uintptr_t)t26, t23, gm_srchntnl6m6g3am, 294, 5);
        void * t27 = gm_l3;
        void * t28 = (void *)(((uint8_t *)t27) + 72);
        gm_tdkjirf1g83ud t29;
        GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t29 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)4ULL));
        GM_ST(void *, ((uint8_t *)&t29 + 8), t28);
        GM_ST(gm_tdkjirf1g83ud, ((((uint8_t *)t22) + (uintptr_t)t26 * 48) + 8), t29);
    }
    {
        void * t30 = gm_l0;
        gm_tl874t2atnlh3 t31 = gm_l1;
        gm_f_switch_thread_m7aoqko((void *)&gm_l6, (void *)NULL, cx, NULL, t30, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    gm_tm98ap4112hee t32 = gm_l5;
    gm_f_join_r8j2dp1(GM_CONV(gm_csfq64m1m1rt6m, t32));
    {
        void * t33 = gm_l3;
        memmove(((uint8_t *)&F.l7), (((uint8_t *)t33) + 32), 40);
    }
    {
        void * t34 = gm_l3;
        gm_b0_1 t35 = gm_f_destroy_o4n4nl0((void *)NULL, cx, NULL, t34, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kh1g3s3qgm7)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (t36) {
    } else {
        gm_t76123gfet73g t37 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l6) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t0jbubompi7qd *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t0jbubompi7qd *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t0jbubompi7qd *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t0jbubompi7qd *)gm_out) + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t0jbubompi7qd *)gm_out) + 40, 0);
        ((uint8_t *)&(*(gm_t0jbubompi7qd *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t0jbubompi7qd *)gm_out) + 8), t37);
        goto gm_fdone;
    }
    {
        gm_t7eae7gj5k2n2 t38 = GM_LD(gm_t7eae7gj5k2n2, ((uint8_t *)&F.l7));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t0jbubompi7qd *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t0jbubompi7qd *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t0jbubompi7qd *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t0jbubompi7qd *)gm_out))[0] = 0;
        GM_ST(gm_t7eae7gj5k2n2, ((uint8_t *)&(*(gm_t0jbubompi7qd *)gm_out) + 8), t38);
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
__attribute__((aligned(4))) uint8_t gm_f_runnable_o1q905g(void * a0, void * a1, gm_tf5srh4mp4b3i a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_runnable_o1q905g F;
    void * gm_l0; /* m */
    void * gm_l1; /* t */
    gm_tf5srh4mp4b3i gm_l2; /* now */
    uint32_t gm_l3; /* h */
    uint64_t gm_l4; /* deadline */
    void * gm_l5; /* done */
    uint64_t gm_l6; /* seen */
    gm_tf5srh4mp4b3i gm_l7; /* deadline */
    GM_STACK_CHECK_SHALLOW(&F, 387u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l8;
    uint8_t gm_l9;
    uint8_t gm_l10;
    uint64_t gm_l11;
    uint8_t gm_l12;
    uint8_t gm_l13;
    uint8_t gm_l14;
    uint8_t gm_l15;
    uint8_t gm_l16;
    uint64_t gm_l17;
    uint64_t gm_l18;
    void * t18 = gm_l1;
    {
        {
            uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t18) + 8) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)0ULL));
            if (!t20) {
                goto gm_lb2;
            }
            gm_l8 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t18) + 8) + 0));
            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)1ULL));
            if (!t22) {
                goto gm_lb3;
            }
            uint32_t t23 = GM_LD(uint32_t, ((((uint8_t *)t18) + 8) + 8));
            gm_l3 = t23;
            {
                void * t24 = gm_l0;
                void * t25 = GM_LD(void *, (((uint8_t *)t24) + 1672));
                uint64_t t26 = GM_LD(uint64_t, ((((uint8_t *)t24) + 1672) + 8));
                uint32_t t27 = gm_l3;
                uint64_t t28 = (uint64_t)t27;
                if ((uintptr_t)t28 >= (uintptr_t)(t26)) gm_trap_index((uintptr_t)t28, t26, gm_srchntnl6m6g3am, 72, 17);
                uint8_t t29 = GM_LD(uint8_t, (((((uint8_t *)t25) + (uintptr_t)t28 * 48) + 8) + 0));
                uint8_t t30 = (uint8_t)((t29 == ((uint8_t)5ULL)));
                gm_tdkjirf1g83ud t31 = GM_LD(gm_tdkjirf1g83ud, ((((uint8_t *)t25) + (uintptr_t)t28 * 48) + 8));
                gm_tdkjirf1g83ud t32;
                GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t32 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t32 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t32 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)5ULL));
                gm_l8 = t30;
            }
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t18) + 8) + 0));
            uint8_t t34 = (uint8_t)(t33 == ((uint8_t)3ULL));
            if (!t34) {
                goto gm_lb4;
            }
            uint64_t t35 = GM_LD(uint64_t, ((((uint8_t *)t18) + 8) + 8));
            gm_l4 = t35;
            {
                {
                    uint8_t t36 = (uint8_t)((((uint8_t *)&gm_l2))[8] != 0);
                    if (t36) {
                        goto gm_lb6;
                    }
                    gm_l10 = ((uint8_t)0);
                    goto gm_lb5;
                }
gm_lb6:;
                {
                    gm_l10 = ((uint8_t)1);
                    goto gm_lb5;
                }
gm_lb7:;
                gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 73, 25);
            }
gm_lb5:;
            uint8_t t37 = gm_l10;
            if (t37) {
                uint8_t t38 = (uint8_t)((((uint8_t *)&gm_l2))[8] != 0);
                if (t38) {
                    uint64_t t39 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
                    gm_l11 = t39;
                } else {
                    gm_l11 = ((uint64_t)0ULL);
                }
                uint64_t t40 = gm_l11;
                uint64_t t41 = gm_l4;
                uint8_t t42 = (t40 >= t41);
                gm_l9 = t42;
            } else {
                gm_l9 = ((uint8_t)0);
            }
            uint8_t t43 = gm_l9;
            gm_l8 = t43;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t44 = GM_LD(uint8_t, ((((uint8_t *)t18) + 8) + 0));
            uint8_t t45 = (uint8_t)(t44 == ((uint8_t)4ULL));
            if (!t45) {
                goto gm_lb8;
            }
            void * t46 = GM_LD(void *, ((((uint8_t *)t18) + 8) + 8));
            gm_l5 = t46;
            void * t47 = gm_l5;
            gm_t9qkh0rsooc8d t48;
            GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)1ULL));
            uint32_t t49 = __atomic_load_n(((uint32_t *)t47), gm_ord[GM_LD(uint8_t, ((uint8_t *)&t48))]);
            uint8_t t50 = (!(t49 == ((uint32_t)0ULL)));
            gm_l8 = t50;
            goto gm_lb1;
        }
gm_lb8:;
        {
            uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)t18) + 8) + 0));
            uint8_t t52 = (uint8_t)(t51 == ((uint8_t)2ULL));
            if (!t52) {
                goto gm_lb9;
            }
            uint64_t t53 = GM_LD(uint64_t, ((((uint8_t *)t18) + 8) + 8));
            gm_l6 = t53;
            gm_tf5srh4mp4b3i t54 = GM_LD(gm_tf5srh4mp4b3i, (((((uint8_t *)t18) + 8) + 8) + 8));
            gm_l7 = t54;
            uint64_t t55 = gm_l6;
            void * t56 = gm_l0;
            uint64_t t57 = GM_LD(uint64_t, ((((uint8_t *)t56) + 1672) + 48));
            uint8_t t58 = (!(t55 == t57));
            if (t58) {
                gm_l12 = ((uint8_t)1);
            } else {
                {
                    {
                        uint8_t t59 = (uint8_t)((((uint8_t *)&gm_l7))[8] != 0);
                        if (t59) {
                            goto gm_lb11;
                        }
                        gm_l15 = ((uint8_t)0);
                        goto gm_lb10;
                    }
gm_lb11:;
                    {
                        gm_l15 = ((uint8_t)1);
                        goto gm_lb10;
                    }
gm_lb12:;
                    gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 75, 68);
                }
gm_lb10:;
                uint8_t t60 = gm_l15;
                if (t60) {
                    {
                        {
                            uint8_t t61 = (uint8_t)((((uint8_t *)&gm_l2))[8] != 0);
                            if (t61) {
                                goto gm_lb14;
                            }
                            gm_l16 = ((uint8_t)0);
                            goto gm_lb13;
                        }
gm_lb14:;
                        {
                            gm_l16 = ((uint8_t)1);
                            goto gm_lb13;
                        }
gm_lb15:;
                        gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 75, 93);
                    }
gm_lb13:;
                    uint8_t t62 = gm_l16;
                    gm_l14 = t62;
                } else {
                    gm_l14 = ((uint8_t)0);
                }
                uint8_t t63 = gm_l14;
                if (t63) {
                    uint8_t t64 = (uint8_t)((((uint8_t *)&gm_l2))[8] != 0);
                    if (t64) {
                        uint64_t t65 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
                        gm_l17 = t65;
                    } else {
                        gm_l17 = ((uint64_t)0ULL);
                    }
                    uint64_t t66 = gm_l17;
                    uint8_t t67 = (uint8_t)((((uint8_t *)&gm_l7))[8] != 0);
                    if (t67) {
                        uint64_t t68 = GM_LD(uint64_t, ((uint8_t *)&gm_l7));
                        gm_l18 = t68;
                    } else {
                        gm_l18 = ((uint64_t)0ULL);
                    }
                    uint64_t t69 = gm_l18;
                    uint8_t t70 = (t66 >= t69);
                    gm_l13 = t70;
                } else {
                    gm_l13 = ((uint8_t)0);
                }
                uint8_t t71 = gm_l13;
                gm_l12 = t71;
            }
            uint8_t t72 = gm_l12;
            gm_l8 = t72;
            goto gm_lb1;
        }
gm_lb9:;
        {
            uint8_t t73 = GM_LD(uint8_t, ((((uint8_t *)t18) + 8) + 0));
            uint8_t t74 = (uint8_t)(t73 == ((uint8_t)5ULL));
            if (!t74) {
                goto gm_lb16;
            }
            gm_l8 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb16:;
        gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 70, 72);
    }
gm_lb1:;
    uint8_t t75 = gm_l8;
    F.ret = t75;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_sleep_gqtie06(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2) {
    struct gm_fr_sleep_gqtie06 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    uint64_t gm_l2; /* ns */
    GM_STACK_CHECK(&F, 254u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t4ce0jd3tdro4 gm_l3;
    gm_tcfa6l7reoosn gm_l4;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, ((((uint8_t *)t2) + 1672) + 8));
    uint8_t t4 = ((t3 == ((uint64_t)1ULL)));
    if (t4) {
        uint64_t t5 = gm_l2;
        gm_t4ce0jd3tdro4 t6;
        GM_ST(uint64_t, ((uint8_t *)&t6 + 0), t5);
        gm_node *pn7 = gm_find(cx, 1579066750);
        if (!pn7) gm_trap("no handler installed for effect `Clock`", gm_srchntnl6m6g3am, 227, 9);
        gm_b0_1 t8;
        t8 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_t4ce0jd3tdro4))pn7->clauses[2])(pn7->env, pn7->hctx, pn7, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
    {
        void * t9 = gm_l0;
        void * t10 = GM_LD(void *, (((uint8_t *)t9) + 1672));
        uint64_t t11 = GM_LD(uint64_t, ((((uint8_t *)t9) + 1672) + 8));
        void * t12 = gm_l0;
        uint32_t t13 = GM_LD(uint32_t, ((((uint8_t *)t12) + 1672) + 40));
        uint64_t t14 = (uint64_t)t13;
        if ((uintptr_t)t14 >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)t14, t11, gm_srchntnl6m6g3am, 230, 5);
        gm_node *pn15 = gm_find(cx, 1579066750);
        if (!pn15) gm_trap("no handler installed for effect `Clock`", gm_srchntnl6m6g3am, 230, 63);
        gm_t4ce0jd3tdro4 t16;
        t16 = ((gm_t4ce0jd3tdro4 (*)(void *, gm_node *, gm_node *))pn15->clauses[0])(pn15->env, pn15->hctx, pn15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t16;
        uint64_t t17 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
        uint64_t t18 = gm_l2;
        uint64_t r19; bool o20 = __builtin_add_overflow(t17, t18, &r19);
        uint64_t t21 = o20 ? (1 ? ((uint64_t)18446744073709551615ULL) : ((uint64_t)0ULL)) : r19;
        gm_tdkjirf1g83ud t22;
        GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t22 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t22 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)3ULL));
        GM_ST(uint64_t, ((uint8_t *)&t22 + 8), t21);
        GM_ST(gm_tdkjirf1g83ud, ((((uint8_t *)t10) + (uintptr_t)t14 * 48) + 8), t22);
    }
    {
        void * t23 = gm_l0;
        gm_tl874t2atnlh3 t24 = gm_l1;
        gm_f_switch_thread_m7aoqko((void *)&gm_l4, (void *)NULL, cx, NULL, t23, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t25 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (t25) {
    } else {
        gm_t76123gfet73g t26 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l4) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t26);
        goto gm_fdone;
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
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_switch_thread_m7aoqko(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_switch_thread_m7aoqko F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* th */
    uint32_t gm_l3; /* me */
    uint64_t gm_l4; /* n */
    gm_tf5srh4mp4b3i gm_l5; /* now */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_tf5srh4mp4b3i gm_l9; /* d */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* k */
    uint64_t gm_l13; /* i */
    void * gm_l14; /* t */
    gm_tf5srh4mp4b3i gm_l15; /* earliest */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_tf5srh4mp4b3i gm_l19; /* d */
    uint64_t gm_l20; /* e */
    uint64_t gm_l21; /* d */
    uint64_t gm_l22; /* e */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    uint64_t gm_l26; /* soon */
    uint64_t gm_l27; /* e */
    uint64_t gm_l28; /* e */
    uint64_t gm_l29; /* t */
    GM_STACK_CHECK(&F, 1733u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l30;
    gm_t4ce0jd3tdro4 gm_l31;
    uint8_t gm_l32;
    gm_t4ce0jd3tdro4 gm_l33;
    uint8_t gm_l34;
    uint64_t gm_l35;
    uint8_t gm_l36;
    uint64_t gm_l37;
    gm_t4ce0jd3tdro4 gm_l38;
    gm_t4ce0jd3tdro4 gm_l39;
    {
        void * t49 = gm_l0;
        void * t50 = (void *)(((uint8_t *)t49) + 1672);
        gm_l2 = t50;
        void * t51 = gm_l2;
        uint32_t t52 = GM_LD(uint32_t, (((uint8_t *)t51) + 40));
        gm_l3 = t52;
        void * t53 = gm_l2;
        void * t54 = GM_LD(void *, ((uint8_t *)t53));
        uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)t53) + 8));
        uint32_t t56 = gm_l3;
        uint64_t t57 = (uint64_t)t56;
        if ((uintptr_t)t57 >= (uintptr_t)(t55)) gm_trap_index((uintptr_t)t57, t55, gm_srchntnl6m6g3am, 84, 5);
        void * t58 = gm_l0;
        uint32_t t59 = GM_LD(uint32_t, (((uint8_t *)t58) + 1064));
        GM_ST(uint32_t, (((uint8_t *)t54) + (uintptr_t)t57 * 48), t59);
        void * t60 = gm_l2;
        uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)t60) + 8));
        gm_l4 = t61;
        {
            for (;;) {
                {
                    if (!((uint8_t)1)) {
                        goto gm_lb1;
                    }
                    {
                        {
                            gm_tf5srh4mp4b3i t62;
                            memset(&t62, 0, sizeof t62);
                            gm_l5 = t62;
                            void * t63 = gm_l2;
                            gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)t63));
                            gm_l6 = t64;
                            gm_l7 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t65 = gm_l7;
                                    uint64_t t66 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                    uint8_t t67 = (t65 < t66);
                                    if (!t67) {
                                        goto gm_lb4;
                                    }
                                    {
                                        void * t68 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                        uint64_t t69 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                        uint64_t t70 = gm_l7;
                                        {
                                            {
                                                memmove(((uint8_t *)&F.l8), (((uint8_t *)t68) + (uintptr_t)t70 * 48), 48);
                                                {
                                                    {
                                                        uint8_t t71 = GM_LD(uint8_t, ((((uint8_t *)&F.l8) + 8) + 0));
                                                        uint8_t t72 = (uint8_t)(t71 == ((uint8_t)2ULL));
                                                        if (!t72) {
                                                            goto gm_lb10;
                                                        }
                                                        gm_tf5srh4mp4b3i t73 = GM_LD(gm_tf5srh4mp4b3i, (((((uint8_t *)&F.l8) + 8) + 8) + 8));
                                                        gm_l9 = t73;
                                                        {
                                                            {
                                                                uint8_t t74 = (uint8_t)((((uint8_t *)&gm_l9))[8] != 0);
                                                                if (t74) {
                                                                    goto gm_lb12;
                                                                }
                                                                gm_l30 = ((uint8_t)0);
                                                                goto gm_lb11;
                                                            }
gm_lb12:;
                                                            {
                                                                gm_l30 = ((uint8_t)1);
                                                                goto gm_lb11;
                                                            }
gm_lb13:;
                                                            gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 90, 45);
                                                        }
gm_lb11:;
                                                        uint8_t t75 = gm_l30;
                                                        if (!t75) {
                                                            goto gm_lb10;
                                                        }
                                                        gm_node *pn76 = gm_find(cx, 1579066750);
                                                        if (!pn76) gm_trap("no handler installed for effect `Clock`", gm_srchntnl6m6g3am, 90, 67);
                                                        gm_t4ce0jd3tdro4 t77;
                                                        t77 = ((gm_t4ce0jd3tdro4 (*)(void *, gm_node *, gm_node *))pn76->clauses[0])(pn76->env, pn76->hctx, pn76);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_l31 = t77;
                                                        uint64_t t78 = GM_LD(uint64_t, ((uint8_t *)&gm_l31));
                                                        gm_tf5srh4mp4b3i t79;
                                                        GM_ST(uint8_t, (uint8_t *)&t79 + 9, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t79 + 10, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&t79 + 12, 0);
                                                        GM_ST(uint64_t, ((uint8_t *)&t79), t78);
                                                        ((uint8_t *)&t79)[8] = 1;
                                                        gm_l5 = t79;
                                                        goto gm_lb9;
                                                    }
gm_lb10:;
                                                }
gm_lb9:;
                                                {
                                                    {
                                                        uint8_t t80 = GM_LD(uint8_t, ((((uint8_t *)&F.l8) + 8) + 0));
                                                        uint8_t t81 = (uint8_t)(t80 == ((uint8_t)3ULL));
                                                        if (!t81) {
                                                            goto gm_lb15;
                                                        }
                                                        gm_l32 = ((uint8_t)1);
                                                        goto gm_lb14;
                                                    }
gm_lb15:;
                                                    {
                                                        gm_l32 = ((uint8_t)0);
                                                        goto gm_lb14;
                                                    }
gm_lb16:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 91, 16);
                                                }
gm_lb14:;
                                                uint8_t t82 = gm_l32;
                                                if (t82) {
                                                    gm_node *pn83 = gm_find(cx, 1579066750);
                                                    if (!pn83) gm_trap("no handler installed for effect `Clock`", gm_srchntnl6m6g3am, 91, 45);
                                                    gm_t4ce0jd3tdro4 t84;
                                                    t84 = ((gm_t4ce0jd3tdro4 (*)(void *, gm_node *, gm_node *))pn83->clauses[0])(pn83->env, pn83->hctx, pn83);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_l33 = t84;
                                                    uint64_t t85 = GM_LD(uint64_t, ((uint8_t *)&gm_l33));
                                                    gm_tf5srh4mp4b3i t86;
                                                    GM_ST(uint8_t, (uint8_t *)&t86 + 9, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t86 + 10, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t86 + 12, 0);
                                                    GM_ST(uint64_t, ((uint8_t *)&t86), t85);
                                                    ((uint8_t *)&t86)[8] = 1;
                                                    gm_l5 = t86;
                                                }
                                                goto gm_lb7;
                                            }
gm_lb8:;
                                            gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 89, 9);
                                        }
gm_lb7:;
                                    }
gm_lb6:;
                                    uint64_t t87 = gm_l7;
                                    uint64_t r88; bool o89 = __builtin_add_overflow(t87, ((uint64_t)1ULL), &r88);
                                    uint64_t t90 = r88;
                                    gm_l7 = t90;
                                }
                            }
gm_lb4:;
                        }
                        {
                            gm_l11 = ((uint64_t)1ULL);
                            uint64_t t91 = gm_l4;
                            uint64_t r92; bool o93 = __builtin_add_overflow(t91, ((uint64_t)1ULL), &r92);
                            if (o93) gm_trap("integer overflow in `+`", gm_srchntnl6m6g3am, 93, 21);
                            uint64_t t94 = r92;
                            gm_l10 = t94;
                            {
                                for (;;) {
                                    uint64_t t95 = gm_l11;
                                    uint64_t t96 = gm_l10;
                                    uint8_t t97 = (t95 < t96);
                                    if (!t97) {
                                        goto gm_lb17;
                                    }
                                    {
                                        {
                                            {
                                                uint64_t t98 = gm_l11;
                                                gm_l12 = t98;
                                                uint32_t t99 = gm_l3;
                                                uint64_t t100 = (uint64_t)t99;
                                                uint64_t t101 = gm_l12;
                                                uint64_t r102; bool o103 = __builtin_add_overflow(t100, t101, &r102);
                                                if (o103) gm_trap("integer overflow in `+`", gm_srchntnl6m6g3am, 94, 19);
                                                uint64_t t104 = r102;
                                                uint64_t t105 = gm_l4;
                                                if (t105 == 0) gm_trap("division by zero", gm_srchntnl6m6g3am, 94, 18);
                                                uint64_t t106 = (uint64_t)(t104 % t105);
                                                gm_l13 = t106;
                                                void * t107 = gm_l2;
                                                void * t108 = GM_LD(void *, ((uint8_t *)t107));
                                                uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)t107) + 8));
                                                uint64_t t110 = gm_l13;
                                                if ((uintptr_t)t110 >= (uintptr_t)(t109)) gm_trap_index((uintptr_t)t110, t109, gm_srchntnl6m6g3am, 95, 19);
                                                void * t111 = (void *)(((uint8_t *)t108) + (uintptr_t)t110 * 48);
                                                gm_l14 = t111;
                                                void * t112 = gm_l0;
                                                void * t113 = gm_l14;
                                                gm_tf5srh4mp4b3i t114 = gm_l5;
                                                uint8_t t115 = gm_f_runnable_o1q905g(t112, t113, t114);
                                                uint8_t t116 = (uint8_t)!t115;
                                                if (t116) {
                                                    goto gm_lb19;
                                                }
                                                {
                                                    void * t117 = gm_l14;
                                                    gm_tdkjirf1g83ud t118;
                                                    GM_ST(uint8_t, (uint8_t *)&t118 + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t118 + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t118 + 4, 0);
                                                    GM_ST(uint64_t, (uint8_t *)&t118 + 8, 0);
                                                    GM_ST(uint64_t, (uint8_t *)&t118 + 16, 0);
                                                    GM_ST(uint64_t, (uint8_t *)&t118 + 24, 0);
                                                    GM_ST(uint8_t, ((uint8_t *)&t118 + 0), ((uint8_t)0ULL));
                                                    GM_ST(gm_tdkjirf1g83ud, (((uint8_t *)t117) + 8), t118);
                                                }
                                                uint64_t t119 = gm_l13;
                                                uint32_t t120 = gm_l3;
                                                uint64_t t121 = (uint64_t)t120;
                                                uint8_t t122 = ((t119 == t121));
                                                if (t122) {
                                                    void * t123 = gm_l0;
                                                    gm_f_check_fatal_dgfalcm((void *)&(*(gm_tcfa6l7reoosn *)gm_out), t123);
                                                    goto gm_fdone;
                                                }
                                                void * t124 = gm_l2;
                                                uint64_t t125 = gm_l13;
                                                uint32_t t126 = (uint32_t)t125;
                                                GM_ST(uint32_t, (((uint8_t *)t124) + 40), t126);
                                                {
                                                    void * t127 = gm_l0;
                                                    void * t128 = gm_l14;
                                                    uint32_t t129 = GM_LD(uint32_t, ((uint8_t *)t128));
                                                    gm_t9hl37efuvhj8 t130;
                                                    memset(&t130, 0, sizeof t130);
                                                    GM_ST(uint8_t, ((uint8_t *)&t130 + 0), ((uint8_t)0ULL));
                                                    gm_t9hl37efuvhj8 t131;
                                                    gm_f_handoff_mtsh8r2((void *)&t131, (void *)NULL, cx, NULL, t127, t129, &t130);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                }
                                                void * t132 = gm_l0;
                                                uint32_t t133 = gm_l3;
                                                GM_ST(uint32_t, ((((uint8_t *)t132) + 1672) + 40), t133);
                                                {
                                                    void * t134 = gm_l0;
                                                    void * t135 = GM_LD(void *, (((uint8_t *)t134) + 1672));
                                                    uint64_t t136 = GM_LD(uint64_t, ((((uint8_t *)t134) + 1672) + 8));
                                                    uint32_t t137 = gm_l3;
                                                    uint64_t t138 = (uint64_t)t137;
                                                    if ((uintptr_t)t138 >= (uintptr_t)(t136)) gm_trap_index((uintptr_t)t138, t136, gm_srchntnl6m6g3am, 102, 16);
                                                    uint8_t t139 = GM_LD(uint8_t, (((((uint8_t *)t135) + (uintptr_t)t138 * 48) + 8) + 0));
                                                    uint8_t t140 = (uint8_t)((t139 == ((uint8_t)0ULL)));
                                                    gm_tdkjirf1g83ud t141 = GM_LD(gm_tdkjirf1g83ud, ((((uint8_t *)t135) + (uintptr_t)t138 * 48) + 8));
                                                    gm_tdkjirf1g83ud t142;
                                                    GM_ST(uint8_t, (uint8_t *)&t142 + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t142 + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t142 + 4, 0);
                                                    GM_ST(uint64_t, (uint8_t *)&t142 + 8, 0);
                                                    GM_ST(uint64_t, (uint8_t *)&t142 + 16, 0);
                                                    GM_ST(uint64_t, (uint8_t *)&t142 + 24, 0);
                                                    GM_ST(uint8_t, ((uint8_t *)&t142 + 0), ((uint8_t)0ULL));
                                                    if (t140) {
                                                        void * t143 = gm_l0;
                                                        gm_f_check_fatal_dgfalcm((void *)&(*(gm_tcfa6l7reoosn *)gm_out), t143);
                                                        goto gm_fdone;
                                                    }
                                                }
                                                goto gm_lb17;
                                            }
gm_lb21:;
                                            gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 93, 9);
                                        }
gm_lb20:;
                                    }
gm_lb19:;
                                    uint64_t t144 = gm_l11;
                                    uint64_t r145; bool o146 = __builtin_add_overflow(t144, ((uint64_t)1ULL), &r145);
                                    uint64_t t147 = r145;
                                    gm_l11 = t147;
                                }
                            }
gm_lb17:;
                        }
                        {
                            void * t148 = gm_l0;
                            void * t149 = gm_l2;
                            void * t150 = GM_LD(void *, ((uint8_t *)t149));
                            uint64_t t151 = GM_LD(uint64_t, (((uint8_t *)t149) + 8));
                            uint32_t t152 = gm_l3;
                            uint64_t t153 = (uint64_t)t152;
                            if ((uintptr_t)t153 >= (uintptr_t)(t151)) gm_trap_index((uintptr_t)t153, t151, gm_srchntnl6m6g3am, 106, 24);
                            void * t154 = (void *)(((uint8_t *)t150) + (uintptr_t)t153 * 48);
                            gm_tf5srh4mp4b3i t155 = gm_l5;
                            uint8_t t156 = gm_f_runnable_o1q905g(t148, t154, t155);
                            if (t156) {
                                goto gm_lb3;
                            }
                            gm_tf5srh4mp4b3i t157;
                            memset(&t157, 0, sizeof t157);
                            gm_l15 = t157;
                            void * t158 = gm_l2;
                            gm_b16_8 t159 = GM_LD(gm_b16_8, ((uint8_t *)t158));
                            gm_l16 = t159;
                            gm_l17 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t160 = gm_l17;
                                    uint64_t t161 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                                    uint8_t t162 = (t160 < t161);
                                    if (!t162) {
                                        goto gm_lb22;
                                    }
                                    {
                                        void * t163 = GM_LD(void *, ((uint8_t *)&gm_l16));
                                        uint64_t t164 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                                        uint64_t t165 = gm_l17;
                                        {
                                            {
                                                memmove(((uint8_t *)&F.l18), (((uint8_t *)t163) + (uintptr_t)t165 * 48), 48);
                                                {
                                                    {
                                                        uint8_t t166 = GM_LD(uint8_t, ((((uint8_t *)&F.l18) + 8) + 0));
                                                        uint8_t t167 = (uint8_t)(t166 == ((uint8_t)2ULL));
                                                        if (!t167) {
                                                            goto gm_lb28;
                                                        }
                                                        gm_tf5srh4mp4b3i t168 = GM_LD(gm_tf5srh4mp4b3i, (((((uint8_t *)&F.l18) + 8) + 8) + 8));
                                                        gm_l19 = t168;
                                                        {
                                                            {
                                                                uint8_t t169 = (uint8_t)((((uint8_t *)&gm_l19))[8] != 0);
                                                                if (t169) {
                                                                    goto gm_lb30;
                                                                }
                                                                gm_l34 = ((uint8_t)0);
                                                                goto gm_lb29;
                                                            }
gm_lb30:;
                                                            {
                                                                gm_l34 = ((uint8_t)1);
                                                                goto gm_lb29;
                                                            }
gm_lb31:;
                                                            gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 110, 45);
                                                        }
gm_lb29:;
                                                        uint8_t t170 = gm_l34;
                                                        if (!t170) {
                                                            goto gm_lb28;
                                                        }
                                                        {
                                                            {
                                                                uint8_t t171 = (uint8_t)((((uint8_t *)&gm_l15))[8] != 0);
                                                                if (!t171) {
                                                                    goto gm_lb33;
                                                                }
                                                                uint64_t t172 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
                                                                gm_l20 = t172;
                                                                uint64_t t173 = gm_l20;
                                                                uint8_t t174 = (uint8_t)((((uint8_t *)&gm_l19))[8] != 0);
                                                                if (t174) {
                                                                    uint64_t t175 = GM_LD(uint64_t, ((uint8_t *)&gm_l19));
                                                                    gm_l35 = t175;
                                                                } else {
                                                                    gm_l35 = ((uint64_t)0ULL);
                                                                }
                                                                uint64_t t176 = gm_l35;
                                                                uint8_t t177 = (t173 <= t176);
                                                                if (!t177) {
                                                                    goto gm_lb33;
                                                                }
                                                                goto gm_lb32;
                                                            }
gm_lb33:;
                                                            gm_tf5srh4mp4b3i t178 = gm_l19;
                                                            gm_l15 = t178;
                                                        }
gm_lb32:;
                                                        goto gm_lb27;
                                                    }
gm_lb28:;
                                                }
gm_lb27:;
                                                {
                                                    {
                                                        uint8_t t179 = GM_LD(uint8_t, ((((uint8_t *)&F.l18) + 8) + 0));
                                                        uint8_t t180 = (uint8_t)(t179 == ((uint8_t)3ULL));
                                                        if (!t180) {
                                                            goto gm_lb35;
                                                        }
                                                        uint64_t t181 = GM_LD(uint64_t, ((((uint8_t *)&F.l18) + 8) + 8));
                                                        gm_l21 = t181;
                                                        {
                                                            {
                                                                uint8_t t182 = (uint8_t)((((uint8_t *)&gm_l15))[8] != 0);
                                                                if (!t182) {
                                                                    goto gm_lb37;
                                                                }
                                                                uint64_t t183 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
                                                                gm_l22 = t183;
                                                                uint64_t t184 = gm_l22;
                                                                uint64_t t185 = gm_l21;
                                                                uint8_t t186 = (t184 <= t185);
                                                                if (!t186) {
                                                                    goto gm_lb37;
                                                                }
                                                                goto gm_lb36;
                                                            }
gm_lb37:;
                                                            uint64_t t187 = gm_l21;
                                                            gm_tf5srh4mp4b3i t188;
                                                            GM_ST(uint8_t, (uint8_t *)&t188 + 9, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&t188 + 10, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&t188 + 12, 0);
                                                            GM_ST(uint64_t, ((uint8_t *)&t188), t187);
                                                            ((uint8_t *)&t188)[8] = 1;
                                                            gm_l15 = t188;
                                                        }
gm_lb36:;
                                                        goto gm_lb34;
                                                    }
gm_lb35:;
                                                }
gm_lb34:;
                                                goto gm_lb25;
                                            }
gm_lb26:;
                                            gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 109, 9);
                                        }
gm_lb25:;
                                    }
gm_lb24:;
                                    uint64_t t189 = gm_l17;
                                    uint64_t r190; bool o191 = __builtin_add_overflow(t189, ((uint64_t)1ULL), &r190);
                                    uint64_t t192 = r190;
                                    gm_l17 = t192;
                                }
                            }
gm_lb22:;
                        }
                        {
                            void * t193 = gm_l2;
                            gm_b16_8 t194 = GM_LD(gm_b16_8, ((uint8_t *)t193));
                            gm_l23 = t194;
                            gm_l24 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t195 = gm_l24;
                                    uint64_t t196 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                                    uint8_t t197 = (t195 < t196);
                                    if (!t197) {
                                        goto gm_lb38;
                                    }
                                    {
                                        void * t198 = GM_LD(void *, ((uint8_t *)&gm_l23));
                                        uint64_t t199 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                                        uint64_t t200 = gm_l24;
                                        {
                                            {
                                                memmove(((uint8_t *)&F.l25), (((uint8_t *)t198) + (uintptr_t)t200 * 48), 48);
                                                {
                                                    {
                                                        uint8_t t201 = GM_LD(uint8_t, ((((uint8_t *)&F.l25) + 8) + 0));
                                                        uint8_t t202 = (uint8_t)(t201 == ((uint8_t)4ULL));
                                                        if (!t202) {
                                                            goto gm_lb44;
                                                        }
                                                        gm_l36 = ((uint8_t)1);
                                                        goto gm_lb43;
                                                    }
gm_lb44:;
                                                    {
                                                        gm_l36 = ((uint8_t)0);
                                                        goto gm_lb43;
                                                    }
gm_lb45:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 120, 16);
                                                }
gm_lb43:;
                                                uint8_t t203 = gm_l36;
                                                if (t203) {
                                                    uint8_t t204 = (uint8_t)((((uint8_t *)&gm_l5))[8] != 0);
                                                    if (t204) {
                                                        uint64_t t205 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
                                                        gm_l37 = t205;
                                                    } else {
                                                        gm_node *pn206 = gm_find(cx, 1579066750);
                                                        if (!pn206) gm_trap("no handler installed for effect `Clock`", gm_srchntnl6m6g3am, 121, 35);
                                                        gm_t4ce0jd3tdro4 t207;
                                                        t207 = ((gm_t4ce0jd3tdro4 (*)(void *, gm_node *, gm_node *))pn206->clauses[0])(pn206->env, pn206->hctx, pn206);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_l38 = t207;
                                                        uint64_t t208 = GM_LD(uint64_t, ((uint8_t *)&gm_l38));
                                                        gm_l37 = t208;
                                                    }
                                                    uint64_t t209 = gm_l37;
                                                    uint64_t r210; bool o211 = __builtin_add_overflow(t209, ((uint64_t)1000000ULL), &r210);
                                                    uint64_t t212 = o211 ? (1 ? ((uint64_t)18446744073709551615ULL) : ((uint64_t)0ULL)) : r210;
                                                    gm_l26 = t212;
                                                    {
                                                        {
                                                            uint8_t t213 = (uint8_t)((((uint8_t *)&gm_l15))[8] != 0);
                                                            if (!t213) {
                                                                goto gm_lb47;
                                                            }
                                                            uint64_t t214 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
                                                            gm_l27 = t214;
                                                            uint64_t t215 = gm_l27;
                                                            uint64_t t216 = gm_l26;
                                                            uint8_t t217 = (t215 <= t216);
                                                            if (!t217) {
                                                                goto gm_lb47;
                                                            }
                                                            goto gm_lb46;
                                                        }
gm_lb47:;
                                                        uint64_t t218 = gm_l26;
                                                        gm_tf5srh4mp4b3i t219;
                                                        GM_ST(uint8_t, (uint8_t *)&t219 + 9, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t219 + 10, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&t219 + 12, 0);
                                                        GM_ST(uint64_t, ((uint8_t *)&t219), t218);
                                                        ((uint8_t *)&t219)[8] = 1;
                                                        gm_l15 = t219;
                                                    }
gm_lb46:;
                                                }
                                                goto gm_lb41;
                                            }
gm_lb42:;
                                            gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 119, 9);
                                        }
gm_lb41:;
                                    }
gm_lb40:;
                                    uint64_t t220 = gm_l24;
                                    uint64_t r221; bool o222 = __builtin_add_overflow(t220, ((uint64_t)1ULL), &r221);
                                    uint64_t t223 = r221;
                                    gm_l24 = t223;
                                }
                            }
gm_lb38:;
                            uint8_t t224 = (uint8_t)((((uint8_t *)&gm_l15))[8] != 0);
                            if (t224) {
                                uint64_t t225 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
                                gm_l28 = t225;
                            } else {
                                void * t226 = gm_l0;
                                gm_tl874t2atnlh3 t227 = gm_l1;
                                gm_b16_8 t228;
                                GM_ST(void *, (uint8_t *)&t228, (void *)(gm_sb5pfucassrk8));
                                GM_ST(uintptr_t, (uint8_t *)&t228 + 8, (uintptr_t)(33));
                                gm_t76123gfet73g t229;
                                gm_f_trap_qh84l9d((void *)&t229, (void *)NULL, cx, NULL, t226, t227, t228);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t229);
                                goto gm_fdone;
                            }
                        }
                        {
                            uint8_t t230 = (uint8_t)((((uint8_t *)&gm_l5))[8] != 0);
                            if (t230) {
                                uint64_t t231 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
                                gm_l29 = t231;
                            } else {
                                gm_node *pn232 = gm_find(cx, 1579066750);
                                if (!pn232) gm_trap("no handler installed for effect `Clock`", gm_srchntnl6m6g3am, 127, 23);
                                gm_t4ce0jd3tdro4 t233;
                                t233 = ((gm_t4ce0jd3tdro4 (*)(void *, gm_node *, gm_node *))pn232->clauses[0])(pn232->env, pn232->hctx, pn232);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l39 = t233;
                                uint64_t t234 = GM_LD(uint64_t, ((uint8_t *)&gm_l39));
                                gm_l29 = t234;
                            }
                            uint64_t t235 = gm_l28;
                            uint64_t t236 = gm_l29;
                            uint8_t t237 = (t235 > t236);
                            if (t237) {
                                uint64_t t238 = gm_l28;
                                uint64_t t239 = gm_l29;
                                uint64_t r240; bool o241 = __builtin_sub_overflow(t238, t239, &r240);
                                if (o241) gm_trap("integer overflow in `-`", gm_srchntnl6m6g3am, 128, 45);
                                uint64_t t242 = r240;
                                gm_t4ce0jd3tdro4 t243;
                                GM_ST(uint64_t, ((uint8_t *)&t243 + 0), t242);
                                gm_node *pn244 = gm_find(cx, 1579066750);
                                if (!pn244) gm_trap("no handler installed for effect `Clock`", gm_srchntnl6m6g3am, 128, 20);
                                gm_b0_1 t245;
                                t245 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_t4ce0jd3tdro4))pn244->clauses[2])(pn244->env, pn244->hctx, pn244, t243);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                        }
                    }
gm_lb3:;
                }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_thread_main_n6r943c(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_thread_main_n6r943c F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* f */
    gm_b16_8 gm_l2; /* arg */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_tpmdr2hmdhsqr gm_l4; /* args */
    void * gm_l5; /* def */
    gm_b16_8 gm_l6; /* ret */
    gm_t76123gfet73g gm_l7; /* fl */
    uint8_t gm_l8; /* c */
    void * gm_l9; /* x */
    void * gm_l10; /* t */
    GM_STACK_CHECK(&F, 662u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tcfa6l7reoosn gm_l11;
    uint8_t gm_l12;
    uint8_t gm_l13;
    gm_tq7s98ihonkju gm_l14;
    gm_b16_8 t12 = gm_l2;
    gm_tpmdr2hmdhsqr t13;
    GM_ST(gm_b16_8, ((uint8_t *)&t13 + 0), t12);
    gm_l4 = t13;
    void * t14 = gm_l0;
    uint32_t t15 = gm_l1;
    void * t16 = gm_f_func_ene90ss((void *)NULL, cx, NULL, t14, t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t16;
    void * t17 = gm_l0;
    void * t18 = gm_l0;
    void * t19 = gm_l5;
    gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t19) + 64));
    uint64_t t21 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t18, t20);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t22 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t17, t21);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l6 = t22;
    {
        void * t23 = gm_l0;
        gm_tl874t2atnlh3 t24 = gm_l3;
        uint32_t t25 = gm_l1;
        void * t26 = (void *)((uint8_t *)&gm_l4);
        uint64_t t27 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t26 + (uintptr_t)((uint64_t)0ULL) * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
        gm_t5lf9i3n1r3co t30;
        memset(&t30, 0, sizeof t30);
        gm_b16_8 t31 = gm_l6;
        gm_f_call_function_u7ovth6((void *)&gm_l11, (void *)NULL, cx, NULL, t23, t24, t25, t29, ((void *)0), ((uint32_t)0ULL), &t30, ((uint8_t)0), t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
    if (t32) {
    } else {
        memmove(((uint8_t *)&gm_l7), (((uint8_t *)&gm_l11) + 8), 24);
        {
            {
                uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                uint8_t t34 = (uint8_t)(t33 == ((uint8_t)3ULL));
                if (!t34) {
                    goto gm_lb2;
                }
                uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 8));
                gm_l8 = t35;
                void * t36 = gm_l0;
                {
                    {
                        uint8_t t37 = (uint8_t)((((((uint8_t *)t36) + 1672) + 64))[1] != 0);
                        if (t37) {
                            goto gm_lb4;
                        }
                        gm_l12 = ((uint8_t)1);
                        goto gm_lb3;
                    }
gm_lb4:;
                    {
                        gm_l12 = ((uint8_t)0);
                        goto gm_lb3;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 215, 16);
                }
gm_lb3:;
                uint8_t t38 = gm_l12;
                if (t38) {
                    void * t39 = gm_l0;
                    uint8_t t40 = gm_l8;
                    gm_tqs5p4n2tq19v t41;
                    GM_ST(uint8_t, ((uint8_t *)&t41), t40);
                    ((uint8_t *)&t41)[1] = 1;
                    GM_ST(gm_tqs5p4n2tq19v, ((((uint8_t *)t39) + 1672) + 64), t41);
                }
                goto gm_lb1;
            }
gm_lb2:;
            void * t42 = gm_l0;
            {
                {
                    uint8_t t43 = (uint8_t)(GM_LD(void *, ((((uint8_t *)t42) + 1672) + 56)) != 0);
                    if (t43) {
                        goto gm_lb7;
                    }
                    gm_l13 = ((uint8_t)1);
                    goto gm_lb6;
                }
gm_lb7:;
                {
                    gm_l13 = ((uint8_t)0);
                    goto gm_lb6;
                }
gm_lb8:;
                gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 216, 19);
            }
gm_lb6:;
            uint8_t t44 = gm_l13;
            if (t44) {
                {
                    {
                        uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                        uint8_t t46 = (uint8_t)(t45 == ((uint8_t)4ULL));
                        if (!t46) {
                            goto gm_lb10;
                        }
                        void * t47 = GM_LD(void *, (((uint8_t *)&gm_l7) + 8));
                        gm_l9 = t47;
                        void * t48 = gm_l9;
                        gm_l10 = t48;
                        goto gm_lb9;
                    }
gm_lb10:;
                    {
                        gm_b16_8 t49;
                        GM_ST(void *, (uint8_t *)&t49, (void *)(gm_s632oftf9nssu));
                        GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(42));
                        gm_tl874t2atnlh3 t50 = gm_l3;
                        gm_trqfvmjtt4sij t51;
                        gm_f_list_j8jr9cr_2((void *)&t51, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tolm6p7jp1hb3 t52;
                        GM_ST(uint32_t, (uint8_t *)&t52 + 28, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t52 + 0), t49);
                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t52 + 16), t50);
                        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t52 + 32), t51);
                        gm_tq7s98ihonkju t53 = gm_f_new_jd11mvt((void *)NULL, cx, NULL, &t52, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k6kfar4qbkb)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l14 = t53;
                    }
                    uint8_t t54 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                    if (!t54) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_srchntnl6m6g3am, 217, 55);
                    void * t55 = GM_LD(void *, (((uint8_t *)&gm_l14) + 8));
                    gm_l10 = t55;
                }
gm_lb9:;
                void * t56 = gm_l0;
                void * t57 = gm_l10;
                GM_ST(void *, ((((uint8_t *)t56) + 1672) + 56), t57);
            }
        }
gm_lb1:;
    }
    uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint8_t t59 = (t58 > ((uint64_t)0ULL));
    if (t59) {
        gm_b16_8 t60 = gm_l2;
        gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
        gm_b0_1 t62 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t61, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kmp1n1vvhms)));
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
__attribute__((aligned(4))) void gm_f_thread_op_pfli7b1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_thread_op_pfli7b1 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t9qkh0rsooc8d gm_l2; /* op */
    gm_b16_8 gm_l3; /* vals */
    gm_b16_8 gm_l4; /* res */
    uint64_t gm_l5; /* addr */
    uint32_t gm_l6; /* f */
    gm_b16_8 gm_l7; /* arg */
    uint32_t gm_l8; /* id */
    uint32_t gm_l9; /* c */
    __int128 gm_l10; /* h */
    uint32_t gm_l11; /* t */
    __int128 gm_l12; /* h */
    uint64_t gm_l13; /* addr */
    gm_tpvg3mjn3dp06 gm_l14; /* word */
    unsigned __int128 gm_l15; /* expected */
    __int128 gm_l16; /* timeout */
    gm_tf5srh4mp4b3i gm_l17; /* deadline */
    GM_STACK_CHECK(&F, 3113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_t9805qtonbsag gm_l18;
    gm_tabb710d9mq2e gm_l19;
    gm_tabb710d9mq2e gm_l20;
    gm_tftb1odc739t5 gm_l21;
    uint8_t gm_l22;
    uint8_t gm_l23;
    gm_tcfa6l7reoosn gm_l24;
    gm_tcfa6l7reoosn gm_l25;
    uint8_t gm_l26;
    uint8_t gm_l27;
    gm_tcfa6l7reoosn gm_l28;
    gm_tcfa6l7reoosn gm_l29;
    gm_tcfa6l7reoosn gm_l30;
    gm_t4ce0jd3tdro4 gm_l31;
    gm_tcfa6l7reoosn gm_l32;
    {
        {
            {
                {
                    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t11 = (uint8_t)(t10 == ((uint8_t)47ULL));
                    if (!t11) {
                        goto gm_lb2;
                    }
                    void * t12 = gm_l0;
                    void * t13 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t14)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t14, gm_srchntnl6m6g3am, 149, 30);
                    gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)t13) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
                    uint64_t t17 = gm_f_ptr_of_b77b4c9(t12, t16);
                    gm_l5 = t17;
                    void * t18 = gm_l0;
                    uint64_t t19 = gm_l5;
                    gm_t9805qtonbsag t20 = gm_f_fn_from_addr_7a6bpqd(t18, t19);
                    gm_l18 = t20;
                    uint8_t t21 = (uint8_t)((((uint8_t *)&gm_l18))[4] != 0);
                    if (t21) {
                        uint32_t t22 = GM_LD(uint32_t, ((uint8_t *)&gm_l18));
                        gm_l6 = t22;
                    } else {
                        void * t23 = gm_l0;
                        gm_tl874t2atnlh3 t24 = gm_l1;
                        uint64_t t25 = gm_l5;
                        gm_t4ce0jd3tdro4 t26;
                        GM_ST(uint64_t, ((uint8_t *)&t26 + 0), t25);
                        gm_f_format_uh8vg1k((void *)&gm_l19, (void *)NULL, cx, NULL, t26);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                        if (!t27) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 8, gm_srchntnl6m6g3am, 150, 65);
                        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 8));
                        gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
                        gm_t76123gfet73g t30;
                        gm_f_trap_qh84l9d((void *)&t30, (void *)NULL, cx, NULL, t23, t24, t29);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t30);
                        goto gm_fdone;
                    }
                    {
                        void * t31 = GM_LD(void *, ((uint8_t *)&gm_l3));
                        uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t32)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t32, gm_srchntnl6m6g3am, 151, 29);
                        uint64_t t33 = GM_LD(uint64_t, ((((uint8_t *)t31) + (uintptr_t)((uint64_t)1ULL) * 16) + 8));
                        uint64_t t34 = (t33 > ((uint64_t)1ULL) ? t33 : ((uint64_t)1ULL));
                        gm_f_make_2ip78bj((void *)&gm_l20, (void *)NULL, cx, NULL, t34, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k3501um71nb)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t35 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                    if (!t35) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 8, gm_srchntnl6m6g3am, 151, 20);
                    gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
                    gm_l7 = t36;
                    void * t37 = GM_LD(void *, ((uint8_t *)&gm_l7));
                    uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    void * t39 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t40)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t40, gm_srchntnl6m6g3am, 152, 20);
                    uint64_t t41 = GM_LD(uint64_t, ((((uint8_t *)t39) + (uintptr_t)((uint64_t)1ULL) * 16) + 8));
                    uint64_t t42 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t43 = (uintptr_t)t41;
                    if (t42 > t43 || t43 > (uintptr_t)t38) gm_trap_slice(t42, t43, t38, gm_srchntnl6m6g3am, 152, 13);
                    uint64_t t44 = ((uintptr_t)t41) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t45;
                    GM_ST(void *, (uint8_t *)&t45, (void *)(((uint8_t *)t37 + (uintptr_t)((uint64_t)0ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(t44));
                    void * t46 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t47)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t47, gm_srchntnl6m6g3am, 152, 43);
                    gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)t46) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
                    gm_f_copy_from_f7j6p05(t45, t49);
                    void * t50 = gm_l0;
                    uint64_t t51 = GM_LD(uint64_t, ((((uint8_t *)t50) + 1672) + 8));
                    uint32_t t52 = (uint32_t)t51;
                    gm_l8 = t52;
                    {
                        void * t53 = gm_l0;
                        uint32_t t54 = gm_l6;
                        void * t55 = GM_LD(void *, ((uint8_t *)&gm_l7));
                        uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                        void * t57 = GM_LD(void *, ((uint8_t *)&gm_l3));
                        uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t58)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t58, gm_srchntnl6m6g3am, 154, 47);
                        uint64_t t59 = GM_LD(uint64_t, ((((uint8_t *)t57) + (uintptr_t)((uint64_t)1ULL) * 16) + 8));
                        uint64_t t60 = (uintptr_t)((uint64_t)0ULL);
                        uint64_t t61 = (uintptr_t)t59;
                        if (t60 > t61 || t61 > (uintptr_t)t56) gm_trap_slice(t60, t61, t56, gm_srchntnl6m6g3am, 154, 40);
                        uint64_t t62 = ((uintptr_t)t59) - (uintptr_t)((uint64_t)0ULL);
                        gm_b16_8 t63;
                        GM_ST(void *, (uint8_t *)&t63, (void *)(((uint8_t *)t55 + (uintptr_t)((uint64_t)0ULL) * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(t62));
                        gm_tl874t2atnlh3 t64 = gm_l1;
                        gm_t79rbdv4rafe5 t65;
                        GM_ST(uint32_t, (uint8_t *)&t65 + 4, 0);
                        GM_ST(uint32_t, (uint8_t *)&t65 + 36, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t65 + 0), t54);
                        GM_ST(gm_b16_8, ((uint8_t *)&t65 + 8), t63);
                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t65 + 24), t64);
                        uint32_t t66 = gm_l8;
                        gm_t8dl9beiqk70v t67;
                        GM_ST(uint8_t, (uint8_t *)&t67 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t67 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t67 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t67 + 48, 0);
                        GM_ST(uint64_t, (uint8_t *)&t67 + 56, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)2ULL));
                        GM_ST(gm_t79rbdv4rafe5, ((uint8_t *)&t67 + 8), t65);
                        uint32_t t68 = gm_f_new_coro_06nq12a((void *)NULL, cx, NULL, t53, &t67, t66);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l9 = t68;
                    }
                    {
                        void * t69 = gm_l0;
                        void * t70 = (void *)(((uint8_t *)t69) + 1672);
                        uint32_t t71 = gm_l9;
                        gm_tdkjirf1g83ud t72;
                        GM_ST(uint8_t, (uint8_t *)&t72 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t72 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t72 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t72 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t72 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t72 + 24, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t72 + 0), ((uint8_t)0ULL));
                        gm_t9jnd88rq7ul8 t73;
                        GM_ST(uint32_t, (uint8_t *)&t73 + 4, 0);
                        GM_ST(uint8_t, (uint8_t *)&t73 + 41, 0);
                        GM_ST(uint16_t, (uint8_t *)&t73 + 42, 0);
                        GM_ST(uint32_t, (uint8_t *)&t73 + 44, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t73 + 0), t71);
                        GM_ST(gm_tdkjirf1g83ud, ((uint8_t *)&t73 + 8), t72);
                        GM_ST(uint8_t, ((uint8_t *)&t73 + 40), ((uint8_t)0));
                        gm_tftb1odc739t5 t74 = gm_f_push_bn7ud1g(t70, &t73);
                        gm_l21 = t74;
                    }
                    uint8_t t75 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                    if (!t75) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srchntnl6m6g3am, 155, 13);
                    void * t76 = gm_l0;
                    gm_b16_8 t77 = gm_l4;
                    uint32_t t78 = gm_l8;
                    unsigned __int128 t79 = (unsigned __int128)t78;
                    gm_f_put_uint_cddin99(t76, t77, t79);
                    goto gm_lb1;
                }
gm_lb2:;
            }
            {
                {
                    uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t81 = (uint8_t)(t80 == ((uint8_t)48ULL));
                    if (!t81) {
                        goto gm_lb3;
                    }
                    void * t82 = gm_l0;
                    void * t83 = gm_l0;
                    void * t84 = GM_LD(void *, (((uint8_t *)t83) + 8));
                    void * t85 = (void *)(((uint8_t *)t84) + 8);
                    gm_t88rcucg1b7uk t86 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t85, ((uint8_t)1), ((uint16_t)64ULL));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t87 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t88)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t88, gm_srchntnl6m6g3am, 159, 51);
                    gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)t87) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&t89));
                    unsigned __int128 t91 = gm_f_get_int_pg7p4gc(t82, GM_CONV(gm_csguankko4lmhd, t86), t90);
                    __int128 t92;
                    memset(&t92, 0, sizeof t92); memcpy(&t92, ((uint8_t *)&t91), sizeof t92 < 16 ? sizeof t92 : 16);
                    gm_l10 = t92;
                    __int128 t93 = gm_l10;
                    uint8_t t94 = (t93 <= ((__int128)(((unsigned __int128)0ULL << 64) | 0ULL)));
                    if (t94) {
                        gm_l23 = ((uint8_t)1);
                    } else {
                        __int128 t95 = gm_l10;
                        void * t96 = gm_l0;
                        uint64_t t97 = GM_LD(uint64_t, ((((uint8_t *)t96) + 1672) + 8));
                        __int128 t98 = (__int128)t97;
                        uint8_t t99 = (t95 >= t98);
                        gm_l23 = t99;
                    }
                    uint8_t t100 = gm_l23;
                    if (t100) {
                        gm_l22 = ((uint8_t)1);
                    } else {
                        void * t101 = gm_l0;
                        void * t102 = GM_LD(void *, (((uint8_t *)t101) + 1672));
                        uint64_t t103 = GM_LD(uint64_t, ((((uint8_t *)t101) + 1672) + 8));
                        __int128 t104 = gm_l10;
                        uint32_t t105 = (uint32_t)t104;
                        uint64_t t106 = (uint64_t)t105;
                        if ((uintptr_t)t106 >= (uintptr_t)(t103)) gm_trap_index((uintptr_t)t106, t103, gm_srchntnl6m6g3am, 161, 65);
                        uint8_t t107 = GM_LD(uint8_t, ((((uint8_t *)t102) + (uintptr_t)t106 * 48) + 40));
                        gm_l22 = t107;
                    }
                    uint8_t t108 = gm_l22;
                    if (t108) {
                        void * t109 = gm_l0;
                        gm_b16_8 t110 = gm_l4;
                        unsigned __int128 r111; bool o112 = __builtin_sub_overflow(((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL)), ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1003ULL)), &r111);
                        unsigned __int128 t113 = r111;
                        gm_f_put_uint_cddin99(t109, t110, t113);
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
                    __int128 t114 = gm_l10;
                    uint32_t t115 = (uint32_t)t114;
                    gm_l11 = t115;
                    void * t116 = gm_l0;
                    void * t117 = GM_LD(void *, (((uint8_t *)t116) + 1672));
                    uint64_t t118 = GM_LD(uint64_t, ((((uint8_t *)t116) + 1672) + 8));
                    uint32_t t119 = gm_l11;
                    uint64_t t120 = (uint64_t)t119;
                    if ((uintptr_t)t120 >= (uintptr_t)(t118)) gm_trap_index((uintptr_t)t120, t118, gm_srchntnl6m6g3am, 166, 13);
                    GM_ST(uint8_t, ((((uint8_t *)t117) + (uintptr_t)t120 * 48) + 40), ((uint8_t)1));
                    {
                        void * t121 = gm_l0;
                        void * t122 = GM_LD(void *, (((uint8_t *)t121) + 1672));
                        uint64_t t123 = GM_LD(uint64_t, ((((uint8_t *)t121) + 1672) + 8));
                        uint32_t t124 = gm_l11;
                        uint64_t t125 = (uint64_t)t124;
                        if ((uintptr_t)t125 >= (uintptr_t)(t123)) gm_trap_index((uintptr_t)t125, t123, gm_srchntnl6m6g3am, 167, 16);
                        uint8_t t126 = GM_LD(uint8_t, (((((uint8_t *)t122) + (uintptr_t)t125 * 48) + 8) + 0));
                        uint8_t t127 = (uint8_t)(!(t126 == ((uint8_t)5ULL)));
                        gm_tdkjirf1g83ud t128 = GM_LD(gm_tdkjirf1g83ud, ((((uint8_t *)t122) + (uintptr_t)t125 * 48) + 8));
                        gm_tdkjirf1g83ud t129;
                        GM_ST(uint8_t, (uint8_t *)&t129 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t129 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t129 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t129 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t129 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t129 + 24, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t129 + 0), ((uint8_t)5ULL));
                        if (t127) {
                            {
                                void * t130 = gm_l0;
                                void * t131 = GM_LD(void *, (((uint8_t *)t130) + 1672));
                                uint64_t t132 = GM_LD(uint64_t, ((((uint8_t *)t130) + 1672) + 8));
                                void * t133 = gm_l0;
                                uint32_t t134 = GM_LD(uint32_t, ((((uint8_t *)t133) + 1672) + 40));
                                uint64_t t135 = (uint64_t)t134;
                                if ((uintptr_t)t135 >= (uintptr_t)(t132)) gm_trap_index((uintptr_t)t135, t132, gm_srchntnl6m6g3am, 168, 17);
                                uint32_t t136 = gm_l11;
                                gm_tdkjirf1g83ud t137;
                                GM_ST(uint8_t, (uint8_t *)&t137 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t137 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t137 + 4, 0);
                                GM_ST(uint32_t, (uint8_t *)&t137 + 12, 0);
                                GM_ST(uint64_t, (uint8_t *)&t137 + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&t137 + 24, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t137 + 0), ((uint8_t)1ULL));
                                GM_ST(uint32_t, ((uint8_t *)&t137 + 8), t136);
                                GM_ST(gm_tdkjirf1g83ud, ((((uint8_t *)t131) + (uintptr_t)t135 * 48) + 8), t137);
                            }
                            {
                                void * t138 = gm_l0;
                                gm_tl874t2atnlh3 t139 = gm_l1;
                                gm_f_switch_thread_m7aoqko((void *)&gm_l24, (void *)NULL, cx, NULL, t138, t139);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            uint8_t t140 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                            if (t140) {
                            } else {
                                gm_t76123gfet73g t141 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l24) + 8));
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t141);
                                goto gm_fdone;
                            }
                        }
                    }
                    {
                        void * t142 = gm_l0;
                        gm_f_check_fatal_dgfalcm((void *)&gm_l25, t142);
                    }
                    uint8_t t143 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                    if (t143) {
                    } else {
                        gm_t76123gfet73g t144 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l25) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t144);
                        goto gm_fdone;
                    }
                    void * t145 = gm_l0;
                    gm_b16_8 t146 = gm_l4;
                    gm_f_put_uint_cddin99(t145, t146, ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL)));
                    goto gm_lb1;
                }
gm_lb3:;
            }
            {
                {
                    uint8_t t147 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t148 = (uint8_t)(t147 == ((uint8_t)49ULL));
                    if (!t148) {
                        goto gm_lb4;
                    }
                    void * t149 = gm_l0;
                    void * t150 = gm_l0;
                    void * t151 = GM_LD(void *, (((uint8_t *)t150) + 8));
                    void * t152 = (void *)(((uint8_t *)t151) + 8);
                    gm_t88rcucg1b7uk t153 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t152, ((uint8_t)1), ((uint16_t)64ULL));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t154 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t155 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t155)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t155, gm_srchntnl6m6g3am, 175, 51);
                    gm_b16_8 t156 = GM_LD(gm_b16_8, (((uint8_t *)t154) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t157 = GM_LD(gm_b16_8, ((uint8_t *)&t156));
                    unsigned __int128 t158 = gm_f_get_int_pg7p4gc(t149, GM_CONV(gm_csguankko4lmhd, t153), t157);
                    __int128 t159;
                    memset(&t159, 0, sizeof t159); memcpy(&t159, ((uint8_t *)&t158), sizeof t159 < 16 ? sizeof t159 : 16);
                    gm_l12 = t159;
                    __int128 t160 = gm_l12;
                    uint8_t t161 = (t160 <= ((__int128)(((unsigned __int128)0ULL << 64) | 0ULL)));
                    if (t161) {
                        gm_l27 = ((uint8_t)1);
                    } else {
                        __int128 t162 = gm_l12;
                        void * t163 = gm_l0;
                        uint64_t t164 = GM_LD(uint64_t, ((((uint8_t *)t163) + 1672) + 8));
                        __int128 t165 = (__int128)t164;
                        uint8_t t166 = (t162 >= t165);
                        gm_l27 = t166;
                    }
                    uint8_t t167 = gm_l27;
                    if (t167) {
                        gm_l26 = ((uint8_t)1);
                    } else {
                        void * t168 = gm_l0;
                        void * t169 = GM_LD(void *, (((uint8_t *)t168) + 1672));
                        uint64_t t170 = GM_LD(uint64_t, ((((uint8_t *)t168) + 1672) + 8));
                        __int128 t171 = gm_l12;
                        uint32_t t172 = (uint32_t)t171;
                        uint64_t t173 = (uint64_t)t172;
                        if ((uintptr_t)t173 >= (uintptr_t)(t170)) gm_trap_index((uintptr_t)t173, t170, gm_srchntnl6m6g3am, 176, 65);
                        uint8_t t174 = GM_LD(uint8_t, ((((uint8_t *)t169) + (uintptr_t)t173 * 48) + 40));
                        gm_l26 = t174;
                    }
                    uint8_t t175 = gm_l26;
                    if (t175) {
                        void * t176 = gm_l0;
                        gm_b16_8 t177 = gm_l4;
                        unsigned __int128 r178; bool o179 = __builtin_sub_overflow(((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL)), ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1003ULL)), &r178);
                        unsigned __int128 t180 = r178;
                        gm_f_put_uint_cddin99(t176, t177, t180);
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
                    void * t181 = gm_l0;
                    void * t182 = GM_LD(void *, (((uint8_t *)t181) + 1672));
                    uint64_t t183 = GM_LD(uint64_t, ((((uint8_t *)t181) + 1672) + 8));
                    __int128 t184 = gm_l12;
                    uint32_t t185 = (uint32_t)t184;
                    uint64_t t186 = (uint64_t)t185;
                    if ((uintptr_t)t186 >= (uintptr_t)(t183)) gm_trap_index((uintptr_t)t186, t183, gm_srchntnl6m6g3am, 180, 13);
                    GM_ST(uint8_t, ((((uint8_t *)t182) + (uintptr_t)t186 * 48) + 40), ((uint8_t)1));
                    void * t187 = gm_l0;
                    gm_b16_8 t188 = gm_l4;
                    gm_f_put_uint_cddin99(t187, t188, ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL)));
                    goto gm_lb1;
                }
gm_lb4:;
                {
                    uint8_t t189 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t190 = (uint8_t)(t189 == ((uint8_t)50ULL));
                    if (!t190) {
                        goto gm_lb5;
                    }
                    void * t191 = gm_l0;
                    gm_tl874t2atnlh3 t192 = gm_l1;
                    gm_f_switch_thread_m7aoqko((void *)&gm_l28, (void *)NULL, cx, NULL, t191, t192);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t193 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                    if (t193) {
                    } else {
                        gm_t76123gfet73g t194 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l28) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t194);
                        goto gm_fdone;
                    }
                    goto gm_lb1;
                }
gm_lb5:;
            }
            {
                {
                    uint8_t t195 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t196 = (uint8_t)(t195 == ((uint8_t)51ULL));
                    if (!t196) {
                        goto gm_lb6;
                    }
                    {
                        void * t197 = gm_l0;
                        gm_f_check_fatal_dgfalcm((void *)&gm_l29, t197);
                    }
                    uint8_t t198 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                    if (t198) {
                    } else {
                        gm_t76123gfet73g t199 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l29) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t199);
                        goto gm_fdone;
                    }
                    void * t200 = gm_l0;
                    void * t201 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t202 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t202)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t202, gm_srchntnl6m6g3am, 186, 30);
                    gm_b16_8 t203 = GM_LD(gm_b16_8, (((uint8_t *)t201) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t204 = GM_LD(gm_b16_8, ((uint8_t *)&t203));
                    uint64_t t205 = gm_f_ptr_of_b77b4c9(t200, t204);
                    gm_l13 = t205;
                    gm_tpvg3mjn3dp06 t206;
                    GM_ST(uint8_t, ((uint8_t *)&t206 + 0), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t206 + 1), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t206 + 2), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t206 + 3), ((uint8_t)0ULL));
                    gm_l14 = t206;
                    {
                        void * t207 = gm_l0;
                        gm_tl874t2atnlh3 t208 = gm_l1;
                        uint64_t t209 = gm_l13;
                        void * t210 = (void *)((uint8_t *)&gm_l14);
                        uint64_t t211 = ((uintptr_t)((uint64_t)4ULL)) - (uintptr_t)((uint64_t)0ULL);
                        gm_b16_8 t212;
                        GM_ST(void *, (uint8_t *)&t212, (void *)(((uint8_t *)t210 + (uintptr_t)((uint64_t)0ULL) * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t212 + 8, (uintptr_t)(t211));
                        gm_f_load_at_6u81jcc((void *)&gm_l30, (void *)NULL, cx, NULL, t207, t208, t209, ((uint64_t)4ULL), t212);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t213 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
                    if (t213) {
                    } else {
                        gm_t76123gfet73g t214 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l30) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t214);
                        goto gm_fdone;
                    }
                    void * t215 = gm_l0;
                    void * t216 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t217 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t217)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t217, gm_srchntnl6m6g3am, 189, 36);
                    gm_b16_8 t218 = GM_LD(gm_b16_8, (((uint8_t *)t216) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t219 = GM_LD(gm_b16_8, ((uint8_t *)&t218));
                    unsigned __int128 t220 = gm_f_get_uint_bq8c4b7(t215, t219);
                    unsigned __int128 t221 = (unsigned __int128)(t220 & ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 4294967295ULL)));
                    gm_l15 = t221;
                    void * t222 = gm_l0;
                    void * t223 = (void *)((uint8_t *)&gm_l14);
                    uint64_t t224 = ((uintptr_t)((uint64_t)4ULL)) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t225;
                    GM_ST(void *, (uint8_t *)&t225, (void *)(((uint8_t *)t223 + (uintptr_t)((uint64_t)0ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t225 + 8, (uintptr_t)(t224));
                    gm_b16_8 t226 = GM_LD(gm_b16_8, ((uint8_t *)&t225));
                    unsigned __int128 t227 = gm_f_get_uint_bq8c4b7(t222, t226);
                    unsigned __int128 t228 = gm_l15;
                    uint8_t t229 = ((t227 == t228));
                    if (t229) {
                        void * t230 = gm_l0;
                        void * t231 = gm_l0;
                        void * t232 = GM_LD(void *, (((uint8_t *)t231) + 8));
                        void * t233 = (void *)(((uint8_t *)t232) + 8);
                        gm_t88rcucg1b7uk t234 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t233, ((uint8_t)1), ((uint16_t)64ULL));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t235 = GM_LD(void *, ((uint8_t *)&gm_l3));
                        uint64_t t236 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                        if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t236)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t236, gm_srchntnl6m6g3am, 191, 61);
                        gm_b16_8 t237 = GM_LD(gm_b16_8, (((uint8_t *)t235) + (uintptr_t)((uint64_t)2ULL) * 16));
                        gm_b16_8 t238 = GM_LD(gm_b16_8, ((uint8_t *)&t237));
                        unsigned __int128 t239 = gm_f_get_int_pg7p4gc(t230, GM_CONV(gm_csguankko4lmhd, t234), t238);
                        __int128 t240;
                        memset(&t240, 0, sizeof t240); memcpy(&t240, ((uint8_t *)&t239), sizeof t240 < 16 ? sizeof t240 : 16);
                        gm_l16 = t240;
                        gm_tf5srh4mp4b3i t241;
                        memset(&t241, 0, sizeof t241);
                        gm_l17 = t241;
                        __int128 t242 = gm_l16;
                        uint8_t t243 = (t242 >= ((__int128)(((unsigned __int128)0ULL << 64) | 0ULL)));
                        if (t243) {
                            gm_node *pn244 = gm_find(cx, 1579066750);
                            if (!pn244) gm_trap("no handler installed for effect `Clock`", gm_srchntnl6m6g3am, 193, 46);
                            gm_t4ce0jd3tdro4 t245;
                            t245 = ((gm_t4ce0jd3tdro4 (*)(void *, gm_node *, gm_node *))pn244->clauses[0])(pn244->env, pn244->hctx, pn244);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l31 = t245;
                            uint64_t t246 = GM_LD(uint64_t, ((uint8_t *)&gm_l31));
                            __int128 t247 = gm_l16;
                            uint64_t t248 = (uint64_t)t247;
                            uint64_t r249; bool o250 = __builtin_add_overflow(t246, t248, &r249);
                            uint64_t t251 = o250 ? (1 ? ((uint64_t)18446744073709551615ULL) : ((uint64_t)0ULL)) : r249;
                            gm_tf5srh4mp4b3i t252;
                            GM_ST(uint8_t, (uint8_t *)&t252 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t252 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t252 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t252), t251);
                            ((uint8_t *)&t252)[8] = 1;
                            gm_l17 = t252;
                        }
                        {
                            void * t253 = gm_l0;
                            void * t254 = GM_LD(void *, (((uint8_t *)t253) + 1672));
                            uint64_t t255 = GM_LD(uint64_t, ((((uint8_t *)t253) + 1672) + 8));
                            void * t256 = gm_l0;
                            uint32_t t257 = GM_LD(uint32_t, ((((uint8_t *)t256) + 1672) + 40));
                            uint64_t t258 = (uint64_t)t257;
                            if ((uintptr_t)t258 >= (uintptr_t)(t255)) gm_trap_index((uintptr_t)t258, t255, gm_srchntnl6m6g3am, 194, 17);
                            void * t259 = gm_l0;
                            uint64_t t260 = GM_LD(uint64_t, ((((uint8_t *)t259) + 1672) + 48));
                            gm_tf5srh4mp4b3i t261 = gm_l17;
                            gm_tk4qs2jduhfn8 t262;
                            GM_ST(uint64_t, ((uint8_t *)&t262 + 0), t260);
                            GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t262 + 8), t261);
                            gm_tdkjirf1g83ud t263;
                            GM_ST(uint8_t, (uint8_t *)&t263 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t263 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t263 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t263 + 0), ((uint8_t)2ULL));
                            GM_ST(gm_tk4qs2jduhfn8, ((uint8_t *)&t263 + 8), t262);
                            GM_ST(gm_tdkjirf1g83ud, ((((uint8_t *)t254) + (uintptr_t)t258 * 48) + 8), t263);
                        }
                        {
                            void * t264 = gm_l0;
                            gm_tl874t2atnlh3 t265 = gm_l1;
                            gm_f_switch_thread_m7aoqko((void *)&gm_l32, (void *)NULL, cx, NULL, t264, t265);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t266 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                        if (t266) {
                        } else {
                            gm_t76123gfet73g t267 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l32) + 8));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t267);
                            goto gm_fdone;
                        }
                    }
                    void * t268 = gm_l0;
                    gm_b16_8 t269 = gm_l4;
                    gm_f_put_uint_cddin99(t268, t269, ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL)));
                    goto gm_lb1;
                }
gm_lb6:;
            }
            {
                {
                    uint8_t t270 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t271 = (uint8_t)(t270 == ((uint8_t)52ULL));
                    if (!t271) {
                        goto gm_lb7;
                    }
                    void * t272 = gm_l0;
                    void * t273 = gm_l0;
                    uint64_t t274 = GM_LD(uint64_t, ((((uint8_t *)t273) + 1672) + 48));
                    uint64_t r275; bool o276 = __builtin_add_overflow(t274, ((uint64_t)1ULL), &r275);
                    if (o276) gm_trap("integer overflow in `+`", gm_srchntnl6m6g3am, 201, 13);
                    uint64_t t277 = r275;
                    GM_ST(uint64_t, ((((uint8_t *)t272) + 1672) + 48), t277);
                    void * t278 = gm_l0;
                    gm_b16_8 t279 = gm_l4;
                    gm_f_put_uint_cddin99(t278, t279, ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL)));
                    goto gm_lb1;
                }
gm_lb7:;
                {
                    void * t280 = gm_l0;
                    gm_tl874t2atnlh3 t281 = gm_l1;
                    gm_b16_8 t282;
                    GM_ST(void *, (uint8_t *)&t282, (void *)(gm_svu0pqir618v4));
                    GM_ST(uintptr_t, (uint8_t *)&t282 + 8, (uintptr_t)(38));
                    gm_t76123gfet73g t283;
                    gm_f_trap_qh84l9d((void *)&t283, (void *)NULL, cx, NULL, t280, t281, t282);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t283);
                    goto gm_fdone;
                }
gm_lb8:;
                gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 147, 5);
            }
        }
gm_lb1:;
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
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_threads_kh5dldj(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_threads_kh5dldj F;
    GM_STACK_CHECK(&F, 175u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tftb1odc739t5 gm_l1;
    {
        gm_trqfvmjtt4sij t2;
        gm_f_list_jh4e1ji((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l0 = t2;
    }
    {
        void * t3 = (void *)((uint8_t *)&F.l0);
        gm_tdkjirf1g83ud t4;
        GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t4 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t4 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t4 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t4 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t4 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)0ULL));
        gm_t9jnd88rq7ul8 t5;
        GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t5 + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&t5 + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&t5 + 44, 0);
        GM_ST(uint32_t, ((uint8_t *)&t5 + 0), ((uint32_t)0ULL));
        GM_ST(gm_tdkjirf1g83ud, ((uint8_t *)&t5 + 8), t4);
        GM_ST(uint8_t, ((uint8_t *)&t5 + 40), ((uint8_t)0));
        gm_tftb1odc739t5 t6 = gm_f_push_bn7ud1g(t3, &t5);
        gm_l1 = t6;
    }
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
    if (!t7) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l1) + 1, gm_srchntnl6m6g3am, 55, 5);
    {
        gm_tqs5p4n2tq19v t8;
        memset(&t8, 0, sizeof t8);
        gm_trqfvmjtt4sij t9 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l0));
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tobnqifq2n0g5 *)gm_out) + 66, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tobnqifq2n0g5 *)gm_out) + 68, 0);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_tobnqifq2n0g5 *)gm_out) + 0), t9);
        GM_ST(uint32_t, ((uint8_t *)&(*(gm_tobnqifq2n0g5 *)gm_out) + 40), ((uint32_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&(*(gm_tobnqifq2n0g5 *)gm_out) + 44), ((uint32_t)0ULL));
        GM_ST(uint64_t, ((uint8_t *)&(*(gm_tobnqifq2n0g5 *)gm_out) + 48), ((uint64_t)0ULL));
        GM_ST(void *, ((uint8_t *)&(*(gm_tobnqifq2n0g5 *)gm_out) + 56), ((void *)0));
        GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&(*(gm_tobnqifq2n0g5 *)gm_out) + 64), t8);
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
inline __attribute__((aligned(4))) void gm_f_tick_6bsfu3n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_tick_6bsfu3n F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* th */
    GM_STACK_CHECK(&F, 187u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tcfa6l7reoosn gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 1672);
    gm_l2 = t3;
    void * t4 = gm_l2;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 8));
    uint8_t t6 = ((t5 == ((uint64_t)1ULL)));
    if (t6) {
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
    void * t7 = gm_l2;
    void * t8 = gm_l2;
    uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)t8) + 44));
    uint32_t r10; bool o11 = __builtin_add_overflow(t9, ((uint32_t)1ULL), &r10);
    if (o11) gm_trap("integer overflow in `+`", gm_srchntnl6m6g3am, 63, 5);
    uint32_t t12 = r10;
    GM_ST(uint32_t, (((uint8_t *)t7) + 44), t12);
    void * t13 = gm_l2;
    uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)t13) + 44));
    uint32_t t15 = GM_LD(uint32_t, ((uint8_t *)gm_cusfosftpfn));
    uint8_t t16 = (t14 < t15);
    if (t16) {
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
    void * t17 = gm_l2;
    GM_ST(uint32_t, (((uint8_t *)t17) + 44), ((uint32_t)0ULL));
    {
        void * t18 = gm_l0;
        gm_tl874t2atnlh3 t19 = gm_l1;
        gm_f_switch_thread_m7aoqko((void *)&gm_l3, (void *)NULL, cx, NULL, t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (t20) {
    } else {
        gm_t76123gfet73g t21 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l3) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t21);
        goto gm_fdone;
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
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}

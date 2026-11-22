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
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { int64_t f0; uint8_t f1; uint8_t pe[7]; } gm_t913bb735pvt5;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_t913bb735pvt5 f0; uint8_t f1; uint8_t pe[7]; } gm_t0id5pe5tq2j8;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_tl874t2atnlh3 f0; uint32_t f1; uint8_t f2; uint8_t pe[3]; } gm_t3u9hrtrhq8ch;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t913bb735pvt5) == 16 && _Alignof(gm_t913bb735pvt5) == 8 && offsetof(gm_t913bb735pvt5, f0) == 0 && offsetof(gm_t913bb735pvt5, f1) == 8, "layout of `?i64`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_b16_8 f3; uint8_t f4; uint8_t pe[7]; } gm_tg9l3k9j9m9b9;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t p3[3]; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tsoie1ppgdvcg;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
_Static_assert(sizeof(gm_t0id5pe5tq2j8) == 24 && _Alignof(gm_t0id5pe5tq2j8) == 8 && offsetof(gm_t0id5pe5tq2j8, f0) == 0 && offsetof(gm_t0id5pe5tq2j8, f1) == 16, "layout of `?Storage`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3u9hrtrhq8ch) == 20 && _Alignof(gm_t3u9hrtrhq8ch) == 4 && offsetof(gm_t3u9hrtrhq8ch, f0) == 0 && offsetof(gm_t3u9hrtrhq8ch, f1) == 12 && offsetof(gm_t3u9hrtrhq8ch, f2) == 16, "layout of `Held`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tg9l3k9j9m9b9) == 72 && _Alignof(gm_tg9l3k9j9m9b9) == 8 && offsetof(gm_tg9l3k9j9m9b9, f0) == 0 && offsetof(gm_tg9l3k9j9m9b9, f1) == 16 && offsetof(gm_tg9l3k9j9m9b9, f2) == 32 && offsetof(gm_tg9l3k9j9m9b9, f3) == 48 && offsetof(gm_tg9l3k9j9m9b9, f4) == 64, "layout of `Dangling`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { void * f0; void * f1; gm_trqfvmjtt4sij f2; int64_t f3; void * f4; gm_trqfvmjtt4sij f5; } gm_tqp3o9evff257;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tsoie1ppgdvcg) == 40 && _Alignof(gm_tsoie1ppgdvcg) == 8 && offsetof(gm_tsoie1ppgdvcg, f0) == 0 && offsetof(gm_tsoie1ppgdvcg, f1) == 16 && offsetof(gm_tsoie1ppgdvcg, f2) == 20 && offsetof(gm_tsoie1ppgdvcg, f3) == 24, "layout of `LocalDecl`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
typedef struct { uint64_t f0; gm_trcmmd12cefb1 f1; } gm_tfquseojpqg7b;
_Static_assert(sizeof(gm_tqp3o9evff257) == 112 && _Alignof(gm_tqp3o9evff257) == 8 && offsetof(gm_tqp3o9evff257, f0) == 0 && offsetof(gm_tqp3o9evff257, f1) == 8 && offsetof(gm_tqp3o9evff257, f2) == 16 && offsetof(gm_tqp3o9evff257, f3) == 56 && offsetof(gm_tqp3o9evff257, f4) == 64 && offsetof(gm_tqp3o9evff257, f5) == 72, "layout of `DWalk`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tfquseojpqg7b) == 40 && _Alignof(gm_tfquseojpqg7b) == 8 && offsetof(gm_tfquseojpqg7b, f0) == 0 && offsetof(gm_tfquseojpqg7b, f1) == 8, "layout of `(i: usize, x: Stmt)`");
struct gm_fr_add_dangling_notes_s4j764p {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_add_note_46v502s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_assigned_later_vf7s32b {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_check_dangling_addresses_9hre1eh {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* found */
    gm_trqfvmjtt4sij l7; /* depth */
    gm_tqp3o9evff257 l13; /* w */
    gm_tg9l3k9j9m9b9 l16; /* f */
    gm_t4gut2c9fj1kt l17; /* d */
    gm_b0_1 ret;
};
struct gm_fr_d_block_5tur90b {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_d_call_f4t084p {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* params */
    gm_t5v0c8ia6a5mo l8; /* p */
    gm_b0_1 ret;
};
struct gm_fr_d_check_store_ldgo1fg {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* held */
    gm_b0_1 ret;
};
struct gm_fr_d_cond_6aiv3jd {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_d_declare_nctg7jr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_d_declare_pat_53h71nd {
    gm_frame h;
    void * l0; /* w */
    gm_b0_1 ret;
};
struct gm_fr_d_expr_bbktnn5 {
    gm_frame h;
    void * l0; /* w */
    gm_t710lsrq1m8gk l15; /* x */
    gm_b0_1 ret;
};
struct gm_fr_d_held_nduio4j {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_d_report_fdjjedi {
    gm_frame h;
    gm_tsoie1ppgdvcg l5; /* local */
    gm_b0_1 ret;
};
struct gm_fr_d_root_a9vujp4 {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_d_stmt_a97l2r1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_d_stmts_frrm4kl {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_d_storage_kqhfs5d {
    gm_frame h;
    gm_t0id5pe5tq2j8 ret;
};
struct gm_fr_dangling_message_s0d8vok {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_emit_dph5v1e {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hnufvkq {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_i9dbui1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jbu9fo3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_k4gaogu {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_r81t436 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fn_35etm6d {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_450cirs {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_0jd2rfq {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_e0k7mdb {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ou6lg1k {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_usfrdin {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_0a7vhht {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7s4om10 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_hddcdrs {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nof7vfj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_v9rkbv6 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_possible_dangling_addres_d403vr0 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* found */
    gm_trqfvmjtt4sij l5; /* depth */
    gm_tqp3o9evff257 l11; /* w */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_push_0mk7udh {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_hbmq4bj {
    gm_frame h;
    gm_tg9l3k9j9m9b9 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_k3l9nic {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_kiclmcn {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_p826iq8 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_pqaml2p {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8skjmj4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_icfs4gf {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j4iu2lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_t74aoec {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_tk19nrs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_v0p8ic9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_same_place_fl3dc6t {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_dangling_notes_s4j764p(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_assigned_later_vf7s32b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_dangling_addresses_9hre1eh(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_d_block_5tur90b(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_d_call_f4t084p(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_d_check_store_ldgo1fg(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, int64_t a2, gm_b16_8 a3, uint8_t a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_d_cond_6aiv3jd(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_d_declare_nctg7jr(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_d_declare_pat_53h71nd(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_d_expr_bbktnn5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_d_held_nduio4j(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, void * a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_d_report_fdjjedi(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint32_t a2, gm_b16_8 a3, uint8_t a4);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_d_root_a9vujp4(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_d_stmt_a97l2r1(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_d_stmts_frrm4kl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_d_storage_kqhfs5d(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) gm_t0id5pe5tq2j8 gm_f_d_storage_kqhfs5d_c(void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_dangling_message_s0d8vok(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hnufvkq(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_i9dbui1(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_k4gaogu(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_35etm6d(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_450cirs(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_for_each_subpat_arlrtrm(void * a0, gm_closure a1);
inline __attribute__((aligned(4))) void gm_f_format_0jd2rfq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_e0k7mdb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_ou6lg1k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_usfrdin(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_0a7vhht(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_hddcdrs(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_v9rkbv6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_possible_dangling_addres_d403vr0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_hbmq4bj(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_p826iq8(void * a0, int64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pqaml2p(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_icfs4gf(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_t74aoec(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_tk19nrs(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_v0p8ic9(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_same_place_fl3dc6t(void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
extern uint8_t gm_c9paf88rqhe[8] __attribute__((aligned(8)));
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern uint8_t gm_cpn2np70vcf[8] __attribute__((aligned(8)));
extern uint8_t gm_cs1qbc2gaqs[16] __attribute__((aligned(8)));
extern const uint8_t gm_s1f440st4mnkp[2];
extern const uint8_t gm_s6hha9j1e16ru[90];
extern const uint8_t gm_s8gkio2134kd6[70];
extern const uint8_t gm_s8pjrp7ublsfs[171];
extern const uint8_t gm_sb6bik10m4ppo[74];
extern const uint8_t gm_scbkk1tper8o2[54];
extern const uint8_t gm_scd7cqpmpn0p9[78];
extern const uint8_t gm_sjssolp3qfpqg[7];
extern const uint8_t gm_skvqiv7no78c2[9];
extern const uint8_t gm_sp1h1hg79ru76[38];
extern const uint8_t gm_spj65mdvs9ei4[20];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcb2bqqsnukmrd[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srtkupsn5fo0o[123];
extern const uint8_t gm_ssj1mfqvo884a[62];
extern const uint8_t gm_stou2bl2ross2[70];
extern const gm_tmpl gm_tpl6lgf07g31gim;
extern const gm_tmpl gm_tpl88ij4e6nvk4u;
extern const gm_tmpl gm_tplcssa18c20t1h;
extern const gm_tmpl gm_tpllbgi055uoiek;
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
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_label_50fj7vv F;
    void * gm_l0; /* d */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 117u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 40);
    gm_tl874t2atnlh3 t4 = gm_l1;
    gm_b16_8 t5 = gm_l2;
    gm_tbgd14esf0jvi t6;
    GM_ST(uint32_t, (uint8_t *)&t6 + 12, 0);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t6 + 0), t4);
    GM_ST(gm_b16_8, ((uint8_t *)&t6 + 16), t5);
    gm_tftb1odc739t5 t7 = gm_f_push_kiclmcn(t3, &t6);
    gm_l3 = t7;
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srcgfglnv8p059r, 92, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_note_46v502s F;
    void * gm_l0; /* d */
    gm_b16_8 gm_l1; /* note */
    GM_STACK_CHECK(&F, 61u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l2;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 80);
    gm_b16_8 t4 = gm_l1;
    gm_tftb1odc739t5 t5 = gm_f_push_3ef746b(t3, t4);
    gm_l2 = t5;
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srcgfglnv8p059r, 96, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_d_declare_nctg7jr(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_d_declare_nctg7jr F;
    void * gm_l0; /* w */
    uint32_t gm_l1; /* l */
    GM_STACK_CHECK_SHALLOW(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 16));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 16) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_srcb2bqqsnukmrd, 107, 38);
    void * t7 = gm_l0;
    int64_t t8 = GM_LD(int64_t, (((uint8_t *)t7) + 56));
    GM_ST(int64_t, (((uint8_t *)t3) + (uintptr_t)t6 * 8), t8);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_d_declare_pat_53h71nd(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_d_declare_pat_53h71nd F;
    void * gm_l1; /* p */
    uint32_t gm_l2; /* l */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    void * t5 = gm_l1;
    {
        {
            uint8_t t6 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)1ULL));
            if (!t7) {
                goto gm_lb2;
            }
            uint32_t t8 = GM_LD(uint32_t, ((((uint8_t *)t5) + 8) + 8));
            gm_l2 = t8;
            void * t9 = F.l0;
            uint32_t t10 = gm_l2;
            gm_f_d_declare_nctg7jr(t9, t10);
            goto gm_lb1;
        }
gm_lb2:;
        {
            void * t11 = gm_l1;
            gm_closure t12 = ((gm_closure){(void *)gm_f_fn_35etm6d, (void *)&F.h, cx});
            gm_f_for_each_subpat_arlrtrm(t11, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 110, 5);
    }
gm_lb1:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_emit_dph5v1e F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 8));
    gm_f_push_o4ckdr1(t4, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l1)));
    F.ret = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hnufvkq(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hnufvkq F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_i9dbui1(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_i9dbui1 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jbu9fo3 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_k4gaogu(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_k4gaogu F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_r81t436 F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_35etm6d(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_35etm6d F;
    void * gm_l0; /* q */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_d_declare_pat_53h71nd *)((&F.h)->up))->l0));
    void * t3 = gm_l0;
    gm_f_d_declare_pat_53h71nd(t2, t3);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_450cirs(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_450cirs F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_d_expr_bbktnn5 *)((&F.h)->up))->l0));
    void * t3 = gm_l0;
    gm_b0_1 t4 = gm_f_d_expr_bbktnn5((void *)NULL, cx, NULL, t2, t3, ((uint8_t)0));
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
inline __attribute__((aligned(4))) void gm_f_format_0jd2rfq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_0jd2rfq F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sp1h1hg79ru76));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(37));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl88ij4e6nvk4u), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_e0k7mdb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_e0k7mdb F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sb6bik10m4ppo));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(73));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpllbgi055uoiek), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ou6lg1k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ou6lg1k F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_ssj1mfqvo884a));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(61));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl6lgf07g31gim), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_usfrdin(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_usfrdin F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_scbkk1tper8o2));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(53));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplcssa18c20t1h), t2);
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
inline __attribute__((aligned(4))) void gm_f_list_0a7vhht(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0a7vhht F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_k4gaogu();
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
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7s4om10 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_r81t436();
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
inline __attribute__((aligned(4))) void gm_f_list_hddcdrs(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_hddcdrs F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_hnufvkq();
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
inline __attribute__((aligned(4))) void gm_f_list_v9rkbv6(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_v9rkbv6 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_i9dbui1();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_0mk7udh F;
    void * gm_l0; /* l */
    gm_tl874t2atnlh3 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
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
        gm_b16_8 t9 = gm_f_empty_slice_r81t436();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_v0p8ic9(t8, ((uint64_t)1ULL), t10);
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
    gm_tl874t2atnlh3 t18 = gm_l1;
    GM_ST(gm_tl874t2atnlh3, (((uint8_t *)t16) + (uintptr_t)t17 * 12), t18);
    void * t19 = gm_l0;
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, ((uint8_t *)t20));
    uint64_t t22 = gm_l2;
    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
    uint64_t t25 = r23;
    uint64_t t26 = ((uintptr_t)t25) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t27;
    GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t21 + (uintptr_t)((uint64_t)0ULL) * 12)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_hbmq4bj(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_hbmq4bj F;
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
        gm_b16_8 t9 = gm_f_empty_slice_i9dbui1();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_icfs4gf(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_kiclmcn F;
    void * gm_l0; /* l */
    gm_tbgd14esf0jvi gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_jbu9fo3();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_8skjmj4(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_p826iq8(void * a0, int64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_p826iq8 F;
    void * gm_l0; /* l */
    int64_t gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_k4gaogu();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_tk19nrs(t8, ((uint64_t)1ULL), t10);
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
    int64_t t18 = gm_l1;
    GM_ST(int64_t, (((uint8_t *)t16) + (uintptr_t)t17 * 8), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pqaml2p(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_pqaml2p F;
    void * gm_l0; /* l */
    gm_t3u9hrtrhq8ch gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_hnufvkq();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_t74aoec(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 20), ((uint8_t *)a1), 20);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 20)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8skjmj4 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_icfs4gf(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_icfs4gf F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_t74aoec(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_t74aoec F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)20ULL), ((uint64_t)4ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_tk19nrs(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_tk19nrs F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_v0p8ic9(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_v0p8ic9 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)12ULL), ((uint64_t)4ULL));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_add_dangling_notes_s4j764p(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_add_dangling_notes_s4j764p F;
    void * gm_l0; /* d */
    gm_b16_8 gm_l1; /* name */
    gm_tl874t2atnlh3 gm_l2; /* local */
    GM_STACK_CHECK(&F, 262u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l3;
    gm_b16_8 t2 = gm_l1;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)gm_cs1qbc2gaqs));
    uint8_t t4 = (gm_eq0iqiubfm7e(((uint8_t *)&t2), ((uint8_t *)&t3)));
    if (t4) {
        void * t5 = gm_l0;
        gm_tl874t2atnlh3 t6 = gm_l2;
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(gm_s8gkio2134kd6));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(69));
        gm_f_add_label_50fj7vv(t5, GM_CONV(gm_cseahvg8v7p0vs, t6), t7);
        void * t8 = gm_l0;
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_srtkupsn5fo0o));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(122));
        gm_f_add_note_46v502s(t8, t9);
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t10 = gm_l0;
    gm_tl874t2atnlh3 t11 = gm_l2;
    gm_b16_8 t12 = gm_l1;
    gm_tnpifv20irnph t13;
    GM_ST(gm_b16_8, ((uint8_t *)&t13 + 0), t12);
    gm_f_format_0jd2rfq((void *)&gm_l3, (void *)NULL, cx, NULL, t13);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t14) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srcb2bqqsnukmrd, 55, 24);
    gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
    gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
    gm_f_add_label_50fj7vv(t10, GM_CONV(gm_cseahvg8v7p0vs, t11), t16);
    void * t17 = gm_l0;
    gm_b16_8 t18;
    GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s8pjrp7ublsfs));
    GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(170));
    gm_f_add_note_46v502s(t17, t18);
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
inline __attribute__((aligned(4))) uint8_t gm_f_assigned_later_vf7s32b(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_assigned_later_vf7s32b F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* stmts */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_trcmmd12cefb1 gm_l4; /* st */
    void * gm_l5; /* place */
    GM_STACK_CHECK(&F, 190u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t9 = gm_l1;
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
                void * t13 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t15 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&gm_l4), (((uint8_t *)t13) + (uintptr_t)t15 * 32), 32);
                        {
                            {
                                uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t17 = (uint8_t)(t16 == ((uint8_t)2ULL));
                                if (!t17) {
                                    goto gm_lb7;
                                }
                                void * t18 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                                gm_l5 = t18;
                                void * t19 = gm_l0;
                                void * t20 = gm_l5;
                                uint8_t t21 = gm_f_same_place_fl3dc6t(t19, t20);
                                if (!t21) {
                                    goto gm_lb7;
                                }
                                F.ret = ((uint8_t)1);
                                goto gm_fret;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 321, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t22 = gm_l3;
            uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
            uint64_t t25 = r23;
            gm_l3 = t25;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_check_dangling_addresses_9hre1eh(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_check_dangling_addresses_9hre1eh F;
    void * gm_l0; /* s */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    void * gm_l5; /* def */
    void * gm_l6; /* body */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint32_t gm_l12; /* p */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    GM_STACK_CHECK(&F, 1163u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l18;
    gm_tftb1odc739t5 gm_l19;
    gm_trqfvmjtt4sij t25;
    gm_f_list_v9rkbv6((void *)&t25, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t25;
    gm_l3 = ((uint64_t)0ULL);
    void * t26 = gm_l0;
    uint64_t t27 = GM_LD(uint64_t, ((((uint8_t *)t26) + 384) + 8));
    gm_l2 = t27;
    {
        for (;;) {
            uint64_t t28 = gm_l3;
            uint64_t t29 = gm_l2;
            uint8_t t30 = (t28 < t29);
            if (!t30) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t31 = gm_l3;
                        gm_l4 = t31;
                        void * t32 = gm_l0;
                        void * t33 = GM_LD(void *, (((uint8_t *)t32) + 344));
                        uint64_t t34 = GM_LD(uint64_t, ((((uint8_t *)t32) + 344) + 8));
                        uint64_t t35 = gm_l4;
                        if ((uintptr_t)t35 >= (uintptr_t)(t34)) gm_trap_index((uintptr_t)t35, t34, gm_srcb2bqqsnukmrd, 17, 12);
                        {
                            {
                                uint8_t t36 = (uint8_t)((((((uint8_t *)t33) + (uintptr_t)t35 * 136) + 96))[24] != 0);
                                if (t36) {
                                    goto gm_lb7;
                                }
                                gm_l18 = ((uint8_t)0);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                gm_l18 = ((uint8_t)1);
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 17, 12);
                        }
gm_lb6:;
                        uint8_t t37 = gm_l18;
                        if (t37) {
                            goto gm_lb3;
                        }
                        void * t38 = gm_l0;
                        void * t39 = GM_LD(void *, (((uint8_t *)t38) + 384));
                        uint64_t t40 = GM_LD(uint64_t, ((((uint8_t *)t38) + 384) + 8));
                        uint64_t t41 = gm_l4;
                        if ((uintptr_t)t41 >= (uintptr_t)(t40)) gm_trap_index((uintptr_t)t41, t40, gm_srcb2bqqsnukmrd, 18, 17);
                        void * t42 = (void *)(((uint8_t *)t39) + (uintptr_t)t41 * 64);
                        gm_l5 = t42;
                        void * t43 = gm_l5;
                        uint8_t t44 = (uint8_t)(GM_LD(void *, (((uint8_t *)t43) + 40)) != 0);
                        if (t44) {
                            void * t45 = GM_LD(void *, (((uint8_t *)t43) + 40));
                            gm_l6 = t45;
                        } else {
                            goto gm_lb3;
                        }
                        {
                            gm_trqfvmjtt4sij t46;
                            gm_f_list_0a7vhht((void *)&t46, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.l7 = t46;
                        }
                        void * t47 = gm_l5;
                        gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)t47) + 24));
                        gm_l8 = t48;
                        gm_l9 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t49 = gm_l9;
                                uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                uint8_t t51 = (t49 < t50);
                                if (!t51) {
                                    goto gm_lb9;
                                }
                                {
                                    void * t52 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                    uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                    uint64_t t54 = gm_l9;
                                    {
                                        {
                                            void * t55 = (void *)((uint8_t *)&F.l7);
                                            int64_t t56 = GM_LD(int64_t, ((uint8_t *)gm_cpn2np70vcf));
                                            gm_tftb1odc739t5 t57 = gm_f_push_p826iq8(t55, t56);
                                            gm_l19 = t57;
                                            uint8_t t58 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                                            if (!t58) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_srcb2bqqsnukmrd, 21, 31);
                                            goto gm_lb12;
                                        }
gm_lb13:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 21, 9);
                                    }
gm_lb12:;
                                }
gm_lb11:;
                                uint64_t t59 = gm_l9;
                                uint64_t r60; bool o61 = __builtin_add_overflow(t59, ((uint64_t)1ULL), &r60);
                                uint64_t t62 = r60;
                                gm_l9 = t62;
                            }
                        }
gm_lb9:;
                        void * t63 = gm_l5;
                        gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)t63) + 8));
                        gm_l10 = t64;
                        gm_l11 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t65 = gm_l11;
                                uint64_t t66 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                uint8_t t67 = (t65 < t66);
                                if (!t67) {
                                    goto gm_lb14;
                                }
                                {
                                    void * t68 = GM_LD(void *, ((uint8_t *)&gm_l10));
                                    uint64_t t69 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                    uint64_t t70 = gm_l11;
                                    {
                                        {
                                            uint32_t t71 = GM_LD(uint32_t, (((uint8_t *)t68) + (uintptr_t)t70 * 4));
                                            gm_l12 = t71;
                                            void * t72 = GM_LD(void *, ((uint8_t *)&F.l7));
                                            uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
                                            uint32_t t74 = gm_l12;
                                            uint64_t t75 = (uint64_t)t74;
                                            if ((uintptr_t)t75 >= (uintptr_t)(t73)) gm_trap_index((uintptr_t)t75, t73, gm_srcb2bqqsnukmrd, 22, 31);
                                            GM_ST(int64_t, (((uint8_t *)t72) + (uintptr_t)t75 * 8), ((int64_t)0LL));
                                            goto gm_lb17;
                                        }
gm_lb18:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 22, 9);
                                    }
gm_lb17:;
                                }
gm_lb16:;
                                uint64_t t76 = gm_l11;
                                uint64_t r77; bool o78 = __builtin_add_overflow(t76, ((uint64_t)1ULL), &r77);
                                uint64_t t79 = r77;
                                gm_l11 = t79;
                            }
                        }
gm_lb14:;
                        {
                            void * t80 = gm_l0;
                            void * t81 = gm_l5;
                            void * t82 = (void *)((uint8_t *)&F.l1);
                            gm_trqfvmjtt4sij t83 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l7));
                            gm_trqfvmjtt4sij t84;
                            gm_f_list_7s4om10((void *)&t84, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tqp3o9evff257 t85;
                            GM_ST(void *, ((uint8_t *)&t85 + 0), t80);
                            GM_ST(void *, ((uint8_t *)&t85 + 8), t81);
                            GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t85 + 16), t83);
                            GM_ST(int64_t, ((uint8_t *)&t85 + 56), ((int64_t)0LL));
                            GM_ST(void *, ((uint8_t *)&t85 + 64), t82);
                            GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t85 + 72), t84);
                            F.l13 = t85;
                        }
                        void * t86 = (void *)((uint8_t *)&F.l13);
                        void * t87 = gm_l6;
                        gm_b0_1 t88 = gm_f_d_expr_bbktnn5((void *)NULL, cx, NULL, t86, t87, ((uint8_t)1));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 16, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t89 = gm_l3;
            uint64_t r90; bool o91 = __builtin_add_overflow(t89, ((uint64_t)1ULL), &r90);
            uint64_t t92 = r90;
            gm_l3 = t92;
        }
    }
gm_lb1:;
    gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    gm_l14 = t93;
    gm_l15 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t94 = gm_l15;
            uint64_t t95 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
            uint8_t t96 = (t94 < t95);
            if (!t96) {
                goto gm_lb19;
            }
            {
                void * t97 = GM_LD(void *, ((uint8_t *)&gm_l14));
                uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                uint64_t t99 = gm_l15;
                {
                    {
                        {
                            memmove(((uint8_t *)&F.l16), (((uint8_t *)t97) + (uintptr_t)t99 * 72), 72);
                        }
                        uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&F.l16) + 64));
                        uint8_t t101 = (uint8_t)!t100;
                        if (t101) {
                            goto gm_lb21;
                        }
                        {
                            gm_tl874t2atnlh3 t102 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&F.l16));
                            gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 16));
                            gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 48));
                            gm_b16_8 t105 = gm_f_dangling_message_s0d8vok((void *)NULL, cx, NULL, t103, t104);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_t4gut2c9fj1kt t106;
                            gm_f_error_s6hn871((void *)&t106, (void *)NULL, cx, NULL, t102, t105);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.l17 = t106;
                        }
                        void * t107 = (void *)((uint8_t *)&F.l17);
                        gm_b16_8 t108 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 16));
                        gm_tl874t2atnlh3 t109 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l16) + 32));
                        gm_b0_1 t110 = gm_f_add_dangling_notes_s4j764p((void *)NULL, cx, NULL, t107, t108, t109);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        {
                            void * t111 = gm_l0;
                            gm_b0_1 t112 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t111, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l17)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        goto gm_lb22;
                    }
gm_lb23:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 26, 5);
                }
gm_lb22:;
            }
gm_lb21:;
            uint64_t t113 = gm_l15;
            uint64_t r114; bool o115 = __builtin_add_overflow(t113, ((uint64_t)1ULL), &r114);
            uint64_t t116 = r114;
            gm_l15 = t116;
        }
    }
gm_lb19:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_d_block_5tur90b(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_d_block_5tur90b F;
    void * gm_l0; /* w */
    void * gm_l1; /* b */
    void * gm_l2; /* t */
    GM_STACK_CHECK(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t4 = gm_l0;
    void * t5 = gm_l0;
    int64_t t6 = GM_LD(int64_t, (((uint8_t *)t5) + 56));
    int64_t r7; bool o8 = __builtin_add_overflow(t6, ((int64_t)1LL), &r7);
    if (o8) gm_trap("integer overflow in `+`", gm_srcb2bqqsnukmrd, 285, 5);
    int64_t t9 = r7;
    GM_ST(int64_t, (((uint8_t *)t4) + 56), t9);
    void * t10 = gm_l0;
    void * t11 = gm_l1;
    gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)t11));
    gm_b0_1 t13 = gm_f_d_stmts_frrm4kl((void *)NULL, cx, NULL, t10, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            void * t14 = gm_l1;
            uint8_t t15 = (uint8_t)(GM_LD(void *, (((uint8_t *)t14) + 16)) != 0);
            if (!t15) {
                goto gm_lb2;
            }
            void * t16 = GM_LD(void *, (((uint8_t *)t14) + 16));
            gm_l2 = t16;
            void * t17 = gm_l0;
            void * t18 = gm_l2;
            gm_b0_1 t19 = gm_f_d_expr_bbktnn5((void *)NULL, cx, NULL, t17, t18, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t20 = gm_l0;
    void * t21 = gm_l0;
    int64_t t22 = GM_LD(int64_t, (((uint8_t *)t21) + 56));
    int64_t r23; bool o24 = __builtin_sub_overflow(t22, ((int64_t)1LL), &r23);
    if (o24) gm_trap("integer overflow in `-`", gm_srcb2bqqsnukmrd, 288, 5);
    int64_t t25 = r23;
    GM_ST(int64_t, (((uint8_t *)t20) + 56), t25);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_d_call_f4t084p(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2) {
    struct gm_fr_d_call_f4t084p F;
    void * gm_l0; /* w */
    uint32_t gm_l1; /* c */
    gm_b16_8 gm_l2; /* args */
    uint32_t gm_l3; /* template */
    gm_tiilmfd16v3fc gm_l4; /* inst */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t913bb735pvt5 gm_l9; /* target */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    void * gm_l13; /* a */
    uint8_t gm_l14; /* c2 */
    gm_t913bb735pvt5 gm_l15; /* t */
    void * gm_l16; /* p */
    gm_t913bb735pvt5 gm_l17; /* st */
    int64_t gm_l18; /* x */
    int64_t gm_l19; /* cur */
    int64_t gm_l20; /* t */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    uint64_t gm_l23; /* i */
    GM_STACK_CHECK(&F, 1113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l24;
    gm_tftb1odc739t5 gm_l25;
    gm_t53at80pfdus3 gm_l26;
    gm_t0id5pe5tq2j8 gm_l27;
    uint8_t gm_l28;
    gm_t53at80pfdus3 gm_l29;
    uint32_t t35 = gm_l1;
    gm_l3 = t35;
    {
        {
            void * t36 = gm_l0;
            void * t37 = GM_LD(void *, ((uint8_t *)t36));
            void * t38 = GM_LD(void *, (((uint8_t *)t37) + 344));
            uint64_t t39 = GM_LD(uint64_t, ((((uint8_t *)t37) + 344) + 8));
            uint32_t t40 = gm_l1;
            uint64_t t41 = (uint64_t)t40;
            if ((uintptr_t)t41 >= (uintptr_t)(t39)) gm_trap_index((uintptr_t)t41, t39, gm_srcb2bqqsnukmrd, 197, 16);
            uint8_t t42 = (uint8_t)((((((uint8_t *)t38) + (uintptr_t)t41 * 136) + 96))[24] != 0);
            if (!t42) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l4), ((((uint8_t *)t38) + (uintptr_t)t41 * 136) + 96), 24);
            uint32_t t43 = GM_LD(uint32_t, ((uint8_t *)&gm_l4));
            gm_l3 = t43;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_trqfvmjtt4sij t44;
    gm_f_list_nof7vfj((void *)&t44, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l5 = t44;
    void * t45 = gm_l0;
    void * t46 = GM_LD(void *, ((uint8_t *)t45));
    void * t47 = GM_LD(void *, (((uint8_t *)t46) + 344));
    uint64_t t48 = GM_LD(uint64_t, ((((uint8_t *)t46) + 344) + 8));
    uint32_t t49 = gm_l3;
    uint64_t t50 = (uint64_t)t49;
    if ((uintptr_t)t50 >= (uintptr_t)(t48)) gm_trap_index((uintptr_t)t50, t48, gm_srcb2bqqsnukmrd, 199, 14);
    gm_b16_8 t51 = GM_LD(gm_b16_8, ((((uint8_t *)t47) + (uintptr_t)t50 * 136) + 40));
    gm_l6 = t51;
    gm_l7 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t52 = gm_l7;
            uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint8_t t54 = (t52 < t53);
            if (!t54) {
                goto gm_lb3;
            }
            {
                void * t55 = GM_LD(void *, ((uint8_t *)&gm_l6));
                uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint64_t t57 = gm_l7;
                {
                    {
                        memmove(((uint8_t *)&F.l8), (((uint8_t *)t55) + (uintptr_t)t57 * 56), 56);
                        {
                            {
                                uint8_t t58 = (uint8_t)(((((uint8_t *)&F.l8) + 40))[4] != 0);
                                if (t58) {
                                    goto gm_lb9;
                                }
                                gm_l24 = ((uint8_t)1);
                                goto gm_lb8;
                            }
gm_lb9:;
                            {
                                gm_l24 = ((uint8_t)0);
                                goto gm_lb8;
                            }
gm_lb10:;
                            gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 200, 12);
                        }
gm_lb8:;
                        uint8_t t59 = gm_l24;
                        if (t59) {
                            void * t60 = (void *)((uint8_t *)&F.l5);
                            gm_t88rcucg1b7uk t61 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l8) + 16));
                            gm_tftb1odc739t5 t62 = gm_f_push_k3l9nic(t60, GM_CONV(gm_csguankko4lmhd, t61));
                            gm_l25 = t62;
                            uint8_t t63 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                            if (!t63) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_srcb2bqqsnukmrd, 200, 32);
                        }
                        goto gm_lb6;
                    }
gm_lb7:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 199, 5);
                }
gm_lb6:;
            }
gm_lb5:;
            uint64_t t64 = gm_l7;
            uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
            uint64_t t67 = r65;
            gm_l7 = t67;
        }
    }
gm_lb3:;
    gm_t913bb735pvt5 t68;
    memset(&t68, 0, sizeof t68);
    gm_l9 = t68;
    gm_l11 = ((uint64_t)0ULL);
    uint64_t t69 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    gm_l10 = t69;
    {
        for (;;) {
            uint64_t t70 = gm_l11;
            uint64_t t71 = gm_l10;
            uint8_t t72 = (t70 < t71);
            if (!t72) {
                goto gm_lb11;
            }
            {
                {
                    {
                        uint64_t t73 = gm_l11;
                        gm_l12 = t73;
                        void * t74 = GM_LD(void *, ((uint8_t *)&gm_l2));
                        uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                        uint64_t t76 = gm_l12;
                        void * t77 = (void *)(((uint8_t *)t74) + (uintptr_t)t76 * 88);
                        gm_l13 = t77;
                        {
                            {
                                void * t78 = gm_l0;
                                void * t79 = GM_LD(void *, ((uint8_t *)t78));
                                void * t80 = (void *)(((uint8_t *)t79) + 8);
                                void * t81 = gm_l13;
                                gm_t88rcucg1b7uk t82 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t81));
                                gm_f_kind_jcnt4hm((void *)&gm_l26, t80, GM_CONV(gm_csguankko4lmhd, t82));
                                uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                                uint8_t t84 = (uint8_t)(t83 == ((uint8_t)8ULL));
                                if (!t84) {
                                    goto gm_lb17;
                                }
                                uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 8));
                                gm_l14 = t85;
                                uint8_t t86 = gm_l14;
                                uint8_t t87 = (uint8_t)!t86;
                                if (!t87) {
                                    goto gm_lb17;
                                }
                                int64_t t88 = GM_LD(int64_t, ((uint8_t *)gm_c9paf88rqhe));
                                gm_t913bb735pvt5 t89;
                                GM_ST(uint8_t, (uint8_t *)&t89 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&t89 + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&t89 + 12, 0);
                                GM_ST(int64_t, ((uint8_t *)&t89), t88);
                                ((uint8_t *)&t89)[8] = 1;
                                gm_l15 = t89;
                                {
                                    {
                                        void * t90 = gm_l13;
                                        uint8_t t91 = GM_LD(uint8_t, ((((uint8_t *)t90) + 8) + 0));
                                        uint8_t t92 = (uint8_t)(t91 == ((uint8_t)24ULL));
                                        if (!t92) {
                                            goto gm_lb19;
                                        }
                                        void * t93 = GM_LD(void *, ((((uint8_t *)t90) + 8) + 8));
                                        gm_l16 = t93;
                                        gm_t913bb735pvt5 t94;
                                        memset(&t94, 0, sizeof t94);
                                        gm_l15 = t94;
                                        {
                                            {
                                                void * t95 = gm_l0;
                                                void * t96 = gm_l16;
                                                gm_f_d_storage_kqhfs5d((void *)&gm_l27, t95, t96);
                                                uint8_t t97 = (uint8_t)((((uint8_t *)&gm_l27))[16] != 0);
                                                if (!t97) {
                                                    goto gm_lb21;
                                                }
                                                gm_t913bb735pvt5 t98 = GM_LD(gm_t913bb735pvt5, ((uint8_t *)&gm_l27));
                                                gm_l17 = t98;
                                                int64_t t99 = GM_LD(int64_t, ((uint8_t *)&gm_l17));
                                                gm_t913bb735pvt5 t100;
                                                GM_ST(uint8_t, (uint8_t *)&t100 + 9, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t100 + 10, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t100 + 12, 0);
                                                GM_ST(int64_t, ((uint8_t *)&t100), t99);
                                                ((uint8_t *)&t100)[8] = 1;
                                                gm_l15 = t100;
                                                goto gm_lb20;
                                            }
gm_lb21:;
                                        }
gm_lb20:;
                                        goto gm_lb18;
                                    }
gm_lb19:;
                                }
gm_lb18:;
                                {
                                    {
                                        uint8_t t101 = (uint8_t)((((uint8_t *)&gm_l15))[8] != 0);
                                        if (!t101) {
                                            goto gm_lb23;
                                        }
                                        int64_t t102 = GM_LD(int64_t, ((uint8_t *)&gm_l15));
                                        gm_l18 = t102;
                                        {
                                            {
                                                uint8_t t103 = (uint8_t)((((uint8_t *)&gm_l9))[8] != 0);
                                                if (!t103) {
                                                    goto gm_lb25;
                                                }
                                                int64_t t104 = GM_LD(int64_t, ((uint8_t *)&gm_l9));
                                                gm_l19 = t104;
                                                int64_t t105 = gm_l18;
                                                int64_t t106 = gm_l19;
                                                uint8_t t107 = (t105 < t106);
                                                if (t107) {
                                                    int64_t t108 = gm_l18;
                                                    gm_t913bb735pvt5 t109;
                                                    GM_ST(uint8_t, (uint8_t *)&t109 + 9, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t109 + 10, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t109 + 12, 0);
                                                    GM_ST(int64_t, ((uint8_t *)&t109), t108);
                                                    ((uint8_t *)&t109)[8] = 1;
                                                    gm_l9 = t109;
                                                }
                                                goto gm_lb24;
                                            }
gm_lb25:;
                                            int64_t t110 = gm_l18;
                                            gm_t913bb735pvt5 t111;
                                            GM_ST(uint8_t, (uint8_t *)&t111 + 9, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t111 + 10, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t111 + 12, 0);
                                            GM_ST(int64_t, ((uint8_t *)&t111), t110);
                                            ((uint8_t *)&t111)[8] = 1;
                                            gm_l9 = t111;
                                        }
gm_lb24:;
                                        goto gm_lb22;
                                    }
gm_lb23:;
                                }
gm_lb22:;
                                goto gm_lb16;
                            }
gm_lb17:;
                        }
gm_lb16:;
                        goto gm_lb14;
                    }
gm_lb15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 204, 5);
                }
gm_lb14:;
            }
gm_lb13:;
            uint64_t t112 = gm_l11;
            uint64_t r113; bool o114 = __builtin_add_overflow(t112, ((uint64_t)1ULL), &r113);
            uint64_t t115 = r113;
            gm_l11 = t115;
        }
    }
gm_lb11:;
    uint8_t t116 = (uint8_t)((((uint8_t *)&gm_l9))[8] != 0);
    if (t116) {
        int64_t t117 = GM_LD(int64_t, ((uint8_t *)&gm_l9));
        gm_l20 = t117;
    } else {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    gm_l22 = ((uint64_t)0ULL);
    uint64_t t118 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint64_t t119 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
    uint64_t t120 = (t118 < t119 ? t118 : t119);
    gm_l21 = t120;
    {
        for (;;) {
            uint64_t t121 = gm_l22;
            uint64_t t122 = gm_l21;
            uint8_t t123 = (t121 < t122);
            if (!t123) {
                goto gm_lb26;
            }
            {
                {
                    {
                        uint64_t t124 = gm_l22;
                        gm_l23 = t124;
                        void * t125 = gm_l0;
                        void * t126 = GM_LD(void *, ((uint8_t *)t125));
                        void * t127 = (void *)(((uint8_t *)t126) + 8);
                        void * t128 = GM_LD(void *, ((uint8_t *)&F.l5));
                        uint64_t t129 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
                        uint64_t t130 = gm_l23;
                        if ((uintptr_t)t130 >= (uintptr_t)(t129)) gm_trap_index((uintptr_t)t130, t129, gm_srcb2bqqsnukmrd, 223, 25);
                        gm_t88rcucg1b7uk t131 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t128) + (uintptr_t)t130 * 4));
                        gm_f_kind_jcnt4hm((void *)&gm_l29, t127, GM_CONV(gm_csguankko4lmhd, t131));
                        {
                            {
                                uint8_t t132 = GM_LD(uint8_t, (((uint8_t *)&gm_l29) + 0));
                                uint8_t t133 = (uint8_t)(t132 == ((uint8_t)19ULL));
                                if (!t133) {
                                    goto gm_lb32;
                                }
                                gm_l28 = ((uint8_t)1);
                                goto gm_lb31;
                            }
gm_lb32:;
                            {
                                gm_l28 = ((uint8_t)0);
                                goto gm_lb31;
                            }
gm_lb33:;
                            gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 223, 12);
                        }
gm_lb31:;
                        uint8_t t134 = gm_l28;
                        if (t134) {
                            void * t135 = gm_l0;
                            void * t136 = GM_LD(void *, ((uint8_t *)&gm_l2));
                            uint64_t t137 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                            uint64_t t138 = gm_l23;
                            if ((uintptr_t)t138 >= (uintptr_t)(t137)) gm_trap_index((uintptr_t)t138, t137, gm_srcb2bqqsnukmrd, 223, 74);
                            void * t139 = (void *)(((uint8_t *)t136) + (uintptr_t)t138 * 88);
                            int64_t t140 = gm_l20;
                            gm_b16_8 t141;
                            GM_ST(void *, (uint8_t *)&t141, (void *)(gm_spj65mdvs9ei4));
                            GM_ST(uintptr_t, (uint8_t *)&t141 + 8, (uintptr_t)(19));
                            gm_b0_1 t142 = gm_f_d_check_store_ldgo1fg((void *)NULL, cx, NULL, t135, t139, t140, t141, ((uint8_t)0));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        goto gm_lb29;
                    }
gm_lb30:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 222, 5);
                }
gm_lb29:;
            }
gm_lb28:;
            uint64_t t143 = gm_l22;
            uint64_t r144; bool o145 = __builtin_add_overflow(t143, ((uint64_t)1ULL), &r144);
            uint64_t t146 = r144;
            gm_l22 = t146;
        }
    }
gm_lb26:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_d_check_store_ldgo1fg(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, int64_t a2, gm_b16_8 a3, uint8_t a4) {
    struct gm_fr_d_check_store_ldgo1fg F;
    void * gm_l0; /* w */
    void * gm_l1; /* value */
    int64_t gm_l2; /* target */
    gm_b16_8 gm_l3; /* why */
    uint8_t gm_l4; /* certain */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t3u9hrtrhq8ch gm_l8; /* h */
    int64_t gm_l9; /* d */
    GM_STACK_CHECK(&F, 392u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_trqfvmjtt4sij t11;
    gm_f_list_hddcdrs((void *)&t11, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l5 = t11;
    void * t12 = gm_l0;
    void * t13 = GM_LD(void *, ((uint8_t *)t12));
    void * t14 = gm_l1;
    void * t15 = (void *)((uint8_t *)&F.l5);
    gm_b0_1 t16 = gm_f_d_held_nduio4j((void *)NULL, cx, NULL, t13, t14, ((uint8_t)0), t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
    gm_l6 = t17;
    gm_l7 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t18 = gm_l7;
            uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint8_t t20 = (t18 < t19);
            if (!t20) {
                goto gm_lb1;
            }
            {
                void * t21 = GM_LD(void *, ((uint8_t *)&gm_l6));
                uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint64_t t23 = gm_l7;
                {
                    {
                        memmove(((uint8_t *)&gm_l8), (((uint8_t *)t21) + (uintptr_t)t23 * 20), 20);
                        {
                            {
                                gm_b16_8 t24 = gm_l3;
                                gm_b16_8 t25;
                                GM_ST(void *, (uint8_t *)&t25, (void *)(gm_skvqiv7no78c2));
                                GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(8));
                                uint8_t t26 = (gm_eq0iqiubfm7e(((uint8_t *)&t24), ((uint8_t *)&t25)));
                                if (!t26) {
                                    goto gm_lb7;
                                }
                                uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 16));
                                uint8_t t28 = (uint8_t)!t27;
                                if (!t28) {
                                    goto gm_lb7;
                                }
                                goto gm_lb3;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        void * t29 = gm_l0;
                        void * t30 = GM_LD(void *, (((uint8_t *)t29) + 16));
                        uint64_t t31 = GM_LD(uint64_t, ((((uint8_t *)t29) + 16) + 8));
                        uint32_t t32 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 12));
                        uint64_t t33 = (uint64_t)t32;
                        if ((uintptr_t)t33 >= (uintptr_t)(t31)) gm_trap_index((uintptr_t)t33, t31, gm_srcb2bqqsnukmrd, 190, 14);
                        int64_t t34 = GM_LD(int64_t, (((uint8_t *)t30) + (uintptr_t)t33 * 8));
                        gm_l9 = t34;
                        {
                            {
                                int64_t t35 = gm_l9;
                                int64_t t36 = GM_LD(int64_t, ((uint8_t *)gm_cpn2np70vcf));
                                uint8_t t37 = (!(t35 == t36));
                                if (!t37) {
                                    goto gm_lb9;
                                }
                                int64_t t38 = gm_l2;
                                int64_t t39 = gm_l9;
                                uint8_t t40 = (t38 < t39);
                                if (!t40) {
                                    goto gm_lb9;
                                }
                                void * t41 = gm_l0;
                                gm_tl874t2atnlh3 t42 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l8));
                                uint32_t t43 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 12));
                                gm_b16_8 t44 = gm_l3;
                                uint8_t t45 = gm_l4;
                                gm_b0_1 t46 = gm_f_d_report_fdjjedi((void *)NULL, cx, NULL, t41, t42, t43, t44, t45);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb8;
                            }
gm_lb9:;
                        }
gm_lb8:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 187, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t47 = gm_l7;
            uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
            uint64_t t50 = r48;
            gm_l7 = t50;
        }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_d_cond_6aiv3jd(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_d_cond_6aiv3jd F;
    void * gm_l0; /* w */
    gm_b16_8 gm_l1; /* cond */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tlr9dp305htm7 gm_l4; /* c */
    void * gm_l5; /* x */
    void * gm_l6; /* p */
    void * gm_l7; /* v */
    GM_STACK_CHECK(&F, 245u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
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
                        memmove(((uint8_t *)&gm_l4), (((uint8_t *)t14) + (uintptr_t)t16 * 24), 24);
                        {
                            {
                                uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t18 = (uint8_t)(t17 == ((uint8_t)0ULL));
                                if (!t18) {
                                    goto gm_lb7;
                                }
                                void * t19 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                                gm_l5 = t19;
                                void * t20 = gm_l0;
                                void * t21 = gm_l5;
                                gm_b0_1 t22 = gm_f_d_expr_bbktnn5((void *)NULL, cx, NULL, t20, t21, ((uint8_t)0));
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t24 = (uint8_t)(t23 == ((uint8_t)1ULL));
                                if (!t24) {
                                    goto gm_lb8;
                                }
                                void * t25 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                                gm_l6 = t25;
                                void * t26 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                                gm_l7 = t26;
                                void * t27 = gm_l0;
                                void * t28 = gm_l7;
                                gm_b0_1 t29 = gm_f_d_expr_bbktnn5((void *)NULL, cx, NULL, t27, t28, ((uint8_t)0));
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t30 = gm_l0;
                                void * t31 = gm_l6;
                                gm_f_d_declare_pat_53h71nd(t30, t31);
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 293, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 292, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t32 = gm_l3;
            uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
            uint64_t t35 = r33;
            gm_l3 = t35;
        }
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
extern __attribute__((aligned(4))) void gm_f_d_declare_nctg7jr(void * a0, uint32_t a1);
extern __attribute__((aligned(4))) void gm_f_d_declare_pat_53h71nd(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_d_expr_bbktnn5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2) {
    struct gm_fr_d_expr_bbktnn5 F;
    void * gm_l1; /* e */
    uint8_t gm_l2; /* tail */
    void * gm_l3; /* b */
    void * gm_l4; /* t */
    uint32_t gm_l5; /* levels */
    void * gm_l6; /* value */
    void * gm_l7; /* scrutinee */
    gm_b16_8 gm_l8; /* arms */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_tbbqefuhv2r8h gm_l11; /* a */
    void * gm_l12; /* g */
    gm_trl2khmuphncd gm_l13; /* x */
    void * gm_l14; /* el */
    void * gm_l16; /* st */
    gm_t3dcoc7077t1q gm_l17; /* x */
    uint32_t gm_l18; /* l */
    uint32_t gm_l19; /* c */
    gm_b16_8 gm_l20; /* args */
    GM_STACK_CHECK(&F, 1178u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t26 = gm_l1;
    {
        {
            uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
            uint8_t t28 = (uint8_t)(t27 == ((uint8_t)46ULL));
            if (!t28) {
                goto gm_lb2;
            }
            void * t29 = GM_LD(void *, ((((uint8_t *)t26) + 8) + 8));
            gm_l3 = t29;
            void * t30 = F.l0;
            void * t31 = F.l0;
            int64_t t32 = GM_LD(int64_t, (((uint8_t *)t31) + 56));
            int64_t r33; bool o34 = __builtin_add_overflow(t32, ((int64_t)1LL), &r33);
            if (o34) gm_trap("integer overflow in `+`", gm_srcb2bqqsnukmrd, 231, 13);
            int64_t t35 = r33;
            GM_ST(int64_t, (((uint8_t *)t30) + 56), t35);
            void * t36 = F.l0;
            void * t37 = gm_l3;
            gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)t37));
            gm_b0_1 t39 = gm_f_d_stmts_frrm4kl((void *)NULL, cx, NULL, t36, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                {
                    void * t40 = gm_l3;
                    uint8_t t41 = (uint8_t)(GM_LD(void *, (((uint8_t *)t40) + 16)) != 0);
                    if (!t41) {
                        goto gm_lb4;
                    }
                    void * t42 = GM_LD(void *, (((uint8_t *)t40) + 16));
                    gm_l4 = t42;
                    void * t43 = F.l0;
                    void * t44 = gm_l4;
                    uint8_t t45 = gm_l2;
                    gm_b0_1 t46 = gm_f_d_expr_bbktnn5((void *)NULL, cx, NULL, t43, t44, t45);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
            }
gm_lb3:;
            void * t47 = F.l0;
            void * t48 = F.l0;
            int64_t t49 = GM_LD(int64_t, (((uint8_t *)t48) + 56));
            int64_t r50; bool o51 = __builtin_sub_overflow(t49, ((int64_t)1LL), &r50);
            if (o51) gm_trap("integer overflow in `-`", gm_srcb2bqqsnukmrd, 234, 13);
            int64_t t52 = r50;
            GM_ST(int64_t, (((uint8_t *)t47) + 56), t52);
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t53 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
            uint8_t t54 = (uint8_t)(t53 == ((uint8_t)51ULL));
            if (!t54) {
                goto gm_lb5;
            }
            uint32_t t55 = GM_LD(uint32_t, ((((uint8_t *)t26) + 8) + 8));
            gm_l5 = t55;
            void * t56 = GM_LD(void *, (((((uint8_t *)t26) + 8) + 8) + 8));
            gm_l6 = t56;
            uint32_t t57 = gm_l5;
            uint8_t t58 = ((t57 == ((uint32_t)0ULL)));
            if (t58) {
                void * t59 = F.l0;
                void * t60 = gm_l6;
                int64_t t61 = GM_LD(int64_t, ((uint8_t *)gm_c9paf88rqhe));
                gm_b16_8 t62;
                GM_ST(void *, (uint8_t *)&t62, (void *)(gm_skvqiv7no78c2));
                GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(8));
                gm_b0_1 t63 = gm_f_d_check_store_ldgo1fg((void *)NULL, cx, NULL, t59, t60, t61, t62, ((uint8_t)1));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t64 = F.l0;
                void * t65 = gm_l6;
                gm_b0_1 t66 = gm_f_d_expr_bbktnn5((void *)NULL, cx, NULL, t64, t65, ((uint8_t)0));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t67 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
            uint8_t t68 = (uint8_t)(t67 == ((uint8_t)44ULL));
            if (!t68) {
                goto gm_lb6;
            }
            void * t69 = GM_LD(void *, ((((uint8_t *)t26) + 8) + 8));
            gm_l7 = t69;
            gm_b16_8 t70 = GM_LD(gm_b16_8, (((((uint8_t *)t26) + 8) + 8) + 8));
            gm_l8 = t70;
            void * t71 = F.l0;
            void * t72 = gm_l7;
            gm_b0_1 t73 = gm_f_d_expr_bbktnn5((void *)NULL, cx, NULL, t71, t72, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t74 = gm_l8;
            gm_l9 = t74;
            gm_l10 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t76 = gm_l10;
                    uint64_t t77 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                    uint8_t t78 = (t76 < t77);
                    if (!t78) {
                        goto gm_lb7;
                    }
                    {
                        void * t79 = GM_LD(void *, ((uint8_t *)&gm_l9));
                        uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                        uint64_t t81 = gm_l10;
                        {
                            {
                                memmove(((uint8_t *)&gm_l11), (((uint8_t *)t79) + (uintptr_t)t81 * 24), 24);
                                void * t82 = F.l0;
                                void * t83 = F.l0;
                                int64_t t84 = GM_LD(int64_t, (((uint8_t *)t83) + 56));
                                int64_t r85; bool o86 = __builtin_add_overflow(t84, ((int64_t)1LL), &r85);
                                if (o86) gm_trap("integer overflow in `+`", gm_srcb2bqqsnukmrd, 245, 17);
                                int64_t t87 = r85;
                                GM_ST(int64_t, (((uint8_t *)t82) + 56), t87);
                                void * t88 = F.l0;
                                void * t89 = GM_LD(void *, ((uint8_t *)&gm_l11));
                                gm_f_d_declare_pat_53h71nd(t88, t89);
                                {
                                    {
                                        uint8_t t90 = (uint8_t)(GM_LD(void *, (((uint8_t *)&gm_l11) + 8)) != 0);
                                        if (!t90) {
                                            goto gm_lb13;
                                        }
                                        void * t91 = GM_LD(void *, (((uint8_t *)&gm_l11) + 8));
                                        gm_l12 = t91;
                                        void * t92 = F.l0;
                                        void * t93 = gm_l12;
                                        gm_b0_1 t94 = gm_f_d_expr_bbktnn5((void *)NULL, cx, NULL, t92, t93, ((uint8_t)0));
                                        if (gm_unw.kind) {
                                            goto gm_ll75;
                                        }
                                        goto gm_lb12;
                                    }
gm_lb13:;
                                }
gm_lb12:;
                                void * t95 = F.l0;
                                void * t96 = GM_LD(void *, (((uint8_t *)&gm_l11) + 16));
                                uint8_t t97 = gm_l2;
                                gm_b0_1 t98 = gm_f_d_expr_bbktnn5((void *)NULL, cx, NULL, t95, t96, t97);
                                if (gm_unw.kind) {
                                    goto gm_ll75;
                                }
                                void * t99 = F.l0;
                                void * t100 = F.l0;
                                int64_t t101 = GM_LD(int64_t, (((uint8_t *)t100) + 56));
                                int64_t r102; bool o103 = __builtin_sub_overflow(t101, ((int64_t)1LL), &r102);
                                if (o103) gm_trap("integer overflow in `-`", gm_srcb2bqqsnukmrd, 249, 17);
                                int64_t t104 = r102;
                                GM_ST(int64_t, (((uint8_t *)t99) + 56), t104);
                                goto gm_lb10;
                            }
gm_lb11:;
                            gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 244, 13);
                        }
gm_lb10:;
                    }
gm_lb9:;
                    uint64_t t105 = gm_l10;
                    uint64_t r106; bool o107 = __builtin_add_overflow(t105, ((uint64_t)1ULL), &r106);
                    uint64_t t108 = r106;
                    gm_l10 = t108;
                }
            }
            goto gm_ls109;
gm_ll75:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb7; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb9; }
            }
            goto gm_lad;
gm_ls109:;
gm_lb7:;
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
gm_lb6:;
        {
            uint8_t t110 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
            uint8_t t111 = (uint8_t)(t110 == ((uint8_t)47ULL));
            if (!t111) {
                goto gm_lb14;
            }
            memmove(((uint8_t *)&gm_l13), ((((uint8_t *)t26) + 8) + 8), 32);
            void * t112 = F.l0;
            void * t113 = F.l0;
            int64_t t114 = GM_LD(int64_t, (((uint8_t *)t113) + 56));
            int64_t r115; bool o116 = __builtin_add_overflow(t114, ((int64_t)1LL), &r115);
            if (o116) gm_trap("integer overflow in `+`", gm_srcb2bqqsnukmrd, 254, 13);
            int64_t t117 = r115;
            GM_ST(int64_t, (((uint8_t *)t112) + 56), t117);
            void * t118 = F.l0;
            gm_b16_8 t119 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l13));
            gm_b0_1 t120 = gm_f_d_cond_6aiv3jd((void *)NULL, cx, NULL, t118, t119);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t121 = F.l0;
            void * t122 = GM_LD(void *, (((uint8_t *)&gm_l13) + 16));
            uint8_t t123 = gm_l2;
            gm_b0_1 t124 = gm_f_d_expr_bbktnn5((void *)NULL, cx, NULL, t121, t122, t123);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t125 = F.l0;
            void * t126 = F.l0;
            int64_t t127 = GM_LD(int64_t, (((uint8_t *)t126) + 56));
            int64_t r128; bool o129 = __builtin_sub_overflow(t127, ((int64_t)1LL), &r128);
            if (o129) gm_trap("integer overflow in `-`", gm_srcb2bqqsnukmrd, 257, 13);
            int64_t t130 = r128;
            GM_ST(int64_t, (((uint8_t *)t125) + 56), t130);
            {
                {
                    uint8_t t131 = (uint8_t)(GM_LD(void *, (((uint8_t *)&gm_l13) + 24)) != 0);
                    if (!t131) {
                        goto gm_lb16;
                    }
                    void * t132 = GM_LD(void *, (((uint8_t *)&gm_l13) + 24));
                    gm_l14 = t132;
                    void * t133 = F.l0;
                    void * t134 = gm_l14;
                    uint8_t t135 = gm_l2;
                    gm_b0_1 t136 = gm_f_d_expr_bbktnn5((void *)NULL, cx, NULL, t133, t134, t135);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb15;
                }
gm_lb16:;
            }
gm_lb15:;
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
gm_lb14:;
        {
            uint8_t t137 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
            uint8_t t138 = (uint8_t)(t137 == ((uint8_t)48ULL));
            if (!t138) {
                goto gm_lb17;
            }
            memmove(((uint8_t *)&F.l15), ((((uint8_t *)t26) + 8) + 8), 40);
            void * t139 = F.l0;
            void * t140 = F.l0;
            int64_t t141 = GM_LD(int64_t, (((uint8_t *)t140) + 56));
            int64_t r142; bool o143 = __builtin_add_overflow(t141, ((int64_t)1LL), &r142);
            if (o143) gm_trap("integer overflow in `+`", gm_srcb2bqqsnukmrd, 262, 13);
            int64_t t144 = r142;
            GM_ST(int64_t, (((uint8_t *)t139) + 56), t144);
            void * t145 = F.l0;
            gm_b16_8 t146 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 8));
            gm_b0_1 t147 = gm_f_d_cond_6aiv3jd((void *)NULL, cx, NULL, t145, t146);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t148 = F.l0;
            void * t149 = GM_LD(void *, (((uint8_t *)&F.l15) + 24));
            gm_b0_1 t150 = gm_f_d_block_5tur90b((void *)NULL, cx, NULL, t148, t149);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                {
                    uint8_t t151 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l15) + 32)) != 0);
                    if (!t151) {
                        goto gm_lb19;
                    }
                    void * t152 = GM_LD(void *, (((uint8_t *)&F.l15) + 32));
                    gm_l16 = t152;
                    void * t153 = F.l0;
                    void * t154 = gm_l16;
                    gm_b0_1 t155 = gm_f_d_expr_bbktnn5((void *)NULL, cx, NULL, t153, t154, ((uint8_t)0));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb18;
                }
gm_lb19:;
            }
gm_lb18:;
            void * t156 = F.l0;
            void * t157 = F.l0;
            int64_t t158 = GM_LD(int64_t, (((uint8_t *)t157) + 56));
            int64_t r159; bool o160 = __builtin_sub_overflow(t158, ((int64_t)1LL), &r159);
            if (o160) gm_trap("integer overflow in `-`", gm_srcb2bqqsnukmrd, 266, 13);
            int64_t t161 = r159;
            GM_ST(int64_t, (((uint8_t *)t156) + 56), t161);
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
gm_lb17:;
        {
            uint8_t t162 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
            uint8_t t163 = (uint8_t)(t162 == ((uint8_t)41ULL));
            if (!t163) {
                goto gm_lb20;
            }
            memmove(((uint8_t *)&gm_l17), ((((uint8_t *)t26) + 8) + 8), 24);
            void * t164 = F.l0;
            void * t165 = GM_LD(void *, ((uint8_t *)&gm_l17));
            gm_b0_1 t166 = gm_f_d_expr_bbktnn5((void *)NULL, cx, NULL, t164, t165, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t167 = F.l0;
            void * t168 = F.l0;
            int64_t t169 = GM_LD(int64_t, (((uint8_t *)t168) + 56));
            int64_t r170; bool o171 = __builtin_add_overflow(t169, ((int64_t)1LL), &r170);
            if (o171) gm_trap("integer overflow in `+`", gm_srcb2bqqsnukmrd, 271, 13);
            int64_t t172 = r170;
            GM_ST(int64_t, (((uint8_t *)t167) + 56), t172);
            {
                {
                    uint8_t t173 = (uint8_t)(((((uint8_t *)&gm_l17) + 8))[4] != 0);
                    if (!t173) {
                        goto gm_lb22;
                    }
                    uint32_t t174 = GM_LD(uint32_t, (((uint8_t *)&gm_l17) + 8));
                    gm_l18 = t174;
                    void * t175 = F.l0;
                    uint32_t t176 = gm_l18;
                    gm_f_d_declare_nctg7jr(t175, t176);
                    goto gm_lb21;
                }
gm_lb22:;
            }
gm_lb21:;
            void * t177 = F.l0;
            void * t178 = GM_LD(void *, (((uint8_t *)&gm_l17) + 16));
            uint8_t t179 = gm_l2;
            gm_b0_1 t180 = gm_f_d_expr_bbktnn5((void *)NULL, cx, NULL, t177, t178, t179);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t181 = F.l0;
            void * t182 = F.l0;
            int64_t t183 = GM_LD(int64_t, (((uint8_t *)t182) + 56));
            int64_t r184; bool o185 = __builtin_sub_overflow(t183, ((int64_t)1LL), &r184);
            if (o185) gm_trap("integer overflow in `-`", gm_srcb2bqqsnukmrd, 274, 13);
            int64_t t186 = r184;
            GM_ST(int64_t, (((uint8_t *)t181) + 56), t186);
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
gm_lb20:;
        {
            uint8_t t187 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
            uint8_t t188 = (uint8_t)(t187 == ((uint8_t)34ULL));
            if (!t188) {
                goto gm_lb23;
            }
            uint8_t t189 = GM_LD(uint8_t, (((((uint8_t *)t26) + 8) + 8) + 0));
            uint8_t t190 = (uint8_t)(t189 == ((uint8_t)0ULL));
            if (!t190) {
                goto gm_lb23;
            }
            uint32_t t191 = GM_LD(uint32_t, (((((uint8_t *)t26) + 8) + 8) + 8));
            gm_l19 = t191;
            gm_b16_8 t192 = GM_LD(gm_b16_8, (((((uint8_t *)t26) + 8) + 8) + 40));
            gm_l20 = t192;
            void * t193 = F.l0;
            uint32_t t194 = gm_l19;
            gm_b16_8 t195 = gm_l20;
            gm_b0_1 t196 = gm_f_d_call_f4t084p((void *)NULL, cx, NULL, t193, t194, t195);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb23:;
        {
            goto gm_lb1;
        }
gm_lb24:;
        gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 229, 5);
    }
gm_lb1:;
    uint8_t t197 = gm_l2;
    if (t197) {
        void * t198 = F.l0;
        void * t199 = gm_l1;
        int64_t t200 = GM_LD(int64_t, ((uint8_t *)gm_c9paf88rqhe));
        gm_b16_8 t201;
        GM_ST(void *, (uint8_t *)&t201, (void *)(gm_skvqiv7no78c2));
        GM_ST(uintptr_t, (uint8_t *)&t201 + 8, (uintptr_t)(8));
        gm_b0_1 t202 = gm_f_d_check_store_ldgo1fg((void *)NULL, cx, NULL, t198, t199, t200, t201, ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    void * t203 = gm_l1;
    gm_closure t204 = ((gm_closure){(void *)gm_f_fn_450cirs, (void *)&F.h, cx});
    gm_f_for_each_child_fm49s73(t203, t204);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_d_held_nduio4j(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, void * a3) {
    struct gm_fr_d_held_nduio4j F;
    void * gm_l0; /* s */
    void * gm_l1; /* e */
    uint8_t gm_l2; /* inside */
    void * gm_l3; /* out */
    void * gm_l4; /* p */
    uint32_t gm_l5; /* l */
    gm_tao63pk3r18v5 gm_l6; /* sl */
    uint32_t gm_l7; /* l */
    void * gm_l8; /* x */
    void * gm_l9; /* x */
    gm_b16_8 gm_l10; /* xs */
    gm_b16_8 gm_l11; /* xs */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    gm_ttggq7ng4kp3p gm_l15; /* v */
    void * gm_l16; /* x */
    void * gm_l17; /* b */
    void * gm_l18; /* t */
    gm_trl2khmuphncd gm_l19; /* x */
    void * gm_l20; /* el */
    GM_STACK_CHECK(&F, 889u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t9805qtonbsag gm_l21;
    gm_tftb1odc739t5 gm_l22;
    uint8_t gm_l23;
    gm_t53at80pfdus3 gm_l24;
    gm_t9805qtonbsag gm_l25;
    gm_tftb1odc739t5 gm_l26;
    void * t33 = gm_l1;
    {
        {
            uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)t33) + 8) + 0));
            uint8_t t35 = (uint8_t)(t34 == ((uint8_t)24ULL));
            if (!t35) {
                goto gm_lb2;
            }
            void * t36 = GM_LD(void *, ((((uint8_t *)t33) + 8) + 8));
            gm_l4 = t36;
            {
                {
                    void * t37 = gm_l0;
                    void * t38 = gm_l4;
                    gm_t9805qtonbsag t39 = gm_f_d_root_a9vujp4(t37, t38);
                    gm_l21 = t39;
                    uint8_t t40 = (uint8_t)((((uint8_t *)&gm_l21))[4] != 0);
                    if (!t40) {
                        goto gm_lb4;
                    }
                    uint32_t t41 = GM_LD(uint32_t, ((uint8_t *)&gm_l21));
                    gm_l5 = t41;
                    void * t42 = gm_l3;
                    void * t43 = gm_l1;
                    gm_tl874t2atnlh3 t44 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t43) + 72));
                    uint32_t t45 = gm_l5;
                    uint8_t t46 = gm_l2;
                    gm_t3u9hrtrhq8ch t47;
                    GM_ST(uint8_t, (uint8_t *)&t47 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t47 + 18, 0);
                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t47 + 0), t44);
                    GM_ST(uint32_t, ((uint8_t *)&t47 + 12), t45);
                    GM_ST(uint8_t, ((uint8_t *)&t47 + 16), t46);
                    gm_tftb1odc739t5 t48 = gm_f_push_pqaml2p(t42, &t47);
                    gm_l22 = t48;
                    uint8_t t49 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                    if (!t49) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 1, gm_srcb2bqqsnukmrd, 157, 46);
                    goto gm_lb3;
                }
gm_lb4:;
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)t33) + 8) + 0));
            uint8_t t51 = (uint8_t)(t50 == ((uint8_t)21ULL));
            if (!t51) {
                goto gm_lb5;
            }
            {
                memmove(((uint8_t *)&gm_l6), ((((uint8_t *)t33) + 8) + 8), 32);
            }
            {
                void * t52 = gm_l0;
                void * t53 = (void *)(((uint8_t *)t52) + 8);
                void * t54 = GM_LD(void *, ((uint8_t *)&gm_l6));
                gm_t88rcucg1b7uk t55 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t54));
                gm_f_kind_jcnt4hm((void *)&gm_l24, t53, GM_CONV(gm_csguankko4lmhd, t55));
            }
            {
                {
                    uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                    uint8_t t57 = (uint8_t)(t56 == ((uint8_t)11ULL));
                    if (!t57) {
                        goto gm_lb7;
                    }
                    gm_l23 = ((uint8_t)1);
                    goto gm_lb6;
                }
gm_lb7:;
                {
                    gm_l23 = ((uint8_t)0);
                    goto gm_lb6;
                }
gm_lb8:;
                gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 158, 26);
            }
gm_lb6:;
            uint8_t t58 = gm_l23;
            if (t58) {
                {
                    {
                        void * t59 = gm_l0;
                        void * t60 = GM_LD(void *, ((uint8_t *)&gm_l6));
                        gm_t9805qtonbsag t61 = gm_f_d_root_a9vujp4(t59, t60);
                        gm_l25 = t61;
                        uint8_t t62 = (uint8_t)((((uint8_t *)&gm_l25))[4] != 0);
                        if (!t62) {
                            goto gm_lb10;
                        }
                        uint32_t t63 = GM_LD(uint32_t, ((uint8_t *)&gm_l25));
                        gm_l7 = t63;
                        void * t64 = gm_l3;
                        void * t65 = gm_l1;
                        gm_tl874t2atnlh3 t66 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t65) + 72));
                        uint32_t t67 = gm_l7;
                        uint8_t t68 = gm_l2;
                        gm_t3u9hrtrhq8ch t69;
                        GM_ST(uint8_t, (uint8_t *)&t69 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&t69 + 18, 0);
                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t69 + 0), t66);
                        GM_ST(uint32_t, ((uint8_t *)&t69 + 12), t67);
                        GM_ST(uint8_t, ((uint8_t *)&t69 + 16), t68);
                        gm_tftb1odc739t5 t70 = gm_f_push_pqaml2p(t64, &t69);
                        gm_l26 = t70;
                        uint8_t t71 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                        if (!t71) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_srcb2bqqsnukmrd, 159, 42);
                        goto gm_lb9;
                    }
gm_lb10:;
                }
gm_lb9:;
            }
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t72 = GM_LD(uint8_t, ((((uint8_t *)t33) + 8) + 0));
            uint8_t t73 = (uint8_t)(t72 == ((uint8_t)32ULL));
            if (!t73) {
                goto gm_lb11;
            }
            void * t74 = GM_LD(void *, (((((uint8_t *)t33) + 8) + 8) + 8));
            gm_l8 = t74;
            void * t75 = gm_l0;
            void * t76 = gm_l8;
            uint8_t t77 = gm_l2;
            void * t78 = gm_l3;
            gm_b0_1 t79 = gm_f_d_held_nduio4j((void *)NULL, cx, NULL, t75, t76, t77, t78);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb11:;
        {
            uint8_t t80 = GM_LD(uint8_t, ((((uint8_t *)t33) + 8) + 0));
            uint8_t t81 = (uint8_t)(t80 == ((uint8_t)39ULL));
            if (!t81) {
                goto gm_lb12;
            }
            void * t82 = GM_LD(void *, ((((uint8_t *)t33) + 8) + 8));
            gm_l9 = t82;
            void * t83 = gm_l0;
            void * t84 = gm_l9;
            void * t85 = gm_l3;
            gm_b0_1 t86 = gm_f_d_held_nduio4j((void *)NULL, cx, NULL, t83, t84, ((uint8_t)1), t85);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb12:;
        {
            {
                {
                    uint8_t t87 = GM_LD(uint8_t, ((((uint8_t *)t33) + 8) + 0));
                    uint8_t t88 = (uint8_t)(t87 == ((uint8_t)35ULL));
                    if (!t88) {
                        goto gm_lb15;
                    }
                    gm_b16_8 t89 = GM_LD(gm_b16_8, ((((uint8_t *)t33) + 8) + 8));
                    gm_l10 = t89;
                    goto gm_lb14;
                }
gm_lb15:;
                {
                    uint8_t t90 = GM_LD(uint8_t, ((((uint8_t *)t33) + 8) + 0));
                    uint8_t t91 = (uint8_t)(t90 == ((uint8_t)36ULL));
                    if (!t91) {
                        goto gm_lb16;
                    }
                    gm_b16_8 t92 = GM_LD(gm_b16_8, ((((uint8_t *)t33) + 8) + 8));
                    gm_l10 = t92;
                    goto gm_lb14;
                }
gm_lb16:;
                goto gm_lb13;
            }
gm_lb14:;
            gm_l13 = ((uint64_t)0ULL);
            uint64_t t93 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
            gm_l12 = t93;
            {
                for (;;) {
                    uint64_t t94 = gm_l13;
                    uint64_t t95 = gm_l12;
                    uint8_t t96 = (t94 < t95);
                    if (!t96) {
                        goto gm_lb17;
                    }
                    {
                        {
                            {
                                uint64_t t97 = gm_l13;
                                gm_l14 = t97;
                                void * t98 = gm_l0;
                                void * t99 = GM_LD(void *, ((uint8_t *)&gm_l10));
                                uint64_t t100 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                uint64_t t101 = gm_l14;
                                void * t102 = (void *)(((uint8_t *)t99) + (uintptr_t)t101 * 88);
                                void * t103 = gm_l3;
                                gm_b0_1 t104 = gm_f_d_held_nduio4j((void *)NULL, cx, NULL, t98, t102, ((uint8_t)1), t103);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb20;
                            }
gm_lb21:;
                            gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 163, 39);
                        }
gm_lb20:;
                    }
gm_lb19:;
                    uint64_t t105 = gm_l13;
                    uint64_t r106; bool o107 = __builtin_add_overflow(t105, ((uint64_t)1ULL), &r106);
                    uint64_t t108 = r106;
                    gm_l13 = t108;
                }
            }
gm_lb17:;
            goto gm_lb1;
        }
gm_lb13:;
        {
            uint8_t t109 = GM_LD(uint8_t, ((((uint8_t *)t33) + 8) + 0));
            uint8_t t110 = (uint8_t)(t109 == ((uint8_t)38ULL));
            if (!t110) {
                goto gm_lb22;
            }
            gm_ttggq7ng4kp3p t111 = GM_LD(gm_ttggq7ng4kp3p, ((((uint8_t *)t33) + 8) + 8));
            gm_l15 = t111;
            {
                {
                    uint8_t t112 = (uint8_t)(GM_LD(void *, (((uint8_t *)&gm_l15) + 8)) != 0);
                    if (!t112) {
                        goto gm_lb24;
                    }
                    void * t113 = GM_LD(void *, (((uint8_t *)&gm_l15) + 8));
                    gm_l16 = t113;
                    void * t114 = gm_l0;
                    void * t115 = gm_l16;
                    void * t116 = gm_l3;
                    gm_b0_1 t117 = gm_f_d_held_nduio4j((void *)NULL, cx, NULL, t114, t115, ((uint8_t)1), t116);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb23;
                }
gm_lb24:;
            }
gm_lb23:;
            goto gm_lb1;
        }
gm_lb22:;
        {
            uint8_t t118 = GM_LD(uint8_t, ((((uint8_t *)t33) + 8) + 0));
            uint8_t t119 = (uint8_t)(t118 == ((uint8_t)46ULL));
            if (!t119) {
                goto gm_lb25;
            }
            void * t120 = GM_LD(void *, ((((uint8_t *)t33) + 8) + 8));
            gm_l17 = t120;
            {
                {
                    void * t121 = gm_l17;
                    uint8_t t122 = (uint8_t)(GM_LD(void *, (((uint8_t *)t121) + 16)) != 0);
                    if (!t122) {
                        goto gm_lb27;
                    }
                    void * t123 = GM_LD(void *, (((uint8_t *)t121) + 16));
                    gm_l18 = t123;
                    void * t124 = gm_l0;
                    void * t125 = gm_l18;
                    uint8_t t126 = gm_l2;
                    void * t127 = gm_l3;
                    gm_b0_1 t128 = gm_f_d_held_nduio4j((void *)NULL, cx, NULL, t124, t125, t126, t127);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb26;
                }
gm_lb27:;
            }
gm_lb26:;
            goto gm_lb1;
        }
gm_lb25:;
        {
            uint8_t t129 = GM_LD(uint8_t, ((((uint8_t *)t33) + 8) + 0));
            uint8_t t130 = (uint8_t)(t129 == ((uint8_t)47ULL));
            if (!t130) {
                goto gm_lb28;
            }
            memmove(((uint8_t *)&gm_l19), ((((uint8_t *)t33) + 8) + 8), 32);
            void * t131 = gm_l0;
            void * t132 = GM_LD(void *, (((uint8_t *)&gm_l19) + 16));
            uint8_t t133 = gm_l2;
            void * t134 = gm_l3;
            gm_b0_1 t135 = gm_f_d_held_nduio4j((void *)NULL, cx, NULL, t131, t132, t133, t134);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                {
                    uint8_t t136 = (uint8_t)(GM_LD(void *, (((uint8_t *)&gm_l19) + 24)) != 0);
                    if (!t136) {
                        goto gm_lb30;
                    }
                    void * t137 = GM_LD(void *, (((uint8_t *)&gm_l19) + 24));
                    gm_l20 = t137;
                    void * t138 = gm_l0;
                    void * t139 = gm_l20;
                    uint8_t t140 = gm_l2;
                    void * t141 = gm_l3;
                    gm_b0_1 t142 = gm_f_d_held_nduio4j((void *)NULL, cx, NULL, t138, t139, t140, t141);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb29;
                }
gm_lb30:;
            }
gm_lb29:;
            goto gm_lb1;
        }
gm_lb28:;
        {
            goto gm_lb1;
        }
gm_lb31:;
        gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 156, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_d_report_fdjjedi(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint32_t a2, gm_b16_8 a3, uint8_t a4) {
    struct gm_fr_d_report_fdjjedi F;
    void * gm_l0; /* w */
    gm_tl874t2atnlh3 gm_l1; /* at */
    uint32_t gm_l2; /* l */
    gm_b16_8 gm_l3; /* why */
    uint8_t gm_l4; /* certain */
    uint8_t gm_l6; /* hidden */
    GM_STACK_CHECK(&F, 405u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    uint8_t gm_l7;
    uint8_t gm_l8;
    uint8_t gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 8));
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 24));
        uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t3) + 24) + 8));
        uint32_t t6 = gm_l2;
        uint64_t t7 = (uint64_t)t6;
        if ((uintptr_t)t7 >= (uintptr_t)(t5)) gm_trap_index((uintptr_t)t7, t5, gm_srcb2bqqsnukmrd, 175, 14);
        memmove(((uint8_t *)&F.l5), (((uint8_t *)t4) + (uintptr_t)t7 * 40), 40);
    }
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
    gm_b16_8 t9;
    GM_ST(void *, (uint8_t *)&t9, (void *)(gm_s1f440st4mnkp));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(1));
    uint8_t t10 = gm_f_starts_with_6vvt01e(t8, t9);
    if (t10) {
        gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
        gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)gm_cs1qbc2gaqs));
        uint8_t t13 = (!gm_eq0iqiubfm7e(((uint8_t *)&t11), ((uint8_t *)&t12)));
        gm_l7 = t13;
    } else {
        gm_l7 = ((uint8_t)0);
    }
    uint8_t t14 = gm_l7;
    gm_l6 = t14;
    gm_tl874t2atnlh3 t15 = gm_l1;
    gm_tl874t2atnlh3 t16 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
    uint8_t t17 = (gm_eqff85ac10o3(((uint8_t *)&t15), ((uint8_t *)&t16)));
    if (t17) {
        gm_l9 = ((uint8_t)1);
    } else {
        uint8_t t18 = gm_l6;
        gm_l9 = t18;
    }
    uint8_t t19 = gm_l9;
    if (t19) {
        gm_l8 = ((uint8_t)1);
    } else {
        gm_tl874t2atnlh3 t20 = gm_l1;
        void * t21 = gm_l0;
        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)t21) + 72));
        uint8_t t23 = 0;
        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t22) + 8)); i++) if (gm_eqff85ac10o3(((uint8_t *)GM_LD(void *, ((uint8_t *)&t22))) + i * 12, ((uint8_t *)&t20))) { t23 = 1; break; }
        uint8_t t24 = (uint8_t)(t23);
        gm_l8 = t24;
    }
    uint8_t t25 = gm_l8;
    if (t25) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t26 = gm_l0;
    void * t27 = (void *)(((uint8_t *)t26) + 72);
    gm_tl874t2atnlh3 t28 = gm_l1;
    gm_tftb1odc739t5 t29 = gm_f_push_0mk7udh(t27, GM_CONV(gm_cseahvg8v7p0vs, t28));
    gm_l10 = t29;
    uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srcb2bqqsnukmrd, 178, 5);
    {
        void * t31 = gm_l0;
        void * t32 = GM_LD(void *, (((uint8_t *)t31) + 64));
        gm_tl874t2atnlh3 t33 = gm_l1;
        gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
        gm_tl874t2atnlh3 t35 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l5) + 24));
        gm_b16_8 t36 = gm_l3;
        uint8_t t37 = gm_l4;
        gm_tg9l3k9j9m9b9 t38;
        GM_ST(uint32_t, (uint8_t *)&t38 + 12, 0);
        GM_ST(uint32_t, (uint8_t *)&t38 + 44, 0);
        GM_ST(uint8_t, (uint8_t *)&t38 + 65, 0);
        GM_ST(uint16_t, (uint8_t *)&t38 + 66, 0);
        GM_ST(uint32_t, (uint8_t *)&t38 + 68, 0);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t38 + 0), t33);
        GM_ST(gm_b16_8, ((uint8_t *)&t38 + 16), t34);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t38 + 32), t35);
        GM_ST(gm_b16_8, ((uint8_t *)&t38 + 48), t36);
        GM_ST(uint8_t, ((uint8_t *)&t38 + 64), t37);
        gm_tftb1odc739t5 t39 = gm_f_push_hbmq4bj(t32, &t38);
        gm_l11 = t39;
    }
    uint8_t t40 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
    if (!t40) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srcb2bqqsnukmrd, 179, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_d_root_a9vujp4(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_d_root_a9vujp4 F;
    void * gm_l0; /* s */
    void * gm_l1; /* e */
    uint32_t gm_l2; /* l */
    void * gm_l3; /* base */
    void * gm_l4; /* base */
    gm_tao63pk3r18v5 gm_l5; /* sl */
    GM_STACK_CHECK(&F, 392u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l6;
    gm_t53at80pfdus3 gm_l7;
    uint8_t gm_l8;
    gm_t53at80pfdus3 gm_l9;
    void * t16 = gm_l1;
    {
        {
            uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)15ULL));
            if (!t18) {
                goto gm_lb2;
            }
            uint32_t t19 = GM_LD(uint32_t, ((((uint8_t *)t16) + 8) + 8));
            gm_l2 = t19;
            uint32_t t20 = gm_l2;
            gm_t9805qtonbsag t21;
            GM_ST(uint8_t, (uint8_t *)&t21 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t21 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t21), t20);
            ((uint8_t *)&t21)[4] = 1;
            F.ret = t21;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
            uint8_t t23 = (uint8_t)(t22 == ((uint8_t)19ULL));
            if (!t23) {
                goto gm_lb3;
            }
            void * t24 = GM_LD(void *, ((((uint8_t *)t16) + 8) + 8));
            gm_l3 = t24;
            void * t25 = gm_l0;
            void * t26 = gm_l3;
            gm_t9805qtonbsag t27 = gm_f_d_root_a9vujp4(t25, t26);
            F.ret = t27;
            goto gm_fret;
        }
gm_lb3:;
        {
            uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
            uint8_t t29 = (uint8_t)(t28 == ((uint8_t)20ULL));
            if (!t29) {
                goto gm_lb4;
            }
            void * t30 = GM_LD(void *, ((((uint8_t *)t16) + 8) + 8));
            gm_l4 = t30;
            void * t31 = gm_l0;
            void * t32 = (void *)(((uint8_t *)t31) + 8);
            void * t33 = gm_l4;
            gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t33));
            gm_f_kind_jcnt4hm((void *)&gm_l7, t32, GM_CONV(gm_csguankko4lmhd, t34));
            {
                {
                    uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                    uint8_t t36 = (uint8_t)(t35 == ((uint8_t)11ULL));
                    if (!t36) {
                        goto gm_lb6;
                    }
                    gm_l6 = ((uint8_t)1);
                    goto gm_lb5;
                }
gm_lb6:;
                {
                    uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                    uint8_t t38 = (uint8_t)(t37 == ((uint8_t)20ULL));
                    if (!t38) {
                        goto gm_lb7;
                    }
                    gm_l6 = ((uint8_t)1);
                    goto gm_lb5;
                }
gm_lb7:;
                {
                    gm_l6 = ((uint8_t)0);
                    goto gm_lb5;
                }
gm_lb8:;
                gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 121, 34);
            }
gm_lb5:;
            uint8_t t39 = gm_l6;
            if (t39) {
                void * t40 = gm_l0;
                void * t41 = gm_l4;
                gm_t9805qtonbsag t42 = gm_f_d_root_a9vujp4(t40, t41);
                F.ret = t42;
                goto gm_fret;
            }
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
            uint8_t t44 = (uint8_t)(t43 == ((uint8_t)21ULL));
            if (!t44) {
                goto gm_lb9;
            }
            {
                memmove(((uint8_t *)&gm_l5), ((((uint8_t *)t16) + 8) + 8), 32);
            }
            {
                void * t45 = gm_l0;
                void * t46 = (void *)(((uint8_t *)t45) + 8);
                void * t47 = GM_LD(void *, ((uint8_t *)&gm_l5));
                gm_t88rcucg1b7uk t48 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t47));
                gm_f_kind_jcnt4hm((void *)&gm_l9, t46, GM_CONV(gm_csguankko4lmhd, t48));
            }
            {
                {
                    uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
                    uint8_t t50 = (uint8_t)(t49 == ((uint8_t)11ULL));
                    if (!t50) {
                        goto gm_lb11;
                    }
                    gm_l8 = ((uint8_t)1);
                    goto gm_lb10;
                }
gm_lb11:;
                {
                    uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
                    uint8_t t52 = (uint8_t)(t51 == ((uint8_t)20ULL));
                    if (!t52) {
                        goto gm_lb12;
                    }
                    gm_l8 = ((uint8_t)1);
                    goto gm_lb10;
                }
gm_lb12:;
                {
                    gm_l8 = ((uint8_t)0);
                    goto gm_lb10;
                }
gm_lb13:;
                gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 122, 26);
            }
gm_lb10:;
            uint8_t t53 = gm_l8;
            if (t53) {
                void * t54 = gm_l0;
                void * t55 = GM_LD(void *, ((uint8_t *)&gm_l5));
                gm_t9805qtonbsag t56 = gm_f_d_root_a9vujp4(t54, t55);
                F.ret = t56;
                goto gm_fret;
            }
            goto gm_lb1;
        }
gm_lb9:;
        {
            goto gm_lb1;
        }
gm_lb14:;
        gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 118, 5);
    }
gm_lb1:;
    gm_t9805qtonbsag t57;
    memset(&t57, 0, sizeof t57);
    F.ret = t57;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_d_stmt_a97l2r1(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_d_stmt_a97l2r1 F;
    void * gm_l0; /* w */
    gm_trcmmd12cefb1 gm_l1; /* st */
    uint32_t gm_l2; /* l */
    uint32_t gm_l3; /* l */
    void * gm_l4; /* x */
    gm_b16_8 gm_l5; /* ls */
    void * gm_l6; /* x */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_t9805qtonbsag gm_l9; /* l */
    uint32_t gm_l10; /* id */
    void * gm_l11; /* place */
    void * gm_l12; /* value */
    gm_t913bb735pvt5 gm_l13; /* t */
    gm_b16_8 gm_l14; /* places */
    void * gm_l15; /* value */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    void * gm_l18; /* p */
    void * gm_l19; /* x */
    void * gm_l20; /* x */
    void * gm_l21; /* body */
    GM_STACK_CHECK(&F, 778u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t0id5pe5tq2j8 gm_l22;
    {
        {
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t27 = (uint8_t)(t26 == ((uint8_t)0ULL));
            if (!t27) {
                goto gm_lb2;
            }
            uint32_t t28 = GM_LD(uint32_t, (((uint8_t *)a1) + 8));
            gm_l2 = t28;
            uint8_t t29 = (uint8_t)(GM_LD(void *, ((((uint8_t *)a1) + 8) + 8)) != 0);
            if (t29) {
                goto gm_lb2;
            }
            void * t30 = gm_l0;
            uint32_t t31 = gm_l2;
            gm_f_d_declare_nctg7jr(t30, t31);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t33 = (uint8_t)(t32 == ((uint8_t)0ULL));
            if (!t33) {
                goto gm_lb3;
            }
            uint32_t t34 = GM_LD(uint32_t, (((uint8_t *)a1) + 8));
            gm_l3 = t34;
            uint8_t t35 = (uint8_t)(GM_LD(void *, ((((uint8_t *)a1) + 8) + 8)) != 0);
            if (!t35) {
                goto gm_lb3;
            }
            void * t36 = GM_LD(void *, ((((uint8_t *)a1) + 8) + 8));
            gm_l4 = t36;
            void * t37 = gm_l0;
            void * t38 = gm_l4;
            gm_b0_1 t39 = gm_f_d_expr_bbktnn5((void *)NULL, cx, NULL, t37, t38, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t40 = gm_l0;
            uint32_t t41 = gm_l3;
            gm_f_d_declare_nctg7jr(t40, t41);
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t43 = (uint8_t)(t42 == ((uint8_t)1ULL));
            if (!t43) {
                goto gm_lb4;
            }
            gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
            gm_l5 = t44;
            void * t45 = GM_LD(void *, ((((uint8_t *)a1) + 8) + 16));
            gm_l6 = t45;
            void * t46 = gm_l0;
            void * t47 = gm_l6;
            gm_b0_1 t48 = gm_f_d_expr_bbktnn5((void *)NULL, cx, NULL, t46, t47, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t49 = gm_l5;
            gm_l7 = t49;
            gm_l8 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t50 = gm_l8;
                    uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    uint8_t t52 = (t50 < t51);
                    if (!t52) {
                        goto gm_lb5;
                    }
                    {
                        void * t53 = GM_LD(void *, ((uint8_t *)&gm_l7));
                        uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                        uint64_t t55 = gm_l8;
                        {
                            {
                                gm_t9805qtonbsag t56 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)t53) + (uintptr_t)t55 * 8));
                                gm_l9 = t56;
                                {
                                    {
                                        uint8_t t57 = (uint8_t)((((uint8_t *)&gm_l9))[4] != 0);
                                        if (!t57) {
                                            goto gm_lb11;
                                        }
                                        uint32_t t58 = GM_LD(uint32_t, ((uint8_t *)&gm_l9));
                                        gm_l10 = t58;
                                        void * t59 = gm_l0;
                                        uint32_t t60 = gm_l10;
                                        gm_f_d_declare_nctg7jr(t59, t60);
                                        goto gm_lb10;
                                    }
gm_lb11:;
                                }
gm_lb10:;
                                goto gm_lb8;
                            }
gm_lb9:;
                            gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 350, 13);
                        }
gm_lb8:;
                    }
gm_lb7:;
                    uint64_t t61 = gm_l8;
                    uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                    uint64_t t64 = r62;
                    gm_l8 = t64;
                }
            }
gm_lb5:;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t66 = (uint8_t)(t65 == ((uint8_t)2ULL));
            if (!t66) {
                goto gm_lb12;
            }
            void * t67 = GM_LD(void *, (((uint8_t *)a1) + 8));
            gm_l11 = t67;
            void * t68 = GM_LD(void *, ((((uint8_t *)a1) + 8) + 8));
            gm_l12 = t68;
            void * t69 = gm_l0;
            void * t70 = gm_l11;
            gm_b0_1 t71 = gm_f_d_expr_bbktnn5((void *)NULL, cx, NULL, t69, t70, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t72 = gm_l0;
            void * t73 = gm_l12;
            gm_b0_1 t74 = gm_f_d_expr_bbktnn5((void *)NULL, cx, NULL, t72, t73, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                {
                    void * t75 = gm_l0;
                    void * t76 = gm_l11;
                    gm_f_d_storage_kqhfs5d((void *)&gm_l22, t75, t76);
                    uint8_t t77 = (uint8_t)((((uint8_t *)&gm_l22))[16] != 0);
                    if (!t77) {
                        goto gm_lb14;
                    }
                    gm_t913bb735pvt5 t78 = GM_LD(gm_t913bb735pvt5, ((uint8_t *)&gm_l22));
                    gm_l13 = t78;
                    void * t79 = gm_l0;
                    void * t80 = gm_l12;
                    int64_t t81 = GM_LD(int64_t, ((uint8_t *)&gm_l13));
                    gm_b16_8 t82;
                    GM_ST(void *, (uint8_t *)&t82, (void *)(gm_sjssolp3qfpqg));
                    GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(6));
                    uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 8));
                    gm_b0_1 t84 = gm_f_d_check_store_ldgo1fg((void *)NULL, cx, NULL, t79, t80, t81, t82, t83);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb13;
                }
gm_lb14:;
            }
gm_lb13:;
            goto gm_lb1;
        }
gm_lb12:;
        {
            uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t86 = (uint8_t)(t85 == ((uint8_t)3ULL));
            if (!t86) {
                goto gm_lb15;
            }
            gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
            gm_l14 = t87;
            void * t88 = GM_LD(void *, ((((uint8_t *)a1) + 8) + 16));
            gm_l15 = t88;
            gm_b16_8 t89 = gm_l14;
            gm_l16 = t89;
            gm_l17 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t90 = gm_l17;
                    uint64_t t91 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                    uint8_t t92 = (t90 < t91);
                    if (!t92) {
                        goto gm_lb16;
                    }
                    {
                        void * t93 = GM_LD(void *, ((uint8_t *)&gm_l16));
                        uint64_t t94 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                        uint64_t t95 = gm_l17;
                        {
                            {
                                void * t96 = GM_LD(void *, (((uint8_t *)t93) + (uintptr_t)t95 * 8));
                                gm_l18 = t96;
                                {
                                    {
                                        uint8_t t97 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l18)) != 0);
                                        if (!t97) {
                                            goto gm_lb22;
                                        }
                                        void * t98 = GM_LD(void *, ((uint8_t *)&gm_l18));
                                        gm_l19 = t98;
                                        void * t99 = gm_l0;
                                        void * t100 = gm_l19;
                                        gm_b0_1 t101 = gm_f_d_expr_bbktnn5((void *)NULL, cx, NULL, t99, t100, ((uint8_t)0));
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb21;
                                    }
gm_lb22:;
                                }
gm_lb21:;
                                goto gm_lb19;
                            }
gm_lb20:;
                            gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 360, 13);
                        }
gm_lb19:;
                    }
gm_lb18:;
                    uint64_t t102 = gm_l17;
                    uint64_t r103; bool o104 = __builtin_add_overflow(t102, ((uint64_t)1ULL), &r103);
                    uint64_t t105 = r103;
                    gm_l17 = t105;
                }
            }
gm_lb16:;
            void * t106 = gm_l0;
            void * t107 = gm_l15;
            gm_b0_1 t108 = gm_f_d_expr_bbktnn5((void *)NULL, cx, NULL, t106, t107, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb15:;
        {
            uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t110 = (uint8_t)(t109 == ((uint8_t)4ULL));
            if (!t110) {
                goto gm_lb23;
            }
            void * t111 = GM_LD(void *, (((uint8_t *)a1) + 8));
            gm_l20 = t111;
            void * t112 = gm_l0;
            void * t113 = gm_l20;
            gm_b0_1 t114 = gm_f_d_expr_bbktnn5((void *)NULL, cx, NULL, t112, t113, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb23:;
        {
            uint8_t t115 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t116 = (uint8_t)(t115 == ((uint8_t)5ULL));
            if (!t116) {
                goto gm_lb24;
            }
            void * t117 = GM_LD(void *, ((((uint8_t *)a1) + 8) + 8));
            gm_l21 = t117;
            void * t118 = gm_l0;
            void * t119 = gm_l21;
            gm_b0_1 t120 = gm_f_d_expr_bbktnn5((void *)NULL, cx, NULL, t118, t119, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb24:;
        gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 342, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_d_stmts_frrm4kl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_d_stmts_frrm4kl F;
    void * gm_l0; /* w */
    gm_b16_8 gm_l1; /* stmts */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    gm_trcmmd12cefb1 gm_l5; /* st */
    void * gm_l6; /* place */
    void * gm_l7; /* value */
    gm_t913bb735pvt5 gm_l8; /* t */
    GM_STACK_CHECK(&F, 495u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tfquseojpqg7b gm_l9;
    gm_t0id5pe5tq2j8 gm_l10;
    gm_b16_8 t11 = gm_l1;
    gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
    gm_l2 = t12;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t13 = gm_l3;
            uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t15 = (t13 < t14);
            if (!t15) {
                goto gm_lb1;
            }
            {
                {
                    uint64_t t16 = gm_l3;
                    void * t17 = GM_LD(void *, ((uint8_t *)&gm_l2));
                    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                    uint64_t t19 = gm_l3;
                    gm_trcmmd12cefb1 t20 = GM_LD(gm_trcmmd12cefb1, (((uint8_t *)t17) + (uintptr_t)t19 * 32));
                    GM_ST(uint64_t, ((uint8_t *)&gm_l9 + 0), t16);
                    GM_ST(gm_trcmmd12cefb1, ((uint8_t *)&gm_l9 + 8), t20);
                }
                {
                    {
                        uint64_t t21 = GM_LD(uint64_t, ((uint8_t *)&gm_l9));
                        gm_l4 = t21;
                        {
                            memmove(((uint8_t *)&gm_l5), (((uint8_t *)&gm_l9) + 8), 32);
                        }
                        {
                            {
                                uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                uint8_t t23 = (uint8_t)(t22 == ((uint8_t)2ULL));
                                if (!t23) {
                                    goto gm_lb7;
                                }
                                void * t24 = GM_LD(void *, (((uint8_t *)&gm_l5) + 8));
                                gm_l6 = t24;
                                void * t25 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 8));
                                gm_l7 = t25;
                                {
                                    {
                                        void * t26 = gm_l0;
                                        void * t27 = gm_l6;
                                        gm_f_d_storage_kqhfs5d((void *)&gm_l10, t26, t27);
                                        uint8_t t28 = (uint8_t)((((uint8_t *)&gm_l10))[16] != 0);
                                        if (!t28) {
                                            goto gm_lb9;
                                        }
                                        gm_t913bb735pvt5 t29 = GM_LD(gm_t913bb735pvt5, ((uint8_t *)&gm_l10));
                                        gm_l8 = t29;
                                        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 8));
                                        uint8_t t31 = (uint8_t)!t30;
                                        if (!t31) {
                                            goto gm_lb9;
                                        }
                                        void * t32 = gm_l6;
                                        void * t33 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                        uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                        uint64_t t35 = gm_l4;
                                        uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                                        if (o37) gm_trap("integer overflow in `+`", gm_srcb2bqqsnukmrd, 309, 90);
                                        uint64_t t38 = r36;
                                        uint64_t t39 = (uintptr_t)t38;
                                        uint64_t t40 = (uintptr_t)t34;
                                        if (t39 > t40 || t40 > (uintptr_t)t34) gm_trap_slice(t39, t40, t34, gm_srcb2bqqsnukmrd, 309, 84);
                                        uint64_t t41 = ((uintptr_t)t34) - (uintptr_t)t38;
                                        gm_b16_8 t42;
                                        GM_ST(void *, (uint8_t *)&t42, (void *)(((uint8_t *)t33 + (uintptr_t)t38 * 32)));
                                        GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(t41));
                                        uint8_t t43 = gm_f_assigned_later_vf7s32b(t32, t42);
                                        if (!t43) {
                                            goto gm_lb9;
                                        }
                                        void * t44 = gm_l0;
                                        void * t45 = gm_l6;
                                        gm_b0_1 t46 = gm_f_d_expr_bbktnn5((void *)NULL, cx, NULL, t44, t45, ((uint8_t)0));
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        void * t47 = gm_l0;
                                        void * t48 = gm_l7;
                                        gm_b0_1 t49 = gm_f_d_expr_bbktnn5((void *)NULL, cx, NULL, t47, t48, ((uint8_t)0));
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb3;
                                    }
gm_lb9:;
                                }
gm_lb8:;
                                goto gm_lb6;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        {
                            void * t50 = gm_l0;
                            gm_b0_1 t51 = gm_f_d_stmt_a97l2r1((void *)NULL, cx, NULL, t50, &(*(gm_trcmmd12cefb1 *)((uint8_t *)&gm_l5)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 307, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t52 = gm_l3;
            uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)1ULL), &r53);
            uint64_t t55 = r53;
            gm_l3 = t55;
        }
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
__attribute__((aligned(4))) void gm_f_d_storage_kqhfs5d(void *gm_out, void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_d_storage_kqhfs5d F;
    void * gm_l0; /* w */
    void * gm_l1; /* p */
    uint32_t gm_l2; /* l */
    int64_t gm_l3; /* d */
    void * gm_l4; /* base */
    void * gm_l5; /* base */
    gm_tao63pk3r18v5 gm_l6; /* sl */
    GM_STACK_CHECK(&F, 543u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l7;
    gm_t53at80pfdus3 gm_l8;
    uint8_t gm_l9;
    gm_t53at80pfdus3 gm_l10;
    void * t21 = gm_l1;
    {
        {
            uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t21) + 8) + 0));
            uint8_t t23 = (uint8_t)(t22 == ((uint8_t)15ULL));
            if (!t23) {
                goto gm_lb2;
            }
            uint32_t t24 = GM_LD(uint32_t, ((((uint8_t *)t21) + 8) + 8));
            gm_l2 = t24;
            void * t25 = gm_l0;
            void * t26 = GM_LD(void *, (((uint8_t *)t25) + 16));
            uint64_t t27 = GM_LD(uint64_t, ((((uint8_t *)t25) + 16) + 8));
            uint32_t t28 = gm_l2;
            uint64_t t29 = (uint64_t)t28;
            if ((uintptr_t)t29 >= (uintptr_t)(t27)) gm_trap_index((uintptr_t)t29, t27, gm_srcb2bqqsnukmrd, 134, 18);
            int64_t t30 = GM_LD(int64_t, (((uint8_t *)t26) + (uintptr_t)t29 * 8));
            gm_l3 = t30;
            int64_t t31 = gm_l3;
            int64_t t32 = GM_LD(int64_t, ((uint8_t *)gm_cpn2np70vcf));
            uint8_t t33 = ((t31 == t32));
            if (t33) {
                memset(&(*(gm_t0id5pe5tq2j8 *)gm_out), 0, sizeof (*(gm_t0id5pe5tq2j8 *)gm_out));
                goto gm_fdone;
            }
            int64_t t34 = gm_l3;
            gm_t913bb735pvt5 t35;
            GM_ST(uint8_t, (uint8_t *)&t35 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 12, 0);
            GM_ST(int64_t, ((uint8_t *)&t35 + 0), t34);
            GM_ST(uint8_t, ((uint8_t *)&t35 + 8), ((uint8_t)1));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t0id5pe5tq2j8 *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t0id5pe5tq2j8 *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t0id5pe5tq2j8 *)gm_out) + 20, 0);
            GM_ST(gm_t913bb735pvt5, ((uint8_t *)&(*(gm_t0id5pe5tq2j8 *)gm_out)), t35);
            ((uint8_t *)&(*(gm_t0id5pe5tq2j8 *)gm_out))[16] = 1;
            goto gm_fdone;
        }
gm_lb2:;
        {
            {
                {
                    uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)t21) + 8) + 0));
                    uint8_t t37 = (uint8_t)(t36 == ((uint8_t)16ULL));
                    if (!t37) {
                        goto gm_lb5;
                    }
                    goto gm_lb4;
                }
gm_lb5:;
                {
                    uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)t21) + 8) + 0));
                    uint8_t t39 = (uint8_t)(t38 == ((uint8_t)17ULL));
                    if (!t39) {
                        goto gm_lb6;
                    }
                    goto gm_lb4;
                }
gm_lb6:;
                goto gm_lb3;
            }
gm_lb4:;
            int64_t t40 = GM_LD(int64_t, ((uint8_t *)gm_c9paf88rqhe));
            gm_t913bb735pvt5 t41;
            GM_ST(uint8_t, (uint8_t *)&t41 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t41 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t41 + 12, 0);
            GM_ST(int64_t, ((uint8_t *)&t41 + 0), t40);
            GM_ST(uint8_t, ((uint8_t *)&t41 + 8), ((uint8_t)1));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t0id5pe5tq2j8 *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t0id5pe5tq2j8 *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t0id5pe5tq2j8 *)gm_out) + 20, 0);
            GM_ST(gm_t913bb735pvt5, ((uint8_t *)&(*(gm_t0id5pe5tq2j8 *)gm_out)), t41);
            ((uint8_t *)&(*(gm_t0id5pe5tq2j8 *)gm_out))[16] = 1;
            goto gm_fdone;
        }
gm_lb3:;
        {
            uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)t21) + 8) + 0));
            uint8_t t43 = (uint8_t)(t42 == ((uint8_t)18ULL));
            if (!t43) {
                goto gm_lb7;
            }
            int64_t t44 = GM_LD(int64_t, ((uint8_t *)gm_c9paf88rqhe));
            gm_t913bb735pvt5 t45;
            GM_ST(uint8_t, (uint8_t *)&t45 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t45 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t45 + 12, 0);
            GM_ST(int64_t, ((uint8_t *)&t45 + 0), t44);
            GM_ST(uint8_t, ((uint8_t *)&t45 + 8), ((uint8_t)0));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t0id5pe5tq2j8 *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t0id5pe5tq2j8 *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t0id5pe5tq2j8 *)gm_out) + 20, 0);
            GM_ST(gm_t913bb735pvt5, ((uint8_t *)&(*(gm_t0id5pe5tq2j8 *)gm_out)), t45);
            ((uint8_t *)&(*(gm_t0id5pe5tq2j8 *)gm_out))[16] = 1;
            goto gm_fdone;
        }
gm_lb7:;
        {
            uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)t21) + 8) + 0));
            uint8_t t47 = (uint8_t)(t46 == ((uint8_t)19ULL));
            if (!t47) {
                goto gm_lb8;
            }
            void * t48 = GM_LD(void *, ((((uint8_t *)t21) + 8) + 8));
            gm_l4 = t48;
            void * t49 = gm_l0;
            void * t50 = gm_l4;
            gm_f_d_storage_kqhfs5d((void *)&(*(gm_t0id5pe5tq2j8 *)gm_out), t49, t50);
            goto gm_fdone;
        }
gm_lb8:;
        {
            uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)t21) + 8) + 0));
            uint8_t t52 = (uint8_t)(t51 == ((uint8_t)20ULL));
            if (!t52) {
                goto gm_lb9;
            }
            void * t53 = GM_LD(void *, ((((uint8_t *)t21) + 8) + 8));
            gm_l5 = t53;
            {
                void * t54 = gm_l0;
                void * t55 = GM_LD(void *, ((uint8_t *)t54));
                void * t56 = (void *)(((uint8_t *)t55) + 8);
                void * t57 = gm_l5;
                gm_t88rcucg1b7uk t58 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t57));
                gm_f_kind_jcnt4hm((void *)&gm_l8, t56, GM_CONV(gm_csguankko4lmhd, t58));
            }
            {
                {
                    uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                    uint8_t t60 = (uint8_t)(t59 == ((uint8_t)11ULL));
                    if (!t60) {
                        goto gm_lb11;
                    }
                    gm_l7 = ((uint8_t)1);
                    goto gm_lb10;
                }
gm_lb11:;
                {
                    uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                    uint8_t t62 = (uint8_t)(t61 == ((uint8_t)20ULL));
                    if (!t62) {
                        goto gm_lb12;
                    }
                    gm_l7 = ((uint8_t)1);
                    goto gm_lb10;
                }
gm_lb12:;
                {
                    gm_l7 = ((uint8_t)0);
                    goto gm_lb10;
                }
gm_lb13:;
                gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 142, 16);
            }
gm_lb10:;
            uint8_t t63 = gm_l7;
            if (t63) {
                void * t64 = gm_l0;
                void * t65 = gm_l5;
                gm_f_d_storage_kqhfs5d((void *)&(*(gm_t0id5pe5tq2j8 *)gm_out), t64, t65);
                goto gm_fdone;
            }
            {
                int64_t t66 = GM_LD(int64_t, ((uint8_t *)gm_c9paf88rqhe));
                gm_t913bb735pvt5 t67;
                GM_ST(uint8_t, (uint8_t *)&t67 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t67 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t67 + 12, 0);
                GM_ST(int64_t, ((uint8_t *)&t67 + 0), t66);
                GM_ST(uint8_t, ((uint8_t *)&t67 + 8), ((uint8_t)0));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t0id5pe5tq2j8 *)gm_out) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t0id5pe5tq2j8 *)gm_out) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t0id5pe5tq2j8 *)gm_out) + 20, 0);
                GM_ST(gm_t913bb735pvt5, ((uint8_t *)&(*(gm_t0id5pe5tq2j8 *)gm_out)), t67);
                ((uint8_t *)&(*(gm_t0id5pe5tq2j8 *)gm_out))[16] = 1;
                goto gm_fdone;
            }
        }
gm_lb9:;
        {
            uint8_t t68 = GM_LD(uint8_t, ((((uint8_t *)t21) + 8) + 0));
            uint8_t t69 = (uint8_t)(t68 == ((uint8_t)21ULL));
            if (!t69) {
                goto gm_lb14;
            }
            {
                memmove(((uint8_t *)&gm_l6), ((((uint8_t *)t21) + 8) + 8), 32);
            }
            {
                void * t70 = gm_l0;
                void * t71 = GM_LD(void *, ((uint8_t *)t70));
                void * t72 = (void *)(((uint8_t *)t71) + 8);
                void * t73 = GM_LD(void *, ((uint8_t *)&gm_l6));
                gm_t88rcucg1b7uk t74 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t73));
                gm_f_kind_jcnt4hm((void *)&gm_l10, t72, GM_CONV(gm_csguankko4lmhd, t74));
            }
            {
                {
                    uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                    uint8_t t76 = (uint8_t)(t75 == ((uint8_t)11ULL));
                    if (!t76) {
                        goto gm_lb16;
                    }
                    gm_l9 = ((uint8_t)1);
                    goto gm_lb15;
                }
gm_lb16:;
                {
                    uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                    uint8_t t78 = (uint8_t)(t77 == ((uint8_t)20ULL));
                    if (!t78) {
                        goto gm_lb17;
                    }
                    gm_l9 = ((uint8_t)1);
                    goto gm_lb15;
                }
gm_lb17:;
                {
                    gm_l9 = ((uint8_t)0);
                    goto gm_lb15;
                }
gm_lb18:;
                gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 146, 16);
            }
gm_lb15:;
            uint8_t t79 = gm_l9;
            if (t79) {
                void * t80 = gm_l0;
                void * t81 = GM_LD(void *, ((uint8_t *)&gm_l6));
                gm_f_d_storage_kqhfs5d((void *)&(*(gm_t0id5pe5tq2j8 *)gm_out), t80, t81);
                goto gm_fdone;
            }
            {
                int64_t t82 = GM_LD(int64_t, ((uint8_t *)gm_c9paf88rqhe));
                gm_t913bb735pvt5 t83;
                GM_ST(uint8_t, (uint8_t *)&t83 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t83 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t83 + 12, 0);
                GM_ST(int64_t, ((uint8_t *)&t83 + 0), t82);
                GM_ST(uint8_t, ((uint8_t *)&t83 + 8), ((uint8_t)0));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t0id5pe5tq2j8 *)gm_out) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t0id5pe5tq2j8 *)gm_out) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t0id5pe5tq2j8 *)gm_out) + 20, 0);
                GM_ST(gm_t913bb735pvt5, ((uint8_t *)&(*(gm_t0id5pe5tq2j8 *)gm_out)), t83);
                ((uint8_t *)&(*(gm_t0id5pe5tq2j8 *)gm_out))[16] = 1;
                goto gm_fdone;
            }
        }
gm_lb14:;
        {
            memset(&(*(gm_t0id5pe5tq2j8 *)gm_out), 0, sizeof (*(gm_t0id5pe5tq2j8 *)gm_out));
            goto gm_fdone;
        }
gm_lb19:;
        gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 132, 5);
    }
gm_lb1:;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t0id5pe5tq2j8 gm_f_d_storage_kqhfs5d_c(void * a0, void * a1) { gm_t0id5pe5tq2j8 gm_r; gm_f_d_storage_kqhfs5d(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) gm_b16_8 gm_f_dangling_message_s0d8vok(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_dangling_message_s0d8vok F;
    gm_b16_8 gm_l0; /* name */
    gm_b16_8 gm_l1; /* why */
    GM_STACK_CHECK(&F, 704u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 gm_l2;
    gm_b16_8 gm_l3;
    gm_tabb710d9mq2e gm_l4;
    gm_tabb710d9mq2e gm_l5;
    gm_tabb710d9mq2e gm_l6;
    gm_b16_8 t10 = gm_l0;
    gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)gm_cs1qbc2gaqs));
    uint8_t t12 = (gm_eq0iqiubfm7e(((uint8_t *)&t10), ((uint8_t *)&t11)));
    if (t12) {
        {
            {
                gm_b16_8 t13 = gm_l1;
                gm_b16_8 t14;
                GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sjssolp3qfpqg));
                GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(6));
                uint8_t t15 = (gm_eq0iqiubfm7e(((uint8_t *)&t13), ((uint8_t *)&t14)));
                if (!t15) {
                    goto gm_lb2;
                }
                gm_b16_8 t16;
                GM_ST(void *, (uint8_t *)&t16, (void *)(gm_stou2bl2ross2));
                GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(69));
                gm_l2 = t16;
                goto gm_lb1;
            }
gm_lb2:;
            {
                gm_b16_8 t17 = gm_l1;
                gm_b16_8 t18;
                GM_ST(void *, (uint8_t *)&t18, (void *)(gm_skvqiv7no78c2));
                GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(8));
                uint8_t t19 = (gm_eq0iqiubfm7e(((uint8_t *)&t17), ((uint8_t *)&t18)));
                if (!t19) {
                    goto gm_lb3;
                }
                gm_b16_8 t20;
                GM_ST(void *, (uint8_t *)&t20, (void *)(gm_scd7cqpmpn0p9));
                GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(77));
                gm_l2 = t20;
                goto gm_lb1;
            }
gm_lb3:;
            {
                gm_b16_8 t21;
                GM_ST(void *, (uint8_t *)&t21, (void *)(gm_s6hha9j1e16ru));
                GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(89));
                gm_l2 = t21;
                goto gm_lb1;
            }
gm_lb4:;
            gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 61, 16);
        }
gm_lb1:;
        gm_b16_8 t22 = gm_l2;
        F.ret = t22;
        goto gm_fret;
    }
    {
        {
            gm_b16_8 t23 = gm_l1;
            gm_b16_8 t24;
            GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sjssolp3qfpqg));
            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(6));
            uint8_t t25 = (gm_eq0iqiubfm7e(((uint8_t *)&t23), ((uint8_t *)&t24)));
            if (!t25) {
                goto gm_lb6;
            }
            {
                gm_b16_8 t26 = gm_l0;
                gm_b16_8 t27 = gm_l0;
                gm_t6j6smj7qoctu t28;
                GM_ST(gm_b16_8, ((uint8_t *)&t28 + 0), t26);
                GM_ST(gm_b16_8, ((uint8_t *)&t28 + 16), t27);
                gm_f_format_usfrdin((void *)&gm_l4, (void *)NULL, cx, NULL, &t28);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t29 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
            if (!t29) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 8, gm_srcb2bqqsnukmrd, 68, 21);
            gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
            gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
            gm_l3 = t31;
            goto gm_lb5;
        }
gm_lb6:;
        {
            gm_b16_8 t32 = gm_l1;
            gm_b16_8 t33;
            GM_ST(void *, (uint8_t *)&t33, (void *)(gm_skvqiv7no78c2));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(8));
            uint8_t t34 = (gm_eq0iqiubfm7e(((uint8_t *)&t32), ((uint8_t *)&t33)));
            if (!t34) {
                goto gm_lb7;
            }
            {
                gm_b16_8 t35 = gm_l0;
                gm_b16_8 t36 = gm_l0;
                gm_t6j6smj7qoctu t37;
                GM_ST(gm_b16_8, ((uint8_t *)&t37 + 0), t35);
                GM_ST(gm_b16_8, ((uint8_t *)&t37 + 16), t36);
                gm_f_format_ou6lg1k((void *)&gm_l5, (void *)NULL, cx, NULL, &t37);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t38 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
            if (!t38) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 8, gm_srcb2bqqsnukmrd, 69, 23);
            gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
            gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
            gm_l3 = t40;
            goto gm_lb5;
        }
gm_lb7:;
        {
            {
                gm_b16_8 t41 = gm_l0;
                gm_b16_8 t42 = gm_l0;
                gm_t6j6smj7qoctu t43;
                GM_ST(gm_b16_8, ((uint8_t *)&t43 + 0), t41);
                GM_ST(gm_b16_8, ((uint8_t *)&t43 + 16), t42);
                gm_f_format_e0k7mdb((void *)&gm_l6, (void *)NULL, cx, NULL, &t43);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t44 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
            if (!t44) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_srcb2bqqsnukmrd, 70, 14);
            gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
            gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
            gm_l3 = t46;
            goto gm_lb5;
        }
gm_lb8:;
        gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 67, 12);
    }
gm_lb5:;
    gm_b16_8 t47 = gm_l3;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_35etm6d(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_450cirs(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_possible_dangling_addres_d403vr0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_possible_dangling_addres_d403vr0 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* f */
    void * gm_l3; /* def */
    void * gm_l4; /* body */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint32_t gm_l10; /* p */
    GM_STACK_CHECK(&F, 668u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l12;
    {
        gm_trqfvmjtt4sij t12;
        gm_f_list_v9rkbv6((void *)&t12, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t12;
    }
    void * t13 = gm_l0;
    void * t14 = GM_LD(void *, (((uint8_t *)t13) + 384));
    uint64_t t15 = GM_LD(uint64_t, ((((uint8_t *)t13) + 384) + 8));
    uint64_t t16 = gm_l1;
    if ((uintptr_t)t16 >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)t16, t15, gm_srcb2bqqsnukmrd, 38, 13);
    void * t17 = (void *)(((uint8_t *)t14) + (uintptr_t)t16 * 64);
    gm_l3 = t17;
    void * t18 = gm_l3;
    uint8_t t19 = (uint8_t)(GM_LD(void *, (((uint8_t *)t18) + 40)) != 0);
    if (t19) {
        void * t20 = GM_LD(void *, (((uint8_t *)t18) + 40));
        gm_l4 = t20;
    } else {
        memmove(gm_out, ((uint8_t *)&F.l2), 40);
        goto gm_fdone;
    }
    {
        gm_trqfvmjtt4sij t21;
        gm_f_list_0a7vhht((void *)&t21, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l5 = t21;
    }
    void * t22 = gm_l3;
    gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)t22) + 24));
    gm_l6 = t23;
    gm_l7 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t24 = gm_l7;
            uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint8_t t26 = (t24 < t25);
            if (!t26) {
                goto gm_lb1;
            }
            {
                void * t27 = GM_LD(void *, ((uint8_t *)&gm_l6));
                uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint64_t t29 = gm_l7;
                {
                    {
                        void * t30 = (void *)((uint8_t *)&F.l5);
                        int64_t t31 = GM_LD(int64_t, ((uint8_t *)gm_cpn2np70vcf));
                        gm_tftb1odc739t5 t32 = gm_f_push_p826iq8(t30, t31);
                        gm_l12 = t32;
                        uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                        if (!t33) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srcb2bqqsnukmrd, 41, 27);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 41, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t34 = gm_l7;
            uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
            uint64_t t37 = r35;
            gm_l7 = t37;
        }
    }
gm_lb1:;
    void * t38 = gm_l3;
    gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)t38) + 8));
    gm_l8 = t39;
    gm_l9 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t40 = gm_l9;
            uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
            uint8_t t42 = (t40 < t41);
            if (!t42) {
                goto gm_lb6;
            }
            {
                void * t43 = GM_LD(void *, ((uint8_t *)&gm_l8));
                uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                uint64_t t45 = gm_l9;
                {
                    {
                        uint32_t t46 = GM_LD(uint32_t, (((uint8_t *)t43) + (uintptr_t)t45 * 4));
                        gm_l10 = t46;
                        void * t47 = GM_LD(void *, ((uint8_t *)&F.l5));
                        uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
                        uint32_t t49 = gm_l10;
                        uint64_t t50 = (uint64_t)t49;
                        if ((uintptr_t)t50 >= (uintptr_t)(t48)) gm_trap_index((uintptr_t)t50, t48, gm_srcb2bqqsnukmrd, 42, 27);
                        GM_ST(int64_t, (((uint8_t *)t47) + (uintptr_t)t50 * 8), ((int64_t)0LL));
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 42, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t51 = gm_l9;
            uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
            uint64_t t54 = r52;
            gm_l9 = t54;
        }
    }
gm_lb6:;
    {
        void * t55 = gm_l0;
        void * t56 = gm_l3;
        void * t57 = (void *)((uint8_t *)&F.l2);
        gm_trqfvmjtt4sij t58 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l5));
        gm_trqfvmjtt4sij t59;
        gm_f_list_7s4om10((void *)&t59, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tqp3o9evff257 t60;
        GM_ST(void *, ((uint8_t *)&t60 + 0), t55);
        GM_ST(void *, ((uint8_t *)&t60 + 8), t56);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t60 + 16), t58);
        GM_ST(int64_t, ((uint8_t *)&t60 + 56), ((int64_t)0LL));
        GM_ST(void *, ((uint8_t *)&t60 + 64), t57);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t60 + 72), t59);
        F.l11 = t60;
    }
    void * t61 = (void *)((uint8_t *)&F.l11);
    void * t62 = gm_l4;
    gm_b0_1 t63 = gm_f_d_expr_bbktnn5((void *)NULL, cx, NULL, t61, t62, ((uint8_t)1));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l2), 40);
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
__attribute__((aligned(4))) uint8_t gm_f_same_place_fl3dc6t(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_same_place_fl3dc6t F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    uint32_t gm_l2; /* x */
    uint32_t gm_l3; /* y */
    uint32_t gm_l4; /* xl */
    uint32_t gm_l5; /* x */
    uint32_t gm_l6; /* yl */
    uint32_t gm_l7; /* y */
    uint64_t gm_l8; /* x */
    uint64_t gm_l9; /* y */
    void * gm_l10; /* x */
    void * gm_l11; /* y */
    void * gm_l12; /* x */
    uint32_t gm_l13; /* i */
    void * gm_l14; /* y */
    uint32_t gm_l15; /* j */
    GM_STACK_CHECK(&F, 337u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l16;
    uint8_t gm_l17;
    void * t19 = gm_l0;
    {
        {
            uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
            uint8_t t21 = (uint8_t)(t20 == ((uint8_t)15ULL));
            if (!t21) {
                goto gm_lb2;
            }
            uint32_t t22 = GM_LD(uint32_t, ((((uint8_t *)t19) + 8) + 8));
            gm_l2 = t22;
            {
                {
                    void * t23 = gm_l1;
                    uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t23) + 8) + 0));
                    uint8_t t25 = (uint8_t)(t24 == ((uint8_t)15ULL));
                    if (!t25) {
                        goto gm_lb4;
                    }
                    uint32_t t26 = GM_LD(uint32_t, ((((uint8_t *)t23) + 8) + 8));
                    gm_l3 = t26;
                    uint32_t t27 = gm_l2;
                    uint32_t t28 = gm_l3;
                    uint8_t t29 = ((t27 == t28));
                    F.ret = t29;
                    goto gm_fret;
                }
gm_lb4:;
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
            uint8_t t31 = (uint8_t)(t30 == ((uint8_t)16ULL));
            if (!t31) {
                goto gm_lb5;
            }
            uint32_t t32 = GM_LD(uint32_t, ((((uint8_t *)t19) + 8) + 8));
            gm_l4 = t32;
            uint32_t t33 = GM_LD(uint32_t, (((((uint8_t *)t19) + 8) + 8) + 4));
            gm_l5 = t33;
            {
                {
                    void * t34 = gm_l1;
                    uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t34) + 8) + 0));
                    uint8_t t36 = (uint8_t)(t35 == ((uint8_t)16ULL));
                    if (!t36) {
                        goto gm_lb7;
                    }
                    uint32_t t37 = GM_LD(uint32_t, ((((uint8_t *)t34) + 8) + 8));
                    gm_l6 = t37;
                    uint32_t t38 = GM_LD(uint32_t, (((((uint8_t *)t34) + 8) + 8) + 4));
                    gm_l7 = t38;
                    uint32_t t39 = gm_l4;
                    uint32_t t40 = gm_l6;
                    uint8_t t41 = ((t39 == t40));
                    if (t41) {
                        uint32_t t42 = gm_l5;
                        uint32_t t43 = gm_l7;
                        uint8_t t44 = ((t42 == t43));
                        gm_l16 = t44;
                    } else {
                        gm_l16 = ((uint8_t)0);
                    }
                    uint8_t t45 = gm_l16;
                    F.ret = t45;
                    goto gm_fret;
                }
gm_lb7:;
            }
gm_lb6:;
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
            uint8_t t47 = (uint8_t)(t46 == ((uint8_t)17ULL));
            if (!t47) {
                goto gm_lb8;
            }
            uint64_t t48 = GM_LD(uint64_t, (((((uint8_t *)t19) + 8) + 8) + 16));
            gm_l8 = t48;
            {
                {
                    void * t49 = gm_l1;
                    uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)t49) + 8) + 0));
                    uint8_t t51 = (uint8_t)(t50 == ((uint8_t)17ULL));
                    if (!t51) {
                        goto gm_lb10;
                    }
                    uint64_t t52 = GM_LD(uint64_t, (((((uint8_t *)t49) + 8) + 8) + 16));
                    gm_l9 = t52;
                    uint64_t t53 = gm_l8;
                    uint64_t t54 = gm_l9;
                    uint8_t t55 = ((t53 == t54));
                    F.ret = t55;
                    goto gm_fret;
                }
gm_lb10:;
            }
gm_lb9:;
            goto gm_lb1;
        }
gm_lb8:;
        {
            uint8_t t56 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
            uint8_t t57 = (uint8_t)(t56 == ((uint8_t)18ULL));
            if (!t57) {
                goto gm_lb11;
            }
            void * t58 = GM_LD(void *, ((((uint8_t *)t19) + 8) + 8));
            gm_l10 = t58;
            {
                {
                    void * t59 = gm_l1;
                    uint8_t t60 = GM_LD(uint8_t, ((((uint8_t *)t59) + 8) + 0));
                    uint8_t t61 = (uint8_t)(t60 == ((uint8_t)18ULL));
                    if (!t61) {
                        goto gm_lb13;
                    }
                    void * t62 = GM_LD(void *, ((((uint8_t *)t59) + 8) + 8));
                    gm_l11 = t62;
                    void * t63 = gm_l10;
                    void * t64 = gm_l11;
                    uint8_t t65 = gm_f_same_place_fl3dc6t(t63, t64);
                    F.ret = t65;
                    goto gm_fret;
                }
gm_lb13:;
            }
gm_lb12:;
            goto gm_lb1;
        }
gm_lb11:;
        {
            uint8_t t66 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
            uint8_t t67 = (uint8_t)(t66 == ((uint8_t)19ULL));
            if (!t67) {
                goto gm_lb14;
            }
            void * t68 = GM_LD(void *, ((((uint8_t *)t19) + 8) + 8));
            gm_l12 = t68;
            uint32_t t69 = GM_LD(uint32_t, (((((uint8_t *)t19) + 8) + 8) + 8));
            gm_l13 = t69;
            {
                {
                    void * t70 = gm_l1;
                    uint8_t t71 = GM_LD(uint8_t, ((((uint8_t *)t70) + 8) + 0));
                    uint8_t t72 = (uint8_t)(t71 == ((uint8_t)19ULL));
                    if (!t72) {
                        goto gm_lb16;
                    }
                    void * t73 = GM_LD(void *, ((((uint8_t *)t70) + 8) + 8));
                    gm_l14 = t73;
                    uint32_t t74 = GM_LD(uint32_t, (((((uint8_t *)t70) + 8) + 8) + 8));
                    gm_l15 = t74;
                    uint32_t t75 = gm_l13;
                    uint32_t t76 = gm_l15;
                    uint8_t t77 = ((t75 == t76));
                    if (t77) {
                        void * t78 = gm_l12;
                        void * t79 = gm_l14;
                        uint8_t t80 = gm_f_same_place_fl3dc6t(t78, t79);
                        gm_l17 = t80;
                    } else {
                        gm_l17 = ((uint8_t)0);
                    }
                    uint8_t t81 = gm_l17;
                    F.ret = t81;
                    goto gm_fret;
                }
gm_lb16:;
            }
gm_lb15:;
            goto gm_lb1;
        }
gm_lb14:;
        {
            goto gm_lb1;
        }
gm_lb17:;
        gm_trap("no match arm matched the scrutinee", gm_srcb2bqqsnukmrd, 330, 5);
    }
gm_lb1:;
    F.ret = ((uint8_t)0);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}

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
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { int64_t f0; } gm_tm98ap4112hee;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; int32_t v1; } f1; } gm_tsoiri44d0i6q;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tm98ap4112hee v1; } f1; } gm_t62ebbi2i5rkd;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; uint64_t v1; } f1; } gm_thf5kr0is9u9i;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_tsoiri44d0i6q v1; } f1; } gm_tlmjd3nndlf3r;
_Static_assert(sizeof(gm_tm98ap4112hee) == 8 && _Alignof(gm_tm98ap4112hee) == 8 && offsetof(gm_tm98ap4112hee, f0) == 0, "layout of `File`");
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_tsoiri44d0i6q f0; uint8_t f1; uint8_t pe[3]; } gm_tqnlpelep1582;
_Static_assert(sizeof(gm_tsoiri44d0i6q) == 8 && _Alignof(gm_tsoiri44d0i6q) == 4 && offsetof(gm_tsoiri44d0i6q, f0) == 0 && offsetof(gm_tsoiri44d0i6q, f1) == 4, "layout of `ExitStatus`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; } f1; } gm_ttln0d0hd70en;
_Static_assert(sizeof(gm_t62ebbi2i5rkd) == 16 && _Alignof(gm_t62ebbi2i5rkd) == 8 && offsetof(gm_t62ebbi2i5rkd, f0) == 0 && offsetof(gm_t62ebbi2i5rkd, f1) == 8, "layout of `IoError!File`");
_Static_assert(sizeof(gm_thf5kr0is9u9i) == 16 && _Alignof(gm_thf5kr0is9u9i) == 8 && offsetof(gm_thf5kr0is9u9i, f0) == 0 && offsetof(gm_thf5kr0is9u9i, f1) == 8, "layout of `IoError!usize`");
_Static_assert(sizeof(gm_tlmjd3nndlf3r) == 12 && _Alignof(gm_tlmjd3nndlf3r) == 4 && offsetof(gm_tlmjd3nndlf3r, f0) == 0 && offsetof(gm_tlmjd3nndlf3r, f1) == 4, "layout of `IoError!ExitStatus`");
_Static_assert(sizeof(gm_tqnlpelep1582) == 12 && _Alignof(gm_tqnlpelep1582) == 4 && offsetof(gm_tqnlpelep1582, f0) == 0 && offsetof(gm_tqnlpelep1582, f1) == 8, "layout of `?ExitStatus`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_tqnlpelep1582 v1; } f1; } gm_tr6norheal11n;
_Static_assert(sizeof(gm_ttln0d0hd70en) == 12 && _Alignof(gm_ttln0d0hd70en) == 4 && offsetof(gm_ttln0d0hd70en, f0) == 0 && offsetof(gm_ttln0d0hd70en, f1) == 4, "layout of `IoError!()`");
_Static_assert(sizeof(gm_tr6norheal11n) == 16 && _Alignof(gm_tr6norheal11n) == 4 && offsetof(gm_tr6norheal11n, f0) == 0 && offsetof(gm_tr6norheal11n, f1) == 4, "layout of `IoError!?ExitStatus`");
gm_b0_1 gm_dt3ip07hju0a_0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1);
gm_b0_1 gm_dt3ip07hju0a_1(void *env, gm_node *cx, gm_node *inst, void * a1, void * a2, gm_b16_8 a3, gm_b16_8 a4);
gm_b0_1 gm_dt3ip07hju0a_2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1);
gm_b0_1 gm_dt3ip07hju0a_3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1);
gm_b0_1 gm_dt3ip07hju0a_4(void *env, gm_node *cx, gm_node *inst, void * a1, gm_b16_8 a2);
gm_b0_1 gm_dt3ip07hju0a_5(void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a1, gm_b16_8 a2);
gm_t62ebbi2i5rkd gm_dtaqemfrshlp_0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1, const void * a2);
gm_thf5kr0is9u9i gm_dtaqemfrshlp_1(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, gm_b16_8 a2);
gm_b0_1 gm_dtaqemfrshlp_2(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1);
void gm_dtaqemfrshlp_3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, gm_b16_8 a2);
gm_tlmjd3nndlf3r gm_dtaqemfrshlp_4(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1);
gm_tr6norheal11n gm_dtaqemfrshlp_5(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1);
gm_ttln0d0hd70en gm_dtaqemfrshlp_6(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_6ji90d8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_fnpff4s(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_ghum27l(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_nnvr1n3(void *env, gm_node *cx, gm_node *inst, const void * a0);
extern uint8_t gm_c0utunn6c58[23] __attribute__((aligned(1)));
extern uint8_t gm_c1a92ugfgjv[112] __attribute__((aligned(8)));
extern uint8_t gm_c26gi09a298[63] __attribute__((aligned(1)));
extern uint8_t gm_c2elhq7l2vf[4] __attribute__((aligned(1)));
extern uint8_t gm_c3qosqjl81k[8] __attribute__((aligned(8)));
extern uint8_t gm_c3t36dlh4g5[16] __attribute__((aligned(8)));
extern uint8_t gm_c4tkno4ihu4[3] __attribute__((aligned(1)));
extern uint8_t gm_c5fueo3c6mt[14] __attribute__((aligned(1)));
extern uint8_t gm_c653lc0efgo[3] __attribute__((aligned(1)));
extern uint8_t gm_c6d5506jf7q[8] __attribute__((aligned(8)));
extern uint8_t gm_c6ojdgkpe75[3] __attribute__((aligned(1)));
extern uint8_t gm_c7mc3uaor1r[80] __attribute__((aligned(8)));
extern uint8_t gm_c7p3n1736t1[4] __attribute__((aligned(4)));
extern uint8_t gm_c89882kont8[14] __attribute__((aligned(1)));
extern uint8_t gm_c9b5effiaau[60] __attribute__((aligned(1)));
extern uint8_t gm_c9gbtic0o97[5] __attribute__((aligned(1)));
extern uint8_t gm_cai164r033u[16] __attribute__((aligned(8)));
extern uint8_t gm_cbapkrq7q1c[35] __attribute__((aligned(1)));
extern uint8_t gm_cbm4eknr5tg[1] __attribute__((aligned(1)));
extern uint8_t gm_cbr9gd2rl9n[13] __attribute__((aligned(1)));
extern uint8_t gm_cc9ochdo4cn[8] __attribute__((aligned(1)));
extern uint8_t gm_ceq0vig573u[10] __attribute__((aligned(1)));
extern uint8_t gm_cev3sfhfj2p[48] __attribute__((aligned(8)));
extern uint8_t gm_cf5loitcbc7[13] __attribute__((aligned(1)));
extern uint8_t gm_cf6m50d8ln4[9] __attribute__((aligned(1)));
extern uint8_t gm_cfv7g68ca5r[8] __attribute__((aligned(1)));
extern uint8_t gm_chkob26flgg[9] __attribute__((aligned(1)));
extern uint8_t gm_chnrragfrjp[13] __attribute__((aligned(1)));
extern uint8_t gm_ci5i3g6ghat[6] __attribute__((aligned(1)));
extern uint8_t gm_cigdn81ldc9[16] __attribute__((aligned(8)));
extern uint8_t gm_cjnuje66084[21] __attribute__((aligned(1)));
extern uint8_t gm_cka4oohkshv[512] __attribute__((aligned(8)));
extern uint8_t gm_ckahn0ehf27[1] __attribute__((aligned(8)));
extern uint8_t gm_ckhlqd9sde7[4] __attribute__((aligned(1)));
extern uint8_t gm_cknsh3g32om[19] __attribute__((aligned(1)));
extern uint8_t gm_ckpkn5umvr0[1] __attribute__((aligned(1)));
extern uint8_t gm_clps76qnh98[3] __attribute__((aligned(1)));
extern uint8_t gm_cm2dkubhtop[8] __attribute__((aligned(8)));
extern uint8_t gm_cmumimn4u17[6] __attribute__((aligned(1)));
extern uint8_t gm_cnaolbe83uo[15] __attribute__((aligned(1)));
extern uint8_t gm_cpk359ca82u[26] __attribute__((aligned(1)));
extern uint8_t gm_cpvard348ed[4] __attribute__((aligned(4)));
extern uint8_t gm_cq3tl31s253[2] __attribute__((aligned(1)));
extern uint8_t gm_cq8o894hglj[7] __attribute__((aligned(1)));
extern uint8_t gm_cqb5jrucdqt[11] __attribute__((aligned(1)));
extern uint8_t gm_cro0h2panct[24] __attribute__((aligned(8)));
extern uint8_t gm_css0is294pa[5] __attribute__((aligned(1)));
extern uint8_t gm_csvndp98166[11] __attribute__((aligned(1)));
extern uint8_t gm_ctngbflld19[16] __attribute__((aligned(8)));
extern uint8_t gm_ctsgq16sumf[3] __attribute__((aligned(1)));
extern uint8_t gm_cu7epimqni2[3] __attribute__((aligned(1)));
extern uint8_t gm_culvhpe56id[10] __attribute__((aligned(1)));
extern uint8_t gm_cur6jdbgb9n[7] __attribute__((aligned(1)));
extern uint8_t gm_curvqf1hf03[98] __attribute__((aligned(1)));
extern uint8_t gm_cvkm788lqpo[17] __attribute__((aligned(1)));
extern void *const gm_hd3ip07hju0a[];
extern void *const gm_hdaqemfrshlp[];
extern void *const gm_hl5p278go8bp[];
extern void *const gm_hle6hel8vh71[];
extern void *const gm_hlh176qr0nti[];
extern void *const gm_hlqklhf369l0[];
extern uint8_t gm_k34avs9rqsl[40] __attribute__((aligned(8)));
extern uint8_t gm_kb94e40l0br[40] __attribute__((aligned(8)));
extern uint8_t gm_kbri4nir86d[40] __attribute__((aligned(8)));
extern uint8_t gm_keokac8bchm[40] __attribute__((aligned(8)));
extern uint8_t gm_kg028u69smr[40] __attribute__((aligned(8)));
extern uint8_t gm_kh7445i0ed8[40] __attribute__((aligned(8)));
extern uint8_t gm_khvqu09sagr[40] __attribute__((aligned(8)));
extern uint8_t gm_kieqgrgghc5[40] __attribute__((aligned(8)));
extern uint8_t gm_kl5pit792cd[40] __attribute__((aligned(8)));
extern uint8_t gm_knhk77btln5[40] __attribute__((aligned(8)));
extern uint8_t gm_ko7k0ku4679[40] __attribute__((aligned(8)));
extern uint8_t gm_kr1pd6n5fr4[40] __attribute__((aligned(8)));
extern uint8_t gm_ks263t40fiu[40] __attribute__((aligned(8)));
extern uint8_t gm_ktvkv2rtjbj[40] __attribute__((aligned(8)));
extern uint8_t gm_ku0iiu0o88g[40] __attribute__((aligned(8)));
extern uint8_t gm_kutnfkdjl3d[40] __attribute__((aligned(8)));
extern uint8_t gm_kv6tql7n7ah[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0039h44ked10[39];
extern const uint8_t gm_s014h3o6j2aj2[21];
extern const uint8_t gm_s018g1qkt4p93[9];
extern const uint8_t gm_s03c24qg7i06q[13];
extern const uint8_t gm_s08dcqdp58r5f[3];
extern const uint8_t gm_s09r417b6q8b4[14];
extern const uint8_t gm_s0bmhg7t703ts[22];
extern const uint8_t gm_s0ffe093dar6e[19];
extern const uint8_t gm_s0i4qvihaki34[41];
extern const uint8_t gm_s0jhhkkbgc6ks[18];
extern const uint8_t gm_s0lh70hs4of8e[22];
extern const uint8_t gm_s0m0qaokk51g0[4];
extern const uint8_t gm_s0of5gcf1i7ic[29];
extern const uint8_t gm_s0r9vb1qh90ue[116];
extern const uint8_t gm_s0si75ormd2gn[15];
extern const uint8_t gm_s0tfhmgv1pal7[9];
extern const uint8_t gm_s0tjng49g04bk[15];
extern const uint8_t gm_s122r7j2imgmj[22];
extern const uint8_t gm_s15gg5sblr4kl[4];
extern const uint8_t gm_s16bsnuu7i04s[3];
extern const uint8_t gm_s18e8dmribdb3[7];
extern const uint8_t gm_s1aecjev6ahg8[5];
extern const uint8_t gm_s1ak5vhpqj4ju[5];
extern const uint8_t gm_s1asl1e1tvb0k[2];
extern const uint8_t gm_s1d20j48bjjln[12];
extern const uint8_t gm_s1d982oimmu24[7];
extern const uint8_t gm_s1dbslfck7laf[3];
extern const uint8_t gm_s1emqv9srjhhk[38];
extern const uint8_t gm_s1f440st4mnkp[2];
extern const uint8_t gm_s1hbm05s14qo4[30];
extern const uint8_t gm_s1hha8fc2kd79[18];
extern const uint8_t gm_s1ishc3mvan41[31];
extern const uint8_t gm_s1j5448m6ruti[10];
extern const uint8_t gm_s1jboc8lkvac0[6];
extern const uint8_t gm_s1lekaltijhvv[8];
extern const uint8_t gm_s1nvbb638rr96[8];
extern const uint8_t gm_s1objv452j1q2[11];
extern const uint8_t gm_s1tqe3mtj6vb1[8];
extern const uint8_t gm_s1u0m6lm5u1su[7];
extern const uint8_t gm_s1vmtsmtbio1j[15];
extern const uint8_t gm_s2053hhvf16fm[2];
extern const uint8_t gm_s20ceq2pdpd1i[14];
extern const uint8_t gm_s20qcu4sv6mcn[19];
extern const uint8_t gm_s20tdu9mtc6fi[8];
extern const uint8_t gm_s22tv9c5r20tl[4];
extern const uint8_t gm_s24s9g8r9gc0v[7];
extern const uint8_t gm_s26cjdp185rsj[16];
extern const uint8_t gm_s26crtotu14pn[3];
extern const uint8_t gm_s27f321qjn1nc[86];
extern const uint8_t gm_s27jprdv4pn6e[10];
extern const uint8_t gm_s2a4d8jj74v6o[49];
extern const uint8_t gm_s2a9t8smkevg0[4];
extern const uint8_t gm_s2d2ar24n4if2[6];
extern const uint8_t gm_s2dssfe3261t2[2];
extern const uint8_t gm_s2ea46cnsj1sj[20];
extern const uint8_t gm_s2eu0g6qdje9o[41];
extern const uint8_t gm_s2g391595qd1n[10];
extern const uint8_t gm_s2gnpj7k4l22n[2];
extern const uint8_t gm_s2kg96lb0hss7[28];
extern const uint8_t gm_s2lhokjucfplm[10];
extern const uint8_t gm_s2m4r2316r14d[11];
extern const uint8_t gm_s2qk17n09116b[14];
extern const uint8_t gm_s2r68j08i2lia[5];
extern const uint8_t gm_s2sghqjh0qbk9[8];
extern const uint8_t gm_s2u0i5o1ej1mg[10];
extern const uint8_t gm_s2u7k18iev3v2[4];
extern const uint8_t gm_s2u7kalhku9i5[152];
extern const uint8_t gm_s2v8uomf8pkfv[13];
extern const uint8_t gm_s312ibu3ccl18[12];
extern const uint8_t gm_s315r4147rf48[40];
extern const uint8_t gm_s329kcr9363eu[11];
extern const uint8_t gm_s3385032lmmqq[24];
extern const uint8_t gm_s33fm62gcdbhu[11];
extern const uint8_t gm_s33h2lce46bnm[9];
extern const uint8_t gm_s361hl447ggcv[20];
extern const uint8_t gm_s38qa2pnnt0lv[25];
extern const uint8_t gm_s39o4eejklnd6[12];
extern const uint8_t gm_s3a1gf31m3ei6[10];
extern const uint8_t gm_s3cu59itn73k7[23];
extern const uint8_t gm_s3d0v42kavhv7[19];
extern const uint8_t gm_s3gftv3r0boaq[5];
extern const uint8_t gm_s3j0d9e0k3atk[5];
extern const uint8_t gm_s3jflb7gdk32k[32];
extern const uint8_t gm_s3mbo1s262isg[14];
extern const uint8_t gm_s3md83t7b4thr[10];
extern const uint8_t gm_s3nknbqjesra1[19];
extern const uint8_t gm_s3ntjearde849[41];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s3phpt25k93cc[39];
extern const uint8_t gm_s3pufl09hvbbf[6];
extern const uint8_t gm_s3sgbpnnlr6sb[5];
extern const uint8_t gm_s3ss0hcg1h7js[7];
extern const uint8_t gm_s3tacjdl2ciei[13];
extern const uint8_t gm_s4108vvmvu0gn[70];
extern const uint8_t gm_s48o8olsbqnph[12];
extern const uint8_t gm_s48vlq6lddb8m[29];
extern const uint8_t gm_s49o1oiv7k0po[4];
extern const uint8_t gm_s4a980fahtblo[3];
extern const uint8_t gm_s4acc6d5101lp[45];
extern const uint8_t gm_s4be485tkfn41[15];
extern const uint8_t gm_s4dqb8b35j0gp[24];
extern const uint8_t gm_s4e3q3n6msi6m[6];
extern const uint8_t gm_s4j1eg5g5a64h[5];
extern const uint8_t gm_s4j3ecogbjkvn[13];
extern const uint8_t gm_s4jrltvkedvou[31];
extern const uint8_t gm_s4k5q4ehrakfe[15];
extern const uint8_t gm_s4l3p0f0c6mmc[44];
extern const uint8_t gm_s4l676io35g5u[12];
extern const uint8_t gm_s4ld5r1oi34jh[18];
extern const uint8_t gm_s4p5imo39raaq[40];
extern const uint8_t gm_s4tgvb7s2dbuj[20];
extern const uint8_t gm_s4ufkrqv2ag7g[7];
extern const uint8_t gm_s4uh375udgni3[206];
extern const uint8_t gm_s4ujp5fsfjtut[8];
extern const uint8_t gm_s4v09sn49l08n[7];
extern const uint8_t gm_s52jn4s18md6n[16];
extern const uint8_t gm_s52ul4dg63jhe[11];
extern const uint8_t gm_s5a98jvj83tqj[16];
extern const uint8_t gm_s5aj93skvhpnl[17];
extern const uint8_t gm_s5bh8t3p9l4nk[16];
extern const uint8_t gm_s5bj9cslts4df[34];
extern const uint8_t gm_s5bohs6opthjm[24];
extern const uint8_t gm_s5ctp78jmtbv8[41];
extern const uint8_t gm_s5dpb04jti9nb[17];
extern const uint8_t gm_s5dqpb4hur24f[37];
extern const uint8_t gm_s5g5rfc96krge[9];
extern const uint8_t gm_s5gkblcfbl1mv[121];
extern const uint8_t gm_s5glpb7fbtk4c[27];
extern const uint8_t gm_s5gn6cmh4thug[38];
extern const uint8_t gm_s5hp4n46rtp6j[15];
extern const uint8_t gm_s5irlrsnb30cc[24];
extern const uint8_t gm_s5j95frjqt5af[8];
extern const uint8_t gm_s5jtsqa43tb6m[14];
extern const uint8_t gm_s5k1c58je5d4s[25];
extern const uint8_t gm_s5k1m00kjtc17[14];
extern const uint8_t gm_s5le7rpnp1iq6[9];
extern const uint8_t gm_s5nig6ebl26qo[61];
extern const uint8_t gm_s5pra70i8o734[14];
extern const uint8_t gm_s5qkai093c1q0[6];
extern const uint8_t gm_s5qpqp0tch0l9[7];
extern const uint8_t gm_s5t1b3eks758h[27];
extern const uint8_t gm_s5u07ahpa78ud[27];
extern const uint8_t gm_s5us2hgd173vu[27];
extern const uint8_t gm_s5v003pfcdo1r[22];
extern const uint8_t gm_s5vakuk6ui4kj[9];
extern const uint8_t gm_s60n1k0cqjuaq[144];
extern const uint8_t gm_s63jrdfu5o5ps[9];
extern const uint8_t gm_s677r76pa1oks[7];
extern const uint8_t gm_s67h6d8kdg7qc[167];
extern const uint8_t gm_s6aamtn65omr1[7];
extern const uint8_t gm_s6bbu24cfq0ok[14];
extern const uint8_t gm_s6ccueucpa122[6];
extern const uint8_t gm_s6dpovcf17va4[4];
extern const uint8_t gm_s6g1iihs2bped[46];
extern const uint8_t gm_s6i2dst792b3t[54];
extern const uint8_t gm_s6iuue4hdrbt9[5];
extern const uint8_t gm_s6jlko9gsl7im[26];
extern const uint8_t gm_s6mimndcv0cge[4];
extern const uint8_t gm_s6n3pd0tmqbqc[10];
extern const uint8_t gm_s6npjseknrjhj[41];
extern const uint8_t gm_s6o3rebeo47ka[16];
extern const uint8_t gm_s6qihcjfe8481[10];
extern const uint8_t gm_s6t4r831mukdr[3];
extern const uint8_t gm_s6vg88ce880ha[13];
extern const uint8_t gm_s6vvtrmtnt3j1[5];
extern const uint8_t gm_s72hk5u5ngq4i[11];
extern const uint8_t gm_s73vt0nuu7890[28];
extern const uint8_t gm_s75m3dt9015lh[15];
extern const uint8_t gm_s76hg0po5usup[9];
extern const uint8_t gm_s78cm457osdms[49];
extern const uint8_t gm_s79el5a3kii78[12];
extern const uint8_t gm_s79r5ktousup8[157];
extern const uint8_t gm_s79s3h98matgr[6];
extern const uint8_t gm_s7ah1q5ah26rj[12];
extern const uint8_t gm_s7atji10aaq5k[7];
extern const uint8_t gm_s7bpqsd6i8gc3[15];
extern const uint8_t gm_s7cn2q9qfeafa[79];
extern const uint8_t gm_s7d1d0uk71q3u[19];
extern const uint8_t gm_s7doqph2gqafo[60];
extern const uint8_t gm_s7f8crhgefl4u[28];
extern const uint8_t gm_s7j1ioljmsuka[19];
extern const uint8_t gm_s7lavhjsa5dc0[62];
extern const uint8_t gm_s7p349cm5h5jf[20];
extern const uint8_t gm_s7u3qhd03rfo3[17];
extern const uint8_t gm_s7ucknluds45h[5];
extern const uint8_t gm_s7v0kgsq3q7vf[57];
extern const uint8_t gm_s80p0ld1j6rfg[10];
extern const uint8_t gm_s80t4sc5bf88n[82];
extern const uint8_t gm_s81ppjm26jd5s[148];
extern const uint8_t gm_s852r4rtnj5dj[54];
extern const uint8_t gm_s86js8lt7ibda[48];
extern const uint8_t gm_s873ct0as0vg4[48];
extern const uint8_t gm_s89gtqa9dnss0[35];
extern const uint8_t gm_s89j7s4avcmfs[28];
extern const uint8_t gm_s8aupotg4okcv[103];
extern const uint8_t gm_s8de5r2vp8807[20];
extern const uint8_t gm_s8eis4410oprq[21];
extern const uint8_t gm_s8f69f7tnslut[16];
extern const uint8_t gm_s8krdq6a1r5nt[53];
extern const uint8_t gm_s8l5u1ln9m2k0[58];
extern const uint8_t gm_s8lkplgn6m7u9[8];
extern const uint8_t gm_s8oua93imbc1q[15];
extern const uint8_t gm_s8ovik98r39ct[13];
extern const uint8_t gm_s8qpjmalo4drt[13];
extern const uint8_t gm_s8rsdirj4dmun[4];
extern const uint8_t gm_s8sl52ot187i2[50];
extern const uint8_t gm_s8tq0rdmthkth[16];
extern const uint8_t gm_s8uplmiglsqkm[15];
extern const uint8_t gm_s8v8krjfns655[20];
extern const uint8_t gm_s8vgs1ersvo58[8];
extern const uint8_t gm_s8vjpeo1vp60m[16];
extern const uint8_t gm_s959ni36b80a4[4];
extern const uint8_t gm_s95mahatn8od3[41];
extern const uint8_t gm_s988kcbq5r7tp[14];
extern const uint8_t gm_s99scb3gc7vkm[29];
extern const uint8_t gm_s9aatfmgqhb47[7];
extern const uint8_t gm_s9co2b6h6nhos[16];
extern const uint8_t gm_s9e7jg1nv469k[6];
extern const uint8_t gm_s9es2tos1bhfp[5];
extern const uint8_t gm_s9f47s6tkuln3[42];
extern const uint8_t gm_s9grtrvtmi962[8];
extern const uint8_t gm_s9j0jokaa6cqi[113];
extern const uint8_t gm_s9lc9q0nba0ng[8];
extern const uint8_t gm_s9lh30p1g8vti[54];
extern const uint8_t gm_s9lvs84vghure[12];
extern const uint8_t gm_s9mmepka9n2j2[10];
extern const uint8_t gm_s9nuud03bu2mh[11];
extern const uint8_t gm_s9ol82jrvi3ma[4];
extern const uint8_t gm_s9qv939r7cv21[88];
extern const uint8_t gm_s9rk6ipadg2pq[111];
extern const uint8_t gm_s9sev457i3j7n[5];
extern const uint8_t gm_s9tnrhv72lohp[8];
extern const uint8_t gm_s9u1dek1h1q8t[5];
extern const uint8_t gm_s9vg8k4mbj38f[22];
extern const uint8_t gm_sa027i133kcv3[4];
extern const uint8_t gm_sa10lthaugqih[45];
extern const uint8_t gm_sa349un8b060v[19];
extern const uint8_t gm_sa39uag0oed9a[16];
extern const uint8_t gm_sa90e17lvpr4h[41];
extern const uint8_t gm_sa9sfrgc5vaao[5];
extern const uint8_t gm_sadte2ukivobf[28];
extern const uint8_t gm_sae23ckujbpp9[107];
extern const uint8_t gm_safiul2d9a6fl[14];
extern const uint8_t gm_safvj03783ie0[65];
extern const uint8_t gm_sagpv7fgcnm3e[64];
extern const uint8_t gm_sah431l1f82o2[113];
extern const uint8_t gm_sahim4484f9l7[3];
extern const uint8_t gm_sajhum9b4nr96[2];
extern const uint8_t gm_sal17k00cm1q9[23];
extern const uint8_t gm_samr3e6eqsavu[27];
extern const uint8_t gm_sannl4idt68d4[42];
extern const uint8_t gm_saqgvkkehjsjb[33];
extern const uint8_t gm_saqhrs072atsq[10];
extern const uint8_t gm_sar0vrndv51ep[14];
extern const uint8_t gm_sar7ovt8nasdo[8];
extern const uint8_t gm_sau1b8ivpr0us[8];
extern const uint8_t gm_saunlif71pbir[16];
extern const uint8_t gm_sb35te3dpkhv1[13];
extern const uint8_t gm_sb5it581p5nll[5];
extern const uint8_t gm_sb7lq2a8rdpq3[10];
extern const uint8_t gm_sb8c0irhu65ra[140];
extern const uint8_t gm_sb8kl54a499bf[7];
extern const uint8_t gm_sb9dglh7ekrt2[7];
extern const uint8_t gm_sb9k4ip91d7oa[6];
extern const uint8_t gm_sbel6qookv0gl[54];
extern const uint8_t gm_sbf8eactntf1g[21];
extern const uint8_t gm_sbgj0biroecr0[13];
extern const uint8_t gm_sbh3nm117l7ra[35];
extern const uint8_t gm_sbl3pbs9195ho[4];
extern const uint8_t gm_sblao71d2av8h[22];
extern const uint8_t gm_sbmtphtelgust[29];
extern const uint8_t gm_sbngfgv31nmur[61];
extern const uint8_t gm_sbo52miqad506[4];
extern const uint8_t gm_sbrifoc7ea21m[7];
extern const uint8_t gm_sbrr14r7dj3rt[9];
extern const uint8_t gm_sbrtnmpf78kqq[10];
extern const uint8_t gm_sbs1lj6q8rfiu[44];
extern const uint8_t gm_sbtnbgj3h592u[19];
extern const uint8_t gm_sbu9dvh1hebne[26];
extern const uint8_t gm_sc103nruhmor7[3];
extern const uint8_t gm_sc24a25pi012e[8];
extern const uint8_t gm_scedkbh0j6d78[6];
extern const uint8_t gm_scfb66ogf0spa[19];
extern const uint8_t gm_sch2ljca2kt47[12];
extern const uint8_t gm_sck2rq9li012q[23];
extern const uint8_t gm_sckeklr60tlq3[34];
extern const uint8_t gm_scn1hel0olv2p[41];
extern const uint8_t gm_scnb11g2ghrd0[35];
extern const uint8_t gm_scohuf4dfj32r[37];
extern const uint8_t gm_scp8gj1kjauec[29];
extern const uint8_t gm_scq4j3j3c00j6[41];
extern const uint8_t gm_scstobchrdgiu[67];
extern const uint8_t gm_scte4ush0iqha[7];
extern const uint8_t gm_sd276t5aajuq8[3];
extern const uint8_t gm_sd7guj32ltqh8[47];
extern const uint8_t gm_sd7vrpqmmkbm0[111];
extern const uint8_t gm_sdcigk5ajbcfi[68];
extern const uint8_t gm_sdckdu8fe6iog[7];
extern const uint8_t gm_sdh44op4kc2is[15];
extern const uint8_t gm_sdib10lkj65nm[45];
extern const uint8_t gm_sdib2k2ql9mgl[9];
extern const uint8_t gm_sdjba9ailb8m1[4];
extern const uint8_t gm_sdjfsrjr2b2ke[2];
extern const uint8_t gm_sdjk9c005ec9t[13];
extern const uint8_t gm_sdk5n6a42f7nl[3];
extern const uint8_t gm_sdllavet8v9kb[41];
extern const uint8_t gm_sdori1qhqiv1t[42];
extern const uint8_t gm_sdugj3qbhl84q[5];
extern const uint8_t gm_sdujhgrk0oln8[5];
extern const uint8_t gm_sdv6tt97thssp[22];
extern const uint8_t gm_sdvlseva8qvl4[77];
extern const uint8_t gm_sdvpeftn9n4aa[7];
extern const uint8_t gm_se3smlbkt8d02[37];
extern const uint8_t gm_se3u415f6s7p1[13];
extern const uint8_t gm_se5tulnsn3o9s[10];
extern const uint8_t gm_se7h6nse0tc33[26];
extern const uint8_t gm_se7oqjo2sonp9[9];
extern const uint8_t gm_se8cpd8s09lkc[13];
extern const uint8_t gm_se8ggmq7h9bin[35];
extern const uint8_t gm_sedk3p1dc2it3[17];
extern const uint8_t gm_sedq8b22f9agn[91];
extern const uint8_t gm_sef68gcceqr04[21];
extern const uint8_t gm_sefm4vafv1a3l[3];
extern const uint8_t gm_seg5mo9lt1vgq[11];
extern const uint8_t gm_seglpsc899j08[11];
extern const uint8_t gm_segv6dkv6t4tr[7];
extern const uint8_t gm_seh4hl8ei3cha[20];
extern const uint8_t gm_sehgil1ui47md[7];
extern const uint8_t gm_seiuio4eghuvu[19];
extern const uint8_t gm_sek0ue1rtvb2r[3];
extern const uint8_t gm_sel2c30qvm74f[7];
extern const uint8_t gm_selmorb0539e5[5];
extern const uint8_t gm_senjrqtakqrde[16];
extern const uint8_t gm_set3pi1jhetbi[7];
extern const uint8_t gm_sf3fmcr9mrflm[15];
extern const uint8_t gm_sf46mekb18qm8[14];
extern const uint8_t gm_sf5llhuhdlajp[9];
extern const uint8_t gm_sf8e06i5juo6a[29];
extern const uint8_t gm_sf8mbf71lsfeh[35];
extern const uint8_t gm_sfaeuqh39av6i[28];
extern const uint8_t gm_sfc624hllrs4g[6];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sfeuhas7a05k6[6];
extern const uint8_t gm_sfi2ha42r1nr2[2];
extern const uint8_t gm_sfir6p8a22i6s[31];
extern const uint8_t gm_sfk3r5va3vobm[5];
extern const uint8_t gm_sfknvqdnjah7l[18];
extern const uint8_t gm_sfkrpkv21b1rq[4];
extern const uint8_t gm_sfm5elvivaoue[41];
extern const uint8_t gm_sfm739ta4hkl2[6];
extern const uint8_t gm_sfm9elkq6bn2e[2];
extern const uint8_t gm_sfr3tv65k63d4[16];
extern const uint8_t gm_sfrdbcc53tuv9[18];
extern const uint8_t gm_sfsnsm5njae7n[19];
extern const uint8_t gm_sfug4bo4t4p28[41];
extern const uint8_t gm_sfvbeam2o42hd[38];
extern const uint8_t gm_sg1uf2uigo01q[27];
extern const uint8_t gm_sg398rgmck67b[4];
extern const uint8_t gm_sg3ba4vpic0jb[19];
extern const uint8_t gm_sg46oe1nh64ub[9];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sg5o5gf2u71g7[9];
extern const uint8_t gm_sg7n1u5l63uaa[9];
extern const uint8_t gm_sgbcvt6rqctvd[7];
extern const uint8_t gm_sgdopr7odbih9[10];
extern const uint8_t gm_sgge76tb2bmvi[32];
extern const uint8_t gm_sghk06svnbp7m[40];
extern const uint8_t gm_sgie3las787c9[31];
extern const uint8_t gm_sgip9ekbho6mc[11];
extern const uint8_t gm_sgjp5nurn7jdm[8];
extern const uint8_t gm_sgm565ktbj9ua[12];
extern const uint8_t gm_sgn9uq6nipp5t[33];
extern const uint8_t gm_sgrpmaenv85i1[73];
extern const uint8_t gm_sgsd94uuisj8t[73];
extern const uint8_t gm_sgu789edodj19[70];
extern const uint8_t gm_sgukm1evmrjal[4];
extern const uint8_t gm_sh0huebkcvk29[20];
extern const uint8_t gm_sh43rpq1oqm5d[6];
extern const uint8_t gm_sh50kc23mi3vj[36];
extern const uint8_t gm_sh9g09icusqpu[14];
extern const uint8_t gm_shdh4dd2ejebj[158];
extern const uint8_t gm_sheqs83o4tgp8[45];
extern const uint8_t gm_shh36vc8p1t4e[7];
extern const uint8_t gm_shhak6l2rrn43[15];
extern const uint8_t gm_shj3jonnfj09m[8];
extern const uint8_t gm_shja5bmmh8ag2[30];
extern const uint8_t gm_shkhe2ptevja3[18];
extern const uint8_t gm_shki2pqrgmpd1[19];
extern const uint8_t gm_shl0ucmfrvk77[54];
extern const uint8_t gm_shlds5pd69pfn[16];
extern const uint8_t gm_shlilv0hq3bqu[33];
extern const uint8_t gm_shmdne5cle16i[8];
extern const uint8_t gm_shnig5tqtdedi[4];
extern const uint8_t gm_shqseesnqrpjf[22];
extern const uint8_t gm_shr2dul5unlgn[16];
extern const uint8_t gm_shrlc4g1vtjsu[47];
extern const uint8_t gm_sht5g388qmj1f[15];
extern const uint8_t gm_shv4snn6f8hcr[29];
extern const uint8_t gm_si2v1nj23o22k[41];
extern const uint8_t gm_si430gujtnl9u[11];
extern const uint8_t gm_si4k9tiofr8t2[7];
extern const uint8_t gm_si5jb1hd2qqp1[12];
extern const uint8_t gm_si6sr5gqkcui7[6];
extern const uint8_t gm_si6va5n9qkjau[185];
extern const uint8_t gm_si7hg8a0jmhu5[3];
extern const uint8_t gm_si8s9ids0otre[44];
extern const uint8_t gm_siamquc19uikr[2];
extern const uint8_t gm_sicgmpca63jln[2];
extern const uint8_t gm_siddm9qkt382a[15];
extern const uint8_t gm_sig89vu0mvqtm[19];
extern const uint8_t gm_sigl9de72ni6d[16];
extern const uint8_t gm_sihdt3ld7f53j[16];
extern const uint8_t gm_siiln09e6475q[5];
extern const uint8_t gm_sijs25uglkpn8[9];
extern const uint8_t gm_sikspprj7od7l[43];
extern const uint8_t gm_silt1tnb0phmp[3];
extern const uint8_t gm_simmbi8o1lo4k[46];
extern const uint8_t gm_siopg3b55pic9[12];
extern const uint8_t gm_sipnguisfnjrj[49];
extern const uint8_t gm_sj0ah0ccdpkiu[46];
extern const uint8_t gm_sj0qpc1n9qui9[7];
extern const uint8_t gm_sj1gcl0qo4i9p[7];
extern const uint8_t gm_sj1m58fkg956m[8];
extern const uint8_t gm_sj21lfdl9q0t4[6];
extern const uint8_t gm_sj3ehailc2pgo[52];
extern const uint8_t gm_sj7s3tnnvob4e[9];
extern const uint8_t gm_sj8b32e846r90[11];
extern const uint8_t gm_sje5p1r4d76uk[57];
extern const uint8_t gm_sjeoqheucmn3m[19];
extern const uint8_t gm_sjerq54hvns79[11];
extern const uint8_t gm_sjfpr08jc3ep1[23];
extern const uint8_t gm_sjfqm9gvu1om9[8];
extern const uint8_t gm_sjid61i849qic[3];
extern const uint8_t gm_sjkj5g78lejrj[4];
extern const uint8_t gm_sjlci8b0qvbpc[3];
extern const uint8_t gm_sjltai3b82h79[4];
extern const uint8_t gm_sjm2vqdj9hddi[51];
extern const uint8_t gm_sjmmigkb19htg[4];
extern const uint8_t gm_sjnkguo0atpm5[3];
extern const uint8_t gm_sjoj5mss14s83[6];
extern const uint8_t gm_sjojf6rrbl49u[6];
extern const uint8_t gm_sjqmjo9ak50j8[5];
extern const uint8_t gm_sjrg55dooppac[6];
extern const uint8_t gm_sk31jlojfl0rr[8];
extern const uint8_t gm_sk754mj3p1el6[2];
extern const uint8_t gm_sk88tp2337lf6[13];
extern const uint8_t gm_ska87i9f7924f[8];
extern const uint8_t gm_skefd3dps40es[40];
extern const uint8_t gm_skeo8nnnc4o7f[40];
extern const uint8_t gm_skfdo9qggtrbp[6];
extern const uint8_t gm_skgcaidqnnhjr[11];
extern const uint8_t gm_skj7cpsc7j6g9[5];
extern const uint8_t gm_skmo18eln23n6[16];
extern const uint8_t gm_skn7gps778eo2[3];
extern const uint8_t gm_sko48juemga53[34];
extern const uint8_t gm_skocj074p3r97[9];
extern const uint8_t gm_skpna6jsqumoc[9];
extern const uint8_t gm_sks5shc2qs9ve[49];
extern const uint8_t gm_sktf16uersaon[9];
extern const uint8_t gm_sktopm8j9jc57[802];
extern const uint8_t gm_skvch52visg8i[13];
extern const uint8_t gm_sl0vvvamhv0to[12];
extern const uint8_t gm_sl1a3p7pocs82[15];
extern const uint8_t gm_sl2oiu9s0celr[49];
extern const uint8_t gm_sl2sr5fl46c73[48];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_sl63bnit79nfo[41];
extern const uint8_t gm_sl66hhhp1j2a6[14];
extern const uint8_t gm_slahfmk66v1v4[15];
extern const uint8_t gm_slb3ondne09t1[28];
extern const uint8_t gm_slc0d2s6km3s5[5];
extern const uint8_t gm_slc58j6a4e39a[56];
extern const uint8_t gm_slf1jt4j4vgli[9];
extern const uint8_t gm_slgmqgkioa4u6[3];
extern const uint8_t gm_slj7i6lishtfg[47];
extern const uint8_t gm_sll7gknpe999d[4];
extern const uint8_t gm_slmlefe4cpf5e[15];
extern const uint8_t gm_slqgr0k308e4c[30];
extern const uint8_t gm_slr574u01mn8m[17];
extern const uint8_t gm_slshl1j266n3l[7];
extern const uint8_t gm_slsmq9u0kk3l3[6];
extern const uint8_t gm_sm1kt2tr7m23b[4];
extern const uint8_t gm_sm3gke457vdm3[5];
extern const uint8_t gm_sm4ujt9jgj9au[8];
extern const uint8_t gm_sm55k23067j17[16];
extern const uint8_t gm_sm6j3q0d4n2jp[3];
extern const uint8_t gm_sm732n1u78vvc[16];
extern const uint8_t gm_sm922ui8k0oca[9];
extern const uint8_t gm_sm94b3smoinpd[60];
extern const uint8_t gm_smdj5floh97je[10];
extern const uint8_t gm_smggdkb5phvp0[56];
extern const uint8_t gm_smnfu5n5pum1l[2];
extern const uint8_t gm_smo0niiqr9onn[11];
extern const uint8_t gm_smooaouqmm8dj[2];
extern const uint8_t gm_smpmtloh1fun3[36];
extern const uint8_t gm_smrahl8ppkcu9[34];
extern const uint8_t gm_smrc1u3r4i5hd[7];
extern const uint8_t gm_smsf0v36m37b1[66];
extern const uint8_t gm_smu6g4sneo92j[6];
extern const uint8_t gm_smu8iti9fc903[85];
extern const uint8_t gm_sn0ac8j2fho01[22];
extern const uint8_t gm_sn0d6pm4psq1h[21];
extern const uint8_t gm_sn0kh110541oo[23];
extern const uint8_t gm_sn1puqmdafub0[14];
extern const uint8_t gm_sn1seggk13bop[4];
extern const uint8_t gm_sn5f6h6alcm9q[12];
extern const uint8_t gm_sn6gnsehoh5ru[7];
extern const uint8_t gm_sn9ftiite84ek[41];
extern const uint8_t gm_snaenr23tbgmb[23];
extern const uint8_t gm_sncti78voitis[76];
extern const uint8_t gm_snhskkd5kfgfa[31];
extern const uint8_t gm_snk6krn32vlub[10];
extern const uint8_t gm_snkldl7hr4htt[4];
extern const uint8_t gm_snls9i3gr3n3f[41];
extern const uint8_t gm_snqmbfl2fr85m[17];
extern const uint8_t gm_snqmeaue6lcfo[9];
extern const uint8_t gm_snr5o5tak6405[4];
extern const uint8_t gm_snrrubbi4s05g[16];
extern const uint8_t gm_snud2jia3fe8d[18];
extern const uint8_t gm_snukl6jqep6jf[4];
extern const uint8_t gm_so1gbme93p3pe[5];
extern const uint8_t gm_so25i3en68imc[27];
extern const uint8_t gm_so2dhrd52m05k[49];
extern const uint8_t gm_so3ukp67pds22[190];
extern const uint8_t gm_so48b6sdoerku[18];
extern const uint8_t gm_so4d6ttcc6c3j[41];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_so7pbnda42pgd[9];
extern const uint8_t gm_so8oee2j1e9l0[6];
extern const uint8_t gm_so9fi6r28770a[3];
extern const uint8_t gm_soa9mnl4ahjud[8];
extern const uint8_t gm_sodgjk6pvm3b4[21];
extern const uint8_t gm_sodivosk5r8rb[2];
extern const uint8_t gm_sojb2hhp0ln7r[31];
extern const uint8_t gm_sojts6dcvra33[35];
extern const uint8_t gm_solsl3u2gfc4f[7];
extern const uint8_t gm_som0q53jqvjh3[5];
extern const uint8_t gm_sopjc7gjg0pgu[32];
extern const uint8_t gm_sp0bci8i8o3nt[3];
extern const uint8_t gm_sp0ce5j43v7u2[26];
extern const uint8_t gm_sp1srk1sv0crk[52];
extern const uint8_t gm_sp24uj950qpkl[16];
extern const uint8_t gm_sp46pm3sna4bl[86];
extern const uint8_t gm_sp796mgrgp4mj[45];
extern const uint8_t gm_spaa4sseck2fg[45];
extern const uint8_t gm_spaflckd9bou5[29];
extern const uint8_t gm_spasf3abnfbjh[8];
extern const uint8_t gm_spbdk1kpko0k2[141];
extern const uint8_t gm_spducfcn5c083[69];
extern const uint8_t gm_spervk999ikim[8];
extern const uint8_t gm_spf5cnp0l9k4n[449];
extern const uint8_t gm_spfojlg262423[13];
extern const uint8_t gm_spi47lg4tr80j[3];
extern const uint8_t gm_spl4qnmq2arsc[13];
extern const uint8_t gm_spm4139orpkvm[10];
extern const uint8_t gm_spmhbt07q6beu[9];
extern const uint8_t gm_spnr89edksdqp[4];
extern const uint8_t gm_spod3tmkh25up[2];
extern const uint8_t gm_spp2faaelnnt0[8];
extern const uint8_t gm_spr959sr1jhja[5];
extern const uint8_t gm_spt7rbjl7ucc9[8];
extern const uint8_t gm_sq0bes3qstkub[15];
extern const uint8_t gm_sq2es9bij87b7[14];
extern const uint8_t gm_sq32stff79ol1[22];
extern const uint8_t gm_sq6e9d3107t3s[11];
extern const uint8_t gm_sq7ijigadotf8[17];
extern const uint8_t gm_sq8839lqmgrd7[11];
extern const uint8_t gm_sq8aqd2uj6ppq[12];
extern const uint8_t gm_sq9lne9r5j3u2[8];
extern const uint8_t gm_sq9org4qoda29[12];
extern const uint8_t gm_sq9qefps1uqei[10];
extern const uint8_t gm_sqa29afvhov50[6];
extern const uint8_t gm_sqbbhnhgkc31j[14];
extern const uint8_t gm_sqcjuf1r2epl2[7];
extern const uint8_t gm_sqdfr4k3vea5p[4];
extern const uint8_t gm_sqdqshat9srld[37];
extern const uint8_t gm_sqdvt3ooptrnc[5];
extern const uint8_t gm_sqflguscq1uc5[6];
extern const uint8_t gm_sqfsba89a30n8[26];
extern const uint8_t gm_sqg70d5uem21e[35];
extern const uint8_t gm_sqi8fcv4bi7s6[10];
extern const uint8_t gm_sqit0ai5v3joc[23];
extern const uint8_t gm_sql1qe620spbq[23];
extern const uint8_t gm_sqlqubis691m5[66];
extern const uint8_t gm_sqm6u7rqmi1il[19];
extern const uint8_t gm_sqn7kgtu67hrg[42];
extern const uint8_t gm_sqqvi1jr97u13[10];
extern const uint8_t gm_sqr1rjjq3dpuh[17];
extern const uint8_t gm_sqrh6i8djv6b4[7];
extern const uint8_t gm_sqrsljl0ejj01[41];
extern const uint8_t gm_sqt8gk1nsci25[5];
extern const uint8_t gm_sqtabum90d47u[10];
extern const uint8_t gm_sqtultomupp4c[22];
extern const uint8_t gm_squ4ohvqdqaap[79];
extern const uint8_t gm_sqvepl1893o5g[3];
extern const uint8_t gm_sqvn1en6c2n9e[32];
extern const uint8_t gm_sqvvajmjm3fif[11];
extern const uint8_t gm_sr0hr1ep2j2vv[5];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const uint8_t gm_sr5mkle2k9poq[28];
extern const uint8_t gm_sr899jbhl9noj[3];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src0m6lnv6m2d3p[];
extern const char gm_src2pgaip05o19o[];
extern const char gm_src3dhhc4laniae[];
extern const char gm_src4tam33qo20j7[];
extern const char gm_src6unagr666v56[];
extern const char gm_src7u0d314efpdm[];
extern const char gm_src9vg8k4mbj38f[];
extern const char gm_srca29n3fkrpbdt[];
extern const char gm_srccfhjr4r9mdef[];
extern const char gm_srcihhtlsvd7f6i[];
extern const char gm_srclt70oog8j9io[];
extern const char gm_srcr9oeab0h5mup[];
extern const char gm_srct4pcukr5kqhq[];
extern const char gm_srcu4ee17t4impf[];
extern const char gm_srcue4ionbe9lgh[];
extern const uint8_t gm_sremdpsmf4ala[50];
extern const uint8_t gm_srg9h7ti0cg3a[25];
extern const uint8_t gm_srhsu0ubjqf5l[7];
extern const uint8_t gm_sri1d6adh4kkk[11];
extern const uint8_t gm_srim6iisdhve7[41];
extern const uint8_t gm_srj7urnaf6a1u[9];
extern const uint8_t gm_srjcgu24i74p9[48];
extern const uint8_t gm_srk1d80eamfhd[15];
extern const uint8_t gm_srlklqc70q6ju[3];
extern const uint8_t gm_srq25fqvivo2q[73];
extern const uint8_t gm_srrm1sevfig2o[3];
extern const uint8_t gm_srs4r9h62u04l[13];
extern const uint8_t gm_sru4dh297ccek[19];
extern const uint8_t gm_ss03naco6s5l7[5];
extern const uint8_t gm_ss3i577tvjetf[8];
extern const uint8_t gm_ss4337v5p4tj3[5];
extern const uint8_t gm_ss496b65r9omj[6];
extern const uint8_t gm_ss52pcn7gi1ca[25];
extern const uint8_t gm_ss54u8us11c4v[42];
extern const uint8_t gm_ss7iv2kssvf80[7];
extern const uint8_t gm_ss9sitl8h5uvg[18];
extern const uint8_t gm_ssb6m4k22290v[17];
extern const uint8_t gm_sse16dr2qj0im[3];
extern const uint8_t gm_ssfg877iegnma[4];
extern const uint8_t gm_ssfs2ebod65fl[38];
extern const uint8_t gm_ssgn9mppmu3vl[16];
extern const uint8_t gm_sshhi6fn3vfaa[45];
extern const uint8_t gm_sshi9ffc6nkc4[10];
extern const uint8_t gm_ssi4rrma2q00g[7];
extern const uint8_t gm_ssi95ri2h4d02[28];
extern const uint8_t gm_ssici0eo813ko[17];
extern const uint8_t gm_sslm2oq18gln3[6];
extern const uint8_t gm_ssmr7jgj9e42t[25];
extern const uint8_t gm_ssnku2e1q7u3k[13];
extern const uint8_t gm_ssoik29mo3fp0[53];
extern const uint8_t gm_ssr91slt3ah1c[11];
extern const uint8_t gm_ssrd6bs7gcs4g[29];
extern const uint8_t gm_sstppps49t867[11];
extern const uint8_t gm_sstqe1228ic6k[51];
extern const uint8_t gm_ssv1lstjje7r5[7];
extern const uint8_t gm_ssvci9220kuoj[45];
extern const uint8_t gm_ssvcneuafnfds[8];
extern const uint8_t gm_st0jtdoe1b7a6[15];
extern const uint8_t gm_st28rv7m1lueb[9];
extern const uint8_t gm_st3m5iduoqh8b[5];
extern const uint8_t gm_st3rnlu97af0r[63];
extern const uint8_t gm_st4r3sao0kja5[6];
extern const uint8_t gm_st518h9bgt9n7[22];
extern const uint8_t gm_st53sho4849cg[18];
extern const uint8_t gm_st62vtcrpam9i[17];
extern const uint8_t gm_st6ksn77egld4[13];
extern const uint8_t gm_stadskj9k22h7[30];
extern const uint8_t gm_stcd71a4feia0[10];
extern const uint8_t gm_std877jr2ed05[29];
extern const uint8_t gm_stddp43pjd9r1[51];
extern const uint8_t gm_sterslo87taeu[39];
extern const uint8_t gm_stfg6elt037dh[6];
extern const uint8_t gm_stfp83525k3nr[7];
extern const uint8_t gm_sth0p0t3b0nta[24];
extern const uint8_t gm_stj45lnsahel3[30];
extern const uint8_t gm_stlojc09k1lf1[41];
extern const uint8_t gm_stmgel74lrtrn[33];
extern const uint8_t gm_stn52kuiq0jk2[12];
extern const uint8_t gm_stoicv7ns2elf[14];
extern const uint8_t gm_stoidbi3vfa99[30];
extern const uint8_t gm_stqlepc346jgk[85];
extern const uint8_t gm_stum5f38vfqa2[15];
extern const uint8_t gm_stuutd7nfq21h[3];
extern const uint8_t gm_stvja3baka61k[8];
extern const uint8_t gm_su06jr9k1rhdo[15];
extern const uint8_t gm_su20i5iji4ao3[4];
extern const uint8_t gm_su2ima1nbr01d[52];
extern const uint8_t gm_su38t05utsk53[67];
extern const uint8_t gm_su3ahsl9p584f[9];
extern const uint8_t gm_su41v0g030vmk[95];
extern const uint8_t gm_su6oav9vpqcnu[17];
extern const uint8_t gm_su8bu9ae1ds2f[19];
extern const uint8_t gm_suc581b27el81[166];
extern const uint8_t gm_suctpm7bivu2g[48];
extern const uint8_t gm_suf3bl270p4gs[56];
extern const uint8_t gm_sufp89bevlq87[5];
extern const uint8_t gm_suhlp72acbph4[43];
extern const uint8_t gm_suia0naoru7mp[7];
extern const uint8_t gm_sukj4damah3dd[85];
extern const uint8_t gm_sulvvqvd5gcql[15];
extern const uint8_t gm_sumvvvaqn43sl[10];
extern const uint8_t gm_sut7mhrkhqp77[56];
extern const uint8_t gm_suup20cg9lo2d[8];
extern const uint8_t gm_suvsl6ie8luas[15];
extern const uint8_t gm_sv0odl408mcff[24];
extern const uint8_t gm_sv1bu4l4kq188[8];
extern const uint8_t gm_sv2qh30031o3v[5];
extern const uint8_t gm_sv52j7o6hr9j8[10];
extern const uint8_t gm_sv56kn0notafb[17];
extern const uint8_t gm_sv6dkmq38siqt[27];
extern const uint8_t gm_sv7tbc2gvmpa1[100];
extern const uint8_t gm_sv8pauo1bhltl[9];
extern const uint8_t gm_sv96uhaheg6dq[41];
extern const uint8_t gm_svdcmt7tps0ma[57];
extern const uint8_t gm_sve2u8a7eapjo[18];
extern const uint8_t gm_svirf1u8t89n0[47];
extern const uint8_t gm_svjl1hr0jvvbv[9];
extern const uint8_t gm_svl0gta90bsdr[49];
extern const uint8_t gm_svmt0iljnadus[4];
extern const uint8_t gm_svpr02j6er8vd[15];
extern const uint8_t gm_svq6hhfarqhmv[10];
extern const uint8_t gm_svsf0pu3t8u6m[7];
extern const uint8_t gm_svutc5hf6cgiv[2];
extern const gm_spec *const gm_tpl07e4gjk0ub6d_s[];
extern const uint32_t gm_tpl0e2i60vm7gtr_i[];
extern const gm_spec *const gm_tpl0epjr4745p10_s[];
extern const uint32_t gm_tpl0f11vet24cfp_i[];
extern const gm_tmpl gm_tpl0pva6pvhuq6i;
extern const uint32_t gm_tpl0pva6pvhuq6i_i[];
extern const gm_tmpl_piece gm_tpl0pva6pvhuq6i_p[];
extern const gm_spec *const gm_tpl0pva6pvhuq6i_s[];
extern const gm_spec *const gm_tpl0rlavm9oqi24_s[];
extern const gm_tmpl gm_tpl0scna75sco5f;
extern const uint32_t gm_tpl0scna75sco5f_i[];
extern const gm_tmpl_piece gm_tpl0scna75sco5f_p[];
extern const gm_spec *const gm_tpl0scna75sco5f_s[];
extern const gm_spec *const gm_tpl16elqfq3d1nr_s[];
extern const uint32_t gm_tpl16gsv91msflc_i[];
extern const gm_tmpl_piece gm_tpl1bno78mc58dl_p[];
extern const gm_tmpl gm_tpl1dleoebfmvhp;
extern const uint32_t gm_tpl1dleoebfmvhp_i[];
extern const gm_tmpl_piece gm_tpl1dleoebfmvhp_p[];
extern const gm_spec *const gm_tpl1dleoebfmvhp_s[];
extern const uint32_t gm_tpl1f1njss89nj4_i[];
extern const gm_spec *const gm_tpl1f1njss89nj4_s[];
extern const gm_tmpl gm_tpl1g1lh0kp24oa;
extern const uint32_t gm_tpl1g1lh0kp24oa_i[];
extern const gm_tmpl_piece gm_tpl1g1lh0kp24oa_p[];
extern const gm_spec *const gm_tpl1g1lh0kp24oa_s[];
extern const gm_tmpl_piece gm_tpl1lvejpvpq0qh_p[];
extern const gm_spec *const gm_tpl1nqa0en23gvp_s[];
extern const gm_spec *const gm_tpl1o2hk14gjjj0_s[];
extern const gm_tmpl_piece gm_tpl1ut7q7afn48u_p[];
extern const gm_tmpl gm_tpl24fci6dm83eo;
extern const uint32_t gm_tpl24fci6dm83eo_i[];
extern const gm_tmpl_piece gm_tpl24fci6dm83eo_p[];
extern const gm_spec *const gm_tpl24fci6dm83eo_s[];
extern const gm_tmpl_piece gm_tpl2bm4b6itkmm6_p[];
extern const uint32_t gm_tpl2cgrvj4ke42d_i[];
extern const gm_tmpl_piece gm_tpl328oas8hh4jg_p[];
extern const gm_tmpl gm_tpl32o7ct8vibbi;
extern const uint32_t gm_tpl32o7ct8vibbi_i[];
extern const gm_tmpl_piece gm_tpl32o7ct8vibbi_p[];
extern const gm_spec *const gm_tpl32o7ct8vibbi_s[];
extern const gm_tmpl gm_tpl33jf6q62dk07;
extern const uint32_t gm_tpl33jf6q62dk07_i[];
extern const gm_tmpl_piece gm_tpl33jf6q62dk07_p[];
extern const gm_spec *const gm_tpl33jf6q62dk07_s[];
extern const gm_tmpl gm_tpl344kemfor61g;
extern const uint32_t gm_tpl344kemfor61g_i[];
extern const gm_tmpl_piece gm_tpl344kemfor61g_p[];
extern const gm_spec *const gm_tpl344kemfor61g_s[];
extern const gm_tmpl gm_tpl34tavcs9635o;
extern const uint32_t gm_tpl34tavcs9635o_i[];
extern const gm_tmpl_piece gm_tpl34tavcs9635o_p[];
extern const gm_spec *const gm_tpl34tavcs9635o_s[];
extern const uint32_t gm_tpl3dghmndqn708_i[];
extern const gm_tmpl_piece gm_tpl3dghmndqn708_p[];
extern const uint32_t gm_tpl3ergg6mce23h_i[];
extern const uint32_t gm_tpl3kt8q9c3ih5v_i[];
extern const gm_tmpl_piece gm_tpl3lqpj3d2egtb_p[];
extern const gm_tmpl gm_tpl3qt6ss8l9cgf;
extern const uint32_t gm_tpl3qt6ss8l9cgf_i[];
extern const gm_tmpl_piece gm_tpl3qt6ss8l9cgf_p[];
extern const gm_spec *const gm_tpl3qt6ss8l9cgf_s[];
extern const gm_tmpl gm_tpl3ttr7njhqa54;
extern const uint32_t gm_tpl3ttr7njhqa54_i[];
extern const gm_tmpl_piece gm_tpl3ttr7njhqa54_p[];
extern const gm_spec *const gm_tpl3ttr7njhqa54_s[];
extern const gm_tmpl_piece gm_tpl3v2bn2jpabsf_p[];
extern const gm_tmpl_piece gm_tpl4476c5bksft1_p[];
extern const gm_tmpl gm_tpl44b660pgg1c8;
extern const uint32_t gm_tpl44b660pgg1c8_i[];
extern const gm_tmpl_piece gm_tpl44b660pgg1c8_p[];
extern const gm_spec *const gm_tpl44b660pgg1c8_s[];
extern const gm_spec *const gm_tpl461nvut9qmgp_s[];
extern const gm_spec *const gm_tpl4cbbis3gd1k3_s[];
extern const gm_spec *const gm_tpl4g1h0tr3apfi_s[];
extern const uint32_t gm_tpl4lt7abhi3ej4_i[];
extern const gm_tmpl gm_tpl4mli0f9rjihf;
extern const uint32_t gm_tpl4mli0f9rjihf_i[];
extern const gm_tmpl_piece gm_tpl4mli0f9rjihf_p[];
extern const gm_spec *const gm_tpl4mli0f9rjihf_s[];
extern const uint32_t gm_tpl4nfbp2ikscbk_i[];
extern const gm_spec *const gm_tpl4nfbp2ikscbk_s[];
extern const gm_spec *const gm_tpl4npct6bth1c7_s[];
extern const gm_tmpl_piece gm_tpl4o9g8c43tino_p[];
extern const gm_tmpl gm_tpl4phv0vr9as7f;
extern const uint32_t gm_tpl4phv0vr9as7f_i[];
extern const gm_tmpl_piece gm_tpl4phv0vr9as7f_p[];
extern const gm_spec *const gm_tpl4phv0vr9as7f_s[];
extern const gm_tmpl gm_tpl4qnmv5o1ceod;
extern const uint32_t gm_tpl4qnmv5o1ceod_i[];
extern const gm_tmpl_piece gm_tpl4qnmv5o1ceod_p[];
extern const gm_spec *const gm_tpl4qnmv5o1ceod_s[];
extern const gm_spec *const gm_tpl4slc3so8u8pa_s[];
extern const gm_tmpl_piece gm_tpl4t11fkqh738f_p[];
extern const gm_tmpl gm_tpl5677p7i476ao;
extern const uint32_t gm_tpl5677p7i476ao_i[];
extern const gm_tmpl_piece gm_tpl5677p7i476ao_p[];
extern const gm_spec *const gm_tpl5677p7i476ao_s[];
extern const gm_tmpl_piece gm_tpl59lqltdr0h0r_p[];
extern const gm_spec *const gm_tpl5a7qf3e2brk4_s[];
extern const gm_spec *const gm_tpl5cbugllfe764_s[];
extern const gm_spec *const gm_tpl5d3ksa1ntdkn_s[];
extern const gm_tmpl gm_tpl5g9nntg83p8b;
extern const uint32_t gm_tpl5g9nntg83p8b_i[];
extern const gm_tmpl_piece gm_tpl5g9nntg83p8b_p[];
extern const gm_spec *const gm_tpl5g9nntg83p8b_s[];
extern const gm_spec *const gm_tpl5idbni8n88m7_s[];
extern const gm_tmpl gm_tpl5kgb1tfsut02;
extern const uint32_t gm_tpl5kgb1tfsut02_i[];
extern const gm_tmpl_piece gm_tpl5kgb1tfsut02_p[];
extern const gm_spec *const gm_tpl5kgb1tfsut02_s[];
extern const gm_tmpl gm_tpl5kl598jq5h12;
extern const uint32_t gm_tpl5kl598jq5h12_i[];
extern const gm_tmpl_piece gm_tpl5kl598jq5h12_p[];
extern const gm_spec *const gm_tpl5kl598jq5h12_s[];
extern const gm_tmpl_piece gm_tpl5ldtdub2029b_p[];
extern const gm_spec *const gm_tpl5m2t8us2v800_s[];
extern const gm_tmpl gm_tpl5poqk71bg1bn;
extern const uint32_t gm_tpl5poqk71bg1bn_i[];
extern const gm_tmpl_piece gm_tpl5poqk71bg1bn_p[];
extern const gm_spec *const gm_tpl5poqk71bg1bn_s[];
extern const gm_tmpl_piece gm_tpl5prdhjdrg194_p[];
extern const gm_tmpl gm_tpl5q4rr3nd9637;
extern const uint32_t gm_tpl5q4rr3nd9637_i[];
extern const gm_tmpl_piece gm_tpl5q4rr3nd9637_p[];
extern const gm_spec *const gm_tpl5q4rr3nd9637_s[];
extern const gm_tmpl_piece gm_tpl5qm9nkkj1gjg_p[];
extern const gm_tmpl gm_tpl5r25bpo8hobt;
extern const uint32_t gm_tpl5r25bpo8hobt_i[];
extern const gm_tmpl_piece gm_tpl5r25bpo8hobt_p[];
extern const gm_spec *const gm_tpl5r25bpo8hobt_s[];
extern const gm_tmpl gm_tpl5s6q12qjusi2;
extern const uint32_t gm_tpl5s6q12qjusi2_i[];
extern const gm_tmpl_piece gm_tpl5s6q12qjusi2_p[];
extern const gm_spec *const gm_tpl5s6q12qjusi2_s[];
extern const gm_spec *const gm_tpl5uko75p49g56_s[];
extern const gm_spec *const gm_tpl5vbp9fc0mpc1_s[];
extern const uint32_t gm_tpl5vfcnh2qs08h_i[];
extern const gm_tmpl_piece gm_tpl60na9lt7qpkj_p[];
extern const gm_tmpl_piece gm_tpl63djfct0mcad_p[];
extern const gm_tmpl gm_tpl63iuq3je4sbr;
extern const uint32_t gm_tpl63iuq3je4sbr_i[];
extern const gm_tmpl_piece gm_tpl63iuq3je4sbr_p[];
extern const gm_spec *const gm_tpl63iuq3je4sbr_s[];
extern const gm_tmpl gm_tpl65tpfh2uj3bg;
extern const uint32_t gm_tpl65tpfh2uj3bg_i[];
extern const gm_tmpl_piece gm_tpl65tpfh2uj3bg_p[];
extern const gm_spec *const gm_tpl65tpfh2uj3bg_s[];
extern const gm_tmpl gm_tpl6a59mrfvoo09;
extern const uint32_t gm_tpl6a59mrfvoo09_i[];
extern const gm_tmpl_piece gm_tpl6a59mrfvoo09_p[];
extern const gm_spec *const gm_tpl6a59mrfvoo09_s[];
extern const gm_spec *const gm_tpl6gp425a13qgr_s[];
extern const gm_tmpl gm_tpl6muahgpe70pr;
extern const uint32_t gm_tpl6muahgpe70pr_i[];
extern const gm_tmpl_piece gm_tpl6muahgpe70pr_p[];
extern const gm_spec *const gm_tpl6muahgpe70pr_s[];
extern const gm_spec *const gm_tpl6nq0lpkkblpc_s[];
extern const uint32_t gm_tpl6ptbf3l6su1b_i[];
extern const uint32_t gm_tpl6qj65gvm8vp3_i[];
extern const gm_tmpl_piece gm_tpl70tkged35ahl_p[];
extern const gm_spec *const gm_tpl75d11no80fsi_s[];
extern const gm_tmpl_piece gm_tpl75h0vbo3vani_p[];
extern const uint32_t gm_tpl7da8g7lhjmr1_i[];
extern const gm_tmpl_piece gm_tpl7eeir95ujdvn_p[];
extern const gm_tmpl_piece gm_tpl7kgo5i3aumd2_p[];
extern const gm_tmpl_piece gm_tpl7kmb2inm0gau_p[];
extern const uint32_t gm_tpl7m3t7r6fqpf7_i[];
extern const gm_tmpl_piece gm_tpl7qfg883s3b97_p[];
extern const gm_tmpl gm_tpl7r51aeh92b80;
extern const uint32_t gm_tpl7r51aeh92b80_i[];
extern const gm_tmpl_piece gm_tpl7r51aeh92b80_p[];
extern const gm_spec *const gm_tpl7r51aeh92b80_s[];
extern const uint32_t gm_tpl7sqjangpsngf_i[];
extern const gm_tmpl gm_tpl7t589bv3t4us;
extern const uint32_t gm_tpl7t589bv3t4us_i[];
extern const gm_tmpl_piece gm_tpl7t589bv3t4us_p[];
extern const gm_spec *const gm_tpl7t589bv3t4us_s[];
extern const uint32_t gm_tpl810t9tkid6i2_i[];
extern const uint32_t gm_tpl81nh98hgo1rk_i[];
extern const gm_tmpl_piece gm_tpl81uel28fst2a_p[];
extern const gm_tmpl_piece gm_tpl84co505h34ud_p[];
extern const gm_tmpl_piece gm_tpl86pqqgq5ch0o_p[];
extern const gm_tmpl_piece gm_tpl87nh31l9js4u_p[];
extern const gm_tmpl_piece gm_tpl88ij4e6nvk4u_p[];
extern const gm_tmpl_piece gm_tpl8bar0du85qu4_p[];
extern const gm_tmpl_piece gm_tpl8f1efhudc5d4_p[];
extern const gm_tmpl gm_tpl8geisveh2vug;
extern const uint32_t gm_tpl8geisveh2vug_i[];
extern const gm_tmpl_piece gm_tpl8geisveh2vug_p[];
extern const gm_spec *const gm_tpl8geisveh2vug_s[];
extern const gm_tmpl_piece gm_tpl8lt5kipnmd64_p[];
extern const gm_spec *const gm_tpl8ssnfo686ogd_s[];
extern const gm_spec *const gm_tpl8tri2h8bpkpg_s[];
extern const uint32_t gm_tpl8vbes2v6184u_i[];
extern const gm_tmpl_piece gm_tpl8vbes2v6184u_p[];
extern const uint32_t gm_tpl91b7sjgnmine_i[];
extern const uint32_t gm_tpl92h94vggr1gu_i[];
extern const gm_tmpl gm_tpl941gb7vu8kgv;
extern const uint32_t gm_tpl941gb7vu8kgv_i[];
extern const gm_tmpl_piece gm_tpl941gb7vu8kgv_p[];
extern const gm_spec *const gm_tpl941gb7vu8kgv_s[];
extern const gm_tmpl_piece gm_tpl955dep3t38r6_p[];
extern const gm_tmpl gm_tpl9cnobsjbh0r2;
extern const uint32_t gm_tpl9cnobsjbh0r2_i[];
extern const gm_tmpl_piece gm_tpl9cnobsjbh0r2_p[];
extern const gm_spec *const gm_tpl9cnobsjbh0r2_s[];
extern const gm_spec *const gm_tpl9kt3nn7p3a92_s[];
extern const gm_tmpl_piece gm_tpl9ne0ajfec16n_p[];
extern const gm_tmpl_piece gm_tpl9qc0voknsp7g_p[];
extern const gm_spec *const gm_tpl9qchhbq9qlbm_s[];
extern const gm_spec *const gm_tpl9sh9d989om94_s[];
extern const uint32_t gm_tpl9va1ubk8bean_i[];
extern const gm_tmpl_piece gm_tpla0a5a633lqve_p[];
extern const uint32_t gm_tpla5rqmr49dnnk_i[];
extern const uint32_t gm_tpla6rojfclgc54_i[];
extern const gm_spec *const gm_tpla8nvlakostko_s[];
extern const gm_tmpl_piece gm_tpla93ou7df923g_p[];
extern const gm_tmpl gm_tplacjkb64s2e83;
extern const uint32_t gm_tplacjkb64s2e83_i[];
extern const gm_tmpl_piece gm_tplacjkb64s2e83_p[];
extern const gm_spec *const gm_tplacjkb64s2e83_s[];
extern const uint32_t gm_tplahe50smef36f_i[];
extern const gm_tmpl_piece gm_tplak7vr9q0b05e_p[];
extern const gm_tmpl gm_tplaka4u9f9dfvk;
extern const uint32_t gm_tplaka4u9f9dfvk_i[];
extern const gm_tmpl_piece gm_tplaka4u9f9dfvk_p[];
extern const gm_spec *const gm_tplaka4u9f9dfvk_s[];
extern const gm_tmpl_piece gm_tplalgvrfo85soq_p[];
extern const gm_spec *const gm_tplalgvrfo85soq_s[];
extern const gm_tmpl_piece gm_tplanhmvcr622qo_p[];
extern const uint32_t gm_tplaotru3rka6ku_i[];
extern const gm_spec *const gm_tplap4dl087fd4l_s[];
extern const gm_tmpl_piece gm_tplaq7jlj8i0s87_p[];
extern const gm_tmpl gm_tplar04pgor24ut;
extern const uint32_t gm_tplar04pgor24ut_i[];
extern const gm_tmpl_piece gm_tplar04pgor24ut_p[];
extern const gm_spec *const gm_tplar04pgor24ut_s[];
extern const uint32_t gm_tplb2ihdc8dbskd_i[];
extern const gm_tmpl_piece gm_tplb4rog3tmrj9n_p[];
extern const gm_tmpl gm_tplb899rpghqa93;
extern const uint32_t gm_tplb899rpghqa93_i[];
extern const gm_tmpl_piece gm_tplb899rpghqa93_p[];
extern const gm_spec *const gm_tplb899rpghqa93_s[];
extern const gm_tmpl_piece gm_tplb9surs05k5po_p[];
extern const gm_tmpl gm_tplbckos3fl05qe;
extern const uint32_t gm_tplbckos3fl05qe_i[];
extern const gm_tmpl_piece gm_tplbckos3fl05qe_p[];
extern const gm_spec *const gm_tplbckos3fl05qe_s[];
extern const gm_tmpl_piece gm_tplbds86ojmcoik_p[];
extern const gm_tmpl_piece gm_tplbjh9dr2llnkb_p[];
extern const gm_spec *const gm_tplbnt69qpq91i8_s[];
extern const gm_tmpl_piece gm_tplbog2sc0rf3ot_p[];
extern const gm_tmpl gm_tplbulo51k20om4;
extern const uint32_t gm_tplbulo51k20om4_i[];
extern const gm_tmpl_piece gm_tplbulo51k20om4_p[];
extern const gm_spec *const gm_tplbulo51k20om4_s[];
extern const gm_spec *const gm_tplc1emmpcosf7b_s[];
extern const gm_spec *const gm_tplc2ekf83ggtj3_s[];
extern const gm_tmpl_piece gm_tplc47d0vivqs48_p[];
extern const gm_tmpl gm_tplc5prn5jeukj6;
extern const uint32_t gm_tplc5prn5jeukj6_i[];
extern const gm_tmpl_piece gm_tplc5prn5jeukj6_p[];
extern const gm_spec *const gm_tplc5prn5jeukj6_s[];
extern const uint32_t gm_tplc8kkdoja48ii_i[];
extern const gm_tmpl_piece gm_tplca6pmroagoou_p[];
extern const gm_spec *const gm_tplca6pmroagoou_s[];
extern const gm_spec *const gm_tplcic56vj3l629_s[];
extern const gm_tmpl gm_tplck20jih4okbk;
extern const uint32_t gm_tplck20jih4okbk_i[];
extern const gm_tmpl_piece gm_tplck20jih4okbk_p[];
extern const gm_spec *const gm_tplck20jih4okbk_s[];
extern const uint32_t gm_tplck4jpb6nd99p_i[];
extern const gm_tmpl_piece gm_tplck4jpb6nd99p_p[];
extern const uint32_t gm_tplcktokqd7n6fo_i[];
extern const gm_tmpl_piece gm_tplcl8es85s6qrs_p[];
extern const uint32_t gm_tplcn6c0fsph4j2_i[];
extern const gm_tmpl_piece gm_tplcoet0p57pg4s_p[];
extern const gm_tmpl_piece gm_tplcpt7hf5iscqd_p[];
extern const uint32_t gm_tplcqfaq9vjn7hh_i[];
extern const gm_tmpl_piece gm_tplcte0qqjaa0q0_p[];
extern const uint32_t gm_tplctjp7jb08m5t_i[];
extern const gm_tmpl_piece gm_tplctjp7jb08m5t_p[];
extern const gm_tmpl_piece gm_tpld0t14m1fld0p_p[];
extern const gm_tmpl_piece gm_tpld1ficc6ikf1e_p[];
extern const uint32_t gm_tpld1hhk0kef14l_i[];
extern const gm_spec *const gm_tpld4u3te7g9c5v_s[];
extern const uint32_t gm_tpld7d7u7to2a7u_i[];
extern const uint32_t gm_tpld7ebjjnpmsr0_i[];
extern const gm_tmpl gm_tpld9ts015fint3;
extern const uint32_t gm_tpld9ts015fint3_i[];
extern const gm_tmpl_piece gm_tpld9ts015fint3_p[];
extern const gm_spec *const gm_tpld9ts015fint3_s[];
extern const gm_spec *const gm_tpldn8tr7ddtik6_s[];
extern const gm_spec *const gm_tpldqjt9kdk4ntt_s[];
extern const gm_tmpl gm_tpldrcjvgmk3ig3;
extern const uint32_t gm_tpldrcjvgmk3ig3_i[];
extern const gm_tmpl_piece gm_tpldrcjvgmk3ig3_p[];
extern const gm_spec *const gm_tpldrcjvgmk3ig3_s[];
extern const uint32_t gm_tpldt4nur6h705h_i[];
extern const uint32_t gm_tpldtm53npup7ha_i[];
extern const gm_tmpl gm_tpldvhfe50cnb69;
extern const uint32_t gm_tpldvhfe50cnb69_i[];
extern const gm_tmpl_piece gm_tpldvhfe50cnb69_p[];
extern const gm_spec *const gm_tpldvhfe50cnb69_s[];
extern const gm_spec *const gm_tple4r0jrrru5m7_s[];
extern const uint32_t gm_tple4r1aumd134l_i[];
extern const gm_spec *const gm_tple4r1aumd134l_s[];
extern const gm_spec *const gm_tple59d4hgjgp3n_s[];
extern const gm_tmpl_piece gm_tple7i0r0sg6sjs_p[];
extern const uint32_t gm_tple7ipevcaonst_i[];
extern const gm_tmpl_piece gm_tplebqthjijvqt0_p[];
extern const gm_tmpl gm_tplecdkophfbiam;
extern const uint32_t gm_tplecdkophfbiam_i[];
extern const gm_tmpl_piece gm_tplecdkophfbiam_p[];
extern const gm_spec *const gm_tplecdkophfbiam_s[];
extern const gm_spec *const gm_tplehc5f52ftol1_s[];
extern const gm_tmpl gm_tplei8v65ors6jc;
extern const uint32_t gm_tplei8v65ors6jc_i[];
extern const gm_tmpl_piece gm_tplei8v65ors6jc_p[];
extern const gm_spec *const gm_tplei8v65ors6jc_s[];
extern const gm_tmpl gm_tplei9v853i04sp;
extern const uint32_t gm_tplei9v853i04sp_i[];
extern const gm_tmpl_piece gm_tplei9v853i04sp_p[];
extern const gm_spec *const gm_tplei9v853i04sp_s[];
extern const gm_tmpl_piece gm_tpleie0q8nvjqch_p[];
extern const gm_tmpl_piece gm_tplem02qig8laoi_p[];
extern const gm_spec *const gm_tplengfdkso35ua_s[];
extern const uint32_t gm_tplennc1kv0529b_i[];
extern const uint32_t gm_tpleoabj1u2rb54_i[];
extern const gm_tmpl_piece gm_tpleoabj1u2rb54_p[];
extern const gm_tmpl_piece gm_tpleonu21sc9fi7_p[];
extern const gm_tmpl gm_tplepdvp8ofjal7;
extern const uint32_t gm_tplepdvp8ofjal7_i[];
extern const gm_tmpl_piece gm_tplepdvp8ofjal7_p[];
extern const gm_spec *const gm_tplepdvp8ofjal7_s[];
extern const uint32_t gm_tplevafblg3gmq9_i[];
extern const uint32_t gm_tplf27d39e1vcer_i[];
extern const gm_tmpl gm_tplf3c2iulsht2e;
extern const uint32_t gm_tplf3c2iulsht2e_i[];
extern const gm_tmpl_piece gm_tplf3c2iulsht2e_p[];
extern const gm_spec *const gm_tplf3c2iulsht2e_s[];
extern const gm_tmpl gm_tplfft5kohcphn8;
extern const uint32_t gm_tplfft5kohcphn8_i[];
extern const gm_tmpl_piece gm_tplfft5kohcphn8_p[];
extern const gm_spec *const gm_tplfft5kohcphn8_s[];
extern const gm_tmpl gm_tplfjobpnodgc6e;
extern const uint32_t gm_tplfjobpnodgc6e_i[];
extern const gm_tmpl_piece gm_tplfjobpnodgc6e_p[];
extern const gm_spec *const gm_tplfjobpnodgc6e_s[];
extern const uint32_t gm_tplfm527r729efa_i[];
extern const gm_spec *const gm_tplfo19v5itil8t_s[];
extern const uint32_t gm_tplfoosl93e8k5t_i[];
extern const gm_tmpl_piece gm_tplfqk35ejq39la_p[];
extern const gm_tmpl_piece gm_tplfqr816g9de0k_p[];
extern const gm_spec *const gm_tplfsstr25ojml5_s[];
extern const uint32_t gm_tplftchh41b473r_i[];
extern const gm_spec *const gm_tplfv85d3oogq60_s[];
extern const gm_spec *const gm_tplg0rms0t7302m_s[];
extern const gm_tmpl gm_tplg3ifbt1a32e1;
extern const uint32_t gm_tplg3ifbt1a32e1_i[];
extern const gm_tmpl_piece gm_tplg3ifbt1a32e1_p[];
extern const gm_spec *const gm_tplg3ifbt1a32e1_s[];
extern const gm_spec *const gm_tplg500ggupdf4i_s[];
extern const gm_tmpl_piece gm_tplggcatbkggmn2_p[];
extern const gm_tmpl gm_tplghi16hba81gr;
extern const uint32_t gm_tplghi16hba81gr_i[];
extern const gm_tmpl_piece gm_tplghi16hba81gr_p[];
extern const gm_spec *const gm_tplghi16hba81gr_s[];
extern const uint32_t gm_tplgism5t1v67p6_i[];
extern const uint32_t gm_tplgjeridtvkml4_i[];
extern const uint32_t gm_tplgl79fs8kklen_i[];
extern const gm_tmpl gm_tplgp15dehsbtkk;
extern const uint32_t gm_tplgp15dehsbtkk_i[];
extern const gm_tmpl_piece gm_tplgp15dehsbtkk_p[];
extern const gm_spec *const gm_tplgp15dehsbtkk_s[];
extern const gm_tmpl_piece gm_tplgp3qh1m090bm_p[];
extern const gm_spec *const gm_tplgq3upqu7s79r_s[];
extern const uint32_t gm_tplgt8u7d5aku5b_i[];
extern const gm_tmpl gm_tplh1blptpemhkl;
extern const uint32_t gm_tplh1blptpemhkl_i[];
extern const gm_tmpl_piece gm_tplh1blptpemhkl_p[];
extern const gm_spec *const gm_tplh1blptpemhkl_s[];
extern const uint32_t gm_tplh5ff53j0j0d2_i[];
extern const gm_tmpl gm_tplhen8m8tu01kk;
extern const uint32_t gm_tplhen8m8tu01kk_i[];
extern const gm_tmpl_piece gm_tplhen8m8tu01kk_p[];
extern const gm_spec *const gm_tplhen8m8tu01kk_s[];
extern const uint32_t gm_tplhiqpct20jd67_i[];
extern const gm_tmpl_piece gm_tplhkjcpttbsqt0_p[];
extern const gm_tmpl gm_tplhrko1gs9n6tk;
extern const uint32_t gm_tplhrko1gs9n6tk_i[];
extern const gm_tmpl_piece gm_tplhrko1gs9n6tk_p[];
extern const gm_spec *const gm_tplhrko1gs9n6tk_s[];
extern const gm_spec *const gm_tpli3v24a6u14sa_s[];
extern const gm_tmpl gm_tpli7ohq5o5sj5n;
extern const uint32_t gm_tpli7ohq5o5sj5n_i[];
extern const gm_tmpl_piece gm_tpli7ohq5o5sj5n_p[];
extern const gm_spec *const gm_tpli7ohq5o5sj5n_s[];
extern const gm_tmpl gm_tpli9uflldtkfq3;
extern const uint32_t gm_tpli9uflldtkfq3_i[];
extern const gm_tmpl_piece gm_tpli9uflldtkfq3_p[];
extern const gm_spec *const gm_tpli9uflldtkfq3_s[];
extern const gm_spec *const gm_tplibfhnj0illo7_s[];
extern const gm_tmpl_piece gm_tplibgopbmor3p6_p[];
extern const uint32_t gm_tpligbj3kvvunpr_i[];
extern const gm_tmpl gm_tpligtpf80arr7h;
extern const uint32_t gm_tpligtpf80arr7h_i[];
extern const gm_tmpl_piece gm_tpligtpf80arr7h_p[];
extern const gm_spec *const gm_tpligtpf80arr7h_s[];
extern const gm_tmpl gm_tplijibrlknrr0j;
extern const uint32_t gm_tplijibrlknrr0j_i[];
extern const gm_tmpl_piece gm_tplijibrlknrr0j_p[];
extern const gm_spec *const gm_tplijibrlknrr0j_s[];
extern const uint32_t gm_tplims1qainr636_i[];
extern const gm_tmpl_piece gm_tplin004j5gtkru_p[];
extern const gm_tmpl_piece gm_tplio7v2odie8p1_p[];
extern const gm_spec *const gm_tplio7v2odie8p1_s[];
extern const uint32_t gm_tplip1v4sd2477s_i[];
extern const gm_spec *const gm_tplivckk5jvcluc_s[];
extern const gm_tmpl gm_tplivip1s1762gt;
extern const uint32_t gm_tplivip1s1762gt_i[];
extern const gm_tmpl_piece gm_tplivip1s1762gt_p[];
extern const gm_spec *const gm_tplivip1s1762gt_s[];
extern const gm_spec *const gm_tplj2nt5hsf4u73_s[];
extern const gm_tmpl_piece gm_tplj4gqj9p0fguq_p[];
extern const gm_spec *const gm_tplj6imuu9dopvn_s[];
extern const gm_tmpl_piece gm_tplj78420ela1cg_p[];
extern const gm_spec *const gm_tplj7a4hf9tva3v_s[];
extern const gm_spec *const gm_tplj972sj9sp61d_s[];
extern const gm_spec *const gm_tpljdh6phei25vf_s[];
extern const gm_tmpl_piece gm_tpljgv7fkm5ld25_p[];
extern const uint32_t gm_tpljkecvj49gp71_i[];
extern const gm_tmpl_piece gm_tpljq1be78i0ptp_p[];
extern const uint32_t gm_tpljv32on6ui3vn_i[];
extern const gm_tmpl_piece gm_tpljv32on6ui3vn_p[];
extern const gm_tmpl_piece gm_tplk13976gpibvh_p[];
extern const gm_tmpl gm_tplk25d3iiaamil;
extern const uint32_t gm_tplk25d3iiaamil_i[];
extern const gm_tmpl_piece gm_tplk25d3iiaamil_p[];
extern const gm_spec *const gm_tplk25d3iiaamil_s[];
extern const uint32_t gm_tplk27ieg1b14ol_i[];
extern const uint32_t gm_tplk6g6bjh5rkat_i[];
extern const gm_tmpl_piece gm_tplk7hjamhd5jcl_p[];
extern const gm_tmpl_piece gm_tplk83cf3bpuiqb_p[];
extern const gm_tmpl_piece gm_tplkik9smkp1eb6_p[];
extern const gm_tmpl gm_tplkj8cf8psv8p6;
extern const uint32_t gm_tplkj8cf8psv8p6_i[];
extern const gm_tmpl_piece gm_tplkj8cf8psv8p6_p[];
extern const gm_spec *const gm_tplkj8cf8psv8p6_s[];
extern const gm_tmpl_piece gm_tplkjc5mpc5i80h_p[];
extern const uint32_t gm_tplknfbq49i74sp_i[];
extern const gm_spec *const gm_tplkodpfg3ik1kh_s[];
extern const gm_tmpl gm_tplkp06s34ois53;
extern const uint32_t gm_tplkp06s34ois53_i[];
extern const gm_tmpl_piece gm_tplkp06s34ois53_p[];
extern const gm_spec *const gm_tplkp06s34ois53_s[];
extern const gm_spec *const gm_tplksrq9kcn68vt_s[];
extern const uint32_t gm_tplkucr7969socr_i[];
extern const gm_tmpl gm_tplkun0h8smcrq9;
extern const uint32_t gm_tplkun0h8smcrq9_i[];
extern const gm_tmpl_piece gm_tplkun0h8smcrq9_p[];
extern const gm_spec *const gm_tplkun0h8smcrq9_s[];
extern const gm_tmpl gm_tpll1ot8bjie65s;
extern const uint32_t gm_tpll1ot8bjie65s_i[];
extern const gm_tmpl_piece gm_tpll1ot8bjie65s_p[];
extern const gm_spec *const gm_tpll1ot8bjie65s_s[];
extern const gm_spec *const gm_tpll4f2ihchoe9c_s[];
extern const gm_tmpl gm_tpllak8uc09ui7v;
extern const uint32_t gm_tpllak8uc09ui7v_i[];
extern const gm_tmpl_piece gm_tpllak8uc09ui7v_p[];
extern const gm_spec *const gm_tpllak8uc09ui7v_s[];
extern const gm_tmpl gm_tpllcp48j1qim70;
extern const uint32_t gm_tpllcp48j1qim70_i[];
extern const gm_tmpl_piece gm_tpllcp48j1qim70_p[];
extern const gm_spec *const gm_tpllcp48j1qim70_s[];
extern const gm_tmpl gm_tpllcvau9l0fqcd;
extern const uint32_t gm_tpllcvau9l0fqcd_i[];
extern const gm_tmpl_piece gm_tpllcvau9l0fqcd_p[];
extern const gm_spec *const gm_tpllcvau9l0fqcd_s[];
extern const gm_spec *const gm_tplld08lp5kl07i_s[];
extern const uint32_t gm_tplld9eu0oenqb7_i[];
extern const gm_spec *const gm_tplld9eu0oenqb7_s[];
extern const gm_tmpl_piece gm_tpllir4kiseuk2b_p[];
extern const gm_spec *const gm_tpllnpq25q3a5a4_s[];
extern const gm_spec *const gm_tpllovel7a2ips1_s[];
extern const gm_tmpl gm_tpllp752obmvr55;
extern const uint32_t gm_tpllp752obmvr55_i[];
extern const gm_tmpl_piece gm_tpllp752obmvr55_p[];
extern const gm_spec *const gm_tpllp752obmvr55_s[];
extern const gm_tmpl gm_tplls1lesito750;
extern const uint32_t gm_tplls1lesito750_i[];
extern const gm_tmpl_piece gm_tplls1lesito750_p[];
extern const gm_spec *const gm_tplls1lesito750_s[];
extern const gm_spec *const gm_tpllsaus03ne167_s[];
extern const gm_tmpl_piece gm_tpllsre3itruefn_p[];
extern const gm_spec *const gm_tpllv3q9mfan4rg_s[];
extern const gm_tmpl_piece gm_tplm3f7baid20me_p[];
extern const gm_tmpl gm_tplm3qlt5n96af0;
extern const uint32_t gm_tplm3qlt5n96af0_i[];
extern const gm_tmpl_piece gm_tplm3qlt5n96af0_p[];
extern const gm_spec *const gm_tplm3qlt5n96af0_s[];
extern const gm_tmpl_piece gm_tplm6qp3tlbq28j_p[];
extern const gm_spec *const gm_tplm8s7gnb70j1j_s[];
extern const gm_spec *const gm_tplm9hj31tk9ip4_s[];
extern const gm_tmpl_piece gm_tplmaqqdmni75bf_p[];
extern const uint32_t gm_tplme3pul5hs3bf_i[];
extern const gm_tmpl_piece gm_tplmekan10d5buf_p[];
extern const gm_spec *const gm_tplmghq7vvcvnvi_s[];
extern const gm_tmpl gm_tplmiqlr2bu75gm;
extern const uint32_t gm_tplmiqlr2bu75gm_i[];
extern const gm_tmpl_piece gm_tplmiqlr2bu75gm_p[];
extern const gm_spec *const gm_tplmiqlr2bu75gm_s[];
extern const gm_tmpl_piece gm_tplmjku4vg3eupf_p[];
extern const gm_tmpl_piece gm_tplmjmservvuqtp_p[];
extern const gm_spec *const gm_tplmkb4jrr2jqel_s[];
extern const uint32_t gm_tplmo458jpb0n1e_i[];
extern const uint32_t gm_tplmoo8v17lb0ql_i[];
extern const gm_tmpl gm_tpln2i759e19cgt;
extern const uint32_t gm_tpln2i759e19cgt_i[];
extern const gm_tmpl_piece gm_tpln2i759e19cgt_p[];
extern const gm_spec *const gm_tpln2i759e19cgt_s[];
extern const gm_tmpl gm_tpln4i290aeoim9;
extern const uint32_t gm_tpln4i290aeoim9_i[];
extern const gm_tmpl_piece gm_tpln4i290aeoim9_p[];
extern const gm_spec *const gm_tpln4i290aeoim9_s[];
extern const gm_tmpl_piece gm_tpln4t9g9do4qg7_p[];
extern const gm_tmpl gm_tpln65gnioheijm;
extern const uint32_t gm_tpln65gnioheijm_i[];
extern const gm_tmpl_piece gm_tpln65gnioheijm_p[];
extern const gm_spec *const gm_tpln65gnioheijm_s[];
extern const uint32_t gm_tpln8mihgcsij40_i[];
extern const gm_tmpl_piece gm_tpln8mihgcsij40_p[];
extern const uint32_t gm_tplna8gfl3ukgs3_i[];
extern const gm_tmpl gm_tplnevoink17fov;
extern const uint32_t gm_tplnevoink17fov_i[];
extern const gm_tmpl_piece gm_tplnevoink17fov_p[];
extern const gm_spec *const gm_tplnevoink17fov_s[];
extern const gm_tmpl_piece gm_tplng1u8s5kl5ir_p[];
extern const gm_spec *const gm_tplng1u8s5kl5ir_s[];
extern const gm_tmpl_piece gm_tplnm0o65j76qg0_p[];
extern const uint32_t gm_tplnm352386bmep_i[];
extern const gm_tmpl gm_tplnodn319tb55r;
extern const uint32_t gm_tplnodn319tb55r_i[];
extern const gm_tmpl_piece gm_tplnodn319tb55r_p[];
extern const gm_spec *const gm_tplnodn319tb55r_s[];
extern const gm_spec *const gm_tplnud722tu4rrk_s[];
extern const uint32_t gm_tplo12j44trajdm_i[];
extern const gm_spec *const gm_tplo4cs7gli8ts2_s[];
extern const gm_tmpl_piece gm_tplo4mr4l55q97r_p[];
extern const gm_spec *const gm_tplo5614u5649ka_s[];
extern const gm_spec *const gm_tplo867268s51oj_s[];
extern const gm_tmpl gm_tplo8vr0jo68llo;
extern const uint32_t gm_tplo8vr0jo68llo_i[];
extern const gm_tmpl_piece gm_tplo8vr0jo68llo_p[];
extern const gm_spec *const gm_tplo8vr0jo68llo_s[];
extern const gm_tmpl gm_tplob6co4svk3k5;
extern const uint32_t gm_tplob6co4svk3k5_i[];
extern const gm_tmpl_piece gm_tplob6co4svk3k5_p[];
extern const gm_spec *const gm_tplob6co4svk3k5_s[];
extern const uint32_t gm_tploblcdf1g38ca_i[];
extern const gm_tmpl gm_tplodl7i2apj0bs;
extern const uint32_t gm_tplodl7i2apj0bs_i[];
extern const gm_tmpl_piece gm_tplodl7i2apj0bs_p[];
extern const gm_spec *const gm_tplodl7i2apj0bs_s[];
extern const uint32_t gm_tplogq0vb9du4ii_i[];
extern const gm_tmpl_piece gm_tplogq0vb9du4ii_p[];
extern const gm_tmpl_piece gm_tploh2305pu3svo_p[];
extern const gm_tmpl_piece gm_tploj0vopi0n43l_p[];
extern const gm_spec *const gm_tploj0vopi0n43l_s[];
extern const uint32_t gm_tploj5r79v5kirr_i[];
extern const gm_spec *const gm_tploj5r79v5kirr_s[];
extern const gm_tmpl_piece gm_tplomrgvu31uqof_p[];
extern const uint32_t gm_tplosjvq6nju6rc_i[];
extern const gm_tmpl gm_tplotas2madniqp;
extern const uint32_t gm_tplotas2madniqp_i[];
extern const gm_tmpl_piece gm_tplotas2madniqp_p[];
extern const gm_spec *const gm_tplotas2madniqp_s[];
extern const gm_tmpl_piece gm_tploui9f30uh460_p[];
extern const gm_tmpl_piece gm_tplp4f8nloo6fdo_p[];
extern const uint32_t gm_tplp7faum694g3u_i[];
extern const gm_spec *const gm_tplp7gdl2lerimh_s[];
extern const gm_tmpl gm_tplpd6kdal33co2;
extern const uint32_t gm_tplpd6kdal33co2_i[];
extern const gm_tmpl_piece gm_tplpd6kdal33co2_p[];
extern const gm_spec *const gm_tplpd6kdal33co2_s[];
extern const uint32_t gm_tplpqhejpqnulcm_i[];
extern const gm_tmpl_piece gm_tplpqhejpqnulcm_p[];
extern const gm_tmpl_piece gm_tplpsmnsqmt7n3e_p[];
extern const gm_spec *const gm_tplptgaia3s19c9_s[];
extern const gm_tmpl_piece gm_tplptk7v9d6qojh_p[];
extern const gm_tmpl gm_tplptu6sgqf50o0;
extern const uint32_t gm_tplptu6sgqf50o0_i[];
extern const gm_tmpl_piece gm_tplptu6sgqf50o0_p[];
extern const gm_spec *const gm_tplptu6sgqf50o0_s[];
extern const gm_spec *const gm_tplpv6nab1nov2l_s[];
extern const gm_tmpl_piece gm_tplpvfl1p0n8is8_p[];
extern const gm_tmpl_piece gm_tplq74l4dgr0l9g_p[];
extern const uint32_t gm_tplqbbl1qig0e0s_i[];
extern const gm_tmpl gm_tplqbbmojia0cmf;
extern const gm_tmpl_piece gm_tplqbbmojia0cmf_p[];
extern const gm_tmpl gm_tplqbglsd979t5c;
extern const uint32_t gm_tplqbglsd979t5c_i[];
extern const gm_tmpl_piece gm_tplqbglsd979t5c_p[];
extern const gm_spec *const gm_tplqbglsd979t5c_s[];
extern const uint32_t gm_tplqc1ev2l2fda9_i[];
extern const gm_tmpl gm_tplqfgvjo31nom3;
extern const uint32_t gm_tplqfgvjo31nom3_i[];
extern const gm_tmpl_piece gm_tplqfgvjo31nom3_p[];
extern const gm_spec *const gm_tplqfgvjo31nom3_s[];
extern const uint32_t gm_tplqhu7t9iljb1t_i[];
extern const uint32_t gm_tplql7vfevun5ae_i[];
extern const uint32_t gm_tplqm5shuhajt4a_i[];
extern const gm_spec *const gm_tplqm5shuhajt4a_s[];
extern const gm_spec *const gm_tplqn6e8rqmlvpo_s[];
extern const gm_spec *const gm_tplqpg9790rv4ki_s[];
extern const gm_spec *const gm_tplr2tuff1mvdst_s[];
extern const uint32_t gm_tplr3ni3ai315ji_i[];
extern const gm_tmpl_piece gm_tplr7712ujh33fl_p[];
extern const uint32_t gm_tplr7pcqj5621b9_i[];
extern const gm_spec *const gm_tplr7pcqj5621b9_s[];
extern const gm_tmpl_piece gm_tplr8vkqgn7raqo_p[];
extern const gm_tmpl gm_tplr9mrmvfj4528;
extern const uint32_t gm_tplr9mrmvfj4528_i[];
extern const gm_tmpl_piece gm_tplr9mrmvfj4528_p[];
extern const gm_spec *const gm_tplr9mrmvfj4528_s[];
extern const gm_spec *const gm_tplrd5hip684e7e_s[];
extern const gm_tmpl_piece gm_tplre5cfbnr7ltv_p[];
extern const uint32_t gm_tplrkvt0j8tqlqg_i[];
extern const gm_tmpl_piece gm_tplrodjuj3fgmsa_p[];
extern const gm_spec *const gm_tplrof4shrhlnf8_s[];
extern const gm_tmpl gm_tplrqgv2l0evbdg;
extern const uint32_t gm_tplrqgv2l0evbdg_i[];
extern const gm_tmpl_piece gm_tplrqgv2l0evbdg_p[];
extern const gm_spec *const gm_tplrqgv2l0evbdg_s[];
extern const gm_tmpl_piece gm_tpls2ou5a11vcn7_p[];
extern const gm_tmpl_piece gm_tpls4bcac20qnq6_p[];
extern const uint32_t gm_tpls4d1lh9jd2u8_i[];
extern const gm_spec *const gm_tpls5kpq7mskrde_s[];
extern const gm_tmpl_piece gm_tpls6vb484t8cfc_p[];
extern const gm_spec *const gm_tpls7s61lfbg9l6_s[];
extern const gm_spec *const gm_tplsa4s74k8e6ci_s[];
extern const gm_tmpl gm_tplscq8dbtjfamh;
extern const uint32_t gm_tplscq8dbtjfamh_i[];
extern const gm_tmpl_piece gm_tplscq8dbtjfamh_p[];
extern const gm_spec *const gm_tplscq8dbtjfamh_s[];
extern const gm_tmpl_piece gm_tplsg1jhb9vp948_p[];
extern const uint32_t gm_tplsh3mbm625h0g_i[];
extern const gm_spec *const gm_tplsiiifs0eedvi_s[];
extern const gm_tmpl gm_tplsjtf9pg2ergu;
extern const uint32_t gm_tplsjtf9pg2ergu_i[];
extern const gm_tmpl_piece gm_tplsjtf9pg2ergu_p[];
extern const gm_spec *const gm_tplsjtf9pg2ergu_s[];
extern const gm_tmpl gm_tplso01g2paq6o6;
extern const uint32_t gm_tplso01g2paq6o6_i[];
extern const gm_tmpl_piece gm_tplso01g2paq6o6_p[];
extern const gm_spec *const gm_tplso01g2paq6o6_s[];
extern const gm_spec *const gm_tplso4fjesdi372_s[];
extern const gm_spec *const gm_tplsogg2sp0m9p9_s[];
extern const gm_tmpl gm_tplt0hjq4nb3jer;
extern const uint32_t gm_tplt0hjq4nb3jer_i[];
extern const gm_tmpl_piece gm_tplt0hjq4nb3jer_p[];
extern const gm_spec *const gm_tplt0hjq4nb3jer_s[];
extern const gm_tmpl_piece gm_tplt0qsjg06adbb_p[];
extern const gm_tmpl_piece gm_tplt6nq68u4co32_p[];
extern const uint32_t gm_tpltd7vnsp01mvt_i[];
extern const gm_tmpl gm_tpltdoamgi2k9f9;
extern const uint32_t gm_tpltdoamgi2k9f9_i[];
extern const gm_tmpl_piece gm_tpltdoamgi2k9f9_p[];
extern const gm_spec *const gm_tpltdoamgi2k9f9_s[];
extern const gm_tmpl_piece gm_tplteu0ijrjjsvv_p[];
extern const gm_tmpl gm_tpltrvmjk8jhmh7;
extern const uint32_t gm_tpltrvmjk8jhmh7_i[];
extern const gm_tmpl_piece gm_tpltrvmjk8jhmh7_p[];
extern const gm_spec *const gm_tpltrvmjk8jhmh7_s[];
extern const gm_tmpl_piece gm_tpltur94fmrrcmk_p[];
extern const gm_spec *const gm_tplu65cpe6nhlbb_s[];
extern const uint32_t gm_tplu76evoc63de2_i[];
extern const gm_tmpl_piece gm_tplu76evoc63de2_p[];
extern const gm_tmpl_piece gm_tplu9uohqqitldd_p[];
extern const gm_tmpl gm_tpludqpd0ngjfu6;
extern const uint32_t gm_tpludqpd0ngjfu6_i[];
extern const gm_tmpl_piece gm_tpludqpd0ngjfu6_p[];
extern const gm_spec *const gm_tpludqpd0ngjfu6_s[];
extern const uint32_t gm_tpluh0hed6nj59d_i[];
extern const gm_tmpl_piece gm_tpluhfd3hvppdji_p[];
extern const uint32_t gm_tplukjghf5ng2te_i[];
extern const gm_tmpl gm_tplulq1qidn3nog;
extern const uint32_t gm_tplulq1qidn3nog_i[];
extern const gm_tmpl_piece gm_tplulq1qidn3nog_p[];
extern const gm_spec *const gm_tplulq1qidn3nog_s[];
extern const gm_tmpl_piece gm_tpluoji9akrnj9c_p[];
extern const gm_tmpl_piece gm_tplut4ib9ov72oe_p[];
extern const gm_tmpl gm_tpluvp47iqcj4uv;
extern const uint32_t gm_tpluvp47iqcj4uv_i[];
extern const gm_tmpl_piece gm_tpluvp47iqcj4uv_p[];
extern const gm_spec *const gm_tpluvp47iqcj4uv_s[];
extern const gm_tmpl_piece gm_tplv0flrfdqimb1_p[];
extern const gm_spec *const gm_tplvbcfr0k6nmhv_s[];
extern const gm_tmpl_piece gm_tplvcccam8nulr1_p[];
extern const gm_spec *const gm_tplvcuursopmobk_s[];
extern const uint32_t gm_tplvk1pmcp9tr4p_i[];
extern const uint32_t gm_tplvkvnlb2orh55_i[];
extern const uint32_t gm_tplvm7e7lfhcheu_i[];
extern const gm_tmpl_piece gm_tplvonunpuh0kn8_p[];
extern const gm_spec *const gm_tplvqa6aqf5q6ij_s[];
extern const gm_tmpl_piece gm_tplvqfop4uu1u5q_p[];
extern const gm_tmpl_piece gm_tplvrmulr6rg858_p[];
const char gm_src0ih77f43mhf2[] = "compiler/types/types.gm";
const char gm_src0m6lnv6m2d3p[] = "compiler/linting/linting.gm";
const char gm_src2pgaip05o19o[] = "compiler/load/when.gm";
const char gm_src3dhhc4laniae[] = "compiler/repl/repl.gm";
const char gm_src4tam33qo20j7[] = "compiler/check/stack_size.gm";
const char gm_src6unagr666v56[] = "compiler/interp/bc.gm";
const char gm_src7u0d314efpdm[] = "compiler/pkg/link.gm";
const char gm_src9vg8k4mbj38f[] = "compiler/eval/eval.gm";
const char gm_srca29n3fkrpbdt[] = "compiler/cgen/borrow.gm";
const char gm_srccfhjr4r9mdef[] = "compiler/cgen/units.gm";
const char gm_srcihhtlsvd7f6i[] = "compiler/cgen/fiber.gm";
const char gm_srclt70oog8j9io[] = "compiler/pkg/edit.gm";
const char gm_srcr9oeab0h5mup[] = "lib/std/fiber/fiber.gm";
const char gm_srct4pcukr5kqhq[] = "compiler/check/perform.gm";
const char gm_srcu4ee17t4impf[] = "compiler/pkg/lock.gm";
const char gm_srcue4ionbe9lgh[] = "compiler/eval/ops.gm";
const gm_spec *const gm_tpl07e4gjk0ub6d_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpl0epjr4745p10_s[] = {NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpl0rlavm9oqi24_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl16elqfq3d1nr_s[] = {NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpl1f1njss89nj4_s[] = {NULL, NULL};
const gm_spec *const gm_tpl1nqa0en23gvp_s[] = {NULL};
const gm_spec *const gm_tpl1o2hk14gjjj0_s[] = {NULL};
const gm_spec *const gm_tpl461nvut9qmgp_s[] = {NULL};
const gm_spec *const gm_tpl4cbbis3gd1k3_s[] = {NULL, NULL};
const gm_spec *const gm_tpl4g1h0tr3apfi_s[] = {NULL};
const gm_spec *const gm_tpl4nfbp2ikscbk_s[] = {NULL};
const gm_spec *const gm_tpl4npct6bth1c7_s[] = {&(gm_spec){32, 0, 0, 0, 1, 2, 4u, -1}};
const gm_spec *const gm_tpl4slc3so8u8pa_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl5a7qf3e2brk4_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl5cbugllfe764_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl5d3ksa1ntdkn_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl5idbni8n88m7_s[] = {NULL, NULL};
const gm_spec *const gm_tpl5m2t8us2v800_s[] = {NULL, NULL};
const gm_spec *const gm_tpl5uko75p49g56_s[] = {NULL, NULL};
const gm_spec *const gm_tpl5vbp9fc0mpc1_s[] = {NULL};
const gm_spec *const gm_tpl6gp425a13qgr_s[] = {NULL, NULL};
const gm_spec *const gm_tpl6nq0lpkkblpc_s[] = {NULL};
const gm_spec *const gm_tpl75d11no80fsi_s[] = {NULL, NULL};
const gm_spec *const gm_tpl7r51aeh92b80_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl8ssnfo686ogd_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl8tri2h8bpkpg_s[] = {NULL};
const gm_spec *const gm_tpl941gb7vu8kgv_s[] = {NULL, NULL};
const gm_spec *const gm_tpl9kt3nn7p3a92_s[] = {NULL};
const gm_spec *const gm_tpl9qchhbq9qlbm_s[] = {NULL, NULL};
const gm_spec *const gm_tpl9sh9d989om94_s[] = {NULL};
const gm_spec *const gm_tpla8nvlakostko_s[] = {NULL};
const gm_spec *const gm_tplalgvrfo85soq_s[] = {NULL};
const gm_spec *const gm_tplap4dl087fd4l_s[] = {NULL};
const gm_spec *const gm_tplbnt69qpq91i8_s[] = {NULL};
const gm_spec *const gm_tplc1emmpcosf7b_s[] = {NULL, NULL};
const gm_spec *const gm_tplc2ekf83ggtj3_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplca6pmroagoou_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplcic56vj3l629_s[] = {NULL};
const gm_spec *const gm_tpld4u3te7g9c5v_s[] = {NULL};
const gm_spec *const gm_tpldn8tr7ddtik6_s[] = {NULL, NULL};
const gm_spec *const gm_tpldqjt9kdk4ntt_s[] = {NULL};
const gm_spec *const gm_tple4r0jrrru5m7_s[] = {NULL};
const gm_spec *const gm_tple4r1aumd134l_s[] = {NULL};
const gm_spec *const gm_tple59d4hgjgp3n_s[] = {NULL, NULL};
const gm_spec *const gm_tplehc5f52ftol1_s[] = {NULL, NULL};
const gm_spec *const gm_tplengfdkso35ua_s[] = {NULL, NULL};
const gm_spec *const gm_tplfo19v5itil8t_s[] = {NULL, NULL};
const gm_spec *const gm_tplfsstr25ojml5_s[] = {NULL};
const gm_spec *const gm_tplfv85d3oogq60_s[] = {NULL};
const gm_spec *const gm_tplg0rms0t7302m_s[] = {NULL};
const gm_spec *const gm_tplg500ggupdf4i_s[] = {NULL};
const gm_spec *const gm_tplgq3upqu7s79r_s[] = {NULL, NULL};
const gm_spec *const gm_tpli3v24a6u14sa_s[] = {NULL};
const gm_spec *const gm_tplibfhnj0illo7_s[] = {NULL, NULL};
const gm_spec *const gm_tplio7v2odie8p1_s[] = {NULL, NULL};
const gm_spec *const gm_tplivckk5jvcluc_s[] = {NULL};
const gm_spec *const gm_tplj2nt5hsf4u73_s[] = {NULL};
const gm_spec *const gm_tplj6imuu9dopvn_s[] = {NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplj7a4hf9tva3v_s[] = {NULL};
const gm_spec *const gm_tplj972sj9sp61d_s[] = {NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpljdh6phei25vf_s[] = {NULL};
const gm_spec *const gm_tplkodpfg3ik1kh_s[] = {NULL};
const gm_spec *const gm_tplksrq9kcn68vt_s[] = {NULL};
const gm_spec *const gm_tpll4f2ihchoe9c_s[] = {NULL, NULL};
const gm_spec *const gm_tplld08lp5kl07i_s[] = {NULL};
const gm_spec *const gm_tplld9eu0oenqb7_s[] = {NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpllnpq25q3a5a4_s[] = {NULL, NULL};
const gm_spec *const gm_tpllovel7a2ips1_s[] = {NULL, NULL};
const gm_spec *const gm_tpllsaus03ne167_s[] = {NULL};
const gm_spec *const gm_tpllv3q9mfan4rg_s[] = {NULL};
const gm_spec *const gm_tplm8s7gnb70j1j_s[] = {NULL};
const gm_spec *const gm_tplm9hj31tk9ip4_s[] = {NULL};
const gm_spec *const gm_tplmghq7vvcvnvi_s[] = {NULL};
const gm_spec *const gm_tplmkb4jrr2jqel_s[] = {NULL};
const gm_spec *const gm_tplng1u8s5kl5ir_s[] = {NULL};
const gm_spec *const gm_tplnud722tu4rrk_s[] = {NULL};
const gm_spec *const gm_tplo4cs7gli8ts2_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplo5614u5649ka_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplo867268s51oj_s[] = {NULL};
const gm_spec *const gm_tploj0vopi0n43l_s[] = {NULL};
const gm_spec *const gm_tploj5r79v5kirr_s[] = {NULL};
const gm_spec *const gm_tplp7gdl2lerimh_s[] = {NULL};
const gm_spec *const gm_tplptgaia3s19c9_s[] = {NULL};
const gm_spec *const gm_tplpv6nab1nov2l_s[] = {NULL};
const gm_spec *const gm_tplqm5shuhajt4a_s[] = {NULL};
const gm_spec *const gm_tplqn6e8rqmlvpo_s[] = {NULL, NULL};
const gm_spec *const gm_tplqpg9790rv4ki_s[] = {NULL};
const gm_spec *const gm_tplr2tuff1mvdst_s[] = {NULL};
const gm_spec *const gm_tplr7pcqj5621b9_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplrd5hip684e7e_s[] = {NULL};
const gm_spec *const gm_tplrof4shrhlnf8_s[] = {NULL};
const gm_spec *const gm_tplrqgv2l0evbdg_s[] = {NULL, NULL};
const gm_spec *const gm_tpls5kpq7mskrde_s[] = {NULL};
const gm_spec *const gm_tpls7s61lfbg9l6_s[] = {NULL};
const gm_spec *const gm_tplsa4s74k8e6ci_s[] = {NULL};
const gm_spec *const gm_tplsiiifs0eedvi_s[] = {NULL, NULL};
const gm_spec *const gm_tplso4fjesdi372_s[] = {NULL};
const gm_spec *const gm_tplsogg2sp0m9p9_s[] = {NULL, NULL};
const gm_spec *const gm_tplu65cpe6nhlbb_s[] = {NULL};
const gm_spec *const gm_tplvbcfr0k6nmhv_s[] = {NULL, NULL};
const gm_spec *const gm_tplvcuursopmobk_s[] = {NULL};
const gm_spec *const gm_tplvqa6aqf5q6ij_s[] = {NULL};
const gm_tmpl gm_tpl0pva6pvhuq6i = {3u, gm_tpl0pva6pvhuq6i_p, gm_tpl0pva6pvhuq6i_i, gm_tpl0pva6pvhuq6i_s};
const gm_tmpl gm_tpl0scna75sco5f = {2u, gm_tpl0scna75sco5f_p, gm_tpl0scna75sco5f_i, gm_tpl0scna75sco5f_s};
const gm_tmpl gm_tpl1dleoebfmvhp = {2u, gm_tpl1dleoebfmvhp_p, gm_tpl1dleoebfmvhp_i, gm_tpl1dleoebfmvhp_s};
const gm_tmpl gm_tpl1g1lh0kp24oa = {2u, gm_tpl1g1lh0kp24oa_p, gm_tpl1g1lh0kp24oa_i, gm_tpl1g1lh0kp24oa_s};
const gm_tmpl gm_tpl24fci6dm83eo = {3u, gm_tpl24fci6dm83eo_p, gm_tpl24fci6dm83eo_i, gm_tpl24fci6dm83eo_s};
const gm_tmpl gm_tpl32o7ct8vibbi = {1u, gm_tpl32o7ct8vibbi_p, gm_tpl32o7ct8vibbi_i, gm_tpl32o7ct8vibbi_s};
const gm_tmpl gm_tpl33jf6q62dk07 = {1u, gm_tpl33jf6q62dk07_p, gm_tpl33jf6q62dk07_i, gm_tpl33jf6q62dk07_s};
const gm_tmpl gm_tpl344kemfor61g = {1u, gm_tpl344kemfor61g_p, gm_tpl344kemfor61g_i, gm_tpl344kemfor61g_s};
const gm_tmpl gm_tpl34tavcs9635o = {2u, gm_tpl34tavcs9635o_p, gm_tpl34tavcs9635o_i, gm_tpl34tavcs9635o_s};
const gm_tmpl gm_tpl3qt6ss8l9cgf = {1u, gm_tpl3qt6ss8l9cgf_p, gm_tpl3qt6ss8l9cgf_i, gm_tpl3qt6ss8l9cgf_s};
const gm_tmpl gm_tpl3ttr7njhqa54 = {1u, gm_tpl3ttr7njhqa54_p, gm_tpl3ttr7njhqa54_i, gm_tpl3ttr7njhqa54_s};
const gm_tmpl gm_tpl44b660pgg1c8 = {2u, gm_tpl44b660pgg1c8_p, gm_tpl44b660pgg1c8_i, gm_tpl44b660pgg1c8_s};
const gm_tmpl gm_tpl4mli0f9rjihf = {4u, gm_tpl4mli0f9rjihf_p, gm_tpl4mli0f9rjihf_i, gm_tpl4mli0f9rjihf_s};
const gm_tmpl gm_tpl4phv0vr9as7f = {2u, gm_tpl4phv0vr9as7f_p, gm_tpl4phv0vr9as7f_i, gm_tpl4phv0vr9as7f_s};
const gm_tmpl gm_tpl4qnmv5o1ceod = {1u, gm_tpl4qnmv5o1ceod_p, gm_tpl4qnmv5o1ceod_i, gm_tpl4qnmv5o1ceod_s};
const gm_tmpl gm_tpl5677p7i476ao = {3u, gm_tpl5677p7i476ao_p, gm_tpl5677p7i476ao_i, gm_tpl5677p7i476ao_s};
const gm_tmpl gm_tpl5g9nntg83p8b = {2u, gm_tpl5g9nntg83p8b_p, gm_tpl5g9nntg83p8b_i, gm_tpl5g9nntg83p8b_s};
const gm_tmpl gm_tpl5kgb1tfsut02 = {1u, gm_tpl5kgb1tfsut02_p, gm_tpl5kgb1tfsut02_i, gm_tpl5kgb1tfsut02_s};
const gm_tmpl gm_tpl5kl598jq5h12 = {7u, gm_tpl5kl598jq5h12_p, gm_tpl5kl598jq5h12_i, gm_tpl5kl598jq5h12_s};
const gm_tmpl gm_tpl5poqk71bg1bn = {2u, gm_tpl5poqk71bg1bn_p, gm_tpl5poqk71bg1bn_i, gm_tpl5poqk71bg1bn_s};
const gm_tmpl gm_tpl5q4rr3nd9637 = {1u, gm_tpl5q4rr3nd9637_p, gm_tpl5q4rr3nd9637_i, gm_tpl5q4rr3nd9637_s};
const gm_tmpl gm_tpl5r25bpo8hobt = {2u, gm_tpl5r25bpo8hobt_p, gm_tpl5r25bpo8hobt_i, gm_tpl5r25bpo8hobt_s};
const gm_tmpl gm_tpl5s6q12qjusi2 = {5u, gm_tpl5s6q12qjusi2_p, gm_tpl5s6q12qjusi2_i, gm_tpl5s6q12qjusi2_s};
const gm_tmpl gm_tpl63iuq3je4sbr = {1u, gm_tpl63iuq3je4sbr_p, gm_tpl63iuq3je4sbr_i, gm_tpl63iuq3je4sbr_s};
const gm_tmpl gm_tpl65tpfh2uj3bg = {1u, gm_tpl65tpfh2uj3bg_p, gm_tpl65tpfh2uj3bg_i, gm_tpl65tpfh2uj3bg_s};
const gm_tmpl gm_tpl6a59mrfvoo09 = {1u, gm_tpl6a59mrfvoo09_p, gm_tpl6a59mrfvoo09_i, gm_tpl6a59mrfvoo09_s};
const gm_tmpl gm_tpl6muahgpe70pr = {2u, gm_tpl6muahgpe70pr_p, gm_tpl6muahgpe70pr_i, gm_tpl6muahgpe70pr_s};
const gm_tmpl gm_tpl7r51aeh92b80 = {3u, gm_tpl7r51aeh92b80_p, gm_tpl7r51aeh92b80_i, gm_tpl7r51aeh92b80_s};
const gm_tmpl gm_tpl7t589bv3t4us = {1u, gm_tpl7t589bv3t4us_p, gm_tpl7t589bv3t4us_i, gm_tpl7t589bv3t4us_s};
const gm_tmpl gm_tpl8geisveh2vug = {10u, gm_tpl8geisveh2vug_p, gm_tpl8geisveh2vug_i, gm_tpl8geisveh2vug_s};
const gm_tmpl gm_tpl941gb7vu8kgv = {2u, gm_tpl941gb7vu8kgv_p, gm_tpl941gb7vu8kgv_i, gm_tpl941gb7vu8kgv_s};
const gm_tmpl gm_tpl9cnobsjbh0r2 = {1u, gm_tpl9cnobsjbh0r2_p, gm_tpl9cnobsjbh0r2_i, gm_tpl9cnobsjbh0r2_s};
const gm_tmpl gm_tplacjkb64s2e83 = {3u, gm_tplacjkb64s2e83_p, gm_tplacjkb64s2e83_i, gm_tplacjkb64s2e83_s};
const gm_tmpl gm_tplaka4u9f9dfvk = {1u, gm_tplaka4u9f9dfvk_p, gm_tplaka4u9f9dfvk_i, gm_tplaka4u9f9dfvk_s};
const gm_tmpl gm_tplar04pgor24ut = {1u, gm_tplar04pgor24ut_p, gm_tplar04pgor24ut_i, gm_tplar04pgor24ut_s};
const gm_tmpl gm_tplb899rpghqa93 = {2u, gm_tplb899rpghqa93_p, gm_tplb899rpghqa93_i, gm_tplb899rpghqa93_s};
const gm_tmpl gm_tplbckos3fl05qe = {2u, gm_tplbckos3fl05qe_p, gm_tplbckos3fl05qe_i, gm_tplbckos3fl05qe_s};
const gm_tmpl gm_tplbulo51k20om4 = {8u, gm_tplbulo51k20om4_p, gm_tplbulo51k20om4_i, gm_tplbulo51k20om4_s};
const gm_tmpl gm_tplc5prn5jeukj6 = {2u, gm_tplc5prn5jeukj6_p, gm_tplc5prn5jeukj6_i, gm_tplc5prn5jeukj6_s};
const gm_tmpl gm_tplck20jih4okbk = {10u, gm_tplck20jih4okbk_p, gm_tplck20jih4okbk_i, gm_tplck20jih4okbk_s};
const gm_tmpl gm_tpld9ts015fint3 = {1u, gm_tpld9ts015fint3_p, gm_tpld9ts015fint3_i, gm_tpld9ts015fint3_s};
const gm_tmpl gm_tpldrcjvgmk3ig3 = {1u, gm_tpldrcjvgmk3ig3_p, gm_tpldrcjvgmk3ig3_i, gm_tpldrcjvgmk3ig3_s};
const gm_tmpl gm_tpldvhfe50cnb69 = {4u, gm_tpldvhfe50cnb69_p, gm_tpldvhfe50cnb69_i, gm_tpldvhfe50cnb69_s};
const gm_tmpl gm_tplecdkophfbiam = {1u, gm_tplecdkophfbiam_p, gm_tplecdkophfbiam_i, gm_tplecdkophfbiam_s};
const gm_tmpl gm_tplei8v65ors6jc = {2u, gm_tplei8v65ors6jc_p, gm_tplei8v65ors6jc_i, gm_tplei8v65ors6jc_s};
const gm_tmpl gm_tplei9v853i04sp = {2u, gm_tplei9v853i04sp_p, gm_tplei9v853i04sp_i, gm_tplei9v853i04sp_s};
const gm_tmpl gm_tplepdvp8ofjal7 = {1u, gm_tplepdvp8ofjal7_p, gm_tplepdvp8ofjal7_i, gm_tplepdvp8ofjal7_s};
const gm_tmpl gm_tplf3c2iulsht2e = {1u, gm_tplf3c2iulsht2e_p, gm_tplf3c2iulsht2e_i, gm_tplf3c2iulsht2e_s};
const gm_tmpl gm_tplfft5kohcphn8 = {2u, gm_tplfft5kohcphn8_p, gm_tplfft5kohcphn8_i, gm_tplfft5kohcphn8_s};
const gm_tmpl gm_tplfjobpnodgc6e = {6u, gm_tplfjobpnodgc6e_p, gm_tplfjobpnodgc6e_i, gm_tplfjobpnodgc6e_s};
const gm_tmpl gm_tplg3ifbt1a32e1 = {3u, gm_tplg3ifbt1a32e1_p, gm_tplg3ifbt1a32e1_i, gm_tplg3ifbt1a32e1_s};
const gm_tmpl gm_tplghi16hba81gr = {1u, gm_tplghi16hba81gr_p, gm_tplghi16hba81gr_i, gm_tplghi16hba81gr_s};
const gm_tmpl gm_tplgp15dehsbtkk = {1u, gm_tplgp15dehsbtkk_p, gm_tplgp15dehsbtkk_i, gm_tplgp15dehsbtkk_s};
const gm_tmpl gm_tplh1blptpemhkl = {1u, gm_tplh1blptpemhkl_p, gm_tplh1blptpemhkl_i, gm_tplh1blptpemhkl_s};
const gm_tmpl gm_tplhen8m8tu01kk = {2u, gm_tplhen8m8tu01kk_p, gm_tplhen8m8tu01kk_i, gm_tplhen8m8tu01kk_s};
const gm_tmpl gm_tplhrko1gs9n6tk = {1u, gm_tplhrko1gs9n6tk_p, gm_tplhrko1gs9n6tk_i, gm_tplhrko1gs9n6tk_s};
const gm_tmpl gm_tpli7ohq5o5sj5n = {2u, gm_tpli7ohq5o5sj5n_p, gm_tpli7ohq5o5sj5n_i, gm_tpli7ohq5o5sj5n_s};
const gm_tmpl gm_tpli9uflldtkfq3 = {2u, gm_tpli9uflldtkfq3_p, gm_tpli9uflldtkfq3_i, gm_tpli9uflldtkfq3_s};
const gm_tmpl gm_tpligtpf80arr7h = {3u, gm_tpligtpf80arr7h_p, gm_tpligtpf80arr7h_i, gm_tpligtpf80arr7h_s};
const gm_tmpl gm_tplijibrlknrr0j = {1u, gm_tplijibrlknrr0j_p, gm_tplijibrlknrr0j_i, gm_tplijibrlknrr0j_s};
const gm_tmpl gm_tplivip1s1762gt = {1u, gm_tplivip1s1762gt_p, gm_tplivip1s1762gt_i, gm_tplivip1s1762gt_s};
const gm_tmpl gm_tplk25d3iiaamil = {3u, gm_tplk25d3iiaamil_p, gm_tplk25d3iiaamil_i, gm_tplk25d3iiaamil_s};
const gm_tmpl gm_tplkj8cf8psv8p6 = {3u, gm_tplkj8cf8psv8p6_p, gm_tplkj8cf8psv8p6_i, gm_tplkj8cf8psv8p6_s};
const gm_tmpl gm_tplkp06s34ois53 = {2u, gm_tplkp06s34ois53_p, gm_tplkp06s34ois53_i, gm_tplkp06s34ois53_s};
const gm_tmpl gm_tplkun0h8smcrq9 = {1u, gm_tplkun0h8smcrq9_p, gm_tplkun0h8smcrq9_i, gm_tplkun0h8smcrq9_s};
const gm_tmpl gm_tpll1ot8bjie65s = {1u, gm_tpll1ot8bjie65s_p, gm_tpll1ot8bjie65s_i, gm_tpll1ot8bjie65s_s};
const gm_tmpl gm_tpllak8uc09ui7v = {2u, gm_tpllak8uc09ui7v_p, gm_tpllak8uc09ui7v_i, gm_tpllak8uc09ui7v_s};
const gm_tmpl gm_tpllcp48j1qim70 = {1u, gm_tpllcp48j1qim70_p, gm_tpllcp48j1qim70_i, gm_tpllcp48j1qim70_s};
const gm_tmpl gm_tpllcvau9l0fqcd = {1u, gm_tpllcvau9l0fqcd_p, gm_tpllcvau9l0fqcd_i, gm_tpllcvau9l0fqcd_s};
const gm_tmpl gm_tpllp752obmvr55 = {1u, gm_tpllp752obmvr55_p, gm_tpllp752obmvr55_i, gm_tpllp752obmvr55_s};
const gm_tmpl gm_tplls1lesito750 = {1u, gm_tplls1lesito750_p, gm_tplls1lesito750_i, gm_tplls1lesito750_s};
const gm_tmpl gm_tplm3qlt5n96af0 = {3u, gm_tplm3qlt5n96af0_p, gm_tplm3qlt5n96af0_i, gm_tplm3qlt5n96af0_s};
const gm_tmpl gm_tplmiqlr2bu75gm = {1u, gm_tplmiqlr2bu75gm_p, gm_tplmiqlr2bu75gm_i, gm_tplmiqlr2bu75gm_s};
const gm_tmpl gm_tpln2i759e19cgt = {1u, gm_tpln2i759e19cgt_p, gm_tpln2i759e19cgt_i, gm_tpln2i759e19cgt_s};
const gm_tmpl gm_tpln4i290aeoim9 = {2u, gm_tpln4i290aeoim9_p, gm_tpln4i290aeoim9_i, gm_tpln4i290aeoim9_s};
const gm_tmpl gm_tpln65gnioheijm = {2u, gm_tpln65gnioheijm_p, gm_tpln65gnioheijm_i, gm_tpln65gnioheijm_s};
const gm_tmpl gm_tplnevoink17fov = {2u, gm_tplnevoink17fov_p, gm_tplnevoink17fov_i, gm_tplnevoink17fov_s};
const gm_tmpl gm_tplnodn319tb55r = {1u, gm_tplnodn319tb55r_p, gm_tplnodn319tb55r_i, gm_tplnodn319tb55r_s};
const gm_tmpl gm_tplo8vr0jo68llo = {1u, gm_tplo8vr0jo68llo_p, gm_tplo8vr0jo68llo_i, gm_tplo8vr0jo68llo_s};
const gm_tmpl gm_tplob6co4svk3k5 = {2u, gm_tplob6co4svk3k5_p, gm_tplob6co4svk3k5_i, gm_tplob6co4svk3k5_s};
const gm_tmpl gm_tplodl7i2apj0bs = {1u, gm_tplodl7i2apj0bs_p, gm_tplodl7i2apj0bs_i, gm_tplodl7i2apj0bs_s};
const gm_tmpl gm_tplotas2madniqp = {1u, gm_tplotas2madniqp_p, gm_tplotas2madniqp_i, gm_tplotas2madniqp_s};
const gm_tmpl gm_tplpd6kdal33co2 = {2u, gm_tplpd6kdal33co2_p, gm_tplpd6kdal33co2_i, gm_tplpd6kdal33co2_s};
const gm_tmpl gm_tplptu6sgqf50o0 = {1u, gm_tplptu6sgqf50o0_p, gm_tplptu6sgqf50o0_i, gm_tplptu6sgqf50o0_s};
const gm_tmpl gm_tplqbbmojia0cmf = {0u, gm_tplqbbmojia0cmf_p, NULL, NULL};
const gm_tmpl gm_tplqbglsd979t5c = {2u, gm_tplqbglsd979t5c_p, gm_tplqbglsd979t5c_i, gm_tplqbglsd979t5c_s};
const gm_tmpl gm_tplqfgvjo31nom3 = {3u, gm_tplqfgvjo31nom3_p, gm_tplqfgvjo31nom3_i, gm_tplqfgvjo31nom3_s};
const gm_tmpl gm_tplr9mrmvfj4528 = {1u, gm_tplr9mrmvfj4528_p, gm_tplr9mrmvfj4528_i, gm_tplr9mrmvfj4528_s};
const gm_tmpl gm_tplrqgv2l0evbdg = {2u, gm_tplrqgv2l0evbdg_p, gm_tplrqgv2l0evbdg_i, gm_tplrqgv2l0evbdg_s};
const gm_tmpl gm_tplscq8dbtjfamh = {1u, gm_tplscq8dbtjfamh_p, gm_tplscq8dbtjfamh_i, gm_tplscq8dbtjfamh_s};
const gm_tmpl gm_tplsjtf9pg2ergu = {1u, gm_tplsjtf9pg2ergu_p, gm_tplsjtf9pg2ergu_i, gm_tplsjtf9pg2ergu_s};
const gm_tmpl gm_tplso01g2paq6o6 = {2u, gm_tplso01g2paq6o6_p, gm_tplso01g2paq6o6_i, gm_tplso01g2paq6o6_s};
const gm_tmpl gm_tplt0hjq4nb3jer = {1u, gm_tplt0hjq4nb3jer_p, gm_tplt0hjq4nb3jer_i, gm_tplt0hjq4nb3jer_s};
const gm_tmpl gm_tpltdoamgi2k9f9 = {2u, gm_tpltdoamgi2k9f9_p, gm_tpltdoamgi2k9f9_i, gm_tpltdoamgi2k9f9_s};
const gm_tmpl gm_tpltrvmjk8jhmh7 = {1u, gm_tpltrvmjk8jhmh7_p, gm_tpltrvmjk8jhmh7_i, gm_tpltrvmjk8jhmh7_s};
const gm_tmpl gm_tpludqpd0ngjfu6 = {2u, gm_tpludqpd0ngjfu6_p, gm_tpludqpd0ngjfu6_i, gm_tpludqpd0ngjfu6_s};
const gm_tmpl gm_tplulq1qidn3nog = {3u, gm_tplulq1qidn3nog_p, gm_tplulq1qidn3nog_i, gm_tplulq1qidn3nog_s};
const gm_tmpl gm_tpluvp47iqcj4uv = {1u, gm_tpluvp47iqcj4uv_p, gm_tpluvp47iqcj4uv_i, gm_tpluvp47iqcj4uv_s};
const gm_tmpl_piece gm_tpl1bno78mc58dl_p[] = {{gm_sqdqshat9srld, 36}, {gm_ssb6m4k22290v, 16}};
const gm_tmpl_piece gm_tpl1lvejpvpq0qh_p[] = {{gm_s1vmtsmtbio1j, 14}, {gm_slgmqgkioa4u6, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl1ut7q7afn48u_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sfrdbcc53tuv9, 17}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl2bm4b6itkmm6_p[] = {{gm_sn5f6h6alcm9q, 11}, {gm_sbtnbgj3h592u, 18}};
const gm_tmpl_piece gm_tpl328oas8hh4jg_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s48o8olsbqnph, 11}, {gm_spod3tmkh25up, 1}};
const gm_tmpl_piece gm_tpl3dghmndqn708_p[] = {{gm_sopjc7gjg0pgu, 31}, {gm_s1hbm05s14qo4, 29}};
const gm_tmpl_piece gm_tpl3lqpj3d2egtb_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_stuutd7nfq21h, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl3v2bn2jpabsf_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s16bsnuu7i04s, 2}};
const gm_tmpl_piece gm_tpl4476c5bksft1_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_slgmqgkioa4u6, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl4o9g8c43tino_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sslm2oq18gln3, 5}, {gm_sfkrpkv21b1rq, 3}, {gm_ssnku2e1q7u3k, 12}, {gm_s3ss0hcg1h7js, 6}};
const gm_tmpl_piece gm_tpl4t11fkqh738f_p[] = {{gm_sqfsba89a30n8, 25}, {gm_sm6j3q0d4n2jp, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl59lqltdr0h0r_p[] = {{gm_s3jflb7gdk32k, 31}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl5ldtdub2029b_p[] = {{gm_shr2dul5unlgn, 15}, {gm_sk754mj3p1el6, 1}, {gm_s6jlko9gsl7im, 25}, {gm_s4e3q3n6msi6m, 5}};
const gm_tmpl_piece gm_tpl5prdhjdrg194_p[] = {{gm_su6oav9vpqcnu, 16}, {gm_scq4j3j3c00j6, 40}, {gm_sj1gcl0qo4i9p, 6}};
const gm_tmpl_piece gm_tpl5qm9nkkj1gjg_p[] = {{gm_st28rv7m1lueb, 8}, {gm_sq9lne9r5j3u2, 7}, {gm_s8l5u1ln9m2k0, 57}};
const gm_tmpl_piece gm_tpl60na9lt7qpkj_p[] = {{gm_sp0bci8i8o3nt, 2}, {gm_sek0ue1rtvb2r, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl63djfct0mcad_p[] = {{gm_sfi2ha42r1nr2, 1}, {gm_shlds5pd69pfn, 15}};
const gm_tmpl_piece gm_tpl70tkged35ahl_p[] = {{gm_sbmtphtelgust, 28}, {gm_so8oee2j1e9l0, 5}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl75h0vbo3vani_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_simmbi8o1lo4k, 45}};
const gm_tmpl_piece gm_tpl7eeir95ujdvn_p[] = {{gm_s9e7jg1nv469k, 5}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl7kgo5i3aumd2_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sk754mj3p1el6, 1}, {gm_s2053hhvf16fm, 1}};
const gm_tmpl_piece gm_tpl7kmb2inm0gau_p[] = {{gm_s2gnpj7k4l22n, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl7qfg883s3b97_p[] = {{gm_s8tq0rdmthkth, 15}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl81uel28fst2a_p[] = {{gm_s2d2ar24n4if2, 5}, {gm_sr4qbrk6tlgaa, 0}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl84co505h34ud_p[] = {{gm_su8bu9ae1ds2f, 18}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl86pqqgq5ch0o_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_srlklqc70q6ju, 2}};
const gm_tmpl_piece gm_tpl87nh31l9js4u_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sgie3las787c9, 30}};
const gm_tmpl_piece gm_tpl88ij4e6nvk4u_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_se8ggmq7h9bin, 34}};
const gm_tmpl_piece gm_tpl8bar0du85qu4_p[] = {{gm_sq9org4qoda29, 11}, {gm_sg5gdnrith2i6, 2}, {gm_s3tacjdl2ciei, 12}, {gm_snkldl7hr4htt, 3}, {gm_sn1seggk13bop, 3}};
const gm_tmpl_piece gm_tpl8f1efhudc5d4_p[] = {{gm_som0q53jqvjh3, 4}, {gm_ssv1lstjje7r5, 6}};
const gm_tmpl_piece gm_tpl8lt5kipnmd64_p[] = {{gm_smooaouqmm8dj, 1}, {gm_siddm9qkt382a, 14}, {gm_snkldl7hr4htt, 3}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpl8vbes2v6184u_p[] = {{gm_skocj074p3r97, 8}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl955dep3t38r6_p[] = {{gm_se5tulnsn3o9s, 9}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl9ne0ajfec16n_p[] = {{gm_smooaouqmm8dj, 1}, {gm_stoicv7ns2elf, 13}, {gm_smooaouqmm8dj, 1}, {gm_sg5gdnrith2i6, 2}, {gm_s1u0m6lm5u1su, 6}, {gm_srhsu0ubjqf5l, 6}, {gm_sqvepl1893o5g, 2}};
const gm_tmpl_piece gm_tpl9qc0voknsp7g_p[] = {{gm_s2u0i5o1ej1mg, 9}, {gm_ss9sitl8h5uvg, 17}};
const gm_tmpl_piece gm_tpla0a5a633lqve_p[] = {{gm_s0of5gcf1i7ic, 28}, {gm_sth0p0t3b0nta, 23}};
const gm_tmpl_piece gm_tpla93ou7df923g_p[] = {{gm_sj8b32e846r90, 10}, {gm_s3p0rbs9hc3lg, 1}, {gm_sr4qbrk6tlgaa, 0}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplak7vr9q0b05e_p[] = {{gm_si7hg8a0jmhu5, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplalgvrfo85soq_p[] = {{gm_sdckdu8fe6iog, 6}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplanhmvcr622qo_p[] = {{gm_s9rk6ipadg2pq, 110}, {gm_st518h9bgt9n7, 21}};
const gm_tmpl_piece gm_tplaq7jlj8i0s87_p[] = {{gm_s0ffe093dar6e, 18}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplb4rog3tmrj9n_p[] = {{gm_s2kg96lb0hss7, 27}, {gm_so7pbnda42pgd, 8}, {gm_sg398rgmck67b, 3}};
const gm_tmpl_piece gm_tplb9surs05k5po_p[] = {{gm_ssfg877iegnma, 3}, {gm_sqdfr4k3vea5p, 3}, {gm_so514oj5nbhcu, 1}};
const gm_tmpl_piece gm_tplbds86ojmcoik_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sbh3nm117l7ra, 34}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplbjh9dr2llnkb_p[] = {{gm_som0q53jqvjh3, 4}, {gm_selmorb0539e5, 4}, {gm_slb3ondne09t1, 27}};
const gm_tmpl_piece gm_tplbog2sc0rf3ot_p[] = {{gm_shl0ucmfrvk77, 53}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplc47d0vivqs48_p[] = {{gm_slqgr0k308e4c, 29}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplca6pmroagoou_p[] = {{gm_ss496b65r9omj, 5}, {gm_sr4qbrk6tlgaa, 0}, {gm_sr4qbrk6tlgaa, 0}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplck4jpb6nd99p_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_s9lvs84vghure, 11}};
const gm_tmpl_piece gm_tplcl8es85s6qrs_p[] = {{gm_s99scb3gc7vkm, 28}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplcoet0p57pg4s_p[] = {{gm_sdv6tt97thssp, 21}, {gm_s9sev457i3j7n, 4}, {gm_s2u7k18iev3v2, 3}, {gm_s9u1dek1h1q8t, 4}, {gm_sqdvt3ooptrnc, 4}, {gm_sfm739ta4hkl2, 5}};
const gm_tmpl_piece gm_tplcpt7hf5iscqd_p[] = {{gm_suctpm7bivu2g, 47}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplcte0qqjaa0q0_p[] = {{gm_s677r76pa1oks, 6}, {gm_so514oj5nbhcu, 1}};
const gm_tmpl_piece gm_tplctjp7jb08m5t_p[] = {{gm_sb8c0irhu65ra, 139}, {gm_sjnkguo0atpm5, 2}};
const gm_tmpl_piece gm_tpld0t14m1fld0p_p[] = {{gm_sm55k23067j17, 15}, {gm_s24s9g8r9gc0v, 6}, {gm_s89gtqa9dnss0, 34}, {gm_sa39uag0oed9a, 15}};
const gm_tmpl_piece gm_tpld1ficc6ikf1e_p[] = {{gm_s3p0rbs9hc3lg, 1}, {gm_s3p0rbs9hc3lg, 1}};
const gm_tmpl_piece gm_tple7i0r0sg6sjs_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sbu9dvh1hebne, 25}};
const gm_tmpl_piece gm_tplebqthjijvqt0_p[] = {{gm_soa9mnl4ahjud, 7}, {gm_s3p0rbs9hc3lg, 1}, {gm_s6t4r831mukdr, 2}};
const gm_tmpl_piece gm_tpleie0q8nvjqch_p[] = {{gm_sfc624hllrs4g, 5}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplem02qig8laoi_p[] = {{gm_srjcgu24i74p9, 47}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpleoabj1u2rb54_p[] = {{gm_sjlci8b0qvbpc, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpleonu21sc9fi7_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sv2qh30031o3v, 4}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplepdvp8ofjal7_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sahim4484f9l7, 2}};
const gm_tmpl_piece gm_tplfqk35ejq39la_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_s7d1d0uk71q3u, 18}, {gm_sfm9elkq6bn2e, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplfqr816g9de0k_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sr4qbrk6tlgaa, 0}, {gm_sr4qbrk6tlgaa, 0}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplggcatbkggmn2_p[] = {{gm_ss7iv2kssvf80, 6}, {gm_so514oj5nbhcu, 1}};
const gm_tmpl_piece gm_tplgp3qh1m090bm_p[] = {{gm_s20qcu4sv6mcn, 18}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplhkjcpttbsqt0_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_so514oj5nbhcu, 1}};
const gm_tmpl_piece gm_tplibgopbmor3p6_p[] = {{gm_s5v003pfcdo1r, 21}, {gm_s0m0qaokk51g0, 3}, {gm_s6n3pd0tmqbqc, 9}};
const gm_tmpl_piece gm_tplin004j5gtkru_p[] = {{gm_s3phpt25k93cc, 38}, {gm_stqlepc346jgk, 84}};
const gm_tmpl_piece gm_tplio7v2odie8p1_p[] = {{gm_so9fi6r28770a, 2}, {gm_s52ul4dg63jhe, 10}, {gm_segv6dkv6t4tr, 6}};
const gm_tmpl_piece gm_tplj4gqj9p0fguq_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_s33h2lce46bnm, 8}, {gm_s8qpjmalo4drt, 12}};
const gm_tmpl_piece gm_tplj78420ela1cg_p[] = {{gm_sdjfsrjr2b2ke, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpljgv7fkm5ld25_p[] = {{gm_sktf16uersaon, 8}, {gm_s3p0rbs9hc3lg, 1}, {gm_s48vlq6lddb8m, 28}, {gm_s4e3q3n6msi6m, 5}};
const gm_tmpl_piece gm_tpljq1be78i0ptp_p[] = {{gm_s5irlrsnb30cc, 23}, {gm_su2ima1nbr01d, 51}};
const gm_tmpl_piece gm_tpljv32on6ui3vn_p[] = {{gm_spp2faaelnnt0, 7}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplk13976gpibvh_p[] = {{gm_sqi8fcv4bi7s6, 9}, {gm_s8de5r2vp8807, 19}};
const gm_tmpl_piece gm_tplk7hjamhd5jcl_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sojts6dcvra33, 34}, {gm_s1emqv9srjhhk, 37}, {gm_slmlefe4cpf5e, 14}};
const gm_tmpl_piece gm_tplk83cf3bpuiqb_p[] = {{gm_svutc5hf6cgiv, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplkik9smkp1eb6_p[] = {{gm_sr5mkle2k9poq, 27}, {gm_srrm1sevfig2o, 2}};
const gm_tmpl_piece gm_tplkjc5mpc5i80h_p[] = {{gm_sm922ui8k0oca, 8}, {gm_sq8839lqmgrd7, 10}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpllir4kiseuk2b_p[] = {{gm_s5bj9cslts4df, 33}, {gm_s6mimndcv0cge, 3}, {gm_sr4qbrk6tlgaa, 0}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpllsre3itruefn_p[] = {{gm_s988kcbq5r7tp, 13}, {gm_s3p0rbs9hc3lg, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplm3f7baid20me_p[] = {{gm_s5glpb7fbtk4c, 26}, {gm_siopg3b55pic9, 11}, {gm_s959ni36b80a4, 3}};
const gm_tmpl_piece gm_tplm6qp3tlbq28j_p[] = {{gm_s76hg0po5usup, 8}, {gm_so514oj5nbhcu, 1}};
const gm_tmpl_piece gm_tplmaqqdmni75bf_p[] = {{gm_s5pra70i8o734, 13}, {gm_s08dcqdp58r5f, 2}, {gm_scte4ush0iqha, 6}};
const gm_tmpl_piece gm_tplmekan10d5buf_p[] = {{gm_smnfu5n5pum1l, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplmjku4vg3eupf_p[] = {{gm_svq6hhfarqhmv, 9}, {gm_sgukm1evmrjal, 3}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplmjmservvuqtp_p[] = {{gm_svjl1hr0jvvbv, 8}, {gm_sg5gdnrith2i6, 2}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpln4t9g9do4qg7_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_s9j0jokaa6cqi, 112}, {gm_sjnkguo0atpm5, 2}};
const gm_tmpl_piece gm_tpln8mihgcsij40_p[] = {{gm_sm732n1u78vvc, 15}, {gm_s1asl1e1tvb0k, 1}, {gm_spm4139orpkvm, 9}, {gm_skfdo9qggtrbp, 5}, {gm_sqrh6i8djv6b4, 6}, {gm_snr5o5tak6405, 3}};
const gm_tmpl_piece gm_tplnevoink17fov_p[] = {{gm_sghk06svnbp7m, 39}, {gm_sr4qbrk6tlgaa, 0}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplng1u8s5kl5ir_p[] = {{gm_sdjk9c005ec9t, 12}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplnm0o65j76qg0_p[] = {{gm_s3tacjdl2ciei, 12}, {gm_sr4qbrk6tlgaa, 0}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplo4mr4l55q97r_p[] = {{gm_s4l676io35g5u, 11}, {gm_sql1qe620spbq, 22}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplogq0vb9du4ii_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sd276t5aajuq8, 2}, {gm_s2053hhvf16fm, 1}};
const gm_tmpl_piece gm_tploh2305pu3svo_p[] = {{gm_ssici0eo813ko, 16}, {gm_s15gg5sblr4kl, 3}};
const gm_tmpl_piece gm_tploj0vopi0n43l_p[] = {{gm_sikspprj7od7l, 42}, {gm_s2lhokjucfplm, 9}};
const gm_tmpl_piece gm_tplomrgvu31uqof_p[] = {{gm_sse16dr2qj0im, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tploui9f30uh460_p[] = {{gm_ss52pcn7gi1ca, 24}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplp4f8nloo6fdo_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sfk3r5va3vobm, 4}};
const gm_tmpl_piece gm_tplpqhejpqnulcm_p[] = {{gm_s4j1eg5g5a64h, 4}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplpsmnsqmt7n3e_p[] = {{gm_sj21lfdl9q0t4, 5}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplptk7v9d6qojh_p[] = {{gm_sqtabum90d47u, 9}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplpvfl1p0n8is8_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sjkj5g78lejrj, 3}, {gm_s26crtotu14pn, 2}};
const gm_tmpl_piece gm_tplq74l4dgr0l9g_p[] = {{gm_spmhbt07q6beu, 8}, {gm_snr5o5tak6405, 3}};
const gm_tmpl_piece gm_tplr7712ujh33fl_p[] = {{gm_seglpsc899j08, 10}, {gm_s5gn6cmh4thug, 37}, {gm_sj1gcl0qo4i9p, 6}};
const gm_tmpl_piece gm_tplr8vkqgn7raqo_p[] = {{gm_su06jr9k1rhdo, 14}, {gm_sg5gdnrith2i6, 2}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplre5cfbnr7ltv_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sqr1rjjq3dpuh, 16}, {gm_s73vt0nuu7890, 27}};
const gm_tmpl_piece gm_tplrodjuj3fgmsa_p[] = {{gm_sh43rpq1oqm5d, 5}, {gm_sfdbfrv46lihd, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpls2ou5a11vcn7_p[] = {{gm_s4ujp5fsfjtut, 7}, {gm_snukl6jqep6jf, 3}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpls4bcac20qnq6_p[] = {{gm_s8vjpeo1vp60m, 15}, {gm_sn0kh110541oo, 22}};
const gm_tmpl_piece gm_tpls6vb484t8cfc_p[] = {{gm_s2m4r2316r14d, 10}, {gm_s9mmepka9n2j2, 9}, {gm_s3p0rbs9hc3lg, 1}, {gm_sr4qbrk6tlgaa, 0}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplsg1jhb9vp948_p[] = {{gm_sgjp5nurn7jdm, 7}, {gm_slf1jt4j4vgli, 8}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplt0qsjg06adbb_p[] = {{gm_sf46mekb18qm8, 13}, {gm_sjfpr08jc3ep1, 22}, {gm_s72hk5u5ngq4i, 10}};
const gm_tmpl_piece gm_tplt6nq68u4co32_p[] = {{gm_si430gujtnl9u, 10}, {gm_sq7ijigadotf8, 16}, {gm_s0m0qaokk51g0, 3}, {gm_sjoj5mss14s83, 5}};
const gm_tmpl_piece gm_tpltdoamgi2k9f9_p[] = {{gm_st28rv7m1lueb, 8}, {gm_sq9lne9r5j3u2, 7}, {gm_smpmtloh1fun3, 35}};
const gm_tmpl_piece gm_tplteu0ijrjjsvv_p[] = {{gm_sh0huebkcvk29, 19}, {gm_scstobchrdgiu, 66}};
const gm_tmpl_piece gm_tpltur94fmrrcmk_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_slgmqgkioa4u6, 2}, {gm_so514oj5nbhcu, 1}};
const gm_tmpl_piece gm_tplu76evoc63de2_p[] = {{gm_s0jhhkkbgc6ks, 17}, {gm_s7p349cm5h5jf, 19}, {gm_sj1gcl0qo4i9p, 6}};
const gm_tmpl_piece gm_tplu9uohqqitldd_p[] = {{gm_smooaouqmm8dj, 1}, {gm_s22tv9c5r20tl, 3}, {gm_sjltai3b82h79, 3}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpluhfd3hvppdji_p[] = {{gm_s03c24qg7i06q, 12}, {gm_sqdfr4k3vea5p, 3}, {gm_s4l3p0f0c6mmc, 43}};
const gm_tmpl_piece gm_tpluoji9akrnj9c_p[] = {{gm_siamquc19uikr, 1}, {gm_sb35te3dpkhv1, 12}, {gm_sl3q52ep6aa53, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplut4ib9ov72oe_p[] = {{gm_s6g1iihs2bped, 45}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpluvp47iqcj4uv_p[] = {{gm_s014h3o6j2aj2, 20}, {gm_so514oj5nbhcu, 1}};
const gm_tmpl_piece gm_tplv0flrfdqimb1_p[] = {{gm_s8lkplgn6m7u9, 7}, {gm_s20tdu9mtc6fi, 7}};
const gm_tmpl_piece gm_tplvcccam8nulr1_p[] = {{gm_sg5o5gf2u71g7, 8}, {gm_sg5gdnrith2i6, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplvonunpuh0kn8_p[] = {{gm_sihdt3ld7f53j, 15}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplvqfop4uu1u5q_p[] = {{gm_st28rv7m1lueb, 8}, {gm_sq9lne9r5j3u2, 7}, {gm_sig89vu0mvqtm, 18}};
const gm_tmpl_piece gm_tplvrmulr6rg858_p[] = {{gm_s5vakuk6ui4kj, 8}, {gm_so514oj5nbhcu, 1}};
const uint32_t gm_tpl0e2i60vm7gtr_i[] = {0u};
const uint32_t gm_tpl0f11vet24cfp_i[] = {0u};
const uint32_t gm_tpl16gsv91msflc_i[] = {0u};
const uint32_t gm_tpl1f1njss89nj4_i[] = {0u, 1u};
const uint32_t gm_tpl2cgrvj4ke42d_i[] = {0u, 1u};
const uint32_t gm_tpl3dghmndqn708_i[] = {0u};
const uint32_t gm_tpl3ergg6mce23h_i[] = {0u, 1u};
const uint32_t gm_tpl3kt8q9c3ih5v_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl4lt7abhi3ej4_i[] = {0u};
const uint32_t gm_tpl4nfbp2ikscbk_i[] = {0u};
const uint32_t gm_tpl5vfcnh2qs08h_i[] = {0u};
const uint32_t gm_tpl6ptbf3l6su1b_i[] = {0u};
const uint32_t gm_tpl6qj65gvm8vp3_i[] = {0u, 1u, 2u, 3u, 4u};
const uint32_t gm_tpl7da8g7lhjmr1_i[] = {0u};
const uint32_t gm_tpl7m3t7r6fqpf7_i[] = {0u};
const uint32_t gm_tpl7r51aeh92b80_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl7sqjangpsngf_i[] = {0u, 1u, 2u, 3u, 4u, 5u};
const uint32_t gm_tpl810t9tkid6i2_i[] = {0u};
const uint32_t gm_tpl81nh98hgo1rk_i[] = {0u};
const uint32_t gm_tpl8vbes2v6184u_i[] = {0u};
const uint32_t gm_tpl91b7sjgnmine_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl92h94vggr1gu_i[] = {0u};
const uint32_t gm_tpl9va1ubk8bean_i[] = {0u, 1u};
const uint32_t gm_tpla5rqmr49dnnk_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tpla6rojfclgc54_i[] = {0u};
const uint32_t gm_tplahe50smef36f_i[] = {0u, 1u, 2u};
const uint32_t gm_tplaotru3rka6ku_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tplb2ihdc8dbskd_i[] = {0u, 1u};
const uint32_t gm_tplc8kkdoja48ii_i[] = {0u, 1u, 2u};
const uint32_t gm_tplck4jpb6nd99p_i[] = {0u};
const uint32_t gm_tplcktokqd7n6fo_i[] = {0u};
const uint32_t gm_tplcn6c0fsph4j2_i[] = {0u};
const uint32_t gm_tplcqfaq9vjn7hh_i[] = {0u, 1u};
const uint32_t gm_tplctjp7jb08m5t_i[] = {0u};
const uint32_t gm_tpld1hhk0kef14l_i[] = {0u};
const uint32_t gm_tpld7d7u7to2a7u_i[] = {0u};
const uint32_t gm_tpld7ebjjnpmsr0_i[] = {0u};
const uint32_t gm_tpldt4nur6h705h_i[] = {0u, 1u};
const uint32_t gm_tpldtm53npup7ha_i[] = {0u, 1u};
const uint32_t gm_tple4r1aumd134l_i[] = {0u};
const uint32_t gm_tple7ipevcaonst_i[] = {0u, 1u, 2u, 3u, 4u, 5u};
const uint32_t gm_tplennc1kv0529b_i[] = {0u, 1u};
const uint32_t gm_tpleoabj1u2rb54_i[] = {0u};
const uint32_t gm_tplevafblg3gmq9_i[] = {0u, 1u, 2u};
const uint32_t gm_tplf27d39e1vcer_i[] = {0u, 1u};
const uint32_t gm_tplfm527r729efa_i[] = {0u};
const uint32_t gm_tplfoosl93e8k5t_i[] = {0u};
const uint32_t gm_tplftchh41b473r_i[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u};
const uint32_t gm_tplgism5t1v67p6_i[] = {0u, 1u, 2u};
const uint32_t gm_tplgjeridtvkml4_i[] = {0u};
const uint32_t gm_tplgl79fs8kklen_i[] = {0u};
const uint32_t gm_tplgt8u7d5aku5b_i[] = {0u, 1u};
const uint32_t gm_tplh5ff53j0j0d2_i[] = {0u};
const uint32_t gm_tplhiqpct20jd67_i[] = {0u};
const uint32_t gm_tpligbj3kvvunpr_i[] = {0u};
const uint32_t gm_tplims1qainr636_i[] = {0u};
const uint32_t gm_tplip1v4sd2477s_i[] = {0u};
const uint32_t gm_tpljkecvj49gp71_i[] = {0u, 1u};
const uint32_t gm_tpljv32on6ui3vn_i[] = {0u, 1u, 2u};
const uint32_t gm_tplk27ieg1b14ol_i[] = {0u, 1u, 2u};
const uint32_t gm_tplk6g6bjh5rkat_i[] = {0u};
const uint32_t gm_tplknfbq49i74sp_i[] = {0u, 1u};
const uint32_t gm_tplkucr7969socr_i[] = {0u};
const uint32_t gm_tplld9eu0oenqb7_i[] = {0u, 1u, 2u, 3u, 4u, 5u};
const uint32_t gm_tplm3qlt5n96af0_i[] = {0u, 1u, 2u};
const uint32_t gm_tplme3pul5hs3bf_i[] = {0u};
const uint32_t gm_tplmo458jpb0n1e_i[] = {0u, 1u, 2u};
const uint32_t gm_tplmoo8v17lb0ql_i[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u};
const uint32_t gm_tpln8mihgcsij40_i[] = {0u, 1u, 2u, 3u, 4u};
const uint32_t gm_tplna8gfl3ukgs3_i[] = {0u, 1u, 2u};
const uint32_t gm_tplnm352386bmep_i[] = {0u, 1u};
const uint32_t gm_tplo12j44trajdm_i[] = {0u};
const uint32_t gm_tplob6co4svk3k5_i[] = {0u, 1u};
const uint32_t gm_tploblcdf1g38ca_i[] = {0u};
const uint32_t gm_tplogq0vb9du4ii_i[] = {0u, 1u};
const uint32_t gm_tploj5r79v5kirr_i[] = {0u};
const uint32_t gm_tplosjvq6nju6rc_i[] = {0u, 1u};
const uint32_t gm_tplp7faum694g3u_i[] = {0u, 1u};
const uint32_t gm_tplpqhejpqnulcm_i[] = {0u};
const uint32_t gm_tplqbbl1qig0e0s_i[] = {0u, 1u};
const uint32_t gm_tplqc1ev2l2fda9_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tplqhu7t9iljb1t_i[] = {0u, 1u};
const uint32_t gm_tplql7vfevun5ae_i[] = {0u};
const uint32_t gm_tplqm5shuhajt4a_i[] = {0u};
const uint32_t gm_tplr3ni3ai315ji_i[] = {0u};
const uint32_t gm_tplr7pcqj5621b9_i[] = {0u, 1u, 2u};
const uint32_t gm_tplrkvt0j8tqlqg_i[] = {0u};
const uint32_t gm_tpls4d1lh9jd2u8_i[] = {0u};
const uint32_t gm_tplsh3mbm625h0g_i[] = {0u, 1u};
const uint32_t gm_tpltd7vnsp01mvt_i[] = {0u, 1u};
const uint32_t gm_tplu76evoc63de2_i[] = {0u, 1u};
const uint32_t gm_tpludqpd0ngjfu6_i[] = {0u, 1u};
const uint32_t gm_tpluh0hed6nj59d_i[] = {0u, 1u, 2u};
const uint32_t gm_tplukjghf5ng2te_i[] = {0u, 1u};
const uint32_t gm_tplvk1pmcp9tr4p_i[] = {0u};
const uint32_t gm_tplvkvnlb2orh55_i[] = {0u, 1u, 2u};
const uint32_t gm_tplvm7e7lfhcheu_i[] = {0u, 1u};
const uint8_t gm_s0039h44ked10[39] = "unreachable: the key was just inserted";
const uint8_t gm_s018g1qkt4p93[9] = "tmp_next";
const uint8_t gm_s09r417b6q8b4[14] = "--show-output";
const uint8_t gm_s0bmhg7t703ts[22] = "`--{}` takes no value";
const uint8_t gm_s0i4qvihaki34[41] = "\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000\217\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_s0lh70hs4of8e[22] = "didChangeWatchedFiles";
const uint8_t gm_s0r9vb1qh90ue[116] = "`: targets have no instruction that divides integer lanes, so the cost would be hidden; divide the lanes one by one";
const uint8_t gm_s0si75ormd2gn[15] = "    if (s) {}\012";
const uint8_t gm_s0tfhmgv1pal7[9] = "location";
const uint8_t gm_s0tjng49g04bk[15] = "checksum = {}\012";
const uint8_t gm_s122r7j2imgmj[22] = "to close the tag type";
const uint8_t gm_s18e8dmribdb3[7] = ")0 : (";
const uint8_t gm_s1aecjev6ahg8[5] = "Add(";
const uint8_t gm_s1ak5vhpqj4ju[5] = "u128";
const uint8_t gm_s1d20j48bjjln[12] = "__FreeBSD__";
const uint8_t gm_s1d982oimmu24[7] = "git+{}";
const uint8_t gm_s1dbslfck7laf[3] = "*%";
const uint8_t gm_s1f440st4mnkp[2] = "<";
const uint8_t gm_s1hbm05s14qo4[30] = "`, which leaves its directory";
const uint8_t gm_s1hha8fc2kd79[18] = "remove the `else`";
const uint8_t gm_s1ishc3mvan41[31] = "math vectors have no method `.";
const uint8_t gm_s1j5448m6ruti[10] = "HeapAlloc";
const uint8_t gm_s1jboc8lkvac0[6] = "field";
const uint8_t gm_s1lekaltijhvv[8] = "prog-{}";
const uint8_t gm_s1nvbb638rr96[8] = "->arg, ";
const uint8_t gm_s1objv452j1q2[11] = "this slice";
const uint8_t gm_s1tqe3mtj6vb1[8] = "struct ";
const uint8_t gm_s20ceq2pdpd1i[14] = "parse_foreign";
const uint8_t gm_s26cjdp185rsj[16] = "    if (tg == (";
const uint8_t gm_s27f321qjn1nc[86] = "    for (uint64_t i = 0; i < {}; i++) if (!{}(a + i * {}, b + i * {})) return false;\012";
const uint8_t gm_s27jprdv4pn6e[10] = "passing `";
const uint8_t gm_s2a4d8jj74v6o[49] = "`_` may only go between digits, expected nothing";
const uint8_t gm_s2a9t8smkevg0[4] = "Get";
const uint8_t gm_s2dssfe3261t2[2] = "2";
const uint8_t gm_s2ea46cnsj1sj[20] = "exited with status ";
const uint8_t gm_s2eu0g6qdje9o[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000e\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_s2g391595qd1n[10] = "ident {}\012";
const uint8_t gm_s2qk17n09116b[14] = "-bit integer>";
const uint8_t gm_s2r68j08i2lia[5] = "<tr>";
const uint8_t gm_s2sghqjh0qbk9[8] = "<bench>";
const uint8_t gm_s2u7kalhku9i5[152] = "the package contains the symbolic link `{}` -> `{}`, which points outside the package through other links; links must stay inside the package directory";
const uint8_t gm_s2v8uomf8pkfv[13] = " ta = GM_LD(";
const uint8_t gm_s312ibu3ccl18[12] = "{}|{}|{}|{}";
const uint8_t gm_s315r4147rf48[40] = "GM_PTR_ADD({}, {}((uintptr_t){} * {}u))";
const uint8_t gm_s329kcr9363eu[11] = "{}({}, {})";
const uint8_t gm_s3385032lmmqq[24] = "integer overflow in `%`";
const uint8_t gm_s33fm62gcdbhu[11] = "targets a ";
const uint8_t gm_s361hl447ggcv[20] = "compiler/fix/fix.gm";
const uint8_t gm_s38qa2pnnt0lv[25] = "#pragma pop_macro(\"{}\")\012";
const uint8_t gm_s39o4eejklnd6[12] = "a file name";
const uint8_t gm_s3a1gf31m3ei6[10] = "(uint8_t)";
const uint8_t gm_s3cu59itn73k7[23] = "__attribute__((cold)) ";
const uint8_t gm_s3d0v42kavhv7[19] = "lib/std/str/str.gm";
const uint8_t gm_s3gftv3r0boaq[5] = "loop";
const uint8_t gm_s3j0d9e0k3atk[5] = "plus";
const uint8_t gm_s3mbo1s262isg[14] = "`@stack_size(";
const uint8_t gm_s3md83t7b4thr[10] = "this `in`";
const uint8_t gm_s3nknbqjesra1[19] = "array construction";
const uint8_t gm_s3ntjearde849[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000\022\001\000\000\033\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_s3phpt25k93cc[39] = "generic instantiations nest more than ";
const uint8_t gm_s3pufl09hvbbf[6] = "arm64";
const uint8_t gm_s3sgbpnnlr6sb[5] = " are";
const uint8_t gm_s4108vvmvu0gn[70] = "`{}` modifies its receiver, which must be a variable, not a temporary";
const uint8_t gm_s49o1oiv7k0po[4] = "Oct";
const uint8_t gm_s4acc6d5101lp[45] = " parameters (the first is the handler value)";
const uint8_t gm_s4be485tkfn41[15] = "nvariants = {}";
const uint8_t gm_s4dqb8b35j0gp[24] = "textDocument/codeAction";
const uint8_t gm_s4j3ecogbjkvn[13] = "textDocument";
const uint8_t gm_s4jrltvkedvou[31] = "((gm_closure){{{}, NULL, {}}})";
const uint8_t gm_s4k5q4ehrakfe[15] = "(void *){}, {}";
const uint8_t gm_s4ld5r1oi34jh[18] = "the import path `";
const uint8_t gm_s4p5imo39raaq[40] = " is read where it is not written before";
const uint8_t gm_s4tgvb7s2dbuj[20] = ": warning: module `";
const uint8_t gm_s4ufkrqv2ag7g[7] = "--fail";
const uint8_t gm_s4uh375udgni3[206] = "GM_NOINLINE {} {{\012{}    {} a;\012    GM_ST(void *, (uint8_t *)&a, (void *)({}));\012    GM_ST(uintptr_t, (uint8_t *)&a + {}, (uintptr_t)({}));\012    {}{}(cx, a, file, line, col);\012{}    return gm_unw.kind != 0;\012}}\012";
const uint8_t gm_s4v09sn49l08n[7] = "_union";
const uint8_t gm_s52jn4s18md6n[16] = " != 0) memmove(";
const uint8_t gm_s5a98jvj83tqj[16] = "gamma/debugTrap";
const uint8_t gm_s5aj93skvhpnl[17] = ")` or `.checked(";
const uint8_t gm_s5bh8t3p9l4nk[16] = "refs/gamma/head";
const uint8_t gm_s5bohs6opthjm[24] = "    return 0;\012#undef F\012";
const uint8_t gm_s5ctp78jmtbv8[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000S\003\000\000\015\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000";
const uint8_t gm_s5dpb04jti9nb[17] = "to begin a block";
const uint8_t gm_s5dqpb4hur24f[37] = "({} < {} \077 {} : ({} > {} \077 {} : {}))";
const uint8_t gm_s5g5rfc96krge[9] = "\036\000\000\000\000\000\000\000";
const uint8_t gm_s5gkblcfbl1mv[121] = "this `errdefer` never runs: it is the last statement of its block, so nothing after it can leave the block with an error";
const uint8_t gm_s5hp4n46rtp6j[15] = "fmt_shortest16";
const uint8_t gm_s5j95frjqt5af[8] = "{}[={}]";
const uint8_t gm_s5jtsqa43tb6m[14] = "gm_trap_lanes";
const uint8_t gm_s5k1c58je5d4s[25] = "integer overflow in `{}`";
const uint8_t gm_s5k1m00kjtc17[14] = "    {}: {}{}\012";
const uint8_t gm_s5le7rpnp1iq6[9] = "FileKind";
const uint8_t gm_s5nig6ebl26qo[61] = "`#trap_hook` applies only on a freestanding target (\302\2473.7.1)";
const uint8_t gm_s5qkai093c1q0[6] = "value";
const uint8_t gm_s5qpqp0tch0l9[7] = "[{}]{}";
const uint8_t gm_s5t1b3eks758h[27] = "unterminated block comment";
const uint8_t gm_s5u07ahpa78ud[27] = "(({}){})({}{}, {}, NULL{})";
const uint8_t gm_s5us2hgd173vu[27] = "to close the argument list";
const uint8_t gm_s60n1k0cqjuaq[144] = "`{}` of dependency `{}` names `{}`, which is not inside the package; paths in a dependency's [build] must be relative and stay in its directory";
const uint8_t gm_s63jrdfu5o5ps[9] = "volatile";
const uint8_t gm_s67h6d8kdg7qc[167] = "a name in the position of an optional matches only a present value (\302\2479.3); to bind the optional as it is, absent value included, write `\077` before the name (`.V(\077x)`)";
const uint8_t gm_s6aamtn65omr1[7] = "LaneNe";
const uint8_t gm_s6bbu24cfq0ok[14] = ", sha256 = {}";
const uint8_t gm_s6ccueucpa122[6] = "  {}\012";
const uint8_t gm_s6dpovcf17va4[4] = "F32";
const uint8_t gm_s6i2dst792b3t[54] = "expected a compile-time `{}`, found `{}` of type `{}`";
const uint8_t gm_s6iuue4hdrbt9[5] = "safe";
const uint8_t gm_s6npjseknrjhj[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\0008\001\000\000\011\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_s6o3rebeo47ka[16] = "a present value";
const uint8_t gm_s6qihcjfe8481[10] = "type_name";
const uint8_t gm_s6vg88ce880ha[13] = "unused-label";
const uint8_t gm_s6vvtrmtnt3j1[5] = "bits";
const uint8_t gm_s75m3dt9015lh[15] = "(gm_node *cx, ";
const uint8_t gm_s78cm457osdms[49] = "\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000";
const uint8_t gm_s79el5a3kii78[12] = "gamma-test-";
const uint8_t gm_s79r5ktousup8[157] = "checksum mismatch for package {} (expected {}, found {}); gamma.lock was edited or the cached archive {} was modified (`gamma cache clean --all` removes it)";
const uint8_t gm_s79s3h98matgr[6] = "error";
const uint8_t gm_s7ah1q5ah26rj[12] = "not a union";
const uint8_t gm_s7atji10aaq5k[7] = " lift ";
const uint8_t gm_s7bpqsd6i8gc3[15] = "gamma: {}\012\012{}\012";
const uint8_t gm_s7cn2q9qfeafa[79] = "` does not apply to complex numbers; they have `+`, `-`, `*` and `/` (\302\2477.4.7)";
const uint8_t gm_s7doqph2gqafo[60] = "internal error: a `ctl` operation performed outside a fiber";
const uint8_t gm_s7f8crhgefl4u[28] = "` does not match the open `";
const uint8_t gm_s7j1ioljmsuka[19] = "the default branch";
const uint8_t gm_s7lavhjsa5dc0[62] = "</title>\012<style>\012{}</style>\012</head>\012<body>\012{}</body>\012</html>\012";
const uint8_t gm_s7u3qhd03rfo3[17] = "unsupported URI ";
const uint8_t gm_s7ucknluds45h[5] = "gm_l";
const uint8_t gm_s7v0kgsq3q7vf[57] = "`#callconv` takes a convention name: `#callconv(.Win64)`";
const uint8_t gm_s80p0ld1j6rfg[10] = "link_name";
const uint8_t gm_s80t4sc5bf88n[82] = "a thread runs a `^fn(arg: rawptr) needs Sys` with the C calling convention, not `";
const uint8_t gm_s81ppjm26jd5s[148] = "the copy shares its elements with the original but not its length, so what is added through it is lost; to change the original, take its address: `";
const uint8_t gm_s852r4rtnj5dj[54] = "        gm_format(&b, {}, (uint8_t *)&r + {}, true);\012";
const uint8_t gm_s86js8lt7ibda[48] = "(gm_node *cx, const gm_type *d, const void *v, ";
const uint8_t gm_s873ct0as0vg4[48] = "gm_alloc_check({}, {}, {}, {}, {}, {}, {}, {});";
const uint8_t gm_s89j7s4avcmfs[28] = "expected an integer literal";
const uint8_t gm_s8aupotg4okcv[103] = "gm_fret:\012    memcpy(gm_out, &F.ret, sizeof F.ret);\012gm_fdone:\012    {}return;\012gm_fprop:\012    {}return;\012}}\012";
const uint8_t gm_s8de5r2vp8807[20] = "` is supplied twice";
const uint8_t gm_s8eis4410oprq[21] = "` is returned, but `";
const uint8_t gm_s8f69f7tnslut[16] = "gm_net_shutdown";
const uint8_t gm_s8krdq6a1r5nt[53] = "this compares a value with itself, so it is always `";
const uint8_t gm_s8l5u1ln9m2k0[58] = "`: use letters, digits and `_`, not starting with a digit";
const uint8_t gm_s8oua93imbc1q[15] = "gm_set_timeout";
const uint8_t gm_s8ovik98r39ct[13] = "uint8_t gm_g";
const uint8_t gm_s8rsdirj4dmun[4] = "doc";
const uint8_t gm_s8sl52ot187i2[50] = "a string literal naming the module after `import`";
const uint8_t gm_s8uplmiglsqkm[15] = "the level of `";
const uint8_t gm_s8v8krjfns655[20] = "gamma-watched-files";
const uint8_t gm_s8vgs1ersvo58[8] = "collect";
const uint8_t gm_s959ni36b80a4[4] = "));";
const uint8_t gm_s95mahatn8od3[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\226\001\000\000\036\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_s9aatfmgqhb47[7] = "break ";
const uint8_t gm_s9co2b6h6nhos[16] = "` in one `with`";
const uint8_t gm_s9es2tos1bhfp[5] = "else";
const uint8_t gm_s9f47s6tkuln3[42] = "`: No such file or directory (os error 2)";
const uint8_t gm_s9grtrvtmi962[8] = "uint8_t";
const uint8_t gm_s9lc9q0nba0ng[8] = "gm_args";
const uint8_t gm_s9lh30p1g8vti[54] = "; `v[..]` is a slice of its elements, which has `.{}`";
const uint8_t gm_s9nuud03bu2mh[11] = "++) GM_ST(";
const uint8_t gm_s9ol82jrvi3ma[4] = "*)(";
const uint8_t gm_s9qv939r7cv21[88] = "    for (uintptr_t i = 0; i < n; i++) if (!{}(pa + i * {}, pb + i * {})) return false;\012";
const uint8_t gm_s9sev457i3j7n[5] = " = {";
const uint8_t gm_s9tnrhv72lohp[8] = ", not `";
const uint8_t gm_s9vg8k4mbj38f[22] = "compiler/eval/eval.gm";
const uint8_t gm_sa027i133kcv3[4] = "F16";
const uint8_t gm_sa10lthaugqih[45] = "static const gm_tmpl_piece {}_p[] = {{{}}};\012";
const uint8_t gm_sa349un8b060v[19] = "unsigned __int128 ";
const uint8_t gm_sa90e17lvpr4h[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000\203\003\000\000\020\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_sa9sfrgc5vaao[5] = " >> ";
const uint8_t gm_sadte2ukivobf[28] = "{} {} = ({})({})({} {} {});";
const uint8_t gm_sae23ckujbpp9[107] = "regular string literals may not contain line terminators; use a raw string (backticks) for multi-line text";
const uint8_t gm_safiul2d9a6fl[14] = "unterminated ";
const uint8_t gm_safvj03783ie0[65] = "declare it with `::` to make it a compile-time constant (\302\24717.2)";
const uint8_t gm_sagpv7fgcnm3e[64] = "-Wl,--thinlto-cache-policy=cache_size_bytes=1g:prune_after=168h";
const uint8_t gm_sah431l1f82o2[113] = "\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\024\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\021\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\024\000\000\000\000\000\000\000";
const uint8_t gm_sajhum9b4nr96[2] = "+";
const uint8_t gm_sal17k00cm1q9[23] = "({})(({}){} {} ({}){})";
const uint8_t gm_samr3e6eqsavu[27] = "a place assigned to itself";
const uint8_t gm_sannl4idt68d4[42] = "`: it is immutable (declare it with `:=`)";
const uint8_t gm_saqgvkkehjsjb[33] = " (in an instantiation used here)";
const uint8_t gm_saqhrs072atsq[10] = "({} / {})";
const uint8_t gm_sar0vrndv51ep[14] = "gm_rt_acquire";
const uint8_t gm_sar7ovt8nasdo[8] = "Riscv64";
const uint8_t gm_sau1b8ivpr0us[8] = "GM_F16(";
const uint8_t gm_saunlif71pbir[16] = "gm_scratch_grow";
const uint8_t gm_sb5it581p5nll[5] = "Shl(";
const uint8_t gm_sb7lq2a8rdpq3[10] = "atomic_or";
const uint8_t gm_sb8kl54a499bf[7] = "(&F.h)";
const uint8_t gm_sb9dglh7ekrt2[7] = ", b);\012";
const uint8_t gm_sb9k4ip91d7oa[6] = ": \", ";
const uint8_t gm_sbel6qookv0gl[54] = "internal error: `abs` of a value that is not a number";
const uint8_t gm_sbf8eactntf1g[21] = "{} cannot be ordered";
const uint8_t gm_sbgj0biroecr0[13] = "fs_close_dir";
const uint8_t gm_sbl3pbs9195ho[4] = "abs";
const uint8_t gm_sblao71d2av8h[22] = "unterminated {} quote";
const uint8_t gm_sbngfgv31nmur[61] = "`{}` has no arithmetic: only many-pointers `[^]T` do (\302\2475.5)";
const uint8_t gm_sbo52miqad506[4] = "uri";
const uint8_t gm_sbrifoc7ea21m[7] = "Wasm32";
const uint8_t gm_sbrr14r7dj3rt[9] = "type {}\012";
const uint8_t gm_sbrtnmpf78kqq[10] = "GM_RETURN";
const uint8_t gm_sbs1lj6q8rfiu[44] = "variadic arguments are written positionally";
const uint8_t gm_sc103nruhmor7[3] = "\\x";
const uint8_t gm_sc24a25pi012e[8] = "))) \077 -";
const uint8_t gm_scedkbh0j6d78[6] = " r = ";
const uint8_t gm_scfb66ogf0spa[19] = "GM_ST({}, {}, {});";
const uint8_t gm_sch2ljca2kt47[12] = " #align({})";
const uint8_t gm_sck2rq9li012q[23] = "    gm_init_consts();\012";
const uint8_t gm_sckeklr60tlq3[34] = "integer overflow in `pow` ({}^{})";
const uint8_t gm_scn1hel0olv2p[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\215\000\000\000\026\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000";
const uint8_t gm_scnb11g2ghrd0[35] = "after the pattern of the match arm";
const uint8_t gm_scohuf4dfj32r[37] = ".ascii \"gdb.inlined-script.gamma\\n\"\012";
const uint8_t gm_scp8gj1kjauec[29] = "(uint8_t)(__builtin_signbit(";
const uint8_t gm_scte4ush0iqha[7] = "] = 1;";
const uint8_t gm_sd7guj32ltqh8[47] = "expected a variant, constant or struct pattern";
const uint8_t gm_sd7vrpqmmkbm0[111] = "an effect is declared at top level; inside a function only aliases, structs and unions can be declared (\302\2473.5)";
const uint8_t gm_sdcigk5ajbcfi[68] = "the literal {} does not fit in `{}` (in an instantiation used here)";
const uint8_t gm_sdh44op4kc2is[15] = "gm_overflowing";
const uint8_t gm_sdib10lkj65nm[45] = "` is not a C-compatible result type (\302\24719.1)";
const uint8_t gm_sdib2k2ql9mgl[9] = "op_scale";
const uint8_t gm_sdjba9ailb8m1[4] = "lsp";
const uint8_t gm_sdk5n6a42f7nl[3] = "0x";
const uint8_t gm_sdllavet8v9kb[41] = "\000\000\000\000\000\000\000\000\033\000\000\000\000\000\000\000G\001\000\000H\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000";
const uint8_t gm_sdori1qhqiv1t[42] = "    for (uint64_t i = 0; i < {}; i++) {}\012";
const uint8_t gm_sdugj3qbhl84q[5] = " : (";
const uint8_t gm_sdujhgrk0oln8[5] = "only";
const uint8_t gm_sdvlseva8qvl4[77] = "    uintptr_t na = GM_LD(uintptr_t, a + {}), nb = GM_LD(uintptr_t, b + {});\012";
const uint8_t gm_sdvpeftn9n4aa[7] = "{}_s{}";
const uint8_t gm_se3smlbkt8d02[37] = "if this is deliberate, name it `_{}`";
const uint8_t gm_se3u415f6s7p1[13] = "gm_cont_take";
const uint8_t gm_se7h6nse0tc33[26] = "to begin the `when` block";
const uint8_t gm_se7oqjo2sonp9[9] = "\"><code>";
const uint8_t gm_se8cpd8s09lkc[13] = "the module `";
const uint8_t gm_sedk3p1dc2it3[17] = "\000\000\000\000\000\000\000\000\"\000\000\000\000\000\000\000";
const uint8_t gm_sedq8b22f9agn[91] = "`cast` converts between pointer types, not from `{}` to `{}`; use `.addr()` for an address";
const uint8_t gm_sef68gcceqr04[21] = "-fno-stack-protector";
const uint8_t gm_sefm4vafv1a3l[3] = "\012/";
const uint8_t gm_seg5mo9lt1vgq[11] = "lc_op_node";
const uint8_t gm_seh4hl8ei3cha[20] = "dependency cycle: `";
const uint8_t gm_sehgil1ui47md[7] = ", a + ";
const uint8_t gm_seiuio4eghuvu[19] = "too many arguments";
const uint8_t gm_sel2c30qvm74f[7] = "BitXor";
const uint8_t gm_senjrqtakqrde[16] = "the declaration";
const uint8_t gm_set3pi1jhetbi[7] = "` ends";
const uint8_t gm_sf3fmcr9mrflm[15] = "evok \077 {} : {}";
const uint8_t gm_sf5llhuhdlajp[9] = "FsRemove";
const uint8_t gm_sf8e06i5juo6a[29] = "static void {}(gm_fiber *fb)";
const uint8_t gm_sf8mbf71lsfeh[35] = "    return memcmp(a, b, {}) == 0;\012";
const uint8_t gm_sfaeuqh39av6i[28] = "unwrapping this error value";
const uint8_t gm_sfeuhas7a05k6[6] = "{}\012{}";
const uint8_t gm_sfir6p8a22i6s[31] = " parameters, but this one has ";
const uint8_t gm_sfknvqdnjah7l[18] = "; gm_unw.label = ";
const uint8_t gm_sfm5elvivaoue[41] = "\000\000\000\000\000\000\000\000\033\000\000\000\000\000\000\000\005\001\000\000E\000\000\000\000\000\000\000\000\000\000\000\023\000\000\000\000\000\000\000";
const uint8_t gm_sfr3tv65k63d4[16] = "double-negation";
const uint8_t gm_sfsnsm5njae7n[19] = "static const char ";
const uint8_t gm_sfug4bo4t4p28[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000\227\000\000\000\024\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_sfvbeam2o42hd[38] = "`--run-test` needs a number, not `{}`";
const uint8_t gm_sg1uf2uigo01q[27] = "this compile-time optional";
const uint8_t gm_sg398rgmck67b[4] = "};\012";
const uint8_t gm_sg3ba4vpic0jb[19] = "__atomic_fetch_xor";
const uint8_t gm_sg46oe1nh64ub[9] = "closure ";
const uint8_t gm_sg7n1u5l63uaa[9] = "soa_free";
const uint8_t gm_sgbcvt6rqctvd[7] = "->fw, ";
const uint8_t gm_sgdopr7odbih9[10] = "cancel {}";
const uint8_t gm_sgge76tb2bmvi[32] = "workspace/didChangeWatchedFiles";
const uint8_t gm_sgip9ekbho6mc[11] = "` scales `";
const uint8_t gm_sgjp5nurn7jdm[8] = "memset(";
const uint8_t gm_sgm565ktbj9ua[12] = "ProcWriteIn";
const uint8_t gm_sgn9uq6nipp5t[33] = "`ctl` operations at compile time";
const uint8_t gm_sgrpmaenv85i1[73] = "`#soa` takes no arguments and is written once: `#soa [N]S` or `#soa []S`";
const uint8_t gm_sgsd94uuisj8t[73] = ": instantiations with different arguments are different effects (\302\24712.3)";
const uint8_t gm_sgu789edodj19[70] = "gm_fiber *{} = gm_fiber_new(inst->fw, (uint8_t *){}, {}, {}, {}, {});";
const uint8_t gm_sh50kc23mi3vj[36] = "{}({}.code, {}.env, {}, {}, {}, {})";
const uint8_t gm_sh9g09icusqpu[14] = ": its field `";
const uint8_t gm_shdh4dd2ejebj[158] = "`, which a generic function does not know; a `#soa` of a type parameter has whole elements (`a[i]`), `len`, slicing, `copy_from`, `make` and `free` (\302\2475.6.4)";
const uint8_t gm_sheqs83o4tgp8[45] = "fail if gamma.lock is missing or out of date";
const uint8_t gm_shh36vc8p1t4e[7] = " error";
const uint8_t gm_shhak6l2rrn43[15] = "({} - {} % {})";
const uint8_t gm_shj3jonnfj09m[8] = "when {}";
const uint8_t gm_shja5bmmh8ag2[30] = "the import `{}` is never used";
const uint8_t gm_shkhe2ptevja3[18] = "</code></a></li>\012";
const uint8_t gm_shki2pqrgmpd1[19] = "/opt/fil/bin/filcc";
const uint8_t gm_shlilv0hq3bqu[33] = "invalid boolean, expected `true`";
const uint8_t gm_shmdne5cle16i[8] = "write `";
const uint8_t gm_shnig5tqtdedi[4] = "f16";
const uint8_t gm_shqseesnqrpjf[22] = "expected comma after ";
const uint8_t gm_shrlc4g1vtjsu[47] = "` needs an element for every variant: missing ";
const uint8_t gm_sht5g388qmj1f[15] = "already exists";
const uint8_t gm_shv4snn6f8hcr[29] = ")` required by this function";
const uint8_t gm_si2v1nj23o22k[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\271\001\000\000$\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_si4k9tiofr8t2[7] = "LaneEq";
const uint8_t gm_si5jb1hd2qqp1[12] = "slice{}{}{}";
const uint8_t gm_si6sr5gqkcui7[6] = "); }\012";
const uint8_t gm_si6va5n9qkjau[185] = "({} \077 (GM_LD(uintptr_t, (const uint8_t *){} + {}) != 0 && {}({}.code, {}.env, {}, GM_LD(void *, {}), GM_LD(uintptr_t, (const uint8_t *){} + {}))) : {}({}.code, {}.env, {}, {}, {}, {}))";
const uint8_t gm_si8s9ids0otre[44] = "` is a module, which is not a value (\302\2473.2)";
const uint8_t gm_siamquc19uikr[2] = "%";
const uint8_t gm_sicgmpca63jln[2] = "0";
const uint8_t gm_sigl9de72ni6d[16] = "gm_thread_yield";
const uint8_t gm_siiln09e6475q[5] = "i128";
const uint8_t gm_sijs25uglkpn8[9] = "const {}";
const uint8_t gm_silt1tnb0phmp[3] = "-S";
const uint8_t gm_sipnguisfnjrj[49] = "{}->fn = (void *){}; {}->cx = inst->fw->body_cx;";
const uint8_t gm_sj0ah0ccdpkiu[46] = "# {}\012\012| Module | Description |\012| --- | --- |\012";
const uint8_t gm_sj0qpc1n9qui9[7] = "it was";
const uint8_t gm_sj1m58fkg956m[8] = "FromTag";
const uint8_t gm_sj3ehailc2pgo[52] = "`in` compares with `is`, which is not defined for `";
const uint8_t gm_sj7s3tnnvob4e[9] = "lanes_eq";
const uint8_t gm_sje5p1r4d76uk[57] = "a range cannot be an operand here; put it in parentheses";
const uint8_t gm_sjeoqheucmn3m[19] = "%{} = coerce {} {}";
const uint8_t gm_sjerq54hvns79[11] = "builtin {}";
const uint8_t gm_sjfqm9gvu1om9[8] = "` lanes";
const uint8_t gm_sjid61i849qic[3] = "+\077";
const uint8_t gm_sjm2vqdj9hddi[51] = "unexpected character after wildcard in version req";
const uint8_t gm_sjmmigkb19htg[4] = "Big";
const uint8_t gm_sjojf6rrbl49u[6] = "case ";
const uint8_t gm_sjqmjo9ak50j8[5] = "rp{}";
const uint8_t gm_sjrg55dooppac[6] = "after";
const uint8_t gm_sk31jlojfl0rr[8] = "element";
const uint8_t gm_sk754mj3p1el6[2] = "[";
const uint8_t gm_sk88tp2337lf6[13] = "VolatileLoad";
const uint8_t gm_ska87i9f7924f[8] = "int32_t";
const uint8_t gm_skefd3dps40es[40] = "an inclusive range needs an upper bound";
const uint8_t gm_skeo8nnnc4o7f[40] = "`sha256` of dependency `{}` needs `url`";
const uint8_t gm_skgcaidqnnhjr[11] = "bitreverse";
const uint8_t gm_skj7cpsc7j6g9[5] = "rotl";
const uint8_t gm_skmo18eln23n6[16] = "parse_stmt_kind";
const uint8_t gm_skn7gps778eo2[3] = "-E";
const uint8_t gm_sko48juemga53[34] = "struct __attribute__((packed)) {}";
const uint8_t gm_skpna6jsqumoc[9] = "operator";
const uint8_t gm_sks5shc2qs9ve[49] = "(({})(((unsigned __int128){}ULL << 64) | {}ULL))";
const uint8_t gm_sktopm8j9jc57[802] = "usage: gamma doc <module directory | file | package root> [options]\012\012Writes the documentation of the public declarations of a module (the directory of\012.gm files that `import` names, or one file), from their `///` comments, and the `//!`\012comments at the top of its files.\012\012options:\012  --format=markdown|html   the output format (default: markdown)\012  -o <path>                write to <path>: a directory gets one file per module and an\012                           index (index.md or index.html); a single module may also go\012                           to a file. Without -o one module is printed to stdout\012  --all                    document every module below the directory (needs -o)\012  --missing                warn about public declarations and modules without\012                           documentation\012";
const uint8_t gm_skvch52visg8i[13] = "this handler";
const uint8_t gm_sl0vvvamhv0to[12] = "gm_site_str";
const uint8_t gm_sl1a3p7pocs82[15] = ", (uintptr_t)(";
const uint8_t gm_sl2oiu9s0celr[49] = "\000\000\000\000\000\000\000\000\036\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000";
const uint8_t gm_sl2sr5fl46c73[48] = "fmt: buffer too small for the formatted integer";
const uint8_t gm_sl63bnit79nfo[41] = "\000\000\000\000\000\000\000\000\034\000\000\000\000\000\000\000C\000\000\000\032\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000";
const uint8_t gm_sl66hhhp1j2a6[14] = " has no tag `";
const uint8_t gm_slahfmk66v1v4[15] = "--max-filesize";
const uint8_t gm_slc0d2s6km3s5[5] = "-L{}";
const uint8_t gm_slc58j6a4e39a[56] = "` binds an optional or a result as it is, but this is `";
const uint8_t gm_slgmqgkioa4u6[3] = ": ";
const uint8_t gm_slj7i6lishtfg[47] = "a boolean constant cannot be converted to `{}`";
const uint8_t gm_sll7gknpe999d[4] = "-%=";
const uint8_t gm_slr574u01mn8m[17] = "gm_wasi_main_ctx";
const uint8_t gm_slshl1j266n3l[7] = "--warn";
const uint8_t gm_slsmq9u0kk3l3[6] = "gm_f_";
const uint8_t gm_sm1kt2tr7m23b[4] = "clz";
const uint8_t gm_sm3gke457vdm3[5] = "<<%=";
const uint8_t gm_sm4ujt9jgj9au[8] = "` has `";
const uint8_t gm_sm94b3smoinpd[60] = "internal error: a reduction of a value that is not a vector";
const uint8_t gm_smdj5floh97je[10] = "{}int{}_t";
const uint8_t gm_smggdkb5phvp0[56] = "the slice method `.{}` is missing from the core library";
const uint8_t gm_smo0niiqr9onn[11] = "deprecated";
const uint8_t gm_smrahl8ppkcu9[34] = "invalid integer width in suffix `";
const uint8_t gm_smrc1u3r4i5hd[7] = "note: ";
const uint8_t gm_smsf0v36m37b1[66] = "a `when` condition may not depend on a generic parameter (\302\24716.7)";
const uint8_t gm_smu6g4sneo92j[6] = "freed";
const uint8_t gm_smu8iti9fc903[85] = "`=>` takes an expression; a block body is written without it: `fn(\342\200\246) -> R { \342\200\246 }`";
const uint8_t gm_sn0ac8j2fho01[22] = "`{}` is not an effect";
const uint8_t gm_sn0d6pm4psq1h[21] = "` ends with the call";
const uint8_t gm_sn1puqmdafub0[14] = "unclosed `{}`";
const uint8_t gm_sn6gnsehoh5ru[7] = "to_ptr";
const uint8_t gm_sn9ftiite84ek[41] = "\000\000\000\000\000\000\000\000\027\000\000\000\000\000\000\000\324\000\000\000\022\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000";
const uint8_t gm_snaenr23tbgmb[23] = "_Static_assert(sizeof(";
const uint8_t gm_sncti78voitis[76] = "if ({}) gm_format_spec(&{}, {}, {}, {}); else gm_format(&{}, {}, {}, true);";
const uint8_t gm_snhskkd5kfgfa[31] = "`{}` has no meaning on vectors";
const uint8_t gm_snk6krn32vlub[10] = "<a href=\"";
const uint8_t gm_snls9i3gr3n3f[41] = "gamma.lock does not satisfy dependency `";
const uint8_t gm_snqmbfl2fr85m[17] = "cannot convert `";
const uint8_t gm_snqmeaue6lcfo[9] = "gm_atags";
const uint8_t gm_snrrubbi4s05g[16] = "` has a field `";
const uint8_t gm_snud2jia3fe8d[18] = "a handler literal";
const uint8_t gm_so1gbme93p3pe[5] = "sort";
const uint8_t gm_so25i3en68imc[27] = "(cx, a, file, line, col);\012";
const uint8_t gm_so2dhrd52m05k[49] = "` of a slice is a field, not a method: write `s.";
const uint8_t gm_so3ukp67pds22[190] = "a name in the position of an optional or a result below the top of a pattern matches only a present or success value (\302\2479.3); add an arm for the other case (such as `.V(null)`), or write `\077";
const uint8_t gm_so48b6sdoerku[18] = "u+rwX,u-s,g-s,a-t";
const uint8_t gm_so4d6ttcc6c3j[41] = "\000\000\000\000\000\000\000\000\024\000\000\000\000\000\000\000!\001\000\000\036\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000";
const uint8_t gm_sodgjk6pvm3b4[21] = "memory allocated at ";
const uint8_t gm_sodivosk5r8rb[2] = "\002";
const uint8_t gm_sojb2hhp0ln7r[31] = "    print(\"{}\\n\", __repl_type(";
const uint8_t gm_solsl3u2gfc4f[7] = "a test";
const uint8_t gm_sp0ce5j43v7u2[26] = "` has a payload; write `.";
const uint8_t gm_sp1srk1sv0crk[52] = "`for` iterates over ranges of integers, not of `{}`";
const uint8_t gm_sp24uj950qpkl[16] = "variants = {}_v";
const uint8_t gm_sp46pm3sna4bl[86] = "`{}` instantiates itself with ever-larger type arguments (more than {} deep) (\302\24716.7)";
const uint8_t gm_sp796mgrgp4mj[45] = "`=` or `:` and an initializer after the type";
const uint8_t gm_spaa4sseck2fg[45] = "`; use only one of version, tag, branch, rev";
const uint8_t gm_spaflckd9bou5[29] = "`never` has no values, so `@";
const uint8_t gm_spasf3abnfbjh[8] = "general";
const uint8_t gm_spbdk1kpko0k2[141] = "static void gm_start(void) {{\012    static int started;\012    if (started) return;\012    started = 1;\012    gm_debug = {};\012    gm_init_consts();\012}}\012";
const uint8_t gm_spducfcn5c083[69] = "`.{}` of a slice is a field, not a method: write `s.{}` without `()`";
const uint8_t gm_spervk999ikim[8] = "Aarch64";
const uint8_t gm_spf5cnp0l9k4n[449] = "\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\022\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000";
const uint8_t gm_spfojlg262423[13] = "    {} ret;\012";
const uint8_t gm_spi47lg4tr80j[3] = "or";
const uint8_t gm_spl4qnmq2arsc[13] = "binding-stmt";
const uint8_t gm_spnr89edksdqp[4] = "), ";
const uint8_t gm_spr959sr1jhja[5] = ".ptr";
const uint8_t gm_spt7rbjl7ucc9[8] = "prepare";
const uint8_t gm_sq0bes3qstkub[15] = ".exe\012/.gamma/\012";
const uint8_t gm_sq2es9bij87b7[14] = "negating `{}`";
const uint8_t gm_sq32stff79ol1[22] = "`, but its value is `";
const uint8_t gm_sq6e9d3107t3s[11] = "debug-info";
const uint8_t gm_sq8839lqmgrd7[11] = ",\"offset\":";
const uint8_t gm_sq8aqd2uj6ppq[12] = "diagnostics";
const uint8_t gm_sq9qefps1uqei[10] = "{}.{}: {}";
const uint8_t gm_sqa29afvhov50[6] = "gm_fr";
const uint8_t gm_sqbbhnhgkc31j[14] = "repo_dir_name";
const uint8_t gm_sqcjuf1r2epl2[7] = " table";
const uint8_t gm_sqflguscq1uc5[6] = "guard";
const uint8_t gm_sqg70d5uem21e[35] = "const uint8_t *a, const uint8_t *b";
const uint8_t gm_sqit0ai5v3joc[23] = "remove the parentheses";
const uint8_t gm_sqlqubis691m5[66] = "    if (!gm_unw.kind) gm_trap(\"a fiber's function returned\", {});";
const uint8_t gm_sqm6u7rqmi1il[19] = "a `for` expression";
const uint8_t gm_sqn7kgtu67hrg[42] = "internal error: not a file-system service";
const uint8_t gm_sqqvi1jr97u13[10] = "gm_cancel";
const uint8_t gm_sqrsljl0ejj01[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\025\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_sqt8gk1nsci25[5] = ")) {";
const uint8_t gm_sqtultomupp4c[22] = ",\"line\":{},\"message\":";
const uint8_t gm_squ4ohvqdqaap[79] = "a vector's lanes are `i8`\342\200\223`i64`, `u8`\342\200\223`u64`, `f32`, `f64` or `bool`, not `";
const uint8_t gm_sqvn1en6c2n9e[32] = "` is stored where it outlives `";
const uint8_t gm_sqvvajmjm3fif[11] = ".tmp-{}-{}";
const uint8_t gm_sr0hr1ep2j2vv[5] = "root";
const uint8_t gm_sr5mkle2k9poq[28] = "rename it, for example to `";
const uint8_t gm_sr899jbhl9noj[3] = "-q";
const uint8_t gm_sremdpsmf4ala[50] = "a union's tag type must be an integer type, not `";
const uint8_t gm_srg9h7ti0cg3a[25] = "(field: \342\200\246)` (\302\2475.10.5)";
const uint8_t gm_sri1d6adh4kkk[11] = "(void *){}";
const uint8_t gm_srim6iisdhve7[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000}\003\000\000\027\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_srj7urnaf6a1u[9] = "handlers";
const uint8_t gm_srjcgu24i74p9[48] = "a compile-time parameter cannot have the type `";
const uint8_t gm_srk1d80eamfhd[15] = "a variant name";
const uint8_t gm_srq25fqvivo2q[73] = "(code, env, cx, d, n);\012    if (b.data) gm_buf_free(&b);\012    return u;\012}\012";
const uint8_t gm_srrm1sevfig2o[3] = "_`";
const uint8_t gm_srs4r9h62u04l[13] = "add_siblings";
const uint8_t gm_sru4dh297ccek[19] = "` for its target:\012";
const uint8_t gm_ss03naco6s5l7[5] = "gm_r";
const uint8_t gm_ss3i577tvjetf[8] = "-fwrapv";
const uint8_t gm_ss4337v5p4tj3[5] = ".len";
const uint8_t gm_ss54u8us11c4v[42] = "the clause for `{}` takes {} parameter(s)";
const uint8_t gm_ssfs2ebod65fl[38] = "` does not apply to complex constants";
const uint8_t gm_ssgn9mppmu3vl[16] = "index unchecked";
const uint8_t gm_sshhi6fn3vfaa[45] = "`cache` needs a subcommand: `dir` or `clean`";
const uint8_t gm_sshi9ffc6nkc4[10] = "is unused";
const uint8_t gm_ssi4rrma2q00g[7] = "static";
const uint8_t gm_ssi95ri2h4d02[28] = "integer overflow in `pow` (";
const uint8_t gm_ssici0eo813ko[17] = "timed out after ";
const uint8_t gm_ssmr7jgj9e42t[25] = "Free Software Foundation";
const uint8_t gm_ssoik29mo3fp0[53] = "{} {}; bool {} = __builtin_{}_overflow({}, {}, &{});";
const uint8_t gm_ssr91slt3ah1c[11] = "(uint8_t)(";
const uint8_t gm_ssrd6bs7gcs4g[29] = "expected `{}`, found {} {}{}";
const uint8_t gm_sstppps49t867[11] = "lane count";
const uint8_t gm_sstqe1228ic6k[51] = "`{}` is not a C-compatible parameter type (\302\24719.1)";
const uint8_t gm_ssvci9220kuoj[45] = "internal error: a shuffle index out of range";
const uint8_t gm_ssvcneuafnfds[8] = "changes";
const uint8_t gm_st0jtdoe1b7a6[15] = "unknown name `";
const uint8_t gm_st28rv7m1lueb[9] = "invalid ";
const uint8_t gm_st3m5iduoqh8b[5] = "deps";
const uint8_t gm_st3rnlu97af0r[63] = "`@{}` writes through the pointer, so it needs a `^T`, not `{}`";
const uint8_t gm_st4r3sao0kja5[6] = "bswap";
const uint8_t gm_st53sho4849cg[18] = "kind = GM_K_FLOAT";
const uint8_t gm_st62vtcrpam9i[17] = "the handler of `";
const uint8_t gm_st6ksn77egld4[13] = "variant_name";
const uint8_t gm_stadskj9k22h7[30] = ": an element cannot be `null`";
const uint8_t gm_stcd71a4feia0[10] = "cell_addr";
const uint8_t gm_std877jr2ed05[29] = "unclosed table, expected `]`";
const uint8_t gm_stddp43pjd9r1[51] = " a, const char *file, uint32_t line, uint32_t col)";
const uint8_t gm_sterslo87taeu[39] = "never use the network for dependencies";
const uint8_t gm_stfg6elt037dh[6] = "trap ";
const uint8_t gm_stfp83525k3nr[7] = "little";
const uint8_t gm_stj45lnsahel3[30] = " __attribute__((aligned({})))";
const uint8_t gm_stlojc09k1lf1[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\330\001\000\000\035\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_stmgel74lrtrn[33] = "global variables at compile time";
const uint8_t gm_stn52kuiq0jk2[12] = "Unreachable";
const uint8_t gm_stoidbi3vfa99[30] = "cannot convert `{}` to `{}`{}";
const uint8_t gm_stqlepc346jgk[85] = " deep; does a generic function call itself with ever-larger type arguments\077 (\302\24716.7)";
const uint8_t gm_stum5f38vfqa2[15] = "content-length";
const uint8_t gm_stvja3baka61k[8] = "gm_sink";
const uint8_t gm_su20i5iji4ao3[4] = "a{}";
const uint8_t gm_su38t05utsk53[67] = "explicit tag values need an explicit tag type: `union(u8) { \342\200\246 }`";
const uint8_t gm_su3ahsl9p584f[9] = "IsFinite";
const uint8_t gm_su41v0g030vmk[95] = "`{}` is a symbol of this implementation's runtime, so it cannot name a foreign member (\302\24723.5)";
const uint8_t gm_suc581b27el81[166] = "gm_node {} = {{.eff = {}, .kind = GM_DECL, .clauses = {}, .env = (void *)&{}, .hctx = {}, .with_frame = &F.h, .with_label = {}, .result = {}, .fw = {}, .next = {}}};";
const uint8_t gm_suf3bl270p4gs[56] = "if (GM_LD(uintptr_t, (const uint8_t *){} + {}) != 0) {{";
const uint8_t gm_sufp89bevlq87[5] = "libs";
const uint8_t gm_suhlp72acbph4[43] = "a math vector has 2, 3 or 4 elements, not ";
const uint8_t gm_suia0naoru7mp[7] = "AcqRel";
const uint8_t gm_sukj4damah3dd[85] = "parameter {} is `{}` in one and `{}` in the other; only pointers convert to pointers";
const uint8_t gm_sulvvqvd5gcql[15] = "a handler of `";
const uint8_t gm_sumvvvaqn43sl[10] = ", which `";
const uint8_t gm_sut7mhrkhqp77[56] = "a pointer offset must be a `usize` or an `isize`, not `";
const uint8_t gm_suup20cg9lo2d[8] = " = *f; ";
const uint8_t gm_suvsl6ie8luas[15] = " in the cache\012";
const uint8_t gm_sv0odl408mcff[24] = "` leaves the repository";
const uint8_t gm_sv1bu4l4kq188[8] = "fs_seek";
const uint8_t gm_sv52j7o6hr9j8[10] = "Constants";
const uint8_t gm_sv56kn0notafb[17] = "complex constant";
const uint8_t gm_sv6dkmq38siqt[27] = "{}((double){}, (double){})";
const uint8_t gm_sv7tbc2gvmpa1[100] = "    long which = gm_test_index(argc, argv);\012    if (which < 0) return 2;\012    gm_set_args(1, argv);\012";
const uint8_t gm_sv8pauo1bhltl[9] = "lanes_le";
const uint8_t gm_sv96uhaheg6dq[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\031\001\000\000D\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_svdcmt7tps0ma[57] = "`--timeout` needs a positive number of seconds, not `{}`";
const uint8_t gm_sve2u8a7eapjo[18] = "typedef struct { ";
const uint8_t gm_svirf1u8t89n0[47] = "internal error: formatting would change token ";
const uint8_t gm_svl0gta90bsdr[49] = "`in` needs a range, array, slice or tuple, not `";
const uint8_t gm_svmt0iljnadus[4] = "t{}";
const uint8_t gm_svpr02j6er8vd[15] = "__builtin_fmaf";
const uint8_t gm_svsf0pu3t8u6m[7] = "write ";
uint8_t gm_c0utunn6c58[23] __attribute__((aligned(1)));
uint8_t gm_c1a92ugfgjv[112] __attribute__((aligned(8)));
uint8_t gm_c26gi09a298[63] __attribute__((aligned(1)));
uint8_t gm_c2elhq7l2vf[4] __attribute__((aligned(1)));
uint8_t gm_c3qosqjl81k[8] __attribute__((aligned(8)));
uint8_t gm_c3t36dlh4g5[16] __attribute__((aligned(8)));
uint8_t gm_c4tkno4ihu4[3] __attribute__((aligned(1)));
uint8_t gm_c5fueo3c6mt[14] __attribute__((aligned(1)));
uint8_t gm_c653lc0efgo[3] __attribute__((aligned(1)));
uint8_t gm_c6d5506jf7q[8] __attribute__((aligned(8)));
uint8_t gm_c6ojdgkpe75[3] __attribute__((aligned(1)));
uint8_t gm_c7mc3uaor1r[80] __attribute__((aligned(8)));
uint8_t gm_c7p3n1736t1[4] __attribute__((aligned(4)));
uint8_t gm_c89882kont8[14] __attribute__((aligned(1)));
uint8_t gm_c9b5effiaau[60] __attribute__((aligned(1)));
uint8_t gm_c9gbtic0o97[5] __attribute__((aligned(1)));
uint8_t gm_cai164r033u[16] __attribute__((aligned(8)));
uint8_t gm_cbapkrq7q1c[35] __attribute__((aligned(1)));
uint8_t gm_cbm4eknr5tg[1] __attribute__((aligned(1)));
uint8_t gm_cbr9gd2rl9n[13] __attribute__((aligned(1)));
uint8_t gm_cc9ochdo4cn[8] __attribute__((aligned(1)));
uint8_t gm_ceq0vig573u[10] __attribute__((aligned(1)));
uint8_t gm_cev3sfhfj2p[48] __attribute__((aligned(8)));
uint8_t gm_cf5loitcbc7[13] __attribute__((aligned(1)));
uint8_t gm_cf6m50d8ln4[9] __attribute__((aligned(1)));
uint8_t gm_cfv7g68ca5r[8] __attribute__((aligned(1)));
uint8_t gm_chkob26flgg[9] __attribute__((aligned(1)));
uint8_t gm_chnrragfrjp[13] __attribute__((aligned(1)));
uint8_t gm_ci5i3g6ghat[6] __attribute__((aligned(1)));
uint8_t gm_cigdn81ldc9[16] __attribute__((aligned(8)));
uint8_t gm_cjnuje66084[21] __attribute__((aligned(1)));
uint8_t gm_cka4oohkshv[512] __attribute__((aligned(8)));
uint8_t gm_ckahn0ehf27[1] __attribute__((aligned(8)));
uint8_t gm_ckhlqd9sde7[4] __attribute__((aligned(1)));
uint8_t gm_cknsh3g32om[19] __attribute__((aligned(1)));
uint8_t gm_ckpkn5umvr0[1] __attribute__((aligned(1)));
uint8_t gm_clps76qnh98[3] __attribute__((aligned(1)));
uint8_t gm_cm2dkubhtop[8] __attribute__((aligned(8)));
uint8_t gm_cmumimn4u17[6] __attribute__((aligned(1)));
uint8_t gm_cnaolbe83uo[15] __attribute__((aligned(1)));
uint8_t gm_cpk359ca82u[26] __attribute__((aligned(1)));
uint8_t gm_cpvard348ed[4] __attribute__((aligned(4)));
uint8_t gm_cq3tl31s253[2] __attribute__((aligned(1)));
uint8_t gm_cq8o894hglj[7] __attribute__((aligned(1)));
uint8_t gm_cqb5jrucdqt[11] __attribute__((aligned(1)));
uint8_t gm_cro0h2panct[24] __attribute__((aligned(8)));
uint8_t gm_css0is294pa[5] __attribute__((aligned(1)));
uint8_t gm_csvndp98166[11] __attribute__((aligned(1)));
uint8_t gm_ctngbflld19[16] __attribute__((aligned(8)));
uint8_t gm_ctsgq16sumf[3] __attribute__((aligned(1)));
uint8_t gm_cu7epimqni2[3] __attribute__((aligned(1)));
uint8_t gm_culvhpe56id[10] __attribute__((aligned(1)));
uint8_t gm_cur6jdbgb9n[7] __attribute__((aligned(1)));
uint8_t gm_curvqf1hf03[98] __attribute__((aligned(1)));
uint8_t gm_cvkm788lqpo[17] __attribute__((aligned(1)));
uint8_t gm_k34avs9rqsl[40] __attribute__((aligned(8)));
uint8_t gm_kb94e40l0br[40] __attribute__((aligned(8)));
uint8_t gm_kbri4nir86d[40] __attribute__((aligned(8)));
uint8_t gm_keokac8bchm[40] __attribute__((aligned(8)));
uint8_t gm_kg028u69smr[40] __attribute__((aligned(8)));
uint8_t gm_kh7445i0ed8[40] __attribute__((aligned(8)));
uint8_t gm_khvqu09sagr[40] __attribute__((aligned(8)));
uint8_t gm_kieqgrgghc5[40] __attribute__((aligned(8)));
uint8_t gm_kl5pit792cd[40] __attribute__((aligned(8)));
uint8_t gm_knhk77btln5[40] __attribute__((aligned(8)));
uint8_t gm_ko7k0ku4679[40] __attribute__((aligned(8)));
uint8_t gm_kr1pd6n5fr4[40] __attribute__((aligned(8)));
uint8_t gm_ks263t40fiu[40] __attribute__((aligned(8)));
uint8_t gm_ktvkv2rtjbj[40] __attribute__((aligned(8)));
uint8_t gm_ku0iiu0o88g[40] __attribute__((aligned(8)));
uint8_t gm_kutnfkdjl3d[40] __attribute__((aligned(8)));
uint8_t gm_kv6tql7n7ah[40] __attribute__((aligned(8)));
void *const gm_hd3ip07hju0a[] = {(void *)gm_dt3ip07hju0a_0, (void *)gm_dt3ip07hju0a_1, (void *)gm_dt3ip07hju0a_2, (void *)gm_dt3ip07hju0a_3, (void *)gm_dt3ip07hju0a_4, (void *)gm_dt3ip07hju0a_5};
void *const gm_hdaqemfrshlp[] = {(void *)gm_dtaqemfrshlp_0, (void *)gm_dtaqemfrshlp_1, (void *)gm_dtaqemfrshlp_2, (void *)gm_dtaqemfrshlp_3, (void *)gm_dtaqemfrshlp_4, (void *)gm_dtaqemfrshlp_5, (void *)gm_dtaqemfrshlp_6};
void *const gm_hl5p278go8bp[] = {(void *)gm_f_for_body_6ji90d8};
void *const gm_hle6hel8vh71[] = {(void *)gm_f_for_body_fnpff4s};
void *const gm_hlh176qr0nti[] = {(void *)gm_f_for_body_nnvr1n3};
void *const gm_hlqklhf369l0[] = {(void *)gm_f_for_body_ghum27l};

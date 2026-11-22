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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; union { uint8_t v0; uint8_t v1; } f1; } gm_t5sj3u8e14oco;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; } gm_t0am46iohlpol;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t f2; uint8_t pe[3]; } gm_t16on0i8enr1i;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t f2; uint8_t p3[6]; gm_b16_8 f3; void * f4; void * f5; gm_tl874t2atnlh3 f6; uint8_t pe[4]; } gm_t24301fas1b2a;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { gm_t5sj3u8e14oco f0; uint8_t f1; } gm_t5hib78lmj2mf;
_Static_assert(sizeof(gm_t5sj3u8e14oco) == 2 && _Alignof(gm_t5sj3u8e14oco) == 1 && offsetof(gm_t5sj3u8e14oco, f0) == 0 && offsetof(gm_t5sj3u8e14oco, f1) == 1, "layout of `Shape`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t f2; uint8_t p3[6]; void * f3; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tf2orqcrphmek;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
typedef struct { gm_b16_8 f0; void * f1; void * f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tndsf0a4f4lj0;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0am46iohlpol) == 2 && _Alignof(gm_t0am46iohlpol) == 1 && offsetof(gm_t0am46iohlpol, f0) == 0 && offsetof(gm_t0am46iohlpol, f1) == 1, "layout of `(Command, Command)`");
typedef struct { gm_tbgd14esf0jvi a[2]; } gm_t0vh9i8m50f41;
_Static_assert(sizeof(gm_t16on0i8enr1i) == 32 && _Alignof(gm_t16on0i8enr1i) == 8 && offsetof(gm_t16on0i8enr1i, f0) == 0 && offsetof(gm_t16on0i8enr1i, f1) == 16 && offsetof(gm_t16on0i8enr1i, f2) == 28, "layout of `LabelUse`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t24301fas1b2a) == 72 && _Alignof(gm_t24301fas1b2a) == 8 && offsetof(gm_t24301fas1b2a, f0) == 0 && offsetof(gm_t24301fas1b2a, f1) == 16 && offsetof(gm_t24301fas1b2a, f2) == 17 && offsetof(gm_t24301fas1b2a, f3) == 24 && offsetof(gm_t24301fas1b2a, f4) == 40 && offsetof(gm_t24301fas1b2a, f5) == 48 && offsetof(gm_t24301fas1b2a, f6) == 56, "layout of `FieldDecl`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t9qkh0rsooc8d v1; void * v2; void * v3; gm_tibupt4dmcqai v4; gm_b16_8 v5; gm_b16_8 v6; gm_t9qkh0rsooc8d v7; } f1; } gm_t3fcbuehh3bs2;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5hib78lmj2mf) == 3 && _Alignof(gm_t5hib78lmj2mf) == 1 && offsetof(gm_t5hib78lmj2mf, f0) == 0 && offsetof(gm_t5hib78lmj2mf, f1) == 2, "layout of `?Shape`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
typedef struct { void * f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t7bjbmukv26e7;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { gm_t08olq97rscn3 f0; gm_b16_8 f1; void * f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t99vmhfllumbh;
typedef struct { gm_ts6oqppjh0ba3 f0; gm_ts6oqppjh0ba3 f1; } gm_t9ldr1q5hn9fq;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { void * f0; gm_th2lqbo77pvsc f1; } gm_tagc77imt2chm;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t79rbdv4rafe5 v0; void * v1; } f1; } gm_tapdr2ag17fh7;
typedef struct { uint8_t f0; uint8_t f1; uint8_t p2[2]; gm_toceffb697p0u f2; uint8_t f3; uint8_t f4; uint8_t f5; uint8_t pe[1]; } gm_tb9j2bleq9j8f;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
typedef struct { gm_t08olq97rscn3 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; void * f3; gm_tl874t2atnlh3 f4; uint8_t pe[4]; } gm_tc0sbs1tp6d7f;
typedef struct { gm_tibupt4dmcqai f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; gm_t79rbdv4rafe5 f2; uint8_t f3; uint8_t pe[7]; } gm_tcfiotghd1s5k;
typedef struct { gm_t08olq97rscn3 f0; gm_t79rbdv4rafe5 f1; } gm_tcuumjkg5eni6;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tf2orqcrphmek) == 56 && _Alignof(gm_tf2orqcrphmek) == 8 && offsetof(gm_tf2orqcrphmek, f0) == 0 && offsetof(gm_tf2orqcrphmek, f1) == 16 && offsetof(gm_tf2orqcrphmek, f2) == 17 && offsetof(gm_tf2orqcrphmek, f3) == 24 && offsetof(gm_tf2orqcrphmek, f4) == 32 && offsetof(gm_tf2orqcrphmek, f5) == 40, "layout of `Param`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { gm_trqfvmjtt4sij f0; gm_t4c7ipjj9s3fp f1; gm_trqfvmjtt4sij f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; } gm_tg0vmr665gcag;
typedef struct { gm_t16on0i8enr1i f0; uint8_t f1; uint8_t pe[7]; } gm_tgqc207d6ne28;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
typedef struct { uint8_t f0; union { gm_ts6oqppjh0ba3 v0; } f1; } gm_tjsp7bhdn2e8j;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tkacitepg5fk0;
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tndsf0a4f4lj0) == 48 && _Alignof(gm_tndsf0a4f4lj0) == 8 && offsetof(gm_tndsf0a4f4lj0, f0) == 0 && offsetof(gm_tndsf0a4f4lj0, f1) == 16 && offsetof(gm_tndsf0a4f4lj0, f2) == 24 && offsetof(gm_tndsf0a4f4lj0, f3) == 32, "layout of `Arm`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; void * f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tnljbma3s3h16;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_tbgd14esf0jvi f0; uint8_t f1; uint8_t pe[7]; } gm_tnvnsr1bfvtbd;
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; } gm_ts686ckv8ehm2;
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_trqfvmjtt4sij f1; } gm_tsu0mic7gtnv6;
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t f1; } gm_tt7vn9ehj0acr;
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
typedef struct { gm_ts6oqppjh0ba3 f0; gm_ts6oqppjh0ba3 f1; gm_ts6oqppjh0ba3 f2; gm_ts6oqppjh0ba3 f3; gm_ts6oqppjh0ba3 f4; gm_ts6oqppjh0ba3 f5; gm_ts6oqppjh0ba3 f6; gm_ts6oqppjh0ba3 f7; } gm_tv78bma7gi894;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tnljbma3s3h16 v0; gm_t7bjbmukv26e7 v1; } f1; } gm_t03a3imodqi32;
_Static_assert(sizeof(gm_t0vh9i8m50f41) == 64 && _Alignof(gm_t0vh9i8m50f41) == 8 && offsetof(gm_t0vh9i8m50f41, a) == 0, "layout of `[2]Edit`");
typedef struct { void * f0; gm_tg0vmr665gcag f1; gm_trqfvmjtt4sij f2; gm_b16_8 f3; } gm_t1og5k0mlcjk3;
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t3fcbuehh3bs2) == 32 && _Alignof(gm_t3fcbuehh3bs2) == 8 && offsetof(gm_t3fcbuehh3bs2, f0) == 0 && offsetof(gm_t3fcbuehh3bs2, f1) == 8, "layout of `TokenKind`");
typedef struct { gm_t1s75i08qtptd f0; gm_t08olq97rscn3 f1; gm_t79rbdv4rafe5 f2; } gm_t43d6nbdfap6p;
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { gm_tnvnsr1bfvtbd f0; } gm_t5jnq73fben73;
_Static_assert(sizeof(gm_t7bjbmukv26e7) == 64 && _Alignof(gm_t7bjbmukv26e7) == 8 && offsetof(gm_t7bjbmukv26e7, f0) == 0 && offsetof(gm_t7bjbmukv26e7, f1) == 8 && offsetof(gm_t7bjbmukv26e7, f2) == 24 && offsetof(gm_t7bjbmukv26e7, f3) == 48, "layout of `WhenItems`");
_Static_assert(sizeof(gm_t99vmhfllumbh) == 72 && _Alignof(gm_t99vmhfllumbh) == 8 && offsetof(gm_t99vmhfllumbh, f0) == 0 && offsetof(gm_t99vmhfllumbh, f1) == 32 && offsetof(gm_t99vmhfllumbh, f2) == 48 && offsetof(gm_t99vmhfllumbh, f3) == 56, "layout of `Clause`");
_Static_assert(sizeof(gm_t9ldr1q5hn9fq) == 4 && _Alignof(gm_t9ldr1q5hn9fq) == 1 && offsetof(gm_t9ldr1q5hn9fq, f0) == 0 && offsetof(gm_t9ldr1q5hn9fq, f1) == 2, "layout of `(BinOp, BinOp)`");
_Static_assert(sizeof(gm_tagc77imt2chm) == 32 && _Alignof(gm_tagc77imt2chm) == 8 && offsetof(gm_tagc77imt2chm, f0) == 0 && offsetof(gm_tagc77imt2chm, f1) == 8, "layout of `PathPattern`");
_Static_assert(sizeof(gm_tapdr2ag17fh7) == 48 && _Alignof(gm_tapdr2ag17fh7) == 8 && offsetof(gm_tapdr2ag17fh7, f0) == 0 && offsetof(gm_tapdr2ag17fh7, f1) == 8, "layout of `WhenElse`");
_Static_assert(sizeof(gm_tb9j2bleq9j8f) == 24 && _Alignof(gm_tb9j2bleq9j8f) == 4 && offsetof(gm_tb9j2bleq9j8f, f0) == 0 && offsetof(gm_tb9j2bleq9j8f, f1) == 1 && offsetof(gm_tb9j2bleq9j8f, f2) == 4 && offsetof(gm_tb9j2bleq9j8f, f3) == 20 && offsetof(gm_tb9j2bleq9j8f, f4) == 21 && offsetof(gm_tb9j2bleq9j8f, f5) == 22, "layout of `(mutable: bool, typed: bool, op: ?Span, keep_mutable: bool, fn_lit: bool, uninit: bool)`");
_Static_assert(sizeof(gm_tc0sbs1tp6d7f) == 80 && _Alignof(gm_tc0sbs1tp6d7f) == 8 && offsetof(gm_tc0sbs1tp6d7f, f0) == 0 && offsetof(gm_tc0sbs1tp6d7f, f1) == 32 && offsetof(gm_tc0sbs1tp6d7f, f2) == 40 && offsetof(gm_tc0sbs1tp6d7f, f3) == 56 && offsetof(gm_tc0sbs1tp6d7f, f4) == 64, "layout of `OpDecl`");
_Static_assert(sizeof(gm_tcfiotghd1s5k) == 88 && _Alignof(gm_tcfiotghd1s5k) == 8 && offsetof(gm_tcfiotghd1s5k, f0) == 0 && offsetof(gm_tcfiotghd1s5k, f1) == 24 && offsetof(gm_tcfiotghd1s5k, f2) == 40 && offsetof(gm_tcfiotghd1s5k, f3) == 80, "layout of `TestDecl`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tcuumjkg5eni6) == 72 && _Alignof(gm_tcuumjkg5eni6) == 8 && offsetof(gm_tcuumjkg5eni6, f0) == 0 && offsetof(gm_tcuumjkg5eni6, f1) == 32, "layout of `IfBranch`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t24301fas1b2a v0; gm_t7bjbmukv26e7 v1; } f1; } gm_tfc3oljlofib6;
_Static_assert(sizeof(gm_tg0vmr665gcag) == 216 && _Alignof(gm_tg0vmr665gcag) == 8 && offsetof(gm_tg0vmr665gcag, f0) == 0 && offsetof(gm_tg0vmr665gcag, f1) == 40 && offsetof(gm_tg0vmr665gcag, f2) == 96 && offsetof(gm_tg0vmr665gcag, f3) == 136 && offsetof(gm_tg0vmr665gcag, f4) == 176, "layout of `FileFacts`");
_Static_assert(sizeof(gm_tgqc207d6ne28) == 40 && _Alignof(gm_tgqc207d6ne28) == 8 && offsetof(gm_tgqc207d6ne28, f0) == 0 && offsetof(gm_tgqc207d6ne28, f1) == 32, "layout of `?LabelUse`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tjsp7bhdn2e8j) == 3 && _Alignof(gm_tjsp7bhdn2e8j) == 1 && offsetof(gm_tjsp7bhdn2e8j, f0) == 0 && offsetof(gm_tjsp7bhdn2e8j, f1) == 1, "layout of `AssignOp`");
_Static_assert(sizeof(gm_tkacitepg5fk0) == 72 && _Alignof(gm_tkacitepg5fk0) == 8 && offsetof(gm_tkacitepg5fk0, f0) == 0 && offsetof(gm_tkacitepg5fk0, f1) == 32 && offsetof(gm_tkacitepg5fk0, f2) == 56, "layout of `Attribute`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tm4pc0emhrv1g;
_Static_assert(sizeof(gm_tnljbma3s3h16) == 64 && _Alignof(gm_tnljbma3s3h16) == 8 && offsetof(gm_tnljbma3s3h16, f0) == 0 && offsetof(gm_tnljbma3s3h16, f1) == 32 && offsetof(gm_tnljbma3s3h16, f2) == 40 && offsetof(gm_tnljbma3s3h16, f3) == 48, "layout of `Variant`");
_Static_assert(sizeof(gm_tnvnsr1bfvtbd) == 40 && _Alignof(gm_tnvnsr1bfvtbd) == 8 && offsetof(gm_tnvnsr1bfvtbd, f0) == 0 && offsetof(gm_tnvnsr1bfvtbd, f1) == 32, "layout of `?(span: Span, text: string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_t08olq97rscn3 v1; void * v2; gm_tlcl4u558l6p5 v3; void * v4; gm_tagc77imt2chm v5; gm_b16_8 v6; } f1; } gm_to361lgknsu69;
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_ts686ckv8ehm2) == 40 && _Alignof(gm_ts686ckv8ehm2) == 8 && offsetof(gm_ts686ckv8ehm2, f0) == 0 && offsetof(gm_ts686ckv8ehm2, f1) == 32, "layout of `(name: Ident, ty: ^Expr)`");
_Static_assert(sizeof(gm_tsu0mic7gtnv6) == 56 && _Alignof(gm_tsu0mic7gtnv6) == 8 && offsetof(gm_tsu0mic7gtnv6, f0) == 0 && offsetof(gm_tsu0mic7gtnv6, f1) == 16, "layout of `Allow`");
_Static_assert(sizeof(gm_tt7vn9ehj0acr) == 3 && _Alignof(gm_tt7vn9ehj0acr) == 1 && offsetof(gm_tt7vn9ehj0acr, f0) == 0 && offsetof(gm_tt7vn9ehj0acr, f1) == 2, "layout of `?BinOp`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t79rbdv4rafe5 v0; gm_ts686ckv8ehm2 v1; } f1; } gm_ttja7ii61nila;
_Static_assert(sizeof(gm_tv78bma7gi894) == 16 && _Alignof(gm_tv78bma7gi894) == 1 && offsetof(gm_tv78bma7gi894, f0) == 0 && offsetof(gm_tv78bma7gi894, f1) == 2 && offsetof(gm_tv78bma7gi894, f2) == 4 && offsetof(gm_tv78bma7gi894, f3) == 6 && offsetof(gm_tv78bma7gi894, f4) == 8 && offsetof(gm_tv78bma7gi894, f5) == 10 && offsetof(gm_tv78bma7gi894, f6) == 12 && offsetof(gm_tv78bma7gi894, f7) == 14, "layout of `(BinOp, BinOp, BinOp, BinOp, BinOp, BinOp, BinOp, BinOp)`");
_Static_assert(sizeof(gm_t03a3imodqi32) == 72 && _Alignof(gm_t03a3imodqi32) == 8 && offsetof(gm_t03a3imodqi32, f0) == 0 && offsetof(gm_t03a3imodqi32, f1) == 8, "layout of `UnionMember`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_to361lgknsu69 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t0d02142k4tu8;
_Static_assert(sizeof(gm_t1og5k0mlcjk3) == 280 && _Alignof(gm_t1og5k0mlcjk3) == 8 && offsetof(gm_t1og5k0mlcjk3, f0) == 0 && offsetof(gm_t1og5k0mlcjk3, f1) == 8 && offsetof(gm_t1og5k0mlcjk3, f2) == 224 && offsetof(gm_t1og5k0mlcjk3, f3) == 264, "layout of `Walk`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t43d6nbdfap6p) == 112 && _Alignof(gm_t43d6nbdfap6p) == 8 && offsetof(gm_t43d6nbdfap6p, f0) == 0 && offsetof(gm_t43d6nbdfap6p, f1) == 40 && offsetof(gm_t43d6nbdfap6p, f2) == 72, "layout of `WhileStmt`");
_Static_assert(sizeof(gm_t5jnq73fben73) == 40 && _Alignof(gm_t5jnq73fben73) == 8 && offsetof(gm_t5jnq73fben73, f0) == 0, "layout of `(fix: ?(at: Span, to: string))`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tfc3oljlofib6) == 80 && _Alignof(gm_tfc3oljlofib6) == 8 && offsetof(gm_tfc3oljlofib6, f0) == 0 && offsetof(gm_tfc3oljlofib6, f1) == 8, "layout of `StructMember`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tb9j2bleq9j8f v0; gm_tbgd14esf0jvi v1; gm_t5jnq73fben73 v2; } f1; } gm_tl5vm0tcqvdvb;
_Static_assert(sizeof(gm_tm4pc0emhrv1g) == 48 && _Alignof(gm_tm4pc0emhrv1g) == 8 && offsetof(gm_tm4pc0emhrv1g, f0) == 0 && offsetof(gm_tm4pc0emhrv1g, f1) == 32, "layout of `Token`");
_Static_assert(sizeof(gm_to361lgknsu69) == 40 && _Alignof(gm_to361lgknsu69) == 8 && offsetof(gm_to361lgknsu69, f0) == 0 && offsetof(gm_to361lgknsu69, f1) == 8, "layout of `PatternKind`");
_Static_assert(sizeof(gm_ttja7ii61nila) == 48 && _Alignof(gm_ttja7ii61nila) == 8 && offsetof(gm_ttja7ii61nila, f0) == 0 && offsetof(gm_ttja7ii61nila, f1) == 8, "layout of `IfElse`");
_Static_assert(sizeof(gm_t0d02142k4tu8) == 64 && _Alignof(gm_t0d02142k4tu8) == 8 && offsetof(gm_t0d02142k4tu8, f0) == 0 && offsetof(gm_t0d02142k4tu8, f1) == 8 && offsetof(gm_t0d02142k4tu8, f2) == 48, "layout of `Pattern`");
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_tl5vm0tcqvdvb) == 48 && _Alignof(gm_tl5vm0tcqvdvb) == 8 && offsetof(gm_tl5vm0tcqvdvb, f0) == 0 && offsetof(gm_tl5vm0tcqvdvb, f1) == 8, "layout of `Site`");
typedef struct { gm_t0d02142k4tu8 f0; void * f1; } gm_tuqaojgkqdcop;
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuqaojgkqdcop v0; void * v1; } f1; } gm_tq5j0547p8452;
_Static_assert(sizeof(gm_tuqaojgkqdcop) == 72 && _Alignof(gm_tuqaojgkqdcop) == 8 && offsetof(gm_tuqaojgkqdcop, f0) == 0 && offsetof(gm_tuqaojgkqdcop, f1) == 64, "layout of `(pat: Pattern, value: ^Expr)`");
typedef struct { gm_t1s75i08qtptd f0; gm_tc3oht4ksth9d f1; uint8_t f2; uint8_t pe[7]; } gm_tv1o1j44ii4db;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_tq5j0547p8452) == 80 && _Alignof(gm_tq5j0547p8452) == 8 && offsetof(gm_tq5j0547p8452, f0) == 0 && offsetof(gm_tq5j0547p8452, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tv1o1j44ii4db) == 136 && _Alignof(gm_tv1o1j44ii4db) == 8 && offsetof(gm_tv1o1j44ii4db, f0) == 0 && offsetof(gm_tv1o1j44ii4db, f1) == 40 && offsetof(gm_tv1o1j44ii4db, f2) == 128, "layout of `TupleElem`");
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
struct gm_fr_accepts_3h4b27e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_add_note_46v502s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_binding_op_gcp8f8i {
    gm_frame h;
    gm_toceffb697p0u ret;
};
struct gm_fr_binding_stmt_80l1chq {
    gm_frame h;
    gm_tl5vm0tcqvdvb l10; /* site */
    gm_b0_1 ret;
};
struct gm_fr_blank_ineorlq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_block_btrailr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_capture_abs7k4r {
    gm_frame h;
    gm_tnvnsr1bfvtbd l4; /* fix */
    gm_b0_1 ret;
};
struct gm_fr_check_name_hf7dide {
    gm_frame h;
    gm_t4gut2c9fj1kt l6; /* d */
    gm_b0_1 ret;
};
struct gm_fr_collapsible_else_ehhjtii {
    gm_frame h;
    gm_ttja7ii61nila l2; /* els */
    gm_t79rbdv4rafe5 l3; /* b */
    gm_t4gut2c9fj1kt l5; /* d */
    gm_b0_1 ret;
};
struct gm_fr_cond_e0272rd {
    gm_frame h;
    gm_tq5j0547p8452 l5; /* part */
    gm_t0d02142k4tu8 l6; /* pat */
    gm_b0_1 ret;
};
struct gm_fr_contains_57n7jqd {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_convert_c7mdt88 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
    gm_b0_1 w0;
};
struct gm_fr_declared_kind_4vgnqmf {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_dedent_aa5vbel {
    gm_frame h;
    uint64_t l3; /* shift */
    gm_trqfvmjtt4sij l4; /* out */
    uint8_t l5; /* first */
    gm_b16_8 ret;
    gm_b0_1 w0;
};
struct gm_fr_empty_branches_7o4oudt {
    gm_frame h;
    gm_ttja7ii61nila l3; /* els */
    gm_t79rbdv4rafe5 l4; /* b */
    gm_t4gut2c9fj1kt l7; /* d */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_1th396a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_9f3suev {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d53jqcq {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jbu9fo3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_kqiv2v5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_n5n7pq1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qbupbr6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_r81t436 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_r9g5q6n {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_u4e74ha {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_expr_3limf78 {
    gm_frame h;
    void * l0; /* w */
    gm_t4gut2c9fj1kt l9; /* d */
    gm_tioi3jh9v22g5 l10; /* x */
    gm_t99vmhfllumbh l17; /* c */
    gm_tndsf0a4f4lj0 l22; /* arm */
    gm_t79rbdv4rafe5 l36; /* b */
    gm_tchr52g0ema53 l39; /* b */
    gm_t1s75i08qtptd l41; /* label */
    gm_b0_1 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_fields_8ttf3eb {
    gm_frame h;
    gm_tfc3oljlofib6 l4; /* m */
    gm_t24301fas1b2a l5; /* f */
    gm_t7bjbmukv26e7 l9; /* wh */
    gm_b0_1 ret;
};
struct gm_fr_file_facts_tdo30n8 {
    gm_frame h;
    gm_tg0vmr665gcag ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_fn_c8tpla6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_lit_hd5tsqt {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* labels */
    gm_t79rbdv4rafe5 l3; /* b */
    gm_b0_1 ret;
};
struct gm_fr_for_body_1063u09 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_8jjs01b {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_cbj68cn {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_1vkmnih {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4pq08p9 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_8n626pi {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_h13dbf2 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jm2cnr1 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_o2cbrdu {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ooibcq2 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_reauhlh {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t472cpe {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_udg5t2e {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_uiq3pfi {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_has_comment_qa4dr2o {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_hash_map_2auo7cg {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_identical_branches_ug6ccol {
    gm_frame h;
    gm_ttja7ii61nila l2; /* els */
    gm_t79rbdv4rafe5 l3; /* b */
    gm_tcuumjkg5eni6 l7; /* br */
    gm_t4gut2c9fj1kt l9; /* d */
    gm_b0_1 ret;
};
struct gm_fr_if_expr_k16f2n5 {
    gm_frame h;
    gm_tcuumjkg5eni6 l5; /* br */
    gm_tq5j0547p8452 l8; /* part */
    gm_t4gut2c9fj1kt l12; /* d */
    gm_ttja7ii61nila l13; /* els */
    gm_t79rbdv4rafe5 l14; /* b */
    gm_b0_1 ret;
};
struct gm_fr_indent_of_p7r0sg5 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_is_constant_call_vj2jgti {
    gm_frame h;
    gm_t7te2tdpebbt4 l4; /* a */
    gm_tc3oht4ksth9d l5; /* x */
    gm_tc3oht4ksth9d l6; /* x */
    gm_tv1o1j44ii4db l10; /* el */
    uint8_t ret;
};
struct gm_fr_is_literal_9ac2fdt {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_lower_q12rpq9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_space_uen38t5 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_upper_68kn81r {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_item_a3ennq0 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* allowed */
    gm_tkacitepg5fk0 l5; /* a */
    gm_tc3oht4ksth9d l9; /* e */
    gm_tkacitepg5fk0 l13; /* a */
    gm_tc3oht4ksth9d l17; /* e */
    gm_t4gut2c9fj1kt l20; /* d */
    gm_tv2uf4leil5hq l21; /* b */
    gm_t7bjbmukv26e7 l24; /* wh */
    gm_t99vmhfllumbh l35; /* c */
    gm_tcfiotghd1s5k l36; /* t */
    gm_b0_1 ret;
};
struct gm_fr_lint_diag_4vra4qu {
    gm_frame h;
    gm_t4gut2c9fj1kt ret;
};
struct gm_fr_lint_eo1tub5 {
    gm_frame h;
    gm_t4gut2c9fj1kt l3; /* d */
    gm_t4gut2c9fj1kt ret;
};
struct gm_fr_list_7s4om10 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_fu17r8d {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_prbqo9j {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uaa66su {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mentions_4p99unq {
    gm_frame h;
    gm_tm4pc0emhrv1g l9; /* t */
    uint8_t ret;
};
struct gm_fr_names_in_1jbgo4k {
    gm_frame h;
    gm_tm4pc0emhrv1g l5; /* t */
    gm_b0_1 ret;
};
struct gm_fr_needless_bool_mifff69 {
    gm_frame h;
    gm_ttja7ii61nila l3; /* els */
    gm_t79rbdv4rafe5 l4; /* els_block */
    gm_t4gut2c9fj1kt l18; /* d */
    gm_b0_1 ret;
};
struct gm_fr_negation_tmaga6d {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_normalize_space_v7dp7uh {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
    gm_b0_1 w0;
};
struct gm_fr_paren_at_av727r5 {
    gm_frame h;
    gm_t4gut2c9fj1kt l7; /* d */
    gm_t0vh9i8m50f41 l8; /* edits */
    gm_b0_1 ret;
};
struct gm_fr_paren_unless_simple_mhajkgl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_pattern_8qtr7os {
    gm_frame h;
    gm_tl5vm0tcqvdvb l4; /* site */
    gm_tnvnsr1bfvtbd l6; /* fix */
    gm_b0_1 ret;
};
struct gm_fr_pop_h4mkmpb {
    gm_frame h;
    gm_tgqc207d6ne28 ret;
};
struct gm_fr_pop_label_0jfjsf5 {
    gm_frame h;
    gm_t1s75i08qtptd l1; /* label */
    gm_t4gut2c9fj1kt l9; /* d */
    gm_b0_1 ret;
};
struct gm_fr_push_0mk7udh {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_amge2t0 {
    gm_frame h;
    gm_tsu0mic7gtnv6 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_em9e1kh {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_kiclmcn {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_l0e5v4c {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_push_l0rgbjf {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_label_hn6s4vp {
    gm_frame h;
    gm_t1s75i08qtptd l1; /* label */
    gm_b0_1 ret;
};
struct gm_fr_push_t83rg2m {
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
struct gm_fr_regrow_8skjmj4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_kfs9rll {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_r8ccoha {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_usnqfd3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_v0p8ic9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_rule_scmud9h {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_shape_2jg6iuk {
    gm_frame h;
    gm_t5hib78lmj2mf ret;
};
struct gm_fr_shrink_to_hi_5bdve33 {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_sig_fn4opan {
    gm_frame h;
    gm_tf2orqcrphmek l5; /* p */
    gm_b0_1 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_stmt_j3ls0p3 {
    gm_frame h;
    gm_tv2uf4leil5hq l2; /* binding */
    gm_t43d6nbdfap6p l12; /* wl */
    gm_tq5j0547p8452 l15; /* part */
    gm_t0d02142k4tu8 l18; /* pat */
    gm_t79rbdv4rafe5 l21; /* b */
    gm_b0_1 ret;
};
struct gm_fr_text_kspioh6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_top_level_name_dv4kpc9 {
    gm_frame h;
    gm_tkacitepg5fk0 l9; /* a */
    gm_tc0sbs1tp6d7f l15; /* op */
    gm_b0_1 ret;
};
struct gm_fr_unused_local_decl_2l8feim {
    gm_frame h;
    gm_tv2uf4leil5hq l3; /* b */
    gm_t4gut2c9fj1kt l6; /* d */
    gm_b0_1 ret;
};
struct gm_fr_upper_camel_a0jt5mh {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_use_label_8j0qqam {
    gm_frame h;
    gm_t1s75i08qtptd l1; /* label */
    gm_b0_1 ret;
};
struct gm_fr_variants_eef2p79 {
    gm_frame h;
    gm_t03a3imodqi32 l4; /* m */
    gm_tnljbma3s3h16 l5; /* v */
    gm_t7bjbmukv26e7 l6; /* wh */
    gm_b0_1 ret;
};
struct gm_fr_walk_file_do4uq8b {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_walk_tech5q5 {
    gm_frame h;
    gm_t1og5k0mlcjk3 ret;
};
struct gm_fr_warning_q0m5jjs {
    gm_frame h;
    gm_t4gut2c9fj1kt l2; /* d */
    gm_t4gut2c9fj1kt ret;
};
struct gm_fr_when_stmt_qbi9l8v {
    gm_frame h;
    gm_tapdr2ag17fh7 l2; /* els */
    gm_t79rbdv4rafe5 l3; /* b */
    gm_b0_1 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbqb4gb1d92(const uint8_t *a, const uint8_t *b);
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
inline bool gm_eqlv5i3f25jk(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) uint8_t gm_f_accepts_3h4b27e(gm_t9qkh0rsooc8d a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_edits_em0b48a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_binding_op_gcp8f8i(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_binding_stmt_80l1chq(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_binop_text_0ljfc0r(gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) uint8_t gm_f_blank_ineorlq(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_block_btrailr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_capture_abs7k4r(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint8_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_name_hf7dide(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_collapsible_else_ehhjtii(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_cond_e0272rd(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_57n7jqd(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_convert_c7mdt88(void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_declared_kind_4vgnqmf(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_declared_kind_4vgnqmf_c(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_dedent_aa5vbel(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_empty_branches_7o4oudt(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_9f3suev(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d53jqcq(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_kqiv2v5(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n5n7pq1(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qbupbr6(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r9g5q6n(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_u4e74ha(void);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_expr_3limf78(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fields_8ttf3eb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fields_ojgi3et_sq5308c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_file_facts_tdo30n8(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_722uua4(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_c8tpla6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_lit_hd5tsqt(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1063u09(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_8jjs01b(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_cbj68cn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_for_each_child_tkrfoqu(void * a0, gm_closure a1);
inline __attribute__((aligned(4))) void gm_f_format_1vkmnih(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_4pq08p9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_8n626pi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_h13dbf2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_jm2cnr1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_mccr87v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_o2cbrdu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ooibcq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_reauhlh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_t472cpe(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_udg5t2e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_uiq3pfi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_has_comment_qa4dr2o(void * a0, gm_cseahvg8v7p0vs a1);
inline __attribute__((aligned(4))) void gm_f_hash_map_2auo7cg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_identical_branches_ug6ccol(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_if_expr_k16f2n5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) uint64_t gm_f_indent_of_p7r0sg5(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_hdeg351(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) uint8_t gm_f_is_builtin_type_name_v5ttoka(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_is_constant_call_vj2jgti(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_literal_9ac2fdt(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_lower_q12rpq9(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_space_uen38t5(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_upper_68kn81r(uint8_t a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_item_a3ennq0(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_lint_diag_4vra4qu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_lint_eo1tub5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lint_rule_o3ccae1(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_fu17r8d(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_prbqo9j(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uaa66su(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_mentions_4p99unq(void * a0, gm_b16_8 a1, uint32_t a2, uint32_t a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_names_in_1jbgo4k(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_needless_bool_mifff69(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_negation_tmaga6d(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_normalize_space_v7dp7uh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_paren_at_av727r5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, uint8_t a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_paren_unless_simple_mhajkgl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_pattern_8qtr7os(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_h4mkmpb(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_pop_label_0jfjsf5(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_amge2t0(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_em9e1kh(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_l0e5v4c(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_l0rgbjf(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_label_hn6s4vp(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_5c6qgui(void * a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_amueool(void * a0, gm_cseahvg8v7p0vs a1, const void * a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_kfs9rll(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_r8ccoha(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_usnqfd3(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_v0p8ic9(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_rule_scmud9h(gm_b16_8 a0);
__attribute__((aligned(4))) gm_t5hib78lmj2mf gm_f_shape_2jg6iuk(void * a0);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_shrink_to_hi_5bdve33(gm_cseahvg8v7p0vs a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_sig_fn4opan(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_s44sj5s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_s76o7tg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_stmt_j3ls0p3(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_text_kspioh6(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_top_level_name_dv4kpc9(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_179crak(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_unused_local_decl_2l8feim(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2);
inline __attribute__((aligned(4))) uint8_t gm_f_upper_camel_a0jt5mh(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_use_label_8j0qqam(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_use_label_8j0qqam_c(void * a0, gm_t1s75i08qtptd a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_variants_eef2p79(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_walk_file_do4uq8b(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_walk_tech5q5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_warning_q0m5jjs(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_when_stmt_qbi9l8v(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
extern void *const gm_hl7iqllvekj4[];
extern void *const gm_hldg2pcvebta[];
extern void *const gm_hlhb9f1a7omo[];
extern const uint8_t gm_s0nf1rg04f3m5[4];
extern const uint8_t gm_s1hha8fc2kd79[18];
extern const uint8_t gm_s1jboc8lkvac0[6];
extern const uint8_t gm_s1o0cl13ab3oc[18];
extern const uint8_t gm_s2737i1iq57nf[5];
extern const uint8_t gm_s2kg2oqqdqf2r[23];
extern const uint8_t gm_s3i3n5bvpvmmi[16];
extern const uint8_t gm_s5cr5924ev095[65];
extern const uint8_t gm_s5f9u0o3duhns[13];
extern const uint8_t gm_s5gkblcfbl1mv[121];
extern const uint8_t gm_s5pq4t98troao[34];
extern const uint8_t gm_s6mpqtffqhn51[80];
extern const uint8_t gm_s6vg88ce880ha[13];
extern const uint8_t gm_s71j6n6s8up14[17];
extern const uint8_t gm_s816n04ro16nc[48];
extern const uint8_t gm_s8srbeac8og16[29];
extern const uint8_t gm_s91c3em0lmpov[37];
extern const uint8_t gm_s9ln4rnb6talr[4];
extern const uint8_t gm_sbgcsq0m3astj[43];
extern const uint8_t gm_sblt6ctj6g4ck[21];
extern const uint8_t gm_sc06resjcdhg4[19];
extern const uint8_t gm_scd8m51mtmjv9[19];
extern const uint8_t gm_sci6hkonrjrne[19];
extern const uint8_t gm_scmgj2ccom9v4[40];
extern const uint8_t gm_scvmbht29gva9[7];
extern const uint8_t gm_sd1d50j1ea4vf[46];
extern const uint8_t gm_sdtpg69jtnl30[30];
extern const uint8_t gm_seqndsmrihepc[10];
extern const uint8_t gm_sf50oc24o4dhk[11];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sfna4d5mnmoa8[30];
extern const uint8_t gm_sfqk3rro4g7t0[2];
extern const uint8_t gm_sfr3tv65k63d4[16];
extern const uint8_t gm_sg3kpa3h16no3[7];
extern const uint8_t gm_sgbumttiou4de[8];
extern const uint8_t gm_sgrap32255cto[47];
extern const uint8_t gm_sh4r0fq5alj61[14];
extern const uint8_t gm_shfvspt8ngibo[19];
extern const uint8_t gm_shge5n743ua4h[3];
extern const uint8_t gm_shquo2s3jci8a[42];
extern const uint8_t gm_si4j8sh2qika1[3];
extern const uint8_t gm_sium1s93t7o1a[19];
extern const uint8_t gm_sjg1ri23hm753[6];
extern const uint8_t gm_sjuj50mlj6mg0[14];
extern const uint8_t gm_sk6ueggk5ji6v[10];
extern const uint8_t gm_sk754mj3p1el6[2];
extern const uint8_t gm_skgir5knhpsin[9];
extern const uint8_t gm_skr6p9lfc5ftp[11];
extern const uint8_t gm_sl5h9isb3bijl[5];
extern const uint8_t gm_slb6cj7ndgvdm[6];
extern const uint8_t gm_sllrmadmbrcvv[54];
extern const uint8_t gm_sm7emfdvcb7c2[3];
extern const uint8_t gm_sm8obe455p2d9[3];
extern const uint8_t gm_smbbaf3n1gml2[8];
extern const uint8_t gm_smh4q1ndijmcf[39];
extern const uint8_t gm_snh86nvrv0us3[19];
extern const uint8_t gm_snvqfpv571j0h[32];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_so9ldq1i41cj2[19];
extern const uint8_t gm_sob1n7uh7kpps[14];
extern const uint8_t gm_sotb03b2eqtp5[20];
extern const uint8_t gm_spg4ktuno2hf7[51];
extern const uint8_t gm_spic2fa42u00e[19];
extern const uint8_t gm_spj9t9b8lqlpp[8];
extern const uint8_t gm_spn8bpm132g2m[19];
extern const uint8_t gm_sq0dqiq83b924[9];
extern const uint8_t gm_sqit0ai5v3joc[23];
extern const uint8_t gm_sqmr87iegdcns[7];
extern const uint8_t gm_squ601q9vv2so[33];
extern const uint8_t gm_sr1bm2v312fv6[15];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src10tru552hn49[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_srs1vrn2uaams[2];
extern const uint8_t gm_ss67f0gmfen1m[3];
extern const uint8_t gm_ssa2crodel6ih[29];
extern const uint8_t gm_sso47q408qu33[24];
extern const uint8_t gm_suftpl3n8736g[5];
extern const uint8_t gm_suls9doo5jh0i[11];
extern const uint8_t gm_suuv0qpi4v4e4[8];
extern const gm_tmpl gm_tpl386j11hdkrib;
extern const gm_tmpl gm_tpl56vmcuv96cg9;
extern const gm_tmpl gm_tpl59lqltdr0h0r;
extern const gm_tmpl gm_tplei8v65ors6jc;
extern const gm_tmpl gm_tplk6g6bjh5rkat;
extern const gm_tmpl gm_tplld0dt78h0tum;
extern const gm_tmpl gm_tplm16s64mnihe4;
extern const gm_tmpl gm_tplnh359qrng71h;
extern const gm_tmpl gm_tplqfu63ajpo70a;
extern const gm_tmpl gm_tplrd5hip684e7e;
extern const gm_tmpl gm_tplvv610d042tt6;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eqbqb4gb1d92(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)0) return gm_eqehhdtpeti1(a + 1, b + 1);
    if (ta == (uint8_t)1) return gm_eqehhdtpeti1(a + 1, b + 1);
    if (ta == (uint8_t)2) return gm_eqehhdtpeti1(a + 1, b + 1);
    if (ta == (uint8_t)3) return gm_eqehhdtpeti1(a + 1, b + 1);
    return true;
}
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline bool gm_eqlv5i3f25jk(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
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
inline __attribute__((aligned(4))) uint8_t gm_f_contains_57n7jqd(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_57n7jqd F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* k */
    GM_STACK_CHECK(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    gm_tf5srh4mp4b3i gm_l3;
    void * t5 = gm_l0;
    void * t6 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t7 = gm_f_find_722uua4(t5, t6);
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
        gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 173, 12);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1th396a F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 184)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_9f3suev(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_9f3suev F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 56)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d53jqcq(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d53jqcq F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 80)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_kqiv2v5(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_kqiv2v5 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 64)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n5n7pq1(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_n5n7pq1 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qbupbr6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qbupbr6 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r9g5q6n(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_r9g5q6n F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 120)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_u4e74ha(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_u4e74ha F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_c8tpla6(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_c8tpla6 F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_expr_3limf78 *)((&F.h)->up))->l0));
    void * t3 = gm_l0;
    gm_b0_1 t4 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_format_1vkmnih(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_1vkmnih F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_suftpl3n8736g));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplqfu63ajpo70a), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_4pq08p9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_4pq08p9 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s8srbeac8og16));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(28));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl56vmcuv96cg9), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_h13dbf2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_h13dbf2 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2kg2oqqdqf2r));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(22));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplm16s64mnihe4), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_jm2cnr1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_jm2cnr1 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssa2crodel6ih));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(28));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplld0dt78h0tum), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_o2cbrdu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_o2cbrdu F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_skgir5knhpsin));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(8));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl386j11hdkrib), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ooibcq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ooibcq2 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sf50oc24o4dhk));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplk6g6bjh5rkat), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_reauhlh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_reauhlh F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5pq4t98troao));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(33));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl59lqltdr0h0r), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_t472cpe(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_t472cpe F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_squ601q9vv2so));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(32));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplvv610d042tt6), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_udg5t2e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_udg5t2e F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_scvmbht29gva9));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplrd5hip684e7e), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_uiq3pfi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_uiq3pfi F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_snvqfpv571j0h));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(31));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplnh359qrng71h), t2);
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_comment_qa4dr2o(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_comment_qa4dr2o F;
    void * gm_l0; /* w */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK_SHALLOW(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    void * t2 = gm_l0;
    gm_tl874t2atnlh3 t3 = gm_l1;
    gm_b16_8 t4 = gm_f_text_kspioh6(t2, GM_CONV(gm_cseahvg8v7p0vs, t3));
    gm_b16_8 t5;
    GM_ST(void *, (uint8_t *)&t5, (void *)(gm_si4j8sh2qika1));
    GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(2));
    uint8_t t6 = gm_f_contains_gkki4kq(t4, t5);
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_map_2auo7cg(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_2auo7cg F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_kqiv2v5();
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_lower_q12rpq9(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_lower_q12rpq9 F;
    uint8_t gm_l0; /* c */
    uint8_t gm_l1; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 11u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l2;
    uint8_t t2 = gm_l0;
    gm_l1 = t2;
    uint8_t t3 = gm_l1;
    uint8_t t4 = (((uint8_t)97ULL) <= t3);
    if (t4) {
        uint8_t t5 = gm_l1;
        uint8_t t6 = (t5 <= ((uint8_t)122ULL));
        gm_l2 = t6;
    } else {
        gm_l2 = ((uint8_t)0);
    }
    uint8_t t7 = gm_l2;
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_space_uen38t5(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_space_uen38t5 F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 25u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    uint8_t gm_l2;
    uint8_t gm_l3;
    uint8_t gm_l4;
    uint8_t gm_l5;
    uint8_t t2 = gm_l0;
    uint8_t t3 = ((t2 == ((uint8_t)32ULL)));
    if (t3) {
        gm_l5 = ((uint8_t)1);
    } else {
        uint8_t t4 = gm_l0;
        uint8_t t5 = ((t4 == ((uint8_t)9ULL)));
        gm_l5 = t5;
    }
    uint8_t t6 = gm_l5;
    if (t6) {
        gm_l4 = ((uint8_t)1);
    } else {
        uint8_t t7 = gm_l0;
        uint8_t t8 = ((t7 == ((uint8_t)10ULL)));
        gm_l4 = t8;
    }
    uint8_t t9 = gm_l4;
    if (t9) {
        gm_l3 = ((uint8_t)1);
    } else {
        uint8_t t10 = gm_l0;
        uint8_t t11 = ((t10 == ((uint8_t)13ULL)));
        gm_l3 = t11;
    }
    uint8_t t12 = gm_l3;
    if (t12) {
        gm_l2 = ((uint8_t)1);
    } else {
        uint8_t t13 = gm_l0;
        uint8_t t14 = ((t13 == ((uint8_t)11ULL)));
        gm_l2 = t14;
    }
    uint8_t t15 = gm_l2;
    if (t15) {
        gm_l1 = ((uint8_t)1);
    } else {
        uint8_t t16 = gm_l0;
        uint8_t t17 = ((t16 == ((uint8_t)12ULL)));
        gm_l1 = t17;
    }
    uint8_t t18 = gm_l1;
    F.ret = t18;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_upper_68kn81r(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_upper_68kn81r F;
    uint8_t gm_l0; /* c */
    uint8_t gm_l1; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 11u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l2;
    uint8_t t2 = gm_l0;
    gm_l1 = t2;
    uint8_t t3 = gm_l1;
    uint8_t t4 = (((uint8_t)65ULL) <= t3);
    if (t4) {
        uint8_t t5 = gm_l1;
        uint8_t t6 = (t5 <= ((uint8_t)90ULL));
        gm_l2 = t6;
    } else {
        gm_l2 = ((uint8_t)0);
    }
    uint8_t t7 = gm_l2;
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_lint_diag_4vra4qu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_lint_diag_4vra4qu F;
    gm_tl874t2atnlh3 gm_l0; /* span */
    gm_b16_8 gm_l1; /* code */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tl874t2atnlh3 t2 = gm_l0;
    gm_b16_8 t3 = gm_l1;
    gm_b16_8 t4 = gm_l2;
    gm_f_lint_eo1tub5((void *)&(*(gm_t4gut2c9fj1kt *)gm_out), (void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) void gm_f_lint_eo1tub5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_lint_eo1tub5 F;
    gm_tl874t2atnlh3 gm_l0; /* span */
    gm_b16_8 gm_l1; /* code */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_tl874t2atnlh3 t2 = gm_l0;
        gm_b16_8 t3 = gm_l2;
        gm_f_warning_q0m5jjs((void *)&F.l3, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t4 = gm_l1;
        gm_th2lqbo77pvsc t5;
        GM_ST(uint8_t, (uint8_t *)&t5 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t5 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t5 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t5), t4);
        ((uint8_t *)&t5)[16] = 1;
        GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)&F.l3) + 160), t5);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l3), 184);
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
inline __attribute__((aligned(4))) void gm_f_list_fu17r8d(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_fu17r8d F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_9f3suev();
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
inline __attribute__((aligned(4))) void gm_f_list_prbqo9j(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_prbqo9j F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_1th396a();
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
inline __attribute__((aligned(4))) void gm_f_list_uaa66su(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_uaa66su F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_n5n7pq1();
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_normalize_space_v7dp7uh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_normalize_space_v7dp7uh F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l2; /* <tmp> */
    GM_STACK_CHECK(&F, 168u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_trqfvmjtt4sij t3;
    gm_f_list_uvfu1k1((void *)&t3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t3;
    gm_b16_8 t4 = gm_l0;
    gm_l2 = t4;
    {
        gm_node wn5 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlhb9f1a7omo, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t8 = gm_l2;
        gm_b0_1 t9 = gm_f_fields_ojgi3et_sq5308c((void *)NULL, (&wn5), NULL, t8);
        if (gm_unw.kind) {
            goto gm_ll7;
        }
    }
    goto gm_ls10;
gm_ll7:;
    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we6; }
    goto gm_lad;
gm_ls10:;
gm_lb1:;
we6:;
    gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
    F.ret = t12;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_h4mkmpb(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_h4mkmpb F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_t16on0i8enr1i gm_l2; /* x */
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
        memset(&(*(gm_tgqc207d6ne28 *)gm_out), 0, sizeof (*(gm_tgqc207d6ne28 *)gm_out));
        goto gm_fdone;
    }
    {
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, ((uint8_t *)t6));
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t6) + 8));
        uint64_t t9 = gm_l1;
        uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
        uint64_t t12 = r10;
        if ((uintptr_t)t12 >= (uintptr_t)(t8)) gm_trap_index((uintptr_t)t12, t8, gm_srcrgc9bkquf6nr, 133, 10);
        memmove(((uint8_t *)&gm_l2), (((uint8_t *)t7) + (uintptr_t)t12 * 32), 32);
    }
    void * t13 = gm_l0;
    void * t14 = gm_l0;
    void * t15 = GM_LD(void *, ((uint8_t *)t14));
    uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t14) + 8));
    uint64_t t17 = gm_l1;
    uint64_t r18; bool o19 = __builtin_sub_overflow(t17, ((uint64_t)1ULL), &r18);
    if (o19) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
    uint64_t t20 = r18;
    uint64_t t21 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t22 = (uintptr_t)t20;
    if (t21 > t22 || t22 > (uintptr_t)t16) gm_trap_slice(t21, t22, t16, gm_srcrgc9bkquf6nr, 134, 15);
    uint64_t t23 = ((uintptr_t)t20) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t24;
    GM_ST(void *, (uint8_t *)&t24, (void *)(((uint8_t *)t15 + (uintptr_t)((uint64_t)0ULL) * 32)));
    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(t23));
    GM_ST(gm_b16_8, ((uint8_t *)t13), t24);
    {
        gm_t16on0i8enr1i t25 = GM_LD(gm_t16on0i8enr1i, ((uint8_t *)&gm_l2));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tgqc207d6ne28 *)gm_out) + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tgqc207d6ne28 *)gm_out) + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tgqc207d6ne28 *)gm_out) + 36, 0);
        GM_ST(gm_t16on0i8enr1i, ((uint8_t *)&(*(gm_tgqc207d6ne28 *)gm_out)), t25);
        ((uint8_t *)&(*(gm_tgqc207d6ne28 *)gm_out))[32] = 1;
        goto gm_fdone;
    }
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_amge2t0(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_amge2t0 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_9f3suev();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_usnqfd3(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 56), ((uint8_t *)a1), 56);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 56)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_em9e1kh(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_em9e1kh F;
    void * gm_l0; /* l */
    gm_t16on0i8enr1i gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_n5n7pq1();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_r8ccoha(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_l0e5v4c(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_push_l0e5v4c F;
    void * gm_l0; /* w */
    GM_STACK_CHECK(&F, 29u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_tftb1odc739t5 gm_l2;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_tftb1odc739t5 t4 = gm_f_push_l0rgbjf(t3, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l1)));
    gm_l2 = t4;
    uint8_t t5 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t5) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_src10tru552hn49, 74, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_l0rgbjf(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_l0rgbjf F;
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
        gm_b16_8 t9 = gm_f_empty_slice_1th396a();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_kfs9rll(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 184), ((uint8_t *)&F.l1), 184);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 184)));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_label_hn6s4vp(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_push_label_hn6s4vp F;
    void * gm_l0; /* w */
    gm_t08olq97rscn3 gm_l2; /* l */
    GM_STACK_CHECK(&F, 122u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_tftb1odc739t5 gm_l3;
    uint8_t t2 = (uint8_t)((((uint8_t *)&F.l1))[32] != 0);
    if (t2) {
        memmove(((uint8_t *)&gm_l2), ((uint8_t *)&F.l1), 32);
    } else {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t3 = gm_l0;
    void * t4 = (void *)(((uint8_t *)t3) + 224);
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
    gm_tl874t2atnlh3 t6 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l2) + 16));
    gm_t16on0i8enr1i t7;
    GM_ST(uint8_t, (uint8_t *)&t7 + 29, 0);
    GM_ST(uint16_t, (uint8_t *)&t7 + 30, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t7 + 0), t5);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t7 + 16), t6);
    GM_ST(uint8_t, ((uint8_t *)&t7 + 28), ((uint8_t)0));
    gm_tftb1odc739t5 t8 = gm_f_push_em9e1kh(t4, &t7);
    gm_l3 = t8;
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t9) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_src10tru552hn49, 441, 5);
    F.ret = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_kfs9rll(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_kfs9rll F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)184ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_r8ccoha(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_r8ccoha F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_usnqfd3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_usnqfd3 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)56ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_rule_scmud9h(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_rule_scmud9h F;
    gm_b16_8 gm_l0; /* name */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_tf5srh4mp4b3i t3 = gm_f_lint_rule_o3ccae1(t2);
    F.ret = t3;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_shrink_to_hi_5bdve33(gm_cseahvg8v7p0vs a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_shrink_to_hi_5bdve33 F;
    gm_tl874t2atnlh3 gm_l0; /* a */
    GM_STACK_CHECK_SHALLOW(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_tl874t2atnlh3, a0);
    uint32_t t2 = GM_LD(uint32_t, ((uint8_t *)&gm_l0));
    uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 8));
    uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 8));
    gm_tl874t2atnlh3 t5;
    GM_ST(uint32_t, ((uint8_t *)&t5 + 0), t2);
    GM_ST(uint32_t, ((uint8_t *)&t5 + 4), t3);
    GM_ST(uint32_t, ((uint8_t *)&t5 + 8), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
gm_fprop:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_text_kspioh6(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_text_kspioh6 F;
    void * gm_l0; /* w */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK_SHALLOW(&F, 160u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    uint32_t t4 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    void * t5 = gm_f_file_o15fqku(t3, t4);
    void * t6 = GM_LD(void *, (((uint8_t *)t5) + 16));
    uint64_t t7 = GM_LD(uint64_t, ((((uint8_t *)t5) + 16) + 8));
    uint32_t t8 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    uint64_t t9 = (uint64_t)t8;
    uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t11 = (uint64_t)t10;
    uint64_t t12 = (uintptr_t)t9;
    uint64_t t13 = (uintptr_t)t11;
    if (t12 > t13 || t13 > (uintptr_t)t7) gm_trap_slice(t12, t13, t7, gm_src10tru552hn49, 71, 60);
    uint64_t t14 = ((uintptr_t)t11) - (uintptr_t)t9;
    gm_b16_8 t15;
    GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t6 + (uintptr_t)t9 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14));
    F.ret = t15;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_upper_camel_a0jt5mh(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_upper_camel_a0jt5mh F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 68u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    uint8_t gm_l2;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t3 = (t2 > ((uint64_t)0ULL));
    if (t3) {
        void * t4 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)t4) + (uintptr_t)((uint64_t)0ULL) * 1));
        uint8_t t7 = gm_f_is_upper_68kn81r(t6);
        gm_l2 = t7;
    } else {
        gm_l2 = ((uint8_t)0);
    }
    uint8_t t8 = gm_l2;
    if (t8) {
        gm_b16_8 t9 = gm_l0;
        uint8_t t10 = 0;
        uint8_t t11 = ((uint8_t)95ULL);
        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t9) + 8)); i++) if (gm_eqjmccqdb90f(((uint8_t *)GM_LD(void *, ((uint8_t *)&t9))) + i * 1, ((uint8_t *)&t11))) { t10 = 1; break; }
        uint8_t t12 = (uint8_t)(!t10);
        gm_l1 = t12;
    } else {
        gm_l1 = ((uint8_t)0);
    }
    uint8_t t13 = gm_l1;
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_walk_tech5q5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_walk_tech5q5 F;
    void * gm_l0; /* sm */
    GM_STACK_CHECK(&F, 304u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_tg0vmr665gcag t3;
    gm_f_file_facts_tdo30n8((void *)&t3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t4;
    gm_f_list_uaa66su((void *)&t4, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = gm_f_empty_slice_qbupbr6();
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
    GM_ST(void *, ((uint8_t *)&(*(gm_t1og5k0mlcjk3 *)gm_out) + 0), t2);
    GM_ST(gm_tg0vmr665gcag, ((uint8_t *)&(*(gm_t1og5k0mlcjk3 *)gm_out) + 8), t3);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_t1og5k0mlcjk3 *)gm_out) + 224), t4);
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t1og5k0mlcjk3 *)gm_out) + 264), t6);
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
inline __attribute__((aligned(4))) void gm_f_warning_q0m5jjs(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1) {
    struct gm_fr_warning_q0m5jjs F;
    gm_tl874t2atnlh3 gm_l0; /* span */
    gm_b16_8 gm_l1; /* message */
    GM_STACK_CHECK(&F, 57u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_tl874t2atnlh3 t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        gm_f_error_s6hn871((void *)&F.l2, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    gm_t9qkh0rsooc8d t4;
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)1ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l2), t4);
    {
        memmove(gm_out, ((uint8_t *)&F.l2), 184);
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
inline __attribute__((aligned(4))) uint8_t gm_f_accepts_3h4b27e(gm_t9qkh0rsooc8d a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_accepts_3h4b27e F;
    gm_t9qkh0rsooc8d gm_l0; /* case */
    gm_b16_8 gm_l1; /* s */
    uint8_t gm_l2; /* snake */
    uint8_t gm_l3; /* has_lower */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint8_t gm_l6; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 174u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l7;
    uint8_t gm_l8;
    uint8_t gm_l9;
    gm_l2 = ((uint8_t)1);
    gm_l3 = ((uint8_t)0);
    gm_b16_8 t11 = gm_l1;
    gm_l4 = t11;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t12 = gm_l5;
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t14 = (t12 < t13);
            if (!t14) {
                goto gm_lb1;
            }
            {
                void * t15 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t17 = gm_l5;
                {
                    {
                        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)t15) + (uintptr_t)t17 * 1));
                        gm_l6 = t18;
                        uint8_t t19 = gm_l6;
                        uint8_t t20 = gm_f_is_upper_68kn81r(t19);
                        if (t20) {
                            gm_l2 = ((uint8_t)0);
                        }
                        uint8_t t21 = gm_l6;
                        uint8_t t22 = gm_f_is_lower_q12rpq9(t21);
                        if (t22) {
                            gm_l3 = ((uint8_t)1);
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 793, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t23 = gm_l5;
            uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
            uint64_t t26 = r24;
            gm_l5 = t26;
        }
    }
gm_lb1:;
    {
        {
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t28 = (uint8_t)(t27 == ((uint8_t)0ULL));
            if (!t28) {
                goto gm_lb7;
            }
            gm_b16_8 t29 = gm_l1;
            uint8_t t30 = gm_f_upper_camel_a0jt5mh(t29);
            gm_l7 = t30;
            goto gm_lb6;
        }
gm_lb7:;
        {
            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t32 = (uint8_t)(t31 == ((uint8_t)1ULL));
            if (!t32) {
                goto gm_lb8;
            }
            uint8_t t33 = gm_l2;
            gm_l7 = t33;
            goto gm_lb6;
        }
gm_lb8:;
        {
            uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t35 = (uint8_t)(t34 == ((uint8_t)2ULL));
            if (!t35) {
                goto gm_lb9;
            }
            uint8_t t36 = gm_l2;
            if (t36) {
                gm_l9 = ((uint8_t)1);
            } else {
                uint8_t t37 = gm_l3;
                uint8_t t38 = (uint8_t)!t37;
                gm_l9 = t38;
            }
            uint8_t t39 = gm_l9;
            if (t39) {
                gm_l8 = ((uint8_t)1);
            } else {
                gm_b16_8 t40 = gm_l1;
                uint8_t t41 = gm_f_upper_camel_a0jt5mh(t40);
                gm_l8 = t41;
            }
            uint8_t t42 = gm_l8;
            gm_l7 = t42;
            goto gm_lb6;
        }
gm_lb9:;
        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 797, 12);
    }
gm_lb6:;
    uint8_t t43 = gm_l7;
    F.ret = t43;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_toceffb697p0u gm_f_binding_op_gcp8f8i(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_binding_op_gcp8f8i F;
    void * gm_l0; /* w */
    gm_tl874t2atnlh3 gm_l1; /* between */
    gm_b16_8 gm_l2; /* t */
    uint64_t gm_l3; /* off */
    uint32_t gm_l4; /* len */
    uint32_t gm_l5; /* lo */
    GM_STACK_CHECK_SHALLOW(&F, 412u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    uint8_t gm_l6;
    void * t5 = gm_l0;
    gm_tl874t2atnlh3 t6 = gm_l1;
    gm_b16_8 t7 = gm_f_text_kspioh6(t5, GM_CONV(gm_cseahvg8v7p0vs, t6));
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
            void * t11 = GM_LD(void *, ((uint8_t *)&gm_l2));
            uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint64_t t13 = gm_l3;
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)t11) + (uintptr_t)t13 * 1));
            uint8_t t15 = gm_f_is_space_uen38t5(t14);
            if (!t15) {
                goto gm_lb1;
            }
            {
                uint64_t t16 = gm_l3;
                uint64_t r17; bool o18 = __builtin_add_overflow(t16, ((uint64_t)1ULL), &r17);
                if (o18) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 246, 52);
                uint64_t t19 = r17;
                gm_l3 = t19;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    uint64_t t20 = gm_l3;
    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint8_t t22 = (t20 >= t21);
    if (t22) {
        gm_toceffb697p0u t23;
        memset(&t23, 0, sizeof t23);
        F.ret = t23;
        goto gm_fret;
    }
    void * t24 = GM_LD(void *, ((uint8_t *)&gm_l2));
    uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint64_t t26 = gm_l3;
    uint64_t t27 = ((uintptr_t)t25) - (uintptr_t)t26;
    gm_b16_8 t28;
    GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t24 + (uintptr_t)t26 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
    gm_b16_8 t29;
    GM_ST(void *, (uint8_t *)&t29, (void *)(gm_shge5n743ua4h));
    GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(2));
    uint8_t t30 = gm_f_starts_with_6vvt01e(t28, t29);
    if (t30) {
        gm_l6 = ((uint8_t)1);
    } else {
        void * t31 = GM_LD(void *, ((uint8_t *)&gm_l2));
        uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint64_t t33 = gm_l3;
        uint64_t t34 = ((uintptr_t)t32) - (uintptr_t)t33;
        gm_b16_8 t35;
        GM_ST(void *, (uint8_t *)&t35, (void *)(((uint8_t *)t31 + (uintptr_t)t33 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(t34));
        gm_b16_8 t36;
        GM_ST(void *, (uint8_t *)&t36, (void *)(gm_ss67f0gmfen1m));
        GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(2));
        uint8_t t37 = gm_f_starts_with_6vvt01e(t35, t36);
        gm_l6 = t37;
    }
    uint8_t t38 = gm_l6;
    if (t38) {
        gm_l4 = ((uint32_t)2ULL);
    } else {
        gm_l4 = ((uint32_t)1ULL);
    }
    uint32_t t39 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    uint64_t t40 = gm_l3;
    uint32_t t41 = (uint32_t)t40;
    uint32_t r42; bool o43 = __builtin_add_overflow(t39, t41, &r42);
    if (o43) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 249, 11);
    uint32_t t44 = r42;
    gm_l5 = t44;
    uint32_t t45 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    uint32_t t46 = gm_l5;
    uint32_t t47 = gm_l5;
    uint32_t t48 = gm_l4;
    uint32_t r49; bool o50 = __builtin_add_overflow(t47, t48, &r49);
    if (o50) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 250, 56);
    uint32_t t51 = r49;
    gm_tl874t2atnlh3 t52;
    GM_ST(uint32_t, ((uint8_t *)&t52 + 0), t45);
    GM_ST(uint32_t, ((uint8_t *)&t52 + 4), t46);
    GM_ST(uint32_t, ((uint8_t *)&t52 + 8), t51);
    gm_toceffb697p0u t53;
    GM_ST(uint8_t, (uint8_t *)&t53 + 13, 0);
    GM_ST(uint16_t, (uint8_t *)&t53 + 14, 0);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t53), t52);
    ((uint8_t *)&t53)[12] = 1;
    F.ret = t53;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_binding_stmt_80l1chq(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_binding_stmt_80l1chq F;
    void * gm_l0; /* w */
    void * gm_l1; /* b */
    gm_t08olq97rscn3 gm_l2; /* name */
    void * gm_l3; /* inner */
    void * gm_l4; /* i */
    gm_t9qkh0rsooc8d gm_l5; /* case */
    uint32_t gm_l6; /* from */
    void * gm_l7; /* t */
    gm_toceffb697p0u gm_l8; /* op */
    gm_b16_8 gm_l9; /* init_text */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_t08olq97rscn3 gm_l13; /* n */
    void * gm_l14; /* t */
    GM_STACK_CHECK(&F, 1287u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l15;
    uint8_t gm_l16;
    uint8_t gm_l17;
    uint8_t gm_l18;
    uint8_t gm_l19;
    uint8_t gm_l20;
    uint8_t gm_l21;
    uint8_t gm_l22;
    uint8_t gm_l23;
    gm_tftb1odc739t5 gm_l24;
    gm_tftb1odc739t5 gm_l25;
    void * t26 = gm_l1;
    uint64_t t27 = GM_LD(uint64_t, ((((uint8_t *)t26) + 8) + 8));
    uint8_t t28 = ((t27 == ((uint64_t)1ULL)));
    if (t28) {
        {
            void * t29 = gm_l1;
            void * t30 = GM_LD(void *, (((uint8_t *)t29) + 8));
            uint64_t t31 = GM_LD(uint64_t, ((((uint8_t *)t29) + 8) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t31)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t31, gm_src10tru552hn49, 204, 17);
            memmove(((uint8_t *)&gm_l2), (((uint8_t *)t30) + (uintptr_t)((uint64_t)0ULL) * 32), 32);
        }
        void * t32 = gm_l1;
        void * t33 = GM_LD(void *, (((uint8_t *)t32) + 40));
        gm_l3 = t33;
        {
            {
                void * t34 = gm_l3;
                uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t34) + 8) + 0));
                uint8_t t36 = (uint8_t)(t35 == ((uint8_t)35ULL));
                if (!t36) {
                    goto gm_lb2;
                }
                void * t37 = GM_LD(void *, (((((uint8_t *)t34) + 8) + 8) + 16));
                gm_l4 = t37;
                void * t38 = gm_l4;
                gm_l3 = t38;
                goto gm_lb1;
            }
gm_lb2:;
        }
gm_lb1:;
        void * t39 = gm_l1;
        void * t40 = GM_LD(void *, (((uint8_t *)t39) + 40));
        {
            {
                uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
                uint8_t t42 = (uint8_t)(t41 == ((uint8_t)36ULL));
                if (!t42) {
                    goto gm_lb4;
                }
                gm_l15 = ((uint8_t)1);
                goto gm_lb3;
            }
gm_lb4:;
            {
                gm_l15 = ((uint8_t)0);
                goto gm_lb3;
            }
gm_lb5:;
            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 207, 12);
        }
gm_lb3:;
        uint8_t t43 = gm_l15;
        if (t43) {
            void * t44 = gm_l0;
            gm_b16_8 t45;
            GM_ST(void *, (uint8_t *)&t45, (void *)(gm_sq0dqiq83b924));
            GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(8));
            gm_t9qkh0rsooc8d t46;
            GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)1ULL));
            gm_b0_1 t47 = gm_f_check_name_hf7dide((void *)NULL, cx, NULL, t44, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l2)), t46, t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        } else {
            void * t48 = gm_l3;
            {
                {
                    uint8_t t49 = GM_LD(uint8_t, ((((uint8_t *)t48) + 8) + 0));
                    uint8_t t50 = (uint8_t)(t49 == ((uint8_t)32ULL));
                    if (!t50) {
                        goto gm_lb7;
                    }
                    gm_l16 = ((uint8_t)1);
                    goto gm_lb6;
                }
gm_lb7:;
                {
                    uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)t48) + 8) + 0));
                    uint8_t t52 = (uint8_t)(t51 == ((uint8_t)33ULL));
                    if (!t52) {
                        goto gm_lb8;
                    }
                    gm_l16 = ((uint8_t)1);
                    goto gm_lb6;
                }
gm_lb8:;
                {
                    gm_l16 = ((uint8_t)0);
                    goto gm_lb6;
                }
gm_lb9:;
                gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 209, 19);
            }
gm_lb6:;
            uint8_t t53 = gm_l16;
            if (t53) {
                void * t54 = gm_l0;
                void * t55 = gm_l1;
                gm_b0_1 t56 = gm_f_top_level_name_dv4kpc9((void *)NULL, cx, NULL, t54, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l2)), t55);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            } else {
                void * t57 = gm_l1;
                gm_t9qkh0rsooc8d t58 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t57) + 32));
                uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&t58) + 0));
                uint8_t t60 = (uint8_t)((t59 == ((uint8_t)0ULL)));
                gm_t9qkh0rsooc8d t61;
                GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)0ULL));
                if (t60) {
                    gm_t9qkh0rsooc8d t62;
                    GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)2ULL));
                    gm_l5 = t62;
                } else {
                    gm_t9qkh0rsooc8d t63;
                    GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)1ULL));
                    gm_l5 = t63;
                }
                void * t64 = gm_l0;
                gm_t9qkh0rsooc8d t65 = gm_l5;
                gm_b16_8 t66;
                GM_ST(void *, (uint8_t *)&t66, (void *)(gm_sjg1ri23hm753));
                GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(5));
                gm_b0_1 t67 = gm_f_check_name_hf7dide((void *)NULL, cx, NULL, t64, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l2)), t65, t66);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        uint32_t t68 = GM_LD(uint32_t, ((((uint8_t *)&gm_l2) + 16) + 8));
        gm_l6 = t68;
        {
            {
                void * t69 = gm_l1;
                uint8_t t70 = (uint8_t)(GM_LD(void *, (((uint8_t *)t69) + 24)) != 0);
                if (!t70) {
                    goto gm_lb11;
                }
                void * t71 = GM_LD(void *, (((uint8_t *)t69) + 24));
                gm_l7 = t71;
                void * t72 = gm_l7;
                uint32_t t73 = GM_LD(uint32_t, ((((uint8_t *)t72) + 72) + 8));
                gm_l6 = t73;
                goto gm_lb10;
            }
gm_lb11:;
        }
gm_lb10:;
        void * t74 = gm_l0;
        uint32_t t75 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 16));
        uint32_t t76 = gm_l6;
        void * t77 = gm_l1;
        void * t78 = GM_LD(void *, (((uint8_t *)t77) + 40));
        uint32_t t79 = GM_LD(uint32_t, ((((uint8_t *)t78) + 72) + 4));
        gm_tl874t2atnlh3 t80;
        GM_ST(uint32_t, ((uint8_t *)&t80 + 0), t75);
        GM_ST(uint32_t, ((uint8_t *)&t80 + 4), t76);
        GM_ST(uint32_t, ((uint8_t *)&t80 + 8), t79);
        gm_toceffb697p0u t81 = gm_f_binding_op_gcp8f8i(t74, GM_CONV(gm_cseahvg8v7p0vs, t80));
        gm_l8 = t81;
        void * t82 = gm_l0;
        void * t83 = gm_l1;
        void * t84 = GM_LD(void *, (((uint8_t *)t83) + 40));
        gm_tl874t2atnlh3 t85 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t84) + 72));
        gm_b16_8 t86 = gm_f_text_kspioh6(t82, GM_CONV(gm_cseahvg8v7p0vs, t85));
        gm_l9 = t86;
        {
            void * t87 = gm_l1;
            gm_t9qkh0rsooc8d t88 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t87) + 32));
            uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&t88) + 0));
            uint8_t t90 = (uint8_t)((t89 == ((uint8_t)1ULL)));
            void * t91 = gm_l1;
            gm_t9qkh0rsooc8d t92;
            GM_ST(uint8_t, ((uint8_t *)&t92 + 0), ((uint8_t)1ULL));
            {
                {
                    uint8_t t93 = (uint8_t)(GM_LD(void *, (((uint8_t *)t91) + 24)) != 0);
                    if (t93) {
                        goto gm_lb13;
                    }
                    gm_l17 = ((uint8_t)0);
                    goto gm_lb12;
                }
gm_lb13:;
                {
                    gm_l17 = ((uint8_t)1);
                    goto gm_lb12;
                }
gm_lb14:;
                gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 224, 20);
            }
gm_lb12:;
            uint8_t t94 = gm_l17;
            gm_toceffb697p0u t95 = gm_l8;
            void * t96 = gm_l1;
            void * t97 = GM_LD(void *, (((uint8_t *)t96) + 40));
            uint8_t t98 = gm_f_is_literal_9ac2fdt(t97);
            if (t98) {
                gm_l22 = ((uint8_t)1);
            } else {
                void * t99 = gm_l1;
                void * t100 = GM_LD(void *, (((uint8_t *)t99) + 40));
                uint8_t t101 = gm_f_is_constant_call_vj2jgti(t100);
                gm_l22 = t101;
            }
            uint8_t t102 = gm_l22;
            if (t102) {
                gm_l21 = ((uint8_t)1);
            } else {
                gm_b16_8 t103 = gm_l9;
                uint8_t t104 = 0;
                uint8_t t105 = ((uint8_t)64ULL);
                for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t103) + 8)); i++) if (gm_eqjmccqdb90f(((uint8_t *)GM_LD(void *, ((uint8_t *)&t103))) + i * 1, ((uint8_t *)&t105))) { t104 = 1; break; }
                uint8_t t106 = (uint8_t)(t104);
                gm_l21 = t106;
            }
            uint8_t t107 = gm_l21;
            if (t107) {
                gm_l20 = ((uint8_t)1);
            } else {
                gm_b16_8 t108 = gm_l9;
                gm_b16_8 t109;
                GM_ST(void *, (uint8_t *)&t109, (void *)(gm_sk754mj3p1el6));
                GM_ST(uintptr_t, (uint8_t *)&t109 + 8, (uintptr_t)(1));
                uint8_t t110 = gm_f_starts_with_6vvt01e(t108, t109);
                gm_l20 = t110;
            }
            uint8_t t111 = gm_l20;
            if (t111) {
                gm_l19 = ((uint8_t)1);
            } else {
                gm_b16_8 t112 = gm_l9;
                gm_b16_8 t113;
                GM_ST(void *, (uint8_t *)&t113, (void *)(gm_srs1vrn2uaams));
                GM_ST(uintptr_t, (uint8_t *)&t113 + 8, (uintptr_t)(1));
                uint8_t t114 = gm_f_starts_with_6vvt01e(t112, t113);
                gm_l19 = t114;
            }
            uint8_t t115 = gm_l19;
            if (t115) {
                gm_l18 = ((uint8_t)1);
            } else {
                gm_b16_8 t116 = gm_l9;
                gm_b16_8 t117;
                GM_ST(void *, (uint8_t *)&t117, (void *)(gm_sfqk3rro4g7t0));
                GM_ST(uintptr_t, (uint8_t *)&t117 + 8, (uintptr_t)(1));
                uint8_t t118 = gm_f_starts_with_6vvt01e(t116, t117);
                gm_l18 = t118;
            }
            uint8_t t119 = gm_l18;
            void * t120 = gm_l1;
            void * t121 = GM_LD(void *, (((uint8_t *)t120) + 40));
            {
                {
                    uint8_t t122 = GM_LD(uint8_t, ((((uint8_t *)t121) + 8) + 0));
                    uint8_t t123 = (uint8_t)(t122 == ((uint8_t)36ULL));
                    if (!t123) {
                        goto gm_lb16;
                    }
                    gm_l23 = ((uint8_t)1);
                    goto gm_lb15;
                }
gm_lb16:;
                {
                    gm_l23 = ((uint8_t)0);
                    goto gm_lb15;
                }
gm_lb17:;
                gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 227, 21);
            }
gm_lb15:;
            uint8_t t124 = gm_l23;
            void * t125 = gm_l1;
            void * t126 = GM_LD(void *, (((uint8_t *)t125) + 40));
            uint8_t t127 = GM_LD(uint8_t, ((((uint8_t *)t126) + 8) + 0));
            uint8_t t128 = (uint8_t)((t127 == ((uint8_t)5ULL)));
            gm_tb9j2bleq9j8f t129;
            GM_ST(uint16_t, (uint8_t *)&t129 + 2, 0);
            GM_ST(uint8_t, (uint8_t *)&t129 + 23, 0);
            GM_ST(uint8_t, ((uint8_t *)&t129 + 0), t90);
            GM_ST(uint8_t, ((uint8_t *)&t129 + 1), t94);
            GM_ST(gm_toceffb697p0u, ((uint8_t *)&t129 + 4), t95);
            GM_ST(uint8_t, ((uint8_t *)&t129 + 20), t119);
            GM_ST(uint8_t, ((uint8_t *)&t129 + 21), t124);
            GM_ST(uint8_t, ((uint8_t *)&t129 + 22), t128);
            gm_t2gtbh39ptekd t130 = GM_LD(gm_t2gtbh39ptekd, (((uint8_t *)t126) + 8));
            gm_t2gtbh39ptekd t131;
            memset(&t131, 0, sizeof t131);
            GM_ST(uint8_t, ((uint8_t *)&t131 + 0), ((uint8_t)5ULL));
            GM_ST(uint8_t, (uint8_t *)&F.l10 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&F.l10 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&F.l10 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&F.l10 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&F.l10 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&F.l10 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tb9j2bleq9j8f, ((uint8_t *)&F.l10 + 8), t129);
        }
        {
            void * t132 = gm_l0;
            void * t133 = (void *)((((uint8_t *)t132) + 8) + 40);
            gm_tl874t2atnlh3 t134 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l2) + 16));
            gm_tftb1odc739t5 t135 = gm_f_put_amueool(t133, GM_CONV(gm_cseahvg8v7p0vs, t134), &(*(gm_tl5vm0tcqvdvb *)((uint8_t *)&F.l10)));
            gm_l24 = t135;
        }
        uint8_t t136 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
        if (!t136) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_src10tru552hn49, 230, 9);
    } else {
        void * t137 = gm_l1;
        gm_b16_8 t138 = GM_LD(gm_b16_8, (((uint8_t *)t137) + 8));
        gm_l11 = t138;
        gm_l12 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t139 = gm_l12;
                uint64_t t140 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                uint8_t t141 = (t139 < t140);
                if (!t141) {
                    goto gm_lb18;
                }
                {
                    void * t142 = GM_LD(void *, ((uint8_t *)&gm_l11));
                    uint64_t t143 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                    uint64_t t144 = gm_l12;
                    {
                        {
                            {
                                memmove(((uint8_t *)&gm_l13), (((uint8_t *)t142) + (uintptr_t)t144 * 32), 32);
                            }
                            {
                                void * t145 = gm_l0;
                                gm_b16_8 t146;
                                GM_ST(void *, (uint8_t *)&t146, (void *)(gm_sjg1ri23hm753));
                                GM_ST(uintptr_t, (uint8_t *)&t146 + 8, (uintptr_t)(5));
                                gm_t9qkh0rsooc8d t147;
                                GM_ST(uint8_t, ((uint8_t *)&t147 + 0), ((uint8_t)1ULL));
                                gm_b0_1 t148 = gm_f_check_name_hf7dide((void *)NULL, cx, NULL, t145, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l13)), t147, t146);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            {
                                void * t149 = gm_l0;
                                void * t150 = (void *)((((uint8_t *)t149) + 8) + 40);
                                gm_tl874t2atnlh3 t151 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l13) + 16));
                                gm_tl874t2atnlh3 t152 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l13) + 16));
                                gm_b16_8 t153;
                                GM_ST(void *, (uint8_t *)&t153, (void *)(gm_sfdbfrv46lihd));
                                GM_ST(uintptr_t, (uint8_t *)&t153 + 8, (uintptr_t)(1));
                                gm_tbgd14esf0jvi t154;
                                GM_ST(uint32_t, (uint8_t *)&t154 + 12, 0);
                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t154 + 0), t152);
                                GM_ST(gm_b16_8, ((uint8_t *)&t154 + 16), t153);
                                gm_tl5vm0tcqvdvb t155;
                                GM_ST(uint8_t, (uint8_t *)&t155 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t155 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t155 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t155 + 40, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t155 + 0), ((uint8_t)1ULL));
                                GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t155 + 8), t154);
                                gm_tftb1odc739t5 t156 = gm_f_put_amueool(t150, GM_CONV(gm_cseahvg8v7p0vs, t151), &t155);
                                gm_l25 = t156;
                            }
                            uint8_t t157 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                            if (!t157) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_src10tru552hn49, 234, 13);
                            goto gm_lb21;
                        }
gm_lb22:;
                        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 232, 9);
                    }
gm_lb21:;
                }
gm_lb20:;
                uint64_t t158 = gm_l12;
                uint64_t r159; bool o160 = __builtin_add_overflow(t158, ((uint64_t)1ULL), &r159);
                uint64_t t161 = r159;
                gm_l12 = t161;
            }
        }
gm_lb18:;
    }
    {
        {
            void * t162 = gm_l1;
            uint8_t t163 = (uint8_t)(GM_LD(void *, (((uint8_t *)t162) + 24)) != 0);
            if (!t163) {
                goto gm_lb24;
            }
            void * t164 = GM_LD(void *, (((uint8_t *)t162) + 24));
            gm_l14 = t164;
            void * t165 = gm_l0;
            void * t166 = gm_l14;
            gm_b0_1 t167 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t165, t166);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb23;
        }
gm_lb24:;
    }
gm_lb23:;
    void * t168 = gm_l0;
    gm_b16_8 t169;
    GM_ST(void *, (uint8_t *)&t169, (void *)(gm_s3i3n5bvpvmmi));
    GM_ST(uintptr_t, (uint8_t *)&t169 + 8, (uintptr_t)(15));
    void * t170 = gm_l1;
    void * t171 = GM_LD(void *, (((uint8_t *)t170) + 40));
    gm_b0_1 t172 = gm_f_paren_at_av727r5((void *)NULL, cx, NULL, t168, t169, t171, ((uint8_t)0));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t173 = gm_l0;
    void * t174 = gm_l1;
    void * t175 = GM_LD(void *, (((uint8_t *)t174) + 40));
    gm_b0_1 t176 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t173, t175);
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
inline __attribute__((aligned(4))) uint8_t gm_f_blank_ineorlq(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_blank_ineorlq F;
    gm_b16_8 gm_l0; /* t */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint8_t gm_l3; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 124u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t9 = gm_l0;
    gm_l1 = t9;
    gm_l2 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t10 = gm_l2;
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t12 = (t10 < t11);
            if (!t12) {
                goto gm_lb1;
            }
            {
                void * t13 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t15 = gm_l2;
                {
                    {
                        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)t13) + (uintptr_t)t15 * 1));
                        gm_l3 = t16;
                        {
                            {
                                uint8_t t17 = gm_l3;
                                uint8_t t18 = gm_f_is_space_uen38t5(t17);
                                uint8_t t19 = (uint8_t)!t18;
                                if (!t19) {
                                    goto gm_lb7;
                                }
                                uint8_t t20 = gm_l3;
                                uint8_t t21 = (!(t20 == ((uint8_t)123ULL)));
                                if (!t21) {
                                    goto gm_lb7;
                                }
                                uint8_t t22 = gm_l3;
                                uint8_t t23 = (!(t22 == ((uint8_t)125ULL)));
                                if (!t23) {
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
                    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 723, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t24 = gm_l2;
            uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
            uint64_t t27 = r25;
            gm_l2 = t27;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_block_btrailr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_block_btrailr F;
    void * gm_l0; /* w */
    void * gm_l1; /* b */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    void * gm_l5; /* last */
    uint8_t gm_l6; /* is_err */
    gm_b16_8 gm_l7; /* msg */
    GM_STACK_CHECK(&F, 591u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = ((uint64_t)0ULL);
    void * t9 = gm_l1;
    uint64_t t10 = GM_LD(uint64_t, ((((uint8_t *)t9) + 8) + 8));
    gm_l2 = t10;
    {
        for (;;) {
            uint64_t t11 = gm_l3;
            uint64_t t12 = gm_l2;
            uint8_t t13 = (t11 < t12);
            if (!t13) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t14 = gm_l3;
                        gm_l4 = t14;
                        void * t15 = gm_l0;
                        void * t16 = gm_l1;
                        void * t17 = GM_LD(void *, (((uint8_t *)t16) + 8));
                        uint64_t t18 = GM_LD(uint64_t, ((((uint8_t *)t16) + 8) + 8));
                        uint64_t t19 = gm_l4;
                        if ((uintptr_t)t19 >= (uintptr_t)(t18)) gm_trap_index((uintptr_t)t19, t18, gm_src10tru552hn49, 314, 17);
                        void * t20 = (void *)(((uint8_t *)t17) + (uintptr_t)t19 * 144);
                        gm_b0_1 t21 = gm_f_stmt_j3ls0p3((void *)NULL, cx, NULL, t15, t20);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t22 = gm_l0;
                        void * t23 = gm_l1;
                        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 8));
                        uint64_t t25 = GM_LD(uint64_t, ((((uint8_t *)t23) + 8) + 8));
                        uint64_t t26 = gm_l4;
                        if ((uintptr_t)t26 >= (uintptr_t)(t25)) gm_trap_index((uintptr_t)t26, t25, gm_src10tru552hn49, 315, 30);
                        void * t27 = (void *)(((uint8_t *)t24) + (uintptr_t)t26 * 144);
                        void * t28 = gm_l1;
                        uint32_t t29 = GM_LD(uint32_t, ((((uint8_t *)t28) + 24) + 8));
                        gm_b0_1 t30 = gm_f_unused_local_decl_2l8feim((void *)NULL, cx, NULL, t22, t27, t29);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 313, 5);
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
    void * t35 = gm_l1;
    uint64_t t36 = GM_LD(uint64_t, ((((uint8_t *)t35) + 8) + 8));
    uint8_t t37 = (t36 > ((uint64_t)0ULL));
    if (t37) {
        void * t38 = gm_l1;
        void * t39 = GM_LD(void *, (((uint8_t *)t38) + 8));
        uint64_t t40 = GM_LD(uint64_t, ((((uint8_t *)t38) + 8) + 8));
        void * t41 = gm_l1;
        uint64_t t42 = GM_LD(uint64_t, ((((uint8_t *)t41) + 8) + 8));
        uint64_t r43; bool o44 = __builtin_sub_overflow(t42, ((uint64_t)1ULL), &r43);
        if (o44) gm_trap("integer overflow in `-`", gm_src10tru552hn49, 318, 26);
        uint64_t t45 = r43;
        if ((uintptr_t)t45 >= (uintptr_t)(t40)) gm_trap_index((uintptr_t)t45, t40, gm_src10tru552hn49, 318, 18);
        void * t46 = (void *)(((uint8_t *)t39) + (uintptr_t)t45 * 144);
        gm_l5 = t46;
        {
            {
                void * t47 = gm_l5;
                uint8_t t48 = GM_LD(uint8_t, ((((uint8_t *)t47) + 8) + 0));
                uint8_t t49 = (uint8_t)(t48 == ((uint8_t)3ULL));
                if (!t49) {
                    goto gm_lb7;
                }
                uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)t47) + 8) + 8));
                gm_l6 = t50;
                uint8_t t51 = gm_l6;
                if (t51) {
                    gm_b16_8 t52;
                    GM_ST(void *, (uint8_t *)&t52, (void *)(gm_s5gkblcfbl1mv));
                    GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(120));
                    gm_l7 = t52;
                } else {
                    gm_b16_8 t53;
                    GM_ST(void *, (uint8_t *)&t53, (void *)(gm_s5cr5924ev095));
                    GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(64));
                    gm_l7 = t53;
                }
                void * t54 = gm_l0;
                void * t55 = gm_l5;
                gm_tl874t2atnlh3 t56 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t55) + 128));
                gm_b16_8 t57;
                GM_ST(void *, (uint8_t *)&t57, (void *)(gm_sob1n7uh7kpps));
                GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(13));
                gm_b16_8 t58 = gm_l7;
                gm_t4gut2c9fj1kt t59;
                gm_f_lint_diag_4vra4qu((void *)&t59, (void *)NULL, cx, NULL, t56, t57, t58);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b0_1 t60 = gm_f_push_l0e5v4c((void *)NULL, cx, NULL, t54, &t59);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_lb6;
            }
gm_lb7:;
        }
gm_lb6:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_capture_abs7k4r(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint8_t a2) {
    struct gm_fr_capture_abs7k4r F;
    void * gm_l0; /* w */
    gm_t08olq97rscn3 gm_l1; /* name */
    uint8_t gm_l2; /* removable */
    gm_b16_8 gm_l3; /* src */
    gm_b16_8 gm_l5; /* trimmed */
    gm_b16_8 gm_l6; /* rest */
    uint64_t gm_l7; /* k */
    uint64_t gm_l8; /* end */
    GM_STACK_CHECK(&F, 699u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l9;
    {
        void * t7 = gm_l0;
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(gm_sjg1ri23hm753));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(5));
        gm_t9qkh0rsooc8d t9;
        GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)1ULL));
        gm_b0_1 t10 = gm_f_check_name_hf7dide((void *)NULL, cx, NULL, t7, &(*(gm_t08olq97rscn3 *)((uint8_t *)a1)), t9, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    void * t11 = gm_l0;
    void * t12 = GM_LD(void *, ((uint8_t *)t11));
    uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)a1) + 16));
    void * t14 = gm_f_file_o15fqku(t12, t13);
    void * t15 = GM_LD(void *, (((uint8_t *)t14) + 16));
    uint64_t t16 = GM_LD(uint64_t, ((((uint8_t *)t14) + 16) + 8));
    uint32_t t17 = GM_LD(uint32_t, ((((uint8_t *)a1) + 16) + 8));
    uint64_t t18 = (uint64_t)t17;
    uint64_t t19 = (uintptr_t)t18;
    uint64_t t20 = (uintptr_t)t16;
    if (t19 > t20 || t20 > (uintptr_t)t16) gm_trap_slice(t19, t20, t16, gm_src10tru552hn49, 297, 12);
    uint64_t t21 = ((uintptr_t)t16) - (uintptr_t)t18;
    gm_b16_8 t22;
    GM_ST(void *, (uint8_t *)&t22, (void *)(((uint8_t *)t15 + (uintptr_t)t18 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(t21));
    gm_l3 = t22;
    {
        memset(&F.l4, 0, sizeof F.l4);
    }
    gm_b16_8 t23 = gm_l3;
    gm_b16_8 t24 = gm_f_trim_start_179crak(t23);
    gm_l5 = t24;
    {
        {
            uint8_t t25 = gm_l2;
            if (!t25) {
                goto gm_lb2;
            }
            gm_b16_8 t26 = gm_l5;
            gm_b16_8 t27;
            GM_ST(void *, (uint8_t *)&t27, (void *)(gm_sm8obe455p2d9));
            GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(2));
            uint8_t t28 = gm_f_starts_with_6vvt01e(t26, t27);
            if (!t28) {
                goto gm_lb2;
            }
            void * t29 = GM_LD(void *, ((uint8_t *)&gm_l5));
            uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint64_t t31 = (uintptr_t)((uint64_t)2ULL);
            uint64_t t32 = (uintptr_t)t30;
            if (t31 > t32 || t32 > (uintptr_t)t30) gm_trap_slice(t31, t32, t30, gm_src10tru552hn49, 301, 17);
            uint64_t t33 = ((uintptr_t)t30) - (uintptr_t)((uint64_t)2ULL);
            gm_b16_8 t34;
            GM_ST(void *, (uint8_t *)&t34, (void *)(((uint8_t *)t29 + (uintptr_t)((uint64_t)2ULL) * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(t33));
            gm_l6 = t34;
            gm_l7 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t35 = gm_l7;
                    uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    uint8_t t37 = (t35 < t36);
                    if (!t37) {
                        goto gm_lb3;
                    }
                    void * t38 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    uint64_t t40 = gm_l7;
                    uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)t38) + (uintptr_t)t40 * 1));
                    gm_tqs5p4n2tq19v t42;
                    GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)32ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t42 + 1), ((uint8_t)9ULL));
                    uint8_t t43 = 0;
                    if (gm_eqjmccqdb90f(((uint8_t *)&t42) + 0, ((uint8_t *)&t41))) t43 = 1;
                    if (gm_eqjmccqdb90f(((uint8_t *)&t42) + 1, ((uint8_t *)&t41))) t43 = 1;
                    uint8_t t44 = (uint8_t)(t43);
                    if (!t44) {
                        goto gm_lb3;
                    }
                    {
                        uint64_t t45 = gm_l7;
                        uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
                        if (o47) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 303, 57);
                        uint64_t t48 = r46;
                        gm_l7 = t48;
                    }
gm_lb5:;
                }
            }
gm_lb3:;
            uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint64_t t51 = gm_l7;
            uint64_t r52; bool o53 = __builtin_sub_overflow(t50, t51, &r52);
            if (o53) gm_trap("integer overflow in `-`", gm_src10tru552hn49, 304, 27);
            uint64_t t54 = r52;
            uint64_t r55; bool o56 = __builtin_sub_overflow(t49, t54, &r55);
            if (o56) gm_trap("integer overflow in `-`", gm_src10tru552hn49, 304, 16);
            uint64_t t57 = r55;
            gm_l8 = t57;
            {
                uint32_t t58 = GM_LD(uint32_t, (((uint8_t *)a1) + 16));
                uint32_t t59 = GM_LD(uint32_t, ((((uint8_t *)a1) + 16) + 4));
                uint32_t t60 = GM_LD(uint32_t, ((((uint8_t *)a1) + 16) + 8));
                uint64_t t61 = gm_l8;
                uint32_t t62 = (uint32_t)t61;
                uint32_t r63; bool o64 = __builtin_add_overflow(t60, t62, &r63);
                if (o64) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 305, 76);
                uint32_t t65 = r63;
                gm_tl874t2atnlh3 t66;
                GM_ST(uint32_t, ((uint8_t *)&t66 + 0), t58);
                GM_ST(uint32_t, ((uint8_t *)&t66 + 4), t59);
                GM_ST(uint32_t, ((uint8_t *)&t66 + 8), t65);
                gm_b16_8 t67;
                GM_ST(void *, (uint8_t *)&t67, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(0));
                gm_tbgd14esf0jvi t68;
                GM_ST(uint32_t, (uint8_t *)&t68 + 12, 0);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t68 + 0), t66);
                GM_ST(gm_b16_8, ((uint8_t *)&t68 + 16), t67);
                GM_ST(uint8_t, (uint8_t *)&F.l4 + 33, 0);
                GM_ST(uint16_t, (uint8_t *)&F.l4 + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&F.l4 + 36, 0);
                GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&F.l4), t68);
                ((uint8_t *)&F.l4)[32] = 1;
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        void * t69 = gm_l0;
        void * t70 = (void *)((((uint8_t *)t69) + 8) + 40);
        gm_tl874t2atnlh3 t71 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 16));
        gm_tnvnsr1bfvtbd t72 = GM_LD(gm_tnvnsr1bfvtbd, ((uint8_t *)&F.l4));
        gm_t5jnq73fben73 t73;
        GM_ST(gm_tnvnsr1bfvtbd, ((uint8_t *)&t73 + 0), t72);
        gm_tl5vm0tcqvdvb t74;
        GM_ST(uint8_t, (uint8_t *)&t74 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t74 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t74 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t5jnq73fben73, ((uint8_t *)&t74 + 8), t73);
        gm_tftb1odc739t5 t75 = gm_f_put_amueool(t70, GM_CONV(gm_cseahvg8v7p0vs, t71), &t74);
        gm_l9 = t75;
    }
    uint8_t t76 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (!t76) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_src10tru552hn49, 307, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_name_hf7dide(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3) {
    struct gm_fr_check_name_hf7dide F;
    void * gm_l0; /* w */
    gm_t08olq97rscn3 gm_l1; /* name */
    gm_t9qkh0rsooc8d gm_l2; /* case */
    gm_b16_8 gm_l3; /* what */
    gm_b16_8 gm_l4; /* s */
    gm_b16_8 gm_l5; /* want */
    GM_STACK_CHECK(&F, 758u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l7;
    gm_tabb710d9mq2e gm_l8;
    gm_tabb710d9mq2e gm_l9;
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)a1));
    gm_l4 = t9;
    {
        for (;;) {
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t11 = (t10 > ((uint64_t)0ULL));
            if (!t11) {
                goto gm_lb1;
            }
            void * t12 = GM_LD(void *, ((uint8_t *)&gm_l4));
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)t12) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint8_t t15 = ((t14 == ((uint8_t)95ULL)));
            if (!t15) {
                goto gm_lb1;
            }
            {
                void * t16 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t18 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t19 = (uintptr_t)t17;
                if (t18 > t19 || t19 > (uintptr_t)t17) gm_trap_slice(t18, t19, t17, gm_src10tru552hn49, 188, 43);
                uint64_t t20 = ((uintptr_t)t17) - (uintptr_t)((uint64_t)1ULL);
                gm_b16_8 t21;
                GM_ST(void *, (uint8_t *)&t21, (void *)(((uint8_t *)t16 + (uintptr_t)((uint64_t)1ULL) * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(t20));
                gm_l4 = t21;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
    uint8_t t23 = ((t22 == ((uint64_t)0ULL)));
    if (t23) {
        gm_l7 = ((uint8_t)1);
    } else {
        gm_t9qkh0rsooc8d t24 = gm_l2;
        gm_b16_8 t25 = gm_l4;
        uint8_t t26 = gm_f_accepts_3h4b27e(t24, t25);
        gm_l7 = t26;
    }
    uint8_t t27 = gm_l7;
    if (t27) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    {
        {
            uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t29 = (uint8_t)(t28 == ((uint8_t)0ULL));
            if (!t29) {
                goto gm_lb5;
            }
            gm_b16_8 t30;
            GM_ST(void *, (uint8_t *)&t30, (void *)(gm_sr1bm2v312fv6));
            GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(14));
            gm_l5 = t30;
            goto gm_lb4;
        }
gm_lb5:;
        {
            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t32 = (uint8_t)(t31 == ((uint8_t)1ULL));
            if (!t32) {
                goto gm_lb6;
            }
            gm_b16_8 t33;
            GM_ST(void *, (uint8_t *)&t33, (void *)(gm_suls9doo5jh0i));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(10));
            gm_l5 = t33;
            goto gm_lb4;
        }
gm_lb6:;
        {
            uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t35 = (uint8_t)(t34 == ((uint8_t)2ULL));
            if (!t35) {
                goto gm_lb7;
            }
            gm_b16_8 t36;
            GM_ST(void *, (uint8_t *)&t36, (void *)(gm_spg4ktuno2hf7));
            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(50));
            gm_l5 = t36;
            goto gm_lb4;
        }
gm_lb7:;
        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 190, 13);
    }
gm_lb4:;
    {
        gm_tl874t2atnlh3 t37 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 16));
        gm_b16_8 t38;
        GM_ST(void *, (uint8_t *)&t38, (void *)(gm_sqmr87iegdcns));
        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(6));
        gm_b16_8 t39 = gm_l3;
        gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)a1));
        gm_b16_8 t41 = gm_l5;
        gm_tfeab279c16rr t42;
        GM_ST(gm_b16_8, ((uint8_t *)&t42 + 0), t39);
        GM_ST(gm_b16_8, ((uint8_t *)&t42 + 16), t40);
        GM_ST(gm_b16_8, ((uint8_t *)&t42 + 32), t41);
        gm_f_format_t472cpe((void *)&gm_l8, (void *)NULL, cx, NULL, &t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t43 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
        if (!t43) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_src10tru552hn49, 195, 41);
        gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
        gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
        gm_t4gut2c9fj1kt t46;
        gm_f_lint_diag_4vra4qu((void *)&t46, (void *)NULL, cx, NULL, t37, t38, t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l6 = t46;
    }
    {
        void * t47 = (void *)((uint8_t *)&F.l6);
        gm_t9qkh0rsooc8d t48 = gm_l2;
        gm_b16_8 t49 = gm_l4;
        gm_b16_8 t50 = gm_f_convert_c7mdt88((void *)NULL, cx, NULL, t48, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t51;
        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 0), t50);
        gm_f_format_ooibcq2((void *)&gm_l9, (void *)NULL, cx, NULL, t51);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t52 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
        if (!t52) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_src10tru552hn49, 196, 16);
        gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
        gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
        gm_f_add_note_46v502s(t47, t54);
    }
    {
        void * t55 = gm_l0;
        gm_b0_1 t56 = gm_f_push_l0e5v4c((void *)NULL, cx, NULL, t55, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l6)));
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
__attribute__((aligned(4))) gm_b0_1 gm_f_collapsible_else_ehhjtii(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_collapsible_else_ehhjtii F;
    void * gm_l0; /* w */
    void * gm_l1; /* i */
    void * gm_l4; /* inner */
    uint32_t gm_l6; /* file */
    gm_b16_8 gm_l7; /* before */
    gm_b16_8 gm_l8; /* after */
    gm_b16_8 gm_l9; /* inner_text */
    gm_b16_8 gm_l10; /* t */
    GM_STACK_CHECK(&F, 781u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l11;
    void * t11 = gm_l1;
    uint8_t t12 = (uint8_t)(((((uint8_t *)t11) + 16))[48] != 0);
    if (t12) {
        memmove(((uint8_t *)&F.l2), (((uint8_t *)t11) + 16), 48);
    } else {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    {
        {
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)0ULL));
            if (!t14) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l3), (((uint8_t *)&F.l2) + 8), 40);
            uint64_t t15 = GM_LD(uint64_t, ((((uint8_t *)&F.l3) + 8) + 8));
            uint8_t t16 = (!(t15 == ((uint64_t)1ULL)));
            if (t16) {
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            {
                {
                    void * t17 = GM_LD(void *, (((uint8_t *)&F.l3) + 8));
                    uint64_t t18 = GM_LD(uint64_t, ((((uint8_t *)&F.l3) + 8) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t18)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t18, gm_src10tru552hn49, 707, 21);
                    uint8_t t19 = GM_LD(uint8_t, (((((uint8_t *)t17) + (uintptr_t)((uint64_t)0ULL) * 144) + 8) + 0));
                    uint8_t t20 = (uint8_t)(t19 == ((uint8_t)2ULL));
                    if (!t20) {
                        goto gm_lb4;
                    }
                    void * t21 = GM_LD(void *, (((((uint8_t *)t17) + (uintptr_t)((uint64_t)0ULL) * 144) + 8) + 8));
                    gm_l4 = t21;
                    void * t22 = gm_l4;
                    {
                        {
                            uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
                            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)40ULL));
                            if (!t24) {
                                goto gm_lb6;
                            }
                            gm_l11 = ((uint8_t)0);
                            goto gm_lb5;
                        }
gm_lb6:;
                        {
                            gm_l11 = ((uint8_t)1);
                            goto gm_lb5;
                        }
gm_lb7:;
                        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 708, 8);
                    }
gm_lb5:;
                    uint8_t t25 = gm_l11;
                    if (t25) {
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    {
                        gm_tl874t2atnlh3 t26 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l3) + 24));
                        gm_b16_8 t27;
                        GM_ST(void *, (uint8_t *)&t27, (void *)(gm_sotb03b2eqtp5));
                        GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(19));
                        gm_b16_8 t28;
                        GM_ST(void *, (uint8_t *)&t28, (void *)(gm_s816n04ro16nc));
                        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(47));
                        gm_t4gut2c9fj1kt t29;
                        gm_f_lint_diag_4vra4qu((void *)&t29, (void *)NULL, cx, NULL, t26, t27, t28);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l5 = t29;
                    }
                    uint32_t t30 = GM_LD(uint32_t, (((uint8_t *)&F.l3) + 24));
                    gm_l6 = t30;
                    void * t31 = gm_l0;
                    uint32_t t32 = gm_l6;
                    uint32_t t33 = GM_LD(uint32_t, ((((uint8_t *)&F.l3) + 24) + 4));
                    void * t34 = gm_l4;
                    uint32_t t35 = GM_LD(uint32_t, ((((uint8_t *)t34) + 72) + 4));
                    gm_tl874t2atnlh3 t36;
                    GM_ST(uint32_t, ((uint8_t *)&t36 + 0), t32);
                    GM_ST(uint32_t, ((uint8_t *)&t36 + 4), t33);
                    GM_ST(uint32_t, ((uint8_t *)&t36 + 8), t35);
                    gm_b16_8 t37 = gm_f_text_kspioh6(t31, GM_CONV(gm_cseahvg8v7p0vs, t36));
                    gm_l7 = t37;
                    void * t38 = gm_l0;
                    uint32_t t39 = gm_l6;
                    void * t40 = gm_l4;
                    uint32_t t41 = GM_LD(uint32_t, ((((uint8_t *)t40) + 72) + 8));
                    uint32_t t42 = GM_LD(uint32_t, ((((uint8_t *)&F.l3) + 24) + 8));
                    gm_tl874t2atnlh3 t43;
                    GM_ST(uint32_t, ((uint8_t *)&t43 + 0), t39);
                    GM_ST(uint32_t, ((uint8_t *)&t43 + 4), t41);
                    GM_ST(uint32_t, ((uint8_t *)&t43 + 8), t42);
                    gm_b16_8 t44 = gm_f_text_kspioh6(t38, GM_CONV(gm_cseahvg8v7p0vs, t43));
                    gm_l8 = t44;
                    void * t45 = gm_l0;
                    void * t46 = gm_l4;
                    gm_tl874t2atnlh3 t47 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t46) + 72));
                    gm_b16_8 t48 = gm_f_text_kspioh6(t45, GM_CONV(gm_cseahvg8v7p0vs, t47));
                    gm_l9 = t48;
                    {
                        {
                            gm_b16_8 t49 = gm_l7;
                            uint8_t t50 = gm_f_blank_ineorlq(t49);
                            if (!t50) {
                                goto gm_lb9;
                            }
                            gm_b16_8 t51 = gm_l8;
                            uint8_t t52 = gm_f_blank_ineorlq(t51);
                            if (!t52) {
                                goto gm_lb9;
                            }
                            gm_b16_8 t53 = gm_l9;
                            uint8_t t54 = 0;
                            uint8_t t55 = ((uint8_t)96ULL);
                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t53) + 8)); i++) if (gm_eqjmccqdb90f(((uint8_t *)GM_LD(void *, ((uint8_t *)&t53))) + i * 1, ((uint8_t *)&t55))) { t54 = 1; break; }
                            uint8_t t56 = (uint8_t)(!t54);
                            if (!t56) {
                                goto gm_lb9;
                            }
                            gm_b16_8 t57 = gm_l9;
                            gm_b16_8 t58;
                            GM_ST(void *, (uint8_t *)&t58, (void *)(gm_sm7emfdvcb7c2));
                            GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(2));
                            uint8_t t59 = gm_f_contains_gkki4kq(t57, t58);
                            uint8_t t60 = (uint8_t)!t59;
                            if (!t60) {
                                goto gm_lb9;
                            }
                            gm_b16_8 t61 = gm_l9;
                            void * t62 = gm_l0;
                            void * t63 = gm_l4;
                            gm_tl874t2atnlh3 t64 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t63) + 72));
                            uint64_t t65 = gm_f_indent_of_p7r0sg5(t62, GM_CONV(gm_cseahvg8v7p0vs, t64));
                            void * t66 = gm_l0;
                            gm_tl874t2atnlh3 t67 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l3) + 24));
                            uint64_t t68 = gm_f_indent_of_p7r0sg5(t66, GM_CONV(gm_cseahvg8v7p0vs, t67));
                            gm_b16_8 t69 = gm_f_dedent_aa5vbel((void *)NULL, cx, NULL, t61, t65, t68);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l10 = t69;
                            void * t70 = (void *)((uint8_t *)&F.l5);
                            gm_b16_8 t71;
                            GM_ST(void *, (uint8_t *)&t71, (void *)(gm_sfna4d5mnmoa8));
                            GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(29));
                            gm_tl874t2atnlh3 t72 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l3) + 24));
                            gm_b16_8 t73 = gm_l10;
                            gm_b0_1 t74 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t70, t71, t72, t73);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb8;
                        }
gm_lb9:;
                    }
gm_lb8:;
                    {
                        void * t75 = gm_l0;
                        gm_b0_1 t76 = gm_f_push_l0e5v4c((void *)NULL, cx, NULL, t75, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l5)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_cond_e0272rd(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2) {
    struct gm_fr_cond_e0272rd F;
    void * gm_l0; /* w */
    void * gm_l1; /* c */
    uint8_t gm_l2; /* head */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    void * gm_l7; /* value */
    void * gm_l8; /* e */
    GM_STACK_CHECK(&F, 257u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t10 = gm_l1;
    gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)t10));
    gm_l3 = t11;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t12 = gm_l4;
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t14 = (t12 < t13);
            if (!t14) {
                goto gm_lb1;
            }
            {
                void * t15 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t17 = gm_l4;
                {
                    {
                        memmove(((uint8_t *)&F.l5), (((uint8_t *)t15) + (uintptr_t)t17 * 80), 80);
                        {
                            {
                                uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                                uint8_t t19 = (uint8_t)(t18 == ((uint8_t)0ULL));
                                if (!t19) {
                                    goto gm_lb7;
                                }
                                memmove(((uint8_t *)&F.l6), (((uint8_t *)&F.l5) + 8), 64);
                                void * t20 = GM_LD(void *, ((((uint8_t *)&F.l5) + 8) + 64));
                                gm_l7 = t20;
                                void * t21 = gm_l0;
                                void * t22 = (void *)((uint8_t *)&F.l6);
                                gm_t9qkh0rsooc8d t23;
                                GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
                                gm_b0_1 t24 = gm_f_pattern_8qtr7os((void *)NULL, cx, NULL, t21, t22, t23);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t25 = gm_l0;
                                void * t26 = gm_l7;
                                gm_b0_1 t27 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t25, t26);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                                uint8_t t29 = (uint8_t)(t28 == ((uint8_t)1ULL));
                                if (!t29) {
                                    goto gm_lb8;
                                }
                                void * t30 = GM_LD(void *, (((uint8_t *)&F.l5) + 8));
                                gm_l8 = t30;
                                void * t31 = gm_l1;
                                uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)t31) + 8));
                                uint8_t t33 = ((t32 == ((uint64_t)1ULL)));
                                if (t33) {
                                    void * t34 = gm_l0;
                                    gm_b16_8 t35;
                                    GM_ST(void *, (uint8_t *)&t35, (void *)(gm_sjuj50mlj6mg0));
                                    GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(13));
                                    void * t36 = gm_l8;
                                    uint8_t t37 = gm_l2;
                                    gm_b0_1 t38 = gm_f_paren_at_av727r5((void *)NULL, cx, NULL, t34, t35, t36, t37);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                void * t39 = gm_l0;
                                void * t40 = gm_l8;
                                gm_b0_1 t41 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t39, t40);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 425, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 424, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t42 = gm_l4;
            uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
            uint64_t t45 = r43;
            gm_l4 = t45;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_convert_c7mdt88(void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0, gm_b16_8 a1) {
    struct gm_fr_convert_c7mdt88 F;
    gm_t9qkh0rsooc8d gm_l0; /* case */
    gm_b16_8 gm_l1; /* s */
    gm_b16_8 gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    uint8_t gm_l7; /* c */
    uint8_t gm_l8; /* <tmp> */
    uint8_t gm_l9; /* prev_lower */
    uint8_t gm_l10; /* next_lower */
    uint8_t gm_l11; /* ends_us */
    GM_STACK_CHECK(&F, 602u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l12;
    uint8_t gm_l13;
    uint8_t gm_l14;
    uint8_t gm_l15;
    uint8_t gm_l16;
    uint8_t gm_l17;
    uint8_t gm_l18;
    gm_tftb1odc739t5 gm_l19;
    gm_tftb1odc739t5 gm_l20;
    gm_tftb1odc739t5 gm_l21;
    gm_trqfvmjtt4sij t16;
    gm_f_list_uvfu1k1((void *)&t16, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t16;
    {
        {
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)0ULL));
            if (!t18) {
                goto gm_lb2;
            }
            gm_b16_8 t19 = gm_l1;
            gm_l3 = t19;
            {
                gm_node wn20 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hldg2pcvebta, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
                gm_b16_8 t23 = gm_l3;
                gm_b16_8 t24;
                GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sfdbfrv46lihd));
                GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(1));
                gm_b0_1 t25 = gm_f_split_obtinjv_s44sj5s((void *)NULL, (&wn20), NULL, t23, t24);
                if (gm_unw.kind) {
                    goto gm_ll22;
                }
            }
            goto gm_ls26;
gm_ll22:;
            if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we21; }
            goto gm_lad;
gm_ls26:;
gm_lb3:;
we21:;
            goto gm_lb1;
        }
gm_lb2:;
        {
            {
                {
                    uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t28 = (uint8_t)(t27 == ((uint8_t)1ULL));
                    if (!t28) {
                        goto gm_lb6;
                    }
                    goto gm_lb5;
                }
gm_lb6:;
                {
                    uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t30 = (uint8_t)(t29 == ((uint8_t)2ULL));
                    if (!t30) {
                        goto gm_lb7;
                    }
                    goto gm_lb5;
                }
gm_lb7:;
                goto gm_lb4;
            }
gm_lb5:;
            gm_b16_8 t31 = gm_l1;
            gm_l4 = t31;
            gm_l5 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t33 = gm_l5;
                    uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint8_t t35 = (t33 < t34);
                    if (!t35) {
                        goto gm_lb8;
                    }
                    {
                        uint64_t t36 = gm_l5;
                        void * t37 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        uint64_t t39 = gm_l5;
                        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)t37) + (uintptr_t)t39 * 1));
                        gm_tf5srh4mp4b3i t41;
                        GM_ST(uint8_t, (uint8_t *)&t41 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t41 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t41 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t41 + 0), t36);
                        GM_ST(uint8_t, ((uint8_t *)&t41 + 8), t40);
                        gm_l12 = t41;
                        {
                            {
                                uint64_t t42 = GM_LD(uint64_t, ((uint8_t *)&gm_l12));
                                gm_l6 = t42;
                                uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 8));
                                gm_l7 = t43;
                                uint8_t t44 = gm_l7;
                                uint8_t t45 = gm_f_is_upper_68kn81r(t44);
                                if (t45) {
                                    uint64_t t46 = gm_l6;
                                    uint8_t t47 = (t46 > ((uint64_t)0ULL));
                                    if (t47) {
                                        void * t48 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                        uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                        uint64_t t50 = gm_l6;
                                        uint64_t r51; bool o52 = __builtin_sub_overflow(t50, ((uint64_t)1ULL), &r51);
                                        if (o52) gm_trap("integer overflow in `-`", gm_src10tru552hn49, 820, 57);
                                        uint64_t t53 = r51;
                                        if ((uintptr_t)t53 >= (uintptr_t)(t49)) gm_trap_index((uintptr_t)t53, t49, gm_src10tru552hn49, 820, 55);
                                        uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)t48) + (uintptr_t)t53 * 1));
                                        uint8_t t55 = gm_f_is_lower_q12rpq9(t54);
                                        if (t55) {
                                            gm_l14 = ((uint8_t)1);
                                        } else {
                                            void * t56 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                            uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                            uint64_t t58 = gm_l6;
                                            uint64_t r59; bool o60 = __builtin_sub_overflow(t58, ((uint64_t)1ULL), &r59);
                                            if (o60) gm_trap("integer overflow in `-`", gm_src10tru552hn49, 820, 70);
                                            uint64_t t61 = r59;
                                            if ((uintptr_t)t61 >= (uintptr_t)(t57)) gm_trap_index((uintptr_t)t61, t57, gm_src10tru552hn49, 820, 68);
                                            uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)t56) + (uintptr_t)t61 * 1));
                                            gm_l8 = t62;
                                            uint8_t t63 = gm_l8;
                                            uint8_t t64 = (((uint8_t)48ULL) <= t63);
                                            if (t64) {
                                                uint8_t t65 = gm_l8;
                                                uint8_t t66 = (t65 <= ((uint8_t)57ULL));
                                                gm_l15 = t66;
                                            } else {
                                                gm_l15 = ((uint8_t)0);
                                            }
                                            uint8_t t67 = gm_l15;
                                            gm_l14 = t67;
                                        }
                                        uint8_t t68 = gm_l14;
                                        gm_l13 = t68;
                                    } else {
                                        gm_l13 = ((uint8_t)0);
                                    }
                                    uint8_t t69 = gm_l13;
                                    gm_l9 = t69;
                                    uint64_t t70 = gm_l6;
                                    uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
                                    if (o72) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 821, 35);
                                    uint64_t t73 = r71;
                                    uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                    uint8_t t75 = (t73 < t74);
                                    if (t75) {
                                        void * t76 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                        uint64_t t77 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                        uint64_t t78 = gm_l6;
                                        uint64_t r79; bool o80 = __builtin_add_overflow(t78, ((uint64_t)1ULL), &r79);
                                        if (o80) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 821, 64);
                                        uint64_t t81 = r79;
                                        if ((uintptr_t)t81 >= (uintptr_t)(t77)) gm_trap_index((uintptr_t)t81, t77, gm_src10tru552hn49, 821, 62);
                                        uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)t76) + (uintptr_t)t81 * 1));
                                        uint8_t t83 = gm_f_is_lower_q12rpq9(t82);
                                        gm_l16 = t83;
                                    } else {
                                        gm_l16 = ((uint8_t)0);
                                    }
                                    uint8_t t84 = gm_l16;
                                    gm_l10 = t84;
                                    uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                                    uint8_t t86 = (t85 > ((uint64_t)0ULL));
                                    if (t86) {
                                        void * t87 = GM_LD(void *, ((uint8_t *)&F.l2));
                                        uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                                        uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                                        uint64_t r90; bool o91 = __builtin_sub_overflow(t89, ((uint64_t)1ULL), &r90);
                                        if (o91) gm_trap("integer overflow in `-`", gm_src10tru552hn49, 822, 64);
                                        uint64_t t92 = r90;
                                        if ((uintptr_t)t92 >= (uintptr_t)(t88)) gm_trap_index((uintptr_t)t92, t88, gm_src10tru552hn49, 822, 54);
                                        uint8_t t93 = GM_LD(uint8_t, (((uint8_t *)t87) + (uintptr_t)t92 * 1));
                                        uint8_t t94 = ((t93 == ((uint8_t)95ULL)));
                                        gm_l17 = t94;
                                    } else {
                                        gm_l17 = ((uint8_t)0);
                                    }
                                    uint8_t t95 = gm_l17;
                                    gm_l11 = t95;
                                    {
                                        {
                                            uint64_t t96 = gm_l6;
                                            uint8_t t97 = (t96 > ((uint64_t)0ULL));
                                            if (!t97) {
                                                goto gm_lb14;
                                            }
                                            uint8_t t98 = gm_l11;
                                            uint8_t t99 = (uint8_t)!t98;
                                            if (!t99) {
                                                goto gm_lb14;
                                            }
                                            uint8_t t100 = gm_l9;
                                            if (t100) {
                                                gm_l18 = ((uint8_t)1);
                                            } else {
                                                uint8_t t101 = gm_l10;
                                                gm_l18 = t101;
                                            }
                                            uint8_t t102 = gm_l18;
                                            if (!t102) {
                                                goto gm_lb14;
                                            }
                                            void * t103 = (void *)((uint8_t *)&F.l2);
                                            gm_tftb1odc739t5 t104 = gm_f_push_t83rg2m(t103, ((uint8_t)95ULL));
                                            gm_l19 = t104;
                                            uint8_t t105 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                                            if (!t105) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_src10tru552hn49, 823, 79);
                                            goto gm_lb13;
                                        }
gm_lb14:;
                                    }
gm_lb13:;
                                    void * t106 = (void *)((uint8_t *)&F.l2);
                                    uint8_t t107 = gm_l7;
                                    uint8_t r108; bool o109 = __builtin_add_overflow(t107, ((uint8_t)32ULL), &r108);
                                    if (o109) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 824, 30);
                                    uint8_t t110 = r108;
                                    gm_tftb1odc739t5 t111 = gm_f_push_t83rg2m(t106, t110);
                                    gm_l20 = t111;
                                    uint8_t t112 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                                    if (!t112) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_src10tru552hn49, 824, 21);
                                } else {
                                    void * t113 = (void *)((uint8_t *)&F.l2);
                                    uint8_t t114 = gm_l7;
                                    gm_tftb1odc739t5 t115 = gm_f_push_t83rg2m(t113, t114);
                                    gm_l21 = t115;
                                    uint8_t t116 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                                    if (!t116) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_src10tru552hn49, 826, 21);
                                }
                                goto gm_lb11;
                            }
gm_lb12:;
                            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 818, 13);
                        }
gm_lb11:;
                    }
gm_lb10:;
                    uint64_t t117 = gm_l5;
                    uint64_t r118; bool o119 = __builtin_add_overflow(t117, ((uint64_t)1ULL), &r118);
                    uint64_t t120 = r118;
                    gm_l5 = t120;
                }
            }
gm_lb8:;
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 809, 5);
    }
gm_lb1:;
    gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&t121));
    F.ret = t122;
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
inline __attribute__((aligned(4))) void gm_f_declared_kind_4vgnqmf(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_declared_kind_4vgnqmf F;
    void * gm_l0; /* e */
    void * gm_l1; /* inner */
    void * gm_l2; /* i */
    gm_t08olq97rscn3 gm_l3; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 283u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_th2lqbo77pvsc gm_l4;
    gm_th2lqbo77pvsc gm_l5;
    uint8_t gm_l6;
    gm_th2lqbo77pvsc gm_l7;
    void * t23 = gm_l0;
    gm_l1 = t23;
    {
        {
            void * t24 = gm_l1;
            uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t24) + 8) + 0));
            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)35ULL));
            if (!t26) {
                goto gm_lb2;
            }
            void * t27 = GM_LD(void *, (((((uint8_t *)t24) + 8) + 8) + 16));
            gm_l2 = t27;
            void * t28 = gm_l2;
            gm_l1 = t28;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t29 = gm_l1;
    {
        {
            {
                {
                    uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t31 = (uint8_t)(t30 == ((uint8_t)32ULL));
                    if (!t31) {
                        goto gm_lb6;
                    }
                    goto gm_lb5;
                }
gm_lb6:;
                {
                    uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t33 = (uint8_t)(t32 == ((uint8_t)33ULL));
                    if (!t33) {
                        goto gm_lb7;
                    }
                    goto gm_lb5;
                }
gm_lb7:;
                goto gm_lb4;
            }
gm_lb5:;
            gm_b16_8 t34;
            GM_ST(void *, (uint8_t *)&t34, (void *)(gm_s2737i1iq57nf));
            GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(4));
            GM_ST(uint8_t, (uint8_t *)&gm_l4 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&gm_l4 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&gm_l4 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&gm_l4), t34);
            ((uint8_t *)&gm_l4)[16] = 1;
            goto gm_lb3;
        }
gm_lb4:;
        {
            uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
            uint8_t t36 = (uint8_t)(t35 == ((uint8_t)36ULL));
            if (!t36) {
                goto gm_lb8;
            }
            void * t37 = gm_l0;
            {
                {
                    uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)t37) + 8) + 0));
                    uint8_t t39 = (uint8_t)(t38 == ((uint8_t)36ULL));
                    if (!t39) {
                        goto gm_lb10;
                    }
                    gm_l6 = ((uint8_t)1);
                    goto gm_lb9;
                }
gm_lb10:;
                {
                    gm_l6 = ((uint8_t)0);
                    goto gm_lb9;
                }
gm_lb11:;
                gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 354, 22);
            }
gm_lb9:;
            uint8_t t40 = gm_l6;
            if (t40) {
                gm_b16_8 t41;
                GM_ST(void *, (uint8_t *)&t41, (void *)(gm_sq0dqiq83b924));
                GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(8));
                GM_ST(uint8_t, (uint8_t *)&gm_l5 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&gm_l5 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&gm_l5 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&gm_l5), t41);
                ((uint8_t *)&gm_l5)[16] = 1;
            } else {
                memset(&gm_l5, 0, sizeof gm_l5);
            }
            memmove(((uint8_t *)&gm_l4), ((uint8_t *)&gm_l5), 24);
            goto gm_lb3;
        }
gm_lb8:;
        {
            {
                {
                    uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t43 = (uint8_t)(t42 == ((uint8_t)25ULL));
                    if (!t43) {
                        goto gm_lb14;
                    }
                    goto gm_lb13;
                }
gm_lb14:;
                {
                    uint8_t t44 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t45 = (uint8_t)(t44 == ((uint8_t)26ULL));
                    if (!t45) {
                        goto gm_lb15;
                    }
                    goto gm_lb13;
                }
gm_lb15:;
                {
                    uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t47 = (uint8_t)(t46 == ((uint8_t)27ULL));
                    if (!t47) {
                        goto gm_lb16;
                    }
                    goto gm_lb13;
                }
gm_lb16:;
                {
                    uint8_t t48 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t49 = (uint8_t)(t48 == ((uint8_t)29ULL));
                    if (!t49) {
                        goto gm_lb17;
                    }
                    goto gm_lb13;
                }
gm_lb17:;
                {
                    uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t51 = (uint8_t)(t50 == ((uint8_t)30ULL));
                    if (!t51) {
                        goto gm_lb18;
                    }
                    goto gm_lb13;
                }
gm_lb18:;
                {
                    uint8_t t52 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t53 = (uint8_t)(t52 == ((uint8_t)31ULL));
                    if (!t53) {
                        goto gm_lb19;
                    }
                    goto gm_lb13;
                }
gm_lb19:;
                goto gm_lb12;
            }
gm_lb13:;
            gm_b16_8 t54;
            GM_ST(void *, (uint8_t *)&t54, (void *)(gm_skr6p9lfc5ftp));
            GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(10));
            GM_ST(uint8_t, (uint8_t *)&gm_l4 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&gm_l4 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&gm_l4 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&gm_l4), t54);
            ((uint8_t *)&gm_l4)[16] = 1;
            goto gm_lb3;
        }
gm_lb12:;
        {
            uint8_t t55 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
            uint8_t t56 = (uint8_t)(t55 == ((uint8_t)6ULL));
            if (!t56) {
                goto gm_lb20;
            }
            {
                memmove(((uint8_t *)&gm_l3), ((((uint8_t *)t29) + 8) + 8), 32);
            }
            gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
            uint8_t t58 = gm_f_is_builtin_type_name_v5ttoka(t57);
            if (t58) {
                gm_b16_8 t59;
                GM_ST(void *, (uint8_t *)&t59, (void *)(gm_skr6p9lfc5ftp));
                GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(10));
                GM_ST(uint8_t, (uint8_t *)&gm_l7 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&gm_l7 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&gm_l7 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&gm_l7), t59);
                ((uint8_t *)&gm_l7)[16] = 1;
            } else {
                memset(&gm_l7, 0, sizeof gm_l7);
            }
            {
                memmove(((uint8_t *)&gm_l4), ((uint8_t *)&gm_l7), 24);
            }
            goto gm_lb3;
        }
gm_lb20:;
        {
            memset(&gm_l4, 0, sizeof gm_l4);
            goto gm_lb3;
        }
gm_lb21:;
        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 352, 12);
    }
gm_lb3:;
    memmove(gm_out, ((uint8_t *)&gm_l4), 24);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_declared_kind_4vgnqmf_c(void * a0) { gm_th2lqbo77pvsc gm_r; gm_f_declared_kind_4vgnqmf(&gm_r, a0); return gm_r; }
inline __attribute__((aligned(4))) gm_b16_8 gm_f_dedent_aa5vbel(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1, uint64_t a2) {
    struct gm_fr_dedent_aa5vbel F;
    gm_b16_8 gm_l0; /* t */
    uint64_t gm_l1; /* from */
    uint64_t gm_l2; /* to */
    gm_b16_8 gm_l6; /* <tmp> */
    GM_STACK_CHECK(&F, 224u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint64_t t3 = gm_l1;
    uint64_t t4 = gm_l2;
    uint64_t r5; bool o6 = __builtin_sub_overflow(t3, t4, &r5);
    uint64_t t7 = o6 ? (0 ? ((uint64_t)18446744073709551615ULL) : ((uint64_t)0ULL)) : r5;
    F.l3 = t7;
    gm_trqfvmjtt4sij t8;
    gm_f_list_uvfu1k1((void *)&t8, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l4 = t8;
    F.l5 = ((uint8_t)1);
    gm_b16_8 t9 = gm_l0;
    gm_l6 = t9;
    {
        gm_node wn10 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hl7iqllvekj4, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t13 = gm_l6;
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(gm_so514oj5nbhcu));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(1));
        gm_b0_1 t15 = gm_f_split_obtinjv_s76o7tg((void *)NULL, (&wn10), NULL, t13, t14);
        if (gm_unw.kind) {
            goto gm_ll12;
        }
    }
    goto gm_ls16;
gm_ll12:;
    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we11; }
    goto gm_lad;
gm_ls16:;
gm_lb1:;
we11:;
    gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
    gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
    F.ret = t18;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_empty_branches_7o4oudt(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_empty_branches_7o4oudt F;
    void * gm_l0; /* w */
    void * gm_l1; /* i */
    void * gm_l2; /* then */
    gm_tl874t2atnlh3 gm_l5; /* prev */
    gm_tl874t2atnlh3 gm_l6; /* span */
    GM_STACK_CHECK(&F, 813u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l8;
    {
        {
            void * t15 = gm_l1;
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t15) + 8));
            uint8_t t17 = ((t16 == ((uint64_t)1ULL)));
            if (!t17) {
                goto gm_lb2;
            }
            void * t18 = gm_l1;
            {
                {
                    uint8_t t19 = (uint8_t)(((((uint8_t *)t18) + 16))[48] != 0);
                    if (t19) {
                        goto gm_lb4;
                    }
                    gm_l8 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    gm_l8 = ((uint8_t)0);
                    goto gm_lb3;
                }
gm_lb5:;
                gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 683, 32);
            }
gm_lb3:;
            uint8_t t20 = gm_l8;
            if (!t20) {
                goto gm_lb2;
            }
            void * t21 = gm_l1;
            void * t22 = GM_LD(void *, ((uint8_t *)t21));
            uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)t21) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t23)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t23, gm_src10tru552hn49, 684, 18);
            void * t24 = (void *)((((uint8_t *)t22) + (uintptr_t)((uint64_t)0ULL) * 72) + 32);
            gm_l2 = t24;
            {
                {
                    void * t25 = gm_l2;
                    uint64_t t26 = GM_LD(uint64_t, ((((uint8_t *)t25) + 8) + 8));
                    uint8_t t27 = ((t26 == ((uint64_t)0ULL)));
                    if (!t27) {
                        goto gm_lb7;
                    }
                    void * t28 = gm_l0;
                    void * t29 = gm_l2;
                    gm_tl874t2atnlh3 t30 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t29) + 24));
                    uint8_t t31 = gm_f_has_comment_qa4dr2o(t28, GM_CONV(gm_cseahvg8v7p0vs, t30));
                    uint8_t t32 = (uint8_t)!t31;
                    if (!t32) {
                        goto gm_lb7;
                    }
                    void * t33 = gm_l0;
                    void * t34 = gm_l2;
                    gm_tl874t2atnlh3 t35 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t34) + 24));
                    gm_b16_8 t36;
                    GM_ST(void *, (uint8_t *)&t36, (void *)(gm_s5f9u0o3duhns));
                    GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(12));
                    gm_b16_8 t37;
                    GM_ST(void *, (uint8_t *)&t37, (void *)(gm_shquo2s3jci8a));
                    GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(41));
                    gm_t4gut2c9fj1kt t38;
                    gm_f_lint_diag_4vra4qu((void *)&t38, (void *)NULL, cx, NULL, t35, t36, t37);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b0_1 t39 = gm_f_push_l0e5v4c((void *)NULL, cx, NULL, t33, &t38);
                    if (gm_unw.kind) {
                        goto gm_lad;
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
        {
            void * t40 = gm_l1;
            uint8_t t41 = (uint8_t)(((((uint8_t *)t40) + 16))[48] != 0);
            if (!t41) {
                goto gm_lb9;
            }
            memmove(((uint8_t *)&F.l3), (((uint8_t *)t40) + 16), 48);
            {
                {
                    uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                    uint8_t t43 = (uint8_t)(t42 == ((uint8_t)0ULL));
                    if (!t43) {
                        goto gm_lb11;
                    }
                    memmove(((uint8_t *)&F.l4), (((uint8_t *)&F.l3) + 8), 40);
                    {
                        {
                            uint64_t t44 = GM_LD(uint64_t, ((((uint8_t *)&F.l4) + 8) + 8));
                            uint8_t t45 = ((t44 == ((uint64_t)0ULL)));
                            if (!t45) {
                                goto gm_lb13;
                            }
                            void * t46 = gm_l0;
                            gm_tl874t2atnlh3 t47 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l4) + 24));
                            uint8_t t48 = gm_f_has_comment_qa4dr2o(t46, GM_CONV(gm_cseahvg8v7p0vs, t47));
                            uint8_t t49 = (uint8_t)!t48;
                            if (!t49) {
                                goto gm_lb13;
                            }
                            void * t50 = gm_l1;
                            void * t51 = GM_LD(void *, ((uint8_t *)t50));
                            uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)t50) + 8));
                            void * t53 = gm_l1;
                            uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)t53) + 8));
                            uint64_t r55; bool o56 = __builtin_sub_overflow(t54, ((uint64_t)1ULL), &r55);
                            if (o56) gm_trap("integer overflow in `-`", gm_src10tru552hn49, 692, 36);
                            uint64_t t57 = r55;
                            if ((uintptr_t)t57 >= (uintptr_t)(t52)) gm_trap_index((uintptr_t)t57, t52, gm_src10tru552hn49, 692, 25);
                            gm_tl874t2atnlh3 t58 = GM_LD(gm_tl874t2atnlh3, (((((uint8_t *)t51) + (uintptr_t)t57 * 72) + 32) + 24));
                            gm_l5 = t58;
                            uint32_t t59 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 24));
                            uint32_t t60 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 8));
                            uint32_t t61 = GM_LD(uint32_t, ((((uint8_t *)&F.l4) + 24) + 8));
                            gm_tl874t2atnlh3 t62;
                            GM_ST(uint32_t, ((uint8_t *)&t62 + 0), t59);
                            GM_ST(uint32_t, ((uint8_t *)&t62 + 4), t60);
                            GM_ST(uint32_t, ((uint8_t *)&t62 + 8), t61);
                            gm_l6 = t62;
                            {
                                gm_tl874t2atnlh3 t63 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l4) + 24));
                                gm_b16_8 t64;
                                GM_ST(void *, (uint8_t *)&t64, (void *)(gm_s5f9u0o3duhns));
                                GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(12));
                                gm_b16_8 t65;
                                GM_ST(void *, (uint8_t *)&t65, (void *)(gm_sblt6ctj6g4ck));
                                GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(20));
                                gm_t4gut2c9fj1kt t66;
                                gm_f_lint_diag_4vra4qu((void *)&t66, (void *)NULL, cx, NULL, t63, t64, t65);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.l7 = t66;
                            }
                            void * t67 = (void *)((uint8_t *)&F.l7);
                            gm_b16_8 t68;
                            GM_ST(void *, (uint8_t *)&t68, (void *)(gm_s1hha8fc2kd79));
                            GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(17));
                            gm_tl874t2atnlh3 t69 = gm_l6;
                            gm_b16_8 t70;
                            GM_ST(void *, (uint8_t *)&t70, (void *)(gm_sr4qbrk6tlgaa));
                            GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(0));
                            gm_b0_1 t71 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t67, t68, t69, t70);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            {
                                void * t72 = gm_l0;
                                gm_b0_1 t73 = gm_f_push_l0e5v4c((void *)NULL, cx, NULL, t72, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l7)));
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            goto gm_lb12;
                        }
gm_lb13:;
                    }
gm_lb12:;
                    goto gm_lb10;
                }
gm_lb11:;
            }
gm_lb10:;
            goto gm_lb8;
        }
gm_lb9:;
    }
gm_lb8:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_expr_3limf78(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_expr_3limf78 F;
    void * gm_l1; /* e */
    gm_t9qkh0rsooc8d gm_l2; /* op */
    void * gm_l3; /* operand */
    void * gm_l4; /* inner */
    void * gm_l5; /* x */
    gm_t9qkh0rsooc8d gm_l6; /* op2 */
    void * gm_l7; /* x */
    gm_b16_8 gm_l8; /* t */
    gm_t08olq97rscn3 gm_l11; /* c */
    void * gm_l12; /* sig */
    void * gm_l13; /* f */
    void * gm_l14; /* h */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    void * gm_l18; /* i */
    void * gm_l19; /* m */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    uint64_t gm_l25; /* i */
    void * gm_l26; /* g */
    void * gm_l27; /* wi */
    uint64_t gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    uint64_t gm_l30; /* i */
    void * gm_l31; /* on */
    void * gm_l32; /* f */
    uint64_t gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    uint64_t gm_l35; /* i */
    void * gm_l37; /* v */
    void * gm_l38; /* x */
    void * gm_l40; /* v */
    GM_STACK_CHECK(&F, 1974u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        void * t102 = gm_l1;
        {
            {
                {
                    {
                        {
                            uint8_t t103 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t104 = (uint8_t)(t103 == ((uint8_t)0ULL));
                            if (!t104) {
                                goto gm_lb4;
                            }
                            goto gm_lb3;
                        }
gm_lb4:;
                        {
                            uint8_t t105 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t106 = (uint8_t)(t105 == ((uint8_t)1ULL));
                            if (!t106) {
                                goto gm_lb5;
                            }
                            goto gm_lb3;
                        }
gm_lb5:;
                        {
                            uint8_t t107 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t108 = (uint8_t)(t107 == ((uint8_t)2ULL));
                            if (!t108) {
                                goto gm_lb6;
                            }
                            goto gm_lb3;
                        }
gm_lb6:;
                        {
                            uint8_t t109 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t110 = (uint8_t)(t109 == ((uint8_t)3ULL));
                            if (!t110) {
                                goto gm_lb7;
                            }
                            goto gm_lb3;
                        }
gm_lb7:;
                        {
                            uint8_t t111 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t112 = (uint8_t)(t111 == ((uint8_t)4ULL));
                            if (!t112) {
                                goto gm_lb8;
                            }
                            goto gm_lb3;
                        }
gm_lb8:;
                        {
                            uint8_t t113 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t114 = (uint8_t)(t113 == ((uint8_t)5ULL));
                            if (!t114) {
                                goto gm_lb9;
                            }
                            goto gm_lb3;
                        }
gm_lb9:;
                        {
                            uint8_t t115 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t116 = (uint8_t)(t115 == ((uint8_t)6ULL));
                            if (!t116) {
                                goto gm_lb10;
                            }
                            goto gm_lb3;
                        }
gm_lb10:;
                        {
                            uint8_t t117 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t118 = (uint8_t)(t117 == ((uint8_t)7ULL));
                            if (!t118) {
                                goto gm_lb11;
                            }
                            goto gm_lb3;
                        }
gm_lb11:;
                        {
                            uint8_t t119 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t120 = (uint8_t)(t119 == ((uint8_t)8ULL));
                            if (!t120) {
                                goto gm_lb12;
                            }
                            goto gm_lb3;
                        }
gm_lb12:;
                        {
                            uint8_t t121 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t122 = (uint8_t)(t121 == ((uint8_t)38ULL));
                            if (!t122) {
                                goto gm_lb13;
                            }
                            goto gm_lb3;
                        }
gm_lb13:;
                        {
                            uint8_t t123 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t124 = (uint8_t)(t123 == ((uint8_t)50ULL));
                            if (!t124) {
                                goto gm_lb14;
                            }
                            goto gm_lb3;
                        }
gm_lb14:;
                        goto gm_lb2;
                    }
gm_lb3:;
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    {
                        {
                            uint8_t t125 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t126 = (uint8_t)(t125 == ((uint8_t)9ULL));
                            if (!t126) {
                                goto gm_lb17;
                            }
                            goto gm_lb16;
                        }
gm_lb17:;
                        {
                            uint8_t t127 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t128 = (uint8_t)(t127 == ((uint8_t)11ULL));
                            if (!t128) {
                                goto gm_lb18;
                            }
                            goto gm_lb16;
                        }
gm_lb18:;
                        {
                            uint8_t t129 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t130 = (uint8_t)(t129 == ((uint8_t)15ULL));
                            if (!t130) {
                                goto gm_lb19;
                            }
                            goto gm_lb16;
                        }
gm_lb19:;
                        {
                            uint8_t t131 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t132 = (uint8_t)(t131 == ((uint8_t)16ULL));
                            if (!t132) {
                                goto gm_lb20;
                            }
                            goto gm_lb16;
                        }
gm_lb20:;
                        {
                            uint8_t t133 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t134 = (uint8_t)(t133 == ((uint8_t)18ULL));
                            if (!t134) {
                                goto gm_lb21;
                            }
                            goto gm_lb16;
                        }
gm_lb21:;
                        {
                            uint8_t t135 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t136 = (uint8_t)(t135 == ((uint8_t)24ULL));
                            if (!t136) {
                                goto gm_lb22;
                            }
                            goto gm_lb16;
                        }
gm_lb22:;
                        {
                            uint8_t t137 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t138 = (uint8_t)(t137 == ((uint8_t)31ULL));
                            if (!t138) {
                                goto gm_lb23;
                            }
                            goto gm_lb16;
                        }
gm_lb23:;
                        {
                            uint8_t t139 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t140 = (uint8_t)(t139 == ((uint8_t)25ULL));
                            if (!t140) {
                                goto gm_lb24;
                            }
                            goto gm_lb16;
                        }
gm_lb24:;
                        {
                            uint8_t t141 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t142 = (uint8_t)(t141 == ((uint8_t)26ULL));
                            if (!t142) {
                                goto gm_lb25;
                            }
                            goto gm_lb16;
                        }
gm_lb25:;
                        {
                            uint8_t t143 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t144 = (uint8_t)(t143 == ((uint8_t)27ULL));
                            if (!t144) {
                                goto gm_lb26;
                            }
                            goto gm_lb16;
                        }
gm_lb26:;
                        {
                            uint8_t t145 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t146 = (uint8_t)(t145 == ((uint8_t)10ULL));
                            if (!t146) {
                                goto gm_lb27;
                            }
                            goto gm_lb16;
                        }
gm_lb27:;
                        {
                            uint8_t t147 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t148 = (uint8_t)(t147 == ((uint8_t)13ULL));
                            if (!t148) {
                                goto gm_lb28;
                            }
                            goto gm_lb16;
                        }
gm_lb28:;
                        {
                            uint8_t t149 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t150 = (uint8_t)(t149 == ((uint8_t)17ULL));
                            if (!t150) {
                                goto gm_lb29;
                            }
                            goto gm_lb16;
                        }
gm_lb29:;
                        {
                            uint8_t t151 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t152 = (uint8_t)(t151 == ((uint8_t)29ULL));
                            if (!t152) {
                                goto gm_lb30;
                            }
                            goto gm_lb16;
                        }
gm_lb30:;
                        {
                            uint8_t t153 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t154 = (uint8_t)(t153 == ((uint8_t)19ULL));
                            if (!t154) {
                                goto gm_lb31;
                            }
                            goto gm_lb16;
                        }
gm_lb31:;
                        {
                            uint8_t t155 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t156 = (uint8_t)(t155 == ((uint8_t)49ULL));
                            if (!t156) {
                                goto gm_lb32;
                            }
                            goto gm_lb16;
                        }
gm_lb32:;
                        {
                            uint8_t t157 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t158 = (uint8_t)(t157 == ((uint8_t)20ULL));
                            if (!t158) {
                                goto gm_lb33;
                            }
                            goto gm_lb16;
                        }
gm_lb33:;
                        {
                            uint8_t t159 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t160 = (uint8_t)(t159 == ((uint8_t)21ULL));
                            if (!t160) {
                                goto gm_lb34;
                            }
                            goto gm_lb16;
                        }
gm_lb34:;
                        {
                            uint8_t t161 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t162 = (uint8_t)(t161 == ((uint8_t)22ULL));
                            if (!t162) {
                                goto gm_lb35;
                            }
                            goto gm_lb16;
                        }
gm_lb35:;
                        {
                            uint8_t t163 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t164 = (uint8_t)(t163 == ((uint8_t)23ULL));
                            if (!t164) {
                                goto gm_lb36;
                            }
                            goto gm_lb16;
                        }
gm_lb36:;
                        {
                            uint8_t t165 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t166 = (uint8_t)(t165 == ((uint8_t)28ULL));
                            if (!t166) {
                                goto gm_lb37;
                            }
                            goto gm_lb16;
                        }
gm_lb37:;
                        {
                            uint8_t t167 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t168 = (uint8_t)(t167 == ((uint8_t)35ULL));
                            if (!t168) {
                                goto gm_lb38;
                            }
                            goto gm_lb16;
                        }
gm_lb38:;
                        {
                            uint8_t t169 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t170 = (uint8_t)(t169 == ((uint8_t)48ULL));
                            if (!t170) {
                                goto gm_lb39;
                            }
                            goto gm_lb16;
                        }
gm_lb39:;
                        goto gm_lb15;
                    }
gm_lb16:;
                    void * t171 = gm_l1;
                    gm_closure t172 = ((gm_closure){(void *)gm_f_fn_c8tpla6, (void *)&F.h, cx});
                    gm_f_for_each_child_tkrfoqu(t171, t172);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb15:;
            }
            {
                {
                    uint8_t t173 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                    uint8_t t174 = (uint8_t)(t173 == ((uint8_t)12ULL));
                    if (!t174) {
                        goto gm_lb40;
                    }
                    gm_t9qkh0rsooc8d t175 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t102) + 8) + 8));
                    gm_l2 = t175;
                    void * t176 = GM_LD(void *, (((((uint8_t *)t102) + 8) + 8) + 8));
                    gm_l3 = t176;
                    gm_t9qkh0rsooc8d t177 = gm_l2;
                    uint8_t t178 = GM_LD(uint8_t, (((uint8_t *)&t177) + 0));
                    uint8_t t179 = (uint8_t)((t178 == ((uint8_t)5ULL)));
                    gm_t9qkh0rsooc8d t180;
                    GM_ST(uint8_t, ((uint8_t *)&t180 + 0), ((uint8_t)5ULL));
                    if (t179) {
                        void * t181 = gm_l3;
                        gm_l4 = t181;
                        {
                            for (;;) {
                                if (!((uint8_t)1)) {
                                    goto gm_lb41;
                                }
                                {
                                    {
                                        {
                                            void * t183 = gm_l4;
                                            uint8_t t184 = GM_LD(uint8_t, ((((uint8_t *)t183) + 8) + 0));
                                            uint8_t t185 = (uint8_t)(t184 == ((uint8_t)9ULL));
                                            if (!t185) {
                                                goto gm_lb45;
                                            }
                                            void * t186 = GM_LD(void *, ((((uint8_t *)t183) + 8) + 8));
                                            gm_l5 = t186;
                                            void * t187 = gm_l5;
                                            gm_l4 = t187;
                                            goto gm_lb44;
                                        }
gm_lb45:;
                                        goto gm_lb41;
                                    }
gm_lb44:;
                                }
gm_lb43:;
                            }
                        }
gm_lb41:;
                        {
                            {
                                void * t188 = gm_l4;
                                uint8_t t189 = GM_LD(uint8_t, ((((uint8_t *)t188) + 8) + 0));
                                uint8_t t190 = (uint8_t)(t189 == ((uint8_t)12ULL));
                                if (!t190) {
                                    goto gm_lb47;
                                }
                                gm_t9qkh0rsooc8d t191 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t188) + 8) + 8));
                                gm_l6 = t191;
                                void * t192 = GM_LD(void *, (((((uint8_t *)t188) + 8) + 8) + 8));
                                gm_l7 = t192;
                                gm_t9qkh0rsooc8d t193 = gm_l6;
                                uint8_t t194 = GM_LD(uint8_t, (((uint8_t *)&t193) + 0));
                                uint8_t t195 = (uint8_t)((t194 == ((uint8_t)5ULL)));
                                gm_t9qkh0rsooc8d t196;
                                GM_ST(uint8_t, ((uint8_t *)&t196 + 0), ((uint8_t)5ULL));
                                if (t195) {
                                    void * t197 = F.l0;
                                    void * t198 = gm_l7;
                                    gm_tl874t2atnlh3 t199 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t198) + 72));
                                    gm_b16_8 t200 = gm_f_text_kspioh6(t197, GM_CONV(gm_cseahvg8v7p0vs, t199));
                                    gm_l8 = t200;
                                    {
                                        void * t201 = gm_l1;
                                        gm_tl874t2atnlh3 t202 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t201) + 72));
                                        gm_b16_8 t203;
                                        GM_ST(void *, (uint8_t *)&t203, (void *)(gm_sfr3tv65k63d4));
                                        GM_ST(uintptr_t, (uint8_t *)&t203 + 8, (uintptr_t)(15));
                                        gm_b16_8 t204;
                                        GM_ST(void *, (uint8_t *)&t204, (void *)(gm_snh86nvrv0us3));
                                        GM_ST(uintptr_t, (uint8_t *)&t204 + 8, (uintptr_t)(18));
                                        gm_t4gut2c9fj1kt t205;
                                        gm_f_lint_diag_4vra4qu((void *)&t205, (void *)NULL, cx, NULL, t202, t203, t204);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.l9 = t205;
                                    }
                                    void * t206 = (void *)((uint8_t *)&F.l9);
                                    gm_b16_8 t207;
                                    GM_ST(void *, (uint8_t *)&t207, (void *)(gm_sci6hkonrjrne));
                                    GM_ST(uintptr_t, (uint8_t *)&t207 + 8, (uintptr_t)(18));
                                    void * t208 = gm_l1;
                                    gm_tl874t2atnlh3 t209 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t208) + 72));
                                    void * t210 = gm_l7;
                                    gm_b16_8 t211 = gm_l8;
                                    gm_b16_8 t212 = gm_f_paren_unless_simple_mhajkgl((void *)NULL, cx, NULL, t210, t211, ((uint8_t)0));
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_b0_1 t213 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t206, t207, t209, t212);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    {
                                        void * t214 = F.l0;
                                        gm_b0_1 t215 = gm_f_push_l0e5v4c((void *)NULL, cx, NULL, t214, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l9)));
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                }
                                goto gm_lb46;
                            }
gm_lb47:;
                        }
gm_lb46:;
                    }
                    void * t216 = F.l0;
                    void * t217 = gm_l3;
                    gm_b0_1 t218 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t216, t217);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb40:;
            }
            {
                {
                    uint8_t t219 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                    uint8_t t220 = (uint8_t)(t219 == ((uint8_t)14ULL));
                    if (!t220) {
                        goto gm_lb48;
                    }
                    memmove(((uint8_t *)&F.l10), ((((uint8_t *)t102) + 8) + 8), 56);
                    {
                        {
                            uint8_t t221 = (uint8_t)(((((uint8_t *)&F.l10) + 8))[32] != 0);
                            if (!t221) {
                                goto gm_lb50;
                            }
                            {
                                memmove(((uint8_t *)&gm_l11), (((uint8_t *)&F.l10) + 8), 32);
                            }
                            {
                                void * t222 = F.l0;
                                gm_b0_1 t223 = gm_f_capture_abs7k4r((void *)NULL, cx, NULL, t222, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l11)), ((uint8_t)1));
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            goto gm_lb49;
                        }
gm_lb50:;
                    }
gm_lb49:;
                    void * t224 = F.l0;
                    void * t225 = GM_LD(void *, ((uint8_t *)&F.l10));
                    gm_b0_1 t226 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t224, t225);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t227 = F.l0;
                    void * t228 = GM_LD(void *, (((uint8_t *)&F.l10) + 48));
                    gm_b0_1 t229 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t227, t228);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb48:;
                {
                    uint8_t t230 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                    uint8_t t231 = (uint8_t)(t230 == ((uint8_t)30ULL));
                    if (!t231) {
                        goto gm_lb51;
                    }
                    void * t232 = GM_LD(void *, ((((uint8_t *)t102) + 8) + 8));
                    gm_l12 = t232;
                    void * t233 = F.l0;
                    void * t234 = gm_l12;
                    gm_b0_1 t235 = gm_f_sig_fn4opan((void *)NULL, cx, NULL, t233, t234, ((uint8_t)0));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb51:;
                {
                    {
                        {
                            uint8_t t236 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t237 = (uint8_t)(t236 == ((uint8_t)32ULL));
                            if (!t237) {
                                goto gm_lb54;
                            }
                            goto gm_lb53;
                        }
gm_lb54:;
                        {
                            uint8_t t238 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t239 = (uint8_t)(t238 == ((uint8_t)33ULL));
                            if (!t239) {
                                goto gm_lb55;
                            }
                            goto gm_lb53;
                        }
gm_lb55:;
                        {
                            uint8_t t240 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t241 = (uint8_t)(t240 == ((uint8_t)34ULL));
                            if (!t241) {
                                goto gm_lb56;
                            }
                            goto gm_lb53;
                        }
gm_lb56:;
                        {
                            uint8_t t242 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                            uint8_t t243 = (uint8_t)(t242 == ((uint8_t)39ULL));
                            if (!t243) {
                                goto gm_lb57;
                            }
                            goto gm_lb53;
                        }
gm_lb57:;
                        goto gm_lb52;
                    }
gm_lb53:;
                    goto gm_lb1;
                }
gm_lb52:;
                {
                    uint8_t t244 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                    uint8_t t245 = (uint8_t)(t244 == ((uint8_t)36ULL));
                    if (!t245) {
                        goto gm_lb58;
                    }
                    void * t246 = GM_LD(void *, ((((uint8_t *)t102) + 8) + 8));
                    gm_l13 = t246;
                    void * t247 = F.l0;
                    void * t248 = gm_l13;
                    gm_b0_1 t249 = gm_f_fn_lit_hd5tsqt((void *)NULL, cx, NULL, t247, t248);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb58:;
            }
            {
                {
                    uint8_t t250 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                    uint8_t t251 = (uint8_t)(t250 == ((uint8_t)37ULL));
                    if (!t251) {
                        goto gm_lb59;
                    }
                    void * t252 = GM_LD(void *, ((((uint8_t *)t102) + 8) + 8));
                    gm_l14 = t252;
                    void * t253 = gm_l14;
                    gm_b16_8 t254 = GM_LD(gm_b16_8, (((uint8_t *)t253) + 8));
                    gm_l15 = t254;
                    gm_l16 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t256 = gm_l16;
                            uint64_t t257 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                            uint8_t t258 = (t256 < t257);
                            if (!t258) {
                                goto gm_lb60;
                            }
                            {
                                void * t259 = GM_LD(void *, ((uint8_t *)&gm_l15));
                                uint64_t t260 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                uint64_t t261 = gm_l16;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l17), (((uint8_t *)t259) + (uintptr_t)t261 * 72), 72);
                                        void * t262 = F.l0;
                                        void * t263 = GM_LD(void *, (((uint8_t *)&F.l17) + 48));
                                        gm_b0_1 t264 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t262, t263);
                                        if (gm_unw.kind) {
                                            goto gm_ll255;
                                        }
                                        goto gm_lb63;
                                    }
gm_lb64:;
                                    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 509, 13);
                                }
gm_lb63:;
                            }
gm_lb62:;
                            uint64_t t265 = gm_l16;
                            uint64_t r266; bool o267 = __builtin_add_overflow(t265, ((uint64_t)1ULL), &r266);
                            uint64_t t268 = r266;
                            gm_l16 = t268;
                        }
                    }
                    goto gm_ls269;
gm_ll255:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb60; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb62; }
                    }
                    goto gm_lad;
gm_ls269:;
gm_lb60:;
                    goto gm_lb1;
                }
gm_lb59:;
                {
                    uint8_t t270 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                    uint8_t t271 = (uint8_t)(t270 == ((uint8_t)40ULL));
                    if (!t271) {
                        goto gm_lb65;
                    }
                    void * t272 = GM_LD(void *, ((((uint8_t *)t102) + 8) + 8));
                    gm_l18 = t272;
                    void * t273 = F.l0;
                    void * t274 = gm_l1;
                    void * t275 = gm_l18;
                    gm_b0_1 t276 = gm_f_if_expr_k16f2n5((void *)NULL, cx, NULL, t273, t274, t275);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb65:;
                {
                    uint8_t t277 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                    uint8_t t278 = (uint8_t)(t277 == ((uint8_t)41ULL));
                    if (!t278) {
                        goto gm_lb66;
                    }
                    void * t279 = GM_LD(void *, ((((uint8_t *)t102) + 8) + 8));
                    gm_l19 = t279;
                    void * t280 = F.l0;
                    gm_b16_8 t281;
                    GM_ST(void *, (uint8_t *)&t281, (void *)(gm_s1o0cl13ab3oc));
                    GM_ST(uintptr_t, (uint8_t *)&t281 + 8, (uintptr_t)(17));
                    void * t282 = gm_l19;
                    void * t283 = GM_LD(void *, ((uint8_t *)t282));
                    gm_b0_1 t284 = gm_f_paren_at_av727r5((void *)NULL, cx, NULL, t280, t281, t283, ((uint8_t)1));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t285 = F.l0;
                    void * t286 = gm_l19;
                    void * t287 = GM_LD(void *, ((uint8_t *)t286));
                    gm_b0_1 t288 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t285, t287);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t289 = gm_l19;
                    gm_b16_8 t290 = GM_LD(gm_b16_8, (((uint8_t *)t289) + 8));
                    gm_l20 = t290;
                    gm_l21 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t292 = gm_l21;
                            uint64_t t293 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                            uint8_t t294 = (t292 < t293);
                            if (!t294) {
                                goto gm_lb67;
                            }
                            {
                                void * t295 = GM_LD(void *, ((uint8_t *)&gm_l20));
                                uint64_t t296 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                                uint64_t t297 = gm_l21;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l22), (((uint8_t *)t295) + (uintptr_t)t297 * 48), 48);
                                        gm_l24 = ((uint64_t)0ULL);
                                        uint64_t t298 = GM_LD(uint64_t, (((uint8_t *)&F.l22) + 8));
                                        gm_l23 = t298;
                                        {
                                            for (;;) {
                                                uint64_t t300 = gm_l24;
                                                uint64_t t301 = gm_l23;
                                                uint8_t t302 = (t300 < t301);
                                                if (!t302) {
                                                    goto gm_lb72;
                                                }
                                                {
                                                    {
                                                        {
                                                            uint64_t t303 = gm_l24;
                                                            gm_l25 = t303;
                                                            void * t304 = F.l0;
                                                            void * t305 = GM_LD(void *, ((uint8_t *)&F.l22));
                                                            uint64_t t306 = GM_LD(uint64_t, (((uint8_t *)&F.l22) + 8));
                                                            uint64_t t307 = gm_l25;
                                                            void * t308 = (void *)(((uint8_t *)t305) + (uintptr_t)t307 * 64);
                                                            gm_t9qkh0rsooc8d t309;
                                                            GM_ST(uint8_t, ((uint8_t *)&t309 + 0), ((uint8_t)1ULL));
                                                            gm_b0_1 t310 = gm_f_pattern_8qtr7os((void *)NULL, cx, NULL, t304, t308, t309);
                                                            if (gm_unw.kind) {
                                                                goto gm_ll299;
                                                            }
                                                            goto gm_lb75;
                                                        }
gm_lb76:;
                                                        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 516, 17);
                                                    }
gm_lb75:;
                                                }
gm_lb74:;
                                                uint64_t t311 = gm_l24;
                                                uint64_t r312; bool o313 = __builtin_add_overflow(t311, ((uint64_t)1ULL), &r312);
                                                uint64_t t314 = r312;
                                                gm_l24 = t314;
                                            }
                                        }
                                        goto gm_ls315;
gm_ll299:;
                                        if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb72; }
                                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb74; }
                                        }
                                        goto gm_ll291;
gm_ls315:;
gm_lb72:;
                                        {
                                            {
                                                uint8_t t316 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l22) + 16)) != 0);
                                                if (!t316) {
                                                    goto gm_lb78;
                                                }
                                                void * t317 = GM_LD(void *, (((uint8_t *)&F.l22) + 16));
                                                gm_l26 = t317;
                                                void * t318 = F.l0;
                                                void * t319 = gm_l26;
                                                gm_b0_1 t320 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t318, t319);
                                                if (gm_unw.kind) {
                                                    goto gm_ll291;
                                                }
                                                goto gm_lb77;
                                            }
gm_lb78:;
                                        }
gm_lb77:;
                                        void * t321 = F.l0;
                                        void * t322 = GM_LD(void *, (((uint8_t *)&F.l22) + 24));
                                        gm_b0_1 t323 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t321, t322);
                                        if (gm_unw.kind) {
                                            goto gm_ll291;
                                        }
                                        goto gm_lb70;
                                    }
gm_lb71:;
                                    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 515, 13);
                                }
gm_lb70:;
                            }
gm_lb69:;
                            uint64_t t324 = gm_l21;
                            uint64_t r325; bool o326 = __builtin_add_overflow(t324, ((uint64_t)1ULL), &r325);
                            uint64_t t327 = r325;
                            gm_l21 = t327;
                        }
                    }
                    goto gm_ls328;
gm_ll291:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb67; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb69; }
                    }
                    goto gm_lad;
gm_ls328:;
gm_lb67:;
                    goto gm_lb1;
                }
gm_lb66:;
            }
            {
                {
                    uint8_t t329 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                    uint8_t t330 = (uint8_t)(t329 == ((uint8_t)42ULL));
                    if (!t330) {
                        goto gm_lb79;
                    }
                    void * t331 = GM_LD(void *, ((((uint8_t *)t102) + 8) + 8));
                    gm_l27 = t331;
                    gm_l29 = ((uint64_t)0ULL);
                    void * t332 = gm_l27;
                    uint64_t t333 = GM_LD(uint64_t, ((((uint8_t *)t332) + 40) + 8));
                    gm_l28 = t333;
                    {
                        for (;;) {
                            uint64_t t335 = gm_l29;
                            uint64_t t336 = gm_l28;
                            uint8_t t337 = (t335 < t336);
                            if (!t337) {
                                goto gm_lb80;
                            }
                            {
                                {
                                    {
                                        uint64_t t338 = gm_l29;
                                        gm_l30 = t338;
                                        void * t339 = F.l0;
                                        void * t340 = gm_l27;
                                        void * t341 = GM_LD(void *, (((uint8_t *)t340) + 40));
                                        uint64_t t342 = GM_LD(uint64_t, ((((uint8_t *)t340) + 40) + 8));
                                        uint64_t t343 = gm_l30;
                                        if ((uintptr_t)t343 >= (uintptr_t)(t342)) gm_trap_index((uintptr_t)t343, t342, gm_src10tru552hn49, 522, 51);
                                        void * t344 = (void *)(((uint8_t *)t341) + (uintptr_t)t343 * 88);
                                        gm_b0_1 t345 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t339, t344);
                                        if (gm_unw.kind) {
                                            goto gm_ll334;
                                        }
                                        goto gm_lb83;
                                    }
gm_lb84:;
                                    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 522, 13);
                                }
gm_lb83:;
                            }
gm_lb82:;
                            uint64_t t346 = gm_l29;
                            uint64_t r347; bool o348 = __builtin_add_overflow(t346, ((uint64_t)1ULL), &r347);
                            uint64_t t349 = r347;
                            gm_l29 = t349;
                        }
                    }
                    goto gm_ls350;
gm_ll334:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb80; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb82; }
                    }
                    goto gm_lad;
gm_ls350:;
gm_lb80:;
                    {
                        {
                            void * t351 = gm_l27;
                            uint8_t t352 = (uint8_t)(GM_LD(void *, (((uint8_t *)t351) + 56)) != 0);
                            if (!t352) {
                                goto gm_lb86;
                            }
                            void * t353 = GM_LD(void *, (((uint8_t *)t351) + 56));
                            gm_l31 = t353;
                            void * t354 = F.l0;
                            void * t355 = gm_l31;
                            gm_b0_1 t356 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t354, t355);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb85;
                        }
gm_lb86:;
                    }
gm_lb85:;
                    {
                        void * t357 = F.l0;
                        void * t358 = gm_l27;
                        gm_b0_1 t359 = gm_f_push_label_hn6s4vp((void *)NULL, cx, NULL, t357, &(*(gm_t1s75i08qtptd *)((uint8_t *)t358)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    void * t360 = F.l0;
                    void * t361 = gm_l27;
                    void * t362 = (void *)(((uint8_t *)t361) + 64);
                    gm_b0_1 t363 = gm_f_block_btrailr((void *)NULL, cx, NULL, t360, t362);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    {
                        void * t364 = F.l0;
                        void * t365 = gm_l27;
                        gm_b0_1 t366 = gm_f_pop_label_0jfjsf5((void *)NULL, cx, NULL, t364, &(*(gm_t1s75i08qtptd *)((uint8_t *)t365)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto gm_lb1;
                }
gm_lb79:;
                {
                    uint8_t t367 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                    uint8_t t368 = (uint8_t)(t367 == ((uint8_t)43ULL));
                    if (!t368) {
                        goto gm_lb87;
                    }
                    void * t369 = GM_LD(void *, ((((uint8_t *)t102) + 8) + 8));
                    gm_l32 = t369;
                    gm_l34 = ((uint64_t)0ULL);
                    void * t370 = gm_l32;
                    uint64_t t371 = GM_LD(uint64_t, ((((uint8_t *)t370) + 40) + 8));
                    gm_l33 = t371;
                    {
                        for (;;) {
                            uint64_t t373 = gm_l34;
                            uint64_t t374 = gm_l33;
                            uint8_t t375 = (t373 < t374);
                            if (!t375) {
                                goto gm_lb88;
                            }
                            {
                                {
                                    {
                                        uint64_t t376 = gm_l34;
                                        gm_l35 = t376;
                                        void * t377 = F.l0;
                                        void * t378 = gm_l32;
                                        void * t379 = GM_LD(void *, (((uint8_t *)t378) + 40));
                                        uint64_t t380 = GM_LD(uint64_t, ((((uint8_t *)t378) + 40) + 8));
                                        uint64_t t381 = gm_l35;
                                        if ((uintptr_t)t381 >= (uintptr_t)(t380)) gm_trap_index((uintptr_t)t381, t380, gm_src10tru552hn49, 529, 49);
                                        void * t382 = (void *)(((uint8_t *)t379) + (uintptr_t)t381 * 64);
                                        gm_t9qkh0rsooc8d t383;
                                        GM_ST(uint8_t, ((uint8_t *)&t383 + 0), ((uint8_t)1ULL));
                                        gm_b0_1 t384 = gm_f_pattern_8qtr7os((void *)NULL, cx, NULL, t377, t382, t383);
                                        if (gm_unw.kind) {
                                            goto gm_ll372;
                                        }
                                        goto gm_lb91;
                                    }
gm_lb92:;
                                    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 529, 13);
                                }
gm_lb91:;
                            }
gm_lb90:;
                            uint64_t t385 = gm_l34;
                            uint64_t r386; bool o387 = __builtin_add_overflow(t385, ((uint64_t)1ULL), &r386);
                            uint64_t t388 = r386;
                            gm_l34 = t388;
                        }
                    }
                    goto gm_ls389;
gm_ll372:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb88; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb90; }
                    }
                    goto gm_lad;
gm_ls389:;
gm_lb88:;
                    void * t390 = F.l0;
                    gm_b16_8 t391;
                    GM_ST(void *, (uint8_t *)&t391, (void *)(gm_shfvspt8ngibo));
                    GM_ST(uintptr_t, (uint8_t *)&t391 + 8, (uintptr_t)(18));
                    void * t392 = gm_l32;
                    void * t393 = GM_LD(void *, (((uint8_t *)t392) + 56));
                    gm_b0_1 t394 = gm_f_paren_at_av727r5((void *)NULL, cx, NULL, t390, t391, t393, ((uint8_t)1));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t395 = F.l0;
                    void * t396 = gm_l32;
                    void * t397 = GM_LD(void *, (((uint8_t *)t396) + 56));
                    gm_b0_1 t398 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t395, t397);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    {
                        void * t399 = F.l0;
                        void * t400 = gm_l32;
                        gm_b0_1 t401 = gm_f_push_label_hn6s4vp((void *)NULL, cx, NULL, t399, &(*(gm_t1s75i08qtptd *)((uint8_t *)t400)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    void * t402 = F.l0;
                    void * t403 = gm_l32;
                    void * t404 = (void *)(((uint8_t *)t403) + 64);
                    gm_b0_1 t405 = gm_f_block_btrailr((void *)NULL, cx, NULL, t402, t404);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    {
                        void * t406 = F.l0;
                        void * t407 = gm_l32;
                        gm_b0_1 t408 = gm_f_pop_label_0jfjsf5((void *)NULL, cx, NULL, t406, &(*(gm_t1s75i08qtptd *)((uint8_t *)t407)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto gm_lb1;
                }
gm_lb87:;
            }
            {
                {
                    uint8_t t409 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                    uint8_t t410 = (uint8_t)(t409 == ((uint8_t)44ULL));
                    if (!t410) {
                        goto gm_lb93;
                    }
                    memmove(((uint8_t *)&F.l36), ((((uint8_t *)t102) + 8) + 8), 40);
                    void * t411 = F.l0;
                    void * t412 = (void *)((uint8_t *)&F.l36);
                    gm_b0_1 t413 = gm_f_block_btrailr((void *)NULL, cx, NULL, t411, t412);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb93:;
                {
                    uint8_t t414 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                    uint8_t t415 = (uint8_t)(t414 == ((uint8_t)45ULL));
                    if (!t415) {
                        goto gm_lb94;
                    }
                    void * t416 = GM_LD(void *, ((((uint8_t *)t102) + 8) + 8));
                    gm_l37 = t416;
                    {
                        {
                            uint8_t t417 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l37)) != 0);
                            if (!t417) {
                                goto gm_lb96;
                            }
                            void * t418 = GM_LD(void *, ((uint8_t *)&gm_l37));
                            gm_l38 = t418;
                            void * t419 = F.l0;
                            gm_b16_8 t420;
                            GM_ST(void *, (uint8_t *)&t420, (void *)(gm_sc06resjcdhg4));
                            GM_ST(uintptr_t, (uint8_t *)&t420 + 8, (uintptr_t)(18));
                            void * t421 = gm_l38;
                            gm_b0_1 t422 = gm_f_paren_at_av727r5((void *)NULL, cx, NULL, t419, t420, t421, ((uint8_t)0));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t423 = F.l0;
                            void * t424 = gm_l38;
                            gm_b0_1 t425 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t423, t424);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb95;
                        }
gm_lb96:;
                    }
gm_lb95:;
                    goto gm_lb1;
                }
gm_lb94:;
                {
                    uint8_t t426 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                    uint8_t t427 = (uint8_t)(t426 == ((uint8_t)46ULL));
                    if (!t427) {
                        goto gm_lb97;
                    }
                    {
                        memmove(((uint8_t *)&F.l39), ((((uint8_t *)t102) + 8) + 8), 48);
                    }
                    {
                        void * t428 = F.l0;
                        gm_f_use_label_8j0qqam(t428, &(*(gm_t1s75i08qtptd *)((uint8_t *)&F.l39)));
                    }
                    {
                        {
                            uint8_t t429 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l39) + 40)) != 0);
                            if (!t429) {
                                goto gm_lb99;
                            }
                            void * t430 = GM_LD(void *, (((uint8_t *)&F.l39) + 40));
                            gm_l40 = t430;
                            void * t431 = F.l0;
                            void * t432 = gm_l40;
                            gm_b0_1 t433 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t431, t432);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb98;
                        }
gm_lb99:;
                    }
gm_lb98:;
                    goto gm_lb1;
                }
gm_lb97:;
                {
                    uint8_t t434 = GM_LD(uint8_t, ((((uint8_t *)t102) + 8) + 0));
                    uint8_t t435 = (uint8_t)(t434 == ((uint8_t)47ULL));
                    if (!t435) {
                        goto gm_lb100;
                    }
                    {
                        memmove(((uint8_t *)&F.l41), ((((uint8_t *)t102) + 8) + 8), 40);
                    }
                    {
                        void * t436 = F.l0;
                        gm_f_use_label_8j0qqam(t436, &(*(gm_t1s75i08qtptd *)((uint8_t *)&F.l41)));
                    }
                    goto gm_lb1;
                }
gm_lb100:;
                gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 476, 5);
            }
        }
gm_lb1:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fields_8ttf3eb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_fields_8ttf3eb F;
    void * gm_l0; /* w */
    gm_b16_8 gm_l1; /* members */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t08olq97rscn3 gm_l8; /* n */
    GM_STACK_CHECK(&F, 408u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 gm_l10;
    gm_b16_8 t15 = gm_l1;
    gm_l2 = t15;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t16 = gm_l3;
            uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t18 = (t16 < t17);
            if (!t18) {
                goto gm_lb1;
            }
            {
                void * t19 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t21 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&F.l4), (((uint8_t *)t19) + (uintptr_t)t21 * 80), 80);
                        {
                            {
                                uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                                uint8_t t23 = (uint8_t)(t22 == ((uint8_t)0ULL));
                                if (!t23) {
                                    goto gm_lb7;
                                }
                                memmove(((uint8_t *)&F.l5), (((uint8_t *)&F.l4) + 8), 72);
                                gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 24));
                                gm_l6 = t24;
                                gm_l7 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t25 = gm_l7;
                                        uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                        uint8_t t27 = (t25 < t26);
                                        if (!t27) {
                                            goto gm_lb8;
                                        }
                                        {
                                            void * t28 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                            uint64_t t30 = gm_l7;
                                            {
                                                {
                                                    {
                                                        memmove(((uint8_t *)&gm_l8), (((uint8_t *)t28) + (uintptr_t)t30 * 32), 32);
                                                    }
                                                    {
                                                        void * t31 = gm_l0;
                                                        gm_b16_8 t32;
                                                        GM_ST(void *, (uint8_t *)&t32, (void *)(gm_s1jboc8lkvac0));
                                                        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(5));
                                                        gm_t9qkh0rsooc8d t33;
                                                        GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)1ULL));
                                                        gm_b0_1 t34 = gm_f_check_name_hf7dide((void *)NULL, cx, NULL, t31, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l8)), t33, t32);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                    }
                                                    goto gm_lb11;
                                                }
gm_lb12:;
                                                gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 164, 17);
                                            }
gm_lb11:;
                                        }
gm_lb10:;
                                        uint64_t t35 = gm_l7;
                                        uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                                        uint64_t t38 = r36;
                                        gm_l7 = t38;
                                    }
                                }
gm_lb8:;
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                                uint8_t t40 = (uint8_t)(t39 == ((uint8_t)1ULL));
                                if (!t40) {
                                    goto gm_lb13;
                                }
                                memmove(((uint8_t *)&F.l9), (((uint8_t *)&F.l4) + 8), 64);
                                void * t41 = gm_l0;
                                gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 8));
                                gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
                                gm_b0_1 t44 = gm_f_fields_8ttf3eb((void *)NULL, cx, NULL, t41, t43);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t45 = gm_l0;
                                uint8_t t46 = (uint8_t)(((((uint8_t *)&F.l9) + 24))[16] != 0);
                                if (t46) {
                                    gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 24));
                                    gm_l10 = t47;
                                } else {
                                    gm_b16_8 t48 = gm_f_empty_slice_d53jqcq();
                                    gm_l10 = t48;
                                }
                                gm_b16_8 t49 = gm_l10;
                                gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
                                gm_b0_1 t51 = gm_f_fields_8ttf3eb((void *)NULL, cx, NULL, t45, t50);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb13:;
                            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 162, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 161, 5);
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
inline __attribute__((aligned(4))) void gm_f_file_facts_tdo30n8(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_file_facts_tdo30n8 F;
    GM_STACK_CHECK(&F, 216u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t2;
    gm_f_list_prbqo9j((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t3;
    gm_f_hash_map_2auo7cg((void *)&t3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t4;
    gm_f_list_7s4om10((void *)&t4, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t5;
    gm_f_list_fu17r8d((void *)&t5, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t6;
    gm_f_list_prbqo9j((void *)&t6, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_tg0vmr665gcag *)gm_out) + 0), t2);
    GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&(*(gm_tg0vmr665gcag *)gm_out) + 40), t3);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_tg0vmr665gcag *)gm_out) + 96), t4);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_tg0vmr665gcag *)gm_out) + 136), t5);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_tg0vmr665gcag *)gm_out) + 176), t6);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_c8tpla6(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_lit_hd5tsqt(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_fn_lit_hd5tsqt F;
    void * gm_l0; /* w */
    void * gm_l1; /* f */
    void * gm_l4; /* e */
    GM_STACK_CHECK(&F, 164u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t5 = gm_l0;
    void * t6 = gm_l1;
    void * t7 = (void *)((uint8_t *)t6);
    gm_b0_1 t8 = gm_f_sig_fn4opan((void *)NULL, cx, NULL, t5, t7, ((uint8_t)1));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        void * t9 = gm_l0;
        memmove(((uint8_t *)&F.l2), (((uint8_t *)t9) + 224), 40);
    }
    {
        void * t10 = gm_l0;
        gm_trqfvmjtt4sij t11;
        gm_f_list_uaa66su((void *)&t11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t10) + 224), t11);
    }
    void * t12 = gm_l1;
    {
        {
            uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t12) + 88) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)0ULL));
            if (!t14) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l3), ((((uint8_t *)t12) + 88) + 8), 40);
            void * t15 = gm_l0;
            void * t16 = (void *)((uint8_t *)&F.l3);
            gm_b0_1 t17 = gm_f_block_btrailr((void *)NULL, cx, NULL, t15, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t12) + 88) + 0));
            uint8_t t19 = (uint8_t)(t18 == ((uint8_t)1ULL));
            if (!t19) {
                goto gm_lb3;
            }
            void * t20 = GM_LD(void *, ((((uint8_t *)t12) + 88) + 8));
            gm_l4 = t20;
            void * t21 = gm_l0;
            void * t22 = gm_l4;
            gm_b0_1 t23 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t21, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 576, 5);
    }
gm_lb1:;
    {
        void * t24 = gm_l0;
        memmove((((uint8_t *)t24) + 224), ((uint8_t *)&F.l2), 40);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1063u09(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_1063u09 F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* word */
    GM_STACK_CHECK(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l2;
    gm_tftb1odc739t5 gm_l3;
    {
        {
            gm_b16_8 t4 = gm_l0;
            gm_l1 = t4;
            uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&((struct gm_fr_normalize_space_v7dp7uh *)((&F.h)->up))->l1) + 8));
            uint8_t t6 = (t5 > ((uint64_t)0ULL));
            if (t6) {
                void * t7 = (void *)((uint8_t *)&((struct gm_fr_normalize_space_v7dp7uh *)((&F.h)->up))->l1);
                gm_tftb1odc739t5 t8 = gm_f_push_t83rg2m(t7, ((uint8_t)32ULL));
                gm_l2 = t8;
                uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
                if (!t9) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_src10tru552hn49, 747, 32);
            }
            void * t10 = (void *)((uint8_t *)&((struct gm_fr_normalize_space_v7dp7uh *)((&F.h)->up))->l1);
            gm_b16_8 t11 = gm_l1;
            gm_tftb1odc739t5 t12 = gm_f_extend_0cs85gs(t10, t11);
            gm_l3 = t12;
            uint8_t t13 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
            if (!t13) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_src10tru552hn49, 748, 9);
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 746, 5);
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_8jjs01b(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_8jjs01b F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* word */
    GM_STACK_CHECK(&F, 194u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l2;
    gm_tftb1odc739t5 gm_l3;
    gm_tftb1odc739t5 gm_l4;
    {
        {
            gm_b16_8 t4 = gm_l0;
            gm_l1 = t4;
            uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t6 = ((t5 == ((uint64_t)0ULL)));
            if (t6) {
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            void * t7 = (void *)((uint8_t *)&((struct gm_fr_convert_c7mdt88 *)((&F.h)->up))->l2);
            void * t8 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t9)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t9, gm_src10tru552hn49, 813, 38);
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)t8) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint8_t t11 = gm_f_is_lower_q12rpq9(t10);
            if (t11) {
                void * t12 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t13)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t13, gm_src10tru552hn49, 813, 49);
                uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)t12) + (uintptr_t)((uint64_t)0ULL) * 1));
                uint8_t r15; bool o16 = __builtin_sub_overflow(t14, ((uint8_t)32ULL), &r15);
                if (o16) gm_trap("integer overflow in `-`", gm_src10tru552hn49, 813, 49);
                uint8_t t17 = r15;
                gm_l2 = t17;
            } else {
                void * t18 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t19)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t19, gm_src10tru552hn49, 813, 71);
                uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)t18) + (uintptr_t)((uint64_t)0ULL) * 1));
                gm_l2 = t20;
            }
            uint8_t t21 = gm_l2;
            gm_tftb1odc739t5 t22 = gm_f_push_t83rg2m(t7, t21);
            gm_l3 = t22;
            uint8_t t23 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
            if (!t23) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_src10tru552hn49, 813, 17);
            void * t24 = (void *)((uint8_t *)&((struct gm_fr_convert_c7mdt88 *)((&F.h)->up))->l2);
            void * t25 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint64_t t27 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t28 = (uintptr_t)t26;
            if (t27 > t28 || t28 > (uintptr_t)t26) gm_trap_slice(t27, t28, t26, gm_src10tru552hn49, 814, 28);
            uint64_t t29 = ((uintptr_t)t26) - (uintptr_t)((uint64_t)1ULL);
            gm_b16_8 t30;
            GM_ST(void *, (uint8_t *)&t30, (void *)(((uint8_t *)t25 + (uintptr_t)((uint64_t)1ULL) * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(t29));
            gm_tftb1odc739t5 t31 = gm_f_extend_0cs85gs(t24, t30);
            gm_l4 = t31;
            uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
            if (!t32) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_src10tru552hn49, 814, 17);
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 811, 13);
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_cbj68cn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_cbj68cn F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    uint64_t gm_l2; /* blanks */
    GM_STACK_CHECK(&F, 254u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l3;
    gm_tftb1odc739t5 gm_l4;
    gm_tftb1odc739t5 gm_l5;
    {
        {
            gm_b16_8 t7 = gm_l0;
            gm_l1 = t7;
            uint8_t t8 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_dedent_aa5vbel *)((&F.h)->up))->l5));
            if (t8) {
                void * t9 = (void *)((uint8_t *)&((struct gm_fr_dedent_aa5vbel *)((&F.h)->up))->l4);
                gm_b16_8 t10 = gm_l1;
                gm_tftb1odc739t5 t11 = gm_f_extend_0cs85gs(t9, t10);
                gm_l3 = t11;
                uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
                if (!t12) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_src10tru552hn49, 896, 13);
                GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_dedent_aa5vbel *)((&F.h)->up))->l5), ((uint8_t)0));
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            void * t13 = (void *)((uint8_t *)&((struct gm_fr_dedent_aa5vbel *)((&F.h)->up))->l4);
            gm_tftb1odc739t5 t14 = gm_f_push_t83rg2m(t13, ((uint8_t)10ULL));
            gm_l4 = t14;
            uint8_t t15 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
            if (!t15) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_src10tru552hn49, 900, 9);
            gm_l2 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t16 = gm_l2;
                    uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                    uint8_t t18 = (t16 < t17);
                    if (!t18) {
                        goto gm_lb3;
                    }
                    void * t19 = GM_LD(void *, ((uint8_t *)&gm_l1));
                    uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                    uint64_t t21 = gm_l2;
                    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)t19) + (uintptr_t)t21 * 1));
                    gm_tqs5p4n2tq19v t23;
                    GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)32ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t23 + 1), ((uint8_t)9ULL));
                    uint8_t t24 = 0;
                    if (gm_eqjmccqdb90f(((uint8_t *)&t23) + 0, ((uint8_t *)&t22))) t24 = 1;
                    if (gm_eqjmccqdb90f(((uint8_t *)&t23) + 1, ((uint8_t *)&t22))) t24 = 1;
                    uint8_t t25 = (uint8_t)(t24);
                    if (!t25) {
                        goto gm_lb3;
                    }
                    {
                        uint64_t t26 = gm_l2;
                        uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
                        if (o28) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 902, 67);
                        uint64_t t29 = r27;
                        gm_l2 = t29;
                    }
gm_lb5:;
                }
            }
gm_lb3:;
            void * t30 = (void *)((uint8_t *)&((struct gm_fr_dedent_aa5vbel *)((&F.h)->up))->l4);
            void * t31 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint64_t t33 = gm_l2;
            uint64_t t34 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_dedent_aa5vbel *)((&F.h)->up))->l3));
            uint64_t t35 = (t33 < t34 ? t33 : t34);
            uint64_t t36 = (uintptr_t)t35;
            uint64_t t37 = (uintptr_t)t32;
            if (t36 > t37 || t37 > (uintptr_t)t32) gm_trap_slice(t36, t37, t32, gm_src10tru552hn49, 903, 20);
            uint64_t t38 = ((uintptr_t)t32) - (uintptr_t)t35;
            gm_b16_8 t39;
            GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t31 + (uintptr_t)t35 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(t38));
            gm_tftb1odc739t5 t40 = gm_f_extend_0cs85gs(t30, t39);
            gm_l5 = t40;
            uint8_t t41 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
            if (!t41) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_src10tru552hn49, 903, 9);
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 894, 5);
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_has_comment_qa4dr2o(void * a0, gm_cseahvg8v7p0vs a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_identical_branches_ug6ccol(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_identical_branches_ug6ccol F;
    void * gm_l0; /* w */
    void * gm_l1; /* i */
    gm_b16_8 gm_l4; /* first */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_tl874t2atnlh3 gm_l8; /* span */
    GM_STACK_CHECK(&F, 598u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t9 = gm_l1;
    uint8_t t10 = (uint8_t)(((((uint8_t *)t9) + 16))[48] != 0);
    if (t10) {
        memmove(((uint8_t *)&F.l2), (((uint8_t *)t9) + 16), 48);
    } else {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    {
        {
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            uint8_t t12 = (uint8_t)(t11 == ((uint8_t)0ULL));
            if (!t12) {
                goto gm_lb2;
            }
            {
                memmove(((uint8_t *)&F.l3), (((uint8_t *)&F.l2) + 8), 40);
            }
            uint64_t t13 = GM_LD(uint64_t, ((((uint8_t *)&F.l3) + 8) + 8));
            uint8_t t14 = ((t13 == ((uint64_t)0ULL)));
            if (t14) {
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            void * t15 = gm_l0;
            gm_tl874t2atnlh3 t16 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l3) + 24));
            gm_b16_8 t17 = gm_f_text_kspioh6(t15, GM_CONV(gm_cseahvg8v7p0vs, t16));
            gm_b16_8 t18 = gm_f_normalize_space_v7dp7uh((void *)NULL, cx, NULL, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t18;
            void * t19 = gm_l1;
            gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)t19));
            gm_l5 = t20;
            gm_l6 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t21 = gm_l6;
                    uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint8_t t23 = (t21 < t22);
                    if (!t23) {
                        goto gm_lb3;
                    }
                    {
                        void * t24 = GM_LD(void *, ((uint8_t *)&gm_l5));
                        uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                        uint64_t t26 = gm_l6;
                        {
                            {
                                memmove(((uint8_t *)&F.l7), (((uint8_t *)t24) + (uintptr_t)t26 * 72), 72);
                                void * t27 = gm_l0;
                                gm_tl874t2atnlh3 t28 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&F.l7) + 32) + 24));
                                gm_b16_8 t29 = gm_f_text_kspioh6(t27, GM_CONV(gm_cseahvg8v7p0vs, t28));
                                gm_b16_8 t30 = gm_f_normalize_space_v7dp7uh((void *)NULL, cx, NULL, t29);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_b16_8 t31 = gm_l4;
                                uint8_t t32 = (!gm_eq0iqiubfm7e(((uint8_t *)&t30), ((uint8_t *)&t31)));
                                if (t32) {
                                    F.ret = GM_ZERO(gm_b0_1);
                                    goto gm_fret;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 734, 5);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t33 = gm_l6;
                    uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
                    uint64_t t36 = r34;
                    gm_l6 = t36;
                }
            }
gm_lb3:;
            void * t37 = gm_l1;
            void * t38 = GM_LD(void *, ((uint8_t *)t37));
            uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)t37) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t39)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t39, gm_src10tru552hn49, 737, 13);
            gm_tl874t2atnlh3 t40 = GM_LD(gm_tl874t2atnlh3, (((((uint8_t *)t38) + (uintptr_t)((uint64_t)0ULL) * 72) + 32) + 24));
            gm_l8 = t40;
            {
                gm_tl874t2atnlh3 t41 = gm_l8;
                gm_b16_8 t42;
                GM_ST(void *, (uint8_t *)&t42, (void *)(gm_scd8m51mtmjv9));
                GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(18));
                gm_b16_8 t43;
                GM_ST(void *, (uint8_t *)&t43, (void *)(gm_scmgj2ccom9v4));
                GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(39));
                gm_t4gut2c9fj1kt t44;
                gm_f_lint_diag_4vra4qu((void *)&t44, (void *)NULL, cx, NULL, t41, t42, t43);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l9 = t44;
            }
            void * t45 = (void *)((uint8_t *)&F.l9);
            gm_tl874t2atnlh3 t46 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l3) + 24));
            gm_b16_8 t47;
            GM_ST(void *, (uint8_t *)&t47, (void *)(gm_sso47q408qu33));
            GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(23));
            gm_f_add_label_50fj7vv(t45, GM_CONV(gm_cseahvg8v7p0vs, t46), t47);
            {
                void * t48 = gm_l0;
                gm_b0_1 t49 = gm_f_push_l0e5v4c((void *)NULL, cx, NULL, t48, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l9)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto gm_lb1;
        }
gm_lb2:;
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_if_expr_k16f2n5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_if_expr_k16f2n5 F;
    void * gm_l0; /* w */
    void * gm_l1; /* e */
    void * gm_l2; /* i */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    void * gm_l9; /* x */
    uint8_t gm_l10; /* v */
    gm_b16_8 gm_l11; /* msg */
    gm_t08olq97rscn3 gm_l15; /* name */
    void * gm_l16; /* body */
    GM_STACK_CHECK(&F, 822u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l17;
    void * t24 = gm_l2;
    gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)t24));
    gm_l3 = t25;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t26 = gm_l4;
            uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t28 = (t26 < t27);
            if (!t28) {
                goto gm_lb1;
            }
            {
                void * t29 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t31 = gm_l4;
                {
                    {
                        memmove(((uint8_t *)&F.l5), (((uint8_t *)t29) + (uintptr_t)t31 * 72), 72);
                        void * t32 = gm_l0;
                        void * t33 = (void *)((uint8_t *)&F.l5);
                        gm_b0_1 t34 = gm_f_cond_e0272rd((void *)NULL, cx, NULL, t32, t33, ((uint8_t)1));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
                        gm_l6 = t35;
                        gm_l7 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t36 = gm_l7;
                                uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                uint8_t t38 = (t36 < t37);
                                if (!t38) {
                                    goto gm_lb6;
                                }
                                {
                                    void * t39 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                    uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                    uint64_t t41 = gm_l7;
                                    {
                                        {
                                            memmove(((uint8_t *)&F.l8), (((uint8_t *)t39) + (uintptr_t)t41 * 80), 80);
                                            {
                                                {
                                                    uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + 0));
                                                    uint8_t t43 = (uint8_t)(t42 == ((uint8_t)1ULL));
                                                    if (!t43) {
                                                        goto gm_lb12;
                                                    }
                                                    void * t44 = GM_LD(void *, (((uint8_t *)&F.l8) + 8));
                                                    gm_l9 = t44;
                                                    {
                                                        {
                                                            void * t45 = gm_l9;
                                                            uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)t45) + 8) + 0));
                                                            uint8_t t47 = (uint8_t)(t46 == ((uint8_t)3ULL));
                                                            if (!t47) {
                                                                goto gm_lb14;
                                                            }
                                                            uint8_t t48 = GM_LD(uint8_t, ((((uint8_t *)t45) + 8) + 8));
                                                            gm_l10 = t48;
                                                            uint8_t t49 = gm_l10;
                                                            if (t49) {
                                                                gm_b16_8 t50;
                                                                GM_ST(void *, (uint8_t *)&t50, (void *)(gm_sdtpg69jtnl30));
                                                                GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(29));
                                                                gm_l11 = t50;
                                                            } else {
                                                                gm_b16_8 t51;
                                                                GM_ST(void *, (uint8_t *)&t51, (void *)(gm_sllrmadmbrcvv));
                                                                GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(53));
                                                                gm_l11 = t51;
                                                            }
                                                            {
                                                                void * t52 = gm_l9;
                                                                gm_tl874t2atnlh3 t53 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t52) + 72));
                                                                gm_b16_8 t54;
                                                                GM_ST(void *, (uint8_t *)&t54, (void *)(gm_spic2fa42u00e));
                                                                GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(18));
                                                                gm_b16_8 t55 = gm_l11;
                                                                gm_t4gut2c9fj1kt t56;
                                                                gm_f_lint_diag_4vra4qu((void *)&t56, (void *)NULL, cx, NULL, t53, t54, t55);
                                                                if (gm_unw.kind) {
                                                                    goto gm_lad;
                                                                }
                                                                F.l12 = t56;
                                                            }
                                                            void * t57 = (void *)((uint8_t *)&F.l12);
                                                            gm_b16_8 t58;
                                                            GM_ST(void *, (uint8_t *)&t58, (void *)(gm_s6mpqtffqhn51));
                                                            GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(79));
                                                            gm_f_add_note_46v502s(t57, t58);
                                                            {
                                                                void * t59 = gm_l0;
                                                                gm_b0_1 t60 = gm_f_push_l0e5v4c((void *)NULL, cx, NULL, t59, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l12)));
                                                                if (gm_unw.kind) {
                                                                    goto gm_lad;
                                                                }
                                                            }
                                                            goto gm_lb13;
                                                        }
gm_lb14:;
                                                    }
gm_lb13:;
                                                    goto gm_lb11;
                                                }
gm_lb12:;
                                            }
gm_lb11:;
                                            goto gm_lb9;
                                        }
gm_lb10:;
                                        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 586, 9);
                                    }
gm_lb9:;
                                }
gm_lb8:;
                                uint64_t t61 = gm_l7;
                                uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                                uint64_t t64 = r62;
                                gm_l7 = t64;
                            }
                        }
gm_lb6:;
                        void * t65 = gm_l0;
                        void * t66 = (void *)(((uint8_t *)&F.l5) + 32);
                        gm_b0_1 t67 = gm_f_block_btrailr((void *)NULL, cx, NULL, t65, t66);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 584, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t68 = gm_l4;
            uint64_t r69; bool o70 = __builtin_add_overflow(t68, ((uint64_t)1ULL), &r69);
            uint64_t t71 = r69;
            gm_l4 = t71;
        }
    }
gm_lb1:;
    {
        {
            void * t72 = gm_l2;
            uint8_t t73 = (uint8_t)(((((uint8_t *)t72) + 16))[48] != 0);
            if (!t73) {
                goto gm_lb16;
            }
            memmove(((uint8_t *)&F.l13), (((uint8_t *)t72) + 16), 48);
            {
                {
                    uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&F.l13) + 0));
                    uint8_t t75 = (uint8_t)(t74 == ((uint8_t)0ULL));
                    if (!t75) {
                        goto gm_lb18;
                    }
                    memmove(((uint8_t *)&F.l14), (((uint8_t *)&F.l13) + 8), 40);
                    void * t76 = gm_l0;
                    void * t77 = (void *)((uint8_t *)&F.l14);
                    gm_b0_1 t78 = gm_f_block_btrailr((void *)NULL, cx, NULL, t76, t77);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb17;
                }
gm_lb18:;
                {
                    uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&F.l13) + 0));
                    uint8_t t80 = (uint8_t)(t79 == ((uint8_t)1ULL));
                    if (!t80) {
                        goto gm_lb19;
                    }
                    {
                        memmove(((uint8_t *)&gm_l15), (((uint8_t *)&F.l13) + 8), 32);
                    }
                    void * t81 = GM_LD(void *, ((((uint8_t *)&F.l13) + 8) + 32));
                    gm_l16 = t81;
                    {
                        void * t82 = gm_l0;
                        void * t83 = gm_l16;
                        gm_t08olq97rscn3 t84 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&gm_l15));
                        {
                            {
                                uint8_t t85 = GM_LD(uint8_t, ((((uint8_t *)t83) + 8) + 0));
                                uint8_t t86 = (uint8_t)(t85 == ((uint8_t)44ULL));
                                if (!t86) {
                                    goto gm_lb21;
                                }
                                gm_l17 = ((uint8_t)1);
                                goto gm_lb20;
                            }
gm_lb21:;
                            {
                                gm_l17 = ((uint8_t)0);
                                goto gm_lb20;
                            }
gm_lb22:;
                            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 602, 33);
                        }
gm_lb20:;
                        uint8_t t87 = gm_l17;
                        gm_b0_1 t88 = gm_f_capture_abs7k4r((void *)NULL, cx, NULL, t82, &t84, t87);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    void * t89 = gm_l0;
                    void * t90 = gm_l16;
                    gm_b0_1 t91 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t89, t90);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb17;
                }
gm_lb19:;
                gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 599, 9);
            }
gm_lb17:;
            goto gm_lb15;
        }
gm_lb16:;
    }
gm_lb15:;
    void * t92 = gm_l0;
    void * t93 = gm_l1;
    void * t94 = gm_l2;
    gm_b0_1 t95 = gm_f_needless_bool_mifff69((void *)NULL, cx, NULL, t92, t93, t94);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t96 = gm_l0;
    void * t97 = gm_l2;
    gm_b0_1 t98 = gm_f_empty_branches_7o4oudt((void *)NULL, cx, NULL, t96, t97);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t99 = gm_l0;
    void * t100 = gm_l2;
    gm_b0_1 t101 = gm_f_collapsible_else_ehhjtii((void *)NULL, cx, NULL, t99, t100);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t102 = gm_l0;
    void * t103 = gm_l2;
    gm_b0_1 t104 = gm_f_identical_branches_ug6ccol((void *)NULL, cx, NULL, t102, t103);
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
inline __attribute__((aligned(4))) uint64_t gm_f_indent_of_p7r0sg5(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_indent_of_p7r0sg5 F;
    void * gm_l0; /* w */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* src */
    uint64_t gm_l3; /* start */
    uint64_t gm_l4; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 285u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, ((uint8_t *)t8));
    uint32_t t10 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    void * t11 = gm_f_file_o15fqku(t9, t10);
    gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)t11) + 16));
    gm_l2 = t12;
    uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    uint64_t t14 = (uint64_t)t13;
    gm_l3 = t14;
    {
        for (;;) {
            uint64_t t15 = gm_l3;
            uint8_t t16 = (t15 > ((uint64_t)0ULL));
            if (!t16) {
                goto gm_lb1;
            }
            void * t17 = GM_LD(void *, ((uint8_t *)&gm_l2));
            uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint64_t t19 = gm_l3;
            uint64_t r20; bool o21 = __builtin_sub_overflow(t19, ((uint64_t)1ULL), &r20);
            if (o21) gm_trap("integer overflow in `-`", gm_src10tru552hn49, 778, 29);
            uint64_t t22 = r20;
            if ((uintptr_t)t22 >= (uintptr_t)(t18)) gm_trap_index((uintptr_t)t22, t18, gm_src10tru552hn49, 778, 25);
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)t17) + (uintptr_t)t22 * 1));
            uint8_t t24 = (!(t23 == ((uint8_t)10ULL)));
            if (!t24) {
                goto gm_lb1;
            }
            {
                uint64_t t25 = gm_l3;
                uint64_t r26; bool o27 = __builtin_sub_overflow(t25, ((uint64_t)1ULL), &r26);
                if (o27) gm_trap("integer overflow in `-`", gm_src10tru552hn49, 778, 54);
                uint64_t t28 = r26;
                gm_l3 = t28;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t29 = gm_l3;
            uint64_t t30 = gm_l4;
            uint64_t r31; bool o32 = __builtin_add_overflow(t29, t30, &r31);
            if (o32) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 780, 11);
            uint64_t t33 = r31;
            uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t35 = (t33 < t34);
            if (!t35) {
                goto gm_lb4;
            }
            void * t36 = GM_LD(void *, ((uint8_t *)&gm_l2));
            uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint64_t t38 = gm_l3;
            uint64_t t39 = gm_l4;
            uint64_t r40; bool o41 = __builtin_add_overflow(t38, t39, &r40);
            if (o41) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 780, 39);
            uint64_t t42 = r40;
            if ((uintptr_t)t42 >= (uintptr_t)(t37)) gm_trap_index((uintptr_t)t42, t37, gm_src10tru552hn49, 780, 35);
            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)t36) + (uintptr_t)t42 * 1));
            gm_tqs5p4n2tq19v t44;
            GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)32ULL));
            GM_ST(uint8_t, ((uint8_t *)&t44 + 1), ((uint8_t)9ULL));
            uint8_t t45 = 0;
            if (gm_eqjmccqdb90f(((uint8_t *)&t44) + 0, ((uint8_t *)&t43))) t45 = 1;
            if (gm_eqjmccqdb90f(((uint8_t *)&t44) + 1, ((uint8_t *)&t43))) t45 = 1;
            uint8_t t46 = (uint8_t)(t45);
            if (!t46) {
                goto gm_lb4;
            }
            {
                uint64_t t47 = gm_l4;
                uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                if (o49) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 780, 67);
                uint64_t t50 = r48;
                gm_l4 = t50;
            }
gm_lb6:;
        }
    }
gm_lb4:;
    uint64_t t51 = gm_l4;
    F.ret = t51;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_constant_call_vj2jgti(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_constant_call_vj2jgti F;
    void * gm_l0; /* e */
    gm_b16_8 gm_l1; /* args */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_b16_8 gm_l7; /* elems */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    GM_STACK_CHECK(&F, 306u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t22 = gm_l0;
    {
        {
            uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)19ULL));
            if (!t24) {
                goto gm_lb2;
            }
            gm_b16_8 t25 = GM_LD(gm_b16_8, (((((uint8_t *)t22) + 8) + 8) + 8));
            gm_l1 = t25;
            gm_b16_8 t26 = gm_l1;
            gm_l2 = t26;
            gm_l3 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t27 = gm_l3;
                    uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                    uint8_t t29 = (t27 < t28);
                    if (!t29) {
                        goto gm_lb3;
                    }
                    {
                        void * t30 = GM_LD(void *, ((uint8_t *)&gm_l2));
                        uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                        uint64_t t32 = gm_l3;
                        {
                            {
                                memmove(((uint8_t *)&F.l4), (((uint8_t *)t30) + (uintptr_t)t32 * 144), 144);
                                {
                                    {
                                        {
                                            {
                                                uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                                                uint8_t t34 = (uint8_t)(t33 == ((uint8_t)0ULL));
                                                if (!t34) {
                                                    goto gm_lb11;
                                                }
                                                memmove(((uint8_t *)&F.l5), (((uint8_t *)&F.l4) + 8), 88);
                                                goto gm_lb10;
                                            }
gm_lb11:;
                                            {
                                                uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                                                uint8_t t36 = (uint8_t)(t35 == ((uint8_t)1ULL));
                                                if (!t36) {
                                                    goto gm_lb12;
                                                }
                                                memmove(((uint8_t *)&F.l5), ((((uint8_t *)&F.l4) + 8) + 32), 88);
                                                goto gm_lb10;
                                            }
gm_lb12:;
                                            goto gm_lb9;
                                        }
gm_lb10:;
                                        void * t37 = (void *)((uint8_t *)&F.l5);
                                        uint8_t t38 = gm_f_is_literal_9ac2fdt(t37);
                                        uint8_t t39 = (uint8_t)!t38;
                                        if (t39) {
                                            F.ret = ((uint8_t)0);
                                            goto gm_fret;
                                        }
                                        goto gm_lb8;
                                    }
gm_lb9:;
                                    {
                                        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                                        uint8_t t41 = (uint8_t)(t40 == ((uint8_t)2ULL));
                                        if (!t41) {
                                            goto gm_lb13;
                                        }
                                        F.ret = ((uint8_t)0);
                                        goto gm_fret;
                                    }
gm_lb13:;
                                    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 853, 17);
                                }
gm_lb8:;
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 852, 13);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t42 = gm_l3;
                    uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
                    uint64_t t45 = r43;
                    gm_l3 = t45;
                }
            }
gm_lb3:;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t47 = (uint8_t)(t46 == ((uint8_t)10ULL));
            if (!t47) {
                goto gm_lb14;
            }
            gm_b16_8 t48 = GM_LD(gm_b16_8, ((((uint8_t *)t22) + 8) + 8));
            gm_l7 = t48;
            gm_b16_8 t49 = gm_l7;
            gm_l8 = t49;
            gm_l9 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t50 = gm_l9;
                    uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                    uint8_t t52 = (t50 < t51);
                    if (!t52) {
                        goto gm_lb15;
                    }
                    {
                        void * t53 = GM_LD(void *, ((uint8_t *)&gm_l8));
                        uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                        uint64_t t55 = gm_l9;
                        {
                            {
                                memmove(((uint8_t *)&F.l10), (((uint8_t *)t53) + (uintptr_t)t55 * 136), 136);
                                void * t56 = (void *)(((uint8_t *)&F.l10) + 40);
                                uint8_t t57 = gm_f_is_literal_9ac2fdt(t56);
                                uint8_t t58 = (uint8_t)!t57;
                                if (t58) {
                                    F.ret = ((uint8_t)0);
                                    goto gm_fret;
                                }
                                goto gm_lb18;
                            }
gm_lb19:;
                            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 861, 13);
                        }
gm_lb18:;
                    }
gm_lb17:;
                    uint64_t t59 = gm_l9;
                    uint64_t r60; bool o61 = __builtin_add_overflow(t59, ((uint64_t)1ULL), &r60);
                    uint64_t t62 = r60;
                    gm_l9 = t62;
                }
            }
gm_lb15:;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb14:;
        {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb20:;
        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 850, 5);
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_literal_9ac2fdt(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_literal_9ac2fdt F;
    void * gm_l0; /* e */
    gm_t9qkh0rsooc8d gm_l1; /* op */
    void * gm_l2; /* operand */
    void * gm_l3; /* operand */
    void * gm_l4; /* lhs */
    void * gm_l5; /* rhs */
    GM_STACK_CHECK(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l6;
    uint8_t gm_l7;
    void * t13 = gm_l0;
    {
        {
            {
                {
                    uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t13) + 8) + 0));
                    uint8_t t15 = (uint8_t)(t14 == ((uint8_t)0ULL));
                    if (!t15) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t13) + 8) + 0));
                    uint8_t t17 = (uint8_t)(t16 == ((uint8_t)1ULL));
                    if (!t17) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t13) + 8) + 0));
                    uint8_t t19 = (uint8_t)(t18 == ((uint8_t)2ULL));
                    if (!t19) {
                        goto gm_lb6;
                    }
                    goto gm_lb3;
                }
gm_lb6:;
                {
                    uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t13) + 8) + 0));
                    uint8_t t21 = (uint8_t)(t20 == ((uint8_t)3ULL));
                    if (!t21) {
                        goto gm_lb7;
                    }
                    goto gm_lb3;
                }
gm_lb7:;
                goto gm_lb2;
            }
gm_lb3:;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t13) + 8) + 0));
            uint8_t t23 = (uint8_t)(t22 == ((uint8_t)12ULL));
            if (!t23) {
                goto gm_lb8;
            }
            gm_t9qkh0rsooc8d t24 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t13) + 8) + 8));
            gm_l1 = t24;
            void * t25 = GM_LD(void *, (((((uint8_t *)t13) + 8) + 8) + 8));
            gm_l2 = t25;
            gm_t9qkh0rsooc8d t26 = gm_l1;
            gm_t9qkh0rsooc8d t27;
            GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)0ULL));
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)1ULL));
            gm_t0am46iohlpol t29;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 0), t27);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 1), t28);
            uint8_t t30 = 0;
            if (gm_eqlv5i3f25jk(((uint8_t *)&t29) + 0, ((uint8_t *)&t26))) t30 = 1;
            if (gm_eqlv5i3f25jk(((uint8_t *)&t29) + 1, ((uint8_t *)&t26))) t30 = 1;
            uint8_t t31 = (uint8_t)(t30);
            if (t31) {
                void * t32 = gm_l2;
                uint8_t t33 = gm_f_is_literal_9ac2fdt(t32);
                gm_l6 = t33;
            } else {
                gm_l6 = ((uint8_t)0);
            }
            uint8_t t34 = gm_l6;
            F.ret = t34;
            goto gm_fret;
        }
gm_lb8:;
        {
            uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t13) + 8) + 0));
            uint8_t t36 = (uint8_t)(t35 == ((uint8_t)9ULL));
            if (!t36) {
                goto gm_lb9;
            }
            void * t37 = GM_LD(void *, ((((uint8_t *)t13) + 8) + 8));
            gm_l3 = t37;
            void * t38 = gm_l3;
            uint8_t t39 = gm_f_is_literal_9ac2fdt(t38);
            F.ret = t39;
            goto gm_fret;
        }
gm_lb9:;
        {
            uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)t13) + 8) + 0));
            uint8_t t41 = (uint8_t)(t40 == ((uint8_t)13ULL));
            if (!t41) {
                goto gm_lb10;
            }
            void * t42 = GM_LD(void *, (((((uint8_t *)t13) + 8) + 8) + 8));
            gm_l4 = t42;
            void * t43 = GM_LD(void *, (((((uint8_t *)t13) + 8) + 8) + 16));
            gm_l5 = t43;
            void * t44 = gm_l4;
            uint8_t t45 = gm_f_is_literal_9ac2fdt(t44);
            if (t45) {
                void * t46 = gm_l5;
                uint8_t t47 = gm_f_is_literal_9ac2fdt(t46);
                gm_l7 = t47;
            } else {
                gm_l7 = ((uint8_t)0);
            }
            uint8_t t48 = gm_l7;
            F.ret = t48;
            goto gm_fret;
        }
gm_lb10:;
        {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb11:;
        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 837, 5);
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_is_lower_q12rpq9(uint8_t a0);
extern __attribute__((aligned(4))) uint8_t gm_f_is_upper_68kn81r(uint8_t a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_item_a3ennq0(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_item_a3ennq0 F;
    void * gm_l0; /* w */
    void * gm_l1; /* it */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l6; /* args */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_tibupt4dmcqai gm_l10; /* s */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_b16_8 gm_l14; /* args */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_tibupt4dmcqai gm_l18; /* s */
    gm_b16_8 gm_l19; /* name */
    uint8_t gm_l22; /* foreign_ */
    void * gm_l23; /* t */
    uint64_t gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    uint64_t gm_l27; /* i */
    gm_b16_8 gm_l28; /* els */
    uint64_t gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    uint64_t gm_l31; /* i */
    void * gm_l32; /* h */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    void * gm_l37; /* e */
    GM_STACK_CHECK(&F, 1927u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l38;
    uint8_t gm_l39;
    gm_tf5srh4mp4b3i gm_l40;
    gm_tabb710d9mq2e gm_l41;
    gm_tftb1odc739t5 gm_l42;
    gm_tftb1odc739t5 gm_l43;
    gm_tftb1odc739t5 gm_l44;
    {
        gm_trqfvmjtt4sij t61;
        gm_f_list_epvducg((void *)&t61, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t61;
        void * t62 = gm_l1;
        gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)t62) + 40));
        gm_l3 = t63;
        gm_l4 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t64 = gm_l4;
                uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint8_t t66 = (t64 < t65);
                if (!t66) {
                    goto gm_lb1;
                }
                {
                    void * t67 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    uint64_t t69 = gm_l4;
                    {
                        {
                            memmove(((uint8_t *)&F.l5), (((uint8_t *)t67) + (uintptr_t)t69 * 72), 72);
                            gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
                            gm_b16_8 t71;
                            GM_ST(void *, (uint8_t *)&t71, (void *)(gm_slb6cj7ndgvdm));
                            GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(5));
                            uint8_t t72 = (!gm_eq0iqiubfm7e(((uint8_t *)&t70), ((uint8_t *)&t71)));
                            if (t72) {
                                goto gm_lb3;
                            }
                            uint8_t t73 = (uint8_t)(((((uint8_t *)&F.l5) + 32))[16] != 0);
                            if (t73) {
                                gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 32));
                                gm_l6 = t74;
                            } else {
                                goto gm_lb3;
                            }
                            gm_b16_8 t75 = gm_l6;
                            gm_l7 = t75;
                            gm_l8 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t76 = gm_l8;
                                    uint64_t t77 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                    uint8_t t78 = (t76 < t77);
                                    if (!t78) {
                                        goto gm_lb6;
                                    }
                                    {
                                        void * t79 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                        uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                        uint64_t t81 = gm_l8;
                                        {
                                            {
                                                memmove(((uint8_t *)&F.l9), (((uint8_t *)t79) + (uintptr_t)t81 * 88), 88);
                                                {
                                                    {
                                                        uint8_t t82 = GM_LD(uint8_t, ((((uint8_t *)&F.l9) + 8) + 0));
                                                        uint8_t t83 = (uint8_t)(t82 == ((uint8_t)2ULL));
                                                        if (!t83) {
                                                            goto gm_lb12;
                                                        }
                                                        memmove(((uint8_t *)&gm_l10), ((((uint8_t *)&F.l9) + 8) + 8), 24);
                                                        void * t84 = (void *)((uint8_t *)&F.l2);
                                                        gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l10));
                                                        gm_b16_8 t86 = GM_LD(gm_b16_8, ((uint8_t *)&t85));
                                                        gm_tftb1odc739t5 t87 = gm_f_push_3ef746b(t84, t86);
                                                        gm_l38 = t87;
                                                        uint8_t t88 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                                                        if (!t88) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 1, gm_src10tru552hn49, 88, 36);
                                                        goto gm_lb11;
                                                    }
gm_lb12:;
                                                }
gm_lb11:;
                                                goto gm_lb9;
                                            }
gm_lb10:;
                                            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 87, 9);
                                        }
gm_lb9:;
                                    }
gm_lb8:;
                                    uint64_t t89 = gm_l8;
                                    uint64_t r90; bool o91 = __builtin_add_overflow(t89, ((uint64_t)1ULL), &r90);
                                    uint64_t t92 = r90;
                                    gm_l8 = t92;
                                }
                            }
gm_lb6:;
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 84, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t93 = gm_l4;
                uint64_t r94; bool o95 = __builtin_add_overflow(t93, ((uint64_t)1ULL), &r94);
                uint64_t t96 = r94;
                gm_l4 = t96;
            }
        }
gm_lb1:;
    }
    {
        void * t97 = gm_l1;
        gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)t97) + 40));
        gm_l11 = t98;
        gm_l12 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t99 = gm_l12;
                uint64_t t100 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                uint8_t t101 = (t99 < t100);
                if (!t101) {
                    goto gm_lb13;
                }
                {
                    void * t102 = GM_LD(void *, ((uint8_t *)&gm_l11));
                    uint64_t t103 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                    uint64_t t104 = gm_l12;
                    {
                        {
                            memmove(((uint8_t *)&F.l13), (((uint8_t *)t102) + (uintptr_t)t104 * 72), 72);
                            gm_b16_8 t105 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                            gm_b16_8 t106;
                            GM_ST(void *, (uint8_t *)&t106, (void *)(gm_slb6cj7ndgvdm));
                            GM_ST(uintptr_t, (uint8_t *)&t106 + 8, (uintptr_t)(5));
                            uint8_t t107 = (!gm_eq0iqiubfm7e(((uint8_t *)&t105), ((uint8_t *)&t106)));
                            if (t107) {
                                goto gm_lb15;
                            }
                            uint8_t t108 = (uint8_t)(((((uint8_t *)&F.l13) + 32))[16] != 0);
                            if (t108) {
                                gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 32));
                                gm_l14 = t109;
                            } else {
                                goto gm_lb15;
                            }
                            gm_b16_8 t110 = gm_l14;
                            gm_l15 = t110;
                            gm_l16 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t111 = gm_l16;
                                    uint64_t t112 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                    uint8_t t113 = (t111 < t112);
                                    if (!t113) {
                                        goto gm_lb18;
                                    }
                                    {
                                        void * t114 = GM_LD(void *, ((uint8_t *)&gm_l15));
                                        uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                        uint64_t t116 = gm_l16;
                                        {
                                            {
                                                memmove(((uint8_t *)&F.l17), (((uint8_t *)t114) + (uintptr_t)t116 * 88), 88);
                                                {
                                                    {
                                                        uint8_t t117 = GM_LD(uint8_t, ((((uint8_t *)&F.l17) + 8) + 0));
                                                        uint8_t t118 = (uint8_t)(t117 == ((uint8_t)2ULL));
                                                        if (!t118) {
                                                            goto gm_lb24;
                                                        }
                                                        memmove(((uint8_t *)&gm_l18), ((((uint8_t *)&F.l17) + 8) + 8), 24);
                                                        gm_b16_8 t119 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l18));
                                                        gm_l19 = t119;
                                                        {
                                                            {
                                                                gm_b16_8 t120 = gm_l19;
                                                                gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&t120));
                                                                gm_b16_8 t122;
                                                                GM_ST(void *, (uint8_t *)&t122, (void *)(gm_s0nf1rg04f3m5));
                                                                GM_ST(uintptr_t, (uint8_t *)&t122 + 8, (uintptr_t)(3));
                                                                uint8_t t123 = (!gm_eq0iqiubfm7e(((uint8_t *)&t121), ((uint8_t *)&t122)));
                                                                if (!t123) {
                                                                    goto gm_lb26;
                                                                }
                                                                gm_b16_8 t124 = gm_l19;
                                                                gm_b16_8 t125 = GM_LD(gm_b16_8, ((uint8_t *)&t124));
                                                                gm_tf5srh4mp4b3i t126 = gm_f_rule_scmud9h(t125);
                                                                gm_l40 = t126;
                                                                {
                                                                    {
                                                                        uint8_t t127 = (uint8_t)((((uint8_t *)&gm_l40))[8] != 0);
                                                                        if (t127) {
                                                                            goto gm_lb28;
                                                                        }
                                                                        gm_l39 = ((uint8_t)1);
                                                                        goto gm_lb27;
                                                                    }
gm_lb28:;
                                                                    {
                                                                        gm_l39 = ((uint8_t)0);
                                                                        goto gm_lb27;
                                                                    }
gm_lb29:;
                                                                    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 97, 42);
                                                                }
gm_lb27:;
                                                                uint8_t t128 = gm_l39;
                                                                if (!t128) {
                                                                    goto gm_lb26;
                                                                }
                                                                {
                                                                    gm_tl874t2atnlh3 t129 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 72));
                                                                    gm_b16_8 t130 = gm_l19;
                                                                    gm_tnpifv20irnph t131;
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t131 + 0), t130);
                                                                    gm_f_format_h13dbf2((void *)&gm_l41, (void *)NULL, cx, NULL, t131);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    uint8_t t132 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                                                                    if (!t132) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 8, gm_src10tru552hn49, 98, 45);
                                                                    gm_b16_8 t133 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l41) + 8));
                                                                    gm_b16_8 t134 = GM_LD(gm_b16_8, ((uint8_t *)&t133));
                                                                    gm_t4gut2c9fj1kt t135;
                                                                    gm_f_error_s6hn871((void *)&t135, (void *)NULL, cx, NULL, t129, t134);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    F.l20 = t135;
                                                                }
                                                                void * t136 = (void *)((uint8_t *)&F.l20);
                                                                gm_b16_8 t137;
                                                                GM_ST(void *, (uint8_t *)&t137, (void *)(gm_s91c3em0lmpov));
                                                                GM_ST(uintptr_t, (uint8_t *)&t137 + 8, (uintptr_t)(36));
                                                                gm_f_add_note_46v502s(t136, t137);
                                                                {
                                                                    void * t138 = gm_l0;
                                                                    void * t139 = (void *)((((uint8_t *)t138) + 8) + 176);
                                                                    gm_tftb1odc739t5 t140 = gm_f_push_l0rgbjf(t139, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l20)));
                                                                    gm_l42 = t140;
                                                                }
                                                                uint8_t t141 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
                                                                if (!t141) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l42) + 1, gm_src10tru552hn49, 100, 21);
                                                                goto gm_lb25;
                                                            }
gm_lb26:;
                                                        }
gm_lb25:;
                                                        goto gm_lb23;
                                                    }
gm_lb24:;
                                                }
gm_lb23:;
                                                goto gm_lb21;
                                            }
gm_lb22:;
                                            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 94, 9);
                                        }
gm_lb21:;
                                    }
gm_lb20:;
                                    uint64_t t142 = gm_l16;
                                    uint64_t r143; bool o144 = __builtin_add_overflow(t142, ((uint64_t)1ULL), &r143);
                                    uint64_t t145 = r143;
                                    gm_l16 = t145;
                                }
                            }
gm_lb18:;
                            goto gm_lb16;
                        }
gm_lb17:;
                        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 91, 5);
                    }
gm_lb16:;
                }
gm_lb15:;
                uint64_t t146 = gm_l12;
                uint64_t r147; bool o148 = __builtin_add_overflow(t146, ((uint64_t)1ULL), &r147);
                uint64_t t149 = r147;
                gm_l12 = t149;
            }
        }
gm_lb13:;
    }
    {
        uint64_t t150 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
        uint8_t t151 = (t150 > ((uint64_t)0ULL));
        if (t151) {
            {
                void * t152 = gm_l0;
                void * t153 = (void *)((((uint8_t *)t152) + 8) + 136);
                void * t154 = gm_l1;
                gm_tl874t2atnlh3 t155 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t154) + 4));
                gm_trqfvmjtt4sij t156 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l2));
                gm_tsu0mic7gtnv6 t157;
                GM_ST(uint32_t, (uint8_t *)&t157 + 12, 0);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t157 + 0), t155);
                GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t157 + 16), t156);
                gm_tftb1odc739t5 t158 = gm_f_push_amge2t0(t153, &t157);
                gm_l43 = t158;
            }
            uint8_t t159 = (uint8_t)!((((uint8_t *)&gm_l43))[0] != 0);
            if (!t159) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l43) + 1, gm_src10tru552hn49, 105, 32);
        }
        void * t160 = gm_l1;
        {
            {
                uint8_t t161 = GM_LD(uint8_t, ((((uint8_t *)t160) + 64) + 0));
                uint8_t t162 = (uint8_t)(t161 == ((uint8_t)0ULL));
                if (!t162) {
                    goto gm_lb31;
                }
                memmove(((uint8_t *)&F.l21), ((((uint8_t *)t160) + 64) + 8), 64);
                void * t163 = GM_LD(void *, (((uint8_t *)&F.l21) + 40));
                {
                    {
                        uint8_t t164 = GM_LD(uint8_t, ((((uint8_t *)t163) + 8) + 0));
                        uint8_t t165 = (uint8_t)(t164 == ((uint8_t)39ULL));
                        if (!t165) {
                            goto gm_lb33;
                        }
                        gm_l22 = ((uint8_t)1);
                        goto gm_lb32;
                    }
gm_lb33:;
                    {
                        gm_l22 = ((uint8_t)0);
                        goto gm_lb32;
                    }
gm_lb34:;
                    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 108, 25);
                }
gm_lb32:;
                {
                    {
                        uint64_t t166 = GM_LD(uint64_t, ((((uint8_t *)&F.l21) + 8) + 8));
                        uint8_t t167 = (t166 > ((uint64_t)0ULL));
                        if (!t167) {
                            goto gm_lb36;
                        }
                        uint8_t t168 = gm_l22;
                        uint8_t t169 = (uint8_t)!t168;
                        if (!t169) {
                            goto gm_lb36;
                        }
                        void * t170 = gm_l0;
                        void * t171 = GM_LD(void *, (((uint8_t *)&F.l21) + 8));
                        uint64_t t172 = GM_LD(uint64_t, ((((uint8_t *)&F.l21) + 8) + 8));
                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t172)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t172, gm_src10tru552hn49, 109, 68);
                        void * t173 = (void *)((uint8_t *)&F.l21);
                        gm_b0_1 t174 = gm_f_top_level_name_dv4kpc9((void *)NULL, cx, NULL, t170, &(*(gm_t08olq97rscn3 *)(((uint8_t *)t171) + (uintptr_t)((uint64_t)0ULL) * 32)), t173);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb35;
                    }
gm_lb36:;
                }
gm_lb35:;
                {
                    {
                        uint8_t t175 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l21) + 24)) != 0);
                        if (!t175) {
                            goto gm_lb38;
                        }
                        void * t176 = GM_LD(void *, (((uint8_t *)&F.l21) + 24));
                        gm_l23 = t176;
                        void * t177 = gm_l0;
                        void * t178 = gm_l23;
                        gm_b0_1 t179 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t177, t178);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb37;
                    }
gm_lb38:;
                }
gm_lb37:;
                void * t180 = gm_l0;
                void * t181 = GM_LD(void *, (((uint8_t *)&F.l21) + 40));
                gm_b0_1 t182 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t180, t181);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_lb30;
            }
gm_lb31:;
            {
                uint8_t t183 = GM_LD(uint8_t, ((((uint8_t *)t160) + 64) + 0));
                uint8_t t184 = (uint8_t)(t183 == ((uint8_t)1ULL));
                if (!t184) {
                    goto gm_lb39;
                }
                memmove(((uint8_t *)&F.l24), ((((uint8_t *)t160) + 64) + 8), 64);
                void * t185 = gm_l0;
                void * t186 = (void *)((((uint8_t *)t185) + 8) + 96);
                gm_tl874t2atnlh3 t187 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l24) + 48));
                gm_tftb1odc739t5 t188 = gm_f_push_0mk7udh(t186, GM_CONV(gm_cseahvg8v7p0vs, t187));
                gm_l44 = t188;
                uint8_t t189 = (uint8_t)!((((uint8_t *)&gm_l44))[0] != 0);
                if (!t189) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l44) + 1, gm_src10tru552hn49, 114, 13);
                void * t190 = gm_l0;
                void * t191 = GM_LD(void *, ((uint8_t *)&F.l24));
                gm_b0_1 t192 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t190, t191);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l26 = ((uint64_t)0ULL);
                uint64_t t193 = GM_LD(uint64_t, ((((uint8_t *)&F.l24) + 8) + 8));
                gm_l25 = t193;
                {
                    for (;;) {
                        uint64_t t194 = gm_l26;
                        uint64_t t195 = gm_l25;
                        uint8_t t196 = (t194 < t195);
                        if (!t196) {
                            goto gm_lb40;
                        }
                        {
                            {
                                {
                                    uint64_t t197 = gm_l26;
                                    gm_l27 = t197;
                                    void * t198 = gm_l0;
                                    void * t199 = GM_LD(void *, (((uint8_t *)&F.l24) + 8));
                                    uint64_t t200 = GM_LD(uint64_t, ((((uint8_t *)&F.l24) + 8) + 8));
                                    uint64_t t201 = gm_l27;
                                    void * t202 = (void *)(((uint8_t *)t199) + (uintptr_t)t201 * 160);
                                    gm_b0_1 t203 = gm_f_item_a3ennq0((void *)NULL, cx, NULL, t198, t202);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_lb43;
                                }
gm_lb44:;
                                gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 116, 13);
                            }
gm_lb43:;
                        }
gm_lb42:;
                        uint64_t t204 = gm_l26;
                        uint64_t r205; bool o206 = __builtin_add_overflow(t204, ((uint64_t)1ULL), &r205);
                        uint64_t t207 = r205;
                        gm_l26 = t207;
                    }
                }
gm_lb40:;
                {
                    {
                        uint8_t t208 = (uint8_t)(((((uint8_t *)&F.l24) + 24))[16] != 0);
                        if (!t208) {
                            goto gm_lb46;
                        }
                        gm_b16_8 t209 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 24));
                        gm_l28 = t209;
                        gm_l30 = ((uint64_t)0ULL);
                        uint64_t t210 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                        gm_l29 = t210;
                        {
                            for (;;) {
                                uint64_t t211 = gm_l30;
                                uint64_t t212 = gm_l29;
                                uint8_t t213 = (t211 < t212);
                                if (!t213) {
                                    goto gm_lb47;
                                }
                                {
                                    {
                                        {
                                            uint64_t t214 = gm_l30;
                                            gm_l31 = t214;
                                            void * t215 = gm_l0;
                                            void * t216 = GM_LD(void *, ((uint8_t *)&gm_l28));
                                            uint64_t t217 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                                            uint64_t t218 = gm_l31;
                                            void * t219 = (void *)(((uint8_t *)t216) + (uintptr_t)t218 * 160);
                                            gm_b0_1 t220 = gm_f_item_a3ennq0((void *)NULL, cx, NULL, t215, t219);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            goto gm_lb50;
                                        }
gm_lb51:;
                                        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 118, 17);
                                    }
gm_lb50:;
                                }
gm_lb49:;
                                uint64_t t221 = gm_l30;
                                uint64_t r222; bool o223 = __builtin_add_overflow(t221, ((uint64_t)1ULL), &r222);
                                uint64_t t224 = r222;
                                gm_l30 = t224;
                            }
                        }
gm_lb47:;
                        goto gm_lb45;
                    }
gm_lb46:;
                }
gm_lb45:;
                goto gm_lb30;
            }
gm_lb39:;
            {
                uint8_t t225 = GM_LD(uint8_t, ((((uint8_t *)t160) + 64) + 0));
                uint8_t t226 = (uint8_t)(t225 == ((uint8_t)2ULL));
                if (!t226) {
                    goto gm_lb52;
                }
                void * t227 = GM_LD(void *, ((((uint8_t *)t160) + 64) + 8));
                gm_l32 = t227;
                void * t228 = gm_l32;
                gm_b16_8 t229 = GM_LD(gm_b16_8, (((uint8_t *)t228) + 64));
                gm_l33 = t229;
                gm_l34 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t230 = gm_l34;
                        uint64_t t231 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                        uint8_t t232 = (t230 < t231);
                        if (!t232) {
                            goto gm_lb53;
                        }
                        {
                            void * t233 = GM_LD(void *, ((uint8_t *)&gm_l33));
                            uint64_t t234 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                            uint64_t t235 = gm_l34;
                            {
                                {
                                    memmove(((uint8_t *)&F.l35), (((uint8_t *)t233) + (uintptr_t)t235 * 72), 72);
                                    void * t236 = gm_l0;
                                    void * t237 = GM_LD(void *, (((uint8_t *)&F.l35) + 48));
                                    gm_b0_1 t238 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t236, t237);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_lb56;
                                }
gm_lb57:;
                                gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 122, 13);
                            }
gm_lb56:;
                        }
gm_lb55:;
                        uint64_t t239 = gm_l34;
                        uint64_t r240; bool o241 = __builtin_add_overflow(t239, ((uint64_t)1ULL), &r240);
                        uint64_t t242 = r240;
                        gm_l34 = t242;
                    }
                }
gm_lb53:;
                goto gm_lb30;
            }
gm_lb52:;
            {
                uint8_t t243 = GM_LD(uint8_t, ((((uint8_t *)t160) + 64) + 0));
                uint8_t t244 = (uint8_t)(t243 == ((uint8_t)3ULL));
                if (!t244) {
                    goto gm_lb58;
                }
                memmove(((uint8_t *)&F.l36), ((((uint8_t *)t160) + 64) + 8), 88);
                void * t245 = gm_l0;
                void * t246 = (void *)(((uint8_t *)&F.l36) + 40);
                gm_b0_1 t247 = gm_f_block_btrailr((void *)NULL, cx, NULL, t245, t246);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_lb30;
            }
gm_lb58:;
            {
                uint8_t t248 = GM_LD(uint8_t, ((((uint8_t *)t160) + 64) + 0));
                uint8_t t249 = (uint8_t)(t248 == ((uint8_t)4ULL));
                if (!t249) {
                    goto gm_lb59;
                }
                void * t250 = GM_LD(void *, ((((uint8_t *)t160) + 64) + 8));
                gm_l37 = t250;
                void * t251 = gm_l0;
                void * t252 = gm_l37;
                gm_b0_1 t253 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t251, t252);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_lb30;
            }
gm_lb59:;
            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 106, 5);
        }
gm_lb30:;
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
__attribute__((aligned(4))) uint8_t gm_f_mentions_4p99unq(void * a0, gm_b16_8 a1, uint32_t a2, uint32_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mentions_4p99unq F;
    void * gm_l0; /* w */
    gm_b16_8 gm_l1; /* name */
    uint32_t gm_l2; /* lo */
    uint32_t gm_l3; /* hi */
    uint64_t gm_l4; /* a */
    uint64_t gm_l5; /* b */
    uint64_t gm_l6; /* m */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l10; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 433u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = ((uint64_t)0ULL);
    void * t12 = gm_l0;
    uint64_t t13 = GM_LD(uint64_t, ((((uint8_t *)t12) + 264) + 8));
    gm_l5 = t13;
    {
        for (;;) {
            uint64_t t14 = gm_l4;
            uint64_t t15 = gm_l5;
            uint8_t t16 = (t14 < t15);
            if (!t16) {
                goto gm_lb1;
            }
            {
                uint64_t t17 = gm_l4;
                uint64_t t18 = gm_l5;
                uint64_t t19 = gm_l4;
                uint64_t r20; bool o21 = __builtin_sub_overflow(t18, t19, &r20);
                if (o21) gm_trap("integer overflow in `-`", gm_src10tru552hn49, 367, 19);
                uint64_t t22 = r20;
                if (((uint64_t)2ULL) == 0) gm_trap("division by zero", gm_src10tru552hn49, 367, 18);
                uint64_t t23 = (uint64_t)(t22 / ((uint64_t)2ULL));
                uint64_t r24; bool o25 = __builtin_add_overflow(t17, t23, &r24);
                if (o25) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 367, 14);
                uint64_t t26 = r24;
                gm_l6 = t26;
                void * t27 = gm_l0;
                void * t28 = GM_LD(void *, (((uint8_t *)t27) + 264));
                uint64_t t29 = GM_LD(uint64_t, ((((uint8_t *)t27) + 264) + 8));
                uint64_t t30 = gm_l6;
                if ((uintptr_t)t30 >= (uintptr_t)(t29)) gm_trap_index((uintptr_t)t30, t29, gm_src10tru552hn49, 368, 12);
                uint32_t t31 = GM_LD(uint32_t, (((((uint8_t *)t28) + (uintptr_t)t30 * 48) + 32) + 4));
                uint32_t t32 = gm_l2;
                uint8_t t33 = (t31 < t32);
                if (t33) {
                    uint64_t t34 = gm_l6;
                    uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
                    if (o36) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 368, 43);
                    uint64_t t37 = r35;
                    gm_l4 = t37;
                } else {
                    uint64_t t38 = gm_l6;
                    gm_l5 = t38;
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t39 = gm_l0;
    void * t40 = GM_LD(void *, (((uint8_t *)t39) + 264));
    uint64_t t41 = GM_LD(uint64_t, ((((uint8_t *)t39) + 264) + 8));
    uint64_t t42 = gm_l4;
    uint64_t t43 = (uintptr_t)t42;
    uint64_t t44 = (uintptr_t)t41;
    if (t43 > t44 || t44 > (uintptr_t)t41) gm_trap_slice(t43, t44, t41, gm_src10tru552hn49, 370, 14);
    uint64_t t45 = ((uintptr_t)t41) - (uintptr_t)t42;
    gm_b16_8 t46;
    GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t40 + (uintptr_t)t42 * 48)));
    GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(t45));
    gm_l7 = t46;
    gm_l8 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t47 = gm_l8;
            uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
            uint8_t t49 = (t47 < t48);
            if (!t49) {
                goto gm_lb4;
            }
            {
                void * t50 = GM_LD(void *, ((uint8_t *)&gm_l7));
                uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint64_t t52 = gm_l8;
                {
                    {
                        memmove(((uint8_t *)&F.l9), (((uint8_t *)t50) + (uintptr_t)t52 * 48), 48);
                        uint32_t t53 = GM_LD(uint32_t, ((((uint8_t *)&F.l9) + 32) + 4));
                        uint32_t t54 = gm_l3;
                        uint8_t t55 = (t53 >= t54);
                        if (t55) {
                            goto gm_lb4;
                        }
                        {
                            {
                                uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 0));
                                uint8_t t57 = (uint8_t)(t56 == ((uint8_t)0ULL));
                                if (!t57) {
                                    goto gm_lb10;
                                }
                                gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 8));
                                gm_l10 = t58;
                                gm_b16_8 t59 = gm_l10;
                                gm_b16_8 t60 = gm_l1;
                                uint8_t t61 = (gm_eq0iqiubfm7e(((uint8_t *)&t59), ((uint8_t *)&t60)));
                                if (!t61) {
                                    goto gm_lb10;
                                }
                                F.ret = ((uint8_t)1);
                                goto gm_fret;
                            }
gm_lb10:;
                        }
gm_lb9:;
                        goto gm_lb7;
                    }
gm_lb8:;
                    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 370, 5);
                }
gm_lb7:;
            }
gm_lb6:;
            uint64_t t62 = gm_l8;
            uint64_t r63; bool o64 = __builtin_add_overflow(t62, ((uint64_t)1ULL), &r63);
            uint64_t t65 = r63;
            gm_l8 = t65;
        }
    }
gm_lb4:;
    F.ret = ((uint8_t)0);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_names_in_1jbgo4k(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_names_in_1jbgo4k F;
    gm_b16_8 gm_l0; /* tokens */
    gm_b16_8 gm_l1; /* spans */
    void * gm_l2; /* out */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l6; /* s */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_tl874t2atnlh3 gm_l9; /* w */
    GM_STACK_CHECK(&F, 383u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l10;
    uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t17 = ((t16 == ((uint64_t)0ULL)));
    if (t17) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    gm_b16_8 t18 = gm_l0;
    gm_l3 = t18;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t19 = gm_l4;
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t21 = (t19 < t20);
            if (!t21) {
                goto gm_lb1;
            }
            {
                void * t22 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t24 = gm_l4;
                {
                    {
                        memmove(((uint8_t *)&F.l5), (((uint8_t *)t22) + (uintptr_t)t24 * 48), 48);
                        {
                            {
                                uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                                uint8_t t26 = (uint8_t)(t25 == ((uint8_t)0ULL));
                                if (!t26) {
                                    goto gm_lb7;
                                }
                                gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 8));
                                gm_l6 = t27;
                                gm_b16_8 t28 = gm_l1;
                                gm_l7 = t28;
                                gm_l8 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t29 = gm_l8;
                                        uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                        uint8_t t31 = (t29 < t30);
                                        if (!t31) {
                                            goto gm_lb8;
                                        }
                                        {
                                            void * t32 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                            uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                            uint64_t t34 = gm_l8;
                                            {
                                                {
                                                    gm_tl874t2atnlh3 t35 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t32) + (uintptr_t)t34 * 12));
                                                    gm_l9 = t35;
                                                    {
                                                        {
                                                            uint32_t t36 = GM_LD(uint32_t, ((uint8_t *)&gm_l9));
                                                            uint32_t t37 = GM_LD(uint32_t, (((uint8_t *)&F.l5) + 32));
                                                            uint8_t t38 = ((t36 == t37));
                                                            if (!t38) {
                                                                goto gm_lb14;
                                                            }
                                                            uint32_t t39 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 4));
                                                            uint32_t t40 = GM_LD(uint32_t, ((((uint8_t *)&F.l5) + 32) + 4));
                                                            uint8_t t41 = (t39 <= t40);
                                                            if (!t41) {
                                                                goto gm_lb14;
                                                            }
                                                            uint32_t t42 = GM_LD(uint32_t, ((((uint8_t *)&F.l5) + 32) + 8));
                                                            uint32_t t43 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 8));
                                                            uint8_t t44 = (t42 <= t43);
                                                            if (!t44) {
                                                                goto gm_lb14;
                                                            }
                                                            void * t45 = gm_l2;
                                                            gm_b16_8 t46 = gm_l6;
                                                            uint8_t t47 = gm_f_contains_57n7jqd(t45, t46);
                                                            uint8_t t48 = (uint8_t)!t47;
                                                            if (t48) {
                                                                void * t49 = gm_l2;
                                                                gm_b16_8 t50 = gm_l6;
                                                                gm_tftb1odc739t5 t51 = gm_f_put_5c6qgui(t49, t50, ((uint8_t)1));
                                                                gm_l10 = t51;
                                                                uint8_t t52 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                                                                if (!t52) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_src10tru552hn49, 915, 46);
                                                            }
                                                            goto gm_lb8;
                                                        }
gm_lb14:;
                                                    }
gm_lb13:;
                                                    goto gm_lb11;
                                                }
gm_lb12:;
                                                gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 913, 13);
                                            }
gm_lb11:;
                                        }
gm_lb10:;
                                        uint64_t t53 = gm_l8;
                                        uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)1ULL), &r54);
                                        uint64_t t56 = r54;
                                        gm_l8 = t56;
                                    }
                                }
gm_lb8:;
                                goto gm_lb6;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 911, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t57 = gm_l4;
            uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
            uint64_t t60 = r58;
            gm_l4 = t60;
        }
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_needless_bool_mifff69(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_needless_bool_mifff69 F;
    void * gm_l0; /* w */
    void * gm_l1; /* e */
    void * gm_l2; /* i */
    void * gm_l5; /* br */
    void * gm_l6; /* c */
    gm_b16_8 gm_l7; /* prefix */
    uint8_t gm_l8; /* negate */
    gm_t5sj3u8e14oco gm_l9; /* a */
    gm_t5sj3u8e14oco gm_l10; /* b */
    uint8_t gm_l11; /* x */
    uint8_t gm_l12; /* y */
    uint8_t gm_l13; /* x */
    uint8_t gm_l14; /* y */
    gm_b16_8 gm_l15; /* t */
    gm_b16_8 gm_l16; /* value */
    gm_b16_8 gm_l17; /* msg */
    GM_STACK_CHECK(&F, 1012u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t5hib78lmj2mf gm_l19;
    gm_t5hib78lmj2mf gm_l20;
    uint8_t gm_l21;
    gm_tabb710d9mq2e gm_l22;
    gm_tabb710d9mq2e gm_l23;
    void * t13 = gm_l2;
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 8));
    uint8_t t15 = (!(t14 == ((uint64_t)1ULL)));
    if (t15) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t16 = gm_l2;
    uint8_t t17 = (uint8_t)(((((uint8_t *)t16) + 16))[48] != 0);
    if (t17) {
        memmove(((uint8_t *)&F.l3), (((uint8_t *)t16) + 16), 48);
    } else {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    {
        {
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            uint8_t t19 = (uint8_t)(t18 == ((uint8_t)0ULL));
            if (!t19) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l4), (((uint8_t *)&F.l3) + 8), 40);
            void * t20 = gm_l2;
            void * t21 = GM_LD(void *, ((uint8_t *)t20));
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)t20) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t22, gm_src10tru552hn49, 638, 12);
            void * t23 = (void *)(((uint8_t *)t21) + (uintptr_t)((uint64_t)0ULL) * 72);
            gm_l5 = t23;
            void * t24 = gm_l5;
            uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)t24) + 8));
            uint8_t t26 = (!(t25 == ((uint64_t)1ULL)));
            if (t26) {
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            {
                {
                    void * t27 = gm_l5;
                    void * t28 = GM_LD(void *, ((uint8_t *)t27));
                    uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)t27) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t29)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t29, gm_src10tru552hn49, 640, 17);
                    uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t28) + (uintptr_t)((uint64_t)0ULL) * 80) + 0));
                    uint8_t t31 = (uint8_t)(t30 == ((uint8_t)1ULL));
                    if (!t31) {
                        goto gm_lb4;
                    }
                    void * t32 = GM_LD(void *, ((((uint8_t *)t28) + (uintptr_t)((uint64_t)0ULL) * 80) + 8));
                    gm_l6 = t32;
                    gm_b16_8 t33;
                    GM_ST(void *, (uint8_t *)&t33, (void *)(gm_sr4qbrk6tlgaa));
                    GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(0));
                    gm_l7 = t33;
                    gm_l8 = ((uint8_t)0);
                    void * t34 = gm_l5;
                    void * t35 = (void *)(((uint8_t *)t34) + 32);
                    gm_t5hib78lmj2mf t36 = gm_f_shape_2jg6iuk(t35);
                    gm_l19 = t36;
                    uint8_t t37 = (uint8_t)((((uint8_t *)&gm_l19))[2] != 0);
                    if (t37) {
                        gm_t5sj3u8e14oco t38 = GM_LD(gm_t5sj3u8e14oco, ((uint8_t *)&gm_l19));
                        gm_l9 = t38;
                    } else {
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    void * t39 = (void *)((uint8_t *)&F.l4);
                    gm_t5hib78lmj2mf t40 = gm_f_shape_2jg6iuk(t39);
                    gm_l20 = t40;
                    uint8_t t41 = (uint8_t)((((uint8_t *)&gm_l20))[2] != 0);
                    if (t41) {
                        gm_t5sj3u8e14oco t42 = GM_LD(gm_t5sj3u8e14oco, ((uint8_t *)&gm_l20));
                        gm_l10 = t42;
                    } else {
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    {
                        {
                            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
                            uint8_t t44 = (uint8_t)(t43 == ((uint8_t)0ULL));
                            if (!t44) {
                                goto gm_lb6;
                            }
                            uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 1));
                            gm_l11 = t45;
                            {
                                {
                                    uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                                    uint8_t t47 = (uint8_t)(t46 == ((uint8_t)0ULL));
                                    if (!t47) {
                                        goto gm_lb8;
                                    }
                                    uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 1));
                                    gm_l12 = t48;
                                    uint8_t t49 = gm_l11;
                                    uint8_t t50 = gm_l12;
                                    uint8_t t51 = ((t49 == t50));
                                    if (t51) {
                                        F.ret = GM_ZERO(gm_b0_1);
                                        goto gm_fret;
                                    }
                                    uint8_t t52 = gm_l11;
                                    uint8_t t53 = (uint8_t)!t52;
                                    gm_l8 = t53;
                                    goto gm_lb7;
                                }
gm_lb8:;
                                F.ret = GM_ZERO(gm_b0_1);
                                goto gm_fret;
                            }
gm_lb7:;
                            goto gm_lb5;
                        }
gm_lb6:;
                        {
                            uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
                            uint8_t t55 = (uint8_t)(t54 == ((uint8_t)1ULL));
                            if (!t55) {
                                goto gm_lb9;
                            }
                            uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 1));
                            gm_l13 = t56;
                            {
                                {
                                    uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                                    uint8_t t58 = (uint8_t)(t57 == ((uint8_t)1ULL));
                                    if (!t58) {
                                        goto gm_lb11;
                                    }
                                    uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 1));
                                    gm_l14 = t59;
                                    uint8_t t60 = gm_l13;
                                    uint8_t t61 = gm_l14;
                                    uint8_t t62 = ((t60 == t61));
                                    if (t62) {
                                        F.ret = GM_ZERO(gm_b0_1);
                                        goto gm_fret;
                                    }
                                    gm_b16_8 t63;
                                    GM_ST(void *, (uint8_t *)&t63, (void *)(gm_smbbaf3n1gml2));
                                    GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(7));
                                    gm_l7 = t63;
                                    uint8_t t64 = gm_l13;
                                    uint8_t t65 = (uint8_t)!t64;
                                    gm_l8 = t65;
                                    goto gm_lb10;
                                }
gm_lb11:;
                                F.ret = GM_ZERO(gm_b0_1);
                                goto gm_fret;
                            }
gm_lb10:;
                            goto gm_lb5;
                        }
gm_lb9:;
                        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 645, 5);
                    }
gm_lb5:;
                    void * t66 = gm_l0;
                    void * t67 = gm_l5;
                    gm_tl874t2atnlh3 t68 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t67) + 32) + 24));
                    uint8_t t69 = gm_f_has_comment_qa4dr2o(t66, GM_CONV(gm_cseahvg8v7p0vs, t68));
                    if (t69) {
                        gm_l21 = ((uint8_t)1);
                    } else {
                        void * t70 = gm_l0;
                        gm_tl874t2atnlh3 t71 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l4) + 24));
                        uint8_t t72 = gm_f_has_comment_qa4dr2o(t70, GM_CONV(gm_cseahvg8v7p0vs, t71));
                        gm_l21 = t72;
                    }
                    uint8_t t73 = gm_l21;
                    if (t73) {
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    void * t74 = gm_l0;
                    void * t75 = gm_l6;
                    gm_tl874t2atnlh3 t76 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t75) + 72));
                    gm_b16_8 t77 = gm_f_text_kspioh6(t74, GM_CONV(gm_cseahvg8v7p0vs, t76));
                    gm_l15 = t77;
                    uint8_t t78 = gm_l8;
                    if (t78) {
                        void * t79 = gm_l0;
                        void * t80 = gm_l6;
                        gm_b16_8 t81 = gm_f_negation_tmaga6d((void *)NULL, cx, NULL, t79, t80);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l16 = t81;
                    } else {
                        gm_b16_8 t82 = gm_l15;
                        gm_l16 = t82;
                    }
                    uint8_t t83 = gm_l8;
                    if (t83) {
                        gm_b16_8 t84;
                        GM_ST(void *, (uint8_t *)&t84, (void *)(gm_sd1d50j1ea4vf));
                        GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(45));
                        gm_l17 = t84;
                    } else {
                        gm_b16_8 t85;
                        GM_ST(void *, (uint8_t *)&t85, (void *)(gm_sbgcsq0m3astj));
                        GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(42));
                        gm_l17 = t85;
                    }
                    {
                        void * t86 = gm_l1;
                        gm_tl874t2atnlh3 t87 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t86) + 72));
                        gm_b16_8 t88;
                        GM_ST(void *, (uint8_t *)&t88, (void *)(gm_sh4r0fq5alj61));
                        GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(13));
                        gm_b16_8 t89 = gm_l17;
                        gm_t4gut2c9fj1kt t90;
                        gm_f_lint_diag_4vra4qu((void *)&t90, (void *)NULL, cx, NULL, t87, t88, t89);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l18 = t90;
                    }
                    {
                        void * t91 = (void *)((uint8_t *)&F.l18);
                        gm_b16_8 t92 = gm_l7;
                        gm_b16_8 t93 = gm_l16;
                        gm_t6j6smj7qoctu t94;
                        GM_ST(gm_b16_8, ((uint8_t *)&t94 + 0), t92);
                        GM_ST(gm_b16_8, ((uint8_t *)&t94 + 16), t93);
                        gm_f_format_4pq08p9((void *)&gm_l22, (void *)NULL, cx, NULL, &t94);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t95 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                        if (!t95) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 8, gm_src10tru552hn49, 663, 15);
                        gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
                        gm_b16_8 t97 = GM_LD(gm_b16_8, ((uint8_t *)&t96));
                        void * t98 = gm_l1;
                        gm_tl874t2atnlh3 t99 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t98) + 72));
                        gm_b16_8 t100 = gm_l7;
                        gm_b16_8 t101 = gm_l16;
                        gm_t6j6smj7qoctu t102;
                        GM_ST(gm_b16_8, ((uint8_t *)&t102 + 0), t100);
                        GM_ST(gm_b16_8, ((uint8_t *)&t102 + 16), t101);
                        gm_f_format_8n626pi((void *)&gm_l23, (void *)NULL, cx, NULL, &t102);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t103 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                        if (!t103) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 8, gm_src10tru552hn49, 663, 83);
                        gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 8));
                        gm_b16_8 t105 = GM_LD(gm_b16_8, ((uint8_t *)&t104));
                        gm_b0_1 t106 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t91, t97, t99, t105);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t107 = gm_l0;
                        gm_b0_1 t108 = gm_f_push_l0e5v4c((void *)NULL, cx, NULL, t107, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l18)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_negation_tmaga6d(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_negation_tmaga6d F;
    void * gm_l0; /* w */
    void * gm_l1; /* c */
    gm_ts6oqppjh0ba3 gm_l2; /* op */
    void * gm_l3; /* lhs */
    void * gm_l4; /* rhs */
    gm_tt7vn9ehj0acr gm_l5; /* flipped */
    gm_ts6oqppjh0ba3 gm_l6; /* f */
    GM_STACK_CHECK(&F, 485u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l7;
    gm_tabb710d9mq2e gm_l8;
    {
        {
            void * t12 = gm_l1;
            uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)13ULL));
            if (!t14) {
                goto gm_lb2;
            }
            gm_ts6oqppjh0ba3 t15 = GM_LD(gm_ts6oqppjh0ba3, ((((uint8_t *)t12) + 8) + 8));
            gm_l2 = t15;
            void * t16 = GM_LD(void *, (((((uint8_t *)t12) + 8) + 8) + 8));
            gm_l3 = t16;
            void * t17 = GM_LD(void *, (((((uint8_t *)t12) + 8) + 8) + 16));
            gm_l4 = t17;
            {
                {
                    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t19 = (uint8_t)(t18 == ((uint8_t)12ULL));
                    if (!t19) {
                        goto gm_lb4;
                    }
                    gm_ts6oqppjh0ba3 t20;
                    GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)13ULL));
                    gm_tt7vn9ehj0acr t21;
                    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t21), t20);
                    ((uint8_t *)&t21)[2] = 1;
                    gm_l5 = t21;
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t23 = (uint8_t)(t22 == ((uint8_t)13ULL));
                    if (!t23) {
                        goto gm_lb5;
                    }
                    gm_ts6oqppjh0ba3 t24;
                    GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)12ULL));
                    gm_tt7vn9ehj0acr t25;
                    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t25), t24);
                    ((uint8_t *)&t25)[2] = 1;
                    gm_l5 = t25;
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t27 = (uint8_t)(t26 == ((uint8_t)14ULL));
                    if (!t27) {
                        goto gm_lb6;
                    }
                    gm_ts6oqppjh0ba3 t28;
                    GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)15ULL));
                    gm_tt7vn9ehj0acr t29;
                    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t29), t28);
                    ((uint8_t *)&t29)[2] = 1;
                    gm_l5 = t29;
                    goto gm_lb3;
                }
gm_lb6:;
                {
                    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t31 = (uint8_t)(t30 == ((uint8_t)15ULL));
                    if (!t31) {
                        goto gm_lb7;
                    }
                    gm_ts6oqppjh0ba3 t32;
                    GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)14ULL));
                    gm_tt7vn9ehj0acr t33;
                    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t33), t32);
                    ((uint8_t *)&t33)[2] = 1;
                    gm_l5 = t33;
                    goto gm_lb3;
                }
gm_lb7:;
                {
                    gm_tt7vn9ehj0acr t34;
                    memset(&t34, 0, sizeof t34);
                    gm_l5 = t34;
                    goto gm_lb3;
                }
gm_lb8:;
                gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 670, 31);
            }
gm_lb3:;
            {
                {
                    uint8_t t35 = (uint8_t)((((uint8_t *)&gm_l5))[2] != 0);
                    if (!t35) {
                        goto gm_lb10;
                    }
                    gm_ts6oqppjh0ba3 t36 = GM_LD(gm_ts6oqppjh0ba3, ((uint8_t *)&gm_l5));
                    gm_l6 = t36;
                    {
                        void * t37 = gm_l0;
                        void * t38 = gm_l3;
                        gm_tl874t2atnlh3 t39 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t38) + 72));
                        gm_b16_8 t40 = gm_f_text_kspioh6(t37, GM_CONV(gm_cseahvg8v7p0vs, t39));
                        gm_ts6oqppjh0ba3 t41 = gm_l6;
                        gm_b16_8 t42 = gm_f_binop_text_0ljfc0r(t41);
                        void * t43 = gm_l0;
                        void * t44 = gm_l4;
                        gm_tl874t2atnlh3 t45 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t44) + 72));
                        gm_b16_8 t46 = gm_f_text_kspioh6(t43, GM_CONV(gm_cseahvg8v7p0vs, t45));
                        gm_tfeab279c16rr t47;
                        GM_ST(gm_b16_8, ((uint8_t *)&t47 + 0), t40);
                        GM_ST(gm_b16_8, ((uint8_t *)&t47 + 16), t42);
                        GM_ST(gm_b16_8, ((uint8_t *)&t47 + 32), t46);
                        gm_f_format_o2cbrdu((void *)&gm_l7, (void *)NULL, cx, NULL, &t47);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t48 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                    if (!t48) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_src10tru552hn49, 677, 34);
                    gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
                    gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
                    F.ret = t50;
                    goto gm_fret;
                }
gm_lb10:;
            }
gm_lb9:;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t51 = gm_l1;
    void * t52 = gm_l0;
    void * t53 = gm_l1;
    gm_tl874t2atnlh3 t54 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t53) + 72));
    gm_b16_8 t55 = gm_f_text_kspioh6(t52, GM_CONV(gm_cseahvg8v7p0vs, t54));
    gm_b16_8 t56 = gm_f_paren_unless_simple_mhajkgl((void *)NULL, cx, NULL, t51, t55, ((uint8_t)1));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tnpifv20irnph t57;
    GM_ST(gm_b16_8, ((uint8_t *)&t57 + 0), t56);
    gm_f_format_udg5t2e((void *)&gm_l8, (void *)NULL, cx, NULL, t57);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t58 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (!t58) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_src10tru552hn49, 679, 12);
    gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
    gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&t59));
    F.ret = t60;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_normalize_space_v7dp7uh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_paren_at_av727r5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, uint8_t a3) {
    struct gm_fr_paren_at_av727r5 F;
    void * gm_l0; /* w */
    gm_b16_8 gm_l1; /* what */
    void * gm_l2; /* e */
    uint8_t gm_l3; /* head */
    void * gm_l4; /* inner */
    gm_b16_8 gm_l5; /* t */
    uint32_t gm_l6; /* file */
    GM_STACK_CHECK(&F, 807u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l9;
    uint8_t gm_l10;
    gm_tabb710d9mq2e gm_l11;
    {
        {
            void * t4 = gm_l2;
            uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)9ULL));
            if (!t6) {
                goto gm_lb2;
            }
            void * t7 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
            gm_l4 = t7;
            void * t8 = gm_l0;
            void * t9 = gm_l2;
            gm_tl874t2atnlh3 t10 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t9) + 72));
            gm_b16_8 t11 = gm_f_text_kspioh6(t8, GM_CONV(gm_cseahvg8v7p0vs, t10));
            gm_l5 = t11;
            gm_b16_8 t12 = gm_l5;
            uint8_t t13 = 0;
            uint8_t t14 = ((uint8_t)10ULL);
            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t12) + 8)); i++) if (gm_eqjmccqdb90f(((uint8_t *)GM_LD(void *, ((uint8_t *)&t12))) + i * 1, ((uint8_t *)&t14))) { t13 = 1; break; }
            uint8_t t15 = (uint8_t)(t13);
            if (t15) {
                gm_l9 = ((uint8_t)1);
            } else {
                uint8_t t16 = gm_l3;
                if (t16) {
                    gm_b16_8 t17 = gm_l5;
                    uint8_t t18 = 0;
                    uint8_t t19 = ((uint8_t)123ULL);
                    for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t17) + 8)); i++) if (gm_eqjmccqdb90f(((uint8_t *)GM_LD(void *, ((uint8_t *)&t17))) + i * 1, ((uint8_t *)&t19))) { t18 = 1; break; }
                    uint8_t t20 = (uint8_t)(t18);
                    gm_l10 = t20;
                } else {
                    gm_l10 = ((uint8_t)0);
                }
                uint8_t t21 = gm_l10;
                gm_l9 = t21;
            }
            uint8_t t22 = gm_l9;
            if (t22) {
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            void * t23 = gm_l2;
            uint32_t t24 = GM_LD(uint32_t, (((uint8_t *)t23) + 72));
            gm_l6 = t24;
            {
                void * t25 = gm_l2;
                gm_tl874t2atnlh3 t26 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t25) + 72));
                gm_b16_8 t27;
                GM_ST(void *, (uint8_t *)&t27, (void *)(gm_so9ldq1i41cj2));
                GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(18));
                gm_b16_8 t28 = gm_l1;
                gm_tnpifv20irnph t29;
                GM_ST(gm_b16_8, ((uint8_t *)&t29 + 0), t28);
                gm_f_format_reauhlh((void *)&gm_l11, (void *)NULL, cx, NULL, t29);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_src10tru552hn49, 762, 50);
                gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
                gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
                gm_t4gut2c9fj1kt t33;
                gm_f_lint_diag_4vra4qu((void *)&t33, (void *)NULL, cx, NULL, t26, t27, t32);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l7 = t33;
            }
            {
                uint32_t t34 = gm_l6;
                void * t35 = gm_l2;
                uint32_t t36 = GM_LD(uint32_t, ((((uint8_t *)t35) + 72) + 4));
                void * t37 = gm_l4;
                uint32_t t38 = GM_LD(uint32_t, ((((uint8_t *)t37) + 72) + 4));
                gm_tl874t2atnlh3 t39;
                GM_ST(uint32_t, ((uint8_t *)&t39 + 0), t34);
                GM_ST(uint32_t, ((uint8_t *)&t39 + 4), t36);
                GM_ST(uint32_t, ((uint8_t *)&t39 + 8), t38);
                gm_b16_8 t40;
                GM_ST(void *, (uint8_t *)&t40, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(0));
                gm_tbgd14esf0jvi t41;
                GM_ST(uint32_t, (uint8_t *)&t41 + 12, 0);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t41 + 0), t39);
                GM_ST(gm_b16_8, ((uint8_t *)&t41 + 16), t40);
                uint32_t t42 = gm_l6;
                void * t43 = gm_l4;
                uint32_t t44 = GM_LD(uint32_t, ((((uint8_t *)t43) + 72) + 8));
                void * t45 = gm_l2;
                uint32_t t46 = GM_LD(uint32_t, ((((uint8_t *)t45) + 72) + 8));
                gm_tl874t2atnlh3 t47;
                GM_ST(uint32_t, ((uint8_t *)&t47 + 0), t42);
                GM_ST(uint32_t, ((uint8_t *)&t47 + 4), t44);
                GM_ST(uint32_t, ((uint8_t *)&t47 + 8), t46);
                gm_b16_8 t48;
                GM_ST(void *, (uint8_t *)&t48, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(0));
                gm_tbgd14esf0jvi t49;
                GM_ST(uint32_t, (uint8_t *)&t49 + 12, 0);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t49 + 0), t47);
                GM_ST(gm_b16_8, ((uint8_t *)&t49 + 16), t48);
                gm_t0vh9i8m50f41 t50;
                GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t50 + 0), t41);
                GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t50 + 32), t49);
                F.l8 = t50;
            }
            void * t51 = (void *)((uint8_t *)&F.l7);
            gm_b16_8 t52;
            GM_ST(void *, (uint8_t *)&t52, (void *)(gm_sqit0ai5v3joc));
            GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(22));
            void * t53 = (void *)((uint8_t *)&F.l8);
            uint64_t t54 = ((uintptr_t)((uint64_t)2ULL)) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t55;
            GM_ST(void *, (uint8_t *)&t55, (void *)(((uint8_t *)t53 + (uintptr_t)((uint64_t)0ULL) * 32)));
            GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(t54));
            gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&t55));
            gm_b0_1 t57 = gm_f_add_fix_edits_em0b48a((void *)NULL, cx, NULL, t51, t52, t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                void * t58 = gm_l0;
                gm_b0_1 t59 = gm_f_push_l0e5v4c((void *)NULL, cx, NULL, t58, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l7)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto gm_lb1;
        }
gm_lb2:;
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_paren_unless_simple_mhajkgl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2) {
    struct gm_fr_paren_unless_simple_mhajkgl F;
    void * gm_l0; /* e */
    gm_b16_8 gm_l1; /* t */
    uint8_t gm_l2; /* comparisons */
    uint8_t gm_l3; /* wrap */
    gm_ts6oqppjh0ba3 gm_l4; /* op */
    GM_STACK_CHECK(&F, 261u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 gm_l5;
    gm_tabb710d9mq2e gm_l6;
    gm_l3 = ((uint8_t)0);
    void * t6 = gm_l0;
    {
        {
            uint8_t t7 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)13ULL));
            if (!t8) {
                goto gm_lb2;
            }
            gm_ts6oqppjh0ba3 t9 = GM_LD(gm_ts6oqppjh0ba3, ((((uint8_t *)t6) + 8) + 8));
            gm_l4 = t9;
            gm_ts6oqppjh0ba3 t10 = gm_l4;
            gm_ts6oqppjh0ba3 t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)20ULL));
            gm_ts6oqppjh0ba3 t12;
            GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)21ULL));
            gm_t9ldr1q5hn9fq t13;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t13 + 0), t11);
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t13 + 2), t12);
            uint8_t t14 = 0;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t13) + 0, ((uint8_t *)&t10))) t14 = 1;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t13) + 2, ((uint8_t *)&t10))) t14 = 1;
            uint8_t t15 = (uint8_t)(t14);
            if (t15) {
                gm_l3 = ((uint8_t)1);
            } else {
                uint8_t t16 = gm_l2;
                if (t16) {
                    gm_ts6oqppjh0ba3 t17 = gm_l4;
                    gm_ts6oqppjh0ba3 t18;
                    GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)12ULL));
                    gm_ts6oqppjh0ba3 t19;
                    GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)13ULL));
                    gm_ts6oqppjh0ba3 t20;
                    GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)14ULL));
                    gm_ts6oqppjh0ba3 t21;
                    GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)15ULL));
                    gm_ts6oqppjh0ba3 t22;
                    GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)16ULL));
                    gm_ts6oqppjh0ba3 t23;
                    GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)17ULL));
                    gm_ts6oqppjh0ba3 t24;
                    GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)18ULL));
                    gm_ts6oqppjh0ba3 t25;
                    GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)19ULL));
                    gm_tv78bma7gi894 t26;
                    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t26 + 0), t18);
                    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t26 + 2), t19);
                    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t26 + 4), t20);
                    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t26 + 6), t21);
                    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t26 + 8), t22);
                    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t26 + 10), t23);
                    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t26 + 12), t24);
                    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t26 + 14), t25);
                    uint8_t t27 = 0;
                    if (gm_eqbqb4gb1d92(((uint8_t *)&t26) + 0, ((uint8_t *)&t17))) t27 = 1;
                    if (gm_eqbqb4gb1d92(((uint8_t *)&t26) + 2, ((uint8_t *)&t17))) t27 = 1;
                    if (gm_eqbqb4gb1d92(((uint8_t *)&t26) + 4, ((uint8_t *)&t17))) t27 = 1;
                    if (gm_eqbqb4gb1d92(((uint8_t *)&t26) + 6, ((uint8_t *)&t17))) t27 = 1;
                    if (gm_eqbqb4gb1d92(((uint8_t *)&t26) + 8, ((uint8_t *)&t17))) t27 = 1;
                    if (gm_eqbqb4gb1d92(((uint8_t *)&t26) + 10, ((uint8_t *)&t17))) t27 = 1;
                    if (gm_eqbqb4gb1d92(((uint8_t *)&t26) + 12, ((uint8_t *)&t17))) t27 = 1;
                    if (gm_eqbqb4gb1d92(((uint8_t *)&t26) + 14, ((uint8_t *)&t17))) t27 = 1;
                    uint8_t t28 = (uint8_t)(t27);
                    gm_l3 = t28;
                }
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
            uint8_t t30 = (uint8_t)(t29 == ((uint8_t)14ULL));
            if (!t30) {
                goto gm_lb3;
            }
            gm_l3 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb3:;
        {
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 875, 5);
    }
gm_lb1:;
    uint8_t t31 = gm_l3;
    if (t31) {
        gm_b16_8 t32 = gm_l1;
        gm_tnpifv20irnph t33;
        GM_ST(gm_b16_8, ((uint8_t *)&t33 + 0), t32);
        gm_f_format_1vkmnih((void *)&gm_l6, (void *)NULL, cx, NULL, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t34 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
        if (!t34) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_src10tru552hn49, 886, 22);
        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
        gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
        gm_l5 = t36;
    } else {
        gm_b16_8 t37 = gm_l1;
        gm_l5 = t37;
    }
    gm_b16_8 t38 = gm_l5;
    F.ret = t38;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_pattern_8qtr7os(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2) {
    struct gm_fr_pattern_8qtr7os F;
    void * gm_l0; /* w */
    void * gm_l1; /* p */
    gm_t9qkh0rsooc8d gm_l2; /* ctx */
    gm_t08olq97rscn3 gm_l3; /* id */
    gm_t08olq97rscn3 gm_l5; /* id */
    void * gm_l7; /* inner */
    gm_tagc77imt2chm gm_l8; /* pp */
    uint8_t gm_l9; /* structish */
    gm_b16_8 gm_l10; /* args */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* i */
    gm_t9qkh0rsooc8d gm_l14; /* ctx2 */
    gm_b16_8 gm_l15; /* args */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* i */
    void * gm_l19; /* e */
    void * gm_l20; /* lo */
    void * gm_l21; /* hi */
    GM_STACK_CHECK(&F, 1313u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l22;
    gm_tftb1odc739t5 gm_l23;
    uint8_t gm_l24;
    void * t36 = gm_l1;
    {
        {
            uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
            uint8_t t38 = (uint8_t)(t37 == ((uint8_t)1ULL));
            if (!t38) {
                goto gm_lb2;
            }
            {
                memmove(((uint8_t *)&gm_l3), ((((uint8_t *)t36) + 8) + 8), 32);
            }
            {
                void * t39 = gm_l0;
                gm_b16_8 t40;
                GM_ST(void *, (uint8_t *)&t40, (void *)(gm_sjg1ri23hm753));
                GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(5));
                gm_t9qkh0rsooc8d t41;
                GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)1ULL));
                gm_b0_1 t42 = gm_f_check_name_hf7dide((void *)NULL, cx, NULL, t39, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l3)), t41, t40);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t44 = (uint8_t)(t43 == ((uint8_t)0ULL));
                    if (!t44) {
                        goto gm_lb4;
                    }
                    {
                        gm_tnvnsr1bfvtbd t45;
                        memset(&t45, 0, sizeof t45);
                        gm_t5jnq73fben73 t46;
                        GM_ST(gm_tnvnsr1bfvtbd, ((uint8_t *)&t46 + 0), t45);
                        GM_ST(uint8_t, (uint8_t *)&F.l4 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&F.l4 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&F.l4 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&F.l4 + 0), ((uint8_t)2ULL));
                        GM_ST(gm_t5jnq73fben73, ((uint8_t *)&F.l4 + 8), t46);
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t48 = (uint8_t)(t47 == ((uint8_t)1ULL));
                    if (!t48) {
                        goto gm_lb5;
                    }
                    {
                        gm_tl874t2atnlh3 t49 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l3) + 16));
                        gm_b16_8 t50;
                        GM_ST(void *, (uint8_t *)&t50, (void *)(gm_sfdbfrv46lihd));
                        GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(1));
                        gm_tbgd14esf0jvi t51;
                        GM_ST(uint32_t, (uint8_t *)&t51 + 12, 0);
                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t51 + 0), t49);
                        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 16), t50);
                        gm_tnvnsr1bfvtbd t52;
                        GM_ST(uint8_t, (uint8_t *)&t52 + 33, 0);
                        GM_ST(uint16_t, (uint8_t *)&t52 + 34, 0);
                        GM_ST(uint32_t, (uint8_t *)&t52 + 36, 0);
                        GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t52), t51);
                        ((uint8_t *)&t52)[32] = 1;
                        gm_t5jnq73fben73 t53;
                        GM_ST(gm_tnvnsr1bfvtbd, ((uint8_t *)&t53 + 0), t52);
                        GM_ST(uint8_t, (uint8_t *)&F.l4 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&F.l4 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&F.l4 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&F.l4 + 0), ((uint8_t)2ULL));
                        GM_ST(gm_t5jnq73fben73, ((uint8_t *)&F.l4 + 8), t53);
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t55 = (uint8_t)(t54 == ((uint8_t)2ULL));
                    if (!t55) {
                        goto gm_lb6;
                    }
                    {
                        gm_tl874t2atnlh3 t56 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l3) + 16));
                        gm_tl874t2atnlh3 t57 = GM_CONV(gm_tl874t2atnlh3, gm_f_shrink_to_hi_5bdve33(GM_CONV(gm_cseahvg8v7p0vs, t56)));
                        gm_b16_8 t58;
                        GM_ST(void *, (uint8_t *)&t58, (void *)(gm_s9ln4rnb6talr));
                        GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(3));
                        gm_tbgd14esf0jvi t59;
                        GM_ST(uint32_t, (uint8_t *)&t59 + 12, 0);
                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t59 + 0), t57);
                        GM_ST(gm_b16_8, ((uint8_t *)&t59 + 16), t58);
                        gm_tnvnsr1bfvtbd t60;
                        GM_ST(uint8_t, (uint8_t *)&t60 + 33, 0);
                        GM_ST(uint16_t, (uint8_t *)&t60 + 34, 0);
                        GM_ST(uint32_t, (uint8_t *)&t60 + 36, 0);
                        GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t60), t59);
                        ((uint8_t *)&t60)[32] = 1;
                        gm_t5jnq73fben73 t61;
                        GM_ST(gm_tnvnsr1bfvtbd, ((uint8_t *)&t61 + 0), t60);
                        GM_ST(uint8_t, (uint8_t *)&F.l4 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&F.l4 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&F.l4 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&F.l4 + 0), ((uint8_t)2ULL));
                        GM_ST(gm_t5jnq73fben73, ((uint8_t *)&F.l4 + 8), t61);
                    }
                    goto gm_lb3;
                }
gm_lb6:;
                gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 257, 26);
            }
gm_lb3:;
            {
                void * t62 = gm_l0;
                void * t63 = (void *)((((uint8_t *)t62) + 8) + 40);
                gm_tl874t2atnlh3 t64 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l3) + 16));
                gm_tftb1odc739t5 t65 = gm_f_put_amueool(t63, GM_CONV(gm_cseahvg8v7p0vs, t64), &(*(gm_tl5vm0tcqvdvb *)((uint8_t *)&F.l4)));
                gm_l22 = t65;
            }
            uint8_t t66 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
            if (!t66) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 1, gm_src10tru552hn49, 262, 13);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t67 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
            uint8_t t68 = (uint8_t)(t67 == ((uint8_t)2ULL));
            if (!t68) {
                goto gm_lb7;
            }
            {
                memmove(((uint8_t *)&gm_l5), ((((uint8_t *)t36) + 8) + 8), 32);
            }
            {
                void * t69 = gm_l0;
                gm_b16_8 t70;
                GM_ST(void *, (uint8_t *)&t70, (void *)(gm_sjg1ri23hm753));
                GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(5));
                gm_t9qkh0rsooc8d t71;
                GM_ST(uint8_t, ((uint8_t *)&t71 + 0), ((uint8_t)1ULL));
                gm_b0_1 t72 = gm_f_check_name_hf7dide((void *)NULL, cx, NULL, t69, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l5)), t71, t70);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                memset(&F.l6, 0, sizeof F.l6);
            }
            gm_t9qkh0rsooc8d t73 = gm_l2;
            uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&t73) + 0));
            uint8_t t75 = (uint8_t)((t74 == ((uint8_t)1ULL)));
            gm_t9qkh0rsooc8d t76;
            GM_ST(uint8_t, ((uint8_t *)&t76 + 0), ((uint8_t)1ULL));
            if (t75) {
                void * t77 = gm_l1;
                gm_tl874t2atnlh3 t78 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t77) + 48));
                gm_b16_8 t79;
                GM_ST(void *, (uint8_t *)&t79, (void *)(gm_sfdbfrv46lihd));
                GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(1));
                gm_tbgd14esf0jvi t80;
                GM_ST(uint32_t, (uint8_t *)&t80 + 12, 0);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t80 + 0), t78);
                GM_ST(gm_b16_8, ((uint8_t *)&t80 + 16), t79);
                GM_ST(uint8_t, (uint8_t *)&F.l6 + 33, 0);
                GM_ST(uint16_t, (uint8_t *)&F.l6 + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&F.l6 + 36, 0);
                GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&F.l6), t80);
                ((uint8_t *)&F.l6)[32] = 1;
            }
            {
                void * t81 = gm_l0;
                void * t82 = (void *)((((uint8_t *)t81) + 8) + 40);
                gm_tl874t2atnlh3 t83 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l5) + 16));
                gm_tnvnsr1bfvtbd t84 = GM_LD(gm_tnvnsr1bfvtbd, ((uint8_t *)&F.l6));
                gm_t5jnq73fben73 t85;
                GM_ST(gm_tnvnsr1bfvtbd, ((uint8_t *)&t85 + 0), t84);
                gm_tl5vm0tcqvdvb t86;
                GM_ST(uint8_t, (uint8_t *)&t86 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t86 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t86 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)2ULL));
                GM_ST(gm_t5jnq73fben73, ((uint8_t *)&t86 + 8), t85);
                gm_tftb1odc739t5 t87 = gm_f_put_amueool(t82, GM_CONV(gm_cseahvg8v7p0vs, t83), &t86);
                gm_l23 = t87;
            }
            uint8_t t88 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
            if (!t88) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_src10tru552hn49, 270, 13);
            goto gm_lb1;
        }
gm_lb7:;
        {
            uint8_t t89 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
            uint8_t t90 = (uint8_t)(t89 == ((uint8_t)6ULL));
            if (!t90) {
                goto gm_lb8;
            }
            void * t91 = GM_LD(void *, ((((uint8_t *)t36) + 8) + 8));
            gm_l7 = t91;
            void * t92 = gm_l0;
            void * t93 = gm_l7;
            gm_t9qkh0rsooc8d t94;
            GM_ST(uint8_t, ((uint8_t *)&t94 + 0), ((uint8_t)1ULL));
            gm_b0_1 t95 = gm_f_pattern_8qtr7os((void *)NULL, cx, NULL, t92, t93, t94);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb8:;
        {
            uint8_t t96 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
            uint8_t t97 = (uint8_t)(t96 == ((uint8_t)7ULL));
            if (!t97) {
                goto gm_lb9;
            }
            memmove(((uint8_t *)&gm_l8), ((((uint8_t *)t36) + 8) + 8), 32);
            void * t98 = GM_LD(void *, ((uint8_t *)&gm_l8));
            {
                {
                    uint8_t t99 = GM_LD(uint8_t, ((((uint8_t *)t98) + 8) + 0));
                    uint8_t t100 = (uint8_t)(t99 == ((uint8_t)6ULL));
                    if (!t100) {
                        goto gm_lb11;
                    }
                    gm_l9 = ((uint8_t)1);
                    goto gm_lb10;
                }
gm_lb11:;
                {
                    gm_l9 = ((uint8_t)0);
                    goto gm_lb10;
                }
gm_lb12:;
                gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 275, 26);
            }
gm_lb10:;
            uint8_t t101 = (uint8_t)(((((uint8_t *)&gm_l8) + 8))[16] != 0);
            if (t101) {
                gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
                gm_l10 = t102;
            } else {
                gm_b16_8 t103 = gm_f_empty_slice_r9g5q6n();
                gm_l10 = t103;
            }
            gm_l12 = ((uint64_t)0ULL);
            uint64_t t104 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
            gm_l11 = t104;
            {
                for (;;) {
                    uint64_t t105 = gm_l12;
                    uint64_t t106 = gm_l11;
                    uint8_t t107 = (t105 < t106);
                    if (!t107) {
                        goto gm_lb13;
                    }
                    {
                        {
                            {
                                uint64_t t108 = gm_l12;
                                gm_l13 = t108;
                                {
                                    {
                                        void * t109 = GM_LD(void *, ((uint8_t *)&gm_l10));
                                        uint64_t t110 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                        uint64_t t111 = gm_l13;
                                        {
                                            {
                                                uint8_t t112 = (uint8_t)(((((uint8_t *)t109) + (uintptr_t)t111 * 120))[32] != 0);
                                                if (t112) {
                                                    goto gm_lb21;
                                                }
                                                gm_l24 = ((uint8_t)1);
                                                goto gm_lb20;
                                            }
gm_lb21:;
                                            {
                                                gm_l24 = ((uint8_t)0);
                                                goto gm_lb20;
                                            }
gm_lb22:;
                                            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 278, 36);
                                        }
gm_lb20:;
                                        uint8_t t113 = gm_l24;
                                        if (!t113) {
                                            goto gm_lb19;
                                        }
                                        uint8_t t114 = gm_l9;
                                        if (!t114) {
                                            goto gm_lb19;
                                        }
                                        gm_t9qkh0rsooc8d t115;
                                        GM_ST(uint8_t, ((uint8_t *)&t115 + 0), ((uint8_t)2ULL));
                                        gm_l14 = t115;
                                        goto gm_lb18;
                                    }
gm_lb19:;
                                    gm_t9qkh0rsooc8d t116;
                                    GM_ST(uint8_t, ((uint8_t *)&t116 + 0), ((uint8_t)1ULL));
                                    gm_l14 = t116;
                                }
gm_lb18:;
                                void * t117 = gm_l0;
                                void * t118 = GM_LD(void *, ((uint8_t *)&gm_l10));
                                uint64_t t119 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                uint64_t t120 = gm_l13;
                                void * t121 = (void *)((((uint8_t *)t118) + (uintptr_t)t120 * 120) + 40);
                                gm_t9qkh0rsooc8d t122 = gm_l14;
                                gm_b0_1 t123 = gm_f_pattern_8qtr7os((void *)NULL, cx, NULL, t117, t121, t122);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb16;
                            }
gm_lb17:;
                            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 277, 13);
                        }
gm_lb16:;
                    }
gm_lb15:;
                    uint64_t t124 = gm_l12;
                    uint64_t r125; bool o126 = __builtin_add_overflow(t124, ((uint64_t)1ULL), &r125);
                    uint64_t t127 = r125;
                    gm_l12 = t127;
                }
            }
gm_lb13:;
            goto gm_lb1;
        }
gm_lb9:;
        {
            uint8_t t128 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
            uint8_t t129 = (uint8_t)(t128 == ((uint8_t)8ULL));
            if (!t129) {
                goto gm_lb23;
            }
            gm_b16_8 t130 = GM_LD(gm_b16_8, ((((uint8_t *)t36) + 8) + 8));
            gm_l15 = t130;
            gm_l17 = ((uint64_t)0ULL);
            uint64_t t131 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
            gm_l16 = t131;
            {
                for (;;) {
                    uint64_t t132 = gm_l17;
                    uint64_t t133 = gm_l16;
                    uint8_t t134 = (t132 < t133);
                    if (!t134) {
                        goto gm_lb24;
                    }
                    {
                        {
                            {
                                uint64_t t135 = gm_l17;
                                gm_l18 = t135;
                                void * t136 = gm_l0;
                                void * t137 = GM_LD(void *, ((uint8_t *)&gm_l15));
                                uint64_t t138 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                uint64_t t139 = gm_l18;
                                void * t140 = (void *)((((uint8_t *)t137) + (uintptr_t)t139 * 120) + 40);
                                gm_t9qkh0rsooc8d t141;
                                GM_ST(uint8_t, ((uint8_t *)&t141 + 0), ((uint8_t)1ULL));
                                gm_b0_1 t142 = gm_f_pattern_8qtr7os((void *)NULL, cx, NULL, t136, t140, t141);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb27;
                            }
gm_lb28:;
                            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 283, 13);
                        }
gm_lb27:;
                    }
gm_lb26:;
                    uint64_t t143 = gm_l17;
                    uint64_t r144; bool o145 = __builtin_add_overflow(t143, ((uint64_t)1ULL), &r144);
                    uint64_t t146 = r144;
                    gm_l17 = t146;
                }
            }
gm_lb24:;
            goto gm_lb1;
        }
gm_lb23:;
        {
            uint8_t t147 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
            uint8_t t148 = (uint8_t)(t147 == ((uint8_t)3ULL));
            if (!t148) {
                goto gm_lb29;
            }
            void * t149 = GM_LD(void *, ((((uint8_t *)t36) + 8) + 8));
            gm_l19 = t149;
            void * t150 = gm_l0;
            void * t151 = gm_l19;
            gm_b0_1 t152 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t150, t151);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb29:;
        {
            uint8_t t153 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
            uint8_t t154 = (uint8_t)(t153 == ((uint8_t)5ULL));
            if (!t154) {
                goto gm_lb30;
            }
            void * t155 = GM_LD(void *, ((((uint8_t *)t36) + 8) + 8));
            gm_l20 = t155;
            void * t156 = GM_LD(void *, (((((uint8_t *)t36) + 8) + 8) + 8));
            gm_l21 = t156;
            void * t157 = gm_l0;
            void * t158 = gm_l20;
            gm_b0_1 t159 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t157, t158);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t160 = gm_l0;
            void * t161 = gm_l21;
            gm_b0_1 t162 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t160, t161);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb30:;
        {
            {
                {
                    uint8_t t163 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                    uint8_t t164 = (uint8_t)(t163 == ((uint8_t)0ULL));
                    if (!t164) {
                        goto gm_lb33;
                    }
                    goto gm_lb32;
                }
gm_lb33:;
                {
                    uint8_t t165 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                    uint8_t t166 = (uint8_t)(t165 == ((uint8_t)4ULL));
                    if (!t166) {
                        goto gm_lb34;
                    }
                    goto gm_lb32;
                }
gm_lb34:;
                goto gm_lb31;
            }
gm_lb32:;
            goto gm_lb1;
        }
gm_lb31:;
        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 254, 5);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_pop_label_0jfjsf5(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_pop_label_0jfjsf5 F;
    void * gm_l0; /* w */
    gm_t08olq97rscn3 gm_l2; /* l */
    gm_t16on0i8enr1i gm_l3; /* entry */
    uint32_t gm_l4; /* file */
    gm_b16_8 gm_l5; /* rest */
    uint64_t gm_l6; /* colon */
    uint64_t gm_l7; /* after */
    uint32_t gm_l8; /* end */
    GM_STACK_CHECK(&F, 855u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_tgqc207d6ne28 gm_l10;
    gm_tf5srh4mp4b3i gm_l11;
    gm_tabb710d9mq2e gm_l12;
    uint8_t t5 = (uint8_t)((((uint8_t *)&F.l1))[32] != 0);
    if (t5) {
        memmove(((uint8_t *)&gm_l2), ((uint8_t *)&F.l1), 32);
    } else {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    {
        void * t6 = gm_l0;
        void * t7 = (void *)(((uint8_t *)t6) + 224);
        gm_f_pop_h4mkmpb((void *)&gm_l10, t7);
    }
    uint8_t t8 = (uint8_t)((((uint8_t *)&gm_l10))[32] != 0);
    if (t8) {
        memmove(((uint8_t *)&gm_l3), ((uint8_t *)&gm_l10), 32);
    } else {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 28));
    if (t9) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 16));
    gm_l4 = t10;
    void * t11 = gm_l0;
    void * t12 = GM_LD(void *, ((uint8_t *)t11));
    uint32_t t13 = gm_l4;
    void * t14 = gm_f_file_o15fqku(t12, t13);
    void * t15 = GM_LD(void *, (((uint8_t *)t14) + 16));
    uint64_t t16 = GM_LD(uint64_t, ((((uint8_t *)t14) + 16) + 8));
    uint32_t t17 = GM_LD(uint32_t, ((((uint8_t *)&gm_l2) + 16) + 8));
    uint64_t t18 = (uint64_t)t17;
    uint64_t t19 = (uintptr_t)t18;
    uint64_t t20 = (uintptr_t)t16;
    if (t19 > t20 || t20 > (uintptr_t)t16) gm_trap_slice(t19, t20, t16, gm_src10tru552hn49, 451, 13);
    uint64_t t21 = ((uintptr_t)t16) - (uintptr_t)t18;
    gm_b16_8 t22;
    GM_ST(void *, (uint8_t *)&t22, (void *)(((uint8_t *)t15 + (uintptr_t)t18 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(t21));
    gm_l5 = t22;
    gm_b16_8 t23 = gm_l5;
    gm_tf5srh4mp4b3i t24 = gm_f_index_of_hdeg351(t23, ((uint8_t)58ULL));
    gm_l11 = t24;
    uint8_t t25 = (uint8_t)((((uint8_t *)&gm_l11))[8] != 0);
    if (t25) {
        uint64_t t26 = GM_LD(uint64_t, ((uint8_t *)&gm_l11));
        gm_l6 = t26;
    } else {
        gm_l6 = ((uint64_t)0ULL);
    }
    gm_l7 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t27 = gm_l6;
            uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
            if (o29) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 454, 11);
            uint64_t t30 = r28;
            uint64_t t31 = gm_l7;
            uint64_t r32; bool o33 = __builtin_add_overflow(t30, t31, &r32);
            if (o33) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 454, 11);
            uint64_t t34 = r32;
            uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t36 = (t34 < t35);
            if (!t36) {
                goto gm_lb1;
            }
            void * t37 = GM_LD(void *, ((uint8_t *)&gm_l5));
            uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint64_t t39 = gm_l6;
            uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
            if (o41) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 454, 49);
            uint64_t t42 = r40;
            uint64_t t43 = gm_l7;
            uint64_t r44; bool o45 = __builtin_add_overflow(t42, t43, &r44);
            if (o45) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 454, 49);
            uint64_t t46 = r44;
            if ((uintptr_t)t46 >= (uintptr_t)(t38)) gm_trap_index((uintptr_t)t46, t38, gm_src10tru552hn49, 454, 44);
            uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)t37) + (uintptr_t)t46 * 1));
            gm_tqs5p4n2tq19v t48;
            GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)32ULL));
            GM_ST(uint8_t, ((uint8_t *)&t48 + 1), ((uint8_t)9ULL));
            uint8_t t49 = 0;
            if (gm_eqjmccqdb90f(((uint8_t *)&t48) + 0, ((uint8_t *)&t47))) t49 = 1;
            if (gm_eqjmccqdb90f(((uint8_t *)&t48) + 1, ((uint8_t *)&t47))) t49 = 1;
            uint8_t t50 = (uint8_t)(t49);
            if (!t50) {
                goto gm_lb1;
            }
            {
                uint64_t t51 = gm_l7;
                uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
                if (o53) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 454, 85);
                uint64_t t54 = r52;
                gm_l7 = t54;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    uint32_t t55 = GM_LD(uint32_t, ((((uint8_t *)&gm_l2) + 16) + 8));
    uint64_t t56 = gm_l6;
    uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
    if (o58) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 455, 25);
    uint64_t t59 = r57;
    uint64_t t60 = gm_l7;
    uint64_t r61; bool o62 = __builtin_add_overflow(t59, t60, &r61);
    if (o62) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 455, 25);
    uint64_t t63 = r61;
    uint32_t t64 = (uint32_t)t63;
    uint32_t r65; bool o66 = __builtin_add_overflow(t55, t64, &r65);
    if (o66) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 455, 12);
    uint32_t t67 = r65;
    gm_l8 = t67;
    {
        gm_tl874t2atnlh3 t68 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l2) + 16));
        gm_b16_8 t69;
        GM_ST(void *, (uint8_t *)&t69, (void *)(gm_s6vg88ce880ha));
        GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(12));
        gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
        gm_tnpifv20irnph t71;
        GM_ST(gm_b16_8, ((uint8_t *)&t71 + 0), t70);
        gm_f_format_jm2cnr1((void *)&gm_l12, (void *)NULL, cx, NULL, t71);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t72 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
        if (!t72) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_src10tru552hn49, 456, 44);
        gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
        gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
        gm_t4gut2c9fj1kt t75;
        gm_f_lint_diag_4vra4qu((void *)&t75, (void *)NULL, cx, NULL, t68, t69, t74);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l9 = t75;
    }
    void * t76 = (void *)((uint8_t *)&F.l9);
    gm_b16_8 t77;
    GM_ST(void *, (uint8_t *)&t77, (void *)(gm_s71j6n6s8up14));
    GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(16));
    uint32_t t78 = gm_l4;
    uint32_t t79 = GM_LD(uint32_t, ((((uint8_t *)&gm_l2) + 16) + 4));
    uint32_t t80 = gm_l8;
    gm_tl874t2atnlh3 t81;
    GM_ST(uint32_t, ((uint8_t *)&t81 + 0), t78);
    GM_ST(uint32_t, ((uint8_t *)&t81 + 4), t79);
    GM_ST(uint32_t, ((uint8_t *)&t81 + 8), t80);
    gm_b16_8 t82;
    GM_ST(void *, (uint8_t *)&t82, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(0));
    gm_b0_1 t83 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t76, t77, t81, t82);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        void * t84 = gm_l0;
        gm_b0_1 t85 = gm_f_push_l0e5v4c((void *)NULL, cx, NULL, t84, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l9)));
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_push_l0e5v4c(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_push_label_hn6s4vp(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_t5hib78lmj2mf gm_f_shape_2jg6iuk(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_shape_2jg6iuk F;
    void * gm_l0; /* b */
    void * gm_l1; /* x */
    uint8_t gm_l2; /* v */
    void * gm_l3; /* r */
    void * gm_l4; /* v */
    uint8_t gm_l5; /* b2 */
    GM_STACK_CHECK_SHALLOW(&F, 159u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t10 = gm_l0;
    uint64_t t11 = GM_LD(uint64_t, ((((uint8_t *)t10) + 8) + 8));
    uint8_t t12 = (!(t11 == ((uint64_t)1ULL)));
    if (t12) {
        gm_t5hib78lmj2mf t13;
        memset(&t13, 0, sizeof t13);
        F.ret = t13;
        goto gm_fret;
    }
    {
        {
            void * t14 = gm_l0;
            void * t15 = GM_LD(void *, (((uint8_t *)t14) + 8));
            uint64_t t16 = GM_LD(uint64_t, ((((uint8_t *)t14) + 8) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t16)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t16, gm_src10tru552hn49, 621, 17);
            uint8_t t17 = GM_LD(uint8_t, (((((uint8_t *)t15) + (uintptr_t)((uint64_t)0ULL) * 144) + 8) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)2ULL));
            if (!t18) {
                goto gm_lb2;
            }
            void * t19 = GM_LD(void *, (((((uint8_t *)t15) + (uintptr_t)((uint64_t)0ULL) * 144) + 8) + 8));
            gm_l1 = t19;
            void * t20 = gm_l1;
            {
                {
                    uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t20) + 8) + 0));
                    uint8_t t22 = (uint8_t)(t21 == ((uint8_t)3ULL));
                    if (!t22) {
                        goto gm_lb4;
                    }
                    uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t20) + 8) + 8));
                    gm_l2 = t23;
                    uint8_t t24 = gm_l2;
                    gm_t5sj3u8e14oco t25;
                    GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t25 + 1), t24);
                    gm_t5hib78lmj2mf t26;
                    GM_ST(gm_t5sj3u8e14oco, ((uint8_t *)&t26), t25);
                    ((uint8_t *)&t26)[2] = 1;
                    F.ret = t26;
                    goto gm_fret;
                }
gm_lb4:;
                {
                    uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t20) + 8) + 0));
                    uint8_t t28 = (uint8_t)(t27 == ((uint8_t)45ULL));
                    if (!t28) {
                        goto gm_lb5;
                    }
                    void * t29 = GM_LD(void *, ((((uint8_t *)t20) + 8) + 8));
                    gm_l3 = t29;
                    uint8_t t30 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l3)) != 0);
                    if (t30) {
                        void * t31 = GM_LD(void *, ((uint8_t *)&gm_l3));
                        gm_l4 = t31;
                    } else {
                        gm_t5hib78lmj2mf t32;
                        memset(&t32, 0, sizeof t32);
                        F.ret = t32;
                        goto gm_fret;
                    }
                    {
                        {
                            void * t33 = gm_l4;
                            uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)t33) + 8) + 0));
                            uint8_t t35 = (uint8_t)(t34 == ((uint8_t)3ULL));
                            if (!t35) {
                                goto gm_lb7;
                            }
                            uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)t33) + 8) + 8));
                            gm_l5 = t36;
                            uint8_t t37 = gm_l5;
                            gm_t5sj3u8e14oco t38;
                            GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)1ULL));
                            GM_ST(uint8_t, ((uint8_t *)&t38 + 1), t37);
                            gm_t5hib78lmj2mf t39;
                            GM_ST(gm_t5sj3u8e14oco, ((uint8_t *)&t39), t38);
                            ((uint8_t *)&t39)[2] = 1;
                            F.ret = t39;
                            goto gm_fret;
                        }
gm_lb7:;
                        gm_t5hib78lmj2mf t40;
                        memset(&t40, 0, sizeof t40);
                        F.ret = t40;
                        goto gm_fret;
                    }
gm_lb6:;
                }
gm_lb5:;
                {
                    gm_t5hib78lmj2mf t41;
                    memset(&t41, 0, sizeof t41);
                    F.ret = t41;
                    goto gm_fret;
                }
gm_lb8:;
                gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 622, 5);
            }
gm_lb3:;
        }
gm_lb2:;
        gm_t5hib78lmj2mf t42;
        memset(&t42, 0, sizeof t42);
        F.ret = t42;
        goto gm_fret;
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_shrink_to_hi_5bdve33(gm_cseahvg8v7p0vs a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_sig_fn4opan(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2) {
    struct gm_fr_sig_fn4opan F;
    void * gm_l0; /* w */
    void * gm_l1; /* sg */
    uint8_t gm_l2; /* literal */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint8_t gm_l6; /* ty_param */
    void * gm_l7; /* t */
    gm_t08olq97rscn3 gm_l8; /* i */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_t08olq97rscn3 gm_l11; /* n */
    void * gm_l12; /* t */
    void * gm_l13; /* d */
    void * gm_l14; /* r */
    GM_STACK_CHECK(&F, 529u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t22 = gm_l1;
    gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)t22));
    gm_l3 = t23;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t24 = gm_l4;
            uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t26 = (t24 < t25);
            if (!t26) {
                goto gm_lb1;
            }
            {
                void * t27 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t29 = gm_l4;
                {
                    {
                        memmove(((uint8_t *)&F.l5), (((uint8_t *)t27) + (uintptr_t)t29 * 56), 56);
                        uint8_t t30 = gm_l2;
                        if (t30) {
                            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 16));
                            gm_l6 = t31;
                            {
                                {
                                    uint8_t t32 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l5) + 24)) != 0);
                                    if (!t32) {
                                        goto gm_lb7;
                                    }
                                    void * t33 = GM_LD(void *, (((uint8_t *)&F.l5) + 24));
                                    gm_l7 = t33;
                                    {
                                        {
                                            {
                                                void * t34 = gm_l7;
                                                uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t34) + 8) + 0));
                                                uint8_t t36 = (uint8_t)(t35 == ((uint8_t)6ULL));
                                                if (!t36) {
                                                    goto gm_lb9;
                                                }
                                                memmove(((uint8_t *)&gm_l8), ((((uint8_t *)t34) + 8) + 8), 32);
                                            }
                                            {
                                                gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l8));
                                                gm_b16_8 t38;
                                                GM_ST(void *, (uint8_t *)&t38, (void *)(gm_s2737i1iq57nf));
                                                GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(4));
                                                gm_b16_8 t39;
                                                GM_ST(void *, (uint8_t *)&t39, (void *)(gm_sgbumttiou4de));
                                                GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(7));
                                                gm_t6j6smj7qoctu t40;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t40 + 0), t38);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t40 + 16), t39);
                                                uint8_t t41 = 0;
                                                if (gm_eq0iqiubfm7e(((uint8_t *)&t40) + 0, ((uint8_t *)&t37))) t41 = 1;
                                                if (gm_eq0iqiubfm7e(((uint8_t *)&t40) + 16, ((uint8_t *)&t37))) t41 = 1;
                                                uint8_t t42 = (uint8_t)(t41);
                                                if (t42) {
                                                    gm_l6 = ((uint8_t)1);
                                                }
                                            }
                                            goto gm_lb8;
                                        }
gm_lb9:;
                                    }
gm_lb8:;
                                    goto gm_lb6;
                                }
gm_lb7:;
                            }
gm_lb6:;
                            uint8_t t43 = gm_l6;
                            uint8_t t44 = (uint8_t)!t43;
                            if (t44) {
                                gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
                                gm_l9 = t45;
                                gm_l10 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t46 = gm_l10;
                                        uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                        uint8_t t48 = (t46 < t47);
                                        if (!t48) {
                                            goto gm_lb10;
                                        }
                                        {
                                            void * t49 = GM_LD(void *, ((uint8_t *)&gm_l9));
                                            uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                            uint64_t t51 = gm_l10;
                                            {
                                                {
                                                    {
                                                        memmove(((uint8_t *)&gm_l11), (((uint8_t *)t49) + (uintptr_t)t51 * 32), 32);
                                                    }
                                                    {
                                                        void * t52 = gm_l0;
                                                        gm_b16_8 t53;
                                                        GM_ST(void *, (uint8_t *)&t53, (void *)(gm_seqndsmrihepc));
                                                        GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(9));
                                                        gm_t9qkh0rsooc8d t54;
                                                        GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)1ULL));
                                                        gm_b0_1 t55 = gm_f_check_name_hf7dide((void *)NULL, cx, NULL, t52, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l11)), t54, t53);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                    }
                                                    goto gm_lb13;
                                                }
gm_lb14:;
                                                gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 562, 17);
                                            }
gm_lb13:;
                                        }
gm_lb12:;
                                        uint64_t t56 = gm_l10;
                                        uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
                                        uint64_t t59 = r57;
                                        gm_l10 = t59;
                                    }
                                }
gm_lb10:;
                            }
                        }
                        {
                            {
                                uint8_t t60 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l5) + 24)) != 0);
                                if (!t60) {
                                    goto gm_lb16;
                                }
                                void * t61 = GM_LD(void *, (((uint8_t *)&F.l5) + 24));
                                gm_l12 = t61;
                                void * t62 = gm_l0;
                                void * t63 = gm_l12;
                                gm_b0_1 t64 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t62, t63);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb15;
                            }
gm_lb16:;
                        }
gm_lb15:;
                        {
                            {
                                uint8_t t65 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l5) + 32)) != 0);
                                if (!t65) {
                                    goto gm_lb18;
                                }
                                void * t66 = GM_LD(void *, (((uint8_t *)&F.l5) + 32));
                                gm_l13 = t66;
                                void * t67 = gm_l0;
                                void * t68 = gm_l13;
                                gm_b0_1 t69 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t67, t68);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb17;
                            }
gm_lb18:;
                        }
gm_lb17:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 552, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t70 = gm_l4;
            uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
            uint64_t t73 = r71;
            gm_l4 = t73;
        }
    }
gm_lb1:;
    {
        {
            void * t74 = gm_l1;
            uint8_t t75 = (uint8_t)(GM_LD(void *, (((uint8_t *)t74) + 24)) != 0);
            if (!t75) {
                goto gm_lb20;
            }
            void * t76 = GM_LD(void *, (((uint8_t *)t74) + 24));
            gm_l14 = t76;
            void * t77 = gm_l0;
            void * t78 = gm_l14;
            gm_b0_1 t79 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t77, t78);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb19;
        }
gm_lb20:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_stmt_j3ls0p3(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_stmt_j3ls0p3 F;
    void * gm_l0; /* w */
    void * gm_l1; /* s */
    gm_b16_8 gm_l3; /* places */
    gm_tjsp7bhdn2e8j gm_l4; /* op */
    void * gm_l5; /* value */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    void * gm_l9; /* e */
    void * gm_l10; /* body */
    void * gm_l11; /* wh */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    void * gm_l16; /* e */
    void * gm_l17; /* e */
    void * gm_l19; /* value */
    void * gm_l20; /* fallback */
    GM_STACK_CHECK(&F, 1053u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t24 = gm_l1;
    {
        {
            uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t24) + 8) + 0));
            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)0ULL));
            if (!t26) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l2), (((((uint8_t *)t24) + 8) + 8) + 16), 64);
            void * t27 = gm_l0;
            void * t28 = (void *)((uint8_t *)&F.l2);
            gm_b0_1 t29 = gm_f_binding_stmt_80l1chq((void *)NULL, cx, NULL, t27, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t24) + 8) + 0));
            uint8_t t31 = (uint8_t)(t30 == ((uint8_t)1ULL));
            if (!t31) {
                goto gm_lb3;
            }
            gm_b16_8 t32 = GM_LD(gm_b16_8, ((((uint8_t *)t24) + 8) + 8));
            gm_l3 = t32;
            gm_tjsp7bhdn2e8j t33 = GM_LD(gm_tjsp7bhdn2e8j, (((((uint8_t *)t24) + 8) + 8) + 16));
            gm_l4 = t33;
            void * t34 = GM_LD(void *, (((((uint8_t *)t24) + 8) + 8) + 24));
            gm_l5 = t34;
            gm_l7 = ((uint64_t)0ULL);
            uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            gm_l6 = t35;
            {
                for (;;) {
                    uint64_t t36 = gm_l7;
                    uint64_t t37 = gm_l6;
                    uint8_t t38 = (t36 < t37);
                    if (!t38) {
                        goto gm_lb4;
                    }
                    {
                        {
                            {
                                uint64_t t39 = gm_l7;
                                gm_l8 = t39;
                                void * t40 = gm_l0;
                                void * t41 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                uint64_t t43 = gm_l8;
                                void * t44 = (void *)(((uint8_t *)t41) + (uintptr_t)t43 * 88);
                                gm_b0_1 t45 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t40, t44);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb7;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 381, 13);
                        }
gm_lb7:;
                    }
gm_lb6:;
                    uint64_t t46 = gm_l7;
                    uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
                    uint64_t t49 = r47;
                    gm_l7 = t49;
                }
            }
gm_lb4:;
            gm_tjsp7bhdn2e8j t50 = gm_l4;
            uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&t50) + 0));
            uint8_t t52 = (uint8_t)((t51 == ((uint8_t)0ULL)));
            gm_tjsp7bhdn2e8j t53;
            GM_ST(uint8_t, (uint8_t *)&t53 + 1, 0);
            GM_ST(uint8_t, (uint8_t *)&t53 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)0ULL));
            if (t52) {
                void * t54 = gm_l0;
                gm_b16_8 t55;
                GM_ST(void *, (uint8_t *)&t55, (void *)(gm_sium1s93t7o1a));
                GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(18));
                void * t56 = gm_l5;
                gm_b0_1 t57 = gm_f_paren_at_av727r5((void *)NULL, cx, NULL, t54, t55, t56, ((uint8_t)0));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t58 = gm_l0;
            void * t59 = gm_l5;
            gm_b0_1 t60 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t58, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t61 = GM_LD(uint8_t, ((((uint8_t *)t24) + 8) + 0));
            uint8_t t62 = (uint8_t)(t61 == ((uint8_t)2ULL));
            if (!t62) {
                goto gm_lb9;
            }
            void * t63 = GM_LD(void *, ((((uint8_t *)t24) + 8) + 8));
            gm_l9 = t63;
            void * t64 = gm_l0;
            void * t65 = gm_l9;
            gm_b0_1 t66 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t64, t65);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb9:;
        {
            uint8_t t67 = GM_LD(uint8_t, ((((uint8_t *)t24) + 8) + 0));
            uint8_t t68 = (uint8_t)(t67 == ((uint8_t)3ULL));
            if (!t68) {
                goto gm_lb10;
            }
            void * t69 = GM_LD(void *, (((((uint8_t *)t24) + 8) + 8) + 8));
            gm_l10 = t69;
            void * t70 = gm_l0;
            void * t71 = gm_l10;
            gm_b0_1 t72 = gm_f_stmt_j3ls0p3((void *)NULL, cx, NULL, t70, t71);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb10:;
        {
            uint8_t t73 = GM_LD(uint8_t, ((((uint8_t *)t24) + 8) + 0));
            uint8_t t74 = (uint8_t)(t73 == ((uint8_t)4ULL));
            if (!t74) {
                goto gm_lb11;
            }
            void * t75 = GM_LD(void *, ((((uint8_t *)t24) + 8) + 8));
            gm_l11 = t75;
            void * t76 = gm_l0;
            void * t77 = gm_l11;
            gm_b0_1 t78 = gm_f_when_stmt_qbi9l8v((void *)NULL, cx, NULL, t76, t77);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb11:;
        {
            uint8_t t79 = GM_LD(uint8_t, ((((uint8_t *)t24) + 8) + 0));
            uint8_t t80 = (uint8_t)(t79 == ((uint8_t)5ULL));
            if (!t80) {
                goto gm_lb12;
            }
            {
                memmove(((uint8_t *)&F.l12), ((((uint8_t *)t24) + 8) + 8), 112);
            }
            void * t81 = gm_l0;
            void * t82 = (void *)(((uint8_t *)&F.l12) + 40);
            gm_b0_1 t83 = gm_f_cond_e0272rd((void *)NULL, cx, NULL, t81, t82, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 40));
            gm_l13 = t84;
            gm_l14 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t85 = gm_l14;
                    uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                    uint8_t t87 = (t85 < t86);
                    if (!t87) {
                        goto gm_lb13;
                    }
                    {
                        void * t88 = GM_LD(void *, ((uint8_t *)&gm_l13));
                        uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                        uint64_t t90 = gm_l14;
                        {
                            {
                                memmove(((uint8_t *)&F.l15), (((uint8_t *)t88) + (uintptr_t)t90 * 80), 80);
                                {
                                    {
                                        uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)&F.l15) + 0));
                                        uint8_t t92 = (uint8_t)(t91 == ((uint8_t)1ULL));
                                        if (!t92) {
                                            goto gm_lb19;
                                        }
                                        void * t93 = GM_LD(void *, (((uint8_t *)&F.l15) + 8));
                                        gm_l16 = t93;
                                        {
                                            void * t94 = gm_l16;
                                            uint8_t t95 = GM_LD(uint8_t, ((((uint8_t *)t94) + 8) + 0));
                                            uint8_t t96 = (uint8_t)(t95 == ((uint8_t)3ULL));
                                            if (t96) {
                                                uint8_t t97 = GM_LD(uint8_t, ((((uint8_t *)t94) + 8) + 8));
                                                t96 = (uint8_t)(t97 == ((uint8_t)0));
                                            }
                                            uint8_t t98 = (uint8_t)(t96);
                                            gm_t2gtbh39ptekd t99 = GM_LD(gm_t2gtbh39ptekd, (((uint8_t *)t94) + 8));
                                            gm_t2gtbh39ptekd t100;
                                            memset(&t100, 0, sizeof t100);
                                            GM_ST(uint8_t, ((uint8_t *)&t100 + 0), ((uint8_t)3ULL));
                                            GM_ST(uint8_t, ((uint8_t *)&t100 + 8), ((uint8_t)0));
                                            if (t98) {
                                                void * t101 = gm_l0;
                                                void * t102 = gm_l16;
                                                gm_tl874t2atnlh3 t103 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t102) + 72));
                                                gm_b16_8 t104;
                                                GM_ST(void *, (uint8_t *)&t104, (void *)(gm_spic2fa42u00e));
                                                GM_ST(uintptr_t, (uint8_t *)&t104 + 8, (uintptr_t)(18));
                                                gm_b16_8 t105;
                                                GM_ST(void *, (uint8_t *)&t105, (void *)(gm_sgrap32255cto));
                                                GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)(46));
                                                gm_t4gut2c9fj1kt t106;
                                                gm_f_lint_diag_4vra4qu((void *)&t106, (void *)NULL, cx, NULL, t103, t104, t105);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_b0_1 t107 = gm_f_push_l0e5v4c((void *)NULL, cx, NULL, t101, &t106);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                            }
                                        }
                                        goto gm_lb18;
                                    }
gm_lb19:;
                                }
gm_lb18:;
                                goto gm_lb16;
                            }
gm_lb17:;
                            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 390, 13);
                        }
gm_lb16:;
                    }
gm_lb15:;
                    uint64_t t108 = gm_l14;
                    uint64_t r109; bool o110 = __builtin_add_overflow(t108, ((uint64_t)1ULL), &r109);
                    uint64_t t111 = r109;
                    gm_l14 = t111;
                }
            }
gm_lb13:;
            {
                void * t112 = gm_l0;
                gm_b0_1 t113 = gm_f_push_label_hn6s4vp((void *)NULL, cx, NULL, t112, &(*(gm_t1s75i08qtptd *)((uint8_t *)&F.l12)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t114 = gm_l0;
            void * t115 = (void *)(((uint8_t *)&F.l12) + 72);
            gm_b0_1 t116 = gm_f_block_btrailr((void *)NULL, cx, NULL, t114, t115);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                void * t117 = gm_l0;
                gm_b0_1 t118 = gm_f_pop_label_0jfjsf5((void *)NULL, cx, NULL, t117, &(*(gm_t1s75i08qtptd *)((uint8_t *)&F.l12)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto gm_lb1;
        }
gm_lb12:;
        {
            uint8_t t119 = GM_LD(uint8_t, ((((uint8_t *)t24) + 8) + 0));
            uint8_t t120 = (uint8_t)(t119 == ((uint8_t)6ULL));
            if (!t120) {
                goto gm_lb20;
            }
            void * t121 = GM_LD(void *, ((((uint8_t *)t24) + 8) + 8));
            gm_l17 = t121;
            void * t122 = gm_l0;
            void * t123 = gm_l17;
            gm_b0_1 t124 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t122, t123);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb20:;
        {
            uint8_t t125 = GM_LD(uint8_t, ((((uint8_t *)t24) + 8) + 0));
            uint8_t t126 = (uint8_t)(t125 == ((uint8_t)7ULL));
            if (!t126) {
                goto gm_lb21;
            }
            memmove(((uint8_t *)&F.l18), ((((uint8_t *)t24) + 8) + 8), 64);
            void * t127 = GM_LD(void *, (((((uint8_t *)t24) + 8) + 8) + 64));
            gm_l19 = t127;
            void * t128 = GM_LD(void *, (((((uint8_t *)t24) + 8) + 8) + 72));
            gm_l20 = t128;
            void * t129 = gm_l0;
            void * t130 = (void *)((uint8_t *)&F.l18);
            gm_t9qkh0rsooc8d t131;
            GM_ST(uint8_t, ((uint8_t *)&t131 + 0), ((uint8_t)0ULL));
            gm_b0_1 t132 = gm_f_pattern_8qtr7os((void *)NULL, cx, NULL, t129, t130, t131);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t133 = gm_l0;
            void * t134 = gm_l19;
            gm_b0_1 t135 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t133, t134);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t136 = gm_l0;
            void * t137 = gm_l20;
            gm_b0_1 t138 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t136, t137);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb21:;
        {
            uint8_t t139 = GM_LD(uint8_t, ((((uint8_t *)t24) + 8) + 0));
            uint8_t t140 = (uint8_t)(t139 == ((uint8_t)8ULL));
            if (!t140) {
                goto gm_lb22;
            }
            memmove(((uint8_t *)&F.l21), ((((uint8_t *)t24) + 8) + 8), 40);
            void * t141 = gm_l0;
            void * t142 = (void *)((uint8_t *)&F.l21);
            gm_b0_1 t143 = gm_f_block_btrailr((void *)NULL, cx, NULL, t141, t142);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb22:;
        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 378, 5);
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_text_kspioh6(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_top_level_name_dv4kpc9(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2) {
    struct gm_fr_top_level_name_dv4kpc9 F;
    void * gm_l0; /* w */
    gm_t08olq97rscn3 gm_l1; /* name */
    void * gm_l2; /* b */
    void * gm_l3; /* inner */
    void * gm_l4; /* i */
    uint8_t gm_l5; /* c_union */
    gm_b16_8 gm_l6; /* attrs */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    void * gm_l10; /* s */
    void * gm_l11; /* u */
    void * gm_l12; /* e */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    GM_STACK_CHECK(&F, 670u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    gm_l2 = a2;
    void * t33 = gm_l2;
    void * t34 = GM_LD(void *, (((uint8_t *)t33) + 40));
    gm_l3 = t34;
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb1;
            }
            {
                {
                    {
                        void * t35 = gm_l3;
                        uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)t35) + 8) + 0));
                        uint8_t t37 = (uint8_t)(t36 == ((uint8_t)35ULL));
                        if (!t37) {
                            goto gm_lb5;
                        }
                        void * t38 = GM_LD(void *, (((((uint8_t *)t35) + 8) + 8) + 16));
                        gm_l4 = t38;
                        void * t39 = gm_l4;
                        gm_l3 = t39;
                        goto gm_lb4;
                    }
gm_lb5:;
                    goto gm_lb1;
                }
gm_lb4:;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t40 = gm_l3;
    {
        {
            {
                {
                    uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
                    uint8_t t42 = (uint8_t)(t41 == ((uint8_t)32ULL));
                    if (!t42) {
                        goto gm_lb9;
                    }
                    goto gm_lb8;
                }
gm_lb9:;
                {
                    uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
                    uint8_t t44 = (uint8_t)(t43 == ((uint8_t)33ULL));
                    if (!t44) {
                        goto gm_lb10;
                    }
                    goto gm_lb8;
                }
gm_lb10:;
                {
                    uint8_t t45 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
                    uint8_t t46 = (uint8_t)(t45 == ((uint8_t)34ULL));
                    if (!t46) {
                        goto gm_lb11;
                    }
                    goto gm_lb8;
                }
gm_lb11:;
                goto gm_lb7;
            }
gm_lb8:;
            void * t47 = gm_l0;
            gm_b16_8 t48;
            GM_ST(void *, (uint8_t *)&t48, (void *)(gm_s2737i1iq57nf));
            GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(4));
            gm_t9qkh0rsooc8d t49;
            GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)0ULL));
            gm_b0_1 t50 = gm_f_check_name_hf7dide((void *)NULL, cx, NULL, t47, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l1)), t49, t48);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb6;
        }
gm_lb7:;
        {
            uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
            uint8_t t52 = (uint8_t)(t51 == ((uint8_t)36ULL));
            if (!t52) {
                goto gm_lb12;
            }
            void * t53 = gm_l0;
            gm_b16_8 t54;
            GM_ST(void *, (uint8_t *)&t54, (void *)(gm_sq0dqiq83b924));
            GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(8));
            gm_t9qkh0rsooc8d t55;
            GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)1ULL));
            gm_b0_1 t56 = gm_f_check_name_hf7dide((void *)NULL, cx, NULL, t53, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l1)), t55, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb6;
        }
gm_lb12:;
        {
            uint8_t t57 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
            uint8_t t58 = (uint8_t)(t57 == ((uint8_t)38ULL));
            if (!t58) {
                goto gm_lb13;
            }
            void * t59 = gm_l0;
            gm_b16_8 t60;
            GM_ST(void *, (uint8_t *)&t60, (void *)(gm_sg3kpa3h16no3));
            GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(6));
            gm_t9qkh0rsooc8d t61;
            GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)1ULL));
            gm_b0_1 t62 = gm_f_check_name_hf7dide((void *)NULL, cx, NULL, t59, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l1)), t61, t60);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb6;
        }
gm_lb13:;
        {
            goto gm_lb6;
        }
gm_lb14:;
        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 137, 5);
    }
gm_lb6:;
    gm_l5 = ((uint8_t)0);
    {
        {
            void * t63 = gm_l2;
            void * t64 = GM_LD(void *, (((uint8_t *)t63) + 40));
            uint8_t t65 = GM_LD(uint8_t, ((((uint8_t *)t64) + 8) + 0));
            uint8_t t66 = (uint8_t)(t65 == ((uint8_t)35ULL));
            if (!t66) {
                goto gm_lb16;
            }
            gm_b16_8 t67 = GM_LD(gm_b16_8, ((((uint8_t *)t64) + 8) + 8));
            gm_l6 = t67;
            gm_b16_8 t68 = gm_l6;
            gm_l7 = t68;
            gm_l8 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t69 = gm_l8;
                    uint64_t t70 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    uint8_t t71 = (t69 < t70);
                    if (!t71) {
                        goto gm_lb17;
                    }
                    {
                        void * t72 = GM_LD(void *, ((uint8_t *)&gm_l7));
                        uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                        uint64_t t74 = gm_l8;
                        {
                            {
                                memmove(((uint8_t *)&F.l9), (((uint8_t *)t72) + (uintptr_t)t74 * 72), 72);
                                gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
                                gm_b16_8 t76;
                                GM_ST(void *, (uint8_t *)&t76, (void *)(gm_suuv0qpi4v4e4));
                                GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(7));
                                uint8_t t77 = (gm_eq0iqiubfm7e(((uint8_t *)&t75), ((uint8_t *)&t76)));
                                if (t77) {
                                    gm_l5 = ((uint8_t)1);
                                }
                                goto gm_lb20;
                            }
gm_lb21:;
                            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 146, 9);
                        }
gm_lb20:;
                    }
gm_lb19:;
                    uint64_t t78 = gm_l8;
                    uint64_t r79; bool o80 = __builtin_add_overflow(t78, ((uint64_t)1ULL), &r79);
                    uint64_t t81 = r79;
                    gm_l8 = t81;
                }
            }
gm_lb17:;
            goto gm_lb15;
        }
gm_lb16:;
    }
gm_lb15:;
    void * t82 = gm_l3;
    {
        {
            uint8_t t83 = GM_LD(uint8_t, ((((uint8_t *)t82) + 8) + 0));
            uint8_t t84 = (uint8_t)(t83 == ((uint8_t)32ULL));
            if (!t84) {
                goto gm_lb23;
            }
            void * t85 = GM_LD(void *, ((((uint8_t *)t82) + 8) + 8));
            gm_l10 = t85;
            void * t86 = gm_l0;
            void * t87 = gm_l10;
            gm_b16_8 t88 = GM_LD(gm_b16_8, (((uint8_t *)t87) + 40));
            gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&t88));
            gm_b0_1 t90 = gm_f_fields_8ttf3eb((void *)NULL, cx, NULL, t86, t89);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb22;
        }
gm_lb23:;
        {
            uint8_t t91 = GM_LD(uint8_t, ((((uint8_t *)t82) + 8) + 0));
            uint8_t t92 = (uint8_t)(t91 == ((uint8_t)33ULL));
            if (!t92) {
                goto gm_lb24;
            }
            void * t93 = GM_LD(void *, ((((uint8_t *)t82) + 8) + 8));
            gm_l11 = t93;
            uint8_t t94 = gm_l5;
            uint8_t t95 = (uint8_t)!t94;
            if (t95) {
                void * t96 = gm_l0;
                void * t97 = gm_l11;
                gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)t97) + 48));
                gm_b16_8 t99 = GM_LD(gm_b16_8, ((uint8_t *)&t98));
                gm_b0_1 t100 = gm_f_variants_eef2p79((void *)NULL, cx, NULL, t96, t99);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto gm_lb22;
        }
gm_lb24:;
        {
            uint8_t t101 = GM_LD(uint8_t, ((((uint8_t *)t82) + 8) + 0));
            uint8_t t102 = (uint8_t)(t101 == ((uint8_t)34ULL));
            if (!t102) {
                goto gm_lb25;
            }
            void * t103 = GM_LD(void *, ((((uint8_t *)t82) + 8) + 8));
            gm_l12 = t103;
            void * t104 = gm_l12;
            gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)t104) + 24));
            gm_l13 = t105;
            gm_l14 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t106 = gm_l14;
                    uint64_t t107 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                    uint8_t t108 = (t106 < t107);
                    if (!t108) {
                        goto gm_lb26;
                    }
                    {
                        void * t109 = GM_LD(void *, ((uint8_t *)&gm_l13));
                        uint64_t t110 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                        uint64_t t111 = gm_l14;
                        {
                            {
                                {
                                    memmove(((uint8_t *)&F.l15), (((uint8_t *)t109) + (uintptr_t)t111 * 80), 80);
                                }
                                {
                                    void * t112 = gm_l0;
                                    gm_b16_8 t113;
                                    GM_ST(void *, (uint8_t *)&t113, (void *)(gm_sk6ueggk5ji6v));
                                    GM_ST(uintptr_t, (uint8_t *)&t113 + 8, (uintptr_t)(9));
                                    gm_t9qkh0rsooc8d t114;
                                    GM_ST(uint8_t, ((uint8_t *)&t114 + 0), ((uint8_t)1ULL));
                                    gm_b0_1 t115 = gm_f_check_name_hf7dide((void *)NULL, cx, NULL, t112, &(*(gm_t08olq97rscn3 *)((uint8_t *)&F.l15)), t114, t113);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                goto gm_lb29;
                            }
gm_lb30:;
                            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 154, 13);
                        }
gm_lb29:;
                    }
gm_lb28:;
                    uint64_t t116 = gm_l14;
                    uint64_t r117; bool o118 = __builtin_add_overflow(t116, ((uint64_t)1ULL), &r117);
                    uint64_t t119 = r117;
                    gm_l14 = t119;
                }
            }
gm_lb26:;
            goto gm_lb22;
        }
gm_lb25:;
        {
            goto gm_lb22;
        }
gm_lb31:;
        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 150, 5);
    }
gm_lb22:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unused_local_decl_2l8feim(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2) {
    struct gm_fr_unused_local_decl_2l8feim F;
    void * gm_l0; /* w */
    void * gm_l1; /* s */
    uint32_t gm_l2; /* end */
    gm_t08olq97rscn3 gm_l4; /* name */
    gm_b16_8 gm_l5; /* what */
    GM_STACK_CHECK(&F, 602u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l7;
    uint8_t gm_l8;
    uint8_t gm_l9;
    gm_th2lqbo77pvsc gm_l10;
    gm_tabb710d9mq2e gm_l11;
    {
        {
            {
                void * t7 = gm_l1;
                uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
                uint8_t t9 = (uint8_t)(t8 == ((uint8_t)0ULL));
                if (!t9) {
                    goto gm_lb2;
                }
                memmove(((uint8_t *)&F.l3), (((((uint8_t *)t7) + 8) + 8) + 16), 64);
            }
            gm_t9qkh0rsooc8d t10 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l3) + 32));
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
            uint8_t t12 = (uint8_t)(!(t11 == ((uint8_t)0ULL)));
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            if (t12) {
                gm_l8 = ((uint8_t)1);
            } else {
                {
                    {
                        uint8_t t14 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l3) + 24)) != 0);
                        if (t14) {
                            goto gm_lb4;
                        }
                        gm_l9 = ((uint8_t)0);
                        goto gm_lb3;
                    }
gm_lb4:;
                    {
                        gm_l9 = ((uint8_t)1);
                        goto gm_lb3;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 337, 32);
                }
gm_lb3:;
                uint8_t t15 = gm_l9;
                gm_l8 = t15;
            }
            uint8_t t16 = gm_l8;
            if (t16) {
                gm_l7 = ((uint8_t)1);
            } else {
                uint64_t t17 = GM_LD(uint64_t, ((((uint8_t *)&F.l3) + 8) + 8));
                uint8_t t18 = (!(t17 == ((uint64_t)1ULL)));
                gm_l7 = t18;
            }
            uint8_t t19 = gm_l7;
            if (t19) {
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            {
                void * t20 = GM_LD(void *, (((uint8_t *)&F.l3) + 8));
                uint64_t t21 = GM_LD(uint64_t, ((((uint8_t *)&F.l3) + 8) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t21)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t21, gm_src10tru552hn49, 338, 13);
                memmove(((uint8_t *)&gm_l4), (((uint8_t *)t20) + (uintptr_t)((uint64_t)0ULL) * 32), 32);
            }
            gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sfdbfrv46lihd));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(1));
            uint8_t t24 = gm_f_starts_with_6vvt01e(t22, t23);
            if (t24) {
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            {
                void * t25 = GM_LD(void *, (((uint8_t *)&F.l3) + 40));
                gm_f_declared_kind_4vgnqmf((void *)&gm_l10, t25);
            }
            uint8_t t26 = (uint8_t)((((uint8_t *)&gm_l10))[16] != 0);
            if (t26) {
                gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l10));
                gm_l5 = t27;
            } else {
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            void * t28 = gm_l0;
            gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
            void * t30 = gm_l1;
            uint32_t t31 = GM_LD(uint32_t, ((((uint8_t *)t30) + 128) + 8));
            uint32_t t32 = gm_l2;
            uint8_t t33 = gm_f_mentions_4p99unq(t28, t29, t31, t32);
            if (t33) {
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            {
                gm_tl874t2atnlh3 t34 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l4) + 16));
                gm_b16_8 t35;
                GM_ST(void *, (uint8_t *)&t35, (void *)(gm_spn8bpm132g2m));
                GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(18));
                gm_b16_8 t36 = gm_l5;
                gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                gm_t6j6smj7qoctu t38;
                GM_ST(gm_b16_8, ((uint8_t *)&t38 + 0), t36);
                GM_ST(gm_b16_8, ((uint8_t *)&t38 + 16), t37);
                gm_f_format_uiq3pfi((void *)&gm_l11, (void *)NULL, cx, NULL, &t38);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t39 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                if (!t39) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_src10tru552hn49, 342, 53);
                gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
                gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
                gm_t4gut2c9fj1kt t42;
                gm_f_lint_diag_4vra4qu((void *)&t42, (void *)NULL, cx, NULL, t34, t35, t41);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l6 = t42;
            }
            void * t43 = (void *)((uint8_t *)&F.l6);
            gm_b16_8 t44;
            GM_ST(void *, (uint8_t *)&t44, (void *)(gm_smh4q1ndijmcf));
            GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(38));
            gm_f_add_note_46v502s(t43, t44);
            {
                void * t45 = gm_l0;
                gm_b0_1 t46 = gm_f_push_l0e5v4c((void *)NULL, cx, NULL, t45, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l6)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto gm_lb1;
        }
gm_lb2:;
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
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
extern __attribute__((aligned(4))) uint8_t gm_f_upper_camel_a0jt5mh(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_use_label_8j0qqam(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_use_label_8j0qqam F;
    void * gm_l0; /* w */
    gm_t08olq97rscn3 gm_l2; /* l */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 187u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t5 = (uint8_t)((((uint8_t *)a1))[32] != 0);
    if (t5) {
        memmove(((uint8_t *)&gm_l2), ((uint8_t *)a1), 32);
    } else {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t6 = gm_l0;
    uint64_t t7 = GM_LD(uint64_t, ((((uint8_t *)t6) + 224) + 8));
    gm_l3 = t7;
    {
        for (;;) {
            uint64_t t8 = gm_l3;
            uint8_t t9 = (t8 > ((uint64_t)0ULL));
            if (!t9) {
                goto gm_lb1;
            }
            {
                uint64_t t10 = gm_l3;
                uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)1ULL), &r11);
                if (o12) gm_trap("integer overflow in `-`", gm_src10tru552hn49, 465, 9);
                uint64_t t13 = r11;
                gm_l3 = t13;
                void * t14 = gm_l0;
                void * t15 = GM_LD(void *, (((uint8_t *)t14) + 224));
                uint64_t t16 = GM_LD(uint64_t, ((((uint8_t *)t14) + 224) + 8));
                uint64_t t17 = gm_l3;
                if ((uintptr_t)t17 >= (uintptr_t)(t16)) gm_trap_index((uintptr_t)t17, t16, gm_src10tru552hn49, 466, 12);
                gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)t15) + (uintptr_t)t17 * 32));
                gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
                uint8_t t20 = (gm_eq0iqiubfm7e(((uint8_t *)&t18), ((uint8_t *)&t19)));
                if (t20) {
                    void * t21 = gm_l0;
                    void * t22 = GM_LD(void *, (((uint8_t *)t21) + 224));
                    uint64_t t23 = GM_LD(uint64_t, ((((uint8_t *)t21) + 224) + 8));
                    uint64_t t24 = gm_l3;
                    if ((uintptr_t)t24 >= (uintptr_t)(t23)) gm_trap_index((uintptr_t)t24, t23, gm_src10tru552hn49, 467, 13);
                    GM_ST(uint8_t, ((((uint8_t *)t22) + (uintptr_t)t24 * 32) + 28), ((uint8_t)1));
                    F.ret = GM_ZERO(gm_b0_1);
                    goto gm_fret;
                }
            }
gm_lb3:;
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
__attribute__((aligned(4))) void gm_f_use_label_8j0qqam_c(void * a0, gm_t1s75i08qtptd a1) { gm_f_use_label_8j0qqam(a0, &a1); }
inline __attribute__((aligned(4))) gm_b0_1 gm_f_variants_eef2p79(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_variants_eef2p79 F;
    void * gm_l0; /* w */
    gm_b16_8 gm_l1; /* members */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 279u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 gm_l7;
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
                        memmove(((uint8_t *)&F.l4), (((uint8_t *)t14) + (uintptr_t)t16 * 72), 72);
                        {
                            {
                                uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                                uint8_t t18 = (uint8_t)(t17 == ((uint8_t)0ULL));
                                if (!t18) {
                                    goto gm_lb7;
                                }
                                {
                                    memmove(((uint8_t *)&F.l5), (((uint8_t *)&F.l4) + 8), 64);
                                }
                                {
                                    void * t19 = gm_l0;
                                    gm_b16_8 t20;
                                    GM_ST(void *, (uint8_t *)&t20, (void *)(gm_spj9t9b8lqlpp));
                                    GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(7));
                                    gm_t9qkh0rsooc8d t21;
                                    GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)0ULL));
                                    gm_b0_1 t22 = gm_f_check_name_hf7dide((void *)NULL, cx, NULL, t19, &(*(gm_t08olq97rscn3 *)((uint8_t *)&F.l5)), t21, t20);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                                uint8_t t24 = (uint8_t)(t23 == ((uint8_t)1ULL));
                                if (!t24) {
                                    goto gm_lb8;
                                }
                                memmove(((uint8_t *)&F.l6), (((uint8_t *)&F.l4) + 8), 64);
                                void * t25 = gm_l0;
                                gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 8));
                                gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
                                gm_b0_1 t28 = gm_f_variants_eef2p79((void *)NULL, cx, NULL, t25, t27);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t29 = gm_l0;
                                uint8_t t30 = (uint8_t)(((((uint8_t *)&F.l6) + 24))[16] != 0);
                                if (t30) {
                                    gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 24));
                                    gm_l7 = t31;
                                } else {
                                    gm_b16_8 t32 = gm_f_empty_slice_u4e74ha();
                                    gm_l7 = t32;
                                }
                                gm_b16_8 t33 = gm_l7;
                                gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
                                gm_b0_1 t35 = gm_f_variants_eef2p79((void *)NULL, cx, NULL, t29, t34);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 176, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 175, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t36 = gm_l3;
            uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
            uint64_t t39 = r37;
            gm_l3 = t39;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_walk_file_do4uq8b(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_walk_file_do4uq8b F;
    void * gm_l0; /* w */
    void * gm_l1; /* file */
    gm_b16_8 gm_l2; /* tokens */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    GM_STACK_CHECK(&F, 185u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t7 = gm_l0;
    gm_b16_8 t8 = gm_l2;
    GM_ST(gm_b16_8, (((uint8_t *)t7) + 264), t8);
    gm_l4 = ((uint64_t)0ULL);
    void * t9 = gm_l1;
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t9) + 8));
    gm_l3 = t10;
    {
        for (;;) {
            uint64_t t11 = gm_l4;
            uint64_t t12 = gm_l3;
            uint8_t t13 = (t11 < t12);
            if (!t13) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t14 = gm_l4;
                        gm_l5 = t14;
                        void * t15 = gm_l0;
                        void * t16 = gm_l1;
                        void * t17 = GM_LD(void *, ((uint8_t *)t16));
                        uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)t16) + 8));
                        uint64_t t19 = gm_l5;
                        if ((uintptr_t)t19 >= (uintptr_t)(t18)) gm_trap_index((uintptr_t)t19, t18, gm_src10tru552hn49, 79, 42);
                        void * t20 = (void *)(((uint8_t *)t17) + (uintptr_t)t19 * 160);
                        gm_b0_1 t21 = gm_f_item_a3ennq0((void *)NULL, cx, NULL, t15, t20);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 79, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t22 = gm_l4;
            uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
            uint64_t t25 = r23;
            gm_l4 = t25;
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
extern __attribute__((aligned(4))) void gm_f_walk_tech5q5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_when_stmt_qbi9l8v(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_when_stmt_qbi9l8v F;
    void * gm_l0; /* w */
    void * gm_l1; /* wh */
    void * gm_l4; /* inner */
    GM_STACK_CHECK(&F, 166u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l5;
    void * t7 = gm_l0;
    void * t8 = (void *)((((uint8_t *)t7) + 8) + 96);
    void * t9 = gm_l1;
    gm_tl874t2atnlh3 t10 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t9) + 104));
    gm_tftb1odc739t5 t11 = gm_f_push_0mk7udh(t8, GM_CONV(gm_cseahvg8v7p0vs, t10));
    gm_l5 = t11;
    uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t12) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_src10tru552hn49, 412, 5);
    void * t13 = gm_l0;
    void * t14 = gm_l1;
    void * t15 = GM_LD(void *, ((uint8_t *)t14));
    gm_b0_1 t16 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t13, t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t17 = gm_l0;
    void * t18 = gm_l1;
    void * t19 = (void *)(((uint8_t *)t18) + 8);
    gm_b0_1 t20 = gm_f_block_btrailr((void *)NULL, cx, NULL, t17, t19);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            void * t21 = gm_l1;
            uint8_t t22 = (uint8_t)(((((uint8_t *)t21) + 48))[48] != 0);
            if (!t22) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l2), (((uint8_t *)t21) + 48), 48);
            {
                {
                    uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                    uint8_t t24 = (uint8_t)(t23 == ((uint8_t)0ULL));
                    if (!t24) {
                        goto gm_lb4;
                    }
                    memmove(((uint8_t *)&F.l3), (((uint8_t *)&F.l2) + 8), 40);
                    void * t25 = gm_l0;
                    void * t26 = (void *)((uint8_t *)&F.l3);
                    gm_b0_1 t27 = gm_f_block_btrailr((void *)NULL, cx, NULL, t25, t26);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                    uint8_t t29 = (uint8_t)(t28 == ((uint8_t)1ULL));
                    if (!t29) {
                        goto gm_lb5;
                    }
                    void * t30 = GM_LD(void *, (((uint8_t *)&F.l2) + 8));
                    gm_l4 = t30;
                    void * t31 = gm_l0;
                    void * t32 = gm_l4;
                    gm_b0_1 t33 = gm_f_when_stmt_qbi9l8v((void *)NULL, cx, NULL, t31, t32);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 416, 9);
            }
gm_lb3:;
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

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
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t f3; } gm_t2abnvcnt53ot;
typedef struct { uint8_t a[8]; } gm_t2doi72kmmlil;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t a[32]; } gm_t7mlrlibdq1ja;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t a[8]; } gm_tg8e8rhc3qio9;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t a[64]; } gm_tsn02dhhitgs4;
typedef struct { uint8_t f1; } gm_ttsm01t0d8srg;
_Static_assert(sizeof(gm_t2abnvcnt53ot) == 4 && _Alignof(gm_t2abnvcnt53ot) == 1 && offsetof(gm_t2abnvcnt53ot, f0) == 0 && offsetof(gm_t2abnvcnt53ot, f1) == 1 && offsetof(gm_t2abnvcnt53ot, f2) == 2 && offsetof(gm_t2abnvcnt53ot, f3) == 3, "layout of `(u8, u8, u8, u8)`");
_Static_assert(sizeof(gm_t2doi72kmmlil) == 8 && _Alignof(gm_t2doi72kmmlil) == 1 && offsetof(gm_t2doi72kmmlil, a) == 0, "layout of `[8]u8`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 a[6]; } gm_t4d1ka1c3gdt5;
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; int64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t5j14m8ru132p;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t7mlrlibdq1ja) == 32 && _Alignof(gm_t7mlrlibdq1ja) == 1 && offsetof(gm_t7mlrlibdq1ja, a) == 0, "layout of `[32]u8`");
typedef struct { gm_b16_8 a[3]; } gm_t9bmh80edou9c;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_b16_8 a[4]; } gm_tdmi4fk9nhqa7;
typedef struct { gm_b16_8 a[7]; } gm_tdv3dkjvo1l7t;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_tg8e8rhc3qio9) == 32 && _Alignof(gm_tg8e8rhc3qio9) == 4 && offsetof(gm_tg8e8rhc3qio9, a) == 0, "layout of `[8]u32`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { int64_t f0; gm_b16_8 f1; } gm_thgmovcqlsivv;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tipro5npl6rd8;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_t9qkh0rsooc8d v1; } f1; } gm_tj5jn79kq9pnm;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_tg8e8rhc3qio9 f0; gm_tsn02dhhitgs4 f1; uint64_t f2; uint64_t f3; } gm_tq14qvifgdvpm;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tsn02dhhitgs4) == 64 && _Alignof(gm_tsn02dhhitgs4) == 1 && offsetof(gm_tsn02dhhitgs4, a) == 0, "layout of `[64]u8`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; } f1; } gm_ttln0d0hd70en;
_Static_assert(sizeof(gm_ttsm01t0d8srg) == 1 && _Alignof(gm_ttsm01t0d8srg) == 1 && offsetof(gm_ttsm01t0d8srg, f1) == 0, "layout of `?()`");
typedef struct { uint64_t f0; gm_t6j6smj7qoctu f1; } gm_t1l3r16b8478v;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `(string, usize)`");
_Static_assert(sizeof(gm_t4d1ka1c3gdt5) == 96 && _Alignof(gm_t4d1ka1c3gdt5) == 8 && offsetof(gm_t4d1ka1c3gdt5, a) == 0, "layout of `[6]string`");
_Static_assert(sizeof(gm_t5j14m8ru132p) == 32 && _Alignof(gm_t5j14m8ru132p) == 8 && offsetof(gm_t5j14m8ru132p, f0) == 0 && offsetof(gm_t5j14m8ru132p, f1) == 8 && offsetof(gm_t5j14m8ru132p, f2) == 16 && offsetof(gm_t5j14m8ru132p, f3) == 24, "layout of `FileInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t9bmh80edou9c) == 48 && _Alignof(gm_t9bmh80edou9c) == 8 && offsetof(gm_t9bmh80edou9c, a) == 0, "layout of `[3]string`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tdmi4fk9nhqa7) == 64 && _Alignof(gm_tdmi4fk9nhqa7) == 8 && offsetof(gm_tdmi4fk9nhqa7, a) == 0, "layout of `[4]string`");
_Static_assert(sizeof(gm_tdv3dkjvo1l7t) == 112 && _Alignof(gm_tdv3dkjvo1l7t) == 8 && offsetof(gm_tdv3dkjvo1l7t, a) == 0, "layout of `[7]string`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tibupt4dmcqai v1; } f1; } gm_te2n4j2vtm53d;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tj5jn79kq9pnm v0; gm_b16_8 v1; } f1; } gm_tgvh15qshf643;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thgmovcqlsivv) == 24 && _Alignof(gm_thgmovcqlsivv) == 8 && offsetof(gm_thgmovcqlsivv, f0) == 0 && offsetof(gm_thgmovcqlsivv, f1) == 8, "layout of `CachedProgram`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t5j14m8ru132p v1; } f1; } gm_tih6h4avv5nrb;
_Static_assert(sizeof(gm_tipro5npl6rd8) == 40 && _Alignof(gm_tipro5npl6rd8) == 8 && offsetof(gm_tipro5npl6rd8, f0) == 0 && offsetof(gm_tipro5npl6rd8, f1) == 4 && offsetof(gm_tipro5npl6rd8, f2) == 8 && offsetof(gm_tipro5npl6rd8, f3) == 24, "layout of `Ran`");
_Static_assert(sizeof(gm_tj5jn79kq9pnm) == 12 && _Alignof(gm_tj5jn79kq9pnm) == 4 && offsetof(gm_tj5jn79kq9pnm, f0) == 0 && offsetof(gm_tj5jn79kq9pnm, f1) == 4, "layout of `ReadError`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tk2lc304hfpci;
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tq14qvifgdvpm) == 112 && _Alignof(gm_tq14qvifgdvpm) == 8 && offsetof(gm_tq14qvifgdvpm, f0) == 0 && offsetof(gm_tq14qvifgdvpm, f1) == 32 && offsetof(gm_tq14qvifgdvpm, f2) == 96 && offsetof(gm_tq14qvifgdvpm, f3) == 104, "layout of `Sha256`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ttln0d0hd70en) == 12 && _Alignof(gm_ttln0d0hd70en) == 4 && offsetof(gm_ttln0d0hd70en, f0) == 0 && offsetof(gm_ttln0d0hd70en, f1) == 4, "layout of `IoError!()`");
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_t1l3r16b8478v) == 40 && _Alignof(gm_t1l3r16b8478v) == 8 && offsetof(gm_t1l3r16b8478v, f0) == 0 && offsetof(gm_t1l3r16b8478v, f1) == 8, "layout of `(i: usize, x: CSource)`");
_Static_assert(sizeof(gm_te2n4j2vtm53d) == 32 && _Alignof(gm_te2n4j2vtm53d) == 8 && offsetof(gm_te2n4j2vtm53d, f0) == 0 && offsetof(gm_te2n4j2vtm53d, f1) == 8, "layout of `IoError!Entry`");
_Static_assert(sizeof(gm_tgvh15qshf643) == 24 && _Alignof(gm_tgvh15qshf643) == 8 && offsetof(gm_tgvh15qshf643, f0) == 0 && offsetof(gm_tgvh15qshf643, f1) == 8, "layout of `ReadError![]u8`");
_Static_assert(sizeof(gm_tih6h4avv5nrb) == 40 && _Alignof(gm_tih6h4avv5nrb) == 8 && offsetof(gm_tih6h4avv5nrb, f0) == 0 && offsetof(gm_tih6h4avv5nrb, f1) == 8, "layout of `IoError!FileInfo`");
_Static_assert(sizeof(gm_tk2lc304hfpci) == 112 && _Alignof(gm_tk2lc304hfpci) == 8 && offsetof(gm_tk2lc304hfpci, f0) == 0 && offsetof(gm_tk2lc304hfpci, f1) == 40 && offsetof(gm_tk2lc304hfpci, f2) == 80 && offsetof(gm_tk2lc304hfpci, f3) == 96, "layout of `Link`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tk2lc304hfpci v1; } f1; } gm_tllkbcpqeka9b;
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
_Static_assert(sizeof(gm_tllkbcpqeka9b) == 120 && _Alignof(gm_tllkbcpqeka9b) == 8 && offsetof(gm_tllkbcpqeka9b, f0) == 0 && offsetof(gm_tllkbcpqeka9b, f1) == 8, "layout of `string!Link`");
struct gm_fr_adopt_object_psc9tpi {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_cache_key_tf6ebrd {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* buf */
    gm_b16_8 ret;
};
struct gm_fr_clear_h4369ui {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_compiler_version_hj63jgf {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* argv */
    gm_tipro5npl6rd8 l2; /* r */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_deinit_r70pji5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_deps_current_jlls0pm {
    gm_frame h;
    uint8_t ret;
    gm_b0_1 w0;
};
struct gm_fr_deps_listing_7genqb0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_empty_slice_4pgooq2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_vogsd72 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_fn_c438h0n {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_for_body_1b63vnj {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_6ellthc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_t2jmto9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_3pfhto5 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c31q7f8 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c8gbrg9 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cncadt9 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_gum3rpo {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_n1il1vf {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nk7pf4i {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_pf7dqps {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_soc2hp6 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vhqrhad {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_init_s32ien0 {
    gm_frame h;
    gm_tq14qvifgdvpm ret;
};
struct gm_fr_is_clang_7du2shk {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_file_tcuv9rv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_gcc_epb17a6 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vuor028 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_make_prerequisites_v713689 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* words */
    gm_trqfvmjtt4sij l2; /* word */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_no_link_a2212mt {
    gm_frame h;
    gm_tk2lc304hfpci ret;
};
struct gm_fr_package_link_q0071pm {
    gm_frame h;
    gm_tk2lc304hfpci l5; /* link */
    gm_trqfvmjtt4sij l11; /* flags */
    gm_tllkbcpqeka9b ret;
};
struct gm_fr_package_object_60se70a {
    gm_frame h;
    gm_tdv3dkjvo1l7t l8; /* <temporary array> */
    gm_tdv3dkjvo1l7t l16; /* tail */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_private_name_u7i0rip {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_prune_eu93ghe {
    gm_frame h;
    gm_b16_8 l0; /* dir */
    int64_t l2; /* stale */
    gm_trqfvmjtt4sij l3; /* progs */
    gm_b0_1 ret;
    gm_b0_1 w0;
};
struct gm_fr_prune_units_iqs7tqd {
    gm_frame h;
    gm_b16_8 l0; /* dir */
    int64_t l2; /* stale */
    gm_trqfvmjtt4sij l3; /* objs */
    gm_b0_1 ret;
    gm_b0_1 w0;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_4u3hstp {
    gm_frame h;
    gm_tftb1odc739t5 ret;
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
struct gm_fr_regrow_d2hchu8 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_rename_or_remove_l80dbfs {
    gm_frame h;
    gm_ttsm01t0d8srg ret;
};
struct gm_fr_runtime_object_oihv3ss {
    gm_frame h;
    gm_t4d1ka1c3gdt5 l7; /* <temporary array> */
    gm_tdmi4fk9nhqa7 l14; /* tail */
    gm_trqfvmjtt4sij l15; /* argv */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_sha256_hex_4vhcm7o {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_sort_by_ci5a9mg {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_sum_ugu720i {
    gm_frame h;
    gm_tq14qvifgdvpm l1; /* s */
    gm_t7mlrlibdq1ja ret;
};
struct gm_fr_temporary_object_tji353n {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_touch_frggs6u {
    gm_frame h;
    gm_t9bmh80edou9c l1; /* argv */
    gm_b0_1 ret;
};
struct gm_fr_unit_objects_3f31tcd {
    gm_frame h;
    gm_trqfvmjtt4sij l11; /* objects */
    gm_trqfvmjtt4sij l12; /* todo_c */
    gm_trqfvmjtt4sij l13; /* todo_tmp */
    gm_trqfvmjtt4sij l14; /* todo_obj */
    gm_t4d1ka1c3gdt5 l19; /* <temporary array> */
    gm_tl572d7579hns ret;
};
struct gm_fr_units_to_prune_qp6vjmt {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_write_jltv6p4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_vue6tin {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) uint8_t gm_f_adopt_object_psc9tpi(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_cache_dir_81h90qu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_cache_key_tf6ebrd(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_clear_h4369ui(void * a0);
__attribute__((aligned(4))) void gm_f_command_b2u33fl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_compile_objects_mu5secm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, uint64_t a4);
__attribute__((aligned(4))) void gm_f_compile_source_85vh57g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_compiler_version_hj63jgf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_copy_fg1ge35(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_deinit_r70pji5(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_deps_current_jlls0pm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_deps_listing_7genqb0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_dupe_g9iqpvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_4pgooq2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_sh4kvg0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_sre2b72(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_vogsd72(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_final_rg5ko3p(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_c438h0n(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1b63vnj(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_6ellthc(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_t2jmto9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_format_2kt92vg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_3pfhto5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_c31q7f8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_c8gbrg9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_cncadt9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_gum3rpo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_mccr87v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_n1il1vf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_nk7pf4i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_pf7dqps(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_soc2hp6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_vhqrhad(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_hex_euih242(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_init_s32ien0(void *gm_out);
__attribute__((aligned(4))) void gm_f_introsort_scs0qol(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_clang_7du2shk(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_clang_7du2shk_c(gm_th2lqbo77pvsc a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_tcuv9rv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_gcc_epb17a6(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_gcc_epb17a6_c(gm_th2lqbo77pvsc a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_qveh5b2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_s025qou(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vuor028(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_make_cache_dirs_3bvvbb9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_make_prerequisites_v713689(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_no_link_a2212mt(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_object_flags_aequi2e(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint8_t a0, uint8_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_package_link_q0071pm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, uint8_t a3, uint8_t a4);
__attribute__((aligned(4))) void gm_f_package_object_60se70a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, const void * a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_private_name_u7i0rip(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_prune_eu93ghe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_prune_units_iqs7tqd(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_4u3hstp(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_read_all_r9rh9qd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_d2hchu8(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) gm_b0_1 gm_f_remove_objects_1vq49q8(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_ttsm01t0d8srg gm_f_rename_or_remove_l80dbfs(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_resolve_lines_h4bf30f(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_run_mj5dobu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_runtime_object_oihv3ss(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, const void * a4);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_sha256_hex_4vhcm7o(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_sort_by_ci5a9mg(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_split_once_mqcu1ce(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_sum_ugu720i(void *gm_out, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_temporary_object_tji353n(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_touch_frggs6u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_unit_objects_3f31tcd(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, const void * a3, uint64_t a4, uint8_t a5);
__attribute__((aligned(4))) gm_b16_8 gm_f_units_to_prune_qp6vjmt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, int64_t a1);
__attribute__((aligned(4))) void gm_f_update_pn3fq6a(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2venbiq(void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_6vfhl41(void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_write_all_o8ucaji(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jltv6p4(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_vue6tin(void * a0, const void * a1);
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b);
GM_NOINLINE bool gm_pf454qfrpt6r(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
extern void *const gm_hl8alodt65ii[];
extern void *const gm_hlgmpdevos39[];
extern void *const gm_hlsvrvumnj1d[];
extern uint8_t gm_k2boi01efds[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0pi0emdqd3vb[2];
extern const uint8_t gm_s2cgbvfh1b7rp[6];
extern const uint8_t gm_s2gnpj7k4l22n[2];
extern const uint8_t gm_s2nj060fs0sf4[6];
extern const uint8_t gm_s39mljh669qgl[2];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s4hpf7ii3vj1c[4];
extern const uint8_t gm_s4vd6e3mkiiso[3];
extern const uint8_t gm_s4vnlpe2ovpj8[6];
extern const uint8_t gm_s65ir5du2gd8b[9];
extern const uint8_t gm_s65qt8qo64cje[3];
extern const uint8_t gm_s7278rr8ss873[19];
extern const uint8_t gm_s8p032eorpf80[3];
extern const uint8_t gm_s9ab1ohnfcjkc[6];
extern const uint8_t gm_sa23fj423hlk4[6];
extern const uint8_t gm_sbkvlhvo2a3ic[7];
extern const uint8_t gm_se0f2jka437t4[2];
extern const uint8_t gm_sfbtgpom3l6k2[5];
extern const uint8_t gm_sgcf9cdlaf08n[6];
extern const uint8_t gm_shl253arneuel[19];
extern const uint8_t gm_si698b9virvoh[7];
extern const uint8_t gm_sj83d90uvga1k[8];
extern const uint8_t gm_skou19srual26[10];
extern const uint8_t gm_sm8uf6b4m0n7p[8];
extern const uint8_t gm_smaqn3dnro8sl[14];
extern const uint8_t gm_smjmbu09uoast[7];
extern const uint8_t gm_sni6hcs91b3qh[13];
extern const uint8_t gm_snqvo5bgc2fch[8];
extern const uint8_t gm_so99jjk23jgg8[5];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_src4rbevsr9gjng[];
extern const char gm_src7u0d314efpdm[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srd2ukct5o1qh[6];
extern const uint8_t gm_ssm3bl5los9iv[5];
extern const uint8_t gm_ssmr7jgj9e42t[25];
extern const uint8_t gm_st3m5iduoqh8b[5];
extern const uint8_t gm_stafskn6bv24f[2];
extern const uint8_t gm_stlamkovo4l42[4];
extern const uint8_t gm_stv6f87bn9v0e[7];
extern const uint8_t gm_suvsl6ie8luas[15];
extern const gm_tmpl gm_tpl0eumfheo3vtt;
extern const gm_tmpl gm_tpl1a3usdkusvtq;
extern const gm_tmpl gm_tpl33jf6q62dk07;
extern const gm_tmpl gm_tpl60i3afd6fgrq;
extern const gm_tmpl gm_tpl9aakii48cq8t;
extern const gm_tmpl gm_tple59d4hgjgp3n;
extern const gm_tmpl gm_tplepdvp8ofjal7;
extern const gm_tmpl gm_tpljtaapes6rh2u;
extern const gm_tmpl gm_tplq11qqgpg1tv1;
extern const gm_tmpl gm_tplurleasos6378;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline __attribute__((aligned(4))) void gm_f_clear_h4369ui(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_h4369ui F;
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
    GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t4 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8));
    GM_ST(gm_b16_8, ((uint8_t *)t2), t9);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) void gm_f_deinit_r70pji5(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_r70pji5 F;
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
        uint64_t r15; bool o16 = __builtin_mul_overflow(((uint64_t)1ULL), t14, &r15);
        if (o16) gm_trap("integer overflow in `*`", gm_srcrgc9bkquf6nr, 146, 64);
        uint64_t t17 = r15;
        void * t18 = t7;
        gm_b0_1 t19;
        ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t18))(t9, t12, t17, ((uint64_t)1ULL));
    }
    void * t20 = gm_l0;
    gm_b16_8 t21 = gm_f_empty_slice_ijtp9lo();
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_4pgooq2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_4pgooq2 F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_c438h0n(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_c438h0n F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 137u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    uint8_t gm_l3;
    void * t2 = gm_l0;
    int64_t t3 = GM_LD(int64_t, ((uint8_t *)t2));
    void * t4 = gm_l1;
    int64_t t5 = GM_LD(int64_t, ((uint8_t *)t4));
    uint8_t t6 = (t3 < t5);
    if (t6) {
        gm_l2 = ((uint8_t)1);
    } else {
        void * t7 = gm_l0;
        int64_t t8 = GM_LD(int64_t, ((uint8_t *)t7));
        void * t9 = gm_l1;
        int64_t t10 = GM_LD(int64_t, ((uint8_t *)t9));
        uint8_t t11 = ((t8 == t10));
        if (t11) {
            void * t12 = gm_l0;
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)t12) + 8));
            void * t14 = gm_l1;
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)t14) + 8));
            uint8_t t16 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t13), ((uint8_t *)&t15));
            gm_l3 = t16;
        } else {
            gm_l3 = ((uint8_t)0);
        }
        uint8_t t17 = gm_l3;
        gm_l2 = t17;
    }
    uint8_t t18 = gm_l2;
    F.ret = t18;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_format_3pfhto5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_3pfhto5 F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sni6hcs91b3qh));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(12));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplq11qqgpg1tv1), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_c31q7f8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_c31q7f8 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s2nj060fs0sf4));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(5));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpljtaapes6rh2u), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_c8gbrg9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_c8gbrg9 F;
    gm_t3877pk7lv748 gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s65ir5du2gd8b));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(8));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_2kt92vg((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl1a3usdkusvtq), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_cncadt9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_cncadt9 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssm3bl5los9iv));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplepdvp8ofjal7), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_gum3rpo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_gum3rpo F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smjmbu09uoast));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl60i3afd6fgrq), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_n1il1vf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_n1il1vf F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sm8uf6b4m0n7p));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl33jf6q62dk07), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_nk7pf4i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_nk7pf4i F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssm3bl5los9iv));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplepdvp8ofjal7), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_pf7dqps(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_pf7dqps F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_so99jjk23jgg8));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl0eumfheo3vtt), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_soc2hp6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_soc2hp6 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_so99jjk23jgg8));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl0eumfheo3vtt), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_vhqrhad(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_vhqrhad F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sj83d90uvga1k));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplurleasos6378), t3);
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
inline __attribute__((aligned(4))) void gm_f_init_s32ien0(void *gm_out) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_init_s32ien0 F;
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tg8e8rhc3qio9 t2;
    GM_ST(uint32_t, ((uint8_t *)&t2 + 0), ((uint32_t)1779033703ULL));
    GM_ST(uint32_t, ((uint8_t *)&t2 + 4), ((uint32_t)3144134277ULL));
    GM_ST(uint32_t, ((uint8_t *)&t2 + 8), ((uint32_t)1013904242ULL));
    GM_ST(uint32_t, ((uint8_t *)&t2 + 12), ((uint32_t)2773480762ULL));
    GM_ST(uint32_t, ((uint8_t *)&t2 + 16), ((uint32_t)1359893119ULL));
    GM_ST(uint32_t, ((uint8_t *)&t2 + 20), ((uint32_t)2600822924ULL));
    GM_ST(uint32_t, ((uint8_t *)&t2 + 24), ((uint32_t)528734635ULL));
    GM_ST(uint32_t, ((uint8_t *)&t2 + 28), ((uint32_t)1541459225ULL));
    gm_tsn02dhhitgs4 t3;
    for (size_t i4 = 0; i4 < 64; i4++) GM_ST(uint8_t, (uint8_t *)&t3 + i4 * 1, ((uint8_t)0ULL));
    GM_ST(gm_tg8e8rhc3qio9, ((uint8_t *)&(*(gm_tq14qvifgdvpm *)gm_out) + 0), t2);
    GM_ST(gm_tsn02dhhitgs4, ((uint8_t *)&(*(gm_tq14qvifgdvpm *)gm_out) + 32), t3);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_tq14qvifgdvpm *)gm_out) + 96), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_tq14qvifgdvpm *)gm_out) + 104), ((uint64_t)0ULL));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_clang_7du2shk(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_clang_7du2shk F;
    gm_th2lqbo77pvsc gm_l0; /* version */
    gm_b16_8 gm_l1; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t2 = (uint8_t)((((uint8_t *)a0))[16] != 0);
    if (t2) {
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)a0));
        gm_l1 = t3;
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    gm_b16_8 t4 = gm_l1;
    gm_b16_8 t5;
    GM_ST(void *, (uint8_t *)&t5, (void *)(gm_smaqn3dnro8sl));
    GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(13));
    uint8_t t6 = gm_f_contains_gkki4kq(t4, t5);
    F.ret = t6;
    goto gm_fret;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_gcc_epb17a6(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_gcc_epb17a6 F;
    gm_th2lqbo77pvsc gm_l0; /* version */
    gm_b16_8 gm_l1; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t gm_l2;
    uint8_t t2 = (uint8_t)((((uint8_t *)a0))[16] != 0);
    if (t2) {
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)a0));
        gm_l1 = t3;
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    gm_b16_8 t4 = gm_l1;
    gm_b16_8 t5;
    GM_ST(void *, (uint8_t *)&t5, (void *)(gm_ssmr7jgj9e42t));
    GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(24));
    uint8_t t6 = gm_f_contains_gkki4kq(t4, t5);
    if (t6) {
        gm_b16_8 t7 = gm_l1;
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(gm_s2cgbvfh1b7rp));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(5));
        uint8_t t9 = gm_f_contains_gkki4kq(t7, t8);
        uint8_t t10 = (uint8_t)!t9;
        gm_l2 = t10;
    } else {
        gm_l2 = ((uint8_t)0);
    }
    uint8_t t11 = gm_l2;
    F.ret = t11;
    goto gm_fret;
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
inline __attribute__((aligned(4))) void gm_f_list_vuor028(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vuor028 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_4pgooq2();
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
inline __attribute__((aligned(4))) void gm_f_no_link_a2212mt(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_no_link_a2212mt F;
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t2;
    gm_f_list_epvducg((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t3;
    gm_f_list_epvducg((void *)&t3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t4 = gm_f_empty_slice_5nn7ph0();
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
    gm_b16_8 t6 = gm_f_empty_slice_5nn7ph0();
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_tk2lc304hfpci *)gm_out) + 0), t2);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_tk2lc304hfpci *)gm_out) + 40), t3);
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tk2lc304hfpci *)gm_out) + 80), t5);
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tk2lc304hfpci *)gm_out) + 96), t7);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_4u3hstp(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_4u3hstp F;
    void * gm_l0; /* l */
    gm_thgmovcqlsivv gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_4pgooq2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_d2hchu8(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_d2hchu8(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_d2hchu8 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_sha256_hex_4vhcm7o(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_sha256_hex_4vhcm7o F;
    gm_b16_8 gm_l0; /* data */
    gm_t7mlrlibdq1ja gm_l1; /* d */
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_t7mlrlibdq1ja t3;
    gm_f_sum_ugu720i((void *)&t3, t2);
    gm_l1 = t3;
    void * t4 = (void *)((uint8_t *)&gm_l1);
    uint64_t t5 = ((uintptr_t)((uint64_t)32ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t6;
    GM_ST(void *, (uint8_t *)&t6, (void *)(((uint8_t *)t4 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(t5));
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
    gm_b16_8 t8 = gm_f_hex_euih242((void *)NULL, cx, NULL, t7);
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
inline __attribute__((aligned(4))) void gm_f_sort_by_ci5a9mg(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_by_ci5a9mg F;
    gm_b16_8 gm_l0; /* s */
    gm_closure gm_l1; /* less */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t2 = gm_l0;
    gm_closure t3 = gm_l1;
    gm_f_introsort_scs0qol(t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_sum_ugu720i(void *gm_out, gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sum_ugu720i F;
    gm_b16_8 gm_l0; /* data */
    GM_STACK_CHECK_SHALLOW(&F, 160u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_tq14qvifgdvpm t2;
        gm_f_init_s32ien0((void *)&t2);
        F.l1 = t2;
    }
    void * t3 = (void *)((uint8_t *)&F.l1);
    gm_b16_8 t4 = gm_l0;
    gm_f_update_pn3fq6a(t3, t4);
    {
        void * t5 = (void *)((uint8_t *)&F.l1);
        gm_f_final_rg5ko3p((void *)&(*(gm_t7mlrlibdq1ja *)gm_out), t5);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_temporary_object_tji353n(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1) {
    struct gm_fr_temporary_object_tji353n F;
    gm_b16_8 gm_l0; /* exe */
    uint64_t gm_l1; /* i */
    GM_STACK_CHECK(&F, 161u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l2;
    {
        gm_b16_8 t2 = gm_l0;
        uint64_t t3 = gm_l1;
        gm_t3877pk7lv748 t4;
        GM_ST(gm_b16_8, ((uint8_t *)&t4 + 0), t2);
        GM_ST(uint64_t, ((uint8_t *)&t4 + 16), t3);
        gm_f_format_c8gbrg9((void *)&gm_l2, (void *)NULL, cx, NULL, &t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t5 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t5) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 8, gm_src7u0d314efpdm, 219, 76);
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jltv6p4(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_jltv6p4 F;
    void * gm_l0; /* l */
    gm_tf1bjg81u0glv gm_l1; /* args */
    GM_STACK_CHECK(&F, 62u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sgcf9cdlaf08n));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
    gm_tftb1odc739t5 t4 = gm_f_write_6vfhl41(t2, &(*(gm_tf1bjg81u0glv *)((uint8_t *)a1)), ((void *)&gm_tple59d4hgjgp3n), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_vue6tin(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_vue6tin F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_si698b9virvoh));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_tftb1odc739t5 t4 = gm_f_write_2venbiq(t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tpl9aakii48cq8t), t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_adopt_object_psc9tpi(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_adopt_object_psc9tpi F;
    gm_b16_8 gm_l0; /* source */
    gm_b16_8 gm_l1; /* dir */
    gm_b16_8 gm_l2; /* stem */
    gm_b16_8 gm_l3; /* ahead */
    gm_b16_8 gm_l4; /* from */
    gm_b16_8 gm_l5; /* tmp */
    GM_STACK_CHECK(&F, 770u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_th2lqbo77pvsc gm_l6;
    gm_b16_8 gm_l7;
    gm_tabb710d9mq2e gm_l8;
    gm_ttln0d0hd70en gm_l9;
    uint8_t gm_l10;
    gm_tabb710d9mq2e gm_l11;
    gm_ttsm01t0d8srg gm_l12;
    {
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_s7278rr8ss873));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(18));
        gm_node *pn6 = gm_find(cx, 621690230);
        if (!pn6) gm_trap("no handler installed for effect `Env`", gm_src4rbevsr9gjng, 111, 14);
        gm_th2lqbo77pvsc t7;
        if (pn6->kind == GM_NATIVE) {
            const uint8_t *ev; size_t evn;
            bool evok = gm_env_var(GM_LD(void *, &t5), GM_LD(uintptr_t, (uint8_t *)&t5 + 8), &ev, &evn);
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(ev));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(evn));
            gm_th2lqbo77pvsc t9;
            GM_ST(uint8_t, (uint8_t *)&t9 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t9 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t9 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t9), t8);
            ((uint8_t *)&t9)[16] = 1;
            gm_th2lqbo77pvsc t10;
            memset(&t10, 0, sizeof t10);
            t7 = evok ? t9 : t10;
        } else {
            ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn6->clauses[1])((void *)&t7, pn6->env, pn6->hctx, pn6, t5);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t7;
    }
    uint8_t t11 = (uint8_t)((((uint8_t *)&gm_l6))[16] != 0);
    if (t11) {
        gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
        gm_l3 = t12;
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint8_t t14 = ((t13 == ((uint64_t)0ULL)));
    if (t14) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    {
        gm_b16_8 t15 = gm_l3;
        gm_b16_8 t16 = gm_l0;
        gm_b16_8 t17 = gm_f_sha256_hex_4vhcm7o((void *)NULL, cx, NULL, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t17;
        void * t18 = GM_LD(void *, ((uint8_t *)&gm_l7));
        uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
        uint64_t t20 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t21 = (uintptr_t)((uint64_t)16ULL);
        if (t20 > t21 || t21 > (uintptr_t)t19) gm_trap_slice(t20, t21, t19, gm_src4rbevsr9gjng, 113, 49);
        uint64_t t22 = ((uintptr_t)((uint64_t)16ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(((uint8_t *)t18 + (uintptr_t)((uint64_t)0ULL) * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(t22));
        gm_tnpifv20irnph t24;
        GM_ST(gm_b16_8, ((uint8_t *)&t24 + 0), t23);
        gm_f_format_pf7dqps((void *)&gm_l8, (void *)NULL, cx, NULL, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t25 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
        if (!t25) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_src4rbevsr9gjng, 113, 30);
        gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
        gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
        gm_b16_8 t28 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t15, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t28;
    }
    gm_b16_8 t29 = gm_l4;
    uint8_t t30 = gm_f_is_file_tcuv9rv((void *)NULL, cx, NULL, t29);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t31 = (uint8_t)!t30;
    if (t31) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    gm_b16_8 t32 = gm_l1;
    gm_b16_8 t33 = gm_l2;
    gm_b16_8 t34;
    GM_ST(void *, (uint8_t *)&t34, (void *)(gm_se0f2jka437t4));
    GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(1));
    gm_b16_8 t35 = gm_f_private_name_u7i0rip((void *)NULL, cx, NULL, t32, t33, t34);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t35;
    gm_b16_8 t36 = gm_l4;
    gm_b16_8 t37 = gm_l5;
    gm_ttln0d0hd70en t38 = gm_f_copy_fg1ge35((void *)NULL, cx, NULL, t36, t37);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l9 = t38;
    uint8_t t39 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (t39) {
    } else {
        gm_b16_8 t40 = gm_l5;
        gm_node *pn41 = gm_find(cx, 2126215853);
        if (!pn41) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 117, 13);
        gm_ttln0d0hd70en t42;
        t42 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn41->clauses[6])(pn41->env, pn41->hctx, pn41, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    {
        gm_b16_8 t43 = gm_l5;
        gm_b16_8 t44 = gm_l1;
        gm_b16_8 t45 = gm_l2;
        gm_tnpifv20irnph t46;
        GM_ST(gm_b16_8, ((uint8_t *)&t46 + 0), t45);
        gm_f_format_pf7dqps((void *)&gm_l11, (void *)NULL, cx, NULL, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t47 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
        if (!t47) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_src4rbevsr9gjng, 120, 49);
        gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
        gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
        gm_b16_8 t50 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t44, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttsm01t0d8srg t51 = gm_f_rename_or_remove_l80dbfs((void *)NULL, cx, NULL, t43, t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l12 = t51;
    }
    {
        {
            uint8_t t52 = (uint8_t)((((uint8_t *)&gm_l12))[0] != 0);
            if (t52) {
                goto gm_lb2;
            }
            gm_l10 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l10 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_src4rbevsr9gjng, 120, 12);
    }
gm_lb1:;
    uint8_t t53 = gm_l10;
    F.ret = t53;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_cache_key_tf6ebrd(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_cache_key_tf6ebrd F;
    gm_b16_8 gm_l0; /* parts */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* p */
    GM_STACK_CHECK(&F, 414u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l5;
    gm_b16_8 gm_l6;
    {
        gm_trqfvmjtt4sij t9;
        gm_f_list_uvfu1k1((void *)&t9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t9;
        gm_b16_8 t10 = gm_l0;
        gm_l2 = t10;
        gm_l3 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t11 = gm_l3;
                uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint8_t t13 = (t11 < t12);
                if (!t13) {
                    goto gm_lb2;
                }
                {
                    void * t14 = GM_LD(void *, ((uint8_t *)&gm_l2));
                    uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                    uint64_t t16 = gm_l3;
                    {
                        {
                            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)t14) + (uintptr_t)t16 * 16));
                            gm_l4 = t17;
                            void * t18 = (void *)((uint8_t *)&F.l1);
                            uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                            gm_b16_8 t20 = gm_l4;
                            gm_tf1bjg81u0glv t21;
                            GM_ST(uint64_t, ((uint8_t *)&t21 + 0), t19);
                            GM_ST(gm_b16_8, ((uint8_t *)&t21 + 8), t20);
                            gm_tftb1odc739t5 t22 = gm_f_write_jltv6p4(t18, &t21);
                            gm_l5 = t22;
                            uint8_t t23 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
                            if (!t23) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_src4rbevsr9gjng, 32, 22);
                            goto gm_lb5;
                        }
gm_lb6:;
                        gm_trap("no match arm matched the scrutinee", gm_src4rbevsr9gjng, 32, 5);
                    }
gm_lb5:;
                }
gm_lb4:;
                uint64_t t24 = gm_l3;
                uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
                uint64_t t27 = r25;
                gm_l3 = t27;
            }
        }
gm_lb2:;
        gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
        gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
        gm_b16_8 t30 = gm_f_sha256_hex_4vhcm7o((void *)NULL, cx, NULL, t29);
        if (gm_unw.kind) {
            goto gm_ld8_1;
        }
        gm_l6 = t30;
        void * t31 = GM_LD(void *, ((uint8_t *)&gm_l6));
        uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
        uint64_t t33 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t34 = (uintptr_t)((uint64_t)16ULL);
        if (t33 > t34 || t34 > (uintptr_t)t32) gm_trap_slice(t33, t34, t32, gm_src4rbevsr9gjng, 33, 12);
        uint64_t t35 = ((uintptr_t)((uint64_t)16ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t36;
        GM_ST(void *, (uint8_t *)&t36, (void *)(((uint8_t *)t31 + (uintptr_t)((uint64_t)0ULL) * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(t35));
        F.ret = t36;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
        goto gm_ld8_1;
    }
    {
        void * t37 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_r70pji5(t37);
    }
    goto gm_ls38;
gm_ld8_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t39 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_r70pji5(t39);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls38:;
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
inline __attribute__((aligned(4))) void gm_f_compiler_version_hj63jgf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_compiler_version_hj63jgf F;
    void * gm_l0; /* tc */
    GM_STACK_CHECK(&F, 164u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l3;
    gm_tftb1odc739t5 gm_l4;
    {
        gm_trqfvmjtt4sij t2;
        gm_f_list_epvducg((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t2;
    }
    void * t3 = (void *)((uint8_t *)&F.l1);
    void * t4 = gm_l0;
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)t4));
    gm_tftb1odc739t5 t6 = gm_f_extend_vogsd72(t3, t5);
    gm_l3 = t6;
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t7) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_src4rbevsr9gjng, 40, 5);
    void * t8 = (void *)((uint8_t *)&F.l1);
    gm_b16_8 t9;
    GM_ST(void *, (uint8_t *)&t9, (void *)(gm_skou19srual26));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(9));
    gm_tftb1odc739t5 t10 = gm_f_push_3ef746b(t8, t9);
    gm_l4 = t10;
    uint8_t t11 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t11) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_src4rbevsr9gjng, 41, 5);
    {
        gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
        gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
        gm_f_run_mj5dobu((void *)&F.l2, (void *)NULL, cx, NULL, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t14 = GM_LD(uint8_t, ((uint8_t *)&F.l2));
    uint8_t t15 = (uint8_t)!t14;
    if (t15) {
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
    {
        gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t16);
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
inline __attribute__((aligned(4))) uint8_t gm_f_deps_current_jlls0pm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_deps_current_jlls0pm F;
    gm_b16_8 gm_l0; /* path */
    gm_b16_8 gm_l1; /* listing */
    gm_b16_8 gm_l2; /* <tmp> */
    GM_STACK_CHECK(&F, 155u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tgvh15qshf643 gm_l3;
    gm_b16_8 t3 = gm_l0;
    gm_f_read_all_r9rh9qd((void *)&gm_l3, (void *)NULL, cx, NULL, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t4 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (t4) {
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
        gm_l1 = t5;
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    gm_b16_8 t6 = gm_l1;
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
    gm_l2 = t7;
    {
        gm_node wn8 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlsvrvumnj1d, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t11 = gm_l2;
        gm_b0_1 t12 = gm_f_lines_9uns0k2_s025qou((void *)NULL, (&wn8), NULL, t11);
        if (gm_unw.kind) {
            goto gm_ll10;
        }
    }
    goto gm_ls13;
gm_ll10:;
    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we9; }
    goto gm_lad;
gm_ls13:;
gm_lb1:;
we9:;
    F.ret = ((uint8_t)1);
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
inline __attribute__((aligned(4))) void gm_f_deps_listing_7genqb0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_deps_listing_7genqb0 F;
    gm_b16_8 gm_l0; /* rule */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* p */
    gm_b16_8 gm_l5; /* text */
    GM_STACK_CHECK(&F, 511u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_trqfvmjtt4sij gm_l6;
    gm_tgvh15qshf643 gm_l7;
    gm_b16_8 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    {
        gm_trqfvmjtt4sij t7;
        gm_f_list_uvfu1k1((void *)&t7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t7;
    }
    {
        gm_b16_8 t8 = gm_l0;
        gm_f_make_prerequisites_v713689((void *)&gm_l6, (void *)NULL, cx, NULL, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
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
                        gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)t13) + (uintptr_t)t15 * 16));
                        gm_l4 = t16;
                        {
                            gm_b16_8 t17 = gm_l4;
                            gm_f_read_all_r9rh9qd((void *)&gm_l7, (void *)NULL, cx, NULL, t17);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                        if (t18) {
                            gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
                            gm_l5 = t19;
                        } else {
                            memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
                            goto gm_fdone;
                        }
                        {
                            void * t20 = (void *)((uint8_t *)&F.l1);
                            gm_b16_8 t21 = gm_l5;
                            gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
                            gm_b16_8 t23 = gm_f_sha256_hex_4vhcm7o((void *)NULL, cx, NULL, t22);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l8 = t23;
                            void * t24 = GM_LD(void *, ((uint8_t *)&gm_l8));
                            uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                            uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t27 = (uintptr_t)((uint64_t)16ULL);
                            if (t26 > t27 || t27 > (uintptr_t)t25) gm_trap_slice(t26, t27, t25, gm_src4rbevsr9gjng, 425, 30);
                            uint64_t t28 = ((uintptr_t)((uint64_t)16ULL)) - (uintptr_t)((uint64_t)0ULL);
                            gm_b16_8 t29;
                            GM_ST(void *, (uint8_t *)&t29, (void *)(((uint8_t *)t24 + (uintptr_t)((uint64_t)0ULL) * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(t28));
                            gm_b16_8 t30 = gm_l4;
                            gm_t6j6smj7qoctu t31;
                            GM_ST(gm_b16_8, ((uint8_t *)&t31 + 0), t29);
                            GM_ST(gm_b16_8, ((uint8_t *)&t31 + 16), t30);
                            gm_tftb1odc739t5 t32 = gm_f_write_vue6tin(t20, &t31);
                            gm_l9 = t32;
                        }
                        uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                        if (!t33) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_src4rbevsr9gjng, 425, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src4rbevsr9gjng, 423, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t34 = gm_l3;
            uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
            uint64_t t37 = r35;
            gm_l3 = t37;
        }
    }
gm_lb1:;
    {
        gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t38);
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
extern __attribute__((aligned(4))) uint8_t gm_f_fn_c438h0n(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1b63vnj(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_1b63vnj F;
    gm_te2n4j2vtm53d gm_l0; /* <item> */
    gm_te2n4j2vtm53d gm_l1; /* r */
    gm_tibupt4dmcqai gm_l2; /* e */
    gm_b16_8 gm_l3; /* p */
    gm_t5j14m8ru132p gm_l4; /* info */
    GM_STACK_CHECK(&F, 488u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&gm_l0, a0, sizeof gm_l0);
    gm_tih6h4avv5nrb gm_l5;
    gm_tftb1odc739t5 gm_l6;
    {
        {
            {
                memmove(((uint8_t *)&gm_l1), ((uint8_t *)&gm_l0), 32);
            }
            uint8_t t4 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
            if (t4) {
                memmove(((uint8_t *)&gm_l2), (((uint8_t *)&gm_l1) + 8), 24);
            } else {
                gm_unw.kind = GM_BREAK; gm_unw.frame = inst->with_frame; gm_unw.label = inst->with_label; gm_unw.is_err = 0;
                goto gm_lad;
            }
            gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
            gm_b16_8 t6;
            GM_ST(void *, (uint8_t *)&t6, (void *)(gm_s4vnlpe2ovpj8));
            GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(5));
            uint8_t t7 = gm_f_starts_with_6vvt01e(t5, t6);
            uint8_t t8 = (uint8_t)!t7;
            if (t8) {
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_prune_units_iqs7tqd *)((&F.h)->up))->l0));
            gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
            gm_b16_8 t11 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t9, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l3 = t11;
            {
                gm_b16_8 t12 = gm_l3;
                gm_node *pn13 = gm_find(cx, 2126215853);
                if (!pn13) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 250, 17);
                gm_tih6h4avv5nrb t14;
                ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn13->clauses[13])((void *)&t14, pn13->env, pn13->hctx, pn13, t12);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l5 = t14;
            }
            uint8_t t15 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
            if (t15) {
                memmove(((uint8_t *)&gm_l4), (((uint8_t *)&gm_l5) + 8), 32);
            } else {
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
            gm_b16_8 t17;
            GM_ST(void *, (uint8_t *)&t17, (void *)(gm_srd2ukct5o1qh));
            GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(5));
            uint8_t t18 = gm_f_contains_gkki4kq(t16, t17);
            if (t18) {
                int64_t t19 = GM_LD(int64_t, (((uint8_t *)&gm_l4) + 16));
                int64_t t20 = GM_LD(int64_t, ((uint8_t *)&((struct gm_fr_prune_units_iqs7tqd *)((&F.h)->up))->l2));
                uint8_t t21 = (t19 < t20);
                if (t21) {
                    gm_b16_8 t22 = gm_l3;
                    gm_node *pn23 = gm_find(cx, 2126215853);
                    if (!pn23) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 252, 44);
                    gm_ttln0d0hd70en t24;
                    t24 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn23->clauses[6])(pn23->env, pn23->hctx, pn23, t22);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
            gm_b16_8 t26;
            GM_ST(void *, (uint8_t *)&t26, (void *)(gm_s8p032eorpf80));
            GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(2));
            uint8_t t27 = gm_f_ends_with_biriu5l(t25, t26);
            if (t27) {
                void * t28 = (void *)((uint8_t *)&((struct gm_fr_prune_units_iqs7tqd *)((&F.h)->up))->l3);
                int64_t t29 = GM_LD(int64_t, (((uint8_t *)&gm_l4) + 16));
                gm_b16_8 t30 = gm_l3;
                gm_thgmovcqlsivv t31;
                GM_ST(int64_t, ((uint8_t *)&t31 + 0), t29);
                GM_ST(gm_b16_8, ((uint8_t *)&t31 + 8), t30);
                gm_tftb1odc739t5 t32 = gm_f_push_4u3hstp(t28, &t31);
                gm_l6 = t32;
                uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                if (!t33) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_src4rbevsr9gjng, 255, 37);
            }
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_src4rbevsr9gjng, 246, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_6ellthc(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_6ellthc F;
    gm_te2n4j2vtm53d gm_l0; /* <item> */
    gm_te2n4j2vtm53d gm_l1; /* r */
    gm_tibupt4dmcqai gm_l2; /* e */
    gm_b16_8 gm_l3; /* p */
    gm_t5j14m8ru132p gm_l4; /* info */
    GM_STACK_CHECK(&F, 455u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&gm_l0, a0, sizeof gm_l0);
    gm_tih6h4avv5nrb gm_l5;
    gm_tftb1odc739t5 gm_l6;
    {
        {
            {
                memmove(((uint8_t *)&gm_l1), ((uint8_t *)&gm_l0), 32);
            }
            uint8_t t4 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
            if (t4) {
                memmove(((uint8_t *)&gm_l2), (((uint8_t *)&gm_l1) + 8), 24);
            } else {
                gm_unw.kind = GM_BREAK; gm_unw.frame = inst->with_frame; gm_unw.label = inst->with_label; gm_unw.is_err = 0;
                goto gm_lad;
            }
            gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
            gm_b16_8 t6;
            GM_ST(void *, (uint8_t *)&t6, (void *)(gm_s9ab1ohnfcjkc));
            GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(5));
            uint8_t t7 = gm_f_starts_with_6vvt01e(t5, t6);
            uint8_t t8 = (uint8_t)!t7;
            if (t8) {
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_prune_eu93ghe *)((&F.h)->up))->l0));
            gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
            gm_b16_8 t11 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t9, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l3 = t11;
            {
                gm_b16_8 t12 = gm_l3;
                gm_node *pn13 = gm_find(cx, 2126215853);
                if (!pn13) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 147, 17);
                gm_tih6h4avv5nrb t14;
                ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn13->clauses[13])((void *)&t14, pn13->env, pn13->hctx, pn13, t12);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l5 = t14;
            }
            uint8_t t15 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
            if (t15) {
                memmove(((uint8_t *)&gm_l4), (((uint8_t *)&gm_l5) + 8), 32);
            } else {
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
            gm_b16_8 t17;
            GM_ST(void *, (uint8_t *)&t17, (void *)(gm_srd2ukct5o1qh));
            GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(5));
            uint8_t t18 = gm_f_contains_gkki4kq(t16, t17);
            if (t18) {
                int64_t t19 = GM_LD(int64_t, (((uint8_t *)&gm_l4) + 16));
                int64_t t20 = GM_LD(int64_t, ((uint8_t *)&((struct gm_fr_prune_eu93ghe *)((&F.h)->up))->l2));
                uint8_t t21 = (t19 < t20);
                if (t21) {
                    gm_b16_8 t22 = gm_l3;
                    gm_node *pn23 = gm_find(cx, 2126215853);
                    if (!pn23) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 149, 44);
                    gm_ttln0d0hd70en t24;
                    t24 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn23->clauses[6])(pn23->env, pn23->hctx, pn23, t22);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            {
                void * t25 = (void *)((uint8_t *)&((struct gm_fr_prune_eu93ghe *)((&F.h)->up))->l3);
                int64_t t26 = GM_LD(int64_t, (((uint8_t *)&gm_l4) + 16));
                gm_b16_8 t27 = gm_l3;
                gm_thgmovcqlsivv t28;
                GM_ST(int64_t, ((uint8_t *)&t28 + 0), t26);
                GM_ST(gm_b16_8, ((uint8_t *)&t28 + 8), t27);
                gm_tftb1odc739t5 t29 = gm_f_push_4u3hstp(t25, &t28);
                gm_l6 = t29;
            }
            uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
            if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_src4rbevsr9gjng, 152, 9);
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_src4rbevsr9gjng, 143, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_t2jmto9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_t2jmto9 F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    gm_t6j6smj7qoctu gm_l2; /* split */
    gm_b16_8 gm_l3; /* text */
    GM_STACK_CHECK(&F, 375u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_ttu09lij36f8c gm_l4;
    gm_tgvh15qshf643 gm_l5;
    gm_b16_8 gm_l6;
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
            {
                gm_b16_8 t7 = gm_l1;
                gm_b16_8 t8;
                GM_ST(void *, (uint8_t *)&t8, (void *)(gm_s3p0rbs9hc3lg));
                GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(1));
                gm_f_split_once_mqcu1ce((void *)&gm_l4, t7, t8);
            }
            uint8_t t9 = (uint8_t)((((uint8_t *)&gm_l4))[32] != 0);
            if (t9) {
                memmove(((uint8_t *)&gm_l2), ((uint8_t *)&gm_l4), 32);
            } else {
                uint8_t t10 = 0;
                ((struct gm_fr_deps_current_jlls0pm *)((&F.h)->up))->ret = ((uint8_t)0);
                gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t10;
                goto gm_lad;
            }
            {
                gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 16));
                gm_f_read_all_r9rh9qd((void *)&gm_l5, (void *)NULL, cx, NULL, t11);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
            if (t12) {
                gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
                gm_l3 = t13;
            } else {
                uint8_t t14 = 0;
                ((struct gm_fr_deps_current_jlls0pm *)((&F.h)->up))->ret = ((uint8_t)0);
                gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t14;
                goto gm_lad;
            }
            gm_b16_8 t15 = gm_l3;
            gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
            gm_b16_8 t17 = gm_f_sha256_hex_4vhcm7o((void *)NULL, cx, NULL, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t17;
            void * t18 = GM_LD(void *, ((uint8_t *)&gm_l6));
            uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint64_t t20 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t21 = (uintptr_t)((uint64_t)16ULL);
            if (t20 > t21 || t21 > (uintptr_t)t19) gm_trap_slice(t20, t21, t19, gm_src4rbevsr9gjng, 437, 12);
            uint64_t t22 = ((uintptr_t)((uint64_t)16ULL)) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(((uint8_t *)t18 + (uintptr_t)((uint64_t)0ULL) * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(t22));
            gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
            uint8_t t25 = (!gm_eq0iqiubfm7e(((uint8_t *)&t23), ((uint8_t *)&t24)));
            if (t25) {
                uint8_t t26 = 0;
                ((struct gm_fr_deps_current_jlls0pm *)((&F.h)->up))->ret = ((uint8_t)0);
                gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t26;
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_src4rbevsr9gjng, 433, 5);
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
extern __attribute__((aligned(4))) uint8_t gm_f_is_clang_7du2shk(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_clang_7du2shk_c(gm_th2lqbo77pvsc a0) { return gm_f_is_clang_7du2shk(&a0); }
extern __attribute__((aligned(4))) uint8_t gm_f_is_gcc_epb17a6(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_gcc_epb17a6_c(gm_th2lqbo77pvsc a0) { return gm_f_is_gcc_epb17a6(&a0); }
__attribute__((aligned(4))) void gm_f_make_prerequisites_v713689(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_make_prerequisites_v713689 F;
    gm_b16_8 gm_l0; /* rule */
    uint8_t gm_l3; /* in_target */
    uint64_t gm_l4; /* i */
    uint8_t gm_l5; /* c */
    uint8_t gm_l6; /* n */
    gm_b16_8 gm_l7; /* w */
    GM_STACK_CHECK(&F, 661u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l8;
    uint8_t gm_l9;
    uint8_t gm_l10;
    uint64_t gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_tftb1odc739t5 gm_l13;
    gm_tabb710d9mq2e gm_l14;
    gm_tftb1odc739t5 gm_l15;
    gm_tftb1odc739t5 gm_l16;
    {
        gm_trqfvmjtt4sij t9;
        gm_f_list_epvducg((void *)&t9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t9;
    }
    {
        gm_trqfvmjtt4sij t10;
        gm_f_list_uvfu1k1((void *)&t10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t10;
    }
    gm_l3 = ((uint8_t)1);
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t11 = gm_l4;
            uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t13 = (t11 <= t12);
            if (!t13) {
                goto gm_lb1;
            }
            {
                uint64_t t14 = gm_l4;
                uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint8_t t16 = (t14 < t15);
                if (t16) {
                    void * t17 = GM_LD(void *, ((uint8_t *)&gm_l0));
                    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                    uint64_t t19 = gm_l4;
                    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)t17) + (uintptr_t)t19 * 1));
                    gm_l5 = t20;
                } else {
                    gm_l5 = ((uint8_t)32ULL);
                }
                uint64_t t21 = gm_l4;
                uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
                if (o23) gm_trap("integer overflow in `+`", gm_src4rbevsr9gjng, 452, 9);
                uint64_t t24 = r22;
                gm_l4 = t24;
                {
                    {
                        uint8_t t25 = gm_l5;
                        uint8_t t26 = ((t25 == ((uint8_t)92ULL)));
                        if (!t26) {
                            goto gm_lb5;
                        }
                        uint64_t t27 = gm_l4;
                        uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                        uint8_t t29 = (t27 < t28);
                        if (!t29) {
                            goto gm_lb5;
                        }
                        void * t30 = GM_LD(void *, ((uint8_t *)&gm_l0));
                        uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                        uint64_t t32 = gm_l4;
                        uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)t30) + (uintptr_t)t32 * 1));
                        gm_l6 = t33;
                        uint8_t t34 = gm_l6;
                        uint8_t t35 = ((t34 == ((uint8_t)10ULL)));
                        if (t35) {
                            gm_l8 = ((uint8_t)1);
                        } else {
                            uint8_t t36 = gm_l6;
                            uint8_t t37 = ((t36 == ((uint8_t)13ULL)));
                            if (t37) {
                                uint64_t t38 = gm_l4;
                                uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
                                if (o40) gm_trap("integer overflow in `+`", gm_src4rbevsr9gjng, 455, 44);
                                uint64_t t41 = r39;
                                uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                uint8_t t43 = (t41 < t42);
                                gm_l10 = t43;
                            } else {
                                gm_l10 = ((uint8_t)0);
                            }
                            uint8_t t44 = gm_l10;
                            if (t44) {
                                void * t45 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                uint64_t t47 = gm_l4;
                                uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                                if (o49) gm_trap("integer overflow in `+`", gm_src4rbevsr9gjng, 455, 70);
                                uint64_t t50 = r48;
                                if ((uintptr_t)t50 >= (uintptr_t)(t46)) gm_trap_index((uintptr_t)t50, t46, gm_src4rbevsr9gjng, 455, 65);
                                uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)t45) + (uintptr_t)t50 * 1));
                                uint8_t t52 = ((t51 == ((uint8_t)10ULL)));
                                gm_l9 = t52;
                            } else {
                                gm_l9 = ((uint8_t)0);
                            }
                            uint8_t t53 = gm_l9;
                            gm_l8 = t53;
                        }
                        uint8_t t54 = gm_l8;
                        if (t54) {
                            uint64_t t55 = gm_l4;
                            uint8_t t56 = gm_l6;
                            uint8_t t57 = ((t56 == ((uint8_t)13ULL)));
                            if (t57) {
                                gm_l11 = ((uint64_t)2ULL);
                            } else {
                                gm_l11 = ((uint64_t)1ULL);
                            }
                            uint64_t t58 = gm_l11;
                            uint64_t r59; bool o60 = __builtin_add_overflow(t55, t58, &r59);
                            if (o60) gm_trap("integer overflow in `+`", gm_src4rbevsr9gjng, 456, 17);
                            uint64_t t61 = r59;
                            gm_l4 = t61;
                            gm_l5 = ((uint8_t)32ULL);
                        } else {
                            uint8_t t62 = gm_l6;
                            gm_toh0ootodhjue t63;
                            GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)32ULL));
                            GM_ST(uint8_t, ((uint8_t *)&t63 + 1), ((uint8_t)35ULL));
                            GM_ST(uint8_t, ((uint8_t *)&t63 + 2), ((uint8_t)92ULL));
                            uint8_t t64 = 0;
                            if (gm_eqjmccqdb90f(((uint8_t *)&t63) + 0, ((uint8_t *)&t62))) t64 = 1;
                            if (gm_eqjmccqdb90f(((uint8_t *)&t63) + 1, ((uint8_t *)&t62))) t64 = 1;
                            if (gm_eqjmccqdb90f(((uint8_t *)&t63) + 2, ((uint8_t *)&t62))) t64 = 1;
                            uint8_t t65 = (uint8_t)(t64);
                            if (t65) {
                                void * t66 = (void *)((uint8_t *)&F.l2);
                                uint8_t t67 = gm_l6;
                                gm_tftb1odc739t5 t68 = gm_f_push_t83rg2m(t66, t67);
                                gm_l12 = t68;
                                uint8_t t69 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                                if (!t69) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_src4rbevsr9gjng, 459, 17);
                                uint64_t t70 = gm_l4;
                                uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
                                if (o72) gm_trap("integer overflow in `+`", gm_src4rbevsr9gjng, 460, 17);
                                uint64_t t73 = r71;
                                gm_l4 = t73;
                                goto gm_lb3;
                            }
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    {
                        {
                            uint8_t t74 = gm_l5;
                            uint8_t t75 = ((t74 == ((uint8_t)36ULL)));
                            if (!t75) {
                                goto gm_lb7;
                            }
                            uint64_t t76 = gm_l4;
                            uint64_t t77 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                            uint8_t t78 = (t76 < t77);
                            if (!t78) {
                                goto gm_lb7;
                            }
                            void * t79 = GM_LD(void *, ((uint8_t *)&gm_l0));
                            uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                            uint64_t t81 = gm_l4;
                            uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)t79) + (uintptr_t)t81 * 1));
                            uint8_t t83 = ((t82 == ((uint8_t)36ULL)));
                            if (!t83) {
                                goto gm_lb7;
                            }
                            void * t84 = (void *)((uint8_t *)&F.l2);
                            gm_tftb1odc739t5 t85 = gm_f_push_t83rg2m(t84, ((uint8_t)36ULL));
                            gm_l13 = t85;
                            uint8_t t86 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                            if (!t86) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_src4rbevsr9gjng, 464, 13);
                            uint64_t t87 = gm_l4;
                            uint64_t r88; bool o89 = __builtin_add_overflow(t87, ((uint64_t)1ULL), &r88);
                            if (o89) gm_trap("integer overflow in `+`", gm_src4rbevsr9gjng, 465, 13);
                            uint64_t t90 = r88;
                            gm_l4 = t90;
                            goto gm_lb3;
                        }
gm_lb7:;
                    }
gm_lb6:;
                }
gm_lb4:;
                uint8_t t91 = gm_l5;
                gm_t2abnvcnt53ot t92;
                GM_ST(uint8_t, ((uint8_t *)&t92 + 0), ((uint8_t)32ULL));
                GM_ST(uint8_t, ((uint8_t *)&t92 + 1), ((uint8_t)9ULL));
                GM_ST(uint8_t, ((uint8_t *)&t92 + 2), ((uint8_t)10ULL));
                GM_ST(uint8_t, ((uint8_t *)&t92 + 3), ((uint8_t)13ULL));
                uint8_t t93 = 0;
                if (gm_eqjmccqdb90f(((uint8_t *)&t92) + 0, ((uint8_t *)&t91))) t93 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t92) + 1, ((uint8_t *)&t91))) t93 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t92) + 2, ((uint8_t *)&t91))) t93 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t92) + 3, ((uint8_t *)&t91))) t93 = 1;
                uint8_t t94 = (uint8_t)(t93);
                if (t94) {
                    uint64_t t95 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                    uint8_t t96 = (t95 > ((uint64_t)0ULL));
                    if (t96) {
                        {
                            gm_b16_8 t97 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
                            gm_b16_8 t98 = GM_LD(gm_b16_8, ((uint8_t *)&t97));
                            gm_f_dupe_g9iqpvn((void *)&gm_l14, (void *)NULL, cx, NULL, t98, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k2boi01efds)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t99 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                        if (!t99) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_src4rbevsr9gjng, 470, 22);
                        gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
                        gm_l7 = t100;
                        void * t101 = (void *)((uint8_t *)&F.l2);
                        gm_f_clear_h4369ui(t101);
                        uint8_t t102 = gm_l3;
                        if (t102) {
                            gm_b16_8 t103 = gm_l7;
                            gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&t103));
                            gm_b16_8 t105;
                            GM_ST(void *, (uint8_t *)&t105, (void *)(gm_s39mljh669qgl));
                            GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)(1));
                            uint8_t t106 = gm_f_ends_with_biriu5l(t104, t105);
                            if (t106) {
                                gm_l3 = ((uint8_t)0);
                            }
                        } else {
                            void * t107 = (void *)((uint8_t *)&F.l1);
                            gm_b16_8 t108 = gm_l7;
                            gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)&t108));
                            gm_tftb1odc739t5 t110 = gm_f_push_3ef746b(t107, t109);
                            gm_l15 = t110;
                            uint8_t t111 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                            if (!t111) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_src4rbevsr9gjng, 475, 21);
                        }
                    }
                    goto gm_lb3;
                }
                void * t112 = (void *)((uint8_t *)&F.l2);
                uint8_t t113 = gm_l5;
                gm_tftb1odc739t5 t114 = gm_f_push_t83rg2m(t112, t113);
                gm_l16 = t114;
                uint8_t t115 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                if (!t115) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_src4rbevsr9gjng, 480, 9);
            }
gm_lb3:;
        }
    }
gm_lb1:;
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 40);
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
__attribute__((aligned(4))) void gm_f_package_link_q0071pm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, uint8_t a3, uint8_t a4) {
    struct gm_fr_package_link_q0071pm F;
    void * gm_l0; /* extras */
    void * gm_l1; /* tc */
    gm_b16_8 gm_l2; /* exe */
    uint8_t gm_l3; /* debug */
    uint8_t gm_l4; /* debug_info */
    gm_th2lqbo77pvsc gm_l6; /* version */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    gm_t6j6smj7qoctu gm_l10; /* src */
    gm_b16_8 gm_l12; /* obj */
    gm_b16_8 gm_l13; /* obj */
    gm_b16_8 gm_l14; /* e */
    GM_STACK_CHECK(&F, 1063u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_t1l3r16b8478v gm_l15;
    gm_th2lqbo77pvsc gm_l16;
    gm_tftb1odc739t5 gm_l17;
    gm_tl572d7579hns gm_l18;
    gm_tftb1odc739t5 gm_l19;
    gm_tftb1odc739t5 gm_l20;
    {
        gm_tk2lc304hfpci t9;
        gm_f_no_link_a2212mt((void *)&t9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l5 = t9;
    }
    void * t10 = gm_l0;
    gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)t10) + 40));
    gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
    GM_ST(gm_b16_8, (((uint8_t *)&F.l5) + 80), t12);
    void * t13 = gm_l0;
    gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t13) + 80));
    gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
    GM_ST(gm_b16_8, (((uint8_t *)&F.l5) + 96), t15);
    void * t16 = gm_l0;
    uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t16) + 8));
    uint8_t t18 = (t17 > ((uint64_t)0ULL));
    if (t18) {
        void * t19 = gm_l1;
        gm_f_compiler_version_hj63jgf((void *)&gm_l6, (void *)NULL, cx, NULL, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    } else {
        memset(&gm_l6, 0, sizeof gm_l6);
    }
    void * t20 = gm_l0;
    gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)t20));
    gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
    gm_l7 = t22;
    gm_l8 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t23 = gm_l8;
            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
            uint8_t t25 = (t23 < t24);
            if (!t25) {
                goto gm_lb1;
            }
            {
                {
                    uint64_t t26 = gm_l8;
                    void * t27 = GM_LD(void *, ((uint8_t *)&gm_l7));
                    uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    uint64_t t29 = gm_l8;
                    gm_t6j6smj7qoctu t30 = GM_LD(gm_t6j6smj7qoctu, (((uint8_t *)t27) + (uintptr_t)t29 * 32));
                    GM_ST(uint64_t, ((uint8_t *)&gm_l15 + 0), t26);
                    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&gm_l15 + 8), t30);
                }
                {
                    {
                        uint64_t t31 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
                        gm_l9 = t31;
                        {
                            memmove(((uint8_t *)&gm_l10), (((uint8_t *)&gm_l15) + 8), 32);
                        }
                        {
                            uint8_t t32 = gm_l3;
                            uint8_t t33 = gm_l4;
                            void * t34 = (void *)((uint8_t *)&gm_l10);
                            gm_f_object_flags_aequi2e((void *)&F.l11, (void *)NULL, cx, NULL, t32, t33, t34);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        {
                            {
                                {
                                    void * t35 = gm_l1;
                                    gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
                                    gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
                                    void * t38 = (void *)((uint8_t *)&gm_l10);
                                    gm_th2lqbo77pvsc t39 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l6));
                                    gm_f_package_object_60se70a((void *)&gm_l16, (void *)NULL, cx, NULL, t35, t37, t38, &t39);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                uint8_t t40 = (uint8_t)((((uint8_t *)&gm_l16))[16] != 0);
                                if (!t40) {
                                    goto gm_lb7;
                                }
                                gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l16));
                                gm_l12 = t41;
                                void * t42 = (void *)((uint8_t *)&F.l5);
                                gm_b16_8 t43 = gm_l12;
                                gm_tftb1odc739t5 t44 = gm_f_push_3ef746b(t42, t43);
                                gm_l17 = t44;
                                uint8_t t45 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                                if (!t45) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_src4rbevsr9gjng, 346, 13);
                                goto gm_lb3;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        gm_b16_8 t46 = gm_l2;
                        uint64_t t47 = gm_l9;
                        gm_b16_8 t48 = gm_f_temporary_object_tji353n((void *)NULL, cx, NULL, t46, t47);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l13 = t48;
                        {
                            void * t49 = gm_l1;
                            void * t50 = (void *)((uint8_t *)&gm_l10);
                            gm_b16_8 t51 = gm_l13;
                            gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
                            gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
                            gm_f_compile_source_85vh57g((void *)&gm_l18, (void *)NULL, cx, NULL, t49, t50, t51, t53);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t54 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                        if (t54) {
                        } else {
                            gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
                            gm_l14 = t55;
                            void * t56 = (void *)((uint8_t *)&F.l5);
                            gm_b0_1 t57 = gm_f_remove_objects_1vq49q8((void *)NULL, cx, NULL, t56);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_b16_8 t58 = gm_l14;
                            memset(&(*(gm_tllkbcpqeka9b *)gm_out), 0, sizeof (*(gm_tllkbcpqeka9b *)gm_out));
                            ((uint8_t *)&(*(gm_tllkbcpqeka9b *)gm_out))[0] = 1;
                            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tllkbcpqeka9b *)gm_out) + 8), t58);
                            goto gm_fdone;
                        }
                        void * t59 = (void *)((uint8_t *)&F.l5);
                        gm_b16_8 t60 = gm_l13;
                        gm_tftb1odc739t5 t61 = gm_f_push_3ef746b(t59, t60);
                        gm_l19 = t61;
                        uint8_t t62 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                        if (!t62) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_src4rbevsr9gjng, 354, 9);
                        void * t63 = (void *)(((uint8_t *)&F.l5) + 40);
                        gm_b16_8 t64 = gm_l13;
                        gm_tftb1odc739t5 t65 = gm_f_push_3ef746b(t63, t64);
                        gm_l20 = t65;
                        uint8_t t66 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                        if (!t66) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_src4rbevsr9gjng, 355, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src4rbevsr9gjng, 343, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t67 = gm_l8;
            uint64_t r68; bool o69 = __builtin_add_overflow(t67, ((uint64_t)1ULL), &r68);
            uint64_t t70 = r68;
            gm_l8 = t70;
        }
    }
gm_lb1:;
    {
        gm_tk2lc304hfpci t71 = GM_LD(gm_tk2lc304hfpci, ((uint8_t *)&F.l5));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tllkbcpqeka9b *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tllkbcpqeka9b *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tllkbcpqeka9b *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tllkbcpqeka9b *)gm_out))[0] = 0;
        GM_ST(gm_tk2lc304hfpci, ((uint8_t *)&(*(gm_tllkbcpqeka9b *)gm_out) + 8), t71);
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
__attribute__((aligned(4))) void gm_f_package_object_60se70a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, const void * a3) {
    struct gm_fr_package_object_60se70a F;
    void * gm_l0; /* tc */
    gm_b16_8 gm_l1; /* flags */
    void * gm_l2; /* src */
    gm_th2lqbo77pvsc gm_l3; /* compiler */
    gm_b16_8 gm_l4; /* base */
    gm_b16_8 gm_l5; /* version */
    gm_b16_8 gm_l6; /* text */
    gm_b16_8 gm_l7; /* dir */
    gm_b16_8 gm_l9; /* key */
    gm_b16_8 gm_l10; /* stem */
    gm_b16_8 gm_l11; /* obj */
    gm_b16_8 gm_l12; /* deps */
    gm_t5j14m8ru132p gm_l13; /* info */
    gm_b16_8 gm_l14; /* tmp_o */
    gm_b16_8 gm_l15; /* tmp_d */
    gm_b16_8 gm_l17; /* manifest */
    gm_b16_8 gm_l18; /* listing */
    gm_b16_8 gm_l19; /* tmp_m */
    GM_STACK_CHECK(&F, 2785u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_th2lqbo77pvsc gm_l20;
    gm_tgvh15qshf643 gm_l21;
    gm_tabb710d9mq2e gm_l22;
    gm_tabb710d9mq2e gm_l23;
    gm_tabb710d9mq2e gm_l24;
    gm_tabb710d9mq2e gm_l25;
    gm_tabb710d9mq2e gm_l26;
    gm_tabb710d9mq2e gm_l27;
    gm_tih6h4avv5nrb gm_l28;
    gm_tl572d7579hns gm_l29;
    gm_trqfvmjtt4sij gm_l30;
    gm_tipro5npl6rd8 gm_l31;
    gm_th2lqbo77pvsc gm_l32;
    gm_tgvh15qshf643 gm_l33;
    gm_th2lqbo77pvsc gm_l34;
    gm_th2lqbo77pvsc gm_l35;
    gm_ttln0d0hd70en gm_l36;
    gm_ttsm01t0d8srg gm_l37;
    gm_ttsm01t0d8srg gm_l38;
    {
        {
            gm_f_cache_dir_81h90qu((void *)&gm_l20, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t6 = (uint8_t)((((uint8_t *)&gm_l20))[16] != 0);
        if (t6) {
            gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l20));
            gm_l4 = t7;
        } else {
            memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
            goto gm_fdone;
        }
        uint8_t t8 = (uint8_t)((((uint8_t *)a3))[16] != 0);
        if (t8) {
            gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)a3));
            gm_l5 = t9;
        } else {
            memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
            goto gm_fdone;
        }
        {
            void * t10 = gm_l2;
            gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)t10));
            gm_f_read_all_r9rh9qd((void *)&gm_l21, (void *)NULL, cx, NULL, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
        if (t12) {
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
            gm_l6 = t13;
        } else {
            memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
            goto gm_fdone;
        }
        gm_b16_8 t14 = gm_l4;
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(gm_stv6f87bn9v0e));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(6));
        gm_b16_8 t16 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t16;
        {
            gm_b16_8 t17;
            GM_ST(void *, (uint8_t *)&t17, (void *)(gm_snqvo5bgc2fch));
            GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(7));
            void * t18 = gm_l2;
            gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)t18));
            gm_b16_8 t20 = gm_l6;
            gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
            void * t22 = gm_l0;
            gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)t22));
            gm_b16_8 t24;
            GM_ST(void *, (uint8_t *)&t24, (void *)(gm_s0pi0emdqd3vb));
            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(1));
            gm_f_join_sfknljb((void *)&gm_l22, (void *)NULL, cx, NULL, t23, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t25 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
            if (!t25) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 8, gm_src4rbevsr9gjng, 372, 67);
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
            gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
            void * t28 = gm_l0;
            gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)t28) + 16));
            gm_b16_8 t30;
            GM_ST(void *, (uint8_t *)&t30, (void *)(gm_s0pi0emdqd3vb));
            GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(1));
            gm_f_join_sfknljb((void *)&gm_l23, (void *)NULL, cx, NULL, t29, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t31 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
            if (!t31) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 8, gm_src4rbevsr9gjng, 372, 91);
            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 8));
            gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
            gm_b16_8 t34 = gm_l1;
            gm_b16_8 t35;
            GM_ST(void *, (uint8_t *)&t35, (void *)(gm_s0pi0emdqd3vb));
            GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(1));
            gm_f_join_sfknljb((void *)&gm_l24, (void *)NULL, cx, NULL, t34, t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
            if (!t36) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 8, gm_src4rbevsr9gjng, 372, 119);
            gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 8));
            gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
            gm_b16_8 t39 = gm_l5;
            gm_tdv3dkjvo1l7t t40;
            GM_ST(gm_b16_8, ((uint8_t *)&t40 + 0), t17);
            GM_ST(gm_b16_8, ((uint8_t *)&t40 + 16), t19);
            GM_ST(gm_b16_8, ((uint8_t *)&t40 + 32), t21);
            GM_ST(gm_b16_8, ((uint8_t *)&t40 + 48), t27);
            GM_ST(gm_b16_8, ((uint8_t *)&t40 + 64), t33);
            GM_ST(gm_b16_8, ((uint8_t *)&t40 + 80), t38);
            GM_ST(gm_b16_8, ((uint8_t *)&t40 + 96), t39);
            F.l8 = t40;
        }
        void * t41 = (void *)((uint8_t *)&F.l8);
        uint64_t t42 = ((uintptr_t)((uint64_t)7ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t43;
        GM_ST(void *, (uint8_t *)&t43, (void *)(((uint8_t *)t41 + (uintptr_t)((uint64_t)0ULL) * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(t42));
        gm_b16_8 t44 = gm_f_cache_key_tf6ebrd((void *)NULL, cx, NULL, t43);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t44;
        {
            gm_b16_8 t45 = gm_l9;
            gm_tnpifv20irnph t46;
            GM_ST(gm_b16_8, ((uint8_t *)&t46 + 0), t45);
            gm_f_format_gum3rpo((void *)&gm_l25, (void *)NULL, cx, NULL, t46);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t47 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
        if (!t47) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 8, gm_src4rbevsr9gjng, 373, 13);
        gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l25) + 8));
        gm_l10 = t48;
        {
            gm_b16_8 t49 = gm_l7;
            gm_b16_8 t50 = gm_l10;
            gm_tnpifv20irnph t51;
            GM_ST(gm_b16_8, ((uint8_t *)&t51 + 0), t50);
            gm_f_format_soc2hp6((void *)&gm_l26, (void *)NULL, cx, NULL, t51);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t52 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
            if (!t52) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 8, gm_src4rbevsr9gjng, 374, 27);
            gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l26) + 8));
            gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
            gm_b16_8 t55 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t49, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t55;
        }
        {
            gm_b16_8 t56 = gm_l7;
            gm_b16_8 t57 = gm_l10;
            gm_tnpifv20irnph t58;
            GM_ST(gm_b16_8, ((uint8_t *)&t58 + 0), t57);
            gm_f_format_vhqrhad((void *)&gm_l27, (void *)NULL, cx, NULL, t58);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t59 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
            if (!t59) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 8, gm_src4rbevsr9gjng, 375, 28);
            gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l27) + 8));
            gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
            gm_b16_8 t62 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t56, t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l12 = t62;
        }
        {
            {
                {
                    gm_b16_8 t63 = gm_l11;
                    gm_node *pn64 = gm_find(cx, 2126215853);
                    if (!pn64) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 376, 16);
                    gm_tih6h4avv5nrb t65;
                    ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn64->clauses[5])((void *)&t65, pn64->env, pn64->hctx, pn64, t63);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l28 = t65;
                }
                uint8_t t66 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                if (!t66) {
                    goto gm_lb3;
                }
                {
                    memmove(((uint8_t *)&gm_l13), (((uint8_t *)&gm_l28) + 8), 32);
                }
                gm_b16_8 t67 = gm_l12;
                uint8_t t68 = gm_f_deps_current_jlls0pm((void *)NULL, cx, NULL, t67);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (t68) {
                    int64_t t69 = GM_LD(int64_t, (((uint8_t *)&gm_l13) + 16));
                    gm_node *pn70 = gm_find(cx, 1579066750);
                    if (!pn70) gm_trap("no handler installed for effect `Clock`", gm_src4rbevsr9gjng, 379, 32);
                    int64_t t71;
                    t71 = ((int64_t (*)(void *, gm_node *, gm_node *))pn70->clauses[1])(pn70->env, pn70->hctx, pn70);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    int64_t r72; bool o73 = __builtin_mul_overflow(((int64_t)86400LL), ((int64_t)1000000000LL), &r72);
                    if (o73) gm_trap("integer overflow in `*`", gm_src4rbevsr9gjng, 379, 47);
                    int64_t t74 = r72;
                    int64_t r75; bool o76 = __builtin_sub_overflow(t71, t74, &r75);
                    if (o76) gm_trap("integer overflow in `-`", gm_src4rbevsr9gjng, 379, 32);
                    int64_t t77 = r75;
                    uint8_t t78 = (t69 < t77);
                    if (t78) {
                        gm_b16_8 t79 = gm_l11;
                        gm_b0_1 t80 = gm_f_touch_frggs6u((void *)NULL, cx, NULL, t79);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t81 = gm_l12;
                        gm_b0_1 t82 = gm_f_touch_frggs6u((void *)NULL, cx, NULL, t81);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    gm_b16_8 t83 = gm_l11;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t83);
                    ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                    goto gm_fdone;
                }
                goto gm_lb2;
            }
gm_lb3:;
        }
gm_lb2:;
        {
            gm_b16_8 t84 = gm_l7;
            gm_f_make_cache_dirs_3bvvbb9((void *)&gm_l29, (void *)NULL, cx, NULL, t84);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t85 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
        if (t85) {
        } else {
            memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
            goto gm_fdone;
        }
        gm_b16_8 t86 = gm_l7;
        gm_b16_8 t87 = gm_l10;
        gm_b16_8 t88 = GM_LD(gm_b16_8, ((uint8_t *)&t87));
        gm_b16_8 t89;
        GM_ST(void *, (uint8_t *)&t89, (void *)(gm_se0f2jka437t4));
        GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(1));
        gm_b16_8 t90 = gm_f_private_name_u7i0rip((void *)NULL, cx, NULL, t86, t88, t89);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l14 = t90;
        gm_b16_8 t91 = gm_l7;
        gm_b16_8 t92 = gm_l10;
        gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&t92));
        gm_b16_8 t94;
        GM_ST(void *, (uint8_t *)&t94, (void *)(gm_stafskn6bv24f));
        GM_ST(uintptr_t, (uint8_t *)&t94 + 8, (uintptr_t)(1));
        gm_b16_8 t95 = gm_f_private_name_u7i0rip((void *)NULL, cx, NULL, t91, t93, t94);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l15 = t95;
        {
            gm_b16_8 t96;
            GM_ST(void *, (uint8_t *)&t96, (void *)(gm_stlamkovo4l42));
            GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(3));
            gm_b16_8 t97;
            GM_ST(void *, (uint8_t *)&t97, (void *)(gm_s4hpf7ii3vj1c));
            GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(3));
            gm_b16_8 t98 = gm_l15;
            gm_b16_8 t99;
            GM_ST(void *, (uint8_t *)&t99, (void *)(gm_s4vd6e3mkiiso));
            GM_ST(uintptr_t, (uint8_t *)&t99 + 8, (uintptr_t)(2));
            gm_b16_8 t100;
            GM_ST(void *, (uint8_t *)&t100, (void *)(gm_s65qt8qo64cje));
            GM_ST(uintptr_t, (uint8_t *)&t100 + 8, (uintptr_t)(2));
            gm_b16_8 t101 = gm_l14;
            void * t102 = gm_l2;
            gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)t102));
            gm_tdv3dkjvo1l7t t104;
            GM_ST(gm_b16_8, ((uint8_t *)&t104 + 0), t96);
            GM_ST(gm_b16_8, ((uint8_t *)&t104 + 16), t97);
            GM_ST(gm_b16_8, ((uint8_t *)&t104 + 32), t98);
            GM_ST(gm_b16_8, ((uint8_t *)&t104 + 48), t99);
            GM_ST(gm_b16_8, ((uint8_t *)&t104 + 64), t100);
            GM_ST(gm_b16_8, ((uint8_t *)&t104 + 80), t101);
            GM_ST(gm_b16_8, ((uint8_t *)&t104 + 96), t103);
            F.l16 = t104;
        }
        {
            void * t105 = gm_l0;
            gm_b16_8 t106 = gm_l1;
            void * t107 = (void *)((uint8_t *)&F.l16);
            uint64_t t108 = ((uintptr_t)((uint64_t)7ULL)) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t109;
            GM_ST(void *, (uint8_t *)&t109, (void *)(((uint8_t *)t107 + (uintptr_t)((uint64_t)0ULL) * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t109 + 8, (uintptr_t)(t108));
            gm_b16_8 t110 = GM_LD(gm_b16_8, ((uint8_t *)&t109));
            gm_f_command_b2u33fl((void *)&gm_l30, (void *)NULL, cx, NULL, t105, t106, t110);
            if (gm_unw.kind) {
                goto gm_ld5_1;
            }
        }
        {
            gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l30));
            gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&t111));
            gm_f_run_mj5dobu((void *)&gm_l31, (void *)NULL, cx, NULL, t112);
            if (gm_unw.kind) {
                goto gm_ld5_1;
            }
        }
        uint8_t t113 = GM_LD(uint8_t, ((uint8_t *)&gm_l31));
        uint8_t t114 = (uint8_t)!t113;
        if (t114) {
            gm_b16_8 t115 = gm_l14;
            gm_node *pn116 = gm_find(cx, 2126215853);
            if (!pn116) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 392, 13);
            gm_ttln0d0hd70en t117;
            t117 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn116->clauses[6])(pn116->env, pn116->hctx, pn116, t115);
            if (gm_unw.kind) {
                goto gm_ld5_1;
            }
            memset(&F.ret, 0, sizeof F.ret);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
            goto gm_ld5_1;
        }
        {
            gm_b16_8 t118 = gm_l15;
            gm_f_read_all_r9rh9qd((void *)&gm_l33, (void *)NULL, cx, NULL, t118);
            if (gm_unw.kind) {
                goto gm_ld5_1;
            }
        }
        uint8_t t119 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
        if (t119) {
            gm_b16_8 t120 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l33) + 8));
            GM_ST(uint8_t, (uint8_t *)&gm_l32 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&gm_l32 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&gm_l32 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&gm_l32), t120);
            ((uint8_t *)&gm_l32)[16] = 1;
        } else {
            memset(&gm_l32, 0, sizeof gm_l32);
        }
        {
            memmove(((uint8_t *)&gm_l34), ((uint8_t *)&gm_l32), 24);
        }
        uint8_t t121 = (uint8_t)((((uint8_t *)&gm_l34))[16] != 0);
        if (t121) {
            gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l34));
            gm_l17 = t122;
        } else {
            gm_b16_8 t123 = gm_l14;
            gm_node *pn124 = gm_find(cx, 2126215853);
            if (!pn124) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 396, 13);
            gm_ttln0d0hd70en t125;
            t125 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn124->clauses[6])(pn124->env, pn124->hctx, pn124, t123);
            if (gm_unw.kind) {
                goto gm_ld5_1;
            }
            memset(&F.ret, 0, sizeof F.ret);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
            goto gm_ld5_1;
        }
        {
            gm_b16_8 t126 = gm_l17;
            gm_b16_8 t127 = GM_LD(gm_b16_8, ((uint8_t *)&t126));
            gm_f_deps_listing_7genqb0((void *)&gm_l35, (void *)NULL, cx, NULL, t127);
            if (gm_unw.kind) {
                goto gm_ld5_1;
            }
        }
        uint8_t t128 = (uint8_t)((((uint8_t *)&gm_l35))[16] != 0);
        if (t128) {
            gm_b16_8 t129 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l35));
            gm_l18 = t129;
        } else {
            gm_b16_8 t130 = gm_l14;
            gm_node *pn131 = gm_find(cx, 2126215853);
            if (!pn131) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 400, 13);
            gm_ttln0d0hd70en t132;
            t132 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn131->clauses[6])(pn131->env, pn131->hctx, pn131, t130);
            if (gm_unw.kind) {
                goto gm_ld5_1;
            }
            memset(&F.ret, 0, sizeof F.ret);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
            goto gm_ld5_1;
        }
        gm_b16_8 t133 = gm_l7;
        gm_b16_8 t134 = gm_l10;
        gm_b16_8 t135 = GM_LD(gm_b16_8, ((uint8_t *)&t134));
        gm_b16_8 t136;
        GM_ST(void *, (uint8_t *)&t136, (void *)(gm_st3m5iduoqh8b));
        GM_ST(uintptr_t, (uint8_t *)&t136 + 8, (uintptr_t)(4));
        gm_b16_8 t137 = gm_f_private_name_u7i0rip((void *)NULL, cx, NULL, t133, t135, t136);
        if (gm_unw.kind) {
            goto gm_ld5_1;
        }
        gm_l19 = t137;
        gm_b16_8 t138 = gm_l19;
        gm_b16_8 t139 = gm_l18;
        gm_b16_8 t140 = GM_LD(gm_b16_8, ((uint8_t *)&t139));
        gm_ttln0d0hd70en t141 = gm_f_write_all_o8ucaji((void *)NULL, cx, NULL, t138, t140);
        if (gm_unw.kind) {
            goto gm_ld5_1;
        }
        gm_l36 = t141;
        uint8_t t142 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
        if (t142) {
        } else {
            gm_b16_8 t143 = gm_l14;
            gm_node *pn144 = gm_find(cx, 2126215853);
            if (!pn144) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 405, 13);
            gm_ttln0d0hd70en t145;
            t145 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn144->clauses[6])(pn144->env, pn144->hctx, pn144, t143);
            if (gm_unw.kind) {
                goto gm_ld5_1;
            }
            gm_b16_8 t146 = gm_l19;
            gm_node *pn147 = gm_find(cx, 2126215853);
            if (!pn147) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 406, 13);
            gm_ttln0d0hd70en t148;
            t148 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn147->clauses[6])(pn147->env, pn147->hctx, pn147, t146);
            if (gm_unw.kind) {
                goto gm_ld5_1;
            }
            memset(&F.ret, 0, sizeof F.ret);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
            goto gm_ld5_1;
        }
        gm_b16_8 t149 = gm_l14;
        gm_b16_8 t150 = gm_l11;
        gm_ttsm01t0d8srg t151 = gm_f_rename_or_remove_l80dbfs((void *)NULL, cx, NULL, t149, t150);
        if (gm_unw.kind) {
            goto gm_ld5_1;
        }
        gm_l37 = t151;
        uint8_t t152 = (uint8_t)((((uint8_t *)&gm_l37))[0] != 0);
        if (t152) {
        } else {
            gm_b16_8 t153 = gm_l19;
            gm_node *pn154 = gm_find(cx, 2126215853);
            if (!pn154) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 411, 13);
            gm_ttln0d0hd70en t155;
            t155 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn154->clauses[6])(pn154->env, pn154->hctx, pn154, t153);
            if (gm_unw.kind) {
                goto gm_ld5_1;
            }
            memset(&F.ret, 0, sizeof F.ret);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
            goto gm_ld5_1;
        }
        gm_b16_8 t156 = gm_l19;
        gm_b16_8 t157 = gm_l12;
        gm_ttsm01t0d8srg t158 = gm_f_rename_or_remove_l80dbfs((void *)NULL, cx, NULL, t156, t157);
        if (gm_unw.kind) {
            goto gm_ld5_1;
        }
        gm_l38 = t158;
        uint8_t t159 = (uint8_t)((((uint8_t *)&gm_l38))[0] != 0);
        if (t159) {
        } else {
            memset(&F.ret, 0, sizeof F.ret);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
            goto gm_ld5_1;
        }
        {
            gm_b16_8 t160 = gm_l11;
            GM_ST(uint8_t, (uint8_t *)&F.ret + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&F.ret + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&F.ret + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&F.ret), t160);
            ((uint8_t *)&F.ret)[16] = 1;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
            goto gm_ld5_1;
        }
    }
    {
        gm_b16_8 t161 = gm_l15;
        gm_node *pn162 = gm_find(cx, 2126215853);
        if (!pn162) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 389, 15);
        gm_ttln0d0hd70en t163;
        t163 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn162->clauses[6])(pn162->env, pn162->hctx, pn162, t161);
    }
    goto gm_ls164;
gm_ld5_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        gm_b16_8 t165 = gm_l15;
        gm_node *pn166 = gm_find(cx, 2126215853);
        if (!pn166) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 389, 15);
        gm_ttln0d0hd70en t167;
        t167 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn166->clauses[6])(pn166->env, pn166->hctx, pn166, t165);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls164:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_private_name_u7i0rip(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_private_name_u7i0rip F;
    gm_b16_8 gm_l0; /* dir */
    gm_b16_8 gm_l1; /* stem */
    gm_b16_8 gm_l2; /* ext */
    gm_t2doi72kmmlil gm_l3; /* r */
    GM_STACK_CHECK(&F, 353u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l4;
    void * t2 = (void *)((uint8_t *)&gm_l3);
    uint64_t t3 = ((uintptr_t)((uint64_t)8ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(((uint8_t *)t2 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(t3));
    gm_node *pn5 = gm_find(cx, 1087599154);
    if (!pn5) gm_trap("no handler installed for effect `Rand`", gm_src4rbevsr9gjng, 86, 5);
    gm_b0_1 t6;
    t6 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_b16_8))pn5->clauses[0])(pn5->env, pn5->hctx, pn5, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        gm_b16_8 t7 = gm_l0;
        gm_b16_8 t8 = gm_l1;
        void * t9 = (void *)((uint8_t *)&gm_l3);
        uint64_t t10 = ((uintptr_t)((uint64_t)8ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(((uint8_t *)t9 + (uintptr_t)((uint64_t)0ULL) * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(t10));
        gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
        gm_b16_8 t13 = gm_f_hex_euih242((void *)NULL, cx, NULL, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t14 = gm_l2;
        gm_tfeab279c16rr t15;
        GM_ST(gm_b16_8, ((uint8_t *)&t15 + 0), t8);
        GM_ST(gm_b16_8, ((uint8_t *)&t15 + 16), t13);
        GM_ST(gm_b16_8, ((uint8_t *)&t15 + 32), t14);
        gm_f_format_3pfhto5((void *)&gm_l4, (void *)NULL, cx, NULL, &t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
        if (!t16) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 8, gm_src4rbevsr9gjng, 87, 27);
        gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
        gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
        gm_b16_8 t19 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t7, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t19;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_prune_eu93ghe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_prune_eu93ghe F;
    int64_t gm_l1; /* now */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* n */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    uint64_t gm_l9; /* j */
    gm_thgmovcqlsivv gm_l10; /* tmp */
    int64_t gm_l11; /* recent */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    GM_STACK_CHECK(&F, 848u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    uint8_t gm_l15;
    uint8_t gm_l16;
    gm_node *pn16 = gm_find(cx, 1579066750);
    if (!pn16) gm_trap("no handler installed for effect `Clock`", gm_src4rbevsr9gjng, 140, 12);
    int64_t t17;
    t17 = ((int64_t (*)(void *, gm_node *, gm_node *))pn16->clauses[1])(pn16->env, pn16->hctx, pn16);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l1 = t17;
    int64_t t18 = gm_l1;
    int64_t r19; bool o20 = __builtin_mul_overflow(((int64_t)3600LL), ((int64_t)1000000000LL), &r19);
    if (o20) gm_trap("integer overflow in `*`", gm_src4rbevsr9gjng, 141, 20);
    int64_t t21 = r19;
    int64_t r22; bool o23 = __builtin_sub_overflow(t18, t21, &r22);
    if (o23) gm_trap("integer overflow in `-`", gm_src4rbevsr9gjng, 141, 14);
    int64_t t24 = r22;
    F.l2 = t24;
    gm_trqfvmjtt4sij t25;
    gm_f_list_vuor028((void *)&t25, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t25;
    gm_b16_8 t26 = F.l0;
    gm_l4 = t26;
    {
        gm_node wn27 = {.eff = 2076700068, .kind = GM_LIT, .clauses = gm_hlgmpdevos39, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t30 = gm_l4;
        gm_b0_1 t31 = gm_f_entries_44o5n9f_sre2b72((void *)NULL, (&wn27), NULL, t30);
        if (gm_unw.kind) {
            goto gm_ll29;
        }
    }
    goto gm_ls32;
gm_ll29:;
    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we28; }
    goto gm_lad;
gm_ls32:;
gm_lb1:;
we28:;
    uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
    uint8_t t34 = (t33 <= ((uint64_t)32ULL));
    if (t34) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
    gm_l5 = t35;
    gm_l7 = ((uint64_t)1ULL);
    uint64_t t36 = gm_l5;
    gm_l6 = t36;
    {
        for (;;) {
            uint64_t t38 = gm_l7;
            uint64_t t39 = gm_l6;
            uint8_t t40 = (t38 < t39);
            if (!t40) {
                goto gm_lb2;
            }
            {
                {
                    {
                        uint64_t t41 = gm_l7;
                        gm_l8 = t41;
                        uint64_t t42 = gm_l8;
                        gm_l9 = t42;
                        {
                            for (;;) {
                                uint64_t t44 = gm_l9;
                                uint8_t t45 = (t44 > ((uint64_t)0ULL));
                                if (!t45) {
                                    goto gm_lb7;
                                }
                                void * t46 = GM_LD(void *, ((uint8_t *)&F.l3));
                                uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                                uint64_t t48 = gm_l9;
                                uint64_t r49; bool o50 = __builtin_sub_overflow(t48, ((uint64_t)1ULL), &r49);
                                if (o50) gm_trap("integer overflow in `-`", gm_src4rbevsr9gjng, 159, 38);
                                uint64_t t51 = r49;
                                if ((uintptr_t)t51 >= (uintptr_t)(t47)) gm_trap_index((uintptr_t)t51, t47, gm_src4rbevsr9gjng, 159, 26);
                                int64_t t52 = GM_LD(int64_t, (((uint8_t *)t46) + (uintptr_t)t51 * 24));
                                void * t53 = GM_LD(void *, ((uint8_t *)&F.l3));
                                uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                                uint64_t t55 = gm_l9;
                                if ((uintptr_t)t55 >= (uintptr_t)(t54)) gm_trap_index((uintptr_t)t55, t54, gm_src4rbevsr9gjng, 159, 56);
                                int64_t t56 = GM_LD(int64_t, (((uint8_t *)t53) + (uintptr_t)t55 * 24));
                                uint8_t t57 = (t52 > t56);
                                if (t57) {
                                    gm_l15 = ((uint8_t)1);
                                } else {
                                    void * t58 = GM_LD(void *, ((uint8_t *)&F.l3));
                                    uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                                    uint64_t t60 = gm_l9;
                                    uint64_t r61; bool o62 = __builtin_sub_overflow(t60, ((uint64_t)1ULL), &r61);
                                    if (o62) gm_trap("integer overflow in `-`", gm_src4rbevsr9gjng, 159, 96);
                                    uint64_t t63 = r61;
                                    if ((uintptr_t)t63 >= (uintptr_t)(t59)) gm_trap_index((uintptr_t)t63, t59, gm_src4rbevsr9gjng, 159, 84);
                                    int64_t t64 = GM_LD(int64_t, (((uint8_t *)t58) + (uintptr_t)t63 * 24));
                                    void * t65 = GM_LD(void *, ((uint8_t *)&F.l3));
                                    uint64_t t66 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                                    uint64_t t67 = gm_l9;
                                    if ((uintptr_t)t67 >= (uintptr_t)(t66)) gm_trap_index((uintptr_t)t67, t66, gm_src4rbevsr9gjng, 159, 115);
                                    int64_t t68 = GM_LD(int64_t, (((uint8_t *)t65) + (uintptr_t)t67 * 24));
                                    uint8_t t69 = ((t64 == t68));
                                    if (t69) {
                                        void * t70 = GM_LD(void *, ((uint8_t *)&F.l3));
                                        uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                                        uint64_t t72 = gm_l9;
                                        uint64_t r73; bool o74 = __builtin_sub_overflow(t72, ((uint64_t)1ULL), &r73);
                                        if (o74) gm_trap("integer overflow in `-`", gm_src4rbevsr9gjng, 159, 155);
                                        uint64_t t75 = r73;
                                        if ((uintptr_t)t75 >= (uintptr_t)(t71)) gm_trap_index((uintptr_t)t75, t71, gm_src4rbevsr9gjng, 159, 143);
                                        gm_b16_8 t76 = GM_LD(gm_b16_8, ((((uint8_t *)t70) + (uintptr_t)t75 * 24) + 8));
                                        void * t77 = GM_LD(void *, ((uint8_t *)&F.l3));
                                        uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                                        uint64_t t79 = gm_l9;
                                        if ((uintptr_t)t79 >= (uintptr_t)(t78)) gm_trap_index((uintptr_t)t79, t78, gm_src4rbevsr9gjng, 159, 169);
                                        gm_b16_8 t80 = GM_LD(gm_b16_8, ((((uint8_t *)t77) + (uintptr_t)t79 * 24) + 8));
                                        uint8_t t81 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t80), ((uint8_t *)&t76));
                                        gm_l16 = t81;
                                    } else {
                                        gm_l16 = ((uint8_t)0);
                                    }
                                    uint8_t t82 = gm_l16;
                                    gm_l15 = t82;
                                }
                                uint8_t t83 = gm_l15;
                                if (!t83) {
                                    goto gm_lb7;
                                }
                                {
                                    {
                                        void * t84 = GM_LD(void *, ((uint8_t *)&F.l3));
                                        uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                                        uint64_t t86 = gm_l9;
                                        uint64_t r87; bool o88 = __builtin_sub_overflow(t86, ((uint64_t)1ULL), &r87);
                                        if (o88) gm_trap("integer overflow in `-`", gm_src4rbevsr9gjng, 160, 32);
                                        uint64_t t89 = r87;
                                        if ((uintptr_t)t89 >= (uintptr_t)(t85)) gm_trap_index((uintptr_t)t89, t85, gm_src4rbevsr9gjng, 160, 20);
                                        memmove(((uint8_t *)&gm_l10), (((uint8_t *)t84) + (uintptr_t)t89 * 24), 24);
                                    }
                                    {
                                        void * t90 = GM_LD(void *, ((uint8_t *)&F.l3));
                                        uint64_t t91 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                                        uint64_t t92 = gm_l9;
                                        uint64_t r93; bool o94 = __builtin_sub_overflow(t92, ((uint64_t)1ULL), &r93);
                                        if (o94) gm_trap("integer overflow in `-`", gm_src4rbevsr9gjng, 161, 25);
                                        uint64_t t95 = r93;
                                        if ((uintptr_t)t95 >= (uintptr_t)(t91)) gm_trap_index((uintptr_t)t95, t91, gm_src4rbevsr9gjng, 161, 13);
                                        void * t96 = GM_LD(void *, ((uint8_t *)&F.l3));
                                        uint64_t t97 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                                        uint64_t t98 = gm_l9;
                                        if ((uintptr_t)t98 >= (uintptr_t)(t97)) gm_trap_index((uintptr_t)t98, t97, gm_src4rbevsr9gjng, 161, 34);
                                        memmove((((uint8_t *)t90) + (uintptr_t)t95 * 24), (((uint8_t *)t96) + (uintptr_t)t98 * 24), 24);
                                    }
                                    {
                                        void * t99 = GM_LD(void *, ((uint8_t *)&F.l3));
                                        uint64_t t100 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                                        uint64_t t101 = gm_l9;
                                        if ((uintptr_t)t101 >= (uintptr_t)(t100)) gm_trap_index((uintptr_t)t101, t100, gm_src4rbevsr9gjng, 162, 13);
                                        memmove((((uint8_t *)t99) + (uintptr_t)t101 * 24), ((uint8_t *)&gm_l10), 24);
                                    }
                                    uint64_t t102 = gm_l9;
                                    uint64_t r103; bool o104 = __builtin_sub_overflow(t102, ((uint64_t)1ULL), &r103);
                                    if (o104) gm_trap("integer overflow in `-`", gm_src4rbevsr9gjng, 163, 13);
                                    uint64_t t105 = r103;
                                    gm_l9 = t105;
                                }
gm_lb9:;
                            }
                        }
gm_lb7:;
                        goto gm_lb5;
                    }
gm_lb6:;
                    gm_trap("no match arm matched the scrutinee", gm_src4rbevsr9gjng, 157, 5);
                }
gm_lb5:;
            }
gm_lb4:;
            uint64_t t106 = gm_l7;
            uint64_t r107; bool o108 = __builtin_add_overflow(t106, ((uint64_t)1ULL), &r107);
            uint64_t t109 = r107;
            gm_l7 = t109;
        }
    }
gm_lb2:;
    int64_t t110 = gm_l1;
    int64_t r111; bool o112 = __builtin_mul_overflow(((int64_t)600LL), ((int64_t)1000000000LL), &r111);
    if (o112) gm_trap("integer overflow in `*`", gm_src4rbevsr9gjng, 166, 21);
    int64_t t113 = r111;
    int64_t r114; bool o115 = __builtin_sub_overflow(t110, t113, &r114);
    if (o115) gm_trap("integer overflow in `-`", gm_src4rbevsr9gjng, 166, 15);
    int64_t t116 = r114;
    gm_l11 = t116;
    gm_l13 = ((uint64_t)0ULL);
    uint64_t t117 = gm_l5;
    uint64_t r118; bool o119 = __builtin_sub_overflow(t117, ((uint64_t)32ULL), &r118);
    if (o119) gm_trap("integer overflow in `-`", gm_src4rbevsr9gjng, 167, 18);
    uint64_t t120 = r118;
    gm_l12 = t120;
    {
        for (;;) {
            uint64_t t122 = gm_l13;
            uint64_t t123 = gm_l12;
            uint8_t t124 = (t122 < t123);
            if (!t124) {
                goto gm_lb10;
            }
            {
                {
                    {
                        uint64_t t125 = gm_l13;
                        gm_l14 = t125;
                        void * t126 = GM_LD(void *, ((uint8_t *)&F.l3));
                        uint64_t t127 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                        uint64_t t128 = gm_l14;
                        if ((uintptr_t)t128 >= (uintptr_t)(t127)) gm_trap_index((uintptr_t)t128, t127, gm_src4rbevsr9gjng, 168, 12);
                        int64_t t129 = GM_LD(int64_t, (((uint8_t *)t126) + (uintptr_t)t128 * 24));
                        int64_t t130 = gm_l11;
                        uint8_t t131 = (t129 < t130);
                        if (t131) {
                            void * t132 = GM_LD(void *, ((uint8_t *)&F.l3));
                            uint64_t t133 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                            uint64_t t134 = gm_l14;
                            if ((uintptr_t)t134 >= (uintptr_t)(t133)) gm_trap_index((uintptr_t)t134, t133, gm_src4rbevsr9gjng, 168, 61);
                            gm_b16_8 t135 = GM_LD(gm_b16_8, ((((uint8_t *)t132) + (uintptr_t)t134 * 24) + 8));
                            gm_node *pn136 = gm_find(cx, 2126215853);
                            if (!pn136) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 168, 51);
                            gm_ttln0d0hd70en t137;
                            t137 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn136->clauses[6])(pn136->env, pn136->hctx, pn136, t135);
                            if (gm_unw.kind) {
                                goto gm_ll121;
                            }
                        }
                        goto gm_lb13;
                    }
gm_lb14:;
                    gm_trap("no match arm matched the scrutinee", gm_src4rbevsr9gjng, 167, 5);
                }
gm_lb13:;
            }
gm_lb12:;
            uint64_t t138 = gm_l13;
            uint64_t r139; bool o140 = __builtin_add_overflow(t138, ((uint64_t)1ULL), &r139);
            uint64_t t141 = r139;
            gm_l13 = t141;
        }
    }
    goto gm_ls142;
gm_ll121:;
    if (gm_unw.frame == &F.h && gm_unw.label == 3) {
        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb10; }
        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb12; }
    }
    goto gm_lad;
gm_ls142:;
gm_lb10:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_prune_units_iqs7tqd(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_prune_units_iqs7tqd F;
    int64_t gm_l1; /* now */
    gm_b16_8 gm_l4; /* <tmp> */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_b16_8 gm_l7; /* o */
    GM_STACK_CHECK(&F, 466u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_tabb710d9mq2e gm_l8;
    gm_node *pn8 = gm_find(cx, 1579066750);
    if (!pn8) gm_trap("no handler installed for effect `Clock`", gm_src4rbevsr9gjng, 243, 12);
    int64_t t9;
    t9 = ((int64_t (*)(void *, gm_node *, gm_node *))pn8->clauses[1])(pn8->env, pn8->hctx, pn8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l1 = t9;
    int64_t t10 = gm_l1;
    int64_t r11; bool o12 = __builtin_mul_overflow(((int64_t)3600LL), ((int64_t)1000000000LL), &r11);
    if (o12) gm_trap("integer overflow in `*`", gm_src4rbevsr9gjng, 244, 20);
    int64_t t13 = r11;
    int64_t r14; bool o15 = __builtin_sub_overflow(t10, t13, &r14);
    if (o15) gm_trap("integer overflow in `-`", gm_src4rbevsr9gjng, 244, 14);
    int64_t t16 = r14;
    F.l2 = t16;
    gm_trqfvmjtt4sij t17;
    gm_f_list_vuor028((void *)&t17, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t17;
    gm_b16_8 t18 = F.l0;
    gm_l4 = t18;
    {
        gm_node wn19 = {.eff = 2076700068, .kind = GM_LIT, .clauses = gm_hl8alodt65ii, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t22 = gm_l4;
        gm_b0_1 t23 = gm_f_entries_44o5n9f_sh4kvg0((void *)NULL, (&wn19), NULL, t22);
        if (gm_unw.kind) {
            goto gm_ll21;
        }
    }
    goto gm_ls24;
gm_ll21:;
    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we20; }
    goto gm_lad;
gm_ls24:;
gm_lb1:;
we20:;
    gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    int64_t t26 = gm_l1;
    gm_b16_8 t27 = gm_f_units_to_prune_qp6vjmt((void *)NULL, cx, NULL, t25, t26);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t27;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t29 = gm_l6;
            uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t31 = (t29 < t30);
            if (!t31) {
                goto gm_lb2;
            }
            {
                void * t32 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t34 = gm_l6;
                {
                    {
                        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)t32) + (uintptr_t)t34 * 16));
                        gm_l7 = t35;
                        gm_b16_8 t36 = gm_l7;
                        gm_node *pn37 = gm_find(cx, 2126215853);
                        if (!pn37) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 258, 13);
                        gm_ttln0d0hd70en t38;
                        t38 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn37->clauses[6])(pn37->env, pn37->hctx, pn37, t36);
                        if (gm_unw.kind) {
                            goto gm_ll28;
                        }
                        void * t39 = GM_LD(void *, ((uint8_t *)&gm_l7));
                        uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                        uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                        uint64_t r42; bool o43 = __builtin_sub_overflow(t41, ((uint64_t)2ULL), &r42);
                        if (o43) gm_trap("integer overflow in `-`", gm_src4rbevsr9gjng, 259, 47);
                        uint64_t t44 = r42;
                        uint64_t t45 = (uintptr_t)((uint64_t)0ULL);
                        uint64_t t46 = (uintptr_t)t44;
                        if (t45 > t46 || t46 > (uintptr_t)t40) gm_trap_slice(t45, t46, t40, gm_src4rbevsr9gjng, 259, 42);
                        uint64_t t47 = ((uintptr_t)t44) - (uintptr_t)((uint64_t)0ULL);
                        gm_b16_8 t48;
                        GM_ST(void *, (uint8_t *)&t48, (void *)(((uint8_t *)t39 + (uintptr_t)((uint64_t)0ULL) * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(t47));
                        gm_tnpifv20irnph t49;
                        GM_ST(gm_b16_8, ((uint8_t *)&t49 + 0), t48);
                        gm_f_format_cncadt9((void *)&gm_l8, (void *)NULL, cx, NULL, t49);
                        if (gm_unw.kind) {
                            goto gm_ll28;
                        }
                        uint8_t t50 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                        if (!t50) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_src4rbevsr9gjng, 259, 23);
                        gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
                        gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
                        gm_node *pn53 = gm_find(cx, 2126215853);
                        if (!pn53) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 259, 13);
                        gm_ttln0d0hd70en t54;
                        t54 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn53->clauses[6])(pn53->env, pn53->hctx, pn53, t52);
                        if (gm_unw.kind) {
                            goto gm_ll28;
                        }
                        goto gm_lb5;
                    }
gm_lb6:;
                    gm_trap("no match arm matched the scrutinee", gm_src4rbevsr9gjng, 257, 5);
                }
gm_lb5:;
            }
gm_lb4:;
            uint64_t t55 = gm_l6;
            uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
            uint64_t t58 = r56;
            gm_l6 = t58;
        }
    }
    goto gm_ls59;
gm_ll28:;
    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb2; }
        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb4; }
    }
    goto gm_lad;
gm_ls59:;
gm_lb2:;
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
inline __attribute__((aligned(4))) gm_ttsm01t0d8srg gm_f_rename_or_remove_l80dbfs(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_rename_or_remove_l80dbfs F;
    gm_b16_8 gm_l0; /* from */
    gm_b16_8 gm_l1; /* to */
    GM_STACK_CHECK(&F, 139u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_ttln0d0hd70en gm_l2;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3 = gm_l1;
    gm_node *pn4 = gm_find(cx, 2126215853);
    if (!pn4) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 96, 5);
    gm_ttln0d0hd70en t5;
    t5 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_b16_8))pn4->clauses[7])(pn4->env, pn4->hctx, pn4, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t5;
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (t6) {
    } else {
        gm_b16_8 t7 = gm_l0;
        gm_node *pn8 = gm_find(cx, 2126215853);
        if (!pn8) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 97, 13);
        gm_ttln0d0hd70en t9;
        t9 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn8->clauses[6])(pn8->env, pn8->hctx, pn8, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t10 = gm_l1;
        uint8_t t11 = gm_f_is_file_tcuv9rv((void *)NULL, cx, NULL, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (t11) {
            gm_ttsm01t0d8srg t12;
            GM_ST(gm_b0_1, ((uint8_t *)&t12), GM_ZERO(gm_b0_1));
            ((uint8_t *)&t12)[0] = 1;
            F.ret = t12;
            goto gm_fret;
        }
        gm_ttsm01t0d8srg t13;
        memset(&t13, 0, sizeof t13);
        F.ret = t13;
        goto gm_fret;
    }
    gm_ttsm01t0d8srg t14;
    GM_ST(gm_b0_1, ((uint8_t *)&t14), GM_ZERO(gm_b0_1));
    ((uint8_t *)&t14)[0] = 1;
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
__attribute__((aligned(4))) void gm_f_runtime_object_oihv3ss(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, const void * a4) {
    struct gm_fr_runtime_object_oihv3ss F;
    void * gm_l0; /* tc */
    gm_b16_8 gm_l1; /* flags */
    gm_b16_8 gm_l2; /* source */
    gm_b16_8 gm_l3; /* prefix */
    gm_th2lqbo77pvsc gm_l4; /* compiler */
    gm_b16_8 gm_l5; /* dir */
    gm_b16_8 gm_l6; /* version */
    gm_b16_8 gm_l8; /* key */
    gm_b16_8 gm_l9; /* stem */
    gm_b16_8 gm_l10; /* obj */
    gm_b16_8 gm_l11; /* c_file */
    gm_b16_8 gm_l12; /* tmp */
    gm_b16_8 gm_l13; /* tmp */
    GM_STACK_CHECK(&F, 1882u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_th2lqbo77pvsc gm_l16;
    gm_tabb710d9mq2e gm_l17;
    gm_tabb710d9mq2e gm_l18;
    gm_tabb710d9mq2e gm_l19;
    gm_tabb710d9mq2e gm_l20;
    gm_tabb710d9mq2e gm_l21;
    gm_tl572d7579hns gm_l22;
    gm_tabb710d9mq2e gm_l23;
    gm_ttln0d0hd70en gm_l24;
    gm_ttsm01t0d8srg gm_l25;
    gm_tipro5npl6rd8 gm_l26;
    gm_ttsm01t0d8srg gm_l27;
    {
        gm_f_cache_dir_81h90qu((void *)&gm_l16, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t2 = (uint8_t)((((uint8_t *)&gm_l16))[16] != 0);
    if (t2) {
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l16));
        gm_l5 = t3;
    } else {
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
    uint8_t t4 = (uint8_t)((((uint8_t *)a4))[16] != 0);
    if (t4) {
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)a4));
        gm_l6 = t5;
    } else {
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
    {
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_sbkvlhvo2a3ic));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(6));
        gm_b16_8 t7 = gm_l2;
        void * t8 = gm_l0;
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)t8));
        gm_b16_8 t10;
        GM_ST(void *, (uint8_t *)&t10, (void *)(gm_s0pi0emdqd3vb));
        GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(1));
        gm_f_join_sfknljb((void *)&gm_l17, (void *)NULL, cx, NULL, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t11 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
        if (!t11) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_src4rbevsr9gjng, 54, 50);
        gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
        gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
        void * t14 = gm_l0;
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)t14) + 16));
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_s0pi0emdqd3vb));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(1));
        gm_f_join_sfknljb((void *)&gm_l18, (void *)NULL, cx, NULL, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t17 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
        if (!t17) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_src4rbevsr9gjng, 54, 74);
        gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
        gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
        gm_b16_8 t20 = gm_l1;
        gm_b16_8 t21;
        GM_ST(void *, (uint8_t *)&t21, (void *)(gm_s0pi0emdqd3vb));
        GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(1));
        gm_f_join_sfknljb((void *)&gm_l19, (void *)NULL, cx, NULL, t20, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t22 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
        if (!t22) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 8, gm_src4rbevsr9gjng, 54, 102);
        gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 8));
        gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
        gm_b16_8 t25 = gm_l6;
        gm_t4d1ka1c3gdt5 t26;
        GM_ST(gm_b16_8, ((uint8_t *)&t26 + 0), t6);
        GM_ST(gm_b16_8, ((uint8_t *)&t26 + 16), t7);
        GM_ST(gm_b16_8, ((uint8_t *)&t26 + 32), t13);
        GM_ST(gm_b16_8, ((uint8_t *)&t26 + 48), t19);
        GM_ST(gm_b16_8, ((uint8_t *)&t26 + 64), t24);
        GM_ST(gm_b16_8, ((uint8_t *)&t26 + 80), t25);
        F.l7 = t26;
    }
    void * t27 = (void *)((uint8_t *)&F.l7);
    uint64_t t28 = ((uintptr_t)((uint64_t)6ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t29;
    GM_ST(void *, (uint8_t *)&t29, (void *)(((uint8_t *)t27 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(t28));
    gm_b16_8 t30 = gm_f_cache_key_tf6ebrd((void *)NULL, cx, NULL, t29);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l8 = t30;
    {
        gm_b16_8 t31 = gm_l3;
        gm_b16_8 t32 = gm_l8;
        gm_t6j6smj7qoctu t33;
        GM_ST(gm_b16_8, ((uint8_t *)&t33 + 0), t31);
        GM_ST(gm_b16_8, ((uint8_t *)&t33 + 16), t32);
        gm_f_format_c31q7f8((void *)&gm_l20, (void *)NULL, cx, NULL, &t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t34 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
    if (!t34) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 8, gm_src4rbevsr9gjng, 55, 13);
    gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
    gm_l9 = t35;
    {
        gm_b16_8 t36 = gm_l5;
        gm_b16_8 t37 = gm_l9;
        gm_tnpifv20irnph t38;
        GM_ST(gm_b16_8, ((uint8_t *)&t38 + 0), t37);
        gm_f_format_soc2hp6((void *)&gm_l21, (void *)NULL, cx, NULL, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t39 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
        if (!t39) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 8, gm_src4rbevsr9gjng, 56, 27);
        gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
        gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
        gm_b16_8 t42 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t36, t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t42;
    }
    gm_b16_8 t43 = gm_l10;
    uint8_t t44 = gm_f_is_file_tcuv9rv((void *)NULL, cx, NULL, t43);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (t44) {
        gm_b16_8 t45 = gm_l10;
        gm_b0_1 t46 = gm_f_touch_frggs6u((void *)NULL, cx, NULL, t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t47 = gm_l10;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t47);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        goto gm_fdone;
    }
    {
        gm_b16_8 t48 = gm_l5;
        gm_f_make_cache_dirs_3bvvbb9((void *)&gm_l22, (void *)NULL, cx, NULL, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t49 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
    if (t49) {
    } else {
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
    gm_b16_8 t50 = gm_l2;
    gm_b16_8 t51 = gm_l5;
    gm_b16_8 t52 = gm_l9;
    gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
    uint8_t t54 = gm_f_adopt_object_psc9tpi((void *)NULL, cx, NULL, t50, t51, t53);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (t54) {
        gm_b16_8 t55 = gm_l10;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t55);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        goto gm_fdone;
    }
    {
        gm_b16_8 t56 = gm_l5;
        gm_b16_8 t57 = gm_l9;
        gm_tnpifv20irnph t58;
        GM_ST(gm_b16_8, ((uint8_t *)&t58 + 0), t57);
        gm_f_format_nk7pf4i((void *)&gm_l23, (void *)NULL, cx, NULL, t58);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t59 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
        if (!t59) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 8, gm_src4rbevsr9gjng, 66, 30);
        gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 8));
        gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
        gm_b16_8 t62 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t56, t61);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t62;
    }
    gm_b16_8 t63 = gm_l11;
    uint8_t t64 = gm_f_is_file_tcuv9rv((void *)NULL, cx, NULL, t63);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t65 = (uint8_t)!t64;
    if (t65) {
        gm_b16_8 t66 = gm_l5;
        gm_b16_8 t67 = gm_l9;
        gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
        gm_b16_8 t69;
        GM_ST(void *, (uint8_t *)&t69, (void *)(gm_s2gnpj7k4l22n));
        GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(1));
        gm_b16_8 t70 = gm_f_private_name_u7i0rip((void *)NULL, cx, NULL, t66, t68, t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l12 = t70;
        gm_b16_8 t71 = gm_l12;
        gm_b16_8 t72 = gm_l2;
        gm_ttln0d0hd70en t73 = gm_f_write_all_o8ucaji((void *)NULL, cx, NULL, t71, t72);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l24 = t73;
        uint8_t t74 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
        if (t74) {
        } else {
            memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
            goto gm_fdone;
        }
        gm_b16_8 t75 = gm_l12;
        gm_b16_8 t76 = gm_l11;
        gm_ttsm01t0d8srg t77 = gm_f_rename_or_remove_l80dbfs((void *)NULL, cx, NULL, t75, t76);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l25 = t77;
        uint8_t t78 = (uint8_t)((((uint8_t *)&gm_l25))[0] != 0);
        if (t78) {
        } else {
            memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
            goto gm_fdone;
        }
    }
    gm_b16_8 t79 = gm_l5;
    gm_b16_8 t80 = gm_l9;
    gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&t80));
    gm_b16_8 t82;
    GM_ST(void *, (uint8_t *)&t82, (void *)(gm_se0f2jka437t4));
    GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(1));
    gm_b16_8 t83 = gm_f_private_name_u7i0rip((void *)NULL, cx, NULL, t79, t81, t82);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l13 = t83;
    {
        gm_b16_8 t84;
        GM_ST(void *, (uint8_t *)&t84, (void *)(gm_s4vd6e3mkiiso));
        GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(2));
        gm_b16_8 t85;
        GM_ST(void *, (uint8_t *)&t85, (void *)(gm_s65qt8qo64cje));
        GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(2));
        gm_b16_8 t86 = gm_l13;
        gm_b16_8 t87 = gm_l11;
        gm_tdmi4fk9nhqa7 t88;
        GM_ST(gm_b16_8, ((uint8_t *)&t88 + 0), t84);
        GM_ST(gm_b16_8, ((uint8_t *)&t88 + 16), t85);
        GM_ST(gm_b16_8, ((uint8_t *)&t88 + 32), t86);
        GM_ST(gm_b16_8, ((uint8_t *)&t88 + 48), t87);
        F.l14 = t88;
    }
    {
        void * t89 = gm_l0;
        gm_b16_8 t90 = gm_l1;
        void * t91 = (void *)((uint8_t *)&F.l14);
        uint64_t t92 = ((uintptr_t)((uint64_t)4ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t93;
        GM_ST(void *, (uint8_t *)&t93, (void *)(((uint8_t *)t91 + (uintptr_t)((uint64_t)0ULL) * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(t92));
        gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
        gm_f_command_b2u33fl((void *)&F.l15, (void *)NULL, cx, NULL, t89, t90, t94);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&F.l15));
        gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&t95));
        gm_f_run_mj5dobu((void *)&gm_l26, (void *)NULL, cx, NULL, t96);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t97 = GM_LD(uint8_t, ((uint8_t *)&gm_l26));
    uint8_t t98 = (uint8_t)!t97;
    if (t98) {
        gm_b16_8 t99 = gm_l13;
        gm_node *pn100 = gm_find(cx, 2126215853);
        if (!pn100) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 76, 13);
        gm_ttln0d0hd70en t101;
        t101 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn100->clauses[6])(pn100->env, pn100->hctx, pn100, t99);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
    gm_b16_8 t102 = gm_l13;
    gm_b16_8 t103 = gm_l10;
    gm_ttsm01t0d8srg t104 = gm_f_rename_or_remove_l80dbfs((void *)NULL, cx, NULL, t102, t103);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l27 = t104;
    uint8_t t105 = (uint8_t)((((uint8_t *)&gm_l27))[0] != 0);
    if (t105) {
    } else {
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
    {
        gm_b16_8 t106 = gm_l10;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t106);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_touch_frggs6u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_touch_frggs6u F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 200u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sa23fj423hlk4));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(5));
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4vd6e3mkiiso));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(2));
        gm_b16_8 t4 = gm_l0;
        gm_t9bmh80edou9c t5;
        GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t2);
        GM_ST(gm_b16_8, ((uint8_t *)&t5 + 16), t3);
        GM_ST(gm_b16_8, ((uint8_t *)&t5 + 32), t4);
        F.l1 = t5;
    }
    {
        void * t6 = (void *)((uint8_t *)&F.l1);
        uint64_t t7 = ((uintptr_t)((uint64_t)3ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t6 + (uintptr_t)((uint64_t)0ULL) * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7));
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
        gm_tipro5npl6rd8 t10;
        gm_f_run_mj5dobu((void *)&t10, (void *)NULL, cx, NULL, t9);
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
__attribute__((aligned(4))) void gm_f_unit_objects_3f31tcd(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, const void * a3, uint64_t a4, uint8_t a5) {
    struct gm_fr_unit_objects_3f31tcd F;
    void * gm_l0; /* tc */
    gm_b16_8 gm_l1; /* flags */
    gm_b16_8 gm_l2; /* sources */
    gm_th2lqbo77pvsc gm_l3; /* compiler */
    uint64_t gm_l4; /* jobs */
    uint8_t gm_l5; /* line_paths */
    gm_b16_8 gm_l6; /* dir */
    gm_b16_8 gm_l7; /* version */
    gm_b16_8 gm_l8; /* cc */
    gm_b16_8 gm_l9; /* cflags */
    gm_b16_8 gm_l10; /* fl */
    int64_t gm_l15; /* day */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_b16_8 gm_l18; /* src */
    gm_b16_8 gm_l20; /* key */
    gm_b16_8 gm_l21; /* stem */
    gm_b16_8 gm_l22; /* obj */
    gm_t5j14m8ru132p gm_l23; /* info */
    gm_b16_8 gm_l24; /* c_file */
    gm_b16_8 gm_l25; /* text */
    gm_b16_8 gm_l26; /* tmp_c */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    gm_b16_8 gm_l29; /* o */
    gm_b16_8 gm_l30; /* <tmp> */
    uint64_t gm_l31; /* <tmp> */
    uint64_t gm_l32; /* i */
    gm_b16_8 gm_l33; /* tmp */
    GM_STACK_CHECK(&F, 3008u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_th2lqbo77pvsc gm_l34;
    gm_tl572d7579hns gm_l35;
    gm_tabb710d9mq2e gm_l36;
    gm_tabb710d9mq2e gm_l37;
    gm_tabb710d9mq2e gm_l38;
    gm_tabb710d9mq2e gm_l39;
    gm_tabb710d9mq2e gm_l40;
    gm_tftb1odc739t5 gm_l41;
    gm_tih6h4avv5nrb gm_l42;
    gm_tabb710d9mq2e gm_l43;
    gm_tabb710d9mq2e gm_l44;
    gm_tabb710d9mq2e gm_l45;
    gm_ttln0d0hd70en gm_l46;
    gm_ttsm01t0d8srg gm_l47;
    gm_tftb1odc739t5 gm_l48;
    gm_tabb710d9mq2e gm_l49;
    gm_tftb1odc739t5 gm_l50;
    gm_tftb1odc739t5 gm_l51;
    gm_tf1bjg81u0glv gm_l52;
    gm_ttsm01t0d8srg gm_l53;
    {
        gm_f_cache_dir_81h90qu((void *)&gm_l34, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t21 = (uint8_t)((((uint8_t *)&gm_l34))[16] != 0);
        if (t21) {
            gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l34));
            gm_l6 = t22;
        } else {
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 0), ((uint8_t)1ULL));
            goto gm_fdone;
        }
        uint8_t t23 = (uint8_t)((((uint8_t *)a3))[16] != 0);
        if (t23) {
            gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)a3));
            gm_l7 = t24;
        } else {
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 0), ((uint8_t)1ULL));
            goto gm_fdone;
        }
        gm_b16_8 t25 = gm_l6;
        gm_f_make_cache_dirs_3bvvbb9((void *)&gm_l35, (void *)NULL, cx, NULL, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
        if (t26) {
        } else {
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 0), ((uint8_t)1ULL));
            goto gm_fdone;
        }
        void * t27 = gm_l0;
        gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)t27));
        gm_b16_8 t29;
        GM_ST(void *, (uint8_t *)&t29, (void *)(gm_s0pi0emdqd3vb));
        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(1));
        gm_f_join_sfknljb((void *)&gm_l36, (void *)NULL, cx, NULL, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
        if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 8, gm_src4rbevsr9gjng, 188, 11);
        gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l36) + 8));
        gm_l8 = t31;
        void * t32 = gm_l0;
        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)t32) + 16));
        gm_b16_8 t34;
        GM_ST(void *, (uint8_t *)&t34, (void *)(gm_s0pi0emdqd3vb));
        GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(1));
        gm_f_join_sfknljb((void *)&gm_l37, (void *)NULL, cx, NULL, t33, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t35 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
        if (!t35) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 8, gm_src4rbevsr9gjng, 189, 15);
        gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l37) + 8));
        gm_l9 = t36;
        gm_b16_8 t37 = gm_l1;
        gm_b16_8 t38;
        GM_ST(void *, (uint8_t *)&t38, (void *)(gm_s0pi0emdqd3vb));
        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(1));
        gm_f_join_sfknljb((void *)&gm_l38, (void *)NULL, cx, NULL, t37, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t39 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
        if (!t39) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 8, gm_src4rbevsr9gjng, 190, 11);
        gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l38) + 8));
        gm_l10 = t40;
        gm_trqfvmjtt4sij t41;
        gm_f_list_epvducg((void *)&t41, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l11 = t41;
        gm_trqfvmjtt4sij t42;
        gm_f_list_epvducg((void *)&t42, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l12 = t42;
        gm_trqfvmjtt4sij t43;
        gm_f_list_epvducg((void *)&t43, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l13 = t43;
        gm_trqfvmjtt4sij t44;
        gm_f_list_epvducg((void *)&t44, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l14 = t44;
        gm_node *pn45 = gm_find(cx, 1579066750);
        if (!pn45) gm_trap("no handler installed for effect `Clock`", gm_src4rbevsr9gjng, 195, 12);
        int64_t t46;
        t46 = ((int64_t (*)(void *, gm_node *, gm_node *))pn45->clauses[1])(pn45->env, pn45->hctx, pn45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        int64_t r47; bool o48 = __builtin_mul_overflow(((int64_t)86400LL), ((int64_t)1000000000LL), &r47);
        if (o48) gm_trap("integer overflow in `*`", gm_src4rbevsr9gjng, 195, 27);
        int64_t t49 = r47;
        int64_t r50; bool o51 = __builtin_sub_overflow(t46, t49, &r50);
        if (o51) gm_trap("integer overflow in `-`", gm_src4rbevsr9gjng, 195, 12);
        int64_t t52 = r50;
        gm_l15 = t52;
        gm_b16_8 t53 = gm_l2;
        gm_l16 = t53;
        gm_l17 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t54 = gm_l17;
                uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                uint8_t t56 = (t54 < t55);
                if (!t56) {
                    goto gm_lb1;
                }
                {
                    void * t57 = GM_LD(void *, ((uint8_t *)&gm_l16));
                    uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                    uint64_t t59 = gm_l17;
                    {
                        {
                            gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)t57) + (uintptr_t)t59 * 16));
                            gm_l18 = t60;
                            {
                                gm_b16_8 t61;
                                GM_ST(void *, (uint8_t *)&t61, (void *)(gm_sfbtgpom3l6k2));
                                GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(4));
                                gm_b16_8 t62 = gm_l18;
                                gm_b16_8 t63 = gm_l8;
                                gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&t63));
                                gm_b16_8 t65 = gm_l9;
                                gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
                                gm_b16_8 t67 = gm_l10;
                                gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
                                gm_b16_8 t69 = gm_l7;
                                gm_t4d1ka1c3gdt5 t70;
                                GM_ST(gm_b16_8, ((uint8_t *)&t70 + 0), t61);
                                GM_ST(gm_b16_8, ((uint8_t *)&t70 + 16), t62);
                                GM_ST(gm_b16_8, ((uint8_t *)&t70 + 32), t64);
                                GM_ST(gm_b16_8, ((uint8_t *)&t70 + 48), t66);
                                GM_ST(gm_b16_8, ((uint8_t *)&t70 + 64), t68);
                                GM_ST(gm_b16_8, ((uint8_t *)&t70 + 80), t69);
                                F.l19 = t70;
                            }
                            void * t71 = (void *)((uint8_t *)&F.l19);
                            uint64_t t72 = ((uintptr_t)((uint64_t)6ULL)) - (uintptr_t)((uint64_t)0ULL);
                            gm_b16_8 t73;
                            GM_ST(void *, (uint8_t *)&t73, (void *)(((uint8_t *)t71 + (uintptr_t)((uint64_t)0ULL) * 16)));
                            GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(t72));
                            gm_b16_8 t74 = gm_f_cache_key_tf6ebrd((void *)NULL, cx, NULL, t73);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l20 = t74;
                            {
                                gm_b16_8 t75 = gm_l6;
                                gm_b16_8 t76 = gm_l20;
                                gm_tnpifv20irnph t77;
                                GM_ST(gm_b16_8, ((uint8_t *)&t77 + 0), t76);
                                gm_f_format_n1il1vf((void *)&gm_l39, (void *)NULL, cx, NULL, t77);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t78 = (uint8_t)!((((uint8_t *)&gm_l39))[0] != 0);
                                if (!t78) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l39) + 8, gm_src4rbevsr9gjng, 198, 32);
                                gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l39) + 8));
                                gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
                                gm_b16_8 t81 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t75, t80);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l21 = t81;
                            }
                            {
                                gm_b16_8 t82 = gm_l21;
                                gm_tnpifv20irnph t83;
                                GM_ST(gm_b16_8, ((uint8_t *)&t83 + 0), t82);
                                gm_f_format_pf7dqps((void *)&gm_l40, (void *)NULL, cx, NULL, t83);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            uint8_t t84 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
                            if (!t84) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l40) + 8, gm_src4rbevsr9gjng, 199, 16);
                            gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l40) + 8));
                            gm_l22 = t85;
                            void * t86 = (void *)((uint8_t *)&F.l11);
                            gm_b16_8 t87 = gm_l22;
                            gm_b16_8 t88 = GM_LD(gm_b16_8, ((uint8_t *)&t87));
                            gm_tftb1odc739t5 t89 = gm_f_push_3ef746b(t86, t88);
                            gm_l41 = t89;
                            uint8_t t90 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                            if (!t90) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 1, gm_src4rbevsr9gjng, 200, 9);
                            {
                                {
                                    {
                                        gm_b16_8 t91 = gm_l22;
                                        gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&t91));
                                        gm_node *pn93 = gm_find(cx, 2126215853);
                                        if (!pn93) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 201, 20);
                                        gm_tih6h4avv5nrb t94;
                                        ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn93->clauses[5])((void *)&t94, pn93->env, pn93->hctx, pn93, t92);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l42 = t94;
                                    }
                                    uint8_t t95 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
                                    if (!t95) {
                                        goto gm_lb7;
                                    }
                                    {
                                        memmove(((uint8_t *)&gm_l23), (((uint8_t *)&gm_l42) + 8), 32);
                                    }
                                    int64_t t96 = GM_LD(int64_t, (((uint8_t *)&gm_l23) + 16));
                                    int64_t t97 = gm_l15;
                                    uint8_t t98 = (t96 < t97);
                                    if (t98) {
                                        gm_b16_8 t99 = gm_l22;
                                        gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&t99));
                                        gm_b0_1 t101 = gm_f_touch_frggs6u((void *)NULL, cx, NULL, t100);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    goto gm_lb3;
                                }
gm_lb7:;
                            }
gm_lb6:;
                            {
                                {
                                    uint8_t t102 = gm_l5;
                                    uint8_t t103 = (uint8_t)!t102;
                                    if (!t103) {
                                        goto gm_lb9;
                                    }
                                    gm_b16_8 t104 = gm_l18;
                                    gm_b16_8 t105 = gm_l6;
                                    gm_b16_8 t106 = gm_l20;
                                    gm_tnpifv20irnph t107;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t107 + 0), t106);
                                    gm_f_format_n1il1vf((void *)&gm_l43, (void *)NULL, cx, NULL, t107);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t108 = (uint8_t)!((((uint8_t *)&gm_l43))[0] != 0);
                                    if (!t108) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l43) + 8, gm_src4rbevsr9gjng, 207, 54);
                                    gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l43) + 8));
                                    gm_b16_8 t110 = GM_LD(gm_b16_8, ((uint8_t *)&t109));
                                    uint8_t t111 = gm_f_adopt_object_psc9tpi((void *)NULL, cx, NULL, t104, t105, t110);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    if (!t111) {
                                        goto gm_lb9;
                                    }
                                    goto gm_lb3;
                                }
gm_lb9:;
                            }
gm_lb8:;
                            {
                                gm_b16_8 t112 = gm_l21;
                                gm_tnpifv20irnph t113;
                                GM_ST(gm_b16_8, ((uint8_t *)&t113 + 0), t112);
                                gm_f_format_cncadt9((void *)&gm_l44, (void *)NULL, cx, NULL, t113);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            uint8_t t114 = (uint8_t)!((((uint8_t *)&gm_l44))[0] != 0);
                            if (!t114) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l44) + 8, gm_src4rbevsr9gjng, 208, 19);
                            gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l44) + 8));
                            gm_l24 = t115;
                            uint8_t t116 = gm_l5;
                            if (t116) {
                                gm_b16_8 t117 = gm_l18;
                                gm_b16_8 t118 = gm_l24;
                                gm_b16_8 t119 = GM_LD(gm_b16_8, ((uint8_t *)&t118));
                                gm_b16_8 t120 = gm_f_resolve_lines_h4bf30f((void *)NULL, cx, NULL, t117, t119);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l25 = t120;
                            } else {
                                gm_b16_8 t121 = gm_l18;
                                gm_l25 = t121;
                            }
                            {
                                gm_b16_8 t122 = gm_l6;
                                gm_b16_8 t123 = gm_l20;
                                gm_tnpifv20irnph t124;
                                GM_ST(gm_b16_8, ((uint8_t *)&t124 + 0), t123);
                                gm_f_format_n1il1vf((void *)&gm_l45, (void *)NULL, cx, NULL, t124);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t125 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
                                if (!t125) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l45) + 8, gm_src4rbevsr9gjng, 210, 36);
                                gm_b16_8 t126 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l45) + 8));
                                gm_b16_8 t127 = GM_LD(gm_b16_8, ((uint8_t *)&t126));
                                gm_b16_8 t128;
                                GM_ST(void *, (uint8_t *)&t128, (void *)(gm_s2gnpj7k4l22n));
                                GM_ST(uintptr_t, (uint8_t *)&t128 + 8, (uintptr_t)(1));
                                gm_b16_8 t129 = gm_f_private_name_u7i0rip((void *)NULL, cx, NULL, t122, t127, t128);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l26 = t129;
                            }
                            gm_b16_8 t130 = gm_l26;
                            gm_b16_8 t131 = gm_l25;
                            gm_ttln0d0hd70en t132 = gm_f_write_all_o8ucaji((void *)NULL, cx, NULL, t130, t131);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l46 = t132;
                            uint8_t t133 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
                            if (t133) {
                            } else {
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
                                GM_ST(uint8_t, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 0), ((uint8_t)1ULL));
                                goto gm_fdone;
                            }
                            gm_b16_8 t134 = gm_l26;
                            gm_b16_8 t135 = gm_l24;
                            gm_b16_8 t136 = GM_LD(gm_b16_8, ((uint8_t *)&t135));
                            gm_ttsm01t0d8srg t137 = gm_f_rename_or_remove_l80dbfs((void *)NULL, cx, NULL, t134, t136);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l47 = t137;
                            uint8_t t138 = (uint8_t)((((uint8_t *)&gm_l47))[0] != 0);
                            if (t138) {
                            } else {
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
                                GM_ST(uint8_t, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 0), ((uint8_t)1ULL));
                                goto gm_fdone;
                            }
                            void * t139 = (void *)((uint8_t *)&F.l12);
                            gm_b16_8 t140 = gm_l24;
                            gm_b16_8 t141 = GM_LD(gm_b16_8, ((uint8_t *)&t140));
                            gm_tftb1odc739t5 t142 = gm_f_push_3ef746b(t139, t141);
                            gm_l48 = t142;
                            uint8_t t143 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                            if (!t143) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l48) + 1, gm_src4rbevsr9gjng, 213, 9);
                            {
                                void * t144 = (void *)((uint8_t *)&F.l13);
                                gm_b16_8 t145 = gm_l6;
                                gm_b16_8 t146 = gm_l20;
                                gm_tnpifv20irnph t147;
                                GM_ST(gm_b16_8, ((uint8_t *)&t147 + 0), t146);
                                gm_f_format_n1il1vf((void *)&gm_l49, (void *)NULL, cx, NULL, t147);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t148 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
                                if (!t148) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l49) + 8, gm_src4rbevsr9gjng, 214, 41);
                                gm_b16_8 t149 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l49) + 8));
                                gm_b16_8 t150 = GM_LD(gm_b16_8, ((uint8_t *)&t149));
                                gm_b16_8 t151;
                                GM_ST(void *, (uint8_t *)&t151, (void *)(gm_se0f2jka437t4));
                                GM_ST(uintptr_t, (uint8_t *)&t151 + 8, (uintptr_t)(1));
                                gm_b16_8 t152 = gm_f_private_name_u7i0rip((void *)NULL, cx, NULL, t145, t150, t151);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tftb1odc739t5 t153 = gm_f_push_3ef746b(t144, t152);
                                gm_l50 = t153;
                            }
                            uint8_t t154 = (uint8_t)!((((uint8_t *)&gm_l50))[0] != 0);
                            if (!t154) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l50) + 1, gm_src4rbevsr9gjng, 214, 9);
                            void * t155 = (void *)((uint8_t *)&F.l14);
                            gm_b16_8 t156 = gm_l22;
                            gm_b16_8 t157 = GM_LD(gm_b16_8, ((uint8_t *)&t156));
                            gm_tftb1odc739t5 t158 = gm_f_push_3ef746b(t155, t157);
                            gm_l51 = t158;
                            uint8_t t159 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
                            if (!t159) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l51) + 1, gm_src4rbevsr9gjng, 215, 9);
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_src4rbevsr9gjng, 196, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t160 = gm_l17;
                uint64_t r161; bool o162 = __builtin_add_overflow(t160, ((uint64_t)1ULL), &r161);
                uint64_t t163 = r161;
                gm_l17 = t163;
            }
        }
gm_lb1:;
    }
    {
        uint64_t t164 = GM_LD(uint64_t, (((uint8_t *)&F.l12) + 8));
        uint8_t t165 = (t164 > ((uint64_t)0ULL));
        if (t165) {
            void * t166 = gm_l0;
            gm_b16_8 t167 = gm_l1;
            gm_b16_8 t168 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
            gm_b16_8 t169 = GM_LD(gm_b16_8, ((uint8_t *)&t168));
            gm_b16_8 t170 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
            gm_b16_8 t171 = GM_LD(gm_b16_8, ((uint8_t *)&t170));
            uint64_t t172 = gm_l4;
            uint8_t t173 = gm_f_compile_objects_mu5secm((void *)NULL, cx, NULL, t166, t167, t169, t171, t172);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t174 = (uint8_t)!t173;
            if (t174) {
                gm_b16_8 t175 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                gm_l27 = t175;
                gm_l28 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t176 = gm_l28;
                        uint64_t t177 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                        uint8_t t178 = (t176 < t177);
                        if (!t178) {
                            goto gm_lb10;
                        }
                        {
                            void * t179 = GM_LD(void *, ((uint8_t *)&gm_l27));
                            uint64_t t180 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                            uint64_t t181 = gm_l28;
                            {
                                {
                                    gm_b16_8 t182 = GM_LD(gm_b16_8, (((uint8_t *)t179) + (uintptr_t)t181 * 16));
                                    gm_l29 = t182;
                                    gm_b16_8 t183 = gm_l29;
                                    gm_node *pn184 = gm_find(cx, 2126215853);
                                    if (!pn184) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 219, 43);
                                    gm_ttln0d0hd70en t185;
                                    t185 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn184->clauses[6])(pn184->env, pn184->hctx, pn184, t183);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_lb13;
                                }
gm_lb14:;
                                gm_trap("no match arm matched the scrutinee", gm_src4rbevsr9gjng, 219, 13);
                            }
gm_lb13:;
                        }
gm_lb12:;
                        uint64_t t186 = gm_l28;
                        uint64_t r187; bool o188 = __builtin_add_overflow(t186, ((uint64_t)1ULL), &r187);
                        uint64_t t189 = r187;
                        gm_l28 = t189;
                    }
                }
gm_lb10:;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
                GM_ST(uint8_t, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 0), ((uint8_t)2ULL));
                goto gm_fdone;
            }
            gm_b16_8 t190 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
            gm_b16_8 t191 = GM_LD(gm_b16_8, ((uint8_t *)&t190));
            gm_l30 = t191;
            gm_l31 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t192 = gm_l31;
                    uint64_t t193 = GM_LD(uint64_t, (((uint8_t *)&gm_l30) + 8));
                    uint8_t t194 = (t192 < t193);
                    if (!t194) {
                        goto gm_lb15;
                    }
                    {
                        uint64_t t195 = gm_l31;
                        void * t196 = GM_LD(void *, ((uint8_t *)&gm_l30));
                        uint64_t t197 = GM_LD(uint64_t, (((uint8_t *)&gm_l30) + 8));
                        uint64_t t198 = gm_l31;
                        gm_b16_8 t199 = GM_LD(gm_b16_8, (((uint8_t *)t196) + (uintptr_t)t198 * 16));
                        GM_ST(uint64_t, ((uint8_t *)&gm_l52 + 0), t195);
                        GM_ST(gm_b16_8, ((uint8_t *)&gm_l52 + 8), t199);
                        {
                            {
                                uint64_t t200 = GM_LD(uint64_t, ((uint8_t *)&gm_l52));
                                gm_l32 = t200;
                                gm_b16_8 t201 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l52) + 8));
                                gm_l33 = t201;
                                gm_b16_8 t202 = gm_l33;
                                void * t203 = GM_LD(void *, ((uint8_t *)&F.l14));
                                uint64_t t204 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 8));
                                uint64_t t205 = gm_l32;
                                if ((uintptr_t)t205 >= (uintptr_t)(t204)) gm_trap_index((uintptr_t)t205, t204, gm_src4rbevsr9gjng, 223, 35);
                                gm_b16_8 t206 = GM_LD(gm_b16_8, (((uint8_t *)t203) + (uintptr_t)t205 * 16));
                                gm_ttsm01t0d8srg t207 = gm_f_rename_or_remove_l80dbfs((void *)NULL, cx, NULL, t202, t206);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l53 = t207;
                                uint8_t t208 = (uint8_t)((((uint8_t *)&gm_l53))[0] != 0);
                                if (t208) {
                                } else {
                                    void * t209 = GM_LD(void *, ((uint8_t *)&F.l14));
                                    uint64_t t210 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 8));
                                    uint64_t t211 = gm_l32;
                                    if ((uintptr_t)t211 >= (uintptr_t)(t210)) gm_trap_index((uintptr_t)t211, t210, gm_src4rbevsr9gjng, 224, 63);
                                    gm_b16_8 t212 = GM_LD(gm_b16_8, (((uint8_t *)t209) + (uintptr_t)t211 * 16));
                                    if (gm_pf454qfrpt6r(cx, gm_shl253arneuel, 18, gm_src4rbevsr9gjng, 224, 17)) {
                                        goto gm_lad;
                                    }
                                    if ((GM_LD(uintptr_t, ((uint8_t *)&t212) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t212)), GM_LD(uintptr_t, ((uint8_t *)&t212) + 8), gm_src4rbevsr9gjng, 224, 17))) {
                                        goto gm_lad;
                                    }
                                    if (gm_pf454qfrpt6r(cx, gm_suvsl6ie8luas, 14, gm_src4rbevsr9gjng, 224, 17)) {
                                        goto gm_lad;
                                    }
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
                                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 0), ((uint8_t)2ULL));
                                    goto gm_fdone;
                                }
                                goto gm_lb18;
                            }
gm_lb19:;
                            gm_trap("no match arm matched the scrutinee", gm_src4rbevsr9gjng, 222, 9);
                        }
gm_lb18:;
                    }
gm_lb17:;
                    uint64_t t213 = gm_l31;
                    uint64_t r214; bool o215 = __builtin_add_overflow(t213, ((uint64_t)1ULL), &r214);
                    uint64_t t216 = r214;
                    gm_l31 = t216;
                }
            }
gm_lb15:;
            gm_b16_8 t217 = gm_l6;
            gm_b0_1 t218 = gm_f_prune_units_iqs7tqd((void *)NULL, cx, NULL, t217);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        gm_b16_8 t219 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
        gm_b16_8 t220 = GM_LD(gm_b16_8, ((uint8_t *)&t219));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 0), ((uint8_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t220);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_units_to_prune_qp6vjmt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, int64_t a1) {
    struct gm_fr_units_to_prune_qp6vjmt F;
    gm_b16_8 gm_l0; /* objs */
    int64_t gm_l1; /* now */
    int64_t gm_l3; /* recent */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    GM_STACK_CHECK(&F, 416u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l7;
    gm_trqfvmjtt4sij t7;
    gm_f_list_epvducg((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t7;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t9 = (t8 <= ((uint64_t)4000ULL));
    if (t9) {
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
        gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&t10));
        F.ret = t11;
        goto gm_fret;
    }
    gm_b16_8 t12 = gm_l0;
    gm_closure t13 = ((gm_closure){GM_PTR_TAG(gm_f_fn_c438h0n), NULL, cx});
    gm_f_sort_by_ci5a9mg(t12, t13);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    int64_t t14 = gm_l1;
    int64_t r15; bool o16 = __builtin_mul_overflow(((int64_t)86400LL), ((int64_t)1000000000LL), &r15);
    if (o16) gm_trap("integer overflow in `*`", gm_src4rbevsr9gjng, 272, 21);
    int64_t t17 = r15;
    int64_t r18; bool o19 = __builtin_sub_overflow(t14, t17, &r18);
    if (o19) gm_trap("integer overflow in `-`", gm_src4rbevsr9gjng, 272, 15);
    int64_t t20 = r18;
    gm_l3 = t20;
    gm_l5 = ((uint64_t)0ULL);
    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t r22; bool o23 = __builtin_sub_overflow(t21, ((uint64_t)4000ULL), &r22);
    if (o23) gm_trap("integer overflow in `-`", gm_src4rbevsr9gjng, 273, 18);
    uint64_t t24 = r22;
    gm_l4 = t24;
    {
        for (;;) {
            uint64_t t25 = gm_l5;
            uint64_t t26 = gm_l4;
            uint8_t t27 = (t25 < t26);
            if (!t27) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t28 = gm_l5;
                        gm_l6 = t28;
                        void * t29 = GM_LD(void *, ((uint8_t *)&gm_l0));
                        uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                        uint64_t t31 = gm_l6;
                        if ((uintptr_t)t31 >= (uintptr_t)(t30)) gm_trap_index((uintptr_t)t31, t30, gm_src4rbevsr9gjng, 274, 12);
                        int64_t t32 = GM_LD(int64_t, (((uint8_t *)t29) + (uintptr_t)t31 * 24));
                        int64_t t33 = gm_l3;
                        uint8_t t34 = (t32 >= t33);
                        if (t34) {
                            goto gm_lb1;
                        }
                        void * t35 = (void *)((uint8_t *)&F.l2);
                        void * t36 = GM_LD(void *, ((uint8_t *)&gm_l0));
                        uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                        uint64_t t38 = gm_l6;
                        if ((uintptr_t)t38 >= (uintptr_t)(t37)) gm_trap_index((uintptr_t)t38, t37, gm_src4rbevsr9gjng, 275, 18);
                        gm_b16_8 t39 = GM_LD(gm_b16_8, ((((uint8_t *)t36) + (uintptr_t)t38 * 24) + 8));
                        gm_tftb1odc739t5 t40 = gm_f_push_3ef746b(t35, t39);
                        gm_l7 = t40;
                        uint8_t t41 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                        if (!t41) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_src4rbevsr9gjng, 275, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src4rbevsr9gjng, 273, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t42 = gm_l5;
            uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
            uint64_t t45 = r43;
            gm_l5 = t45;
        }
    }
gm_lb1:;
    gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
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

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
typedef struct gm_cs6mnftt383dnt { void * f0; void * f1; } gm_cs6mnftt383dnt;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint64_t a[3]; } gm_tc0nbpdhofi37;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
_Static_assert(sizeof(gm_cs6mnftt383dnt) == 16 && _Alignof(gm_cs6mnftt383dnt) == 8 && offsetof(gm_cs6mnftt383dnt, f0) == 0 && offsetof(gm_cs6mnftt383dnt, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t f2; uint8_t f3; uint8_t pe[5]; } gm_t0to1u196c9pc;
typedef struct { uint32_t f0; uint32_t f1; gm_b16_8 f2; } gm_t2s5j3n0guq0t;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; } gm_t5hdnnftkup6m;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
_Static_assert(sizeof(gm_tc0nbpdhofi37) == 24 && _Alignof(gm_tc0nbpdhofi37) == 8 && offsetof(gm_tc0nbpdhofi37, a) == 0, "layout of `[3]usize`");
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { gm_t3mf58gv9kf3n f0; void * f1; uint64_t f2; uint64_t f3; uint64_t f4; } gm_tjt4ka19str13;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tk9n3vlila9qg;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0to1u196c9pc) == 24 && _Alignof(gm_t0to1u196c9pc) == 8 && offsetof(gm_t0to1u196c9pc, f0) == 0 && offsetof(gm_t0to1u196c9pc, f1) == 16 && offsetof(gm_t0to1u196c9pc, f2) == 17 && offsetof(gm_t0to1u196c9pc, f3) == 18, "layout of `Scan`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
typedef struct { gm_trqfvmjtt4sij f0; uint64_t f1; uint64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t2792tsg5eg72;
_Static_assert(sizeof(gm_t2s5j3n0guq0t) == 24 && _Alignof(gm_t2s5j3n0guq0t) == 8 && offsetof(gm_t2s5j3n0guq0t, f0) == 0 && offsetof(gm_t2s5j3n0guq0t, f1) == 4 && offsetof(gm_t2s5j3n0guq0t, f2) == 8, "layout of `(u32, u32, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t9qkh0rsooc8d v1; void * v2; void * v3; gm_tibupt4dmcqai v4; gm_b16_8 v5; gm_b16_8 v6; gm_t9qkh0rsooc8d v7; } f1; } gm_t3fcbuehh3bs2;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5hdnnftkup6m) == 24 && _Alignof(gm_t5hdnnftkup6m) == 8 && offsetof(gm_t5hdnnftkup6m, f0) == 0 && offsetof(gm_t5hdnnftkup6m, f1) == 16 && offsetof(gm_t5hdnnftkup6m, f2) == 20, "layout of `(string, u32, u32)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
typedef struct { void * f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t7bjbmukv26e7;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { void * f0; gm_th2lqbo77pvsc f1; } gm_tagc77imt2chm;
typedef struct { gm_b16_8 f0; gm_tv2uf4leil5hq f1; } gm_taiafe1jasg4m;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { gm_tibupt4dmcqai f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; gm_t79rbdv4rafe5 f2; uint8_t f3; uint8_t pe[7]; } gm_tcfiotghd1s5k;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_th2lqbo77pvsc f0; uint8_t f1; uint8_t pe[7]; } gm_ti5ramhglnvdt;
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
typedef struct { uint8_t f0; union { gm_ts6oqppjh0ba3 v0; } f1; } gm_tjsp7bhdn2e8j;
_Static_assert(sizeof(gm_tjt4ka19str13) == 48 && _Alignof(gm_tjt4ka19str13) == 8 && offsetof(gm_tjt4ka19str13, f0) == 0 && offsetof(gm_tjt4ka19str13, f1) == 16 && offsetof(gm_tjt4ka19str13, f2) == 24 && offsetof(gm_tjt4ka19str13, f3) == 32 && offsetof(gm_tjt4ka19str13, f4) == 40, "layout of `Arena`");
_Static_assert(sizeof(gm_tk9n3vlila9qg) == 40 && _Alignof(gm_tk9n3vlila9qg) == 8 && offsetof(gm_tk9n3vlila9qg, f0) == 0 && offsetof(gm_tk9n3vlila9qg, f1) == 16 && offsetof(gm_tk9n3vlila9qg, f2) == 32, "layout of `(lib: string, link_name: string, variadic: bool)`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tkacitepg5fk0;
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; } gm_tr8dvlu9ik4gj;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; uint64_t f2; void * f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_trqfvmjtt4sij f6; gm_tc0nbpdhofi37 f7; gm_trqfvmjtt4sij f8; gm_toceffb697p0u f9; gm_trqfvmjtt4sij f10; uint8_t f11; uint8_t pe[7]; } gm_trd6bg8is1v4n;
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t2792tsg5eg72) == 64 && _Alignof(gm_t2792tsg5eg72) == 8 && offsetof(gm_t2792tsg5eg72, f0) == 0 && offsetof(gm_t2792tsg5eg72, f1) == 40 && offsetof(gm_t2792tsg5eg72, f2) == 48 && offsetof(gm_t2792tsg5eg72, f3) == 56, "layout of `Diagnostics`");
_Static_assert(sizeof(gm_t3fcbuehh3bs2) == 32 && _Alignof(gm_t3fcbuehh3bs2) == 8 && offsetof(gm_t3fcbuehh3bs2, f0) == 0 && offsetof(gm_t3fcbuehh3bs2, f1) == 8, "layout of `TokenKind`");
typedef struct { gm_t1s75i08qtptd f0; gm_t08olq97rscn3 f1; gm_t79rbdv4rafe5 f2; } gm_t43d6nbdfap6p;
typedef struct { gm_b16_8 f0; gm_tjsp7bhdn2e8j f1; uint8_t p2[5]; void * f2; } gm_t4d1qrgh0vnq1;
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_t7bjbmukv26e7) == 64 && _Alignof(gm_t7bjbmukv26e7) == 8 && offsetof(gm_t7bjbmukv26e7, f0) == 0 && offsetof(gm_t7bjbmukv26e7, f1) == 8 && offsetof(gm_t7bjbmukv26e7, f2) == 24 && offsetof(gm_t7bjbmukv26e7, f3) == 48, "layout of `WhenItems`");
typedef struct { gm_th2lqbo77pvsc f0; gm_ti5ramhglnvdt f1; uint8_t f2; uint8_t pe[7]; } gm_t9feugf5hcdtg;
_Static_assert(sizeof(gm_tagc77imt2chm) == 32 && _Alignof(gm_tagc77imt2chm) == 8 && offsetof(gm_tagc77imt2chm, f0) == 0 && offsetof(gm_tagc77imt2chm, f1) == 8, "layout of `PathPattern`");
_Static_assert(sizeof(gm_taiafe1jasg4m) == 80 && _Alignof(gm_taiafe1jasg4m) == 8 && offsetof(gm_taiafe1jasg4m, f0) == 0 && offsetof(gm_taiafe1jasg4m, f1) == 16, "layout of `(attrs: []Attribute, binding: Binding)`");
_Static_assert(sizeof(gm_tcfiotghd1s5k) == 88 && _Alignof(gm_tcfiotghd1s5k) == 8 && offsetof(gm_tcfiotghd1s5k, f0) == 0 && offsetof(gm_tcfiotghd1s5k, f1) == 24 && offsetof(gm_tcfiotghd1s5k, f2) == 40 && offsetof(gm_tcfiotghd1s5k, f3) == 80, "layout of `TestDecl`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
_Static_assert(sizeof(gm_ti5ramhglnvdt) == 32 && _Alignof(gm_ti5ramhglnvdt) == 8 && offsetof(gm_ti5ramhglnvdt, f0) == 0 && offsetof(gm_ti5ramhglnvdt, f1) == 24, "layout of `??string`");
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tjsp7bhdn2e8j) == 3 && _Alignof(gm_tjsp7bhdn2e8j) == 1 && offsetof(gm_tjsp7bhdn2e8j, f0) == 0 && offsetof(gm_tjsp7bhdn2e8j, f1) == 1, "layout of `AssignOp`");
_Static_assert(sizeof(gm_tkacitepg5fk0) == 72 && _Alignof(gm_tkacitepg5fk0) == 8 && offsetof(gm_tkacitepg5fk0, f0) == 0 && offsetof(gm_tkacitepg5fk0, f1) == 32 && offsetof(gm_tkacitepg5fk0, f2) == 56, "layout of `Attribute`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tv2uf4leil5hq v0; gm_t7bjbmukv26e7 v1; void * v2; gm_tcfiotghd1s5k v3; void * v4; } f1; } gm_tkoope2qgegd9;
typedef struct { gm_t1s75i08qtptd f0; gm_t9805qtonbsag f1; } gm_tm3mvv7hooqkm;
typedef struct { gm_t3fcbuehh3bs2 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tm4pc0emhrv1g;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_t08olq97rscn3 v1; void * v2; gm_tlcl4u558l6p5 v3; void * v4; gm_tagc77imt2chm v5; gm_b16_8 v6; } f1; } gm_to361lgknsu69;
_Static_assert(sizeof(gm_tr8dvlu9ik4gj) == 120 && _Alignof(gm_tr8dvlu9ik4gj) == 8 && offsetof(gm_tr8dvlu9ik4gj, f0) == 0 && offsetof(gm_tr8dvlu9ik4gj, f1) == 40 && offsetof(gm_tr8dvlu9ik4gj, f2) == 80, "layout of `Extras`");
_Static_assert(sizeof(gm_trd6bg8is1v4n) == 288 && _Alignof(gm_trd6bg8is1v4n) == 8 && offsetof(gm_trd6bg8is1v4n, f0) == 0 && offsetof(gm_trd6bg8is1v4n, f1) == 8 && offsetof(gm_trd6bg8is1v4n, f2) == 24 && offsetof(gm_trd6bg8is1v4n, f3) == 32 && offsetof(gm_trd6bg8is1v4n, f4) == 40 && offsetof(gm_trd6bg8is1v4n, f5) == 80 && offsetof(gm_trd6bg8is1v4n, f6) == 120 && offsetof(gm_trd6bg8is1v4n, f7) == 160 && offsetof(gm_trd6bg8is1v4n, f8) == 184 && offsetof(gm_trd6bg8is1v4n, f9) == 224 && offsetof(gm_trd6bg8is1v4n, f10) == 240 && offsetof(gm_trd6bg8is1v4n, f11) == 280, "layout of `Lexer`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_to361lgknsu69 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t0d02142k4tu8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t43d6nbdfap6p) == 112 && _Alignof(gm_t43d6nbdfap6p) == 8 && offsetof(gm_t43d6nbdfap6p, f0) == 0 && offsetof(gm_t43d6nbdfap6p, f1) == 40 && offsetof(gm_t43d6nbdfap6p, f2) == 72, "layout of `WhileStmt`");
_Static_assert(sizeof(gm_t4d1qrgh0vnq1) == 32 && _Alignof(gm_t4d1qrgh0vnq1) == 8 && offsetof(gm_t4d1qrgh0vnq1, f0) == 0 && offsetof(gm_t4d1qrgh0vnq1, f1) == 16 && offsetof(gm_t4d1qrgh0vnq1, f2) == 24, "layout of `(places: []Expr, op: AssignOp, value: ^Expr)`");
_Static_assert(sizeof(gm_t9feugf5hcdtg) == 64 && _Alignof(gm_t9feugf5hcdtg) == 8 && offsetof(gm_t9feugf5hcdtg, f0) == 0 && offsetof(gm_t9feugf5hcdtg, f1) == 24 && offsetof(gm_t9feugf5hcdtg, f2) == 56, "layout of `Classified`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
typedef struct { uint64_t f0; gm_tm4pc0emhrv1g f1; } gm_tfhcigdju4v01;
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tkoope2qgegd9) == 96 && _Alignof(gm_tkoope2qgegd9) == 8 && offsetof(gm_tkoope2qgegd9, f0) == 0 && offsetof(gm_tkoope2qgegd9, f1) == 8, "layout of `ItemKind`");
_Static_assert(sizeof(gm_tm3mvv7hooqkm) == 48 && _Alignof(gm_tm3mvv7hooqkm) == 8 && offsetof(gm_tm3mvv7hooqkm, f0) == 0 && offsetof(gm_tm3mvv7hooqkm, f1) == 40, "layout of `Parsed`");
_Static_assert(sizeof(gm_tm4pc0emhrv1g) == 48 && _Alignof(gm_tm4pc0emhrv1g) == 8 && offsetof(gm_tm4pc0emhrv1g, f0) == 0 && offsetof(gm_tm4pc0emhrv1g, f1) == 32, "layout of `Token`");
_Static_assert(sizeof(gm_to361lgknsu69) == 40 && _Alignof(gm_to361lgknsu69) == 8 && offsetof(gm_to361lgknsu69, f0) == 0 && offsetof(gm_to361lgknsu69, f1) == 8, "layout of `PatternKind`");
typedef struct { uint32_t f0; gm_tl874t2atnlh3 f1; gm_th2lqbo77pvsc f2; gm_b16_8 f3; gm_t9qkh0rsooc8d f4; uint8_t p5[7]; gm_tkoope2qgegd9 f5; } gm_tvpnktv5fo8nl;
_Static_assert(sizeof(gm_t0d02142k4tu8) == 64 && _Alignof(gm_t0d02142k4tu8) == 8 && offsetof(gm_t0d02142k4tu8, f0) == 0 && offsetof(gm_t0d02142k4tu8, f1) == 8 && offsetof(gm_t0d02142k4tu8, f2) == 48, "layout of `Pattern`");
typedef struct { gm_t0d02142k4tu8 f0; void * f1; void * f2; } gm_t15nkjiutlvf3;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_tfhcigdju4v01) == 56 && _Alignof(gm_tfhcigdju4v01) == 8 && offsetof(gm_tfhcigdju4v01, f0) == 0 && offsetof(gm_tfhcigdju4v01, f1) == 8, "layout of `(i: usize, x: Token)`");
_Static_assert(sizeof(gm_tvpnktv5fo8nl) == 160 && _Alignof(gm_tvpnktv5fo8nl) == 8 && offsetof(gm_tvpnktv5fo8nl, f0) == 0 && offsetof(gm_tvpnktv5fo8nl, f1) == 4 && offsetof(gm_tvpnktv5fo8nl, f2) == 16 && offsetof(gm_tvpnktv5fo8nl, f3) == 40 && offsetof(gm_tvpnktv5fo8nl, f4) == 56 && offsetof(gm_tvpnktv5fo8nl, f5) == 64, "layout of `Item`");
_Static_assert(sizeof(gm_t15nkjiutlvf3) == 80 && _Alignof(gm_t15nkjiutlvf3) == 8 && offsetof(gm_t15nkjiutlvf3, f0) == 0 && offsetof(gm_t15nkjiutlvf3, f1) == 64 && offsetof(gm_t15nkjiutlvf3, f2) == 72, "layout of `(pat: Pattern, value: ^Expr, fallback: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_taiafe1jasg4m v0; gm_t4d1qrgh0vnq1 v1; void * v2; gm_tgtub0v94d5u5 v3; void * v4; gm_t43d6nbdfap6p v5; void * v6; gm_t15nkjiutlvf3 v7; gm_t79rbdv4rafe5 v8; } f1; } gm_tem8p0t66u1ce;
_Static_assert(sizeof(gm_tem8p0t66u1ce) == 120 && _Alignof(gm_tem8p0t66u1ce) == 8 && offsetof(gm_tem8p0t66u1ce, f0) == 0 && offsetof(gm_tem8p0t66u1ce, f1) == 8, "layout of `StmtKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_tem8p0t66u1ce f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_to88ae1b67aev;
_Static_assert(sizeof(gm_to88ae1b67aev) == 144 && _Alignof(gm_to88ae1b67aev) == 8 && offsetof(gm_to88ae1b67aev, f0) == 0 && offsetof(gm_to88ae1b67aev, f1) == 8 && offsetof(gm_to88ae1b67aev, f2) == 128, "layout of `Stmt`");
struct gm_fr_allocator_5m783ja {
    gm_frame h;
    gm_t3mf58gv9kf3n ret;
};
struct gm_fr_classify_in_4kp1t31 {
    gm_frame h;
    gm_t9feugf5hcdtg l2; /* none */
    gm_t9feugf5hcdtg l3; /* c */
    gm_tm3mvv7hooqkm l4; /* items_parse */
    gm_trqfvmjtt4sij l6; /* items */
    gm_tvpnktv5fo8nl l10; /* it */
    gm_trqfvmjtt4sij l11; /* names */
    gm_tv2uf4leil5hq l13; /* b */
    gm_tm3mvv7hooqkm l17; /* stmts_parse */
    gm_tv2uf4leil5hq l19; /* b */
    gm_t79rbdv4rafe5 l21; /* block */
    gm_trqfvmjtt4sij l23; /* bindings */
    gm_to88ae1b67aev l26; /* st */
    gm_taiafe1jasg4m l27; /* bd */
    gm_t9feugf5hcdtg ret;
};
struct gm_fr_classify_rm38cme {
    gm_frame h;
    gm_tjt4ka19str13 l1; /* a */
    gm_t9feugf5hcdtg l2; /* c */
    gm_t9feugf5hcdtg ret;
    gm_t9feugf5hcdtg w0;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_depth_js5aj0d {
    gm_frame h;
    gm_tm4pc0emhrv1g l4; /* t */
    int64_t ret;
};
struct gm_fr_diagnostics_n3kkhf4 {
    gm_frame h;
    gm_t2792tsg5eg72 ret;
};
struct gm_fr_empty_slice_1th396a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8jt91p4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fcinhvp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lpi80k4_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s801pu6_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_fn_8no2c2v {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_has_attr_7fhere0 {
    gm_frame h;
    gm_tkacitepg5fk0 l4; /* a */
    uint8_t ret;
};
struct gm_fr_has_errors_if7ai0m {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_blank_asoolmf {
    gm_frame h;
    gm_tjt4ka19str13 l1; /* a */
    gm_tm4pc0emhrv1g l6; /* t */
    uint8_t ret;
    uint8_t w0;
};
struct gm_fr_is_definition_3en3eog {
    gm_frame h;
    gm_t2gtbh39ptekd l0; /* kind */
    uint8_t ret;
};
struct gm_fr_lex_dc8mbmf {
    gm_frame h;
    gm_trd6bg8is1v4n l3; /* lx */
    gm_tr8dvlu9ik4gj ret;
};
struct gm_fr_list_293fcpb {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_bvkp9ir {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_f0gjjbh_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_jekodor_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_prbqo9j {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lookup_var_3vpm3u6 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_may_mutate_uj4e7jp {
    gm_frame h;
    gm_tjt4ka19str13 l1; /* a */
    gm_tm4pc0emhrv1g l7; /* t */
    uint8_t ret;
    uint8_t w0;
};
struct gm_fr_new_jr5ejs0 {
    gm_frame h;
    gm_tjt4ka19str13 ret;
};
struct gm_fr_open_brackets_hicehud {
    gm_frame h;
    gm_tjt4ka19str13 l1; /* a */
    uint8_t ret;
    uint8_t w0;
};
struct gm_fr_own_46jsjq2 {
    gm_frame h;
    gm_t9feugf5hcdtg l0; /* c */
    gm_t9feugf5hcdtg l1; /* out */
    gm_trqfvmjtt4sij l3; /* copy */
    gm_tk9n3vlila9qg l6; /* it */
    gm_trqfvmjtt4sij l7; /* names */
    gm_trqfvmjtt4sij l12; /* copy */
    gm_t9feugf5hcdtg ret;
};
struct gm_fr_parse_items_g2pt6ce {
    gm_frame h;
    gm_tin97tt4ud0s1 l1; /* sm */
    gm_t2792tsg5eg72 l2; /* diags */
    gm_tm3mvv7hooqkm ret;
};
struct gm_fr_parse_source_i1af11u {
    gm_frame h;
    gm_tr8dvlu9ik4gj l4; /* lexed */
    gm_t4gut2c9fj1kt l10; /* d */
    gm_tm3mvv7hooqkm ret;
};
struct gm_fr_parse_stmts_mefdnbk {
    gm_frame h;
    gm_tin97tt4ud0s1 l1; /* sm */
    gm_t2792tsg5eg72 l2; /* diags */
    gm_trqfvmjtt4sij l3; /* src */
    gm_tm3mvv7hooqkm ret;
};
struct gm_fr_push_1e500nc_2 {
    gm_frame h;
    gm_tk9n3vlila9qg l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_6fomkur {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_k96mobn_2 {
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
struct gm_fr_regrow_5dhp5la {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_o86m6d7_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_urfcki5_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_rename_47b7og5 {
    gm_frame h;
    gm_tjt4ka19str13 l3; /* a */
    gm_b16_8 ret;
    gm_b16_8 w0;
};
struct gm_fr_rename_in_nstmasl {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* edits */
    gm_tm4pc0emhrv1g l11; /* t */
    gm_trqfvmjtt4sij l20; /* out */
    gm_b16_8 ret;
};
struct gm_fr_scan_e9v4omo {
    gm_frame h;
    gm_tin97tt4ud0s1 l1; /* sm */
    gm_t2792tsg5eg72 l3; /* diags */
    gm_tr8dvlu9ik4gj l4; /* out */
    gm_t4gut2c9fj1kt l8; /* d */
    gm_t0to1u196c9pc ret;
};
struct gm_fr_sort_by_jo6rcsi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_source_map_4v8dg4b {
    gm_frame h;
    gm_tin97tt4ud0s1 ret;
};
struct gm_fr_syntax_errors_fk23f5t {
    gm_frame h;
    gm_tjt4ka19str13 l2; /* a */
    gm_b16_8 ret;
    gm_b16_8 w0;
};
struct gm_fr_syntax_errors_in_o8fnm4b {
    gm_frame h;
    gm_tin97tt4ud0s1 l10; /* sm */
    gm_t2792tsg5eg72 l11; /* diags */
    gm_trqfvmjtt4sij l12; /* src */
    gm_b16_8 ret;
};
struct gm_fr_truncate_ov4pgoq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_write_qe3m8jq {
    gm_frame h;
    gm_tfeab279c16rr l1; /* args */
    gm_tftb1odc739t5 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) uint32_t gm_f_add_pva1tcq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_allocator_5m783ja(void * a0);
__attribute__((aligned(4))) void gm_f_classify_in_4kp1t31(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_classify_rm38cme(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_deinit_sejdpqv(void * a0);
__attribute__((aligned(4))) int64_t gm_f_depth_js5aj0d(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_diagnostics_n3kkhf4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_dupe_g9iqpvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8jt91p4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fcinhvp(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lpi80k4_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s801pu6_2(void);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_8no2c2v(void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_has_attr_7fhere0(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_errors_if7ai0m(void * a0);
__attribute__((aligned(4))) void gm_f_introsort_3cueg4e(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) uint8_t gm_f_is_blank_asoolmf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_is_definition_3en3eog(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_definition_3en3eog_c(gm_t2gtbh39ptekd a0);
inline __attribute__((aligned(4))) void gm_f_lex_dc8mbmf(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_list_293fcpb(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_bvkp9ir(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_f0gjjbh_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_jekodor_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_prbqo9j(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_lookup_var_3vpm3u6(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_lookup_var_3vpm3u6_c(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_may_mutate_uj4e7jp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_new_jr5ejs0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_new_lexer_dm12g12(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_open_brackets_hicehud(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_own_46jsjq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_parse_file_70g7vok(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, void * a1, void * a2, void * a3);
inline __attribute__((aligned(4))) void gm_f_parse_items_g2pt6ce(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_source_i1af11u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_parse_stmts_mefdnbk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1e500nc_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_6fomkur(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k96mobn_2(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5dhp5la(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o86m6d7_2(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_urfcki5_2(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_rename_47b7og5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_rename_in_nstmasl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_render_region_ppot3lm(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, uint32_t a3, gm_b16_8 a4, gm_t9qkh0rsooc8d a5);
__attribute__((aligned(4))) void gm_f_run_v26is74(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_scan_e9v4omo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_sort_by_jo6rcsi(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) void gm_f_source_map_4v8dg4b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_syntax_errors_fk23f5t(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_syntax_errors_in_o8fnm4b(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_6sjv6rg(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_truncate_ov4pgoq(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_qe3m8jq(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_s3v41br(void * a0, const void * a1, void * a2, gm_b16_8 a3);
extern uint8_t gm_cbms0bo4q1a[16] __attribute__((aligned(8)));
extern uint8_t gm_cfc3i2mbui9[16] __attribute__((aligned(8)));
extern uint8_t gm_cjlj93gp0or[24] __attribute__((aligned(8)));
extern void *const gm_hd3t1u951rul[];
extern uint8_t gm_k3mmr0p0mh8[40] __attribute__((aligned(8)));
extern uint8_t gm_k61ski6fvbu[40] __attribute__((aligned(8)));
extern uint8_t gm_kd6k1g0uehn[40] __attribute__((aligned(8)));
extern uint8_t gm_klim7pfh497[40] __attribute__((aligned(8)));
extern uint8_t gm_kmgp10tqfjk[40] __attribute__((aligned(8)));
extern uint8_t gm_krt0ppukvqv[40] __attribute__((aligned(8)));
extern const uint8_t gm_s5t1b3eks758h[27];
extern const uint8_t gm_sb2k9e71jsraf[7];
extern const uint8_t gm_sb3u9q4alu9bg[7];
extern const uint8_t gm_sfcm41q17pr37[46];
extern const uint8_t gm_sptnm8c6c52dr[7];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcip69en21ht7v[];
extern const char gm_srcr4magk9naq4q[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_sshj97ls6n4f3[18];
extern const gm_tmpl gm_tplfqr816g9de0k;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_allocator_5m783ja(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_allocator_5m783ja F;
    void * gm_l0; /* a */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = t2;
    void * t4 = (void *)((uint8_t *)gm_cjlj93gp0or);
    gm_t3mf58gv9kf3n t5;
    GM_ST(void *, ((uint8_t *)&t5 + 0), t3);
    GM_ST(void *, ((uint8_t *)&t5 + 8), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return GM_CONV(gm_cs6mnftt383dnt, F.ret);
gm_fprop:
    return GM_CONV(gm_cs6mnftt383dnt, F.ret);
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
inline __attribute__((aligned(4))) void gm_f_diagnostics_n3kkhf4(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_diagnostics_n3kkhf4 F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t2;
    gm_f_list_prbqo9j((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(uint32_t, (uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 60, 0);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 40), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 48), ((uint64_t)0ULL));
    GM_ST(uint32_t, ((uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 56), ((uint32_t)0ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8jt91p4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8jt91p4 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fcinhvp(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fcinhvp F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lpi80k4_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lpi80k4_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s801pu6_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_s801pu6_2 F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_8no2c2v(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_8no2c2v F;
    void * gm_l0; /* x */
    void * gm_l1; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 51u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, ((uint8_t *)t2));
    void * t4 = gm_l1;
    uint64_t t5 = GM_LD(uint64_t, ((uint8_t *)t4));
    uint8_t t6 = (t3 > t5);
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_has_errors_if7ai0m(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_errors_if7ai0m F;
    void * gm_l0; /* ds */
    GM_STACK_CHECK_SHALLOW(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 40));
    uint8_t t4 = (t3 > ((uint64_t)0ULL));
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_lex_dc8mbmf(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_lex_dc8mbmf F;
    uint32_t gm_l0; /* file */
    gm_b16_8 gm_l1; /* src */
    void * gm_l2; /* diags */
    GM_STACK_CHECK(&F, 352u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint32_t t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        void * t4 = gm_l2;
        gm_trd6bg8is1v4n t5;
        gm_f_new_lexer_dm12g12((void *)&t5, (void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l3 = t5;
    }
    {
        void * t6 = (void *)((uint8_t *)&F.l3);
        gm_f_run_v26is74((void *)&(*(gm_tr8dvlu9ik4gj *)gm_out), (void *)NULL, cx, NULL, t6);
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
inline __attribute__((aligned(4))) void gm_f_list_293fcpb(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_293fcpb F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_fcinhvp();
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
inline __attribute__((aligned(4))) void gm_f_list_bvkp9ir(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_bvkp9ir F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_8jt91p4();
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
inline __attribute__((aligned(4))) void gm_f_list_f0gjjbh_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_f0gjjbh_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_lpi80k4_2();
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
inline __attribute__((aligned(4))) void gm_f_list_jekodor_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_jekodor_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_s801pu6_2();
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
inline __attribute__((aligned(4))) void gm_f_new_jr5ejs0(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_new_jr5ejs0 F;
    GM_STACK_CHECK(&F, 16u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_node *pn2 = gm_find(cx, 587034433);
    if (!pn2) gm_trap("no handler installed for effect `Alloc`", gm_srcip69en21ht7v, 41, 55);
    gm_t3mf58gv9kf3n t3;
    t3 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn2->clauses[0])(pn2->env, pn2->hctx, pn2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 0), t3);
    GM_ST(void *, ((uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 16), ((void *)0));
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 24), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 32), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 40), ((uint64_t)0ULL));
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
inline __attribute__((aligned(4))) void gm_f_parse_items_g2pt6ce(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_parse_items_g2pt6ce F;
    gm_b16_8 gm_l0; /* text */
    GM_STACK_CHECK(&F, 152u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_tin97tt4ud0s1 t2;
        gm_f_source_map_4v8dg4b((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t2;
    }
    {
        gm_t2792tsg5eg72 t3;
        gm_f_diagnostics_n3kkhf4((void *)&t3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t3;
    }
    {
        gm_b16_8 t4 = gm_l0;
        void * t5 = (void *)((uint8_t *)&F.l1);
        void * t6 = (void *)((uint8_t *)&F.l2);
        gm_f_parse_source_i1af11u((void *)&(*(gm_tm3mvv7hooqkm *)gm_out), (void *)NULL, cx, NULL, t4, t5, t6);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1e500nc_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_1e500nc_2 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_lpi80k4_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_o86m6d7_2(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_6fomkur(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_6fomkur F;
    void * gm_l0; /* l */
    gm_t5hdnnftkup6m gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_fcinhvp();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_5dhp5la(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k96mobn_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_k96mobn_2 F;
    void * gm_l0; /* l */
    gm_tbccvikg6b96e gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_s801pu6_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_urfcki5_2(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5dhp5la(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_5dhp5la F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o86m6d7_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_o86m6d7_2 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_urfcki5_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_urfcki5_2 F;
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
inline __attribute__((aligned(4))) void gm_f_sort_by_jo6rcsi(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_by_jo6rcsi F;
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
    gm_f_introsort_3cueg4e(t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_source_map_4v8dg4b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_source_map_4v8dg4b F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t2;
    gm_f_list_bvkp9ir((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_tin97tt4ud0s1 *)gm_out) + 0), t2);
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
inline __attribute__((aligned(4))) void gm_f_truncate_ov4pgoq(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_ov4pgoq F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t2 = gm_l1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 8));
    uint8_t t5 = (t2 < t4);
    if (t5) {
        void * t6 = gm_l0;
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, ((uint8_t *)t7));
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
        uint64_t t10 = gm_l1;
        uint64_t t11 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t12 = (uintptr_t)t10;
        if (t11 > t12 || t12 > (uintptr_t)t9) gm_trap_slice(t11, t12, t9, gm_srcrgc9bkquf6nr, 125, 36);
        uint64_t t13 = ((uintptr_t)t10) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13));
        GM_ST(gm_b16_8, ((uint8_t *)t6), t14);
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_qe3m8jq(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_qe3m8jq F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sb2k9e71jsraf));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_tftb1odc739t5 t4 = gm_f_write_s3v41br(t2, &(*(gm_tfeab279c16rr *)((uint8_t *)a1)), ((void *)&gm_tplfqr816g9de0k), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_classify_in_4kp1t31(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_classify_in_4kp1t31 F;
    gm_b16_8 gm_l0; /* text */
    gm_t0to1u196c9pc gm_l1; /* s */
    gm_t08olq97rscn3 gm_l5; /* tree */
    uint8_t gm_l7; /* all_decls */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint8_t gm_l12; /* plain_const */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_t08olq97rscn3 gm_l16; /* n */
    gm_t08olq97rscn3 gm_l18; /* tree */
    void * gm_l20; /* f */
    uint32_t gm_l22; /* off */
    gm_b16_8 gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    gm_b16_8 gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    gm_t08olq97rscn3 gm_l30; /* n */
    uint8_t gm_l31; /* single */
    uint32_t gm_l32; /* end */
    uint8_t gm_l33; /* stmts_at_end */
    uint8_t gm_l34; /* items_at_end */
    GM_STACK_CHECK(&F, 1653u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l35;
    gm_tftb1odc739t5 gm_l36;
    uint8_t gm_l37;
    uint8_t gm_l38;
    uint8_t gm_l39;
    uint8_t gm_l40;
    gm_tftb1odc739t5 gm_l41;
    gm_tftb1odc739t5 gm_l42;
    uint8_t gm_l43;
    uint8_t gm_l44;
    uint8_t gm_l45;
    uint8_t gm_l46;
    gm_b16_8 gm_l47;
    uint8_t gm_l48;
    uint32_t gm_l49;
    uint8_t gm_l50;
    uint8_t gm_l51;
    uint32_t gm_l52;
    uint8_t gm_l53;
    uint8_t gm_l54;
    {
        gm_b16_8 t58 = gm_l0;
        gm_f_scan_e9v4omo((void *)&gm_l1, (void *)NULL, cx, NULL, t58);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t59;
        memset(&t59, 0, sizeof t59);
        gm_ti5ramhglnvdt t60;
        memset(&t60, 0, sizeof t60);
        GM_ST(uint8_t, (uint8_t *)&F.l2 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&F.l2 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&F.l2 + 60, 0);
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&F.l2 + 0), t59);
        GM_ST(gm_ti5ramhglnvdt, ((uint8_t *)&F.l2 + 24), t60);
        GM_ST(uint8_t, ((uint8_t *)&F.l2 + 56), ((uint8_t)0));
        uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 16));
        if (t61) {
            gm_l35 = ((uint8_t)1);
        } else {
            gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l1));
            gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&t62));
            int64_t t64 = gm_f_depth_js5aj0d(t63);
            uint8_t t65 = (t64 > ((int64_t)0LL));
            gm_l35 = t65;
        }
        uint8_t t66 = gm_l35;
        if (t66) {
            gm_th2lqbo77pvsc t67;
            memset(&t67, 0, sizeof t67);
            gm_ti5ramhglnvdt t68;
            memset(&t68, 0, sizeof t68);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t9feugf5hcdtg *)gm_out) + 57, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t9feugf5hcdtg *)gm_out) + 58, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t9feugf5hcdtg *)gm_out) + 60, 0);
            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&(*(gm_t9feugf5hcdtg *)gm_out) + 0), t67);
            GM_ST(gm_ti5ramhglnvdt, ((uint8_t *)&(*(gm_t9feugf5hcdtg *)gm_out) + 24), t68);
            GM_ST(uint8_t, ((uint8_t *)&(*(gm_t9feugf5hcdtg *)gm_out) + 56), ((uint8_t)1));
            goto gm_fdone;
        }
        uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 17));
        if (t69) {
            memmove(gm_out, ((uint8_t *)&F.l2), 64);
            goto gm_fdone;
        }
        memmove(((uint8_t *)&F.l3), ((uint8_t *)&F.l2), 64);
        gm_b16_8 t70 = gm_l0;
        gm_f_parse_items_g2pt6ce((void *)&F.l4, (void *)NULL, cx, NULL, t70);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        {
            {
                uint8_t t71 = (uint8_t)((((uint8_t *)&F.l4))[32] != 0);
                if (!t71) {
                    goto gm_lb2;
                }
                memmove(((uint8_t *)&gm_l5), ((uint8_t *)&F.l4), 32);
                uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint8_t t73 = (t72 > ((uint64_t)0ULL));
                if (t73) {
                    gm_trqfvmjtt4sij t74;
                    gm_f_list_f0gjjbh_2((void *)&t74, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l6 = t74;
                    gm_l7 = ((uint8_t)1);
                    gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
                    gm_l8 = t75;
                    gm_l9 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t76 = gm_l9;
                            uint64_t t77 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                            uint8_t t78 = (t76 < t77);
                            if (!t78) {
                                goto gm_lb3;
                            }
                            {
                                void * t79 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                uint64_t t81 = gm_l9;
                                {
                                    {
                                        {
                                            memmove(((uint8_t *)&F.l10), (((uint8_t *)t79) + (uintptr_t)t81 * 160), 160);
                                        }
                                        {
                                            gm_trqfvmjtt4sij t82;
                                            gm_f_list_epvducg((void *)&t82, (void *)NULL, cx, NULL);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            F.l11 = t82;
                                        }
                                        gm_l12 = ((uint8_t)0);
                                        {
                                            {
                                                uint8_t t83 = GM_LD(uint8_t, ((((uint8_t *)&F.l10) + 64) + 0));
                                                uint8_t t84 = (uint8_t)(t83 == ((uint8_t)0ULL));
                                                if (!t84) {
                                                    goto gm_lb9;
                                                }
                                                memmove(((uint8_t *)&F.l13), ((((uint8_t *)&F.l10) + 64) + 8), 64);
                                                gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 8));
                                                gm_l14 = t85;
                                                gm_l15 = ((uint64_t)0ULL);
                                                {
                                                    for (;;) {
                                                        uint64_t t86 = gm_l15;
                                                        uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                                                        uint8_t t88 = (t86 < t87);
                                                        if (!t88) {
                                                            goto gm_lb10;
                                                        }
                                                        {
                                                            void * t89 = GM_LD(void *, ((uint8_t *)&gm_l14));
                                                            uint64_t t90 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                                                            uint64_t t91 = gm_l15;
                                                            {
                                                                {
                                                                    memmove(((uint8_t *)&gm_l16), (((uint8_t *)t89) + (uintptr_t)t91 * 32), 32);
                                                                    void * t92 = (void *)((uint8_t *)&F.l11);
                                                                    gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l16));
                                                                    gm_tftb1odc739t5 t94 = gm_f_push_3ef746b(t92, t93);
                                                                    gm_l36 = t94;
                                                                    uint8_t t95 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
                                                                    if (!t95) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 1, gm_srcr4magk9naq4q, 183, 40);
                                                                    goto gm_lb13;
                                                                }
gm_lb14:;
                                                                gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 183, 21);
                                                            }
gm_lb13:;
                                                        }
gm_lb12:;
                                                        uint64_t t96 = gm_l15;
                                                        uint64_t r97; bool o98 = __builtin_add_overflow(t96, ((uint64_t)1ULL), &r97);
                                                        uint64_t t99 = r97;
                                                        gm_l15 = t99;
                                                    }
                                                }
gm_lb10:;
                                                gm_t9qkh0rsooc8d t100 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l13) + 32));
                                                uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&t100) + 0));
                                                uint8_t t102 = (uint8_t)((t101 == ((uint8_t)0ULL)));
                                                gm_t9qkh0rsooc8d t103;
                                                GM_ST(uint8_t, ((uint8_t *)&t103 + 0), ((uint8_t)0ULL));
                                                if (t102) {
                                                    {
                                                        {
                                                            uint8_t t104 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l13) + 24)) != 0);
                                                            if (t104) {
                                                                goto gm_lb16;
                                                            }
                                                            gm_l40 = ((uint8_t)1);
                                                            goto gm_lb15;
                                                        }
gm_lb16:;
                                                        {
                                                            gm_l40 = ((uint8_t)0);
                                                            goto gm_lb15;
                                                        }
gm_lb17:;
                                                        gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 184, 56);
                                                    }
gm_lb15:;
                                                    uint8_t t105 = gm_l40;
                                                    gm_l39 = t105;
                                                } else {
                                                    gm_l39 = ((uint8_t)0);
                                                }
                                                uint8_t t106 = gm_l39;
                                                if (t106) {
                                                    void * t107 = GM_LD(void *, (((uint8_t *)&F.l13) + 40));
                                                    uint8_t t108 = gm_f_is_definition_3en3eog(&(*(gm_t2gtbh39ptekd *)(((uint8_t *)t107) + 8)));
                                                    uint8_t t109 = (uint8_t)!t108;
                                                    gm_l38 = t109;
                                                } else {
                                                    gm_l38 = ((uint8_t)0);
                                                }
                                                uint8_t t110 = gm_l38;
                                                if (t110) {
                                                    gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 40));
                                                    gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&t111));
                                                    gm_b16_8 t113;
                                                    GM_ST(void *, (uint8_t *)&t113, (void *)(gm_sptnm8c6c52dr));
                                                    GM_ST(uintptr_t, (uint8_t *)&t113 + 8, (uintptr_t)(6));
                                                    uint8_t t114 = gm_f_has_attr_7fhere0(t112, t113);
                                                    uint8_t t115 = (uint8_t)!t114;
                                                    gm_l37 = t115;
                                                } else {
                                                    gm_l37 = ((uint8_t)0);
                                                }
                                                uint8_t t116 = gm_l37;
                                                gm_l12 = t116;
                                                {
                                                    {
                                                        gm_t9qkh0rsooc8d t117 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l13) + 32));
                                                        uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&t117) + 0));
                                                        uint8_t t119 = (uint8_t)((t118 == ((uint8_t)1ULL)));
                                                        if (!t119) {
                                                            goto gm_lb19;
                                                        }
                                                        gm_b16_8 t120 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 40));
                                                        gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&t120));
                                                        gm_b16_8 t122;
                                                        GM_ST(void *, (uint8_t *)&t122, (void *)(gm_sptnm8c6c52dr));
                                                        GM_ST(uintptr_t, (uint8_t *)&t122 + 8, (uintptr_t)(6));
                                                        gm_t9qkh0rsooc8d t123;
                                                        GM_ST(uint8_t, ((uint8_t *)&t123 + 0), ((uint8_t)1ULL));
                                                        uint8_t t124 = gm_f_has_attr_7fhere0(t121, t122);
                                                        uint8_t t125 = (uint8_t)!t124;
                                                        if (!t125) {
                                                            goto gm_lb19;
                                                        }
                                                        gm_l7 = ((uint8_t)0);
                                                        goto gm_lb18;
                                                    }
gm_lb19:;
                                                }
gm_lb18:;
                                                goto gm_lb8;
                                            }
gm_lb9:;
                                        }
gm_lb8:;
                                        {
                                            void * t126 = (void *)((uint8_t *)&F.l6);
                                            gm_b16_8 t127 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
                                            void * t128 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                            uint64_t t129 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                            uint32_t t130 = GM_LD(uint32_t, ((((uint8_t *)&F.l10) + 4) + 4));
                                            uint64_t t131 = (uint64_t)t130;
                                            uint32_t t132 = GM_LD(uint32_t, ((((uint8_t *)&F.l10) + 4) + 8));
                                            uint64_t t133 = (uint64_t)t132;
                                            uint64_t t134 = (uintptr_t)t131;
                                            uint64_t t135 = (uintptr_t)t133;
                                            if (t134 > t135 || t135 > (uintptr_t)t129) gm_trap_slice(t134, t135, t129, gm_srcr4magk9naq4q, 188, 59);
                                            uint64_t t136 = ((uintptr_t)t133) - (uintptr_t)t131;
                                            gm_b16_8 t137;
                                            GM_ST(void *, (uint8_t *)&t137, (void *)(((uint8_t *)t128 + (uintptr_t)t131 * 1)));
                                            GM_ST(uintptr_t, (uint8_t *)&t137 + 8, (uintptr_t)(t136));
                                            uint8_t t138 = gm_l12;
                                            gm_tk9n3vlila9qg t139;
                                            GM_ST(uint8_t, (uint8_t *)&t139 + 33, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t139 + 34, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t139 + 36, 0);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t139 + 0), t127);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t139 + 16), t137);
                                            GM_ST(uint8_t, ((uint8_t *)&t139 + 32), t138);
                                            gm_tftb1odc739t5 t140 = gm_f_push_1e500nc_2(t126, &t139);
                                            gm_l41 = t140;
                                        }
                                        uint8_t t141 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                                        if (!t141) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 1, gm_srcr4magk9naq4q, 188, 17);
                                        goto gm_lb6;
                                    }
gm_lb7:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 179, 13);
                                }
gm_lb6:;
                            }
gm_lb5:;
                            uint64_t t142 = gm_l9;
                            uint64_t r143; bool o144 = __builtin_add_overflow(t142, ((uint64_t)1ULL), &r143);
                            uint64_t t145 = r143;
                            gm_l9 = t145;
                        }
                    }
gm_lb3:;
                    uint8_t t146 = gm_l7;
                    if (t146) {
                        gm_b16_8 t147 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
                        gm_th2lqbo77pvsc t148;
                        GM_ST(uint8_t, (uint8_t *)&t148 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&t148 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&t148 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t148), t147);
                        ((uint8_t *)&t148)[16] = 1;
                        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&F.l3), t148);
                    }
                }
                goto gm_lb1;
            }
gm_lb2:;
        }
gm_lb1:;
    }
    {
        gm_b16_8 t149 = gm_l0;
        gm_f_parse_stmts_mefdnbk((void *)&F.l17, (void *)NULL, cx, NULL, t149);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        {
            {
                uint8_t t150 = (uint8_t)((((uint8_t *)&F.l17))[32] != 0);
                if (!t150) {
                    goto gm_lb21;
                }
                memmove(((uint8_t *)&gm_l18), ((uint8_t *)&F.l17), 32);
                uint64_t t151 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                uint8_t t152 = ((t151 == ((uint64_t)1ULL)));
                if (t152) {
                    {
                        {
                            void * t153 = GM_LD(void *, ((uint8_t *)&gm_l18));
                            uint64_t t154 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t154)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t154, gm_srcr4magk9naq4q, 196, 31);
                            uint8_t t155 = GM_LD(uint8_t, (((((uint8_t *)t153) + (uintptr_t)((uint64_t)0ULL) * 160) + 64) + 0));
                            uint8_t t156 = (uint8_t)(t155 == ((uint8_t)0ULL));
                            if (!t156) {
                                goto gm_lb23;
                            }
                            memmove(((uint8_t *)&F.l19), (((((uint8_t *)t153) + (uintptr_t)((uint64_t)0ULL) * 160) + 64) + 8), 64);
                            {
                                {
                                    void * t157 = GM_LD(void *, (((uint8_t *)&F.l19) + 40));
                                    uint8_t t158 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                                    uint8_t t159 = (uint8_t)(t158 == ((uint8_t)36ULL));
                                    if (!t159) {
                                        goto gm_lb25;
                                    }
                                    void * t160 = GM_LD(void *, ((((uint8_t *)t157) + 8) + 8));
                                    gm_l20 = t160;
                                    {
                                        {
                                            {
                                                void * t161 = gm_l20;
                                                uint8_t t162 = GM_LD(uint8_t, ((((uint8_t *)t161) + 88) + 0));
                                                uint8_t t163 = (uint8_t)(t162 == ((uint8_t)0ULL));
                                                if (!t163) {
                                                    goto gm_lb27;
                                                }
                                                memmove(((uint8_t *)&F.l21), ((((uint8_t *)t161) + 88) + 8), 40);
                                            }
                                            uint64_t t164 = GM_LD(uint64_t, (((uint8_t *)gm_cfc3i2mbui9) + 8));
                                            uint32_t t165 = (uint32_t)t164;
                                            gm_l22 = t165;
                                            {
                                                gm_trqfvmjtt4sij t166;
                                                gm_f_list_293fcpb((void *)&t166, (void *)NULL, cx, NULL);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                F.l23 = t166;
                                            }
                                            gm_b16_8 t167 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 8));
                                            gm_l24 = t167;
                                            gm_l25 = ((uint64_t)0ULL);
                                            {
                                                for (;;) {
                                                    uint64_t t168 = gm_l25;
                                                    uint64_t t169 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                                                    uint8_t t170 = (t168 < t169);
                                                    if (!t170) {
                                                        goto gm_lb28;
                                                    }
                                                    {
                                                        void * t171 = GM_LD(void *, ((uint8_t *)&gm_l24));
                                                        uint64_t t172 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                                                        uint64_t t173 = gm_l25;
                                                        {
                                                            {
                                                                memmove(((uint8_t *)&F.l26), (((uint8_t *)t171) + (uintptr_t)t173 * 144), 144);
                                                                {
                                                                    {
                                                                        uint8_t t174 = GM_LD(uint8_t, ((((uint8_t *)&F.l26) + 8) + 0));
                                                                        uint8_t t175 = (uint8_t)(t174 == ((uint8_t)0ULL));
                                                                        if (!t175) {
                                                                            goto gm_lb34;
                                                                        }
                                                                        memmove(((uint8_t *)&F.l27), ((((uint8_t *)&F.l26) + 8) + 8), 80);
                                                                        gm_b16_8 t176 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l27) + 16) + 8));
                                                                        gm_l28 = t176;
                                                                        gm_l29 = ((uint64_t)0ULL);
                                                                        {
                                                                            for (;;) {
                                                                                uint64_t t177 = gm_l29;
                                                                                uint64_t t178 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                                                                                uint8_t t179 = (t177 < t178);
                                                                                if (!t179) {
                                                                                    goto gm_lb35;
                                                                                }
                                                                                {
                                                                                    void * t180 = GM_LD(void *, ((uint8_t *)&gm_l28));
                                                                                    uint64_t t181 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                                                                                    uint64_t t182 = gm_l29;
                                                                                    {
                                                                                        {
                                                                                            {
                                                                                                memmove(((uint8_t *)&gm_l30), (((uint8_t *)t180) + (uintptr_t)t182 * 32), 32);
                                                                                            }
                                                                                            {
                                                                                                void * t183 = (void *)((uint8_t *)&F.l23);
                                                                                                gm_b16_8 t184 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l30));
                                                                                                uint32_t t185 = GM_LD(uint32_t, ((((uint8_t *)&gm_l30) + 16) + 4));
                                                                                                uint32_t t186 = gm_l22;
                                                                                                uint32_t r187; bool o188 = __builtin_sub_overflow(t185, t186, &r187);
                                                                                                if (o188) gm_trap("integer overflow in `-`", gm_srcr4magk9naq4q, 204, 75);
                                                                                                uint32_t t189 = r187;
                                                                                                uint32_t t190 = GM_LD(uint32_t, ((((uint8_t *)&gm_l30) + 16) + 8));
                                                                                                uint32_t t191 = gm_l22;
                                                                                                uint32_t r192; bool o193 = __builtin_sub_overflow(t190, t191, &r192);
                                                                                                if (o193) gm_trap("integer overflow in `-`", gm_srcr4magk9naq4q, 204, 96);
                                                                                                uint32_t t194 = r192;
                                                                                                gm_t5hdnnftkup6m t195;
                                                                                                GM_ST(gm_b16_8, ((uint8_t *)&t195 + 0), t184);
                                                                                                GM_ST(uint32_t, ((uint8_t *)&t195 + 16), t189);
                                                                                                GM_ST(uint32_t, ((uint8_t *)&t195 + 20), t194);
                                                                                                gm_tftb1odc739t5 t196 = gm_f_push_6fomkur(t183, &t195);
                                                                                                gm_l42 = t196;
                                                                                            }
                                                                                            uint8_t t197 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
                                                                                            if (!t197) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l42) + 1, gm_srcr4magk9naq4q, 204, 37);
                                                                                            goto gm_lb38;
                                                                                        }
gm_lb39:;
                                                                                        gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 203, 33);
                                                                                    }
gm_lb38:;
                                                                                }
gm_lb37:;
                                                                                uint64_t t198 = gm_l29;
                                                                                uint64_t r199; bool o200 = __builtin_add_overflow(t198, ((uint64_t)1ULL), &r199);
                                                                                uint64_t t201 = r199;
                                                                                gm_l29 = t201;
                                                                            }
                                                                        }
gm_lb35:;
                                                                        goto gm_lb33;
                                                                    }
gm_lb34:;
                                                                }
gm_lb33:;
                                                                goto gm_lb31;
                                                            }
gm_lb32:;
                                                            gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 201, 25);
                                                        }
gm_lb31:;
                                                    }
gm_lb30:;
                                                    uint64_t t202 = gm_l25;
                                                    uint64_t r203; bool o204 = __builtin_add_overflow(t202, ((uint64_t)1ULL), &r203);
                                                    uint64_t t205 = r203;
                                                    gm_l25 = t205;
                                                }
                                            }
gm_lb28:;
                                            uint64_t t206 = GM_LD(uint64_t, ((((uint8_t *)&F.l21) + 8) + 8));
                                            uint8_t t207 = ((t206 == ((uint64_t)1ULL)));
                                            if (t207) {
                                                void * t208 = GM_LD(void *, (((uint8_t *)&F.l21) + 8));
                                                uint64_t t209 = GM_LD(uint64_t, ((((uint8_t *)&F.l21) + 8) + 8));
                                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t209)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t209, gm_srcr4magk9naq4q, 208, 60);
                                                {
                                                    {
                                                        uint8_t t210 = GM_LD(uint8_t, (((((uint8_t *)t208) + (uintptr_t)((uint64_t)0ULL) * 144) + 8) + 0));
                                                        uint8_t t211 = (uint8_t)(t210 == ((uint8_t)2ULL));
                                                        if (!t211) {
                                                            goto gm_lb41;
                                                        }
                                                        gm_l44 = ((uint8_t)1);
                                                        goto gm_lb40;
                                                    }
gm_lb41:;
                                                    {
                                                        gm_l44 = ((uint8_t)0);
                                                        goto gm_lb40;
                                                    }
gm_lb42:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 208, 60);
                                                }
gm_lb40:;
                                                uint8_t t212 = gm_l44;
                                                gm_l43 = t212;
                                            } else {
                                                gm_l43 = ((uint8_t)0);
                                            }
                                            uint8_t t213 = gm_l43;
                                            gm_l31 = t213;
                                            uint64_t t214 = GM_LD(uint64_t, ((((uint8_t *)&F.l21) + 8) + 8));
                                            uint8_t t215 = (t214 > ((uint64_t)0ULL));
                                            if (t215) {
                                                gm_b16_8 t216 = GM_LD(gm_b16_8, ((uint8_t *)&F.l23));
                                                uint8_t t217 = gm_l31;
                                                gm_th2lqbo77pvsc t218;
                                                GM_ST(uint8_t, (uint8_t *)&t218 + 17, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t218 + 18, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t218 + 20, 0);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t218 + 0), t216);
                                                GM_ST(uint8_t, ((uint8_t *)&t218 + 16), t217);
                                                gm_ti5ramhglnvdt t219;
                                                GM_ST(uint8_t, (uint8_t *)&t219 + 25, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t219 + 26, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t219 + 28, 0);
                                                GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t219), t218);
                                                ((uint8_t *)&t219)[24] = 1;
                                                GM_ST(gm_ti5ramhglnvdt, (((uint8_t *)&F.l3) + 24), t219);
                                            }
                                            goto gm_lb26;
                                        }
gm_lb27:;
                                    }
gm_lb26:;
                                    goto gm_lb24;
                                }
gm_lb25:;
                            }
gm_lb24:;
                            goto gm_lb22;
                        }
gm_lb23:;
                    }
gm_lb22:;
                }
                goto gm_lb20;
            }
gm_lb21:;
        }
gm_lb20:;
    }
    {
        {
            {
                {
                    {
                        uint8_t t220 = (uint8_t)((((uint8_t *)&F.l3))[16] != 0);
                        if (t220) {
                            goto gm_lb46;
                        }
                        gm_l45 = ((uint8_t)1);
                        goto gm_lb45;
                    }
gm_lb46:;
                    {
                        gm_l45 = ((uint8_t)0);
                        goto gm_lb45;
                    }
gm_lb47:;
                    gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 215, 8);
                }
gm_lb45:;
                uint8_t t221 = gm_l45;
                if (!t221) {
                    goto gm_lb44;
                }
                {
                    {
                        uint8_t t222 = (uint8_t)(((((uint8_t *)&F.l3) + 24))[24] != 0);
                        if (t222) {
                            goto gm_lb49;
                        }
                        gm_l46 = ((uint8_t)1);
                        goto gm_lb48;
                    }
gm_lb49:;
                    {
                        gm_l46 = ((uint8_t)0);
                        goto gm_lb48;
                    }
gm_lb50:;
                    gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 215, 28);
                }
gm_lb48:;
                uint8_t t223 = gm_l46;
                if (!t223) {
                    goto gm_lb44;
                }
                gm_b16_8 t224 = gm_l0;
                gm_b16_8 t225 = gm_f_trim_end_6sjv6rg(t224);
                gm_l47 = t225;
                uint64_t t226 = GM_LD(uint64_t, (((uint8_t *)&gm_l47) + 8));
                uint32_t t227 = (uint32_t)t226;
                gm_l32 = t227;
                uint8_t t228 = (uint8_t)(((((uint8_t *)&F.l17) + 40))[4] != 0);
                if (t228) {
                    uint32_t t229 = GM_LD(uint32_t, (((uint8_t *)&F.l17) + 40));
                    gm_l49 = t229;
                } else {
                    gm_l49 = ((uint32_t)0ULL);
                }
                uint32_t t230 = gm_l49;
                uint64_t t231 = GM_LD(uint64_t, (((uint8_t *)gm_cfc3i2mbui9) + 8));
                uint32_t t232 = (uint32_t)t231;
                uint32_t t233 = gm_l32;
                uint32_t r234; bool o235 = __builtin_add_overflow(t232, t233, &r234);
                if (o235) gm_trap("integer overflow in `+`", gm_srcr4magk9naq4q, 217, 59);
                uint32_t t236 = r234;
                uint8_t t237 = (t230 >= t236);
                if (t237) {
                    {
                        {
                            uint8_t t238 = (uint8_t)(((((uint8_t *)&F.l17) + 40))[4] != 0);
                            if (t238) {
                                goto gm_lb52;
                            }
                            gm_l50 = ((uint8_t)0);
                            goto gm_lb51;
                        }
gm_lb52:;
                        {
                            gm_l50 = ((uint8_t)1);
                            goto gm_lb51;
                        }
gm_lb53:;
                        gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 217, 96);
                    }
gm_lb51:;
                    uint8_t t239 = gm_l50;
                    gm_l48 = t239;
                } else {
                    gm_l48 = ((uint8_t)0);
                }
                uint8_t t240 = gm_l48;
                gm_l33 = t240;
                uint8_t t241 = (uint8_t)(((((uint8_t *)&F.l4) + 40))[4] != 0);
                if (t241) {
                    uint32_t t242 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 40));
                    gm_l52 = t242;
                } else {
                    gm_l52 = ((uint32_t)0ULL);
                }
                uint32_t t243 = gm_l52;
                uint32_t t244 = gm_l32;
                uint8_t t245 = (t243 >= t244);
                if (t245) {
                    {
                        {
                            uint8_t t246 = (uint8_t)(((((uint8_t *)&F.l4) + 40))[4] != 0);
                            if (t246) {
                                goto gm_lb55;
                            }
                            gm_l53 = ((uint8_t)0);
                            goto gm_lb54;
                        }
gm_lb55:;
                        {
                            gm_l53 = ((uint8_t)1);
                            goto gm_lb54;
                        }
gm_lb56:;
                        gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 218, 67);
                    }
gm_lb54:;
                    uint8_t t247 = gm_l53;
                    gm_l51 = t247;
                } else {
                    gm_l51 = ((uint8_t)0);
                }
                uint8_t t248 = gm_l51;
                gm_l34 = t248;
                uint8_t t249 = gm_l33;
                if (t249) {
                    gm_l54 = ((uint8_t)1);
                } else {
                    uint8_t t250 = gm_l34;
                    gm_l54 = t250;
                }
                uint8_t t251 = gm_l54;
                GM_ST(uint8_t, (((uint8_t *)&F.l3) + 56), t251);
                goto gm_lb43;
            }
gm_lb44:;
        }
gm_lb43:;
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l3), 64);
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
inline __attribute__((aligned(4))) void gm_f_classify_rm38cme(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_classify_rm38cme F;
    gm_b16_8 gm_l0; /* text */
    GM_STACK_CHECK(&F, 264u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9feugf5hcdtg gm_l3;
    {
        {
            gm_tjt4ka19str13 t5;
            gm_f_new_jr5ejs0((void *)&t5, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l1 = t5;
        }
        {
            void * t6 = (void *)((uint8_t *)&F.l1);
            gm_t3mf58gv9kf3n t7 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_allocator_5m783ja(t6));
            gm_t3mf58gv9kf3n t8 = t7;
            gm_node wn9 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t8, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
            gm_b16_8 t12 = gm_l0;
            gm_f_classify_in_4kp1t31((void *)&gm_l3, (void *)NULL, (&wn9), NULL, t12);
            if (gm_unw.kind) {
                goto gm_ll11;
            }
        }
        goto gm_ls13;
gm_ll11:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we10; }
        goto gm_ld4_1;
gm_ls13:;
gm_lb2:;
        F.w0 = gm_l3;
we10:;
        gm_l3 = F.w0;
        {
            memmove(((uint8_t *)&F.l2), ((uint8_t *)&gm_l3), 64);
        }
        {
            gm_t9feugf5hcdtg t14 = GM_LD(gm_t9feugf5hcdtg, ((uint8_t *)&F.l2));
            gm_f_own_46jsjq2((void *)&F.ret, (void *)NULL, cx, NULL, &t14);
            if (gm_unw.kind) {
                goto gm_ld4_1;
            }
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
            goto gm_ld4_1;
        }
    }
    {
        void * t15 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_sejdpqv(t15);
    }
    goto gm_ls16;
gm_ld4_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t17 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_sejdpqv(t17);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls16:;
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
inline __attribute__((aligned(4))) int64_t gm_f_depth_js5aj0d(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_depth_js5aj0d F;
    gm_b16_8 gm_l0; /* tokens */
    int64_t gm_l1; /* d */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_t9qkh0rsooc8d gm_l5; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 193u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = ((int64_t)0LL);
    gm_b16_8 t21 = gm_l0;
    gm_l2 = t21;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t22 = gm_l3;
            uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t24 = (t22 < t23);
            if (!t24) {
                goto gm_lb1;
            }
            {
                void * t25 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t27 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&F.l4), (((uint8_t *)t25) + (uintptr_t)t27 * 48), 48);
                        {
                            {
                                uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                                uint8_t t29 = (uint8_t)(t28 == ((uint8_t)8ULL));
                                if (!t29) {
                                    goto gm_lb7;
                                }
                                gm_t9qkh0rsooc8d t30 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l4) + 8));
                                gm_l5 = t30;
                                {
                                    {
                                        {
                                            {
                                                uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                                uint8_t t32 = (uint8_t)(t31 == ((uint8_t)61ULL));
                                                if (!t32) {
                                                    goto gm_lb11;
                                                }
                                                goto gm_lb10;
                                            }
gm_lb11:;
                                            {
                                                uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                                uint8_t t34 = (uint8_t)(t33 == ((uint8_t)63ULL));
                                                if (!t34) {
                                                    goto gm_lb12;
                                                }
                                                goto gm_lb10;
                                            }
gm_lb12:;
                                            {
                                                uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                                uint8_t t36 = (uint8_t)(t35 == ((uint8_t)65ULL));
                                                if (!t36) {
                                                    goto gm_lb13;
                                                }
                                                goto gm_lb10;
                                            }
gm_lb13:;
                                            goto gm_lb9;
                                        }
gm_lb10:;
                                        int64_t t37 = gm_l1;
                                        int64_t r38; bool o39 = __builtin_add_overflow(t37, ((int64_t)1LL), &r38);
                                        if (o39) gm_trap("integer overflow in `+`", gm_srcr4magk9naq4q, 76, 50);
                                        int64_t t40 = r38;
                                        gm_l1 = t40;
                                        goto gm_lb8;
                                    }
gm_lb9:;
                                    {
                                        {
                                            {
                                                uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                                uint8_t t42 = (uint8_t)(t41 == ((uint8_t)62ULL));
                                                if (!t42) {
                                                    goto gm_lb16;
                                                }
                                                goto gm_lb15;
                                            }
gm_lb16:;
                                            {
                                                uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                                uint8_t t44 = (uint8_t)(t43 == ((uint8_t)64ULL));
                                                if (!t44) {
                                                    goto gm_lb17;
                                                }
                                                goto gm_lb15;
                                            }
gm_lb17:;
                                            {
                                                uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                                uint8_t t46 = (uint8_t)(t45 == ((uint8_t)66ULL));
                                                if (!t46) {
                                                    goto gm_lb18;
                                                }
                                                goto gm_lb15;
                                            }
gm_lb18:;
                                            goto gm_lb14;
                                        }
gm_lb15:;
                                        int64_t t47 = gm_l1;
                                        int64_t r48; bool o49 = __builtin_sub_overflow(t47, ((int64_t)1LL), &r48);
                                        if (o49) gm_trap("integer overflow in `-`", gm_srcr4magk9naq4q, 77, 50);
                                        int64_t t50 = r48;
                                        gm_l1 = t50;
                                        goto gm_lb8;
                                    }
gm_lb14:;
                                    {
                                        goto gm_lb8;
                                    }
gm_lb19:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 75, 13);
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
                    gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 73, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t51 = gm_l3;
            uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
            uint64_t t54 = r52;
            gm_l3 = t54;
        }
    }
gm_lb1:;
    int64_t t55 = gm_l1;
    F.ret = t55;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_fn_8no2c2v(void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_attr_7fhere0(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_attr_7fhere0 F;
    gm_b16_8 gm_l0; /* attrs */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 164u);
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
                        memmove(((uint8_t *)&F.l4), (((uint8_t *)t11) + (uintptr_t)t13 * 72), 72);
                        gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
                        gm_b16_8 t15 = gm_l1;
                        uint8_t t16 = (gm_eq0iqiubfm7e(((uint8_t *)&t14), ((uint8_t *)&t15)));
                        if (t16) {
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 152, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t17 = gm_l3;
            uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)1ULL), &r18);
            uint64_t t20 = r18;
            gm_l3 = t20;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_blank_asoolmf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_blank_asoolmf F;
    gm_b16_8 gm_l0; /* text */
    gm_t0to1u196c9pc gm_l2; /* s */
    uint8_t gm_l3; /* blank */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 269u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l7;
    {
        gm_tjt4ka19str13 t12;
        gm_f_new_jr5ejs0((void *)&t12, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t12;
        {
            void * t13 = (void *)((uint8_t *)&F.l1);
            gm_t3mf58gv9kf3n t14 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_allocator_5m783ja(t13));
            gm_t3mf58gv9kf3n t15 = t14;
            gm_node wn16 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t15, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
            gm_b16_8 t19 = gm_l0;
            gm_f_scan_e9v4omo((void *)&gm_l2, (void *)NULL, (&wn16), NULL, t19);
            if (gm_unw.kind) {
                goto gm_ll18;
            }
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 18));
            uint8_t t21 = (uint8_t)!t20;
            gm_l3 = t21;
            gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
            gm_l4 = t22;
            gm_l5 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t23 = gm_l5;
                    uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint8_t t25 = (t23 < t24);
                    if (!t25) {
                        goto gm_lb3;
                    }
                    {
                        void * t26 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        uint64_t t28 = gm_l5;
                        {
                            {
                                memmove(((uint8_t *)&F.l6), (((uint8_t *)t26) + (uintptr_t)t28 * 48), 48);
                                {
                                    {
                                        {
                                            uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                                            uint8_t t30 = (uint8_t)(!(t29 == ((uint8_t)9ULL)));
                                            if (!t30) {
                                                goto gm_lb9;
                                            }
                                        }
                                        {
                                            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                                            uint8_t t32 = (uint8_t)(!(t31 == ((uint8_t)10ULL)));
                                            if (!t32) {
                                                goto gm_lb9;
                                            }
                                        }
                                        gm_l3 = ((uint8_t)0);
                                        goto gm_lb8;
                                    }
gm_lb9:;
                                }
gm_lb8:;
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 104, 9);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t33 = gm_l5;
                    uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
                    uint64_t t36 = r34;
                    gm_l5 = t36;
                }
            }
gm_lb3:;
            uint8_t t37 = gm_l3;
            gm_l7 = t37;
        }
        goto gm_ls38;
gm_ll18:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we17; }
        goto gm_ld11_1;
gm_ls38:;
gm_lb2:;
        F.w0 = gm_l7;
we17:;
        gm_l7 = F.w0;
        uint8_t t39 = gm_l7;
        F.ret = t39;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
        goto gm_ld11_1;
    }
    {
        void * t40 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_sejdpqv(t40);
    }
    goto gm_ls41;
gm_ld11_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t42 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_sejdpqv(t42);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls41:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_definition_3en3eog(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_definition_3en3eog F;
    GM_STACK_CHECK_SHALLOW(&F, 14u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t gm_l1;
    {
        {
            {
                {
                    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
                    uint8_t t12 = (uint8_t)(t11 == ((uint8_t)36ULL));
                    if (!t12) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
                    uint8_t t14 = (uint8_t)(t13 == ((uint8_t)32ULL));
                    if (!t14) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
                    uint8_t t16 = (uint8_t)(t15 == ((uint8_t)33ULL));
                    if (!t16) {
                        goto gm_lb6;
                    }
                    goto gm_lb3;
                }
gm_lb6:;
                {
                    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
                    uint8_t t18 = (uint8_t)(t17 == ((uint8_t)34ULL));
                    if (!t18) {
                        goto gm_lb7;
                    }
                    goto gm_lb3;
                }
gm_lb7:;
                {
                    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
                    uint8_t t20 = (uint8_t)(t19 == ((uint8_t)38ULL));
                    if (!t20) {
                        goto gm_lb8;
                    }
                    goto gm_lb3;
                }
gm_lb8:;
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
gm_lb9:;
        gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 146, 52);
    }
gm_lb1:;
    uint8_t t21 = gm_l1;
    F.ret = t21;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_is_definition_3en3eog_c(gm_t2gtbh39ptekd a0) { return gm_f_is_definition_3en3eog(&a0); }
inline __attribute__((aligned(4))) void gm_f_lookup_var_3vpm3u6(void *gm_out, gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lookup_var_3vpm3u6 F;
    gm_b16_8 gm_l0; /* vars */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_t6j6smj7qoctu gm_l4; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 210u);
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
                        memmove(((uint8_t *)&gm_l4), (((uint8_t *)t11) + (uintptr_t)t13 * 32), 32);
                        gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                        gm_b16_8 t15 = gm_l1;
                        uint8_t t16 = (gm_eq0iqiubfm7e(((uint8_t *)&t14), ((uint8_t *)&t15)));
                        if (t16) {
                            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 16));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t17);
                            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                            goto gm_fdone;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 277, 5);
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
    memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_lookup_var_3vpm3u6_c(gm_b16_8 a0, gm_b16_8 a1) { gm_th2lqbo77pvsc gm_r; gm_f_lookup_var_3vpm3u6(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) uint8_t gm_f_may_mutate_uj4e7jp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_may_mutate_uj4e7jp F;
    gm_b16_8 gm_l0; /* text */
    gm_b16_8 gm_l2; /* tokens */
    uint8_t gm_l3; /* found */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    gm_t9qkh0rsooc8d gm_l8; /* p */
    GM_STACK_CHECK(&F, 594u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l9;
    gm_t0to1u196c9pc gm_l10;
    gm_tfhcigdju4v01 gm_l11;
    uint8_t gm_l12;
    {
        gm_tjt4ka19str13 t23;
        gm_f_new_jr5ejs0((void *)&t23, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t23;
        {
            void * t24 = (void *)((uint8_t *)&F.l1);
            gm_t3mf58gv9kf3n t25 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_allocator_5m783ja(t24));
            gm_t3mf58gv9kf3n t26 = t25;
            gm_node wn27 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t26, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
            gm_b16_8 t30 = gm_l0;
            gm_f_scan_e9v4omo((void *)&gm_l10, (void *)NULL, (&wn27), NULL, t30);
            if (gm_unw.kind) {
                goto gm_ll29;
            }
            gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l10));
            gm_l2 = t31;
            gm_l3 = ((uint8_t)0);
            gm_b16_8 t32 = gm_l2;
            gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
            gm_l4 = t33;
            gm_l5 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t34 = gm_l5;
                    uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint8_t t36 = (t34 < t35);
                    if (!t36) {
                        goto gm_lb3;
                    }
                    {
                        {
                            uint64_t t37 = gm_l5;
                            void * t38 = GM_LD(void *, ((uint8_t *)&gm_l4));
                            uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                            uint64_t t40 = gm_l5;
                            gm_tm4pc0emhrv1g t41 = GM_LD(gm_tm4pc0emhrv1g, (((uint8_t *)t38) + (uintptr_t)t40 * 48));
                            GM_ST(uint64_t, ((uint8_t *)&gm_l11 + 0), t37);
                            GM_ST(gm_tm4pc0emhrv1g, ((uint8_t *)&gm_l11 + 8), t41);
                        }
                        {
                            {
                                uint64_t t42 = GM_LD(uint64_t, ((uint8_t *)&gm_l11));
                                gm_l6 = t42;
                                memmove(((uint8_t *)&F.l7), (((uint8_t *)&gm_l11) + 8), 48);
                                {
                                    {
                                        uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
                                        uint8_t t44 = (uint8_t)(t43 == ((uint8_t)8ULL));
                                        if (!t44) {
                                            goto gm_lb9;
                                        }
                                        gm_t9qkh0rsooc8d t45 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l7) + 8));
                                        gm_l8 = t45;
                                        gm_t9qkh0rsooc8d t46 = gm_l8;
                                        uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&t46) + 0));
                                        uint8_t t48 = (uint8_t)((t47 == ((uint8_t)7ULL)));
                                        gm_t9qkh0rsooc8d t49;
                                        GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)7ULL));
                                        if (t48) {
                                            uint64_t t50 = gm_l6;
                                            uint8_t t51 = ((t50 == ((uint64_t)0ULL)));
                                            if (t51) {
                                                gm_l3 = ((uint8_t)1);
                                            } else {
                                                void * t52 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                                uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                                uint64_t t54 = gm_l6;
                                                uint64_t r55; bool o56 = __builtin_sub_overflow(t54, ((uint64_t)1ULL), &r55);
                                                if (o56) gm_trap("integer overflow in `-`", gm_srcr4magk9naq4q, 256, 38);
                                                uint64_t t57 = r55;
                                                if ((uintptr_t)t57 >= (uintptr_t)(t53)) gm_trap_index((uintptr_t)t57, t53, gm_srcr4magk9naq4q, 256, 31);
                                                {
                                                    {
                                                        {
                                                            {
                                                                uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)t52) + (uintptr_t)t57 * 48) + 0));
                                                                uint8_t t59 = (uint8_t)(t58 == ((uint8_t)0ULL));
                                                                if (!t59) {
                                                                    goto gm_lb13;
                                                                }
                                                                goto gm_lb12;
                                                            }
gm_lb13:;
                                                            {
                                                                uint8_t t60 = GM_LD(uint8_t, ((((uint8_t *)t52) + (uintptr_t)t57 * 48) + 0));
                                                                uint8_t t61 = (uint8_t)(t60 == ((uint8_t)3ULL));
                                                                if (!t61) {
                                                                    goto gm_lb14;
                                                                }
                                                                goto gm_lb12;
                                                            }
gm_lb14:;
                                                            goto gm_lb11;
                                                        }
gm_lb12:;
                                                        goto gm_lb10;
                                                    }
gm_lb11:;
                                                    {
                                                        gm_l3 = ((uint8_t)1);
                                                        goto gm_lb10;
                                                    }
gm_lb15:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 256, 25);
                                                }
gm_lb10:;
                                            }
                                        }
                                        {
                                            {
                                                gm_t9qkh0rsooc8d t62 = gm_l8;
                                                uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&t62) + 0));
                                                uint8_t t64 = (uint8_t)((t63 == ((uint8_t)58ULL)));
                                                if (!t64) {
                                                    goto gm_lb17;
                                                }
                                                uint64_t t65 = gm_l6;
                                                uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)2ULL), &r66);
                                                if (o67) gm_trap("integer overflow in `+`", gm_srcr4magk9naq4q, 262, 34);
                                                uint64_t t68 = r66;
                                                uint64_t t69 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                                uint8_t t70 = (t68 < t69);
                                                if (!t70) {
                                                    goto gm_lb17;
                                                }
                                                void * t71 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                                uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                                uint64_t t73 = gm_l6;
                                                uint64_t r74; bool o75 = __builtin_add_overflow(t73, ((uint64_t)1ULL), &r74);
                                                if (o75) gm_trap("integer overflow in `+`", gm_srcr4magk9naq4q, 262, 64);
                                                uint64_t t76 = r74;
                                                if ((uintptr_t)t76 >= (uintptr_t)(t72)) gm_trap_index((uintptr_t)t76, t72, gm_srcr4magk9naq4q, 262, 57);
                                                gm_t9qkh0rsooc8d t77;
                                                GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)58ULL));
                                                {
                                                    {
                                                        uint8_t t78 = GM_LD(uint8_t, ((((uint8_t *)t71) + (uintptr_t)t76 * 48) + 0));
                                                        uint8_t t79 = (uint8_t)(t78 == ((uint8_t)0ULL));
                                                        if (!t79) {
                                                            goto gm_lb19;
                                                        }
                                                        gm_l12 = ((uint8_t)1);
                                                        goto gm_lb18;
                                                    }
gm_lb19:;
                                                    {
                                                        gm_l12 = ((uint8_t)0);
                                                        goto gm_lb18;
                                                    }
gm_lb20:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 262, 57);
                                                }
gm_lb18:;
                                                uint8_t t80 = gm_l12;
                                                if (!t80) {
                                                    goto gm_lb17;
                                                }
                                                {
                                                    void * t81 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                                    uint64_t t82 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                                    uint64_t t83 = gm_l6;
                                                    uint64_t r84; bool o85 = __builtin_add_overflow(t83, ((uint64_t)2ULL), &r84);
                                                    if (o85) gm_trap("integer overflow in `+`", gm_srcr4magk9naq4q, 262, 100);
                                                    uint64_t t86 = r84;
                                                    if ((uintptr_t)t86 >= (uintptr_t)(t82)) gm_trap_index((uintptr_t)t86, t82, gm_srcr4magk9naq4q, 262, 93);
                                                    gm_t9qkh0rsooc8d t87;
                                                    GM_ST(uint8_t, ((uint8_t *)&t87 + 0), ((uint8_t)61ULL));
                                                    uint8_t t88 = GM_LD(uint8_t, ((((uint8_t *)t81) + (uintptr_t)t86 * 48) + 0));
                                                    uint8_t t89 = (uint8_t)(t88 == ((uint8_t)8ULL));
                                                    if (t89) {
                                                        gm_t9qkh0rsooc8d t90 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t81) + (uintptr_t)t86 * 48) + 8));
                                                        t89 = (uint8_t)gm_eqv3tj1pu43g(((uint8_t *)&t90), ((uint8_t *)&t87));
                                                    }
                                                    uint8_t t91 = (uint8_t)(t89);
                                                    if (!t91) {
                                                        goto gm_lb17;
                                                    }
                                                }
                                                gm_l3 = ((uint8_t)1);
                                                goto gm_lb16;
                                            }
gm_lb17:;
                                        }
gm_lb16:;
                                        goto gm_lb8;
                                    }
gm_lb9:;
                                }
gm_lb8:;
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 252, 9);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t92 = gm_l5;
                    uint64_t r93; bool o94 = __builtin_add_overflow(t92, ((uint64_t)1ULL), &r93);
                    uint64_t t95 = r93;
                    gm_l5 = t95;
                }
            }
gm_lb3:;
            uint8_t t96 = gm_l3;
            gm_l9 = t96;
        }
        goto gm_ls97;
gm_ll29:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we28; }
        goto gm_ld22_1;
gm_ls97:;
gm_lb2:;
        F.w0 = gm_l9;
we28:;
        gm_l9 = F.w0;
        uint8_t t98 = gm_l9;
        F.ret = t98;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
        goto gm_ld22_1;
    }
    {
        void * t99 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_sejdpqv(t99);
    }
    goto gm_ls100;
gm_ld22_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t101 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_sejdpqv(t101);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls100:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_open_brackets_hicehud(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_open_brackets_hicehud F;
    gm_b16_8 gm_l0; /* text */
    gm_t0to1u196c9pc gm_l2; /* s */
    GM_STACK_CHECK(&F, 208u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l3;
    uint8_t gm_l4;
    {
        gm_tjt4ka19str13 t5;
        gm_f_new_jr5ejs0((void *)&t5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t5;
        {
            void * t6 = (void *)((uint8_t *)&F.l1);
            gm_t3mf58gv9kf3n t7 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_allocator_5m783ja(t6));
            gm_t3mf58gv9kf3n t8 = t7;
            gm_node wn9 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t8, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
            gm_b16_8 t12 = gm_l0;
            gm_f_scan_e9v4omo((void *)&gm_l2, (void *)NULL, (&wn9), NULL, t12);
            if (gm_unw.kind) {
                goto gm_ll11;
            }
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 16));
            if (t13) {
                gm_l4 = ((uint8_t)1);
            } else {
                gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
                gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
                int64_t t16 = gm_f_depth_js5aj0d(t15);
                uint8_t t17 = (t16 > ((int64_t)0LL));
                gm_l4 = t17;
            }
            uint8_t t18 = gm_l4;
            gm_l3 = t18;
        }
        goto gm_ls19;
gm_ll11:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we10; }
        goto gm_ld4_1;
gm_ls19:;
gm_lb2:;
        F.w0 = gm_l3;
we10:;
        gm_l3 = F.w0;
        uint8_t t20 = gm_l3;
        F.ret = t20;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
        goto gm_ld4_1;
    }
    {
        void * t21 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_sejdpqv(t21);
    }
    goto gm_ls22;
gm_ld4_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_sejdpqv(t23);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls22:;
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
__attribute__((aligned(4))) void gm_f_own_46jsjq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_own_46jsjq2 F;
    gm_b16_8 gm_l2; /* items */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_b16_8 gm_l10; /* n */
    gm_th2lqbo77pvsc gm_l11; /* s */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    gm_t5hdnnftkup6m gm_l15; /* b */
    GM_STACK_CHECK(&F, 1042u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tabb710d9mq2e gm_l16;
    gm_tftb1odc739t5 gm_l17;
    gm_tabb710d9mq2e gm_l18;
    gm_tftb1odc739t5 gm_l19;
    gm_tabb710d9mq2e gm_l20;
    gm_tftb1odc739t5 gm_l21;
    {
        gm_th2lqbo77pvsc t21;
        memset(&t21, 0, sizeof t21);
        gm_ti5ramhglnvdt t22;
        memset(&t22, 0, sizeof t22);
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)a0) + 56));
        GM_ST(uint8_t, (uint8_t *)&F.l1 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&F.l1 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&F.l1 + 60, 0);
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&F.l1 + 0), t21);
        GM_ST(gm_ti5ramhglnvdt, ((uint8_t *)&F.l1 + 24), t22);
        GM_ST(uint8_t, ((uint8_t *)&F.l1 + 56), t23);
    }
    {
        {
            uint8_t t24 = (uint8_t)((((uint8_t *)a0))[16] != 0);
            if (!t24) {
                goto gm_lb2;
            }
            gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)a0));
            gm_l2 = t25;
            {
                gm_trqfvmjtt4sij t26;
                gm_f_list_f0gjjbh_2((void *)&t26, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l3 = t26;
            }
            gm_b16_8 t27 = gm_l2;
            gm_l4 = t27;
            gm_l5 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t28 = gm_l5;
                    uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint8_t t30 = (t28 < t29);
                    if (!t30) {
                        goto gm_lb3;
                    }
                    {
                        void * t31 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        uint64_t t33 = gm_l5;
                        {
                            {
                                {
                                    memmove(((uint8_t *)&F.l6), (((uint8_t *)t31) + (uintptr_t)t33 * 40), 40);
                                }
                                {
                                    gm_trqfvmjtt4sij t34;
                                    gm_f_list_epvducg((void *)&t34, (void *)NULL, cx, NULL);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.l7 = t34;
                                }
                                gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
                                gm_l8 = t35;
                                gm_l9 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t36 = gm_l9;
                                        uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                        uint8_t t38 = (t36 < t37);
                                        if (!t38) {
                                            goto gm_lb8;
                                        }
                                        {
                                            void * t39 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                            uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                            uint64_t t41 = gm_l9;
                                            {
                                                {
                                                    gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)t39) + (uintptr_t)t41 * 16));
                                                    gm_l10 = t42;
                                                    {
                                                        void * t43 = (void *)((uint8_t *)&F.l7);
                                                        gm_b16_8 t44 = gm_l10;
                                                        gm_f_dupe_g9iqpvn((void *)&gm_l16, (void *)NULL, cx, NULL, t44, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kmgp10tqfjk)));
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        uint8_t t45 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                                                        if (!t45) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_srcr4magk9naq4q, 231, 44);
                                                        gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
                                                        gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
                                                        gm_tftb1odc739t5 t48 = gm_f_push_3ef746b(t43, t47);
                                                        gm_l17 = t48;
                                                    }
                                                    uint8_t t49 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                                                    if (!t49) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srcr4magk9naq4q, 231, 33);
                                                    goto gm_lb11;
                                                }
gm_lb12:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 231, 13);
                                            }
gm_lb11:;
                                        }
gm_lb10:;
                                        uint64_t t50 = gm_l9;
                                        uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                                        uint64_t t53 = r51;
                                        gm_l9 = t53;
                                    }
                                }
gm_lb8:;
                                {
                                    void * t54 = (void *)((uint8_t *)&F.l3);
                                    gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
                                    gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 16));
                                    gm_f_dupe_g9iqpvn((void *)&gm_l18, (void *)NULL, cx, NULL, t56, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k61ski6fvbu)));
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                                    if (!t57) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_srcr4magk9naq4q, 232, 54);
                                    gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
                                    gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
                                    uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 32));
                                    gm_tk9n3vlila9qg t61;
                                    GM_ST(uint8_t, (uint8_t *)&t61 + 33, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t61 + 34, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t61 + 36, 0);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t61 + 0), t55);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t61 + 16), t59);
                                    GM_ST(uint8_t, ((uint8_t *)&t61 + 32), t60);
                                    gm_tftb1odc739t5 t62 = gm_f_push_1e500nc_2(t54, &t61);
                                    gm_l19 = t62;
                                }
                                uint8_t t63 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                                if (!t63) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_srcr4magk9naq4q, 232, 13);
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 229, 9);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t64 = gm_l5;
                    uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
                    uint64_t t67 = r65;
                    gm_l5 = t67;
                }
            }
gm_lb3:;
            {
                gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
                gm_th2lqbo77pvsc t69;
                GM_ST(uint8_t, (uint8_t *)&t69 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t69 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t69 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t69), t68);
                ((uint8_t *)&t69)[16] = 1;
                GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&F.l1), t69);
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            uint8_t t70 = (uint8_t)(((((uint8_t *)a0) + 24))[24] != 0);
            if (!t70) {
                goto gm_lb14;
            }
            {
                memmove(((uint8_t *)&gm_l11), (((uint8_t *)a0) + 24), 24);
            }
            {
                gm_trqfvmjtt4sij t71;
                gm_f_list_293fcpb((void *)&t71, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l12 = t71;
            }
            gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l11));
            gm_l13 = t72;
            gm_l14 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t73 = gm_l14;
                    uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                    uint8_t t75 = (t73 < t74);
                    if (!t75) {
                        goto gm_lb15;
                    }
                    {
                        void * t76 = GM_LD(void *, ((uint8_t *)&gm_l13));
                        uint64_t t77 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                        uint64_t t78 = gm_l14;
                        {
                            {
                                {
                                    memmove(((uint8_t *)&gm_l15), (((uint8_t *)t76) + (uintptr_t)t78 * 24), 24);
                                }
                                {
                                    void * t79 = (void *)((uint8_t *)&F.l12);
                                    gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l15));
                                    gm_f_dupe_g9iqpvn((void *)&gm_l20, (void *)NULL, cx, NULL, t80, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_krt0ppukvqv)));
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t81 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                                    if (!t81) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 8, gm_srcr4magk9naq4q, 238, 53);
                                    gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
                                    gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&t82));
                                    uint32_t t84 = GM_LD(uint32_t, (((uint8_t *)&gm_l15) + 16));
                                    uint32_t t85 = GM_LD(uint32_t, (((uint8_t *)&gm_l15) + 20));
                                    gm_t5hdnnftkup6m t86;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t86 + 0), t83);
                                    GM_ST(uint32_t, ((uint8_t *)&t86 + 16), t84);
                                    GM_ST(uint32_t, ((uint8_t *)&t86 + 20), t85);
                                    gm_tftb1odc739t5 t87 = gm_f_push_6fomkur(t79, &t86);
                                    gm_l21 = t87;
                                }
                                uint8_t t88 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                                if (!t88) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srcr4magk9naq4q, 238, 31);
                                goto gm_lb18;
                            }
gm_lb19:;
                            gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 238, 9);
                        }
gm_lb18:;
                    }
gm_lb17:;
                    uint64_t t89 = gm_l14;
                    uint64_t r90; bool o91 = __builtin_add_overflow(t89, ((uint64_t)1ULL), &r90);
                    uint64_t t92 = r90;
                    gm_l14 = t92;
                }
            }
gm_lb15:;
            {
                gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
                uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 16));
                gm_th2lqbo77pvsc t95;
                GM_ST(uint8_t, (uint8_t *)&t95 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t95 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t95 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t95 + 0), t93);
                GM_ST(uint8_t, ((uint8_t *)&t95 + 16), t94);
                gm_ti5ramhglnvdt t96;
                GM_ST(uint8_t, (uint8_t *)&t96 + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&t96 + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&t96 + 28, 0);
                GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t96), t95);
                ((uint8_t *)&t96)[24] = 1;
                GM_ST(gm_ti5ramhglnvdt, (((uint8_t *)&F.l1) + 24), t96);
            }
            goto gm_lb13;
        }
gm_lb14:;
    }
gm_lb13:;
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 64);
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
extern __attribute__((aligned(4))) void gm_f_parse_items_g2pt6ce(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_parse_source_i1af11u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, void * a2) {
    struct gm_fr_parse_source_i1af11u F;
    gm_b16_8 gm_l0; /* src */
    void * gm_l1; /* sm */
    void * gm_l2; /* diags */
    uint32_t gm_l3; /* id */
    uint32_t gm_l5; /* next_id */
    gm_t08olq97rscn3 gm_l6; /* tree */
    gm_t9805qtonbsag gm_l7; /* first */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    GM_STACK_CHECK(&F, 585u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l11;
    uint8_t gm_l12;
    uint32_t gm_l13;
    void * t10 = gm_l1;
    gm_b16_8 t11;
    GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sb3u9q4alu9bg));
    GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(6));
    gm_b16_8 t12 = gm_l0;
    uint32_t t13 = gm_f_add_pva1tcq((void *)NULL, cx, NULL, t10, t11, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t13;
    {
        uint32_t t14 = gm_l3;
        gm_b16_8 t15 = gm_l0;
        void * t16 = gm_l2;
        gm_tr8dvlu9ik4gj t17;
        gm_f_lex_dc8mbmf((void *)&t17, (void *)NULL, cx, NULL, t14, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l4 = t17;
    }
    gm_l5 = ((uint32_t)0ULL);
    {
        uint32_t t18 = gm_l3;
        void * t19 = (void *)((uint8_t *)&F.l4);
        void * t20 = (void *)((uint8_t *)&gm_l5);
        void * t21 = gm_l2;
        gm_f_parse_file_70g7vok((void *)&gm_l6, (void *)NULL, cx, NULL, t18, t19, t20, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    gm_t9805qtonbsag t22;
    memset(&t22, 0, sizeof t22);
    gm_l7 = t22;
    void * t23 = gm_l2;
    gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)t23));
    gm_l8 = t24;
    gm_l9 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t25 = gm_l9;
            uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
            uint8_t t27 = (t25 < t26);
            if (!t27) {
                goto gm_lb1;
            }
            {
                void * t28 = GM_LD(void *, ((uint8_t *)&gm_l8));
                uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                uint64_t t30 = gm_l9;
                {
                    {
                        memmove(((uint8_t *)&F.l10), (((uint8_t *)t28) + (uintptr_t)t30 * 184), 184);
                        {
                            {
                                uint8_t t31 = (uint8_t)((((uint8_t *)&gm_l7))[4] != 0);
                                if (t31) {
                                    goto gm_lb7;
                                }
                                gm_l12 = ((uint8_t)1);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                gm_l12 = ((uint8_t)0);
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 126, 12);
                        }
gm_lb6:;
                        uint8_t t32 = gm_l12;
                        if (t32) {
                            gm_l11 = ((uint8_t)1);
                        } else {
                            uint32_t t33 = GM_LD(uint32_t, ((((uint8_t *)&F.l10) + 24) + 4));
                            uint8_t t34 = (uint8_t)((((uint8_t *)&gm_l7))[4] != 0);
                            if (t34) {
                                uint32_t t35 = GM_LD(uint32_t, ((uint8_t *)&gm_l7));
                                gm_l13 = t35;
                            } else {
                                gm_l13 = ((uint32_t)0ULL);
                            }
                            uint32_t t36 = gm_l13;
                            uint8_t t37 = (t33 < t36);
                            gm_l11 = t37;
                        }
                        uint8_t t38 = gm_l11;
                        if (t38) {
                            uint32_t t39 = GM_LD(uint32_t, ((((uint8_t *)&F.l10) + 24) + 4));
                            gm_t9805qtonbsag t40;
                            GM_ST(uint8_t, (uint8_t *)&t40 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t40 + 6, 0);
                            GM_ST(uint32_t, ((uint8_t *)&t40), t39);
                            ((uint8_t *)&t40)[4] = 1;
                            gm_l7 = t40;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 125, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t41 = gm_l9;
            uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
            uint64_t t44 = r42;
            gm_l9 = t44;
        }
    }
gm_lb1:;
    void * t45 = gm_l2;
    uint8_t t46 = gm_f_has_errors_if7ai0m(t45);
    if (t46) {
        gm_t1s75i08qtptd t47;
        memset(&t47, 0, sizeof t47);
        gm_t9805qtonbsag t48 = gm_l7;
        GM_ST(gm_t1s75i08qtptd, ((uint8_t *)&(*(gm_tm3mvv7hooqkm *)gm_out) + 0), t47);
        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&(*(gm_tm3mvv7hooqkm *)gm_out) + 40), t48);
        goto gm_fdone;
    }
    {
        gm_t08olq97rscn3 t49 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&gm_l6));
        gm_t1s75i08qtptd t50;
        GM_ST(uint8_t, (uint8_t *)&t50 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t50 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t50 + 36, 0);
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t50), t49);
        ((uint8_t *)&t50)[32] = 1;
        gm_t9805qtonbsag t51;
        memset(&t51, 0, sizeof t51);
        GM_ST(gm_t1s75i08qtptd, ((uint8_t *)&(*(gm_tm3mvv7hooqkm *)gm_out) + 0), t50);
        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&(*(gm_tm3mvv7hooqkm *)gm_out) + 40), t51);
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
inline __attribute__((aligned(4))) void gm_f_parse_stmts_mefdnbk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_parse_stmts_mefdnbk F;
    gm_b16_8 gm_l0; /* text */
    GM_STACK_CHECK(&F, 317u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l4;
    {
        gm_tin97tt4ud0s1 t2;
        gm_f_source_map_4v8dg4b((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t2;
    }
    {
        gm_t2792tsg5eg72 t3;
        gm_f_diagnostics_n3kkhf4((void *)&t3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t3;
    }
    {
        gm_trqfvmjtt4sij t4;
        gm_f_list_uvfu1k1((void *)&t4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l3 = t4;
    }
    {
        void * t5 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)gm_cfc3i2mbui9));
        gm_b16_8 t7 = gm_l0;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)gm_cbms0bo4q1a));
        gm_tfeab279c16rr t9;
        GM_ST(gm_b16_8, ((uint8_t *)&t9 + 0), t6);
        GM_ST(gm_b16_8, ((uint8_t *)&t9 + 16), t7);
        GM_ST(gm_b16_8, ((uint8_t *)&t9 + 32), t8);
        gm_tftb1odc739t5 t10 = gm_f_write_qe3m8jq(t5, &t9);
        gm_l4 = t10;
    }
    uint8_t t11 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t11) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srcr4magk9naq4q, 142, 5);
    {
        gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
        gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
        void * t14 = (void *)((uint8_t *)&F.l1);
        void * t15 = (void *)((uint8_t *)&F.l2);
        gm_f_parse_source_i1af11u((void *)&(*(gm_tm3mvv7hooqkm *)gm_out), (void *)NULL, cx, NULL, t13, t14, t15);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_rename_47b7og5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_rename_47b7og5 F;
    gm_b16_8 gm_l0; /* text */
    gm_b16_8 gm_l1; /* defs */
    gm_b16_8 gm_l2; /* uses */
    gm_b16_8 gm_l4; /* result */
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 gm_l5;
    gm_tabb710d9mq2e gm_l6;
    {
        {
            gm_tjt4ka19str13 t5;
            gm_f_new_jr5ejs0((void *)&t5, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l3 = t5;
        }
        {
            void * t6 = (void *)((uint8_t *)&F.l3);
            gm_t3mf58gv9kf3n t7 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_allocator_5m783ja(t6));
            gm_t3mf58gv9kf3n t8 = t7;
            gm_node wn9 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t8, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
            gm_b16_8 t12 = gm_l0;
            gm_b16_8 t13 = gm_l1;
            gm_b16_8 t14 = gm_l2;
            gm_b16_8 t15 = gm_f_rename_in_nstmasl((void *)NULL, (&wn9), NULL, t12, t13, t14);
            if (gm_unw.kind) {
                goto gm_ll11;
            }
            gm_l5 = t15;
        }
        goto gm_ls16;
gm_ll11:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we10; }
        goto gm_ld4_1;
gm_ls16:;
gm_lb2:;
        F.w0 = gm_l5;
we10:;
        gm_l5 = F.w0;
        gm_b16_8 t17 = gm_l5;
        gm_l4 = t17;
        {
            gm_b16_8 t18 = gm_l4;
            gm_f_dupe_g9iqpvn((void *)&gm_l6, (void *)NULL, cx, NULL, t18, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kd6k1g0uehn)));
            if (gm_unw.kind) {
                goto gm_ld4_1;
            }
        }
        uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
        if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_srcr4magk9naq4q, 303, 12);
        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
        gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
        F.ret = t21;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
        goto gm_ld4_1;
    }
    {
        void * t22 = (void *)((uint8_t *)&F.l3);
        gm_f_deinit_sejdpqv(t22);
    }
    goto gm_ls23;
gm_ld4_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t24 = (void *)((uint8_t *)&F.l3);
        gm_f_deinit_sejdpqv(t24);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls23:;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_rename_in_nstmasl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_rename_in_nstmasl F;
    gm_b16_8 gm_l0; /* text */
    gm_b16_8 gm_l1; /* defs */
    gm_b16_8 gm_l2; /* uses */
    gm_b16_8 gm_l3; /* tokens */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_t2s5j3n0guq0t gm_l7; /* d */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    gm_b16_8 gm_l12; /* name */
    gm_b16_8 gm_l13; /* new */
    uint64_t gm_l14; /* lo */
    uint64_t gm_l15; /* hi */
    uint8_t gm_l16; /* taken */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    gm_tbccvikg6b96e gm_l19; /* e */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    gm_tbccvikg6b96e gm_l23; /* e */
    gm_b16_8 gm_l24; /* tail */
    GM_STACK_CHECK(&F, 1632u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t0to1u196c9pc gm_l25;
    gm_tftb1odc739t5 gm_l26;
    gm_tfhcigdju4v01 gm_l27;
    gm_th2lqbo77pvsc gm_l28;
    gm_tftb1odc739t5 gm_l29;
    gm_tftb1odc739t5 gm_l30;
    gm_tabb710d9mq2e gm_l31;
    gm_tftb1odc739t5 gm_l32;
    gm_tftb1odc739t5 gm_l33;
    {
        gm_b16_8 t28 = gm_l0;
        gm_f_scan_e9v4omo((void *)&gm_l25, (void *)NULL, cx, NULL, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l25));
    gm_l3 = t29;
    {
        gm_trqfvmjtt4sij t30;
        gm_f_list_jekodor_2((void *)&t30, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l4 = t30;
    }
    gm_b16_8 t31 = gm_l1;
    gm_l5 = t31;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t32 = gm_l6;
            uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t34 = (t32 < t33);
            if (!t34) {
                goto gm_lb1;
            }
            {
                void * t35 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t37 = gm_l6;
                {
                    {
                        {
                            memmove(((uint8_t *)&gm_l7), (((uint8_t *)t35) + (uintptr_t)t37 * 24), 24);
                        }
                        {
                            void * t38 = (void *)((uint8_t *)&F.l4);
                            uint32_t t39 = GM_LD(uint32_t, ((uint8_t *)&gm_l7));
                            uint64_t t40 = (uint64_t)t39;
                            uint32_t t41 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 4));
                            uint64_t t42 = (uint64_t)t41;
                            gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
                            gm_tbccvikg6b96e t44;
                            GM_ST(uint64_t, ((uint8_t *)&t44 + 0), t40);
                            GM_ST(uint64_t, ((uint8_t *)&t44 + 8), t42);
                            GM_ST(gm_b16_8, ((uint8_t *)&t44 + 16), t43);
                            gm_tftb1odc739t5 t45 = gm_f_push_k96mobn_2(t38, &t44);
                            gm_l26 = t45;
                        }
                        uint8_t t46 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                        if (!t46) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_srcr4magk9naq4q, 309, 21);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 309, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t47 = gm_l6;
            uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
            uint64_t t50 = r48;
            gm_l6 = t50;
        }
    }
gm_lb1:;
    gm_b16_8 t51 = gm_l3;
    gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
    gm_l8 = t52;
    gm_l9 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t53 = gm_l9;
            uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
            uint8_t t55 = (t53 < t54);
            if (!t55) {
                goto gm_lb6;
            }
            {
                {
                    uint64_t t56 = gm_l9;
                    void * t57 = GM_LD(void *, ((uint8_t *)&gm_l8));
                    uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                    uint64_t t59 = gm_l9;
                    gm_tm4pc0emhrv1g t60 = GM_LD(gm_tm4pc0emhrv1g, (((uint8_t *)t57) + (uintptr_t)t59 * 48));
                    GM_ST(uint64_t, ((uint8_t *)&gm_l27 + 0), t56);
                    GM_ST(gm_tm4pc0emhrv1g, ((uint8_t *)&gm_l27 + 8), t60);
                }
                {
                    {
                        uint64_t t61 = GM_LD(uint64_t, ((uint8_t *)&gm_l27));
                        gm_l10 = t61;
                        memmove(((uint8_t *)&F.l11), (((uint8_t *)&gm_l27) + 8), 48);
                        {
                            {
                                uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
                                uint8_t t63 = (uint8_t)(t62 == ((uint8_t)0ULL));
                                if (!t63) {
                                    goto gm_lb12;
                                }
                                gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 8));
                                gm_l12 = t64;
                                {
                                    gm_b16_8 t65 = gm_l2;
                                    gm_b16_8 t66 = gm_l12;
                                    gm_f_lookup_var_3vpm3u6((void *)&gm_l28, t65, t66);
                                }
                                uint8_t t67 = (uint8_t)((((uint8_t *)&gm_l28))[16] != 0);
                                if (t67) {
                                    gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l28));
                                    gm_l13 = t68;
                                } else {
                                    goto gm_lb8;
                                }
                                uint32_t t69 = GM_LD(uint32_t, ((((uint8_t *)&F.l11) + 32) + 4));
                                uint64_t t70 = (uint64_t)t69;
                                gm_l14 = t70;
                                uint32_t t71 = GM_LD(uint32_t, ((((uint8_t *)&F.l11) + 32) + 8));
                                uint64_t t72 = (uint64_t)t71;
                                gm_l15 = t72;
                                gm_l16 = ((uint8_t)0);
                                gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
                                gm_l17 = t73;
                                gm_l18 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t74 = gm_l18;
                                        uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                                        uint8_t t76 = (t74 < t75);
                                        if (!t76) {
                                            goto gm_lb13;
                                        }
                                        {
                                            void * t77 = GM_LD(void *, ((uint8_t *)&gm_l17));
                                            uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                                            uint64_t t79 = gm_l18;
                                            {
                                                {
                                                    memmove(((uint8_t *)&gm_l19), (((uint8_t *)t77) + (uintptr_t)t79 * 32), 32);
                                                    uint64_t t80 = GM_LD(uint64_t, ((uint8_t *)&gm_l19));
                                                    uint64_t t81 = gm_l14;
                                                    uint8_t t82 = ((t80 == t81));
                                                    if (t82) {
                                                        gm_l16 = ((uint8_t)1);
                                                    }
                                                    goto gm_lb16;
                                                }
gm_lb17:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 316, 9);
                                            }
gm_lb16:;
                                        }
gm_lb15:;
                                        uint64_t t83 = gm_l18;
                                        uint64_t r84; bool o85 = __builtin_add_overflow(t83, ((uint64_t)1ULL), &r84);
                                        uint64_t t86 = r84;
                                        gm_l18 = t86;
                                    }
                                }
gm_lb13:;
                                uint8_t t87 = gm_l16;
                                if (t87) {
                                    goto gm_lb8;
                                }
                                {
                                    {
                                        uint64_t t88 = gm_l10;
                                        uint8_t t89 = (t88 > ((uint64_t)0ULL));
                                        if (!t89) {
                                            goto gm_lb19;
                                        }
                                        {
                                            void * t90 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                            uint64_t t91 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                            uint64_t t92 = gm_l10;
                                            uint64_t r93; bool o94 = __builtin_sub_overflow(t92, ((uint64_t)1ULL), &r93);
                                            if (o94) gm_trap("integer overflow in `-`", gm_srcr4magk9naq4q, 320, 29);
                                            uint64_t t95 = r93;
                                            if ((uintptr_t)t95 >= (uintptr_t)(t91)) gm_trap_index((uintptr_t)t95, t91, gm_srcr4magk9naq4q, 320, 22);
                                            gm_t9qkh0rsooc8d t96;
                                            GM_ST(uint8_t, ((uint8_t *)&t96 + 0), ((uint8_t)58ULL));
                                            uint8_t t97 = GM_LD(uint8_t, ((((uint8_t *)t90) + (uintptr_t)t95 * 48) + 0));
                                            uint8_t t98 = (uint8_t)(t97 == ((uint8_t)8ULL));
                                            if (t98) {
                                                gm_t9qkh0rsooc8d t99 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t90) + (uintptr_t)t95 * 48) + 8));
                                                t98 = (uint8_t)gm_eqv3tj1pu43g(((uint8_t *)&t99), ((uint8_t *)&t96));
                                            }
                                            uint8_t t100 = (uint8_t)(t98);
                                            if (!t100) {
                                                goto gm_lb19;
                                            }
                                        }
                                        goto gm_lb8;
                                    }
gm_lb19:;
                                }
gm_lb18:;
                                {
                                    {
                                        uint64_t t101 = gm_l10;
                                        uint64_t r102; bool o103 = __builtin_add_overflow(t101, ((uint64_t)1ULL), &r102);
                                        if (o103) gm_trap("integer overflow in `+`", gm_srcr4magk9naq4q, 321, 12);
                                        uint64_t t104 = r102;
                                        uint64_t t105 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                        uint8_t t106 = (t104 < t105);
                                        if (!t106) {
                                            goto gm_lb21;
                                        }
                                        {
                                            void * t107 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                            uint64_t t108 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                            uint64_t t109 = gm_l10;
                                            uint64_t r110; bool o111 = __builtin_add_overflow(t109, ((uint64_t)1ULL), &r110);
                                            if (o111) gm_trap("integer overflow in `+`", gm_srcr4magk9naq4q, 321, 42);
                                            uint64_t t112 = r110;
                                            if ((uintptr_t)t112 >= (uintptr_t)(t108)) gm_trap_index((uintptr_t)t112, t108, gm_srcr4magk9naq4q, 321, 35);
                                            gm_t9qkh0rsooc8d t113;
                                            GM_ST(uint8_t, ((uint8_t *)&t113 + 0), ((uint8_t)56ULL));
                                            uint8_t t114 = GM_LD(uint8_t, ((((uint8_t *)t107) + (uintptr_t)t112 * 48) + 0));
                                            uint8_t t115 = (uint8_t)(t114 == ((uint8_t)8ULL));
                                            if (t115) {
                                                gm_t9qkh0rsooc8d t116 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t107) + (uintptr_t)t112 * 48) + 8));
                                                t115 = (uint8_t)gm_eqv3tj1pu43g(((uint8_t *)&t116), ((uint8_t *)&t113));
                                            }
                                            uint8_t t117 = (uint8_t)(t115);
                                            if (!t117) {
                                                goto gm_lb21;
                                            }
                                        }
                                        goto gm_lb8;
                                    }
gm_lb21:;
                                }
gm_lb20:;
                                {
                                    void * t118 = (void *)((uint8_t *)&F.l4);
                                    uint64_t t119 = gm_l14;
                                    uint64_t t120 = gm_l15;
                                    gm_b16_8 t121 = gm_l13;
                                    gm_tbccvikg6b96e t122;
                                    GM_ST(uint64_t, ((uint8_t *)&t122 + 0), t119);
                                    GM_ST(uint64_t, ((uint8_t *)&t122 + 8), t120);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t122 + 16), t121);
                                    gm_tftb1odc739t5 t123 = gm_f_push_k96mobn_2(t118, &t122);
                                    gm_l29 = t123;
                                }
                                uint8_t t124 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                                if (!t124) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_srcr4magk9naq4q, 322, 9);
                                goto gm_lb11;
                            }
gm_lb12:;
                            goto gm_lb8;
                        }
gm_lb11:;
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 310, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t125 = gm_l9;
            uint64_t r126; bool o127 = __builtin_add_overflow(t125, ((uint64_t)1ULL), &r126);
            uint64_t t128 = r126;
            gm_l9 = t128;
        }
    }
gm_lb6:;
    gm_b16_8 t129 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
    gm_closure t130 = ((gm_closure){GM_PTR_TAG(gm_f_fn_8no2c2v), NULL, cx});
    gm_f_sort_by_jo6rcsi(t129, t130);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        gm_trqfvmjtt4sij t131;
        gm_f_list_uvfu1k1((void *)&t131, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l20 = t131;
    }
    void * t132 = (void *)((uint8_t *)&F.l20);
    gm_b16_8 t133 = gm_l0;
    gm_tftb1odc739t5 t134 = gm_f_extend_0cs85gs(t132, t133);
    gm_l30 = t134;
    uint8_t t135 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
    if (!t135) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 1, gm_srcr4magk9naq4q, 327, 5);
    gm_b16_8 t136 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
    gm_l21 = t136;
    gm_l22 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t137 = gm_l22;
            uint64_t t138 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
            uint8_t t139 = (t137 < t138);
            if (!t139) {
                goto gm_lb22;
            }
            {
                void * t140 = GM_LD(void *, ((uint8_t *)&gm_l21));
                uint64_t t141 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                uint64_t t142 = gm_l22;
                {
                    {
                        {
                            memmove(((uint8_t *)&gm_l23), (((uint8_t *)t140) + (uintptr_t)t142 * 32), 32);
                        }
                        {
                            void * t143 = GM_LD(void *, ((uint8_t *)&F.l20));
                            uint64_t t144 = GM_LD(uint64_t, (((uint8_t *)&F.l20) + 8));
                            uint64_t t145 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                            uint64_t t146 = (uintptr_t)t145;
                            uint64_t t147 = (uintptr_t)t144;
                            if (t146 > t147 || t147 > (uintptr_t)t144) gm_trap_slice(t146, t147, t144, gm_srcr4magk9naq4q, 329, 22);
                            uint64_t t148 = ((uintptr_t)t144) - (uintptr_t)t145;
                            gm_b16_8 t149;
                            GM_ST(void *, (uint8_t *)&t149, (void *)(((uint8_t *)t143 + (uintptr_t)t145 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t149 + 8, (uintptr_t)(t148));
                            gm_b16_8 t150 = GM_LD(gm_b16_8, ((uint8_t *)&t149));
                            gm_f_dupe_g9iqpvn((void *)&gm_l31, (void *)NULL, cx, NULL, t150, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k3mmr0p0mh8)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t151 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                        if (!t151) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 8, gm_srcr4magk9naq4q, 329, 17);
                        gm_b16_8 t152 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l31) + 8));
                        gm_l24 = t152;
                        void * t153 = (void *)((uint8_t *)&F.l20);
                        uint64_t t154 = GM_LD(uint64_t, ((uint8_t *)&gm_l23));
                        gm_f_truncate_ov4pgoq(t153, t154);
                        void * t155 = (void *)((uint8_t *)&F.l20);
                        gm_b16_8 t156 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 16));
                        gm_tftb1odc739t5 t157 = gm_f_extend_0cs85gs(t155, t156);
                        gm_l32 = t157;
                        uint8_t t158 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                        if (!t158) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 1, gm_srcr4magk9naq4q, 331, 9);
                        void * t159 = (void *)((uint8_t *)&F.l20);
                        gm_b16_8 t160 = gm_l24;
                        gm_b16_8 t161 = GM_LD(gm_b16_8, ((uint8_t *)&t160));
                        gm_tftb1odc739t5 t162 = gm_f_extend_0cs85gs(t159, t161);
                        gm_l33 = t162;
                        uint8_t t163 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                        if (!t163) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 1, gm_srcr4magk9naq4q, 332, 9);
                        goto gm_lb25;
                    }
gm_lb26:;
                    gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 328, 5);
                }
gm_lb25:;
            }
gm_lb24:;
            uint64_t t164 = gm_l22;
            uint64_t r165; bool o166 = __builtin_add_overflow(t164, ((uint64_t)1ULL), &r165);
            uint64_t t167 = r165;
            gm_l22 = t167;
        }
    }
gm_lb22:;
    gm_b16_8 t168 = GM_LD(gm_b16_8, ((uint8_t *)&F.l20));
    gm_b16_8 t169 = GM_LD(gm_b16_8, ((uint8_t *)&t168));
    F.ret = t169;
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
inline __attribute__((aligned(4))) void gm_f_scan_e9v4omo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_scan_e9v4omo F;
    gm_b16_8 gm_l0; /* text */
    uint32_t gm_l2; /* id */
    gm_t0to1u196c9pc gm_l5; /* s */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    GM_STACK_CHECK(&F, 408u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_tin97tt4ud0s1 t7;
        gm_f_source_map_4v8dg4b((void *)&t7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t7;
    }
    void * t8 = (void *)((uint8_t *)&F.l1);
    gm_b16_8 t9;
    GM_ST(void *, (uint8_t *)&t9, (void *)(gm_sb3u9q4alu9bg));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(6));
    gm_b16_8 t10 = gm_l0;
    uint32_t t11 = gm_f_add_pva1tcq((void *)NULL, cx, NULL, t8, t9, t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t11;
    {
        gm_t2792tsg5eg72 t12;
        gm_f_diagnostics_n3kkhf4((void *)&t12, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l3 = t12;
    }
    {
        uint32_t t13 = gm_l2;
        gm_b16_8 t14 = gm_l0;
        void * t15 = (void *)((uint8_t *)&F.l3);
        gm_f_lex_dc8mbmf((void *)&F.l4, (void *)NULL, cx, NULL, t13, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
        void * t17 = (void *)((uint8_t *)&F.l3);
        uint8_t t18 = gm_f_has_errors_if7ai0m(t17);
        GM_ST(uint8_t, (uint8_t *)&gm_l5 + 19, 0);
        GM_ST(uint32_t, (uint8_t *)&gm_l5 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&gm_l5 + 0), t16);
        GM_ST(uint8_t, ((uint8_t *)&gm_l5 + 16), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&gm_l5 + 17), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&gm_l5 + 18), t18);
    }
    gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    gm_l6 = t19;
    gm_l7 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t20 = gm_l7;
            uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint8_t t22 = (t20 < t21);
            if (!t22) {
                goto gm_lb1;
            }
            {
                void * t23 = GM_LD(void *, ((uint8_t *)&gm_l6));
                uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint64_t t25 = gm_l7;
                {
                    {
                        memmove(((uint8_t *)&F.l8), (((uint8_t *)t23) + (uintptr_t)t25 * 184), 184);
                        gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 8));
                        gm_b16_8 t27;
                        GM_ST(void *, (uint8_t *)&t27, (void *)(gm_s5t1b3eks758h));
                        GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(26));
                        uint8_t t28 = gm_f_contains_gkki4kq(t26, t27);
                        if (t28) {
                            GM_ST(uint8_t, (((uint8_t *)&gm_l5) + 16), ((uint8_t)1));
                        }
                        gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 8));
                        gm_b16_8 t30;
                        GM_ST(void *, (uint8_t *)&t30, (void *)(gm_sshj97ls6n4f3));
                        GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(17));
                        uint8_t t31 = gm_f_contains_gkki4kq(t29, t30);
                        if (t31) {
                            GM_ST(uint8_t, (((uint8_t *)&gm_l5) + 17), ((uint8_t)1));
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 63, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t32 = gm_l7;
            uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
            uint64_t t35 = r33;
            gm_l7 = t35;
        }
    }
gm_lb1:;
    {
        memmove(gm_out, ((uint8_t *)&gm_l5), 24);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_syntax_errors_fk23f5t(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1) {
    struct gm_fr_syntax_errors_fk23f5t F;
    gm_b16_8 gm_l0; /* text */
    gm_t9qkh0rsooc8d gm_l1; /* format */
    gm_b16_8 gm_l3; /* result */
    GM_STACK_CHECK(&F, 307u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 gm_l4;
    gm_tabb710d9mq2e gm_l5;
    {
        {
            gm_tjt4ka19str13 t5;
            gm_f_new_jr5ejs0((void *)&t5, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l2 = t5;
        }
        {
            void * t6 = (void *)((uint8_t *)&F.l2);
            gm_t3mf58gv9kf3n t7 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_allocator_5m783ja(t6));
            gm_t3mf58gv9kf3n t8 = t7;
            gm_node wn9 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t8, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
            gm_b16_8 t12 = gm_l0;
            gm_t9qkh0rsooc8d t13 = gm_l1;
            gm_b16_8 t14 = gm_f_syntax_errors_in_o8fnm4b((void *)NULL, (&wn9), NULL, t12, t13);
            if (gm_unw.kind) {
                goto gm_ll11;
            }
            gm_l4 = t14;
        }
        goto gm_ls15;
gm_ll11:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we10; }
        goto gm_ld4_1;
gm_ls15:;
gm_lb2:;
        F.w0 = gm_l4;
we10:;
        gm_l4 = F.w0;
        gm_b16_8 t16 = gm_l4;
        gm_l3 = t16;
        {
            gm_b16_8 t17 = gm_l3;
            gm_f_dupe_g9iqpvn((void *)&gm_l5, (void *)NULL, cx, NULL, t17, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_klim7pfh497)));
            if (gm_unw.kind) {
                goto gm_ld4_1;
            }
        }
        uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
        if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 8, gm_srcr4magk9naq4q, 343, 12);
        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
        gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
        F.ret = t20;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
        goto gm_ld4_1;
    }
    {
        void * t21 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_sejdpqv(t21);
    }
    goto gm_ls22;
gm_ld4_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t23 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_sejdpqv(t23);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls22:;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_syntax_errors_in_o8fnm4b(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1) {
    struct gm_fr_syntax_errors_in_o8fnm4b F;
    gm_b16_8 gm_l0; /* text */
    gm_t9qkh0rsooc8d gm_l1; /* format */
    uint8_t gm_l2; /* stray */
    gm_t9805qtonbsag gm_l3; /* items_err */
    gm_t9805qtonbsag gm_l4; /* stmts_err */
    uint32_t gm_l5; /* pre */
    uint32_t gm_l6; /* p */
    uint8_t gm_l7; /* use_stmts */
    uint32_t gm_l8; /* i */
    uint32_t gm_l9; /* s */
    GM_STACK_CHECK(&F, 940u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t0to1u196c9pc gm_l13;
    gm_tm3mvv7hooqkm gm_l14;
    gm_tm3mvv7hooqkm gm_l15;
    uint8_t gm_l16;
    gm_b16_8 gm_l17;
    gm_tftb1odc739t5 gm_l18;
    {
        gm_b16_8 t13 = gm_l0;
        gm_f_scan_e9v4omo((void *)&gm_l13, (void *)NULL, cx, NULL, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 17));
    gm_l2 = t14;
    {
        gm_b16_8 t15 = gm_l0;
        gm_f_parse_items_g2pt6ce((void *)&gm_l14, (void *)NULL, cx, NULL, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    gm_t9805qtonbsag t16 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l14) + 40));
    gm_l3 = t16;
    {
        gm_b16_8 t17 = gm_l0;
        gm_f_parse_stmts_mefdnbk((void *)&gm_l15, (void *)NULL, cx, NULL, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    gm_t9805qtonbsag t18 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l15) + 40));
    gm_l4 = t18;
    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)gm_cfc3i2mbui9) + 8));
    uint32_t t20 = (uint32_t)t19;
    gm_l5 = t20;
    {
        {
            uint8_t t21 = gm_l2;
            uint8_t t22 = (uint8_t)!t21;
            if (!t22) {
                goto gm_lb2;
            }
            {
                {
                    uint8_t t23 = (uint8_t)((((uint8_t *)&gm_l3))[4] != 0);
                    if (t23) {
                        goto gm_lb4;
                    }
                    gm_l16 = ((uint8_t)0);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    gm_l16 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb5:;
                gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 351, 22);
            }
gm_lb3:;
            uint8_t t24 = gm_l16;
            if (!t24) {
                goto gm_lb2;
            }
            {
                {
                    uint8_t t25 = (uint8_t)((((uint8_t *)&gm_l4))[4] != 0);
                    if (!t25) {
                        goto gm_lb7;
                    }
                    uint32_t t26 = GM_LD(uint32_t, ((uint8_t *)&gm_l4));
                    gm_l6 = t26;
                    uint32_t t27 = gm_l6;
                    uint32_t t28 = gm_l5;
                    gm_b16_8 t29 = gm_l0;
                    gm_b16_8 t30 = gm_f_trim_end_6sjv6rg(t29);
                    gm_l17 = t30;
                    uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                    uint32_t t32 = (uint32_t)t31;
                    uint32_t r33; bool o34 = __builtin_add_overflow(t28, t32, &r33);
                    if (o34) gm_trap("integer overflow in `+`", gm_srcr4magk9naq4q, 353, 21);
                    uint32_t t35 = r33;
                    uint8_t t36 = (t27 >= t35);
                    if (t36) {
                        gm_b16_8 t37;
                        GM_ST(void *, (uint8_t *)&t37, (void *)(gm_sfcm41q17pr37));
                        GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(45));
                        F.ret = t37;
                        goto gm_fret;
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
    gm_l7 = ((uint8_t)0);
    uint8_t t38 = gm_l2;
    uint8_t t39 = (uint8_t)!t38;
    if (t39) {
        {
            {
                uint8_t t40 = (uint8_t)((((uint8_t *)&gm_l3))[4] != 0);
                if (!t40) {
                    goto gm_lb9;
                }
                uint32_t t41 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
                gm_l8 = t41;
                {
                    {
                        uint8_t t42 = (uint8_t)((((uint8_t *)&gm_l4))[4] != 0);
                        if (!t42) {
                            goto gm_lb11;
                        }
                        uint32_t t43 = GM_LD(uint32_t, ((uint8_t *)&gm_l4));
                        gm_l9 = t43;
                        uint32_t t44 = gm_l9;
                        uint32_t t45 = gm_l5;
                        uint32_t r46; bool o47 = __builtin_sub_overflow(t44, t45, &r46);
                        uint32_t t48 = o47 ? (0 ? ((uint32_t)4294967295ULL) : ((uint32_t)0ULL)) : r46;
                        uint32_t t49 = gm_l8;
                        uint8_t t50 = (t48 > t49);
                        gm_l7 = t50;
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
    }
    {
        gm_tin97tt4ud0s1 t51;
        gm_f_source_map_4v8dg4b((void *)&t51, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l10 = t51;
    }
    {
        gm_t2792tsg5eg72 t52;
        gm_f_diagnostics_n3kkhf4((void *)&t52, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l11 = t52;
    }
    uint8_t t53 = gm_l7;
    if (t53) {
        {
            gm_trqfvmjtt4sij t54;
            gm_f_list_uvfu1k1((void *)&t54, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l12 = t54;
        }
        {
            void * t55 = (void *)((uint8_t *)&F.l12);
            gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)gm_cfc3i2mbui9));
            gm_b16_8 t57 = gm_l0;
            gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)gm_cbms0bo4q1a));
            gm_tfeab279c16rr t59;
            GM_ST(gm_b16_8, ((uint8_t *)&t59 + 0), t56);
            GM_ST(gm_b16_8, ((uint8_t *)&t59 + 16), t57);
            GM_ST(gm_b16_8, ((uint8_t *)&t59 + 32), t58);
            gm_tftb1odc739t5 t60 = gm_f_write_qe3m8jq(t55, &t59);
            gm_l18 = t60;
        }
        uint8_t t61 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
        if (!t61) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_srcr4magk9naq4q, 366, 9);
        {
            gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
            gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&t62));
            void * t64 = (void *)((uint8_t *)&F.l10);
            void * t65 = (void *)((uint8_t *)&F.l11);
            gm_tm3mvv7hooqkm t66;
            gm_f_parse_source_i1af11u((void *)&t66, (void *)NULL, cx, NULL, t63, t64, t65);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        void * t67 = (void *)((uint8_t *)&F.l10);
        void * t68 = (void *)((uint8_t *)&F.l11);
        uint32_t t69 = gm_l5;
        gm_b16_8 t70 = gm_l0;
        gm_t9qkh0rsooc8d t71 = gm_l1;
        gm_b16_8 t72 = gm_f_render_region_ppot3lm((void *)NULL, cx, NULL, t67, t68, ((uint32_t)0ULL), t69, t70, t71);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t72;
        goto gm_fret;
    }
    {
        gm_b16_8 t73 = gm_l0;
        void * t74 = (void *)((uint8_t *)&F.l10);
        void * t75 = (void *)((uint8_t *)&F.l11);
        gm_tm3mvv7hooqkm t76;
        gm_f_parse_source_i1af11u((void *)&t76, (void *)NULL, cx, NULL, t73, t74, t75);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    void * t77 = (void *)((uint8_t *)&F.l10);
    void * t78 = (void *)((uint8_t *)&F.l11);
    gm_b16_8 t79 = gm_l0;
    gm_t9qkh0rsooc8d t80 = gm_l1;
    gm_b16_8 t81 = gm_f_render_region_ppot3lm((void *)NULL, cx, NULL, t77, t78, ((uint32_t)0ULL), ((uint32_t)0ULL), t79, t80);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t81;
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

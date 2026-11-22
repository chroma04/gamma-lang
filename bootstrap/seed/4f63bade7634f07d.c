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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_for_body_ukps8ob {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* parts */
    gm_b0_1 ret;
    gm_b0_1 w0;
};
struct gm_fr_for_body_v43n3ub {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_5dma72c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_ivf5vgl(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_tprkpoe(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_v43n3ub(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
extern uint8_t gm_c1l7393q60j[4] __attribute__((aligned(1)));
extern uint8_t gm_c1oqisgjqh8[20] __attribute__((aligned(1)));
extern uint8_t gm_c23mu1onplu[3] __attribute__((aligned(1)));
extern uint8_t gm_c24m5jfl30g[12] __attribute__((aligned(1)));
extern uint8_t gm_c2f5b0m05j6[11] __attribute__((aligned(1)));
extern uint8_t gm_c30d0vfr4ha[8] __attribute__((aligned(1)));
extern uint8_t gm_c3cpugekfrb[11] __attribute__((aligned(1)));
extern uint8_t gm_c40fejj7lfq[17] __attribute__((aligned(1)));
extern uint8_t gm_c44l6ho119d[65] __attribute__((aligned(1)));
extern uint8_t gm_c4dbb3oj7ol[7] __attribute__((aligned(1)));
extern uint8_t gm_c4tbnbi68ao[320] __attribute__((aligned(8)));
extern uint8_t gm_c55218gtcr0[448] __attribute__((aligned(8)));
extern uint8_t gm_c5dhf54lj9h[1] __attribute__((aligned(1)));
extern uint8_t gm_c5id9hnno81[12] __attribute__((aligned(1)));
extern uint8_t gm_c5led8t7730[39] __attribute__((aligned(1)));
extern uint8_t gm_c5lrgm5j673[3] __attribute__((aligned(1)));
extern uint8_t gm_c7svj0uc8qe[4] __attribute__((aligned(1)));
extern uint8_t gm_c8b3omj6k2u[14] __attribute__((aligned(1)));
extern uint8_t gm_c8d44ct21t1[13] __attribute__((aligned(1)));
extern uint8_t gm_c9b4lh7gft4[4] __attribute__((aligned(4)));
extern uint8_t gm_ca2pc57d5g4[5] __attribute__((aligned(1)));
extern uint8_t gm_cbbu3qvtp3q[8] __attribute__((aligned(8)));
extern uint8_t gm_cblhiiedl5v[14] __attribute__((aligned(1)));
extern uint8_t gm_cfaubb414id[18] __attribute__((aligned(1)));
extern uint8_t gm_cga7n0g3amu[8] __attribute__((aligned(8)));
extern uint8_t gm_cgd1khc5sal[24] __attribute__((aligned(8)));
extern uint8_t gm_ch3g3buh6cp[16] __attribute__((aligned(8)));
extern uint8_t gm_ch6j88vtkil[2] __attribute__((aligned(1)));
extern uint8_t gm_ci3gn3427mq[12] __attribute__((aligned(1)));
extern uint8_t gm_ci51f79gd9k[8] __attribute__((aligned(1)));
extern uint8_t gm_ck0dn7th4er[38] __attribute__((aligned(1)));
extern uint8_t gm_cka88d5t8lu[16] __attribute__((aligned(8)));
extern uint8_t gm_ckpdl03lgov[16] __attribute__((aligned(1)));
extern uint8_t gm_ckpsekc86jv[18] __attribute__((aligned(1)));
extern uint8_t gm_ckus03er382[4] __attribute__((aligned(4)));
extern uint8_t gm_cl03rhutns3[112] __attribute__((aligned(8)));
extern uint8_t gm_clacg9fr6j2[58] __attribute__((aligned(1)));
extern uint8_t gm_clam38tqp81[14] __attribute__((aligned(1)));
extern uint8_t gm_clluf6b4idl[15] __attribute__((aligned(1)));
extern uint8_t gm_cmn023hbeik[8] __attribute__((aligned(1)));
extern uint8_t gm_cmq9in3i5gq[3] __attribute__((aligned(1)));
extern uint8_t gm_cmu8f7egfr6[2] __attribute__((aligned(1)));
extern uint8_t gm_coj5ig90obd[5] __attribute__((aligned(1)));
extern uint8_t gm_cojk58d4vkf[16] __attribute__((aligned(8)));
extern uint8_t gm_cotbkjp0ndq[17] __attribute__((aligned(1)));
extern uint8_t gm_cp10508qfq2[9] __attribute__((aligned(1)));
extern uint8_t gm_cplet0icpi8[13] __attribute__((aligned(1)));
extern uint8_t gm_cqmh9ctmru3[8] __attribute__((aligned(1)));
extern uint8_t gm_cqpdkhks98h[10] __attribute__((aligned(1)));
extern uint8_t gm_cr1c8sm70hn[14] __attribute__((aligned(1)));
extern uint8_t gm_cr443kab48n[13] __attribute__((aligned(1)));
extern uint8_t gm_crjl9v1c7hv[4] __attribute__((aligned(1)));
extern uint8_t gm_csoii12j71c[5] __attribute__((aligned(1)));
extern uint8_t gm_ctcahh0irb8[1] __attribute__((aligned(1)));
extern uint8_t gm_cu9odmhbpt4[18] __attribute__((aligned(1)));
extern uint8_t gm_cundh4q6nta[11] __attribute__((aligned(1)));
extern void *const gm_hl6hnpe3vbk7[];
extern void *const gm_hlh12hh1mjda[];
extern void *const gm_hli2tfvgok6p[];
extern void *const gm_hlvanl0isvqe[];
extern uint8_t gm_k75gp9b9r49[40] __attribute__((aligned(8)));
extern uint8_t gm_k8k7i06ilpr[40] __attribute__((aligned(8)));
extern uint8_t gm_k9o9iqv20c0[40] __attribute__((aligned(8)));
extern uint8_t gm_kaa2flncdji[40] __attribute__((aligned(8)));
extern uint8_t gm_kbeesj1a2or[40] __attribute__((aligned(8)));
extern uint8_t gm_kbng43moaud[40] __attribute__((aligned(8)));
extern uint8_t gm_kdjk0ti7atv[40] __attribute__((aligned(8)));
extern uint8_t gm_kfnccdpc4g8[40] __attribute__((aligned(8)));
extern uint8_t gm_kgccnm9nmdk[40] __attribute__((aligned(8)));
extern uint8_t gm_kl3hhlfsksr[40] __attribute__((aligned(8)));
extern uint8_t gm_klc17hcqkii[40] __attribute__((aligned(8)));
extern uint8_t gm_klr1ounjj8d[40] __attribute__((aligned(8)));
extern uint8_t gm_kr7fqi9m0ih[40] __attribute__((aligned(8)));
extern uint8_t gm_krlms0mdefs[40] __attribute__((aligned(8)));
extern uint8_t gm_krsahem6qn0[40] __attribute__((aligned(8)));
extern uint8_t gm_ks0d3nb0eoq[40] __attribute__((aligned(8)));
extern uint8_t gm_ksi8gqve35e[40] __attribute__((aligned(8)));
extern uint8_t gm_ksnfevokhod[40] __attribute__((aligned(8)));
extern uint8_t gm_kv17k6a87tq[40] __attribute__((aligned(8)));
extern uint8_t gm_kvu1vd24o16[40] __attribute__((aligned(8)));
extern const uint8_t gm_s02nu08jrc7mi[13];
extern const uint8_t gm_s03c24qg7i06q[13];
extern const uint8_t gm_s03itq0fotubb[14];
extern const uint8_t gm_s05336tfii06a[18];
extern const uint8_t gm_s06pkj5koudes[11];
extern const uint8_t gm_s078u1763sqk3[11];
extern const uint8_t gm_s09rebkngh86a[34];
extern const uint8_t gm_s0a1978iad5c9[5];
extern const uint8_t gm_s0ajsem4h8nft[58];
extern const uint8_t gm_s0aqhubr4upni[7];
extern const uint8_t gm_s0b795193rvs2[7];
extern const uint8_t gm_s0bc9s1qderg8[70];
extern const uint8_t gm_s0eipmcrsllno[8];
extern const uint8_t gm_s0f4hcujnvqta[95];
extern const uint8_t gm_s0ibfi39mcc7n[9];
extern const uint8_t gm_s0johk446liuj[28];
extern const uint8_t gm_s0m0qaokk51g0[4];
extern const uint8_t gm_s0m3gh47554ab[6];
extern const uint8_t gm_s0mm4r75od12l[7];
extern const uint8_t gm_s0pqdsaf6jp6t[9];
extern const uint8_t gm_s0qfhsu29ohf6[14];
extern const uint8_t gm_s0s45q9kur545[15];
extern const uint8_t gm_s0tt0537if3ve[29];
extern const uint8_t gm_s10iv34fu7qe7[3];
extern const uint8_t gm_s10qn2lc8ck56[78];
extern const uint8_t gm_s13vssup4nqsr[12];
extern const uint8_t gm_s147lrab6u68i[29];
extern const uint8_t gm_s15c9j2vv1d2a[39];
extern const uint8_t gm_s15tjb1oda5hu[6];
extern const uint8_t gm_s1asl1e1tvb0k[2];
extern const uint8_t gm_s1b0nuksrh572[29];
extern const uint8_t gm_s1bpuqd44lur7[7];
extern const uint8_t gm_s1cn5ab5opg9f[131];
extern const uint8_t gm_s1ec711cn6c5f[33];
extern const uint8_t gm_s1ecdmpgj8faq[24];
extern const uint8_t gm_s1f440st4mnkp[2];
extern const uint8_t gm_s1fshvu7vb9i4[16];
extern const uint8_t gm_s1hspsqta2g4h[9];
extern const uint8_t gm_s1i00i6e3g77j[3];
extern const uint8_t gm_s1k6vrpuaktdj[13];
extern const uint8_t gm_s1l7s35k2bj17[8];
extern const uint8_t gm_s1mc2427h3p1h[9];
extern const uint8_t gm_s1nvbb638rr96[8];
extern const uint8_t gm_s1o0cl13ab3oc[18];
extern const uint8_t gm_s1p5ld7i8mkfu[10];
extern const uint8_t gm_s1r4msb7spg8n[4];
extern const uint8_t gm_s1tfd03l0gv6e[14];
extern const uint8_t gm_s1utcdes4r8ku[7];
extern const uint8_t gm_s20mq4dk47bdd[4];
extern const uint8_t gm_s23n3pek53qm1[24];
extern const uint8_t gm_s24et6r4rgifd[16];
extern const uint8_t gm_s24mra5tninbk[15];
extern const uint8_t gm_s26gmgbksn2pm[61];
extern const uint8_t gm_s274psem80gbe[10];
extern const uint8_t gm_s27jprdv4pn6e[10];
extern const uint8_t gm_s27tupsc0esm0[17];
extern const uint8_t gm_s2a85dvpmjp4u[14];
extern const uint8_t gm_s2bumqed39p41[17];
extern const uint8_t gm_s2esennel4fsa[38];
extern const uint8_t gm_s2ha7brjf2gmp[7];
extern const uint8_t gm_s2idf31094gi9[8];
extern const uint8_t gm_s2j5j1obh5p7l[6];
extern const uint8_t gm_s2j8t1jqamft8[5];
extern const uint8_t gm_s2kr3v1c3a77k[17];
extern const uint8_t gm_s2kubr08sqj72[15];
extern const uint8_t gm_s2m4r2316r14d[11];
extern const uint8_t gm_s2n2cdbc6724t[10];
extern const uint8_t gm_s2o9ogopuec6c[108];
extern const uint8_t gm_s2oqp92hnbubt[11];
extern const uint8_t gm_s2u7e0bju5qb8[28];
extern const uint8_t gm_s2urait735ofq[5];
extern const uint8_t gm_s2vj6kgk19dag[32];
extern const uint8_t gm_s3347hvco05di[6];
extern const uint8_t gm_s33hvuvcdjs1b[11];
extern const uint8_t gm_s38t0si42sbdd[49];
extern const uint8_t gm_s3bkmscn5fi56[3];
extern const uint8_t gm_s3duthvmnjluh[148];
extern const uint8_t gm_s3ea3k3og84ri[9];
extern const uint8_t gm_s3gqldvcud6t6[17];
extern const uint8_t gm_s3hg5go4uihat[52];
extern const uint8_t gm_s3jllttnltak3[26];
extern const uint8_t gm_s3js9g3l46oiv[40];
extern const uint8_t gm_s3ku3d0vbon9r[12];
extern const uint8_t gm_s3lqm9addfamn[56];
extern const uint8_t gm_s3m12mc3q4gpj[71];
extern const uint8_t gm_s3nhq46nogqdt[12];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s3p8gug7bvl1p[3];
extern const uint8_t gm_s3q858hngo0ho[39];
extern const uint8_t gm_s3t2d4i4t34ct[74];
extern const uint8_t gm_s3tqkqetpkgjr[84];
extern const uint8_t gm_s3vboaodpmi9a[4];
extern const uint8_t gm_s41bap0m04tt9[12];
extern const uint8_t gm_s41h7dv6tes4i[57];
extern const uint8_t gm_s42atdm90l4do[14];
extern const uint8_t gm_s48n17gh1k2md[3];
extern const uint8_t gm_s49ep90bjfcs6[41];
extern const uint8_t gm_s4a980fahtblo[3];
extern const uint8_t gm_s4av1h975k9ok[47];
extern const uint8_t gm_s4bcn9pd0h83n[48];
extern const uint8_t gm_s4bf0cndbl9lv[3];
extern const uint8_t gm_s4cess8gum6i7[6];
extern const uint8_t gm_s4gt1buiaiaiq[3];
extern const uint8_t gm_s4i1t9tcolakn[48];
extern const uint8_t gm_s4l3p0f0c6mmc[44];
extern const uint8_t gm_s4lj2bemc87jp[3];
extern const uint8_t gm_s4lmdtjb9rnah[70];
extern const uint8_t gm_s4n8c66u7mg94[19];
extern const uint8_t gm_s4o541hcfllua[9];
extern const uint8_t gm_s4oil3epcv041[3];
extern const uint8_t gm_s4pv32nkh7r2r[11];
extern const uint8_t gm_s4qg9pu9r16jq[7];
extern const uint8_t gm_s4rj8vs79ru3v[12];
extern const uint8_t gm_s4rugtmn8jarn[57];
extern const uint8_t gm_s4uu8ali3icr8[40];
extern const uint8_t gm_s52vv4hfcfuh5[15];
extern const uint8_t gm_s542rn9shphg8[16];
extern const uint8_t gm_s57kk096d0ite[45];
extern const uint8_t gm_s57qe8p5hq2v9[13];
extern const uint8_t gm_s5aj93skvhpnl[17];
extern const uint8_t gm_s5dln0neuav6j[11];
extern const uint8_t gm_s5epronuu4280[7];
extern const uint8_t gm_s5ku0alcfvlas[41];
extern const uint8_t gm_s5lqhiu2bn6of[8];
extern const uint8_t gm_s5m8hf43qu2i7[27];
extern const uint8_t gm_s5m8rkq92eohf[82];
extern const uint8_t gm_s5m9qimbv7e8k[3];
extern const uint8_t gm_s5pra70i8o734[14];
extern const uint8_t gm_s5s1m7053mh3n[8];
extern const uint8_t gm_s5sdmhb4570qm[20];
extern const uint8_t gm_s5sin3j2044cm[41];
extern const uint8_t gm_s5u1jd4mc0941[32];
extern const uint8_t gm_s5u5a7gd2hl9p[6];
extern const uint8_t gm_s5udr3eget84q[6];
extern const uint8_t gm_s5url3si4frr2[69];
extern const uint8_t gm_s654e61ni0n2t[67];
extern const uint8_t gm_s65dghs61r7cp[13];
extern const uint8_t gm_s66hh83pieh4l[49];
extern const uint8_t gm_s66ph0gfa95db[14];
extern const uint8_t gm_s67qasqqp59vt[513];
extern const uint8_t gm_s688e8m4pgcda[6];
extern const uint8_t gm_s6aprmc3mqp0t[2];
extern const uint8_t gm_s6c1femb11dge[76];
extern const uint8_t gm_s6eeocplfbr8s[41];
extern const uint8_t gm_s6g1vvoofsgo5[21];
extern const uint8_t gm_s6hq50r4rk7e6[26];
extern const uint8_t gm_s6kvnpuo6o71f[26];
extern const uint8_t gm_s6lr92ekld44s[13];
extern const uint8_t gm_s6m1a6s4q8cuh[50];
extern const uint8_t gm_s6n6v50inu7p0[8];
extern const uint8_t gm_s6nsj8sc6uhgg[17];
extern const uint8_t gm_s6pe06l7iops0[5];
extern const uint8_t gm_s6q7rsv30u3ma[34];
extern const uint8_t gm_s6rmjmkjior9j[13];
extern const uint8_t gm_s6tkmvprase06[14];
extern const uint8_t gm_s6u6c6o8c4lg0[5];
extern const uint8_t gm_s6uch3l6osunv[50];
extern const uint8_t gm_s72l6okdj2d3g[95];
extern const uint8_t gm_s78m4481dj77r[9];
extern const uint8_t gm_s78naq2blbmbt[32];
extern const uint8_t gm_s7a0708p0cdgv[38];
extern const uint8_t gm_s7a42e93eb2m6[15];
extern const uint8_t gm_s7eippa38tft8[7];
extern const uint8_t gm_s7fpdr39r789h[15];
extern const uint8_t gm_s7h9qsm54pb7m[6];
extern const uint8_t gm_s7ifc1fku673p[5];
extern const uint8_t gm_s7jj9572hd242[6];
extern const uint8_t gm_s7jvhfqq2rri8[30];
extern const uint8_t gm_s7knd69l0mhc0[36];
extern const uint8_t gm_s7q4c8j1mnq1f[22];
extern const uint8_t gm_s7qbqthgde8lk[29];
extern const uint8_t gm_s7uuha79fcmn5[18];
extern const uint8_t gm_s7vsso7k8tg8d[25];
extern const uint8_t gm_s7vv7qoquqmn9[14];
extern const uint8_t gm_s81l00c48rg4v[26];
extern const uint8_t gm_s81ppjm26jd5s[148];
extern const uint8_t gm_s827qun5pki1c[5];
extern const uint8_t gm_s82k80d9jrgm6[4];
extern const uint8_t gm_s84hftqv0533g[5];
extern const uint8_t gm_s85jtgs7au5b5[9];
extern const uint8_t gm_s866ofni9sb8u[7];
extern const uint8_t gm_s87irmg2sqslv[33];
extern const uint8_t gm_s88u2nt2tl9hi[11];
extern const uint8_t gm_s8becutukvgrh[17];
extern const uint8_t gm_s8c49n38a2v0d[41];
extern const uint8_t gm_s8cq7ej9hnrr0[12];
extern const uint8_t gm_s8hhhdlan8pqr[8];
extern const uint8_t gm_s8i3r4s519jok[8];
extern const uint8_t gm_s8n2bs6jikgsm[4];
extern const uint8_t gm_s8n858pjjep3e[10];
extern const uint8_t gm_s8pjrp7ublsfs[171];
extern const uint8_t gm_s8rm42n03sfss[4];
extern const uint8_t gm_s8sgunk80orlb[26];
extern const uint8_t gm_s8vjpeo1vp60m[16];
extern const uint8_t gm_s90aivk1p6or5[16];
extern const uint8_t gm_s92nfbpklold8[41];
extern const uint8_t gm_s93ft79mfnlvp[10];
extern const uint8_t gm_s95cbsclgo7gk[36];
extern const uint8_t gm_s95gukpblkpua[33];
extern const uint8_t gm_s97nfli622uqg[5];
extern const uint8_t gm_s98novtk49ooh[28];
extern const uint8_t gm_s9cb6j5ok7bdk[3];
extern const uint8_t gm_s9dh9d8gs6a16[9];
extern const uint8_t gm_s9e3360r5ee0o[7];
extern const uint8_t gm_s9fseescjm8u4[25];
extern const uint8_t gm_s9hi7i7cl3e2t[78];
extern const uint8_t gm_s9indmpqvpug4[58];
extern const uint8_t gm_s9jqgpofojve4[35];
extern const uint8_t gm_s9kudvjpckcsm[10];
extern const uint8_t gm_s9npjns7bnqaj[19];
extern const uint8_t gm_s9o2c36nosv54[41];
extern const uint8_t gm_s9oarmm2oe3id[41];
extern const uint8_t gm_s9ol82jrvi3ma[4];
extern const uint8_t gm_s9oo6ammv0sch[17];
extern const uint8_t gm_s9rm2mbmo3nku[7];
extern const uint8_t gm_sa29cuhpvu442[54];
extern const uint8_t gm_sa2e14pm9g0bb[17];
extern const uint8_t gm_sa7c621p9d7mr[13];
extern const uint8_t gm_sa7chm2nt7fsk[27];
extern const uint8_t gm_sa8gjfvck9d40[30];
extern const uint8_t gm_sa8mtcad5rfmf[26];
extern const uint8_t gm_sa8neoommnpov[24];
extern const uint8_t gm_sa9pve8r9407o[35];
extern const uint8_t gm_saa0ue0fi38t8[15];
extern const uint8_t gm_sabd09jov3efr[32];
extern const uint8_t gm_sabdpctdijia8[41];
extern const uint8_t gm_saeed1dnph0jk[52];
extern const uint8_t gm_sajhc6og5bv8v[59];
extern const uint8_t gm_sajso5sujsruj[47];
extern const uint8_t gm_san68r6np95il[6];
extern const uint8_t gm_sao4mumvadoct[4];
extern const uint8_t gm_saqh6vgbmf8m2[153];
extern const uint8_t gm_sasea0sobfkrs[8];
extern const uint8_t gm_sat505mlqphna[26];
extern const uint8_t gm_sat80i3p7mq2f[26];
extern const uint8_t gm_saveva62qedei[8];
extern const uint8_t gm_sb11lrfd85gub[14];
extern const uint8_t gm_sb22ajpcscr7v[41];
extern const uint8_t gm_sb3o36rmuvj28[56];
extern const uint8_t gm_sb6smklmjl60o[7];
extern const uint8_t gm_sb745qqv6kdrg[11];
extern const uint8_t gm_sb895t6tk3m9h[6];
extern const uint8_t gm_sb8i7j9s2n6jl[8];
extern const uint8_t gm_sb9bgabg66tgj[42];
extern const uint8_t gm_sb9dglh7ekrt2[7];
extern const uint8_t gm_sba0bp6ej30io[30];
extern const uint8_t gm_sba60r64q4l0i[31];
extern const uint8_t gm_sbanjlvk3n14i[4];
extern const uint8_t gm_sbb1ri5er9djq[4];
extern const uint8_t gm_sbc7a60c47o3n[19];
extern const uint8_t gm_sbdno1ojbbrbs[45];
extern const uint8_t gm_sbgbo6kv938k3[19];
extern const uint8_t gm_sbk9dggcntbu6[100];
extern const uint8_t gm_sbkdsdb6p9kk7[4];
extern const uint8_t gm_sbp9encpv0e61[75];
extern const uint8_t gm_sbvutvpa8pe3d[3];
extern const uint8_t gm_sc0mj6111f7cc[10];
extern const uint8_t gm_sc4hsc9atc69r[33];
extern const uint8_t gm_sc758i4r41ge5[128];
extern const uint8_t gm_sc7ertn8kmtl3[4];
extern const uint8_t gm_scbch1b3qo8hr[9];
extern const uint8_t gm_scbj8d61i03he[14];
extern const uint8_t gm_scc54gv4a7k04[6];
extern const uint8_t gm_scds626ikjji4[13];
extern const uint8_t gm_scduvjunh0oei[9];
extern const uint8_t gm_scgahod5hue9b[56];
extern const uint8_t gm_schmrvl8mk83l[9];
extern const uint8_t gm_schvua5hkahp9[16];
extern const uint8_t gm_scj618tc62815[15];
extern const uint8_t gm_scmgj2ccom9v4[40];
extern const uint8_t gm_scq06n33arhgv[5];
extern const uint8_t gm_scqt00otrhg1t[41];
extern const uint8_t gm_scr60nbumsj2h[25];
extern const uint8_t gm_scs28q27cdacb[10];
extern const uint8_t gm_sd7cce7oc21kr[36];
extern const uint8_t gm_sd7vgd2pm42gk[14];
extern const uint8_t gm_sdaknqn35hokt[9];
extern const uint8_t gm_sdam23bqvafqn[18];
extern const uint8_t gm_sdbvtaeiovn59[102];
extern const uint8_t gm_sdc36oketvdk3[4];
extern const uint8_t gm_sddnsndqgnosl[31];
extern const uint8_t gm_sdf1o8jga4hc6[38];
extern const uint8_t gm_sdg7s5c89ve6u[64];
extern const uint8_t gm_sdgdb59na1854[23];
extern const uint8_t gm_sdhddtlu453l5[13];
extern const uint8_t gm_sdhelph0q09tv[5];
extern const uint8_t gm_sdhlphf1ucpbo[18];
extern const uint8_t gm_sdhns4s61djoc[41];
extern const uint8_t gm_sdi3k3oc2kq9i[15];
extern const uint8_t gm_sdnh9bi444ml9[14];
extern const uint8_t gm_sdo1hnokt0mq8[17];
extern const uint8_t gm_sdpo2hjlq8e9b[31];
extern const uint8_t gm_sdqgviehbfoad[6];
extern const uint8_t gm_sdrbnq4aduai5[16];
extern const uint8_t gm_sdu3h28udk8s9[13];
extern const uint8_t gm_sdvp5f4riom9f[3];
extern const uint8_t gm_se006k0nbfdeg[8];
extern const uint8_t gm_se08n5d1gngu7[5];
extern const uint8_t gm_se0plntg0il3g[14];
extern const uint8_t gm_se2n4k927a8pv[41];
extern const uint8_t gm_se2qf1ipmcha3[26];
extern const uint8_t gm_se3md9tm534o5[33];
extern const uint8_t gm_se7kda46rm021[107];
extern const uint8_t gm_se7oqjo2sonp9[9];
extern const uint8_t gm_se8v3b7tfvt0e[23];
extern const uint8_t gm_sech7am46drki[16];
extern const uint8_t gm_sej6t944fna3a[61];
extern const uint8_t gm_sekn24oquoo60[9];
extern const uint8_t gm_selatfetsf85i[10];
extern const uint8_t gm_sem2htac3j4np[66];
extern const uint8_t gm_semhbv8seph85[21];
extern const uint8_t gm_seo41mk3n3ro5[13];
extern const uint8_t gm_seor1k5gntiuc[52];
extern const uint8_t gm_sep8ve6ec8tv7[24];
extern const uint8_t gm_sepmad5fu1s0n[8];
extern const uint8_t gm_ser747td9jt8d[41];
extern const uint8_t gm_sevf8q26aov70[6];
extern const uint8_t gm_sf1f5se9222sg[35];
extern const uint8_t gm_sf3kc6ftec6at[166];
extern const uint8_t gm_sf42bd5sk6816[26];
extern const uint8_t gm_sf44a6nc03ff5[10];
extern const uint8_t gm_sf6pbim706kre[4];
extern const uint8_t gm_sf8bp31mrq32l[19];
extern const uint8_t gm_sfali8nl85hc9[25];
extern const uint8_t gm_sfbip2dg905u8[7];
extern const uint8_t gm_sfbr39r30psvt[4];
extern const uint8_t gm_sfc0aek8lt25a[5];
extern const uint8_t gm_sfd0mn3sol25u[4];
extern const uint8_t gm_sfdhf0de18rq4[6];
extern const uint8_t gm_sfdj0c5vltira[31];
extern const uint8_t gm_sfelk2428ha3h[19];
extern const uint8_t gm_sfeo7g7huf2q1[17];
extern const uint8_t gm_sfftb05vrc1qn[81];
extern const uint8_t gm_sfhct1r6m1qqo[11];
extern const uint8_t gm_sfi2ha42r1nr2[2];
extern const uint8_t gm_sfjc41f66fqb2[10];
extern const uint8_t gm_sfjsmf1lebgp7[14];
extern const uint8_t gm_sfk3r5va3vobm[5];
extern const uint8_t gm_sfl0j5tlur0e6[55];
extern const uint8_t gm_sfm9elkq6bn2e[2];
extern const uint8_t gm_sfmm1kh41nanh[15];
extern const uint8_t gm_sfna4d5mnmoa8[30];
extern const uint8_t gm_sfpeqbitcbq42[6];
extern const uint8_t gm_sfr85ju00e9hk[138];
extern const uint8_t gm_sfusuld9q5ekv[44];
extern const uint8_t gm_sg1s2lngtt92k[13];
extern const uint8_t gm_sg2r3pcbg3kr2[32];
extern const uint8_t gm_sg3dnv3iaqhkk[8];
extern const uint8_t gm_sg550i4fp569j[55];
extern const uint8_t gm_sg5e6vqb8nie3[18];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sg5n0glcljcci[33];
extern const uint8_t gm_sg7of7eks8a5u[7];
extern const uint8_t gm_sg7ronlh5k7js[84];
extern const uint8_t gm_sgb12qbh3gevi[5];
extern const uint8_t gm_sgc7crh43q9tj[6];
extern const uint8_t gm_sgcku4bq4407l[19];
extern const uint8_t gm_sgcporqt2gqqg[16];
extern const uint8_t gm_sgepo3aen3iei[15];
extern const uint8_t gm_sgfrcimthamdn[10];
extern const uint8_t gm_sgfufhs3psibd[4];
extern const uint8_t gm_sgh1o65rai9bu[8];
extern const uint8_t gm_sghh3jr7dlapd[66];
extern const uint8_t gm_sgi6la00bpjt2[15];
extern const uint8_t gm_sgisth1q9gntc[41];
extern const uint8_t gm_sgmn23so20vv9[33];
extern const uint8_t gm_sgq8hfs47hogb[9];
extern const uint8_t gm_sgrdu6jn9jtpr[35];
extern const uint8_t gm_sgukm1evmrjal[4];
extern const uint8_t gm_sgurqv8kb2rs7[32];
extern const uint8_t gm_sh00ruc3m501e[42];
extern const uint8_t gm_sh08f8dq9lenu[41];
extern const uint8_t gm_sh16qm7deqse5[7];
extern const uint8_t gm_sh4r0fq5alj61[14];
extern const uint8_t gm_sh57gpir0vvh2[24];
extern const uint8_t gm_shbaullahutpe[37];
extern const uint8_t gm_shda201svb9eg[119];
extern const uint8_t gm_shfckrpors679[49];
extern const uint8_t gm_shh2fi8ibtel0[41];
extern const uint8_t gm_shhtc33mlfd4u[31];
extern const uint8_t gm_shhugap3t1rkg[41];
extern const uint8_t gm_shi7nidbc64qf[4];
extern const uint8_t gm_shl6ef6if4a86[80];
extern const uint8_t gm_shm91i3u3uv6p[5];
extern const uint8_t gm_shmpls8n0g8h9[4];
extern const uint8_t gm_shnp9b6k2mab8[5];
extern const uint8_t gm_shrc3j552p60m[9];
extern const uint8_t gm_shrlc4g1vtjsu[47];
extern const uint8_t gm_shrq42dntcbrd[19];
extern const uint8_t gm_shukorb0nk4kg[7];
extern const uint8_t gm_shv2qb39iqelf[31];
extern const uint8_t gm_si0b5q45ot754[101];
extern const uint8_t gm_si413smv36ejf[7];
extern const uint8_t gm_siamquc19uikr[2];
extern const uint8_t gm_sibmp9mck7osv[13];
extern const uint8_t gm_sidpdvffdra02[14];
extern const uint8_t gm_sif59l16kubo3[17];
extern const uint8_t gm_sif5vtcq0ji03[17];
extern const uint8_t gm_sifiecpi6vdsm[11];
extern const uint8_t gm_sig2adm7e9t55[79];
extern const uint8_t gm_sighahslvsc1i[11];
extern const uint8_t gm_sigup1gvr7a13[5];
extern const uint8_t gm_sihdt3ld7f53j[16];
extern const uint8_t gm_sihs6d097ocev[5];
extern const uint8_t gm_sikpqtja8pqpv[3];
extern const uint8_t gm_siooblm685vmo[16];
extern const uint8_t gm_siotufd8s66oj[21];
extern const uint8_t gm_siqfg68f4ud6j[16];
extern const uint8_t gm_siqpmbbs844ch[41];
extern const uint8_t gm_sj1gcl0qo4i9p[7];
extern const uint8_t gm_sj3ug62t7s271[14];
extern const uint8_t gm_sj4cbut2umg1f[35];
extern const uint8_t gm_sj7cc74b29dv3[14];
extern const uint8_t gm_sj9krj49k3580[10];
extern const uint8_t gm_sj9t4d2ku5m56[47];
extern const uint8_t gm_sjac221bnk353[8];
extern const uint8_t gm_sjb8gf8n3vsft[42];
extern const uint8_t gm_sjbhqu0o8ebub[6];
extern const uint8_t gm_sjdk7e5fs4top[6];
extern const uint8_t gm_sje4rbc88csdu[7];
extern const uint8_t gm_sjgrq0dv175qo[30];
extern const uint8_t gm_sjil3d717dq6h[63];
extern const uint8_t gm_sjlktf1otofg9[10];
extern const uint8_t gm_sjmhgc2b6rnks[10];
extern const uint8_t gm_sjpr4fvui63qj[13];
extern const uint8_t gm_sjqp7nokhg4lk[5];
extern const uint8_t gm_sjrcmga748cdu[24];
extern const uint8_t gm_sjtfkltao8c2o[49];
extern const uint8_t gm_sjvb17co9jlff[15];
extern const uint8_t gm_sk01103sqa2lh[36];
extern const uint8_t gm_sk0qegfrnee00[4];
extern const uint8_t gm_sk25jsmieu0fo[41];
extern const uint8_t gm_sk2nrl11c01gj[20];
extern const uint8_t gm_skbgm82aqj3q0[6];
extern const uint8_t gm_ski66jdm8e243[9];
extern const uint8_t gm_sko4cgkdijve3[57];
extern const uint8_t gm_skq2uj9oi1243[41];
extern const uint8_t gm_skrdl5pdcve8g[43];
extern const uint8_t gm_sksh6i7obnq40[8];
extern const uint8_t gm_skv53e6m2c88s[10];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_sl5jkfh6104lp[5];
extern const uint8_t gm_sl7doakbaoimv[7];
extern const uint8_t gm_sld9l7nonn4o9[17];
extern const uint8_t gm_slgbfnvgul4q1[7];
extern const uint8_t gm_slgu49nv3o8e7[12];
extern const uint8_t gm_slhsp4invn4vh[7];
extern const uint8_t gm_slmlefe4cpf5e[15];
extern const uint8_t gm_sloeu62tmfgqn[9];
extern const uint8_t gm_slopv2r8r4od2[5];
extern const uint8_t gm_slpd42s8rcbm0[8];
extern const uint8_t gm_slqq08j0fro21[31];
extern const uint8_t gm_sltejl1fekbag[6];
extern const uint8_t gm_sltjg0sbhbo73[13];
extern const uint8_t gm_slu6nnvsgea2t[41];
extern const uint8_t gm_slu92ianijdhp[7];
extern const uint8_t gm_sm56lhobvb6g5[6];
extern const uint8_t gm_sm5be4659ulci[2];
extern const uint8_t gm_sm7179tmr0nhi[27];
extern const uint8_t gm_sm7hp6cgh4jle[6];
extern const uint8_t gm_sm8uf6b4m0n7p[8];
extern const uint8_t gm_sma6ellkcmke6[26];
extern const uint8_t gm_smacop0e0umsg[257];
extern const uint8_t gm_smaqn3dnro8sl[14];
extern const uint8_t gm_smb8nsuj9ie93[15];
extern const uint8_t gm_smbgfcmcr0r8n[11];
extern const uint8_t gm_smbls870q1nm0[16];
extern const uint8_t gm_smc854k364mj6[21];
extern const uint8_t gm_smddipuf83u9j[3];
extern const uint8_t gm_smhjrt0kub63u[15];
extern const uint8_t gm_smk6fmrgs1hnc[6];
extern const uint8_t gm_smo8ffg4jq2d6[45];
extern const uint8_t gm_smooaouqmm8dj[2];
extern const uint8_t gm_smovt8m5cpa13[7];
extern const uint8_t gm_smpde4t8bsefo[14];
extern const uint8_t gm_smpmbscbko3gk[4];
extern const uint8_t gm_smqhtg33814q9[23];
extern const uint8_t gm_smror2m9tsk7v[8];
extern const uint8_t gm_sn275ucdkgsk6[82];
extern const uint8_t gm_sn2t7jm5mf8pm[5];
extern const uint8_t gm_sn35ih51dmdii[45];
extern const uint8_t gm_sn3tcnrle7f4r[4];
extern const uint8_t gm_sn4utba4jl66l[9];
extern const uint8_t gm_snbb2gfh3k17u[26];
extern const uint8_t gm_snbrjpgiqf592[6];
extern const uint8_t gm_sncbtqrmp1734[14];
extern const uint8_t gm_sneh1858d1bko[20];
extern const uint8_t gm_snh0sl2cdagdn[13];
extern const uint8_t gm_snh2jpqenu534[17];
extern const uint8_t gm_snjq35j2j9d3c[113];
extern const uint8_t gm_snr5o5tak6405[4];
extern const uint8_t gm_sns1hl83hdlcn[5];
extern const uint8_t gm_snukl6jqep6jf[4];
extern const uint8_t gm_so05t2goj6895[24];
extern const uint8_t gm_so2ihtmk3e8kf[17];
extern const uint8_t gm_so43rnba8j5pb[35];
extern const uint8_t gm_so44j4cr69vhs[16];
extern const uint8_t gm_so4acijgbtr2b[34];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_so5kmn66hbf25[22];
extern const uint8_t gm_so686pdrsalfh[587];
extern const uint8_t gm_so70siafe1e7a[13];
extern const uint8_t gm_so77flgm8klar[52];
extern const uint8_t gm_so7qmna0fo4so[6];
extern const uint8_t gm_so7svtiv1a8q1[5];
extern const uint8_t gm_so81f3f3mbv7o[54];
extern const uint8_t gm_so9fi6r28770a[3];
extern const uint8_t gm_soah6d9unb7uo[32];
extern const uint8_t gm_sobecpce3c56n[35];
extern const uint8_t gm_sodq6lf7v3uc2[11];
extern const uint8_t gm_soechff4t3q96[9];
extern const uint8_t gm_sog2vuuvk361m[22];
extern const uint8_t gm_soh6rol6f5r5k[27];
extern const uint8_t gm_soh8b8t1v3pmj[56];
extern const uint8_t gm_sokol7c4qt12p[6];
extern const uint8_t gm_sokruug51vi6k[11];
extern const uint8_t gm_somhu4vltiil1[4];
extern const uint8_t gm_soogmlptfncs1[12];
extern const uint8_t gm_soreld657hea2[25];
extern const uint8_t gm_sos7rilr21per[7];
extern const uint8_t gm_sosa5l0k6d34c[15];
extern const uint8_t gm_sottk452c14qm[56];
extern const uint8_t gm_sovtt6julbcd8[12];
extern const uint8_t gm_sp02qqgucmpt7[3];
extern const uint8_t gm_sp13ju5l53585[24];
extern const uint8_t gm_sp21r812mg46a[4];
extern const uint8_t gm_sp4l42tpo6btg[11];
extern const uint8_t gm_sp5hf74268mcu[41];
extern const uint8_t gm_sp625mljmvm7d[11];
extern const uint8_t gm_sp68fbgfk0baj[39];
extern const uint8_t gm_sp6f2rgoi8toe[7];
extern const uint8_t gm_sp75410cl55v2[13];
extern const uint8_t gm_sp7cdd0n91vli[8];
extern const uint8_t gm_sp7ec3ksfdilk[5];
extern const uint8_t gm_sp8jfj95g4f47[7];
extern const uint8_t gm_spa5q1834e71s[49];
extern const uint8_t gm_spajg3q27hr58[15];
extern const uint8_t gm_spdntfv7vkd2t[17];
extern const uint8_t gm_spg4d1df6jnji[7];
extern const uint8_t gm_spgfhj218uj9k[8];
extern const uint8_t gm_spj5hvik290iq[41];
extern const uint8_t gm_spj9rva8kb5ar[3];
extern const uint8_t gm_spjfgtrrvac57[14];
extern const uint8_t gm_splv67uudsbdi[23];
extern const uint8_t gm_spm4139orpkvm[10];
extern const uint8_t gm_spni6j7splqto[8];
extern const uint8_t gm_spod3tmkh25up[2];
extern const uint8_t gm_spp0mb1aidgi1[13];
extern const uint8_t gm_spskd7qirpoe3[25];
extern const uint8_t gm_sq05bfmao5v8n[47];
extern const uint8_t gm_sq1rqv8ou3vsv[14];
extern const uint8_t gm_sq3jr80otu7tj[3];
extern const uint8_t gm_sq3oimi1i4re9[27];
extern const uint8_t gm_sq5ab39u4btdn[41];
extern const uint8_t gm_sq5p8t05ccn5h[24];
extern const uint8_t gm_sq74a8ckacjn2[41];
extern const uint8_t gm_sq94kuvk30n36[6];
extern const uint8_t gm_sq95m8isfa7vd[2];
extern const uint8_t gm_sq99f4u8gmi6n[28];
extern const uint8_t gm_sqa6gfa8omk7o[4];
extern const uint8_t gm_sqc2or5f1575b[31];
extern const uint8_t gm_sqcj2s8m7mpqj[38];
extern const uint8_t gm_sqdfr4k3vea5p[4];
extern const uint8_t gm_sqdjjj1odavib[34];
extern const uint8_t gm_sqdvt3ooptrnc[5];
extern const uint8_t gm_sqeho958d04c0[17];
extern const uint8_t gm_sqen1asili489[52];
extern const uint8_t gm_sqfc0qpl56rfm[7];
extern const uint8_t gm_sqh06nbksvl8i[17];
extern const uint8_t gm_sqjlosotosjfp[18];
extern const uint8_t gm_sql5634qudrlq[41];
extern const uint8_t gm_sqlorrkumh3an[23];
extern const uint8_t gm_sqnathr6pbque[38];
extern const uint8_t gm_sqo6bs5b6kae9[8];
extern const uint8_t gm_sqqtsglgb4lu3[28];
extern const uint8_t gm_sqtgbqlca4c1g[25];
extern const uint8_t gm_squ1rl0dbtrtr[59];
extern const uint8_t gm_sqvepl1893o5g[3];
extern const uint8_t gm_sr3et5tffndla[28];
extern const uint8_t gm_sr3uqaosat41v[41];
extern const uint8_t gm_sr499ju9p7ks2[23];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const uint8_t gm_sr899mamktpvo[7];
extern const uint8_t gm_sr8jfj391gf5o[3];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src4o48ksu43djd[];
extern const char gm_src5cfou479ljhn[];
extern const char gm_src92rfidruvalq[];
extern const char gm_srcb2bqqsnukmrd[];
extern const char gm_srcb3t5fii3khrp[];
extern const char gm_srcbq9k9kvdokbf[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcdds6vrrht3m4[];
extern const char gm_srcgksioc7kn741[];
extern const char gm_srcjrcmga748cdu[];
extern const char gm_srck89elqhmqqdc[];
extern const char gm_srcp8o48ld4n4jq[];
extern const char gm_srcpit8mdn0781q[];
extern const char gm_srcq1k9f0klj1q6[];
extern const char gm_srcqod65l394oc8[];
extern const char gm_srcqtrdhljiapb8[];
extern const char gm_srcruugaa9g5kd5[];
extern const char gm_srcstmd1b5da98l[];
extern const uint8_t gm_srej05ml2urca[41];
extern const uint8_t gm_sreprot2s508e[3];
extern const uint8_t gm_srfcp96u5v1ui[37];
extern const uint8_t gm_srhrn3fa70ni4[38];
extern const uint8_t gm_sri6fceeqvrql[19];
extern const uint8_t gm_srj5ohb3f61b7[14];
extern const uint8_t gm_srj9vrlard0qi[14];
extern const uint8_t gm_srkvn420e4huu[26];
extern const uint8_t gm_srlpdrobo1i0e[38];
extern const uint8_t gm_srp5cji88najc[10];
extern const uint8_t gm_srp6tpaiaeunu[8];
extern const uint8_t gm_srplcc0p49iui[9];
extern const uint8_t gm_srq5utruoa2v3[28];
extern const uint8_t gm_srru5vqet7hl3[11];
extern const uint8_t gm_srssp1uhlkrll[20];
extern const uint8_t gm_srtm6s969i45s[22];
extern const uint8_t gm_sru7itami12tv[8];
extern const uint8_t gm_srv4dcfccuset[3];
extern const uint8_t gm_srvgp776ipjtg[30];
extern const uint8_t gm_ss2v2ursd8pms[4];
extern const uint8_t gm_ss42hvrprenkk[26];
extern const uint8_t gm_ss4tls6jr9q3m[10];
extern const uint8_t gm_ss558hlhv1kah[31];
extern const uint8_t gm_ss66qg3t949dp[5];
extern const uint8_t gm_ss9d6rvgfqq53[13];
extern const uint8_t gm_ssc5ahdetbl48[17];
extern const uint8_t gm_sscekt5vbls0f[18];
extern const uint8_t gm_sse16dr2qj0im[3];
extern const uint8_t gm_ssfg877iegnma[4];
extern const uint8_t gm_ssga7g3v3nnl7[10];
extern const uint8_t gm_ssj8r83l87dr2[16];
extern const uint8_t gm_ssk8mqd1u6nn7[52];
extern const uint8_t gm_sskv7tavvjk2l[23];
extern const uint8_t gm_sslm2oq18gln3[6];
extern const uint8_t gm_ssmvuscdpkf2v[41];
extern const uint8_t gm_ssnsddtjt3gpa[35];
extern const uint8_t gm_sso4tmqr8gin2[20];
extern const uint8_t gm_sso6pgmtg5r11[3];
extern const uint8_t gm_ssoop4uttr1t0[43];
extern const uint8_t gm_ssq0semhq7h0c[13];
extern const uint8_t gm_ssqt22rph287b[4];
extern const uint8_t gm_ssr6vk8erbj18[13];
extern const uint8_t gm_ssrcn9mu9bp3e[19];
extern const uint8_t gm_ssv5povjldd7c[3];
extern const uint8_t gm_st3c9ev3jkdrf[32];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_st803nqm7iok0[17];
extern const uint8_t gm_st9f93lco88fg[3];
extern const uint8_t gm_sta2ai87l17np[41];
extern const uint8_t gm_stafskn6bv24f[2];
extern const uint8_t gm_stbtfovn5dhm4[5];
extern const uint8_t gm_stco696vp9tto[14];
extern const uint8_t gm_ste1ujg4tll4q[10];
extern const uint8_t gm_stevggi5tvq7c[18];
extern const uint8_t gm_sti61120eur38[59];
extern const uint8_t gm_stkm2m8f3okk8[18];
extern const uint8_t gm_stlhqhje21tdn[97];
extern const uint8_t gm_stmg18u2tvso0[18];
extern const uint8_t gm_stmsadvf54k73[3];
extern const uint8_t gm_sto310q1gi1de[12];
extern const uint8_t gm_sto6ssj6r66e3[30];
extern const uint8_t gm_sto8li44rv2ou[8];
extern const uint8_t gm_stqiimqtsnd8d[9];
extern const uint8_t gm_stqvtvp1hc371[12];
extern const uint8_t gm_stshdb5m6rl63[5];
extern const uint8_t gm_stsr1tpfadro2[15];
extern const uint8_t gm_stu0uvs5p21oc[17];
extern const uint8_t gm_stugmrojdno85[6];
extern const uint8_t gm_stvci3fthpt8h[47];
extern const uint8_t gm_su41i9n9tujml[14];
extern const uint8_t gm_sua0n2tdbtos7[40];
extern const uint8_t gm_sub7l8h1sjc33[12];
extern const uint8_t gm_sucoj11hr978o[14];
extern const uint8_t gm_sue4ionbe9lgh[21];
extern const uint8_t gm_suftl0kath3th[21];
extern const uint8_t gm_suifusi0pb6s0[12];
extern const uint8_t gm_sujqb5peare4g[6];
extern const uint8_t gm_suklu1i9clb5l[48];
extern const uint8_t gm_summega2vonc9[6];
extern const uint8_t gm_suqsoolo29or4[24];
extern const uint8_t gm_suuq26jjqhul4[7];
extern const uint8_t gm_suv60trdc5vnn[41];
extern const uint8_t gm_suvpcmvpe5152[41];
extern const uint8_t gm_sv2qh30031o3v[5];
extern const uint8_t gm_sv397f5vej3g7[30];
extern const uint8_t gm_sv44r8pinka4e[13];
extern const uint8_t gm_sv8lr20bqpljh[12];
extern const uint8_t gm_sv8s0ogr3iflj[8];
extern const uint8_t gm_svbftsm94r0bv[7];
extern const uint8_t gm_svdodmni8et3b[15];
extern const uint8_t gm_sveg1mb15s1b2[18];
extern const uint8_t gm_sveguffvp7vd6[15];
extern const uint8_t gm_svfn64lgobnrm[48];
extern const uint8_t gm_svgk3n4sk1uat[9];
extern const uint8_t gm_svhavo13qha5k[4];
extern const uint8_t gm_svi6c6nnkemmt[13];
extern const uint8_t gm_svkdb6kaja59r[7];
extern const uint8_t gm_svkk03nujpn4k[8];
extern const uint8_t gm_svl0gta90bsdr[49];
extern const uint8_t gm_svl13be5ca8rt[5];
extern const uint8_t gm_svpf3kbht0fr3[14];
extern const uint8_t gm_svs91tvrok38m[7];
extern const uint8_t gm_svsiu4jgdp97c[12];
extern const uint8_t gm_svthrqc59hf0v[21];
extern const uint8_t gm_svutc5hf6cgiv[2];
extern const uint8_t gm_svv3t2si5df2q[15];
extern const uint8_t gm_svvf5qhjb1oan[11];
extern const uint32_t gm_tpl005eoei82778_i[];
extern const gm_tmpl gm_tpl00cra9g0k45s;
extern const uint32_t gm_tpl00cra9g0k45s_i[];
extern const gm_tmpl_piece gm_tpl00cra9g0k45s_p[];
extern const gm_spec *const gm_tpl00cra9g0k45s_s[];
extern const gm_tmpl gm_tpl02lckpq8ae01;
extern const uint32_t gm_tpl02lckpq8ae01_i[];
extern const gm_tmpl_piece gm_tpl02lckpq8ae01_p[];
extern const gm_spec *const gm_tpl02lckpq8ae01_s[];
extern const gm_tmpl_piece gm_tpl06vq09a1bq44_p[];
extern const gm_spec *const gm_tpl086m1ajg247c_s[];
extern const gm_tmpl gm_tpl0aluqdlmv333;
extern const uint32_t gm_tpl0aluqdlmv333_i[];
extern const gm_tmpl_piece gm_tpl0aluqdlmv333_p[];
extern const gm_spec *const gm_tpl0aluqdlmv333_s[];
extern const gm_spec *const gm_tpl0c7sat2t7j6v_s[];
extern const gm_tmpl gm_tpl0emlhhlpmv0s;
extern const uint32_t gm_tpl0emlhhlpmv0s_i[];
extern const gm_tmpl_piece gm_tpl0emlhhlpmv0s_p[];
extern const gm_spec *const gm_tpl0emlhhlpmv0s_s[];
extern const gm_tmpl gm_tpl0eumfheo3vtt;
extern const uint32_t gm_tpl0eumfheo3vtt_i[];
extern const gm_tmpl_piece gm_tpl0eumfheo3vtt_p[];
extern const gm_spec *const gm_tpl0eumfheo3vtt_s[];
extern const gm_tmpl_piece gm_tpl0f11vet24cfp_p[];
extern const gm_spec *const gm_tpl0fpifsdum8e8_s[];
extern const uint32_t gm_tpl0ke3r8fiia4r_i[];
extern const gm_spec *const gm_tpl0pva6pvhuq6i_s[];
extern const gm_tmpl_piece gm_tpl0rhldcdavmcg_p[];
extern const gm_tmpl_piece gm_tpl0tljhf3n7k2p_p[];
extern const gm_spec *const gm_tpl0tr163o4bba1_s[];
extern const gm_tmpl gm_tpl0v0dinrpdv6d;
extern const uint32_t gm_tpl0v0dinrpdv6d_i[];
extern const gm_tmpl_piece gm_tpl0v0dinrpdv6d_p[];
extern const gm_spec *const gm_tpl0v0dinrpdv6d_s[];
extern const gm_tmpl_piece gm_tpl14b6qietv6bm_p[];
extern const uint32_t gm_tpl16petnrgdcd0_i[];
extern const uint32_t gm_tpl1a3usdkusvtq_i[];
extern const gm_tmpl gm_tpl1bh0s5ll1m9j;
extern const uint32_t gm_tpl1bh0s5ll1m9j_i[];
extern const gm_tmpl_piece gm_tpl1bh0s5ll1m9j_p[];
extern const gm_spec *const gm_tpl1bh0s5ll1m9j_s[];
extern const gm_spec *const gm_tpl1c0b1s109fa4_s[];
extern const uint32_t gm_tpl1c419vqpem2n_i[];
extern const uint32_t gm_tpl1dirc8ldrsao_i[];
extern const gm_spec *const gm_tpl1dirc8ldrsao_s[];
extern const gm_tmpl gm_tpl1irt5kietk82;
extern const uint32_t gm_tpl1irt5kietk82_i[];
extern const gm_tmpl_piece gm_tpl1irt5kietk82_p[];
extern const gm_spec *const gm_tpl1irt5kietk82_s[];
extern const gm_spec *const gm_tpl1lvejpvpq0qh_s[];
extern const uint32_t gm_tpl1oodsdaf08oj_i[];
extern const gm_spec *const gm_tpl1q6o13b940dg_s[];
extern const gm_tmpl_piece gm_tpl1r51q3hoe535_p[];
extern const gm_tmpl_piece gm_tpl1uf3f46uq3oj_p[];
extern const gm_spec *const gm_tpl1uf3f46uq3oj_s[];
extern const gm_spec *const gm_tpl1vbgcviqausd_s[];
extern const uint32_t gm_tpl233lm3m83499_i[];
extern const gm_tmpl_piece gm_tpl2a7pmi7aq67m_p[];
extern const gm_spec *const gm_tpl2a8tmpvk11vt_s[];
extern const gm_tmpl_piece gm_tpl2dia2gbnfh9l_p[];
extern const gm_tmpl_piece gm_tpl2fb8qlpeqlet_p[];
extern const gm_tmpl_piece gm_tpl2gebin91ghlu_p[];
extern const gm_tmpl gm_tpl2jsq89buriv9;
extern const uint32_t gm_tpl2jsq89buriv9_i[];
extern const gm_tmpl_piece gm_tpl2jsq89buriv9_p[];
extern const gm_spec *const gm_tpl2jsq89buriv9_s[];
extern const uint32_t gm_tpl2qal2i0dmdum_i[];
extern const gm_tmpl gm_tpl313mu5t4735i;
extern const uint32_t gm_tpl313mu5t4735i_i[];
extern const gm_tmpl_piece gm_tpl313mu5t4735i_p[];
extern const gm_spec *const gm_tpl313mu5t4735i_s[];
extern const uint32_t gm_tpl31lgghh3mal3_i[];
extern const uint32_t gm_tpl33em4maossan_i[];
extern const gm_spec *const gm_tpl33jf6q62dk07_s[];
extern const gm_tmpl_piece gm_tpl352s0p85j412_p[];
extern const gm_spec *const gm_tpl372mhqlcchb3_s[];
extern const uint32_t gm_tpl386j11hdkrib_i[];
extern const gm_tmpl_piece gm_tpl3gep2a57gf23_p[];
extern const gm_spec *const gm_tpl3lqpj3d2egtb_s[];
extern const uint32_t gm_tpl3m6tjsa4flq8_i[];
extern const uint32_t gm_tpl3ssq6hkk3qrj_i[];
extern const gm_spec *const gm_tpl3tcah2c39gjk_s[];
extern const gm_tmpl gm_tpl40m4ah8dc3ae;
extern const uint32_t gm_tpl40m4ah8dc3ae_i[];
extern const gm_tmpl_piece gm_tpl40m4ah8dc3ae_p[];
extern const gm_spec *const gm_tpl40m4ah8dc3ae_s[];
extern const gm_tmpl gm_tpl43224j8old0s;
extern const uint32_t gm_tpl43224j8old0s_i[];
extern const gm_tmpl_piece gm_tpl43224j8old0s_p[];
extern const gm_spec *const gm_tpl43224j8old0s_s[];
extern const uint32_t gm_tpl4476c5bksft1_i[];
extern const uint32_t gm_tpl461nvut9qmgp_i[];
extern const gm_tmpl_piece gm_tpl4aiadg123uql_p[];
extern const gm_tmpl_piece gm_tpl4bj6lgqq92ev_p[];
extern const gm_tmpl gm_tpl4cbbis3gd1k3;
extern const uint32_t gm_tpl4cbbis3gd1k3_i[];
extern const gm_tmpl_piece gm_tpl4cbbis3gd1k3_p[];
extern const gm_spec *const gm_tpl4cbbis3gd1k3_s[];
extern const gm_tmpl gm_tpl4clb07vpsm45;
extern const uint32_t gm_tpl4clb07vpsm45_i[];
extern const gm_tmpl_piece gm_tpl4clb07vpsm45_p[];
extern const gm_spec *const gm_tpl4clb07vpsm45_s[];
extern const gm_spec *const gm_tpl4eg9pptom3ij_s[];
extern const gm_tmpl_piece gm_tpl4lt7abhi3ej4_p[];
extern const uint32_t gm_tpl4qnmv5o1ceod_i[];
extern const uint32_t gm_tpl4t11fkqh738f_i[];
extern const gm_tmpl gm_tpl4u6a7su34sch;
extern const uint32_t gm_tpl4u6a7su34sch_i[];
extern const gm_tmpl_piece gm_tpl4u6a7su34sch_p[];
extern const gm_spec *const gm_tpl4u6a7su34sch_s[];
extern const gm_tmpl_piece gm_tpl51u3ki3ilqou_p[];
extern const uint32_t gm_tpl59s4m8f706c9_i[];
extern const gm_tmpl_piece gm_tpl5b7905cihaqk_p[];
extern const uint32_t gm_tpl5i8vi7ph4hfi_i[];
extern const gm_tmpl gm_tpl5jb9rn2u95g6;
extern const uint32_t gm_tpl5jb9rn2u95g6_i[];
extern const gm_tmpl_piece gm_tpl5jb9rn2u95g6_p[];
extern const gm_spec *const gm_tpl5jb9rn2u95g6_s[];
extern const gm_tmpl gm_tpl5o8dcpa0ctot;
extern const uint32_t gm_tpl5o8dcpa0ctot_i[];
extern const gm_tmpl_piece gm_tpl5o8dcpa0ctot_p[];
extern const gm_spec *const gm_tpl5o8dcpa0ctot_s[];
extern const uint32_t gm_tpl5poqk71bg1bn_i[];
extern const uint32_t gm_tpl5q4rr3nd9637_i[];
extern const gm_tmpl gm_tpl5q9oi9aqgs43;
extern const uint32_t gm_tpl5q9oi9aqgs43_i[];
extern const gm_tmpl_piece gm_tpl5q9oi9aqgs43_p[];
extern const gm_spec *const gm_tpl5q9oi9aqgs43_s[];
extern const gm_tmpl gm_tpl5qm9nkkj1gjg;
extern const uint32_t gm_tpl5qm9nkkj1gjg_i[];
extern const gm_tmpl_piece gm_tpl5qm9nkkj1gjg_p[];
extern const gm_spec *const gm_tpl5qm9nkkj1gjg_s[];
extern const uint32_t gm_tpl5uqj3jbiq5f0_i[];
extern const gm_tmpl gm_tpl60i3afd6fgrq;
extern const uint32_t gm_tpl60i3afd6fgrq_i[];
extern const gm_tmpl_piece gm_tpl60i3afd6fgrq_p[];
extern const gm_spec *const gm_tpl60i3afd6fgrq_s[];
extern const gm_tmpl_piece gm_tpl65tpfh2uj3bg_p[];
extern const gm_spec *const gm_tpl65tpfh2uj3bg_s[];
extern const gm_tmpl_piece gm_tpl6a59mrfvoo09_p[];
extern const gm_spec *const gm_tpl6boglt2rkmh4_s[];
extern const gm_tmpl gm_tpl6d7dqphpmhs7;
extern const uint32_t gm_tpl6d7dqphpmhs7_i[];
extern const gm_tmpl_piece gm_tpl6d7dqphpmhs7_p[];
extern const gm_spec *const gm_tpl6d7dqphpmhs7_s[];
extern const gm_spec *const gm_tpl6hqg4qptvadc_s[];
extern const gm_tmpl gm_tpl6ilgcn878l74;
extern const uint32_t gm_tpl6ilgcn878l74_i[];
extern const gm_tmpl_piece gm_tpl6ilgcn878l74_p[];
extern const gm_spec *const gm_tpl6ilgcn878l74_s[];
extern const gm_tmpl_piece gm_tpl6mra39d4fo2s_p[];
extern const gm_spec *const gm_tpl6muahgpe70pr_s[];
extern const gm_tmpl_piece gm_tpl6tu8f1h1mg85_p[];
extern const uint32_t gm_tpl6vbod7vja73l_i[];
extern const gm_spec *const gm_tpl74mr52kbqst5_s[];
extern const uint32_t gm_tpl75h0vbo3vani_i[];
extern const uint32_t gm_tpl7dh44jmr11le_i[];
extern const uint32_t gm_tpl7e5tr64a0u6e_i[];
extern const gm_tmpl gm_tpl7ilj5n9objtq;
extern const uint32_t gm_tpl7ilj5n9objtq_i[];
extern const gm_tmpl_piece gm_tpl7ilj5n9objtq_p[];
extern const gm_spec *const gm_tpl7ilj5n9objtq_s[];
extern const gm_tmpl_piece gm_tpl7o4acs2tt6pp_p[];
extern const gm_tmpl gm_tpl7sqjangpsngf;
extern const uint32_t gm_tpl7sqjangpsngf_i[];
extern const gm_tmpl_piece gm_tpl7sqjangpsngf_p[];
extern const gm_spec *const gm_tpl7sqjangpsngf_s[];
extern const gm_spec *const gm_tpl7ufg84gs1lc7_s[];
extern const gm_spec *const gm_tpl7vtkjsp2pa8b_s[];
extern const gm_tmpl_piece gm_tpl81nunf5qvo6u_p[];
extern const gm_tmpl_piece gm_tpl83abvjjf3qd1_p[];
extern const gm_tmpl_piece gm_tpl86i0s811rrlp_p[];
extern const uint32_t gm_tpl8eifhruu514e_i[];
extern const gm_tmpl gm_tpl8nqn0hncnfd4;
extern const uint32_t gm_tpl8nqn0hncnfd4_i[];
extern const gm_tmpl_piece gm_tpl8nqn0hncnfd4_p[];
extern const gm_spec *const gm_tpl8nqn0hncnfd4_s[];
extern const gm_tmpl gm_tpl8s825rf2tq3f;
extern const uint32_t gm_tpl8s825rf2tq3f_i[];
extern const gm_tmpl_piece gm_tpl8s825rf2tq3f_p[];
extern const gm_spec *const gm_tpl8s825rf2tq3f_s[];
extern const gm_tmpl_piece gm_tpl8ssnfo686ogd_p[];
extern const gm_tmpl_piece gm_tpl8un0qv11eeom_p[];
extern const gm_spec *const gm_tpl8urde81v7030_s[];
extern const uint32_t gm_tpl953juq2iaklp_i[];
extern const gm_spec *const gm_tpl9900stiqclaj_s[];
extern const gm_spec *const gm_tpl9bim66fl3b37_s[];
extern const gm_spec *const gm_tpl9d1siq0c9pir_s[];
extern const gm_spec *const gm_tpl9e18q8to9md2_s[];
extern const gm_spec *const gm_tpl9fn8oj7nnnsq_s[];
extern const gm_spec *const gm_tpl9fvss98clvrp_s[];
extern const uint32_t gm_tpl9gmvk2gm4l5h_i[];
extern const gm_tmpl_piece gm_tpl9jrfnomsrfk1_p[];
extern const uint32_t gm_tpl9l9rgf8juo7o_i[];
extern const gm_tmpl gm_tpl9q5j6bisf2fa;
extern const uint32_t gm_tpl9q5j6bisf2fa_i[];
extern const gm_tmpl_piece gm_tpl9q5j6bisf2fa_p[];
extern const gm_spec *const gm_tpl9q5j6bisf2fa_s[];
extern const gm_tmpl_piece gm_tpl9sifsarv7t06_p[];
extern const gm_tmpl gm_tpl9ufa4j1b7kal;
extern const uint32_t gm_tpl9ufa4j1b7kal_i[];
extern const gm_tmpl_piece gm_tpl9ufa4j1b7kal_p[];
extern const gm_spec *const gm_tpl9ufa4j1b7kal_s[];
extern const uint32_t gm_tpl9uslg4i3oo2s_i[];
extern const uint32_t gm_tpl9v7lb7n4a602_i[];
extern const gm_tmpl gm_tpl9va1ubk8bean;
extern const uint32_t gm_tpl9va1ubk8bean_i[];
extern const gm_tmpl_piece gm_tpl9va1ubk8bean_p[];
extern const gm_spec *const gm_tpl9va1ubk8bean_s[];
extern const gm_spec *const gm_tpl9vu8tqcv1d9s_s[];
extern const gm_tmpl gm_tpla0a5a633lqve;
extern const uint32_t gm_tpla0a5a633lqve_i[];
extern const gm_tmpl_piece gm_tpla0a5a633lqve_p[];
extern const gm_spec *const gm_tpla0a5a633lqve_s[];
extern const gm_tmpl_piece gm_tpla0dc0a9b9i8b_p[];
extern const gm_tmpl_piece gm_tpla3k9rgcg5o3q_p[];
extern const gm_spec *const gm_tpla67mmq1id2h3_s[];
extern const gm_tmpl gm_tpla6sajpp07b9l;
extern const uint32_t gm_tpla6sajpp07b9l_i[];
extern const gm_tmpl_piece gm_tpla6sajpp07b9l_p[];
extern const gm_spec *const gm_tpla6sajpp07b9l_s[];
extern const uint32_t gm_tpla93ou7df923g_i[];
extern const gm_tmpl_piece gm_tplacb81m1n6g9e_p[];
extern const gm_tmpl_piece gm_tplad75mid9f0qe_p[];
extern const uint32_t gm_tplae28pp9b49u1_i[];
extern const gm_spec *const gm_tplae6k9mv0s1dm_s[];
extern const uint32_t gm_tplaemahh05v026_i[];
extern const uint32_t gm_tplaggdk85jgg2m_i[];
extern const gm_tmpl_piece gm_tplaggdk85jgg2m_p[];
extern const gm_tmpl_piece gm_tplancpnb63qiq5_p[];
extern const gm_spec *const gm_tplancpnb63qiq5_s[];
extern const gm_spec *const gm_tplaos6tmpdtrie_s[];
extern const gm_tmpl gm_tplastf5j9ge45b;
extern const uint32_t gm_tplastf5j9ge45b_i[];
extern const gm_tmpl_piece gm_tplastf5j9ge45b_p[];
extern const gm_spec *const gm_tplastf5j9ge45b_s[];
extern const gm_tmpl gm_tplb043ibme104j;
extern const uint32_t gm_tplb043ibme104j_i[];
extern const gm_tmpl_piece gm_tplb043ibme104j_p[];
extern const gm_spec *const gm_tplb043ibme104j_s[];
extern const gm_tmpl gm_tplb2s3lfi50rhi;
extern const uint32_t gm_tplb2s3lfi50rhi_i[];
extern const gm_tmpl_piece gm_tplb2s3lfi50rhi_p[];
extern const gm_spec *const gm_tplb2s3lfi50rhi_s[];
extern const gm_tmpl gm_tplbathm90qd36n;
extern const uint32_t gm_tplbathm90qd36n_i[];
extern const gm_tmpl_piece gm_tplbathm90qd36n_p[];
extern const gm_spec *const gm_tplbathm90qd36n_s[];
extern const gm_tmpl gm_tplbj4npuroda3f;
extern const uint32_t gm_tplbj4npuroda3f_i[];
extern const gm_tmpl_piece gm_tplbj4npuroda3f_p[];
extern const gm_spec *const gm_tplbj4npuroda3f_s[];
extern const uint32_t gm_tplbm5ip0ejsndh_i[];
extern const gm_spec *const gm_tplbm5ip0ejsndh_s[];
extern const uint32_t gm_tplbmf4neusqbo5_i[];
extern const uint32_t gm_tplbppmjhpd8ol0_i[];
extern const gm_tmpl gm_tplbr5d1kr4l84u;
extern const uint32_t gm_tplbr5d1kr4l84u_i[];
extern const gm_tmpl_piece gm_tplbr5d1kr4l84u_p[];
extern const gm_spec *const gm_tplbr5d1kr4l84u_s[];
extern const gm_tmpl gm_tplbtqgdimqhkr1;
extern const uint32_t gm_tplbtqgdimqhkr1_i[];
extern const gm_tmpl_piece gm_tplbtqgdimqhkr1_p[];
extern const gm_spec *const gm_tplbtqgdimqhkr1_s[];
extern const gm_spec *const gm_tplbulo51k20om4_s[];
extern const gm_tmpl_piece gm_tplc2ir650ohv3d_p[];
extern const gm_spec *const gm_tplc809u7fr7kg6_s[];
extern const gm_tmpl_piece gm_tplc90dj04km0n5_p[];
extern const gm_tmpl_piece gm_tplcddb08maq348_p[];
extern const gm_spec *const gm_tplcef5l6cvp5lt_s[];
extern const gm_spec *const gm_tplceqrbdtcd9p5_s[];
extern const gm_tmpl gm_tplcic56vj3l629;
extern const uint32_t gm_tplcic56vj3l629_i[];
extern const gm_tmpl_piece gm_tplcic56vj3l629_p[];
extern const gm_spec *const gm_tplcic56vj3l629_s[];
extern const uint32_t gm_tplcilp3mvhfo4n_i[];
extern const uint32_t gm_tplcne9d7171tqp_i[];
extern const gm_spec *const gm_tplcoet0p57pg4s_s[];
extern const gm_spec *const gm_tplcouqld11oeil_s[];
extern const uint32_t gm_tplcpa735d0vjnu_i[];
extern const gm_tmpl gm_tplcq107msa8ktd;
extern const uint32_t gm_tplcq107msa8ktd_i[];
extern const gm_tmpl_piece gm_tplcq107msa8ktd_p[];
extern const gm_spec *const gm_tplcq107msa8ktd_s[];
extern const gm_spec *const gm_tplcre592qvlrqu_s[];
extern const gm_tmpl gm_tplcseelhcfsg75;
extern const uint32_t gm_tplcseelhcfsg75_i[];
extern const gm_tmpl_piece gm_tplcseelhcfsg75_p[];
extern const gm_spec *const gm_tplcseelhcfsg75_s[];
extern const gm_tmpl_piece gm_tplcvnbkkkji94u_p[];
extern const gm_tmpl gm_tpld0t14m1fld0p;
extern const uint32_t gm_tpld0t14m1fld0p_i[];
extern const gm_tmpl_piece gm_tpld0t14m1fld0p_p[];
extern const gm_spec *const gm_tpld0t14m1fld0p_s[];
extern const gm_tmpl gm_tpld1hhk0kef14l;
extern const uint32_t gm_tpld1hhk0kef14l_i[];
extern const gm_tmpl_piece gm_tpld1hhk0kef14l_p[];
extern const gm_spec *const gm_tpld1hhk0kef14l_s[];
extern const uint32_t gm_tpld47p9te8ar36_i[];
extern const gm_tmpl gm_tpld7d7u7to2a7u;
extern const uint32_t gm_tpld7d7u7to2a7u_i[];
extern const gm_tmpl_piece gm_tpld7d7u7to2a7u_p[];
extern const gm_spec *const gm_tpld7d7u7to2a7u_s[];
extern const gm_spec *const gm_tplde4760b0jcjn_s[];
extern const gm_tmpl gm_tpldff5gvnhne6f;
extern const uint32_t gm_tpldff5gvnhne6f_i[];
extern const gm_tmpl_piece gm_tpldff5gvnhne6f_p[];
extern const gm_spec *const gm_tpldff5gvnhne6f_s[];
extern const gm_tmpl_piece gm_tpldfm52le63mtn_p[];
extern const gm_tmpl gm_tpldhof7u5v5b2c;
extern const uint32_t gm_tpldhof7u5v5b2c_i[];
extern const gm_tmpl_piece gm_tpldhof7u5v5b2c_p[];
extern const gm_spec *const gm_tpldhof7u5v5b2c_s[];
extern const gm_tmpl_piece gm_tpldi9jj8a80irn_p[];
extern const gm_tmpl_piece gm_tpldj14c3jgvlnb_p[];
extern const gm_tmpl_piece gm_tpldjbe17jhbkpg_p[];
extern const uint32_t gm_tpldlrsentitfg3_i[];
extern const uint32_t gm_tpldrcjvgmk3ig3_i[];
extern const gm_spec *const gm_tpldrlq7uo0cbrm_s[];
extern const uint32_t gm_tpldtaonf5laaam_i[];
extern const gm_spec *const gm_tpldtecn6n5hb86_s[];
extern const gm_tmpl gm_tpldueenbh2sgd4;
extern const uint32_t gm_tpldueenbh2sgd4_i[];
extern const gm_tmpl_piece gm_tpldueenbh2sgd4_p[];
extern const gm_spec *const gm_tpldueenbh2sgd4_s[];
extern const uint32_t gm_tplduhq10oq213m_i[];
extern const gm_spec *const gm_tplduhq10oq213m_s[];
extern const uint32_t gm_tpldunbbu9dpisn_i[];
extern const uint32_t gm_tplduutdqd8m5r5_i[];
extern const uint32_t gm_tple377dkn6pj7u_i[];
extern const gm_tmpl_piece gm_tple6jpca094i2j_p[];
extern const uint32_t gm_tple75bobdc6q0g_i[];
extern const gm_spec *const gm_tple7msnncsq0hr_s[];
extern const gm_tmpl gm_tple7ukme0qchh6;
extern const uint32_t gm_tple7ukme0qchh6_i[];
extern const gm_tmpl_piece gm_tple7ukme0qchh6_p[];
extern const gm_spec *const gm_tple7ukme0qchh6_s[];
extern const gm_spec *const gm_tplei9v853i04sp_s[];
extern const gm_tmpl gm_tplej21mtlj140h;
extern const uint32_t gm_tplej21mtlj140h_i[];
extern const gm_tmpl_piece gm_tplej21mtlj140h_p[];
extern const gm_spec *const gm_tplej21mtlj140h_s[];
extern const gm_tmpl_piece gm_tplekdseqnnupsj_p[];
extern const gm_spec *const gm_tplekdseqnnupsj_s[];
extern const gm_tmpl gm_tplellk9btkq1f2;
extern const uint32_t gm_tplellk9btkq1f2_i[];
extern const gm_tmpl_piece gm_tplellk9btkq1f2_p[];
extern const gm_spec *const gm_tplellk9btkq1f2_s[];
extern const gm_spec *const gm_tplemb9urr8jmq6_s[];
extern const gm_tmpl gm_tpleoabj1u2rb54;
extern const uint32_t gm_tpleoabj1u2rb54_i[];
extern const gm_tmpl_piece gm_tpleoabj1u2rb54_p[];
extern const gm_spec *const gm_tpleoabj1u2rb54_s[];
extern const gm_tmpl_piece gm_tplep2l66hmuvrc_p[];
extern const gm_spec *const gm_tples1m1m9iv6b0_s[];
extern const gm_spec *const gm_tplevafblg3gmq9_s[];
extern const gm_tmpl gm_tplf28d52hmmd2n;
extern const uint32_t gm_tplf28d52hmmd2n_i[];
extern const gm_tmpl_piece gm_tplf28d52hmmd2n_p[];
extern const gm_spec *const gm_tplf28d52hmmd2n_s[];
extern const gm_spec *const gm_tplf41j0oji3nkl_s[];
extern const gm_tmpl gm_tplf48rihumtlpr;
extern const uint32_t gm_tplf48rihumtlpr_i[];
extern const gm_tmpl_piece gm_tplf48rihumtlpr_p[];
extern const gm_spec *const gm_tplf48rihumtlpr_s[];
extern const gm_tmpl gm_tplf8er2klne616;
extern const uint32_t gm_tplf8er2klne616_i[];
extern const gm_tmpl_piece gm_tplf8er2klne616_p[];
extern const gm_spec *const gm_tplf8er2klne616_s[];
extern const gm_tmpl gm_tplf9434bnahqi1;
extern const uint32_t gm_tplf9434bnahqi1_i[];
extern const gm_tmpl_piece gm_tplf9434bnahqi1_p[];
extern const gm_spec *const gm_tplf9434bnahqi1_s[];
extern const gm_tmpl gm_tplfbtlvu2isf2h;
extern const uint32_t gm_tplfbtlvu2isf2h_i[];
extern const gm_tmpl_piece gm_tplfbtlvu2isf2h_p[];
extern const gm_spec *const gm_tplfbtlvu2isf2h_s[];
extern const gm_spec *const gm_tplfe6giveqr9e1_s[];
extern const gm_tmpl gm_tplff72i6ku4gld;
extern const uint32_t gm_tplff72i6ku4gld_i[];
extern const gm_tmpl_piece gm_tplff72i6ku4gld_p[];
extern const gm_spec *const gm_tplff72i6ku4gld_s[];
extern const gm_spec *const gm_tplfft5kohcphn8_s[];
extern const gm_tmpl_piece gm_tplfkugp8hqqhvu_p[];
extern const uint32_t gm_tplfpi7koctqa7b_i[];
extern const gm_tmpl_piece gm_tplfrm74l83ht8l_p[];
extern const gm_spec *const gm_tplfsk62pc25cdb_s[];
extern const gm_tmpl gm_tplftchh41b473r;
extern const uint32_t gm_tplftchh41b473r_i[];
extern const gm_tmpl_piece gm_tplftchh41b473r_p[];
extern const gm_spec *const gm_tplftchh41b473r_s[];
extern const gm_tmpl gm_tplfvv1qe6os0le;
extern const uint32_t gm_tplfvv1qe6os0le_i[];
extern const gm_tmpl_piece gm_tplfvv1qe6os0le_p[];
extern const gm_spec *const gm_tplfvv1qe6os0le_s[];
extern const gm_spec *const gm_tplg21ilonldhva_s[];
extern const gm_tmpl_piece gm_tplg21rd4as3ph6_p[];
extern const gm_tmpl gm_tplg2qlrjk1sunc;
extern const uint32_t gm_tplg2qlrjk1sunc_i[];
extern const gm_tmpl_piece gm_tplg2qlrjk1sunc_p[];
extern const gm_spec *const gm_tplg2qlrjk1sunc_s[];
extern const gm_spec *const gm_tplg4k0uitmfhf0_s[];
extern const gm_tmpl gm_tplgavllsav46n0;
extern const uint32_t gm_tplgavllsav46n0_i[];
extern const gm_tmpl_piece gm_tplgavllsav46n0_p[];
extern const gm_spec *const gm_tplgavllsav46n0_s[];
extern const gm_tmpl_piece gm_tplgeqcgpvi6n7p_p[];
extern const uint32_t gm_tplgih8dfq5hun7_i[];
extern const gm_spec *const gm_tplgja2nber62dc_s[];
extern const gm_spec *const gm_tplgnobkmf75b9i_s[];
extern const uint32_t gm_tplgqohfqetaph7_i[];
extern const gm_spec *const gm_tplgt6vkfjdh8cd_s[];
extern const uint32_t gm_tplgv9va17kt7ll_i[];
extern const gm_spec *const gm_tplgvbr3vejj74c_s[];
extern const uint32_t gm_tplh2sse9mocm92_i[];
extern const gm_spec *const gm_tplhatoh83mi4is_s[];
extern const gm_tmpl_piece gm_tplhi0fd9nctd4b_p[];
extern const gm_spec *const gm_tplhksbien67lqe_s[];
extern const gm_spec *const gm_tplho1kg74nlm64_s[];
extern const uint32_t gm_tplht122d0ji4km_i[];
extern const uint32_t gm_tpli4rfoi75onhj_i[];
extern const uint32_t gm_tpli9foo8ib8qrk_i[];
extern const gm_tmpl_piece gm_tpli9foo8ib8qrk_p[];
extern const gm_spec *const gm_tpli9uflldtkfq3_s[];
extern const gm_tmpl gm_tplibgopbmor3p6;
extern const uint32_t gm_tplibgopbmor3p6_i[];
extern const gm_tmpl_piece gm_tplibgopbmor3p6_p[];
extern const gm_spec *const gm_tplibgopbmor3p6_s[];
extern const gm_tmpl gm_tpligbj3kvvunpr;
extern const uint32_t gm_tpligbj3kvvunpr_i[];
extern const gm_tmpl_piece gm_tpligbj3kvvunpr_p[];
extern const gm_spec *const gm_tpligbj3kvvunpr_s[];
extern const uint32_t gm_tpliig5tr0gjn5a_i[];
extern const gm_tmpl_piece gm_tplim7pg0q4u4tg_p[];
extern const gm_spec *const gm_tplin004j5gtkru_s[];
extern const gm_tmpl_piece gm_tpliqi7pna59a9o_p[];
extern const gm_tmpl_piece gm_tplit8jkrg5bt3p_p[];
extern const uint32_t gm_tplj0t0s73b2v4j_i[];
extern const gm_tmpl gm_tplj0vaqlfn7orc;
extern const uint32_t gm_tplj0vaqlfn7orc_i[];
extern const gm_tmpl_piece gm_tplj0vaqlfn7orc_p[];
extern const gm_spec *const gm_tplj0vaqlfn7orc_s[];
extern const uint32_t gm_tplj16n1o03849r_i[];
extern const gm_tmpl_piece gm_tplj2qf541fmi6f_p[];
extern const gm_tmpl gm_tplj3vfuklvht3e;
extern const uint32_t gm_tplj3vfuklvht3e_i[];
extern const gm_tmpl_piece gm_tplj3vfuklvht3e_p[];
extern const gm_spec *const gm_tplj3vfuklvht3e_s[];
extern const uint32_t gm_tplj57b11lvidge_i[];
extern const gm_tmpl gm_tplj6bgc315nev3;
extern const uint32_t gm_tplj6bgc315nev3_i[];
extern const gm_tmpl_piece gm_tplj6bgc315nev3_p[];
extern const gm_spec *const gm_tplj6bgc315nev3_s[];
extern const uint32_t gm_tplj6ss1j7sgouf_i[];
extern const gm_tmpl gm_tplj7j0vuugbh2u;
extern const uint32_t gm_tplj7j0vuugbh2u_i[];
extern const gm_tmpl_piece gm_tplj7j0vuugbh2u_p[];
extern const gm_spec *const gm_tplj7j0vuugbh2u_s[];
extern const gm_tmpl_piece gm_tpljc7kqi31f4d2_p[];
extern const gm_spec *const gm_tplk13976gpibvh_s[];
extern const gm_tmpl gm_tplk1dctbs79mmk;
extern const uint32_t gm_tplk1dctbs79mmk_i[];
extern const gm_tmpl_piece gm_tplk1dctbs79mmk_p[];
extern const gm_spec *const gm_tplk1dctbs79mmk_s[];
extern const gm_tmpl_piece gm_tplk4i636uhvi9m_p[];
extern const uint32_t gm_tplkfag0fic60vv_i[];
extern const gm_tmpl gm_tplkh4k0ikfsmp6;
extern const uint32_t gm_tplkh4k0ikfsmp6_i[];
extern const gm_tmpl_piece gm_tplkh4k0ikfsmp6_p[];
extern const gm_spec *const gm_tplkh4k0ikfsmp6_s[];
extern const uint32_t gm_tplkhlcq331nni9_i[];
extern const gm_tmpl_piece gm_tplkhlcq331nni9_p[];
extern const uint32_t gm_tplkhnuvgfhr9v5_i[];
extern const gm_tmpl gm_tplkjc1g73er3ub;
extern const uint32_t gm_tplkjc1g73er3ub_i[];
extern const gm_tmpl_piece gm_tplkjc1g73er3ub_p[];
extern const gm_spec *const gm_tplkjc1g73er3ub_s[];
extern const gm_tmpl gm_tplkji0sd9phcb9;
extern const uint32_t gm_tplkji0sd9phcb9_i[];
extern const gm_tmpl_piece gm_tplkji0sd9phcb9_p[];
extern const gm_spec *const gm_tplkji0sd9phcb9_s[];
extern const gm_spec *const gm_tplkk98947i5b3i_s[];
extern const gm_spec *const gm_tplkmrsfbsle095_s[];
extern const gm_tmpl gm_tplknfbq49i74sp;
extern const uint32_t gm_tplknfbq49i74sp_i[];
extern const gm_tmpl_piece gm_tplknfbq49i74sp_p[];
extern const gm_spec *const gm_tplknfbq49i74sp_s[];
extern const gm_tmpl gm_tplkokmqjs81d4g;
extern const uint32_t gm_tplkokmqjs81d4g_i[];
extern const gm_tmpl_piece gm_tplkokmqjs81d4g_p[];
extern const gm_spec *const gm_tplkokmqjs81d4g_s[];
extern const gm_tmpl gm_tplkr4062p8a8bi;
extern const uint32_t gm_tplkr4062p8a8bi_i[];
extern const gm_tmpl_piece gm_tplkr4062p8a8bi_p[];
extern const gm_spec *const gm_tplkr4062p8a8bi_s[];
extern const gm_spec *const gm_tplktj7cco9lq40_s[];
extern const uint32_t gm_tpll060ppa56f8b_i[];
extern const gm_tmpl gm_tpll487d6t52m4g;
extern const uint32_t gm_tpll487d6t52m4g_i[];
extern const gm_tmpl_piece gm_tpll487d6t52m4g_p[];
extern const gm_spec *const gm_tpll487d6t52m4g_s[];
extern const gm_tmpl gm_tpll4f2ihchoe9c;
extern const uint32_t gm_tpll4f2ihchoe9c_i[];
extern const gm_tmpl_piece gm_tpll4f2ihchoe9c_p[];
extern const gm_spec *const gm_tpll4f2ihchoe9c_s[];
extern const gm_tmpl_piece gm_tpll6udkhd3c609_p[];
extern const gm_spec *const gm_tpll9g0ell7ocpk_s[];
extern const gm_spec *const gm_tpllak8uc09ui7v_s[];
extern const gm_tmpl_piece gm_tpllcvau9l0fqcd_p[];
extern const gm_tmpl_piece gm_tplld0dt78h0tum_p[];
extern const gm_spec *const gm_tplle7qvdq2jai6_s[];
extern const gm_tmpl gm_tpllikrj1hgkvih;
extern const uint32_t gm_tpllikrj1hgkvih_i[];
extern const gm_tmpl_piece gm_tpllikrj1hgkvih_p[];
extern const gm_spec *const gm_tpllikrj1hgkvih_s[];
extern const gm_tmpl_piece gm_tpllp788bsiqs1r_p[];
extern const gm_tmpl gm_tpllqboe50ngs7g;
extern const uint32_t gm_tpllqboe50ngs7g_i[];
extern const gm_tmpl_piece gm_tpllqboe50ngs7g_p[];
extern const gm_spec *const gm_tpllqboe50ngs7g_s[];
extern const gm_tmpl gm_tplls606vhvqojp;
extern const uint32_t gm_tplls606vhvqojp_i[];
extern const gm_tmpl_piece gm_tplls606vhvqojp_p[];
extern const gm_spec *const gm_tplls606vhvqojp_s[];
extern const uint32_t gm_tpllsaus03ne167_i[];
extern const gm_tmpl_piece gm_tplm1fuloaqnfv4_p[];
extern const gm_tmpl_piece gm_tplm2m182mt5ljj_p[];
extern const uint32_t gm_tplm3dvc32djhr7_i[];
extern const gm_tmpl gm_tplm9t4pej720h2;
extern const uint32_t gm_tplm9t4pej720h2_i[];
extern const gm_tmpl_piece gm_tplm9t4pej720h2_p[];
extern const gm_spec *const gm_tplm9t4pej720h2_s[];
extern const gm_spec *const gm_tplmaqqdmni75bf_s[];
extern const gm_tmpl gm_tplmemqaoor78ph;
extern const uint32_t gm_tplmemqaoor78ph_i[];
extern const gm_tmpl_piece gm_tplmemqaoor78ph_p[];
extern const gm_spec *const gm_tplmemqaoor78ph_s[];
extern const gm_spec *const gm_tplmjmservvuqtp_s[];
extern const gm_spec *const gm_tplmkgblgbo7imb_s[];
extern const gm_tmpl_piece gm_tplmm8hld32929h_p[];
extern const gm_tmpl_piece gm_tplmo1fik79j6ea_p[];
extern const gm_spec *const gm_tplmoo8v17lb0ql_s[];
extern const uint32_t gm_tplmqflu0tp0eas_i[];
extern const uint32_t gm_tplmr9p697ovoo7_i[];
extern const gm_tmpl_piece gm_tplmr9p697ovoo7_p[];
extern const gm_spec *const gm_tplmtg92v4cesud_s[];
extern const uint32_t gm_tplmurfi7rvdamc_i[];
extern const gm_spec *const gm_tplmurfi7rvdamc_s[];
extern const uint32_t gm_tpln1d9amhhlm2o_i[];
extern const uint32_t gm_tpln3j86kdpqd7a_i[];
extern const uint32_t gm_tpln4hlddpv31vi_i[];
extern const gm_tmpl gm_tpln8mihgcsij40;
extern const uint32_t gm_tpln8mihgcsij40_i[];
extern const gm_tmpl_piece gm_tpln8mihgcsij40_p[];
extern const gm_spec *const gm_tpln8mihgcsij40_s[];
extern const gm_tmpl_piece gm_tpln92q8vfbc4aq_p[];
extern const gm_spec *const gm_tplnbsaptme3355_s[];
extern const gm_tmpl gm_tplnc4en2s3t0da;
extern const uint32_t gm_tplnc4en2s3t0da_i[];
extern const gm_tmpl_piece gm_tplnc4en2s3t0da_p[];
extern const gm_spec *const gm_tplnc4en2s3t0da_s[];
extern const gm_spec *const gm_tplngsd5kcuu6fv_s[];
extern const uint32_t gm_tplnj647l00ku7g_i[];
extern const gm_tmpl_piece gm_tplnkb386hrc6va_p[];
extern const uint32_t gm_tplnodn319tb55r_i[];
extern const gm_spec *const gm_tplnr9pmjldkl35_s[];
extern const gm_tmpl gm_tplnrs6u0h2oc92;
extern const uint32_t gm_tplnrs6u0h2oc92_i[];
extern const gm_tmpl_piece gm_tplnrs6u0h2oc92_p[];
extern const gm_spec *const gm_tplnrs6u0h2oc92_s[];
extern const uint32_t gm_tplnrt3ito76b2v_i[];
extern const gm_tmpl gm_tplnunrbopfpsiq;
extern const uint32_t gm_tplnunrbopfpsiq_i[];
extern const gm_tmpl_piece gm_tplnunrbopfpsiq_p[];
extern const gm_spec *const gm_tplnunrbopfpsiq_s[];
extern const gm_tmpl gm_tplnv6l3ecgreid;
extern const uint32_t gm_tplnv6l3ecgreid_i[];
extern const gm_tmpl_piece gm_tplnv6l3ecgreid_p[];
extern const gm_spec *const gm_tplnv6l3ecgreid_s[];
extern const gm_spec *const gm_tplo08f1kg3k97n_s[];
extern const gm_tmpl gm_tplo38nq02vgfo5;
extern const uint32_t gm_tplo38nq02vgfo5_i[];
extern const gm_tmpl_piece gm_tplo38nq02vgfo5_p[];
extern const gm_spec *const gm_tplo38nq02vgfo5_s[];
extern const gm_tmpl gm_tplo481o6n7og1g;
extern const uint32_t gm_tplo481o6n7og1g_i[];
extern const gm_tmpl_piece gm_tplo481o6n7og1g_p[];
extern const gm_spec *const gm_tplo481o6n7og1g_s[];
extern const gm_tmpl_piece gm_tplo5614u5649ka_p[];
extern const gm_spec *const gm_tploa995k06npbj_s[];
extern const gm_spec *const gm_tplod6iso1fh3co_s[];
extern const gm_tmpl_piece gm_tplodl7i2apj0bs_p[];
extern const uint32_t gm_tploik9nhlf98dm_i[];
extern const uint32_t gm_tplolnkad3lslng_i[];
extern const gm_tmpl_piece gm_tplolsc5tfmth66_p[];
extern const gm_spec *const gm_tplooqfb0umhrq0_s[];
extern const gm_spec *const gm_tplopmhgmch9jsj_s[];
extern const uint32_t gm_tplp0kc3a3fekq4_i[];
extern const gm_spec *const gm_tplp3l2s17nqpoi_s[];
extern const gm_tmpl gm_tplp3rnngb19vkd;
extern const uint32_t gm_tplp3rnngb19vkd_i[];
extern const gm_tmpl_piece gm_tplp3rnngb19vkd_p[];
extern const gm_spec *const gm_tplp3rnngb19vkd_s[];
extern const gm_tmpl gm_tplp7faum694g3u;
extern const uint32_t gm_tplp7faum694g3u_i[];
extern const gm_tmpl_piece gm_tplp7faum694g3u_p[];
extern const gm_spec *const gm_tplp7faum694g3u_s[];
extern const uint32_t gm_tplp81h2rbu9gd2_i[];
extern const uint32_t gm_tplp83ba2hehir9_i[];
extern const gm_tmpl gm_tplp98fsht4k706;
extern const uint32_t gm_tplp98fsht4k706_i[];
extern const gm_tmpl_piece gm_tplp98fsht4k706_p[];
extern const gm_spec *const gm_tplp98fsht4k706_s[];
extern const gm_spec *const gm_tplpav1vs8t9nu9_s[];
extern const uint32_t gm_tplpcqqtntqphus_i[];
extern const gm_tmpl_piece gm_tplpcqqtntqphus_p[];
extern const gm_spec *const gm_tplpcqqtntqphus_s[];
extern const gm_tmpl gm_tplpe5hifub5ms7;
extern const uint32_t gm_tplpe5hifub5ms7_i[];
extern const gm_tmpl_piece gm_tplpe5hifub5ms7_p[];
extern const gm_spec *const gm_tplpe5hifub5ms7_s[];
extern const gm_tmpl_piece gm_tplpehi2hdi7j7m_p[];
extern const gm_tmpl_piece gm_tplph8v0nbmofpg_p[];
extern const gm_tmpl_piece gm_tplpjjba8pqbhe2_p[];
extern const gm_spec *const gm_tplpog54do9oh2a_s[];
extern const gm_tmpl gm_tplptgaia3s19c9;
extern const uint32_t gm_tplptgaia3s19c9_i[];
extern const gm_tmpl_piece gm_tplptgaia3s19c9_p[];
extern const gm_spec *const gm_tplptgaia3s19c9_s[];
extern const gm_tmpl_piece gm_tplpvh6srii84sj_p[];
extern const gm_tmpl_piece gm_tplq3ra7ik9ej59_p[];
extern const gm_tmpl gm_tplq7vstpm0lgda;
extern const uint32_t gm_tplq7vstpm0lgda_i[];
extern const gm_tmpl_piece gm_tplq7vstpm0lgda_p[];
extern const gm_spec *const gm_tplq7vstpm0lgda_s[];
extern const uint32_t gm_tplqc6j3ejcoft6_i[];
extern const gm_tmpl gm_tplqci9e677opav;
extern const uint32_t gm_tplqci9e677opav_i[];
extern const gm_tmpl_piece gm_tplqci9e677opav_p[];
extern const gm_spec *const gm_tplqci9e677opav_s[];
extern const gm_tmpl_piece gm_tplqfgvjo31nom3_p[];
extern const gm_tmpl_piece gm_tplqh3e2aojp71u_p[];
extern const gm_spec *const gm_tplql7vfevun5ae_s[];
extern const gm_tmpl_piece gm_tplqrbhvccemc9k_p[];
extern const gm_tmpl gm_tplqt9e8rihgogv;
extern const uint32_t gm_tplqt9e8rihgogv_i[];
extern const gm_tmpl_piece gm_tplqt9e8rihgogv_p[];
extern const gm_spec *const gm_tplqt9e8rihgogv_s[];
extern const gm_spec *const gm_tplr1if0cs3oolt_s[];
extern const uint32_t gm_tplr35nagqnpmad_i[];
extern const gm_tmpl gm_tplre7l7cu4fpbv;
extern const uint32_t gm_tplre7l7cu4fpbv_i[];
extern const gm_tmpl_piece gm_tplre7l7cu4fpbv_p[];
extern const gm_spec *const gm_tplre7l7cu4fpbv_s[];
extern const gm_tmpl_piece gm_tplrfab517u4o97_p[];
extern const gm_spec *const gm_tplrfab517u4o97_s[];
extern const gm_tmpl_piece gm_tplrfnb607u5hvd_p[];
extern const gm_tmpl_piece gm_tplrmkc8hrdtbfh_p[];
extern const gm_tmpl gm_tplrnntb2r7ns40;
extern const uint32_t gm_tplrnntb2r7ns40_i[];
extern const gm_tmpl_piece gm_tplrnntb2r7ns40_p[];
extern const gm_spec *const gm_tplrnntb2r7ns40_s[];
extern const gm_tmpl gm_tplrpvalpqjvipe;
extern const uint32_t gm_tplrpvalpqjvipe_i[];
extern const gm_tmpl_piece gm_tplrpvalpqjvipe_p[];
extern const gm_spec *const gm_tplrpvalpqjvipe_s[];
extern const gm_spec *const gm_tplrq1qj3euarkk_s[];
extern const gm_tmpl_piece gm_tplrsdjn4cifhr9_p[];
extern const gm_spec *const gm_tplrsdjn4cifhr9_s[];
extern const uint32_t gm_tplrtgv8em132rl_i[];
extern const gm_spec *const gm_tplru5t7oahrjah_s[];
extern const gm_tmpl gm_tplrveq8m992ml7;
extern const uint32_t gm_tplrveq8m992ml7_i[];
extern const gm_tmpl_piece gm_tplrveq8m992ml7_p[];
extern const gm_spec *const gm_tplrveq8m992ml7_s[];
extern const uint32_t gm_tplrvl1hqu8hbkh_i[];
extern const uint32_t gm_tpls6cg1uub8gfo_i[];
extern const uint32_t gm_tpls6dj3unub5ff_i[];
extern const gm_spec *const gm_tpls6f1h1hgtqgb_s[];
extern const gm_tmpl_piece gm_tplsmpaecrda5mc_p[];
extern const uint32_t gm_tplsns868vihk0u_i[];
extern const gm_tmpl gm_tplspgpn4qu6rd4;
extern const uint32_t gm_tplspgpn4qu6rd4_i[];
extern const gm_tmpl_piece gm_tplspgpn4qu6rd4_p[];
extern const gm_spec *const gm_tplspgpn4qu6rd4_s[];
extern const gm_tmpl gm_tplsrmv18emggok;
extern const uint32_t gm_tplsrmv18emggok_i[];
extern const gm_tmpl_piece gm_tplsrmv18emggok_p[];
extern const gm_spec *const gm_tplsrmv18emggok_s[];
extern const gm_spec *const gm_tplt1i3tc7bfdhf_s[];
extern const uint32_t gm_tplt2babpet96ij_i[];
extern const uint32_t gm_tplt3iub1goev8r_i[];
extern const gm_tmpl gm_tplt5ks894658kl;
extern const uint32_t gm_tplt5ks894658kl_i[];
extern const gm_tmpl_piece gm_tplt5ks894658kl_p[];
extern const gm_spec *const gm_tplt5ks894658kl_s[];
extern const gm_spec *const gm_tplt68r4kihke54_s[];
extern const gm_spec *const gm_tplt9d80smn1kum_s[];
extern const gm_spec *const gm_tpltd7vnsp01mvt_s[];
extern const gm_spec *const gm_tpltfirffnkp263_s[];
extern const gm_spec *const gm_tplthkeg7jngn6q_s[];
extern const uint32_t gm_tpltj7umjd53udq_i[];
extern const uint32_t gm_tpltktipp7m43tk_i[];
extern const gm_tmpl gm_tpltlq1bga4cgul;
extern const uint32_t gm_tpltlq1bga4cgul_i[];
extern const gm_tmpl_piece gm_tpltlq1bga4cgul_p[];
extern const gm_spec *const gm_tpltlq1bga4cgul_s[];
extern const uint32_t gm_tpltm9r5n97je13_i[];
extern const gm_tmpl_piece gm_tpltpd3ap2ql1ip_p[];
extern const gm_spec *const gm_tpltpd3ap2ql1ip_s[];
extern const uint32_t gm_tpltur94fmrrcmk_i[];
extern const gm_tmpl_piece gm_tplu0h8mqm004jg_p[];
extern const uint32_t gm_tplu272587ugnvi_i[];
extern const gm_tmpl gm_tplu65cpe6nhlbb;
extern const uint32_t gm_tplu65cpe6nhlbb_i[];
extern const gm_tmpl_piece gm_tplu65cpe6nhlbb_p[];
extern const gm_spec *const gm_tplu65cpe6nhlbb_s[];
extern const gm_tmpl gm_tplu76evoc63de2;
extern const uint32_t gm_tplu76evoc63de2_i[];
extern const gm_tmpl_piece gm_tplu76evoc63de2_p[];
extern const gm_spec *const gm_tplu76evoc63de2_s[];
extern const gm_spec *const gm_tplu7gq2cm8aanh_s[];
extern const uint32_t gm_tplu9669cm9mqja_i[];
extern const gm_tmpl gm_tplue2kdcikgcof;
extern const uint32_t gm_tplue2kdcikgcof_i[];
extern const gm_tmpl_piece gm_tplue2kdcikgcof_p[];
extern const gm_spec *const gm_tplue2kdcikgcof_s[];
extern const gm_spec *const gm_tpluf8termqcruk_s[];
extern const uint32_t gm_tplugrpoiqe5r59_i[];
extern const gm_tmpl gm_tpluguraohl9kn4;
extern const uint32_t gm_tpluguraohl9kn4_i[];
extern const gm_tmpl_piece gm_tpluguraohl9kn4_p[];
extern const gm_spec *const gm_tpluguraohl9kn4_s[];
extern const uint32_t gm_tplumuahjb5m8m8_i[];
extern const gm_spec *const gm_tplup0bf5d2eopf_s[];
extern const uint32_t gm_tpluuts50bm11md_i[];
extern const gm_spec *const gm_tpluuts50bm11md_s[];
extern const uint32_t gm_tpluvp47iqcj4uv_i[];
extern const gm_tmpl gm_tplv23k7silrvnp;
extern const uint32_t gm_tplv23k7silrvnp_i[];
extern const gm_tmpl_piece gm_tplv23k7silrvnp_p[];
extern const gm_spec *const gm_tplv23k7silrvnp_s[];
extern const uint32_t gm_tplv2usruqglqv9_i[];
extern const gm_tmpl_piece gm_tplv2usruqglqv9_p[];
extern const uint32_t gm_tplv6dbvs9bjnes_i[];
extern const gm_tmpl gm_tplv8ckuk7p1s8m;
extern const uint32_t gm_tplv8ckuk7p1s8m_i[];
extern const gm_tmpl_piece gm_tplv8ckuk7p1s8m_p[];
extern const gm_spec *const gm_tplv8ckuk7p1s8m_s[];
extern const gm_spec *const gm_tplv9sqjp27m76e_s[];
extern const gm_spec *const gm_tplvbotf15ps5s5_s[];
extern const gm_tmpl_piece gm_tplvc9hnrt45n6d_p[];
extern const uint32_t gm_tplvdft4iatcp17_i[];
extern const gm_tmpl_piece gm_tplvk1pmcp9tr4p_p[];
extern const gm_tmpl_piece gm_tplvqpq1chrhvno_p[];
extern const gm_tmpl gm_tplvuahs93lrft8;
extern const uint32_t gm_tplvuahs93lrft8_i[];
extern const gm_tmpl_piece gm_tplvuahs93lrft8_p[];
extern const gm_spec *const gm_tplvuahs93lrft8_s[];
extern const uint32_t gm_tplvvtt1jk959m0_i[];
extern const gm_type gm_ty0lfe1s2pin;
extern const gm_field gm_ty0lfe1s2pin_f[];
extern const gm_variant gm_tyasplg81cl8_v[];
extern const gm_type gm_tyo86ks9qkvt;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_v43n3ub(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_v43n3ub F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* p */
    GM_STACK_CHECK(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l2;
    {
        {
            gm_b16_8 t4 = gm_l0;
            gm_l1 = t4;
            void * t5 = (void *)((uint8_t *)&((struct gm_fr_for_body_ukps8ob *)((&F.h)->up))->l2);
            gm_b16_8 t6 = gm_l1;
            gm_tftb1odc739t5 t7 = gm_f_push_3ef746b(t5, t6);
            gm_l2 = t7;
            uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
            if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srcbq9k9kvdokbf, 173, 42);
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_srcbq9k9kvdokbf, 173, 9);
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
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
const char gm_src4o48ksu43djd[] = "compiler/load/target.gm";
const char gm_src5cfou479ljhn[] = "compiler/check/array_ctor.gm";
const char gm_src92rfidruvalq[] = "compiler/cgen/spec.gm";
const char gm_srcb2bqqsnukmrd[] = "compiler/check/dangling.gm";
const char gm_srcb3t5fii3khrp[] = "compiler/pkg/scaffold.gm";
const char gm_srcc24pnihhgkug[] = "<core>/slice.gm";
const char gm_srcdds6vrrht3m4[] = "lib/std/math/math.gm";
const char gm_srcgksioc7kn741[] = "compiler/tir/clone.gm";
const char gm_srcjrcmga748cdu[] = "compiler/interp/node.gm";
const char gm_srck89elqhmqqdc[] = "compiler/sema/vector.gm";
const char gm_srcp8o48ld4n4jq[] = "compiler/parse/types.gm";
const char gm_srcpit8mdn0781q[] = "compiler/interp/interp.gm";
const char gm_srcq1k9f0klj1q6[] = "compiler/sema/frame.gm";
const char gm_srcqod65l394oc8[] = "compiler/interp/intarith.gm";
const char gm_srcqtrdhljiapb8[] = "lib/std/io/io.gm";
const char gm_srcruugaa9g5kd5[] = "compiler/check/intrinsics.gm";
const char gm_srcstmd1b5da98l[] = "compiler/check/consts.gm";
const gm_spec *const gm_tpl086m1ajg247c_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl0c7sat2t7j6v_s[] = {NULL};
const gm_spec *const gm_tpl0fpifsdum8e8_s[] = {NULL, NULL};
const gm_spec *const gm_tpl0pva6pvhuq6i_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl0tr163o4bba1_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl1c0b1s109fa4_s[] = {NULL, NULL};
const gm_spec *const gm_tpl1dirc8ldrsao_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl1lvejpvpq0qh_s[] = {NULL, NULL};
const gm_spec *const gm_tpl1q6o13b940dg_s[] = {NULL};
const gm_spec *const gm_tpl1uf3f46uq3oj_s[] = {NULL};
const gm_spec *const gm_tpl1vbgcviqausd_s[] = {NULL, NULL};
const gm_spec *const gm_tpl2a8tmpvk11vt_s[] = {&(gm_spec){32, 0, 0, 1, 0, 1, 0u, -1}, NULL};
const gm_spec *const gm_tpl33jf6q62dk07_s[] = {NULL};
const gm_spec *const gm_tpl372mhqlcchb3_s[] = {NULL};
const gm_spec *const gm_tpl3lqpj3d2egtb_s[] = {NULL, NULL};
const gm_spec *const gm_tpl3tcah2c39gjk_s[] = {NULL};
const gm_spec *const gm_tpl4clb07vpsm45_s[] = {NULL, NULL};
const gm_spec *const gm_tpl4eg9pptom3ij_s[] = {NULL, NULL};
const gm_spec *const gm_tpl65tpfh2uj3bg_s[] = {NULL};
const gm_spec *const gm_tpl6boglt2rkmh4_s[] = {NULL};
const gm_spec *const gm_tpl6hqg4qptvadc_s[] = {NULL};
const gm_spec *const gm_tpl6muahgpe70pr_s[] = {NULL, NULL};
const gm_spec *const gm_tpl74mr52kbqst5_s[] = {NULL};
const gm_spec *const gm_tpl7ilj5n9objtq_s[] = {NULL};
const gm_spec *const gm_tpl7ufg84gs1lc7_s[] = {NULL, NULL};
const gm_spec *const gm_tpl7vtkjsp2pa8b_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpl8urde81v7030_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpl9900stiqclaj_s[] = {NULL};
const gm_spec *const gm_tpl9bim66fl3b37_s[] = {NULL, NULL};
const gm_spec *const gm_tpl9d1siq0c9pir_s[] = {&(gm_spec){32, 0, 0, 0, 0, 1, 0u, -1}};
const gm_spec *const gm_tpl9e18q8to9md2_s[] = {NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpl9fn8oj7nnnsq_s[] = {NULL};
const gm_spec *const gm_tpl9fvss98clvrp_s[] = {NULL};
const gm_spec *const gm_tpl9va1ubk8bean_s[] = {NULL, NULL};
const gm_spec *const gm_tpl9vu8tqcv1d9s_s[] = {NULL, NULL};
const gm_spec *const gm_tpla67mmq1id2h3_s[] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplae6k9mv0s1dm_s[] = {NULL, NULL};
const gm_spec *const gm_tplancpnb63qiq5_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplaos6tmpdtrie_s[] = {NULL, NULL};
const gm_spec *const gm_tplbm5ip0ejsndh_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplbulo51k20om4_s[] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplc809u7fr7kg6_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplcef5l6cvp5lt_s[] = {NULL, NULL};
const gm_spec *const gm_tplceqrbdtcd9p5_s[] = {NULL};
const gm_spec *const gm_tplcoet0p57pg4s_s[] = {NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplcouqld11oeil_s[] = {NULL};
const gm_spec *const gm_tplcre592qvlrqu_s[] = {NULL, NULL};
const gm_spec *const gm_tplde4760b0jcjn_s[] = {NULL, NULL};
const gm_spec *const gm_tpldrlq7uo0cbrm_s[] = {NULL};
const gm_spec *const gm_tpldtecn6n5hb86_s[] = {NULL, NULL};
const gm_spec *const gm_tplduhq10oq213m_s[] = {NULL};
const gm_spec *const gm_tple7msnncsq0hr_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplei9v853i04sp_s[] = {NULL, NULL};
const gm_spec *const gm_tplekdseqnnupsj_s[] = {NULL, NULL};
const gm_spec *const gm_tplellk9btkq1f2_s[] = {NULL, NULL};
const gm_spec *const gm_tplemb9urr8jmq6_s[] = {NULL, NULL};
const gm_spec *const gm_tples1m1m9iv6b0_s[] = {NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplevafblg3gmq9_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplf41j0oji3nkl_s[] = {NULL};
const gm_spec *const gm_tplfe6giveqr9e1_s[] = {NULL};
const gm_spec *const gm_tplfft5kohcphn8_s[] = {NULL, NULL};
const gm_spec *const gm_tplfsk62pc25cdb_s[] = {NULL};
const gm_spec *const gm_tplg21ilonldhva_s[] = {NULL, NULL};
const gm_spec *const gm_tplg4k0uitmfhf0_s[] = {NULL, NULL};
const gm_spec *const gm_tplgja2nber62dc_s[] = {NULL};
const gm_spec *const gm_tplgnobkmf75b9i_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplgt6vkfjdh8cd_s[] = {NULL};
const gm_spec *const gm_tplgvbr3vejj74c_s[] = {NULL, NULL};
const gm_spec *const gm_tplhatoh83mi4is_s[] = {NULL};
const gm_spec *const gm_tplhksbien67lqe_s[] = {NULL};
const gm_spec *const gm_tplho1kg74nlm64_s[] = {NULL};
const gm_spec *const gm_tpli9uflldtkfq3_s[] = {NULL, NULL};
const gm_spec *const gm_tplin004j5gtkru_s[] = {NULL};
const gm_spec *const gm_tplk13976gpibvh_s[] = {NULL};
const gm_spec *const gm_tplkji0sd9phcb9_s[] = {NULL};
const gm_spec *const gm_tplkk98947i5b3i_s[] = {NULL, NULL};
const gm_spec *const gm_tplkmrsfbsle095_s[] = {NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplktj7cco9lq40_s[] = {NULL, NULL};
const gm_spec *const gm_tpll9g0ell7ocpk_s[] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpllak8uc09ui7v_s[] = {NULL, NULL};
const gm_spec *const gm_tplle7qvdq2jai6_s[] = {NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplmaqqdmni75bf_s[] = {NULL, NULL};
const gm_spec *const gm_tplmjmservvuqtp_s[] = {NULL, NULL};
const gm_spec *const gm_tplmkgblgbo7imb_s[] = {NULL};
const gm_spec *const gm_tplmoo8v17lb0ql_s[] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplmtg92v4cesud_s[] = {NULL};
const gm_spec *const gm_tplmurfi7rvdamc_s[] = {NULL};
const gm_spec *const gm_tplnbsaptme3355_s[] = {NULL};
const gm_spec *const gm_tplngsd5kcuu6fv_s[] = {NULL};
const gm_spec *const gm_tplnr9pmjldkl35_s[] = {NULL, NULL};
const gm_spec *const gm_tplo08f1kg3k97n_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tploa995k06npbj_s[] = {NULL, NULL};
const gm_spec *const gm_tplod6iso1fh3co_s[] = {NULL, NULL};
const gm_spec *const gm_tplooqfb0umhrq0_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplopmhgmch9jsj_s[] = {NULL};
const gm_spec *const gm_tplp3l2s17nqpoi_s[] = {NULL};
const gm_spec *const gm_tplp7faum694g3u_s[] = {NULL, NULL};
const gm_spec *const gm_tplpav1vs8t9nu9_s[] = {NULL};
const gm_spec *const gm_tplpcqqtntqphus_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplpog54do9oh2a_s[] = {NULL, NULL};
const gm_spec *const gm_tplqci9e677opav_s[] = {NULL};
const gm_spec *const gm_tplql7vfevun5ae_s[] = {NULL};
const gm_spec *const gm_tplr1if0cs3oolt_s[] = {NULL};
const gm_spec *const gm_tplrfab517u4o97_s[] = {NULL};
const gm_spec *const gm_tplrq1qj3euarkk_s[] = {NULL};
const gm_spec *const gm_tplrsdjn4cifhr9_s[] = {NULL};
const gm_spec *const gm_tplru5t7oahrjah_s[] = {NULL};
const gm_spec *const gm_tpls6f1h1hgtqgb_s[] = {NULL};
const gm_spec *const gm_tplt1i3tc7bfdhf_s[] = {NULL, NULL};
const gm_spec *const gm_tplt68r4kihke54_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplt9d80smn1kum_s[] = {NULL};
const gm_spec *const gm_tpltd7vnsp01mvt_s[] = {NULL, NULL};
const gm_spec *const gm_tpltfirffnkp263_s[] = {NULL, NULL};
const gm_spec *const gm_tplthkeg7jngn6q_s[] = {NULL};
const gm_spec *const gm_tpltpd3ap2ql1ip_s[] = {NULL, NULL};
const gm_spec *const gm_tplu7gq2cm8aanh_s[] = {NULL};
const gm_spec *const gm_tpluf8termqcruk_s[] = {NULL};
const gm_spec *const gm_tplup0bf5d2eopf_s[] = {NULL};
const gm_spec *const gm_tpluuts50bm11md_s[] = {NULL};
const gm_spec *const gm_tplv9sqjp27m76e_s[] = {NULL, NULL};
const gm_spec *const gm_tplvbotf15ps5s5_s[] = {NULL};
const gm_tmpl gm_tpl00cra9g0k45s = {2u, gm_tpl00cra9g0k45s_p, gm_tpl00cra9g0k45s_i, gm_tpl00cra9g0k45s_s};
const gm_tmpl gm_tpl02lckpq8ae01 = {2u, gm_tpl02lckpq8ae01_p, gm_tpl02lckpq8ae01_i, gm_tpl02lckpq8ae01_s};
const gm_tmpl gm_tpl0aluqdlmv333 = {2u, gm_tpl0aluqdlmv333_p, gm_tpl0aluqdlmv333_i, gm_tpl0aluqdlmv333_s};
const gm_tmpl gm_tpl0emlhhlpmv0s = {2u, gm_tpl0emlhhlpmv0s_p, gm_tpl0emlhhlpmv0s_i, gm_tpl0emlhhlpmv0s_s};
const gm_tmpl gm_tpl0eumfheo3vtt = {1u, gm_tpl0eumfheo3vtt_p, gm_tpl0eumfheo3vtt_i, gm_tpl0eumfheo3vtt_s};
const gm_tmpl gm_tpl0v0dinrpdv6d = {1u, gm_tpl0v0dinrpdv6d_p, gm_tpl0v0dinrpdv6d_i, gm_tpl0v0dinrpdv6d_s};
const gm_tmpl gm_tpl1bh0s5ll1m9j = {1u, gm_tpl1bh0s5ll1m9j_p, gm_tpl1bh0s5ll1m9j_i, gm_tpl1bh0s5ll1m9j_s};
const gm_tmpl gm_tpl1irt5kietk82 = {5u, gm_tpl1irt5kietk82_p, gm_tpl1irt5kietk82_i, gm_tpl1irt5kietk82_s};
const gm_tmpl gm_tpl2jsq89buriv9 = {1u, gm_tpl2jsq89buriv9_p, gm_tpl2jsq89buriv9_i, gm_tpl2jsq89buriv9_s};
const gm_tmpl gm_tpl313mu5t4735i = {1u, gm_tpl313mu5t4735i_p, gm_tpl313mu5t4735i_i, gm_tpl313mu5t4735i_s};
const gm_tmpl gm_tpl40m4ah8dc3ae = {1u, gm_tpl40m4ah8dc3ae_p, gm_tpl40m4ah8dc3ae_i, gm_tpl40m4ah8dc3ae_s};
const gm_tmpl gm_tpl43224j8old0s = {1u, gm_tpl43224j8old0s_p, gm_tpl43224j8old0s_i, gm_tpl43224j8old0s_s};
const gm_tmpl gm_tpl4cbbis3gd1k3 = {2u, gm_tpl4cbbis3gd1k3_p, gm_tpl4cbbis3gd1k3_i, gm_tpl4cbbis3gd1k3_s};
const gm_tmpl gm_tpl4clb07vpsm45 = {2u, gm_tpl4clb07vpsm45_p, gm_tpl4clb07vpsm45_i, gm_tpl4clb07vpsm45_s};
const gm_tmpl gm_tpl4u6a7su34sch = {1u, gm_tpl4u6a7su34sch_p, gm_tpl4u6a7su34sch_i, gm_tpl4u6a7su34sch_s};
const gm_tmpl gm_tpl5jb9rn2u95g6 = {2u, gm_tpl5jb9rn2u95g6_p, gm_tpl5jb9rn2u95g6_i, gm_tpl5jb9rn2u95g6_s};
const gm_tmpl gm_tpl5o8dcpa0ctot = {2u, gm_tpl5o8dcpa0ctot_p, gm_tpl5o8dcpa0ctot_i, gm_tpl5o8dcpa0ctot_s};
const gm_tmpl gm_tpl5q9oi9aqgs43 = {1u, gm_tpl5q9oi9aqgs43_p, gm_tpl5q9oi9aqgs43_i, gm_tpl5q9oi9aqgs43_s};
const gm_tmpl gm_tpl5qm9nkkj1gjg = {2u, gm_tpl5qm9nkkj1gjg_p, gm_tpl5qm9nkkj1gjg_i, gm_tpl5qm9nkkj1gjg_s};
const gm_tmpl gm_tpl60i3afd6fgrq = {1u, gm_tpl60i3afd6fgrq_p, gm_tpl60i3afd6fgrq_i, gm_tpl60i3afd6fgrq_s};
const gm_tmpl gm_tpl6d7dqphpmhs7 = {3u, gm_tpl6d7dqphpmhs7_p, gm_tpl6d7dqphpmhs7_i, gm_tpl6d7dqphpmhs7_s};
const gm_tmpl gm_tpl6ilgcn878l74 = {2u, gm_tpl6ilgcn878l74_p, gm_tpl6ilgcn878l74_i, gm_tpl6ilgcn878l74_s};
const gm_tmpl gm_tpl7ilj5n9objtq = {1u, gm_tpl7ilj5n9objtq_p, gm_tpl7ilj5n9objtq_i, gm_tpl7ilj5n9objtq_s};
const gm_tmpl gm_tpl7sqjangpsngf = {6u, gm_tpl7sqjangpsngf_p, gm_tpl7sqjangpsngf_i, gm_tpl7sqjangpsngf_s};
const gm_tmpl gm_tpl8nqn0hncnfd4 = {2u, gm_tpl8nqn0hncnfd4_p, gm_tpl8nqn0hncnfd4_i, gm_tpl8nqn0hncnfd4_s};
const gm_tmpl gm_tpl8s825rf2tq3f = {3u, gm_tpl8s825rf2tq3f_p, gm_tpl8s825rf2tq3f_i, gm_tpl8s825rf2tq3f_s};
const gm_tmpl gm_tpl9q5j6bisf2fa = {4u, gm_tpl9q5j6bisf2fa_p, gm_tpl9q5j6bisf2fa_i, gm_tpl9q5j6bisf2fa_s};
const gm_tmpl gm_tpl9ufa4j1b7kal = {2u, gm_tpl9ufa4j1b7kal_p, gm_tpl9ufa4j1b7kal_i, gm_tpl9ufa4j1b7kal_s};
const gm_tmpl gm_tpl9va1ubk8bean = {2u, gm_tpl9va1ubk8bean_p, gm_tpl9va1ubk8bean_i, gm_tpl9va1ubk8bean_s};
const gm_tmpl gm_tpla0a5a633lqve = {1u, gm_tpla0a5a633lqve_p, gm_tpla0a5a633lqve_i, gm_tpla0a5a633lqve_s};
const gm_tmpl gm_tpla6sajpp07b9l = {1u, gm_tpla6sajpp07b9l_p, gm_tpla6sajpp07b9l_i, gm_tpla6sajpp07b9l_s};
const gm_tmpl gm_tplastf5j9ge45b = {1u, gm_tplastf5j9ge45b_p, gm_tplastf5j9ge45b_i, gm_tplastf5j9ge45b_s};
const gm_tmpl gm_tplb043ibme104j = {7u, gm_tplb043ibme104j_p, gm_tplb043ibme104j_i, gm_tplb043ibme104j_s};
const gm_tmpl gm_tplb2s3lfi50rhi = {2u, gm_tplb2s3lfi50rhi_p, gm_tplb2s3lfi50rhi_i, gm_tplb2s3lfi50rhi_s};
const gm_tmpl gm_tplbathm90qd36n = {2u, gm_tplbathm90qd36n_p, gm_tplbathm90qd36n_i, gm_tplbathm90qd36n_s};
const gm_tmpl gm_tplbj4npuroda3f = {3u, gm_tplbj4npuroda3f_p, gm_tplbj4npuroda3f_i, gm_tplbj4npuroda3f_s};
const gm_tmpl gm_tplbr5d1kr4l84u = {1u, gm_tplbr5d1kr4l84u_p, gm_tplbr5d1kr4l84u_i, gm_tplbr5d1kr4l84u_s};
const gm_tmpl gm_tplbtqgdimqhkr1 = {2u, gm_tplbtqgdimqhkr1_p, gm_tplbtqgdimqhkr1_i, gm_tplbtqgdimqhkr1_s};
const gm_tmpl gm_tplcic56vj3l629 = {1u, gm_tplcic56vj3l629_p, gm_tplcic56vj3l629_i, gm_tplcic56vj3l629_s};
const gm_tmpl gm_tplcq107msa8ktd = {1u, gm_tplcq107msa8ktd_p, gm_tplcq107msa8ktd_i, gm_tplcq107msa8ktd_s};
const gm_tmpl gm_tplcseelhcfsg75 = {2u, gm_tplcseelhcfsg75_p, gm_tplcseelhcfsg75_i, gm_tplcseelhcfsg75_s};
const gm_tmpl gm_tpld0t14m1fld0p = {3u, gm_tpld0t14m1fld0p_p, gm_tpld0t14m1fld0p_i, gm_tpld0t14m1fld0p_s};
const gm_tmpl gm_tpld1hhk0kef14l = {1u, gm_tpld1hhk0kef14l_p, gm_tpld1hhk0kef14l_i, gm_tpld1hhk0kef14l_s};
const gm_tmpl gm_tpld7d7u7to2a7u = {1u, gm_tpld7d7u7to2a7u_p, gm_tpld7d7u7to2a7u_i, gm_tpld7d7u7to2a7u_s};
const gm_tmpl gm_tpldff5gvnhne6f = {3u, gm_tpldff5gvnhne6f_p, gm_tpldff5gvnhne6f_i, gm_tpldff5gvnhne6f_s};
const gm_tmpl gm_tpldhof7u5v5b2c = {1u, gm_tpldhof7u5v5b2c_p, gm_tpldhof7u5v5b2c_i, gm_tpldhof7u5v5b2c_s};
const gm_tmpl gm_tpldueenbh2sgd4 = {2u, gm_tpldueenbh2sgd4_p, gm_tpldueenbh2sgd4_i, gm_tpldueenbh2sgd4_s};
const gm_tmpl gm_tple7ukme0qchh6 = {1u, gm_tple7ukme0qchh6_p, gm_tple7ukme0qchh6_i, gm_tple7ukme0qchh6_s};
const gm_tmpl gm_tplej21mtlj140h = {2u, gm_tplej21mtlj140h_p, gm_tplej21mtlj140h_i, gm_tplej21mtlj140h_s};
const gm_tmpl gm_tplellk9btkq1f2 = {2u, gm_tplellk9btkq1f2_p, gm_tplellk9btkq1f2_i, gm_tplellk9btkq1f2_s};
const gm_tmpl gm_tpleoabj1u2rb54 = {1u, gm_tpleoabj1u2rb54_p, gm_tpleoabj1u2rb54_i, gm_tpleoabj1u2rb54_s};
const gm_tmpl gm_tplf28d52hmmd2n = {1u, gm_tplf28d52hmmd2n_p, gm_tplf28d52hmmd2n_i, gm_tplf28d52hmmd2n_s};
const gm_tmpl gm_tplf48rihumtlpr = {3u, gm_tplf48rihumtlpr_p, gm_tplf48rihumtlpr_i, gm_tplf48rihumtlpr_s};
const gm_tmpl gm_tplf8er2klne616 = {1u, gm_tplf8er2klne616_p, gm_tplf8er2klne616_i, gm_tplf8er2klne616_s};
const gm_tmpl gm_tplf9434bnahqi1 = {1u, gm_tplf9434bnahqi1_p, gm_tplf9434bnahqi1_i, gm_tplf9434bnahqi1_s};
const gm_tmpl gm_tplfbtlvu2isf2h = {2u, gm_tplfbtlvu2isf2h_p, gm_tplfbtlvu2isf2h_i, gm_tplfbtlvu2isf2h_s};
const gm_tmpl gm_tplff72i6ku4gld = {2u, gm_tplff72i6ku4gld_p, gm_tplff72i6ku4gld_i, gm_tplff72i6ku4gld_s};
const gm_tmpl gm_tplftchh41b473r = {8u, gm_tplftchh41b473r_p, gm_tplftchh41b473r_i, gm_tplftchh41b473r_s};
const gm_tmpl gm_tplfvv1qe6os0le = {1u, gm_tplfvv1qe6os0le_p, gm_tplfvv1qe6os0le_i, gm_tplfvv1qe6os0le_s};
const gm_tmpl gm_tplg2qlrjk1sunc = {1u, gm_tplg2qlrjk1sunc_p, gm_tplg2qlrjk1sunc_i, gm_tplg2qlrjk1sunc_s};
const gm_tmpl gm_tplgavllsav46n0 = {1u, gm_tplgavllsav46n0_p, gm_tplgavllsav46n0_i, gm_tplgavllsav46n0_s};
const gm_tmpl gm_tplibgopbmor3p6 = {2u, gm_tplibgopbmor3p6_p, gm_tplibgopbmor3p6_i, gm_tplibgopbmor3p6_s};
const gm_tmpl gm_tpligbj3kvvunpr = {1u, gm_tpligbj3kvvunpr_p, gm_tpligbj3kvvunpr_i, gm_tpligbj3kvvunpr_s};
const gm_tmpl gm_tplj0vaqlfn7orc = {2u, gm_tplj0vaqlfn7orc_p, gm_tplj0vaqlfn7orc_i, gm_tplj0vaqlfn7orc_s};
const gm_tmpl gm_tplj3vfuklvht3e = {2u, gm_tplj3vfuklvht3e_p, gm_tplj3vfuklvht3e_i, gm_tplj3vfuklvht3e_s};
const gm_tmpl gm_tplj6bgc315nev3 = {1u, gm_tplj6bgc315nev3_p, gm_tplj6bgc315nev3_i, gm_tplj6bgc315nev3_s};
const gm_tmpl gm_tplj7j0vuugbh2u = {2u, gm_tplj7j0vuugbh2u_p, gm_tplj7j0vuugbh2u_i, gm_tplj7j0vuugbh2u_s};
const gm_tmpl gm_tplk1dctbs79mmk = {3u, gm_tplk1dctbs79mmk_p, gm_tplk1dctbs79mmk_i, gm_tplk1dctbs79mmk_s};
const gm_tmpl gm_tplkh4k0ikfsmp6 = {1u, gm_tplkh4k0ikfsmp6_p, gm_tplkh4k0ikfsmp6_i, gm_tplkh4k0ikfsmp6_s};
const gm_tmpl gm_tplkjc1g73er3ub = {1u, gm_tplkjc1g73er3ub_p, gm_tplkjc1g73er3ub_i, gm_tplkjc1g73er3ub_s};
const gm_tmpl gm_tplkji0sd9phcb9 = {1u, gm_tplkji0sd9phcb9_p, gm_tplkji0sd9phcb9_i, gm_tplkji0sd9phcb9_s};
const gm_tmpl gm_tplknfbq49i74sp = {2u, gm_tplknfbq49i74sp_p, gm_tplknfbq49i74sp_i, gm_tplknfbq49i74sp_s};
const gm_tmpl gm_tplkokmqjs81d4g = {2u, gm_tplkokmqjs81d4g_p, gm_tplkokmqjs81d4g_i, gm_tplkokmqjs81d4g_s};
const gm_tmpl gm_tplkr4062p8a8bi = {3u, gm_tplkr4062p8a8bi_p, gm_tplkr4062p8a8bi_i, gm_tplkr4062p8a8bi_s};
const gm_tmpl gm_tpll487d6t52m4g = {1u, gm_tpll487d6t52m4g_p, gm_tpll487d6t52m4g_i, gm_tpll487d6t52m4g_s};
const gm_tmpl gm_tpll4f2ihchoe9c = {2u, gm_tpll4f2ihchoe9c_p, gm_tpll4f2ihchoe9c_i, gm_tpll4f2ihchoe9c_s};
const gm_tmpl gm_tpllikrj1hgkvih = {1u, gm_tpllikrj1hgkvih_p, gm_tpllikrj1hgkvih_i, gm_tpllikrj1hgkvih_s};
const gm_tmpl gm_tpllqboe50ngs7g = {1u, gm_tpllqboe50ngs7g_p, gm_tpllqboe50ngs7g_i, gm_tpllqboe50ngs7g_s};
const gm_tmpl gm_tplls606vhvqojp = {1u, gm_tplls606vhvqojp_p, gm_tplls606vhvqojp_i, gm_tplls606vhvqojp_s};
const gm_tmpl gm_tplm9t4pej720h2 = {3u, gm_tplm9t4pej720h2_p, gm_tplm9t4pej720h2_i, gm_tplm9t4pej720h2_s};
const gm_tmpl gm_tplmemqaoor78ph = {7u, gm_tplmemqaoor78ph_p, gm_tplmemqaoor78ph_i, gm_tplmemqaoor78ph_s};
const gm_tmpl gm_tpln8mihgcsij40 = {5u, gm_tpln8mihgcsij40_p, gm_tpln8mihgcsij40_i, gm_tpln8mihgcsij40_s};
const gm_tmpl gm_tplnc4en2s3t0da = {3u, gm_tplnc4en2s3t0da_p, gm_tplnc4en2s3t0da_i, gm_tplnc4en2s3t0da_s};
const gm_tmpl gm_tplnrs6u0h2oc92 = {1u, gm_tplnrs6u0h2oc92_p, gm_tplnrs6u0h2oc92_i, gm_tplnrs6u0h2oc92_s};
const gm_tmpl gm_tplnunrbopfpsiq = {2u, gm_tplnunrbopfpsiq_p, gm_tplnunrbopfpsiq_i, gm_tplnunrbopfpsiq_s};
const gm_tmpl gm_tplnv6l3ecgreid = {2u, gm_tplnv6l3ecgreid_p, gm_tplnv6l3ecgreid_i, gm_tplnv6l3ecgreid_s};
const gm_tmpl gm_tplo38nq02vgfo5 = {1u, gm_tplo38nq02vgfo5_p, gm_tplo38nq02vgfo5_i, gm_tplo38nq02vgfo5_s};
const gm_tmpl gm_tplo481o6n7og1g = {3u, gm_tplo481o6n7og1g_p, gm_tplo481o6n7og1g_i, gm_tplo481o6n7og1g_s};
const gm_tmpl gm_tplp3rnngb19vkd = {1u, gm_tplp3rnngb19vkd_p, gm_tplp3rnngb19vkd_i, gm_tplp3rnngb19vkd_s};
const gm_tmpl gm_tplp7faum694g3u = {2u, gm_tplp7faum694g3u_p, gm_tplp7faum694g3u_i, gm_tplp7faum694g3u_s};
const gm_tmpl gm_tplp98fsht4k706 = {1u, gm_tplp98fsht4k706_p, gm_tplp98fsht4k706_i, gm_tplp98fsht4k706_s};
const gm_tmpl gm_tplpe5hifub5ms7 = {3u, gm_tplpe5hifub5ms7_p, gm_tplpe5hifub5ms7_i, gm_tplpe5hifub5ms7_s};
const gm_tmpl gm_tplptgaia3s19c9 = {1u, gm_tplptgaia3s19c9_p, gm_tplptgaia3s19c9_i, gm_tplptgaia3s19c9_s};
const gm_tmpl gm_tplq7vstpm0lgda = {3u, gm_tplq7vstpm0lgda_p, gm_tplq7vstpm0lgda_i, gm_tplq7vstpm0lgda_s};
const gm_tmpl gm_tplqci9e677opav = {1u, gm_tplqci9e677opav_p, gm_tplqci9e677opav_i, gm_tplqci9e677opav_s};
const gm_tmpl gm_tplqt9e8rihgogv = {2u, gm_tplqt9e8rihgogv_p, gm_tplqt9e8rihgogv_i, gm_tplqt9e8rihgogv_s};
const gm_tmpl gm_tplre7l7cu4fpbv = {2u, gm_tplre7l7cu4fpbv_p, gm_tplre7l7cu4fpbv_i, gm_tplre7l7cu4fpbv_s};
const gm_tmpl gm_tplrnntb2r7ns40 = {1u, gm_tplrnntb2r7ns40_p, gm_tplrnntb2r7ns40_i, gm_tplrnntb2r7ns40_s};
const gm_tmpl gm_tplrpvalpqjvipe = {2u, gm_tplrpvalpqjvipe_p, gm_tplrpvalpqjvipe_i, gm_tplrpvalpqjvipe_s};
const gm_tmpl gm_tplrveq8m992ml7 = {2u, gm_tplrveq8m992ml7_p, gm_tplrveq8m992ml7_i, gm_tplrveq8m992ml7_s};
const gm_tmpl gm_tplspgpn4qu6rd4 = {1u, gm_tplspgpn4qu6rd4_p, gm_tplspgpn4qu6rd4_i, gm_tplspgpn4qu6rd4_s};
const gm_tmpl gm_tplsrmv18emggok = {4u, gm_tplsrmv18emggok_p, gm_tplsrmv18emggok_i, gm_tplsrmv18emggok_s};
const gm_tmpl gm_tplt5ks894658kl = {1u, gm_tplt5ks894658kl_p, gm_tplt5ks894658kl_i, gm_tplt5ks894658kl_s};
const gm_tmpl gm_tpltlq1bga4cgul = {6u, gm_tpltlq1bga4cgul_p, gm_tpltlq1bga4cgul_i, gm_tpltlq1bga4cgul_s};
const gm_tmpl gm_tplu65cpe6nhlbb = {1u, gm_tplu65cpe6nhlbb_p, gm_tplu65cpe6nhlbb_i, gm_tplu65cpe6nhlbb_s};
const gm_tmpl gm_tplu76evoc63de2 = {2u, gm_tplu76evoc63de2_p, gm_tplu76evoc63de2_i, gm_tplu76evoc63de2_s};
const gm_tmpl gm_tplue2kdcikgcof = {4u, gm_tplue2kdcikgcof_p, gm_tplue2kdcikgcof_i, gm_tplue2kdcikgcof_s};
const gm_tmpl gm_tpluguraohl9kn4 = {2u, gm_tpluguraohl9kn4_p, gm_tpluguraohl9kn4_i, gm_tpluguraohl9kn4_s};
const gm_tmpl gm_tplv23k7silrvnp = {1u, gm_tplv23k7silrvnp_p, gm_tplv23k7silrvnp_i, gm_tplv23k7silrvnp_s};
const gm_tmpl gm_tplv8ckuk7p1s8m = {2u, gm_tplv8ckuk7p1s8m_p, gm_tplv8ckuk7p1s8m_i, gm_tplv8ckuk7p1s8m_s};
const gm_tmpl gm_tplvuahs93lrft8 = {2u, gm_tplvuahs93lrft8_p, gm_tplvuahs93lrft8_i, gm_tplvuahs93lrft8_s};
const gm_tmpl_piece gm_tpl06vq09a1bq44_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_se08n5d1gngu7, 4}, {gm_smc854k364mj6, 20}, {gm_ssr6vk8erbj18, 12}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpl0f11vet24cfp_p[] = {{gm_sj9krj49k3580, 9}, {gm_sko4cgkdijve3, 56}};
const gm_tmpl_piece gm_tpl0rhldcdavmcg_p[] = {{gm_sr3et5tffndla, 27}, {gm_sqdfr4k3vea5p, 3}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl0tljhf3n7k2p_p[] = {{gm_smovt8m5cpa13, 6}, {gm_sg5gdnrith2i6, 2}, {gm_s1nvbb638rr96, 7}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tpl14b6qietv6bm_p[] = {{gm_s4oil3epcv041, 2}, {gm_sdpo2hjlq8e9b, 30}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpl1r51q3hoe535_p[] = {{gm_sdhlphf1ucpbo, 17}, {gm_svv3t2si5df2q, 14}, {gm_sb9dglh7ekrt2, 6}};
const gm_tmpl_piece gm_tpl1uf3f46uq3oj_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_s41h7dv6tes4i, 56}};
const gm_tmpl_piece gm_tpl2a7pmi7aq67m_p[] = {{gm_ss9d6rvgfqq53, 12}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl2dia2gbnfh9l_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl2fb8qlpeqlet_p[] = {{gm_sskv7tavvjk2l, 22}, {gm_spni6j7splqto, 7}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl2gebin91ghlu_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_spp0mb1aidgi1, 12}, {gm_smddipuf83u9j, 2}};
const gm_tmpl_piece gm_tpl352s0p85j412_p[] = {{gm_s4qg9pu9r16jq, 6}, {gm_spod3tmkh25up, 1}};
const gm_tmpl_piece gm_tpl3gep2a57gf23_p[] = {{gm_s3gqldvcud6t6, 16}, {gm_stbtfovn5dhm4, 4}, {gm_sscekt5vbls0f, 17}, {gm_sgukm1evmrjal, 3}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tpl40m4ah8dc3ae_p[] = {{gm_snh0sl2cdagdn, 12}, {gm_s0johk446liuj, 27}};
const gm_tmpl_piece gm_tpl4aiadg123uql_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sbp9encpv0e61, 74}};
const gm_tmpl_piece gm_tpl4bj6lgqq92ev_p[] = {{gm_sf42bd5sk6816, 25}, {gm_sg5gdnrith2i6, 2}, {gm_s2ha7brjf2gmp, 6}};
const gm_tmpl_piece gm_tpl4lt7abhi3ej4_p[] = {{gm_sfjc41f66fqb2, 9}, {gm_s05336tfii06a, 17}};
const gm_tmpl_piece gm_tpl4u6a7su34sch_p[] = {{gm_s5u1jd4mc0941, 31}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl51u3ki3ilqou_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s3p0rbs9hc3lg, 1}, {gm_s2u7e0bju5qb8, 27}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tpl5b7905cihaqk_p[] = {{gm_svthrqc59hf0v, 20}, {gm_s0ajsem4h8nft, 57}};
const gm_tmpl_piece gm_tpl65tpfh2uj3bg_p[] = {{gm_s4lmdtjb9rnah, 69}, {gm_seo41mk3n3ro5, 12}};
const gm_tmpl_piece gm_tpl6a59mrfvoo09_p[] = {{gm_sfmm1kh41nanh, 14}, {gm_shfckrpors679, 48}};
const gm_tmpl_piece gm_tpl6mra39d4fo2s_p[] = {{gm_siamquc19uikr, 1}, {gm_sifiecpi6vdsm, 10}, {gm_s3p0rbs9hc3lg, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl6tu8f1h1mg85_p[] = {{gm_sn2t7jm5mf8pm, 4}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl7o4acs2tt6pp_p[] = {{gm_stkm2m8f3okk8, 17}, {gm_s03itq0fotubb, 13}};
const gm_tmpl_piece gm_tpl81nunf5qvo6u_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sg3dnv3iaqhkk, 7}};
const gm_tmpl_piece gm_tpl83abvjjf3qd1_p[] = {{gm_somhu4vltiil1, 3}, {gm_shi7nidbc64qf, 3}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpl86i0s811rrlp_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_si0b5q45ot754, 100}};
const gm_tmpl_piece gm_tpl8ssnfo686ogd_p[] = {{gm_siamquc19uikr, 1}, {gm_svsiu4jgdp97c, 11}, {gm_sr4qbrk6tlgaa, 0}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl8un0qv11eeom_p[] = {{gm_sh08f8dq9lenu, 40}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpl9jrfnomsrfk1_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_s3hg5go4uihat, 51}, {gm_svbftsm94r0bv, 6}};
const gm_tmpl_piece gm_tpl9sifsarv7t06_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_shmpls8n0g8h9, 3}};
const gm_tmpl_piece gm_tpla0dc0a9b9i8b_p[] = {{gm_stqiimqtsnd8d, 8}, {gm_s5m9qimbv7e8k, 2}};
const gm_tmpl_piece gm_tpla3k9rgcg5o3q_p[] = {{gm_s1cn5ab5opg9f, 130}, {gm_s078u1763sqk3, 10}};
const gm_tmpl_piece gm_tplacb81m1n6g9e_p[] = {{gm_s8becutukvgrh, 16}, {gm_sg5gdnrith2i6, 2}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplad75mid9f0qe_p[] = {{gm_sgurqv8kb2rs7, 31}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplaggdk85jgg2m_p[] = {{gm_s3t2d4i4t34ct, 73}, {gm_sp7cdd0n91vli, 7}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplancpnb63qiq5_p[] = {{gm_sksh6i7obnq40, 7}, {gm_sv8s0ogr3iflj, 7}, {gm_stqvtvp1hc371, 11}, {gm_so514oj5nbhcu, 1}};
const gm_tmpl_piece gm_tplastf5j9ge45b_p[] = {{gm_stco696vp9tto, 13}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplc2ir650ohv3d_p[] = {{gm_srvgp776ipjtg, 29}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplc90dj04km0n5_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_s6q7rsv30u3ma, 33}, {gm_shv2qb39iqelf, 30}};
const gm_tmpl_piece gm_tplcddb08maq348_p[] = {{gm_s9dh9d8gs6a16, 8}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplcvnbkkkji94u_p[] = {{gm_stmsadvf54k73, 2}, {gm_sddnsndqgnosl, 30}, {gm_s1p5ld7i8mkfu, 9}};
const gm_tmpl_piece gm_tpldfm52le63mtn_p[] = {{gm_semhbv8seph85, 20}, {gm_srv4dcfccuset, 2}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpldi9jj8a80irn_p[] = {{gm_ss4tls6jr9q3m, 9}, {gm_sveg1mb15s1b2, 17}};
const gm_tmpl_piece gm_tpldj14c3jgvlnb_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sbc7a60c47o3n, 18}, {gm_scr60nbumsj2h, 24}, {gm_sl3q52ep6aa53, 1}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpldjbe17jhbkpg_p[] = {{gm_sl3q52ep6aa53, 1}, {gm_sfi2ha42r1nr2, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tple6jpca094i2j_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sao4mumvadoct, 3}, {gm_spod3tmkh25up, 1}};
const gm_tmpl_piece gm_tplekdseqnnupsj_p[] = {{gm_s81ppjm26jd5s, 147}, {gm_s7jj9572hd242, 5}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplep2l66hmuvrc_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s7vsso7k8tg8d, 24}, {gm_sp7cdd0n91vli, 7}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplfkugp8hqqhvu_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_srp5cji88najc, 9}, {gm_s93ft79mfnlvp, 9}, {gm_s1tfd03l0gv6e, 13}};
const gm_tmpl_piece gm_tplfrm74l83ht8l_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sp02qqgucmpt7, 2}, {gm_s9ol82jrvi3ma, 3}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplg21rd4as3ph6_p[] = {{gm_sdu3h28udk8s9, 12}, {gm_s1bpuqd44lur7, 6}, {gm_sg5gdnrith2i6, 2}, {gm_sqvepl1893o5g, 2}};
const gm_tmpl_piece gm_tplgeqcgpvi6n7p_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sn3tcnrle7f4r, 3}};
const gm_tmpl_piece gm_tplhi0fd9nctd4b_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sbkdsdb6p9kk7, 3}, {gm_smo8ffg4jq2d6, 44}};
const gm_tmpl_piece gm_tpli9foo8ib8qrk_p[] = {{gm_slgu49nv3o8e7, 11}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_snr5o5tak6405, 3}};
const gm_tmpl_piece gm_tplim7pg0q4u4tg_p[] = {{gm_slpd42s8rcbm0, 7}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpliqi7pna59a9o_p[] = {{gm_sevf8q26aov70, 5}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplit8jkrg5bt3p_p[] = {{gm_so9fi6r28770a, 2}, {gm_sba60r64q4l0i, 30}};
const gm_tmpl_piece gm_tplj2qf541fmi6f_p[] = {{gm_sq95m8isfa7vd, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpljc7kqi31f4d2_p[] = {{gm_s2j8t1jqamft8, 4}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplk4i636uhvi9m_p[] = {{gm_sasea0sobfkrs, 7}, {gm_sr4qbrk6tlgaa, 0}, {gm_s3p0rbs9hc3lg, 1}, {gm_smooaouqmm8dj, 1}, {gm_sg5e6vqb8nie3, 17}, {gm_sdhelph0q09tv, 4}};
const gm_tmpl_piece gm_tplkhlcq331nni9_p[] = {{gm_snh2jpqenu534, 16}, {gm_sg5gdnrith2i6, 2}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tpll6udkhd3c609_p[] = {{gm_sq94kuvk30n36, 5}, {gm_sbvutvpa8pe3d, 2}};
const gm_tmpl_piece gm_tpllcvau9l0fqcd_p[] = {{gm_sr8jfj391gf5o, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplld0dt78h0tum_p[] = {{gm_suifusi0pb6s0, 11}, {gm_s542rn9shphg8, 15}};
const gm_tmpl_piece gm_tpllikrj1hgkvih_p[] = {{gm_s27jprdv4pn6e, 9}, {gm_sep8ve6ec8tv7, 23}};
const gm_tmpl_piece gm_tpllp788bsiqs1r_p[] = {{gm_s3jllttnltak3, 25}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplm1fuloaqnfv4_p[] = {{gm_slqq08j0fro21, 30}, {gm_s41bap0m04tt9, 11}, {gm_svgk3n4sk1uat, 8}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplm2m182mt5ljj_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_s10qn2lc8ck56, 77}};
const gm_tmpl_piece gm_tplmm8hld32929h_p[] = {{gm_so4acijgbtr2b, 33}, {gm_sso4tmqr8gin2, 19}, {gm_sj1gcl0qo4i9p, 6}};
const gm_tmpl_piece gm_tplmo1fik79j6ea_p[] = {{gm_s03c24qg7i06q, 12}, {gm_sti61120eur38, 58}};
const gm_tmpl_piece gm_tplmr9p697ovoo7_p[] = {{gm_s15c9j2vv1d2a, 38}, {gm_so70siafe1e7a, 12}, {gm_s0b795193rvs2, 6}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpln92q8vfbc4aq_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sech7am46drki, 15}};
const gm_tmpl_piece gm_tplnkb386hrc6va_p[] = {{gm_soh8b8t1v3pmj, 55}, {gm_sneh1858d1bko, 19}, {gm_s5aj93skvhpnl, 16}, {gm_ssnsddtjt3gpa, 34}};
const gm_tmpl_piece gm_tplo38nq02vgfo5_p[] = {{gm_snbrjpgiqf592, 5}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplo5614u5649ka_p[] = {{gm_siamquc19uikr, 1}, {gm_sao4mumvadoct, 3}, {gm_s3p0rbs9hc3lg, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplodl7i2apj0bs_p[] = {{gm_s1f440st4mnkp, 1}, {gm_sm5be4659ulci, 1}};
const gm_tmpl_piece gm_tplolsc5tfmth66_p[] = {{gm_so9fi6r28770a, 2}, {gm_svhavo13qha5k, 3}, {gm_spgfhj218uj9k, 7}, {gm_s1utcdes4r8ku, 6}, {gm_s1mc2427h3p1h, 8}};
const gm_tmpl_piece gm_tplpcqqtntqphus_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_svkdb6kaja59r, 6}, {gm_so514oj5nbhcu, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplpehi2hdi7j7m_p[] = {{gm_siamquc19uikr, 1}, {gm_sodq6lf7v3uc2, 10}, {gm_s3p0rbs9hc3lg, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplph8v0nbmofpg_p[] = {{gm_smovt8m5cpa13, 6}, {gm_sosa5l0k6d34c, 14}, {gm_s0m0qaokk51g0, 3}, {gm_sg5gdnrith2i6, 2}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplpjjba8pqbhe2_p[] = {{gm_sf8bp31mrq32l, 18}, {gm_snukl6jqep6jf, 3}, {gm_snukl6jqep6jf, 3}, {gm_snukl6jqep6jf, 3}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplpvh6srii84sj_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_s0ibfi39mcc7n, 8}};
const gm_tmpl_piece gm_tplq3ra7ik9ej59_p[] = {{gm_sns1hl83hdlcn, 4}, {gm_sibmp9mck7osv, 12}};
const gm_tmpl_piece gm_tplqfgvjo31nom3_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_st3ja0nqi37bv, 1}, {gm_sr4qbrk6tlgaa, 0}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplqh3e2aojp71u_p[] = {{gm_s7ifc1fku673p, 4}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplqrbhvccemc9k_p[] = {{gm_ski66jdm8e243, 8}, {gm_se7oqjo2sonp9, 8}};
const gm_tmpl_piece gm_tplqt9e8rihgogv_p[] = {{gm_smooaouqmm8dj, 1}, {gm_s1asl1e1tvb0k, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplrfab517u4o97_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sqlorrkumh3an, 22}};
const gm_tmpl_piece gm_tplrfnb607u5hvd_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s2urait735ofq, 4}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplrmkc8hrdtbfh_p[] = {{gm_s9npjns7bnqaj, 18}, {gm_sf1f5se9222sg, 34}};
const gm_tmpl_piece gm_tplrsdjn4cifhr9_p[] = {{gm_s6pe06l7iops0, 4}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplsmpaecrda5mc_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_so514oj5nbhcu, 1}, {gm_so514oj5nbhcu, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpltpd3ap2ql1ip_p[] = {{gm_sovtt6julbcd8, 11}, {gm_seor1k5gntiuc, 51}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplu0h8mqm004jg_p[] = {{gm_srkvn420e4huu, 25}, {gm_sb3o36rmuvj28, 55}};
const gm_tmpl_piece gm_tplv2usruqglqv9_p[] = {{gm_sat505mlqphna, 25}, {gm_sqdfr4k3vea5p, 3}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplvc9hnrt45n6d_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_shrlc4g1vtjsu, 46}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplvk1pmcp9tr4p_p[] = {{gm_s23n3pek53qm1, 23}, {gm_s3q858hngo0ho, 38}};
const gm_tmpl_piece gm_tplvqpq1chrhvno_p[] = {{gm_svl0gta90bsdr, 48}, {gm_sfm9elkq6bn2e, 1}};
const gm_type gm_ty0lfe1s2pin = {.size = 8, .align = 4, .kind = GM_K_TUPLE, .named = true, .nfields = 2, .fields = gm_ty0lfe1s2pin_f};
const gm_type gm_ty0lfe1s2pin;
const gm_variant gm_tyasplg81cl8_v[] = {{"Sqrt", 0, NULL}, {"Trunc", 1, NULL}, {"Round", 2, NULL}, {"Floor", 3, NULL}, {"Ceil", 4, NULL}, {"Sat", 5, NULL}};
const uint32_t gm_tpl005eoei82778_i[] = {0u};
const uint32_t gm_tpl0emlhhlpmv0s_i[] = {0u, 1u};
const uint32_t gm_tpl0ke3r8fiia4r_i[] = {0u};
const uint32_t gm_tpl16petnrgdcd0_i[] = {0u};
const uint32_t gm_tpl1a3usdkusvtq_i[] = {0u, 1u};
const uint32_t gm_tpl1c419vqpem2n_i[] = {0u, 1u};
const uint32_t gm_tpl1dirc8ldrsao_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl1oodsdaf08oj_i[] = {0u, 1u};
const uint32_t gm_tpl233lm3m83499_i[] = {0u};
const uint32_t gm_tpl2qal2i0dmdum_i[] = {0u};
const uint32_t gm_tpl31lgghh3mal3_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tpl33em4maossan_i[] = {0u};
const uint32_t gm_tpl386j11hdkrib_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl3m6tjsa4flq8_i[] = {0u};
const uint32_t gm_tpl3ssq6hkk3qrj_i[] = {0u};
const uint32_t gm_tpl4476c5bksft1_i[] = {0u, 1u};
const uint32_t gm_tpl461nvut9qmgp_i[] = {0u};
const uint32_t gm_tpl4qnmv5o1ceod_i[] = {0u};
const uint32_t gm_tpl4t11fkqh738f_i[] = {0u, 1u};
const uint32_t gm_tpl59s4m8f706c9_i[] = {0u, 1u};
const uint32_t gm_tpl5i8vi7ph4hfi_i[] = {0u, 1u};
const uint32_t gm_tpl5poqk71bg1bn_i[] = {0u, 1u};
const uint32_t gm_tpl5q4rr3nd9637_i[] = {0u};
const uint32_t gm_tpl5uqj3jbiq5f0_i[] = {0u};
const uint32_t gm_tpl6vbod7vja73l_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl75h0vbo3vani_i[] = {0u};
const uint32_t gm_tpl7dh44jmr11le_i[] = {0u, 1u, 2u, 3u, 4u, 5u};
const uint32_t gm_tpl7e5tr64a0u6e_i[] = {0u};
const uint32_t gm_tpl8eifhruu514e_i[] = {0u};
const uint32_t gm_tpl953juq2iaklp_i[] = {0u, 1u};
const uint32_t gm_tpl9gmvk2gm4l5h_i[] = {0u};
const uint32_t gm_tpl9l9rgf8juo7o_i[] = {0u, 1u};
const uint32_t gm_tpl9uslg4i3oo2s_i[] = {0u};
const uint32_t gm_tpl9v7lb7n4a602_i[] = {0u, 1u};
const uint32_t gm_tpla93ou7df923g_i[] = {0u, 1u, 2u};
const uint32_t gm_tplae28pp9b49u1_i[] = {0u};
const uint32_t gm_tplaemahh05v026_i[] = {0u};
const uint32_t gm_tplaggdk85jgg2m_i[] = {0u, 1u};
const uint32_t gm_tplbm5ip0ejsndh_i[] = {0u, 1u, 2u};
const uint32_t gm_tplbmf4neusqbo5_i[] = {0u, 1u, 2u};
const uint32_t gm_tplbppmjhpd8ol0_i[] = {0u};
const uint32_t gm_tplcilp3mvhfo4n_i[] = {0u};
const uint32_t gm_tplcne9d7171tqp_i[] = {0u, 1u};
const uint32_t gm_tplcpa735d0vjnu_i[] = {0u};
const uint32_t gm_tpld47p9te8ar36_i[] = {0u};
const uint32_t gm_tpldlrsentitfg3_i[] = {0u, 1u};
const uint32_t gm_tpldrcjvgmk3ig3_i[] = {0u};
const uint32_t gm_tpldtaonf5laaam_i[] = {0u};
const uint32_t gm_tplduhq10oq213m_i[] = {0u};
const uint32_t gm_tpldunbbu9dpisn_i[] = {0u};
const uint32_t gm_tplduutdqd8m5r5_i[] = {0u};
const uint32_t gm_tple377dkn6pj7u_i[] = {0u};
const uint32_t gm_tple75bobdc6q0g_i[] = {0u, 1u};
const uint32_t gm_tplf28d52hmmd2n_i[] = {0u};
const uint32_t gm_tplfpi7koctqa7b_i[] = {0u};
const uint32_t gm_tplgih8dfq5hun7_i[] = {0u, 1u};
const uint32_t gm_tplgqohfqetaph7_i[] = {0u};
const uint32_t gm_tplgv9va17kt7ll_i[] = {0u};
const uint32_t gm_tplh2sse9mocm92_i[] = {0u, 1u};
const uint32_t gm_tplht122d0ji4km_i[] = {0u, 1u};
const uint32_t gm_tpli4rfoi75onhj_i[] = {0u, 1u};
const uint32_t gm_tpli9foo8ib8qrk_i[] = {0u, 1u, 2u};
const uint32_t gm_tpliig5tr0gjn5a_i[] = {0u, 1u};
const uint32_t gm_tplj0t0s73b2v4j_i[] = {0u};
const uint32_t gm_tplj16n1o03849r_i[] = {0u, 1u, 2u};
const uint32_t gm_tplj57b11lvidge_i[] = {0u, 1u};
const uint32_t gm_tplj6ss1j7sgouf_i[] = {0u};
const uint32_t gm_tplkfag0fic60vv_i[] = {0u};
const uint32_t gm_tplkhlcq331nni9_i[] = {0u, 1u};
const uint32_t gm_tplkhnuvgfhr9v5_i[] = {0u, 1u};
const uint32_t gm_tplkokmqjs81d4g_i[] = {0u, 1u};
const uint32_t gm_tpll060ppa56f8b_i[] = {0u};
const uint32_t gm_tpllikrj1hgkvih_i[] = {0u};
const uint32_t gm_tpllsaus03ne167_i[] = {0u};
const uint32_t gm_tplm3dvc32djhr7_i[] = {0u};
const uint32_t gm_tplmemqaoor78ph_i[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u};
const uint32_t gm_tplmqflu0tp0eas_i[] = {0u};
const uint32_t gm_tplmr9p697ovoo7_i[] = {0u, 1u, 2u};
const uint32_t gm_tplmurfi7rvdamc_i[] = {0u};
const uint32_t gm_tpln1d9amhhlm2o_i[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u};
const uint32_t gm_tpln3j86kdpqd7a_i[] = {0u};
const uint32_t gm_tpln4hlddpv31vi_i[] = {0u, 1u, 2u};
const uint32_t gm_tplnj647l00ku7g_i[] = {0u, 1u, 2u};
const uint32_t gm_tplnodn319tb55r_i[] = {0u};
const uint32_t gm_tplnrs6u0h2oc92_i[] = {0u};
const uint32_t gm_tplnrt3ito76b2v_i[] = {0u};
const uint32_t gm_tplo481o6n7og1g_i[] = {0u, 1u, 2u};
const uint32_t gm_tploik9nhlf98dm_i[] = {0u, 1u, 2u};
const uint32_t gm_tplolnkad3lslng_i[] = {0u};
const uint32_t gm_tplp0kc3a3fekq4_i[] = {0u};
const uint32_t gm_tplp81h2rbu9gd2_i[] = {0u};
const uint32_t gm_tplp83ba2hehir9_i[] = {0u, 1u};
const uint32_t gm_tplpcqqtntqphus_i[] = {0u, 1u, 2u};
const uint32_t gm_tplqc6j3ejcoft6_i[] = {0u, 1u};
const uint32_t gm_tplr35nagqnpmad_i[] = {0u};
const uint32_t gm_tplrtgv8em132rl_i[] = {0u, 1u};
const uint32_t gm_tplrvl1hqu8hbkh_i[] = {0u, 1u};
const uint32_t gm_tpls6cg1uub8gfo_i[] = {0u};
const uint32_t gm_tpls6dj3unub5ff_i[] = {0u, 1u};
const uint32_t gm_tplsns868vihk0u_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tplt2babpet96ij_i[] = {0u};
const uint32_t gm_tplt3iub1goev8r_i[] = {0u, 1u};
const uint32_t gm_tpltj7umjd53udq_i[] = {0u};
const uint32_t gm_tpltktipp7m43tk_i[] = {0u, 1u, 2u};
const uint32_t gm_tpltm9r5n97je13_i[] = {0u};
const uint32_t gm_tpltur94fmrrcmk_i[] = {0u, 1u};
const uint32_t gm_tplu272587ugnvi_i[] = {0u, 1u};
const uint32_t gm_tplu9669cm9mqja_i[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};
const uint32_t gm_tplugrpoiqe5r59_i[] = {0u};
const uint32_t gm_tplumuahjb5m8m8_i[] = {0u, 1u};
const uint32_t gm_tpluuts50bm11md_i[] = {0u};
const uint32_t gm_tpluvp47iqcj4uv_i[] = {0u};
const uint32_t gm_tplv2usruqglqv9_i[] = {0u, 1u};
const uint32_t gm_tplv6dbvs9bjnes_i[] = {0u, 1u};
const uint32_t gm_tplvdft4iatcp17_i[] = {0u};
const uint32_t gm_tplvuahs93lrft8_i[] = {0u, 1u};
const uint32_t gm_tplvvtt1jk959m0_i[] = {0u};
const uint8_t gm_s02nu08jrc7mi[13] = "gm_fs_rename";
const uint8_t gm_s06pkj5koudes[11] = "-std=gnu11";
const uint8_t gm_s09rebkngh86a[34] = "slicing this type at compile time";
const uint8_t gm_s0a1978iad5c9[5] = "List";
const uint8_t gm_s0aqhubr4upni[7] = "fnsink";
const uint8_t gm_s0bc9s1qderg8[70] = "only an array variable (a place) can be sliced, not a temporary array";
const uint8_t gm_s0eipmcrsllno[8] = "{} \"{}\"";
const uint8_t gm_s0f4hcujnvqta[95] = "`{}` cannot be matched with a struct pattern outside its {}: its field `{}` is private (\302\2473.3)";
const uint8_t gm_s0m3gh47554ab[6] = "gm_x_";
const uint8_t gm_s0mm4r75od12l[7] = "unwrap";
const uint8_t gm_s0pqdsaf6jp6t[9] = "\000\000\001\000\000\000\000\000";
const uint8_t gm_s0qfhsu29ohf6[14] = "` needs `git`";
const uint8_t gm_s0s45q9kur545[15] = "), &ev, &evn);";
const uint8_t gm_s0tt0537if3ve[29] = "`.{}` cannot convert to `{}`";
const uint8_t gm_s10iv34fu7qe7[3] = "cx";
const uint8_t gm_s13vssup4nqsr[12] = "read_string";
const uint8_t gm_s147lrab6u68i[29] = "value of {} exceeds u64::MAX";
const uint8_t gm_s15tjb1oda5hu[6] = "(\342\200\246)";
const uint8_t gm_s1b0nuksrh572[29] = "`dependencies` holds strings";
const uint8_t gm_s1ec711cn6c5f[33] = ") { gm_unw.kind = GM_NONE; goto ";
const uint8_t gm_s1ecdmpgj8faq[24] = "GM_LD({}, {} + {} * {})";
const uint8_t gm_s1fshvu7vb9i4[16] = "float 0x{:016x}";
const uint8_t gm_s1hspsqta2g4h[9] = "<strong>";
const uint8_t gm_s1i00i6e3g77j[3] = "\"\"";
const uint8_t gm_s1k6vrpuaktdj[13] = "elide_checks";
const uint8_t gm_s1l7s35k2bj17[8] = "gm_ctz(";
const uint8_t gm_s1mc2427h3p1h[9] = ") - 1)))";
const uint8_t gm_s1o0cl13ab3oc[18] = "the matched value";
const uint8_t gm_s1r4msb7spg8n[4] = "map";
const uint8_t gm_s20mq4dk47bdd[4] = "m{}";
const uint8_t gm_s24et6r4rgifd[16] = "declare_foreign";
const uint8_t gm_s24mra5tninbk[15] = "GM_NOINLINE {}";
const uint8_t gm_s26gmgbksn2pm[61] = "`{}` is not a symbol name: use letters, digits and `_ $ . @`";
const uint8_t gm_s274psem80gbe[10] = "openClose";
const uint8_t gm_s27tupsc0esm0[17] = "cannot compare `";
const uint8_t gm_s2a85dvpmjp4u[14] = "{:.3} \302\265s/run";
const uint8_t gm_s2bumqed39p41[17] = "fn {} escapes {}";
const uint8_t gm_s2esennel4fsa[38] = "expected a value, found the type `{}`";
const uint8_t gm_s2idf31094gi9[8] = "sort_by";
const uint8_t gm_s2j5j1obh5p7l[6] = "loop ";
const uint8_t gm_s2kr3v1c3a77k[17] = "\000\000\000\000\000\000\000\000\021\000\000\000\000\000\000\000";
const uint8_t gm_s2kubr08sqj72[15] = "this assertion";
const uint8_t gm_s2m4r2316r14d[11] = "expected `";
const uint8_t gm_s2n2cdbc6724t[10] = "0x{:x}:{}";
const uint8_t gm_s2o9ogopuec6c[108] = "this performs `{}` inside a `with` that handles `{}`, which the effect-set parameter might include (\302\24716.6)";
const uint8_t gm_s2oqp92hnbubt[11] = "{} = a{}; ";
const uint8_t gm_s2vj6kgk19dag[32] = "(uint8_t)__builtin_isfinite({})";
const uint8_t gm_s3347hvco05di[6] = ") == ";
const uint8_t gm_s33hvuvcdjs1b[11] = "<argument>";
const uint8_t gm_s38t0si42sbdd[49] = ", (uintptr_t)(n));\012    if (GM_PTR_TAGGED(code)) ";
const uint8_t gm_s3bkmscn5fi56[3] = "*\077";
const uint8_t gm_s3duthvmnjluh[148] = "main :: fn() needs Out {{\012    print(\"hello from {}\\n\")\012}}\012\012double :: fn(n: i32) -> i32 => n * 2\012\012test \"double\" {{\012    expect_eq(double(21), 42)\012}}\012";
const uint8_t gm_s3ea3k3og84ri[9] = "needs {}";
const uint8_t gm_s3jllttnltak3[26] = "cannot read the archive: ";
const uint8_t gm_s3js9g3l46oiv[40] = "a constant without a compile-time value";
const uint8_t gm_s3ku3d0vbon9r[12] = "keyword {}\012";
const uint8_t gm_s3lqm9addfamn[56] = "if ({} == {}) gm_trap(\"integer overflow in `abs`\", {});";
const uint8_t gm_s3m12mc3q4gpj[71] = "the archive of dependency `{}` ({}) has the SHA-256 {}, but {} says {}";
const uint8_t gm_s3nhq46nogqdt[12] = "working_dir";
const uint8_t gm_s3p8gug7bvl1p[3] = "\\\\";
const uint8_t gm_s3tqkqetpkgjr[84] = "` cannot be spread: its elements are named by its variants, not positions (\302\2475.6.1)";
const uint8_t gm_s3vboaodpmi9a[4] = "-O0";
const uint8_t gm_s42atdm90l4do[14] = "an expression";
const uint8_t gm_s48n17gh1k2md[3] = "-u";
const uint8_t gm_s49ep90bjfcs6[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000I\002\000\000(\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_s4av1h975k9ok[47] = "a `using` field must have a struct type, not `";
const uint8_t gm_s4bcn9pd0h83n[48] = "internal error: formatted output does not lex: ";
const uint8_t gm_s4bf0cndbl9lv[3] = ")]";
const uint8_t gm_s4cess8gum6i7[6] = "float";
const uint8_t gm_s4gt1buiaiaiq[3] = "In";
const uint8_t gm_s4i1t9tcolakn[48] = "`{}` is not a C-compatible result type (\302\24719.1)";
const uint8_t gm_s4l3p0f0c6mmc[44] = " has no gamma.toml in its package directory";
const uint8_t gm_s4lj2bemc87jp[3] = "-L";
const uint8_t gm_s4n8c66u7mg94[19] = "{}((void *)&{}{});";
const uint8_t gm_s4o541hcfllua[9] = "{}|{}|{}";
const uint8_t gm_s4pv32nkh7r2r[11] = "; memcpy(&";
const uint8_t gm_s4rj8vs79ru3v[12] = "gm_utf8_arg";
const uint8_t gm_s4rugtmn8jarn[57] = "`, which the effect-set parameter might include (\302\24716.6)";
const uint8_t gm_s4uu8ali3icr8[40] = "`add` needs exactly one dependency name";
const uint8_t gm_s52vv4hfcfuh5[15] = "gm_win_cmdline";
const uint8_t gm_s57kk096d0ite[45] = "the variable `{}` is assigned but never read";
const uint8_t gm_s57qe8p5hq2v9[13] = "<em><strong>";
const uint8_t gm_s5dln0neuav6j[11] = "archive {}";
const uint8_t gm_s5epronuu4280[7] = ") & ~(";
const uint8_t gm_s5ku0alcfvlas[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\237\001\000\000\015\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000";
const uint8_t gm_s5lqhiu2bn6of[8] = "gamma: ";
const uint8_t gm_s5m8hf43qu2i7[27] = "` gives the package name `";
const uint8_t gm_s5m8rkq92eohf[82] = "a vector's lanes are `i8`\342\200\223`i64`, `u8`\342\200\223`u64`, `f32`, `f64` or `bool`, not `{}`";
const uint8_t gm_s5pra70i8o734[14] = "((uint8_t *)&";
const uint8_t gm_s5s1m7053mh3n[8] = "global ";
const uint8_t gm_s5sdmhb4570qm[20] = "` takes no receiver";
const uint8_t gm_s5sin3j2044cm[41] = "\000\000\000\000\000\000\000\000\027\000\000\000\000\000\000\000\322\000\000\000\"\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000";
const uint8_t gm_s5u5a7gd2hl9p[6] = " < 0)";
const uint8_t gm_s5udr3eget84q[6] = "Clamp";
const uint8_t gm_s5url3si4frr2[69] = "package `{}`: `checksum` must be `sha256:` and 64 hexadecimal digits";
const uint8_t gm_s654e61ni0n2t[67] = "    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto ";
const uint8_t gm_s65dghs61r7cp[13] = " bytes and `";
const uint8_t gm_s66hh83pieh4l[49] = "the cache is disabled (GAMMA_CACHE_DIR is empty)";
const uint8_t gm_s66ph0gfa95db[14] = "the builtin `";
const uint8_t gm_s67qasqqp59vt[513] = "\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_s688e8m4pgcda[6] = "block";
const uint8_t gm_s6aprmc3mqp0t[2] = "C";
const uint8_t gm_s6c1femb11dge[76] = "a pattern on the right of `is` cannot bind names; use `if pattern := value`";
const uint8_t gm_s6eeocplfbr8s[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000#\001\000\000\011\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000";
const uint8_t gm_s6g1vvoofsgo5[21] = "invalid tag value S(";
const uint8_t gm_s6hq50r4rk7e6[26] = "compiler/interp/frames.gm";
const uint8_t gm_s6kvnpuo6o71f[26] = "static const gm_type {};\012";
const uint8_t gm_s6lr92ekld44s[13] = "the literal ";
const uint8_t gm_s6m1a6s4q8cuh[50] = "`; the conventions are `.C`, `.SysV` and `.Win64`";
const uint8_t gm_s6n6v50inu7p0[8] = "slicing";
const uint8_t gm_s6nsj8sc6uhgg[17] = "`debug-info` in ";
const uint8_t gm_s6rmjmkjior9j[13] = "` is a label";
const uint8_t gm_s6tkmvprase06[14] = "gm_free_cstrs";
const uint8_t gm_s6u6c6o8c4lg0[5] = "&lt;";
const uint8_t gm_s6uch3l6osunv[50] = "a `using` field must have a struct type, not `{}`";
const uint8_t gm_s72l6okdj2d3g[95] = "\\n\")\012}\012\012double :: fn(n: i32) -> i32 => n * 2\012\012test \"double\" {\012    expect_eq(double(21), 42)\012}\012";
const uint8_t gm_s78m4481dj77r[9] = "sys_read";
const uint8_t gm_s78naq2blbmbt[32] = "union __attribute__((packed)) {";
const uint8_t gm_s7a0708p0cdgv[38] = " != 0) gm_trap_misaligned((uintptr_t)";
const uint8_t gm_s7a42e93eb2m6[15] = "perform_native";
const uint8_t gm_s7eippa38tft8[7] = "<code>";
const uint8_t gm_s7fpdr39r789h[15] = "((gm_closure){";
const uint8_t gm_s7h9qsm54pb7m[6] = "with ";
const uint8_t gm_s7jvhfqq2rri8[30] = "typedef {} {}{} {{ {} }} {};\012";
const uint8_t gm_s7knd69l0mhc0[36] = "`!` unwraps a result, but this is `";
const uint8_t gm_s7q4c8j1mnq1f[22] = "Read-only file system";
const uint8_t gm_s7qbqthgde8lk[29] = "`{}` is a label, not a value";
const uint8_t gm_s7uuha79fcmn5[18] = "Nothing to remove";
const uint8_t gm_s7vsso7k8tg8d[25] = " have different types: `";
const uint8_t gm_s7vv7qoquqmn9[14] = "gm_wasi_entry";
const uint8_t gm_s81l00c48rg4v[26] = " places needs a tuple of ";
const uint8_t gm_s827qun5pki1c[5] = "^\012{}";
const uint8_t gm_s82k80d9jrgm6[4] = "%{}";
const uint8_t gm_s84hftqv0533g[5] = " << ";
const uint8_t gm_s85jtgs7au5b5[9] = "HexUpper";
const uint8_t gm_s866ofni9sb8u[7] = "double";
const uint8_t gm_s87irmg2sqslv[33] = "                                ";
const uint8_t gm_s88u2nt2tl9hi[11] = "enum gm_dv";
const uint8_t gm_s8c49n38a2v0d[41] = "\000\000\000\000\000\000\000\000\033\000\000\000\000\000\000\000\246\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000";
const uint8_t gm_s8cq7ej9hnrr0[12] = "gm_fault_sp";
const uint8_t gm_s8hhhdlan8pqr[8] = "->cx = ";
const uint8_t gm_s8i3r4s519jok[8] = "<core>/";
const uint8_t gm_s8n2bs6jikgsm[4] = "// ";
const uint8_t gm_s8n858pjjep3e[10] = "and {} {}";
const uint8_t gm_s8pjrp7ublsfs[171] = "a loop variable or a pattern binding is a copy, whose storage is gone or reused after its scope (\302\24715.1); point into the original (`&items[i]`), or keep the value instead";
const uint8_t gm_s8rm42n03sfss[4] = "\"\"\"";
const uint8_t gm_s8sgunk80orlb[26] = "compiler/interp/consts.gm";
const uint8_t gm_s8vjpeo1vp60m[16] = "unknown --vcs `";
const uint8_t gm_s90aivk1p6or5[16] = "(*({} *)gm_out)";
const uint8_t gm_s92nfbpklold8[41] = "` is a url dependency; run `gamma fetch`";
const uint8_t gm_s95cbsclgo7gk[36] = "the literal {} does not fit in `{}`";
const uint8_t gm_s95gukpblkpua[33] = "`not` takes a `bool`, not `{}`{}";
const uint8_t gm_s97nfli622uqg[5] = "`: \012";
const uint8_t gm_s98novtk49ooh[28] = "static assertion failed: {}";
const uint8_t gm_s9cb6j5ok7bdk[3] = "\\r";
const uint8_t gm_s9e3360r5ee0o[7] = "inline";
const uint8_t gm_s9fseescjm8u4[25] = "`), which is not allowed";
const uint8_t gm_s9hi7i7cl3e2t[78] = "parameters of a type are always compile-time; declare them with `:`, not `::`";
const uint8_t gm_s9indmpqvpug4[58] = "` is a compile-time value parameter, but a type was given";
const uint8_t gm_s9jqgpofojve4[35] = "`@stack_size({})` has no bound: {}";
const uint8_t gm_s9kudvjpckcsm[10] = "({} % {})";
const uint8_t gm_s9o2c36nosv54[41] = "float to integer conversion out of range";
const uint8_t gm_s9oarmm2oe3id[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000J\001\000\000\035\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000";
const uint8_t gm_s9oo6ammv0sch[17] = "reached through ";
const uint8_t gm_s9rm2mbmo3nku[7] = "a type";
const uint8_t gm_sa29cuhpvu442[54] = "cannot use {}: No such file or directory (os error 2)";
const uint8_t gm_sa2e14pm9g0bb[17] = "\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000";
const uint8_t gm_sa7c621p9d7mr[13] = ".cache/gamma";
const uint8_t gm_sa7chm2nt7fsk[27] = "make the binding immutable";
const uint8_t gm_sa8gjfvck9d40[30] = ", .kind = GM_LIT, .clauses = ";
const uint8_t gm_sa8mtcad5rfmf[26] = "after the constraint name";
const uint8_t gm_sa8neoommnpov[24] = "static const uint8_t vs";
const uint8_t gm_sa9pve8r9407o[35] = "passing `{}` by value to or from C";
const uint8_t gm_saa0ue0fi38t8[15] = "Is a directory";
const uint8_t gm_sabd09jov3efr[32] = "    {} tg = GM_LD({}, v + {});\012";
const uint8_t gm_sabdpctdijia8[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000~\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_saeed1dnph0jk[52] = "a string pattern needs a `string`, but this is `{}`";
const uint8_t gm_sajhc6og5bv8v[59] = "invalid unicode escape: expected 1 to 6 hexadecimal digits";
const uint8_t gm_sajso5sujsruj[47] = "; the lanes of a mask are complemented by `~m`";
const uint8_t gm_san68r6np95il[6] = "<std>";
const uint8_t gm_saqh6vgbmf8m2[153] = "a result is not a union: match its error with `!e` (or `!.Variant`) and its success value with a plain pattern, as in `!e => \342\200\246` and `v => \342\200\246` (\302\2479.3)";
const uint8_t gm_sat80i3p7mq2f[26] = "TOML parse error at line ";
const uint8_t gm_saveva62qedei[8] = " + i * ";
const uint8_t gm_sb11lrfd85gub[14] = "` applies to ";
const uint8_t gm_sb22ajpcscr7v[41] = "#if defined(__clang__) || __GNUC__ >= 12";
const uint8_t gm_sb6smklmjl60o[7] = "detail";
const uint8_t gm_sb745qqv6kdrg[11] = "serverInfo";
const uint8_t gm_sb895t6tk3m9h[6] = ") || ";
const uint8_t gm_sb8i7j9s2n6jl[8] = "gm_cont";
const uint8_t gm_sb9bgabg66tgj[42] = " left the program failing to compile, so ";
const uint8_t gm_sba0bp6ej30io[30] = "internal error: fiber aborted";
const uint8_t gm_sba60r64q4l0i[31] = ")(-9223372036854775807LL - 1))";
const uint8_t gm_sbanjlvk3n14i[4] = "sat";
const uint8_t gm_sbb1ri5er9djq[4] = "get";
const uint8_t gm_sbdno1ojbbrbs[45] = "`{}` is not a git dependency of this package";
const uint8_t gm_sbgbo6kv938k3[19] = "relatedInformation";
const uint8_t gm_sbk9dggcntbu6[100] = "    gm_with_end(&{}, !(gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == {}), {});";
const uint8_t gm_sc0mj6111f7cc[10] = "pat-or : ";
const uint8_t gm_sc4hsc9atc69r[33] = "a package entry needs a string `";
const uint8_t gm_sc758i4r41ge5[128] = "    for (uintptr_t i = 0; i < na && i < nb; i++) {{ if (!{}(pa + i * {}, pb + i * {})) return {}(pa + i * {}, pb + i * {}); }}\012";
const uint8_t gm_sc7ertn8kmtl3[4] = "2.0";
const uint8_t gm_scbch1b3qo8hr[9] = "shutdown";
const uint8_t gm_scbj8d61i03he[14] = "GIT_WORK_TREE";
const uint8_t gm_scc54gv4a7k04[6] = "errno";
const uint8_t gm_scds626ikjji4[13] = "gm_mode_kind";
const uint8_t gm_scduvjunh0oei[9] = "len = {}";
const uint8_t gm_scgahod5hue9b[56] = "`expect_eq` takes the actual value and the expected one";
const uint8_t gm_schmrvl8mk83l[9] = "watchers";
const uint8_t gm_schvua5hkahp9[16] = "activeParameter";
const uint8_t gm_scj618tc62815[15] = "invalid number";
const uint8_t gm_scmgj2ccom9v4[40] = "every branch of this `if` does the same";
const uint8_t gm_scq06n33arhgv[5] = "`\", ";
const uint8_t gm_scqt00otrhg1t[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\017\001\000\000\025\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_scs28q27cdacb[10] = ", {} = {}";
const uint8_t gm_sd7cce7oc21kr[36] = "#pragma push_macro(\"{}\")\012#undef {}\012";
const uint8_t gm_sd7vgd2pm42gk[14] = "op_div_scalar";
const uint8_t gm_sdaknqn35hokt[9] = "download";
const uint8_t gm_sdam23bqvafqn[18] = "kind = GM_K_SLICE";
const uint8_t gm_sdbvtaeiovn59[102] = "if (({}) == 0 || (({}) & (({}) - 1)) != 0) gm_trap(\"allocation alignment is not a power of two\", {});";
const uint8_t gm_sdc36oketvdk3[4] = "op ";
const uint8_t gm_sdf1o8jga4hc6[38] = "the operation `{}` can only be called";
const uint8_t gm_sdg7s5c89ve6u[64] = "` names a type; a local may not have the name of a type (\302\2474.6)";
const uint8_t gm_sdgdb59na1854[23] = "the foreign variable `";
const uint8_t gm_sdhddtlu453l5[13] = "((__int128)(";
const uint8_t gm_sdhelph0q09tv[5] = "));\012";
const uint8_t gm_sdhns4s61djoc[41] = "\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000}\000\000\000\021\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_sdi3k3oc2kq9i[15] = "` is recursive";
const uint8_t gm_sdnh9bi444ml9[14] = "list_liveness";
const uint8_t gm_sdo1hnokt0mq8[17] = "lib/std/fs/fs.gm";
const uint8_t gm_sdqgviehbfoad[6] = ".rt.c";
const uint8_t gm_sdrbnq4aduai5[16] = "gm_clause_thunk";
const uint8_t gm_sdvp5f4riom9f[3] = "&a";
const uint8_t gm_se006k0nbfdeg[8] = "riscv64";
const uint8_t gm_se0plntg0il3g[14] = "->env = env; ";
const uint8_t gm_se2n4k927a8pv[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000{\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_se2qf1ipmcha3[26] = "No such file or directory";
const uint8_t gm_se3md9tm534o5[33] = "module {}\012  dir {}\012  package {}\012";
const uint8_t gm_se7kda46rm021[107] = "`needs not` is only for foreign functions (\302\24719.2); a Gamma function without `needs` already needs nothing";
const uint8_t gm_se8v3b7tfvt0e[23] = "this loop's value is `";
const uint8_t gm_sej6t944fna3a[61] = "the {} has no parameter named `{}` (supplied by this spread)";
const uint8_t gm_sekn24oquoo60[9] = "gm_frame";
const uint8_t gm_selatfetsf85i[10] = "range_len";
const uint8_t gm_sem2htac3j4np[66] = "` instantiates itself with ever-larger type arguments (more than ";
const uint8_t gm_sepmad5fu1s0n[8] = "--allow";
const uint8_t gm_ser747td9jt8d[41] = "variant `{}` needs a payload: `.{}(\342\200\246)`";
const uint8_t gm_sf3kc6ftec6at[166] = "gm_node {} = {{.eff = {}, .kind = GM_LIT, .clauses = {}, .env = (void *)&F.h, .hctx = {}, .with_frame = &F.h, .with_label = {}, .result = {}, .fw = {}, .next = {}}};";
const uint8_t gm_sf44a6nc03ff5[10] = "a complex";
const uint8_t gm_sf6pbim706kre[4] = "zig";
const uint8_t gm_sfali8nl85hc9[25] = "this `#bits` field value";
const uint8_t gm_sfbip2dg905u8[7] = "gm_veh";
const uint8_t gm_sfbr39r30psvt[4] = "v{}";
const uint8_t gm_sfc0aek8lt25a[5] = "gm_f";
const uint8_t gm_sfd0mn3sol25u[4] = "Abs";
const uint8_t gm_sfdhf0de18rq4[6] = "0.3.0";
const uint8_t gm_sfdj0c5vltira[31] = "`{}` has no element named `{}`";
const uint8_t gm_sfelk2428ha3h[19] = "| [`{}`]({}.md) | ";
const uint8_t gm_sfeo7g7huf2q1[17] = "a pointer offset";
const uint8_t gm_sfftb05vrc1qn[81] = "    uint64_t n = GM_LD(uintptr_t, v + {}); const uint8_t *d = GM_LD(void *, v);\012";
const uint8_t gm_sfhct1r6m1qqo[11] = "gm_ld{}_{}";
const uint8_t gm_sfjsmf1lebgp7[14] = "parse_pattern";
const uint8_t gm_sfk3r5va3vobm[5] = ".env";
const uint8_t gm_sfl0j5tlur0e6[55] = "a function literal is not a value to format (\302\24721.6.1)";
const uint8_t gm_sfna4d5mnmoa8[30] = "merge the `else` and the `if`";
const uint8_t gm_sfpeqbitcbq42[6] = "-tvzf";
const uint8_t gm_sfr85ju00e9hk[138] = "a line that starts with `.name` continues the previous line (\302\2472.8); to begin a new expression with a variant, name its type: `T.Variant`";
const uint8_t gm_sfusuld9q5ekv[44] = "`, so it cannot be a type argument (\302\24716.1)";
const uint8_t gm_sg1s2lngtt92k[13] = "; if (ml == ";
const uint8_t gm_sg2r3pcbg3kr2[32] = "    {} r = {}(NULL, cx, NULL);\012";
const uint8_t gm_sg550i4fp569j[55] = "digit separators `_` must be single and between digits";
const uint8_t gm_sg5n0glcljcci[33] = "gm_format_spec(&{}, {}, {}, {});";
const uint8_t gm_sg7of7eks8a5u[7] = "method";
const uint8_t gm_sg7ronlh5k7js[84] = "; `free` releases a slice from `make`: release a value from `new` with `destroy(p)`";
const uint8_t gm_sgb12qbh3gevi[5] = "[i];";
const uint8_t gm_sgc7crh43q9tj[6] = "` in ";
const uint8_t gm_sgcku4bq4407l[19] = "`; use one of them";
const uint8_t gm_sgcporqt2gqqg[16] = " deep) (\302\24716.7)";
const uint8_t gm_sgepo3aen3iei[15] = "gm_susp_unlink";
const uint8_t gm_sgfrcimthamdn[10] = "__s390x__";
const uint8_t gm_sgfufhs3psibd[4] = "Exp";
const uint8_t gm_sgh1o65rai9bu[8] = "bits = ";
const uint8_t gm_sghh3jr7dlapd[66] = "the template has {}, but {} given (in an instantiation used here)";
const uint8_t gm_sgi6la00bpjt2[15] = "`{}` is a copy";
const uint8_t gm_sgisth1q9gntc[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000y\000\000\0000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_sgmn23so20vv9[33] = "<p>{}:</p>\012<dl class=\"members\">\012";
const uint8_t gm_sgq8hfs47hogb[9] = "(a, b);\012";
const uint8_t gm_sgrdu6jn9jtpr[35] = " does not go to an enclosing block";
const uint8_t gm_sgukm1evmrjal[4] = "\", ";
const uint8_t gm_sh00ruc3m501e[42] = "cannot infer the result type of `!e` here";
const uint8_t gm_sh16qm7deqse5[7] = "{} a{}";
const uint8_t gm_sh4r0fq5alj61[14] = "needless-bool";
const uint8_t gm_sh57gpir0vvh2[24] = "signed integer or float";
const uint8_t gm_shbaullahutpe[37] = "a tuple type cannot contain a spread";
const uint8_t gm_shda201svb9eg[119] = "field `{}` of a `#bits` struct must be `bool`, an integer, an enum with an explicit tag type or another `#bits` struct";
const uint8_t gm_shh2fi8ibtel0[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\030\001\000\000\016\001\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_shhtc33mlfd4u[31] = "the name of a foreign function";
const uint8_t gm_shhugap3t1rkg[41] = "`#soa` needs a struct with fields, and `";
const uint8_t gm_shl6ef6if4a86[80] = "function returning `{}` can reach the end of its body without returning a value";
const uint8_t gm_shm91i3u3uv6p[5] = "\334\005\000\000";
const uint8_t gm_shnp9b6k2mab8[5] = "list";
const uint8_t gm_shrc3j552p60m[9] = "SeekFrom";
const uint8_t gm_shrq42dntcbrd[19] = "kind = GM_K_RESULT";
const uint8_t gm_shukorb0nk4kg[7] = "triple";
const uint8_t gm_si413smv36ejf[7] = "</{}>\012";
const uint8_t gm_sidpdvffdra02[14] = "out of memory";
const uint8_t gm_sif59l16kubo3[17] = "`{}` requires {}";
const uint8_t gm_sif5vtcq0ji03[17] = "\000\000\000\000\000\000\000\000J\002\000\000\000\000\000\000";
const uint8_t gm_sig2adm7e9t55[79] = "the template numbers its values, so this spread must be a tuple of them, not `";
const uint8_t gm_sighahslvsc1i[11] = "` (\302\24712.7)";
const uint8_t gm_sigup1gvr7a13[5] = " && ";
const uint8_t gm_sihdt3ld7f53j[16] = "GIT_INDEX_FILE=";
const uint8_t gm_sihs6d097ocev[5] = "Rand";
const uint8_t gm_sikpqtja8pqpv[3] = "}\012";
const uint8_t gm_siooblm685vmo[16] = "self-comparison";
const uint8_t gm_siotufd8s66oj[21] = "{} passed, {} failed";
const uint8_t gm_siqfg68f4ud6j[16] = " gm_r = F.ret; ";
const uint8_t gm_siqpmbbs844ch[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\246\001\000\000\034\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_sj3ug62t7s271[14] = "%{} = addr {}";
const uint8_t gm_sj4cbut2umg1f[35] = "compile-time evaluation failed: {}";
const uint8_t gm_sj7cc74b29dv3[14] = "WASI_SDK_PATH";
const uint8_t gm_sj9t4d2ku5m56[47] = "this handler has no clause for the operation `";
const uint8_t gm_sjac221bnk353[8] = " #bits(";
const uint8_t gm_sjb8gf8n3vsft[42] = "this continuation needs a value of type `";
const uint8_t gm_sjbhqu0o8ebub[6] = "tests";
const uint8_t gm_sjdk7e5fs4top[6] = ") >= ";
const uint8_t gm_sje4rbc88csdu[7] = "X86_64";
const uint8_t gm_sjgrq0dv175qo[30] = "calls to foreign functions (`";
const uint8_t gm_sjil3d717dq6h[63] = "a format spec has either `0` padding or an alignment, not both";
const uint8_t gm_sjlktf1otofg9[10] = "new_frame";
const uint8_t gm_sjmhgc2b6rnks[10] = "(void *)&";
const uint8_t gm_sjpr4fvui63qj[13] = "(uint64_t){}";
const uint8_t gm_sjqp7nokhg4lk[5] = "); }";
const uint8_t gm_sjrcmga748cdu[24] = "compiler/interp/node.gm";
const uint8_t gm_sjtfkltao8c2o[49] = "internal error: a `ctl` clause finished normally";
const uint8_t gm_sjvb17co9jlff[15] = "<li><a href=\"#";
const uint8_t gm_sk01103sqa2lh[36] = "GM_ST({}, (uint8_t *)&{} + {}, {});";
const uint8_t gm_sk0qegfrnee00[4] = "{}u";
const uint8_t gm_sk25jsmieu0fo[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\223\000\000\0001\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_sk2nrl11c01gj[20] = "cannot write {}: {}";
const uint8_t gm_skbgm82aqj3q0[6] = "Fence";
const uint8_t gm_skq2uj9oi1243[41] = "internal error: no driver for this fiber";
const uint8_t gm_skrdl5pdcve8g[43] = "(gm_node *cx, const void *d, uintptr_t n, ";
const uint8_t gm_skv53e6m2c88s[10] = "a literal";
const uint8_t gm_sl5jkfh6104lp[5] = "file";
const uint8_t gm_sl7doakbaoimv[7] = "this `";
const uint8_t gm_sld9l7nonn4o9[17] = " && env != NULL;";
const uint8_t gm_slgbfnvgul4q1[7] = "\"\"\"\012{}";
const uint8_t gm_slhsp4invn4vh[7] = " of a ";
const uint8_t gm_slmlefe4cpf5e[15] = "(..x)` (\302\2476.5)";
const uint8_t gm_sloeu62tmfgqn[9] = "case {}:";
const uint8_t gm_slopv2r8r4od2[5] = "<li>";
const uint8_t gm_sltejl1fekbag[6] = ", &ev";
const uint8_t gm_sltjg0sbhbo73[13] = "static bool ";
const uint8_t gm_slu6nnvsgea2t[41] = "extern {}{} {}({}) __asm__(GM_SYM({}));\012";
const uint8_t gm_slu92ianijdhp[7] = "deinit";
const uint8_t gm_sm56lhobvb6g5[6] = "names";
const uint8_t gm_sm7179tmr0nhi[27] = "this compile-time argument";
const uint8_t gm_sm7hp6cgh4jle[6] = "write";
const uint8_t gm_sm8uf6b4m0n7p[8] = "unit-{}";
const uint8_t gm_sma6ellkcmke6[26] = "` are not ordered (\302\2477.5)";
const uint8_t gm_smacop0e0umsg[257] = "\230/\212B\221D7q\317\373\300\265\245\333\265\351[\302V9\361\021\361Y\244\202\077\222\325^\034\253\230\252\007\330\001[\203\022\276\2051$\303}\014Ut]\276r\376\261\336\200\247\006\334\233t\361\233\301\301i\233\344\206G\276\357\306\235\301\017\314\241\014$o,\351-\252\204tJ\334\251\260\\\332\210\371vRQ>\230m\3061\250\310'\003\260\307\177Y\277\363\013\340\306G\221\247\325Qc\312\006g))\024\205\012\267'8!\033.\374m,M\023\0158STs\012e\273\012jv.\311\302\201\205,r\222\241\350\277\242Kf\032\250p\213K\302\243Ql\307\031\350\222\321$\006\231\326\2055\016\364p\240j\020\026\301\244\031\010l7\036LwH'\265\274\2604\263\014\0349J\252\330NO\312\234[\363o.h\356\202\217toc\245x\024x\310\204\010\002\307\214\372\377\276\220\353lP\244\367\243\371\276\362xq\306";
const uint8_t gm_smaqn3dnro8sl[14] = "clang version";
const uint8_t gm_smb8nsuj9ie93[15] = "untyped-float ";
const uint8_t gm_smbgfcmcr0r8n[11] = "` (\302\24717.4)";
const uint8_t gm_smbls870q1nm0[16] = "integer literal";
const uint8_t gm_smhjrt0kub63u[15] = "gm_stack_limit";
const uint8_t gm_smk6fmrgs1hnc[6] = "gm_hl";
const uint8_t gm_smpde4t8bsefo[14] = ", kind: {} }}";
const uint8_t gm_smpmbscbko3gk[4] = "mul";
const uint8_t gm_smqhtg33814q9[23] = "this assertion message";
const uint8_t gm_smror2m9tsk7v[8] = "branch=";
const uint8_t gm_sn275ucdkgsk6[82] = "static {} {};\012static int gm_xt{}(gm_node *cx) {{ (void)cx; {} = {}; return 0; }}\012";
const uint8_t gm_sn35ih51dmdii[45] = "` takes no arguments and is written once: `#";
const uint8_t gm_sn4utba4jl66l[9] = "rev `{}`";
const uint8_t gm_snbb2gfh3k17u[26] = "message body is too large";
const uint8_t gm_sncbtqrmp1734[14] = "`!` on values";
const uint8_t gm_snjq35j2j9d3c[113] = "`{}` and `{}` might be the same effect for some type arguments, so performing either would be ambiguous (\302\24716.6)";
const uint8_t gm_so05t2goj6895[24] = "])(n->env, n->hctx, n, ";
const uint8_t gm_so2ihtmk3e8kf[17] = " is matched here";
const uint8_t gm_so43rnba8j5pb[35] = "(({})(({}) & ((({})1 << {}) - 1)))";
const uint8_t gm_so44j4cr69vhs[16] = "missing newName";
const uint8_t gm_so5kmn66hbf25[22] = "print the syntax tree";
const uint8_t gm_so686pdrsalfh[587] = "Enter an expression to print its value, a statement or a definition to add it to the session.\012An input continues on the next line while brackets are open or it is incomplete.\012\012  :help             this text (:h, :\077)\012  :quit             leave (:q, :exit; so does end of input)\012  :type <expr>      (:t) the type of an expression, which is not run unless it must be\012  :list             the definitions and variables of the session\012  :load <file>      add the declarations of a file\012  :reset            forget everything\012  :cancel           (on a continuation line) discard the input so far\012";
const uint8_t gm_so77flgm8klar[52] = "this `match` is not exhaustive: `{}` is not covered";
const uint8_t gm_so7qmna0fo4so[6] = "#line";
const uint8_t gm_so7svtiv1a8q1[5] = "h = ";
const uint8_t gm_so81f3f3mbv7o[54] = "string values must be quoted, expected literal string";
const uint8_t gm_soah6d9unb7uo[32] = "static GM_HUGE_FN __attribute__";
const uint8_t gm_sobecpce3c56n[35] = "`,` or a newline after the variant";
const uint8_t gm_soechff4t3q96[9] = "url = {}";
const uint8_t gm_sog2vuuvk361m[22] = "    return {}(a, b);\012";
const uint8_t gm_soh6rol6f5r5k[27] = "invalid unicode code point";
const uint8_t gm_sokol7c4qt12p[6] = ", 0, ";
const uint8_t gm_sokruug51vi6k[11] = "gm_rt_argc";
const uint8_t gm_soogmlptfncs1[12] = "build_write";
const uint8_t gm_soreld657hea2[25] = "a number, for `+` or `0`";
const uint8_t gm_sos7rilr21per[7] = "rename";
const uint8_t gm_sottk452c14qm[56] = "`#link_name` takes the symbol name: `#link_name(\"sym\")`";
const uint8_t gm_sp13ju5l53585[24] = "replace the `if` with `";
const uint8_t gm_sp21r812mg46a[4] = "///";
const uint8_t gm_sp4l42tpo6btg[11] = "refs/heads";
const uint8_t gm_sp5hf74268mcu[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000\024\000\000\000\207\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_sp625mljmvm7d[11] = "parse_item";
const uint8_t gm_sp68fbgfk0baj[39] = "    if (a[{}] != b[{}]) return false;\012";
const uint8_t gm_sp6f2rgoi8toe[7] = "cflags";
const uint8_t gm_sp75410cl55v2[13] = "enum gm_dv{}";
const uint8_t gm_sp7ec3ksfdilk[5] = ".git";
const uint8_t gm_sp8jfj95g4f47[7] = " = {}\012";
const uint8_t gm_spa5q1834e71s[49] = "); gamma.lock was edited or the cached checkout ";
const uint8_t gm_spajg3q27hr58[15] = ") / (intptr_t)";
const uint8_t gm_spdntfv7vkd2t[17] = "GM_TEMP_BYTES);\012";
const uint8_t gm_spg4d1df6jnji[7] = "type `";
const uint8_t gm_spj5hvik290iq[41] = "    return GM_LD({}, a) < GM_LD({}, b);\012";
const uint8_t gm_spj9rva8kb5ar[3] = " {";
const uint8_t gm_spjfgtrrvac57[14] = "cannot read `";
const uint8_t gm_splv67uudsbdi[23] = "  note: {} ({}:{}:{})\012";
const uint8_t gm_spm4139orpkvm[10] = ") return ";
const uint8_t gm_spp0mb1aidgi1[13] = " :: import \"";
const uint8_t gm_spskd7qirpoe3[25] = "expected a type argument";
const uint8_t gm_sq05bfmao5v8n[47] = "gm_fprop:\012    gm_fiber_finish(fb);\012#undef F\012}\012";
const uint8_t gm_sq1rqv8ou3vsv[14] = "release_facts";
const uint8_t gm_sq3jr80otu7tj[3] = ")~";
const uint8_t gm_sq3oimi1i4re9[27] = "memset(&{}, 0, sizeof {});";
const uint8_t gm_sq5ab39u4btdn[41] = "br @{} does not go to an enclosing block";
const uint8_t gm_sq5p8t05ccn5h[24] = "expected an effect name";
const uint8_t gm_sq74a8ckacjn2[41] = "`{}` is not a C-compatible type (\302\24719.1)";
const uint8_t gm_sq99f4u8gmi6n[28] = "\012Next steps:\012    gamma run ";
const uint8_t gm_sqa6gfa8omk7o[4] = "i32";
const uint8_t gm_sqc2or5f1575b[31] = "gm_mix_float(h, (double)GM_LD(";
const uint8_t gm_sqcj2s8m7mpqj[38] = "(uint8_t)(__builtin_signbit({}) != 0)";
const uint8_t gm_sqdjjj1odavib[34] = "`continue` can only target a loop";
const uint8_t gm_sqdvt3ooptrnc[5] = "_i, ";
const uint8_t gm_sqeho958d04c0[17] = "gm_thread_detach";
const uint8_t gm_sqen1asili489[52] = "tag value {} of `{}` does not fit the tag type `{}`";
const uint8_t gm_sqfc0qpl56rfm[7] = "length";
const uint8_t gm_sqh06nbksvl8i[17] = "package `{}`: {}";
const uint8_t gm_sqjlosotosjfp[18] = ")) return false;\012";
const uint8_t gm_sql5634qudrlq[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\0006\002\000\000\017\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_sqnathr6pbque[38] = "{} (\302\261 {:.1}%, {} samples of {} runs)";
const uint8_t gm_sqo6bs5b6kae9[8] = "gm_with";
const uint8_t gm_sqqtsglgb4lu3[28] = "cannot ask the C compiler `";
const uint8_t gm_sqtgbqlca4c1g[25] = "gm_asyncify_start_unwind";
const uint8_t gm_squ1rl0dbtrtr[59] = "the handler of `{}` performed here is not known statically";
const uint8_t gm_sr3uqaosat41v[41] = "\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000=\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_sr499ju9p7ks2[23] = "pre-release identifier";
const uint8_t gm_sr899mamktpvo[7] = "uninit";
const uint8_t gm_srej05ml2urca[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000}\001\000\000\014\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_sreprot2s508e[3] = ":q";
const uint8_t gm_srfcp96u5v1ui[37] = "`@hash` of this type at compile time";
const uint8_t gm_srhrn3fa70ni4[38] = "__atomic_load_n({}, __ATOMIC_RELAXED)";
const uint8_t gm_sri6fceeqvrql[19] = "\012pub arch :: Arch.";
const uint8_t gm_srj5ohb3f61b7[14] = "gm_fiber_slab";
const uint8_t gm_srj9vrlard0qi[14] = "#soa []const ";
const uint8_t gm_srlpdrobo1i0e[38] = "the function has no parameter named `";
const uint8_t gm_srp6tpaiaeunu[8] = "->env, ";
const uint8_t gm_srplcc0p49iui[9] = "contents";
const uint8_t gm_srq5utruoa2v3[28] = " *)req; gm_node *n = r->n; ";
const uint8_t gm_srru5vqet7hl3[11] = "instrument";
const uint8_t gm_srssp1uhlkrll[20] = "expected `,` or `}`";
const uint8_t gm_srtm6s969i45s[22] = "`@hash` of this value";
const uint8_t gm_sru7itami12tv[8] = " len {}";
const uint8_t gm_ss2v2ursd8pms[4] = "n{}";
const uint8_t gm_ss42hvrprenkk[26] = "; { gm_node *f = gm_find(";
const uint8_t gm_ss558hlhv1kah[31] = "` is not a valid C symbol name";
const uint8_t gm_ss66qg3t949dp[5] = "\320\007\000\000";
const uint8_t gm_ssc5ahdetbl48[17] = "gm_altstack_init";
const uint8_t gm_sse16dr2qj0im[3] = "pr";
const uint8_t gm_ssfg877iegnma[4] = "- `";
const uint8_t gm_ssga7g3v3nnl7[10] = "benchmark";
const uint8_t gm_ssj8r83l87dr2[16] = "fs_set_modified";
const uint8_t gm_ssk8mqd1u6nn7[52] = "`in` needs a range, array, slice or tuple, not `{}`";
const uint8_t gm_sslm2oq18gln3[6] = " (\302\261 ";
const uint8_t gm_ssmvuscdpkf2v[41] = "\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000\216\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_sso6pgmtg5r11[3] = "(!";
const uint8_t gm_ssoop4uttr1t0[43] = "struct {} *gm_Fp = (struct {} *)fb->frame;";
const uint8_t gm_ssq0semhq7h0c[13] = "gm_sys_write";
const uint8_t gm_ssqt22rph287b[4] = "i64";
const uint8_t gm_ssrcn9mu9bp3e[19] = "gm_scratch_release";
const uint8_t gm_ssv5povjldd7c[3] = ")\012";
const uint8_t gm_st3c9ev3jkdrf[32] = "{}({}.code, {}.env, {}, {}, {})";
const uint8_t gm_st803nqm7iok0[17] = "to close `cont(`";
const uint8_t gm_st9f93lco88fg[3] = "/.";
const uint8_t gm_sta2ai87l17np[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\263\001\000\000`\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_stafskn6bv24f[2] = "d";
const uint8_t gm_ste1ujg4tll4q[10] = "gm_thread";
const uint8_t gm_stevggi5tvq7c[18] = "instantiated here";
const uint8_t gm_stlhqhje21tdn[97] = "\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\024\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000 \000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000";
const uint8_t gm_stmg18u2tvso0[18] = "gm_frame_overflow";
const uint8_t gm_sto310q1gi1de[12] = "underscore\012";
const uint8_t gm_sto6ssj6r66e3[30] = "conflicting requirements for ";
const uint8_t gm_sto8li44rv2ou[8] = "a tuple";
const uint8_t gm_stshdb5m6rl63[5] = "] \077 ";
const uint8_t gm_stsr1tpfadro2[15] = "/proc/self/exe";
const uint8_t gm_stu0uvs5p21oc[17] = "cannot read CC: ";
const uint8_t gm_stugmrojdno85[6] = "const";
const uint8_t gm_stvci3fthpt8h[47] = "gm_trap(\"no match arm matched the scrutinee\", ";
const uint8_t gm_su41i9n9tujml[14] = "gm_next_entry";
const uint8_t gm_sua0n2tdbtos7[40] = "excessive number of version comparators";
const uint8_t gm_sub7l8h1sjc33[12] = "` is used (";
const uint8_t gm_sucoj11hr978o[14] = "the operation";
const uint8_t gm_sue4ionbe9lgh[21] = "compiler/eval/ops.gm";
const uint8_t gm_suftl0kath3th[21] = "), GM_LD(uintptr_t, ";
const uint8_t gm_sujqb5peare4g[6] = "not (";
const uint8_t gm_suklu1i9clb5l[48] = "invalid memory access: null pointer dereference";
const uint8_t gm_summega2vonc9[6] = "{}{}\012";
const uint8_t gm_suqsoolo29or4[24] = "ratio: zero denominator";
const uint8_t gm_suuq26jjqhul4[7] = "int8_t";
const uint8_t gm_suv60trdc5vnn[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000W\001\000\000\033\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_suvpcmvpe5152[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000m\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_sv2qh30031o3v[5] = "tir ";
const uint8_t gm_sv397f5vej3g7[30] = "cannot compare `{}` with `{}`";
const uint8_t gm_sv44r8pinka4e[13] = "needless-pub";
const uint8_t gm_sv8lr20bqpljh[12] = "`{}` clause";
const uint8_t gm_svbftsm94r0bv[7] = "(\342\200\246)`";
const uint8_t gm_svdodmni8et3b[15] = "this condition";
const uint8_t gm_sveguffvp7vd6[15] = ")GM_PTR_UNTAG(";
const uint8_t gm_svfn64lgobnrm[48] = "`: a method call must find exactly one (\302\2477.10)";
const uint8_t gm_svi6c6nnkemmt[13] = "subdir = {}\012";
const uint8_t gm_svkk03nujpn4k[8] = "gm_pow_";
const uint8_t gm_svl13be5ca8rt[5] = "Wasi";
const uint8_t gm_svpf3kbht0fr3[14] = "GM_COVER({});";
const uint8_t gm_svs91tvrok38m[7] = " = 0; ";
const uint8_t gm_svutc5hf6cgiv[2] = "e";
const uint8_t gm_svvf5qhjb1oan[11] = "debug-only";
uint8_t gm_c1l7393q60j[4] __attribute__((aligned(1)));
uint8_t gm_c1oqisgjqh8[20] __attribute__((aligned(1)));
uint8_t gm_c23mu1onplu[3] __attribute__((aligned(1)));
uint8_t gm_c24m5jfl30g[12] __attribute__((aligned(1)));
uint8_t gm_c2f5b0m05j6[11] __attribute__((aligned(1)));
uint8_t gm_c30d0vfr4ha[8] __attribute__((aligned(1)));
uint8_t gm_c3cpugekfrb[11] __attribute__((aligned(1)));
uint8_t gm_c40fejj7lfq[17] __attribute__((aligned(1)));
uint8_t gm_c44l6ho119d[65] __attribute__((aligned(1)));
uint8_t gm_c4dbb3oj7ol[7] __attribute__((aligned(1)));
uint8_t gm_c4tbnbi68ao[320] __attribute__((aligned(8)));
uint8_t gm_c55218gtcr0[448] __attribute__((aligned(8)));
uint8_t gm_c5dhf54lj9h[1] __attribute__((aligned(1)));
uint8_t gm_c5id9hnno81[12] __attribute__((aligned(1)));
uint8_t gm_c5led8t7730[39] __attribute__((aligned(1)));
uint8_t gm_c5lrgm5j673[3] __attribute__((aligned(1)));
uint8_t gm_c7svj0uc8qe[4] __attribute__((aligned(1)));
uint8_t gm_c8b3omj6k2u[14] __attribute__((aligned(1)));
uint8_t gm_c8d44ct21t1[13] __attribute__((aligned(1)));
uint8_t gm_c9b4lh7gft4[4] __attribute__((aligned(4)));
uint8_t gm_ca2pc57d5g4[5] __attribute__((aligned(1)));
uint8_t gm_cbbu3qvtp3q[8] __attribute__((aligned(8)));
uint8_t gm_cblhiiedl5v[14] __attribute__((aligned(1)));
uint8_t gm_cfaubb414id[18] __attribute__((aligned(1)));
uint8_t gm_cga7n0g3amu[8] __attribute__((aligned(8)));
uint8_t gm_cgd1khc5sal[24] __attribute__((aligned(8)));
uint8_t gm_ch3g3buh6cp[16] __attribute__((aligned(8)));
uint8_t gm_ch6j88vtkil[2] __attribute__((aligned(1)));
uint8_t gm_ci3gn3427mq[12] __attribute__((aligned(1)));
uint8_t gm_ci51f79gd9k[8] __attribute__((aligned(1)));
uint8_t gm_ck0dn7th4er[38] __attribute__((aligned(1)));
uint8_t gm_cka88d5t8lu[16] __attribute__((aligned(8)));
uint8_t gm_ckpdl03lgov[16] __attribute__((aligned(1)));
uint8_t gm_ckpsekc86jv[18] __attribute__((aligned(1)));
uint8_t gm_ckus03er382[4] __attribute__((aligned(4)));
uint8_t gm_cl03rhutns3[112] __attribute__((aligned(8)));
uint8_t gm_clacg9fr6j2[58] __attribute__((aligned(1)));
uint8_t gm_clam38tqp81[14] __attribute__((aligned(1)));
uint8_t gm_clluf6b4idl[15] __attribute__((aligned(1)));
uint8_t gm_cmn023hbeik[8] __attribute__((aligned(1)));
uint8_t gm_cmq9in3i5gq[3] __attribute__((aligned(1)));
uint8_t gm_cmu8f7egfr6[2] __attribute__((aligned(1)));
uint8_t gm_coj5ig90obd[5] __attribute__((aligned(1)));
uint8_t gm_cojk58d4vkf[16] __attribute__((aligned(8)));
uint8_t gm_cotbkjp0ndq[17] __attribute__((aligned(1)));
uint8_t gm_cp10508qfq2[9] __attribute__((aligned(1)));
uint8_t gm_cplet0icpi8[13] __attribute__((aligned(1)));
uint8_t gm_cqmh9ctmru3[8] __attribute__((aligned(1)));
uint8_t gm_cqpdkhks98h[10] __attribute__((aligned(1)));
uint8_t gm_cr1c8sm70hn[14] __attribute__((aligned(1)));
uint8_t gm_cr443kab48n[13] __attribute__((aligned(1)));
uint8_t gm_crjl9v1c7hv[4] __attribute__((aligned(1)));
uint8_t gm_csoii12j71c[5] __attribute__((aligned(1)));
uint8_t gm_ctcahh0irb8[1] __attribute__((aligned(1)));
uint8_t gm_cu9odmhbpt4[18] __attribute__((aligned(1)));
uint8_t gm_cundh4q6nta[11] __attribute__((aligned(1)));
uint8_t gm_k75gp9b9r49[40] __attribute__((aligned(8)));
uint8_t gm_k8k7i06ilpr[40] __attribute__((aligned(8)));
uint8_t gm_k9o9iqv20c0[40] __attribute__((aligned(8)));
uint8_t gm_kaa2flncdji[40] __attribute__((aligned(8)));
uint8_t gm_kbeesj1a2or[40] __attribute__((aligned(8)));
uint8_t gm_kbng43moaud[40] __attribute__((aligned(8)));
uint8_t gm_kdjk0ti7atv[40] __attribute__((aligned(8)));
uint8_t gm_kfnccdpc4g8[40] __attribute__((aligned(8)));
uint8_t gm_kgccnm9nmdk[40] __attribute__((aligned(8)));
uint8_t gm_kl3hhlfsksr[40] __attribute__((aligned(8)));
uint8_t gm_klc17hcqkii[40] __attribute__((aligned(8)));
uint8_t gm_klr1ounjj8d[40] __attribute__((aligned(8)));
uint8_t gm_kr7fqi9m0ih[40] __attribute__((aligned(8)));
uint8_t gm_krlms0mdefs[40] __attribute__((aligned(8)));
uint8_t gm_krsahem6qn0[40] __attribute__((aligned(8)));
uint8_t gm_ks0d3nb0eoq[40] __attribute__((aligned(8)));
uint8_t gm_ksi8gqve35e[40] __attribute__((aligned(8)));
uint8_t gm_ksnfevokhod[40] __attribute__((aligned(8)));
uint8_t gm_kv17k6a87tq[40] __attribute__((aligned(8)));
uint8_t gm_kvu1vd24o16[40] __attribute__((aligned(8)));
void *const gm_hl6hnpe3vbk7[] = {(void *)gm_f_for_body_v43n3ub};
void *const gm_hlh12hh1mjda[] = {(void *)gm_f_for_body_ivf5vgl};
void *const gm_hli2tfvgok6p[] = {(void *)gm_f_for_body_tprkpoe};
void *const gm_hlvanl0isvqe[] = {(void *)gm_f_for_body_5dma72c};

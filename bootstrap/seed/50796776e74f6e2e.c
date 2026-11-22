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
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; uint8_t v2; gm_t9qkh0rsooc8d v3; gm_t9qkh0rsooc8d v4; gm_tcc1rq4n1avb2 v5; uint32_t v6; uint32_t v7; } f1; } gm_t8588ma2cr9d7;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tk9n3vlila9qg;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; gm_b16_8 f3; } gm_t4rau9abddl2u;
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t8588ma2cr9d7) == 12 && _Alignof(gm_t8588ma2cr9d7) == 4 && offsetof(gm_t8588ma2cr9d7, f0) == 0 && offsetof(gm_t8588ma2cr9d7, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_t88rcucg1b7uk f2; uint64_t f3; void * f4; gm_tkn42u7na3t81 f5; uint64_t f6; void * f7; gm_t9qkh0rsooc8d f8; uint8_t p9[7]; void * f9; } gm_tjcptv9sagfq8;
_Static_assert(sizeof(gm_tk9n3vlila9qg) == 40 && _Alignof(gm_tk9n3vlila9qg) == 8 && offsetof(gm_tk9n3vlila9qg, f0) == 0 && offsetof(gm_tk9n3vlila9qg, f1) == 16 && offsetof(gm_tk9n3vlila9qg, f2) == 32, "layout of `(lib: string, link_name: string, variadic: bool)`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
typedef struct { gm_tkaapcii86udk f0; uint64_t f1; } gm_to6glrduves6o;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { uint64_t f0; gm_b16_8 f1; gm_t88rcucg1b7uk f2; uint8_t p3[4]; uint64_t f3; void * f4; uint8_t f5; uint8_t p6[7]; gm_th2lqbo77pvsc f6; gm_tf5srh4mp4b3i f7; void * f8; } gm_tq7qjpkmqmc5s;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tk9n3vlila9qg v0; } f1; } gm_tq9qng821udh4;
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t4rau9abddl2u) == 104 && _Alignof(gm_t4rau9abddl2u) == 8 && offsetof(gm_t4rau9abddl2u, f0) == 0 && offsetof(gm_t4rau9abddl2u, f1) == 8 && offsetof(gm_t4rau9abddl2u, f2) == 48 && offsetof(gm_t4rau9abddl2u, f3) == 88, "layout of `SiteConst`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { unsigned __int128 v0; double v1; uint8_t v2; void * v3; gm_tkaapcii86udk v4; gm_to6glrduves6o v5; void * v6; void * v7; void * v8; gm_ttggq7ng4kp3p v9; uint32_t v10; gm_ttggq7ng4kp3p v11; gm_ttggq7ng4kp3p v12; } f1; } gm_t6evimt84lndq;
_Static_assert(sizeof(gm_tjcptv9sagfq8) == 88 && _Alignof(gm_tjcptv9sagfq8) == 8 && offsetof(gm_tjcptv9sagfq8, f0) == 0 && offsetof(gm_tjcptv9sagfq8, f1) == 16 && offsetof(gm_tjcptv9sagfq8, f2) == 28 && offsetof(gm_tjcptv9sagfq8, f3) == 32 && offsetof(gm_tjcptv9sagfq8, f4) == 40 && offsetof(gm_tjcptv9sagfq8, f5) == 48 && offsetof(gm_tjcptv9sagfq8, f6) == 56 && offsetof(gm_tjcptv9sagfq8, f7) == 64 && offsetof(gm_tjcptv9sagfq8, f8) == 72 && offsetof(gm_tjcptv9sagfq8, f9) == 80, "layout of `ConstDef`");
_Static_assert(sizeof(gm_to6glrduves6o) == 32 && _Alignof(gm_to6glrduves6o) == 8 && offsetof(gm_to6glrduves6o, f0) == 0 && offsetof(gm_to6glrduves6o, f1) == 24, "layout of `(start: Place, len: u64)`");
_Static_assert(sizeof(gm_tq7qjpkmqmc5s) == 104 && _Alignof(gm_tq7qjpkmqmc5s) == 8 && offsetof(gm_tq7qjpkmqmc5s, f0) == 0 && offsetof(gm_tq7qjpkmqmc5s, f1) == 8 && offsetof(gm_tq7qjpkmqmc5s, f2) == 24 && offsetof(gm_tq7qjpkmqmc5s, f3) == 32 && offsetof(gm_tq7qjpkmqmc5s, f4) == 40 && offsetof(gm_tq7qjpkmqmc5s, f5) == 48 && offsetof(gm_tq7qjpkmqmc5s, f6) == 56 && offsetof(gm_tq7qjpkmqmc5s, f7) == 80 && offsetof(gm_tq7qjpkmqmc5s, f8) == 96, "layout of `GlobalInfo`");
_Static_assert(sizeof(gm_tq9qng821udh4) == 48 && _Alignof(gm_tq9qng821udh4) == 8 && offsetof(gm_tq9qng821udh4, f0) == 0 && offsetof(gm_tq9qng821udh4, f1) == 8, "layout of `FnKind`");
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_t6evimt84lndq) == 48 && _Alignof(gm_t6evimt84lndq) == 16 && offsetof(gm_t6evimt84lndq, f0) == 0 && offsetof(gm_t6evimt84lndq, f1) == 16, "layout of `Value`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
struct gm_fr_borrowed_params_b39va3r {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_trqfvmjtt4sij l9; /* none */
    gm_trqfvmjtt4sij l12; /* none */
    gm_trqfvmjtt4sij l16; /* touched */
    gm_b16_8 ret;
};
struct gm_fr_cell_fn_refs_6icb85l {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_compute_steady_c2v2ffl {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* escaping */
    gm_t4c7ipjj9s3fp l11; /* seen */
    gm_tjcptv9sagfq8 l14; /* c */
    gm_tq7qjpkmqmc5s l19; /* gl */
    gm_trqfvmjtt4sij l26; /* written */
    gm_trqfvmjtt4sij l30; /* w */
    gm_t4c7ipjj9s3fp l33; /* captured */
    gm_trqfvmjtt4sij l34; /* steady */
    gm_trqfvmjtt4sij l41; /* row */
    gm_trqfvmjtt4sij l51; /* calls */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_direct_calls_tgjq7k6 {
    gm_frame h;
    void * l1; /* out */
    gm_tsth9qu1dv0g3 l2; /* callee */
    gm_b0_1 ret;
};
struct gm_fr_each_cond_hcnk23r {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_each_pat_vg13dgd {
    gm_frame h;
    gm_closure l1; /* f */
    gm_b0_1 ret;
};
struct gm_fr_each_stmt_tt8e8go {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ft2q3st {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hs811oq {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jc4kvvv {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vub5ih1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fn_05d3agq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_cmb4ji5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_count_g9jh9qr {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_fn_djrq2n1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_gnspmq2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_n57683i {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_p8hr8ki {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_refs_tvld7g2 {
    gm_frame h;
    gm_b16_8 l1; /* out */
    gm_b0_1 ret;
};
struct gm_fr_fn_url0b1k {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_vg5vaqp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_get_ag7vhtq {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_get_oe801t5 {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_get_s2mpo5a {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_hash_map_l12of40 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_is_const_arg_98lfonj {
    gm_frame h;
    gm_t4rau9abddl2u l2; /* v */
    uint8_t ret;
};
struct gm_fr_is_pure_l81kp3k {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_list_72a1c0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_frr0ceh {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_r8qc0er {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mutated_in_block_26pupm0 {
    gm_frame h;
    void * l0; /* g */
    void * l2; /* out */
    gm_b0_1 ret;
};
struct gm_fr_mutated_locals_tltgkrt {
    gm_frame h;
    void * l0; /* g */
    void * l2; /* out */
    gm_t710lsrq1m8gk l8; /* l */
    gm_b0_1 ret;
};
struct gm_fr_own_place_peh2qe9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_pure_block_94oo6k4 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_pure_expr_fee5b14 {
    gm_frame h;
    void * l0; /* g */
    uint8_t l2; /* ok */
    gm_t710lsrq1m8gk l4; /* l */
    gm_tsth9qu1dv0g3 l6; /* callee */
    uint8_t ret;
};
struct gm_fr_push_69vvlrm {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_f8cmgo9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_im9d4ro {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_o6sr5eu {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_vd1290p {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_2onqfca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_4f9kvh0 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_dbu7qud {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_hkmvuvc {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_vl9m9ao {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_steady_arg_7904qej {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_steady_params_ng2sd7r {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* s */
    gm_trqfvmjtt4sij l3; /* all */
    gm_trqfvmjtt4sij l4; /* copy */
    gm_b16_8 ret;
};
struct gm_fr_value_fn_refs_jv5jhbq {
    gm_frame h;
    gm_t6evimt84lndq l0; /* v */
    gm_t6evimt84lndq l7; /* x */
    gm_b0_1 ret;
};
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b16_8 gm_f_borrowed_params_b39va3r(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_by_ref_fi0o860(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_captured_locals_glsvloj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_cell_fn_refs_6icb85l(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) void gm_f_compute_steady_c2v2ffl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_const_into_v9nj7g1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2, void * a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_direct_calls_tgjq7k6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_each_cond_hcnk23r(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_each_pat_vg13dgd(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_each_stmt_tt8e8go(void * a0, gm_closure a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ft2q3st(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hs811oq(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jc4kvvv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vub5ih1(void);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_0q6v4m5(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_efqiqfc(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_tajhkla(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_05d3agq(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cmb4ji5(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint64_t gm_f_fn_count_g9jh9qr(void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_djrq2n1(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_gnspmq2(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_n57683i(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_p8hr8ki(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_fn_refs_tvld7g2(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_url0b1k(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vg5vaqp(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_for_each_subpat_arlrtrm(void * a0, gm_closure a1);
__attribute__((aligned(4))) void * gm_f_func_in17r5n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_ag7vhtq(void * a0, gm_tcc1rq4n1avb2 a1);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_oe801t5(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_s2mpo5a(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_hash_map_l12of40(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint8_t gm_f_is_const_arg_98lfonj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_pure_l81kp3k(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_list_72a1c0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_frr0ceh(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_r8qc0er(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_mutated_in_block_26pupm0(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_mutated_locals_tltgkrt(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_new_site_const_qfbs2k2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint8_t gm_f_own_place_peh2qe9(void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_pure_block_94oo6k4(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_pure_expr_fee5b14(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_69vvlrm(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_f8cmgo9(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_o6sr5eu(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_15gklke(void * a0, uint64_t a1, uint8_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_iucnhfg(void * a0, uint32_t a1, uint8_t a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4f9kvh0(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_dbu7qud(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hkmvuvc(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_vl9m9ao(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_root_local_40frg7f(void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_steady_arg_7904qej(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, gm_b16_8 a3, void * a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_steady_params_ng2sd7r(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_value_fn_refs_jv5jhbq(void *env, gm_node *cx, gm_node *inst, const void * a0, gm_b16_8 a1, void * a2);
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srca29n3fkrpbdt[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcrgc9bkquf6nr[];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ft2q3st(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ft2q3st F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hs811oq(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hs811oq F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jc4kvvv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jc4kvvv F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vub5ih1(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vub5ih1 F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_05d3agq(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_05d3agq F;
    void * gm_l0; /* x */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_mutated_in_block_26pupm0 *)((&F.h)->up))->l0));
    void * t3 = gm_l0;
    void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_mutated_in_block_26pupm0 *)((&F.h)->up))->l2));
    gm_b0_1 t5 = gm_f_mutated_locals_tltgkrt((void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cmb4ji5(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_cmb4ji5 F;
    void * gm_l0; /* x */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_direct_calls_tgjq7k6 *)((&F.h)->up))->l1));
    gm_b0_1 t4 = gm_f_direct_calls_tgjq7k6((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) uint64_t gm_f_fn_count_g9jh9qr(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_count_g9jh9qr F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t3) + 344) + 8));
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_djrq2n1(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_djrq2n1 F;
    void * gm_l0; /* x */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_mutated_locals_tltgkrt *)((&F.h)->up))->l0));
    void * t3 = gm_l0;
    void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_mutated_locals_tltgkrt *)((&F.h)->up))->l2));
    gm_b0_1 t5 = gm_f_mutated_locals_tltgkrt((void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_gnspmq2(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_gnspmq2 F;
    void * gm_l0; /* q */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_closure t3 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_each_pat_vg13dgd *)((&F.h)->up))->l1));
    gm_f_each_pat_vg13dgd(t2, t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_n57683i(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_n57683i F;
    void * gm_l0; /* x */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_mutated_locals_tltgkrt *)((&F.h)->up))->l0));
    void * t3 = gm_l0;
    void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_mutated_locals_tltgkrt *)((&F.h)->up))->l2));
    gm_b0_1 t5 = gm_f_mutated_locals_tltgkrt((void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_p8hr8ki(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_p8hr8ki F;
    void * gm_l0; /* x */
    GM_STACK_CHECK(&F, 28u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_pure_expr_fee5b14 *)((&F.h)->up))->l2));
    if (t2) {
        void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_pure_expr_fee5b14 *)((&F.h)->up))->l0));
        void * t4 = gm_l0;
        uint8_t t5 = gm_f_pure_expr_fee5b14((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l1 = t5;
    } else {
        gm_l1 = ((uint8_t)0);
    }
    uint8_t t6 = gm_l1;
    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_pure_expr_fee5b14 *)((&F.h)->up))->l2), t6);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_url0b1k(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_url0b1k F;
    void * gm_l0; /* x */
    GM_STACK_CHECK(&F, 28u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_pure_expr_fee5b14 *)((&F.h)->up))->l2));
    if (t2) {
        void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_pure_expr_fee5b14 *)((&F.h)->up))->l0));
        void * t4 = gm_l0;
        uint8_t t5 = gm_f_pure_expr_fee5b14((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l1 = t5;
    } else {
        gm_l1 = ((uint8_t)0);
    }
    uint8_t t6 = gm_l1;
    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_pure_expr_fee5b14 *)((&F.h)->up))->l2), t6);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vg5vaqp(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_vg5vaqp F;
    void * gm_l0; /* x */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_fn_refs_tvld7g2 *)((&F.h)->up))->l1));
    gm_f_fn_refs_tvld7g2(t2, t3);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_oe801t5(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_oe801t5 F;
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
    gm_tf5srh4mp4b3i t4 = gm_f_find_0q6v4m5(t2, t3);
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
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_s2mpo5a(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_s2mpo5a F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* k */
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
    gm_tf5srh4mp4b3i t4 = gm_f_find_tajhkla(t2, t3);
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
    uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t9) + (uintptr_t)t11 * 24) + 16));
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
inline __attribute__((aligned(4))) void gm_f_hash_map_l12of40(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_l12of40 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_jc4kvvv();
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_const_arg_98lfonj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_is_const_arg_98lfonj F;
    void * gm_l0; /* g */
    void * gm_l1; /* e */
    GM_STACK_CHECK(&F, 167u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = gm_l1;
    gm_t88rcucg1b7uk t4 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t3));
    gm_t4rau9abddl2u t5;
    gm_f_new_site_const_qfbs2k2((void *)&t5, (void *)NULL, cx, NULL, t2, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t5;
    void * t6 = gm_l0;
    void * t7 = gm_l1;
    void * t8 = (void *)((uint8_t *)&F.l2);
    uint8_t t9 = gm_f_const_into_v9nj7g1((void *)NULL, cx, NULL, t6, t7, ((uint64_t)0ULL), t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) void gm_f_list_72a1c0b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_72a1c0b F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_vub5ih1();
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
inline __attribute__((aligned(4))) void gm_f_list_frr0ceh(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_frr0ceh F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_hs811oq();
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
inline __attribute__((aligned(4))) void gm_f_list_r8qc0er(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_r8qc0er F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_ft2q3st();
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
inline __attribute__((aligned(4))) uint8_t gm_f_own_place_peh2qe9(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_own_place_peh2qe9 F;
    void * gm_l0; /* g */
    void * gm_l1; /* place */
    GM_STACK_CHECK(&F, 69u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    gm_t9805qtonbsag gm_l3;
    void * t5 = gm_l0;
    void * t6 = GM_LD(void *, ((uint8_t *)t5));
    void * t7 = GM_LD(void *, ((uint8_t *)t6));
    void * t8 = gm_l1;
    gm_t9805qtonbsag t9 = gm_f_root_local_40frg7f(t7, t8);
    gm_l3 = t9;
    {
        {
            uint8_t t10 = (uint8_t)((((uint8_t *)&gm_l3))[4] != 0);
            if (t10) {
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
        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 209, 51);
    }
gm_lb1:;
    uint8_t t11 = gm_l2;
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_69vvlrm(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_69vvlrm F;
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
        gm_b16_8 t9 = gm_f_empty_slice_vub5ih1();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_4f9kvh0(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_f8cmgo9(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_f8cmgo9 F;
    void * gm_l0; /* l */
    gm_tiilmfd16v3fc gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_hs811oq();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_dbu7qud(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_o6sr5eu(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_o6sr5eu F;
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
        gm_b16_8 t9 = gm_f_empty_slice_ft2q3st();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_vl9m9ao(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4f9kvh0(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_4f9kvh0 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_dbu7qud(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_dbu7qud F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_vl9m9ao(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_vl9m9ao F;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_borrowed_params_b39va3r(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_borrowed_params_b39va3r F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* def */
    uint8_t gm_l4; /* any */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint32_t gm_l7; /* p */
    uint8_t gm_l8; /* c */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    void * gm_l15; /* body */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* i */
    uint32_t gm_l20; /* p */
    GM_STACK_CHECK(&F, 1056u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l21;
    gm_tftb1odc739t5 gm_l22;
    gm_tftb1odc739t5 gm_l23;
    gm_tn68el8d14vku gm_l24;
    void * t24 = gm_l0;
    void * t25 = GM_LD(void *, ((uint8_t *)t24));
    uint32_t t26 = gm_l1;
    void * t27 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t25, t26);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t27;
    {
        gm_trqfvmjtt4sij t28;
        gm_f_list_s83jn2q((void *)&t28, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l3 = t28;
    }
    gm_l4 = ((uint8_t)0);
    void * t29 = gm_l2;
    gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)t29) + 32));
    gm_l5 = t30;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t31 = gm_l6;
            uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t33 = (t31 < t32);
            if (!t33) {
                goto gm_lb1;
            }
            {
                void * t34 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t36 = gm_l6;
                {
                    {
                        uint32_t t37 = GM_LD(uint32_t, (((uint8_t *)t34) + (uintptr_t)t36 * 4));
                        gm_l7 = t37;
                        void * t38 = gm_l0;
                        void * t39 = (void *)(((uint8_t *)t38) + 8);
                        void * t40 = gm_l2;
                        void * t41 = GM_LD(void *, (((uint8_t *)t40) + 48));
                        uint64_t t42 = GM_LD(uint64_t, ((((uint8_t *)t40) + 48) + 8));
                        uint32_t t43 = gm_l7;
                        uint64_t t44 = (uint64_t)t43;
                        if ((uintptr_t)t44 >= (uintptr_t)(t42)) gm_trap_index((uintptr_t)t44, t42, gm_srca29n3fkrpbdt, 26, 26);
                        gm_t88rcucg1b7uk t45 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t41) + (uintptr_t)t44 * 40) + 16));
                        uint8_t t46 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t39, t45);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l8 = t46;
                        void * t47 = (void *)((uint8_t *)&F.l3);
                        uint8_t t48 = gm_l8;
                        gm_tftb1odc739t5 t49 = gm_f_push_vd1290p(t47, t48);
                        gm_l21 = t49;
                        uint8_t t50 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                        if (!t50) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srca29n3fkrpbdt, 27, 9);
                        uint8_t t51 = gm_l8;
                        if (t51) {
                            gm_l4 = ((uint8_t)1);
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 25, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t52 = gm_l6;
            uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)1ULL), &r53);
            uint64_t t55 = r53;
            gm_l6 = t55;
        }
    }
gm_lb1:;
    uint8_t t56 = gm_l4;
    uint8_t t57 = (uint8_t)!t56;
    if (t57) {
        gm_trqfvmjtt4sij t58;
        gm_f_list_s83jn2q((void *)&t58, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l9 = t58;
        void * t59 = gm_l2;
        gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)t59) + 32));
        gm_l10 = t60;
        gm_l11 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t61 = gm_l11;
                uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                uint8_t t63 = (t61 < t62);
                if (!t63) {
                    goto gm_lb6;
                }
                {
                    void * t64 = GM_LD(void *, ((uint8_t *)&gm_l10));
                    uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                    uint64_t t66 = gm_l11;
                    {
                        {
                            void * t67 = (void *)((uint8_t *)&F.l9);
                            gm_tftb1odc739t5 t68 = gm_f_push_vd1290p(t67, ((uint8_t)0));
                            gm_l22 = t68;
                            uint8_t t69 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                            if (!t69) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 1, gm_srca29n3fkrpbdt, 32, 31);
                            goto gm_lb9;
                        }
gm_lb10:;
                        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 32, 9);
                    }
gm_lb9:;
                }
gm_lb8:;
                uint64_t t70 = gm_l11;
                uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
                uint64_t t73 = r71;
                gm_l11 = t73;
            }
        }
gm_lb6:;
        gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
        F.ret = t74;
        goto gm_fret;
    }
    void * t75 = gm_l0;
    uint32_t t76 = gm_l1;
    uint8_t t77 = gm_f_is_pure_l81kp3k((void *)NULL, cx, NULL, t75, t76);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t78 = (uint8_t)!t77;
    if (t78) {
        void * t79 = gm_l0;
        uint32_t t80 = gm_l1;
        gm_b16_8 t81 = gm_f_steady_params_ng2sd7r((void *)NULL, cx, NULL, t79, t80);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t81;
        goto gm_fret;
    }
    void * t82 = gm_l2;
    uint8_t t83 = (uint8_t)(GM_LD(void *, (((uint8_t *)t82) + 152)) != 0);
    if (t83) {
        void * t84 = GM_LD(void *, (((uint8_t *)t82) + 152));
        gm_l15 = t84;
    } else {
        gm_trqfvmjtt4sij t85;
        gm_f_list_s83jn2q((void *)&t85, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l12 = t85;
        void * t86 = gm_l2;
        gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)t86) + 32));
        gm_l13 = t87;
        gm_l14 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t88 = gm_l14;
                uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                uint8_t t90 = (t88 < t89);
                if (!t90) {
                    goto gm_lb11;
                }
                {
                    void * t91 = GM_LD(void *, ((uint8_t *)&gm_l13));
                    uint64_t t92 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                    uint64_t t93 = gm_l14;
                    {
                        {
                            void * t94 = (void *)((uint8_t *)&F.l12);
                            gm_tftb1odc739t5 t95 = gm_f_push_vd1290p(t94, ((uint8_t)0));
                            gm_l23 = t95;
                            uint8_t t96 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                            if (!t96) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_srca29n3fkrpbdt, 38, 31);
                            goto gm_lb14;
                        }
gm_lb15:;
                        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 38, 9);
                    }
gm_lb14:;
                }
gm_lb13:;
                uint64_t t97 = gm_l14;
                uint64_t r98; bool o99 = __builtin_add_overflow(t97, ((uint64_t)1ULL), &r98);
                uint64_t t100 = r98;
                gm_l14 = t100;
            }
        }
gm_lb11:;
        gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
        F.ret = t101;
        goto gm_fret;
    }
    void * t102 = gm_l15;
    gm_l15 = t102;
    {
        gm_trqfvmjtt4sij t103;
        gm_f_list_go2qg0b((void *)&t103, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l16 = t103;
    }
    void * t104 = gm_l0;
    void * t105 = gm_l15;
    void * t106 = (void *)((uint8_t *)&F.l16);
    gm_b0_1 t107 = gm_f_mutated_locals_tltgkrt((void *)NULL, cx, NULL, t104, t105, t106);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t108 = gm_l2;
    gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)t108) + 32));
    gm_l17 = t109;
    gm_l18 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t110 = gm_l18;
            uint64_t t111 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
            uint8_t t112 = (t110 < t111);
            if (!t112) {
                goto gm_lb16;
            }
            {
                uint64_t t113 = gm_l18;
                void * t114 = GM_LD(void *, ((uint8_t *)&gm_l17));
                uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                uint64_t t116 = gm_l18;
                uint32_t t117 = GM_LD(uint32_t, (((uint8_t *)t114) + (uintptr_t)t116 * 4));
                gm_tn68el8d14vku t118;
                GM_ST(uint32_t, (uint8_t *)&t118 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t118 + 0), t113);
                GM_ST(uint32_t, ((uint8_t *)&t118 + 8), t117);
                gm_l24 = t118;
                {
                    {
                        uint64_t t119 = GM_LD(uint64_t, ((uint8_t *)&gm_l24));
                        gm_l19 = t119;
                        uint32_t t120 = GM_LD(uint32_t, (((uint8_t *)&gm_l24) + 8));
                        gm_l20 = t120;
                        {
                            {
                                void * t121 = GM_LD(void *, ((uint8_t *)&F.l3));
                                uint64_t t122 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                                uint64_t t123 = gm_l19;
                                if ((uintptr_t)t123 >= (uintptr_t)(t122)) gm_trap_index((uintptr_t)t123, t122, gm_srca29n3fkrpbdt, 44, 12);
                                uint8_t t124 = GM_LD(uint8_t, (((uint8_t *)t121) + (uintptr_t)t123 * 1));
                                if (!t124) {
                                    goto gm_lb22;
                                }
                                uint32_t t125 = gm_l20;
                                gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&F.l16));
                                uint8_t t127 = 0;
                                for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t126) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t126))) + i * 4, ((uint8_t *)&t125))) { t127 = 1; break; }
                                uint8_t t128 = (uint8_t)(t127);
                                if (!t128) {
                                    goto gm_lb22;
                                }
                                void * t129 = GM_LD(void *, ((uint8_t *)&F.l3));
                                uint64_t t130 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                                uint64_t t131 = gm_l19;
                                if ((uintptr_t)t131 >= (uintptr_t)(t130)) gm_trap_index((uintptr_t)t131, t130, gm_srca29n3fkrpbdt, 44, 50);
                                GM_ST(uint8_t, (((uint8_t *)t129) + (uintptr_t)t131 * 1), ((uint8_t)0));
                                goto gm_lb21;
                            }
gm_lb22:;
                        }
gm_lb21:;
                        goto gm_lb19;
                    }
gm_lb20:;
                    gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 43, 5);
                }
gm_lb19:;
            }
gm_lb18:;
            uint64_t t132 = gm_l18;
            uint64_t r133; bool o134 = __builtin_add_overflow(t132, ((uint64_t)1ULL), &r133);
            uint64_t t135 = r133;
            gm_l18 = t135;
        }
    }
gm_lb16:;
    gm_b16_8 t136 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    F.ret = t136;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_cell_fn_refs_6icb85l(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_cell_fn_refs_6icb85l F;
    void * gm_l0; /* cell */
    gm_b16_8 gm_l1; /* out */
    void * gm_l2; /* seen */
    uint64_t gm_l3; /* key */
    GM_STACK_CHECK(&F, 132u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l4;
    gm_tqs5p4n2tq19v gm_l5;
    gm_tftb1odc739t5 gm_l6;
    void * t5 = gm_l0;
    uint64_t t6 = (uintptr_t)t5;
    gm_l3 = t6;
    void * t7 = gm_l2;
    uint64_t t8 = gm_l3;
    gm_tqs5p4n2tq19v t9 = gm_f_get_s2mpo5a(t7, t8);
    gm_l5 = t9;
    {
        {
            uint8_t t10 = (uint8_t)((((uint8_t *)&gm_l5))[1] != 0);
            if (t10) {
                goto gm_lb2;
            }
            gm_l4 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l4 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 316, 8);
    }
gm_lb1:;
    uint8_t t11 = gm_l4;
    if (t11) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t12 = gm_l2;
    uint64_t t13 = gm_l3;
    gm_tftb1odc739t5 t14 = gm_f_put_15gklke(t12, t13, ((uint8_t)1));
    gm_l6 = t14;
    uint8_t t15 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (!t15) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srca29n3fkrpbdt, 317, 5);
    void * t16 = gm_l0;
    gm_b16_8 t17 = gm_l1;
    void * t18 = gm_l2;
    gm_b0_1 t19 = gm_f_value_fn_refs_jv5jhbq((void *)NULL, cx, NULL, &(*(gm_t6evimt84lndq *)((uint8_t *)t16)), t17, t18);
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
__attribute__((aligned(4))) void gm_f_compute_steady_c2v2ffl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_compute_steady_c2v2ffl F;
    void * gm_l0; /* g */
    void * gm_l1; /* prog */
    uint64_t gm_l2; /* n */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    void * gm_l9; /* d */
    void * gm_l10; /* b */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    void * gm_l15; /* x */
    void * gm_l16; /* cell */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    void * gm_l20; /* cell */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    uint32_t gm_l23; /* f */
    uint32_t gm_l24; /* h */
    uint8_t gm_l25; /* debug_info */
    uint64_t gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    uint64_t gm_l29; /* i */
    void * gm_l31; /* d */
    void * gm_l32; /* b */
    uint64_t gm_l35; /* <tmp> */
    uint64_t gm_l36; /* <tmp> */
    uint64_t gm_l37; /* i */
    void * gm_l38; /* d */
    uint32_t gm_l39; /* f */
    uint8_t gm_l40; /* eligible */
    gm_b16_8 gm_l42; /* <tmp> */
    uint64_t gm_l43; /* <tmp> */
    uint32_t gm_l44; /* p */
    uint64_t gm_l45; /* <tmp> */
    uint64_t gm_l46; /* <tmp> */
    uint64_t gm_l47; /* i */
    void * gm_l48; /* d */
    void * gm_l49; /* b */
    uint32_t gm_l50; /* caller */
    gm_b16_8 gm_l52; /* <tmp> */
    uint64_t gm_l53; /* <tmp> */
    gm_tiilmfd16v3fc gm_l54; /* c */
    gm_b16_8 gm_l55; /* row */
    uint64_t gm_l56; /* <tmp> */
    uint64_t gm_l57; /* <tmp> */
    uint64_t gm_l58; /* k */
    GM_STACK_CHECK(&F, 2590u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l59;
    gm_tftb1odc739t5 gm_l60;
    uint8_t gm_l61;
    uint8_t gm_l62;
    uint8_t gm_l63;
    uint8_t gm_l64;
    uint8_t gm_l65;
    uint8_t gm_l66;
    uint8_t gm_l67;
    uint8_t gm_l68;
    uint8_t gm_l69;
    uint8_t gm_l70;
    gm_tqs5p4n2tq19v gm_l71;
    gm_tftb1odc739t5 gm_l72;
    gm_tftb1odc739t5 gm_l73;
    {
        void * t80 = gm_l0;
        void * t81 = GM_LD(void *, ((uint8_t *)t80));
        gm_l1 = t81;
        void * t82 = gm_l1;
        uint64_t t83 = gm_f_fn_count_g9jh9qr(t82);
        gm_l2 = t83;
        gm_trqfvmjtt4sij t84;
        gm_f_list_s83jn2q((void *)&t84, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l3 = t84;
        gm_l5 = ((uint64_t)0ULL);
        uint64_t t85 = gm_l2;
        gm_l4 = t85;
        {
            for (;;) {
                uint64_t t86 = gm_l5;
                uint64_t t87 = gm_l4;
                uint8_t t88 = (t86 < t87);
                if (!t88) {
                    goto gm_lb1;
                }
                {
                    {
                        {
                            void * t89 = (void *)((uint8_t *)&F.l3);
                            gm_tftb1odc739t5 t90 = gm_f_push_vd1290p(t89, ((uint8_t)0));
                            gm_l59 = t90;
                            uint8_t t91 = (uint8_t)!((((uint8_t *)&gm_l59))[0] != 0);
                            if (!t91) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l59) + 1, gm_srca29n3fkrpbdt, 69, 21);
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 69, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t92 = gm_l5;
                uint64_t r93; bool o94 = __builtin_add_overflow(t92, ((uint64_t)1ULL), &r93);
                uint64_t t95 = r93;
                gm_l5 = t95;
            }
        }
gm_lb1:;
        gm_l7 = ((uint64_t)0ULL);
        uint64_t t96 = gm_l2;
        gm_l6 = t96;
        {
            for (;;) {
                uint64_t t97 = gm_l7;
                uint64_t t98 = gm_l6;
                uint8_t t99 = (t97 < t98);
                if (!t99) {
                    goto gm_lb6;
                }
                {
                    {
                        {
                            uint64_t t100 = gm_l7;
                            gm_l8 = t100;
                            void * t101 = gm_l1;
                            uint64_t t102 = gm_l8;
                            uint32_t t103 = (uint32_t)t102;
                            void * t104 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t101, t103);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l9 = t104;
                            {
                                {
                                    void * t105 = gm_l9;
                                    uint8_t t106 = (uint8_t)(GM_LD(void *, (((uint8_t *)t105) + 152)) != 0);
                                    if (!t106) {
                                        goto gm_lb12;
                                    }
                                    void * t107 = GM_LD(void *, (((uint8_t *)t105) + 152));
                                    gm_l10 = t107;
                                    void * t108 = gm_l10;
                                    gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
                                    gm_f_fn_refs_tvld7g2(t108, t109);
                                    goto gm_lb11;
                                }
gm_lb12:;
                            }
gm_lb11:;
                            goto gm_lb9;
                        }
gm_lb10:;
                        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 70, 5);
                    }
gm_lb9:;
                }
gm_lb8:;
                uint64_t t110 = gm_l7;
                uint64_t r111; bool o112 = __builtin_add_overflow(t110, ((uint64_t)1ULL), &r111);
                uint64_t t113 = r111;
                gm_l7 = t113;
            }
        }
gm_lb6:;
    }
    {
        gm_t4c7ipjj9s3fp t114;
        gm_f_hash_map_l12of40((void *)&t114, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l11 = t114;
        void * t115 = gm_l1;
        void * t116 = GM_LD(void *, ((uint8_t *)t115));
        gm_b16_8 t117 = GM_LD(gm_b16_8, (((uint8_t *)t116) + 2088));
        gm_l12 = t117;
        gm_l13 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t118 = gm_l13;
                uint64_t t119 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                uint8_t t120 = (t118 < t119);
                if (!t120) {
                    goto gm_lb13;
                }
                {
                    void * t121 = GM_LD(void *, ((uint8_t *)&gm_l12));
                    uint64_t t122 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                    uint64_t t123 = gm_l13;
                    {
                        {
                            memmove(((uint8_t *)&F.l14), (((uint8_t *)t121) + (uintptr_t)t123 * 88), 88);
                            {
                                {
                                    uint8_t t124 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l14) + 64)) != 0);
                                    if (!t124) {
                                        goto gm_lb19;
                                    }
                                    void * t125 = GM_LD(void *, (((uint8_t *)&F.l14) + 64));
                                    gm_l15 = t125;
                                    void * t126 = gm_l15;
                                    gm_b16_8 t127 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
                                    gm_f_fn_refs_tvld7g2(t126, t127);
                                    goto gm_lb18;
                                }
gm_lb19:;
                            }
gm_lb18:;
                            {
                                {
                                    uint8_t t128 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l14) + 80)) != 0);
                                    if (!t128) {
                                        goto gm_lb21;
                                    }
                                    void * t129 = GM_LD(void *, (((uint8_t *)&F.l14) + 80));
                                    gm_l16 = t129;
                                    void * t130 = gm_l16;
                                    gm_b16_8 t131 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
                                    void * t132 = (void *)((uint8_t *)&F.l11);
                                    gm_b0_1 t133 = gm_f_value_fn_refs_jv5jhbq((void *)NULL, cx, NULL, &(*(gm_t6evimt84lndq *)((uint8_t *)t130)), t131, t132);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_lb20;
                                }
gm_lb21:;
                            }
gm_lb20:;
                            goto gm_lb16;
                        }
gm_lb17:;
                        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 75, 5);
                    }
gm_lb16:;
                }
gm_lb15:;
                uint64_t t134 = gm_l13;
                uint64_t r135; bool o136 = __builtin_add_overflow(t134, ((uint64_t)1ULL), &r135);
                uint64_t t137 = r135;
                gm_l13 = t137;
            }
        }
gm_lb13:;
        void * t138 = gm_l1;
        void * t139 = GM_LD(void *, ((uint8_t *)t138));
        gm_b16_8 t140 = GM_LD(gm_b16_8, (((uint8_t *)t139) + 1568));
        gm_l17 = t140;
        gm_l18 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t141 = gm_l18;
                uint64_t t142 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                uint8_t t143 = (t141 < t142);
                if (!t143) {
                    goto gm_lb22;
                }
                {
                    void * t144 = GM_LD(void *, ((uint8_t *)&gm_l17));
                    uint64_t t145 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                    uint64_t t146 = gm_l18;
                    {
                        {
                            memmove(((uint8_t *)&F.l19), (((uint8_t *)t144) + (uintptr_t)t146 * 104), 104);
                            {
                                {
                                    uint8_t t147 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l19) + 96)) != 0);
                                    if (!t147) {
                                        goto gm_lb28;
                                    }
                                    void * t148 = GM_LD(void *, (((uint8_t *)&F.l19) + 96));
                                    gm_l20 = t148;
                                    void * t149 = gm_l20;
                                    gm_b16_8 t150 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
                                    void * t151 = (void *)((uint8_t *)&F.l11);
                                    gm_b0_1 t152 = gm_f_value_fn_refs_jv5jhbq((void *)NULL, cx, NULL, &(*(gm_t6evimt84lndq *)((uint8_t *)t149)), t150, t151);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_lb27;
                                }
gm_lb28:;
                            }
gm_lb27:;
                            goto gm_lb25;
                        }
gm_lb26:;
                        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 79, 5);
                    }
gm_lb25:;
                }
gm_lb24:;
                uint64_t t153 = gm_l18;
                uint64_t r154; bool o155 = __builtin_add_overflow(t153, ((uint64_t)1ULL), &r154);
                uint64_t t156 = r154;
                gm_l18 = t156;
            }
        }
gm_lb22:;
    }
    {
        void * t157 = gm_l1;
        void * t158 = GM_LD(void *, ((uint8_t *)t157));
        gm_b16_8 t159 = GM_LD(gm_b16_8, (((uint8_t *)t158) + 920));
        gm_l21 = t159;
        gm_l22 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t160 = gm_l22;
                uint64_t t161 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                uint8_t t162 = (t160 < t161);
                if (!t162) {
                    goto gm_lb29;
                }
                {
                    void * t163 = GM_LD(void *, ((uint8_t *)&gm_l21));
                    uint64_t t164 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                    uint64_t t165 = gm_l22;
                    {
                        {
                            uint32_t t166 = GM_LD(uint32_t, (((uint8_t *)t163) + (uintptr_t)t165 * 4));
                            gm_l23 = t166;
                            void * t167 = GM_LD(void *, ((uint8_t *)&F.l3));
                            uint64_t t168 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                            uint32_t t169 = gm_l23;
                            uint64_t t170 = (uint64_t)t169;
                            if ((uintptr_t)t170 >= (uintptr_t)(t168)) gm_trap_index((uintptr_t)t170, t168, gm_srca29n3fkrpbdt, 82, 40);
                            GM_ST(uint8_t, (((uint8_t *)t167) + (uintptr_t)t170 * 1), ((uint8_t)1));
                            goto gm_lb32;
                        }
gm_lb33:;
                        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 82, 5);
                    }
gm_lb32:;
                }
gm_lb31:;
                uint64_t t171 = gm_l22;
                uint64_t r172; bool o173 = __builtin_add_overflow(t171, ((uint64_t)1ULL), &r172);
                uint64_t t174 = r172;
                gm_l22 = t174;
            }
        }
gm_lb29:;
        {
            {
                void * t175 = gm_l1;
                void * t176 = GM_LD(void *, ((uint8_t *)t175));
                uint8_t t177 = (uint8_t)(((((uint8_t *)t176) + 960))[4] != 0);
                if (!t177) {
                    goto gm_lb35;
                }
                uint32_t t178 = GM_LD(uint32_t, (((uint8_t *)t176) + 960));
                gm_l24 = t178;
                void * t179 = GM_LD(void *, ((uint8_t *)&F.l3));
                uint64_t t180 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                uint32_t t181 = gm_l24;
                uint64_t t182 = (uint64_t)t181;
                if ((uintptr_t)t182 >= (uintptr_t)(t180)) gm_trap_index((uintptr_t)t182, t180, gm_srca29n3fkrpbdt, 83, 32);
                GM_ST(uint8_t, (((uint8_t *)t179) + (uintptr_t)t182 * 1), ((uint8_t)1));
                goto gm_lb34;
            }
gm_lb35:;
        }
gm_lb34:;
        void * t183 = gm_l0;
        uint8_t t184 = GM_LD(uint8_t, ((((uint8_t *)t183) + 1136) + 2));
        gm_l25 = t184;
        gm_trqfvmjtt4sij t185;
        gm_f_list_r8qc0er((void *)&t185, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l26 = t185;
        gm_l28 = ((uint64_t)0ULL);
        uint64_t t186 = gm_l2;
        gm_l27 = t186;
        {
            for (;;) {
                uint64_t t187 = gm_l28;
                uint64_t t188 = gm_l27;
                uint8_t t189 = (t187 < t188);
                if (!t189) {
                    goto gm_lb36;
                }
                {
                    {
                        {
                            uint64_t t190 = gm_l28;
                            gm_l29 = t190;
                            gm_trqfvmjtt4sij t191;
                            gm_f_list_go2qg0b((void *)&t191, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.l30 = t191;
                            void * t192 = gm_l1;
                            uint64_t t193 = gm_l29;
                            uint32_t t194 = (uint32_t)t193;
                            void * t195 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t192, t194);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l31 = t195;
                            {
                                {
                                    void * t196 = gm_l31;
                                    uint8_t t197 = (uint8_t)(GM_LD(void *, (((uint8_t *)t196) + 152)) != 0);
                                    if (!t197) {
                                        goto gm_lb42;
                                    }
                                    void * t198 = GM_LD(void *, (((uint8_t *)t196) + 152));
                                    gm_l32 = t198;
                                    void * t199 = gm_l0;
                                    void * t200 = gm_l32;
                                    void * t201 = (void *)((uint8_t *)&F.l30);
                                    gm_b0_1 t202 = gm_f_mutated_locals_tltgkrt((void *)NULL, cx, NULL, t199, t200, t201);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_lb41;
                                }
gm_lb42:;
                            }
gm_lb41:;
                            void * t203 = (void *)((uint8_t *)&F.l26);
                            gm_b16_8 t204 = GM_LD(gm_b16_8, ((uint8_t *)&F.l30));
                            gm_tftb1odc739t5 t205 = gm_f_push_o6sr5eu(t203, t204);
                            gm_l60 = t205;
                            uint8_t t206 = (uint8_t)!((((uint8_t *)&gm_l60))[0] != 0);
                            if (!t206) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l60) + 1, gm_srca29n3fkrpbdt, 91, 9);
                            goto gm_lb39;
                        }
gm_lb40:;
                        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 87, 5);
                    }
gm_lb39:;
                }
gm_lb38:;
                uint64_t t207 = gm_l28;
                uint64_t r208; bool o209 = __builtin_add_overflow(t207, ((uint64_t)1ULL), &r208);
                uint64_t t210 = r208;
                gm_l28 = t210;
            }
        }
gm_lb36:;
    }
    {
        void * t211 = gm_l0;
        gm_t4c7ipjj9s3fp t212;
        gm_f_captured_locals_glsvloj((void *)&t212, (void *)NULL, cx, NULL, t211);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l33 = t212;
        gm_trqfvmjtt4sij t213;
        gm_f_list_72a1c0b((void *)&t213, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l34 = t213;
        gm_l36 = ((uint64_t)0ULL);
        uint64_t t214 = gm_l2;
        gm_l35 = t214;
        {
            for (;;) {
                uint64_t t215 = gm_l36;
                uint64_t t216 = gm_l35;
                uint8_t t217 = (t215 < t216);
                if (!t217) {
                    goto gm_lb43;
                }
                {
                    {
                        {
                            uint64_t t218 = gm_l36;
                            gm_l37 = t218;
                            void * t219 = gm_l1;
                            uint64_t t220 = gm_l37;
                            uint32_t t221 = (uint32_t)t220;
                            void * t222 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t219, t221);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l38 = t222;
                            uint64_t t223 = gm_l37;
                            uint32_t t224 = (uint32_t)t223;
                            gm_l39 = t224;
                            uint8_t t225 = gm_l25;
                            uint8_t t226 = (uint8_t)!t225;
                            if (t226) {
                                void * t227 = gm_l38;
                                uint8_t t228 = GM_LD(uint8_t, ((((uint8_t *)t227) + 96) + 0));
                                uint8_t t229 = (uint8_t)((t228 == ((uint8_t)0ULL)));
                                gm_tq9qng821udh4 t230 = GM_LD(gm_tq9qng821udh4, (((uint8_t *)t227) + 96));
                                gm_tq9qng821udh4 t231;
                                memset(&t231, 0, sizeof t231);
                                GM_ST(uint8_t, ((uint8_t *)&t231 + 0), ((uint8_t)0ULL));
                                gm_l64 = t229;
                            } else {
                                gm_l64 = ((uint8_t)0);
                            }
                            uint8_t t232 = gm_l64;
                            if (t232) {
                                void * t233 = gm_l38;
                                {
                                    {
                                        uint8_t t234 = (uint8_t)(((((uint8_t *)t233) + 88))[4] != 0);
                                        if (t234) {
                                            goto gm_lb49;
                                        }
                                        gm_l65 = ((uint8_t)1);
                                        goto gm_lb48;
                                    }
gm_lb49:;
                                    {
                                        gm_l65 = ((uint8_t)0);
                                        goto gm_lb48;
                                    }
gm_lb50:;
                                    gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 98, 61);
                                }
gm_lb48:;
                                uint8_t t235 = gm_l65;
                                gm_l63 = t235;
                            } else {
                                gm_l63 = ((uint8_t)0);
                            }
                            uint8_t t236 = gm_l63;
                            if (t236) {
                                void * t237 = gm_l38;
                                {
                                    {
                                        uint8_t t238 = (uint8_t)(GM_LD(void *, (((uint8_t *)t237) + 152)) != 0);
                                        if (t238) {
                                            goto gm_lb52;
                                        }
                                        gm_l66 = ((uint8_t)0);
                                        goto gm_lb51;
                                    }
gm_lb52:;
                                    {
                                        gm_l66 = ((uint8_t)1);
                                        goto gm_lb51;
                                    }
gm_lb53:;
                                    gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 98, 82);
                                }
gm_lb51:;
                                uint8_t t239 = gm_l66;
                                gm_l62 = t239;
                            } else {
                                gm_l62 = ((uint8_t)0);
                            }
                            uint8_t t240 = gm_l62;
                            if (t240) {
                                void * t241 = GM_LD(void *, ((uint8_t *)&F.l3));
                                uint64_t t242 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                                uint64_t t243 = gm_l37;
                                if ((uintptr_t)t243 >= (uintptr_t)(t242)) gm_trap_index((uintptr_t)t243, t242, gm_srca29n3fkrpbdt, 98, 109);
                                uint8_t t244 = GM_LD(uint8_t, (((uint8_t *)t241) + (uintptr_t)t243 * 1));
                                uint8_t t245 = (uint8_t)!t244;
                                gm_l61 = t245;
                            } else {
                                gm_l61 = ((uint8_t)0);
                            }
                            uint8_t t246 = gm_l61;
                            gm_l40 = t246;
                            gm_trqfvmjtt4sij t247;
                            gm_f_list_s83jn2q((void *)&t247, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.l41 = t247;
                            void * t248 = gm_l38;
                            gm_b16_8 t249 = GM_LD(gm_b16_8, (((uint8_t *)t248) + 32));
                            gm_l42 = t249;
                            gm_l43 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t250 = gm_l43;
                                    uint64_t t251 = GM_LD(uint64_t, (((uint8_t *)&gm_l42) + 8));
                                    uint8_t t252 = (t250 < t251);
                                    if (!t252) {
                                        goto gm_lb54;
                                    }
                                    {
                                        void * t253 = GM_LD(void *, ((uint8_t *)&gm_l42));
                                        uint64_t t254 = GM_LD(uint64_t, (((uint8_t *)&gm_l42) + 8));
                                        uint64_t t255 = gm_l43;
                                        {
                                            {
                                                uint32_t t256 = GM_LD(uint32_t, (((uint8_t *)t253) + (uintptr_t)t255 * 4));
                                                gm_l44 = t256;
                                                void * t257 = (void *)((uint8_t *)&F.l41);
                                                uint8_t t258 = gm_l40;
                                                if (t258) {
                                                    void * t259 = gm_l0;
                                                    void * t260 = (void *)(((uint8_t *)t259) + 8);
                                                    void * t261 = gm_l38;
                                                    void * t262 = GM_LD(void *, (((uint8_t *)t261) + 48));
                                                    uint64_t t263 = GM_LD(uint64_t, ((((uint8_t *)t261) + 48) + 8));
                                                    uint32_t t264 = gm_l44;
                                                    uint64_t t265 = (uint64_t)t264;
                                                    if ((uintptr_t)t265 >= (uintptr_t)(t263)) gm_trap_index((uintptr_t)t265, t263, gm_srca29n3fkrpbdt, 101, 47);
                                                    gm_t88rcucg1b7uk t266 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t262) + (uintptr_t)t265 * 40) + 16));
                                                    uint8_t t267 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t260, t266);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_l69 = t267;
                                                } else {
                                                    gm_l69 = ((uint8_t)0);
                                                }
                                                uint8_t t268 = gm_l69;
                                                if (t268) {
                                                    uint32_t t269 = gm_l44;
                                                    void * t270 = GM_LD(void *, ((uint8_t *)&F.l26));
                                                    uint64_t t271 = GM_LD(uint64_t, (((uint8_t *)&F.l26) + 8));
                                                    uint64_t t272 = gm_l37;
                                                    if ((uintptr_t)t272 >= (uintptr_t)(t271)) gm_trap_index((uintptr_t)t272, t271, gm_srca29n3fkrpbdt, 101, 83);
                                                    gm_b16_8 t273 = GM_LD(gm_b16_8, (((uint8_t *)t270) + (uintptr_t)t272 * 16));
                                                    uint8_t t274 = 0;
                                                    for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t273) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t273))) + i * 4, ((uint8_t *)&t269))) { t274 = 1; break; }
                                                    uint8_t t275 = (uint8_t)(!t274);
                                                    gm_l68 = t275;
                                                } else {
                                                    gm_l68 = ((uint8_t)0);
                                                }
                                                uint8_t t276 = gm_l68;
                                                if (t276) {
                                                    void * t277 = (void *)((uint8_t *)&F.l33);
                                                    uint32_t t278 = gm_l39;
                                                    uint32_t t279 = gm_l44;
                                                    gm_tcc1rq4n1avb2 t280;
                                                    GM_ST(uint32_t, ((uint8_t *)&t280 + 0), t278);
                                                    GM_ST(uint32_t, ((uint8_t *)&t280 + 4), t279);
                                                    gm_tqs5p4n2tq19v t281 = gm_f_get_ag7vhtq(t277, t280);
                                                    gm_l71 = t281;
                                                    {
                                                        {
                                                            uint8_t t282 = (uint8_t)((((uint8_t *)&gm_l71))[1] != 0);
                                                            if (t282) {
                                                                goto gm_lb60;
                                                            }
                                                            gm_l70 = ((uint8_t)1);
                                                            goto gm_lb59;
                                                        }
gm_lb60:;
                                                        {
                                                            gm_l70 = ((uint8_t)0);
                                                            goto gm_lb59;
                                                        }
gm_lb61:;
                                                        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 101, 104);
                                                    }
gm_lb59:;
                                                    uint8_t t283 = gm_l70;
                                                    gm_l67 = t283;
                                                } else {
                                                    gm_l67 = ((uint8_t)0);
                                                }
                                                uint8_t t284 = gm_l67;
                                                gm_tftb1odc739t5 t285 = gm_f_push_vd1290p(t257, t284);
                                                gm_l72 = t285;
                                                uint8_t t286 = (uint8_t)!((((uint8_t *)&gm_l72))[0] != 0);
                                                if (!t286) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l72) + 1, gm_srca29n3fkrpbdt, 101, 13);
                                                goto gm_lb57;
                                            }
gm_lb58:;
                                            gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 100, 9);
                                        }
gm_lb57:;
                                    }
gm_lb56:;
                                    uint64_t t287 = gm_l43;
                                    uint64_t r288; bool o289 = __builtin_add_overflow(t287, ((uint64_t)1ULL), &r288);
                                    uint64_t t290 = r288;
                                    gm_l43 = t290;
                                }
                            }
gm_lb54:;
                            void * t291 = (void *)((uint8_t *)&F.l34);
                            gm_b16_8 t292 = GM_LD(gm_b16_8, ((uint8_t *)&F.l41));
                            gm_tftb1odc739t5 t293 = gm_f_push_69vvlrm(t291, t292);
                            gm_l73 = t293;
                            uint8_t t294 = (uint8_t)!((((uint8_t *)&gm_l73))[0] != 0);
                            if (!t294) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l73) + 1, gm_srca29n3fkrpbdt, 103, 9);
                            goto gm_lb46;
                        }
gm_lb47:;
                        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 95, 5);
                    }
gm_lb46:;
                }
gm_lb45:;
                uint64_t t295 = gm_l36;
                uint64_t r296; bool o297 = __builtin_add_overflow(t295, ((uint64_t)1ULL), &r296);
                uint64_t t298 = r296;
                gm_l36 = t298;
            }
        }
gm_lb43:;
    }
    {
        gm_l46 = ((uint64_t)0ULL);
        uint64_t t299 = gm_l2;
        gm_l45 = t299;
        {
            for (;;) {
                uint64_t t300 = gm_l46;
                uint64_t t301 = gm_l45;
                uint8_t t302 = (t300 < t301);
                if (!t302) {
                    goto gm_lb62;
                }
                {
                    {
                        {
                            uint64_t t303 = gm_l46;
                            gm_l47 = t303;
                            void * t304 = gm_l1;
                            uint64_t t305 = gm_l47;
                            uint32_t t306 = (uint32_t)t305;
                            void * t307 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t304, t306);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l48 = t307;
                            void * t308 = gm_l48;
                            uint8_t t309 = (uint8_t)(GM_LD(void *, (((uint8_t *)t308) + 152)) != 0);
                            if (t309) {
                                void * t310 = GM_LD(void *, (((uint8_t *)t308) + 152));
                                gm_l49 = t310;
                            } else {
                                goto gm_lb64;
                            }
                            uint64_t t311 = gm_l47;
                            uint32_t t312 = (uint32_t)t311;
                            gm_l50 = t312;
                            gm_trqfvmjtt4sij t313;
                            gm_f_list_frr0ceh((void *)&t313, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.l51 = t313;
                            void * t314 = gm_l49;
                            void * t315 = (void *)((uint8_t *)&F.l51);
                            gm_b0_1 t316 = gm_f_direct_calls_tgjq7k6((void *)NULL, cx, NULL, t314, t315);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_b16_8 t317 = GM_LD(gm_b16_8, ((uint8_t *)&F.l51));
                            gm_l52 = t317;
                            gm_l53 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t318 = gm_l53;
                                    uint64_t t319 = GM_LD(uint64_t, (((uint8_t *)&gm_l52) + 8));
                                    uint8_t t320 = (t318 < t319);
                                    if (!t320) {
                                        goto gm_lb67;
                                    }
                                    {
                                        void * t321 = GM_LD(void *, ((uint8_t *)&gm_l52));
                                        uint64_t t322 = GM_LD(uint64_t, (((uint8_t *)&gm_l52) + 8));
                                        uint64_t t323 = gm_l53;
                                        {
                                            {
                                                memmove(((uint8_t *)&gm_l54), (((uint8_t *)t321) + (uintptr_t)t323 * 24), 24);
                                                void * t324 = GM_LD(void *, ((uint8_t *)&F.l34));
                                                uint64_t t325 = GM_LD(uint64_t, (((uint8_t *)&F.l34) + 8));
                                                uint32_t t326 = GM_LD(uint32_t, ((uint8_t *)&gm_l54));
                                                uint64_t t327 = (uint64_t)t326;
                                                if ((uintptr_t)t327 >= (uintptr_t)(t325)) gm_trap_index((uintptr_t)t327, t325, gm_srca29n3fkrpbdt, 113, 20);
                                                gm_b16_8 t328 = GM_LD(gm_b16_8, (((uint8_t *)t324) + (uintptr_t)t327 * 16));
                                                gm_l55 = t328;
                                                gm_l57 = ((uint64_t)0ULL);
                                                uint64_t t329 = GM_LD(uint64_t, ((((uint8_t *)&gm_l54) + 8) + 8));
                                                gm_l56 = t329;
                                                {
                                                    for (;;) {
                                                        uint64_t t330 = gm_l57;
                                                        uint64_t t331 = gm_l56;
                                                        uint8_t t332 = (t330 < t331);
                                                        if (!t332) {
                                                            goto gm_lb72;
                                                        }
                                                        {
                                                            {
                                                                {
                                                                    uint64_t t333 = gm_l57;
                                                                    gm_l58 = t333;
                                                                    {
                                                                        {
                                                                            uint64_t t334 = gm_l58;
                                                                            uint64_t t335 = GM_LD(uint64_t, (((uint8_t *)&gm_l55) + 8));
                                                                            uint8_t t336 = (t334 < t335);
                                                                            if (!t336) {
                                                                                goto gm_lb78;
                                                                            }
                                                                            void * t337 = GM_LD(void *, ((uint8_t *)&gm_l55));
                                                                            uint64_t t338 = GM_LD(uint64_t, (((uint8_t *)&gm_l55) + 8));
                                                                            uint64_t t339 = gm_l58;
                                                                            uint8_t t340 = GM_LD(uint8_t, (((uint8_t *)t337) + (uintptr_t)t339 * 1));
                                                                            if (!t340) {
                                                                                goto gm_lb78;
                                                                            }
                                                                            void * t341 = gm_l0;
                                                                            uint32_t t342 = gm_l50;
                                                                            void * t343 = GM_LD(void *, (((uint8_t *)&gm_l54) + 8));
                                                                            uint64_t t344 = GM_LD(uint64_t, ((((uint8_t *)&gm_l54) + 8) + 8));
                                                                            uint64_t t345 = gm_l58;
                                                                            void * t346 = (void *)(((uint8_t *)t343) + (uintptr_t)t345 * 88);
                                                                            void * t347 = GM_LD(void *, ((uint8_t *)&F.l26));
                                                                            uint64_t t348 = GM_LD(uint64_t, (((uint8_t *)&F.l26) + 8));
                                                                            uint64_t t349 = gm_l47;
                                                                            if ((uintptr_t)t349 >= (uintptr_t)(t348)) gm_trap_index((uintptr_t)t349, t348, gm_srca29n3fkrpbdt, 115, 85);
                                                                            gm_b16_8 t350 = GM_LD(gm_b16_8, (((uint8_t *)t347) + (uintptr_t)t349 * 16));
                                                                            gm_b16_8 t351 = GM_LD(gm_b16_8, ((uint8_t *)&t350));
                                                                            void * t352 = (void *)((uint8_t *)&F.l33);
                                                                            uint8_t t353 = gm_f_steady_arg_7904qej((void *)NULL, cx, NULL, t341, t342, t346, t351, t352);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_lad;
                                                                            }
                                                                            uint8_t t354 = (uint8_t)!t353;
                                                                            if (!t354) {
                                                                                goto gm_lb78;
                                                                            }
                                                                            void * t355 = GM_LD(void *, ((uint8_t *)&gm_l55));
                                                                            uint64_t t356 = GM_LD(uint64_t, (((uint8_t *)&gm_l55) + 8));
                                                                            uint64_t t357 = gm_l58;
                                                                            GM_ST(uint8_t, (((uint8_t *)t355) + (uintptr_t)t357 * 1), ((uint8_t)0));
                                                                            goto gm_lb77;
                                                                        }
gm_lb78:;
                                                                    }
gm_lb77:;
                                                                    goto gm_lb75;
                                                                }
gm_lb76:;
                                                                gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 114, 13);
                                                            }
gm_lb75:;
                                                        }
gm_lb74:;
                                                        uint64_t t358 = gm_l57;
                                                        uint64_t r359; bool o360 = __builtin_add_overflow(t358, ((uint64_t)1ULL), &r359);
                                                        uint64_t t361 = r359;
                                                        gm_l57 = t361;
                                                    }
                                                }
gm_lb72:;
                                                goto gm_lb70;
                                            }
gm_lb71:;
                                            gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 112, 9);
                                        }
gm_lb70:;
                                    }
gm_lb69:;
                                    uint64_t t362 = gm_l53;
                                    uint64_t r363; bool o364 = __builtin_add_overflow(t362, ((uint64_t)1ULL), &r363);
                                    uint64_t t365 = r363;
                                    gm_l53 = t365;
                                }
                            }
gm_lb67:;
                            goto gm_lb65;
                        }
gm_lb66:;
                        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 106, 5);
                    }
gm_lb65:;
                }
gm_lb64:;
                uint64_t t366 = gm_l46;
                uint64_t r367; bool o368 = __builtin_add_overflow(t366, ((uint64_t)1ULL), &r367);
                uint64_t t369 = r367;
                gm_l46 = t369;
            }
        }
gm_lb62:;
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l34), 40);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_direct_calls_tgjq7k6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_direct_calls_tgjq7k6 F;
    void * gm_l0; /* e */
    gm_b16_8 gm_l3; /* args */
    uint32_t gm_l4; /* f */
    GM_STACK_CHECK(&F, 149u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    gm_tftb1odc739t5 gm_l5;
    {
        {
            void * t6 = gm_l0;
            uint8_t t7 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)34ULL));
            if (!t8) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l2), ((((uint8_t *)t6) + 8) + 8), 40);
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((((uint8_t *)t6) + 8) + 8) + 40));
            gm_l3 = t9;
            {
                {
                    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                    uint8_t t11 = (uint8_t)(t10 == ((uint8_t)0ULL));
                    if (!t11) {
                        goto gm_lb4;
                    }
                    uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)&F.l2) + 8));
                    gm_l4 = t12;
                    void * t13 = F.l1;
                    uint32_t t14 = gm_l4;
                    gm_b16_8 t15 = gm_l3;
                    gm_tiilmfd16v3fc t16;
                    GM_ST(uint32_t, (uint8_t *)&t16 + 4, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t16 + 0), t14);
                    GM_ST(gm_b16_8, ((uint8_t *)&t16 + 8), t15);
                    gm_tftb1odc739t5 t17 = gm_f_push_f8cmgo9(t13, &t16);
                    gm_l5 = t17;
                    uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
                    if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srca29n3fkrpbdt, 330, 31);
                    goto gm_lb3;
                }
gm_lb4:;
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t19 = gm_l0;
    gm_closure t20 = ((gm_closure){(void *)gm_f_fn_cmb4ji5, (void *)&F.h, cx});
    gm_f_for_each_child_fm49s73(t19, t20);
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
inline __attribute__((aligned(4))) void gm_f_each_cond_hcnk23r(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_each_cond_hcnk23r F;
    gm_b16_8 gm_l0; /* cond */
    gm_closure gm_l1; /* f */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tlr9dp305htm7 gm_l4; /* c */
    void * gm_l5; /* x */
    void * gm_l6; /* p */
    void * gm_l7; /* v */
    GM_STACK_CHECK(&F, 373u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t10 = gm_l0;
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
                                gm_closure t20 = gm_l1;
                                void * t21 = gm_l5;
                                gm_closure t22 = t20;
                                gm_node *ccx23 = t22.cx;
                                void * t24 = t22.code;
                                gm_b0_1 t25;
                                if (GM_PTR_TAGGED(t24)) {
                                    ((void (*)(void *))GM_PTR_UNTAG(t24))(t21);
                                } else {
                                    t25 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t24)(t22.env, ccx23, NULL, t21);
                                }
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t27 = (uint8_t)(t26 == ((uint8_t)1ULL));
                                if (!t27) {
                                    goto gm_lb8;
                                }
                                void * t28 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                                gm_l6 = t28;
                                void * t29 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                                gm_l7 = t29;
                                void * t30 = gm_l6;
                                gm_closure t31 = gm_l1;
                                gm_f_each_pat_vg13dgd(t30, t31);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_closure t32 = gm_l1;
                                void * t33 = gm_l7;
                                gm_closure t34 = t32;
                                gm_node *ccx35 = t34.cx;
                                void * t36 = t34.code;
                                gm_b0_1 t37;
                                if (GM_PTR_TAGGED(t36)) {
                                    ((void (*)(void *))GM_PTR_UNTAG(t36))(t33);
                                } else {
                                    t37 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t36)(t34.env, ccx35, NULL, t33);
                                }
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 214, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 213, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t38 = gm_l3;
            uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
            uint64_t t41 = r39;
            gm_l3 = t41;
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
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_each_pat_vg13dgd(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_each_pat_vg13dgd F;
    void * gm_l0; /* p */
    void * gm_l2; /* e */
    GM_STACK_CHECK(&F, 130u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    {
        {
            void * t4 = gm_l0;
            uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)2ULL));
            if (!t6) {
                goto gm_lb2;
            }
            void * t7 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
            gm_l2 = t7;
            gm_closure t8 = F.l1;
            void * t9 = gm_l2;
            gm_closure t10 = t8;
            gm_node *ccx11 = t10.cx;
            void * t12 = t10.code;
            gm_b0_1 t13;
            if (GM_PTR_TAGGED(t12)) {
                ((void (*)(void *))GM_PTR_UNTAG(t12))(t9);
            } else {
                t13 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t12)(t10.env, ccx11, NULL, t9);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t14 = gm_l0;
    gm_closure t15 = ((gm_closure){(void *)gm_f_fn_gnspmq2, (void *)&F.h, cx});
    gm_f_for_each_subpat_arlrtrm(t14, t15);
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
__attribute__((aligned(4))) void gm_f_each_stmt_tt8e8go(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_each_stmt_tt8e8go F;
    void * gm_l0; /* b */
    gm_closure gm_l1; /* f */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_trcmmd12cefb1 gm_l4; /* s */
    void * gm_l5; /* init */
    void * gm_l6; /* x */
    void * gm_l7; /* x */
    void * gm_l8; /* p */
    void * gm_l9; /* v */
    gm_b16_8 gm_l10; /* ps */
    void * gm_l11; /* v */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    void * gm_l14; /* p */
    void * gm_l15; /* x */
    void * gm_l16; /* x */
    void * gm_l17; /* x */
    void * gm_l18; /* t */
    GM_STACK_CHECK(&F, 1009u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t27 = gm_l0;
    gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)t27));
    gm_l2 = t28;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t29 = gm_l3;
            uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t31 = (t29 < t30);
            if (!t31) {
                goto gm_lb1;
            }
            {
                void * t32 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t34 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&gm_l4), (((uint8_t *)t32) + (uintptr_t)t34 * 32), 32);
                        {
                            {
                                uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t36 = (uint8_t)(t35 == ((uint8_t)0ULL));
                                if (!t36) {
                                    goto gm_lb7;
                                }
                                void * t37 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                                gm_l5 = t37;
                                {
                                    {
                                        uint8_t t38 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l5)) != 0);
                                        if (!t38) {
                                            goto gm_lb9;
                                        }
                                        void * t39 = GM_LD(void *, ((uint8_t *)&gm_l5));
                                        gm_l6 = t39;
                                        gm_closure t40 = gm_l1;
                                        void * t41 = gm_l6;
                                        gm_closure t42 = t40;
                                        gm_node *ccx43 = t42.cx;
                                        void * t44 = t42.code;
                                        gm_b0_1 t45;
                                        if (GM_PTR_TAGGED(t44)) {
                                            ((void (*)(void *))GM_PTR_UNTAG(t44))(t41);
                                        } else {
                                            t45 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t44)(t42.env, ccx43, NULL, t41);
                                        }
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb8;
                                    }
gm_lb9:;
                                }
gm_lb8:;
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t47 = (uint8_t)(t46 == ((uint8_t)1ULL));
                                if (!t47) {
                                    goto gm_lb10;
                                }
                                void * t48 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 16));
                                gm_l7 = t48;
                                gm_closure t49 = gm_l1;
                                void * t50 = gm_l7;
                                gm_closure t51 = t49;
                                gm_node *ccx52 = t51.cx;
                                void * t53 = t51.code;
                                gm_b0_1 t54;
                                if (GM_PTR_TAGGED(t53)) {
                                    ((void (*)(void *))GM_PTR_UNTAG(t53))(t50);
                                } else {
                                    t54 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t53)(t51.env, ccx52, NULL, t50);
                                }
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb10:;
                            {
                                uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t56 = (uint8_t)(t55 == ((uint8_t)2ULL));
                                if (!t56) {
                                    goto gm_lb11;
                                }
                                void * t57 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                                gm_l8 = t57;
                                void * t58 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                                gm_l9 = t58;
                                gm_closure t59 = gm_l1;
                                void * t60 = gm_l8;
                                gm_closure t61 = t59;
                                gm_node *ccx62 = t61.cx;
                                void * t63 = t61.code;
                                gm_b0_1 t64;
                                if (GM_PTR_TAGGED(t63)) {
                                    ((void (*)(void *))GM_PTR_UNTAG(t63))(t60);
                                } else {
                                    t64 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t63)(t61.env, ccx62, NULL, t60);
                                }
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_closure t65 = gm_l1;
                                void * t66 = gm_l9;
                                gm_closure t67 = t65;
                                gm_node *ccx68 = t67.cx;
                                void * t69 = t67.code;
                                gm_b0_1 t70;
                                if (GM_PTR_TAGGED(t69)) {
                                    ((void (*)(void *))GM_PTR_UNTAG(t69))(t66);
                                } else {
                                    t70 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t69)(t67.env, ccx68, NULL, t66);
                                }
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb11:;
                            {
                                uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t72 = (uint8_t)(t71 == ((uint8_t)3ULL));
                                if (!t72) {
                                    goto gm_lb12;
                                }
                                gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
                                gm_l10 = t73;
                                void * t74 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 16));
                                gm_l11 = t74;
                                gm_b16_8 t75 = gm_l10;
                                gm_l12 = t75;
                                gm_l13 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t76 = gm_l13;
                                        uint64_t t77 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                        uint8_t t78 = (t76 < t77);
                                        if (!t78) {
                                            goto gm_lb13;
                                        }
                                        {
                                            void * t79 = GM_LD(void *, ((uint8_t *)&gm_l12));
                                            uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                            uint64_t t81 = gm_l13;
                                            {
                                                {
                                                    void * t82 = GM_LD(void *, (((uint8_t *)t79) + (uintptr_t)t81 * 8));
                                                    gm_l14 = t82;
                                                    {
                                                        {
                                                            uint8_t t83 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l14)) != 0);
                                                            if (!t83) {
                                                                goto gm_lb19;
                                                            }
                                                            void * t84 = GM_LD(void *, ((uint8_t *)&gm_l14));
                                                            gm_l15 = t84;
                                                            gm_closure t85 = gm_l1;
                                                            void * t86 = gm_l15;
                                                            gm_closure t87 = t85;
                                                            gm_node *ccx88 = t87.cx;
                                                            void * t89 = t87.code;
                                                            gm_b0_1 t90;
                                                            if (GM_PTR_TAGGED(t89)) {
                                                                ((void (*)(void *))GM_PTR_UNTAG(t89))(t86);
                                                            } else {
                                                                t90 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t89)(t87.env, ccx88, NULL, t86);
                                                            }
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            goto gm_lb18;
                                                        }
gm_lb19:;
                                                    }
gm_lb18:;
                                                    goto gm_lb16;
                                                }
gm_lb17:;
                                                gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 241, 17);
                                            }
gm_lb16:;
                                        }
gm_lb15:;
                                        uint64_t t91 = gm_l13;
                                        uint64_t r92; bool o93 = __builtin_add_overflow(t91, ((uint64_t)1ULL), &r92);
                                        uint64_t t94 = r92;
                                        gm_l13 = t94;
                                    }
                                }
gm_lb13:;
                                gm_closure t95 = gm_l1;
                                void * t96 = gm_l11;
                                gm_closure t97 = t95;
                                gm_node *ccx98 = t97.cx;
                                void * t99 = t97.code;
                                gm_b0_1 t100;
                                if (GM_PTR_TAGGED(t99)) {
                                    ((void (*)(void *))GM_PTR_UNTAG(t99))(t96);
                                } else {
                                    t100 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t99)(t97.env, ccx98, NULL, t96);
                                }
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb12:;
                            {
                                {
                                    {
                                        uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                        uint8_t t102 = (uint8_t)(t101 == ((uint8_t)4ULL));
                                        if (!t102) {
                                            goto gm_lb22;
                                        }
                                        void * t103 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                                        gm_l16 = t103;
                                        goto gm_lb21;
                                    }
gm_lb22:;
                                    {
                                        uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                        uint8_t t105 = (uint8_t)(t104 == ((uint8_t)5ULL));
                                        if (!t105) {
                                            goto gm_lb23;
                                        }
                                        void * t106 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                                        gm_l16 = t106;
                                        goto gm_lb21;
                                    }
gm_lb23:;
                                    goto gm_lb20;
                                }
gm_lb21:;
                                gm_closure t107 = gm_l1;
                                void * t108 = gm_l16;
                                gm_closure t109 = t107;
                                gm_node *ccx110 = t109.cx;
                                void * t111 = t109.code;
                                gm_b0_1 t112;
                                if (GM_PTR_TAGGED(t111)) {
                                    ((void (*)(void *))GM_PTR_UNTAG(t111))(t108);
                                } else {
                                    t112 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t111)(t109.env, ccx110, NULL, t108);
                                }
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb20:;
                            gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 233, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 232, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t113 = gm_l3;
            uint64_t r114; bool o115 = __builtin_add_overflow(t113, ((uint64_t)1ULL), &r114);
            uint64_t t116 = r114;
            gm_l3 = t116;
        }
    }
gm_lb1:;
    {
        {
            void * t117 = gm_l0;
            uint8_t t118 = (uint8_t)(GM_LD(void *, (((uint8_t *)t117) + 16)) != 0);
            if (!t118) {
                goto gm_lb25;
            }
            void * t119 = GM_LD(void *, (((uint8_t *)t117) + 16));
            gm_l18 = t119;
            gm_closure t120 = gm_l1;
            void * t121 = gm_l18;
            gm_closure t122 = t120;
            gm_node *ccx123 = t122.cx;
            void * t124 = t122.code;
            gm_b0_1 t125;
            if (GM_PTR_TAGGED(t124)) {
                ((void (*)(void *))GM_PTR_UNTAG(t124))(t121);
            } else {
                t125 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t124)(t122.env, ccx123, NULL, t121);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb24;
        }
gm_lb25:;
    }
gm_lb24:;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_05d3agq(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cmb4ji5(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_djrq2n1(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_gnspmq2(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_n57683i(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_p8hr8ki(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_fn_refs_tvld7g2(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_refs_tvld7g2 F;
    void * gm_l0; /* e */
    uint32_t gm_l2; /* f */
    uint32_t gm_l3; /* f */
    GM_STACK_CHECK(&F, 124u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    void * t6 = gm_l0;
    {
        {
            uint8_t t7 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)26ULL));
            if (!t8) {
                goto gm_lb2;
            }
            uint32_t t9 = GM_LD(uint32_t, ((((uint8_t *)t6) + 8) + 8));
            gm_l2 = t9;
            void * t10 = GM_LD(void *, ((uint8_t *)&F.l1));
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&F.l1) + 8));
            uint32_t t12 = gm_l2;
            uint64_t t13 = (uint64_t)t12;
            if ((uintptr_t)t13 >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)t13, t11, gm_srca29n3fkrpbdt, 293, 24);
            GM_ST(uint8_t, (((uint8_t *)t10) + (uintptr_t)t13 * 1), ((uint8_t)1));
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
            uint8_t t15 = (uint8_t)(t14 == ((uint8_t)27ULL));
            if (!t15) {
                goto gm_lb3;
            }
            uint32_t t16 = GM_LD(uint32_t, ((((uint8_t *)t6) + 8) + 8));
            gm_l3 = t16;
            void * t17 = GM_LD(void *, ((uint8_t *)&F.l1));
            uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&F.l1) + 8));
            uint32_t t19 = gm_l3;
            uint64_t t20 = (uint64_t)t19;
            if ((uintptr_t)t20 >= (uintptr_t)(t18)) gm_trap_index((uintptr_t)t20, t18, gm_srca29n3fkrpbdt, 294, 26);
            GM_ST(uint8_t, (((uint8_t *)t17) + (uintptr_t)t20 * 1), ((uint8_t)1));
            goto gm_lb1;
        }
gm_lb3:;
        {
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 292, 5);
    }
gm_lb1:;
    void * t21 = gm_l0;
    gm_closure t22 = ((gm_closure){(void *)gm_f_fn_vg5vaqp, (void *)&F.h, cx});
    gm_f_for_each_child_fm49s73(t21, t22);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_url0b1k(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vg5vaqp(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_pure_l81kp3k(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_is_pure_l81kp3k F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    uint8_t gm_l2; /* m */
    void * gm_l3; /* def */
    uint8_t gm_l4; /* r */
    void * gm_l5; /* b */
    GM_STACK_CHECK(&F, 423u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tqs5p4n2tq19v gm_l6;
    gm_tftb1odc739t5 gm_l7;
    uint8_t gm_l8;
    uint8_t gm_l9;
    uint8_t gm_l10;
    uint8_t gm_l11;
    uint8_t gm_l12;
    gm_tftb1odc739t5 gm_l13;
    {
        {
            void * t9 = gm_l0;
            void * t10 = (void *)(((uint8_t *)t9) + 2448);
            uint32_t t11 = gm_l1;
            gm_tqs5p4n2tq19v t12 = gm_f_get_oe801t5(t10, t11);
            gm_l6 = t12;
            uint8_t t13 = (uint8_t)((((uint8_t *)&gm_l6))[1] != 0);
            if (!t13) {
                goto gm_lb2;
            }
            uint8_t t14 = GM_LD(uint8_t, ((uint8_t *)&gm_l6));
            gm_l2 = t14;
            uint8_t t15 = gm_l2;
            uint8_t t16 = ((t15 == ((uint8_t)3ULL)));
            F.ret = t16;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t17 = gm_l0;
    void * t18 = (void *)(((uint8_t *)t17) + 2448);
    uint32_t t19 = gm_l1;
    gm_tftb1odc739t5 t20 = gm_f_put_iucnhfg(t18, t19, ((uint8_t)1ULL));
    gm_l7 = t20;
    uint8_t t21 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (!t21) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srca29n3fkrpbdt, 145, 5);
    void * t22 = gm_l0;
    void * t23 = GM_LD(void *, ((uint8_t *)t22));
    uint32_t t24 = gm_l1;
    void * t25 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t23, t24);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t25;
    {
        void * t26 = gm_l3;
        uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t26) + 96) + 0));
        uint8_t t28 = (uint8_t)((t27 == ((uint8_t)0ULL)));
        gm_tq9qng821udh4 t29 = GM_LD(gm_tq9qng821udh4, (((uint8_t *)t26) + 96));
        gm_tq9qng821udh4 t30;
        memset(&t30, 0, sizeof t30);
        GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
        if (t28) {
            gm_l10 = ((uint8_t)1);
        } else {
            void * t31 = gm_l3;
            uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)t31) + 96) + 0));
            uint8_t t33 = (uint8_t)((t32 == ((uint8_t)1ULL)));
            gm_tq9qng821udh4 t34 = GM_LD(gm_tq9qng821udh4, (((uint8_t *)t31) + 96));
            gm_tq9qng821udh4 t35;
            memset(&t35, 0, sizeof t35);
            GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)1ULL));
            gm_l10 = t33;
        }
    }
    uint8_t t36 = gm_l10;
    if (t36) {
        void * t37 = gm_l3;
        {
            {
                uint8_t t38 = (uint8_t)(((((uint8_t *)t37) + 88))[4] != 0);
                if (t38) {
                    goto gm_lb4;
                }
                gm_l11 = ((uint8_t)1);
                goto gm_lb3;
            }
gm_lb4:;
            {
                gm_l11 = ((uint8_t)0);
                goto gm_lb3;
            }
gm_lb5:;
            gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 147, 59);
        }
gm_lb3:;
        uint8_t t39 = gm_l11;
        gm_l9 = t39;
    } else {
        gm_l9 = ((uint8_t)0);
    }
    uint8_t t40 = gm_l9;
    if (t40) {
        void * t41 = gm_l3;
        uint64_t t42 = GM_LD(uint64_t, ((((uint8_t *)t41) + 72) + 8));
        uint8_t t43 = ((t42 == ((uint64_t)0ULL)));
        gm_l8 = t43;
    } else {
        gm_l8 = ((uint8_t)0);
    }
    uint8_t t44 = gm_l8;
    gm_l4 = t44;
    uint8_t t45 = gm_l4;
    if (t45) {
        gm_l4 = ((uint8_t)0);
        {
            {
                void * t46 = gm_l3;
                uint8_t t47 = (uint8_t)(GM_LD(void *, (((uint8_t *)t46) + 152)) != 0);
                if (!t47) {
                    goto gm_lb7;
                }
                void * t48 = GM_LD(void *, (((uint8_t *)t46) + 152));
                gm_l5 = t48;
                void * t49 = gm_l0;
                void * t50 = gm_l5;
                uint8_t t51 = gm_f_pure_expr_fee5b14((void *)NULL, cx, NULL, t49, t50);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l4 = t51;
                goto gm_lb6;
            }
gm_lb7:;
        }
gm_lb6:;
    }
    void * t52 = gm_l0;
    void * t53 = (void *)(((uint8_t *)t52) + 2448);
    uint32_t t54 = gm_l1;
    uint8_t t55 = gm_l4;
    if (t55) {
        gm_l12 = ((uint8_t)3ULL);
    } else {
        gm_l12 = ((uint8_t)2ULL);
    }
    uint8_t t56 = gm_l12;
    gm_tftb1odc739t5 t57 = gm_f_put_iucnhfg(t53, t54, t56);
    gm_l13 = t57;
    uint8_t t58 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
    if (!t58) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srca29n3fkrpbdt, 152, 5);
    uint8_t t59 = gm_l4;
    F.ret = t59;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_mutated_in_block_26pupm0(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_mutated_in_block_26pupm0 F;
    void * gm_l1; /* b */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_trcmmd12cefb1 gm_l5; /* s */
    void * gm_l6; /* place */
    uint32_t gm_l7; /* l */
    gm_b16_8 gm_l8; /* places */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    void * gm_l11; /* p */
    void * gm_l12; /* x */
    uint32_t gm_l13; /* l */
    GM_STACK_CHECK(&F, 507u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    F.l2 = a2;
    gm_t9805qtonbsag gm_l14;
    gm_tftb1odc739t5 gm_l15;
    gm_t9805qtonbsag gm_l16;
    gm_tftb1odc739t5 gm_l17;
    void * t22 = gm_l1;
    gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)t22));
    gm_l3 = t23;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t25 = gm_l4;
            uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t27 = (t25 < t26);
            if (!t27) {
                goto gm_lb1;
            }
            {
                void * t28 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t30 = gm_l4;
                {
                    {
                        memmove(((uint8_t *)&gm_l5), (((uint8_t *)t28) + (uintptr_t)t30 * 32), 32);
                        {
                            {
                                uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                uint8_t t32 = (uint8_t)(t31 == ((uint8_t)2ULL));
                                if (!t32) {
                                    goto gm_lb7;
                                }
                                void * t33 = GM_LD(void *, (((uint8_t *)&gm_l5) + 8));
                                gm_l6 = t33;
                                {
                                    {
                                        void * t34 = F.l0;
                                        void * t35 = GM_LD(void *, ((uint8_t *)t34));
                                        void * t36 = GM_LD(void *, ((uint8_t *)t35));
                                        void * t37 = gm_l6;
                                        gm_t9805qtonbsag t38 = gm_f_root_local_40frg7f(t36, t37);
                                        gm_l14 = t38;
                                        uint8_t t39 = (uint8_t)((((uint8_t *)&gm_l14))[4] != 0);
                                        if (!t39) {
                                            goto gm_lb9;
                                        }
                                        uint32_t t40 = GM_LD(uint32_t, ((uint8_t *)&gm_l14));
                                        gm_l7 = t40;
                                        void * t41 = F.l2;
                                        uint32_t t42 = gm_l7;
                                        gm_tftb1odc739t5 t43 = gm_f_push_im9d4ro(t41, t42);
                                        gm_l15 = t43;
                                        uint8_t t44 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                                        if (!t44) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srca29n3fkrpbdt, 276, 73);
                                        goto gm_lb8;
                                    }
gm_lb9:;
                                }
gm_lb8:;
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                uint8_t t46 = (uint8_t)(t45 == ((uint8_t)3ULL));
                                if (!t46) {
                                    goto gm_lb10;
                                }
                                gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
                                gm_l8 = t47;
                                gm_b16_8 t48 = gm_l8;
                                gm_l9 = t48;
                                gm_l10 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t50 = gm_l10;
                                        uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                        uint8_t t52 = (t50 < t51);
                                        if (!t52) {
                                            goto gm_lb11;
                                        }
                                        {
                                            void * t53 = GM_LD(void *, ((uint8_t *)&gm_l9));
                                            uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                            uint64_t t55 = gm_l10;
                                            {
                                                {
                                                    void * t56 = GM_LD(void *, (((uint8_t *)t53) + (uintptr_t)t55 * 8));
                                                    gm_l11 = t56;
                                                    {
                                                        {
                                                            uint8_t t57 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l11)) != 0);
                                                            if (!t57) {
                                                                goto gm_lb17;
                                                            }
                                                            void * t58 = GM_LD(void *, ((uint8_t *)&gm_l11));
                                                            gm_l12 = t58;
                                                            {
                                                                {
                                                                    void * t59 = F.l0;
                                                                    void * t60 = GM_LD(void *, ((uint8_t *)t59));
                                                                    void * t61 = GM_LD(void *, ((uint8_t *)t60));
                                                                    void * t62 = gm_l12;
                                                                    gm_t9805qtonbsag t63 = gm_f_root_local_40frg7f(t61, t62);
                                                                    gm_l16 = t63;
                                                                    uint8_t t64 = (uint8_t)((((uint8_t *)&gm_l16))[4] != 0);
                                                                    if (!t64) {
                                                                        goto gm_lb19;
                                                                    }
                                                                    uint32_t t65 = GM_LD(uint32_t, ((uint8_t *)&gm_l16));
                                                                    gm_l13 = t65;
                                                                    void * t66 = F.l2;
                                                                    uint32_t t67 = gm_l13;
                                                                    gm_tftb1odc739t5 t68 = gm_f_push_im9d4ro(t66, t67);
                                                                    gm_l17 = t68;
                                                                    uint8_t t69 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                                                                    if (!t69) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srca29n3fkrpbdt, 280, 58);
                                                                    goto gm_lb18;
                                                                }
gm_lb19:;
                                                            }
gm_lb18:;
                                                            goto gm_lb16;
                                                        }
gm_lb17:;
                                                    }
gm_lb16:;
                                                    goto gm_lb14;
                                                }
gm_lb15:;
                                                gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 278, 17);
                                            }
gm_lb14:;
                                        }
gm_lb13:;
                                        uint64_t t70 = gm_l10;
                                        uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
                                        uint64_t t73 = r71;
                                        gm_l10 = t73;
                                    }
                                }
gm_lb11:;
                                goto gm_lb6;
                            }
gm_lb10:;
                            {
                                goto gm_lb6;
                            }
gm_lb20:;
                            gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 275, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 274, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t74 = gm_l4;
            uint64_t r75; bool o76 = __builtin_add_overflow(t74, ((uint64_t)1ULL), &r75);
            uint64_t t77 = r75;
            gm_l4 = t77;
        }
    }
gm_lb1:;
    void * t78 = gm_l1;
    gm_closure t79 = ((gm_closure){(void *)gm_f_fn_05d3agq, (void *)&F.h, cx});
    gm_f_each_stmt_tt8e8go(t78, t79);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_mutated_locals_tltgkrt(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_mutated_locals_tltgkrt F;
    void * gm_l1; /* e */
    void * gm_l3; /* p */
    uint32_t gm_l4; /* l */
    gm_tao63pk3r18v5 gm_l5; /* sl */
    uint32_t gm_l6; /* l */
    void * gm_l7; /* b */
    void * gm_l9; /* s */
    GM_STACK_CHECK(&F, 397u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    F.l2 = a2;
    gm_t9805qtonbsag gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_t9805qtonbsag gm_l12;
    gm_tftb1odc739t5 gm_l13;
    void * t14 = gm_l1;
    {
        {
            uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
            uint8_t t16 = (uint8_t)(t15 == ((uint8_t)24ULL));
            if (!t16) {
                goto gm_lb2;
            }
            void * t17 = GM_LD(void *, ((((uint8_t *)t14) + 8) + 8));
            gm_l3 = t17;
            {
                {
                    void * t18 = F.l0;
                    void * t19 = GM_LD(void *, ((uint8_t *)t18));
                    void * t20 = GM_LD(void *, ((uint8_t *)t19));
                    void * t21 = gm_l3;
                    gm_t9805qtonbsag t22 = gm_f_root_local_40frg7f(t20, t21);
                    gm_l10 = t22;
                    uint8_t t23 = (uint8_t)((((uint8_t *)&gm_l10))[4] != 0);
                    if (!t23) {
                        goto gm_lb4;
                    }
                    uint32_t t24 = GM_LD(uint32_t, ((uint8_t *)&gm_l10));
                    gm_l4 = t24;
                    void * t25 = F.l2;
                    uint32_t t26 = gm_l4;
                    gm_tftb1odc739t5 t27 = gm_f_push_im9d4ro(t25, t26);
                    gm_l11 = t27;
                    uint8_t t28 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                    if (!t28) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srca29n3fkrpbdt, 256, 58);
                    goto gm_lb3;
                }
gm_lb4:;
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
            uint8_t t30 = (uint8_t)(t29 == ((uint8_t)21ULL));
            if (!t30) {
                goto gm_lb5;
            }
            memmove(((uint8_t *)&gm_l5), ((((uint8_t *)t14) + 8) + 8), 32);
            {
                {
                    void * t31 = F.l0;
                    void * t32 = GM_LD(void *, ((uint8_t *)t31));
                    void * t33 = GM_LD(void *, ((uint8_t *)t32));
                    void * t34 = GM_LD(void *, ((uint8_t *)&gm_l5));
                    gm_t9805qtonbsag t35 = gm_f_root_local_40frg7f(t33, t34);
                    gm_l12 = t35;
                    uint8_t t36 = (uint8_t)((((uint8_t *)&gm_l12))[4] != 0);
                    if (!t36) {
                        goto gm_lb7;
                    }
                    uint32_t t37 = GM_LD(uint32_t, ((uint8_t *)&gm_l12));
                    gm_l6 = t37;
                    void * t38 = F.l2;
                    uint32_t t39 = gm_l6;
                    gm_tftb1odc739t5 t40 = gm_f_push_im9d4ro(t38, t39);
                    gm_l13 = t40;
                    uint8_t t41 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                    if (!t41) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srca29n3fkrpbdt, 257, 64);
                    goto gm_lb6;
                }
gm_lb7:;
            }
gm_lb6:;
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
            uint8_t t43 = (uint8_t)(t42 == ((uint8_t)46ULL));
            if (!t43) {
                goto gm_lb8;
            }
            void * t44 = GM_LD(void *, ((((uint8_t *)t14) + 8) + 8));
            gm_l7 = t44;
            void * t45 = F.l0;
            void * t46 = gm_l7;
            void * t47 = F.l2;
            gm_b0_1 t48 = gm_f_mutated_in_block_26pupm0((void *)NULL, cx, NULL, t45, t46, t47);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
gm_lb8:;
        {
            uint8_t t49 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
            uint8_t t50 = (uint8_t)(t49 == ((uint8_t)48ULL));
            if (!t50) {
                goto gm_lb9;
            }
            memmove(((uint8_t *)&F.l8), ((((uint8_t *)t14) + 8) + 8), 40);
            gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 8));
            gm_closure t52 = ((gm_closure){(void *)gm_f_fn_n57683i, (void *)&F.h, cx});
            gm_f_each_cond_hcnk23r(t51, t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t53 = F.l0;
            void * t54 = GM_LD(void *, (((uint8_t *)&F.l8) + 24));
            void * t55 = F.l2;
            gm_b0_1 t56 = gm_f_mutated_in_block_26pupm0((void *)NULL, cx, NULL, t53, t54, t55);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                {
                    uint8_t t57 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l8) + 32)) != 0);
                    if (!t57) {
                        goto gm_lb11;
                    }
                    void * t58 = GM_LD(void *, (((uint8_t *)&F.l8) + 32));
                    gm_l9 = t58;
                    void * t59 = F.l0;
                    void * t60 = gm_l9;
                    void * t61 = F.l2;
                    gm_b0_1 t62 = gm_f_mutated_locals_tltgkrt((void *)NULL, cx, NULL, t59, t60, t61);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb10;
                }
gm_lb11:;
            }
gm_lb10:;
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
gm_lb9:;
        {
            goto gm_lb1;
        }
gm_lb12:;
        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 255, 5);
    }
gm_lb1:;
    void * t63 = gm_l1;
    gm_closure t64 = ((gm_closure){(void *)gm_f_fn_djrq2n1, (void *)&F.h, cx});
    gm_f_for_each_child_fm49s73(t63, t64);
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
extern __attribute__((aligned(4))) uint8_t gm_f_own_place_peh2qe9(void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_pure_block_94oo6k4(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_pure_block_94oo6k4 F;
    void * gm_l0; /* g */
    void * gm_l1; /* b */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_trcmmd12cefb1 gm_l4; /* s */
    uint8_t gm_l5; /* ok */
    void * gm_l6; /* place */
    void * gm_l7; /* value */
    gm_b16_8 gm_l8; /* places */
    void * gm_l9; /* value */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    void * gm_l12; /* p */
    void * gm_l13; /* x */
    void * gm_l14; /* init */
    void * gm_l15; /* x */
    void * gm_l16; /* init */
    void * gm_l17; /* x */
    void * gm_l18; /* x */
    void * gm_l19; /* t */
    GM_STACK_CHECK(&F, 666u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l20;
    uint8_t gm_l21;
    uint8_t gm_l22;
    uint8_t gm_l23;
    void * t27 = gm_l1;
    gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)t27));
    gm_l2 = t28;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t29 = gm_l3;
            uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t31 = (t29 < t30);
            if (!t31) {
                goto gm_lb1;
            }
            {
                void * t32 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t34 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&gm_l4), (((uint8_t *)t32) + (uintptr_t)t34 * 32), 32);
                        gm_l5 = ((uint8_t)1);
                        {
                            {
                                uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t36 = (uint8_t)(t35 == ((uint8_t)2ULL));
                                if (!t36) {
                                    goto gm_lb7;
                                }
                                void * t37 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                                gm_l6 = t37;
                                void * t38 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                                gm_l7 = t38;
                                void * t39 = gm_l0;
                                void * t40 = gm_l6;
                                uint8_t t41 = gm_f_own_place_peh2qe9(t39, t40);
                                if (t41) {
                                    void * t42 = gm_l0;
                                    void * t43 = gm_l6;
                                    uint8_t t44 = gm_f_pure_expr_fee5b14((void *)NULL, cx, NULL, t42, t43);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l21 = t44;
                                } else {
                                    gm_l21 = ((uint8_t)0);
                                }
                                uint8_t t45 = gm_l21;
                                if (t45) {
                                    void * t46 = gm_l0;
                                    void * t47 = gm_l7;
                                    uint8_t t48 = gm_f_pure_expr_fee5b14((void *)NULL, cx, NULL, t46, t47);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l20 = t48;
                                } else {
                                    gm_l20 = ((uint8_t)0);
                                }
                                uint8_t t49 = gm_l20;
                                gm_l5 = t49;
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t51 = (uint8_t)(t50 == ((uint8_t)3ULL));
                                if (!t51) {
                                    goto gm_lb8;
                                }
                                gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
                                gm_l8 = t52;
                                void * t53 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 16));
                                gm_l9 = t53;
                                gm_b16_8 t54 = gm_l8;
                                gm_l10 = t54;
                                gm_l11 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t55 = gm_l11;
                                        uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                        uint8_t t57 = (t55 < t56);
                                        if (!t57) {
                                            goto gm_lb9;
                                        }
                                        {
                                            void * t58 = GM_LD(void *, ((uint8_t *)&gm_l10));
                                            uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                            uint64_t t60 = gm_l11;
                                            {
                                                {
                                                    void * t61 = GM_LD(void *, (((uint8_t *)t58) + (uintptr_t)t60 * 8));
                                                    gm_l12 = t61;
                                                    {
                                                        {
                                                            uint8_t t62 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l12)) != 0);
                                                            if (!t62) {
                                                                goto gm_lb15;
                                                            }
                                                            void * t63 = GM_LD(void *, ((uint8_t *)&gm_l12));
                                                            gm_l13 = t63;
                                                            void * t64 = gm_l0;
                                                            void * t65 = gm_l13;
                                                            uint8_t t66 = gm_f_own_place_peh2qe9(t64, t65);
                                                            if (t66) {
                                                                void * t67 = gm_l0;
                                                                void * t68 = gm_l13;
                                                                uint8_t t69 = gm_f_pure_expr_fee5b14((void *)NULL, cx, NULL, t67, t68);
                                                                if (gm_unw.kind) {
                                                                    goto gm_lad;
                                                                }
                                                                gm_l22 = t69;
                                                            } else {
                                                                gm_l22 = ((uint8_t)0);
                                                            }
                                                            uint8_t t70 = gm_l22;
                                                            uint8_t t71 = (uint8_t)!t70;
                                                            if (t71) {
                                                                gm_l5 = ((uint8_t)0);
                                                            }
                                                            goto gm_lb14;
                                                        }
gm_lb15:;
                                                    }
gm_lb14:;
                                                    goto gm_lb12;
                                                }
gm_lb13:;
                                                gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 190, 17);
                                            }
gm_lb12:;
                                        }
gm_lb11:;
                                        uint64_t t72 = gm_l11;
                                        uint64_t r73; bool o74 = __builtin_add_overflow(t72, ((uint64_t)1ULL), &r73);
                                        uint64_t t75 = r73;
                                        gm_l11 = t75;
                                    }
                                }
gm_lb9:;
                                uint8_t t76 = gm_l5;
                                if (t76) {
                                    void * t77 = gm_l0;
                                    void * t78 = gm_l9;
                                    uint8_t t79 = gm_f_pure_expr_fee5b14((void *)NULL, cx, NULL, t77, t78);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l23 = t79;
                                } else {
                                    gm_l23 = ((uint8_t)0);
                                }
                                uint8_t t80 = gm_l23;
                                gm_l5 = t80;
                                goto gm_lb6;
                            }
gm_lb8:;
                            {
                                uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t82 = (uint8_t)(t81 == ((uint8_t)0ULL));
                                if (!t82) {
                                    goto gm_lb16;
                                }
                                void * t83 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                                gm_l14 = t83;
                                {
                                    {
                                        uint8_t t84 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l14)) != 0);
                                        if (!t84) {
                                            goto gm_lb18;
                                        }
                                        void * t85 = GM_LD(void *, ((uint8_t *)&gm_l14));
                                        gm_l15 = t85;
                                        void * t86 = gm_l0;
                                        void * t87 = gm_l15;
                                        uint8_t t88 = gm_f_pure_expr_fee5b14((void *)NULL, cx, NULL, t86, t87);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l5 = t88;
                                        goto gm_lb17;
                                    }
gm_lb18:;
                                }
gm_lb17:;
                                goto gm_lb6;
                            }
gm_lb16:;
                            {
                                uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t90 = (uint8_t)(t89 == ((uint8_t)1ULL));
                                if (!t90) {
                                    goto gm_lb19;
                                }
                                void * t91 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 16));
                                gm_l16 = t91;
                                void * t92 = gm_l0;
                                void * t93 = gm_l16;
                                uint8_t t94 = gm_f_pure_expr_fee5b14((void *)NULL, cx, NULL, t92, t93);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l5 = t94;
                                goto gm_lb6;
                            }
gm_lb19:;
                            {
                                {
                                    {
                                        uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                        uint8_t t96 = (uint8_t)(t95 == ((uint8_t)4ULL));
                                        if (!t96) {
                                            goto gm_lb22;
                                        }
                                        void * t97 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                                        gm_l17 = t97;
                                        goto gm_lb21;
                                    }
gm_lb22:;
                                    {
                                        uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                        uint8_t t99 = (uint8_t)(t98 == ((uint8_t)5ULL));
                                        if (!t99) {
                                            goto gm_lb23;
                                        }
                                        void * t100 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                                        gm_l17 = t100;
                                        goto gm_lb21;
                                    }
gm_lb23:;
                                    goto gm_lb20;
                                }
gm_lb21:;
                                void * t101 = gm_l0;
                                void * t102 = gm_l17;
                                uint8_t t103 = gm_f_pure_expr_fee5b14((void *)NULL, cx, NULL, t101, t102);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l5 = t103;
                                goto gm_lb6;
                            }
gm_lb20:;
                            gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 187, 9);
                        }
gm_lb6:;
                        uint8_t t104 = gm_l5;
                        uint8_t t105 = (uint8_t)!t104;
                        if (t105) {
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 185, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t106 = gm_l3;
            uint64_t r107; bool o108 = __builtin_add_overflow(t106, ((uint64_t)1ULL), &r107);
            uint64_t t109 = r107;
            gm_l3 = t109;
        }
    }
gm_lb1:;
    {
        {
            void * t110 = gm_l1;
            uint8_t t111 = (uint8_t)(GM_LD(void *, (((uint8_t *)t110) + 16)) != 0);
            if (!t111) {
                goto gm_lb25;
            }
            void * t112 = GM_LD(void *, (((uint8_t *)t110) + 16));
            gm_l19 = t112;
            void * t113 = gm_l0;
            void * t114 = gm_l19;
            uint8_t t115 = gm_f_pure_expr_fee5b14((void *)NULL, cx, NULL, t113, t114);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t115;
            goto gm_fret;
        }
gm_lb25:;
    }
gm_lb24:;
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
__attribute__((aligned(4))) uint8_t gm_f_pure_expr_fee5b14(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_pure_expr_fee5b14 F;
    void * gm_l1; /* e */
    void * gm_l3; /* b */
    void * gm_l5; /* s */
    uint32_t gm_l7; /* h */
    gm_t8588ma2cr9d7 gm_l8; /* op */
    GM_STACK_CHECK(&F, 281u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    F.l2 = ((uint8_t)1);
    void * t42 = gm_l1;
    {
        {
            uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t42) + 8) + 0));
            uint8_t t44 = (uint8_t)(t43 == ((uint8_t)46ULL));
            if (!t44) {
                goto gm_lb2;
            }
            void * t45 = GM_LD(void *, ((((uint8_t *)t42) + 8) + 8));
            gm_l3 = t45;
            void * t46 = F.l0;
            void * t47 = gm_l3;
            uint8_t t48 = gm_f_pure_block_94oo6k4((void *)NULL, cx, NULL, t46, t47);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t48;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t49 = GM_LD(uint8_t, ((((uint8_t *)t42) + 8) + 0));
            uint8_t t50 = (uint8_t)(t49 == ((uint8_t)48ULL));
            if (!t50) {
                goto gm_lb3;
            }
            memmove(((uint8_t *)&F.l4), ((((uint8_t *)t42) + 8) + 8), 40);
            F.l2 = ((uint8_t)1);
            gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 8));
            gm_closure t52 = ((gm_closure){(void *)gm_f_fn_url0b1k, (void *)&F.h, cx});
            gm_f_each_cond_hcnk23r(t51, t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t53 = F.l2;
            uint8_t t54 = (uint8_t)!t53;
            if (t54) {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            void * t55 = F.l0;
            void * t56 = GM_LD(void *, (((uint8_t *)&F.l4) + 24));
            uint8_t t57 = gm_f_pure_block_94oo6k4((void *)NULL, cx, NULL, t55, t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t58 = (uint8_t)!t57;
            if (t58) {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            {
                {
                    uint8_t t59 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l4) + 32)) != 0);
                    if (!t59) {
                        goto gm_lb5;
                    }
                    void * t60 = GM_LD(void *, (((uint8_t *)&F.l4) + 32));
                    gm_l5 = t60;
                    void * t61 = F.l0;
                    void * t62 = gm_l5;
                    uint8_t t63 = gm_f_pure_expr_fee5b14((void *)NULL, cx, NULL, t61, t62);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t63;
                    goto gm_fret;
                }
gm_lb5:;
            }
gm_lb4:;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb3:;
        {
            uint8_t t64 = GM_LD(uint8_t, ((((uint8_t *)t42) + 8) + 0));
            uint8_t t65 = (uint8_t)(t64 == ((uint8_t)34ULL));
            if (!t65) {
                goto gm_lb6;
            }
            memmove(((uint8_t *)&F.l6), ((((uint8_t *)t42) + 8) + 8), 40);
            {
                {
                    uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                    uint8_t t67 = (uint8_t)(t66 == ((uint8_t)0ULL));
                    if (!t67) {
                        goto gm_lb8;
                    }
                    uint32_t t68 = GM_LD(uint32_t, (((uint8_t *)&F.l6) + 8));
                    gm_l7 = t68;
                    void * t69 = F.l0;
                    uint32_t t70 = gm_l7;
                    uint8_t t71 = gm_f_is_pure_l81kp3k((void *)NULL, cx, NULL, t69, t70);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l2 = t71;
                    goto gm_lb7;
                }
gm_lb8:;
                {
                    F.l2 = ((uint8_t)0);
                    goto gm_lb7;
                }
gm_lb9:;
                gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 168, 29);
            }
gm_lb7:;
            goto gm_lb1;
        }
gm_lb6:;
        {
            {
                {
                    uint8_t t72 = GM_LD(uint8_t, ((((uint8_t *)t42) + 8) + 0));
                    uint8_t t73 = (uint8_t)(t72 == ((uint8_t)59ULL));
                    if (!t73) {
                        goto gm_lb12;
                    }
                    goto gm_lb11;
                }
gm_lb12:;
                {
                    uint8_t t74 = GM_LD(uint8_t, ((((uint8_t *)t42) + 8) + 0));
                    uint8_t t75 = (uint8_t)(t74 == ((uint8_t)27ULL));
                    if (!t75) {
                        goto gm_lb13;
                    }
                    goto gm_lb11;
                }
gm_lb13:;
                {
                    uint8_t t76 = GM_LD(uint8_t, ((((uint8_t *)t42) + 8) + 0));
                    uint8_t t77 = (uint8_t)(t76 == ((uint8_t)28ULL));
                    if (!t77) {
                        goto gm_lb14;
                    }
                    goto gm_lb11;
                }
gm_lb14:;
                {
                    uint8_t t78 = GM_LD(uint8_t, ((((uint8_t *)t42) + 8) + 0));
                    uint8_t t79 = (uint8_t)(t78 == ((uint8_t)53ULL));
                    if (!t79) {
                        goto gm_lb15;
                    }
                    goto gm_lb11;
                }
gm_lb15:;
                {
                    uint8_t t80 = GM_LD(uint8_t, ((((uint8_t *)t42) + 8) + 0));
                    uint8_t t81 = (uint8_t)(t80 == ((uint8_t)54ULL));
                    if (!t81) {
                        goto gm_lb16;
                    }
                    goto gm_lb11;
                }
gm_lb16:;
                {
                    uint8_t t82 = GM_LD(uint8_t, ((((uint8_t *)t42) + 8) + 0));
                    uint8_t t83 = (uint8_t)(t82 == ((uint8_t)55ULL));
                    if (!t83) {
                        goto gm_lb17;
                    }
                    goto gm_lb11;
                }
gm_lb17:;
                {
                    uint8_t t84 = GM_LD(uint8_t, ((((uint8_t *)t42) + 8) + 0));
                    uint8_t t85 = (uint8_t)(t84 == ((uint8_t)56ULL));
                    if (!t85) {
                        goto gm_lb18;
                    }
                    goto gm_lb11;
                }
gm_lb18:;
                {
                    uint8_t t86 = GM_LD(uint8_t, ((((uint8_t *)t42) + 8) + 0));
                    uint8_t t87 = (uint8_t)(t86 == ((uint8_t)58ULL));
                    if (!t87) {
                        goto gm_lb19;
                    }
                    goto gm_lb11;
                }
gm_lb19:;
                {
                    uint8_t t88 = GM_LD(uint8_t, ((((uint8_t *)t42) + 8) + 0));
                    uint8_t t89 = (uint8_t)(t88 == ((uint8_t)17ULL));
                    if (!t89) {
                        goto gm_lb20;
                    }
                    goto gm_lb11;
                }
gm_lb20:;
                {
                    uint8_t t90 = GM_LD(uint8_t, ((((uint8_t *)t42) + 8) + 0));
                    uint8_t t91 = (uint8_t)(t90 == ((uint8_t)16ULL));
                    if (!t91) {
                        goto gm_lb21;
                    }
                    goto gm_lb11;
                }
gm_lb21:;
                goto gm_lb10;
            }
gm_lb11:;
            F.l2 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb10:;
        {
            uint8_t t92 = GM_LD(uint8_t, ((((uint8_t *)t42) + 8) + 0));
            uint8_t t93 = (uint8_t)(t92 == ((uint8_t)33ULL));
            if (!t93) {
                goto gm_lb22;
            }
            gm_t8588ma2cr9d7 t94 = GM_LD(gm_t8588ma2cr9d7, ((((uint8_t *)t42) + 8) + 8));
            gm_l8 = t94;
            {
                {
                    {
                        {
                            uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                            uint8_t t96 = (uint8_t)(t95 == ((uint8_t)66ULL));
                            if (!t96) {
                                goto gm_lb26;
                            }
                            goto gm_lb25;
                        }
gm_lb26:;
                        {
                            uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                            uint8_t t98 = (uint8_t)(t97 == ((uint8_t)67ULL));
                            if (!t98) {
                                goto gm_lb27;
                            }
                            goto gm_lb25;
                        }
gm_lb27:;
                        {
                            uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                            uint8_t t100 = (uint8_t)(t99 == ((uint8_t)68ULL));
                            if (!t100) {
                                goto gm_lb28;
                            }
                            goto gm_lb25;
                        }
gm_lb28:;
                        {
                            uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                            uint8_t t102 = (uint8_t)(t101 == ((uint8_t)69ULL));
                            if (!t102) {
                                goto gm_lb29;
                            }
                            goto gm_lb25;
                        }
gm_lb29:;
                        {
                            uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                            uint8_t t104 = (uint8_t)(t103 == ((uint8_t)72ULL));
                            if (!t104) {
                                goto gm_lb30;
                            }
                            goto gm_lb25;
                        }
gm_lb30:;
                        {
                            uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                            uint8_t t106 = (uint8_t)(t105 == ((uint8_t)73ULL));
                            if (!t106) {
                                goto gm_lb31;
                            }
                            goto gm_lb25;
                        }
gm_lb31:;
                        {
                            uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                            uint8_t t108 = (uint8_t)(t107 == ((uint8_t)74ULL));
                            if (!t108) {
                                goto gm_lb32;
                            }
                            goto gm_lb25;
                        }
gm_lb32:;
                        {
                            uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                            uint8_t t110 = (uint8_t)(t109 == ((uint8_t)75ULL));
                            if (!t110) {
                                goto gm_lb33;
                            }
                            goto gm_lb25;
                        }
gm_lb33:;
                        {
                            uint8_t t111 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                            uint8_t t112 = (uint8_t)(t111 == ((uint8_t)76ULL));
                            if (!t112) {
                                goto gm_lb34;
                            }
                            goto gm_lb25;
                        }
gm_lb34:;
                        {
                            uint8_t t113 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                            uint8_t t114 = (uint8_t)(t113 == ((uint8_t)77ULL));
                            if (!t114) {
                                goto gm_lb35;
                            }
                            goto gm_lb25;
                        }
gm_lb35:;
                        {
                            uint8_t t115 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                            uint8_t t116 = (uint8_t)(t115 == ((uint8_t)78ULL));
                            if (!t116) {
                                goto gm_lb36;
                            }
                            goto gm_lb25;
                        }
gm_lb36:;
                        {
                            uint8_t t117 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                            uint8_t t118 = (uint8_t)(t117 == ((uint8_t)79ULL));
                            if (!t118) {
                                goto gm_lb37;
                            }
                            goto gm_lb25;
                        }
gm_lb37:;
                        {
                            uint8_t t119 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                            uint8_t t120 = (uint8_t)(t119 == ((uint8_t)80ULL));
                            if (!t120) {
                                goto gm_lb38;
                            }
                            goto gm_lb25;
                        }
gm_lb38:;
                        goto gm_lb24;
                    }
gm_lb25:;
                    F.l2 = ((uint8_t)0);
                    goto gm_lb23;
                }
gm_lb24:;
                {
                    goto gm_lb23;
                }
gm_lb39:;
                gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 173, 28);
            }
gm_lb23:;
            goto gm_lb1;
        }
gm_lb22:;
        {
            goto gm_lb1;
        }
gm_lb40:;
        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 158, 5);
    }
gm_lb1:;
    uint8_t t121 = F.l2;
    uint8_t t122 = (uint8_t)!t121;
    if (t122) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t123 = gm_l1;
    gm_closure t124 = ((gm_closure){(void *)gm_f_fn_p8hr8ki, (void *)&F.h, cx});
    gm_f_for_each_child_fm49s73(t123, t124);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t125 = F.l2;
    F.ret = t125;
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
inline __attribute__((aligned(4))) uint8_t gm_f_steady_arg_7904qej(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, gm_b16_8 a3, void * a4) {
    struct gm_fr_steady_arg_7904qej F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* caller */
    void * gm_l2; /* a */
    gm_b16_8 gm_l3; /* written */
    void * gm_l4; /* captured */
    uint32_t gm_l5; /* l */
    GM_STACK_CHECK(&F, 189u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_t9805qtonbsag gm_l6;
    uint8_t gm_l7;
    uint8_t gm_l8;
    gm_tqs5p4n2tq19v gm_l9;
    void * t14 = gm_l2;
    {
        {
            {
                {
                    uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
                    uint8_t t16 = (uint8_t)(t15 == ((uint8_t)35ULL));
                    if (!t16) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
                    uint8_t t18 = (uint8_t)(t17 == ((uint8_t)36ULL));
                    if (!t18) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
                    uint8_t t20 = (uint8_t)(t19 == ((uint8_t)34ULL));
                    if (!t20) {
                        goto gm_lb6;
                    }
                    goto gm_lb3;
                }
gm_lb6:;
                goto gm_lb2;
            }
gm_lb3:;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb2:;
        {
            goto gm_lb1;
        }
gm_lb7:;
        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 127, 5);
    }
gm_lb1:;
    {
        {
            void * t21 = gm_l0;
            void * t22 = GM_LD(void *, ((uint8_t *)t21));
            void * t23 = GM_LD(void *, ((uint8_t *)t22));
            void * t24 = gm_l2;
            gm_t9805qtonbsag t25 = gm_f_root_local_40frg7f(t23, t24);
            gm_l6 = t25;
            uint8_t t26 = (uint8_t)((((uint8_t *)&gm_l6))[4] != 0);
            if (!t26) {
                goto gm_lb9;
            }
            uint32_t t27 = GM_LD(uint32_t, ((uint8_t *)&gm_l6));
            gm_l5 = t27;
            uint32_t t28 = gm_l5;
            gm_b16_8 t29 = gm_l3;
            uint8_t t30 = 0;
            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t29) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t29))) + i * 4, ((uint8_t *)&t28))) { t30 = 1; break; }
            uint8_t t31 = (uint8_t)(!t30);
            if (t31) {
                void * t32 = gm_l4;
                uint32_t t33 = gm_l1;
                uint32_t t34 = gm_l5;
                gm_tcc1rq4n1avb2 t35;
                GM_ST(uint32_t, ((uint8_t *)&t35 + 0), t33);
                GM_ST(uint32_t, ((uint8_t *)&t35 + 4), t34);
                gm_tqs5p4n2tq19v t36 = gm_f_get_ag7vhtq(t32, t35);
                gm_l9 = t36;
                {
                    {
                        uint8_t t37 = (uint8_t)((((uint8_t *)&gm_l9))[1] != 0);
                        if (t37) {
                            goto gm_lb11;
                        }
                        gm_l8 = ((uint8_t)1);
                        goto gm_lb10;
                    }
gm_lb11:;
                    {
                        gm_l8 = ((uint8_t)0);
                        goto gm_lb10;
                    }
gm_lb12:;
                    gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 135, 66);
                }
gm_lb10:;
                uint8_t t38 = gm_l8;
                gm_l7 = t38;
            } else {
                gm_l7 = ((uint8_t)0);
            }
            uint8_t t39 = gm_l7;
            F.ret = t39;
            goto gm_fret;
        }
gm_lb9:;
    }
gm_lb8:;
    void * t40 = gm_l0;
    void * t41 = gm_l2;
    uint8_t t42 = gm_f_is_const_arg_98lfonj((void *)NULL, cx, NULL, t40, t41);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t42;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_steady_params_ng2sd7r(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_steady_params_ng2sd7r F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint8_t gm_l7; /* x */
    GM_STACK_CHECK(&F, 365u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l8;
    gm_tftb1odc739t5 gm_l9;
    void * t10 = gm_l0;
    {
        {
            uint8_t t11 = (uint8_t)(((((uint8_t *)t10) + 2504))[40] != 0);
            if (t11) {
                goto gm_lb2;
            }
            gm_l8 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l8 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 53, 8);
    }
gm_lb1:;
    uint8_t t12 = gm_l8;
    if (t12) {
        {
            void * t13 = gm_l0;
            gm_f_compute_steady_c2v2ffl((void *)&F.l2, (void *)NULL, cx, NULL, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t14 = gm_l0;
            gm_trqfvmjtt4sij t15 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l2));
            gm_tqetrgo0q24b5 t16;
            GM_ST(uint8_t, (uint8_t *)&t16 + 41, 0);
            GM_ST(uint16_t, (uint8_t *)&t16 + 42, 0);
            GM_ST(uint32_t, (uint8_t *)&t16 + 44, 0);
            GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t16), t15);
            ((uint8_t *)&t16)[40] = 1;
            GM_ST(gm_tqetrgo0q24b5, (((uint8_t *)t14) + 2504), t16);
        }
    }
    {
        void * t17 = gm_l0;
        uint8_t t18 = (uint8_t)(((((uint8_t *)t17) + 2504))[40] != 0);
        if (!t18) gm_trap("unwrap of null", gm_srca29n3fkrpbdt, 57, 12);
        memmove(((uint8_t *)&F.l3), (((uint8_t *)t17) + 2504), 40);
    }
    {
        gm_trqfvmjtt4sij t19;
        gm_f_list_s83jn2q((void *)&t19, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l4 = t19;
    }
    void * t20 = GM_LD(void *, ((uint8_t *)&F.l3));
    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
    uint32_t t22 = gm_l1;
    uint64_t t23 = (uint64_t)t22;
    if ((uintptr_t)t23 >= (uintptr_t)(t21)) gm_trap_index((uintptr_t)t23, t21, gm_srca29n3fkrpbdt, 60, 14);
    gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)t20) + (uintptr_t)t23 * 16));
    gm_l5 = t24;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t25 = gm_l6;
            uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t27 = (t25 < t26);
            if (!t27) {
                goto gm_lb4;
            }
            {
                void * t28 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t30 = gm_l6;
                {
                    {
                        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)t28) + (uintptr_t)t30 * 1));
                        gm_l7 = t31;
                        void * t32 = (void *)((uint8_t *)&F.l4);
                        uint8_t t33 = gm_l7;
                        gm_tftb1odc739t5 t34 = gm_f_push_vd1290p(t32, t33);
                        gm_l9 = t34;
                        uint8_t t35 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                        if (!t35) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srca29n3fkrpbdt, 60, 36);
                        goto gm_lb7;
                    }
gm_lb8:;
                    gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 60, 5);
                }
gm_lb7:;
            }
gm_lb6:;
            uint64_t t36 = gm_l6;
            uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
            uint64_t t39 = r37;
            gm_l6 = t39;
        }
    }
gm_lb4:;
    gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
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
__attribute__((aligned(4))) gm_b0_1 gm_f_value_fn_refs_jv5jhbq(void *env, gm_node *cx, gm_node *inst, const void * a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_value_fn_refs_jv5jhbq F;
    gm_b16_8 gm_l1; /* out */
    void * gm_l2; /* seen */
    uint32_t gm_l3; /* f */
    void * gm_l4; /* a */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_tkaapcii86udk gm_l8; /* pl */
    gm_tkaapcii86udk gm_l9; /* start */
    void * gm_l10; /* x */
    void * gm_l11; /* x */
    void * gm_l12; /* x */
    void * gm_l13; /* payload */
    void * gm_l14; /* x */
    GM_STACK_CHECK(&F, 478u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)11ULL));
            if (!t22) {
                goto gm_lb2;
            }
            uint32_t t23 = GM_LD(uint32_t, (((uint8_t *)&F.l0) + 16));
            gm_l3 = t23;
            void * t24 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint32_t t26 = gm_l3;
            uint64_t t27 = (uint64_t)t26;
            if ((uintptr_t)t27 >= (uintptr_t)(t25)) gm_trap_index((uintptr_t)t27, t25, gm_srca29n3fkrpbdt, 304, 21);
            GM_ST(uint8_t, (((uint8_t *)t24) + (uintptr_t)t27 * 1), ((uint8_t)1));
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
            uint8_t t29 = (uint8_t)(t28 == ((uint8_t)3ULL));
            if (!t29) {
                goto gm_lb3;
            }
            void * t30 = GM_LD(void *, (((uint8_t *)&F.l0) + 16));
            gm_l4 = t30;
            void * t31 = gm_l4;
            gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)t31));
            gm_l5 = t32;
            gm_l6 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t33 = gm_l6;
                    uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint8_t t35 = (t33 < t34);
                    if (!t35) {
                        goto gm_lb4;
                    }
                    {
                        void * t36 = GM_LD(void *, ((uint8_t *)&gm_l5));
                        uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                        uint64_t t38 = gm_l6;
                        {
                            {
                                {
                                    memmove(((uint8_t *)&F.l7), (((uint8_t *)t36) + (uintptr_t)t38 * 48), 48);
                                }
                                {
                                    gm_b16_8 t39 = gm_l1;
                                    void * t40 = gm_l2;
                                    gm_b0_1 t41 = gm_f_value_fn_refs_jv5jhbq((void *)NULL, cx, NULL, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l7)), t39, t40);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                goto gm_lb7;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 305, 20);
                        }
gm_lb7:;
                    }
gm_lb6:;
                    uint64_t t42 = gm_l6;
                    uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
                    uint64_t t45 = r43;
                    gm_l6 = t45;
                }
            }
gm_lb4:;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
            uint8_t t47 = (uint8_t)(t46 == ((uint8_t)4ULL));
            if (!t47) {
                goto gm_lb9;
            }
            memmove(((uint8_t *)&gm_l8), (((uint8_t *)&F.l0) + 16), 24);
            void * t48 = GM_LD(void *, ((uint8_t *)&gm_l8));
            gm_b16_8 t49 = gm_l1;
            void * t50 = gm_l2;
            gm_b0_1 t51 = gm_f_cell_fn_refs_6icb85l((void *)NULL, cx, NULL, t48, t49, t50);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb9:;
        {
            uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
            uint8_t t53 = (uint8_t)(t52 == ((uint8_t)5ULL));
            if (!t53) {
                goto gm_lb10;
            }
            memmove(((uint8_t *)&gm_l9), (((uint8_t *)&F.l0) + 16), 24);
            void * t54 = GM_LD(void *, ((uint8_t *)&gm_l9));
            gm_b16_8 t55 = gm_l1;
            void * t56 = gm_l2;
            gm_b0_1 t57 = gm_f_cell_fn_refs_6icb85l((void *)NULL, cx, NULL, t54, t55, t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb10:;
        {
            {
                {
                    uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
                    uint8_t t59 = (uint8_t)(t58 == ((uint8_t)7ULL));
                    if (!t59) {
                        goto gm_lb13;
                    }
                    void * t60 = GM_LD(void *, (((uint8_t *)&F.l0) + 16));
                    gm_l10 = t60;
                    goto gm_lb12;
                }
gm_lb13:;
                {
                    uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
                    uint8_t t62 = (uint8_t)(t61 == ((uint8_t)8ULL));
                    if (!t62) {
                        goto gm_lb14;
                    }
                    void * t63 = GM_LD(void *, (((uint8_t *)&F.l0) + 16));
                    gm_l10 = t63;
                    goto gm_lb12;
                }
gm_lb14:;
                {
                    uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
                    uint8_t t65 = (uint8_t)(t64 == ((uint8_t)9ULL));
                    if (!t65) {
                        goto gm_lb15;
                    }
                    void * t66 = GM_LD(void *, (((uint8_t *)&F.l0) + 16));
                    gm_l10 = t66;
                    goto gm_lb12;
                }
gm_lb15:;
                goto gm_lb11;
            }
gm_lb12:;
            void * t67 = gm_l10;
            gm_b16_8 t68 = gm_l1;
            void * t69 = gm_l2;
            gm_b0_1 t70 = gm_f_value_fn_refs_jv5jhbq((void *)NULL, cx, NULL, &(*(gm_t6evimt84lndq *)((uint8_t *)t67)), t68, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb11:;
        {
            uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
            uint8_t t72 = (uint8_t)(t71 == ((uint8_t)10ULL));
            if (!t72) {
                goto gm_lb16;
            }
            void * t73 = GM_LD(void *, ((((uint8_t *)&F.l0) + 16) + 8));
            gm_l13 = t73;
            {
                {
                    uint8_t t74 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l13)) != 0);
                    if (!t74) {
                        goto gm_lb18;
                    }
                    void * t75 = GM_LD(void *, ((uint8_t *)&gm_l13));
                    gm_l14 = t75;
                    void * t76 = gm_l14;
                    gm_b16_8 t77 = gm_l1;
                    void * t78 = gm_l2;
                    gm_b0_1 t79 = gm_f_value_fn_refs_jv5jhbq((void *)NULL, cx, NULL, &(*(gm_t6evimt84lndq *)((uint8_t *)t76)), t77, t78);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb17;
                }
gm_lb18:;
            }
gm_lb17:;
            goto gm_lb1;
        }
gm_lb16:;
        {
            goto gm_lb1;
        }
gm_lb19:;
        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 303, 5);
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

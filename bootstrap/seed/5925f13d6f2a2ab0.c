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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct gm_cs7e9ppsd384bj { uint32_t f0; gm_csguankko4lmhd f1; } gm_cs7e9ppsd384bj;
_Static_assert(sizeof(gm_cs9ufftihtbpo2) == 8 && _Alignof(gm_cs9ufftihtbpo2) == 4 && offsetof(gm_cs9ufftihtbpo2, f0) == 0 && offsetof(gm_cs9ufftihtbpo2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { uint32_t f0; uint32_t f1; gm_b16_8 f2; gm_tl874t2atnlh3 f3; uint32_t f4; } gm_t0ut88vjdgrt1;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; uint8_t v2; gm_t9qkh0rsooc8d v3; gm_t9qkh0rsooc8d v4; gm_tcc1rq4n1avb2 v5; uint32_t v6; uint32_t v7; } f1; } gm_t8588ma2cr9d7;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
typedef struct { uint32_t f0; gm_tl874t2atnlh3 f1; } gm_t9d4sf0vqn7dt;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `(e: ^const Expr, l: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_b16_8 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_tdgfduqjheqdm;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; uint64_t f3; } gm_thddbqb6snabm;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t p3[3]; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tsoie1ppgdvcg;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_cs7e9ppsd384bj) == 8 && _Alignof(gm_cs7e9ppsd384bj) == 4 && offsetof(gm_cs7e9ppsd384bj, f0) == 0 && offsetof(gm_cs7e9ppsd384bj, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0ut88vjdgrt1) == 40 && _Alignof(gm_t0ut88vjdgrt1) == 8 && offsetof(gm_t0ut88vjdgrt1, f0) == 0 && offsetof(gm_t0ut88vjdgrt1, f1) == 4 && offsetof(gm_t0ut88vjdgrt1, f2) == 8 && offsetof(gm_t0ut88vjdgrt1, f3) == 24 && offsetof(gm_t0ut88vjdgrt1, f4) == 36, "layout of `MonoRequest`");
typedef struct { gm_tiilmfd16v3fc f0; gm_b16_8 f1; } gm_t1cs2fjnrqap1;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `(string, usize)`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t0ut88vjdgrt1 f0; uint8_t f1; uint8_t pe[7]; } gm_t4fb05ktf8oll;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_tpe1hv3fekn05 v1; } f1; } gm_t4lnf71mruqpt;
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
typedef struct { void * f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t7bjbmukv26e7;
_Static_assert(sizeof(gm_t8588ma2cr9d7) == 12 && _Alignof(gm_t8588ma2cr9d7) == 4 && offsetof(gm_t8588ma2cr9d7, f0) == 0 && offsetof(gm_t8588ma2cr9d7, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_t9d4sf0vqn7dt) == 16 && _Alignof(gm_t9d4sf0vqn7dt) == 4 && offsetof(gm_t9d4sf0vqn7dt, f0) == 0 && offsetof(gm_t9d4sf0vqn7dt, f1) == 4, "layout of `TraceCall`");
typedef struct { gm_t8588ma2cr9d7 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_t9q8lhljes74c;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
typedef struct { gm_b16_8 f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; gm_tl874t2atnlh3 f3; uint8_t f4; uint8_t p5[3]; void * f5; gm_b16_8 f6; void * f7; } gm_tblkd491pk4cr;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { gm_tibupt4dmcqai f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; gm_t79rbdv4rafe5 f2; uint8_t f3; uint8_t pe[7]; } gm_tcfiotghd1s5k;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tdgfduqjheqdm) == 64 && _Alignof(gm_tdgfduqjheqdm) == 8 && offsetof(gm_tdgfduqjheqdm, f0) == 0 && offsetof(gm_tdgfduqjheqdm, f1) == 8 && offsetof(gm_tdgfduqjheqdm, f2) == 24 && offsetof(gm_tdgfduqjheqdm, f3) == 40 && offsetof(gm_tdgfduqjheqdm, f4) == 48 && offsetof(gm_tdgfduqjheqdm, f5) == 56, "layout of `FnDef`");
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thddbqb6snabm) == 48 && _Alignof(gm_thddbqb6snabm) == 8 && offsetof(gm_thddbqb6snabm, f0) == 0 && offsetof(gm_thddbqb6snabm, f1) == 16 && offsetof(gm_thddbqb6snabm, f2) == 24 && offsetof(gm_thddbqb6snabm, f3) == 40, "layout of `(string, u64, string, u64)`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tsoie1ppgdvcg) == 40 && _Alignof(gm_tsoie1ppgdvcg) == 8 && offsetof(gm_tsoie1ppgdvcg, f0) == 0 && offsetof(gm_tsoie1ppgdvcg, f1) == 16 && offsetof(gm_tsoie1ppgdvcg, f2) == 20 && offsetof(gm_tsoie1ppgdvcg, f3) == 24, "layout of `LocalDecl`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
_Static_assert(sizeof(gm_t1cs2fjnrqap1) == 40 && _Alignof(gm_t1cs2fjnrqap1) == 8 && offsetof(gm_t1cs2fjnrqap1, f0) == 0 && offsetof(gm_t1cs2fjnrqap1, f1) == 24, "layout of `HandlerLit`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4fb05ktf8oll) == 48 && _Alignof(gm_t4fb05ktf8oll) == 8 && offsetof(gm_t4fb05ktf8oll, f0) == 0 && offsetof(gm_t4fb05ktf8oll, f1) == 40, "layout of `?MonoRequest`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_t4lnf71mruqpt) == 32 && _Alignof(gm_t4lnf71mruqpt) == 8 && offsetof(gm_t4lnf71mruqpt, f0) == 0 && offsetof(gm_t4lnf71mruqpt, f1) == 8, "layout of `HandlerValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t7bjbmukv26e7) == 64 && _Alignof(gm_t7bjbmukv26e7) == 8 && offsetof(gm_t7bjbmukv26e7, f0) == 0 && offsetof(gm_t7bjbmukv26e7, f1) == 8 && offsetof(gm_t7bjbmukv26e7, f2) == 24 && offsetof(gm_t7bjbmukv26e7, f3) == 48, "layout of `WhenItems`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t9q8lhljes74c) == 32 && _Alignof(gm_t9q8lhljes74c) == 8 && offsetof(gm_t9q8lhljes74c, f0) == 0 && offsetof(gm_t9q8lhljes74c, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tblkd491pk4cr) == 144 && _Alignof(gm_tblkd491pk4cr) == 8 && offsetof(gm_tblkd491pk4cr, f0) == 0 && offsetof(gm_tblkd491pk4cr, f1) == 16 && offsetof(gm_tblkd491pk4cr, f2) == 56 && offsetof(gm_tblkd491pk4cr, f3) == 96 && offsetof(gm_tblkd491pk4cr, f4) == 108 && offsetof(gm_tblkd491pk4cr, f5) == 112 && offsetof(gm_tblkd491pk4cr, f6) == 120 && offsetof(gm_tblkd491pk4cr, f7) == 136, "layout of `MonoCx`");
_Static_assert(sizeof(gm_tcfiotghd1s5k) == 88 && _Alignof(gm_tcfiotghd1s5k) == 8 && offsetof(gm_tcfiotghd1s5k, f0) == 0 && offsetof(gm_tcfiotghd1s5k, f1) == 24 && offsetof(gm_tcfiotghd1s5k, f2) == 40 && offsetof(gm_tcfiotghd1s5k, f3) == 80, "layout of `TestDecl`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
typedef struct { gm_tet9guhev5r4u f0; uint8_t f1; uint8_t pe[7]; } gm_teqc715vjig79;
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t419mo45g7j0a v0; void * v1; } f1; } gm_th1g69d7vpipg;
typedef struct { gm_tbci5ospk5nnn f0; uint8_t f1; uint8_t pe[7]; } gm_thrdpodrs8pgt;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tv2uf4leil5hq v0; gm_t7bjbmukv26e7 v1; void * v2; gm_tcfiotghd1s5k v3; void * v4; } f1; } gm_tkoope2qgegd9;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_t428ue2u3hf5g v1; } f1; } gm_toa2vhe3l4hfr;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; uint64_t f2; gm_b16_8 f3; gm_t88rcucg1b7uk f4; uint8_t p5[4]; gm_tnpifv20irnph f5; gm_b16_8 f6; gm_tqu3m2l3n8mnc f7; uint8_t f8; uint8_t pe[7]; } gm_tqqhoivahirr0;
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_teqc715vjig79) == 64 && _Alignof(gm_teqc715vjig79) == 8 && offsetof(gm_teqc715vjig79, f0) == 0 && offsetof(gm_teqc715vjig79, f1) == 56, "layout of `?IntrinsicExpr`");
_Static_assert(sizeof(gm_th1g69d7vpipg) == 40 && _Alignof(gm_th1g69d7vpipg) == 8 && offsetof(gm_th1g69d7vpipg, f0) == 0 && offsetof(gm_th1g69d7vpipg, f1) == 8, "layout of `WriteSink`");
_Static_assert(sizeof(gm_thrdpodrs8pgt) == 56 && _Alignof(gm_thrdpodrs8pgt) == 8 && offsetof(gm_thrdpodrs8pgt, f0) == 0 && offsetof(gm_thrdpodrs8pgt, f1) == 48, "layout of `?Int`");
_Static_assert(sizeof(gm_tkoope2qgegd9) == 96 && _Alignof(gm_tkoope2qgegd9) == 8 && offsetof(gm_tkoope2qgegd9, f0) == 0 && offsetof(gm_tkoope2qgegd9, f1) == 8, "layout of `ItemKind`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_toa2vhe3l4hfr) == 40 && _Alignof(gm_toa2vhe3l4hfr) == 8 && offsetof(gm_toa2vhe3l4hfr, f0) == 0 && offsetof(gm_toa2vhe3l4hfr, f1) == 8, "layout of `GenericArg`");
_Static_assert(sizeof(gm_tqqhoivahirr0) == 136 && _Alignof(gm_tqqhoivahirr0) == 8 && offsetof(gm_tqqhoivahirr0, f0) == 0 && offsetof(gm_tqqhoivahirr0, f1) == 16 && offsetof(gm_tqqhoivahirr0, f2) == 32 && offsetof(gm_tqqhoivahirr0, f3) == 40 && offsetof(gm_tqqhoivahirr0, f4) == 56 && offsetof(gm_tqqhoivahirr0, f5) == 64 && offsetof(gm_tqqhoivahirr0, f6) == 80 && offsetof(gm_tqqhoivahirr0, f7) == 96 && offsetof(gm_tqqhoivahirr0, f8) == 128, "layout of `FnSig`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint32_t f0; gm_tl874t2atnlh3 f1; gm_th2lqbo77pvsc f2; gm_b16_8 f3; gm_t9qkh0rsooc8d f4; uint8_t p5[7]; gm_tkoope2qgegd9 f5; } gm_tvpnktv5fo8nl;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_t9q8lhljes74c v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; void * v55; gm_tandvbt63uk7n v56; gm_t710lsrq1m8gk v57; } f1; } gm_to0f5qn75cgg8;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
typedef struct { uint64_t f0; gm_tvpnktv5fo8nl f1; } gm_tp8h583g7s5hd;
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_tvpnktv5fo8nl) == 160 && _Alignof(gm_tvpnktv5fo8nl) == 8 && offsetof(gm_tvpnktv5fo8nl, f0) == 0 && offsetof(gm_tvpnktv5fo8nl, f1) == 4 && offsetof(gm_tvpnktv5fo8nl, f2) == 16 && offsetof(gm_tvpnktv5fo8nl, f3) == 40 && offsetof(gm_tvpnktv5fo8nl, f4) == 56 && offsetof(gm_tvpnktv5fo8nl, f5) == 64, "layout of `Item`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
typedef struct { gm_to0f5qn75cgg8 f0; uint8_t f1; uint8_t pe[7]; } gm_t9f76dterbqj6;
_Static_assert(sizeof(gm_to0f5qn75cgg8) == 64 && _Alignof(gm_to0f5qn75cgg8) == 8 && offsetof(gm_to0f5qn75cgg8, f0) == 0 && offsetof(gm_to0f5qn75cgg8, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_tp8h583g7s5hd) == 168 && _Alignof(gm_tp8h583g7s5hd) == 8 && offsetof(gm_tp8h583g7s5hd, f0) == 0 && offsetof(gm_tp8h583g7s5hd, f1) == 8, "layout of `OtherItem`");
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
_Static_assert(sizeof(gm_t9f76dterbqj6) == 72 && _Alignof(gm_t9f76dterbqj6) == 8 && offsetof(gm_t9f76dterbqj6, f0) == 0 && offsetof(gm_t9f76dterbqj6, f1) == 64, "layout of `?ExprKind`");
struct gm_fr_check_inline_n7kskmb {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* seen */
    gm_trqfvmjtt4sij l5; /* work */
    gm_trqfvmjtt4sij l9; /* calls */
    gm_b0_1 ret;
};
struct gm_fr_check_program_rltsqqp {
    gm_frame h;
    gm_tp8h583g7s5hd l6; /* o */
    gm_teqc715vjig79 l8; /* i */
    gm_tet9guhev5r4u l9; /* x */
    gm_tet9guhev5r4u l10; /* x */
    gm_b0_1 ret;
};
struct gm_fr_collect_calls_7s68jhs {
    gm_frame h;
    void * l1; /* out */
    gm_b0_1 ret;
};
struct gm_fr_effect_name_prpp0ip {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_6trg8r7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8fnsr5h {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_c3qnk0a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_crpekm1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fa4sjvq {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fh7svmj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_he774nl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s65n31g {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_find_main_ob90gra {
    gm_frame h;
    gm_tqqhoivahirr0 l4; /* sig */
    gm_b0_1 ret;
};
struct gm_fr_find_pair_ddtpbq9 {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_fn_2d811oi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_5puhsbi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_eg2r5au {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_afagrs5 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ht23jk8 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jhm2032 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l251doo {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_le00do2 {
    gm_frame h;
    gm_thddbqb6snabm l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_mlqiori {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_r4smojn {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ra91g4n {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_from_int_ofjuoqh {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* i */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_get_espvf96 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_is_unit_n83ttnb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_7l73vjb {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_af74s0q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nof7vfj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_oalq03d {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_p9s2t99 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vmvl81b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lookup_59juqoo {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_mono_cond_pats_armprh7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_mono_expr_p8qb7fe {
    gm_frame h;
    void * l0; /* s */
    void * l1; /* mc */
    gm_tbci5ospk5nnn l16; /* i */
    gm_tbci5ospk5nnn l19; /* v */
    gm_tbci5ospk5nnn l27; /* v */
    gm_t710lsrq1m8gk l38; /* w */
    gm_t710lsrq1m8gk l64; /* x */
    gm_b0_1 ret;
};
struct gm_fr_mono_fn_aj1vgri {
    gm_frame h;
    gm_trqfvmjtt4sij l9; /* args */
    gm_tqqhoivahirr0 l13; /* sig */
    gm_trqfvmjtt4sij l14; /* params */
    gm_t5v0c8ia6a5mo l17; /* p */
    gm_t5v0c8ia6a5mo l18; /* q */
    gm_tdgfduqjheqdm l19; /* def */
    gm_trqfvmjtt4sij l21; /* locals */
    gm_tsoie1ppgdvcg l24; /* l */
    gm_tsoie1ppgdvcg l25; /* x */
    uint32_t ret;
};
struct gm_fr_mono_handler_38b78qh {
    gm_frame h;
    gm_t1cs2fjnrqap1 l4; /* lit */
    gm_trqfvmjtt4sij l6; /* clauses */
    uint32_t ret;
};
struct gm_fr_mono_pat_lipeb0h {
    gm_frame h;
    void * l0; /* s */
    void * l1; /* mc */
    gm_b0_1 ret;
};
struct gm_fr_mono_ty_atcu0jn {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_monomorphize_21k6pku {
    gm_frame h;
    gm_t0ut88vjdgrt1 l1; /* req */
    gm_b0_1 ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_pop_570g79n {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_pop_7vc0ftk {
    gm_frame h;
    gm_t0ut88vjdgrt1 l2; /* x */
    gm_t4fb05ktf8oll ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_produce_instance_kcqgj3e {
    gm_frame h;
    gm_t0ut88vjdgrt1 l1; /* req */
    gm_tdgfduqjheqdm l2; /* def */
    gm_trqfvmjtt4sij l5; /* map */
    gm_trqfvmjtt4sij l11; /* locals */
    gm_tblkd491pk4cr l12; /* mc */
    gm_tsoie1ppgdvcg l15; /* l */
    gm_tsoie1ppgdvcg l16; /* x */
    gm_tsoie1ppgdvcg l20; /* l */
    gm_b0_1 ret;
};
struct gm_fr_push_1ncolkq {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_am7bo0v {
    gm_frame h;
    gm_tsoie1ppgdvcg l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_bu0mlkm {
    gm_frame h;
    gm_t1cs2fjnrqap1 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_h69n5fi {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_im9d4ro {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_k3l9nic {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_o8u01sh {
    gm_frame h;
    gm_t5v0c8ia6a5mo l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_s5vr2a2 {
    gm_frame h;
    gm_toa2vhe3l4hfr l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_1sqb3ir {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_2onqfca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_4mbcm9u {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_7sj41hq {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8l7j2k6 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8r12kcb {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j4iu2lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_o08qncv {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_set_inst_def_tcpabg0 {
    gm_frame h;
    gm_tdgfduqjheqdm l2; /* d */
    gm_b0_1 ret;
};
struct gm_fr_show_8uijkbs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_subst_eref_kf9o1q5 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* args */
    gm_toa2vhe3l4hfr l6; /* a */
    gm_toa2vhe3l4hfr l8; /* v */
    gm_tiilmfd16v3fc ret;
};
struct gm_fr_to_float_o1iop8t {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    double ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_ohk62mc {
    gm_frame h;
    void * ret;
};
struct gm_fr_untyped_float_m6lbr0g {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    gm_to0f5qn75cgg8 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_bodies_qrmq5ll(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_consts_fsj0q4a(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_continuations_ukkdj9c(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_dangling_addresses_9hre1eh(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_decl_breaks_pk9cnmn(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_declared_defaults_m12p6ao(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_escapes_brb2os4(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_handler_clauses_n3lq2s1(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_inline_n7kskmb(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_len_exprs_m8s6m0h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_toceffb697p0u a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_program_rltsqqp(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_check_spec_4vp7jhg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_static_assert_s4f7i6i(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_tl874t2atnlh3 a3, gm_toceffb697p0u a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_tests_ogqv9b0(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void * gm_f_clone_expr_af0u1qc(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_collect_calls_7s68jhs(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_compile_error_qlau792(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_handlers_c3ac89r(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_name_prpp0ip(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6trg8r7(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8fnsr5h(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c3qnk0a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_crpekm1(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fa4sjvq(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s65n31g(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_find_main_ob90gra(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_ovslcai(void * a0, void * a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_find_pair_ddtpbq9(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2d811oi(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5puhsbi(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_eg2r5au(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_for_each_subpat_arlrtrm(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_format_3tlgo2e(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_afagrs5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_ht23jk8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_jhm2032(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_l251doo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_le00do2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_mlqiori(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_r4smojn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ra91g4n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_forward_call_qat9d2e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_handlers_of_type_5gareb4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_41b084m(gm_b16_8 a0, uint32_t a1);
__attribute__((aligned(4))) uint32_t gm_f_instantiate_35r5pb2(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) uint8_t gm_f_int_fits_imbqv0c(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void gm_f_int_limit_nncqn56(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint8_t a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_ambient_m41pl1e(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_key_int_pqscuvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_list_7l73vjb(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_af74s0q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_oalq03d(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_p9s2t99(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vmvl81b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_mono_cond_pats_armprh7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_mono_expr_p8qb7fe(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) uint32_t gm_f_mono_fn_aj1vgri(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2);
__attribute__((aligned(4))) uint32_t gm_f_mono_handler_38b78qh(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_mono_pat_lipeb0h(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_mono_ty_atcu0jn(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_monomorphize_21k6pku(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint32_t gm_f_new_nested_fn_r5u4ioo(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3, uint64_t a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6, gm_tnpifv20irnph a7);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_u1gvj7k(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_7vc0ftk(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_produce_instance_kcqgj3e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1ncolkq(void * a0, gm_cs9ufftihtbpo2 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_am7bo0v(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bu0mlkm(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_h69n5fi(void * a0, gm_cs7e9ppsd384bj a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_o8u01sh(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_s5vr2a2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_33kejrs(void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_3pvr70r(void * a0, uint32_t a1, const void * a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_1sqb3ir(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4mbcm9u(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_7sj41hq(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8l7j2k6(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8r12kcb(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o08qncv(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_report_soa_errors_m1bqopj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_resolve_write_e97fjom(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_inst_def_tcpabg0(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2);
__attribute__((aligned(4))) uint8_t gm_f_share_instance_1q66m8k(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_int_ub6npub(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void * gm_f_soa_expand_2e8rmqf(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void * gm_f_static_assert_k8dt551(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_subst_a2s1mb3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_subst_effects_jnlbu5m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_subst_eref_kf9o1q5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_template_params_6stj17b(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) double gm_f_to_f16_0pcu0pq(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) float gm_f_to_f32_8bnhsji(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) double gm_f_to_f64_4avhrht(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) double gm_f_to_float_o1iop8t(void *env, gm_node *cx, gm_node *inst, const void * a0, uint16_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_type_size_ok_tr1llg4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void * gm_f_unit_ohk62mc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_untyped_float_m6lbr0g(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern uint8_t gm_k3bm2i2dfqj[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0ug1ukauk3l9[74];
extern const uint8_t gm_s5r4lidhg0cfv[64];
extern const uint8_t gm_s6d960pgtv3l9[26];
extern const uint8_t gm_s92lujmr0a2c3[35];
extern const uint8_t gm_sbfr1qrcvp774[59];
extern const uint8_t gm_sdcigk5ajbcfi[68];
extern const uint8_t gm_sfrcaft0lo33k[60];
extern const uint8_t gm_sg3a8rmfq3j12[50];
extern const uint8_t gm_shhlfsve4gvoq[72];
extern const uint8_t gm_sisvmc7d4huip[14];
extern const uint8_t gm_sjg57h040l08d[56];
extern const uint8_t gm_sl85qrve5cdud[31];
extern const uint8_t gm_sl9i9hcgkt8fd[97];
extern const uint8_t gm_smhs805pum0dn[26];
extern const uint8_t gm_snikjq3vkoue5[14];
extern const uint8_t gm_sog4rksrmsqd2[5];
extern const uint8_t gm_soo4rtckloi4d[13];
extern const uint8_t gm_sq7c24l9r5rti[118];
extern const uint8_t gm_srar03v1ldkqi[49];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src5eu5l4a7gv6s[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const gm_tmpl gm_tpl606t7k5n88ss;
extern const gm_tmpl gm_tpl6rgl0akrj87c;
extern const gm_tmpl gm_tpl7ilj5n9objtq;
extern const gm_tmpl gm_tpl9ibjmfjq0iar;
extern const gm_tmpl gm_tpla5rqmr49dnnk;
extern const gm_tmpl gm_tplgavllsav46n0;
extern const gm_tmpl gm_tpli8blk453jgvg;
extern const gm_tmpl gm_tplnko388ar3ptd;
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
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_name_prpp0ip(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_effect_name_prpp0ip F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 216));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 216) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 283, 62);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)t3) + (uintptr_t)t6 * 16));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6trg8r7(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_6trg8r7 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8fnsr5h(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8fnsr5h F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c3qnk0a(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_c3qnk0a F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_crpekm1(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_crpekm1 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fa4sjvq(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fa4sjvq F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 144)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g3tddrb_2 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 88)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_he774nl F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s65n31g(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_s65n31g F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_error_89glhjr F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 8));
    gm_tl874t2atnlh3 t5 = gm_l1;
    gm_b16_8 t6 = gm_l2;
    gm_b0_1 t7 = gm_f_report_eardf07((void *)NULL, cx, NULL, t4, t5, t6);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2d811oi(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_2d811oi F;
    void * gm_l0; /* x */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_collect_calls_7s68jhs *)((&F.h)->up))->l1));
    gm_b0_1 t4 = gm_f_collect_calls_7s68jhs((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5puhsbi(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_5puhsbi F;
    void * gm_l0; /* q */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_mono_pat_lipeb0h *)((&F.h)->up))->l0));
    void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_mono_pat_lipeb0h *)((&F.h)->up))->l1));
    void * t4 = gm_l0;
    gm_b0_1 t5 = gm_f_mono_pat_lipeb0h((void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_eg2r5au(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_eg2r5au F;
    void * gm_l0; /* x */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_mono_expr_p8qb7fe *)((&F.h)->up))->l0));
    void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_mono_expr_p8qb7fe *)((&F.h)->up))->l1));
    void * t4 = gm_l0;
    gm_b0_1 t5 = gm_f_mono_expr_p8qb7fe((void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) void gm_f_format_afagrs5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_afagrs5 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sdcigk5ajbcfi));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(67));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl606t7k5n88ss), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ht23jk8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ht23jk8 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0ug1ukauk3l9));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(73));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplnko388ar3ptd), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_jhm2032(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_jhm2032 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbfr1qrcvp774));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(58));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl6rgl0akrj87c), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_l251doo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_l251doo F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sg3a8rmfq3j12));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(49));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpli8blk453jgvg), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_le00do2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_le00do2 F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sl9i9hcgkt8fd));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(96));
    gm_thddbqb6snabm t3 = GM_LD(gm_thddbqb6snabm, ((uint8_t *)a0));
    gm_f_format_3tlgo2e((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpla5rqmr49dnnk), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_mlqiori(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_mlqiori F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s92lujmr0a2c3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(34));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl7ilj5n9objtq), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_r4smojn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_r4smojn F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5r4lidhg0cfv));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(63));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplgavllsav46n0), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ra91g4n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ra91g4n F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shhlfsve4gvoq));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(71));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl9ibjmfjq0iar), t3);
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
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_from_int_ofjuoqh F;
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tbci5ospk5nnn t2 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l0));
    gm_tbci5ospk5nnn t3;
    gm_f_one_08qg1op((void *)&t3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&(*(gm_tngtuiuskn938 *)gm_out) + 0), t2);
    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&(*(gm_tngtuiuskn938 *)gm_out) + 48), t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_param_u9t0efs F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 53u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 40));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 40) + 8));
    uint32_t t5 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 249, 57);
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)t3) + (uintptr_t)t6 * 1));
    uint8_t t8 = (uint8_t)(t7 & ((uint8_t)1ULL));
    uint8_t t9 = (!(t8 == ((uint8_t)0ULL)));
    F.ret = t9;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) void gm_f_list_7l73vjb(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7l73vjb F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_crpekm1();
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
inline __attribute__((aligned(4))) void gm_f_list_af74s0q(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_af74s0q F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_he774nl();
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
inline __attribute__((aligned(4))) void gm_f_list_oalq03d(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_oalq03d F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_8fnsr5h();
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
inline __attribute__((aligned(4))) void gm_f_list_p9s2t99(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_p9s2t99 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_6trg8r7();
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
inline __attribute__((aligned(4))) void gm_f_list_vmvl81b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vmvl81b F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_fh7svmj();
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lookup_59juqoo F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 200));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 200) + 8));
    uint64_t t5 = gm_l1;
    if ((uintptr_t)t5 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t5, t4, gm_srcc2qutubarto1, 547, 78);
    void * t6 = (void *)((((uint8_t *)t3) + (uintptr_t)t5 * 184) + 56);
    gm_b16_8 t7 = gm_l2;
    gm_tf5srh4mp4b3i t8 = gm_f_get_espvf96(t6, t7);
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_mono_pat_lipeb0h(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_mono_pat_lipeb0h F;
    void * gm_l2; /* p */
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l2;
    void * t3 = F.l0;
    void * t4 = gm_l2;
    gm_t88rcucg1b7uk t5 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t4));
    void * t6 = F.l1;
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)t6));
    gm_t88rcucg1b7uk t8 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t3, t5, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)t2), t8);
    void * t9 = gm_l2;
    gm_closure t10 = ((gm_closure){(void *)gm_f_fn_5puhsbi, (void *)&F.h, cx});
    gm_f_for_each_subpat_arlrtrm(t9, t10);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_monomorphize_21k6pku(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_monomorphize_21k6pku F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 81u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4fb05ktf8oll gm_l2;
    {
        for (;;) {
            {
                void * t5 = gm_l0;
                void * t6 = (void *)(((uint8_t *)t5) + 1320);
                gm_f_pop_7vc0ftk((void *)&gm_l2, t6);
            }
            uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l2))[40] != 0);
            if (!t7) {
                goto gm_lb1;
            }
            {
                memmove(((uint8_t *)&F.l1), ((uint8_t *)&gm_l2), 40);
            }
            {
                void * t8 = gm_l0;
                gm_b0_1 t9 = gm_f_produce_instance_kcqgj3e((void *)NULL, cx, NULL, t8, &(*(gm_t0ut88vjdgrt1 *)((uint8_t *)&F.l1)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
gm_lb3:;
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
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_one_08qg1op F;
    GM_STACK_CHECK(&F, 0u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_f_from_u64_ud13q5v((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, ((uint64_t)1ULL));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_7vc0ftk(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_7vc0ftk F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
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
        memset(&(*(gm_t4fb05ktf8oll *)gm_out), 0, sizeof (*(gm_t4fb05ktf8oll *)gm_out));
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
        memmove(((uint8_t *)&F.l2), (((uint8_t *)t7) + (uintptr_t)t12 * 40), 40);
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
    GM_ST(void *, (uint8_t *)&t24, (void *)(((uint8_t *)t15 + (uintptr_t)((uint64_t)0ULL) * 40)));
    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(t23));
    GM_ST(gm_b16_8, ((uint8_t *)t13), t24);
    {
        gm_t0ut88vjdgrt1 t25 = GM_LD(gm_t0ut88vjdgrt1, ((uint8_t *)&F.l2));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t4fb05ktf8oll *)gm_out) + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t4fb05ktf8oll *)gm_out) + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t4fb05ktf8oll *)gm_out) + 44, 0);
        GM_ST(gm_t0ut88vjdgrt1, ((uint8_t *)&(*(gm_t4fb05ktf8oll *)gm_out)), t25);
        ((uint8_t *)&(*(gm_t4fb05ktf8oll *)gm_out))[40] = 1;
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1ncolkq(void * a0, gm_cs9ufftihtbpo2 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_1ncolkq F;
    void * gm_l0; /* l */
    gm_tcc1rq4n1avb2 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tcc1rq4n1avb2, a1);
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
        gm_b16_8 t9 = gm_f_empty_slice_crpekm1();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_o08qncv(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_am7bo0v(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_am7bo0v F;
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
        gm_b16_8 t9 = gm_f_empty_slice_6trg8r7();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_8r12kcb(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bu0mlkm(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_bu0mlkm F;
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
        gm_b16_8 t9 = gm_f_empty_slice_c3qnk0a();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_8l7j2k6(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_h69n5fi(void * a0, gm_cs7e9ppsd384bj a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_h69n5fi F;
    void * gm_l0; /* l */
    gm_tplpbqf3duvn3 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tplpbqf3duvn3, a1);
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
        gm_b16_8 t9 = gm_f_empty_slice_8fnsr5h();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_1sqb3ir(t8, ((uint64_t)1ULL), t10);
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
    gm_tplpbqf3duvn3 t18 = gm_l1;
    GM_ST(gm_tplpbqf3duvn3, (((uint8_t *)t16) + (uintptr_t)t17 * 8), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_o8u01sh(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_o8u01sh F;
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
        gm_b16_8 t9 = gm_f_empty_slice_he774nl();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_7sj41hq(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 56), ((uint8_t *)&F.l1), 56);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_s5vr2a2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_s5vr2a2 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_fh7svmj();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_4mbcm9u(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_1sqb3ir(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_1sqb3ir F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4mbcm9u(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_4mbcm9u F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_7sj41hq(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_7sj41hq F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8l7j2k6(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8l7j2k6 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8r12kcb(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8r12kcb F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o08qncv(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_o08qncv F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_report_eardf07 F;
    void * gm_l0; /* ds */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 256u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    gm_tl874t2atnlh3 t3 = gm_l1;
    gm_b16_8 t4 = gm_l2;
    gm_t4gut2c9fj1kt t5;
    gm_f_error_s6hn871((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_push_o4ckdr1(t2, &t5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_inst_def_tcpabg0(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2) {
    struct gm_fr_set_inst_def_tcpabg0 F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK(&F, 37u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 2184);
    uint32_t t4 = gm_l1;
    gm_tftb1odc739t5 t5 = gm_f_put_3pvr70r(t3, t4, &(*(gm_tdgfduqjheqdm *)((uint8_t *)a2)));
    gm_l3 = t5;
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srclv0dcbbfhves, 615, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_8uijkbs F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_b16_8 t5 = gm_f_show_kmjeh8s((void *)NULL, cx, NULL, t3, t4);
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
inline __attribute__((aligned(4))) double gm_f_to_float_o1iop8t(void *env, gm_node *cx, gm_node *inst, const void * a0, uint16_t a1) {
    struct gm_fr_to_float_o1iop8t F;
    uint16_t gm_l1; /* bits */
    GM_STACK_CHECK(&F, 52u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_l1 = a1;
    uint16_t t2 = gm_l1;
    uint8_t t3 = ((t2 == ((uint16_t)16ULL)));
    if (t3) {
        double t4 = gm_f_to_f16_0pcu0pq((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t4;
        goto gm_fret;
    }
    uint16_t t5 = gm_l1;
    uint8_t t6 = ((t5 == ((uint16_t)32ULL)));
    if (t6) {
        float t7 = gm_f_to_f32_8bnhsji((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        double t8 = (double)t7;
        F.ret = t8;
        goto gm_fret;
    }
    double t9 = gm_f_to_f64_4avhrht((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l0)));
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_u8_ty_afkvj25 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 28u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t2, ((uint8_t)0), ((uint16_t)8ULL));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t3;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_2age34u F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 180u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3 = gm_f_empty_slice_rcd2jln();
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    gm_b16_8 t5 = gm_f_empty_slice_5nn7ph0();
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
    gm_t6j6smj7qoctu t7;
    GM_ST(gm_b16_8, ((uint8_t *)&t7 + 0), t4);
    GM_ST(gm_b16_8, ((uint8_t *)&t7 + 16), t6);
    gm_t53at80pfdus3 t8;
    GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t8 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t8 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)12ULL));
    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t8 + 8), t7);
    gm_t88rcucg1b7uk t9 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t2, &t8);
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
inline __attribute__((aligned(4))) void * gm_f_unit_ohk62mc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_unit_ohk62mc F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 156u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = gm_f_empty_slice_g3tddrb_2();
    gm_tl874t2atnlh3 t6 = gm_l1;
    gm_to0f5qn75cgg8 t7;
    memset(&t7, 0, sizeof t7);
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)35ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t7 + 8), t5);
    void * t8 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t4, &t7, t6);
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
inline __attribute__((aligned(4))) void gm_f_untyped_float_m6lbr0g(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_untyped_float_m6lbr0g F;
    GM_STACK_CHECK(&F, 41u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tq7s98ihonkju gm_l1;
    {
        gm_tq7s98ihonkju t2 = gm_f_new_u1gvj7k((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l0)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k3bm2i2dfqj)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l1 = t2;
    }
    uint8_t t3 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
    if (!t3) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l1) + 8, gm_srcod98slk3vnra, 592, 80);
    {
        void * t4 = GM_LD(void *, (((uint8_t *)&gm_l1) + 8));
        memset(&(*(gm_to0f5qn75cgg8 *)gm_out), 0, sizeof (*(gm_to0f5qn75cgg8 *)gm_out));
        GM_ST(uint8_t, ((uint8_t *)&(*(gm_to0f5qn75cgg8 *)gm_out) + 0), ((uint8_t)2ULL));
        GM_ST(void *, ((uint8_t *)&(*(gm_to0f5qn75cgg8 *)gm_out) + 8), t4);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_check_inline_n7kskmb(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_check_inline_n7kskmb F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    gm_t9d4sf0vqn7dt gm_l3; /* inl */
    uint8_t gm_l6; /* recursive */
    uint32_t gm_l7; /* g */
    void * gm_l8; /* body */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint32_t gm_l12; /* c */
    GM_STACK_CHECK(&F, 750u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l13;
    gm_t9805qtonbsag gm_l14;
    uint8_t gm_l15;
    gm_tf5srh4mp4b3i gm_l16;
    gm_tftb1odc739t5 gm_l17;
    gm_tftb1odc739t5 gm_l18;
    gm_tabb710d9mq2e gm_l19;
    void * t18 = gm_l0;
    gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)t18) + 800));
    gm_l1 = t19;
    gm_l2 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t20 = gm_l2;
            uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t22 = (t20 < t21);
            if (!t22) {
                goto gm_lb1;
            }
            {
                void * t23 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t25 = gm_l2;
                {
                    {
                        gm_t9d4sf0vqn7dt t26 = GM_LD(gm_t9d4sf0vqn7dt, (((uint8_t *)t23) + (uintptr_t)t25 * 16));
                        gm_l3 = t26;
                        {
                            gm_trqfvmjtt4sij t27;
                            gm_f_list_go2qg0b((void *)&t27, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.l4 = t27;
                        }
                        {
                            gm_trqfvmjtt4sij t28;
                            gm_f_list_go2qg0b((void *)&t28, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.l5 = t28;
                        }
                        void * t29 = (void *)((uint8_t *)&F.l5);
                        uint32_t t30 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
                        gm_tftb1odc739t5 t31 = gm_f_push_im9d4ro(t29, t30);
                        gm_l13 = t31;
                        uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                        if (!t32) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_src5eu5l4a7gv6s, 377, 9);
                        gm_l6 = ((uint8_t)0);
                        {
                            for (;;) {
                                void * t33 = (void *)((uint8_t *)&F.l5);
                                gm_t9805qtonbsag t34 = gm_f_pop_570g79n(t33);
                                gm_l14 = t34;
                                uint8_t t35 = (uint8_t)((((uint8_t *)&gm_l14))[4] != 0);
                                if (!t35) {
                                    goto gm_lb6;
                                }
                                uint32_t t36 = GM_LD(uint32_t, ((uint8_t *)&gm_l14));
                                gm_l7 = t36;
                                {
                                    void * t37 = gm_l0;
                                    void * t38 = GM_LD(void *, (((uint8_t *)t37) + 384));
                                    uint64_t t39 = GM_LD(uint64_t, ((((uint8_t *)t37) + 384) + 8));
                                    uint32_t t40 = gm_l7;
                                    uint64_t t41 = (uint64_t)t40;
                                    if ((uintptr_t)t41 >= (uintptr_t)(t39)) gm_trap_index((uintptr_t)t41, t39, gm_src5eu5l4a7gv6s, 380, 21);
                                    uint8_t t42 = (uint8_t)(GM_LD(void *, ((((uint8_t *)t38) + (uintptr_t)t41 * 64) + 40)) != 0);
                                    if (t42) {
                                        void * t43 = GM_LD(void *, ((((uint8_t *)t38) + (uintptr_t)t41 * 64) + 40));
                                        gm_l8 = t43;
                                    } else {
                                        goto gm_lb8;
                                    }
                                    gm_trqfvmjtt4sij t44;
                                    gm_f_list_go2qg0b((void *)&t44, (void *)NULL, cx, NULL);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.l9 = t44;
                                    void * t45 = gm_l8;
                                    void * t46 = (void *)((uint8_t *)&F.l9);
                                    gm_b0_1 t47 = gm_f_collect_calls_7s68jhs((void *)NULL, cx, NULL, t45, t46);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
                                    gm_l10 = t48;
                                    gm_l11 = ((uint64_t)0ULL);
                                    {
                                        for (;;) {
                                            uint64_t t49 = gm_l11;
                                            uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                            uint8_t t51 = (t49 < t50);
                                            if (!t51) {
                                                goto gm_lb9;
                                            }
                                            {
                                                void * t52 = GM_LD(void *, ((uint8_t *)&gm_l10));
                                                uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                                uint64_t t54 = gm_l11;
                                                {
                                                    {
                                                        uint32_t t55 = GM_LD(uint32_t, (((uint8_t *)t52) + (uintptr_t)t54 * 4));
                                                        gm_l12 = t55;
                                                        uint32_t t56 = gm_l12;
                                                        uint32_t t57 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
                                                        uint8_t t58 = ((t56 == t57));
                                                        if (t58) {
                                                            gm_l6 = ((uint8_t)1);
                                                        } else {
                                                            gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
                                                            gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&t59));
                                                            uint32_t t61 = gm_l12;
                                                            gm_tf5srh4mp4b3i t62 = gm_f_index_of_41b084m(t60, t61);
                                                            gm_l16 = t62;
                                                            {
                                                                {
                                                                    uint8_t t63 = (uint8_t)((((uint8_t *)&gm_l16))[8] != 0);
                                                                    if (t63) {
                                                                        goto gm_lb15;
                                                                    }
                                                                    gm_l15 = ((uint8_t)1);
                                                                    goto gm_lb14;
                                                                }
gm_lb15:;
                                                                {
                                                                    gm_l15 = ((uint8_t)0);
                                                                    goto gm_lb14;
                                                                }
gm_lb16:;
                                                                gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 386, 27);
                                                            }
gm_lb14:;
                                                            uint8_t t64 = gm_l15;
                                                            if (t64) {
                                                                void * t65 = (void *)((uint8_t *)&F.l4);
                                                                uint32_t t66 = gm_l12;
                                                                gm_tftb1odc739t5 t67 = gm_f_push_im9d4ro(t65, t66);
                                                                gm_l17 = t67;
                                                                uint8_t t68 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                                                                if (!t68) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_src5eu5l4a7gv6s, 387, 21);
                                                                void * t69 = (void *)((uint8_t *)&F.l5);
                                                                uint32_t t70 = gm_l12;
                                                                gm_tftb1odc739t5 t71 = gm_f_push_im9d4ro(t69, t70);
                                                                gm_l18 = t71;
                                                                uint8_t t72 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                                                                if (!t72) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_src5eu5l4a7gv6s, 388, 21);
                                                            }
                                                        }
                                                        goto gm_lb12;
                                                    }
gm_lb13:;
                                                    gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 383, 13);
                                                }
gm_lb12:;
                                            }
gm_lb11:;
                                            uint64_t t73 = gm_l11;
                                            uint64_t r74; bool o75 = __builtin_add_overflow(t73, ((uint64_t)1ULL), &r74);
                                            uint64_t t76 = r74;
                                            gm_l11 = t76;
                                        }
                                    }
gm_lb9:;
                                }
gm_lb8:;
                            }
                        }
gm_lb6:;
                        uint8_t t77 = gm_l6;
                        if (t77) {
                            void * t78 = gm_l0;
                            gm_tl874t2atnlh3 t79 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l3) + 4));
                            void * t80 = gm_l0;
                            void * t81 = GM_LD(void *, (((uint8_t *)t80) + 344));
                            uint64_t t82 = GM_LD(uint64_t, ((((uint8_t *)t80) + 344) + 8));
                            uint32_t t83 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
                            uint64_t t84 = (uint64_t)t83;
                            if ((uintptr_t)t84 >= (uintptr_t)(t82)) gm_trap_index((uintptr_t)t84, t82, gm_src5eu5l4a7gv6s, 393, 116);
                            gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)t81) + (uintptr_t)t84 * 136));
                            gm_tnpifv20irnph t86;
                            GM_ST(gm_b16_8, ((uint8_t *)&t86 + 0), t85);
                            gm_f_format_ra91g4n((void *)&gm_l19, (void *)NULL, cx, NULL, t86);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t87 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                            if (!t87) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 8, gm_src5eu5l4a7gv6s, 393, 31);
                            gm_b16_8 t88 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 8));
                            gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&t88));
                            gm_b0_1 t90 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t78, t79, t89);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 374, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t91 = gm_l2;
            uint64_t r92; bool o93 = __builtin_add_overflow(t91, ((uint64_t)1ULL), &r92);
            uint64_t t94 = r92;
            gm_l2 = t94;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_check_program_rltsqqp(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint8_t a2) {
    struct gm_fr_check_program_rltsqqp F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* root */
    uint8_t gm_l2; /* tests */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* k */
    void * gm_l7; /* e */
    gm_b16_8 gm_l11; /* args */
    uint64_t gm_l12; /* d */
    uint64_t gm_l13; /* d */
    GM_STACK_CHECK(&F, 1083u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tf5srh4mp4b3i gm_l14;
    gm_tf5srh4mp4b3i gm_l15;
    gm_l4 = ((uint64_t)0ULL);
    void * t24 = gm_l0;
    void * t25 = GM_LD(void *, ((uint8_t *)t24));
    uint64_t t26 = GM_LD(uint64_t, ((((uint8_t *)t25) + 280) + 8));
    gm_l3 = t26;
    {
        for (;;) {
            uint64_t t27 = gm_l4;
            uint64_t t28 = gm_l3;
            uint8_t t29 = (t27 < t28);
            if (!t29) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t30 = gm_l4;
                        gm_l5 = t30;
                        void * t31 = gm_l0;
                        void * t32 = GM_LD(void *, ((uint8_t *)t31));
                        void * t33 = GM_LD(void *, (((uint8_t *)t32) + 280));
                        uint64_t t34 = GM_LD(uint64_t, ((((uint8_t *)t32) + 280) + 8));
                        uint64_t t35 = gm_l5;
                        if ((uintptr_t)t35 >= (uintptr_t)(t34)) gm_trap_index((uintptr_t)t35, t34, gm_src5eu5l4a7gv6s, 8, 14);
                        memmove(((uint8_t *)&F.l6), (((uint8_t *)t33) + (uintptr_t)t35 * 168), 168);
                        void * t36 = gm_l0;
                        void * t37 = GM_LD(void *, ((uint8_t *)t36));
                        void * t38 = GM_LD(void *, (((uint8_t *)t37) + 200));
                        uint64_t t39 = GM_LD(uint64_t, ((((uint8_t *)t37) + 200) + 8));
                        uint64_t t40 = GM_LD(uint64_t, ((uint8_t *)&F.l6));
                        if ((uintptr_t)t40 >= (uintptr_t)(t39)) gm_trap_index((uintptr_t)t40, t39, gm_src5eu5l4a7gv6s, 9, 12);
                        uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t38) + (uintptr_t)t40 * 184) + 112));
                        if (t41) {
                            goto gm_lb3;
                        }
                        {
                            {
                                {
                                    {
                                        uint8_t t42 = GM_LD(uint8_t, (((((uint8_t *)&F.l6) + 8) + 64) + 0));
                                        uint8_t t43 = (uint8_t)(t42 == ((uint8_t)3ULL));
                                        if (!t43) {
                                            goto gm_lb9;
                                        }
                                        goto gm_lb8;
                                    }
gm_lb9:;
                                    {
                                        uint8_t t44 = GM_LD(uint8_t, (((((uint8_t *)&F.l6) + 8) + 64) + 0));
                                        uint8_t t45 = (uint8_t)(t44 == ((uint8_t)2ULL));
                                        if (!t45) {
                                            goto gm_lb10;
                                        }
                                        goto gm_lb8;
                                    }
gm_lb10:;
                                    goto gm_lb7;
                                }
gm_lb8:;
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t46 = GM_LD(uint8_t, (((((uint8_t *)&F.l6) + 8) + 64) + 0));
                                uint8_t t47 = (uint8_t)(t46 == ((uint8_t)4ULL));
                                if (!t47) {
                                    goto gm_lb11;
                                }
                                void * t48 = GM_LD(void *, (((((uint8_t *)&F.l6) + 8) + 64) + 8));
                                gm_l7 = t48;
                                memset(&F.l8, 0, sizeof F.l8);
                                {
                                    {
                                        {
                                            void * t49 = gm_l7;
                                            uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)t49) + 8) + 0));
                                            uint8_t t51 = (uint8_t)(t50 == ((uint8_t)49ULL));
                                            if (!t51) {
                                                goto gm_lb13;
                                            }
                                            memmove(((uint8_t *)&F.l9), ((((uint8_t *)t49) + 8) + 8), 56);
                                        }
                                        {
                                            gm_tet9guhev5r4u t52 = GM_LD(gm_tet9guhev5r4u, ((uint8_t *)&F.l9));
                                            GM_ST(uint8_t, (uint8_t *)&F.l8 + 57, 0);
                                            GM_ST(uint16_t, (uint8_t *)&F.l8 + 58, 0);
                                            GM_ST(uint32_t, (uint8_t *)&F.l8 + 60, 0);
                                            GM_ST(gm_tet9guhev5r4u, ((uint8_t *)&F.l8), t52);
                                            ((uint8_t *)&F.l8)[56] = 1;
                                        }
                                        goto gm_lb12;
                                    }
gm_lb13:;
                                }
gm_lb12:;
                                uint8_t t53 = (uint8_t)((((uint8_t *)&F.l8))[56] != 0);
                                if (t53) {
                                    memmove(((uint8_t *)&F.l10), ((uint8_t *)&F.l8), 56);
                                } else {
                                    goto gm_lb3;
                                }
                                uint8_t t54 = (uint8_t)(((((uint8_t *)&F.l10) + 32))[16] != 0);
                                if (t54) {
                                    gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 32));
                                    gm_l11 = t55;
                                } else {
                                    gm_b16_8 t56 = gm_f_empty_slice_fa4sjvq();
                                    gm_l11 = t56;
                                }
                                {
                                    {
                                        gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                                        gm_b16_8 t58;
                                        GM_ST(void *, (uint8_t *)&t58, (void *)(gm_snikjq3vkoue5));
                                        GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(13));
                                        uint8_t t59 = (gm_eq0iqiubfm7e(((uint8_t *)&t57), ((uint8_t *)&t58)));
                                        if (!t59) {
                                            goto gm_lb15;
                                        }
                                        void * t60 = gm_l0;
                                        uint64_t t61 = GM_LD(uint64_t, ((uint8_t *)&F.l6));
                                        gm_b16_8 t62 = gm_l11;
                                        gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&t62));
                                        void * t64 = gm_l7;
                                        gm_tl874t2atnlh3 t65 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t64) + 72));
                                        void * t66 = gm_f_static_assert_k8dt551((void *)NULL, cx, NULL, t60, t61, t63, t65);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb14;
                                    }
gm_lb15:;
                                    {
                                        gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                                        gm_b16_8 t68;
                                        GM_ST(void *, (uint8_t *)&t68, (void *)(gm_sisvmc7d4huip));
                                        GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(13));
                                        uint8_t t69 = (gm_eq0iqiubfm7e(((uint8_t *)&t67), ((uint8_t *)&t68)));
                                        if (!t69) {
                                            goto gm_lb16;
                                        }
                                        void * t70 = gm_l0;
                                        uint64_t t71 = GM_LD(uint64_t, ((uint8_t *)&F.l6));
                                        gm_b16_8 t72 = gm_l11;
                                        gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
                                        void * t74 = gm_l7;
                                        gm_tl874t2atnlh3 t75 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t74) + 72));
                                        gm_b0_1 t76 = gm_f_compile_error_qlau792((void *)NULL, cx, NULL, t70, t71, t73, t75);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb14;
                                    }
gm_lb16:;
                                    {
                                        void * t77 = gm_l0;
                                        gm_tl874t2atnlh3 t78 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&F.l6) + 8) + 4));
                                        gm_b16_8 t79;
                                        GM_ST(void *, (uint8_t *)&t79, (void *)(gm_sfrcaft0lo33k));
                                        GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(59));
                                        gm_b0_1 t80 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t77, t78, t79);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb14;
                                    }
gm_lb17:;
                                    gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 17, 17);
                                }
gm_lb14:;
                                goto gm_lb6;
                            }
gm_lb11:;
                            {
                                void * t81 = gm_l0;
                                gm_tl874t2atnlh3 t82 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&F.l6) + 8) + 4));
                                gm_b16_8 t83;
                                GM_ST(void *, (uint8_t *)&t83, (void *)(gm_smhs805pum0dn));
                                GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(25));
                                gm_b0_1 t84 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t81, t82, t83);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb18:;
                            gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 10, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 7, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t85 = gm_l4;
            uint64_t r86; bool o87 = __builtin_add_overflow(t85, ((uint64_t)1ULL), &r86);
            uint64_t t88 = r86;
            gm_l4 = t88;
        }
    }
gm_lb1:;
    void * t89 = gm_l0;
    gm_b0_1 t90 = gm_f_declare_handlers_c3ac89r((void *)NULL, cx, NULL, t89);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t91 = gm_l2;
    if (t91) {
        void * t92 = gm_l0;
        uint64_t t93 = gm_l1;
        gm_b0_1 t94 = gm_f_check_tests_ogqv9b0((void *)NULL, cx, NULL, t92, t93);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    } else {
        void * t95 = gm_l0;
        void * t96 = GM_LD(void *, ((uint8_t *)t95));
        gm_b16_8 t97 = GM_LD(gm_b16_8, ((((uint8_t *)t96) + 88) + 24));
        gm_b16_8 t98;
        GM_ST(void *, (uint8_t *)&t98, (void *)(gm_soo4rtckloi4d));
        GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(12));
        uint8_t t99 = (gm_eq0iqiubfm7e(((uint8_t *)&t97), ((uint8_t *)&t98)));
        if (t99) {
            {
                {
                    void * t100 = gm_l0;
                    void * t101 = GM_LD(void *, ((uint8_t *)t100));
                    uint64_t t102 = gm_l1;
                    gm_b16_8 t103;
                    GM_ST(void *, (uint8_t *)&t103, (void *)(gm_sog4rksrmsqd2));
                    GM_ST(uintptr_t, (uint8_t *)&t103 + 8, (uintptr_t)(4));
                    gm_tf5srh4mp4b3i t104 = gm_f_lookup_59juqoo(t101, t102, t103);
                    gm_l14 = t104;
                    uint8_t t105 = (uint8_t)((((uint8_t *)&gm_l14))[8] != 0);
                    if (!t105) {
                        goto gm_lb20;
                    }
                    uint64_t t106 = GM_LD(uint64_t, ((uint8_t *)&gm_l14));
                    gm_l12 = t106;
                    void * t107 = gm_l0;
                    void * t108 = gm_l0;
                    void * t109 = GM_LD(void *, ((uint8_t *)t108));
                    void * t110 = GM_LD(void *, (((uint8_t *)t109) + 240));
                    uint64_t t111 = GM_LD(uint64_t, ((((uint8_t *)t109) + 240) + 8));
                    uint64_t t112 = gm_l12;
                    if ((uintptr_t)t112 >= (uintptr_t)(t111)) gm_trap_index((uintptr_t)t112, t111, gm_src5eu5l4a7gv6s, 33, 21);
                    gm_tl874t2atnlh3 t113 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t110) + (uintptr_t)t112 * 216) + 16));
                    gm_b16_8 t114;
                    GM_ST(void *, (uint8_t *)&t114, (void *)(gm_sq7c24l9r5rti));
                    GM_ST(uintptr_t, (uint8_t *)&t114 + 8, (uintptr_t)(117));
                    gm_b0_1 t115 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t107, t113, t114);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb19;
                }
gm_lb20:;
            }
gm_lb19:;
        } else {
            {
                {
                    void * t116 = gm_l0;
                    void * t117 = GM_LD(void *, ((uint8_t *)t116));
                    uint64_t t118 = gm_l1;
                    gm_b16_8 t119;
                    GM_ST(void *, (uint8_t *)&t119, (void *)(gm_sog4rksrmsqd2));
                    GM_ST(uintptr_t, (uint8_t *)&t119 + 8, (uintptr_t)(4));
                    gm_tf5srh4mp4b3i t120 = gm_f_lookup_59juqoo(t117, t118, t119);
                    gm_l15 = t120;
                    uint8_t t121 = (uint8_t)((((uint8_t *)&gm_l15))[8] != 0);
                    if (!t121) {
                        goto gm_lb22;
                    }
                    uint64_t t122 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
                    gm_l13 = t122;
                    void * t123 = gm_l0;
                    uint64_t t124 = gm_l13;
                    gm_b0_1 t125 = gm_f_find_main_ob90gra((void *)NULL, cx, NULL, t123, t124);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb21;
                }
gm_lb22:;
                void * t126 = gm_l0;
                gm_tl874t2atnlh3 t127 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                gm_b16_8 t128;
                GM_ST(void *, (uint8_t *)&t128, (void *)(gm_srar03v1ldkqi));
                GM_ST(uintptr_t, (uint8_t *)&t128 + 8, (uintptr_t)(48));
                gm_b0_1 t129 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t126, t127, t128);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
gm_lb21:;
        }
    }
    void * t130 = gm_l0;
    gm_b0_1 t131 = gm_f_check_consts_fsj0q4a((void *)NULL, cx, NULL, t130);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t132 = gm_l0;
    gm_b0_1 t133 = gm_f_check_bodies_qrmq5ll((void *)NULL, cx, NULL, t132);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t134 = gm_l0;
    gm_b0_1 t135 = gm_f_check_handler_clauses_n3lq2s1((void *)NULL, cx, NULL, t134);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t136 = gm_l0;
    gm_b0_1 t137 = gm_f_check_bodies_qrmq5ll((void *)NULL, cx, NULL, t136);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t138 = gm_l0;
    gm_b0_1 t139 = gm_f_check_declared_defaults_m12p6ao((void *)NULL, cx, NULL, t138);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t140 = gm_l0;
    gm_b0_1 t141 = gm_f_check_bodies_qrmq5ll((void *)NULL, cx, NULL, t140);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t142 = gm_l0;
    gm_b0_1 t143 = gm_f_check_decl_breaks_pk9cnmn((void *)NULL, cx, NULL, t142);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t144 = gm_l0;
    gm_b0_1 t145 = gm_f_monomorphize_21k6pku((void *)NULL, cx, NULL, t144);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t146 = gm_l0;
    gm_b0_1 t147 = gm_f_check_inline_n7kskmb((void *)NULL, cx, NULL, t146);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t148 = gm_l0;
    gm_b0_1 t149 = gm_f_check_escapes_brb2os4((void *)NULL, cx, NULL, t148);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t150 = gm_l0;
    gm_b0_1 t151 = gm_f_check_dangling_addresses_9hre1eh((void *)NULL, cx, NULL, t150);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t152 = gm_l0;
    gm_b0_1 t153 = gm_f_check_continuations_ukkdj9c((void *)NULL, cx, NULL, t152);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_collect_calls_7s68jhs(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_collect_calls_7s68jhs F;
    void * gm_l0; /* e */
    uint32_t gm_l2; /* c */
    GM_STACK_CHECK(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    gm_tftb1odc739t5 gm_l3;
    {
        {
            void * t4 = gm_l0;
            uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)34ULL));
            if (!t6) {
                goto gm_lb2;
            }
            uint8_t t7 = GM_LD(uint8_t, (((((uint8_t *)t4) + 8) + 8) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)0ULL));
            if (!t8) {
                goto gm_lb2;
            }
            uint32_t t9 = GM_LD(uint32_t, (((((uint8_t *)t4) + 8) + 8) + 8));
            gm_l2 = t9;
            void * t10 = F.l1;
            uint32_t t11 = gm_l2;
            gm_tftb1odc739t5 t12 = gm_f_push_im9d4ro(t10, t11);
            gm_l3 = t12;
            uint8_t t13 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
            if (!t13) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_src5eu5l4a7gv6s, 399, 37);
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t14 = gm_l0;
    gm_closure t15 = ((gm_closure){(void *)gm_f_fn_2d811oi, (void *)&F.h, cx});
    gm_f_for_each_child_fm49s73(t14, t15);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_find_main_ob90gra(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_find_main_ob90gra F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* id */
    gm_tl874t2atnlh3 gm_l2; /* span */
    uint32_t gm_l3; /* f */
    uint8_t gm_l5; /* ret_ok */
    gm_t88rcucg1b7uk gm_l6; /* ok */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_tiilmfd16v3fc gm_l9; /* e */
    GM_STACK_CHECK(&F, 976u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t6qooom8qa2do gm_l10;
    uint8_t gm_l11;
    gm_t53at80pfdus3 gm_l12;
    gm_tabb710d9mq2e gm_l13;
    gm_tabb710d9mq2e gm_l14;
    void * t13 = gm_l0;
    void * t14 = GM_LD(void *, ((uint8_t *)t13));
    void * t15 = GM_LD(void *, (((uint8_t *)t14) + 240));
    uint64_t t16 = GM_LD(uint64_t, ((((uint8_t *)t14) + 240) + 8));
    uint64_t t17 = gm_l1;
    if ((uintptr_t)t17 >= (uintptr_t)(t16)) gm_trap_index((uintptr_t)t17, t16, gm_src5eu5l4a7gv6s, 405, 13);
    gm_tl874t2atnlh3 t18 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t15) + (uintptr_t)t17 * 216) + 16));
    gm_l2 = t18;
    void * t19 = gm_l0;
    uint64_t t20 = gm_l1;
    gm_f_resolve_decl_pus042v((void *)&gm_l10, (void *)NULL, cx, NULL, t19, t20);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)5ULL));
            if (!t22) {
                goto gm_lb2;
            }
            uint32_t t23 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 8));
            gm_l3 = t23;
            void * t24 = gm_l0;
            void * t25 = GM_LD(void *, (((uint8_t *)t24) + 344));
            uint64_t t26 = GM_LD(uint64_t, ((((uint8_t *)t24) + 344) + 8));
            uint32_t t27 = gm_l3;
            uint64_t t28 = (uint64_t)t27;
            if ((uintptr_t)t28 >= (uintptr_t)(t26)) gm_trap_index((uintptr_t)t28, t26, gm_src5eu5l4a7gv6s, 408, 20);
            memmove(((uint8_t *)&F.l4), (((uint8_t *)t25) + (uintptr_t)t28 * 136), 136);
            uint64_t t29 = GM_LD(uint64_t, ((((uint8_t *)&F.l4) + 40) + 8));
            uint8_t t30 = (t29 > ((uint64_t)0ULL));
            if (t30) {
                void * t31 = gm_l0;
                gm_tl874t2atnlh3 t32 = gm_l2;
                gm_b16_8 t33;
                GM_ST(void *, (uint8_t *)&t33, (void *)(gm_sl85qrve5cdud));
                GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(30));
                gm_b0_1 t34 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t31, t32, t33);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t35 = gm_l0;
            void * t36 = (void *)(((uint8_t *)t35) + 8);
            gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 56));
            uint8_t t38 = gm_f_is_unit_n83ttnb(t36, GM_CONV(gm_csguankko4lmhd, t37));
            if (t38) {
                gm_l11 = ((uint8_t)1);
            } else {
                gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 56));
                void * t40 = gm_l0;
                void * t41 = (void *)(((uint8_t *)t40) + 8);
                gm_t88rcucg1b7uk t42 = gm_f_u8_ty_afkvj25((void *)NULL, cx, NULL, t41);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t43 = (gm_eq9m6mvtmgde(((uint8_t *)&t39), ((uint8_t *)&t42)));
                gm_l11 = t43;
            }
            uint8_t t44 = gm_l11;
            gm_l5 = t44;
            {
                {
                    void * t45 = gm_l0;
                    void * t46 = (void *)(((uint8_t *)t45) + 8);
                    gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 56));
                    gm_f_kind_jcnt4hm((void *)&gm_l12, t46, GM_CONV(gm_csguankko4lmhd, t47));
                    uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 0));
                    uint8_t t49 = (uint8_t)(t48 == ((uint8_t)14ULL));
                    if (!t49) {
                        goto gm_lb4;
                    }
                    gm_t88rcucg1b7uk t50 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l12) + 8) + 4));
                    gm_l6 = t50;
                    void * t51 = gm_l0;
                    void * t52 = (void *)(((uint8_t *)t51) + 8);
                    gm_t88rcucg1b7uk t53 = gm_l6;
                    uint8_t t54 = gm_f_is_unit_n83ttnb(t52, GM_CONV(gm_csguankko4lmhd, t53));
                    if (!t54) {
                        goto gm_lb4;
                    }
                    gm_l5 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb4:;
            }
gm_lb3:;
            uint8_t t55 = gm_l5;
            uint8_t t56 = (uint8_t)!t55;
            if (t56) {
                void * t57 = gm_l0;
                gm_tl874t2atnlh3 t58 = gm_l2;
                void * t59 = gm_l0;
                gm_t88rcucg1b7uk t60 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 56));
                gm_b16_8 t61 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t59, t60);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t62;
                GM_ST(gm_b16_8, ((uint8_t *)&t62 + 0), t61);
                gm_f_format_l251doo((void *)&gm_l13, (void *)NULL, cx, NULL, t62);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t63 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                if (!t63) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_src5eu5l4a7gv6s, 412, 43);
                gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
                gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
                gm_b0_1 t66 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t57, t58, t65);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 64));
            gm_l7 = t67;
            gm_l8 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t68 = gm_l8;
                    uint64_t t69 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    uint8_t t70 = (t68 < t69);
                    if (!t70) {
                        goto gm_lb5;
                    }
                    {
                        void * t71 = GM_LD(void *, ((uint8_t *)&gm_l7));
                        uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                        uint64_t t73 = gm_l8;
                        {
                            {
                                memmove(((uint8_t *)&gm_l9), (((uint8_t *)t71) + (uintptr_t)t73 * 24), 24);
                                void * t74 = gm_l0;
                                uint32_t t75 = GM_LD(uint32_t, ((uint8_t *)&gm_l9));
                                uint8_t t76 = gm_f_is_ambient_m41pl1e(t74, t75);
                                uint8_t t77 = (uint8_t)!t76;
                                if (t77) {
                                    void * t78 = gm_l0;
                                    gm_tl874t2atnlh3 t79 = gm_l2;
                                    void * t80 = gm_l0;
                                    void * t81 = (void *)(((uint8_t *)t80) + 8);
                                    uint32_t t82 = GM_LD(uint32_t, ((uint8_t *)&gm_l9));
                                    gm_b16_8 t83 = gm_f_effect_name_prpp0ip(t81, t82);
                                    gm_tnpifv20irnph t84;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t84 + 0), t83);
                                    gm_f_format_jhm2032((void *)&gm_l14, (void *)NULL, cx, NULL, t84);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t85 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                                    if (!t85) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_src5eu5l4a7gv6s, 415, 35);
                                    gm_b16_8 t86 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
                                    gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&t86));
                                    gm_b0_1 t88 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t78, t79, t87);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                goto gm_lb8;
                            }
gm_lb9:;
                            gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 413, 13);
                        }
gm_lb8:;
                    }
gm_lb7:;
                    uint64_t t89 = gm_l8;
                    uint64_t r90; bool o91 = __builtin_add_overflow(t89, ((uint64_t)1ULL), &r90);
                    uint64_t t92 = r90;
                    gm_l8 = t92;
                }
            }
gm_lb5:;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t93 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
            uint8_t t94 = (uint8_t)(t93 == ((uint8_t)11ULL));
            if (!t94) {
                goto gm_lb10;
            }
            goto gm_lb1;
        }
gm_lb10:;
        {
            void * t95 = gm_l0;
            gm_tl874t2atnlh3 t96 = gm_l2;
            gm_b16_8 t97;
            GM_ST(void *, (uint8_t *)&t97, (void *)(gm_s6d960pgtv3l9));
            GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(25));
            gm_b0_1 t98 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t95, t96, t97);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb11:;
        gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 406, 5);
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_find_pair_ddtpbq9(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_find_pair_ddtpbq9 F;
    void * gm_l0; /* l */
    uint32_t gm_l1; /* from */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tcc1rq4n1avb2 gm_l4; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 178u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t7 = gm_l0;
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
                        gm_tcc1rq4n1avb2 t15 = GM_LD(gm_tcc1rq4n1avb2, (((uint8_t *)t12) + (uintptr_t)t14 * 8));
                        gm_l4 = t15;
                        uint32_t t16 = GM_LD(uint32_t, ((uint8_t *)&gm_l4));
                        uint32_t t17 = gm_l1;
                        uint8_t t18 = ((t16 == t17));
                        if (t18) {
                            uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 4));
                            gm_t9805qtonbsag t20;
                            GM_ST(uint8_t, (uint8_t *)&t20 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t20 + 6, 0);
                            GM_ST(uint32_t, ((uint8_t *)&t20), t19);
                            ((uint8_t *)&t20)[4] = 1;
                            F.ret = t20;
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 119, 5);
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
    gm_t9805qtonbsag t25;
    memset(&t25, 0, sizeof t25);
    F.ret = t25;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2d811oi(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5puhsbi(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_eg2r5au(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_mono_cond_pats_armprh7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_mono_cond_pats_armprh7 F;
    void * gm_l0; /* s */
    void * gm_l1; /* mc */
    gm_b16_8 gm_l2; /* cond */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_tlr9dp305htm7 gm_l5; /* c */
    void * gm_l6; /* p */
    GM_STACK_CHECK(&F, 195u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 t9 = gm_l2;
    gm_l3 = t9;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t10 = gm_l4;
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t12 = (t10 < t11);
            if (!t12) {
                goto gm_lb1;
            }
            {
                void * t13 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t15 = gm_l4;
                {
                    {
                        memmove(((uint8_t *)&gm_l5), (((uint8_t *)t13) + (uintptr_t)t15 * 24), 24);
                        {
                            {
                                uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                uint8_t t17 = (uint8_t)(t16 == ((uint8_t)1ULL));
                                if (!t17) {
                                    goto gm_lb7;
                                }
                                void * t18 = GM_LD(void *, (((uint8_t *)&gm_l5) + 8));
                                gm_l6 = t18;
                                void * t19 = gm_l0;
                                void * t20 = gm_l1;
                                void * t21 = gm_l6;
                                gm_b0_1 t22 = gm_f_mono_pat_lipeb0h((void *)NULL, cx, NULL, t19, t20, t21);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 273, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t23 = gm_l4;
            uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
            uint64_t t26 = r24;
            gm_l4 = t26;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_mono_expr_p8qb7fe(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_mono_expr_p8qb7fe F;
    void * gm_l2; /* e */
    uint8_t gm_l3; /* had_param */
    gm_t88rcucg1b7uk gm_l4; /* t */
    gm_tl874t2atnlh3 gm_l5; /* span */
    uint32_t gm_l6; /* p */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_tl874t2atnlh3 gm_l9; /* x */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_tplpbqf3duvn3 gm_l12; /* b */
    gm_t428ue2u3hf5g gm_l13; /* k */
    gm_t88rcucg1b7uk gm_l14; /* kt */
    gm_b16_8 gm_l15; /* bytes */
    uint32_t gm_l17; /* f */
    gm_b16_8 gm_l18; /* bytes */
    gm_t88rcucg1b7uk gm_l20; /* of */
    uint8_t gm_l21; /* align */
    gm_t88rcucg1b7uk gm_l22; /* ot */
    gm_tbccvikg6b96e gm_l23; /* l */
    gm_t88rcucg1b7uk gm_l24; /* of */
    uint8_t gm_l25; /* max */
    gm_t88rcucg1b7uk gm_l26; /* ot */
    gm_t88rcucg1b7uk gm_l28; /* of */
    gm_t88rcucg1b7uk gm_l29; /* ot */
    gm_t88rcucg1b7uk gm_l30; /* a */
    gm_t88rcucg1b7uk gm_l31; /* b */
    uint8_t gm_l32; /* negated */
    gm_t88rcucg1b7uk gm_l33; /* sa */
    gm_t88rcucg1b7uk gm_l34; /* sb */
    void * gm_l35; /* r */
    uint16_t gm_l36; /* bits */
    double gm_l37; /* x */
    uint64_t gm_l39; /* <tmp> */
    uint64_t gm_l40; /* <tmp> */
    uint64_t gm_l41; /* i */
    void * gm_l42; /* value */
    void * gm_l43; /* w */
    uint32_t gm_l44; /* p */
    gm_b16_8 gm_l45; /* <tmp> */
    uint64_t gm_l46; /* <tmp> */
    gm_tl874t2atnlh3 gm_l47; /* x */
    uint32_t gm_l48; /* c */
    gm_b16_8 gm_l49; /* args */
    gm_tiilmfd16v3fc gm_l50; /* eref */
    uint32_t gm_l51; /* op */
    gm_b16_8 gm_l52; /* args */
    uint32_t gm_l53; /* c */
    uint32_t gm_l54; /* c */
    uint32_t gm_l55; /* h */
    void * gm_l56; /* w */
    gm_tiilmfd16v3fc gm_l57; /* eref */
    uint32_t gm_l58; /* op */
    gm_b16_8 gm_l59; /* arms */
    gm_b16_8 gm_l60; /* <tmp> */
    uint64_t gm_l61; /* <tmp> */
    gm_tbbqefuhv2r8h gm_l62; /* a */
    gm_trl2khmuphncd gm_l63; /* x */
    void * gm_l65; /* w */
    uint64_t gm_l66; /* <tmp> */
    uint64_t gm_l67; /* <tmp> */
    uint64_t gm_l68; /* k */
    gm_b16_8 gm_l69; /* msg */
    uint32_t gm_l70; /* id */
    gm_b16_8 gm_l71; /* args */
    gm_b16_8 gm_l72; /* args */
    gm_t88rcucg1b7uk gm_l73; /* src */
    uint64_t gm_l74; /* a */
    uint64_t gm_l75; /* b */
    GM_STACK_CHECK(&F, 6801u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l76;
    gm_thrdpodrs8pgt gm_l77;
    uint8_t gm_l78;
    uint8_t gm_l79;
    gm_t53at80pfdus3 gm_l80;
    gm_tabb710d9mq2e gm_l81;
    uint64_t gm_l82;
    gm_to0f5qn75cgg8 gm_l83;
    gm_thrdpodrs8pgt gm_l84;
    gm_to0f5qn75cgg8 gm_l85;
    gm_to0f5qn75cgg8 gm_l86;
    uint8_t gm_l87;
    gm_t53at80pfdus3 gm_l88;
    gm_tabb710d9mq2e gm_l89;
    gm_tabb710d9mq2e gm_l90;
    gm_to0f5qn75cgg8 gm_l91;
    gm_t9f76dterbqj6 gm_l92;
    gm_th2lqbo77pvsc gm_l93;
    gm_tabb710d9mq2e gm_l94;
    uint8_t gm_l95;
    uint8_t gm_l96;
    gm_tbccvikg6b96e gm_l97;
    gm_tbccvikg6b96e gm_l98;
    gm_tabb710d9mq2e gm_l99;
    {
        void * t95 = F.l0;
        void * t96 = (void *)(((uint8_t *)t95) + 8);
        void * t97 = gm_l2;
        gm_t88rcucg1b7uk t98 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t97));
        uint8_t t99 = gm_f_has_param_u9t0efs(t96, GM_CONV(gm_csguankko4lmhd, t98));
        gm_l3 = t99;
        void * t100 = gm_l2;
        void * t101 = F.l0;
        void * t102 = F.l1;
        void * t103 = gm_l2;
        gm_t88rcucg1b7uk t104 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t103));
        gm_t88rcucg1b7uk t105 = gm_f_mono_ty_atcu0jn((void *)NULL, cx, NULL, t101, t102, t104);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)t100), t105);
        void * t106 = gm_l2;
        gm_t88rcucg1b7uk t107 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t106));
        gm_l4 = t107;
        void * t108 = F.l1;
        gm_tl874t2atnlh3 t109 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t108) + 96));
        gm_l5 = t109;
        {
            {
                void * t110 = gm_l2;
                uint8_t t111 = GM_LD(uint8_t, ((((uint8_t *)t110) + 8) + 0));
                uint8_t t112 = (uint8_t)(t111 == ((uint8_t)9ULL));
                if (!t112) {
                    goto gm_lb2;
                }
                uint32_t t113 = GM_LD(uint32_t, ((((uint8_t *)t110) + 8) + 8));
                gm_l6 = t113;
                void * t114 = F.l1;
                gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)t114) + 120));
                gm_l7 = t115;
                gm_l8 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t117 = gm_l8;
                        uint64_t t118 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                        uint8_t t119 = (t117 < t118);
                        if (!t119) {
                            goto gm_lb3;
                        }
                        {
                            void * t120 = GM_LD(void *, ((uint8_t *)&gm_l7));
                            uint64_t t121 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                            uint64_t t122 = gm_l8;
                            {
                                {
                                    gm_tl874t2atnlh3 t123 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t120) + (uintptr_t)t122 * 12));
                                    gm_l9 = t123;
                                    uint32_t t124 = GM_LD(uint32_t, ((uint8_t *)&gm_l9));
                                    uint32_t t125 = gm_l6;
                                    uint8_t t126 = (!(t124 == t125));
                                    if (t126) {
                                        goto gm_lb5;
                                    }
                                    void * t127 = gm_l2;
                                    uint32_t t128 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 8));
                                    gm_to0f5qn75cgg8 t129;
                                    memset(&t129, 0, sizeof t129);
                                    GM_ST(uint8_t, ((uint8_t *)&t129 + 0), ((uint8_t)15ULL));
                                    GM_ST(uint32_t, ((uint8_t *)&t129 + 8), t128);
                                    GM_ST(gm_to0f5qn75cgg8, (((uint8_t *)t127) + 8), t129);
                                    F.ret = GM_ZERO(gm_b0_1);
                                    goto gm_fret;
                                }
gm_lb7:;
                                gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 135, 9);
                            }
gm_lb6:;
                        }
gm_lb5:;
                        uint64_t t130 = gm_l8;
                        uint64_t r131; bool o132 = __builtin_add_overflow(t130, ((uint64_t)1ULL), &r131);
                        uint64_t t133 = r131;
                        gm_l8 = t133;
                    }
                }
gm_lb3:;
                void * t134 = F.l1;
                gm_b16_8 t135 = GM_LD(gm_b16_8, ((uint8_t *)t134));
                gm_l10 = t135;
                gm_l11 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t137 = gm_l11;
                        uint64_t t138 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                        uint8_t t139 = (t137 < t138);
                        if (!t139) {
                            goto gm_lb8;
                        }
                        {
                            void * t140 = GM_LD(void *, ((uint8_t *)&gm_l10));
                            uint64_t t141 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                            uint64_t t142 = gm_l11;
                            {
                                {
                                    gm_tplpbqf3duvn3 t143 = GM_LD(gm_tplpbqf3duvn3, (((uint8_t *)t140) + (uintptr_t)t142 * 8));
                                    gm_l12 = t143;
                                    uint32_t t144 = GM_LD(uint32_t, ((uint8_t *)&gm_l12));
                                    uint32_t t145 = gm_l6;
                                    uint8_t t146 = (!(t144 == t145));
                                    if (t146) {
                                        goto gm_lb10;
                                    }
                                    {
                                        {
                                            {
                                                void * t147 = F.l0;
                                                void * t148 = (void *)(((uint8_t *)t147) + 8);
                                                gm_t88rcucg1b7uk t149 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l12) + 4));
                                                gm_f_kind_jcnt4hm((void *)&gm_l76, t148, GM_CONV(gm_csguankko4lmhd, t149));
                                            }
                                            uint8_t t150 = GM_LD(uint8_t, (((uint8_t *)&gm_l76) + 0));
                                            uint8_t t151 = (uint8_t)(t150 == ((uint8_t)21ULL));
                                            if (!t151) {
                                                goto gm_lb14;
                                            }
                                            {
                                                memmove(((uint8_t *)&gm_l13), (((uint8_t *)&gm_l76) + 8), 32);
                                            }
                                            {
                                                {
                                                    uint8_t t152 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
                                                    uint8_t t153 = (uint8_t)(t152 == ((uint8_t)0ULL));
                                                    if (!t153) {
                                                        goto gm_lb16;
                                                    }
                                                    gm_t88rcucg1b7uk t154 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l13) + 8));
                                                    gm_l14 = t154;
                                                    gm_b16_8 t155 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l13) + 8) + 8));
                                                    gm_l15 = t155;
                                                    {
                                                        {
                                                            {
                                                                void * t156 = F.l0;
                                                                gm_t428ue2u3hf5g t157 = GM_LD(gm_t428ue2u3hf5g, ((uint8_t *)&gm_l13));
                                                                gm_f_key_int_pqscuvn((void *)&gm_l77, (void *)NULL, cx, NULL, t156, &t157);
                                                                if (gm_unw.kind) {
                                                                    goto gm_ll136;
                                                                }
                                                            }
                                                            uint8_t t158 = (uint8_t)((((uint8_t *)&gm_l77))[48] != 0);
                                                            if (!t158) {
                                                                goto gm_lb18;
                                                            }
                                                            {
                                                                memmove(((uint8_t *)&F.l16), ((uint8_t *)&gm_l77), 48);
                                                            }
                                                            {
                                                                void * t159 = gm_l2;
                                                                gm_tbci5ospk5nnn t160 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l16));
                                                                gm_to0f5qn75cgg8 t161;
                                                                GM_ST(uint8_t, (uint8_t *)&t161 + 1, 0);
                                                                GM_ST(uint16_t, (uint8_t *)&t161 + 2, 0);
                                                                GM_ST(uint32_t, (uint8_t *)&t161 + 4, 0);
                                                                GM_ST(uint64_t, (uint8_t *)&t161 + 56, 0);
                                                                GM_ST(uint8_t, ((uint8_t *)&t161 + 0), ((uint8_t)0ULL));
                                                                GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t161 + 8), t160);
                                                                GM_ST(gm_to0f5qn75cgg8, (((uint8_t *)t159) + 8), t161);
                                                            }
                                                            goto gm_lb17;
                                                        }
gm_lb18:;
                                                        {
                                                            void * t162 = F.l0;
                                                            void * t163 = (void *)(((uint8_t *)t162) + 8);
                                                            gm_t88rcucg1b7uk t164 = gm_l14;
                                                            gm_t53at80pfdus3 t165;
                                                            gm_f_kind_jcnt4hm((void *)&t165, t163, GM_CONV(gm_csguankko4lmhd, t164));
                                                            uint8_t t166 = GM_LD(uint8_t, (((uint8_t *)&t165) + 0));
                                                            uint8_t t167 = (uint8_t)((t166 == ((uint8_t)4ULL)));
                                                            gm_t53at80pfdus3 t168;
                                                            memset(&t168, 0, sizeof t168);
                                                            GM_ST(uint8_t, ((uint8_t *)&t168 + 0), ((uint8_t)4ULL));
                                                            if (t167) {
                                                                void * t169 = gm_l2;
                                                                uint64_t t170 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                                                uint8_t t171 = (t170 > ((uint64_t)0ULL));
                                                                if (t171) {
                                                                    void * t172 = GM_LD(void *, ((uint8_t *)&gm_l15));
                                                                    uint64_t t173 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                                                    uint8_t t174 = GM_LD(uint8_t, (((uint8_t *)t172) + (uintptr_t)((uint64_t)0ULL) * 1));
                                                                    uint8_t t175 = ((t174 == ((uint8_t)1ULL)));
                                                                    gm_l78 = t175;
                                                                } else {
                                                                    gm_l78 = ((uint8_t)0);
                                                                }
                                                                uint8_t t176 = gm_l78;
                                                                gm_to0f5qn75cgg8 t177;
                                                                memset(&t177, 0, sizeof t177);
                                                                GM_ST(uint8_t, ((uint8_t *)&t177 + 0), ((uint8_t)4ULL));
                                                                GM_ST(uint8_t, ((uint8_t *)&t177 + 8), t176);
                                                                GM_ST(gm_to0f5qn75cgg8, (((uint8_t *)t169) + 8), t177);
                                                            } else {
                                                                void * t178 = F.l1;
                                                                uint8_t t179 = GM_LD(uint8_t, (((uint8_t *)t178) + 108));
                                                                if (t179) {
                                                                    void * t180 = F.l0;
                                                                    void * t181 = gm_l2;
                                                                    gm_tl874t2atnlh3 t182 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t181) + 72));
                                                                    gm_b16_8 t183;
                                                                    GM_ST(void *, (uint8_t *)&t183, (void *)(gm_sjg57h040l08d));
                                                                    GM_ST(uintptr_t, (uint8_t *)&t183 + 8, (uintptr_t)(55));
                                                                    gm_b0_1 t184 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t180, t182, t183);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_ll136;
                                                                    }
                                                                }
                                                            }
                                                        }
                                                    }
gm_lb17:;
                                                    goto gm_lb15;
                                                }
gm_lb16:;
                                                {
                                                    uint8_t t185 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
                                                    uint8_t t186 = (uint8_t)(t185 == ((uint8_t)1ULL));
                                                    if (!t186) {
                                                        goto gm_lb19;
                                                    }
                                                    uint32_t t187 = GM_LD(uint32_t, (((uint8_t *)&gm_l13) + 8));
                                                    gm_l17 = t187;
                                                    void * t188 = gm_l2;
                                                    uint32_t t189 = gm_l17;
                                                    gm_to0f5qn75cgg8 t190;
                                                    memset(&t190, 0, sizeof t190);
                                                    GM_ST(uint8_t, ((uint8_t *)&t190 + 0), ((uint8_t)26ULL));
                                                    GM_ST(uint32_t, ((uint8_t *)&t190 + 8), t189);
                                                    GM_ST(gm_to0f5qn75cgg8, (((uint8_t *)t188) + 8), t190);
                                                    goto gm_lb15;
                                                }
gm_lb19:;
                                                {
                                                    uint8_t t191 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
                                                    uint8_t t192 = (uint8_t)(t191 == ((uint8_t)2ULL));
                                                    if (!t192) {
                                                        goto gm_lb20;
                                                    }
                                                    void * t193 = gm_l2;
                                                    gm_to0f5qn75cgg8 t194;
                                                    memset(&t194, 0, sizeof t194);
                                                    GM_ST(uint8_t, ((uint8_t *)&t194 + 0), ((uint8_t)7ULL));
                                                    GM_ST(gm_to0f5qn75cgg8, (((uint8_t *)t193) + 8), t194);
                                                    goto gm_lb15;
                                                }
gm_lb20:;
                                                {
                                                    uint8_t t195 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
                                                    uint8_t t196 = (uint8_t)(t195 == ((uint8_t)3ULL));
                                                    if (!t196) {
                                                        goto gm_lb21;
                                                    }
                                                    gm_b16_8 t197 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
                                                    gm_l18 = t197;
                                                    void * t198 = gm_l2;
                                                    gm_b16_8 t199 = gm_l18;
                                                    gm_to0f5qn75cgg8 t200;
                                                    memset(&t200, 0, sizeof t200);
                                                    GM_ST(uint8_t, ((uint8_t *)&t200 + 0), ((uint8_t)5ULL));
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t200 + 8), t199);
                                                    GM_ST(gm_to0f5qn75cgg8, (((uint8_t *)t198) + 8), t200);
                                                    goto gm_lb15;
                                                }
gm_lb21:;
                                                gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 143, 13);
                                            }
gm_lb15:;
                                            F.ret = GM_ZERO(gm_b0_1);
                                            goto gm_fret;
                                        }
gm_lb14:;
                                        goto gm_lb8;
                                    }
gm_lb13:;
                                }
gm_lb12:;
                                gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 140, 9);
                            }
gm_lb11:;
                        }
gm_lb10:;
                        uint64_t t201 = gm_l11;
                        uint64_t r202; bool o203 = __builtin_add_overflow(t201, ((uint64_t)1ULL), &r202);
                        uint64_t t204 = r202;
                        gm_l11 = t204;
                    }
                }
                goto gm_ls205;
gm_ll136:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb8; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb10; }
                }
                goto gm_lad;
gm_ls205:;
gm_lb8:;
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
gm_lb2:;
        }
gm_lb1:;
    }
    {
        {
            {
                {
                    void * t206 = gm_l2;
                    uint8_t t207 = GM_LD(uint8_t, ((((uint8_t *)t206) + 8) + 0));
                    uint8_t t208 = (uint8_t)(t207 == ((uint8_t)0ULL));
                    if (!t208) {
                        goto gm_lb23;
                    }
                    memmove(((uint8_t *)&F.l19), ((((uint8_t *)t206) + 8) + 8), 48);
                }
                uint8_t t209 = gm_l3;
                if (!t209) {
                    goto gm_lb23;
                }
                {
                    void * t210 = F.l0;
                    void * t211 = (void *)(((uint8_t *)t210) + 8);
                    gm_t88rcucg1b7uk t212 = gm_l4;
                    gm_f_kind_jcnt4hm((void *)&gm_l80, t211, GM_CONV(gm_csguankko4lmhd, t212));
                }
                {
                    {
                        uint8_t t213 = GM_LD(uint8_t, (((uint8_t *)&gm_l80) + 0));
                        uint8_t t214 = (uint8_t)(t213 == ((uint8_t)3ULL));
                        if (!t214) {
                            goto gm_lb25;
                        }
                        gm_l79 = ((uint8_t)1);
                        goto gm_lb24;
                    }
gm_lb25:;
                    {
                        gm_l79 = ((uint8_t)0);
                        goto gm_lb24;
                    }
gm_lb26:;
                    gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 163, 12);
                }
gm_lb24:;
                uint8_t t215 = gm_l79;
                if (t215) {
                    void * t216 = gm_l2;
                    gm_tngtuiuskn938 t217;
                    gm_f_from_int_ofjuoqh((void *)&t217, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l19)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_to0f5qn75cgg8 t218;
                    gm_f_untyped_float_m6lbr0g((void *)&t218, (void *)NULL, cx, NULL, &t217);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(gm_to0f5qn75cgg8, (((uint8_t *)t216) + 8), t218);
                } else {
                    void * t219 = F.l0;
                    gm_t88rcucg1b7uk t220 = gm_l4;
                    uint8_t t221 = gm_f_int_fits_imbqv0c((void *)NULL, cx, NULL, t219, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l19)), t220);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t222 = (uint8_t)!t221;
                    if (t222) {
                        void * t223 = F.l0;
                        gm_tl874t2atnlh3 t224 = gm_l5;
                        gm_b16_8 t225 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l19)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t226 = F.l0;
                        gm_t88rcucg1b7uk t227 = gm_l4;
                        gm_b16_8 t228 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t226, t227);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t6j6smj7qoctu t229;
                        GM_ST(gm_b16_8, ((uint8_t *)&t229 + 0), t225);
                        GM_ST(gm_b16_8, ((uint8_t *)&t229 + 16), t228);
                        gm_f_format_afagrs5((void *)&gm_l81, (void *)NULL, cx, NULL, &t229);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t230 = (uint8_t)!((((uint8_t *)&gm_l81))[0] != 0);
                        if (!t230) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l81) + 8, gm_src5eu5l4a7gv6s, 166, 27);
                        gm_b16_8 t231 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l81) + 8));
                        gm_b16_8 t232 = GM_LD(gm_b16_8, ((uint8_t *)&t231));
                        gm_b0_1 t233 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t223, t224, t232);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                }
                goto gm_lb22;
            }
gm_lb23:;
        }
gm_lb22:;
        void * t234 = gm_l2;
        {
            {
                uint8_t t235 = GM_LD(uint8_t, ((((uint8_t *)t234) + 8) + 0));
                uint8_t t236 = (uint8_t)(t235 == ((uint8_t)10ULL));
                if (!t236) {
                    goto gm_lb28;
                }
                gm_t88rcucg1b7uk t237 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t234) + 8) + 8));
                gm_l20 = t237;
                uint8_t t238 = GM_LD(uint8_t, (((((uint8_t *)t234) + 8) + 8) + 4));
                gm_l21 = t238;
                void * t239 = F.l0;
                gm_t88rcucg1b7uk t240 = gm_l20;
                void * t241 = F.l1;
                gm_b16_8 t242 = GM_LD(gm_b16_8, ((uint8_t *)t241));
                gm_t88rcucg1b7uk t243 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t239, t240, t242);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l22 = t243;
                void * t244 = F.l0;
                void * t245 = (void *)(((uint8_t *)t244) + 8);
                gm_t88rcucg1b7uk t246 = gm_l22;
                uint8_t t247 = gm_f_has_param_u9t0efs(t245, GM_CONV(gm_csguankko4lmhd, t246));
                if (t247) {
                    void * t248 = gm_l2;
                    gm_t88rcucg1b7uk t249 = gm_l22;
                    uint8_t t250 = gm_l21;
                    gm_tkn42u7na3t81 t251;
                    GM_ST(uint8_t, (uint8_t *)&t251 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t251 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t251 + 0), t249);
                    GM_ST(uint8_t, ((uint8_t *)&t251 + 4), t250);
                    gm_to0f5qn75cgg8 t252;
                    memset(&t252, 0, sizeof t252);
                    GM_ST(uint8_t, ((uint8_t *)&t252 + 0), ((uint8_t)10ULL));
                    GM_ST(gm_tkn42u7na3t81, ((uint8_t *)&t252 + 8), t251);
                    GM_ST(gm_to0f5qn75cgg8, (((uint8_t *)t248) + 8), t252);
                } else {
                    {
                        void * t253 = F.l0;
                        gm_t88rcucg1b7uk t254 = gm_l22;
                        gm_f_layout_a5ovnrm((void *)&gm_l23, (void *)NULL, cx, NULL, t253, t254);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t255 = gm_l2;
                        uint8_t t256 = gm_l21;
                        if (t256) {
                            uint64_t t257 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                            gm_l82 = t257;
                        } else {
                            uint64_t t258 = GM_LD(uint64_t, ((uint8_t *)&gm_l23));
                            gm_l82 = t258;
                        }
                        uint64_t t259 = gm_l82;
                        gm_tbci5ospk5nnn t260;
                        gm_f_from_u64_ud13q5v((void *)&t260, (void *)NULL, cx, NULL, t259);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_to0f5qn75cgg8 t261;
                        GM_ST(uint8_t, (uint8_t *)&t261 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t261 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t261 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t261 + 56, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t261 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t261 + 8), t260);
                        GM_ST(gm_to0f5qn75cgg8, (((uint8_t *)t255) + 8), t261);
                    }
                }
                goto gm_lb27;
            }
gm_lb28:;
            {
                uint8_t t262 = GM_LD(uint8_t, ((((uint8_t *)t234) + 8) + 0));
                uint8_t t263 = (uint8_t)(t262 == ((uint8_t)11ULL));
                if (!t263) {
                    goto gm_lb29;
                }
                gm_t88rcucg1b7uk t264 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t234) + 8) + 8));
                gm_l24 = t264;
                uint8_t t265 = GM_LD(uint8_t, (((((uint8_t *)t234) + 8) + 8) + 4));
                gm_l25 = t265;
                void * t266 = F.l0;
                gm_t88rcucg1b7uk t267 = gm_l24;
                void * t268 = F.l1;
                gm_b16_8 t269 = GM_LD(gm_b16_8, ((uint8_t *)t268));
                gm_t88rcucg1b7uk t270 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t266, t267, t269);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l26 = t270;
                void * t271 = gm_l2;
                {
                    {
                        {
                            void * t272 = F.l0;
                            gm_t88rcucg1b7uk t273 = gm_l26;
                            uint8_t t274 = gm_l25;
                            gm_f_int_limit_nncqn56((void *)&gm_l84, (void *)NULL, cx, NULL, t272, t273, t274);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t275 = (uint8_t)((((uint8_t *)&gm_l84))[48] != 0);
                        if (!t275) {
                            goto gm_lb31;
                        }
                        {
                            memmove(((uint8_t *)&F.l27), ((uint8_t *)&gm_l84), 48);
                        }
                        {
                            gm_tbci5ospk5nnn t276 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l27));
                            GM_ST(uint8_t, (uint8_t *)&gm_l83 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&gm_l83 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&gm_l83 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&gm_l83 + 56, 0);
                            GM_ST(uint8_t, ((uint8_t *)&gm_l83 + 0), ((uint8_t)0ULL));
                            GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&gm_l83 + 8), t276);
                        }
                        goto gm_lb30;
                    }
gm_lb31:;
                    gm_t88rcucg1b7uk t277 = gm_l26;
                    uint8_t t278 = gm_l25;
                    gm_tkn42u7na3t81 t279;
                    GM_ST(uint8_t, (uint8_t *)&t279 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t279 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t279 + 0), t277);
                    GM_ST(uint8_t, ((uint8_t *)&t279 + 4), t278);
                    memset(&gm_l83, 0, sizeof gm_l83);
                    GM_ST(uint8_t, ((uint8_t *)&gm_l83 + 0), ((uint8_t)11ULL));
                    GM_ST(gm_tkn42u7na3t81, ((uint8_t *)&gm_l83 + 8), t279);
                }
gm_lb30:;
                memmove((((uint8_t *)t271) + 8), ((uint8_t *)&gm_l83), 64);
                goto gm_lb27;
            }
gm_lb29:;
            {
                uint8_t t280 = GM_LD(uint8_t, ((((uint8_t *)t234) + 8) + 0));
                uint8_t t281 = (uint8_t)(t280 == ((uint8_t)12ULL));
                if (!t281) {
                    goto gm_lb32;
                }
                gm_t88rcucg1b7uk t282 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t234) + 8) + 8));
                gm_l28 = t282;
                void * t283 = F.l0;
                gm_t88rcucg1b7uk t284 = gm_l28;
                void * t285 = F.l1;
                gm_b16_8 t286 = GM_LD(gm_b16_8, ((uint8_t *)t285));
                gm_t88rcucg1b7uk t287 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t283, t284, t286);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l29 = t287;
                void * t288 = gm_l2;
                void * t289 = F.l0;
                void * t290 = (void *)(((uint8_t *)t289) + 8);
                gm_t88rcucg1b7uk t291 = gm_l29;
                uint8_t t292 = gm_f_has_param_u9t0efs(t290, GM_CONV(gm_csguankko4lmhd, t291));
                if (t292) {
                    gm_t88rcucg1b7uk t293 = gm_l29;
                    memset(&gm_l85, 0, sizeof gm_l85);
                    GM_ST(uint8_t, ((uint8_t *)&gm_l85 + 0), ((uint8_t)12ULL));
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l85 + 8), t293);
                } else {
                    void * t294 = F.l0;
                    gm_t88rcucg1b7uk t295 = gm_l29;
                    gm_b16_8 t296 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t294, t295);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    memset(&gm_l85, 0, sizeof gm_l85);
                    GM_ST(uint8_t, ((uint8_t *)&gm_l85 + 0), ((uint8_t)5ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&gm_l85 + 8), t296);
                }
                memmove((((uint8_t *)t288) + 8), ((uint8_t *)&gm_l85), 64);
                goto gm_lb27;
            }
gm_lb32:;
            {
                uint8_t t297 = GM_LD(uint8_t, ((((uint8_t *)t234) + 8) + 0));
                uint8_t t298 = (uint8_t)(t297 == ((uint8_t)13ULL));
                if (!t298) {
                    goto gm_lb33;
                }
                gm_t88rcucg1b7uk t299 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t234) + 8) + 8));
                gm_l30 = t299;
                gm_t88rcucg1b7uk t300 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t234) + 8) + 8) + 4));
                gm_l31 = t300;
                uint8_t t301 = GM_LD(uint8_t, (((((uint8_t *)t234) + 8) + 8) + 8));
                gm_l32 = t301;
                void * t302 = F.l0;
                gm_t88rcucg1b7uk t303 = gm_l30;
                void * t304 = F.l1;
                gm_b16_8 t305 = GM_LD(gm_b16_8, ((uint8_t *)t304));
                gm_t88rcucg1b7uk t306 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t302, t303, t305);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l33 = t306;
                void * t307 = F.l0;
                gm_t88rcucg1b7uk t308 = gm_l31;
                void * t309 = F.l1;
                gm_b16_8 t310 = GM_LD(gm_b16_8, ((uint8_t *)t309));
                gm_t88rcucg1b7uk t311 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t307, t308, t310);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l34 = t311;
                void * t312 = gm_l2;
                void * t313 = F.l0;
                void * t314 = (void *)(((uint8_t *)t313) + 8);
                gm_t88rcucg1b7uk t315 = gm_l33;
                uint8_t t316 = gm_f_has_param_u9t0efs(t314, GM_CONV(gm_csguankko4lmhd, t315));
                if (t316) {
                    gm_l87 = ((uint8_t)1);
                } else {
                    void * t317 = F.l0;
                    void * t318 = (void *)(((uint8_t *)t317) + 8);
                    gm_t88rcucg1b7uk t319 = gm_l34;
                    uint8_t t320 = gm_f_has_param_u9t0efs(t318, GM_CONV(gm_csguankko4lmhd, t319));
                    gm_l87 = t320;
                }
                uint8_t t321 = gm_l87;
                if (t321) {
                    gm_t88rcucg1b7uk t322 = gm_l33;
                    gm_t88rcucg1b7uk t323 = gm_l34;
                    uint8_t t324 = gm_l32;
                    gm_t3rcjibl8kkg4 t325;
                    GM_ST(uint8_t, (uint8_t *)&t325 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t325 + 10, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t325 + 0), t322);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t325 + 4), t323);
                    GM_ST(uint8_t, ((uint8_t *)&t325 + 8), t324);
                    memset(&gm_l86, 0, sizeof gm_l86);
                    GM_ST(uint8_t, ((uint8_t *)&gm_l86 + 0), ((uint8_t)13ULL));
                    GM_ST(gm_t3rcjibl8kkg4, ((uint8_t *)&gm_l86 + 8), t325);
                } else {
                    gm_t88rcucg1b7uk t326 = gm_l33;
                    gm_t88rcucg1b7uk t327 = gm_l34;
                    uint8_t t328 = (gm_eq9m6mvtmgde(((uint8_t *)&t326), ((uint8_t *)&t327)));
                    uint8_t t329 = gm_l32;
                    uint8_t t330 = (!(t328 == t329));
                    memset(&gm_l86, 0, sizeof gm_l86);
                    GM_ST(uint8_t, ((uint8_t *)&gm_l86 + 0), ((uint8_t)4ULL));
                    GM_ST(uint8_t, ((uint8_t *)&gm_l86 + 8), t330);
                }
                memmove((((uint8_t *)t312) + 8), ((uint8_t *)&gm_l86), 64);
                goto gm_lb27;
            }
gm_lb33:;
            {
                goto gm_lb27;
            }
gm_lb34:;
            gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 169, 5);
        }
gm_lb27:;
    }
    {
        {
            {
                void * t331 = gm_l2;
                uint8_t t332 = GM_LD(uint8_t, ((((uint8_t *)t331) + 8) + 0));
                uint8_t t333 = (uint8_t)(t332 == ((uint8_t)2ULL));
                if (!t333) {
                    goto gm_lb36;
                }
                void * t334 = GM_LD(void *, ((((uint8_t *)t331) + 8) + 8));
                gm_l35 = t334;
                {
                    {
                        {
                            void * t335 = F.l0;
                            void * t336 = (void *)(((uint8_t *)t335) + 8);
                            gm_t88rcucg1b7uk t337 = gm_l4;
                            gm_f_kind_jcnt4hm((void *)&gm_l88, t336, GM_CONV(gm_csguankko4lmhd, t337));
                        }
                        uint8_t t338 = GM_LD(uint8_t, (((uint8_t *)&gm_l88) + 0));
                        uint8_t t339 = (uint8_t)(t338 == ((uint8_t)3ULL));
                        if (!t339) {
                            goto gm_lb38;
                        }
                        uint16_t t340 = GM_LD(uint16_t, (((uint8_t *)&gm_l88) + 8));
                        gm_l36 = t340;
                        {
                            void * t341 = gm_l35;
                            uint16_t t342 = gm_l36;
                            double t343 = gm_f_to_float_o1iop8t((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)t341)), t342);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l37 = t343;
                        }
                        double t344 = gm_l37;
                        double t345 = gm_l37;
                        double t346 = (t344 - t345);
                        uint8_t t347 = (!(t346 == ((double)0e0)));
                        if (t347) {
                            void * t348 = F.l0;
                            gm_tl874t2atnlh3 t349 = gm_l5;
                            void * t350 = F.l0;
                            gm_t88rcucg1b7uk t351 = gm_l4;
                            gm_b16_8 t352 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t350, t351);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tnpifv20irnph t353;
                            GM_ST(gm_b16_8, ((uint8_t *)&t353 + 0), t352);
                            gm_f_format_r4smojn((void *)&gm_l89, (void *)NULL, cx, NULL, t353);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t354 = (uint8_t)!((((uint8_t *)&gm_l89))[0] != 0);
                            if (!t354) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l89) + 8, gm_src5eu5l4a7gv6s, 198, 31);
                            gm_b16_8 t355 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l89) + 8));
                            gm_b16_8 t356 = GM_LD(gm_b16_8, ((uint8_t *)&t355));
                            gm_b0_1 t357 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t348, t349, t356);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        } else {
                            void * t358 = gm_l2;
                            double t359 = gm_l37;
                            gm_to0f5qn75cgg8 t360;
                            memset(&t360, 0, sizeof t360);
                            GM_ST(uint8_t, ((uint8_t *)&t360 + 0), ((uint8_t)1ULL));
                            GM_ST(double, ((uint8_t *)&t360 + 8), t359);
                            GM_ST(gm_to0f5qn75cgg8, (((uint8_t *)t358) + 8), t360);
                        }
                        goto gm_lb37;
                    }
gm_lb38:;
                    void * t361 = F.l0;
                    gm_tl874t2atnlh3 t362 = gm_l5;
                    void * t363 = F.l0;
                    gm_t88rcucg1b7uk t364 = gm_l4;
                    gm_b16_8 t365 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t363, t364);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t366;
                    GM_ST(gm_b16_8, ((uint8_t *)&t366 + 0), t365);
                    gm_f_format_ht23jk8((void *)&gm_l90, (void *)NULL, cx, NULL, t366);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t367 = (uint8_t)!((((uint8_t *)&gm_l90))[0] != 0);
                    if (!t367) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l90) + 8, gm_src5eu5l4a7gv6s, 203, 27);
                    gm_b16_8 t368 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l90) + 8));
                    gm_b16_8 t369 = GM_LD(gm_b16_8, ((uint8_t *)&t368));
                    gm_b0_1 t370 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t361, t362, t369);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
gm_lb37:;
                goto gm_lb35;
            }
gm_lb36:;
        }
gm_lb35:;
        {
            {
                void * t371 = gm_l2;
                uint8_t t372 = GM_LD(uint8_t, ((((uint8_t *)t371) + 8) + 0));
                uint8_t t373 = (uint8_t)(t372 == ((uint8_t)59ULL));
                if (!t373) {
                    goto gm_lb40;
                }
                memmove(((uint8_t *)&F.l38), ((((uint8_t *)t371) + 8) + 8), 40);
                gm_l40 = ((uint64_t)0ULL);
                uint64_t t374 = GM_LD(uint64_t, ((((uint8_t *)&F.l38) + 8) + 8));
                gm_l39 = t374;
                {
                    for (;;) {
                        uint64_t t376 = gm_l40;
                        uint64_t t377 = gm_l39;
                        uint8_t t378 = (t376 < t377);
                        if (!t378) {
                            goto gm_lb41;
                        }
                        {
                            {
                                {
                                    uint64_t t379 = gm_l40;
                                    gm_l41 = t379;
                                    {
                                        {
                                            void * t380 = GM_LD(void *, (((uint8_t *)&F.l38) + 8));
                                            uint64_t t381 = GM_LD(uint64_t, ((((uint8_t *)&F.l38) + 8) + 8));
                                            uint64_t t382 = gm_l41;
                                            uint8_t t383 = GM_LD(uint8_t, ((((uint8_t *)t380) + (uintptr_t)t382 * 32) + 0));
                                            uint8_t t384 = (uint8_t)(t383 == ((uint8_t)1ULL));
                                            if (!t384) {
                                                goto gm_lb47;
                                            }
                                            void * t385 = GM_LD(void *, (((((uint8_t *)t380) + (uintptr_t)t382 * 32) + 8) + 16));
                                            gm_l42 = t385;
                                            {
                                                void * t386 = GM_LD(void *, (((uint8_t *)&F.l38) + 8));
                                                uint64_t t387 = GM_LD(uint64_t, ((((uint8_t *)&F.l38) + 8) + 8));
                                                uint64_t t388 = gm_l41;
                                                void * t389 = F.l0;
                                                void * t390 = F.l0;
                                                void * t391 = gm_l42;
                                                gm_t88rcucg1b7uk t392 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t391));
                                                void * t393 = F.l1;
                                                gm_b16_8 t394 = GM_LD(gm_b16_8, ((uint8_t *)t393));
                                                gm_t88rcucg1b7uk t395 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t390, t392, t394);
                                                if (gm_unw.kind) {
                                                    goto gm_ll375;
                                                }
                                                gm_b16_8 t396 = gm_f_handlers_of_type_5gareb4((void *)NULL, cx, NULL, t389, t395);
                                                if (gm_unw.kind) {
                                                    goto gm_ll375;
                                                }
                                                void * t397 = gm_l42;
                                                gm_tpe1hv3fekn05 t398;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t398 + 0), t396);
                                                GM_ST(void *, ((uint8_t *)&t398 + 16), t397);
                                                gm_t4lnf71mruqpt t399;
                                                GM_ST(uint8_t, (uint8_t *)&t399 + 1, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t399 + 2, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t399 + 4, 0);
                                                GM_ST(uint8_t, ((uint8_t *)&t399 + 0), ((uint8_t)1ULL));
                                                GM_ST(gm_tpe1hv3fekn05, ((uint8_t *)&t399 + 8), t398);
                                                GM_ST(gm_t4lnf71mruqpt, (((uint8_t *)t386) + (uintptr_t)t388 * 32), t399);
                                            }
                                            goto gm_lb46;
                                        }
gm_lb47:;
                                    }
gm_lb46:;
                                    goto gm_lb44;
                                }
gm_lb45:;
                                gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 208, 9);
                            }
gm_lb44:;
                        }
gm_lb43:;
                        uint64_t t400 = gm_l40;
                        uint64_t r401; bool o402 = __builtin_add_overflow(t400, ((uint64_t)1ULL), &r401);
                        uint64_t t403 = r401;
                        gm_l40 = t403;
                    }
                }
                goto gm_ls404;
gm_ll375:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb41; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb43; }
                }
                goto gm_lad;
gm_ls404:;
gm_lb41:;
                goto gm_lb39;
            }
gm_lb40:;
        }
gm_lb39:;
    }
    {
        {
            {
                void * t405 = gm_l2;
                uint8_t t406 = GM_LD(uint8_t, ((((uint8_t *)t405) + 8) + 0));
                uint8_t t407 = (uint8_t)(t406 == ((uint8_t)56ULL));
                if (!t407) {
                    goto gm_lb49;
                }
                void * t408 = GM_LD(void *, ((((uint8_t *)t405) + 8) + 8));
                gm_l43 = t408;
                void * t409 = gm_l43;
                void * t410 = GM_LD(void *, (((uint8_t *)t409) + 40));
                uint8_t t411 = GM_LD(uint8_t, ((((uint8_t *)t410) + 8) + 0));
                uint8_t t412 = (uint8_t)(t411 == ((uint8_t)9ULL));
                if (!t412) {
                    goto gm_lb49;
                }
                uint32_t t413 = GM_LD(uint32_t, ((((uint8_t *)t410) + 8) + 8));
                gm_l44 = t413;
                void * t414 = F.l1;
                gm_b16_8 t415 = GM_LD(gm_b16_8, (((uint8_t *)t414) + 120));
                gm_l45 = t415;
                gm_l46 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t417 = gm_l46;
                        uint64_t t418 = GM_LD(uint64_t, (((uint8_t *)&gm_l45) + 8));
                        uint8_t t419 = (t417 < t418);
                        if (!t419) {
                            goto gm_lb50;
                        }
                        {
                            void * t420 = GM_LD(void *, ((uint8_t *)&gm_l45));
                            uint64_t t421 = GM_LD(uint64_t, (((uint8_t *)&gm_l45) + 8));
                            uint64_t t422 = gm_l46;
                            {
                                {
                                    gm_tl874t2atnlh3 t423 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t420) + (uintptr_t)t422 * 12));
                                    gm_l47 = t423;
                                    uint32_t t424 = GM_LD(uint32_t, ((uint8_t *)&gm_l47));
                                    uint32_t t425 = gm_l44;
                                    uint8_t t426 = ((t424 == t425));
                                    if (t426) {
                                        void * t427 = gm_l43;
                                        void * t428 = F.l0;
                                        void * t429 = (void *)(((uint8_t *)t428) + 8);
                                        gm_t53at80pfdus3 t430;
                                        memset(&t430, 0, sizeof t430);
                                        GM_ST(uint8_t, ((uint8_t *)&t430 + 0), ((uint8_t)7ULL));
                                        gm_t88rcucg1b7uk t431 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t429, &t430);
                                        if (gm_unw.kind) {
                                            goto gm_ll416;
                                        }
                                        uint32_t t432 = GM_LD(uint32_t, (((uint8_t *)&gm_l47) + 4));
                                        void * t433 = gm_l43;
                                        void * t434 = GM_LD(void *, (((uint8_t *)t433) + 40));
                                        gm_tl874t2atnlh3 t435 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t434) + 72));
                                        gm_to0f5qn75cgg8 t436;
                                        memset(&t436, 0, sizeof t436);
                                        GM_ST(uint8_t, ((uint8_t *)&t436 + 0), ((uint8_t)15ULL));
                                        GM_ST(uint32_t, ((uint8_t *)&t436 + 8), t432);
                                        void * t437 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t431, &t436, t435);
                                        if (gm_unw.kind) {
                                            goto gm_ll416;
                                        }
                                        GM_ST(void *, (((uint8_t *)t427) + 40), t437);
                                    }
                                    goto gm_lb53;
                                }
gm_lb54:;
                                gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 216, 9);
                            }
gm_lb53:;
                        }
gm_lb52:;
                        uint64_t t438 = gm_l46;
                        uint64_t r439; bool o440 = __builtin_add_overflow(t438, ((uint64_t)1ULL), &r439);
                        uint64_t t441 = r439;
                        gm_l46 = t441;
                    }
                }
                goto gm_ls442;
gm_ll416:;
                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb50; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb52; }
                }
                goto gm_lad;
gm_ls442:;
gm_lb50:;
                goto gm_lb48;
            }
gm_lb49:;
        }
gm_lb48:;
        void * t443 = gm_l2;
        {
            {
                uint8_t t444 = GM_LD(uint8_t, ((((uint8_t *)t443) + 8) + 0));
                uint8_t t445 = (uint8_t)(t444 == ((uint8_t)34ULL));
                if (!t445) {
                    goto gm_lb56;
                }
                uint8_t t446 = GM_LD(uint8_t, (((((uint8_t *)t443) + 8) + 8) + 0));
                uint8_t t447 = (uint8_t)(t446 == ((uint8_t)0ULL));
                if (!t447) {
                    goto gm_lb56;
                }
                uint32_t t448 = GM_LD(uint32_t, (((((uint8_t *)t443) + 8) + 8) + 8));
                gm_l48 = t448;
                gm_b16_8 t449 = GM_LD(gm_b16_8, (((((uint8_t *)t443) + 8) + 8) + 40));
                gm_l49 = t449;
                {
                    void * t450 = gm_l2;
                    void * t451 = F.l0;
                    void * t452 = F.l1;
                    uint32_t t453 = gm_l48;
                    gm_b16_8 t454 = gm_l49;
                    gm_f_forward_call_qat9d2e((void *)&gm_l92, (void *)NULL, cx, NULL, t451, t452, t453, t454);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t455 = (uint8_t)((((uint8_t *)&gm_l92))[64] != 0);
                    if (t455) {
                        memmove(((uint8_t *)&gm_l91), ((uint8_t *)&gm_l92), 64);
                    } else {
                        void * t456 = F.l0;
                        void * t457 = F.l1;
                        uint32_t t458 = gm_l48;
                        uint32_t t459 = gm_f_mono_fn_aj1vgri((void *)NULL, cx, NULL, t456, t457, t458);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t460 = gm_l49;
                        gm_tsth9qu1dv0g3 t461;
                        memset(&t461, 0, sizeof t461);
                        GM_ST(uint8_t, ((uint8_t *)&t461 + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&t461 + 8), t459);
                        gm_to76jli3bqddq t462;
                        GM_ST(gm_tsth9qu1dv0g3, ((uint8_t *)&t462 + 0), t461);
                        GM_ST(gm_b16_8, ((uint8_t *)&t462 + 40), t460);
                        GM_ST(uint8_t, (uint8_t *)&gm_l91 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&gm_l91 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&gm_l91 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&gm_l91 + 0), ((uint8_t)34ULL));
                        GM_ST(gm_to76jli3bqddq, ((uint8_t *)&gm_l91 + 8), t462);
                    }
                    memmove((((uint8_t *)t450) + 8), ((uint8_t *)&gm_l91), 64);
                }
                goto gm_lb55;
            }
gm_lb56:;
            {
                uint8_t t463 = GM_LD(uint8_t, ((((uint8_t *)t443) + 8) + 0));
                uint8_t t464 = (uint8_t)(t463 == ((uint8_t)34ULL));
                if (!t464) {
                    goto gm_lb57;
                }
                uint8_t t465 = GM_LD(uint8_t, (((((uint8_t *)t443) + 8) + 8) + 0));
                uint8_t t466 = (uint8_t)(t465 == ((uint8_t)1ULL));
                if (!t466) {
                    goto gm_lb57;
                }
                {
                    memmove(((uint8_t *)&gm_l50), (((((uint8_t *)t443) + 8) + 8) + 8), 24);
                }
                uint32_t t467 = GM_LD(uint32_t, ((((((uint8_t *)t443) + 8) + 8) + 8) + 24));
                gm_l51 = t467;
                gm_b16_8 t468 = GM_LD(gm_b16_8, (((((uint8_t *)t443) + 8) + 8) + 40));
                gm_l52 = t468;
                {
                    void * t469 = gm_l2;
                    void * t470 = F.l0;
                    void * t471 = (void *)((uint8_t *)&gm_l50);
                    void * t472 = F.l1;
                    gm_b16_8 t473 = GM_LD(gm_b16_8, ((uint8_t *)t472));
                    gm_tiilmfd16v3fc t474;
                    gm_f_subst_eref_kf9o1q5((void *)&t474, (void *)NULL, cx, NULL, t470, t471, t473);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint32_t t475 = gm_l51;
                    gm_t419mo45g7j0a t476;
                    GM_ST(uint32_t, (uint8_t *)&t476 + 28, 0);
                    GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t476 + 0), t474);
                    GM_ST(uint32_t, ((uint8_t *)&t476 + 24), t475);
                    gm_b16_8 t477 = gm_l52;
                    gm_tsth9qu1dv0g3 t478;
                    GM_ST(uint8_t, (uint8_t *)&t478 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t478 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t478 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t478 + 0), ((uint8_t)1ULL));
                    GM_ST(gm_t419mo45g7j0a, ((uint8_t *)&t478 + 8), t476);
                    gm_to76jli3bqddq t479;
                    GM_ST(gm_tsth9qu1dv0g3, ((uint8_t *)&t479 + 0), t478);
                    GM_ST(gm_b16_8, ((uint8_t *)&t479 + 40), t477);
                    gm_to0f5qn75cgg8 t480;
                    GM_ST(uint8_t, (uint8_t *)&t480 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t480 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t480 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t480 + 0), ((uint8_t)34ULL));
                    GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t480 + 8), t479);
                    GM_ST(gm_to0f5qn75cgg8, (((uint8_t *)t469) + 8), t480);
                }
                goto gm_lb55;
            }
gm_lb57:;
            {
                uint8_t t481 = GM_LD(uint8_t, ((((uint8_t *)t443) + 8) + 0));
                uint8_t t482 = (uint8_t)(t481 == ((uint8_t)26ULL));
                if (!t482) {
                    goto gm_lb58;
                }
                uint32_t t483 = GM_LD(uint32_t, ((((uint8_t *)t443) + 8) + 8));
                gm_l53 = t483;
                void * t484 = gm_l2;
                void * t485 = F.l0;
                void * t486 = F.l1;
                uint32_t t487 = gm_l53;
                uint32_t t488 = gm_f_mono_fn_aj1vgri((void *)NULL, cx, NULL, t485, t486, t487);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_to0f5qn75cgg8 t489;
                memset(&t489, 0, sizeof t489);
                GM_ST(uint8_t, ((uint8_t *)&t489 + 0), ((uint8_t)26ULL));
                GM_ST(uint32_t, ((uint8_t *)&t489 + 8), t488);
                GM_ST(gm_to0f5qn75cgg8, (((uint8_t *)t484) + 8), t489);
                goto gm_lb55;
            }
gm_lb58:;
            {
                uint8_t t490 = GM_LD(uint8_t, ((((uint8_t *)t443) + 8) + 0));
                uint8_t t491 = (uint8_t)(t490 == ((uint8_t)27ULL));
                if (!t491) {
                    goto gm_lb59;
                }
                uint32_t t492 = GM_LD(uint32_t, ((((uint8_t *)t443) + 8) + 8));
                gm_l54 = t492;
                void * t493 = gm_l2;
                void * t494 = F.l0;
                void * t495 = F.l1;
                uint32_t t496 = gm_l54;
                uint32_t t497 = gm_f_mono_fn_aj1vgri((void *)NULL, cx, NULL, t494, t495, t496);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_to0f5qn75cgg8 t498;
                memset(&t498, 0, sizeof t498);
                GM_ST(uint8_t, ((uint8_t *)&t498 + 0), ((uint8_t)27ULL));
                GM_ST(uint32_t, ((uint8_t *)&t498 + 8), t497);
                GM_ST(gm_to0f5qn75cgg8, (((uint8_t *)t493) + 8), t498);
                goto gm_lb55;
            }
gm_lb59:;
            {
                uint8_t t499 = GM_LD(uint8_t, ((((uint8_t *)t443) + 8) + 0));
                uint8_t t500 = (uint8_t)(t499 == ((uint8_t)28ULL));
                if (!t500) {
                    goto gm_lb60;
                }
                uint32_t t501 = GM_LD(uint32_t, ((((uint8_t *)t443) + 8) + 8));
                gm_l55 = t501;
                void * t502 = gm_l2;
                void * t503 = F.l0;
                void * t504 = F.l1;
                uint32_t t505 = gm_l55;
                uint32_t t506 = gm_f_mono_handler_38b78qh((void *)NULL, cx, NULL, t503, t504, t505);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_to0f5qn75cgg8 t507;
                memset(&t507, 0, sizeof t507);
                GM_ST(uint8_t, ((uint8_t *)&t507 + 0), ((uint8_t)28ULL));
                GM_ST(uint32_t, ((uint8_t *)&t507 + 8), t506);
                GM_ST(gm_to0f5qn75cgg8, (((uint8_t *)t502) + 8), t507);
                goto gm_lb55;
            }
gm_lb60:;
            {
                uint8_t t508 = GM_LD(uint8_t, ((((uint8_t *)t443) + 8) + 0));
                uint8_t t509 = (uint8_t)(t508 == ((uint8_t)55ULL));
                if (!t509) {
                    goto gm_lb61;
                }
                void * t510 = GM_LD(void *, ((((uint8_t *)t443) + 8) + 8));
                gm_l56 = t510;
                {
                    {
                        {
                            void * t511 = gm_l56;
                            uint8_t t512 = GM_LD(uint8_t, (((uint8_t *)t511) + 0));
                            uint8_t t513 = (uint8_t)(t512 == ((uint8_t)0ULL));
                            if (!t513) {
                                goto gm_lb63;
                            }
                            memmove(((uint8_t *)&gm_l57), (((uint8_t *)t511) + 8), 24);
                            uint32_t t514 = GM_LD(uint32_t, ((((uint8_t *)t511) + 8) + 24));
                            gm_l58 = t514;
                        }
                        {
                            void * t515 = gm_l56;
                            void * t516 = F.l0;
                            void * t517 = (void *)((uint8_t *)&gm_l57);
                            void * t518 = F.l1;
                            gm_b16_8 t519 = GM_LD(gm_b16_8, ((uint8_t *)t518));
                            gm_tiilmfd16v3fc t520;
                            gm_f_subst_eref_kf9o1q5((void *)&t520, (void *)NULL, cx, NULL, t516, t517, t519);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint32_t t521 = gm_l58;
                            gm_t419mo45g7j0a t522;
                            GM_ST(uint32_t, (uint8_t *)&t522 + 28, 0);
                            GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t522 + 0), t520);
                            GM_ST(uint32_t, ((uint8_t *)&t522 + 24), t521);
                            gm_th1g69d7vpipg t523;
                            GM_ST(uint8_t, (uint8_t *)&t523 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t523 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t523 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t523 + 0), ((uint8_t)0ULL));
                            GM_ST(gm_t419mo45g7j0a, ((uint8_t *)&t523 + 8), t522);
                            GM_ST(gm_th1g69d7vpipg, ((uint8_t *)t515), t523);
                        }
                        goto gm_lb62;
                    }
gm_lb63:;
                }
gm_lb62:;
                goto gm_lb55;
            }
gm_lb61:;
            {
                goto gm_lb55;
            }
gm_lb64:;
            gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 220, 5);
        }
gm_lb55:;
    }
    {
        void * t524 = gm_l2;
        gm_closure t525 = ((gm_closure){(void *)gm_f_fn_eg2r5au, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t524, t525);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t526 = gm_l2;
        {
            {
                uint8_t t527 = GM_LD(uint8_t, ((((uint8_t *)t526) + 8) + 0));
                uint8_t t528 = (uint8_t)(t527 == ((uint8_t)44ULL));
                if (!t528) {
                    goto gm_lb66;
                }
                gm_b16_8 t529 = GM_LD(gm_b16_8, (((((uint8_t *)t526) + 8) + 8) + 8));
                gm_l59 = t529;
                gm_b16_8 t530 = gm_l59;
                gm_l60 = t530;
                gm_l61 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t532 = gm_l61;
                        uint64_t t533 = GM_LD(uint64_t, (((uint8_t *)&gm_l60) + 8));
                        uint8_t t534 = (t532 < t533);
                        if (!t534) {
                            goto gm_lb67;
                        }
                        {
                            void * t535 = GM_LD(void *, ((uint8_t *)&gm_l60));
                            uint64_t t536 = GM_LD(uint64_t, (((uint8_t *)&gm_l60) + 8));
                            uint64_t t537 = gm_l61;
                            {
                                {
                                    memmove(((uint8_t *)&gm_l62), (((uint8_t *)t535) + (uintptr_t)t537 * 24), 24);
                                    void * t538 = F.l0;
                                    void * t539 = F.l1;
                                    void * t540 = GM_LD(void *, ((uint8_t *)&gm_l62));
                                    gm_b0_1 t541 = gm_f_mono_pat_lipeb0h((void *)NULL, cx, NULL, t538, t539, t540);
                                    if (gm_unw.kind) {
                                        goto gm_ll531;
                                    }
                                    goto gm_lb70;
                                }
gm_lb71:;
                                gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 231, 28);
                            }
gm_lb70:;
                        }
gm_lb69:;
                        uint64_t t542 = gm_l61;
                        uint64_t r543; bool o544 = __builtin_add_overflow(t542, ((uint64_t)1ULL), &r543);
                        uint64_t t545 = r543;
                        gm_l61 = t545;
                    }
                }
                goto gm_ls546;
gm_ll531:;
                if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb67; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb69; }
                }
                goto gm_lad;
gm_ls546:;
gm_lb67:;
                goto gm_lb65;
            }
gm_lb66:;
            {
                uint8_t t547 = GM_LD(uint8_t, ((((uint8_t *)t526) + 8) + 0));
                uint8_t t548 = (uint8_t)(t547 == ((uint8_t)47ULL));
                if (!t548) {
                    goto gm_lb72;
                }
                memmove(((uint8_t *)&gm_l63), ((((uint8_t *)t526) + 8) + 8), 32);
                void * t549 = F.l0;
                void * t550 = F.l1;
                gm_b16_8 t551 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l63));
                gm_b0_1 t552 = gm_f_mono_cond_pats_armprh7((void *)NULL, cx, NULL, t549, t550, t551);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_lb65;
            }
gm_lb72:;
            {
                uint8_t t553 = GM_LD(uint8_t, ((((uint8_t *)t526) + 8) + 0));
                uint8_t t554 = (uint8_t)(t553 == ((uint8_t)48ULL));
                if (!t554) {
                    goto gm_lb73;
                }
                memmove(((uint8_t *)&F.l64), ((((uint8_t *)t526) + 8) + 8), 40);
                void * t555 = F.l0;
                void * t556 = F.l1;
                gm_b16_8 t557 = GM_LD(gm_b16_8, (((uint8_t *)&F.l64) + 8));
                gm_b0_1 t558 = gm_f_mono_cond_pats_armprh7((void *)NULL, cx, NULL, t555, t556, t557);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_lb65;
            }
gm_lb73:;
            {
                goto gm_lb65;
            }
gm_lb74:;
            gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 230, 5);
        }
gm_lb65:;
        {
            {
                void * t559 = gm_l2;
                uint8_t t560 = GM_LD(uint8_t, ((((uint8_t *)t559) + 8) + 0));
                uint8_t t561 = (uint8_t)(t560 == ((uint8_t)55ULL));
                if (!t561) {
                    goto gm_lb76;
                }
                void * t562 = GM_LD(void *, ((((uint8_t *)t559) + 8) + 8));
                gm_l65 = t562;
                void * t563 = F.l1;
                uint64_t t564 = GM_LD(uint64_t, (((uint8_t *)t563) + 8));
                uint8_t t565 = (t564 > ((uint64_t)0ULL));
                if (!t565) {
                    goto gm_lb76;
                }
                gm_l67 = ((uint64_t)0ULL);
                void * t566 = gm_l65;
                uint64_t t567 = GM_LD(uint64_t, ((((uint8_t *)t566) + 56) + 8));
                void * t568 = gm_l65;
                uint64_t t569 = GM_LD(uint64_t, ((((uint8_t *)t568) + 72) + 8));
                uint64_t t570 = (t567 < t569 ? t567 : t569);
                gm_l66 = t570;
                {
                    for (;;) {
                        uint64_t t572 = gm_l67;
                        uint64_t t573 = gm_l66;
                        uint8_t t574 = (t572 < t573);
                        if (!t574) {
                            goto gm_lb77;
                        }
                        {
                            {
                                {
                                    uint64_t t575 = gm_l67;
                                    gm_l68 = t575;
                                    {
                                        {
                                            {
                                                void * t576 = F.l0;
                                                void * t577 = gm_l65;
                                                void * t578 = GM_LD(void *, (((uint8_t *)t577) + 56));
                                                uint64_t t579 = GM_LD(uint64_t, ((((uint8_t *)t577) + 56) + 8));
                                                uint64_t t580 = gm_l68;
                                                if ((uintptr_t)t580 >= (uintptr_t)(t579)) gm_trap_index((uintptr_t)t580, t579, gm_src5eu5l4a7gv6s, 240, 38);
                                                void * t581 = (void *)(((uint8_t *)t578) + (uintptr_t)t580 * 24);
                                                void * t582 = gm_l65;
                                                void * t583 = GM_LD(void *, (((uint8_t *)t582) + 72));
                                                uint64_t t584 = GM_LD(uint64_t, ((((uint8_t *)t582) + 72) + 8));
                                                uint64_t t585 = gm_l68;
                                                if ((uintptr_t)t585 >= (uintptr_t)(t584)) gm_trap_index((uintptr_t)t585, t584, gm_src5eu5l4a7gv6s, 240, 50);
                                                gm_t88rcucg1b7uk t586 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t583) + (uintptr_t)t585 * 88));
                                                gm_f_check_spec_4vp7jhg((void *)&gm_l93, (void *)NULL, cx, NULL, t576, t581, t586);
                                                if (gm_unw.kind) {
                                                    goto gm_ll571;
                                                }
                                            }
                                            uint8_t t587 = (uint8_t)((((uint8_t *)&gm_l93))[16] != 0);
                                            if (!t587) {
                                                goto gm_lb83;
                                            }
                                            gm_b16_8 t588 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l93));
                                            gm_l69 = t588;
                                            {
                                                void * t589 = F.l0;
                                                gm_tl874t2atnlh3 t590 = gm_l5;
                                                gm_b16_8 t591 = gm_l69;
                                                gm_tnpifv20irnph t592;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t592 + 0), t591);
                                                gm_f_format_mlqiori((void *)&gm_l94, (void *)NULL, cx, NULL, t592);
                                                if (gm_unw.kind) {
                                                    goto gm_ll571;
                                                }
                                                uint8_t t593 = (uint8_t)!((((uint8_t *)&gm_l94))[0] != 0);
                                                if (!t593) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l94) + 8, gm_src5eu5l4a7gv6s, 241, 31);
                                                gm_b16_8 t594 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l94) + 8));
                                                gm_b16_8 t595 = GM_LD(gm_b16_8, ((uint8_t *)&t594));
                                                gm_b0_1 t596 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t589, t590, t595);
                                                if (gm_unw.kind) {
                                                    goto gm_ll571;
                                                }
                                            }
                                            goto gm_lb82;
                                        }
gm_lb83:;
                                    }
gm_lb82:;
                                    goto gm_lb80;
                                }
gm_lb81:;
                                gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 239, 9);
                            }
gm_lb80:;
                        }
gm_lb79:;
                        uint64_t t597 = gm_l67;
                        uint64_t r598; bool o599 = __builtin_add_overflow(t597, ((uint64_t)1ULL), &r598);
                        uint64_t t600 = r598;
                        gm_l67 = t600;
                    }
                }
                goto gm_ls601;
gm_ll571:;
                if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb77; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb79; }
                }
                goto gm_lad;
gm_ls601:;
gm_lb77:;
                goto gm_lb75;
            }
gm_lb76:;
        }
gm_lb75:;
    }
    {
        void * t602 = gm_l2;
        {
            {
                uint8_t t603 = GM_LD(uint8_t, ((((uint8_t *)t602) + 8) + 0));
                uint8_t t604 = (uint8_t)(t603 == ((uint8_t)56ULL));
                if (!t604) {
                    goto gm_lb85;
                }
                gm_l95 = ((uint8_t)1);
                goto gm_lb84;
            }
gm_lb85:;
            {
                gm_l95 = ((uint8_t)0);
                goto gm_lb84;
            }
gm_lb86:;
            gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 245, 8);
        }
gm_lb84:;
        uint8_t t605 = gm_l95;
        if (t605) {
            void * t606 = F.l0;
            void * t607 = F.l1;
            void * t608 = gm_l2;
            gm_b0_1 t609 = gm_f_resolve_write_e97fjom((void *)NULL, cx, NULL, t606, t607, t608);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
        {
            {
                void * t610 = gm_l2;
                uint8_t t611 = GM_LD(uint8_t, ((((uint8_t *)t610) + 8) + 0));
                uint8_t t612 = (uint8_t)(t611 == ((uint8_t)33ULL));
                if (!t612) {
                    goto gm_lb88;
                }
                uint8_t t613 = GM_LD(uint8_t, (((((uint8_t *)t610) + 8) + 8) + 0));
                uint8_t t614 = (uint8_t)(t613 == ((uint8_t)81ULL));
                if (!t614) {
                    goto gm_lb88;
                }
                uint32_t t615 = GM_LD(uint32_t, (((((uint8_t *)t610) + 8) + 8) + 4));
                gm_l70 = t615;
                gm_b16_8 t616 = GM_LD(gm_b16_8, (((((uint8_t *)t610) + 8) + 8) + 16));
                gm_l71 = t616;
                {
                    void * t617 = F.l0;
                    void * t618 = GM_LD(void *, ((uint8_t *)&gm_l71));
                    uint64_t t619 = GM_LD(uint64_t, (((uint8_t *)&gm_l71) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t619)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t619, gm_src5eu5l4a7gv6s, 251, 33);
                    void * t620 = (void *)(((uint8_t *)t618) + (uintptr_t)((uint64_t)0ULL) * 88);
                    void * t621 = F.l0;
                    void * t622 = GM_LD(void *, (((uint8_t *)t621) + 2352));
                    uint64_t t623 = GM_LD(uint64_t, ((((uint8_t *)t621) + 2352) + 8));
                    uint32_t t624 = gm_l70;
                    uint64_t t625 = (uint64_t)t624;
                    if ((uintptr_t)t625 >= (uintptr_t)(t623)) gm_trap_index((uintptr_t)t625, t623, gm_src5eu5l4a7gv6s, 251, 42);
                    void * t626 = gm_l2;
                    gm_tl874t2atnlh3 t627 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t626) + 72));
                    void * t628 = F.l1;
                    gm_tl874t2atnlh3 t629 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t628) + 96));
                    gm_toceffb697p0u t630;
                    GM_ST(uint8_t, (uint8_t *)&t630 + 13, 0);
                    GM_ST(uint16_t, (uint8_t *)&t630 + 14, 0);
                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t630), t629);
                    ((uint8_t *)&t630)[12] = 1;
                    gm_b0_1 t631 = gm_f_check_static_assert_s4f7i6i((void *)NULL, cx, NULL, t617, t620, &(*(gm_th2lqbo77pvsc *)(((uint8_t *)t622) + (uintptr_t)t625 * 24)), t627, t630);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t632 = gm_l2;
                    void * t633 = F.l0;
                    void * t634 = gm_l2;
                    gm_tl874t2atnlh3 t635 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t634) + 72));
                    void * t636 = gm_f_unit_ohk62mc((void *)NULL, cx, NULL, t633, t635);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    memmove(((uint8_t *)t632), ((uint8_t *)t636), 88);
                }
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
gm_lb88:;
        }
gm_lb87:;
        void * t637 = gm_l2;
        {
            {
                uint8_t t638 = GM_LD(uint8_t, ((((uint8_t *)t637) + 8) + 0));
                uint8_t t639 = (uint8_t)(t638 == ((uint8_t)58ULL));
                if (!t639) {
                    goto gm_lb90;
                }
                gm_l96 = ((uint8_t)1);
                goto gm_lb89;
            }
gm_lb90:;
            {
                gm_l96 = ((uint8_t)0);
                goto gm_lb89;
            }
gm_lb91:;
            gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 255, 8);
        }
gm_lb89:;
    }
    {
        uint8_t t640 = gm_l96;
        if (t640) {
            void * t641 = gm_l2;
            void * t642 = F.l0;
            void * t643 = F.l1;
            void * t644 = GM_LD(void *, (((uint8_t *)t643) + 112));
            void * t645 = gm_l2;
            void * t646 = gm_f_soa_expand_2e8rmqf((void *)NULL, cx, NULL, t642, t644, t645);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            memmove(((uint8_t *)t641), ((uint8_t *)t646), 88);
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
        {
            {
                void * t647 = gm_l2;
                uint8_t t648 = GM_LD(uint8_t, ((((uint8_t *)t647) + 8) + 0));
                uint8_t t649 = (uint8_t)(t648 == ((uint8_t)33ULL));
                if (!t649) {
                    goto gm_lb93;
                }
                uint8_t t650 = GM_LD(uint8_t, (((((uint8_t *)t647) + 8) + 8) + 0));
                uint8_t t651 = (uint8_t)(t650 == ((uint8_t)7ULL));
                if (!t651) {
                    goto gm_lb93;
                }
                gm_b16_8 t652 = GM_LD(gm_b16_8, (((((uint8_t *)t647) + 8) + 8) + 16));
                gm_l72 = t652;
                void * t653 = F.l1;
                uint64_t t654 = GM_LD(uint64_t, (((uint8_t *)t653) + 8));
                uint8_t t655 = (t654 > ((uint64_t)0ULL));
                if (!t655) {
                    goto gm_lb93;
                }
                void * t656 = F.l0;
                void * t657 = (void *)(((uint8_t *)t656) + 8);
                gm_t88rcucg1b7uk t658 = gm_l4;
                uint8_t t659 = gm_f_has_param_u9t0efs(t657, GM_CONV(gm_csguankko4lmhd, t658));
                uint8_t t660 = (uint8_t)!t659;
                if (!t660) {
                    goto gm_lb93;
                }
                void * t661 = GM_LD(void *, ((uint8_t *)&gm_l72));
                uint64_t t662 = GM_LD(uint64_t, (((uint8_t *)&gm_l72) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t662)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t662, gm_src5eu5l4a7gv6s, 261, 16);
                gm_t88rcucg1b7uk t663 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t661) + (uintptr_t)((uint64_t)0ULL) * 88));
                gm_l73 = t663;
                void * t664 = F.l0;
                void * t665 = (void *)(((uint8_t *)t664) + 8);
                gm_t88rcucg1b7uk t666 = gm_l73;
                uint8_t t667 = gm_f_has_param_u9t0efs(t665, GM_CONV(gm_csguankko4lmhd, t666));
                uint8_t t668 = (uint8_t)!t667;
                if (t668) {
                    {
                        void * t669 = F.l0;
                        gm_t88rcucg1b7uk t670 = gm_l73;
                        gm_f_layout_a5ovnrm((void *)&gm_l97, (void *)NULL, cx, NULL, t669, t670);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint64_t t671 = GM_LD(uint64_t, ((uint8_t *)&gm_l97));
                    gm_l74 = t671;
                    {
                        void * t672 = F.l0;
                        gm_t88rcucg1b7uk t673 = gm_l4;
                        gm_f_layout_a5ovnrm((void *)&gm_l98, (void *)NULL, cx, NULL, t672, t673);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint64_t t674 = GM_LD(uint64_t, ((uint8_t *)&gm_l98));
                    gm_l75 = t674;
                    uint64_t t675 = gm_l74;
                    uint64_t t676 = gm_l75;
                    uint8_t t677 = (!(t675 == t676));
                    if (t677) {
                        void * t678 = F.l0;
                        gm_tl874t2atnlh3 t679 = gm_l5;
                        void * t680 = F.l0;
                        gm_t88rcucg1b7uk t681 = gm_l73;
                        gm_b16_8 t682 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t680, t681);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint64_t t683 = gm_l74;
                        void * t684 = F.l0;
                        gm_t88rcucg1b7uk t685 = gm_l4;
                        gm_b16_8 t686 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t684, t685);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint64_t t687 = gm_l75;
                        gm_thddbqb6snabm t688;
                        GM_ST(gm_b16_8, ((uint8_t *)&t688 + 0), t682);
                        GM_ST(uint64_t, ((uint8_t *)&t688 + 16), t683);
                        GM_ST(gm_b16_8, ((uint8_t *)&t688 + 24), t686);
                        GM_ST(uint64_t, ((uint8_t *)&t688 + 40), t687);
                        gm_f_format_le00do2((void *)&gm_l99, (void *)NULL, cx, NULL, &t688);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t689 = (uint8_t)!((((uint8_t *)&gm_l99))[0] != 0);
                        if (!t689) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l99) + 8, gm_src5eu5l4a7gv6s, 266, 31);
                        gm_b16_8 t690 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l99) + 8));
                        gm_b16_8 t691 = GM_LD(gm_b16_8, ((uint8_t *)&t690));
                        gm_b0_1 t692 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t678, t679, t691);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                }
                goto gm_lb92;
            }
gm_lb93:;
        }
gm_lb92:;
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
__attribute__((aligned(4))) uint32_t gm_f_mono_fn_aj1vgri(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2) {
    struct gm_fr_mono_fn_aj1vgri F;
    void * gm_l0; /* s */
    void * gm_l1; /* mc */
    uint32_t gm_l2; /* id */
    uint32_t gm_l3; /* n */
    gm_tiilmfd16v3fc gm_l4; /* inst */
    uint8_t gm_l5; /* has_param */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t88rcucg1b7uk gm_l8; /* a */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_t88rcucg1b7uk gm_l12; /* a */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint32_t gm_l20; /* new_id */
    gm_b16_8 gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    gm_t9805qtonbsag gm_l26; /* parent */
    uint32_t gm_l27; /* p */
    void * gm_l28; /* body */
    void * gm_l29; /* b */
    void * gm_l30; /* copy */
    uint8_t gm_l31; /* was */
    void * gm_l32; /* outer_locals */
    void * gm_l33; /* nd */
    GM_STACK_CHECK(&F, 1611u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t9805qtonbsag gm_l34;
    gm_tftb1odc739t5 gm_l35;
    gm_tftb1odc739t5 gm_l36;
    gm_tftb1odc739t5 gm_l37;
    gm_tftb1odc739t5 gm_l38;
    gm_tftb1odc739t5 gm_l39;
    {
        {
            {
                void * t30 = gm_l1;
                void * t31 = (void *)(((uint8_t *)t30) + 16);
                uint32_t t32 = gm_l2;
                gm_t9805qtonbsag t33 = gm_f_find_pair_ddtpbq9(t31, t32);
                gm_l34 = t33;
                uint8_t t34 = (uint8_t)((((uint8_t *)&gm_l34))[4] != 0);
                if (!t34) {
                    goto gm_lb2;
                }
                uint32_t t35 = GM_LD(uint32_t, ((uint8_t *)&gm_l34));
                gm_l3 = t35;
                uint32_t t36 = gm_l3;
                F.ret = t36;
                goto gm_fret;
            }
gm_lb2:;
        }
gm_lb1:;
        {
            {
                {
                    void * t37 = gm_l0;
                    void * t38 = GM_LD(void *, (((uint8_t *)t37) + 344));
                    uint64_t t39 = GM_LD(uint64_t, ((((uint8_t *)t37) + 344) + 8));
                    uint32_t t40 = gm_l2;
                    uint64_t t41 = (uint64_t)t40;
                    if ((uintptr_t)t41 >= (uintptr_t)(t39)) gm_trap_index((uintptr_t)t41, t39, gm_src5eu5l4a7gv6s, 306, 16);
                    uint8_t t42 = (uint8_t)((((((uint8_t *)t38) + (uintptr_t)t41 * 136) + 96))[24] != 0);
                    if (!t42) {
                        goto gm_lb4;
                    }
                    memmove(((uint8_t *)&gm_l4), ((((uint8_t *)t38) + (uintptr_t)t41 * 136) + 96), 24);
                }
                gm_l5 = ((uint8_t)0);
                gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
                gm_l6 = t43;
                gm_l7 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t44 = gm_l7;
                        uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                        uint8_t t46 = (t44 < t45);
                        if (!t46) {
                            goto gm_lb5;
                        }
                        {
                            void * t47 = GM_LD(void *, ((uint8_t *)&gm_l6));
                            uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            uint64_t t49 = gm_l7;
                            {
                                {
                                    gm_t88rcucg1b7uk t50 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t47) + (uintptr_t)t49 * 4));
                                    gm_l8 = t50;
                                    void * t51 = gm_l0;
                                    void * t52 = (void *)(((uint8_t *)t51) + 8);
                                    gm_t88rcucg1b7uk t53 = gm_l8;
                                    uint8_t t54 = gm_f_has_param_u9t0efs(t52, GM_CONV(gm_csguankko4lmhd, t53));
                                    if (t54) {
                                        gm_l5 = ((uint8_t)1);
                                    }
                                    goto gm_lb8;
                                }
gm_lb9:;
                                gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 308, 9);
                            }
gm_lb8:;
                        }
gm_lb7:;
                        uint64_t t55 = gm_l7;
                        uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                        uint64_t t58 = r56;
                        gm_l7 = t58;
                    }
                }
gm_lb5:;
                uint8_t t59 = gm_l5;
                uint8_t t60 = (uint8_t)!t59;
                if (t60) {
                    uint32_t t61 = gm_l2;
                    F.ret = t61;
                    goto gm_fret;
                }
                {
                    gm_trqfvmjtt4sij t62;
                    gm_f_list_nof7vfj((void *)&t62, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l9 = t62;
                }
                gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
                gm_l10 = t63;
                gm_l11 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t64 = gm_l11;
                        uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                        uint8_t t66 = (t64 < t65);
                        if (!t66) {
                            goto gm_lb10;
                        }
                        {
                            void * t67 = GM_LD(void *, ((uint8_t *)&gm_l10));
                            uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                            uint64_t t69 = gm_l11;
                            {
                                {
                                    gm_t88rcucg1b7uk t70 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t67) + (uintptr_t)t69 * 4));
                                    gm_l12 = t70;
                                    void * t71 = (void *)((uint8_t *)&F.l9);
                                    void * t72 = gm_l0;
                                    gm_t88rcucg1b7uk t73 = gm_l12;
                                    void * t74 = gm_l1;
                                    gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)t74));
                                    gm_t88rcucg1b7uk t76 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t72, t73, t75);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tftb1odc739t5 t77 = gm_f_push_k3l9nic(t71, GM_CONV(gm_csguankko4lmhd, t76));
                                    gm_l35 = t77;
                                    uint8_t t78 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
                                    if (!t78) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 1, gm_src5eu5l4a7gv6s, 313, 30);
                                    goto gm_lb13;
                                }
gm_lb14:;
                                gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 313, 9);
                            }
gm_lb13:;
                        }
gm_lb12:;
                        uint64_t t79 = gm_l11;
                        uint64_t r80; bool o81 = __builtin_add_overflow(t79, ((uint64_t)1ULL), &r80);
                        uint64_t t82 = r80;
                        gm_l11 = t82;
                    }
                }
gm_lb10:;
                void * t83 = gm_l0;
                uint32_t t84 = GM_LD(uint32_t, ((uint8_t *)&gm_l4));
                gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
                gm_b16_8 t86 = GM_LD(gm_b16_8, ((uint8_t *)&t85));
                void * t87 = gm_l1;
                gm_tl874t2atnlh3 t88 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t87) + 96));
                uint32_t t89 = gm_f_instantiate_35r5pb2((void *)NULL, cx, NULL, t83, t84, t86, t88);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t89;
                goto gm_fret;
            }
gm_lb4:;
        }
gm_lb3:;
    }
    {
        void * t90 = gm_l0;
        void * t91 = GM_LD(void *, (((uint8_t *)t90) + 384));
        uint64_t t92 = GM_LD(uint64_t, ((((uint8_t *)t90) + 384) + 8));
        uint32_t t93 = gm_l2;
        uint64_t t94 = (uint64_t)t93;
        if ((uintptr_t)t94 >= (uintptr_t)(t92)) gm_trap_index((uintptr_t)t94, t92, gm_src5eu5l4a7gv6s, 316, 12);
        uint8_t t95 = GM_LD(uint8_t, ((((uint8_t *)t91) + (uintptr_t)t94 * 64) + 56));
        uint8_t t96 = (uint8_t)!t95;
        if (t96) {
            uint32_t t97 = gm_l2;
            F.ret = t97;
            goto gm_fret;
        }
        void * t98 = gm_l0;
        void * t99 = GM_LD(void *, (((uint8_t *)t98) + 344));
        uint64_t t100 = GM_LD(uint64_t, ((((uint8_t *)t98) + 344) + 8));
        uint32_t t101 = gm_l2;
        uint64_t t102 = (uint64_t)t101;
        if ((uintptr_t)t102 >= (uintptr_t)(t100)) gm_trap_index((uintptr_t)t102, t100, gm_src5eu5l4a7gv6s, 318, 12);
        memmove(((uint8_t *)&F.l13), (((uint8_t *)t99) + (uintptr_t)t102 * 136), 136);
        gm_trqfvmjtt4sij t103;
        gm_f_list_af74s0q((void *)&t103, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l14 = t103;
        gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 40));
        gm_l15 = t104;
        gm_l16 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t105 = gm_l16;
                uint64_t t106 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                uint8_t t107 = (t105 < t106);
                if (!t107) {
                    goto gm_lb15;
                }
                {
                    void * t108 = GM_LD(void *, ((uint8_t *)&gm_l15));
                    uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                    uint64_t t110 = gm_l16;
                    {
                        {
                            {
                                memmove(((uint8_t *)&F.l17), (((uint8_t *)t108) + (uintptr_t)t110 * 56), 56);
                            }
                            {
                                memmove(((uint8_t *)&F.l18), ((uint8_t *)&F.l17), 56);
                            }
                            void * t111 = gm_l0;
                            gm_t88rcucg1b7uk t112 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l17) + 16));
                            void * t113 = gm_l1;
                            gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)t113));
                            gm_t88rcucg1b7uk t115 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t111, t112, t114);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            GM_ST(gm_t88rcucg1b7uk, (((uint8_t *)&F.l18) + 16), t115);
                            {
                                void * t116 = (void *)((uint8_t *)&F.l14);
                                gm_tftb1odc739t5 t117 = gm_f_push_o8u01sh(t116, &(*(gm_t5v0c8ia6a5mo *)((uint8_t *)&F.l18)));
                                gm_l36 = t117;
                            }
                            uint8_t t118 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
                            if (!t118) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 1, gm_src5eu5l4a7gv6s, 323, 9);
                            goto gm_lb18;
                        }
gm_lb19:;
                        gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 320, 5);
                    }
gm_lb18:;
                }
gm_lb17:;
                uint64_t t119 = gm_l16;
                uint64_t r120; bool o121 = __builtin_add_overflow(t119, ((uint64_t)1ULL), &r120);
                uint64_t t122 = r120;
                gm_l16 = t122;
            }
        }
gm_lb15:;
        void * t123 = gm_l0;
        void * t124 = GM_LD(void *, (((uint8_t *)t123) + 384));
        uint64_t t125 = GM_LD(uint64_t, ((((uint8_t *)t123) + 384) + 8));
        uint32_t t126 = gm_l2;
        uint64_t t127 = (uint64_t)t126;
        if ((uintptr_t)t127 >= (uintptr_t)(t125)) gm_trap_index((uintptr_t)t127, t125, gm_src5eu5l4a7gv6s, 325, 12);
        memmove(((uint8_t *)&F.l19), (((uint8_t *)t124) + (uintptr_t)t127 * 64), 64);
    }
    {
        void * t128 = gm_l0;
        gm_t9qkh0rsooc8d t129 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l19));
        gm_b16_8 t130 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
        gm_tl874t2atnlh3 t131 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l13) + 16));
        uint64_t t132 = GM_LD(uint64_t, (((uint8_t *)&F.l13) + 32));
        gm_b16_8 t133 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
        void * t134 = gm_l0;
        gm_t88rcucg1b7uk t135 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l13) + 56));
        void * t136 = gm_l1;
        gm_b16_8 t137 = GM_LD(gm_b16_8, ((uint8_t *)t136));
        gm_t88rcucg1b7uk t138 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t134, t135, t137);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t139 = gm_l0;
        void * t140 = (void *)(((uint8_t *)&F.l13) + 64);
        void * t141 = gm_l1;
        gm_b16_8 t142 = GM_LD(gm_b16_8, ((uint8_t *)t141));
        gm_tnpifv20irnph t143 = gm_f_subst_effects_jnlbu5m((void *)NULL, cx, NULL, t139, t140, t142);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t144 = gm_f_new_nested_fn_r5u4ioo((void *)NULL, cx, NULL, t128, t129, t130, t131, t132, t133, t138, t143);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l20 = t144;
        void * t145 = gm_l1;
        void * t146 = (void *)(((uint8_t *)t145) + 16);
        uint32_t t147 = gm_l2;
        uint32_t t148 = gm_l20;
        gm_tcc1rq4n1avb2 t149;
        GM_ST(uint32_t, ((uint8_t *)&t149 + 0), t147);
        GM_ST(uint32_t, ((uint8_t *)&t149 + 4), t148);
        gm_tftb1odc739t5 t150 = gm_f_push_1ncolkq(t146, GM_CONV(gm_cs9ufftihtbpo2, t149));
        gm_l37 = t150;
        uint8_t t151 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
        if (!t151) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 1, gm_src5eu5l4a7gv6s, 327, 5);
        void * t152 = gm_l0;
        void * t153 = (void *)(((uint8_t *)t152) + 2856);
        uint32_t t154 = gm_l20;
        uint32_t t155 = gm_l20;
        gm_tftb1odc739t5 t156 = gm_f_put_33kejrs(t153, t154, t155);
        gm_l38 = t156;
        uint8_t t157 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
        if (!t157) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 1, gm_src5eu5l4a7gv6s, 328, 5);
        gm_trqfvmjtt4sij t158;
        gm_f_list_p9s2t99((void *)&t158, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l21 = t158;
        gm_b16_8 t159 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 24));
        gm_l22 = t159;
        gm_l23 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t160 = gm_l23;
                uint64_t t161 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                uint8_t t162 = (t160 < t161);
                if (!t162) {
                    goto gm_lb20;
                }
                {
                    void * t163 = GM_LD(void *, ((uint8_t *)&gm_l22));
                    uint64_t t164 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                    uint64_t t165 = gm_l23;
                    {
                        {
                            {
                                memmove(((uint8_t *)&F.l24), (((uint8_t *)t163) + (uintptr_t)t165 * 40), 40);
                            }
                            {
                                memmove(((uint8_t *)&F.l25), ((uint8_t *)&F.l24), 40);
                            }
                            void * t166 = gm_l0;
                            void * t167 = gm_l1;
                            gm_t88rcucg1b7uk t168 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l24) + 16));
                            gm_t88rcucg1b7uk t169 = gm_f_mono_ty_atcu0jn((void *)NULL, cx, NULL, t166, t167, t168);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            GM_ST(gm_t88rcucg1b7uk, (((uint8_t *)&F.l25) + 16), t169);
                            {
                                void * t170 = (void *)((uint8_t *)&F.l21);
                                gm_tftb1odc739t5 t171 = gm_f_push_am7bo0v(t170, &(*(gm_tsoie1ppgdvcg *)((uint8_t *)&F.l25)));
                                gm_l39 = t171;
                            }
                            uint8_t t172 = (uint8_t)!((((uint8_t *)&gm_l39))[0] != 0);
                            if (!t172) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l39) + 1, gm_src5eu5l4a7gv6s, 333, 9);
                            goto gm_lb23;
                        }
gm_lb24:;
                        gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 330, 5);
                    }
gm_lb23:;
                }
gm_lb22:;
                uint64_t t173 = gm_l23;
                uint64_t r174; bool o175 = __builtin_add_overflow(t173, ((uint64_t)1ULL), &r174);
                uint64_t t176 = r174;
                gm_l23 = t176;
            }
        }
gm_lb20:;
    }
    {
        gm_t9805qtonbsag t177;
        memset(&t177, 0, sizeof t177);
        gm_l26 = t177;
        {
            {
                uint8_t t178 = (uint8_t)(((((uint8_t *)&F.l19) + 48))[4] != 0);
                if (!t178) {
                    goto gm_lb26;
                }
                uint32_t t179 = GM_LD(uint32_t, (((uint8_t *)&F.l19) + 48));
                gm_l27 = t179;
                void * t180 = gm_l0;
                void * t181 = gm_l1;
                uint32_t t182 = gm_l27;
                uint32_t t183 = gm_f_mono_fn_aj1vgri((void *)NULL, cx, NULL, t180, t181, t182);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t9805qtonbsag t184;
                GM_ST(uint8_t, (uint8_t *)&t184 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t184 + 6, 0);
                GM_ST(uint32_t, ((uint8_t *)&t184), t183);
                ((uint8_t *)&t184)[4] = 1;
                gm_l26 = t184;
                goto gm_lb25;
            }
gm_lb26:;
        }
gm_lb25:;
        gm_l28 = ((void *)0);
        {
            {
                uint8_t t185 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l19) + 40)) != 0);
                if (!t185) {
                    goto gm_lb28;
                }
                void * t186 = GM_LD(void *, (((uint8_t *)&F.l19) + 40));
                gm_l29 = t186;
                void * t187 = gm_l29;
                void * t188 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t187);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l30 = t188;
                void * t189 = gm_l1;
                uint8_t t190 = GM_LD(uint8_t, (((uint8_t *)t189) + 108));
                gm_l31 = t190;
                void * t191 = gm_l1;
                void * t192 = GM_LD(void *, (((uint8_t *)t191) + 112));
                gm_l32 = t192;
                void * t193 = gm_l1;
                GM_ST(uint8_t, (((uint8_t *)t193) + 108), ((uint8_t)1));
                void * t194 = gm_l1;
                void * t195 = (void *)((uint8_t *)&F.l21);
                GM_ST(void *, (((uint8_t *)t194) + 112), t195);
                void * t196 = gm_l0;
                void * t197 = gm_l1;
                void * t198 = gm_l30;
                gm_b0_1 t199 = gm_f_mono_expr_p8qb7fe((void *)NULL, cx, NULL, t196, t197, t198);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t200 = gm_l1;
                uint8_t t201 = gm_l31;
                GM_ST(uint8_t, (((uint8_t *)t200) + 108), t201);
                void * t202 = gm_l1;
                void * t203 = gm_l32;
                GM_ST(void *, (((uint8_t *)t202) + 112), t203);
                void * t204 = gm_l30;
                gm_l28 = t204;
                goto gm_lb27;
            }
gm_lb28:;
        }
gm_lb27:;
        void * t205 = gm_l0;
        void * t206 = GM_LD(void *, (((uint8_t *)t205) + 384));
        uint64_t t207 = GM_LD(uint64_t, ((((uint8_t *)t205) + 384) + 8));
        uint32_t t208 = gm_l20;
        uint64_t t209 = (uint64_t)t208;
        if ((uintptr_t)t209 >= (uintptr_t)(t207)) gm_trap_index((uintptr_t)t209, t207, gm_src5eu5l4a7gv6s, 350, 12);
        void * t210 = (void *)(((uint8_t *)t206) + (uintptr_t)t209 * 64);
        gm_l33 = t210;
        void * t211 = gm_l33;
        gm_b16_8 t212 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 8));
        GM_ST(gm_b16_8, (((uint8_t *)t211) + 8), t212);
        void * t213 = gm_l33;
        gm_b16_8 t214 = GM_LD(gm_b16_8, ((uint8_t *)&F.l21));
        GM_ST(gm_b16_8, (((uint8_t *)t213) + 24), t214);
    }
    {
        void * t215 = gm_l33;
        void * t216 = gm_l28;
        GM_ST(void *, (((uint8_t *)t215) + 40), t216);
        void * t217 = gm_l33;
        gm_t9805qtonbsag t218 = gm_l26;
        GM_ST(gm_t9805qtonbsag, (((uint8_t *)t217) + 48), t218);
        void * t219 = gm_l33;
        GM_ST(uint8_t, (((uint8_t *)t219) + 56), ((uint8_t)1));
        uint32_t t220 = gm_l20;
        F.ret = t220;
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
__attribute__((aligned(4))) uint32_t gm_f_mono_handler_38b78qh(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2) {
    struct gm_fr_mono_handler_38b78qh F;
    void * gm_l0; /* s */
    void * gm_l1; /* mc */
    uint32_t gm_l2; /* h */
    uint32_t gm_l3; /* n */
    uint32_t gm_l5; /* new_h */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint32_t gm_l9; /* c */
    GM_STACK_CHECK(&F, 553u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t9805qtonbsag gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_tftb1odc739t5 gm_l13;
    {
        {
            void * t9 = gm_l1;
            void * t10 = (void *)(((uint8_t *)t9) + 56);
            uint32_t t11 = gm_l2;
            gm_t9805qtonbsag t12 = gm_f_find_pair_ddtpbq9(t10, t11);
            gm_l10 = t12;
            uint8_t t13 = (uint8_t)((((uint8_t *)&gm_l10))[4] != 0);
            if (!t13) {
                goto gm_lb2;
            }
            uint32_t t14 = GM_LD(uint32_t, ((uint8_t *)&gm_l10));
            gm_l3 = t14;
            uint32_t t15 = gm_l3;
            F.ret = t15;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        void * t16 = gm_l0;
        void * t17 = GM_LD(void *, (((uint8_t *)t16) + 504));
        uint64_t t18 = GM_LD(uint64_t, ((((uint8_t *)t16) + 504) + 8));
        uint32_t t19 = gm_l2;
        uint64_t t20 = (uint64_t)t19;
        if ((uintptr_t)t20 >= (uintptr_t)(t18)) gm_trap_index((uintptr_t)t20, t18, gm_src5eu5l4a7gv6s, 362, 12);
        memmove(((uint8_t *)&F.l4), (((uint8_t *)t17) + (uintptr_t)t20 * 40), 40);
    }
    void * t21 = gm_l0;
    uint64_t t22 = GM_LD(uint64_t, ((((uint8_t *)t21) + 504) + 8));
    uint32_t t23 = (uint32_t)t22;
    gm_l5 = t23;
    {
        void * t24 = gm_l0;
        void * t25 = (void *)(((uint8_t *)t24) + 504);
        void * t26 = gm_l0;
        void * t27 = (void *)((uint8_t *)&F.l4);
        void * t28 = gm_l1;
        gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)t28));
        gm_tiilmfd16v3fc t30;
        gm_f_subst_eref_kf9o1q5((void *)&t30, (void *)NULL, cx, NULL, t26, t27, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t31 = gm_f_empty_slice_d914i5u();
        gm_t1cs2fjnrqap1 t32;
        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t32 + 0), t30);
        GM_ST(gm_b16_8, ((uint8_t *)&t32 + 24), t31);
        gm_tftb1odc739t5 t33 = gm_f_push_bu0mlkm(t25, &t32);
        gm_l11 = t33;
    }
    uint8_t t34 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
    if (!t34) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_src5eu5l4a7gv6s, 364, 5);
    void * t35 = gm_l1;
    void * t36 = (void *)(((uint8_t *)t35) + 56);
    uint32_t t37 = gm_l2;
    uint32_t t38 = gm_l5;
    gm_tcc1rq4n1avb2 t39;
    GM_ST(uint32_t, ((uint8_t *)&t39 + 0), t37);
    GM_ST(uint32_t, ((uint8_t *)&t39 + 4), t38);
    gm_tftb1odc739t5 t40 = gm_f_push_1ncolkq(t36, GM_CONV(gm_cs9ufftihtbpo2, t39));
    gm_l12 = t40;
    uint8_t t41 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
    if (!t41) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_src5eu5l4a7gv6s, 365, 5);
    {
        gm_trqfvmjtt4sij t42;
        gm_f_list_go2qg0b((void *)&t42, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l6 = t42;
    }
    gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 24));
    gm_l7 = t43;
    gm_l8 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t44 = gm_l8;
            uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
            uint8_t t46 = (t44 < t45);
            if (!t46) {
                goto gm_lb3;
            }
            {
                void * t47 = GM_LD(void *, ((uint8_t *)&gm_l7));
                uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint64_t t49 = gm_l8;
                {
                    {
                        uint32_t t50 = GM_LD(uint32_t, (((uint8_t *)t47) + (uintptr_t)t49 * 4));
                        gm_l9 = t50;
                        void * t51 = (void *)((uint8_t *)&F.l6);
                        void * t52 = gm_l0;
                        void * t53 = gm_l1;
                        uint32_t t54 = gm_l9;
                        uint32_t t55 = gm_f_mono_fn_aj1vgri((void *)NULL, cx, NULL, t52, t53, t54);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t56 = gm_f_push_im9d4ro(t51, t55);
                        gm_l13 = t56;
                        uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                        if (!t57) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_src5eu5l4a7gv6s, 367, 28);
                        goto gm_lb6;
                    }
gm_lb7:;
                    gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 367, 5);
                }
gm_lb6:;
            }
gm_lb5:;
            uint64_t t58 = gm_l8;
            uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
            uint64_t t61 = r59;
            gm_l8 = t61;
        }
    }
gm_lb3:;
    void * t62 = gm_l0;
    void * t63 = GM_LD(void *, (((uint8_t *)t62) + 504));
    uint64_t t64 = GM_LD(uint64_t, ((((uint8_t *)t62) + 504) + 8));
    uint32_t t65 = gm_l5;
    uint64_t t66 = (uint64_t)t65;
    if ((uintptr_t)t66 >= (uintptr_t)(t64)) gm_trap_index((uintptr_t)t66, t64, gm_src5eu5l4a7gv6s, 368, 5);
    gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
    GM_ST(gm_b16_8, ((((uint8_t *)t63) + (uintptr_t)t66 * 40) + 24), t67);
    uint32_t t68 = gm_l5;
    F.ret = t68;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_mono_pat_lipeb0h(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_mono_ty_atcu0jn(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_mono_ty_atcu0jn F;
    void * gm_l0; /* s */
    void * gm_l1; /* mc */
    gm_t88rcucg1b7uk gm_l2; /* t */
    uint32_t gm_l3; /* h */
    GM_STACK_CHECK(&F, 254u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l4;
    {
        {
            {
                void * t4 = gm_l0;
                void * t5 = (void *)(((uint8_t *)t4) + 8);
                gm_t88rcucg1b7uk t6 = gm_l2;
                gm_f_kind_jcnt4hm((void *)&gm_l4, t5, GM_CONV(gm_csguankko4lmhd, t6));
            }
            uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)25ULL));
            if (!t8) {
                goto gm_lb2;
            }
            uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 8));
            gm_l3 = t9;
            {
                void * t10 = gm_l0;
                void * t11 = (void *)(((uint8_t *)t10) + 8);
                void * t12 = gm_l0;
                void * t13 = gm_l1;
                uint32_t t14 = gm_l3;
                uint32_t t15 = gm_f_mono_handler_38b78qh((void *)NULL, cx, NULL, t12, t13, t14);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t53at80pfdus3 t16;
                memset(&t16, 0, sizeof t16);
                GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)25ULL));
                GM_ST(uint32_t, ((uint8_t *)&t16 + 8), t15);
                gm_t88rcucg1b7uk t17 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t11, &t16);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t17;
                goto gm_fret;
            }
        }
gm_lb2:;
    }
gm_lb1:;
    void * t18 = gm_l0;
    gm_t88rcucg1b7uk t19 = gm_l2;
    void * t20 = gm_l1;
    gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)t20));
    gm_t88rcucg1b7uk t22 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t18, t19, t21);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t22;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_monomorphize_21k6pku(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_produce_instance_kcqgj3e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_produce_instance_kcqgj3e F;
    void * gm_l0; /* s */
    void * gm_l3; /* body */
    gm_b16_8 gm_l4; /* tps */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    uint32_t gm_l9; /* g */
    uint32_t gm_l10; /* outer */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    void * gm_l17; /* copy */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    GM_STACK_CHECK(&F, 1429u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tn68el8d14vku gm_l21;
    gm_tftb1odc739t5 gm_l22;
    gm_tftb1odc739t5 gm_l23;
    gm_tftb1odc739t5 gm_l24;
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 384));
        uint64_t t21 = GM_LD(uint64_t, ((((uint8_t *)t19) + 384) + 8));
        uint32_t t22 = GM_LD(uint32_t, ((uint8_t *)a1));
        uint64_t t23 = (uint64_t)t22;
        if ((uintptr_t)t23 >= (uintptr_t)(t21)) gm_trap_index((uintptr_t)t23, t21, gm_src5eu5l4a7gv6s, 66, 12);
        memmove(((uint8_t *)&F.l2), (((uint8_t *)t20) + (uintptr_t)t23 * 64), 64);
    }
    uint8_t t24 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l2) + 40)) != 0);
    if (t24) {
        void * t25 = GM_LD(void *, (((uint8_t *)&F.l2) + 40));
        gm_l3 = t25;
    } else {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t26 = gm_l0;
    uint32_t t27 = GM_LD(uint32_t, ((uint8_t *)a1));
    gm_b16_8 t28 = gm_f_template_params_6stj17b((void *)NULL, cx, NULL, t26, t27);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t28;
    {
        {
            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t30 = (t29 > ((uint64_t)0ULL));
            if (!t30) {
                goto gm_lb2;
            }
            void * t31 = gm_l0;
            gm_b16_8 t32 = gm_l4;
            uint8_t t33 = gm_f_share_instance_1q66m8k((void *)NULL, cx, NULL, t31, &(*(gm_t0ut88vjdgrt1 *)((uint8_t *)a1)), t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t33) {
                goto gm_lb2;
            }
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        gm_trqfvmjtt4sij t34;
        gm_f_list_oalq03d((void *)&t34, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l5 = t34;
    }
    void * t35 = gm_l0;
    void * t36 = GM_LD(void *, (((uint8_t *)t35) + 344));
    uint64_t t37 = GM_LD(uint64_t, ((((uint8_t *)t35) + 344) + 8));
    uint32_t t38 = GM_LD(uint32_t, ((uint8_t *)a1));
    uint64_t t39 = (uint64_t)t38;
    if ((uintptr_t)t39 >= (uintptr_t)(t37)) gm_trap_index((uintptr_t)t39, t37, gm_src5eu5l4a7gv6s, 71, 27);
    gm_b16_8 t40 = GM_LD(gm_b16_8, ((((uint8_t *)t36) + (uintptr_t)t39 * 136) + 80));
    gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
    gm_l6 = t41;
    gm_l7 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t42 = gm_l7;
            uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint8_t t44 = (t42 < t43);
            if (!t44) {
                goto gm_lb3;
            }
            {
                uint64_t t45 = gm_l7;
                void * t46 = GM_LD(void *, ((uint8_t *)&gm_l6));
                uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint64_t t48 = gm_l7;
                uint32_t t49 = GM_LD(uint32_t, (((uint8_t *)t46) + (uintptr_t)t48 * 4));
                gm_tn68el8d14vku t50;
                GM_ST(uint32_t, (uint8_t *)&t50 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t50 + 0), t45);
                GM_ST(uint32_t, ((uint8_t *)&t50 + 8), t49);
                gm_l21 = t50;
                {
                    {
                        uint64_t t51 = GM_LD(uint64_t, ((uint8_t *)&gm_l21));
                        gm_l8 = t51;
                        uint32_t t52 = GM_LD(uint32_t, (((uint8_t *)&gm_l21) + 8));
                        gm_l9 = t52;
                        void * t53 = (void *)((uint8_t *)&F.l5);
                        uint32_t t54 = gm_l9;
                        void * t55 = GM_LD(void *, (((uint8_t *)a1) + 8));
                        uint64_t t56 = GM_LD(uint64_t, ((((uint8_t *)a1) + 8) + 8));
                        uint64_t t57 = gm_l8;
                        if ((uintptr_t)t57 >= (uintptr_t)(t56)) gm_trap_index((uintptr_t)t57, t56, gm_src5eu5l4a7gv6s, 71, 105);
                        gm_t88rcucg1b7uk t58 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t55) + (uintptr_t)t57 * 4));
                        gm_tplpbqf3duvn3 t59;
                        GM_ST(uint32_t, ((uint8_t *)&t59 + 0), t54);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t59 + 4), t58);
                        gm_tftb1odc739t5 t60 = gm_f_push_h69n5fi(t53, GM_CONV(gm_cs7e9ppsd384bj, t59));
                        gm_l22 = t60;
                        uint8_t t61 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                        if (!t61) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 1, gm_src5eu5l4a7gv6s, 71, 71);
                        goto gm_lb6;
                    }
gm_lb7:;
                    gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 71, 5);
                }
gm_lb6:;
            }
gm_lb5:;
            uint64_t t62 = gm_l7;
            uint64_t r63; bool o64 = __builtin_add_overflow(t62, ((uint64_t)1ULL), &r63);
            uint64_t t65 = r63;
            gm_l7 = t65;
        }
    }
gm_lb3:;
    void * t66 = gm_l0;
    uint32_t t67 = GM_LD(uint32_t, (((uint8_t *)t66) + 1360));
    gm_l10 = t67;
    void * t68 = gm_l0;
    uint32_t t69 = GM_LD(uint32_t, (((uint8_t *)a1) + 36));
    GM_ST(uint32_t, (((uint8_t *)t68) + 1360), t69);
    void * t70 = gm_l0;
    gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
    gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
    gm_tl874t2atnlh3 t73 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 24));
    gm_toceffb697p0u t74;
    GM_ST(uint8_t, (uint8_t *)&t74 + 13, 0);
    GM_ST(uint16_t, (uint8_t *)&t74 + 14, 0);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t74), t73);
    ((uint8_t *)&t74)[12] = 1;
    gm_b0_1 t75 = gm_f_check_len_exprs_m8s6m0h((void *)NULL, cx, NULL, t70, t72, t74);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        gm_trqfvmjtt4sij t76;
        gm_f_list_p9s2t99((void *)&t76, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l11 = t76;
    }
    {
        gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
        gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
        gm_trqfvmjtt4sij t79;
        gm_f_list_7l73vjb((void *)&t79, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t80;
        gm_f_list_7l73vjb((void *)&t80, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tl874t2atnlh3 t81 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 24));
        void * t82 = (void *)((uint8_t *)&F.l11);
        gm_b16_8 t83 = gm_f_empty_slice_s65n31g();
        gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
        gm_tblkd491pk4cr t85;
        GM_ST(uint8_t, (uint8_t *)&t85 + 109, 0);
        GM_ST(uint16_t, (uint8_t *)&t85 + 110, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t85 + 0), t78);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t85 + 16), t79);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t85 + 56), t80);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t85 + 96), t81);
        GM_ST(uint8_t, ((uint8_t *)&t85 + 108), ((uint8_t)0));
        GM_ST(void *, ((uint8_t *)&t85 + 112), t82);
        GM_ST(gm_b16_8, ((uint8_t *)&t85 + 120), t84);
        GM_ST(void *, ((uint8_t *)&t85 + 136), ((void *)0));
        F.l12 = t85;
    }
    void * t86 = (void *)(((uint8_t *)&F.l12) + 16);
    uint32_t t87 = GM_LD(uint32_t, ((uint8_t *)a1));
    uint32_t t88 = GM_LD(uint32_t, (((uint8_t *)a1) + 4));
    gm_tcc1rq4n1avb2 t89;
    GM_ST(uint32_t, ((uint8_t *)&t89 + 0), t87);
    GM_ST(uint32_t, ((uint8_t *)&t89 + 4), t88);
    gm_tftb1odc739t5 t90 = gm_f_push_1ncolkq(t86, GM_CONV(gm_cs9ufftihtbpo2, t89));
    gm_l23 = t90;
    uint8_t t91 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
    if (!t91) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_src5eu5l4a7gv6s, 77, 5);
    gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 24));
    gm_l13 = t92;
    gm_l14 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t93 = gm_l14;
            uint64_t t94 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
            uint8_t t95 = (t93 < t94);
            if (!t95) {
                goto gm_lb8;
            }
            {
                void * t96 = GM_LD(void *, ((uint8_t *)&gm_l13));
                uint64_t t97 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                uint64_t t98 = gm_l14;
                {
                    {
                        {
                            memmove(((uint8_t *)&F.l15), (((uint8_t *)t96) + (uintptr_t)t98 * 40), 40);
                        }
                        {
                            memmove(((uint8_t *)&F.l16), ((uint8_t *)&F.l15), 40);
                        }
                        void * t99 = gm_l0;
                        void * t100 = (void *)((uint8_t *)&F.l12);
                        gm_t88rcucg1b7uk t101 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l15) + 16));
                        gm_t88rcucg1b7uk t102 = gm_f_mono_ty_atcu0jn((void *)NULL, cx, NULL, t99, t100, t101);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(gm_t88rcucg1b7uk, (((uint8_t *)&F.l16) + 16), t102);
                        {
                            void * t103 = (void *)((uint8_t *)&F.l11);
                            gm_tftb1odc739t5 t104 = gm_f_push_am7bo0v(t103, &(*(gm_tsoie1ppgdvcg *)((uint8_t *)&F.l16)));
                            gm_l24 = t104;
                        }
                        uint8_t t105 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                        if (!t105) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_src5eu5l4a7gv6s, 81, 9);
                        goto gm_lb11;
                    }
gm_lb12:;
                    gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 78, 5);
                }
gm_lb11:;
            }
gm_lb10:;
            uint64_t t106 = gm_l14;
            uint64_t r107; bool o108 = __builtin_add_overflow(t106, ((uint64_t)1ULL), &r107);
            uint64_t t109 = r107;
            gm_l14 = t109;
        }
    }
gm_lb8:;
    void * t110 = gm_l3;
    void * t111 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t110);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l17 = t111;
    void * t112 = gm_l0;
    void * t113 = (void *)((uint8_t *)&F.l12);
    void * t114 = gm_l17;
    gm_b0_1 t115 = gm_f_mono_expr_p8qb7fe((void *)NULL, cx, NULL, t112, t113, t114);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t116 = gm_l0;
    gm_tl874t2atnlh3 t117 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 24));
    gm_b0_1 t118 = gm_f_report_soa_errors_m1bqopj((void *)NULL, cx, NULL, t116, t117);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t119 = gm_l0;
    uint32_t t120 = gm_l10;
    GM_ST(uint32_t, (((uint8_t *)t119) + 1360), t120);
    {
        void * t121 = gm_l0;
        uint32_t t122 = GM_LD(uint32_t, (((uint8_t *)a1) + 4));
        gm_t9qkh0rsooc8d t123 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l2));
        gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 8));
        gm_b16_8 t125 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
        void * t126 = gm_l17;
        gm_t9805qtonbsag t127 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&F.l2) + 48));
        uint8_t t128 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 56));
        gm_tdgfduqjheqdm t129;
        GM_ST(uint8_t, (uint8_t *)&t129 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t129 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t129 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t129 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&t129 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&t129 + 60, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t129 + 0), t123);
        GM_ST(gm_b16_8, ((uint8_t *)&t129 + 8), t124);
        GM_ST(gm_b16_8, ((uint8_t *)&t129 + 24), t125);
        GM_ST(void *, ((uint8_t *)&t129 + 40), t126);
        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t129 + 48), t127);
        GM_ST(uint8_t, ((uint8_t *)&t129 + 56), t128);
        gm_b0_1 t130 = gm_f_set_inst_def_tcpabg0((void *)NULL, cx, NULL, t121, t122, &t129);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    gm_b16_8 t131 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 24));
    gm_l18 = t131;
    gm_l19 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t132 = gm_l19;
            uint64_t t133 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
            uint8_t t134 = (t132 < t133);
            if (!t134) {
                goto gm_lb13;
            }
            {
                void * t135 = GM_LD(void *, ((uint8_t *)&gm_l18));
                uint64_t t136 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                uint64_t t137 = gm_l19;
                {
                    {
                        memmove(((uint8_t *)&F.l20), (((uint8_t *)t135) + (uintptr_t)t137 * 40), 40);
                        void * t138 = gm_l0;
                        void * t139 = gm_l0;
                        gm_t88rcucg1b7uk t140 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l20) + 16));
                        gm_b16_8 t141 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
                        gm_b16_8 t142 = GM_LD(gm_b16_8, ((uint8_t *)&t141));
                        gm_t88rcucg1b7uk t143 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t139, t140, t142);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tl874t2atnlh3 t144 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 24));
                        uint8_t t145 = gm_f_type_size_ok_tr1llg4((void *)NULL, cx, NULL, t138, t143, t144);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t146 = (uint8_t)!t145;
                        if (t146) {
                            goto gm_lb13;
                        }
                        goto gm_lb16;
                    }
gm_lb17:;
                    gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 89, 5);
                }
gm_lb16:;
            }
gm_lb15:;
            uint64_t t147 = gm_l19;
            uint64_t r148; bool o149 = __builtin_add_overflow(t147, ((uint64_t)1ULL), &r148);
            uint64_t t150 = r148;
            gm_l19 = t150;
        }
    }
gm_lb13:;
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
inline __attribute__((aligned(4))) void gm_f_subst_eref_kf9o1q5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_subst_eref_kf9o1q5 F;
    void * gm_l0; /* s */
    void * gm_l1; /* e */
    gm_b16_8 gm_l2; /* map */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t88rcucg1b7uk gm_l7; /* t */
    GM_STACK_CHECK(&F, 316u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_toa2vhe3l4hfr gm_l9;
    gm_tftb1odc739t5 gm_l10;
    {
        gm_trqfvmjtt4sij t10;
        gm_f_list_vmvl81b((void *)&t10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l3 = t10;
    }
    void * t11 = gm_l1;
    gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)t11) + 8));
    gm_l4 = t12;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t13 = gm_l5;
            uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t15 = (t13 < t14);
            if (!t15) {
                goto gm_lb1;
            }
            {
                void * t16 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t18 = gm_l5;
                {
                    {
                        {
                            memmove(((uint8_t *)&F.l6), (((uint8_t *)t16) + (uintptr_t)t18 * 40), 40);
                        }
                        {
                            void * t19 = (void *)((uint8_t *)&F.l3);
                            {
                                {
                                    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                                    uint8_t t21 = (uint8_t)(t20 == ((uint8_t)0ULL));
                                    if (!t21) {
                                        goto gm_lb7;
                                    }
                                    gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 8));
                                    gm_l7 = t22;
                                    void * t23 = gm_l0;
                                    gm_t88rcucg1b7uk t24 = gm_l7;
                                    gm_b16_8 t25 = gm_l2;
                                    gm_t88rcucg1b7uk t26 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t23, t24, t25);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    memset(&gm_l9, 0, sizeof gm_l9);
                                    GM_ST(uint8_t, ((uint8_t *)&gm_l9 + 0), ((uint8_t)0ULL));
                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l9 + 8), t26);
                                    goto gm_lb6;
                                }
gm_lb7:;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l8), ((uint8_t *)&F.l6), 40);
                                    }
                                    {
                                        memmove(((uint8_t *)&gm_l9), ((uint8_t *)&F.l8), 40);
                                    }
                                    goto gm_lb6;
                                }
gm_lb8:;
                                gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 294, 19);
                            }
gm_lb6:;
                            gm_tftb1odc739t5 t27 = gm_f_push_s5vr2a2(t19, &(*(gm_toa2vhe3l4hfr *)((uint8_t *)&gm_l9)));
                            gm_l10 = t27;
                        }
                        uint8_t t28 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                        if (!t28) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_src5eu5l4a7gv6s, 294, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 293, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t29 = gm_l5;
            uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
            uint64_t t32 = r30;
            gm_l5 = t32;
        }
    }
gm_lb1:;
    {
        void * t33 = gm_l1;
        uint32_t t34 = GM_LD(uint32_t, ((uint8_t *)t33));
        gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
        gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tiilmfd16v3fc *)gm_out) + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&(*(gm_tiilmfd16v3fc *)gm_out) + 0), t34);
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tiilmfd16v3fc *)gm_out) + 8), t36);
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

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
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; uint64_t v1; } f1; } gm_thf5kr0is9u9i;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
_Static_assert(sizeof(gm_thf5kr0is9u9i) == 16 && _Alignof(gm_thf5kr0is9u9i) == 8 && offsetof(gm_thf5kr0is9u9i, f0) == 0 && offsetof(gm_thf5kr0is9u9i, f1) == 8, "layout of `IoError!usize`");
gm_thf5kr0is9u9i gm_dt3g3ujqvah4_0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_8jjs01b(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_ig27ig2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern uint8_t gm_c05rjh68v5e[7] __attribute__((aligned(1)));
extern uint8_t gm_c0fn3on8qv1[10] __attribute__((aligned(1)));
extern uint8_t gm_c0u6va72tvj[45] __attribute__((aligned(1)));
extern uint8_t gm_c1i8nujdgu3[48] __attribute__((aligned(1)));
extern uint8_t gm_c1m3l8o6pqn[4] __attribute__((aligned(1)));
extern uint8_t gm_c2hth7bcr5i[5] __attribute__((aligned(1)));
extern uint8_t gm_c51bciei4ea[15] __attribute__((aligned(1)));
extern uint8_t gm_c695aum3fk1[16] __attribute__((aligned(8)));
extern uint8_t gm_c69cf1c2r6c[7] __attribute__((aligned(1)));
extern uint8_t gm_c6n9n3bf4b3[9] __attribute__((aligned(1)));
extern uint8_t gm_c71rmf91ugn[8] __attribute__((aligned(1)));
extern uint8_t gm_c7magu4r51i[12] __attribute__((aligned(1)));
extern uint8_t gm_c7t254lldbr[13] __attribute__((aligned(1)));
extern uint8_t gm_c90auv8b7m5[12] __attribute__((aligned(1)));
extern uint8_t gm_c9fnj4ddpds[13] __attribute__((aligned(1)));
extern uint8_t gm_c9la4vcjus5[16] __attribute__((aligned(1)));
extern uint8_t gm_c9ogaceq2mh[14] __attribute__((aligned(1)));
extern uint8_t gm_cb0tru27utl[12] __attribute__((aligned(1)));
extern uint8_t gm_ccpiqhf3ha6[9] __attribute__((aligned(1)));
extern uint8_t gm_ccrs5p8c6u1[12] __attribute__((aligned(1)));
extern uint8_t gm_cda1ravdhnf[13] __attribute__((aligned(1)));
extern uint8_t gm_cdeh8704ija[4] __attribute__((aligned(4)));
extern uint8_t gm_cdg6iib59cd[2] __attribute__((aligned(1)));
extern uint8_t gm_ce4lfdpck08[9] __attribute__((aligned(1)));
extern uint8_t gm_cesk7bev32f[15] __attribute__((aligned(1)));
extern uint8_t gm_cfi8o27oni0[3] __attribute__((aligned(1)));
extern uint8_t gm_cfj1s1vu5t2[1] __attribute__((aligned(1)));
extern uint8_t gm_cfmi0tp7ni9[9] __attribute__((aligned(1)));
extern uint8_t gm_cftc9ncjfm7[16] __attribute__((aligned(8)));
extern uint8_t gm_cgrtudv7s61[35] __attribute__((aligned(1)));
extern uint8_t gm_ch8t72j9a3i[11] __attribute__((aligned(1)));
extern uint8_t gm_chgr5anan0t[10] __attribute__((aligned(1)));
extern uint8_t gm_chu76sl03kb[13] __attribute__((aligned(1)));
extern uint8_t gm_ci2pejepm7o[9] __attribute__((aligned(1)));
extern uint8_t gm_cim7gbhispe[13] __attribute__((aligned(1)));
extern uint8_t gm_cj4ge4vvlni[5] __attribute__((aligned(1)));
extern uint8_t gm_cjr3tjj87ef[16] __attribute__((aligned(8)));
extern uint8_t gm_cjskjibtqb8[15] __attribute__((aligned(1)));
extern uint8_t gm_cjuh7rdtbga[17] __attribute__((aligned(1)));
extern uint8_t gm_ckfkap0hmh1[18] __attribute__((aligned(1)));
extern uint8_t gm_cl8mepvcg0b[11] __attribute__((aligned(1)));
extern uint8_t gm_cllmi9128mt[4] __attribute__((aligned(1)));
extern uint8_t gm_cm1s0561676[6] __attribute__((aligned(1)));
extern uint8_t gm_cm496uu8qhc[19] __attribute__((aligned(1)));
extern uint8_t gm_cm4pp8tt365[24] __attribute__((aligned(1)));
extern uint8_t gm_cmrmftl6v0f[18] __attribute__((aligned(1)));
extern uint8_t gm_cn2c0dgg5e8[4] __attribute__((aligned(1)));
extern uint8_t gm_cna5t6g5c6f[7] __attribute__((aligned(1)));
extern uint8_t gm_co0amvig9ac[7] __attribute__((aligned(1)));
extern uint8_t gm_coka7750chc[9] __attribute__((aligned(1)));
extern uint8_t gm_cpb4kn5t0rf[2] __attribute__((aligned(1)));
extern uint8_t gm_cqc8bus8k1v[7] __attribute__((aligned(1)));
extern uint8_t gm_cqma2eq6ij1[80] __attribute__((aligned(1)));
extern uint8_t gm_cqtn1r3tf68[2] __attribute__((aligned(1)));
extern uint8_t gm_cr8ckpnai0f[13] __attribute__((aligned(1)));
extern uint8_t gm_cre7u6dhl25[15] __attribute__((aligned(1)));
extern uint8_t gm_crj6tg85j72[18] __attribute__((aligned(1)));
extern uint8_t gm_crth1fagslo[10] __attribute__((aligned(1)));
extern uint8_t gm_cs8kn6g5l0h[10] __attribute__((aligned(1)));
extern uint8_t gm_cs9nni7g1o0[13] __attribute__((aligned(1)));
extern uint8_t gm_cseqn4no5c3[14] __attribute__((aligned(1)));
extern uint8_t gm_ct6ds3l9j7c[4] __attribute__((aligned(1)));
extern uint8_t gm_ctb2vnk0tul[2] __attribute__((aligned(1)));
extern uint8_t gm_ctfbu78unbf[101] __attribute__((aligned(1)));
extern uint8_t gm_ctloho1a5a0[53] __attribute__((aligned(1)));
extern uint8_t gm_cto913mafo2[8] __attribute__((aligned(8)));
extern uint8_t gm_ctuh6kms0vm[13] __attribute__((aligned(1)));
extern uint8_t gm_cuptbi6uclh[15] __attribute__((aligned(1)));
extern uint8_t gm_cusfosftpfn[4] __attribute__((aligned(4)));
extern uint8_t gm_cv98ob7u5a4[16] __attribute__((aligned(8)));
extern uint8_t gm_cvlea0kb4ed[11] __attribute__((aligned(1)));
extern uint8_t gm_cvq082bup6p[11] __attribute__((aligned(1)));
extern void *const gm_hd3g3ujqvah4[];
extern void *const gm_hldg2pcvebta[];
extern void *const gm_hlifma0pdvu5[];
extern uint8_t gm_k027ibn62fv[40] __attribute__((aligned(8)));
extern uint8_t gm_k0css3bab46[40] __attribute__((aligned(8)));
extern uint8_t gm_k231525i1sp[40] __attribute__((aligned(8)));
extern uint8_t gm_k52ekgkvp6f[40] __attribute__((aligned(8)));
extern uint8_t gm_k6kfar4qbkb[40] __attribute__((aligned(8)));
extern uint8_t gm_k7ik3fa1b9i[40] __attribute__((aligned(8)));
extern uint8_t gm_k8f4m32t0m4[40] __attribute__((aligned(8)));
extern uint8_t gm_ka4kfapougb[40] __attribute__((aligned(8)));
extern uint8_t gm_kbnuij4naoh[40] __attribute__((aligned(8)));
extern uint8_t gm_ke1o9nchpfo[40] __attribute__((aligned(8)));
extern uint8_t gm_kg8c0kikv62[40] __attribute__((aligned(8)));
extern uint8_t gm_kgh95sj78n8[40] __attribute__((aligned(8)));
extern uint8_t gm_kh832np8pgs[40] __attribute__((aligned(8)));
extern uint8_t gm_khp004r0f25[40] __attribute__((aligned(8)));
extern uint8_t gm_kismru0mlt2[40] __attribute__((aligned(8)));
extern uint8_t gm_kkei5iuecp0[40] __attribute__((aligned(8)));
extern uint8_t gm_kmpad6qi84v[40] __attribute__((aligned(8)));
extern uint8_t gm_knjegli7s8j[40] __attribute__((aligned(8)));
extern uint8_t gm_kpmakm2t9q3[40] __attribute__((aligned(8)));
extern uint8_t gm_kr6fmufdvee[40] __attribute__((aligned(8)));
extern uint8_t gm_kreaco1pgk3[40] __attribute__((aligned(8)));
extern uint8_t gm_ksksp29ghso[40] __attribute__((aligned(8)));
extern uint8_t gm_ksp6bldq59o[40] __attribute__((aligned(8)));
extern uint8_t gm_kv3bmftl0ar[40] __attribute__((aligned(8)));
extern uint8_t gm_kvp9m9fg1c2[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0116oq3lk1rs[4];
extern const uint8_t gm_s01fsr0ku4o6k[8];
extern const uint8_t gm_s01kcog235p88[45];
extern const uint8_t gm_s03c24qg7i06q[13];
extern const uint8_t gm_s03pmf867dcsr[2];
extern const uint8_t gm_s04a35h53le03[43];
extern const uint8_t gm_s06nnq6fe98bg[53];
extern const uint8_t gm_s08dcqdp58r5f[3];
extern const uint8_t gm_s08olofbep0dd[14];
extern const uint8_t gm_s0963m82avtn1[23];
extern const uint8_t gm_s0ajhhgcemkg1[18];
extern const uint8_t gm_s0b4nsmmmqukr[12];
extern const uint8_t gm_s0eudsv4chfi5[44];
extern const uint8_t gm_s0fpu2ivt8rj5[8];
extern const uint8_t gm_s0irgfjsc3pat[76];
extern const uint8_t gm_s0jbakoflqfkm[2];
extern const uint8_t gm_s0jrrhhm4skoj[15];
extern const uint8_t gm_s0r71f5repum2[21];
extern const uint8_t gm_s0rhh4q7cpm4k[20];
extern const uint8_t gm_s15gg5sblr4kl[4];
extern const uint8_t gm_s170memutnvs0[19];
extern const uint8_t gm_s174sqhpcliev[43];
extern const uint8_t gm_s19ndlkgdtviu[6];
extern const uint8_t gm_s1asl1e1tvb0k[2];
extern const uint8_t gm_s1b5tn3crejqs[11];
extern const uint8_t gm_s1b80v2hb8d4l[72];
extern const uint8_t gm_s1c0ll7cvuidm[12];
extern const uint8_t gm_s1c4sfff8ac39[7];
extern const uint8_t gm_s1d673tn03i3r[5];
extern const uint8_t gm_s1emqv9srjhhk[38];
extern const uint8_t gm_s1faagcukkgns[10];
extern const uint8_t gm_s1ffqahkqb2ed[141];
extern const uint8_t gm_s1fsfd0lcgf8e[17];
extern const uint8_t gm_s1g2ac815dje3[15];
extern const uint8_t gm_s1h7h9updp99l[54];
extern const uint8_t gm_s1idibqkta392[7];
extern const uint8_t gm_s1mgkad218qag[19];
extern const uint8_t gm_s1nt8u0pgo6vn[17];
extern const uint8_t gm_s1pfh89airaa0[13];
extern const uint8_t gm_s1pmmpf8t54g4[23];
extern const uint8_t gm_s1s3m1fet79ge[7];
extern const uint8_t gm_s1u0odc4938o8[58];
extern const uint8_t gm_s1uq73uineubo[35];
extern const uint8_t gm_s244ts3klchej[25];
extern const uint8_t gm_s24chpb914bep[6];
extern const uint8_t gm_s24s9g8r9gc0v[7];
extern const uint8_t gm_s26ul1d5snk55[9];
extern const uint8_t gm_s27tmg3h56aru[53];
extern const uint8_t gm_s28c3rj06c7d7[50];
extern const uint8_t gm_s2aj89r6sdims[28];
extern const uint8_t gm_s2c2r23li2if2[8];
extern const uint8_t gm_s2cgbvfh1b7rp[6];
extern const uint8_t gm_s2esb38j7hkqv[31];
extern const uint8_t gm_s2g22j1o1t23e[4];
extern const uint8_t gm_s2gtin895e08a[18];
extern const uint8_t gm_s2kieedtscsk1[12];
extern const uint8_t gm_s2lcii16lv48g[9];
extern const uint8_t gm_s2lldqflqcpmt[3];
extern const uint8_t gm_s2qcdb7brsccv[123];
extern const uint8_t gm_s2uphqpvb2vhh[4];
extern const uint8_t gm_s2urait735ofq[5];
extern const uint8_t gm_s338jbcmhh92n[20];
extern const uint8_t gm_s33h2lce46bnm[9];
extern const uint8_t gm_s36eqnq31lemp[29];
extern const uint8_t gm_s378kfmdl5enf[17];
extern const uint8_t gm_s38rtd27u976k[15];
extern const uint8_t gm_s398novfl4jrd[5];
extern const uint8_t gm_s39bonkvupk3m[7];
extern const uint8_t gm_s39mljh669qgl[2];
extern const uint8_t gm_s3d3pns6au9om[61];
extern const uint8_t gm_s3dccpt7atned[41];
extern const uint8_t gm_s3dflsv7jdvmc[44];
extern const uint8_t gm_s3e818t2qbjbh[5];
extern const uint8_t gm_s3erfe82rop7u[5];
extern const uint8_t gm_s3fj8brek97co[86];
extern const uint8_t gm_s3g9qhf2ithea[17];
extern const uint8_t gm_s3kb1co7e63ka[37];
extern const uint8_t gm_s3la5l2o2cgbq[43];
extern const uint8_t gm_s3lik4tvvjana[65];
extern const uint8_t gm_s3o2km4i906va[32];
extern const uint8_t gm_s3ol5q9br79vf[8];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s3p17h7dbk8us[8];
extern const uint8_t gm_s3t7gv740qicd[10];
extern const uint8_t gm_s3thrieq7sddl[23];
extern const uint8_t gm_s3tv2ib2tuvge[7];
extern const uint8_t gm_s40jlud5pi248[16];
extern const uint8_t gm_s41bap0m04tt9[12];
extern const uint8_t gm_s41ef4vrm07q0[41];
extern const uint8_t gm_s44ofm0qft8r4[41];
extern const uint8_t gm_s480gcu6od53p[61];
extern const uint8_t gm_s49hc37hqf8v6[13];
extern const uint8_t gm_s4a980fahtblo[3];
extern const uint8_t gm_s4efr72f9s8j4[29];
extern const uint8_t gm_s4ehc7fsh7k20[42];
extern const uint8_t gm_s4ehfliahvei9[6];
extern const uint8_t gm_s4h2qpm1qm9a0[8];
extern const uint8_t gm_s4hd92lokmtot[4];
extern const uint8_t gm_s4hpf7ii3vj1c[4];
extern const uint8_t gm_s4jmvuc273b30[12];
extern const uint8_t gm_s4k36hl9k22np[63];
extern const uint8_t gm_s4kq1149c9fl2[55];
extern const uint8_t gm_s4nddosr4taav[5];
extern const uint8_t gm_s4p53khe03les[18];
extern const uint8_t gm_s4po5fk785g2b[37];
extern const uint8_t gm_s4q8cddc9473e[14];
extern const uint8_t gm_s4s1nl0uacdl1[22];
extern const uint8_t gm_s50pmptf3idds[2];
extern const uint8_t gm_s512m3mlvu0cv[106];
extern const uint8_t gm_s516pv14t5t6i[73];
extern const uint8_t gm_s51lfi73niqcd[62];
extern const uint8_t gm_s521on3hdq96i[4];
extern const uint8_t gm_s52n7ji3kulf2[29];
extern const uint8_t gm_s55bilge3vko0[13];
extern const uint8_t gm_s55g9qp4arkn7[8];
extern const uint8_t gm_s585om8jb9mop[25];
extern const uint8_t gm_s588gob2um10i[2];
extern const uint8_t gm_s5fdm0n0s8bq4[30];
extern const uint8_t gm_s5gkjub7hejc1[9];
extern const uint8_t gm_s5gt30aqh30fl[19];
extern const uint8_t gm_s5k6emuk51u47[27];
extern const uint8_t gm_s5npk1gmjsfm6[16];
extern const uint8_t gm_s5pra70i8o734[14];
extern const uint8_t gm_s5t2ui8o1e0s4[9];
extern const uint8_t gm_s5u2kh0r9cm9p[7];
extern const uint8_t gm_s5vn66tnlipdf[19];
extern const uint8_t gm_s619rbci7p06v[20];
extern const uint8_t gm_s65khs4s753q2[11];
extern const uint8_t gm_s6g8mt9e5cjjr[11];
extern const uint8_t gm_s6jlko9gsl7im[26];
extern const uint8_t gm_s6jnas2nvtoja[41];
extern const uint8_t gm_s6k5k2kcfu4ll[10];
extern const uint8_t gm_s6l4819n1e7ou[9];
extern const uint8_t gm_s6lr92ekld44s[13];
extern const uint8_t gm_s6mq991cbd8md[53];
extern const uint8_t gm_s6pnfrub0rj1t[6];
extern const uint8_t gm_s6srmcrvka8iq[32];
extern const uint8_t gm_s6t4r831mukdr[3];
extern const uint8_t gm_s6u49d04cqlg1[17];
extern const uint8_t gm_s6uis6ovn881c[9];
extern const uint8_t gm_s6vmoui2ivq06[10];
extern const uint8_t gm_s6vstugpp3ivo[58];
extern const uint8_t gm_s71u85v3jrj86[63];
extern const uint8_t gm_s72phirci8af3[25];
extern const uint8_t gm_s759q7smc97t0[75];
extern const uint8_t gm_s7628qergfok6[11];
extern const uint8_t gm_s76ovr591an92[4];
extern const uint8_t gm_s7f8bb20dn4gg[21];
extern const uint8_t gm_s7kg16l1bme6s[11];
extern const uint8_t gm_s7knd69l0mhc0[36];
extern const uint8_t gm_s7mhpbh399e5s[4];
extern const uint8_t gm_s7mmjn8cgbhv8[67];
extern const uint8_t gm_s7nnecs6i9eki[41];
extern const uint8_t gm_s7pgps43h1eg7[41];
extern const uint8_t gm_s7rk1k9jrjscv[81];
extern const uint8_t gm_s7rltjaeh634d[11];
extern const uint8_t gm_s7t1ot7rgnuch[11];
extern const uint8_t gm_s7u0p3hthk80r[9];
extern const uint8_t gm_s81dvppjuktle[9];
extern const uint8_t gm_s84bra4h6kp3s[57];
extern const uint8_t gm_s8av0350tfnk3[20];
extern const uint8_t gm_s8b01h2tetj3e[45];
extern const uint8_t gm_s8c4bejqsu1da[48];
extern const uint8_t gm_s8hv5sfsqarrt[25];
extern const uint8_t gm_s8kfjp2od2jrl[51];
extern const uint8_t gm_s8kr43nig4sns[8];
extern const uint8_t gm_s8m5q4i8uoe5j[6];
extern const uint8_t gm_s8m8lg034q48k[23];
extern const uint8_t gm_s8pjstf2pdmbk[14];
extern const uint8_t gm_s8qpjmalo4drt[13];
extern const uint8_t gm_s8ro53eq54s60[44];
extern const uint8_t gm_s8rv63m5o5imh[9];
extern const uint8_t gm_s8vr59fbqtsbn[10];
extern const uint8_t gm_s93ft79mfnlvp[10];
extern const uint8_t gm_s94a48vj74t8b[74];
extern const uint8_t gm_s965n8o245s16[10];
extern const uint8_t gm_s966nqk7bt67q[19];
extern const uint8_t gm_s96bbamhk00af[29];
extern const uint8_t gm_s99ea9i63mcla[52];
extern const uint8_t gm_s99ll9oc7rh5u[15];
extern const uint8_t gm_s9ahj85e3uamt[12];
extern const uint8_t gm_s9bggu8oepdvs[19];
extern const uint8_t gm_s9dq8mbgn8du3[35];
extern const uint8_t gm_s9eqc3kak4tak[23];
extern const uint8_t gm_s9ff7kbg5s1vv[10];
extern const uint8_t gm_s9hi985r6f61m[3];
extern const uint8_t gm_s9mmepka9n2j2[10];
extern const uint8_t gm_s9r71kmnlqpv8[17];
extern const uint8_t gm_s9sfk4cm0i8u4[8];
extern const uint8_t gm_sa05vipu6bbea[22];
extern const uint8_t gm_sa291memmphhk[15];
extern const uint8_t gm_sa57j0d5pe8oa[51];
extern const uint8_t gm_sa5jkah09l2ke[20];
extern const uint8_t gm_sa5kad76tohle[3];
extern const uint8_t gm_sa7refaf4ue56[45];
extern const uint8_t gm_sa9dtqeprt7kf[75];
extern const uint8_t gm_sa9ehjurfkhlj[17];
extern const uint8_t gm_saamkuuf6jrtf[8];
extern const uint8_t gm_saco8uvrm6fd3[4];
extern const uint8_t gm_saf3o40admn10[25];
extern const uint8_t gm_sag4ku07t2k8t[25];
extern const uint8_t gm_sai6qda00m87d[49];
extern const uint8_t gm_sal52ei1j638k[17];
extern const uint8_t gm_saqb7i299utsd[18];
extern const uint8_t gm_sars0lfrdmiq5[26];
extern const uint8_t gm_sat7ifv94mb2p[40];
extern const uint8_t gm_sav0q47tkm7j2[20];
extern const uint8_t gm_sb20a9eb6tt9l[50];
extern const uint8_t gm_sb398tg9q30m5[6];
extern const uint8_t gm_sb658vbovt6jg[37];
extern const uint8_t gm_sb9dglh7ekrt2[7];
extern const uint8_t gm_sba439nre06r4[6];
extern const uint8_t gm_sbbgtkch3q4n5[3];
extern const uint8_t gm_sbdlpmcdardns[7];
extern const uint8_t gm_sbfc2b8kquvq8[9];
extern const uint8_t gm_sbfftl64c12sl[13];
extern const uint8_t gm_sbicj451tu4qm[4];
extern const uint8_t gm_sbjdqrlfpuoqj[4];
extern const uint8_t gm_sbjiv038rtlcj[4];
extern const uint8_t gm_sbpf3hgo18oq5[52];
extern const uint8_t gm_sbqbp3u228b97[30];
extern const uint8_t gm_sbr6raqk787l2[10];
extern const uint8_t gm_sbse0oko82bp9[42];
extern const uint8_t gm_sbsurq5b3g8na[22];
extern const uint8_t gm_sbtnbgj3h592u[19];
extern const uint8_t gm_sbvmilijbiuka[26];
extern const uint8_t gm_sbvo1o52epaqq[4];
extern const uint8_t gm_sc003f3fvnij4[17];
extern const uint8_t gm_sc03pp1oj3q4k[7];
extern const uint8_t gm_sc2dqfde3kb47[10];
extern const uint8_t gm_sc37tjaf38s71[9];
extern const uint8_t gm_sc4uc8von90he[6];
extern const uint8_t gm_sc5p6b8d5c87a[13];
extern const uint8_t gm_scai7a7ojvpjh[19];
extern const uint8_t gm_sciqh24mne54a[4];
extern const uint8_t gm_scj2bnbp1mk6t[41];
extern const uint8_t gm_scj5qlamj2bll[39];
extern const uint8_t gm_scl69k9ct9m19[16];
extern const uint8_t gm_scldd26k6fur2[38];
extern const uint8_t gm_scnb9tl13g4lt[8];
extern const uint8_t gm_scqrlin5l437v[9];
extern const uint8_t gm_scr60nbumsj2h[25];
extern const uint8_t gm_scrq1ngg50bjh[41];
extern const uint8_t gm_scvb80fgq305s[11];
extern const uint8_t gm_sd1ldutuv1qfu[12];
extern const uint8_t gm_sd1smnnii4cig[7];
extern const uint8_t gm_sd25v201ev3d4[30];
extern const uint8_t gm_sd3a17sd7gp5u[9];
extern const uint8_t gm_sd99er7oafmtu[17];
extern const uint8_t gm_sde2pekt9oc5v[4];
extern const uint8_t gm_sdhlphf1ucpbo[18];
extern const uint8_t gm_sdi58fds59v3t[30];
extern const uint8_t gm_sdj9p83jifn86[41];
extern const uint8_t gm_sdjp1iiokf034[25];
extern const uint8_t gm_sdko2shp3619i[4];
extern const uint8_t gm_sdpig9sk6ngdh[7];
extern const uint8_t gm_sdpmg51hc7l8e[13];
extern const uint8_t gm_sdq31tldc1ff5[20];
extern const uint8_t gm_sdslahffv7339[17];
extern const uint8_t gm_sdtjos7av9cn7[22];
extern const uint8_t gm_sduaucpbcr6v5[69];
extern const uint8_t gm_sduk7s8m3amo6[12];
extern const uint8_t gm_sdvlm6i5s2j37[23];
extern const uint8_t gm_se01ng9cs6jft[7];
extern const uint8_t gm_se24c7hpp401n[11];
extern const uint8_t gm_se4r520bkfair[18];
extern const uint8_t gm_se61bv3cn2p2r[18];
extern const uint8_t gm_se62v1mjbig2r[21];
extern const uint8_t gm_secrjke11f3ph[13];
extern const uint8_t gm_sed3pofb5bu9b[26];
extern const uint8_t gm_sedq394prkqe4[10];
extern const uint8_t gm_sefndhhn10fqn[15];
extern const uint8_t gm_seg4idqqmijmd[34];
extern const uint8_t gm_sehat3g6ic3fn[50];
extern const uint8_t gm_sehf1ajl5jbrm[6];
extern const uint8_t gm_sekk5er5cide1[12];
extern const uint8_t gm_sekndapa5m95n[12];
extern const uint8_t gm_sel0tu7gvv1na[4];
extern const uint8_t gm_sellfthspl9li[9];
extern const uint8_t gm_sep8ve6ec8tv7[24];
extern const uint8_t gm_sf24jo61ba8vn[23];
extern const uint8_t gm_sf35nlocjg6at[41];
extern const uint8_t gm_sf4cn9nvrjhp1[15];
extern const uint8_t gm_sf5brl3u7mr5h[7];
extern const uint8_t gm_sf6i3pjks8s6u[21];
extern const uint8_t gm_sf9a2kg3eo1gs[2];
extern const uint8_t gm_sfad5qbegckoi[28];
extern const uint8_t gm_sfch9mo7lqp8l[3];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sfg8mkf2826qa[5];
extern const uint8_t gm_sfif278eeflhm[4];
extern const uint8_t gm_sfm0hseeqersg[25];
extern const uint8_t gm_sfm9elkq6bn2e[2];
extern const uint8_t gm_sfnph9ql8pqb1[21];
extern const uint8_t gm_sfo6sal7rn2g3[12];
extern const uint8_t gm_sfpnp190uvmtl[59];
extern const uint8_t gm_sfq6b6v45hv1v[64];
extern const uint8_t gm_sfsrjkeonr0ad[51];
extern const uint8_t gm_sfv26hkjngt1i[6];
extern const uint8_t gm_sg0jqm2pij3el[78];
extern const uint8_t gm_sg12s7bns50dg[201];
extern const uint8_t gm_sg3acic2chh26[34];
extern const uint8_t gm_sg4u1qvcufkum[5];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sg6iftc6c3tph[47];
extern const uint8_t gm_sg7q4ebvc557r[14];
extern const uint8_t gm_sg82mujgottmf[25];
extern const uint8_t gm_sg8f7kt1n4uc5[48];
extern const uint8_t gm_sg9f7nkpuurvt[74];
extern const uint8_t gm_sga6cf3g037hp[8];
extern const uint8_t gm_sgankvlme129s[3];
extern const uint8_t gm_sgeslrkf93f2d[18];
extern const uint8_t gm_sggm36eg9tkga[10];
extern const uint8_t gm_sgh1o65rai9bu[8];
extern const uint8_t gm_sgh4cf979ohjn[11];
extern const uint8_t gm_sgie3las787c9[31];
extern const uint8_t gm_sglgjrjci6t1d[25];
extern const uint8_t gm_sgq8hfs47hogb[9];
extern const uint8_t gm_sgspc9im84cfh[25];
extern const uint8_t gm_sgur19lpnttci[7];
extern const uint8_t gm_sgvnpogt67see[6];
extern const uint8_t gm_sh4b8t54h2jna[49];
extern const uint8_t gm_sh5o336tlj1uf[9];
extern const uint8_t gm_sh5qb2l9hubh3[9];
extern const uint8_t gm_sh7dbt4gdrljc[19];
extern const uint8_t gm_sh7dg9dli11hk[50];
extern const uint8_t gm_shecus79pkigk[19];
extern const uint8_t gm_shem79vgdue54[5];
extern const uint8_t gm_sheokv0g7eanf[50];
extern const uint8_t gm_shhlfsve4gvoq[72];
extern const uint8_t gm_shlds5pd69pfn[16];
extern const uint8_t gm_shmmkvp83pr05[18];
extern const uint8_t gm_shn4b04riimu9[14];
extern const uint8_t gm_shp2btrg6e14g[29];
extern const uint8_t gm_shsifme8hcenm[17];
extern const uint8_t gm_sht5iivcbi64u[12];
extern const uint8_t gm_shv4avtqvfr4p[6];
extern const uint8_t gm_shvak64vhokft[26];
extern const uint8_t gm_si2ks4lbjbojr[22];
extern const uint8_t gm_si416c0mdacrd[5];
extern const uint8_t gm_si44h5pbes4bk[41];
extern const uint8_t gm_si7qrooamq5ei[26];
extern const uint8_t gm_siamquc19uikr[2];
extern const uint8_t gm_sic6qqim15h1p[15];
extern const uint8_t gm_sil0do0eri7f7[44];
extern const uint8_t gm_silop0tio8u7e[105];
extern const uint8_t gm_sio6bogdhis0p[2];
extern const uint8_t gm_sio9tu2ig98ap[21];
extern const uint8_t gm_siqaapb0mqsrg[14];
extern const uint8_t gm_sitfp4vhvj1ba[72];
extern const uint8_t gm_situapcg7atp6[3];
extern const uint8_t gm_sj6ih18t5qqaf[36];
extern const uint8_t gm_sj70rum4dd30i[18];
extern const uint8_t gm_sj7qnjnqu5d9l[5];
extern const uint8_t gm_sj7sbm8e8u97n[5];
extern const uint8_t gm_sjbtn8nt9amae[3];
extern const uint8_t gm_sjcm18p35o9rq[4];
extern const uint8_t gm_sjeopa747clpb[4];
extern const uint8_t gm_sjeqed5d3f5pb[26];
extern const uint8_t gm_sjf6sle93kf3b[18];
extern const uint8_t gm_sjfdnh735o7f8[5];
extern const uint8_t gm_sjfrmur8rvp24[34];
extern const uint8_t gm_sjh6cl9hs66k6[37];
extern const uint8_t gm_sjhi85r5ki9g5[23];
extern const uint8_t gm_sjls1ivhf3eiu[12];
extern const uint8_t gm_sjltai3b82h79[4];
extern const uint8_t gm_sjm4g8bt7kk0q[41];
extern const uint8_t gm_sjnkguo0atpm5[3];
extern const uint8_t gm_sjsokj7ll1qcv[76];
extern const uint8_t gm_sk0g3po7mv29v[12];
extern const uint8_t gm_sk0kqh2pa96ut[73];
extern const uint8_t gm_sk0r84vdcvmnk[13];
extern const uint8_t gm_sk16sqfpds4ga[18];
extern const uint8_t gm_sk175hsmu7b99[22];
extern const uint8_t gm_sk2oomiuvp44g[6];
extern const uint8_t gm_sk2sae8qmnmbb[31];
extern const uint8_t gm_sk30h53vgth78[23];
extern const uint8_t gm_sk6889kd59up6[60];
extern const uint8_t gm_sk89mcfpevgf2[16];
extern const uint8_t gm_sk8j8qotqngd0[41];
extern const uint8_t gm_skfniirr0r2gc[63];
extern const uint8_t gm_skfo9c0nr3nrd[50];
extern const uint8_t gm_skhcthij717g7[11];
extern const uint8_t gm_skm48m2qkkghv[116];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_skpl66e1qvhab[8];
extern const uint8_t gm_skqf89fdi6p8e[12];
extern const uint8_t gm_skr3v6bd9t3ir[59];
extern const uint8_t gm_skr5vsm3evt1r[36];
extern const uint8_t gm_skr89ndurop26[113];
extern const uint8_t gm_skrj4540qohca[37];
extern const uint8_t gm_sktf16uersaon[9];
extern const uint8_t gm_sl0kpeuro9al4[23];
extern const uint8_t gm_sl0n65k3lfps6[18];
extern const uint8_t gm_sl1botv4tp1fk[58];
extern const uint8_t gm_sl3acq1nje8kk[39];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_sl4ilt8nbcp28[15];
extern const uint8_t gm_sl9dbf0dicmme[39];
extern const uint8_t gm_slcnede7c2i4t[34];
extern const uint8_t gm_sldjvqvpa1t8p[16];
extern const uint8_t gm_sle6a93dn88l8[26];
extern const uint8_t gm_slg11fhjrk2f5[9];
extern const uint8_t gm_slgmqgkioa4u6[3];
extern const uint8_t gm_slhaa12fa8am5[24];
extern const uint8_t gm_sljj5lb6g47i1[38];
extern const uint8_t gm_slk4m2cppcp75[23];
extern const uint8_t gm_sllk0j4if03tr[11];
extern const uint8_t gm_slm25hfg2udo4[8];
extern const uint8_t gm_slq58vfha8k9h[2];
extern const uint8_t gm_slstf59uevcv9[9];
extern const uint8_t gm_slvvpj912a5bg[4];
extern const uint8_t gm_sm1a6faj4q6vl[41];
extern const uint8_t gm_sm5752b5mit4p[13];
extern const uint8_t gm_sm6ioruobq064[6];
extern const uint8_t gm_sm7emfdvcb7c2[3];
extern const uint8_t gm_sm7odqe0d6lgu[23];
extern const uint8_t gm_smaq5hrff6a5d[56];
extern const uint8_t gm_smb1apd5lk8bd[11];
extern const uint8_t gm_smdb1h8uhneg6[50];
extern const uint8_t gm_smdscldu1a8hg[42];
extern const uint8_t gm_smdv045psk1u8[25];
extern const uint8_t gm_smeu8adf6pdko[38];
extern const uint8_t gm_smi31ifpstnmf[9];
extern const uint8_t gm_smjth8dldg6d0[59];
extern const uint8_t gm_smnk1cmb990al[7];
extern const uint8_t gm_smom6v4b4s28c[52];
extern const uint8_t gm_smooaouqmm8dj[2];
extern const uint8_t gm_smpl91sh0k8v4[9];
extern const uint8_t gm_smror2m9tsk7v[8];
extern const uint8_t gm_smuluqdaf5qoe[9];
extern const uint8_t gm_smv2mfn1ji3ad[58];
extern const uint8_t gm_sn0kh110541oo[23];
extern const uint8_t gm_sn1j4m1p1cnm1[83];
extern const uint8_t gm_sn1seggk13bop[4];
extern const uint8_t gm_sn3ltks10rrv7[8];
extern const uint8_t gm_sn46ieaokgrck[10];
extern const uint8_t gm_sn57mt8igjdaa[5];
extern const uint8_t gm_sn674iiiekfov[74];
extern const uint8_t gm_sn7tl9jolqoj1[3];
extern const uint8_t gm_sn8c4o9a7lthb[28];
extern const uint8_t gm_sn916351kmsu6[36];
extern const uint8_t gm_sn9b7u92b8vpk[15];
extern const uint8_t gm_sna4vk4o059d0[10];
extern const uint8_t gm_snbque26o7lbi[7];
extern const uint8_t gm_sncgl1t0uiqr4[17];
extern const uint8_t gm_sneh1858d1bko[20];
extern const uint8_t gm_snge4b9tsq8qc[8];
extern const uint8_t gm_sngj7pcs9ksq7[8];
extern const uint8_t gm_snkldl7hr4htt[4];
extern const uint8_t gm_snl2n9aactg8i[41];
extern const uint8_t gm_snl36eh5psull[7];
extern const uint8_t gm_sno069vrt5g9q[4];
extern const uint8_t gm_snpp7pgdikfns[10];
extern const uint8_t gm_snrndjerbtt58[6];
extern const uint8_t gm_sns1hl83hdlcn[5];
extern const uint8_t gm_snv7mc8ntub24[48];
extern const uint8_t gm_so1ub93q801tl[65];
extern const uint8_t gm_so2okcd9d9cgi[53];
extern const uint8_t gm_so4g53vn8c2d4[8];
extern const uint8_t gm_so4p5vea9o316[10];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_so70siafe1e7a[13];
extern const uint8_t gm_so8oee2j1e9l0[6];
extern const uint8_t gm_so99jjk23jgg8[5];
extern const uint8_t gm_so9c50giqp3gn[6];
extern const uint8_t gm_so9fi6r28770a[3];
extern const uint8_t gm_so9ldq1i41cj2[19];
extern const uint8_t gm_soajcqnnsnidh[57];
extern const uint8_t gm_soaukr1udufqk[19];
extern const uint8_t gm_socbkiss431o8[18];
extern const uint8_t gm_sod9r7r024h9q[6];
extern const uint8_t gm_sojpmv6gl0jac[12];
extern const uint8_t gm_sokap7vn66boo[53];
extern const uint8_t gm_sopbumhmsvcgf[10];
extern const uint8_t gm_sor30tetra4nt[5];
extern const uint8_t gm_soso4l0tj1pgt[12];
extern const uint8_t gm_sp0ce5j43v7u2[26];
extern const uint8_t gm_sp1nce97af2b9[6];
extern const uint8_t gm_sp2cos6rmlsbv[51];
extern const uint8_t gm_sp66nadh171v3[19];
extern const uint8_t gm_sp6eh19rmca8b[36];
extern const uint8_t gm_sp920qd66qq8t[20];
extern const uint8_t gm_sp9ailodohkkj[40];
extern const uint8_t gm_spa4t2l419v39[14];
extern const uint8_t gm_spec9er6f839v[17];
extern const uint8_t gm_spgd6qbgum6c8[25];
extern const uint8_t gm_spgt2nbblhu8j[9];
extern const uint8_t gm_spicth2mans91[44];
extern const uint8_t gm_spmkn6krbjnjh[10];
extern const uint8_t gm_spnnqfjs7id3o[13];
extern const uint8_t gm_spod3tmkh25up[2];
extern const uint8_t gm_spoh8ft9n8oqt[4];
extern const uint8_t gm_spp2faaelnnt0[8];
extern const uint8_t gm_spp2t9pj35hcq[7];
extern const uint8_t gm_spvk46905osdv[32];
extern const uint8_t gm_sq3gi0chb3cs2[3];
extern const uint8_t gm_sq43o0crfp05h[18];
extern const uint8_t gm_sq44fbs78td4m[33];
extern const uint8_t gm_sq4a10jk2jerj[51];
extern const uint8_t gm_sq53ajg2dhrbb[49];
extern const uint8_t gm_sq58pm227uuk8[53];
extern const uint8_t gm_sq6v0u0m2nfv8[23];
extern const uint8_t gm_sq964a86o46ph[17];
extern const uint8_t gm_sqadouunp729f[67];
extern const uint8_t gm_sqaji2d91cvct[19];
extern const uint8_t gm_sqgbsg6a0o87i[32];
extern const uint8_t gm_sqi8fcv4bi7s6[10];
extern const uint8_t gm_sqjh8kseu4lti[6];
extern const uint8_t gm_sqk4m1hc1n4b5[49];
extern const uint8_t gm_sql9r2b8fvivt[10];
extern const uint8_t gm_sqmf3k0nnjf4r[4];
extern const uint8_t gm_sqmpncmbmfe90[8];
extern const uint8_t gm_sqn4pcenn3reh[63];
extern const uint8_t gm_sqp133sovb148[25];
extern const uint8_t gm_sqrd7hn8ek7nr[11];
extern const uint8_t gm_sqrfl54mgc34i[8];
extern const uint8_t gm_sqrh6i8djv6b4[7];
extern const uint8_t gm_sqrjr8qjf37v8[17];
extern const uint8_t gm_sqrv416075ito[3];
extern const uint8_t gm_sqs6r7f5gl2vu[26];
extern const uint8_t gm_sqt8gk1nsci25[5];
extern const uint8_t gm_squr90ahaf9ht[13];
extern const uint8_t gm_sqvepl1893o5g[3];
extern const uint8_t gm_sqvgouubrgndv[14];
extern const uint8_t gm_sr29ed60si4re[7];
extern const uint8_t gm_sr2df1mc0vbea[4];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const uint8_t gm_sr5nrafa4mlm6[19];
extern const uint8_t gm_sr64vq6s9ci7q[35];
extern const uint8_t gm_sr7qn5gtrg5md[114];
extern const uint8_t gm_sr8du5b2u9c7v[33];
extern const uint8_t gm_sr8dujibvqj2g[82];
extern const uint8_t gm_srblss12j199i[25];
extern const char gm_src3m73omiakl12[];
extern const char gm_src3nt8pf0qk5gn[];
extern const char gm_src5bc4unh0r6k6[];
extern const char gm_src6c128h826nsg[];
extern const char gm_src6lm8esmv16p1[];
extern const char gm_srcb2hdor4f6g0q[];
extern const char gm_srcb7e7uf3pvdko[];
extern const char gm_srcbu4asq8trfta[];
extern const char gm_srcckqm4prfktot[];
extern const char gm_srcddjnq2mhhonr[];
extern const char gm_srch2l5nl06vsci[];
extern const char gm_srchntnl6m6g3am[];
extern const char gm_srcmj20te78r22f[];
extern const char gm_srcpmlh93537vbl[];
extern const uint8_t gm_sre5oq1um1gnv[17];
extern const uint8_t gm_srefpimq1sas7[4];
extern const uint8_t gm_srhs1fn12neea[33];
extern const uint8_t gm_srjc1gt18ujm5[29];
extern const uint8_t gm_srk8hgivpmv60[18];
extern const uint8_t gm_srn86a84qh2td[55];
extern const uint8_t gm_srne5d503fcjg[5];
extern const uint8_t gm_sruees1a7v0hs[47];
extern const uint8_t gm_srv4dcfccuset[3];
extern const uint8_t gm_ss3tu1f38tkcj[6];
extern const uint8_t gm_ss5hh7jobk2ar[5];
extern const uint8_t gm_ss6qrj79r47rb[70];
extern const uint8_t gm_ss88mk7b1rh87[18];
extern const uint8_t gm_ssbcl1ouchrlk[39];
extern const uint8_t gm_ssc48vnvhmmnh[9];
extern const uint8_t gm_sshlmlg0kjl8a[19];
extern const uint8_t gm_ssigpbgi88it3[4];
extern const uint8_t gm_ssjhh9bdn23jf[17];
extern const uint8_t gm_ssnu9a3et1ilt[6];
extern const uint8_t gm_sso11c17kdal7[35];
extern const uint8_t gm_sso181di0dbb3[10];
extern const uint8_t gm_sso1h04lhm0cu[32];
extern const uint8_t gm_sso47q408qu33[24];
extern const uint8_t gm_sspb2rj2e032s[13];
extern const uint8_t gm_st0l9v6pp5i7b[14];
extern const uint8_t gm_st5bsv3b3beg9[22];
extern const uint8_t gm_st6hj8v0h80gt[6];
extern const uint8_t gm_st6jo8d16th86[41];
extern const uint8_t gm_st6to3kuud2ge[46];
extern const uint8_t gm_st7fm661apctp[17];
extern const uint8_t gm_st7j9rqo01n3i[17];
extern const uint8_t gm_st8i5uhn201op[37];
extern const uint8_t gm_stbtfovn5dhm4[5];
extern const uint8_t gm_stc3e0j0moc2h[5];
extern const uint8_t gm_stc6hgo9kskc3[8];
extern const uint8_t gm_stgfndqo1l714[13];
extern const uint8_t gm_stjafkt8gs4e7[13];
extern const uint8_t gm_stjfbh3ag5r41[52];
extern const uint8_t gm_stkm2m8f3okk8[18];
extern const uint8_t gm_stmsadvf54k73[3];
extern const uint8_t gm_sto98il7spahj[73];
extern const uint8_t gm_stol2i9gi9a35[18];
extern const uint8_t gm_sts2e9lggtpr2[47];
extern const uint8_t gm_stt02csqvk3dh[41];
extern const uint8_t gm_stucn95m8hs6h[9];
extern const uint8_t gm_su03its4kclfl[10];
extern const uint8_t gm_su274du674a98[38];
extern const uint8_t gm_su2c9m69n1vjb[31];
extern const uint8_t gm_su3gef13fgq4c[7];
extern const uint8_t gm_su5bsj1nioctr[22];
extern const uint8_t gm_su6mi500md00b[7];
extern const uint8_t gm_su70l0sh894sa[33];
extern const uint8_t gm_su7f9sbon2vv7[6];
extern const uint8_t gm_suapcbuahu4i6[24];
extern const uint8_t gm_sufk3vqnaden1[3];
extern const uint8_t gm_sug64ltbhov0m[19];
extern const uint8_t gm_suh21tg0nqmfn[10];
extern const uint8_t gm_suhmdi1gd6hta[7];
extern const uint8_t gm_suiq4ou0s86ho[14];
extern const uint8_t gm_sujpen8u5kmon[10];
extern const uint8_t gm_sujqb5peare4g[6];
extern const uint8_t gm_sulgkejn2inql[7];
extern const uint8_t gm_suls9doo5jh0i[11];
extern const uint8_t gm_sun4qq3op5opf[7];
extern const uint8_t gm_sup81hi2h3911[9];
extern const uint8_t gm_sup9af88eqfm1[25];
extern const uint8_t gm_suq2iqmhi931e[3];
extern const uint8_t gm_sut7mhrkhqp77[56];
extern const uint8_t gm_sut92jpq3kk88[11];
extern const uint8_t gm_suvh4o70k4cg3[28];
extern const uint8_t gm_sv242olhg8pke[13];
extern const uint8_t gm_sv394jlms97cd[14];
extern const uint8_t gm_sv3o54ps0ppci[19];
extern const uint8_t gm_sv61f25m0b9gs[4];
extern const uint8_t gm_sv6o879d2okh4[39];
extern const uint8_t gm_sv7ptc4il3v0u[15];
extern const uint8_t gm_sv89nb1qmpoaj[8];
extern const uint8_t gm_sv8s0ogr3iflj[8];
extern const uint8_t gm_svacokl3cteo0[26];
extern const uint8_t gm_svdd37ikql4fk[45];
extern const uint8_t gm_svftrp7hvam92[5];
extern const uint8_t gm_svhavo13qha5k[4];
extern const uint8_t gm_svhh21g5q0quu[25];
extern const uint8_t gm_svhvj3h6g939d[66];
extern const uint8_t gm_svkvl31jmj9ne[12];
extern const uint8_t gm_svpofgjtij7vj[4];
extern const uint8_t gm_svu4pa4qq20q7[6];
extern const uint8_t gm_svua787vprkp6[8];
extern const uint32_t gm_tpl00b67aeoiuop_i[];
extern const gm_spec *const gm_tpl00nmcu166crh_s[];
extern const gm_tmpl_piece gm_tpl00rv7vuig9ur_p[];
extern const gm_spec *const gm_tpl00rv7vuig9ur_s[];
extern const uint32_t gm_tpl051t251mssfu_i[];
extern const gm_spec *const gm_tpl0e4rd6ag9tqa_s[];
extern const gm_spec *const gm_tpl0e782j7cr191_s[];
extern const gm_tmpl gm_tpl0elencma14kh;
extern const uint32_t gm_tpl0elencma14kh_i[];
extern const gm_tmpl_piece gm_tpl0elencma14kh_p[];
extern const gm_spec *const gm_tpl0elencma14kh_s[];
extern const uint32_t gm_tpl0fdke0kkivcj_i[];
extern const gm_tmpl gm_tpl0j5bgvjcbn0u;
extern const uint32_t gm_tpl0j5bgvjcbn0u_i[];
extern const gm_tmpl_piece gm_tpl0j5bgvjcbn0u_p[];
extern const gm_spec *const gm_tpl0j5bgvjcbn0u_s[];
extern const gm_tmpl_piece gm_tpl0j5i33et5u62_p[];
extern const gm_tmpl_piece gm_tpl0lggla435q5r_p[];
extern const gm_spec *const gm_tpl0oli8trq10sp_s[];
extern const gm_tmpl_piece gm_tpl0pva6pvhuq6i_p[];
extern const gm_tmpl_piece gm_tpl0q3rsndpu857_p[];
extern const uint32_t gm_tpl0qht8d5me9ih_i[];
extern const gm_tmpl_piece gm_tpl0v0dinrpdv6d_p[];
extern const gm_tmpl gm_tpl10tenob4hk5k;
extern const uint32_t gm_tpl10tenob4hk5k_i[];
extern const gm_tmpl_piece gm_tpl10tenob4hk5k_p[];
extern const gm_spec *const gm_tpl10tenob4hk5k_s[];
extern const gm_tmpl gm_tpl128vde47jcsu;
extern const uint32_t gm_tpl128vde47jcsu_i[];
extern const gm_tmpl_piece gm_tpl128vde47jcsu_p[];
extern const gm_spec *const gm_tpl128vde47jcsu_s[];
extern const uint32_t gm_tpl12e8u6hvkejb_i[];
extern const gm_spec *const gm_tpl13c887f2o944_s[];
extern const uint32_t gm_tpl14b6qietv6bm_i[];
extern const uint32_t gm_tpl14n6ndgi62j1_i[];
extern const gm_tmpl_piece gm_tpl150n44984e0j_p[];
extern const gm_tmpl_piece gm_tpl15v8rvh46nek_p[];
extern const gm_spec *const gm_tpl15v8rvh46nek_s[];
extern const uint32_t gm_tpl17l9mok2be4a_i[];
extern const gm_spec *const gm_tpl1a3usdkusvtq_s[];
extern const gm_tmpl gm_tpl1buo4rn7srlb;
extern const uint32_t gm_tpl1buo4rn7srlb_i[];
extern const gm_tmpl_piece gm_tpl1buo4rn7srlb_p[];
extern const gm_spec *const gm_tpl1buo4rn7srlb_s[];
extern const gm_tmpl gm_tpl1dmvfuuebopd;
extern const uint32_t gm_tpl1dmvfuuebopd_i[];
extern const gm_tmpl_piece gm_tpl1dmvfuuebopd_p[];
extern const gm_spec *const gm_tpl1dmvfuuebopd_s[];
extern const gm_tmpl gm_tpl1f5t4gadpj5n;
extern const uint32_t gm_tpl1f5t4gadpj5n_i[];
extern const gm_tmpl_piece gm_tpl1f5t4gadpj5n_p[];
extern const gm_spec *const gm_tpl1f5t4gadpj5n_s[];
extern const gm_tmpl_piece gm_tpl1g7tc1f10noh_p[];
extern const gm_spec *const gm_tpl1gtefnja08ps_s[];
extern const gm_tmpl gm_tpl1ige45obm306;
extern const uint32_t gm_tpl1ige45obm306_i[];
extern const gm_tmpl_piece gm_tpl1ige45obm306_p[];
extern const gm_spec *const gm_tpl1ige45obm306_s[];
extern const gm_tmpl_piece gm_tpl1irt5kietk82_p[];
extern const uint32_t gm_tpl1jgrbdjt2sm3_i[];
extern const gm_spec *const gm_tpl1kmako3cfauf_s[];
extern const uint32_t gm_tpl1lmdkbkeigtd_i[];
extern const gm_tmpl gm_tpl1rfj3tqlepqo;
extern const uint32_t gm_tpl1rfj3tqlepqo_i[];
extern const gm_tmpl_piece gm_tpl1rfj3tqlepqo_p[];
extern const gm_spec *const gm_tpl1rfj3tqlepqo_s[];
extern const gm_spec *const gm_tpl1sdu3l536hkg_s[];
extern const gm_spec *const gm_tpl1u24a06iag0n_s[];
extern const gm_spec *const gm_tpl21i1s6vvb3m4_s[];
extern const gm_tmpl_piece gm_tpl29pahnav2feq_p[];
extern const uint32_t gm_tpl2d7ij9b2sa2u_i[];
extern const gm_tmpl gm_tpl2n8jqp2rhn52;
extern const uint32_t gm_tpl2n8jqp2rhn52_i[];
extern const gm_tmpl_piece gm_tpl2n8jqp2rhn52_p[];
extern const gm_spec *const gm_tpl2n8jqp2rhn52_s[];
extern const gm_spec *const gm_tpl2u97uspakqd9_s[];
extern const gm_tmpl_piece gm_tpl3052vn0bjbcl_p[];
extern const gm_tmpl_piece gm_tpl3072uo9s1dtn_p[];
extern const gm_spec *const gm_tpl31lgghh3mal3_s[];
extern const gm_tmpl_piece gm_tpl34g57qfcenfh_p[];
extern const gm_tmpl_piece gm_tpl3cldid2v21cr_p[];
extern const uint32_t gm_tpl3lqpj3d2egtb_i[];
extern const gm_tmpl gm_tpl3m6tjsa4flq8;
extern const uint32_t gm_tpl3m6tjsa4flq8_i[];
extern const gm_tmpl_piece gm_tpl3m6tjsa4flq8_p[];
extern const gm_spec *const gm_tpl3m6tjsa4flq8_s[];
extern const gm_tmpl gm_tpl3sg21sl0o56d;
extern const uint32_t gm_tpl3sg21sl0o56d_i[];
extern const gm_tmpl_piece gm_tpl3sg21sl0o56d_p[];
extern const gm_spec *const gm_tpl3sg21sl0o56d_s[];
extern const gm_tmpl gm_tpl3skngiao4gkl;
extern const uint32_t gm_tpl3skngiao4gkl_i[];
extern const gm_tmpl_piece gm_tpl3skngiao4gkl_p[];
extern const gm_spec *const gm_tpl3skngiao4gkl_s[];
extern const gm_spec *const gm_tpl3ttr7njhqa54_s[];
extern const gm_tmpl_piece gm_tpl4bpbmadpmu1i_p[];
extern const gm_tmpl_piece gm_tpl4g1h0tr3apfi_p[];
extern const gm_tmpl_piece gm_tpl4s7g8d4v4djl_p[];
extern const uint32_t gm_tpl4vjhulqo9e1j_i[];
extern const gm_tmpl_piece gm_tpl4vjhulqo9e1j_p[];
extern const gm_tmpl_piece gm_tpl500h9j75csqa_p[];
extern const gm_spec *const gm_tpl51u3ki3ilqou_s[];
extern const gm_tmpl_piece gm_tpl55mhnmub0ffl_p[];
extern const gm_tmpl_piece gm_tpl59r7h6035548_p[];
extern const gm_spec *const gm_tpl59r7h6035548_s[];
extern const gm_spec *const gm_tpl5cgoa6ddohcv_s[];
extern const uint32_t gm_tpl5h4mc65ti3jt_i[];
extern const gm_spec *const gm_tpl5hatm6q530pn_s[];
extern const uint32_t gm_tpl5hraomijgptv_i[];
extern const gm_tmpl gm_tpl5ikuibflsuu2;
extern const uint32_t gm_tpl5ikuibflsuu2_i[];
extern const gm_tmpl_piece gm_tpl5ikuibflsuu2_p[];
extern const gm_spec *const gm_tpl5ikuibflsuu2_s[];
extern const gm_tmpl gm_tpl5ldtdub2029b;
extern const uint32_t gm_tpl5ldtdub2029b_i[];
extern const gm_tmpl_piece gm_tpl5ldtdub2029b_p[];
extern const gm_spec *const gm_tpl5ldtdub2029b_s[];
extern const uint32_t gm_tpl5m2t8us2v800_i[];
extern const gm_spec *const gm_tpl5q4rr3nd9637_s[];
extern const gm_tmpl_piece gm_tpl5qgrl10qvakk_p[];
extern const gm_spec *const gm_tpl5qkek6smv4rg_s[];
extern const uint32_t gm_tpl5qm9nkkj1gjg_i[];
extern const gm_tmpl gm_tpl5rd4283boomi;
extern const uint32_t gm_tpl5rd4283boomi_i[];
extern const gm_tmpl_piece gm_tpl5rd4283boomi_p[];
extern const gm_spec *const gm_tpl5rd4283boomi_s[];
extern const gm_spec *const gm_tpl5svfna5ij5ui_s[];
extern const gm_tmpl gm_tpl5uqj3jbiq5f0;
extern const uint32_t gm_tpl5uqj3jbiq5f0_i[];
extern const gm_tmpl_piece gm_tpl5uqj3jbiq5f0_p[];
extern const gm_spec *const gm_tpl5uqj3jbiq5f0_s[];
extern const gm_tmpl gm_tpl5vbp9fc0mpc1;
extern const uint32_t gm_tpl5vbp9fc0mpc1_i[];
extern const gm_tmpl_piece gm_tpl5vbp9fc0mpc1_p[];
extern const gm_spec *const gm_tpl5vbp9fc0mpc1_s[];
extern const gm_tmpl_piece gm_tpl606t7k5n88ss_p[];
extern const gm_spec *const gm_tpl60na9lt7qpkj_s[];
extern const gm_spec *const gm_tpl61n2hscg0et2_s[];
extern const gm_spec *const gm_tpl6bjoajp2fh2n_s[];
extern const uint32_t gm_tpl6d7dqphpmhs7_i[];
extern const gm_tmpl_piece gm_tpl6fas3mugop2g_p[];
extern const uint32_t gm_tpl6fm59pne5ng7_i[];
extern const gm_spec *const gm_tpl6jo4mmkdq2l1_s[];
extern const uint32_t gm_tpl6lldue1mm65c_i[];
extern const gm_tmpl gm_tpl6lpvgmmqlalm;
extern const uint32_t gm_tpl6lpvgmmqlalm_i[];
extern const gm_tmpl_piece gm_tpl6lpvgmmqlalm_p[];
extern const gm_spec *const gm_tpl6lpvgmmqlalm_s[];
extern const gm_tmpl_piece gm_tpl6muahgpe70pr_p[];
extern const gm_spec *const gm_tpl6n3s5f3ittsi_s[];
extern const gm_tmpl_piece gm_tpl6ou7bec1fkae_p[];
extern const gm_spec *const gm_tpl6ptbf3l6su1b_s[];
extern const uint32_t gm_tpl6rsespo9a6nq_i[];
extern const gm_spec *const gm_tpl6rsespo9a6nq_s[];
extern const gm_spec *const gm_tpl6s7ufou2uts6_s[];
extern const uint32_t gm_tpl70tkged35ahl_i[];
extern const gm_spec *const gm_tpl737bm31hf71o_s[];
extern const gm_spec *const gm_tpl75bcbum1ntsg_s[];
extern const gm_tmpl gm_tpl7avuc618ph2c;
extern const uint32_t gm_tpl7avuc618ph2c_i[];
extern const gm_tmpl_piece gm_tpl7avuc618ph2c_p[];
extern const gm_spec *const gm_tpl7avuc618ph2c_s[];
extern const gm_spec *const gm_tpl7da8g7lhjmr1_s[];
extern const gm_spec *const gm_tpl7eehu7c1eqka_s[];
extern const gm_tmpl gm_tpl7ll3166bkl17;
extern const uint32_t gm_tpl7ll3166bkl17_i[];
extern const gm_tmpl_piece gm_tpl7ll3166bkl17_p[];
extern const gm_spec *const gm_tpl7ll3166bkl17_s[];
extern const uint32_t gm_tpl7net9ifts13e_i[];
extern const gm_spec *const gm_tpl7sqjangpsngf_s[];
extern const gm_tmpl_piece gm_tpl7vtkjsp2pa8b_p[];
extern const gm_tmpl gm_tpl81nh98hgo1rk;
extern const uint32_t gm_tpl81nh98hgo1rk_i[];
extern const gm_tmpl_piece gm_tpl81nh98hgo1rk_p[];
extern const gm_spec *const gm_tpl81nh98hgo1rk_s[];
extern const gm_tmpl gm_tpl8eifhruu514e;
extern const uint32_t gm_tpl8eifhruu514e_i[];
extern const gm_tmpl_piece gm_tpl8eifhruu514e_p[];
extern const gm_spec *const gm_tpl8eifhruu514e_s[];
extern const gm_tmpl gm_tpl8g9lchslmbjf;
extern const uint32_t gm_tpl8g9lchslmbjf_i[];
extern const gm_tmpl_piece gm_tpl8g9lchslmbjf_p[];
extern const gm_spec *const gm_tpl8g9lchslmbjf_s[];
extern const gm_spec *const gm_tpl8gtkip9ins8o_s[];
extern const gm_tmpl gm_tpl8mdj3nc4lpjv;
extern const uint32_t gm_tpl8mdj3nc4lpjv_i[];
extern const gm_tmpl_piece gm_tpl8mdj3nc4lpjv_p[];
extern const gm_spec *const gm_tpl8mdj3nc4lpjv_s[];
extern const gm_tmpl gm_tpl8nvimfep33sn;
extern const uint32_t gm_tpl8nvimfep33sn_i[];
extern const gm_tmpl_piece gm_tpl8nvimfep33sn_p[];
extern const gm_spec *const gm_tpl8nvimfep33sn_s[];
extern const uint32_t gm_tpl8ocve2bpsu72_i[];
extern const gm_spec *const gm_tpl8v9uhia7uc1b_s[];
extern const gm_tmpl_piece gm_tpl91a2oiuc6ot7_p[];
extern const gm_spec *const gm_tpl9225uv77leci_s[];
extern const gm_tmpl_piece gm_tpl98etevndg0cj_p[];
extern const gm_tmpl gm_tpl9aakii48cq8t;
extern const uint32_t gm_tpl9aakii48cq8t_i[];
extern const gm_tmpl_piece gm_tpl9aakii48cq8t_p[];
extern const gm_spec *const gm_tpl9aakii48cq8t_s[];
extern const uint32_t gm_tpl9fn8oj7nnnsq_i[];
extern const gm_tmpl gm_tpl9ft3vu1e30uh;
extern const uint32_t gm_tpl9ft3vu1e30uh_i[];
extern const gm_tmpl_piece gm_tpl9ft3vu1e30uh_p[];
extern const gm_spec *const gm_tpl9ft3vu1e30uh_s[];
extern const gm_spec *const gm_tpl9ibjmfjq0iar_s[];
extern const gm_tmpl_piece gm_tpl9j9ok1j4nn18_p[];
extern const gm_tmpl gm_tpl9jr09voqi3gh;
extern const uint32_t gm_tpl9jr09voqi3gh_i[];
extern const gm_tmpl_piece gm_tpl9jr09voqi3gh_p[];
extern const gm_spec *const gm_tpl9jr09voqi3gh_s[];
extern const gm_tmpl_piece gm_tpl9l4fqsojrjsa_p[];
extern const uint32_t gm_tpl9q5j6bisf2fa_i[];
extern const uint32_t gm_tpla0a5a633lqve_i[];
extern const gm_tmpl gm_tpla0ci4vs242iu;
extern const uint32_t gm_tpla0ci4vs242iu_i[];
extern const gm_tmpl_piece gm_tpla0ci4vs242iu_p[];
extern const gm_spec *const gm_tpla0ci4vs242iu_s[];
extern const gm_tmpl gm_tpla0dc0a9b9i8b;
extern const uint32_t gm_tpla0dc0a9b9i8b_i[];
extern const gm_tmpl_piece gm_tpla0dc0a9b9i8b_p[];
extern const gm_spec *const gm_tpla0dc0a9b9i8b_s[];
extern const uint32_t gm_tpla0ldfomuin4r_i[];
extern const gm_spec *const gm_tpla6rojfclgc54_s[];
extern const gm_spec *const gm_tpla74als5inv2i_s[];
extern const gm_spec *const gm_tpla7fg14q9gk8f_s[];
extern const gm_tmpl_piece gm_tpla9stdteeiicv_p[];
extern const gm_spec *const gm_tplad75mid9f0qe_s[];
extern const gm_tmpl gm_tplalgvrfo85soq;
extern const uint32_t gm_tplalgvrfo85soq_i[];
extern const gm_tmpl_piece gm_tplalgvrfo85soq_p[];
extern const gm_spec *const gm_tplalgvrfo85soq_s[];
extern const gm_tmpl_piece gm_tplamm5t71udd8k_p[];
extern const uint32_t gm_tplanhmvcr622qo_i[];
extern const gm_spec *const gm_tplaotru3rka6ku_s[];
extern const gm_tmpl_piece gm_tplaqps986okebj_p[];
extern const gm_spec *const gm_tplav4294nbj9c7_s[];
extern const gm_spec *const gm_tplb05kqqtmbu7v_s[];
extern const gm_spec *const gm_tplb4rog3tmrj9n_s[];
extern const gm_tmpl_piece gm_tplb86aokjj8d2b_p[];
extern const gm_spec *const gm_tplb9i7jfun8rvg_s[];
extern const uint32_t gm_tplb9surs05k5po_i[];
extern const uint32_t gm_tplbaonrrpbr1uh_i[];
extern const gm_tmpl_piece gm_tplbbopfj3f21eb_p[];
extern const uint32_t gm_tplbds86ojmcoik_i[];
extern const gm_tmpl_piece gm_tplbgu1ah9e6d87_p[];
extern const uint32_t gm_tplbk1k026jl879_i[];
extern const gm_spec *const gm_tplbk1k026jl879_s[];
extern const gm_spec *const gm_tplblrluopde8aq_s[];
extern const gm_tmpl gm_tplbn5t5stpvru2;
extern const uint32_t gm_tplbn5t5stpvru2_i[];
extern const gm_tmpl_piece gm_tplbn5t5stpvru2_p[];
extern const gm_spec *const gm_tplbn5t5stpvru2_s[];
extern const gm_tmpl_piece gm_tplbnt4jnaihcvu_p[];
extern const gm_tmpl_piece gm_tplbntllbe37f81_p[];
extern const uint32_t gm_tplbo9rmalpf2vj_i[];
extern const gm_tmpl gm_tplbp75knfmjbql;
extern const uint32_t gm_tplbp75knfmjbql_i[];
extern const gm_tmpl_piece gm_tplbp75knfmjbql_p[];
extern const gm_spec *const gm_tplbp75knfmjbql_s[];
extern const gm_tmpl gm_tplc8kkdoja48ii;
extern const uint32_t gm_tplc8kkdoja48ii_i[];
extern const gm_tmpl_piece gm_tplc8kkdoja48ii_p[];
extern const gm_spec *const gm_tplc8kkdoja48ii_s[];
extern const gm_tmpl gm_tplc90dj04km0n5;
extern const uint32_t gm_tplc90dj04km0n5_i[];
extern const gm_tmpl_piece gm_tplc90dj04km0n5_p[];
extern const gm_spec *const gm_tplc90dj04km0n5_s[];
extern const gm_tmpl gm_tplca6pmroagoou;
extern const uint32_t gm_tplca6pmroagoou_i[];
extern const gm_tmpl_piece gm_tplca6pmroagoou_p[];
extern const gm_spec *const gm_tplca6pmroagoou_s[];
extern const uint32_t gm_tplcb5rgmp3lpr2_i[];
extern const gm_tmpl_piece gm_tplccs2j0kgin1q_p[];
extern const gm_tmpl_piece gm_tplcilp3mvhfo4n_p[];
extern const uint32_t gm_tplcoet0p57pg4s_i[];
extern const gm_tmpl gm_tplcpt7hf5iscqd;
extern const uint32_t gm_tplcpt7hf5iscqd_i[];
extern const gm_tmpl_piece gm_tplcpt7hf5iscqd_p[];
extern const gm_spec *const gm_tplcpt7hf5iscqd_s[];
extern const gm_tmpl gm_tplcre592qvlrqu;
extern const uint32_t gm_tplcre592qvlrqu_i[];
extern const gm_tmpl_piece gm_tplcre592qvlrqu_p[];
extern const gm_spec *const gm_tplcre592qvlrqu_s[];
extern const gm_spec *const gm_tpld0t14m1fld0p_s[];
extern const uint32_t gm_tpld1ficc6ikf1e_i[];
extern const gm_spec *const gm_tpld1ficc6ikf1e_s[];
extern const uint32_t gm_tpldfm52le63mtn_i[];
extern const gm_tmpl_piece gm_tpldg1m204qb08c_p[];
extern const uint32_t gm_tpldj14c3jgvlnb_i[];
extern const uint32_t gm_tpldl4pn21ood35_i[];
extern const uint32_t gm_tpldm0f92v61rgi_i[];
extern const gm_spec *const gm_tpldm0f92v61rgi_s[];
extern const gm_tmpl_piece gm_tpldqr3c69jgnqi_p[];
extern const gm_tmpl_piece gm_tpldtv4ooqtkvgm_p[];
extern const uint32_t gm_tpldu1003b7br77_i[];
extern const gm_spec *const gm_tpldu1003b7br77_s[];
extern const gm_spec *const gm_tpldumo4kdlpj8p_s[];
extern const gm_spec *const gm_tpldvhfe50cnb69_s[];
extern const gm_tmpl_piece gm_tple23ed5kepfqi_p[];
extern const uint32_t gm_tple4r0jrrru5m7_i[];
extern const uint32_t gm_tple68gu1adcjq9_i[];
extern const gm_tmpl_piece gm_tple7l1inipu3bb_p[];
extern const uint32_t gm_tpleie0q8nvjqch_i[];
extern const gm_spec *const gm_tpleiji4s5drk3o_s[];
extern const uint32_t gm_tplellk9btkq1f2_i[];
extern const gm_tmpl gm_tples1m1m9iv6b0;
extern const uint32_t gm_tples1m1m9iv6b0_i[];
extern const gm_tmpl_piece gm_tples1m1m9iv6b0_p[];
extern const gm_spec *const gm_tples1m1m9iv6b0_s[];
extern const uint32_t gm_tplf2fho9tvid0p_i[];
extern const gm_spec *const gm_tplf2fho9tvid0p_s[];
extern const gm_tmpl gm_tplf4oacobin5qk;
extern const uint32_t gm_tplf4oacobin5qk_i[];
extern const gm_tmpl_piece gm_tplf4oacobin5qk_p[];
extern const gm_spec *const gm_tplf4oacobin5qk_s[];
extern const uint32_t gm_tplf6hbp7abojsd_i[];
extern const gm_spec *const gm_tplf6hbp7abojsd_s[];
extern const gm_spec *const gm_tplfb67h0nat3pe_s[];
extern const gm_spec *const gm_tplfcgbc9s4hmn7_s[];
extern const gm_tmpl_piece gm_tplfe53mjoakni4_p[];
extern const gm_tmpl_piece gm_tplfm1jmgsrn2ua_p[];
extern const gm_tmpl gm_tplfqk35ejq39la;
extern const uint32_t gm_tplfqk35ejq39la_i[];
extern const gm_tmpl_piece gm_tplfqk35ejq39la_p[];
extern const gm_spec *const gm_tplfqk35ejq39la_s[];
extern const gm_tmpl gm_tplg21ilonldhva;
extern const uint32_t gm_tplg21ilonldhva_i[];
extern const gm_tmpl_piece gm_tplg21ilonldhva_p[];
extern const gm_spec *const gm_tplg21ilonldhva_s[];
extern const gm_tmpl_piece gm_tplg4866gcgr837_p[];
extern const gm_tmpl_piece gm_tplg4k0uitmfhf0_p[];
extern const uint32_t gm_tplgan4klju39em_i[];
extern const gm_spec *const gm_tplgeqcgpvi6n7p_s[];
extern const gm_tmpl_piece gm_tplgfhhtcka09o4_p[];
extern const uint32_t gm_tplggs57b0p2or4_i[];
extern const gm_tmpl_piece gm_tplgjeridtvkml4_p[];
extern const uint32_t gm_tplgp15dehsbtkk_i[];
extern const gm_tmpl_piece gm_tplgqohfqetaph7_p[];
extern const gm_tmpl gm_tplgqr018o660ue;
extern const uint32_t gm_tplgqr018o660ue_i[];
extern const gm_tmpl_piece gm_tplgqr018o660ue_p[];
extern const gm_spec *const gm_tplgqr018o660ue_s[];
extern const gm_tmpl_piece gm_tplgs2l60s65hm3_p[];
extern const gm_tmpl_piece gm_tplgt6vkfjdh8cd_p[];
extern const gm_tmpl_piece gm_tplh2em7r1dmffm_p[];
extern const gm_spec *const gm_tplh2sse9mocm92_s[];
extern const gm_tmpl gm_tplh7ffbsfnitkh;
extern const uint32_t gm_tplh7ffbsfnitkh_i[];
extern const gm_tmpl_piece gm_tplh7ffbsfnitkh_p[];
extern const gm_spec *const gm_tplh7ffbsfnitkh_s[];
extern const uint32_t gm_tplh8soes50a6ps_i[];
extern const uint32_t gm_tplhatoh83mi4is_i[];
extern const uint32_t gm_tplhdb29uo8rm6j_i[];
extern const gm_tmpl gm_tplhghj93ore8u2;
extern const uint32_t gm_tplhghj93ore8u2_i[];
extern const gm_tmpl_piece gm_tplhghj93ore8u2_p[];
extern const gm_spec *const gm_tplhghj93ore8u2_s[];
extern const gm_spec *const gm_tplhhuh415jqli4_s[];
extern const uint32_t gm_tplhl4q6p2b5f92_i[];
extern const gm_tmpl gm_tplhn9hprqs819d;
extern const uint32_t gm_tplhn9hprqs819d_i[];
extern const gm_tmpl_piece gm_tplhn9hprqs819d_p[];
extern const gm_spec *const gm_tplhn9hprqs819d_s[];
extern const gm_tmpl gm_tplhonv8g349dv9;
extern const uint32_t gm_tplhonv8g349dv9_i[];
extern const gm_tmpl_piece gm_tplhonv8g349dv9_p[];
extern const gm_spec *const gm_tplhonv8g349dv9_s[];
extern const gm_spec *const gm_tplhp4k6h527j3k_s[];
extern const uint32_t gm_tplhs7ofk4c8qgg_i[];
extern const gm_tmpl gm_tpli0tmonaamd45;
extern const uint32_t gm_tpli0tmonaamd45_i[];
extern const gm_tmpl_piece gm_tpli0tmonaamd45_p[];
extern const gm_spec *const gm_tpli0tmonaamd45_s[];
extern const gm_spec *const gm_tpli2vu74n9ss6f_s[];
extern const gm_spec *const gm_tpli4rfoi75onhj_s[];
extern const gm_tmpl gm_tplibfhnj0illo7;
extern const uint32_t gm_tplibfhnj0illo7_i[];
extern const gm_tmpl_piece gm_tplibfhnj0illo7_p[];
extern const gm_spec *const gm_tplibfhnj0illo7_s[];
extern const gm_spec *const gm_tplie81jau63n6g_s[];
extern const gm_spec *const gm_tplim7pg0q4u4tg_s[];
extern const uint32_t gm_tpliq0iop4ulk1g_i[];
extern const uint32_t gm_tpliq8p2spcvq06_i[];
extern const gm_tmpl gm_tpliufjdlri3svp;
extern const uint32_t gm_tpliufjdlri3svp_i[];
extern const gm_tmpl_piece gm_tpliufjdlri3svp_p[];
extern const gm_spec *const gm_tpliufjdlri3svp_s[];
extern const gm_tmpl_piece gm_tplj3c7bqq8809l_p[];
extern const gm_tmpl_piece gm_tplj6ss1j7sgouf_p[];
extern const uint32_t gm_tplj7a4hf9tva3v_i[];
extern const uint32_t gm_tplj7j0vuugbh2u_i[];
extern const gm_tmpl gm_tplj92mk62u70db;
extern const uint32_t gm_tplj92mk62u70db_i[];
extern const gm_tmpl_piece gm_tplj92mk62u70db_p[];
extern const gm_spec *const gm_tplj92mk62u70db_s[];
extern const uint32_t gm_tplj9vh1qm5m031_i[];
extern const gm_tmpl_piece gm_tplj9vh1qm5m031_p[];
extern const gm_tmpl gm_tplja7aur1oiv70;
extern const uint32_t gm_tplja7aur1oiv70_i[];
extern const gm_tmpl_piece gm_tplja7aur1oiv70_p[];
extern const gm_spec *const gm_tplja7aur1oiv70_s[];
extern const uint32_t gm_tpljbco1er3agg5_i[];
extern const gm_tmpl gm_tpljcso3h5g4o29;
extern const uint32_t gm_tpljcso3h5g4o29_i[];
extern const gm_tmpl_piece gm_tpljcso3h5g4o29_p[];
extern const gm_spec *const gm_tpljcso3h5g4o29_s[];
extern const gm_tmpl gm_tpljicvo0ni8aba;
extern const uint32_t gm_tpljicvo0ni8aba_i[];
extern const gm_tmpl_piece gm_tpljicvo0ni8aba_p[];
extern const gm_spec *const gm_tpljicvo0ni8aba_s[];
extern const gm_tmpl gm_tpljp2husef3b2l;
extern const uint32_t gm_tpljp2husef3b2l_i[];
extern const gm_tmpl_piece gm_tpljp2husef3b2l_p[];
extern const gm_spec *const gm_tpljp2husef3b2l_s[];
extern const uint32_t gm_tpljpjg7sv41er0_i[];
extern const uint32_t gm_tpljtaapes6rh2u_i[];
extern const gm_tmpl gm_tplk0jab62rgl3v;
extern const uint32_t gm_tplk0jab62rgl3v_i[];
extern const gm_tmpl_piece gm_tplk0jab62rgl3v_p[];
extern const gm_spec *const gm_tplk0jab62rgl3v_s[];
extern const gm_tmpl gm_tplk1botso018is;
extern const uint32_t gm_tplk1botso018is_i[];
extern const gm_tmpl_piece gm_tplk1botso018is_p[];
extern const gm_spec *const gm_tplk1botso018is_s[];
extern const gm_tmpl_piece gm_tplk27ieg1b14ol_p[];
extern const gm_spec *const gm_tplk34ojqiaho84_s[];
extern const gm_tmpl_piece gm_tplk51k04l6756s_p[];
extern const gm_spec *const gm_tplk6g6bjh5rkat_s[];
extern const gm_spec *const gm_tplk7f3hoqrengc_s[];
extern const gm_spec *const gm_tplk7hjamhd5jcl_s[];
extern const gm_tmpl gm_tplkjc5mpc5i80h;
extern const uint32_t gm_tplkjc5mpc5i80h_i[];
extern const gm_tmpl_piece gm_tplkjc5mpc5i80h_p[];
extern const gm_spec *const gm_tplkjc5mpc5i80h_s[];
extern const gm_tmpl_piece gm_tplkjh7li83dd9g_p[];
extern const gm_tmpl_piece gm_tplkkvn546crm4n_p[];
extern const gm_spec *const gm_tplkr4062p8a8bi_s[];
extern const uint32_t gm_tplkte26oe5429d_i[];
extern const gm_tmpl gm_tplkucr7969socr;
extern const uint32_t gm_tplkucr7969socr_i[];
extern const gm_tmpl_piece gm_tplkucr7969socr_p[];
extern const gm_spec *const gm_tplkucr7969socr_s[];
extern const gm_tmpl_piece gm_tpll408251sp58f_p[];
extern const gm_spec *const gm_tpll60ng9eo3ai8_s[];
extern const uint32_t gm_tpll6jntfkrhtt4_i[];
extern const gm_tmpl_piece gm_tpll6pilv9krmhr_p[];
extern const gm_tmpl gm_tpll6udkhd3c609;
extern const uint32_t gm_tpll6udkhd3c609_i[];
extern const gm_tmpl_piece gm_tpll6udkhd3c609_p[];
extern const gm_spec *const gm_tpll6udkhd3c609_s[];
extern const uint32_t gm_tpll738pfnpjq8c_i[];
extern const gm_tmpl_piece gm_tpll8q455cp9mhi_p[];
extern const uint32_t gm_tpllaqikqjbb69k_i[];
extern const uint32_t gm_tpllbgi055uoiek_i[];
extern const gm_spec *const gm_tpllbu1ke0v3v7a_s[];
extern const gm_spec *const gm_tpllcp8fo2sqc1r_s[];
extern const gm_spec *const gm_tpllg65m2i6jvjk_s[];
extern const gm_spec *const gm_tplli6rlvq5vnvb_s[];
extern const uint32_t gm_tpllm39516ci1mu_i[];
extern const uint32_t gm_tpllmofqkiu75lh_i[];
extern const gm_tmpl_piece gm_tpllmofqkiu75lh_p[];
extern const gm_tmpl gm_tpllpopiint29pt;
extern const uint32_t gm_tpllpopiint29pt_i[];
extern const gm_tmpl_piece gm_tpllpopiint29pt_p[];
extern const gm_spec *const gm_tpllpopiint29pt_s[];
extern const gm_tmpl_piece gm_tplls1lesito750_p[];
extern const uint32_t gm_tpllsre3itruefn_i[];
extern const gm_tmpl gm_tplm02t0g7r3hdp;
extern const uint32_t gm_tplm02t0g7r3hdp_i[];
extern const gm_tmpl_piece gm_tplm02t0g7r3hdp_p[];
extern const gm_spec *const gm_tplm02t0g7r3hdp_s[];
extern const uint32_t gm_tplm6g8a9odsasi_i[];
extern const gm_tmpl gm_tplm6qp3tlbq28j;
extern const uint32_t gm_tplm6qp3tlbq28j_i[];
extern const gm_tmpl_piece gm_tplm6qp3tlbq28j_p[];
extern const gm_spec *const gm_tplm6qp3tlbq28j_s[];
extern const uint32_t gm_tplmekan10d5buf_i[];
extern const uint32_t gm_tplmk3amvcsk2gu_i[];
extern const gm_spec *const gm_tplmn77qas0g7s4_s[];
extern const gm_tmpl gm_tplmo1fik79j6ea;
extern const uint32_t gm_tplmo1fik79j6ea_i[];
extern const gm_tmpl_piece gm_tplmo1fik79j6ea_p[];
extern const gm_spec *const gm_tplmo1fik79j6ea_s[];
extern const gm_tmpl_piece gm_tplmr0a02ecbaom_p[];
extern const gm_spec *const gm_tplmr0a02ecbaom_s[];
extern const gm_tmpl_piece gm_tplmsb1klg4n6ap_p[];
extern const gm_tmpl gm_tplmsd0sfgf2v66;
extern const uint32_t gm_tplmsd0sfgf2v66_i[];
extern const gm_tmpl_piece gm_tplmsd0sfgf2v66_p[];
extern const gm_spec *const gm_tplmsd0sfgf2v66_s[];
extern const gm_tmpl gm_tpln4t9g9do4qg7;
extern const uint32_t gm_tpln4t9g9do4qg7_i[];
extern const gm_tmpl_piece gm_tpln4t9g9do4qg7_p[];
extern const gm_spec *const gm_tpln4t9g9do4qg7_s[];
extern const uint32_t gm_tpln6vfieiab6l5_i[];
extern const gm_tmpl_piece gm_tpln6vfieiab6l5_p[];
extern const gm_tmpl gm_tplncm14tpa4cdg;
extern const uint32_t gm_tplncm14tpa4cdg_i[];
extern const gm_tmpl_piece gm_tplncm14tpa4cdg_p[];
extern const gm_spec *const gm_tplncm14tpa4cdg_s[];
extern const uint32_t gm_tplneb8585d50a4_i[];
extern const gm_tmpl_piece gm_tplngekt6vfvnsv_p[];
extern const uint32_t gm_tplnh359qrng71h_i[];
extern const gm_spec *const gm_tplnitaca1dp5li_s[];
extern const gm_tmpl gm_tplnmm43n0jmcod;
extern const uint32_t gm_tplnmm43n0jmcod_i[];
extern const gm_tmpl_piece gm_tplnmm43n0jmcod_p[];
extern const gm_spec *const gm_tplnmm43n0jmcod_s[];
extern const gm_spec *const gm_tplnnqg0hvtcqac_s[];
extern const gm_spec *const gm_tplnqad0r40bfej_s[];
extern const gm_tmpl gm_tplnr3ridveqobn;
extern const uint32_t gm_tplnr3ridveqobn_i[];
extern const gm_tmpl_piece gm_tplnr3ridveqobn_p[];
extern const gm_spec *const gm_tplnr3ridveqobn_s[];
extern const gm_tmpl gm_tplnr9pmjldkl35;
extern const uint32_t gm_tplnr9pmjldkl35_i[];
extern const gm_tmpl_piece gm_tplnr9pmjldkl35_p[];
extern const gm_spec *const gm_tplnr9pmjldkl35_s[];
extern const gm_tmpl gm_tplo2fstigc8bcf;
extern const uint32_t gm_tplo2fstigc8bcf_i[];
extern const gm_tmpl_piece gm_tplo2fstigc8bcf_p[];
extern const gm_spec *const gm_tplo2fstigc8bcf_s[];
extern const gm_tmpl gm_tplo4mr4l55q97r;
extern const uint32_t gm_tplo4mr4l55q97r_i[];
extern const gm_tmpl_piece gm_tplo4mr4l55q97r_p[];
extern const gm_spec *const gm_tplo4mr4l55q97r_s[];
extern const gm_tmpl gm_tplo6lbijqleti1;
extern const uint32_t gm_tplo6lbijqleti1_i[];
extern const gm_tmpl_piece gm_tplo6lbijqleti1_p[];
extern const gm_spec *const gm_tplo6lbijqleti1_s[];
extern const uint32_t gm_tplodlbfsp8tfu3_i[];
extern const gm_tmpl gm_tplogtdrvl783rp;
extern const uint32_t gm_tplogtdrvl783rp_i[];
extern const gm_tmpl_piece gm_tplogtdrvl783rp_p[];
extern const gm_spec *const gm_tplogtdrvl783rp_s[];
extern const gm_tmpl gm_tploh8l9phm91d3;
extern const uint32_t gm_tploh8l9phm91d3_i[];
extern const gm_tmpl_piece gm_tploh8l9phm91d3_p[];
extern const gm_spec *const gm_tploh8l9phm91d3_s[];
extern const gm_tmpl gm_tploik9nhlf98dm;
extern const uint32_t gm_tploik9nhlf98dm_i[];
extern const gm_tmpl_piece gm_tploik9nhlf98dm_p[];
extern const gm_spec *const gm_tploik9nhlf98dm_s[];
extern const gm_tmpl_piece gm_tplokoueqaolfmt_p[];
extern const gm_spec *const gm_tplomf18g3j9if3_s[];
extern const uint32_t gm_tplooqfb0umhrq0_i[];
extern const gm_tmpl gm_tplopmhgmch9jsj;
extern const uint32_t gm_tplopmhgmch9jsj_i[];
extern const gm_tmpl_piece gm_tplopmhgmch9jsj_p[];
extern const gm_spec *const gm_tplopmhgmch9jsj_s[];
extern const gm_spec *const gm_tplotas2madniqp_s[];
extern const gm_tmpl gm_tplovlajhkvntql;
extern const uint32_t gm_tplovlajhkvntql_i[];
extern const gm_tmpl_piece gm_tplovlajhkvntql_p[];
extern const gm_spec *const gm_tplovlajhkvntql_s[];
extern const gm_tmpl gm_tplp0abp7005sqm;
extern const uint32_t gm_tplp0abp7005sqm_i[];
extern const gm_tmpl_piece gm_tplp0abp7005sqm_p[];
extern const gm_spec *const gm_tplp0abp7005sqm_s[];
extern const uint32_t gm_tplp3rnngb19vkd_i[];
extern const gm_tmpl_piece gm_tplp3rnngb19vkd_p[];
extern const gm_tmpl_piece gm_tplp72dgv8cvduk_p[];
extern const gm_spec *const gm_tplp7srseh4nakh_s[];
extern const gm_tmpl gm_tplpfv6kueslks8;
extern const uint32_t gm_tplpfv6kueslks8_i[];
extern const gm_tmpl_piece gm_tplpfv6kueslks8_p[];
extern const gm_spec *const gm_tplpfv6kueslks8_s[];
extern const uint32_t gm_tplpj1vfgivuj13_i[];
extern const gm_spec *const gm_tplplkqcb01ocrd_s[];
extern const gm_tmpl_piece gm_tplpog54do9oh2a_p[];
extern const gm_spec *const gm_tplpsmnsqmt7n3e_s[];
extern const uint32_t gm_tplptgaia3s19c9_i[];
extern const uint32_t gm_tplpvh6srii84sj_i[];
extern const uint32_t gm_tplq0mvos8lisnf_i[];
extern const uint32_t gm_tplq0stkp3a31gd_i[];
extern const gm_spec *const gm_tplq12pt4e53uj3_s[];
extern const gm_tmpl_piece gm_tplq2jm9n7vekeo_p[];
extern const gm_spec *const gm_tplq85e99v067d0_s[];
extern const gm_tmpl_piece gm_tplqbbmojia0cmf_p[];
extern const uint32_t gm_tplqfu63ajpo70a_i[];
extern const gm_tmpl_piece gm_tplqfu63ajpo70a_p[];
extern const gm_tmpl gm_tplqipe054or34v;
extern const uint32_t gm_tplqipe054or34v_i[];
extern const gm_tmpl_piece gm_tplqipe054or34v_p[];
extern const gm_spec *const gm_tplqipe054or34v_s[];
extern const uint32_t gm_tplqjfk6t5rn06e_i[];
extern const gm_tmpl gm_tplqk3ejs6j0ne5;
extern const uint32_t gm_tplqk3ejs6j0ne5_i[];
extern const gm_tmpl_piece gm_tplqk3ejs6j0ne5_p[];
extern const gm_spec *const gm_tplqk3ejs6j0ne5_s[];
extern const gm_tmpl gm_tplqm5shuhajt4a;
extern const uint32_t gm_tplqm5shuhajt4a_i[];
extern const gm_tmpl_piece gm_tplqm5shuhajt4a_p[];
extern const gm_spec *const gm_tplqm5shuhajt4a_s[];
extern const uint32_t gm_tplqnau1elkkogs_i[];
extern const gm_tmpl_piece gm_tplqnufra7h2p1s_p[];
extern const gm_spec *const gm_tplqp3jlt332oq5_s[];
extern const uint32_t gm_tplqug1td3hpetg_i[];
extern const gm_tmpl_piece gm_tplr1if0cs3oolt_p[];
extern const uint32_t gm_tplr2c5vmkfj9df_i[];
extern const gm_tmpl_piece gm_tplr2tuff1mvdst_p[];
extern const gm_spec *const gm_tplr869c1s382eh_s[];
extern const uint32_t gm_tplr9mrmvfj4528_i[];
extern const gm_spec *const gm_tplrgtnfea81qcj_s[];
extern const gm_tmpl gm_tplrh70572i8ko0;
extern const uint32_t gm_tplrh70572i8ko0_i[];
extern const gm_tmpl_piece gm_tplrh70572i8ko0_p[];
extern const gm_spec *const gm_tplrh70572i8ko0_s[];
extern const gm_tmpl_piece gm_tplrivm1rlf1ps3_p[];
extern const gm_spec *const gm_tplrkvt0j8tqlqg_s[];
extern const uint32_t gm_tplrlqq48dtu7p5_i[];
extern const gm_spec *const gm_tplrmkc8hrdtbfh_s[];
extern const gm_tmpl_piece gm_tplrp7ukmgscavj_p[];
extern const gm_tmpl gm_tpls19nuda8st36;
extern const uint32_t gm_tpls19nuda8st36_i[];
extern const gm_tmpl_piece gm_tpls19nuda8st36_p[];
extern const gm_spec *const gm_tpls19nuda8st36_s[];
extern const gm_tmpl_piece gm_tpls2fio2pf4rdq_p[];
extern const gm_tmpl_piece gm_tpls3402qmr7otb_p[];
extern const gm_spec *const gm_tpls3402qmr7otb_s[];
extern const gm_tmpl gm_tpls4ecclriepmb;
extern const uint32_t gm_tpls4ecclriepmb_i[];
extern const gm_tmpl_piece gm_tpls4ecclriepmb_p[];
extern const gm_spec *const gm_tpls4ecclriepmb_s[];
extern const gm_tmpl gm_tpls6dj3unub5ff;
extern const uint32_t gm_tpls6dj3unub5ff_i[];
extern const gm_tmpl_piece gm_tpls6dj3unub5ff_p[];
extern const gm_spec *const gm_tpls6dj3unub5ff_s[];
extern const gm_tmpl gm_tplsa4s74k8e6ci;
extern const uint32_t gm_tplsa4s74k8e6ci_i[];
extern const gm_tmpl_piece gm_tplsa4s74k8e6ci_p[];
extern const gm_spec *const gm_tplsa4s74k8e6ci_s[];
extern const gm_spec *const gm_tplsgc0lnghs9b6_s[];
extern const gm_tmpl_piece gm_tplso4fjesdi372_p[];
extern const gm_spec *const gm_tplsrmv18emggok_s[];
extern const gm_tmpl_piece gm_tplt1i3tc7bfdhf_p[];
extern const gm_tmpl gm_tplt1si3io9mdpe;
extern const uint32_t gm_tplt1si3io9mdpe_i[];
extern const gm_tmpl_piece gm_tplt1si3io9mdpe_p[];
extern const gm_spec *const gm_tplt1si3io9mdpe_s[];
extern const gm_tmpl_piece gm_tplt392n8hqfhn9_p[];
extern const gm_tmpl_piece gm_tplt3iub1goev8r_p[];
extern const gm_tmpl_piece gm_tplt524tdio8698_p[];
extern const gm_spec *const gm_tplt524tdio8698_s[];
extern const gm_tmpl_piece gm_tplt56hco5v1mvf_p[];
extern const gm_tmpl gm_tplt9o3qfo5dqct;
extern const uint32_t gm_tplt9o3qfo5dqct_i[];
extern const gm_tmpl_piece gm_tplt9o3qfo5dqct_p[];
extern const gm_spec *const gm_tplt9o3qfo5dqct_s[];
extern const uint32_t gm_tpltdoamgi2k9f9_i[];
extern const gm_spec *const gm_tplthb0oue219bq_s[];
extern const gm_tmpl gm_tpltj7umjd53udq;
extern const uint32_t gm_tpltj7umjd53udq_i[];
extern const gm_tmpl_piece gm_tpltj7umjd53udq_p[];
extern const gm_spec *const gm_tpltj7umjd53udq_s[];
extern const gm_spec *const gm_tpltl0rfkesf8ms_s[];
extern const gm_spec *const gm_tplto3kksb9sot8_s[];
extern const gm_spec *const gm_tpltvrl7dp7h9m1_s[];
extern const gm_tmpl gm_tplu0ijdfgp8vs3;
extern const uint32_t gm_tplu0ijdfgp8vs3_i[];
extern const gm_tmpl_piece gm_tplu0ijdfgp8vs3_p[];
extern const gm_spec *const gm_tplu0ijdfgp8vs3_s[];
extern const gm_tmpl_piece gm_tplu0n2ra1s30o8_p[];
extern const gm_spec *const gm_tplu0rdj3ds7u0l_s[];
extern const uint32_t gm_tplu2kaori4llai_i[];
extern const gm_tmpl_piece gm_tplu2kaori4llai_p[];
extern const gm_tmpl_piece gm_tplu2tisf3oaee8_p[];
extern const uint32_t gm_tplu4465vjgoqre_i[];
extern const uint32_t gm_tplu58b6rd4mq28_i[];
extern const gm_tmpl gm_tplu7d9ofnf77qa;
extern const uint32_t gm_tplu7d9ofnf77qa_i[];
extern const gm_tmpl_piece gm_tplu7d9ofnf77qa_p[];
extern const gm_spec *const gm_tplu7d9ofnf77qa_s[];
extern const gm_tmpl gm_tplu9uohqqitldd;
extern const uint32_t gm_tplu9uohqqitldd_i[];
extern const gm_tmpl_piece gm_tplu9uohqqitldd_p[];
extern const gm_spec *const gm_tplu9uohqqitldd_s[];
extern const uint32_t gm_tpluf7u812t4p3d_i[];
extern const gm_tmpl_piece gm_tplumuahjb5m8m8_p[];
extern const gm_tmpl_piece gm_tplupuvr4efpn1v_p[];
extern const gm_tmpl gm_tplurleasos6378;
extern const uint32_t gm_tplurleasos6378_i[];
extern const gm_tmpl_piece gm_tplurleasos6378_p[];
extern const gm_spec *const gm_tplurleasos6378_s[];
extern const gm_tmpl gm_tplutkid6t0497p;
extern const uint32_t gm_tplutkid6t0497p_i[];
extern const gm_tmpl_piece gm_tplutkid6t0497p_p[];
extern const gm_spec *const gm_tplutkid6t0497p_s[];
extern const gm_tmpl_piece gm_tpluue9jh13g9df_p[];
extern const gm_tmpl_piece gm_tplv516qd7l6o3q_p[];
extern const uint32_t gm_tplv8ckuk7p1s8m_i[];
extern const gm_tmpl gm_tplv8tsq846c0lj;
extern const uint32_t gm_tplv8tsq846c0lj_i[];
extern const gm_tmpl_piece gm_tplv8tsq846c0lj_p[];
extern const gm_spec *const gm_tplv8tsq846c0lj_s[];
extern const gm_tmpl_piece gm_tplvbotf15ps5s5_p[];
extern const gm_spec *const gm_tplvc9hnrt45n6d_s[];
extern const gm_spec *const gm_tplvkvnlb2orh55_s[];
extern const uint32_t gm_tplvonunpuh0kn8_i[];
extern const uint32_t gm_tplvqfop4uu1u5q_i[];
extern const gm_spec *const gm_tplvvic2bq8a97l_s[];
extern const gm_type gm_tytje7cegoml;
extern const gm_variant gm_tytje7cegoml_v[];
const char gm_src3m73omiakl12[] = "compiler/cgen/outline.gm";
const char gm_src3nt8pf0qk5gn[] = "compiler/load/toml.gm";
const char gm_src5bc4unh0r6k6[] = "compiler/doc/doc.gm";
const char gm_src6c128h826nsg[] = "compiler/interp/calls.gm";
const char gm_src6lm8esmv16p1[] = "compiler/sema/names.gm";
const char gm_srcb2hdor4f6g0q[] = "compiler/ide/walk.gm";
const char gm_srcb7e7uf3pvdko[] = "compiler/lsp/workspace.gm";
const char gm_srcbu4asq8trfta[] = "compiler/interp/vm_lir.gm";
const char gm_srcckqm4prfktot[] = "compiler/ide/facts.gm";
const char gm_srcddjnq2mhhonr[] = "compiler/cgen/wasi.gm";
const char gm_srch2l5nl06vsci[] = "compiler/check/expr.gm";
const char gm_srchntnl6m6g3am[] = "compiler/interp/threads.gm";
const char gm_srcmj20te78r22f[] = "compiler/pkg/cache.gm";
const char gm_srcpmlh93537vbl[] = "compiler/parse/pattern.gm";
const gm_spec *const gm_tpl00nmcu166crh_s[] = {NULL};
const gm_spec *const gm_tpl00rv7vuig9ur_s[] = {NULL};
const gm_spec *const gm_tpl0e4rd6ag9tqa_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl0e782j7cr191_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl0oli8trq10sp_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl13c887f2o944_s[] = {NULL};
const gm_spec *const gm_tpl15v8rvh46nek_s[] = {NULL};
const gm_spec *const gm_tpl1a3usdkusvtq_s[] = {NULL, NULL};
const gm_spec *const gm_tpl1gtefnja08ps_s[] = {&(gm_spec){32, 0, 0, 0, 1, 1, 2u, -1}};
const gm_spec *const gm_tpl1ige45obm306_s[] = {NULL, NULL};
const gm_spec *const gm_tpl1kmako3cfauf_s[] = {NULL};
const gm_spec *const gm_tpl1sdu3l536hkg_s[] = {NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpl1u24a06iag0n_s[] = {NULL};
const gm_spec *const gm_tpl21i1s6vvb3m4_s[] = {NULL};
const gm_spec *const gm_tpl2u97uspakqd9_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl31lgghh3mal3_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpl3ttr7njhqa54_s[] = {NULL};
const gm_spec *const gm_tpl51u3ki3ilqou_s[] = {NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpl59r7h6035548_s[] = {NULL, NULL};
const gm_spec *const gm_tpl5cgoa6ddohcv_s[] = {NULL};
const gm_spec *const gm_tpl5hatm6q530pn_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl5q4rr3nd9637_s[] = {NULL};
const gm_spec *const gm_tpl5qkek6smv4rg_s[] = {NULL};
const gm_spec *const gm_tpl5svfna5ij5ui_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl60na9lt7qpkj_s[] = {NULL, NULL};
const gm_spec *const gm_tpl61n2hscg0et2_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl6bjoajp2fh2n_s[] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpl6jo4mmkdq2l1_s[] = {NULL};
const gm_spec *const gm_tpl6n3s5f3ittsi_s[] = {NULL, NULL};
const gm_spec *const gm_tpl6ptbf3l6su1b_s[] = {NULL};
const gm_spec *const gm_tpl6rsespo9a6nq_s[] = {NULL};
const gm_spec *const gm_tpl6s7ufou2uts6_s[] = {NULL};
const gm_spec *const gm_tpl737bm31hf71o_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl75bcbum1ntsg_s[] = {&(gm_spec){32, 0, 0, 0, 0, 0, 0u, 3}};
const gm_spec *const gm_tpl7da8g7lhjmr1_s[] = {NULL};
const gm_spec *const gm_tpl7eehu7c1eqka_s[] = {NULL};
const gm_spec *const gm_tpl7sqjangpsngf_s[] = {NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpl8gtkip9ins8o_s[] = {NULL};
const gm_spec *const gm_tpl8v9uhia7uc1b_s[] = {NULL};
const gm_spec *const gm_tpl9225uv77leci_s[] = {NULL};
const gm_spec *const gm_tpl9ibjmfjq0iar_s[] = {NULL};
const gm_spec *const gm_tpl9jr09voqi3gh_s[] = {NULL, NULL};
const gm_spec *const gm_tpla6rojfclgc54_s[] = {NULL};
const gm_spec *const gm_tpla74als5inv2i_s[] = {NULL};
const gm_spec *const gm_tpla7fg14q9gk8f_s[] = {NULL, NULL};
const gm_spec *const gm_tplad75mid9f0qe_s[] = {NULL};
const gm_spec *const gm_tplaotru3rka6ku_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplav4294nbj9c7_s[] = {NULL};
const gm_spec *const gm_tplb05kqqtmbu7v_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplb4rog3tmrj9n_s[] = {NULL, NULL};
const gm_spec *const gm_tplb9i7jfun8rvg_s[] = {NULL};
const gm_spec *const gm_tplbk1k026jl879_s[] = {NULL, NULL};
const gm_spec *const gm_tplblrluopde8aq_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpld0t14m1fld0p_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpld1ficc6ikf1e_s[] = {NULL};
const gm_spec *const gm_tpldm0f92v61rgi_s[] = {NULL};
const gm_spec *const gm_tpldu1003b7br77_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpldumo4kdlpj8p_s[] = {NULL, NULL};
const gm_spec *const gm_tpldvhfe50cnb69_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpleiji4s5drk3o_s[] = {NULL, &(gm_spec){32, 0, 0, 1, 0, 1, 0u, -1}, &(gm_spec){32, 0, 0, 1, 0, 1, 0u, -1}, NULL};
const gm_spec *const gm_tplf2fho9tvid0p_s[] = {NULL};
const gm_spec *const gm_tplf6hbp7abojsd_s[] = {NULL, NULL};
const gm_spec *const gm_tplfb67h0nat3pe_s[] = {NULL};
const gm_spec *const gm_tplfcgbc9s4hmn7_s[] = {NULL};
const gm_spec *const gm_tplgeqcgpvi6n7p_s[] = {NULL};
const gm_spec *const gm_tplh2sse9mocm92_s[] = {NULL, NULL};
const gm_spec *const gm_tplhghj93ore8u2_s[] = {NULL};
const gm_spec *const gm_tplhhuh415jqli4_s[] = {NULL, NULL};
const gm_spec *const gm_tplhp4k6h527j3k_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpli2vu74n9ss6f_s[] = {NULL};
const gm_spec *const gm_tpli4rfoi75onhj_s[] = {NULL, NULL};
const gm_spec *const gm_tplie81jau63n6g_s[] = {NULL};
const gm_spec *const gm_tplim7pg0q4u4tg_s[] = {NULL};
const gm_spec *const gm_tplk34ojqiaho84_s[] = {NULL};
const gm_spec *const gm_tplk6g6bjh5rkat_s[] = {NULL};
const gm_spec *const gm_tplk7f3hoqrengc_s[] = {NULL};
const gm_spec *const gm_tplk7hjamhd5jcl_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplkr4062p8a8bi_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpll60ng9eo3ai8_s[] = {NULL};
const gm_spec *const gm_tpllbu1ke0v3v7a_s[] = {NULL, NULL};
const gm_spec *const gm_tpllcp8fo2sqc1r_s[] = {NULL, NULL};
const gm_spec *const gm_tpllg65m2i6jvjk_s[] = {NULL, NULL};
const gm_spec *const gm_tplli6rlvq5vnvb_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplmn77qas0g7s4_s[] = {NULL};
const gm_spec *const gm_tplmr0a02ecbaom_s[] = {&(gm_spec){32, 0, 0, 1, 0, 1, 0u, -1}};
const gm_spec *const gm_tplnitaca1dp5li_s[] = {NULL, NULL};
const gm_spec *const gm_tplnnqg0hvtcqac_s[] = {NULL};
const gm_spec *const gm_tplnqad0r40bfej_s[] = {NULL};
const gm_spec *const gm_tplomf18g3j9if3_s[] = {NULL};
const gm_spec *const gm_tplotas2madniqp_s[] = {NULL};
const gm_spec *const gm_tplp7srseh4nakh_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplplkqcb01ocrd_s[] = {NULL};
const gm_spec *const gm_tplpsmnsqmt7n3e_s[] = {NULL};
const gm_spec *const gm_tplq12pt4e53uj3_s[] = {NULL, NULL};
const gm_spec *const gm_tplq85e99v067d0_s[] = {NULL};
const gm_spec *const gm_tplqp3jlt332oq5_s[] = {NULL};
const gm_spec *const gm_tplr869c1s382eh_s[] = {NULL, NULL};
const gm_spec *const gm_tplrgtnfea81qcj_s[] = {NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplrkvt0j8tqlqg_s[] = {NULL};
const gm_spec *const gm_tplrmkc8hrdtbfh_s[] = {NULL};
const gm_spec *const gm_tpls3402qmr7otb_s[] = {NULL, NULL};
const gm_spec *const gm_tplsgc0lnghs9b6_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplsrmv18emggok_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplt524tdio8698_s[] = {NULL};
const gm_spec *const gm_tplthb0oue219bq_s[] = {NULL, NULL};
const gm_spec *const gm_tpltl0rfkesf8ms_s[] = {NULL, NULL};
const gm_spec *const gm_tplto3kksb9sot8_s[] = {NULL};
const gm_spec *const gm_tpltvrl7dp7h9m1_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplu0rdj3ds7u0l_s[] = {NULL, NULL};
const gm_spec *const gm_tplu7d9ofnf77qa_s[] = {NULL};
const gm_spec *const gm_tplu9uohqqitldd_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplvc9hnrt45n6d_s[] = {NULL, NULL};
const gm_spec *const gm_tplvkvnlb2orh55_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplvvic2bq8a97l_s[] = {NULL, &(gm_spec){32, 0, 0, 0, 1, 2, 4u, -1}};
const gm_tmpl gm_tpl0elencma14kh = {1u, gm_tpl0elencma14kh_p, gm_tpl0elencma14kh_i, gm_tpl0elencma14kh_s};
const gm_tmpl gm_tpl0j5bgvjcbn0u = {2u, gm_tpl0j5bgvjcbn0u_p, gm_tpl0j5bgvjcbn0u_i, gm_tpl0j5bgvjcbn0u_s};
const gm_tmpl gm_tpl10tenob4hk5k = {2u, gm_tpl10tenob4hk5k_p, gm_tpl10tenob4hk5k_i, gm_tpl10tenob4hk5k_s};
const gm_tmpl gm_tpl128vde47jcsu = {1u, gm_tpl128vde47jcsu_p, gm_tpl128vde47jcsu_i, gm_tpl128vde47jcsu_s};
const gm_tmpl gm_tpl1buo4rn7srlb = {5u, gm_tpl1buo4rn7srlb_p, gm_tpl1buo4rn7srlb_i, gm_tpl1buo4rn7srlb_s};
const gm_tmpl gm_tpl1dmvfuuebopd = {1u, gm_tpl1dmvfuuebopd_p, gm_tpl1dmvfuuebopd_i, gm_tpl1dmvfuuebopd_s};
const gm_tmpl gm_tpl1f5t4gadpj5n = {1u, gm_tpl1f5t4gadpj5n_p, gm_tpl1f5t4gadpj5n_i, gm_tpl1f5t4gadpj5n_s};
const gm_tmpl gm_tpl1ige45obm306 = {2u, gm_tpl1ige45obm306_p, gm_tpl1ige45obm306_i, gm_tpl1ige45obm306_s};
const gm_tmpl gm_tpl1rfj3tqlepqo = {1u, gm_tpl1rfj3tqlepqo_p, gm_tpl1rfj3tqlepqo_i, gm_tpl1rfj3tqlepqo_s};
const gm_tmpl gm_tpl2n8jqp2rhn52 = {2u, gm_tpl2n8jqp2rhn52_p, gm_tpl2n8jqp2rhn52_i, gm_tpl2n8jqp2rhn52_s};
const gm_tmpl gm_tpl3m6tjsa4flq8 = {1u, gm_tpl3m6tjsa4flq8_p, gm_tpl3m6tjsa4flq8_i, gm_tpl3m6tjsa4flq8_s};
const gm_tmpl gm_tpl3sg21sl0o56d = {1u, gm_tpl3sg21sl0o56d_p, gm_tpl3sg21sl0o56d_i, gm_tpl3sg21sl0o56d_s};
const gm_tmpl gm_tpl3skngiao4gkl = {1u, gm_tpl3skngiao4gkl_p, gm_tpl3skngiao4gkl_i, gm_tpl3skngiao4gkl_s};
const gm_tmpl gm_tpl5ikuibflsuu2 = {1u, gm_tpl5ikuibflsuu2_p, gm_tpl5ikuibflsuu2_i, gm_tpl5ikuibflsuu2_s};
const gm_tmpl gm_tpl5ldtdub2029b = {3u, gm_tpl5ldtdub2029b_p, gm_tpl5ldtdub2029b_i, gm_tpl5ldtdub2029b_s};
const gm_tmpl gm_tpl5rd4283boomi = {1u, gm_tpl5rd4283boomi_p, gm_tpl5rd4283boomi_i, gm_tpl5rd4283boomi_s};
const gm_tmpl gm_tpl5uqj3jbiq5f0 = {1u, gm_tpl5uqj3jbiq5f0_p, gm_tpl5uqj3jbiq5f0_i, gm_tpl5uqj3jbiq5f0_s};
const gm_tmpl gm_tpl5vbp9fc0mpc1 = {1u, gm_tpl5vbp9fc0mpc1_p, gm_tpl5vbp9fc0mpc1_i, gm_tpl5vbp9fc0mpc1_s};
const gm_tmpl gm_tpl6lpvgmmqlalm = {1u, gm_tpl6lpvgmmqlalm_p, gm_tpl6lpvgmmqlalm_i, gm_tpl6lpvgmmqlalm_s};
const gm_tmpl gm_tpl7avuc618ph2c = {1u, gm_tpl7avuc618ph2c_p, gm_tpl7avuc618ph2c_i, gm_tpl7avuc618ph2c_s};
const gm_tmpl gm_tpl7ll3166bkl17 = {2u, gm_tpl7ll3166bkl17_p, gm_tpl7ll3166bkl17_i, gm_tpl7ll3166bkl17_s};
const gm_tmpl gm_tpl81nh98hgo1rk = {1u, gm_tpl81nh98hgo1rk_p, gm_tpl81nh98hgo1rk_i, gm_tpl81nh98hgo1rk_s};
const gm_tmpl gm_tpl8eifhruu514e = {1u, gm_tpl8eifhruu514e_p, gm_tpl8eifhruu514e_i, gm_tpl8eifhruu514e_s};
const gm_tmpl gm_tpl8g9lchslmbjf = {2u, gm_tpl8g9lchslmbjf_p, gm_tpl8g9lchslmbjf_i, gm_tpl8g9lchslmbjf_s};
const gm_tmpl gm_tpl8mdj3nc4lpjv = {1u, gm_tpl8mdj3nc4lpjv_p, gm_tpl8mdj3nc4lpjv_i, gm_tpl8mdj3nc4lpjv_s};
const gm_tmpl gm_tpl8nvimfep33sn = {2u, gm_tpl8nvimfep33sn_p, gm_tpl8nvimfep33sn_i, gm_tpl8nvimfep33sn_s};
const gm_tmpl gm_tpl9aakii48cq8t = {2u, gm_tpl9aakii48cq8t_p, gm_tpl9aakii48cq8t_i, gm_tpl9aakii48cq8t_s};
const gm_tmpl gm_tpl9ft3vu1e30uh = {1u, gm_tpl9ft3vu1e30uh_p, gm_tpl9ft3vu1e30uh_i, gm_tpl9ft3vu1e30uh_s};
const gm_tmpl gm_tpl9jr09voqi3gh = {2u, gm_tpl9jr09voqi3gh_p, gm_tpl9jr09voqi3gh_i, gm_tpl9jr09voqi3gh_s};
const gm_tmpl gm_tpla0ci4vs242iu = {2u, gm_tpla0ci4vs242iu_p, gm_tpla0ci4vs242iu_i, gm_tpla0ci4vs242iu_s};
const gm_tmpl gm_tpla0dc0a9b9i8b = {1u, gm_tpla0dc0a9b9i8b_p, gm_tpla0dc0a9b9i8b_i, gm_tpla0dc0a9b9i8b_s};
const gm_tmpl gm_tplalgvrfo85soq = {1u, gm_tplalgvrfo85soq_p, gm_tplalgvrfo85soq_i, gm_tplalgvrfo85soq_s};
const gm_tmpl gm_tplbn5t5stpvru2 = {1u, gm_tplbn5t5stpvru2_p, gm_tplbn5t5stpvru2_i, gm_tplbn5t5stpvru2_s};
const gm_tmpl gm_tplbp75knfmjbql = {1u, gm_tplbp75knfmjbql_p, gm_tplbp75knfmjbql_i, gm_tplbp75knfmjbql_s};
const gm_tmpl gm_tplc8kkdoja48ii = {3u, gm_tplc8kkdoja48ii_p, gm_tplc8kkdoja48ii_i, gm_tplc8kkdoja48ii_s};
const gm_tmpl gm_tplc90dj04km0n5 = {2u, gm_tplc90dj04km0n5_p, gm_tplc90dj04km0n5_i, gm_tplc90dj04km0n5_s};
const gm_tmpl gm_tplca6pmroagoou = {3u, gm_tplca6pmroagoou_p, gm_tplca6pmroagoou_i, gm_tplca6pmroagoou_s};
const gm_tmpl gm_tplcpt7hf5iscqd = {1u, gm_tplcpt7hf5iscqd_p, gm_tplcpt7hf5iscqd_i, gm_tplcpt7hf5iscqd_s};
const gm_tmpl gm_tplcre592qvlrqu = {2u, gm_tplcre592qvlrqu_p, gm_tplcre592qvlrqu_i, gm_tplcre592qvlrqu_s};
const gm_tmpl gm_tples1m1m9iv6b0 = {6u, gm_tples1m1m9iv6b0_p, gm_tples1m1m9iv6b0_i, gm_tples1m1m9iv6b0_s};
const gm_tmpl gm_tplf4oacobin5qk = {1u, gm_tplf4oacobin5qk_p, gm_tplf4oacobin5qk_i, gm_tplf4oacobin5qk_s};
const gm_tmpl gm_tplfqk35ejq39la = {3u, gm_tplfqk35ejq39la_p, gm_tplfqk35ejq39la_i, gm_tplfqk35ejq39la_s};
const gm_tmpl gm_tplg21ilonldhva = {2u, gm_tplg21ilonldhva_p, gm_tplg21ilonldhva_i, gm_tplg21ilonldhva_s};
const gm_tmpl gm_tplgqr018o660ue = {1u, gm_tplgqr018o660ue_p, gm_tplgqr018o660ue_i, gm_tplgqr018o660ue_s};
const gm_tmpl gm_tplh7ffbsfnitkh = {2u, gm_tplh7ffbsfnitkh_p, gm_tplh7ffbsfnitkh_i, gm_tplh7ffbsfnitkh_s};
const gm_tmpl gm_tplhghj93ore8u2 = {1u, gm_tplhghj93ore8u2_p, gm_tplhghj93ore8u2_i, gm_tplhghj93ore8u2_s};
const gm_tmpl gm_tplhn9hprqs819d = {1u, gm_tplhn9hprqs819d_p, gm_tplhn9hprqs819d_i, gm_tplhn9hprqs819d_s};
const gm_tmpl gm_tplhonv8g349dv9 = {1u, gm_tplhonv8g349dv9_p, gm_tplhonv8g349dv9_i, gm_tplhonv8g349dv9_s};
const gm_tmpl gm_tpli0tmonaamd45 = {1u, gm_tpli0tmonaamd45_p, gm_tpli0tmonaamd45_i, gm_tpli0tmonaamd45_s};
const gm_tmpl gm_tplibfhnj0illo7 = {2u, gm_tplibfhnj0illo7_p, gm_tplibfhnj0illo7_i, gm_tplibfhnj0illo7_s};
const gm_tmpl gm_tpliufjdlri3svp = {2u, gm_tpliufjdlri3svp_p, gm_tpliufjdlri3svp_i, gm_tpliufjdlri3svp_s};
const gm_tmpl gm_tplj92mk62u70db = {1u, gm_tplj92mk62u70db_p, gm_tplj92mk62u70db_i, gm_tplj92mk62u70db_s};
const gm_tmpl gm_tplja7aur1oiv70 = {2u, gm_tplja7aur1oiv70_p, gm_tplja7aur1oiv70_i, gm_tplja7aur1oiv70_s};
const gm_tmpl gm_tpljcso3h5g4o29 = {1u, gm_tpljcso3h5g4o29_p, gm_tpljcso3h5g4o29_i, gm_tpljcso3h5g4o29_s};
const gm_tmpl gm_tpljicvo0ni8aba = {1u, gm_tpljicvo0ni8aba_p, gm_tpljicvo0ni8aba_i, gm_tpljicvo0ni8aba_s};
const gm_tmpl gm_tpljp2husef3b2l = {1u, gm_tpljp2husef3b2l_p, gm_tpljp2husef3b2l_i, gm_tpljp2husef3b2l_s};
const gm_tmpl gm_tplk0jab62rgl3v = {1u, gm_tplk0jab62rgl3v_p, gm_tplk0jab62rgl3v_i, gm_tplk0jab62rgl3v_s};
const gm_tmpl gm_tplk1botso018is = {1u, gm_tplk1botso018is_p, gm_tplk1botso018is_i, gm_tplk1botso018is_s};
const gm_tmpl gm_tplkjc5mpc5i80h = {2u, gm_tplkjc5mpc5i80h_p, gm_tplkjc5mpc5i80h_i, gm_tplkjc5mpc5i80h_s};
const gm_tmpl gm_tplkucr7969socr = {1u, gm_tplkucr7969socr_p, gm_tplkucr7969socr_i, gm_tplkucr7969socr_s};
const gm_tmpl gm_tpll6udkhd3c609 = {1u, gm_tpll6udkhd3c609_p, gm_tpll6udkhd3c609_i, gm_tpll6udkhd3c609_s};
const gm_tmpl gm_tpllpopiint29pt = {5u, gm_tpllpopiint29pt_p, gm_tpllpopiint29pt_i, gm_tpllpopiint29pt_s};
const gm_tmpl gm_tplm02t0g7r3hdp = {1u, gm_tplm02t0g7r3hdp_p, gm_tplm02t0g7r3hdp_i, gm_tplm02t0g7r3hdp_s};
const gm_tmpl gm_tplm6qp3tlbq28j = {1u, gm_tplm6qp3tlbq28j_p, gm_tplm6qp3tlbq28j_i, gm_tplm6qp3tlbq28j_s};
const gm_tmpl gm_tplmo1fik79j6ea = {1u, gm_tplmo1fik79j6ea_p, gm_tplmo1fik79j6ea_i, gm_tplmo1fik79j6ea_s};
const gm_tmpl gm_tplmsd0sfgf2v66 = {2u, gm_tplmsd0sfgf2v66_p, gm_tplmsd0sfgf2v66_i, gm_tplmsd0sfgf2v66_s};
const gm_tmpl gm_tpln4t9g9do4qg7 = {2u, gm_tpln4t9g9do4qg7_p, gm_tpln4t9g9do4qg7_i, gm_tpln4t9g9do4qg7_s};
const gm_tmpl gm_tplncm14tpa4cdg = {3u, gm_tplncm14tpa4cdg_p, gm_tplncm14tpa4cdg_i, gm_tplncm14tpa4cdg_s};
const gm_tmpl gm_tplnmm43n0jmcod = {1u, gm_tplnmm43n0jmcod_p, gm_tplnmm43n0jmcod_i, gm_tplnmm43n0jmcod_s};
const gm_tmpl gm_tplnr3ridveqobn = {1u, gm_tplnr3ridveqobn_p, gm_tplnr3ridveqobn_i, gm_tplnr3ridveqobn_s};
const gm_tmpl gm_tplnr9pmjldkl35 = {2u, gm_tplnr9pmjldkl35_p, gm_tplnr9pmjldkl35_i, gm_tplnr9pmjldkl35_s};
const gm_tmpl gm_tplo2fstigc8bcf = {5u, gm_tplo2fstigc8bcf_p, gm_tplo2fstigc8bcf_i, gm_tplo2fstigc8bcf_s};
const gm_tmpl gm_tplo4mr4l55q97r = {2u, gm_tplo4mr4l55q97r_p, gm_tplo4mr4l55q97r_i, gm_tplo4mr4l55q97r_s};
const gm_tmpl gm_tplo6lbijqleti1 = {7u, gm_tplo6lbijqleti1_p, gm_tplo6lbijqleti1_i, gm_tplo6lbijqleti1_s};
const gm_tmpl gm_tplogtdrvl783rp = {3u, gm_tplogtdrvl783rp_p, gm_tplogtdrvl783rp_i, gm_tplogtdrvl783rp_s};
const gm_tmpl gm_tploh8l9phm91d3 = {1u, gm_tploh8l9phm91d3_p, gm_tploh8l9phm91d3_i, gm_tploh8l9phm91d3_s};
const gm_tmpl gm_tploik9nhlf98dm = {3u, gm_tploik9nhlf98dm_p, gm_tploik9nhlf98dm_i, gm_tploik9nhlf98dm_s};
const gm_tmpl gm_tplopmhgmch9jsj = {1u, gm_tplopmhgmch9jsj_p, gm_tplopmhgmch9jsj_i, gm_tplopmhgmch9jsj_s};
const gm_tmpl gm_tplovlajhkvntql = {1u, gm_tplovlajhkvntql_p, gm_tplovlajhkvntql_i, gm_tplovlajhkvntql_s};
const gm_tmpl gm_tplp0abp7005sqm = {4u, gm_tplp0abp7005sqm_p, gm_tplp0abp7005sqm_i, gm_tplp0abp7005sqm_s};
const gm_tmpl gm_tplpfv6kueslks8 = {4u, gm_tplpfv6kueslks8_p, gm_tplpfv6kueslks8_i, gm_tplpfv6kueslks8_s};
const gm_tmpl gm_tplqipe054or34v = {1u, gm_tplqipe054or34v_p, gm_tplqipe054or34v_i, gm_tplqipe054or34v_s};
const gm_tmpl gm_tplqk3ejs6j0ne5 = {2u, gm_tplqk3ejs6j0ne5_p, gm_tplqk3ejs6j0ne5_i, gm_tplqk3ejs6j0ne5_s};
const gm_tmpl gm_tplqm5shuhajt4a = {1u, gm_tplqm5shuhajt4a_p, gm_tplqm5shuhajt4a_i, gm_tplqm5shuhajt4a_s};
const gm_tmpl gm_tplrh70572i8ko0 = {1u, gm_tplrh70572i8ko0_p, gm_tplrh70572i8ko0_i, gm_tplrh70572i8ko0_s};
const gm_tmpl gm_tpls19nuda8st36 = {1u, gm_tpls19nuda8st36_p, gm_tpls19nuda8st36_i, gm_tpls19nuda8st36_s};
const gm_tmpl gm_tpls4ecclriepmb = {2u, gm_tpls4ecclriepmb_p, gm_tpls4ecclriepmb_i, gm_tpls4ecclriepmb_s};
const gm_tmpl gm_tpls6dj3unub5ff = {2u, gm_tpls6dj3unub5ff_p, gm_tpls6dj3unub5ff_i, gm_tpls6dj3unub5ff_s};
const gm_tmpl gm_tplsa4s74k8e6ci = {1u, gm_tplsa4s74k8e6ci_p, gm_tplsa4s74k8e6ci_i, gm_tplsa4s74k8e6ci_s};
const gm_tmpl gm_tplt1si3io9mdpe = {4u, gm_tplt1si3io9mdpe_p, gm_tplt1si3io9mdpe_i, gm_tplt1si3io9mdpe_s};
const gm_tmpl gm_tplt9o3qfo5dqct = {1u, gm_tplt9o3qfo5dqct_p, gm_tplt9o3qfo5dqct_i, gm_tplt9o3qfo5dqct_s};
const gm_tmpl gm_tpltj7umjd53udq = {1u, gm_tpltj7umjd53udq_p, gm_tpltj7umjd53udq_i, gm_tpltj7umjd53udq_s};
const gm_tmpl gm_tplu0ijdfgp8vs3 = {1u, gm_tplu0ijdfgp8vs3_p, gm_tplu0ijdfgp8vs3_i, gm_tplu0ijdfgp8vs3_s};
const gm_tmpl gm_tplu7d9ofnf77qa = {1u, gm_tplu7d9ofnf77qa_p, gm_tplu7d9ofnf77qa_i, gm_tplu7d9ofnf77qa_s};
const gm_tmpl gm_tplu9uohqqitldd = {3u, gm_tplu9uohqqitldd_p, gm_tplu9uohqqitldd_i, gm_tplu9uohqqitldd_s};
const gm_tmpl gm_tplurleasos6378 = {1u, gm_tplurleasos6378_p, gm_tplurleasos6378_i, gm_tplurleasos6378_s};
const gm_tmpl gm_tplutkid6t0497p = {1u, gm_tplutkid6t0497p_p, gm_tplutkid6t0497p_i, gm_tplutkid6t0497p_s};
const gm_tmpl gm_tplv8tsq846c0lj = {2u, gm_tplv8tsq846c0lj_p, gm_tplv8tsq846c0lj_i, gm_tplv8tsq846c0lj_s};
const gm_tmpl_piece gm_tpl00rv7vuig9ur_p[] = {{gm_sujqb5peare4g, 5}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpl0j5i33et5u62_p[] = {{gm_stmsadvf54k73, 2}, {gm_sg8f7kt1n4uc5, 47}};
const gm_tmpl_piece gm_tpl0lggla435q5r_p[] = {{gm_ss88mk7b1rh87, 17}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpl0pva6pvhuq6i_p[] = {{gm_smooaouqmm8dj, 1}, {gm_s2kieedtscsk1, 11}, {gm_stc3e0j0moc2h, 4}, {gm_sqvepl1893o5g, 2}};
const gm_tmpl_piece gm_tpl0q3rsndpu857_p[] = {{gm_sh7dg9dli11hk, 49}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl0v0dinrpdv6d_p[] = {{gm_shecus79pkigk, 18}, {gm_sqt8gk1nsci25, 4}};
const gm_tmpl_piece gm_tpl150n44984e0j_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sl3q52ep6aa53, 1}, {gm_sl3q52ep6aa53, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl15v8rvh46nek_p[] = {{gm_svdd37ikql4fk, 44}, {gm_sod9r7r024h9q, 5}};
const gm_tmpl_piece gm_tpl1g7tc1f10noh_p[] = {{gm_sbfc2b8kquvq8, 8}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl1irt5kietk82_p[] = {{gm_ss3tu1f38tkcj, 5}, {gm_sjfdnh735o7f8, 4}, {gm_srv4dcfccuset, 2}, {gm_sujpen8u5kmon, 9}, {gm_sba439nre06r4, 5}, {gm_spod3tmkh25up, 1}};
const gm_tmpl_piece gm_tpl29pahnav2feq_p[] = {{gm_stkm2m8f3okk8, 17}, {gm_s99ll9oc7rh5u, 14}, {gm_s4s1nl0uacdl1, 21}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl3052vn0bjbcl_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sggm36eg9tkga, 9}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl3072uo9s1dtn_p[] = {{gm_s5pra70i8o734, 13}, {gm_s08dcqdp58r5f, 2}, {gm_s1d673tn03i3r, 4}, {gm_spod3tmkh25up, 1}};
const gm_tmpl_piece gm_tpl34g57qfcenfh_p[] = {{gm_s5gt30aqh30fl, 18}, {gm_s4efr72f9s8j4, 28}};
const gm_tmpl_piece gm_tpl3cldid2v21cr_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_soaukr1udufqk, 18}, {gm_s6vstugpp3ivo, 57}};
const gm_tmpl_piece gm_tpl4bpbmadpmu1i_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s39mljh669qgl, 1}, {gm_s39mljh669qgl, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl4g1h0tr3apfi_p[] = {{gm_sgh1o65rai9bu, 7}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl4s7g8d4v4djl_p[] = {{gm_su5bsj1nioctr, 21}, {gm_skfo9c0nr3nrd, 49}};
const gm_tmpl_piece gm_tpl4vjhulqo9e1j_p[] = {{gm_sf9a2kg3eo1gs, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl500h9j75csqa_p[] = {{gm_scnb9tl13g4lt, 7}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl55mhnmub0ffl_p[] = {{gm_srk8hgivpmv60, 17}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tpl59r7h6035548_p[] = {{gm_svu4pa4qq20q7, 5}, {gm_sfdbfrv46lihd, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl5qgrl10qvakk_p[] = {{gm_sqadouunp729f, 66}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpl606t7k5n88ss_p[] = {{gm_s6lr92ekld44s, 12}, {gm_s966nqk7bt67q, 18}, {gm_sjfrmur8rvp24, 33}};
const gm_tmpl_piece gm_tpl6fas3mugop2g_p[] = {{gm_sn57mt8igjdaa, 4}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl6muahgpe70pr_p[] = {{gm_sdhlphf1ucpbo, 17}, {gm_s8pjstf2pdmbk, 13}, {gm_sb9dglh7ekrt2, 6}};
const gm_tmpl_piece gm_tpl6ou7bec1fkae_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sl0kpeuro9al4, 22}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpl7vtkjsp2pa8b_p[] = {{gm_sj6ih18t5qqaf, 35}, {gm_sr4qbrk6tlgaa, 0}, {gm_s3p0rbs9hc3lg, 1}, {gm_s76ovr591an92, 3}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpl8eifhruu514e_p[] = {{gm_s0r71f5repum2, 20}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpl91a2oiuc6ot7_p[] = {{gm_stmsadvf54k73, 2}, {gm_s36eqnq31lemp, 28}, {gm_s96bbamhk00af, 28}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpl98etevndg0cj_p[] = {{gm_so9fi6r28770a, 2}, {gm_s338jbcmhh92n, 19}};
const gm_tmpl_piece gm_tpl9j9ok1j4nn18_p[] = {{gm_smooaouqmm8dj, 1}, {gm_sjltai3b82h79, 3}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpl9l4fqsojrjsa_p[] = {{gm_sehat3g6ic3fn, 49}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpla9stdteeiicv_p[] = {{gm_sciqh24mne54a, 3}, {gm_sfad5qbegckoi, 27}};
const gm_tmpl_piece gm_tplamm5t71udd8k_p[] = {{gm_s5gkjub7hejc1, 8}, {gm_s2aj89r6sdims, 27}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplaqps986okebj_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sfg8mkf2826qa, 4}, {gm_sqrjr8qjf37v8, 16}};
const gm_tmpl_piece gm_tplb86aokjj8d2b_p[] = {{gm_sp6eh19rmca8b, 35}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplbbopfj3f21eb_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplbgu1ah9e6d87_p[] = {{gm_s50pmptf3idds, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplbnt4jnaihcvu_p[] = {{gm_s1idibqkta392, 6}, {gm_sfsrjkeonr0ad, 50}};
const gm_tmpl_piece gm_tplbntllbe37f81_p[] = {{gm_sduk7s8m3amo6, 11}, {gm_so514oj5nbhcu, 1}};
const gm_tmpl_piece gm_tplccs2j0kgin1q_p[] = {{gm_snbque26o7lbi, 6}, {gm_s3p0rbs9hc3lg, 1}, {gm_slgmqgkioa4u6, 2}, {gm_sr4qbrk6tlgaa, 0}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplcilp3mvhfo4n_p[] = {{gm_s4nddosr4taav, 4}, {gm_sufk3vqnaden1, 2}};
const gm_tmpl_piece gm_tplcre592qvlrqu_p[] = {{gm_sv61f25m0b9gs, 3}, {gm_sl3q52ep6aa53, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpldg1m204qb08c_p[] = {{gm_sdtjos7av9cn7, 21}, {gm_sldjvqvpa1t8p, 15}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpldqr3c69jgnqi_p[] = {{gm_sql9r2b8fvivt, 9}, {gm_s0eudsv4chfi5, 43}};
const gm_tmpl_piece gm_tpldtv4ooqtkvgm_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s03pmf867dcsr, 1}};
const gm_tmpl_piece gm_tple23ed5kepfqi_p[] = {{gm_s3kb1co7e63ka, 36}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tple7l1inipu3bb_p[] = {{gm_sdpmg51hc7l8e, 12}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplfe53mjoakni4_p[] = {{gm_sat7ifv94mb2p, 39}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplfm1jmgsrn2ua_p[] = {{gm_se4r520bkfair, 17}, {gm_sav0q47tkm7j2, 19}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplg4866gcgr837_p[] = {{gm_sor30tetra4nt, 4}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplg4k0uitmfhf0_p[] = {{gm_siamquc19uikr, 1}, {gm_sl4ilt8nbcp28, 14}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplgfhhtcka09o4_p[] = {{gm_sv7ptc4il3v0u, 14}, {gm_srefpimq1sas7, 3}, {gm_sno069vrt5g9q, 3}, {gm_s6t4r831mukdr, 2}};
const gm_tmpl_piece gm_tplgjeridtvkml4_p[] = {{gm_sq44fbs78td4m, 32}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplgqohfqetaph7_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_si7qrooamq5ei, 25}};
const gm_tmpl_piece gm_tplgs2l60s65hm3_p[] = {{gm_sio9tu2ig98ap, 20}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplgt6vkfjdh8cd_p[] = {{gm_sc4uc8von90he, 5}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplh2em7r1dmffm_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_s52n7ji3kulf2, 28}};
const gm_tmpl_piece gm_tplh7ffbsfnitkh_p[] = {{gm_shsifme8hcenm, 16}, {gm_slgmqgkioa4u6, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplj3c7bqq8809l_p[] = {{gm_s7knd69l0mhc0, 35}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplj6ss1j7sgouf_p[] = {{gm_s7f8bb20dn4gg, 20}, {gm_s7nnecs6i9eki, 40}};
const gm_tmpl_piece gm_tplj9vh1qm5m031_p[] = {{gm_s08olofbep0dd, 13}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplk0jab62rgl3v_p[] = {{gm_sg7q4ebvc557r, 13}, {gm_sd99er7oafmtu, 16}};
const gm_tmpl_piece gm_tplk27ieg1b14ol_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplk51k04l6756s_p[] = {{gm_sgankvlme129s, 2}, {gm_s2urait735ofq, 4}};
const gm_tmpl_piece gm_tplkjh7li83dd9g_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_smooaouqmm8dj, 1}, {gm_sg5gdnrith2i6, 2}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplkkvn546crm4n_p[] = {{gm_smooaouqmm8dj, 1}, {gm_svhavo13qha5k, 3}, {gm_s1asl1e1tvb0k, 1}, {gm_s9ff7kbg5s1vv, 9}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpll408251sp58f_p[] = {{gm_siamquc19uikr, 1}, {gm_sk16sqfpds4ga, 17}};
const gm_tmpl_piece gm_tpll6pilv9krmhr_p[] = {{gm_st7fm661apctp, 16}, {gm_sa9dtqeprt7kf, 74}};
const gm_tmpl_piece gm_tpll8q455cp9mhi_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s3p0rbs9hc3lg, 1}, {gm_sg4u1qvcufkum, 4}};
const gm_tmpl_piece gm_tpllmofqkiu75lh_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s1faagcukkgns, 9}, {gm_s7t1ot7rgnuch, 10}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplls1lesito750_p[] = {{gm_sa9ehjurfkhlj, 16}, {gm_sqvepl1893o5g, 2}};
const gm_tmpl_piece gm_tplmr0a02ecbaom_p[] = {{gm_s480gcu6od53p, 60}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplmsb1klg4n6ap_p[] = {{gm_sj70rum4dd30i, 17}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpln6vfieiab6l5_p[] = {{gm_s4kq1149c9fl2, 54}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplngekt6vfvnsv_p[] = {{gm_shvak64vhokft, 25}, {gm_so514oj5nbhcu, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplokoueqaolfmt_p[] = {{gm_srne5d503fcjg, 4}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplp3rnngb19vkd_p[] = {{gm_sup9af88eqfm1, 24}, {gm_s8c4bejqsu1da, 47}};
const gm_tmpl_piece gm_tplp72dgv8cvduk_p[] = {{gm_sd1ldutuv1qfu, 11}, {gm_sqaji2d91cvct, 18}, {gm_s1nt8u0pgo6vn, 16}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplpog54do9oh2a_p[] = {{gm_so9fi6r28770a, 2}, {gm_s1asl1e1tvb0k, 1}, {gm_sel0tu7gvv1na, 3}};
const gm_tmpl_piece gm_tplq2jm9n7vekeo_p[] = {{gm_sokap7vn66boo, 52}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplqbbmojia0cmf_p[] = {{gm_sn7tl9jolqoj1, 2}};
const gm_tmpl_piece gm_tplqfu63ajpo70a_p[] = {{gm_smooaouqmm8dj, 1}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplqnufra7h2p1s_p[] = {{gm_s03c24qg7i06q, 12}, {gm_s3d3pns6au9om, 60}};
const gm_tmpl_piece gm_tplr1if0cs3oolt_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sbbgtkch3q4n5, 2}};
const gm_tmpl_piece gm_tplr2tuff1mvdst_p[] = {{gm_s6g8mt9e5cjjr, 10}, {gm_s5k6emuk51u47, 26}};
const gm_tmpl_piece gm_tplrivm1rlf1ps3_p[] = {{gm_spgt2nbblhu8j, 8}, {gm_st6hj8v0h80gt, 5}, {gm_ssjhh9bdn23jf, 16}};
const gm_tmpl_piece gm_tplrp7ukmgscavj_p[] = {{gm_so9fi6r28770a, 2}, {gm_sq6v0u0m2nfv8, 22}, {gm_skqf89fdi6p8e, 11}, {gm_sn1seggk13bop, 3}};
const gm_tmpl_piece gm_tpls2fio2pf4rdq_p[] = {{gm_so9fi6r28770a, 2}, {gm_shem79vgdue54, 4}, {gm_s1asl1e1tvb0k, 1}, {gm_sb398tg9q30m5, 5}, {gm_su7f9sbon2vv7, 5}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpls3402qmr7otb_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sag4ku07t2k8t, 24}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplso4fjesdi372_p[] = {{gm_smror2m9tsk7v, 7}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplt1i3tc7bfdhf_p[] = {{gm_sdslahffv7339, 16}, {gm_s6jlko9gsl7im, 25}, {gm_smeu8adf6pdko, 37}};
const gm_tmpl_piece gm_tplt392n8hqfhn9_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplt3iub1goev8r_p[] = {{gm_s0jrrhhm4skoj, 14}, {gm_sp1nce97af2b9, 5}, {gm_sgq8hfs47hogb, 8}};
const gm_tmpl_piece gm_tplt524tdio8698_p[] = {{gm_snl36eh5psull, 6}, {gm_so514oj5nbhcu, 1}};
const gm_tmpl_piece gm_tplt56hco5v1mvf_p[] = {{gm_s588gob2um10i, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplu0n2ra1s30o8_p[] = {{gm_sql9r2b8fvivt, 9}, {gm_sp0ce5j43v7u2, 25}, {gm_socbkiss431o8, 17}};
const gm_tmpl_piece gm_tplu2kaori4llai_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s8b01h2tetj3e, 44}, {gm_sjnkguo0atpm5, 2}};
const gm_tmpl_piece gm_tplu2tisf3oaee8_p[] = {{gm_sts2e9lggtpr2, 46}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplumuahjb5m8m8_p[] = {{gm_s6u49d04cqlg1, 16}, {gm_s33h2lce46bnm, 8}, {gm_spa4t2l419v39, 13}};
const gm_tmpl_piece gm_tplupuvr4efpn1v_p[] = {{gm_sut7mhrkhqp77, 55}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpluue9jh13g9df_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sht5iivcbi64u, 11}, {gm_st7j9rqo01n3i, 16}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplv516qd7l6o3q_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sr4qbrk6tlgaa, 0}, {gm_sjbtn8nt9amae, 2}, {gm_spod3tmkh25up, 1}};
const gm_tmpl_piece gm_tplvbotf15ps5s5_p[] = {{gm_scai7a7ojvpjh, 18}, {gm_st8i5uhn201op, 36}};
const gm_type gm_tytje7cegoml = {.size = 1, .align = 1, .name = "SysOp", .kind = GM_K_UNION, .nvariants = 55, .variants = gm_tytje7cegoml_v, .tag_offset = 0, .payload_offset = 0, .tag_size = 1, .bits = 8, .tag_signed = false};
const gm_type gm_tytje7cegoml;
const uint32_t gm_tpl00b67aeoiuop_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl051t251mssfu_i[] = {0u};
const uint32_t gm_tpl0fdke0kkivcj_i[] = {0u, 1u};
const uint32_t gm_tpl0qht8d5me9ih_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tpl12e8u6hvkejb_i[] = {0u, 1u};
const uint32_t gm_tpl14b6qietv6bm_i[] = {0u, 1u};
const uint32_t gm_tpl14n6ndgi62j1_i[] = {0u, 1u};
const uint32_t gm_tpl17l9mok2be4a_i[] = {0u};
const uint32_t gm_tpl1jgrbdjt2sm3_i[] = {0u};
const uint32_t gm_tpl1lmdkbkeigtd_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tpl2d7ij9b2sa2u_i[] = {0u};
const uint32_t gm_tpl3lqpj3d2egtb_i[] = {0u, 1u};
const uint32_t gm_tpl4vjhulqo9e1j_i[] = {0u};
const uint32_t gm_tpl5h4mc65ti3jt_i[] = {0u};
const uint32_t gm_tpl5hraomijgptv_i[] = {0u};
const uint32_t gm_tpl5m2t8us2v800_i[] = {0u, 1u};
const uint32_t gm_tpl5qm9nkkj1gjg_i[] = {0u, 1u};
const uint32_t gm_tpl6d7dqphpmhs7_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl6fm59pne5ng7_i[] = {0u};
const uint32_t gm_tpl6lldue1mm65c_i[] = {0u};
const uint32_t gm_tpl6rsespo9a6nq_i[] = {0u};
const uint32_t gm_tpl70tkged35ahl_i[] = {0u, 1u};
const uint32_t gm_tpl7net9ifts13e_i[] = {0u};
const uint32_t gm_tpl8ocve2bpsu72_i[] = {0u};
const uint32_t gm_tpl9fn8oj7nnnsq_i[] = {0u};
const uint32_t gm_tpl9q5j6bisf2fa_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tpla0a5a633lqve_i[] = {0u};
const uint32_t gm_tpla0ldfomuin4r_i[] = {0u, 1u, 2u};
const uint32_t gm_tplanhmvcr622qo_i[] = {0u};
const uint32_t gm_tplb9surs05k5po_i[] = {0u, 1u};
const uint32_t gm_tplbaonrrpbr1uh_i[] = {0u};
const uint32_t gm_tplbds86ojmcoik_i[] = {0u, 1u};
const uint32_t gm_tplbk1k026jl879_i[] = {0u, 1u};
const uint32_t gm_tplbo9rmalpf2vj_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tplcb5rgmp3lpr2_i[] = {0u};
const uint32_t gm_tplcoet0p57pg4s_i[] = {0u, 1u, 2u, 3u, 4u};
const uint32_t gm_tpld1ficc6ikf1e_i[] = {0u};
const uint32_t gm_tpldfm52le63mtn_i[] = {0u, 1u};
const uint32_t gm_tpldj14c3jgvlnb_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tpldl4pn21ood35_i[] = {0u};
const uint32_t gm_tpldm0f92v61rgi_i[] = {0u};
const uint32_t gm_tpldu1003b7br77_i[] = {0u, 1u, 2u};
const uint32_t gm_tple4r0jrrru5m7_i[] = {0u};
const uint32_t gm_tple68gu1adcjq9_i[] = {0u};
const uint32_t gm_tpleie0q8nvjqch_i[] = {0u};
const uint32_t gm_tplellk9btkq1f2_i[] = {0u, 1u};
const uint32_t gm_tplf2fho9tvid0p_i[] = {0u};
const uint32_t gm_tplf6hbp7abojsd_i[] = {0u, 1u};
const uint32_t gm_tplgan4klju39em_i[] = {0u};
const uint32_t gm_tplggs57b0p2or4_i[] = {0u};
const uint32_t gm_tplgp15dehsbtkk_i[] = {0u};
const uint32_t gm_tplh8soes50a6ps_i[] = {0u, 1u};
const uint32_t gm_tplhatoh83mi4is_i[] = {0u};
const uint32_t gm_tplhdb29uo8rm6j_i[] = {0u, 1u};
const uint32_t gm_tplhl4q6p2b5f92_i[] = {0u};
const uint32_t gm_tplhonv8g349dv9_i[] = {0u};
const uint32_t gm_tplhs7ofk4c8qgg_i[] = {0u};
const uint32_t gm_tpliq0iop4ulk1g_i[] = {0u, 1u};
const uint32_t gm_tpliq8p2spcvq06_i[] = {0u, 1u};
const uint32_t gm_tpliufjdlri3svp_i[] = {0u, 1u};
const uint32_t gm_tplj7a4hf9tva3v_i[] = {0u};
const uint32_t gm_tplj7j0vuugbh2u_i[] = {0u, 1u};
const uint32_t gm_tplj9vh1qm5m031_i[] = {0u};
const uint32_t gm_tpljbco1er3agg5_i[] = {0u, 1u, 2u};
const uint32_t gm_tpljpjg7sv41er0_i[] = {0u};
const uint32_t gm_tpljtaapes6rh2u_i[] = {0u, 1u};
const uint32_t gm_tplk0jab62rgl3v_i[] = {0u};
const uint32_t gm_tplkte26oe5429d_i[] = {0u};
const uint32_t gm_tpll6jntfkrhtt4_i[] = {0u, 1u};
const uint32_t gm_tpll738pfnpjq8c_i[] = {0u};
const uint32_t gm_tpllaqikqjbb69k_i[] = {0u};
const uint32_t gm_tpllbgi055uoiek_i[] = {0u, 1u};
const uint32_t gm_tpllm39516ci1mu_i[] = {0u};
const uint32_t gm_tpllmofqkiu75lh_i[] = {0u, 1u, 2u};
const uint32_t gm_tpllsre3itruefn_i[] = {0u, 1u};
const uint32_t gm_tplm6g8a9odsasi_i[] = {0u};
const uint32_t gm_tplm6qp3tlbq28j_i[] = {0u};
const uint32_t gm_tplmekan10d5buf_i[] = {0u};
const uint32_t gm_tplmk3amvcsk2gu_i[] = {0u, 1u};
const uint32_t gm_tpln6vfieiab6l5_i[] = {0u};
const uint32_t gm_tplneb8585d50a4_i[] = {0u};
const uint32_t gm_tplnh359qrng71h_i[] = {0u, 1u};
const uint32_t gm_tplodlbfsp8tfu3_i[] = {0u, 1u, 2u, 3u, 4u, 5u};
const uint32_t gm_tplooqfb0umhrq0_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tplp3rnngb19vkd_i[] = {0u};
const uint32_t gm_tplpj1vfgivuj13_i[] = {0u};
const uint32_t gm_tplptgaia3s19c9_i[] = {0u};
const uint32_t gm_tplpvh6srii84sj_i[] = {0u};
const uint32_t gm_tplq0mvos8lisnf_i[] = {0u};
const uint32_t gm_tplq0stkp3a31gd_i[] = {0u, 1u, 2u, 3u, 4u};
const uint32_t gm_tplqfu63ajpo70a_i[] = {0u};
const uint32_t gm_tplqjfk6t5rn06e_i[] = {0u, 1u};
const uint32_t gm_tplqnau1elkkogs_i[] = {0u, 1u};
const uint32_t gm_tplqug1td3hpetg_i[] = {0u, 1u};
const uint32_t gm_tplr2c5vmkfj9df_i[] = {0u, 1u};
const uint32_t gm_tplr9mrmvfj4528_i[] = {0u};
const uint32_t gm_tplrlqq48dtu7p5_i[] = {0u};
const uint32_t gm_tpls4ecclriepmb_i[] = {0u, 1u};
const uint32_t gm_tpltdoamgi2k9f9_i[] = {0u, 1u};
const uint32_t gm_tplu2kaori4llai_i[] = {0u, 1u};
const uint32_t gm_tplu4465vjgoqre_i[] = {0u};
const uint32_t gm_tplu58b6rd4mq28_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tpluf7u812t4p3d_i[] = {0u};
const uint32_t gm_tplv8ckuk7p1s8m_i[] = {0u, 1u};
const uint32_t gm_tplvonunpuh0kn8_i[] = {0u};
const uint32_t gm_tplvqfop4uu1u5q_i[] = {0u, 1u};
const uint8_t gm_s0116oq3lk1rs[4] = "`\012\012";
const uint8_t gm_s01fsr0ku4o6k[8] = "typedef";
const uint8_t gm_s01kcog235p88[45] = "another function is already exported as `{}`";
const uint8_t gm_s03c24qg7i06q[13] = "dependency `";
const uint8_t gm_s04a35h53le03[43] = "this call is not an iterator (it returns `";
const uint8_t gm_s06nnq6fe98bg[53] = "`{}` scales `{}` by its element type `{}`, not by {}";
const uint8_t gm_s0963m82avtn1[23] = "%{} is not a temporary";
const uint8_t gm_s0ajhhgcemkg1[18] = "` has no column `";
const uint8_t gm_s0b4nsmmmqukr[12] = "float lanes";
const uint8_t gm_s0fpu2ivt8rj5[8] = "gm_find";
const uint8_t gm_s0irgfjsc3pat[76] = "unexpected whitespace character U+{:04X}; only space and tab are whitespace";
const uint8_t gm_s0jbakoflqfkm[2] = "k";
const uint8_t gm_s0rhh4q7cpm4k[20] = ".next); continue; }";
const uint8_t gm_s15gg5sblr4kl[4] = " s\012";
const uint8_t gm_s170memutnvs0[19] = "make_prerequisites";
const uint8_t gm_s174sqhpcliev[43] = "expected an integer array length, found a ";
const uint8_t gm_s19ndlkgdtviu[6] = "named";
const uint8_t gm_s1b5tn3crejqs[11] = "; memset(&";
const uint8_t gm_s1b80v2hb8d4l[72] = "if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == ";
const uint8_t gm_s1c0ll7cvuidm[12] = "`for` loops";
const uint8_t gm_s1c4sfff8ac39[7] = "is not";
const uint8_t gm_s1emqv9srjhhk[38] = " is not converted to it; spread it: `";
const uint8_t gm_s1ffqahkqb2ed[141] = "an optional is not a union: match absence with `null` and the present value with a plain pattern, as in `null => \342\200\246` and `v => \342\200\246` (\302\2479.3)";
const uint8_t gm_s1fsfd0lcgf8e[17] = "dangling-address";
const uint8_t gm_s1g2ac815dje3[15] = "switch ({}) {{";
const uint8_t gm_s1h7h9updp99l[54] = "an index must have an unsigned integer type, not `{}`";
const uint8_t gm_s1mgkad218qag[19] = "unsupported URI {}";
const uint8_t gm_s1pfh89airaa0[13] = "thread_spawn";
const uint8_t gm_s1pmmpf8t54g4[23] = "`{}` takes no receiver";
const uint8_t gm_s1s3m1fet79ge[7] = "gm_hcx";
const uint8_t gm_s1u0odc4938o8[58] = "` is not a label of an enclosing `while`, `for` or `with`";
const uint8_t gm_s1uq73uineubo[35] = "GM_ST({}, (uint8_t *)&{}, ({}){});";
const uint8_t gm_s244ts3klchej[25] = "to begin the effect body";
const uint8_t gm_s24chpb914bep[6] = "IsInf";
const uint8_t gm_s24s9g8r9gc0v[7] = "` to `";
const uint8_t gm_s26ul1d5snk55[9] = "env_exit";
const uint8_t gm_s27tmg3h56aru[53] = "missing argument for the compile-time parameter `{}`";
const uint8_t gm_s28c3rj06c7d7[50] = "a carriage return must be followed by a line feed";
const uint8_t gm_s2c2r23li2if2[8] = "to free";
const uint8_t gm_s2cgbvfh1b7rp[6] = "clang";
const uint8_t gm_s2esb38j7hkqv[31] = "to pass its result, call it: `";
const uint8_t gm_s2g22j1o1t23e[4] = ")) ";
const uint8_t gm_s2gtin895e08a[18] = "GM_FAST_MATH_BODY";
const uint8_t gm_s2lcii16lv48g[9] = "{} or {}";
const uint8_t gm_s2lldqflqcpmt[3] = "_v";
const uint8_t gm_s2qcdb7brsccv[123] = "this points into a field of a `#packed` struct, where a `{}` may be misaligned; copy the field to a local first (\302\2475.10.4)";
const uint8_t gm_s2uphqpvb2vhh[4] = "Div";
const uint8_t gm_s378kfmdl5enf[17] = "1 value is given";
const uint8_t gm_s38rtd27u976k[15] = "stack overflow";
const uint8_t gm_s398novfl4jrd[5] = "sqrt";
const uint8_t gm_s39bonkvupk3m[7] = "gm_buf";
const uint8_t gm_s3dccpt7atned[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\251\000\000\000\025\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_s3dflsv7jdvmc[44] = "internal error: a jump out of deferred code";
const uint8_t gm_s3e818t2qbjbh[5] = "addr";
const uint8_t gm_s3erfe82rop7u[5] = "<{}>";
const uint8_t gm_s3fj8brek97co[86] = "a constant pattern needs an integer, `bool` or string, not `{}`; compare with a guard";
const uint8_t gm_s3g9qhf2ithea[17] = "\000\000\000\000\000\000\000\000\032\004\000\000\000\000\000\000";
const uint8_t gm_s3la5l2o2cgbq[43] = " (\302\2477.4.7): write the other operand as one";
const uint8_t gm_s3lik4tvvjana[65] = "`#bits` takes the underlying unsigned integer type: `#bits(u16)`";
const uint8_t gm_s3o2km4i906va[32] = "constant `{}` depends on itself";
const uint8_t gm_s3ol5q9br79vf[8] = "FsLstat";
const uint8_t gm_s3p17h7dbk8us[8] = "_string";
const uint8_t gm_s3t7gv740qicd[10] = "intrinsic";
const uint8_t gm_s3thrieq7sddl[23] = "` is not available on ";
const uint8_t gm_s3tv2ib2tuvge[7] = "union(";
const uint8_t gm_s40jlud5pi248[16] = "{} placeholders";
const uint8_t gm_s41bap0m04tt9[12] = " (expected ";
const uint8_t gm_s41ef4vrm07q0[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000\212\000\000\000\026\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000";
const uint8_t gm_s44ofm0qft8r4[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\361\001\000\000\032\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000";
const uint8_t gm_s49hc37hqf8v6[13] = "atomic_store";
const uint8_t gm_s4efr72f9s8j4[29] = "] != 0; h = gm_mix64(h, s);\012";
const uint8_t gm_s4ehc7fsh7k20[42] = "after the effect of a handler declaration";
const uint8_t gm_s4ehfliahvei9[6] = "{}/{}";
const uint8_t gm_s4h2qpm1qm9a0[8] = "binary ";
const uint8_t gm_s4hd92lokmtot[4] = ">> ";
const uint8_t gm_s4hpf7ii3vj1c[4] = "-MF";
const uint8_t gm_s4jmvuc273b30[12] = "block_typed";
const uint8_t gm_s4k36hl9k22np[63] = "the archive contains the path `{}`, which leaves its directory";
const uint8_t gm_s4kq1149c9fl2[55] = "a math vector's elements are integers or floats, not `";
const uint8_t gm_s4p53khe03les[18] = "{} {} = {} >> {};";
const uint8_t gm_s4po5fk785g2b[37] = "invalid tag value S({}) for union {}";
const uint8_t gm_s4q8cddc9473e[14] = "(void *)&{}, ";
const uint8_t gm_s512m3mlvu0cv[106] = "(\342\200\246)` with only the types and `::` values, or give a function pointer type that determines them (\302\24716.7)";
const uint8_t gm_s516pv14t5t6i[73] = "`#soa` cannot store `{}`: the fields of a `#bits` struct have no address";
const uint8_t gm_s51lfi73niqcd[62] = "    if (!gm_unw.kind) gm_trap(\"a fiber's function returned\", ";
const uint8_t gm_s521on3hdq96i[4] = "!{}";
const uint8_t gm_s55bilge3vko0[13] = "; i++) if (!";
const uint8_t gm_s55g9qp4arkn7[8] = "gm_rt.h";
const uint8_t gm_s585om8jb9mop[25] = " exists and is not empty";
const uint8_t gm_s5fdm0n0s8bq4[30] = "unknown calling convention `.";
const uint8_t gm_s5npk1gmjsfm6[16] = "gm_round_digits";
const uint8_t gm_s5t2ui8o1e0s4[9] = "\027\000\000\000\000\000\000\000";
const uint8_t gm_s5u2kh0r9cm9p[7] = "gm_unw";
const uint8_t gm_s5vn66tnlipdf[19] = "gm_run_main_fibers";
const uint8_t gm_s619rbci7p06v[20] = "({})({} >> (int){})";
const uint8_t gm_s65khs4s753q2[11] = "net_listen";
const uint8_t gm_s6jnas2nvtoja[41] = "\000\000\000\000\000\000\000\000\033\000\000\000\000\000\000\000K\001\000\000]\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000";
const uint8_t gm_s6k5k2kcfu4ll[10] = "coverage-";
const uint8_t gm_s6l4819n1e7ou[9] = ", true, ";
const uint8_t gm_s6mq991cbd8md[53] = "__typeof__({}) {} = {}; memcpy(&{}, &{}, sizeof {});";
const uint8_t gm_s6pnfrub0rj1t[6] = "fetch";
const uint8_t gm_s6srmcrvka8iq[32] = " = gm_env_var((const uint8_t *)";
const uint8_t gm_s6uis6ovn881c[9] = " modules";
const uint8_t gm_s6vmoui2ivq06[10] = "(int64_t)";
const uint8_t gm_s6vstugpp3ivo[58] = "`; its columns, `a.f`, are arrays and slices with methods";
const uint8_t gm_s71u85v3jrj86[63] = "this name has no declaration in the source that can be renamed";
const uint8_t gm_s72phirci8af3[25] = "__builtin_convertvector(";
const uint8_t gm_s759q7smc97t0[75] = "`else e =>` needs a condition that is a single binding on a result (\302\2478.2)";
const uint8_t gm_s7628qergfok6[11] = ".gitignore";
const uint8_t gm_s7f8bb20dn4gg[21] = "cannot use the path ";
const uint8_t gm_s7kg16l1bme6s[11] = "this array";
const uint8_t gm_s7mhpbh399e5s[4] = "^fn";
const uint8_t gm_s7mmjn8cgbhv8[67] = "` modifies its receiver, which must be a variable, not a temporary";
const uint8_t gm_s7pgps43h1eg7[41] = "internal error: `resume` without a frame";
const uint8_t gm_s7rk1k9jrjscv[81] = "expected `{` after the condition, found `=`; to compare values, use `is` (\302\2477.6)";
const uint8_t gm_s7rltjaeh634d[11] = "AllocCheck";
const uint8_t gm_s7u0p3hthk80r[9] = "abs_diff";
const uint8_t gm_s81dvppjuktle[9] = "gm_v{}{}";
const uint8_t gm_s84bra4h6kp3s[57] = "(intptr_t)((uintptr_t){} - (uintptr_t){}) / (intptr_t){}";
const uint8_t gm_s8av0350tfnk3[20] = "#pragma pop_macro(\"";
const uint8_t gm_s8hv5sfsqarrt[25] = "    return memcmp(a, b, ";
const uint8_t gm_s8kfjp2od2jrl[51] = "attribute `#{}` does not apply to a foreign member";
const uint8_t gm_s8kr43nig4sns[8] = " *gm_Fp";
const uint8_t gm_s8m5q4i8uoe5j[6] = "match";
const uint8_t gm_s8m8lg034q48k[23] = "compiler/parse/lits.gm";
const uint8_t gm_s8qpjmalo4drt[13] = " argument(s)";
const uint8_t gm_s8ro53eq54s60[44] = "`#thread_local` applies only to a `#global`";
const uint8_t gm_s8rv63m5o5imh[9] = "{}\000{}\000{}";
const uint8_t gm_s8vr59fbqtsbn[10] = "max_value";
const uint8_t gm_s93ft79mfnlvp[10] = " failed, ";
const uint8_t gm_s94a48vj74t8b[74] = "the exponent is larger than {}, the limit of this implementation (\302\24723.5)";
const uint8_t gm_s965n8o245s16[10] = "--target=";
const uint8_t gm_s99ea9i63mcla[52] = "lex with the layout recovery tools use after errors";
const uint8_t gm_s9ahj85e3uamt[12] = "pow_checked";
const uint8_t gm_s9bggu8oepdvs[19] = ": warning: public ";
const uint8_t gm_s9dq8mbgn8du3[35] = "const uint8_t *ev{}; size_t evn{};";
const uint8_t gm_s9eqc3kak4tak[23] = "the C compiler `{}` {}";
const uint8_t gm_s9hi985r6f61m[3] = ">>";
const uint8_t gm_s9mmepka9n2j2[10] = "`, found ";
const uint8_t gm_s9r71kmnlqpv8[17] = "` needs exactly ";
const uint8_t gm_s9sfk4cm0i8u4[8] = "aborted";
const uint8_t gm_sa05vipu6bbea[22] = "memset({}, 0xAA, {});";
const uint8_t gm_sa291memmphhk[15] = "SourceLocation";
const uint8_t gm_sa57j0d5pe8oa[51] = "a parameter of a function that its body never uses";
const uint8_t gm_sa5jkah09l2ke[20] = "write it by name: `";
const uint8_t gm_sa5kad76tohle[3] = "])";
const uint8_t gm_sa7refaf4ue56[45] = "dependency `{}` already exists in gamma.toml";
const uint8_t gm_saamkuuf6jrtf[8] = "fs_read";
const uint8_t gm_saco8uvrm6fd3[4] = " a[";
const uint8_t gm_saf3o40admn10[25] = "after the operation name";
const uint8_t gm_sai6qda00m87d[49] = "    uint8_t s = v[{}] != 0; h = gm_mix64(h, s);\012";
const uint8_t gm_sal52ei1j638k[17] = "((uint8_t *)a{})";
const uint8_t gm_saqb7i299utsd[18] = "an effect literal";
const uint8_t gm_sars0lfrdmiq5[26] = "bad Content-Length header";
const uint8_t gm_sb20a9eb6tt9l[50] = "    if (ta == ({}){}) return {}(a + {}, b + {});\012";
const uint8_t gm_sb658vbovt6jg[37] = "gm_trap(\"reached `@unreachable()`\", ";
const uint8_t gm_sbdlpmcdardns[7] = "</tr>\012";
const uint8_t gm_sbfftl64c12sl[13] = "gm_rand_fill";
const uint8_t gm_sbicj451tu4qm[4] = "\012{}";
const uint8_t gm_sbjdqrlfpuoqj[4] = "c++";
const uint8_t gm_sbjiv038rtlcj[4] = "<<=";
const uint8_t gm_sbpf3hgo18oq5[52] = "`, which the function does not declare (add `needs ";
const uint8_t gm_sbqbp3u228b97[30] = "` needs a pointer `^T`, not `";
const uint8_t gm_sbr6raqk787l2[10] = "enumerate";
const uint8_t gm_sbse0oko82bp9[42] = "`#align` takes one argument: `#align(16)`";
const uint8_t gm_sbsurq5b3g8na[22] = "`, but the value is `";
const uint8_t gm_sbtnbgj3h592u[19] = "` is handled twice";
const uint8_t gm_sbvmilijbiuka[26] = "the foreign variable `{}`";
const uint8_t gm_sbvo1o52epaqq[4] = "LF:";
const uint8_t gm_sc003f3fvnij4[17] = "integer constant";
const uint8_t gm_sc03pp1oj3q4k[7] = "FsRead";
const uint8_t gm_sc2dqfde3kb47[10] = "- `{}`: \012";
const uint8_t gm_sc37tjaf38s71[9] = "--format";
const uint8_t gm_sc5p6b8d5c87a[13] = "GAMMA_RUNNER";
const uint8_t gm_scj2bnbp1mk6t[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\251\001\000\000\015\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_scj5qlamj2bll[39] = "` is never assigned: declare it with `";
const uint8_t gm_scl69k9ct9m19[16] = "tag_offset = {}";
const uint8_t gm_scldd26k6fur2[38] = "does not target the architecture of `";
const uint8_t gm_scqrlin5l437v[9] = "lanes_ge";
const uint8_t gm_scr60nbumsj2h[25] = "`: import it and write `";
const uint8_t gm_scrq1ngg50bjh[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\375\006\000\000\033\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000";
const uint8_t gm_scvb80fgq305s[11] = ", inputs: ";
const uint8_t gm_sd1smnnii4cig[7] = "sha256";
const uint8_t gm_sd25v201ev3d4[30] = "unexpected character U+{:04X}";
const uint8_t gm_sd3a17sd7gp5u[9] = "perform ";
const uint8_t gm_sde2pekt9oc5v[4] = "+|=";
const uint8_t gm_sdi58fds59v3t[30] = " satisfies the requirements: ";
const uint8_t gm_sdj9p83jifn86[41] = "a `bool` compared with `true` or `false`";
const uint8_t gm_sdjp1iiokf034[25] = "compiler/check/spread.gm";
const uint8_t gm_sdko2shp3619i[4] = "are";
const uint8_t gm_sdpig9sk6ngdh[7] = "`with`";
const uint8_t gm_sdq31tldc1ff5[20] = "payload_offset = {}";
const uint8_t gm_sduaucpbcr6v5[69] = "`try` needs the function to return an optional or a result, not `{}`";
const uint8_t gm_sdvlm6i5s2j37[23] = "    GM_FAST_MATH_BODY\012";
const uint8_t gm_se01ng9cs6jft[7] = "lir_of";
const uint8_t gm_se24c7hpp401n[11] = "gamma.toml";
const uint8_t gm_se61bv3cn2p2r[18] = " type argument(s)";
const uint8_t gm_se62v1mjbig2r[21] = "the operands of `{}`";
const uint8_t gm_secrjke11f3ph[13] = "GM_NOINLINE ";
const uint8_t gm_sed3pofb5bu9b[26] = "the {} `{}` is never used";
const uint8_t gm_sedq394prkqe4[10] = "`a[i].{}`";
const uint8_t gm_sefndhhn10fqn[15] = "block_expected";
const uint8_t gm_seg4idqqmijmd[34] = "`clamp` of floats at compile time";
const uint8_t gm_sehf1ajl5jbrm[6] = ", 1, ";
const uint8_t gm_sekk5er5cide1[12] = "gm_spec_exp";
const uint8_t gm_sekndapa5m95n[12] = "- `{}`: {}\012";
const uint8_t gm_sellfthspl9li[9] = "%{} = {}";
const uint8_t gm_sep8ve6ec8tv7[24] = "` by value to or from C";
const uint8_t gm_sf24jo61ba8vn[23] = "a `#soa []const` slice";
const uint8_t gm_sf35nlocjg6at[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\030\001\000\000\216\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_sf4cn9nvrjhp1[15] = "union #c_union";
const uint8_t gm_sf5brl3u7mr5h[7] = "store ";
const uint8_t gm_sf6i3pjks8s6u[21] = " is not a plain name";
const uint8_t gm_sfch9mo7lqp8l[3] = "Or";
const uint8_t gm_sfif278eeflhm[4] = "[^]";
const uint8_t gm_sfm0hseeqersg[25] = "    h = gm_mix64(h, n);\012";
const uint8_t gm_sfnph9ql8pqb1[21] = "converting from `{}`";
const uint8_t gm_sfo6sal7rn2g3[12] = "gm_pipe_err";
const uint8_t gm_sfpnp190uvmtl[59] = ");\012    if (!pn) gm_trap(\"no handler installed for effect `";
const uint8_t gm_sfq6b6v45hv1v[64] = "<!doctype html>\012<html lang=\"en\">\012<head>\012<meta charset=\"utf-8\">\012";
const uint8_t gm_sfsrjkeonr0ad[51] = " is not written by any placeholder of the template";
const uint8_t gm_sfv26hkjngt1i[6] = "Plain";
const uint8_t gm_sg0jqm2pij3el[78] = "`{}` has a length that is a compile-time parameter; construct it with `fill:`";
const uint8_t gm_sg12s7bns50dg[201] = "this array length is not supported in a type: a length computed from compile-time parameters may use only `usize` parameters, integer constants and the plain operators `+ - * / % << >> & | ~` (\302\24716.5)";
const uint8_t gm_sg3acic2chh26[34] = "for (uint32_t k{} = 0;; k{}++) {{";
const uint8_t gm_sg6iftc6c3tph[47] = "stack overflow: `{}` needs a frame of {} bytes";
const uint8_t gm_sg82mujgottmf[25] = "compiler/check/assign.gm";
const uint8_t gm_sg9f7nkpuurvt[74] = "` applies only to `#bits` structs whose fields are all `bool` or integers";
const uint8_t gm_sga6cf3g037hp[8] = "alignof";
const uint8_t gm_sgeslrkf93f2d[18] = "Permission denied";
const uint8_t gm_sgh4cf979ohjn[11] = "atomic_cas";
const uint8_t gm_sgie3las787c9[31] = "(\342\200\246)` takes exactly one value";
const uint8_t gm_sglgjrjci6t1d[25] = "`{}` has no variant `{}`";
const uint8_t gm_sgspc9im84cfh[25] = "    memcpy({}, {}, {});\012";
const uint8_t gm_sgur19lpnttci[7] = " != 0)";
const uint8_t gm_sgvnpogt67see[6] = "cross";
const uint8_t gm_sh4b8t54h2jna[49] = "`for` iterates over ranges of integers, not of `";
const uint8_t gm_sh5o336tlj1uf[9] = "gm_isize";
const uint8_t gm_sh5qb2l9hubh3[9] = " (\302\2476.6)";
const uint8_t gm_sh7dbt4gdrljc[19] = "cannot read {}: {}";
const uint8_t gm_sheokv0g7eanf[50] = "internal error: a test of a place of unknown type";
const uint8_t gm_shhlfsve4gvoq[72] = "`{}` cannot be inlined at every call site: it can call itself (\302\24711.10)";
const uint8_t gm_shlds5pd69pfn[16] = " is not a label";
const uint8_t gm_shmmkvp83pr05[18] = "    return true;\012";
const uint8_t gm_shn4b04riimu9[14] = "a math vector";
const uint8_t gm_shp2btrg6e14g[29] = "` has no `//!` documentation";
const uint8_t gm_shv4avtqvfr4p[6] = "needs";
const uint8_t gm_si2ks4lbjbojr[22] = " ((uint8_t *)&errno)\012";
const uint8_t gm_si416c0mdacrd[5] = " = &";
const uint8_t gm_si44h5pbes4bk[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000#\001\000\000D\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_sic6qqim15h1p[15] = "on_host_thread";
const uint8_t gm_sil0do0eri7f7[44] = "C string literals may not contain NUL bytes";
const uint8_t gm_silop0tio8u7e[105] = "GM_ST({}, (uint8_t *)&{}, ({})(((unsigned __int128)GM_LD({}, {}) & ~({} << {})) | ((({}) & {}) << {})));";
const uint8_t gm_sio6bogdhis0p[2] = "\\";
const uint8_t gm_siqaapb0mqsrg[14] = "proc_close_in";
const uint8_t gm_sitfp4vhvj1ba[72] = "`v.shuffle(w, \342\200\246)` takes a `w` of the same type as `v`, `{}`; not `{}`";
const uint8_t gm_situapcg7atp6[3] = " @";
const uint8_t gm_sj6ih18t5qqaf[36] = "static __attribute__((aligned(4))) ";
const uint8_t gm_sj7qnjnqu5d9l[5] = "case";
const uint8_t gm_sj7sbm8e8u97n[5] = "lib-";
const uint8_t gm_sjcm18p35o9rq[4] = ", &";
const uint8_t gm_sjeopa747clpb[4] = " {}";
const uint8_t gm_sjeqed5d3f5pb[26] = "this `try` always returns";
const uint8_t gm_sjf6sle93kf3b[18] = " = gm_fiber_new(&";
const uint8_t gm_sjfdnh735o7f8[5] = " = (";
const uint8_t gm_sjh6cl9hs66k6[37] = "(void);\012static inline uint8_t *gm_ga";
const uint8_t gm_sjhi85r5ki9g5[23] = "check range {}..{}{}{}";
const uint8_t gm_sjls1ivhf3eiu[12] = "gm_shortest";
const uint8_t gm_sjm4g8bt7kk0q[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\0005\001\000\000\025\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_sjsokj7ll1qcv[76] = "types cannot be stored in variables; use `::` at top level for a type alias";
const uint8_t gm_sk0g3po7mv29v[12] = "file_sha256";
const uint8_t gm_sk0kqh2pa96ut[73] = "`#soa` cannot store `{}`: its field `len` would hide the length, `a.len`";
const uint8_t gm_sk0r84vdcvmnk[13] = "); if (f) { ";
const uint8_t gm_sk175hsmu7b99[22] = "    switch (which) {\012";
const uint8_t gm_sk2oomiuvp44g[6] = ") \077 (";
const uint8_t gm_sk2sae8qmnmbb[31] = "`::`, `:=` or `:` in a binding";
const uint8_t gm_sk30h53vgth78[23] = "((gm_closure){(void *)";
const uint8_t gm_sk6889kd59up6[60] = "` cannot precede an array type; only `#simd` and `#vec` can";
const uint8_t gm_sk89mcfpevgf2[16] = "GAMMA_CACHE_DIR";
const uint8_t gm_sk8j8qotqngd0[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\307\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_skfniirr0r2gc[63] = "compile-time parameters are not supported in function literals";
const uint8_t gm_skhcthij717g7[11] = "--sysroot=";
const uint8_t gm_skm48m2qkkghv[116] = "gamma: the program exports no function (`#export(\"symbol\")`, \302\24719.4), so its object file would have no entry point\012";
const uint8_t gm_skolmnfc0ccbd[2] = "-";
const uint8_t gm_skpl66e1qvhab[8] = "--retry";
const uint8_t gm_skr3v6bd9t3ir[59] = "a slice bound must have an unsigned integer type, not `{}`";
const uint8_t gm_skr5vsm3evt1r[36] = "`return` needs a value of type `{}`";
const uint8_t gm_skr89ndurop26[113] = "if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == {}) {{ gm_unw.kind = GM_NONE; goto {}; }}";
const uint8_t gm_skrj4540qohca[37] = "`package` must be an array of tables";
const uint8_t gm_sktf16uersaon[9] = "typedef ";
const uint8_t gm_sl0n65k3lfps6[18] = "` is not a struct";
const uint8_t gm_sl1botv4tp1fk[58] = "`{}` has type `{}` here but `{}` in the first alternative";
const uint8_t gm_sl3acq1nje8kk[39] = "`\077` unwraps an optional, but this is `";
const uint8_t gm_sl9dbf0dicmme[39] = "no other module of its package uses it";
const uint8_t gm_slcnede7c2i4t[34] = "`: `rev` must be a full commit id";
const uint8_t gm_sle6a93dn88l8[26] = "`{}` is not an identifier";
const uint8_t gm_slg11fhjrk2f5[9] = "gm_ld_v_";
const uint8_t gm_slhaa12fa8am5[24] = "`package` in `pub(\342\200\246)`";
const uint8_t gm_sljj5lb6g47i1[38] = "`{}` does not embed `{}` with `using`";
const uint8_t gm_slk4m2cppcp75[23] = "` is an operation of `";
const uint8_t gm_sllk0j4if03tr[11] = "gm_f_{}_{}";
const uint8_t gm_slm25hfg2udo4[8] = ") != 0)";
const uint8_t gm_slq58vfha8k9h[2] = "f";
const uint8_t gm_slstf59uevcv9[9] = "  note: ";
const uint8_t gm_slvvpj912a5bg[4] = ")0)";
const uint8_t gm_sm1a6faj4q6vl[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\216\000\000\000\021\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000";
const uint8_t gm_sm5752b5mit4p[13] = "resume {} {}";
const uint8_t gm_sm6ioruobq064[6] = "gm_ty";
const uint8_t gm_sm7emfdvcb7c2[3] = "/*";
const uint8_t gm_sm7odqe0d6lgu[23] = "gm_errdefer_runs(&F.h)";
const uint8_t gm_smaq5hrff6a5d[56] = "` is not supported yet; a conversion takes one argument";
const uint8_t gm_smb1apd5lk8bd[11] = "gm_fs_sync";
const uint8_t gm_smdb1h8uhneg6[50] = "#define GM_RT_SEPARATE 1\012#define GM_RT_PROGRAM 1\012";
const uint8_t gm_smdscldu1a8hg[42] = "the element and the tuple members of `in`";
const uint8_t gm_smdv045psk1u8[25] = "    gm_node *cx = NULL;\012";
const uint8_t gm_smi31ifpstnmf[9] = "keyword ";
const uint8_t gm_smjth8dldg6d0[59] = "value {} is not written by any placeholder of the template";
const uint8_t gm_smnk1cmb990al[7] = " *ptr;";
const uint8_t gm_smom6v4b4s28c[52] = "a spread passes a template's values only on its own";
const uint8_t gm_smpl91sh0k8v4[9] = "AllocTag";
const uint8_t gm_smuluqdaf5qoe[9] = "gm_debug";
const uint8_t gm_smv2mfn1ji3ad[58] = "an optional has no error value to capture; use `x else d`";
const uint8_t gm_sn0kh110541oo[23] = "`: use `git` or `none`";
const uint8_t gm_sn1j4m1p1cnm1[83] = "a function literal that uses local variables cannot be a function pointer (\302\24711.7)";
const uint8_t gm_sn3ltks10rrv7[8] = "FreeBSD";
const uint8_t gm_sn46ieaokgrck[10] = "new_table";
const uint8_t gm_sn674iiiekfov[74] = "`subdir` of dependency `{}` must be a relative path inside the repository";
const uint8_t gm_sn8c4o9a7lthb[28] = "stack overflow: a local of ";
const uint8_t gm_sn916351kmsu6[36] = "this placeholder needs {}, not `{}`";
const uint8_t gm_sn9b7u92b8vpk[15] = "GM_LD({}, &{})";
const uint8_t gm_sna4vk4o059d0[10] = "<default>";
const uint8_t gm_sncgl1t0uiqr4[17] = "StaticAssert({})";
const uint8_t gm_sneh1858d1bko[20] = "` (\302\2476.5); `.trunc(";
const uint8_t gm_snge4b9tsq8qc[8] = "bench \"";
const uint8_t gm_sngj7pcs9ksq7[8] = "thinlto";
const uint8_t gm_snkldl7hr4htt[4] = " * ";
const uint8_t gm_snl2n9aactg8i[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\355\001\000\000!\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000";
const uint8_t gm_snpp7pgdikfns[10] = "return {}";
const uint8_t gm_snrndjerbtt58[6] = "if : ";
const uint8_t gm_sns1hl83hdlcn[5] = "if (";
const uint8_t gm_snv7mc8ntub24[48] = "a boolean pattern needs a `bool`, but this is `";
const uint8_t gm_so1ub93q801tl[65] = "the Gamma language server stopped after too many internal errors";
const uint8_t gm_so2okcd9d9cgi[53] = "(field: value)` names exactly one field of the union";
const uint8_t gm_so4g53vn8c2d4[8] = "int64_t";
const uint8_t gm_so4p5vea9o316[10] = "gm_w{}_{}";
const uint8_t gm_so70siafe1e7a[13] = " bits, but `";
const uint8_t gm_so8oee2j1e9l0[6] = "\"))) ";
const uint8_t gm_so99jjk23jgg8[5] = "{}.o";
const uint8_t gm_so9c50giqp3gn[6] = "ppc64";
const uint8_t gm_so9ldq1i41cj2[19] = "unnecessary-parens";
const uint8_t gm_soajcqnnsnidh[57] = "checksum mismatch for package {} (expected {}, found {})";
const uint8_t gm_sojpmv6gl0jac[12] = "pat-bind {}";
const uint8_t gm_sopbumhmsvcgf[10] = "binary {}";
const uint8_t gm_soso4l0tj1pgt[12] = "refs/heads/";
const uint8_t gm_sp2cos6rmlsbv[51] = "__atomic_thread_fence(gm_ord[GM_LD(uint8_t, {})]);";
const uint8_t gm_sp66nadh171v3[19] = "an untyped integer";
const uint8_t gm_sp920qd66qq8t[20] = "no values are given";
const uint8_t gm_sp9ailodohkkj[40] = "static const uint32_t {}_i[] = {{{}}};\012";
const uint8_t gm_spec9er6f839v[17] = "the root package";
const uint8_t gm_spgd6qbgum6c8[25] = ")code)(env, cx, NULL, a)";
const uint8_t gm_spicth2mans91[44] = "a boolean constant cannot be converted to `";
const uint8_t gm_spmkn6krbjnjh[10] = " > 0 || (";
const uint8_t gm_spnnqfjs7id3o[13] = "{}->fmt[k{}]";
const uint8_t gm_spod3tmkh25up[2] = ";";
const uint8_t gm_spoh8ft9n8oqt[4] = "Mul";
const uint8_t gm_spp2faaelnnt0[8] = "memcpy(";
const uint8_t gm_spp2t9pj35hcq[7] = " else ";
const uint8_t gm_spvk46905osdv[32] = "the C compiler failed on {}:\012{}";
const uint8_t gm_sq3gi0chb3cs2[3] = "id";
const uint8_t gm_sq43o0crfp05h[18] = "this pattern has ";
const uint8_t gm_sq4a10jk2jerj[51] = "{} names cannot destructure a tuple of {} elements";
const uint8_t gm_sq53ajg2dhrbb[49] = "` is visible only in the package of the module `";
const uint8_t gm_sq58pm227uuk8[53] = "`never` has no values, so `@{}` does not apply to it";
const uint8_t gm_sq964a86o46ph[17] = "gm_sink_unsigned";
const uint8_t gm_sqgbsg6a0o87i[32] = "`{}` requires integer constants";
const uint8_t gm_sqi8fcv4bi7s6[10] = "element `";
const uint8_t gm_sqjh8kseu4lti[6] = "rev `";
const uint8_t gm_sqk4m1hc1n4b5[49] = "`@compile_error` takes a constant string message";
const uint8_t gm_sqmf3k0nnjf4r[4] = "_{}";
const uint8_t gm_sqmpncmbmfe90[8] = "{}.code";
const uint8_t gm_sqn4pcenn3reh[63] = "vectors are not ordered; compare lanes with `.{}(w)` (\302\2475.6.2)";
const uint8_t gm_sqp133sovb148[25] = "compiler/repl/session.gm";
const uint8_t gm_sqrd7hn8ek7nr[11] = " synthetic";
const uint8_t gm_sqrfl54mgc34i[8] = "capture";
const uint8_t gm_sqrh6i8djv6b4[7] = ", b + ";
const uint8_t gm_sqrv416075ito[3] = "Lt";
const uint8_t gm_sqs6r7f5gl2vu[26] = "static assertion failed: ";
const uint8_t gm_squr90ahaf9ht[13] = "intrinsic `@";
const uint8_t gm_sqvgouubrgndv[14] = "gm_tmpl_piece";
const uint8_t gm_sr29ed60si4re[7] = " fiber";
const uint8_t gm_sr2df1mc0vbea[4] = "-O2";
const uint8_t gm_sr5nrafa4mlm6[19] = "{} :: import \"{}\"\012";
const uint8_t gm_sr64vq6s9ci7q[35] = "`m.select(a, b)` takes vectors of ";
const uint8_t gm_sr7qn5gtrg5md[114] = "; only a real constant is added to a complex number (\302\2477.4.7): write a variable `x` as `Complex(T)(re: x, im: 0)`";
const uint8_t gm_sr8du5b2u9c7v[33] = "discard the value with `_ = \342\200\246`";
const uint8_t gm_sr8dujibvqj2g[82] = "`, which points outside the package; links must stay inside the package directory";
const uint8_t gm_srblss12j199i[25] = "\000\000\000\000\000\000\000\000\001\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000";
const uint8_t gm_sre5oq1um1gnv[17] = "gm_proc_close_in";
const uint8_t gm_srhs1fn12neea[33] = ": the process cannot be started\012";
const uint8_t gm_srjc1gt18ujm5[29] = "` points into the items of `";
const uint8_t gm_srn86a84qh2td[55] = "a fiber's argument must have a first-class type, not `";
const uint8_t gm_sruees1a7v0hs[47] = "` is already declared in a scope that overlaps";
const uint8_t gm_srv4dcfccuset[3] = ")(";
const uint8_t gm_ss5hh7jobk2ar[5] = "{} B";
const uint8_t gm_ss6qrj79r47rb[70] = "a binding with a pattern that can fail must be a statement of a block";
const uint8_t gm_ssbcl1ouchrlk[39] = " \077 (GM_LD(uintptr_t, (const uint8_t *)";
const uint8_t gm_ssc48vnvhmmnh[9] = "cstr_len";
const uint8_t gm_sshlmlg0kjl8a[19] = "(const void *)({})";
const uint8_t gm_ssigpbgi88it3[4] = "Sub";
const uint8_t gm_ssnu9a3et1ilt[6] = "</em>";
const uint8_t gm_sso11c17kdal7[35] = " name `_`: it is not a usable name";
const uint8_t gm_sso181di0dbb3[10] = "{} %= {};";
const uint8_t gm_sso1h04lhm0cu[32] = "`dependencies` must be an array";
const uint8_t gm_sso47q408qu33[24] = "the same as this `else`";
const uint8_t gm_sspb2rj2e032s[13] = ") == 0 || ((";
const uint8_t gm_st0l9v6pp5i7b[14] = "VolatileStore";
const uint8_t gm_st5bsv3b3beg9[22] = "(((unsigned __int128)";
const uint8_t gm_st6hj8v0h80gt[6] = " of `";
const uint8_t gm_st6jo8d16th86[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000\356\000\000\0005\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000";
const uint8_t gm_st6to3kuud2ge[46] = "const char *file, uint32_t line, uint32_t col";
const uint8_t gm_stbtfovn5dhm4[5] = ") < ";
const uint8_t gm_stc6hgo9kskc3[8] = "{} len;";
const uint8_t gm_stgfndqo1l714[13] = "    {}{}: {}";
const uint8_t gm_stjafkt8gs4e7[13] = "a field name";
const uint8_t gm_stjfbh3ag5r41[52] = "bool {} = false; {} {} = {}({}, (uint64_t){}, &{});";
const uint8_t gm_stmsadvf54k73[3] = "`.";
const uint8_t gm_sto98il7spahj[73] = "`{}` is a compile-time parameter, whose value is not known here (\302\24716.5)";
const uint8_t gm_stol2i9gi9a35[18] = "GM_LD(void *, {})";
const uint8_t gm_stt02csqvk3dh[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\0003\001\000\000I\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000";
const uint8_t gm_stucn95m8hs6h[9] = "facts_of";
const uint8_t gm_su03its4kclfl[10] = "replace `";
const uint8_t gm_su274du674a98[38] = "library names with special characters";
const uint8_t gm_su2c9m69n1vjb[31] = "const uint8_t *ev; size_t evn;";
const uint8_t gm_su3gef13fgq4c[7] = "op_mul";
const uint8_t gm_su6mi500md00b[7] = "std/fs";
const uint8_t gm_su70l0sh894sa[33] = "    gm_with_end(&{}, false, {});";
const uint8_t gm_suapcbuahu4i6[24] = "__atomic_load_n({}, {})";
const uint8_t gm_sug64ltbhov0m[19] = "    return false;\012";
const uint8_t gm_suh21tg0nqmfn[10] = "expected ";
const uint8_t gm_suhmdi1gd6hta[7] = "Stream";
const uint8_t gm_suiq4ou0s86ho[14] = "unknown type ";
const uint8_t gm_sulgkejn2inql[7] = "`a[i].";
const uint8_t gm_suls9doo5jh0i[11] = "snake_case";
const uint8_t gm_sun4qq3op5opf[7] = "PtrSub";
const uint8_t gm_sup81hi2h3911[9] = "newline\012";
const uint8_t gm_suq2iqmhi931e[3] = "30";
const uint8_t gm_sut92jpq3kk88[11] = "reduce_sum";
const uint8_t gm_suvh4o70k4cg3[28] = "{}{}GM_RUN_EXPORT(gm_xt{});";
const uint8_t gm_sv242olhg8pke[13] = "mutated-copy";
const uint8_t gm_sv394jlms97cd[14] = "GM_TEMP_BYTES";
const uint8_t gm_sv3o54ps0ppci[19] = "]; void *gm_lanes[";
const uint8_t gm_sv6o879d2okh4[39] = "`{}` does not apply to complex numbers";
const uint8_t gm_sv7ptc4il3v0u[15] = "typedef struct";
const uint8_t gm_sv89nb1qmpoaj[8] = "{}.wasm";
const uint8_t gm_sv8s0ogr3iflj[8] = "\012  dir ";
const uint8_t gm_svacokl3cteo0[26] = "expected a compile-time `";
const uint8_t gm_svdd37ikql4fk[45] = "bswap requires a whole number of bytes, not ";
const uint8_t gm_svftrp7hvam92[5] = "eof\012";
const uint8_t gm_svhh21g5q0quu[25] = "    if (((uint8_t *)&r)[";
const uint8_t gm_svhvj3h6g939d[66] = "cannot mix `&`, `|` and `~` in one expression without parentheses";
const uint8_t gm_svkvl31jmj9ne[12] = "fs_make_dir";
const uint8_t gm_svpofgjtij7vj[4] = "pow";
const uint8_t gm_svu4pa4qq20q7[6] = "gm_pt";
const uint8_t gm_svua787vprkp6[8] = "modules";
uint8_t gm_c05rjh68v5e[7] __attribute__((aligned(1)));
uint8_t gm_c0fn3on8qv1[10] __attribute__((aligned(1)));
uint8_t gm_c0u6va72tvj[45] __attribute__((aligned(1)));
uint8_t gm_c1i8nujdgu3[48] __attribute__((aligned(1)));
uint8_t gm_c1m3l8o6pqn[4] __attribute__((aligned(1)));
uint8_t gm_c2hth7bcr5i[5] __attribute__((aligned(1)));
uint8_t gm_c51bciei4ea[15] __attribute__((aligned(1)));
uint8_t gm_c695aum3fk1[16] __attribute__((aligned(8)));
uint8_t gm_c69cf1c2r6c[7] __attribute__((aligned(1)));
uint8_t gm_c6n9n3bf4b3[9] __attribute__((aligned(1)));
uint8_t gm_c71rmf91ugn[8] __attribute__((aligned(1)));
uint8_t gm_c7magu4r51i[12] __attribute__((aligned(1)));
uint8_t gm_c7t254lldbr[13] __attribute__((aligned(1)));
uint8_t gm_c90auv8b7m5[12] __attribute__((aligned(1)));
uint8_t gm_c9fnj4ddpds[13] __attribute__((aligned(1)));
uint8_t gm_c9la4vcjus5[16] __attribute__((aligned(1)));
uint8_t gm_c9ogaceq2mh[14] __attribute__((aligned(1)));
uint8_t gm_cb0tru27utl[12] __attribute__((aligned(1)));
uint8_t gm_ccpiqhf3ha6[9] __attribute__((aligned(1)));
uint8_t gm_ccrs5p8c6u1[12] __attribute__((aligned(1)));
uint8_t gm_cda1ravdhnf[13] __attribute__((aligned(1)));
uint8_t gm_cdeh8704ija[4] __attribute__((aligned(4)));
uint8_t gm_cdg6iib59cd[2] __attribute__((aligned(1)));
uint8_t gm_ce4lfdpck08[9] __attribute__((aligned(1)));
uint8_t gm_cesk7bev32f[15] __attribute__((aligned(1)));
uint8_t gm_cfi8o27oni0[3] __attribute__((aligned(1)));
uint8_t gm_cfj1s1vu5t2[1] __attribute__((aligned(1)));
uint8_t gm_cfmi0tp7ni9[9] __attribute__((aligned(1)));
uint8_t gm_cftc9ncjfm7[16] __attribute__((aligned(8)));
uint8_t gm_cgrtudv7s61[35] __attribute__((aligned(1)));
uint8_t gm_ch8t72j9a3i[11] __attribute__((aligned(1)));
uint8_t gm_chgr5anan0t[10] __attribute__((aligned(1)));
uint8_t gm_chu76sl03kb[13] __attribute__((aligned(1)));
uint8_t gm_ci2pejepm7o[9] __attribute__((aligned(1)));
uint8_t gm_cim7gbhispe[13] __attribute__((aligned(1)));
uint8_t gm_cj4ge4vvlni[5] __attribute__((aligned(1)));
uint8_t gm_cjr3tjj87ef[16] __attribute__((aligned(8)));
uint8_t gm_cjskjibtqb8[15] __attribute__((aligned(1)));
uint8_t gm_cjuh7rdtbga[17] __attribute__((aligned(1)));
uint8_t gm_ckfkap0hmh1[18] __attribute__((aligned(1)));
uint8_t gm_cl8mepvcg0b[11] __attribute__((aligned(1)));
uint8_t gm_cllmi9128mt[4] __attribute__((aligned(1)));
uint8_t gm_cm1s0561676[6] __attribute__((aligned(1)));
uint8_t gm_cm496uu8qhc[19] __attribute__((aligned(1)));
uint8_t gm_cm4pp8tt365[24] __attribute__((aligned(1)));
uint8_t gm_cmrmftl6v0f[18] __attribute__((aligned(1)));
uint8_t gm_cn2c0dgg5e8[4] __attribute__((aligned(1)));
uint8_t gm_cna5t6g5c6f[7] __attribute__((aligned(1)));
uint8_t gm_co0amvig9ac[7] __attribute__((aligned(1)));
uint8_t gm_coka7750chc[9] __attribute__((aligned(1)));
uint8_t gm_cpb4kn5t0rf[2] __attribute__((aligned(1)));
uint8_t gm_cqc8bus8k1v[7] __attribute__((aligned(1)));
uint8_t gm_cqma2eq6ij1[80] __attribute__((aligned(1)));
uint8_t gm_cqtn1r3tf68[2] __attribute__((aligned(1)));
uint8_t gm_cr8ckpnai0f[13] __attribute__((aligned(1)));
uint8_t gm_cre7u6dhl25[15] __attribute__((aligned(1)));
uint8_t gm_crj6tg85j72[18] __attribute__((aligned(1)));
uint8_t gm_crth1fagslo[10] __attribute__((aligned(1)));
uint8_t gm_cs8kn6g5l0h[10] __attribute__((aligned(1)));
uint8_t gm_cs9nni7g1o0[13] __attribute__((aligned(1)));
uint8_t gm_cseqn4no5c3[14] __attribute__((aligned(1)));
uint8_t gm_ct6ds3l9j7c[4] __attribute__((aligned(1)));
uint8_t gm_ctb2vnk0tul[2] __attribute__((aligned(1)));
uint8_t gm_ctfbu78unbf[101] __attribute__((aligned(1)));
uint8_t gm_ctloho1a5a0[53] __attribute__((aligned(1)));
uint8_t gm_cto913mafo2[8] __attribute__((aligned(8)));
uint8_t gm_ctuh6kms0vm[13] __attribute__((aligned(1)));
uint8_t gm_cuptbi6uclh[15] __attribute__((aligned(1)));
uint8_t gm_cusfosftpfn[4] __attribute__((aligned(4)));
uint8_t gm_cv98ob7u5a4[16] __attribute__((aligned(8)));
uint8_t gm_cvlea0kb4ed[11] __attribute__((aligned(1)));
uint8_t gm_cvq082bup6p[11] __attribute__((aligned(1)));
uint8_t gm_k027ibn62fv[40] __attribute__((aligned(8)));
uint8_t gm_k0css3bab46[40] __attribute__((aligned(8)));
uint8_t gm_k231525i1sp[40] __attribute__((aligned(8)));
uint8_t gm_k52ekgkvp6f[40] __attribute__((aligned(8)));
uint8_t gm_k6kfar4qbkb[40] __attribute__((aligned(8)));
uint8_t gm_k7ik3fa1b9i[40] __attribute__((aligned(8)));
uint8_t gm_k8f4m32t0m4[40] __attribute__((aligned(8)));
uint8_t gm_ka4kfapougb[40] __attribute__((aligned(8)));
uint8_t gm_kbnuij4naoh[40] __attribute__((aligned(8)));
uint8_t gm_ke1o9nchpfo[40] __attribute__((aligned(8)));
uint8_t gm_kg8c0kikv62[40] __attribute__((aligned(8)));
uint8_t gm_kgh95sj78n8[40] __attribute__((aligned(8)));
uint8_t gm_kh832np8pgs[40] __attribute__((aligned(8)));
uint8_t gm_khp004r0f25[40] __attribute__((aligned(8)));
uint8_t gm_kismru0mlt2[40] __attribute__((aligned(8)));
uint8_t gm_kkei5iuecp0[40] __attribute__((aligned(8)));
uint8_t gm_kmpad6qi84v[40] __attribute__((aligned(8)));
uint8_t gm_knjegli7s8j[40] __attribute__((aligned(8)));
uint8_t gm_kpmakm2t9q3[40] __attribute__((aligned(8)));
uint8_t gm_kr6fmufdvee[40] __attribute__((aligned(8)));
uint8_t gm_kreaco1pgk3[40] __attribute__((aligned(8)));
uint8_t gm_ksksp29ghso[40] __attribute__((aligned(8)));
uint8_t gm_ksp6bldq59o[40] __attribute__((aligned(8)));
uint8_t gm_kv3bmftl0ar[40] __attribute__((aligned(8)));
uint8_t gm_kvp9m9fg1c2[40] __attribute__((aligned(8)));
void *const gm_hd3g3ujqvah4[] = {(void *)gm_dt3g3ujqvah4_0};
void *const gm_hldg2pcvebta[] = {(void *)gm_f_for_body_8jjs01b};
void *const gm_hlifma0pdvu5[] = {(void *)gm_f_for_body_ig27ig2};
